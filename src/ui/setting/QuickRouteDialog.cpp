#include "include/ui/setting/QuickRouteDialog.h"

#include "include/database/RoutesRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/database/entities/QuickRoutePolicy.hpp"
#include "include/database/entities/SimpleRouteInputPolicy.hpp"
#include "include/global/Configs.hpp"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHostAddress>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QMap>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#undef SetPort
#endif

namespace {
    struct AppChoice {
        QString name;
        QStringList paths;
    };

    QString cleanExe(QString raw) {
        raw = QDir::fromNativeSeparators(raw.trimmed());
        if (raw.startsWith('"')) {
            const qsizetype end = raw.indexOf('"', 1);
            if (end > 0) raw = raw.mid(1, end - 1);
        } else {
            const qsizetype exe = raw.indexOf(QStringLiteral(".exe"), 0, Qt::CaseInsensitive);
            if (exe >= 0) raw = raw.left(exe + 4);
        }
        if (QFileInfo(raw).isFile() && raw.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
            return QDir::toNativeSeparators(QFileInfo(raw).absoluteFilePath());
        return {};
    }

    QStringList executableChildren(const QString& directory, int maxDepth = 1) {
        QStringList result;
        if (!QDir(directory).exists()) return result;
        QStringList directories{directory};
        for (int depth = 0; depth <= maxDepth && !directories.isEmpty() && result.size() < 100; ++depth) {
            QStringList next;
            for (const QString& folder : directories) {
                const QDir dir(folder);
                for (const QFileInfo& exe : dir.entryInfoList({QStringLiteral("*.exe")}, QDir::Files)) {
                    result.append(QDir::toNativeSeparators(exe.absoluteFilePath()));
                    if (result.size() >= 100) break;
                }
                if (depth == maxDepth || next.size() >= 100) continue;
                for (const QFileInfo& sub : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                    next.append(sub.absoluteFilePath());
                    if (next.size() >= 100) break;
                }
                if (result.size() >= 100) break;
            }
            directories = next;
        }
        result.removeDuplicates();
        return result;
    }

    QList<AppChoice> installedApps() {
        QList<AppChoice> result;
#ifdef Q_OS_WIN
        const QStringList roots = {
            QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
            QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
            QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall")};
        QSet<QString> seen;
        for (const QString& root : roots) {
            QSettings registry(root, QSettings::NativeFormat);
            for (const QString& key : registry.childGroups()) {
                registry.beginGroup(key);
                const QString name = registry.value(QStringLiteral("DisplayName")).toString().trimmed();
                const QString icon = cleanExe(registry.value(QStringLiteral("DisplayIcon")).toString());
                const QString location = registry.value(QStringLiteral("InstallLocation")).toString();
                registry.endGroup();
                if (name.isEmpty() || seen.contains(name.toCaseFolded())) continue;
                QStringList paths;
                if (!icon.isEmpty()) paths.append(icon);
                // A shallow scan finds helpers, while avoiding every game/data folder.
                if (!location.isEmpty()) paths.append(executableChildren(location, 1));
                paths.removeDuplicates();
                if (paths.isEmpty()) continue;
                seen.insert(name.toCaseFolded());
                result.append({name, paths});
            }
        }
        // Steam games stay separate from the Steam client, but are suggested
        // immediately in the same installed-app list (including extra libraries).
        QSettings steam(QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Valve\\Steam"), QSettings::NativeFormat);
        const QString steamRoot = QDir::fromNativeSeparators(steam.value(QStringLiteral("SteamPath")).toString());
        QStringList libraries{steamRoot};
        QFile folders(QDir(steamRoot).filePath(QStringLiteral("steamapps/libraryfolders.vdf")));
        if (folders.open(QIODevice::ReadOnly)) {
            const QString contents = QString::fromUtf8(folders.readAll());
            const QRegularExpression pathLine(QStringLiteral("\\\"path\\\"\\s+\\\"([^\\\"]+)\\\""),
                                              QRegularExpression::CaseInsensitiveOption);
            auto matches = pathLine.globalMatch(contents);
            while (matches.hasNext()) {
                QString path = matches.next().captured(1);
                path.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
                libraries.append(QDir::fromNativeSeparators(path));
            }
        }
        libraries.removeDuplicates();
        for (const QString& library : libraries) {
            const QDir games(QDir(library).filePath(QStringLiteral("steamapps/common")));
            for (const QString& game : games.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (seen.contains(game.toCaseFolded())) continue;
                const auto paths = executableChildren(games.filePath(game), 2);
                if (paths.isEmpty()) continue;
                result.append({QStringLiteral("Steam · ") + game, paths});
                seen.insert(game.toCaseFolded());
            }
        }
#endif
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return a.name.localeAwareCompare(b.name) < 0;
        });
        return result;
    }

    QList<AppChoice> runningApps() {
        QList<AppChoice> result;
#ifdef Q_OS_WIN
        QHash<QString, QStringList> byName;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (Process32FirstW(snapshot, &entry)) do {
                HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
                if (!process) continue;
                wchar_t buffer[32768];
                DWORD length = 32768;
                if (QueryFullProcessImageNameW(process, 0, buffer, &length)) {
                    const QString path = QString::fromWCharArray(buffer, length);
                    const QString directory = QFileInfo(path).absolutePath();
                    if (!byName[directory].contains(path)) byName[directory].append(path);
                }
                CloseHandle(process);
            } while (Process32NextW(snapshot, &entry));
            CloseHandle(snapshot);
        }
        for (auto it = byName.cbegin(); it != byName.cend(); ++it)
            result.append({QFileInfo(it.key()).fileName(), it.value()});
#endif
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return a.name.localeAwareCompare(b.name) < 0;
        });
        return result;
    }

    void fillTree(QTreeWidget* tree, const QList<AppChoice>& choices) {
        const QSignalBlocker blocked(tree);
        tree->clear();
        for (const auto& app : choices) {
            auto* parent = new QTreeWidgetItem(tree, {app.name,
                QStringLiteral("%1 exe").arg(app.paths.size())});
            parent->setToolTip(0, app.name);
            parent->setFlags(parent->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
            parent->setCheckState(0, Qt::Unchecked);
            for (const QString& path : app.paths) {
                auto* child = new QTreeWidgetItem(parent, {QFileInfo(path).fileName(), path});
                child->setToolTip(0, QFileInfo(path).fileName());
                child->setToolTip(1, path);
                child->setFlags(child->flags() | Qt::ItemIsUserCheckable);
                child->setCheckState(0, Qt::Unchecked);
                child->setData(0, Qt::UserRole, path);
            }
        }
    }

    QJsonArray asArray(const QStringList& values) {
        QJsonArray out;
        for (const QString& value : values) out.append(value);
        return out;
    }
}

