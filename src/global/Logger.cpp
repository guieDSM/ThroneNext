#include "include/global/Logger.hpp"
#include "include/global/DiagnosticLogPolicy.hpp"
#include "NkrVersion.h"

#include <atomic>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSysInfo>
#include <QThread>

#ifdef Q_OS_WIN
#include <string>
#endif

namespace Logging {
    namespace {
        // Keep enough history for a busy 1-2 hour diagnostic session even when
        // a core component becomes noisy. Total bounded footprint: 144 MiB.
        constexpr qint64 MAX_FILE_BYTES = 16 * 1024 * 1024;
        constexpr int MAX_ROTATED_FILES = 8;
        constexpr int MAX_CRASH_LOGS = 5;
        constexpr int MAX_CRASH_DUMPS = 5;
        constexpr int RING_CAPACITY = MAX_RECENT_LINES;
        constexpr int MAX_PREINIT_LINES = 2000;
        constexpr int FLUSH_INTERVAL_MS = 250;
        constexpr qint64 NOISE_SUMMARY_INTERVAL_MS = 30 * 1000;
        constexpr int MAX_NOISE_BUCKETS = 64;

        QMutex g_mutex;
        QFile g_file;
        qint64 g_fileBytes = 0;
        QElapsedTimer g_sinceFlush;
        bool g_initialized = false;

        std::atomic<int> g_level{static_cast<int>(Level::Debug)};

        // Buffering applies only before Init(); afterwards a closed file means
        // disk logging has failed for good and messages are dropped.
        QStringList g_preInit;
        bool g_preInitOverflowed = false;
        bool g_initDone = false;

        QByteArray g_ring[RING_CAPACITY];
        int g_ringHead = 0;
        int g_ringCount = 0;

        QString g_logDir;
        QString g_crashDir;
        QString g_logPath;
        QString g_markerPath;
        QString g_previousSessionLog;
        bool g_previousCrashed = false;

        struct NoiseBucket {
            qint64 firstSeenMs = 0;
            qint64 lastSeenMs = 0;
            int suppressed = 0;
            QString sample;
        };
        QHash<QString, NoiseBucket> g_noiseBuckets;

#ifdef Q_OS_WIN
        std::wstring g_crashDirNative;
#endif

        const char *levelTag(Level level) {
            switch (level) {
                case Level::Trace: return "TRC";
                case Level::Debug: return "DBG";
                case Level::Info: return "INF";
                case Level::Warn: return "WRN";
                case Level::Error: return "ERR";
                case Level::Fatal: return "FTL";
                default: return "OFF";
            }
        }

        const char *shortFile(const char *file) {
            if (file == nullptr) return nullptr;
            const char *last = file;
            for (const char *p = file; *p != '\0'; ++p) {
                if (*p == '/' || *p == '\\') last = p + 1;
            }
            return last;
        }

        QString formatLine(Level level, const QString &message, const char *file, int line) {
            QString out = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
            out += " [";
            out += QLatin1String(levelTag(level));
            out += "] [";
            out += QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16);
            out += ']';
            if (file != nullptr && line > 0) {
                out += ' ';
                out += QLatin1String(shortFile(file));
                out += ':';
                out += QString::number(line);
            }
            out += " | ";
            out += message;
            return out;
        }

        QString normalizedNoiseSignature(QString line) {
            static const QRegularExpression elapsed(
                R"(\[[0-9]+(?:\.[0-9]+)?(?:ms|s|m|h)(?:[0-9]+(?:\.[0-9]+)?(?:ms|s|m|h))*\])");
            static const QRegularExpression numericId(R"(\[[0-9]+\])");
            static const QRegularExpression endpoint(
                R"(\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}:[0-9]+\b)");
            line.replace(elapsed, "[elapsed]");
            line.replace(numericId, "[id]");
            line.replace(endpoint, "<endpoint>");
            return line;
        }

        // Caller holds g_mutex.
        void pushRing(const QByteArray &utf8) {
            g_ring[g_ringHead] = utf8;
            g_ringHead = (g_ringHead + 1) % RING_CAPACITY;
            if (g_ringCount < RING_CAPACITY) g_ringCount++;
        }

