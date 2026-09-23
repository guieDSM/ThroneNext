#include <csignal>
#include <memory>

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>
#include <QTranslator>
#include <QMessageBox>
#include <QStandardPaths>
#include <QLocalSocket>
#include <QLocalServer>
#include <QThread>
#include <QDateTime>
#include <QTimer>
#include <3rdparty/WinCommander.hpp>


#include "include/global/Configs.hpp"
#include "include/global/Logger.hpp"
#include "include/global/SafeCanaryPolicy.hpp"

#include "include/ui/mainwindow_interface.h"
#include "include/stats/traffic/TrafficStatsManager.hpp"
#include "include/api/RPC.h"
#include "include/configs/generate.h"
#include "include/database/ProfilesRepo.h"

#ifdef Q_OS_WIN
#include "include/sys/windows/MiniDump.h"
#include "include/sys/windows/eventHandler.h"
#include "include/sys/windows/WinVersion.h"
#include <qfontdatabase.h>
#include <windows.h>
#endif
#ifdef Q_OS_LINUX
#include <include/sys/linux/coreDump.h>
#include <qfontdatabase.h>
#include <QSocketNotifier>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#endif
#ifdef Q_OS_MACOS
#include <QFileOpenEvent>

// On macOS the OS reuses the running app and delivers throne:// URLs, as well as
// files opened with the app, as a QFileOpenEvent to the application object (never
// via argv). This filter feeds both into the common pipelines.
class MacOpenEventFilter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::FileOpen) {
            const auto openEvent = static_cast<QFileOpenEvent *>(event);
            const QString url = openEvent->url().toString();
            if (url.startsWith("throne://")) {
                Deeplink_Submit(url);
                return true;
            }
            const QString file = openEvent->file().isEmpty() ? openEvent->url().toLocalFile() : openEvent->file();
            if (!file.isEmpty()) {
                LaunchFiles_Submit({file});
                return true;
            }
        }
        return QObject::eventFilter(obj, event);
    }
};
#endif

void signal_handler(int signum) {
    Q_UNUSED(signum)
    if (auto *mw = GetMainWindow()) mw->prepare_exit();
    qApp->quit();
}

#ifdef Q_OS_LINUX
namespace {
    int g_signalPipe[2] = {-1, -1};

    // Async-signal-safe: a write() to the self-pipe is all that is allowed here. The
    // teardown itself (Qt widgets, QProcess, SQLite) runs from the notifier below, on
    // the main thread, so a session-manager SIGTERM can no longer be delivered on a
    // worker thread or re-enter a lock the interrupted thread was already holding.
    void posix_signal_handler(int signum) {
        const auto byte = static_cast<char>(signum);
        [[maybe_unused]] const ssize_t written = ::write(g_signalPipe[1], &byte, 1);
    }

    void install_termination_handlers() {
        if (::pipe(g_signalPipe) != 0) {
            // Without the pipe, the unsafe direct handler still beats no handler at all.
            signal(SIGTERM, signal_handler);
            signal(SIGINT, signal_handler);
            return;
        }
        for (const int fd : g_signalPipe) {
            ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
            // Non-blocking: a full pipe must fail the write, never block in signal context.
            ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
        }

        auto *notifier = new QSocketNotifier(g_signalPipe[0], QSocketNotifier::Read, qApp);
        QObject::connect(notifier, &QSocketNotifier::activated, qApp, [notifier] {
            notifier->setEnabled(false); // one teardown is enough; later signals just fill the pipe
            char drain[16];
            while (::read(g_signalPipe[0], drain, sizeof(drain)) > 0) {}
            signal_handler(0);
        });

        struct sigaction sa{};
        sa.sa_handler = posix_signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
    }
}
#endif

QTranslator* trans = nullptr;
QTranslator* trans_qt = nullptr;