QuickRouteDialog::QuickRouteDialog(QWidget* parent, StartTab initialTab,
                                   PreviewCallback beginPreview, PreviewReadyCallback previewReady,
                                   FinishPreviewCallback finishPreview,
                                   SavedCallback saved, ValidateCallback validate,
                                   OpenRoutesCallback openRoutes)
    : QDialog(parent), beginPreview_(std::move(beginPreview)),
      previewReady_(std::move(previewReady)),
      finishPreview_(std::move(finishPreview)), saved_(std::move(saved)),
      validate_(std::move(validate)), openRoutes_(std::move(openRoutes)) {
    setWindowTitle(tr("Исключения маршрутизации"));
    resize(780, 660);
    auto* outer = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Направлять выбранные сайты и приложения напрямую, в обход VPN."), this);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    tabs_ = new QTabWidget(this);
    outer->addWidget(tabs_, 1);

    auto* site = new QWidget(tabs_);
    auto* siteLayout = new QVBoxLayout(site);
    siteLayout->setSpacing(10);
    siteLayout->addWidget(new QLabel(tr("Ссылка на сайт или адрес, который должен открываться без VPN"), site));
    address_ = new QLineEdit(site);
    address_->setPlaceholderText(tr("Полная ссылка, домен, IP или подсеть"));
    address_->setClearButtonEnabled(true);
    siteLayout->addWidget(address_);
    auto* targetBox = new QGroupBox(tr("Что добавится в маршрутизацию"), site);
    auto* targetLayout = new QVBoxLayout(targetBox);
    addressInfo_ = new QLabel(targetBox);
    addressInfo_->setWordWrap(true);
    targetLayout->addWidget(addressInfo_);
    targetLayout->addWidget(new QLabel(tr("Выбранные адреса пойдут напрямую, в обход VPN."), targetBox));
    siteLayout->addWidget(targetBox);

    relatedBox_ = new QGroupBox(tr("Домены страницы и внешнего плеера"), site);
    auto* relatedLayout = new QVBoxLayout(relatedBox_);
    scanInfo_ = new QLabel(tr("Для автоматической проверки одной вкладки используйте локальное расширение Chrome. Также можно добавить домен вручную или разобрать HAR."), relatedBox_);
    scanInfo_->setWordWrap(true);
    relatedLayout->addWidget(scanInfo_);
    auto* extraRow = new QHBoxLayout();
    extraDomain_ = new QLineEdit(relatedBox_);
    extraDomain_->setPlaceholderText(tr("Домен или ссылка внешнего плеера"));
    extraDomain_->setClearButtonEnabled(true);
    extraRow->addWidget(extraDomain_, 1);
    addDomainButton_ = new QPushButton(tr("Добавить домен"), relatedBox_);
    extraRow->addWidget(addDomainButton_);
    relatedLayout->addLayout(extraRow);
    auto* diagnosticRow = new QHBoxLayout();
    scanButton_ = new QPushButton(tr("Открыть сайт для проверки"), relatedBox_);
    diagnosticRow->addWidget(scanButton_);
    auto* pasteErrors = new QPushButton(tr("Разобрать ошибки консоли…"), relatedBox_);
    diagnosticRow->addWidget(pasteErrors);
    auto* importHar = new QPushButton(tr("Разобрать HAR…"), relatedBox_);
    diagnosticRow->addWidget(importHar);
    relatedLayout->addLayout(diagnosticRow);
    candidatePages_ = new QStackedWidget(relatedBox_);
    auto* emptyPage = new QWidget(candidatePages_);
    auto* emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->addStretch();
    emptyCandidates_ = new QLabel(tr("Дополнительных доменов пока нет.\n\nОткройте нужную страницу, запустите захват через расширение Chrome и воспроизведите проблему. Затем передайте результат в Throne. Можно также разобрать HAR или ввести домен вручную."), emptyPage);
    emptyCandidates_->setWordWrap(true);
    emptyCandidates_->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyCandidates_);
    emptyLayout->addStretch();
    candidatePages_->addWidget(emptyPage);
    candidates_ = new QListWidget(candidatePages_);
    candidates_->setAlternatingRowColors(true);
    candidatePages_->addWidget(candidates_);
    relatedLayout->addWidget(candidatePages_, 1);
    siteLayout->addWidget(relatedBox_, 1);
    tabs_->addTab(site, tr("Сайт / IP"));

    auto* apps = new QWidget(tabs_);
    auto* appLayout = new QVBoxLayout(apps);
    appLayout->setSpacing(10);
    appLayout->addWidget(new QLabel(tr("Отметьте приложение целиком или раскройте его и выберите нужные exe."), apps));
    appSearch_ = new QLineEdit(apps);
    appSearch_->setPlaceholderText(tr("Поиск программы или exe"));
    appSearch_->setClearButtonEnabled(true);
    appLayout->addWidget(appSearch_);
    auto* appTabs = new QTabWidget(apps);
    installed_ = new QTreeWidget(appTabs);
    running_ = new QTreeWidget(appTabs);
    for (auto* tree : {installed_, running_}) {
        tree->setHeaderLabels({tr("Приложение / процесс"), tr("Число exe или полный путь")});
        tree->setAlternatingRowColors(true);
        tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
        tree->setColumnWidth(0, 280);
        tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
        tree->header()->setStretchLastSection(true);
    }
    appTabs->addTab(installed_, tr("Установленные"));
    appTabs->addTab(running_, tr("Запущенные"));
    appLayout->addWidget(appTabs, 1);
    auto* appBottom = new QHBoxLayout();
    appSelection_ = new QLabel(tr("Не выбрано ни одного exe"), apps);
    appSelection_->setWordWrap(true);
    appBottom->addWidget(appSelection_, 1);
    auto* chooseExe = new QPushButton(tr("Другой exe…"), apps);
    appBottom->addWidget(chooseExe);
    appLayout->addLayout(appBottom);
    if (!Configs::dataManager->settingsRepo->spmode_vpn) {
        auto* modeNote = new QLabel(tr("Без режима VPN/TUN правило действует только для трафика, который проходит через Throne."), apps);
        modeNote->setWordWrap(true);
        appLayout->addWidget(modeNote);
    }
    tabs_->addTab(apps, tr("Приложение"));

    auto* management = new QWidget(tabs_);
    auto* managementLayout = new QVBoxLayout(management);
    auto* managementHint = new QLabel(tr("Исключения из этого меню можно менять здесь. Остальные правила активного профиля доступны для просмотра и открываются в «Маршрутах»."), management);
    managementHint->setWordWrap(true);
    managementLayout->addWidget(managementHint);
    managed_ = new QTreeWidget(management);
    managed_->setHeaderHidden(true);
    managed_->setAlternatingRowColors(true);
    managementLayout->addWidget(managed_, 1);
    ruleDetails_ = new QPlainTextEdit(management);
    ruleDetails_->setReadOnly(true);
    ruleDetails_->setMaximumHeight(130);
    ruleDetails_->setPlaceholderText(tr("Выберите правило, чтобы увидеть его содержимое."));
    managementLayout->addWidget(ruleDetails_);
    auto* editButton = new QPushButton(tr("Изменить выбранное исключение"), management);
    editButton->setEnabled(false);
    managementLayout->addWidget(editButton);
    auto* deleteButton = new QPushButton(tr("Удалить выбранное исключение"), management);
    deleteButton->setEnabled(false);
    managementLayout->addWidget(deleteButton);
    auto* openRoutesButton = new QPushButton(tr("Открыть в «Маршрутах»"), management);
    openRoutesButton->setEnabled(false);
    managementLayout->addWidget(openRoutesButton);
    tabs_->addTab(management, tr("Добавленные"));

    auto* profilesBox = new QGroupBox(tr("Сохранить в профили маршрутизации"), this);
    auto* profilesLayout = new QVBoxLayout(profilesBox);
    profiles_ = new QListWidget(profilesBox);
    profiles_->setMaximumHeight(105);
    profilesLayout->addWidget(profiles_);
    rememberProfiles_ = new QCheckBox(tr("Запомнить выбор профилей для новых исключений"), profilesBox);
    profilesLayout->addWidget(rememberProfiles_);
    outer->addWidget(profilesBox);

    auto* footer = new QHBoxLayout();
    footer->addStretch();
    auto* save = new QPushButton(tr("Проверить и сохранить"), this);
    save->setDefault(true);
    auto* close = new QPushButton(tr("Закрыть"), this);
    footer->addWidget(save);
    footer->addWidget(close);
    outer->addLayout(footer);

    previewTimer_ = new QTimer(this);
    previewTimer_->setInterval(250);
    connect(address_, &QLineEdit::textChanged, this, [this] {
        if (!scanning_) {
            candidates_->clear();
            refreshCandidateView();
            scanInfo_->setText(tr("Расширение Chrome собирает запросы только выбранной вкладки. Запустите захват на нужной странице или разберите HAR."));
        }
        refreshTarget();
    });
    auto addManualDomain = [this] {
        const auto main = Configs::QuickRoute::parseSiteOrIp(address_->text());
        if (main.kind != QStringLiteral("site")) return;
        const auto extra = Configs::QuickRoute::parseSiteOrIp(extraDomain_->text());
        if (extra.kind != QStringLiteral("site")) {
            QMessageBox::warning(this, windowTitle(),
                extra.error.isEmpty() ? tr("Введите ссылку или домен") : extra.error);
            return;
        }
        if (extra.value == main.value) {
            QMessageBox::information(this, windowTitle(),
                tr("Этот домен уже покрыт основным правилом вместе со всеми поддоменами."));
            extraDomain_->clear();
            return;
        }
        for (int i = 0; i < candidates_->count(); ++i) {
            auto* item = candidates_->item(i);
            if (item->data(Qt::UserRole).toString().compare(extra.value, Qt::CaseInsensitive) == 0) {
                item->setCheckState(Qt::Checked);
                candidates_->scrollToItem(item);
                extraDomain_->clear();
                refreshCandidateView();
                return;
            }
        }
        auto* item = new QListWidgetItem(tr("%1 · добавлено вручную").arg(extra.value), candidates_);
        item->setData(Qt::UserRole, extra.value);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        candidates_->scrollToItem(item);
        extraDomain_->clear();
        refreshCandidateView();
    };
    connect(addDomainButton_, &QPushButton::clicked, this, addManualDomain);
    connect(extraDomain_, &QLineEdit::returnPressed, this, addManualDomain);
    connect(pasteErrors, &QPushButton::clicked, this, [this] {
        bool accepted = false;
        const QString log = QInputDialog::getMultiLineText(this, tr("Ошибки вкладки в браузере"),
            tr("Вставьте ошибки Console или неудачные запросы Network только нужной вкладки.\n"
               "404 может означать недоступное или устаревшее видео; блокировка расширением не исправляется маршрутизацией."),
            {}, &accepted);
        if (!accepted || log.trimmed().isEmpty()) return;
        const auto target = Configs::QuickRoute::parseSiteOrIp(address_->text());
        if (target.kind != QStringLiteral("site")) {
            QMessageBox::warning(this, windowTitle(), tr("Сначала введите адрес нужного сайта."));
            return;
        }
        const auto diagnostic = Configs::QuickRoute::analyzeConsoleText(log, target.value);
        int added = 0;
        for (const auto& candidate : diagnostic.candidates) {
            bool exists = false;
            for (int i = 0; i < candidates_->count(); ++i)
                exists |= candidates_->item(i)->data(Qt::UserRole).toString() == candidate.domain;
            if (exists) continue;
            auto* item = new QListWidgetItem(tr("%1 · %2; проверьте доступность ресурса")
                .arg(candidate.domain, candidate.issue), candidates_);
            item->setData(Qt::UserRole, candidate.domain);
            item->setToolTip(candidate.url);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
            ++added;
        }
        refreshCandidateView();
        scanInfo_->setText(tr("Кандидатов из ошибок: %1. HTTP-ошибок: %2, блокировок браузером: %3, ошибок скрипта: %4. Отметьте только нужные домены; HTTP-ошибка сама по себе не доказывает проблему VPN.")
            .arg(added).arg(diagnostic.httpErrors).arg(diagnostic.browserBlocked)
            .arg(diagnostic.scriptErrors));
    });
    connect(importHar, &QPushButton::clicked, this, [this] {
        const auto target = Configs::QuickRoute::parseSiteOrIp(address_->text());
        if (target.kind != QStringLiteral("site")) {
            QMessageBox::warning(this, windowTitle(), tr("Сначала введите адрес нужного сайта."));
            return;
        }
        const QString path = QFileDialog::getOpenFileName(this, tr("HAR нужной вкладки"), {},
                                                           tr("Файлы HAR (*.har);;Все файлы (*)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (file.size() > 32 * 1024 * 1024 || !file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, windowTitle(), tr("Не удалось открыть HAR или файл больше 32 МБ."));
            return;
        }
        QString error;
        const auto diagnostic = Configs::QuickRoute::analyzeHar(file.readAll(), target.value, &error);
        if (!error.isEmpty()) {
            QMessageBox::warning(this, windowTitle(), error);
            return;
        }
        int added = 0;
        for (const auto& candidate : diagnostic.candidates) {
            bool exists = false;
            for (int i = 0; i < candidates_->count(); ++i)
                exists |= candidates_->item(i)->data(Qt::UserRole).toString() == candidate.domain;
            if (exists) continue;
            auto* item = new QListWidgetItem(tr("%1 · %2; проверьте прямой маршрут")
                .arg(candidate.domain, candidate.issue), candidates_);
            item->setData(Qt::UserRole, candidate.domain);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
            ++added;
        }
        refreshCandidateView();
        scanInfo_->setText(tr("HAR: %1 новых доменов, %2 HTML-страниц ошибки (включая ответ 200), "
                              "%3 HTTP-ошибок, %4 блокировки браузером. Проверьте кандидатов перед сохранением: "
                              "ошибка сама по себе не доказывает проблему маршрута.")
            .arg(added).arg(diagnostic.htmlErrorPages).arg(diagnostic.httpErrors)
            .arg(diagnostic.browserBlocked));
    });
    connect(scanButton_, &QPushButton::clicked, this, [this] {
        if (scanning_) stopScan();
        else startScan();
        refreshTarget();
    });
    connect(save, &QPushButton::clicked, this, &QuickRouteDialog::saveException);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(deleteButton, &QPushButton::clicked, this, &QuickRouteDialog::removeException);
    connect(editButton, &QPushButton::clicked, this, &QuickRouteDialog::editException);
    connect(managed_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) { editException(); });
    connect(managed_, &QTreeWidget::currentItemChanged, this,
            [this, editButton, deleteButton, openRoutesButton](QTreeWidgetItem* item) {
                const QString kind = item ? item->data(0, Qt::UserRole + 1).toString() : QString{};
                editButton->setEnabled(kind == QStringLiteral("managed"));
                deleteButton->setEnabled(kind == QStringLiteral("managed"));
                openRoutesButton->setEnabled(kind == QStringLiteral("existing"));
                ruleDetails_->setPlainText(item ? item->data(0, Qt::UserRole + 2).toString() : QString{});
            });
    connect(openRoutesButton, &QPushButton::clicked, this, [this] {
        if (!managed_->currentItem() ||
            managed_->currentItem()->data(0, Qt::UserRole + 1).toString() != QStringLiteral("existing")) return;
        accept();
        if (openRoutes_) openRoutes_();
    });
    connect(appSearch_, &QLineEdit::textChanged, this, [this](const QString& search) {
        for (auto* tree : {installed_, running_})
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                auto* parent = tree->topLevelItem(i);
                const bool parentMatch = parent->text(0).contains(search, Qt::CaseInsensitive);
                bool childMatch = false;
                for (int j = 0; j < parent->childCount(); ++j) {
                    auto* child = parent->child(j);
                    const bool match = child->text(0).contains(search, Qt::CaseInsensitive)
                        || child->text(1).contains(search, Qt::CaseInsensitive);
                    child->setHidden(!search.isEmpty() && !parentMatch && !match);
                    childMatch |= match;
                }
                parent->setHidden(!search.isEmpty() && !parentMatch && !childMatch);
                if (!search.isEmpty() && childMatch && !parentMatch) parent->setExpanded(true);
            }
    });
    for (auto* tree : {installed_, running_})
        connect(tree, &QTreeWidget::itemChanged, this,
                [this](QTreeWidgetItem*, int) { refreshAppSelection(); });
    connect(chooseExe, &QPushButton::clicked, this, [this] {
        const QString chosen = QFileDialog::getOpenFileName(this, tr("Выбрать приложение"), {},
                                                             tr("Приложения (*.exe)"));
        if (chosen.isEmpty()) return;
        populateApplications();
        const QString path = QDir::toNativeSeparators(QFileInfo(chosen).absoluteFilePath());
        auto* group = new QTreeWidgetItem(installed_, {tr("Выбранные вручную"), QStringLiteral("1")});
        group->setFlags(group->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        auto* item = new QTreeWidgetItem(group, {QFileInfo(path).fileName(), path});
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setData(0, Qt::UserRole, path);
        item->setCheckState(0, Qt::Checked);
        installed_->scrollToItem(item);
        refreshAppSelection();
    });
    connect(rememberProfiles_, &QCheckBox::toggled, this, &QuickRouteDialog::updateProfileSelection);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1) populateApplications();
    });

    populateProfiles();
    populateManaged();
    tabs_->setCurrentIndex(initialTab == StartTab::Site ? 0 : 1);
    if (initialTab == StartTab::Application) populateApplications();
    refreshTarget();

    captureServer_ = new QTcpServer(this);
    if (captureServer_->listen(QHostAddress::LocalHost, 18472)) {
        connect(captureServer_, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = captureServer_->nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                auto completed = std::make_shared<bool>(false);
                QTimer::singleShot(10000, socket, [socket] { socket->disconnectFromHost(); });
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, completed] {
                    if (*completed) return;
                    buffer->append(socket->readAll());
                    auto reply = [socket, completed](int status) {
                        *completed = true;
                        const QByteArray body = status == 200 ? "ok" : status == 204 ? "" : "bad request";
                        socket->write("HTTP/1.1 " + QByteArray::number(status) +
                            (status == 200 ? " OK\r\n" : status == 204 ? " No Content\r\n" : " Bad Request\r\n") +
                            "Content-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\n"
                            "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
                            "Access-Control-Allow-Headers: content-type, x-throne-capture\r\n"
                            "Connection: close\r\nContent-Length: " + QByteArray::number(body.size()) +
                            "\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    };
                    if (buffer->size() > 128 * 1024) { reply(413); return; }
                    const qsizetype split = buffer->indexOf("\r\n\r\n");
                    if (split < 0) return;
                    const QString headers = QString::fromLatin1(buffer->left(split));
                    const QRegularExpression originHeader(QStringLiteral(R"((?:^|\r\n)origin:\s*([^\r\n]+))"),
                        QRegularExpression::CaseInsensitiveOption);
                    const QString origin = originHeader.match(headers).captured(1).trimmed();
                    if (!origin.isEmpty() && !origin.startsWith(QStringLiteral("chrome-extension://"))) {
                        reply(400); return;
                    }
                    if (headers.startsWith(QStringLiteral("OPTIONS /capture HTTP/1.1\r\n"))) {
                        reply(204); return;
                    }
                    if (!headers.startsWith(QStringLiteral("POST /capture HTTP/1.1\r\n")) ||
                        !headers.contains(QStringLiteral("\r\nx-throne-capture: 1"),
                                          Qt::CaseInsensitive)) { reply(400); return; }
                    const QRegularExpression lengthHeader(QStringLiteral(R"((?:^|\r\n)content-length:\s*(\d+))"),
                        QRegularExpression::CaseInsensitiveOption);
                    const auto lengthMatch = lengthHeader.match(headers);
                    bool validLength = false;
                    const int length = lengthMatch.captured(1).toInt(&validLength);
                    if (!validLength || length < 0 || length > 64 * 1024) { reply(400); return; }
                    if (buffer->size() - split - 4 < length) return;
                    acceptBrowserCapture(buffer->mid(split + 4, length));
                    reply(200);
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
}

QuickRouteDialog::~QuickRouteDialog() {
    previewTimer_->stop();
    if (previewActive_) finishPreview_();
}

void QuickRouteDialog::acceptBrowserCapture(const QByteArray& body) {
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) return;
    const QJsonObject capture = document.object();
    const auto target = Configs::QuickRoute::parseSiteOrIp(address_->text());
    const auto source = Configs::QuickRoute::parseSiteOrIp(capture.value(QStringLiteral("site")).toString());
    if (target.kind != QStringLiteral("site") || source.value != target.value) {
        scanInfo_->setText(tr("Расширение проверило другую вкладку. Откройте нужный адрес в Throne и повторите захват."));
        return;
    }
    int added = 0;
    int observedAdded = 0;
    for (const auto& candidate : Configs::QuickRoute::readBrowserCaptureCandidates(capture, target.value)) {
        bool exists = false;
        for (int i = 0; i < candidates_->count(); ++i)
            exists |= candidates_->item(i)->data(Qt::UserRole).toString() == candidate.domain;
        if (exists) continue;
        const QString& reason = candidate.reason;
        QString issue;
        if (reason == QStringLiteral("html_error")) issue = tr("страница ошибки в кадре");
        else if (reason == QStringLiteral("http_error")) issue = tr("HTTP-ошибка ресурса");
        else if (reason == QStringLiteral("network_error")) issue = tr("ошибка соединения");
        else if (reason == QStringLiteral("observed_host")) {
            issue = tr("обнаружен во вкладке без сетевой ошибки");
            ++observedAdded;
        }
        else continue;
        auto* item = new QListWidgetItem(tr("%1 · %2; проверьте прямой маршрут")
            .arg(candidate.domain, issue), candidates_);
        item->setData(Qt::UserRole, candidate.domain);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        ++added;
    }
    refreshCandidateView();
    scanInfo_->setText(tr("Захват одной вкладки: %1 новых доменов (из них %2 без сетевой ошибки); %3 запросов заблокировано браузером. "
                          "Кандидаты не сохраняются автоматически: отметьте нужные после проверки.")
        .arg(added).arg(observedAdded).arg(capture.value(QStringLiteral("blocked")).toInt()));
}

void QuickRouteDialog::populateProfiles() {
    profiles_->clear();
    const auto& settings = Configs::dataManager->settingsRepo;
    rememberProfiles_->setChecked(settings->quick_route_remember_profiles);
    const QStringList remembered = settings->quick_route_profile_ids;
    for (const auto& profile : Configs::dataManager->routesRepo->GetAllRouteProfiles()) {
        QString label = profile->name;
        if (profile->isRemote) label += tr(" (будет создана локальная копия)");
        auto* item = new QListWidgetItem(label, profiles_);
        item->setData(Qt::UserRole, profile->id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(rememberProfiles_->isChecked()
            ? (remembered.contains(QString::number(profile->id)) ? Qt::Checked : Qt::Unchecked)
            : (profile->id == settings->current_route_id ? Qt::Checked : Qt::Unchecked));
    }
}

void QuickRouteDialog::populateApplications() {
    if (applicationsLoaded_) return;
    fillTree(installed_, installedApps());
    fillTree(running_, runningApps());
    applicationsLoaded_ = true;
    refreshAppSelection();
}

void QuickRouteDialog::refreshCandidateView() {
    candidatePages_->setCurrentWidget(candidates_->count() ? static_cast<QWidget*>(candidates_)
                                                         : candidatePages_->widget(0));
    emptyCandidates_->setText(scanning_
        ? tr("Сайт открыт. Проверьте страницу или плеер, затем вставьте ошибки Console/Network этой вкладки или укажите внешний домен вручную.")
        : tr("Дополнительных доменов пока нет.\n\nДля внешнего плеера вставьте его домен вручную или ошибки Console/Network нужной вкладки. Найденные в ошибках домены можно проверить перед сохранением."));
}

void QuickRouteDialog::refreshAppSelection() {
    QSet<QString> seen;
    QStringList names;
    for (auto* tree : {installed_, running_})
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto* parent = tree->topLevelItem(i);
            for (int j = 0; j < parent->childCount(); ++j) {
                auto* child = parent->child(j);
                if (child->checkState(0) != Qt::Checked) continue;
                const QString path = child->data(0, Qt::UserRole).toString();
                if (path.isEmpty() || seen.contains(path.toCaseFolded())) continue;
                seen.insert(path.toCaseFolded());
                names.append(QFileInfo(path).fileName());
            }
        }
    appSelection_->setText(names.isEmpty() ? tr("Не выбрано ни одного exe")
        : tr("Выбрано %1 exe: %2%3").arg(names.size())
              .arg(names.mid(0, 3).join(QStringLiteral(", ")))
              .arg(names.size() > 3 ? tr(" и ещё %1").arg(names.size() - 3) : QString{}));
}

void QuickRouteDialog::populateManaged() {
    managed_->clear();
    ruleDetails_->clear();
    const auto& settings = Configs::dataManager->settingsRepo;
    const auto records = Configs::QuickRoute::readRecords(settings->quick_route_records);
    const auto profile = Configs::dataManager->routesRepo->GetRouteProfile(settings->current_route_id);
    QHash<QString, QTreeWidgetItem*> categories;
    QHash<QString, QTreeWidgetItem*> sections;
    QHash<QString, QTreeWidgetItem*> groups;

    auto* sites = new QTreeWidgetItem(managed_, {tr("Сайты и домены")});
    auto* apps = new QTreeWidgetItem(managed_, {tr("Приложения и процессы")});
    auto* ips = new QTreeWidgetItem(managed_, {tr("IP и подсети")});
    auto* others = new QTreeWidgetItem(managed_, {tr("Другие правила")});
    categories.insert(QStringLiteral("site"), sites);
    categories.insert(QStringLiteral("app"), apps);
    categories.insert(QStringLiteral("ip"), ips);
    categories.insert(QStringLiteral("other"), others);

    auto addGroup = [&](const QString& kind, const QString& origin, const QString& groupName) {
        const QString sectionKey = kind + QChar(0x1f) + origin;
        auto* section = sections.value(sectionKey, nullptr);
        if (!section) {
            section = new QTreeWidgetItem(categories.value(kind),
                {origin == QStringLiteral("managed") ? tr("Добавлено через это меню")
                                                     : tr("Уже в активном профиле: %1").arg(profile ? profile->name : QString{})});
            section->setExpanded(origin == QStringLiteral("managed"));
            sections.insert(sectionKey, section);
        }
        const QString groupKey = sectionKey + QChar(0x1f) + groupName;
        auto* group = groups.value(groupKey, nullptr);
        if (!group) {
            group = new QTreeWidgetItem(section, {groupName});
            group->setExpanded(false);
            groups.insert(groupKey, group);
        }
        return group;
    };

    auto selectorGroup = [](const QString& kind, const QString& field, const QString& value) {
        if (kind == QStringLiteral("site")) {
            if (field == QStringLiteral("rule_set")) return QStringLiteral("Наборы правил");
            if (field == QStringLiteral("domain_regex") || field == QStringLiteral("domain_keyword"))
                return QStringLiteral("Шаблоны доменов");
            const QString root = Configs::QuickRoute::registrableDomain(value);
            return root.isEmpty() ? value : root;
        }
        if (kind == QStringLiteral("app")) {
            if (field == QStringLiteral("process_path_regex")) return QStringLiteral("Шаблоны процессов");
            return QFileInfo(value).fileName();
        }
        const QString address = value.section('/', 0, 0);
        return address.contains(':') ? QStringLiteral("IPv6") : QStringLiteral("IPv4");
    };

    for (const auto& record : records) {
        QString kind = record.value("kind").toString();
        if (!categories.contains(kind)) kind = QStringLiteral("other");
        const QString label = record.value("display").toString();
        auto* group = addGroup(kind, QStringLiteral("managed"), label);
        auto* item = new QTreeWidgetItem(group, {label});
        item->setData(0, Qt::UserRole, record.value("id").toString());
        item->setData(0, Qt::UserRole + 1, QStringLiteral("managed"));
        item->setData(0, Qt::UserRole + 2, QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Indented)));
        item->setToolTip(0, tr("Профилей: %1. Выберите это исключение целиком для изменения или удаления.")
            .arg(record.value("profiles").toArray().size()));
        const QString field = kind == QStringLiteral("site") ? QStringLiteral("domains")
                            : kind == QStringLiteral("app") ? QStringLiteral("paths") : QStringLiteral("ips");
        for (const auto& value : record.value(field).toArray())
            new QTreeWidgetItem(item, {value.toString()});
    }

    if (profile) {
        QJsonArray rules;
        if (profile->isRaw) rules = QJsonDocument::fromJson(profile->rawRoute.toUtf8())
                                        .object().value(QStringLiteral("rules")).toArray();
        else for (const auto& rule : profile->Rules) {
            QJsonObject value = rule->get_rule_json(true);
            value.insert(QStringLiteral("name"), rule->name);
            rules.append(value);
        }
        const QHash<QString, QString> selectorKinds{
            {QStringLiteral("domain"), QStringLiteral("site")},
            {QStringLiteral("domain_suffix"), QStringLiteral("site")},
            {QStringLiteral("domain_keyword"), QStringLiteral("site")},
            {QStringLiteral("domain_regex"), QStringLiteral("site")},
            {QStringLiteral("rule_set"), QStringLiteral("site")},
            {QStringLiteral("process_name"), QStringLiteral("app")},
            {QStringLiteral("process_path"), QStringLiteral("app")},
            {QStringLiteral("process_path_regex"), QStringLiteral("app")},
            {QStringLiteral("ip_cidr"), QStringLiteral("ip")},
            {QStringLiteral("source_ip_cidr"), QStringLiteral("ip")}};
        for (int index = 0; index < rules.size(); ++index) {
            const QJsonObject rule = rules.at(index).toObject();
            bool managedRule = false;
            for (const auto& record : records) {
                bool inProfile = false;
                for (const auto& id : record.value("profiles").toArray())
                    inProfile |= id.toInt() == profile->id;
                if (!inProfile) continue;
                if (profile->isRaw) managedRule = rule == Configs::QuickRoute::directRule(record);
                else managedRule = rule.value(QStringLiteral("name")).toString().startsWith(
                    QStringLiteral("Quick route [%1] ").arg(record.value("id").toString()));
                if (managedRule) break;
            }
            if (managedRule) continue;
            const QString outbound = rule.value(QStringLiteral("outbound")).isString()
                ? rule.value(QStringLiteral("outbound")).toString()
                : Configs::outboundIDToString(rule.value(QStringLiteral("outbound")).toInt());
            const QString title = rule.value(QStringLiteral("name")).toString().isEmpty()
                ? tr("Правило %1 · %2").arg(index + 1).arg(outbound)
                : tr("%1 · %2").arg(rule.value(QStringLiteral("name")).toString(), outbound);
            const QString details = QString::fromUtf8(QJsonDocument(rule).toJson(QJsonDocument::Indented));
            bool hasSelector = false;
            for (auto it = selectorKinds.cbegin(); it != selectorKinds.cend(); ++it) {
                const QJsonValue fieldValue = rule.value(it.key());
                const QJsonArray values = fieldValue.isArray() ? fieldValue.toArray()
                    : (fieldValue.isString() ? QJsonArray{fieldValue} : QJsonArray{});
                for (const auto& value : values) {
                    if (!value.isString() || value.toString().isEmpty()) continue;
                    const QString groupName = selectorGroup(it.value(), it.key(), value.toString());
                    auto* group = addGroup(it.value(), QStringLiteral("existing"), groupName);
                    auto* item = new QTreeWidgetItem(group,
                        {tr("%1 · %2 [%3]").arg(value.toString(), title, it.key())});
                    item->setData(0, Qt::UserRole + 1, QStringLiteral("existing"));
                    item->setData(0, Qt::UserRole + 2, details);
                    item->setToolTip(0, tr("Часть существующего правила. Полное правило показано ниже."));
                    hasSelector = true;
                }
            }
            if (!hasSelector) {
                auto* item = new QTreeWidgetItem(addGroup(QStringLiteral("other"),
                    QStringLiteral("existing"), tr("Остальные условия")), {title});
                item->setData(0, Qt::UserRole + 1, QStringLiteral("existing"));
                item->setData(0, Qt::UserRole + 2, details);
            }
        }
    }
    for (auto* category : {sites, apps, ips, others}) {
        if (category->childCount() == 0) delete managed_->takeTopLevelItem(managed_->indexOfTopLevelItem(category));
        else category->setExpanded(true);
    }
}