        // Caller holds g_mutex. throne.log -> throne.log.1 -> ..., oldest dropped.
        void rotateLocked() {
            g_file.close();
            const QString oldest = g_logPath + "." + QString::number(MAX_ROTATED_FILES);
            QFile::remove(oldest);
            for (int i = MAX_ROTATED_FILES - 1; i >= 1; --i) {
                const QString from = g_logPath + "." + QString::number(i);
                const QString to = g_logPath + "." + QString::number(i + 1);
                if (QFile::exists(from)) QFile::rename(from, to);
            }
            QFile::rename(g_logPath, g_logPath + ".1");
            g_file.setFileName(g_logPath);
            if (!g_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
            g_fileBytes = 0;
        }

        // Caller holds g_mutex.
        void writeLocked(const QString &formatted, bool flushNow) {
            const QByteArray utf8 = formatted.toUtf8();
            pushRing(utf8);

            if (!g_file.isOpen()) {
                if (g_initDone) return; // disk logging is off; the ring still has it
                if (g_preInit.size() < MAX_PREINIT_LINES) {
                    g_preInit.append(formatted);
                } else {
                    g_preInitOverflowed = true;
                }
                return;
            }

            g_file.write(utf8);
            g_file.write("\n", 1);
            g_fileBytes += utf8.size() + 1;

            if (flushNow || !g_sinceFlush.isValid() || g_sinceFlush.elapsed() >= FLUSH_INTERVAL_MS) {
                g_file.flush();
                g_sinceFlush.restart();
            }
            if (g_fileBytes >= MAX_FILE_BYTES) rotateLocked();
        }

        // Caller holds g_mutex.
        void writeNoiseSummaryLocked(const NoiseBucket &bucket) {
            if (bucket.suppressed <= 0) return;
            const qint64 seconds = qMax<qint64>(1, (bucket.lastSeenMs - bucket.firstSeenMs) / 1000);
            writeLocked(formatLine(
                            Level::Info,
                            QString("[ui] [transport reset summary] %1 identical lines suppressed over %2 s; sample: %3")
                                .arg(bucket.suppressed)
                                .arg(seconds)
                                .arg(bucket.sample),
                            nullptr,
                            0),
                        false);
        }

        // Caller holds g_mutex.
        void flushNoiseSummariesLocked() {
            for (auto it = g_noiseBuckets.cbegin(); it != g_noiseBuckets.cend(); ++it) {
                writeNoiseSummaryLocked(it.value());
            }
            g_noiseBuckets.clear();
        }

        void pruneOldest(const QString &dir, const QStringList &filters, int keep) {
            QDir d(dir);
            const auto entries = d.entryInfoList(filters, QDir::Files, QDir::Time);
            for (int i = keep; i < entries.size(); ++i) {
                QFile::remove(entries.at(i).absoluteFilePath());
            }
        }

        QString sessionHeader() {
            QString out;
            out += "===== Throne session start =====\n";
            out += "  version   : " NKR_VERSION "\n";
            out += "  built     : " __DATE__ " " __TIME__ "\n";
            out += "  qt        : " + QString(qVersion()) + " (built against " QT_VERSION_STR ")\n";
            out += "  os        : " + QSysInfo::prettyProductName() + " (kernel " + QSysInfo::kernelVersion() + ")\n";
            out += "  arch      : " + QSysInfo::currentCpuArchitecture() + "\n";
            out += "  pid       : " + QString::number(QCoreApplication::applicationPid()) + "\n";
            const auto args = QCoreApplication::arguments();
            out += "  args      : " + (args.isEmpty() ? QStringLiteral("(none)") : args.join(' ')) + "\n";
            out += "  log dir   : " + g_logDir + "\n";
            out += "================================";
            return out;
        }
    } // namespace

    Level LevelFromString(const QString &name) {
        const QString n = name.trimmed().toLower();
        if (n == "trace") return Level::Trace;
        if (n == "debug") return Level::Debug;
        if (n == "info") return Level::Info;
        if (n == "warn" || n == "warning") return Level::Warn;
        if (n == "error") return Level::Error;
        if (n == "fatal" || n == "panic") return Level::Fatal;
        if (n == "off" || n == "none") return Level::Off;
        return Level::Debug;
    }