void loadTranslate(const QString& locale) {
    QT_TRANSLATE_NOOP("QPlatformTheme", "Cancel");
    QT_TRANSLATE_NOOP("QPlatformTheme", "Apply");
    QT_TRANSLATE_NOOP("QPlatformTheme", "Yes");
    QT_TRANSLATE_NOOP("QPlatformTheme", "No");
    QT_TRANSLATE_NOOP("QPlatformTheme", "OK");
    if (trans != nullptr) {
        trans->deleteLater();
    }
    if (trans_qt != nullptr) {
        trans_qt->deleteLater();
    }
    trans = new QTranslator;
    trans_qt = new QTranslator;
    QLocale::setDefault(QLocale(locale));
    //
    const QString diskPath = QCoreApplication::applicationDirPath()+"/translations/" + locale + ".qm";
    const QString qrcPath = ":/translations/" + locale + ".qm";
    bool loadOK=false;
    if (QFileInfo::exists(diskPath)) {
        loadOK = trans->load(diskPath);
    }
    if (!loadOK) {
        loadOK = trans->load(qrcPath);
    }
    if (loadOK) {
        QCoreApplication::installTranslator(trans);
    }
}

namespace {
    constexpr auto FALLBACK_MARKER = "config/.install-dir-unwritable";
    constexpr auto SAFE_CANARY_MARKER = ".throne-safe-canary-v1";
    constexpr auto SAFE_CANARY_MARKER_CONTENT = "THRONE_SAFE_CANARY_V1\n";

    struct SafeCanaryPreparation {
        bool ok = false;
        QString error;
    };

    bool IsReparsePoint(const QString& path) {
#ifdef Q_OS_WIN
        const auto nativePath = QDir::toNativeSeparators(path);
        const auto attributes = GetFileAttributesW(
            reinterpret_cast<LPCWSTR>(nativePath.utf16()));
        return attributes != INVALID_FILE_ATTRIBUTES
            && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
        return QFileInfo(path).isSymLink();
#endif
    }

    bool PathsMatch(const QString& first, const QString& second) {
        const auto left = QDir::cleanPath(QFileInfo(first).absoluteFilePath());
        const auto right = QDir::cleanPath(QFileInfo(second).absoluteFilePath());
#ifdef Q_OS_WIN
        return left.compare(right, Qt::CaseInsensitive) == 0;
#else
        return left == right;
#endif
    }

    bool SafeCanaryMarkerValid(const QString& root) {
        const QFileInfo markerInfo(QDir(root).absoluteFilePath(SAFE_CANARY_MARKER));
        if (!markerInfo.isFile() || markerInfo.isSymLink()) return false;
        QFile marker(markerInfo.absoluteFilePath());
        return marker.open(QIODevice::ReadOnly)
            && marker.readAll() == QByteArray(SAFE_CANARY_MARKER_CONTENT);
    }