void QuickRouteDialog::refreshTarget() {
    const auto target = Configs::QuickRoute::parseSiteOrIp(address_->text());
    QString message = target.error.isEmpty()
        ? (target.kind == "site" ? tr("Будет добавлен %1 и все его поддомены.").arg(target.value)
                                  : tr("Будет добавлен IP/подсеть %1.").arg(target.value))
        : target.error;
    if (target.error.isEmpty()) {
        const auto profile = Configs::dataManager->routesRepo->GetRouteProfile(
            Configs::dataManager->settingsRepo->current_route_id);
        if (profile) {
            const QString key = target.kind == QStringLiteral("site") ? QStringLiteral("domains") : QStringLiteral("ips");
            QStringList covered;
            if (Configs::QuickRoute::withoutCoveredSelectors(*profile,
                QJsonObject{{QStringLiteral("kind"), target.kind}, {key, QJsonArray{target.value}}},
                &covered).isEmpty())
                message = tr("%1 уже направляется напрямую в профиле «%2». Повтор не добавится.")
                    .arg(target.value, profile->name);
        }
    }
    addressInfo_->setText(message);
    const bool site = target.kind == QStringLiteral("site");
    relatedBox_->setVisible(target.kind != QStringLiteral("ip"));
    extraDomain_->setEnabled(site && !scanning_);
    addDomainButton_->setEnabled(site && !scanning_);
    scanButton_->setEnabled(site || scanning_);
    scanButton_->setText(scanning_ ? tr("Закончить проверку") : tr("Открыть сайт для проверки"));
}