    QString LevelToString(Level level) {
        switch (level) {
            case Level::Trace: return "trace";
            case Level::Debug: return "debug";
            case Level::Info: return "info";
            case Level::Warn: return "warn";
            case Level::Error: return "error";
            case Level::Fatal: return "fatal";
            default: return "off";
        }
    }

    void SetLevel(Level level) { g_level.store(static_cast<int>(level), std::memory_order_relaxed); }

    Level GetLevel() { return static_cast<Level>(g_level.load(std::memory_order_relaxed)); }

    bool IsEnabled(Level level) {
        return static_cast<int>(level) >= g_level.load(std::memory_order_relaxed);
    }

    void Write(Level level, const QString &message, const char *file, int line) {
        if (!IsEnabled(level)) return;
        const QString formatted = formatLine(level, message, file, line);
        QMutexLocker lock(&g_mutex);
        writeLocked(formatted, level >= Level::Warn);
    }

    bool WriteUserLog(const QString &message) {
        if (!IsEnabled(Level::Info)) return true;
        bool shouldDisplay = false;
        for (const auto &line : message.split('\n')) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) continue;
            if (Policy::IsCollapsibleTransportNoise(trimmed.toStdString())) {
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                const QString signature = normalizedNoiseSignature(trimmed);
                QMutexLocker lock(&g_mutex);
                auto it = g_noiseBuckets.find(signature);
                if (it == g_noiseBuckets.end()) {
                    if (g_noiseBuckets.size() >= MAX_NOISE_BUCKETS) flushNoiseSummariesLocked();
                    g_noiseBuckets.insert(signature, NoiseBucket{now, now, 0, trimmed});
                } else if (now - it->firstSeenMs < NOISE_SUMMARY_INTERVAL_MS) {
                    it->lastSeenMs = now;
                    it->suppressed++;
                    continue;
                } else {
                    writeNoiseSummaryLocked(it.value());
                    *it = NoiseBucket{now, now, 0, trimmed};
                }
                const QString formatted = formatLine(Level::Info, "[ui] " + trimmed, nullptr, 0);
                writeLocked(formatted, false);
                shouldDisplay = true;
                continue;
            }
            const QString formatted = formatLine(Level::Info, "[ui] " + trimmed, nullptr, 0);
            QMutexLocker lock(&g_mutex);
            writeLocked(formatted, false);
            shouldDisplay = true;
        }
        return shouldDisplay;
    }

    void Init(const QString &baseDir) {
        QMutexLocker lock(&g_mutex);
        if (g_initialized) return;
        g_initialized = true;
        g_initDone = true;

        g_logDir = QDir(baseDir).absoluteFilePath("logs");
        g_crashDir = QDir(baseDir).absoluteFilePath("crashes");
        QDir().mkpath(g_logDir);
        QDir().mkpath(g_crashDir);

        g_logPath = QDir(g_logDir).absoluteFilePath("throne.log");
        g_markerPath = QDir(g_logDir).absoluteFilePath("running.marker");

#ifdef Q_OS_WIN
        g_crashDirNative = QDir::toNativeSeparators(g_crashDir).toStdWString();
#endif

        // Set the crashed session's log aside before this one truncates it and
        // before rotation can push it out.
        g_previousCrashed = QFile::exists(g_markerPath);
        if (g_previousCrashed && QFile::exists(g_logPath)) {
            const QString stamp = QFileInfo(g_logPath).lastModified().toString("yyyyMMdd-HHmmss");
            const QString preserved = QDir(g_logDir).absoluteFilePath("crashed-" + stamp + ".log");
            QFile::remove(preserved);
            if (QFile::rename(g_logPath, preserved)) g_previousSessionLog = preserved;
            pruneOldest(g_logDir, {"crashed-*.log"}, MAX_CRASH_LOGS);
        }

        g_file.setFileName(g_logPath);
        if (!g_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            g_preInit.clear();
            return;
        }
        g_fileBytes = 0;
        g_sinceFlush.start();

        QFile marker(g_markerPath);
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            marker.write(QString("pid=%1 version=%2 started=%3\n")
                             .arg(QCoreApplication::applicationPid())
                             .arg(NKR_VERSION)
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                             .toUtf8());
            marker.close();
        }

        for (const auto &line : sessionHeader().split('\n')) {
            g_file.write(line.toUtf8());
            g_file.write("\n", 1);
            g_fileBytes += line.toUtf8().size() + 1;
        }

        if (g_previousCrashed) {
            writeLocked(formatLine(Level::Warn,
                                   g_previousSessionLog.isEmpty()
                                       ? QStringLiteral("previous session did not shut down cleanly")
                                       : "previous session did not shut down cleanly; its log was kept at " + g_previousSessionLog,
                                   nullptr, 0),
                        true);
        }
        if (g_preInitOverflowed) {
            writeLocked(formatLine(Level::Warn, "pre-init log buffer overflowed, some early lines were dropped", nullptr, 0), false);
        }
        for (const auto &line : g_preInit) {
            g_file.write(line.toUtf8());
            g_file.write("\n", 1);
            g_fileBytes += line.toUtf8().size() + 1;
        }
        g_preInit.clear();
        g_file.flush();

        pruneOldest(g_crashDir, {"*.dmp"}, MAX_CRASH_DUMPS);
        pruneOldest(g_crashDir, {"*.txt"}, MAX_CRASH_DUMPS);
    }

    void Shutdown() {
        QMutexLocker lock(&g_mutex);
        if (!g_initialized) return;
        if (g_file.isOpen()) {
            flushNoiseSummariesLocked();
            writeLocked(formatLine(Level::Info, "===== Throne session end (clean) =====", nullptr, 0), true);
            g_file.close();
        }
        QFile::remove(g_markerPath);
        g_initialized = false;
    }

    void FlushForCrash() {
        // The faulting thread may already hold the lock; do not wait forever.
        if (g_mutex.tryLock(200)) {
            if (g_file.isOpen()) g_file.flush();
            g_mutex.unlock();
        } else if (g_file.isOpen()) {
            g_file.flush();
        }
    }

    int RecentLinesRaw(const QByteArray **out, int max) {
        const int count = qMin(max, g_ringCount);
        if (count <= 0) return 0;
        // Oldest first: head points one past the newest entry.
        int idx = (g_ringHead - count + RING_CAPACITY * 2) % RING_CAPACITY;
        for (int i = 0; i < count; ++i) {
            out[i] = &g_ring[idx];
            idx = (idx + 1) % RING_CAPACITY;
        }
        return count;
    }

    QStringList RecentLines(int max) {
        QMutexLocker lock(&g_mutex);
        const int want = max <= 0 ? g_ringCount : qMin(max, g_ringCount);
        QStringList out;
        out.reserve(want);
        int idx = (g_ringHead - want + RING_CAPACITY * 2) % RING_CAPACITY;
        for (int i = 0; i < want; ++i) {
            out.append(QString::fromUtf8(g_ring[idx]));
            idx = (idx + 1) % RING_CAPACITY;
        }
        return out;
    }

    bool PreviousSessionCrashed() { return g_previousCrashed; }

    QString PreviousSessionLogPath() { return g_previousSessionLog; }

    QString LogDir() { return g_logDir; }

    QString CrashDir() { return g_crashDir; }

#ifdef Q_OS_WIN
    const wchar_t *CrashDirNative() { return g_crashDirNative.c_str(); }
#endif

    namespace {
        void qtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
            Level level = Level::Debug;
            switch (type) {
                case QtDebugMsg: level = Level::Debug; break;
                case QtInfoMsg: level = Level::Info; break;
                case QtWarningMsg: level = Level::Warn; break;
                case QtCriticalMsg: level = Level::Error; break;
                case QtFatalMsg: level = Level::Fatal; break;
            }
            QString text = message;
            if (context.category != nullptr && qstrcmp(context.category, "default") != 0) {
                text = QString("[%1] %2").arg(context.category, message);
            }
            Write(level, text, context.file, context.line);
            // Qt aborts the process the moment this returns for a fatal message.
            if (type == QtFatalMsg) FlushForCrash();
        }
    } // namespace

    void InstallQtMessageHandler() { qInstallMessageHandler(qtMessageHandler); }
} // namespace Logging