    Configs::safe_canary::Input ObserveSafeCanaryRoot(
        const QString& root,
        const QString& applicationDir,
        bool requested,
        bool appdataOptionPresent,
        bool explicitPathPresent) {
        const QFileInfo rootInfo(root);
        const bool exists = rootInfo.exists();
        const QDir rootDir(root);
        const bool markerPresent = QFileInfo::exists(
            rootDir.absoluteFilePath(SAFE_CANARY_MARKER));
        return {
            .requested = requested,
            .appdataOptionPresent = appdataOptionPresent,
            .explicitPathPresent = explicitPathPresent,
            .pathIsAbsolute = QDir::isAbsolutePath(root),
            .pathIsFilesystemRoot = rootDir.isRoot(),
            .pathMatchesApplicationDir = PathsMatch(root, applicationDir),
            .directoryExists = exists,
            .directoryIsReparsePoint = exists && IsReparsePoint(root),
            .directoryIsEmpty = exists
                && rootDir.entryList(
                    QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty(),
            .markerPresent = markerPresent,
            .markerValid = markerPresent && SafeCanaryMarkerValid(root),
        };
    }

    SafeCanaryPreparation PrepareSafeCanaryRoot(
        const QString& root,
        const QString& applicationDir,
        bool appdataOptionPresent) {
        auto input = ObserveSafeCanaryRoot(
            root,
            applicationDir,
            true,
            appdataOptionPresent,
            !root.isEmpty());
        auto result = Configs::safe_canary::Evaluate(input);
        if (!result.allowed()) return {false, QString::fromLatin1(result.reason)};
        if (result.decision == Configs::safe_canary::Decision::ResumeRoot)
            return {true, {}};
        if (result.decision != Configs::safe_canary::Decision::InitializeRoot)
            return {false, "safe canary root was not initialized"};

        if (!QDir().mkpath(root))
            return {false, "safe canary appdata directory could not be created"};
        input = ObserveSafeCanaryRoot(
            root,
            applicationDir,
            true,
            appdataOptionPresent,
            true);
        result = Configs::safe_canary::Evaluate(input);
        if (!result.allowed()
            || result.decision != Configs::safe_canary::Decision::InitializeRoot)
            return {false, QString::fromLatin1(result.reason)};

        QSaveFile marker(QDir(root).absoluteFilePath(SAFE_CANARY_MARKER));
        if (!marker.open(QIODevice::WriteOnly)
            || marker.write(SAFE_CANARY_MARKER_CONTENT)
                != QByteArray(SAFE_CANARY_MARKER_CONTENT).size()
            || !marker.commit())
            return {false, "safe canary marker could not be committed"};

        input = ObserveSafeCanaryRoot(
            root,
            applicationDir,
            true,
            appdataOptionPresent,
            true);
        result = Configs::safe_canary::Evaluate(input);
        if (result.decision != Configs::safe_canary::Decision::ResumeRoot)
            return {false, "safe canary marker verification failed"};
        return {true, {}};
    }

    int SafeCanaryExitDelay(const QStringList& arguments, QString* error) {
        constexpr auto option = "-safe-canary-exit-ms";
        const auto count = arguments.count(option);
        if (count == 0) return 0;
        if (count != 1) {
            *error = "safe canary exit delay may be specified only once";
            return -1;
        }
        const auto index = arguments.indexOf(option);
        bool ok = false;
        const auto value = index + 1 < arguments.size()
            ? arguments.at(index + 1).toInt(&ok)
            : 0;
        if (!ok || value < 250 || value > 30000) {
            *error = "safe canary exit delay must be within 250..30000 ms";
            return -1;
        }
        return value;
    }

    // QFileInfo::isWritable reports the read-only attribute, not what a UAC-filtered
    // token may actually do under Program Files.
    bool DirIsWritable(const QDir &dir) {
        if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) return false;
        QFile probe(dir.absoluteFilePath(".throne-write-test"));
        if (!probe.open(QIODevice::WriteOnly)) return false;
        probe.close();
        probe.remove();
        return true;
    }

    bool ConfigDirIsUsable(const QDir &configDir) {
        if (!DirIsWritable(configDir)) return false;
        const QString db = configDir.absoluteFilePath("throne.db");
        if (!QFile::exists(db)) return true;
        QFile file(db);
        return file.open(QIODevice::ReadWrite);
    }

    void CopyDirContents(const QString &from, const QString &to) {
        QDir().mkpath(to);
        QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
        while (it.hasNext()) {
            it.next();
            const QString target = QDir(to).absoluteFilePath(it.fileName());
            if (it.fileInfo().isDir()) CopyDirContents(it.filePath(), target);
            else if (!QFile::exists(target)) QFile::copy(it.filePath(), target);
        }
    }