QList<int> QuickRouteDialog::selectedProfiles() const {
    QList<int> result;
    for (int i = 0; i < profiles_->count(); ++i) {
        const auto* item = profiles_->item(i);
        if (item->checkState() == Qt::Checked) result.append(item->data(Qt::UserRole).toInt());
    }
    return result;
}

QJsonObject QuickRouteDialog::draftRecord(QString* error) const {
    QJsonObject record{{"id", editingRecord_.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : editingRecord_.value("id").toString()}};
    if (tabs_->currentIndex() == 0) {
        const auto target = Configs::QuickRoute::parseSiteOrIp(address_->text());
        if (!target.error.isEmpty()) { if (error) *error = target.error; return {}; }
        record["kind"] = target.kind;
        record["display"] = target.value;
        if (target.kind == "site") {
            QStringList domains{target.value};
            for (int i = 0; i < candidates_->count(); ++i) {
                auto* item = candidates_->item(i);
                if (item->checkState() == Qt::Checked) domains.append(item->data(Qt::UserRole).toString());
            }
            domains.removeDuplicates();
            record["domains"] = asArray(domains);
        } else record["ips"] = asArray({target.value});
    } else if (tabs_->currentIndex() == 1) {
        QStringList paths;
        for (auto* tree : {installed_, running_})
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                auto* parent = tree->topLevelItem(i);
                for (int j = 0; j < parent->childCount(); ++j) {
                    auto* child = parent->child(j);
                    if (child->checkState(0) == Qt::Checked) paths.append(child->data(0, Qt::UserRole).toString());
                }
            }
        paths.removeDuplicates();
        if (paths.isEmpty()) { if (error) *error = tr("Выберите приложение или процесс"); return {}; }
        record["kind"] = "app";
        record["display"] = paths.size() == 1 ? QFileInfo(paths.first()).fileName()
                                               : tr("Приложения: %1 exe").arg(paths.size());
        record["paths"] = asArray(paths);
    } else { if (error) *error = tr("Выберите вкладку сайта или приложения"); return {}; }
    return record;
}

void QuickRouteDialog::startScan() {
    const auto target = Configs::QuickRoute::parseSiteOrIp(address_->text());
    if (target.kind != "site") {
        QMessageBox::warning(this, windowTitle(), tr("Для проверки нужна ссылка на сайт."));
        return;
    }
    QUrl url = QUrl::fromUserInput(address_->text());
    if (url.scheme() != "https" && url.scheme() != "http") url = QUrl("https://" + target.display);
    if (!url.isValid()) return;
    const auto current = Configs::dataManager->routesRepo->GetRouteProfile(
        Configs::dataManager->settingsRepo->current_route_id);
    if (current && Configs::QuickRoute::withoutCoveredSelectors(*current,
        QJsonObject{{"kind", "site"}, {"domains", QJsonArray{target.value}}}).isEmpty()) {
        scanning_ = true;
        address_->setEnabled(false);
        if (!QDesktopServices::openUrl(url)) {
            scanning_ = false;
            address_->setEnabled(true);
            QMessageBox::warning(this, windowTitle(), tr("Не удалось открыть сайт в браузере"));
        } else scanInfo_->setText(tr("Сайт открыт. Запустите захват через расширение Chrome в этой вкладке, воспроизведите проблему и передайте результат в Throne."));
        refreshTarget();
        return;
    }
    QString error;
    const QJsonObject preview{{"kind", "site"}, {"domains", QJsonArray{target.value}}};
    if (!beginPreview_(preview, &error)) {
        QMessageBox::warning(this, windowTitle(), error.isEmpty() ? tr("Не удалось включить проверку") : error);
        return;
    }
    previewActive_ = true;
    scanning_ = true;
    address_->setEnabled(false);
    scanStartedAtMs_ = QDateTime::currentMSecsSinceEpoch();
    scanInfo_->setText(tr("Применяем временное правило и открываем сайт. После открытия запустите захват через расширение Chrome."));
    refreshCandidateView();
    previewTimer_->disconnect(this);
    connect(previewTimer_, &QTimer::timeout, this, [this, url] {
        if (!scanning_) { previewTimer_->stop(); return; }
        if (previewReady_()) {
            previewTimer_->stop();
            if (!QDesktopServices::openUrl(url)) {
                stopScan();
                QMessageBox::warning(this, windowTitle(), tr("Не удалось открыть сайт в браузере"));
                return;
            }
            scanInfo_->setText(tr("Сайт открыт. Запустите захват через расширение Chrome, затем воспроизведите проблему и передайте результат."));
        } else if (QDateTime::currentMSecsSinceEpoch() - scanStartedAtMs_ > 20000) {
            previewTimer_->stop();
            stopScan();
            QMessageBox::warning(this, windowTitle(), tr("Временное правило не запустилось за 20 секунд. Проверьте журнал Throne."));
        }
    });
    previewTimer_->start();
}