    // An elevated relaunch finds the install dir writable again, so the fallback is
    // pinned by a marker or the two runs land on different databases.
    bool AdoptUserConfigDir(const QDir &installWd, const QDir &userWd) {
        QFile marker(userWd.absoluteFilePath(FALLBACK_MARKER));
        if (marker.open(QIODevice::ReadOnly)) {
            const bool pinnedHere = QString::fromUtf8(marker.readAll()).trimmed() == installWd.absolutePath();
            marker.close();
            if (pinnedHere) return true;
        }

        const QString installConfig = installWd.absoluteFilePath("config");
        if (ConfigDirIsUsable(QDir(installConfig))) return false;

        const QString userConfig = userWd.absoluteFilePath("config");
        QDir().mkpath(userConfig);
        if (!QFile::exists(userConfig + "/throne.db") && QFile::exists(installConfig + "/throne.db")) {
            CopyDirContents(installConfig, userConfig);
            LOG_WARN(QString("copied existing config from %1").arg(installConfig));
        }
        if (marker.open(QIODevice::WriteOnly)) {
            marker.write(installWd.absolutePath().toUtf8());
            marker.close();
        }
        LOG_WARN(QString("%1 is not writable, using %2").arg(installConfig, userConfig));
        return true;
    }
} // namespace

#define LOCAL_SERVER_PREFIX "throne-"