void QuickRouteDialog::stopScan() {
    previewTimer_->stop();
    scanning_ = false;
    address_->setEnabled(true);
    if (previewActive_) { finishPreview_(); previewActive_ = false; }
    scanInfo_->setText(tr("Проверка завершена. Добавьте домен вручную либо разберите захват расширения или HAR нужной вкладки."));
    refreshCandidateView();
    refreshTarget();
}

void QuickRouteDialog::updateProfileSelection() {
    // The checkbox changes future persistence; current checks remain visible and editable.
}

bool QuickRouteDialog::saveManaged(const QJsonObject& record, bool remove, QString* error) {
    lastSkippedSelectors_.clear();
    const auto& settings = Configs::dataManager->settingsRepo;
    if (settings->noSave) {
        if (error) *error = tr("Сейчас нельзя сохранить настройки маршрутизации");
        return false;
    }
    auto records = Configs::QuickRoute::readRecords(settings->quick_route_records);
    QList<std::shared_ptr<Configs::RouteProfile>> changed;
    QJsonObject stored = record;
    const QList<int> ids = remove ? [&] {
        QList<int> result;
        for (const auto& id : record.value("profiles").toArray()) result.append(id.toInt());
        return result;
    }() : selectedProfiles();
    if (ids.isEmpty()) { if (error) *error = tr("Выберите хотя бы один профиль"); return false; }
    QList<int> oldIds;
    if (!remove && !editingRecord_.isEmpty())
        for (const auto& id : editingRecord_.value("profiles").toArray()) oldIds.append(id.toInt());
    QList<int> allIds = ids;
    for (const int id : oldIds) if (!allIds.contains(id)) allIds.append(id);
    QList<int> committedIds;
    for (const int id : allIds) {
        const auto source = Configs::dataManager->routesRepo->GetRouteProfile(id);
        if (!source) { if (error) *error = tr("Профиль %1 не найден").arg(id); return false; }
        auto target = std::make_shared<Configs::RouteProfile>(*source);
        if (!remove && ids.contains(id) && target->isRemote) {
            target->id = -1;
            target->name += tr(" (локальная копия)");
            target->isRemote = false;
            target->remoteURL.clear();
            target->autoUpdate = false;
            target->remoteLastUpdate = 0;
        }
        if (remove && !Configs::QuickRoute::removeRule(*target, record, error)) return false;
        if (!remove && oldIds.contains(id) &&
            !Configs::QuickRoute::removeRule(*target, editingRecord_, error)) return false;
        if (!remove && ids.contains(id)) {
            QStringList covered;
            const QJsonObject cleaned = Configs::QuickRoute::withoutCoveredSelectors(*target, record, &covered);
            if (cleaned.isEmpty()) {
                if (error) *error = tr("В профиле «%1» это исключение уже есть: %2. Новое правило не добавлено.")
                    .arg(source->name, covered.join(QStringLiteral(", ")));
                return false;
            }
            if (!committedIds.isEmpty() && Configs::QuickRoute::directRule(stored)
                                            != Configs::QuickRoute::directRule(cleaned)) {
                if (error) *error = tr("В выбранных профилях уже покрыты разные адреса. Сохраните исключение по одному профилю.");
                return false;
            }
            stored = cleaned;
            for (const auto& value : covered)
                if (!lastSkippedSelectors_.contains(value)) lastSkippedSelectors_.append(value);
            if (!Configs::QuickRoute::insertRule(*target, cleaned, error)) return false;
        }
        if (validate_ && !validate_(target, error)) return false;
        changed.append(target);
        if (ids.contains(id)) committedIds.append(changed.size() - 1);
    }
    QString savedRecords;
    QStringList savedProfileIds = settings->quick_route_profile_ids;
    bool savedRemember = settings->quick_route_remember_profiles;
    int savedCurrentRoute = settings->current_route_id;
    const auto prepareSettings = [&](const QList<std::shared_ptr<Configs::RouteProfile>>& saved) {
        auto updated = records;
        if (remove) {
            updated.erase(std::remove_if(updated.begin(), updated.end(), [&](const auto& old) {
                return old.value("id") == record.value("id");
            }), updated.end());
        } else {
            if (!editingRecord_.isEmpty()) {
                updated.erase(std::remove_if(updated.begin(), updated.end(), [&](const auto& old) {
                    return old.value("id") == editingRecord_.value("id");
                }), updated.end());
            }
            QJsonArray profileIds;
            for (const int index : committedIds) profileIds.append(saved.at(index)->id);
            stored["profiles"] = profileIds;
            updated.append(stored);
            if (rememberProfiles_->isChecked()) {
                savedProfileIds.clear();
                for (const int index : committedIds)
                    savedProfileIds.append(QString::number(saved.at(index)->id));
            }
            savedRemember = rememberProfiles_->isChecked();
            for (const int index : committedIds)
                if (allIds.at(index) == savedCurrentRoute && saved.at(index)->id != allIds.at(index))
                    savedCurrentRoute = saved.at(index)->id;
        }
        savedRecords = Configs::QuickRoute::writeRecords(updated);
        return std::vector<std::pair<std::string, std::string>>{
            {"quick_route_records", savedRecords.toStdString()},
            {"quick_route_profile_ids", QString::fromUtf8(QJsonDocument(asArray(savedProfileIds))
                .toJson(QJsonDocument::Compact)).toStdString()},
            {"quick_route_remember_profiles", savedRemember ? "true" : "false"},
            {"current_route_id", QString::number(savedCurrentRoute).toStdString()}
        };
    };
    if (!Configs::dataManager->routesRepo->SaveBatch(changed, error, prepareSettings)) return false;
    settings->quick_route_records = savedRecords;
    settings->quick_route_profile_ids = savedProfileIds;
    settings->quick_route_remember_profiles = savedRemember;
    settings->current_route_id = savedCurrentRoute;
    populateManaged();
    populateProfiles();
    editingRecord_ = {};
    return true;
}

void QuickRouteDialog::saveException() {
    if (scanning_) stopScan();
    QString error;
    QJsonObject record = draftRecord(&error);
    if (record.isEmpty()) { QMessageBox::warning(this, windowTitle(), error); return; }
    const int previousActive = Configs::dataManager->settingsRepo->current_route_id;
    bool oldActive = false;
    for (const auto& id : editingRecord_.value("profiles").toArray()) oldActive |= id.toInt() == previousActive;
    const bool newActive = selectedProfiles().contains(previousActive);
    if (!saveManaged(record, false, &error)) { QMessageBox::warning(this, windowTitle(), error); return; }
    const bool activeChanged = oldActive || newActive
        || Configs::dataManager->settingsRepo->current_route_id != previousActive;
    saved_(activeChanged);
    QMessageBox::information(this, windowTitle(), lastSkippedSelectors_.isEmpty()
        ? tr("Исключение сохранено. Его можно удалить на вкладке «Добавленные».")
        : tr("Исключение сохранено. Уже покрытые адреса не добавлялись повторно: %1")
              .arg(lastSkippedSelectors_.join(QStringLiteral(", "))));
}

void QuickRouteDialog::editException() {
    if (!managed_->currentItem() ||
        managed_->currentItem()->data(0, Qt::UserRole + 1).toString() != QStringLiteral("managed")) return;
    const QString id = managed_->currentItem()->data(0, Qt::UserRole).toString();
    for (const auto& record : Configs::QuickRoute::readRecords(Configs::dataManager->settingsRepo->quick_route_records)) {
        if (record.value("id").toString() != id) continue;
        editingRecord_ = record;
        for (int i = 0; i < profiles_->count(); ++i) {
            auto* item = profiles_->item(i);
            bool selected = false;
            for (const auto& profile : record.value("profiles").toArray())
                selected |= profile.toInt() == item->data(Qt::UserRole).toInt();
            item->setCheckState(selected ? Qt::Checked : Qt::Unchecked);
        }
        if (record.value("kind") == "app") {
            populateApplications();
            const QJsonArray paths = record.value("paths").toArray();
            QSet<QString> wanted;
            QSet<QString> appFolders;
            for (const auto& path : paths) {
                wanted.insert(path.toString());
                appFolders.insert(QFileInfo(path.toString()).absolutePath().toCaseFolded());
            }
            for (auto* tree : {installed_, running_})
                for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                    auto* parent = tree->topLevelItem(i);
                    for (int j = 0; j < parent->childCount(); ++j) {
                        auto* child = parent->child(j);
                        const QString path = child->data(0, Qt::UserRole).toString();
                        child->setCheckState(0, wanted.contains(path)
                            ? Qt::Checked : Qt::Unchecked);
                        if (!wanted.contains(path) &&
                            appFolders.contains(QFileInfo(path).absolutePath().toCaseFolded())) {
                            child->setText(0, tr("%1 · новый exe").arg(QFileInfo(path).fileName()));
                            child->setToolTip(0, tr("Этот процесс появился после создания исключения. Отметьте его, чтобы добавить."));
                        }
                    }
                }
            tabs_->setCurrentIndex(1);
        } else {
            address_->setText(record.value("kind") == "site"
                ? record.value("display").toString()
                : (record.value("ips").toArray().isEmpty() ? QString{}
                   : record.value("ips").toArray().at(0).toString()));
            candidates_->clear();
            const auto domains = record.value("domains").toArray();
            for (int i = 1; i < domains.size(); ++i) {
                const QString domain = domains.at(i).toString();
                auto* item = new QListWidgetItem(domain, candidates_);
                item->setData(Qt::UserRole, domain);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Checked);
            }
            tabs_->setCurrentIndex(0);
        }
        return;
    }
}

void QuickRouteDialog::removeException() {
    if (!managed_->currentItem() ||
        managed_->currentItem()->data(0, Qt::UserRole + 1).toString() != QStringLiteral("managed")) return;
    const QString id = managed_->currentItem()->data(0, Qt::UserRole).toString();
    const auto records = Configs::QuickRoute::readRecords(Configs::dataManager->settingsRepo->quick_route_records);
    for (const auto& record : records) {
        if (record.value("id").toString() != id) continue;
        QString error;
        const int active = Configs::dataManager->settingsRepo->current_route_id;
        bool affectsActive = false;
        for (const auto& profile : record.value("profiles").toArray()) affectsActive |= profile.toInt() == active;
        if (!saveManaged(record, true, &error)) QMessageBox::warning(this, windowTitle(), error);
        else saved_(affectsActive);
        return;
    }
}