int main(int argc, char* argv[]) {
    Logging::InstallQtMessageHandler();

    // Core dump
#ifdef Q_OS_WIN
    Windows_SetCrashHandler();
#endif
#ifdef Q_OS_LINUX
    enable_core_dumps();
#endif

    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication a(argc, argv);

#ifdef Q_OS_MACOS
    // Install before the event loop so launch-by-deeplink FileOpen events are caught.
    a.installEventFilter(new MacOpenEventFilter(&a));
#endif

#if !defined(Q_OS_MACOS) && (QT_VERSION >= QT_VERSION_CHECK(6,9,0))
    // Load the emoji fonts
#ifdef Q_OS_WIN
    int fontId = QFontDatabase::addApplicationFont(WinVersion::IsBuildNumGreaterOrEqual(BuildNumber::Windows_11_22H2) ? ":/font/notoEmoji" : ":/font/Twemoji");
#else
    int fontId = QFontDatabase::addApplicationFont(":/font/notoEmoji");
#endif
    if (fontId >= 0)
    {
        QStringList fontFamilies = QFontDatabase::applicationFontFamilies(fontId);
        QFontDatabase::setApplicationEmojiFontFamilies(fontFamilies);
    } else
    {
        qDebug() << "could not load emoji font!";
    }
#endif

    QStringList arguments = QApplication::arguments();
    const bool safeCanary = arguments.contains("-safe-canary");
    // A throne:// URL may be passed as a launch argument (Windows/Linux), and so may
    // config files opened with the app. Both are delivered after the window is up, or
    // forwarded to the primary instance via the socket below. Files are resolved
    // before the working directory moves, since their paths may be relative to it.
    const QString launchDeeplink = Deeplink_ExtractFromArgs(arguments);
    const QStringList launchFiles = LaunchFiles_ExtractFromArgs(arguments, QDir::current());

    // Clean
    QDir::setCurrent(QApplication::applicationDirPath());
    if (QFile::exists("updater.old")) {
        QFile::remove("updater.old");
    }

    // dirs & clean
    auto wd = QDir(QApplication::applicationDirPath());
    bool useAppdata = false;
    QString appdataDir;
    if (arguments.contains("-appdata")) {
        useAppdata = true;
        int appdataIndex = arguments.indexOf("-appdata");
        if (arguments.size() > appdataIndex + 1 && !arguments.at(appdataIndex + 1).startsWith("-")) {
            appdataDir = arguments.at(appdataIndex + 1);
        }
    }
#ifdef NKR_CPP_USE_APPDATA
    useAppdata = true; // Example: Package & MacOS
#endif
    QApplication::setApplicationName("Throne");
    int safeCanaryExitMs = 0;
    if (safeCanary) {
        QString exitDelayError;
        safeCanaryExitMs = SafeCanaryExitDelay(arguments, &exitDelayError);
        if (safeCanaryExitMs < 0) {
            QMessageBox::critical(nullptr, "Throne safe canary", exitDelayError);
            return 2;
        }
        const auto preparation = PrepareSafeCanaryRoot(
            appdataDir,
            QApplication::applicationDirPath(),
            arguments.count("-appdata") == 1);
        if (!preparation.ok) {
            QMessageBox::critical(
                nullptr, "Throne safe canary", preparation.error);
            return 2;
        }
    } else if (arguments.contains("-safe-canary-exit-ms")) {
        QMessageBox::critical(
            nullptr,
            "Throne safe canary",
            "-safe-canary-exit-ms requires -safe-canary");
        return 2;
    }
    if(useAppdata) {
        if (!appdataDir.isEmpty()) {
            wd.setPath(appdataDir);
        } else {
            wd.setPath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        }
    } else {
        const QDir userWd(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        if (AdoptUserConfigDir(wd, userWd)) {
            wd = userWd;
            useAppdata = true;
        }
    }
    if (!wd.exists()) wd.mkpath(wd.absolutePath());
    if (!wd.exists("config")) wd.mkdir("config");
    const QString configDir = wd.absoluteFilePath("config");
    QDir::setCurrent(configDir);
    QDir("temp").removeRecursively();

    // Record app start for the Runtime Stats uptime readout.
    appStartEpoch = QDateTime::currentSecsSinceEpoch();

    // Load database
    Configs::initDB(QString(QDir::currentPath() + QDir::separator() + "throne.db").toStdString());

#ifdef THRONE_BUILD_AUDIT_TOOLS
    // Developer-only export from a marked disposable copy. No core is started.
    if (arguments.contains("-audit-export-profile")) {
        if (!safeCanary) return 20;
        const auto index = arguments.indexOf("-audit-export-profile");
        bool valid = false;
        const int id = arguments.value(index + 1).toInt(&valid);
        const auto profile = valid ? Configs::dataManager->profilesRepo->GetProfile(id) : nullptr;
        if (!profile) return 21;
        auto &settings = *Configs::dataManager->settingsRepo;
        settings.noSave = true;
        settings.spmode_vpn = false;
        settings.enable_dns_server = false;
        settings.enable_redirect = false;
        settings.inbound_address = "127.0.0.1";
        settings.inbound_socks_port = 11881;
        settings.inbound_auth = false;
        settings.core_dns_in_port = 15534;
        settings.core_box_clash_api = -1;
        const auto result = Configs::BuildSingBoxConfig(profile);
        if (!result->error.isEmpty()) return 22;
        QJsonObject output{{"core", result->coreConfig}, {"xray", result->xrayConfig},
            {"dnsAddress", "127.0.0.1:15534"}, {"dnsStrategy", Configs::getXrayOutboundDomainStrategy()}};
        QSaveFile file(QDir::current().absoluteFilePath("audit-config.json"));
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly)) return 23;
        const auto data = QJsonDocument(output).toJson(QJsonDocument::Compact);
        if (file.write(data) != data.size() || !file.commit()) return 24;
        return 0;
    }
#endif

    Logging::SetLevel(Logging::LevelFromString(Configs::dataManager->settingsRepo->log_file_level));

    // Start traffic-statistics maintenance (startup downsample + background rollup).
    Stats::trafficStatsManager->Init();

    // Store Flags
    Configs::dataManager->settingsRepo->argv = arguments;
    if (Configs::dataManager->settingsRepo->argv.contains("-many")) Configs::dataManager->settingsRepo->flag_many = true;
    if (Configs::dataManager->settingsRepo->argv.contains("-tray")) Configs::dataManager->settingsRepo->flag_tray = true;
    if (Configs::dataManager->settingsRepo->argv.contains("-debug")) Configs::dataManager->settingsRepo->flag_debug = true;
    if (Configs::dataManager->settingsRepo->argv.contains("-flag_restart_tun_on")) Configs::dataManager->settingsRepo->flag_restart_tun_on = true;
    if (Configs::dataManager->settingsRepo->argv.contains("-flag_restart_dns_set")) Configs::dataManager->settingsRepo->flag_dns_set = true;
    Configs::dataManager->settingsRepo->flag_use_appdata = useAppdata;
    Configs::dataManager->settingsRepo->flag_safe_canary = safeCanary;
    if(useAppdata && !appdataDir.isEmpty()) Configs::dataManager->settingsRepo->appdataDir = appdataDir;
#ifdef NKR_CPP_DEBUG
    Configs::dataManager->settingsRepo->flag_debug = true;
#endif

#ifdef Q_OS_LINUX
    QApplication::addLibraryPath(QApplication::applicationDirPath() + "/usr/plugins");
#endif

    // dispatchers
    DS_cores = new QThread;
    DS_cores->start();

    LogThread = new QThread;
    LogThread->start();

// icons
    QIcon::setFallbackSearchPaths(QStringList{
        ":/icon",
    });

    // icon for no theme
    if (QIcon::themeName().isEmpty()) {
        QIcon::setThemeName("breeze");
    }

#ifdef Q_OS_WIN
    if (!safeCanary && Configs::dataManager->settingsRepo->windows_set_admin
        && !Configs::IsAdmin() && !Configs::dataManager->settingsRepo->disable_run_admin)
    {
        Configs::dataManager->settingsRepo->windows_set_admin = false; // so that if permission denied, we will run as user on the next run
        Configs::dataManager->settingsRepo->Save();
        auto elevatedArguments = arguments;
        if (!elevatedArguments.isEmpty()) elevatedArguments.removeFirst();
        WinCommander::runProcessElevated(
            QApplication::applicationFilePath(), elevatedArguments, "", 1, false);
        QApplication::quit();
        return 0;
    }
#endif

    // dataManager->settingsRepo & Flags
    if (Configs::dataManager->settingsRepo->start_minimal) Configs::dataManager->settingsRepo->flag_tray = true;

    // Translate
    QString locale;
    switch (Configs::dataManager->settingsRepo->language) {
        case 1: // English
            break;
        case 2:
            locale = "zh_CN";
            break;
        case 3:
            locale = "fa_IR"; // farsi(iran)
            break;
        case 4:
            locale = "ru_RU"; // Russian
            break;
        default:
            locale = QLocale().name();
    }
    QGuiApplication::tr("QT_LAYOUT_DIRECTION");
    loadTranslate(locale);

    // Check if another instance is running
    QByteArray hashBytes = QCryptographicHash::hash(wd.absolutePath().toUtf8(), QCryptographicHash::Md5).toBase64(QByteArray::OmitTrailingEquals);
    hashBytes.replace('+', '0').replace('/', '1');
    auto serverName = LOCAL_SERVER_PREFIX + QString::fromUtf8(hashBytes);
    qDebug() << "server name: " << serverName;
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(250))
    {
        qDebug() << "Another instance is running, let's wake it up and quit";
        // Hand off whatever we were launched with so the primary instance handles it:
        // one item per line, a throne:// url or a file:// url. Paths go over as urls
        // so that a name containing a newline cannot break the framing.
        QStringList payload;
        if (!safeCanary) {
            if (!launchDeeplink.isEmpty()) payload << launchDeeplink;
            for (const auto &file : launchFiles) payload << QUrl::fromLocalFile(file).toString();
        }
        if (!payload.isEmpty()) {
            socket.write(payload.join('\n').toUtf8());
            socket.flush();
            socket.waitForBytesWritten(250);
        }
        socket.disconnectFromServer();
        return 0;
    }

    // Must follow the single-instance check: opening the log earlier truncates
    // the running instance's file and leaves a marker it would report as a crash.
    Logging::Init(configDir);
    LOG_INFO(QString("appdata mode: %1").arg(useAppdata ? "yes" : "no"));
#ifdef Q_OS_WIN
    Windows_SetCrashDumpPath();
    if (!safeCanary) Windows_ConfigureWER();
#endif

    // QLocalServer
    QLocalServer server(qApp);
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(serverName)) {
        qWarning() << "Failed to start QLocalServer! Error:" << server.errorString();
        Logging::Shutdown();
        return 1;
    }
    QObject::connect(&server, &QLocalServer::newConnection, qApp, [&] {
        auto s = server.nextPendingConnection();
        qDebug() << "Another instance tried to wake us up on " << serverName << s;
        // The waking instance may forward deeplinks and opened files as payload, one
        // url per line. Only whole lines are handled as they arrive; the tail, which
        // carries no trailing newline, is flushed once the peer is done.
        auto pending = std::make_shared<QByteArray>();
        auto handleLine = [safeCanary](const QString &line) {
            if (safeCanary) return;
            if (line.startsWith("throne://")) {
                Deeplink_Submit(line);
            } else if (line.startsWith("file://")) {
                LaunchFiles_Submit({QUrl(line).toLocalFile()});
            }
        };
        auto readPayload = [s, pending, handleLine](bool last) {
            pending->append(s->readAll());
            while (true) {
                const auto at = pending->indexOf('\n');
                if (at < 0) break;
                handleLine(QString::fromUtf8(pending->first(at)).trimmed());
                pending->remove(0, at + 1);
            }
            if (last) {
                handleLine(QString::fromUtf8(*pending).trimmed());
                pending->clear();
            }
        };
        QObject::connect(s, &QLocalSocket::readyRead, s, [readPayload] { readPayload(false); });
        QObject::connect(s, &QLocalSocket::disconnected, s, [readPayload] { readPayload(true); });
        QObject::connect(s, &QLocalSocket::disconnected, s, &QLocalSocket::deleteLater);
        readPayload(false); // in case the payload already arrived
        // raise main window
        MW_dialog_message(MwMessage::Raise, {});
    });
    QObject::connect(qApp, &QApplication::aboutToQuit, [&]
    {
        server.close();
        QLocalServer::removeServer(serverName);
        // Every quit path lands here; missing it is reported as a crash next start.
        Logging::Shutdown();
    });

#ifdef Q_OS_LINUX
    install_termination_handlers();
#endif

#ifdef Q_OS_WIN
    auto eventFilter = new PowerOffTaskkillFilter(signal_handler);
    a.installNativeEventFilter(eventFilter);
#endif

#ifdef Q_OS_MACOS
    QObject::connect(qApp, &QGuiApplication::commitDataRequest, [&](QSessionManager &manager)
    {
        Q_UNUSED(manager);
        signal_handler(0);
    });
#endif

    API::defaultClient = new API::Client();

    UI_InitMainWindow();

    Configs::dataManager->RunDeferredMaintenance();

    if (Logging::PreviousSessionCrashed()) {
        MW_show_log(QObject::tr("[Warn] Throne did not shut down cleanly last time. "
                                "Diagnostics were saved to: %1").arg(Logging::LogDir()));
    }

    // Deliver a deeplink and any files passed on the command line (cold start), then
    // replay whatever arrived during startup (e.g. a macOS FileOpen event before the
    // window existed).
    if (!safeCanary) {
        if (!launchDeeplink.isEmpty()) Deeplink_Submit(launchDeeplink);
        Deeplink_FlushPending();
        LaunchFiles_Submit(launchFiles);
        LaunchFiles_FlushPending();
    }

    if (safeCanaryExitMs > 0) {
        QTimer::singleShot(safeCanaryExitMs, &a, &QCoreApplication::quit);
    }

    return QApplication::exec();
}
