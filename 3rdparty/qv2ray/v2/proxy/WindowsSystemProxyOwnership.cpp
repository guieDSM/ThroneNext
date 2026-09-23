#include "WindowsSystemProxyOwnership.hpp"

#ifdef Q_OS_WIN

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <ras.h>
#include <raserror.h>
#include <wininet.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStringList>
#include <QUuid>

#include <iterator>
#include <cmath>
#include <limits>
#include <vector>

#include "SystemProxyOwnershipPolicy.hpp"
#include "include/global/Configs.hpp"
#include "include/global/Logger.hpp"

namespace Qv2ray::components::proxy {
    namespace {
        using ownership::State;

        constexpr int JournalVersion = 2;
        constexpr auto JournalFileName = "system-proxy-ownership.json";
        constexpr auto OwnershipMutexName = L"Local\\Throne.SystemProxyOwnership.v2";
        constexpr DWORD OwnershipMutexTimeoutMs = 5000;

        struct JournalEntry {
            QString connection;
            State before;
            State applied;
        };

        struct Journal {
            qint64 ownerPid = 0;
            ownership::ProcessIdentity ownerIdentity;
            QList<JournalEntry> entries;
        };

        enum class ProcessInspection {
            NotRunning,
            RunningKnown,
            RunningUnknown,
        };

        class InterprocessProxyLock {
        public:
            InterprocessProxyLock() {
                handle_ = CreateMutexW(nullptr, FALSE, OwnershipMutexName);
                if (handle_ == nullptr) {
                    LOG_ERROR(QString("Cannot create the system proxy ownership mutex (Win32 %1).")
                                  .arg(GetLastError()));
                    return;
                }

                const auto status = WaitForSingleObject(handle_, OwnershipMutexTimeoutMs);
                if (status == WAIT_OBJECT_0 || status == WAIT_ABANDONED) {
                    locked_ = true;
                    if (status == WAIT_ABANDONED) {
                        LOG_WARN("Previous system proxy mutation ended unexpectedly; ownership recovery will run first.");
                    }
                    return;
                }

                LOG_ERROR(QString("Timed out waiting for exclusive system proxy ownership (Win32 %1).")
                              .arg(status == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT));
            }

            InterprocessProxyLock(const InterprocessProxyLock &) = delete;
            InterprocessProxyLock &operator=(const InterprocessProxyLock &) = delete;

            ~InterprocessProxyLock() {
                if (locked_) ReleaseMutex(handle_);
                if (handle_ != nullptr) CloseHandle(handle_);
            }

            [[nodiscard]] bool locked() const { return locked_; }

        private:
            HANDLE handle_ = nullptr;
            bool locked_ = false;
        };

        QMutex proxyMutex;

        QString connectionLabel(const QString &connection) {
            return connection.isEmpty() ? QStringLiteral("LAN") : connection;
        }

        QString journalPath() {
            // main.cpp changes the working directory to the active config
            // directory after processing -appdata. Configs::GetBasePath()
            // deliberately describes the asset root and does not preserve an
            // explicit portable -appdata directory, so using it here made the
            // ownership journal point at an unrelated (and often missing)
            // AppData path. Keep recovery state beside the database it owns.
            return QDir::current().filePath(QString::fromLatin1(JournalFileName));
        }

        std::wstring normalizeImagePath(const QString &path) {
            return QDir::toNativeSeparators(QDir::cleanPath(path)).toCaseFolded().toStdWString();
        }

        ProcessInspection inspectProcess(qint64 pid, ownership::ProcessIdentity *identity) {
            if (identity != nullptr) *identity = {};
            if (pid <= 0 || static_cast<quint64>(pid) > std::numeric_limits<DWORD>::max()) {
                return ProcessInspection::NotRunning;
            }

            const HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                               FALSE, static_cast<DWORD>(pid));
            if (process == nullptr) {
                // ERROR_INVALID_PARAMETER is the documented result for a PID
                // that no longer exists. Every other failure is ambiguous and
                // must fail closed rather than stealing live ownership.
                return GetLastError() == ERROR_INVALID_PARAMETER
                           ? ProcessInspection::NotRunning
                           : ProcessInspection::RunningUnknown;
            }

            const auto waitStatus = WaitForSingleObject(process, 0);
            if (waitStatus == WAIT_OBJECT_0) {
                CloseHandle(process);
                return ProcessInspection::NotRunning;
            }
            if (waitStatus != WAIT_TIMEOUT) {
                CloseHandle(process);
                return ProcessInspection::RunningUnknown;
            }

            FILETIME created{}, exited{}, kernel{}, user{};
            std::vector<wchar_t> image(32768);
            DWORD imageLength = static_cast<DWORD>(image.size());
            if (!GetProcessTimes(process, &created, &exited, &kernel, &user)
                || !QueryFullProcessImageNameW(process, 0, image.data(), &imageLength)) {
                CloseHandle(process);
                return ProcessInspection::RunningUnknown;
            }
            CloseHandle(process);

            ULARGE_INTEGER createdValue{};
            createdValue.LowPart = created.dwLowDateTime;
            createdValue.HighPart = created.dwHighDateTime;
            if (identity != nullptr) {
                identity->created100ns = createdValue.QuadPart;
                identity->normalizedImagePath = normalizeImagePath(
                    QString::fromWCharArray(image.data(), static_cast<qsizetype>(imageLength)));
            }
            return ProcessInspection::RunningKnown;
        }

        void freeOptionString(INTERNET_PER_CONN_OPTION &option) {
            if (option.Value.pszValue != nullptr) {
                GlobalFree(option.Value.pszValue);
                option.Value.pszValue = nullptr;
            }
        }

        bool queryState(const QString &connection, State *state) {
            if (state == nullptr) return false;
            *state = {};

            INTERNET_PER_CONN_OPTION options[5]{};
            options[0].dwOption = INTERNET_PER_CONN_FLAGS;
            options[1].dwOption = INTERNET_PER_CONN_AUTODISCOVERY_FLAGS;
            options[2].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
            options[3].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
            options[4].dwOption = INTERNET_PER_CONN_PROXY_SERVER;

            auto connectionStorage = connection.toStdWString();
            INTERNET_PER_CONN_OPTION_LIST list{};
            list.dwSize = sizeof(list);
            list.pszConnection = connection.isEmpty() ? nullptr : connectionStorage.data();
            list.dwOptionCount = static_cast<DWORD>(std::size(options));
            list.pOptions = options;

            DWORD size = sizeof(list);
            if (!InternetQueryOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, &size)) {
                LOG_ERROR(QString("System proxy query failed for %1 (Win32 %2)")
                              .arg(connectionLabel(connection))
                              .arg(GetLastError()));
                freeOptionString(options[2]);
                freeOptionString(options[3]);
                freeOptionString(options[4]);
                return false;
            }

            state->flags = options[0].Value.dwValue;
            state->autodiscoveryFlags = options[1].Value.dwValue;
            if (options[2].Value.pszValue != nullptr) state->autoConfigUrl = options[2].Value.pszValue;
            if (options[3].Value.pszValue != nullptr) state->bypass = options[3].Value.pszValue;
            if (options[4].Value.pszValue != nullptr) state->server = options[4].Value.pszValue;

            freeOptionString(options[2]);
            freeOptionString(options[3]);
            freeOptionString(options[4]);
            return true;
        }

        bool setState(const QString &connection, const State &state) {
            auto autoConfigUrl = state.autoConfigUrl;
            auto bypass = state.bypass;
            auto server = state.server;
            auto connectionStorage = connection.toStdWString();

            INTERNET_PER_CONN_OPTION options[5]{};
            options[0].dwOption = INTERNET_PER_CONN_FLAGS;
            options[0].Value.dwValue = state.flags;
            options[1].dwOption = INTERNET_PER_CONN_AUTODISCOVERY_FLAGS;
            options[1].Value.dwValue = state.autodiscoveryFlags;
            options[2].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
            options[2].Value.pszValue = autoConfigUrl.data();
            options[3].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
            options[3].Value.pszValue = bypass.data();
            options[4].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
            options[4].Value.pszValue = server.data();

            INTERNET_PER_CONN_OPTION_LIST list{};
            list.dwSize = sizeof(list);
            list.pszConnection = connection.isEmpty() ? nullptr : connectionStorage.data();
            list.dwOptionCount = static_cast<DWORD>(std::size(options));
            list.pOptions = options;

            if (!InternetSetOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, sizeof(list))) {
                LOG_ERROR(QString("System proxy update failed for %1 (Win32 %2)")
                              .arg(connectionLabel(connection))
                              .arg(GetLastError()));
                return false;
            }
            return true;
        }

        void notifyProxyChanged() {
            InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
            InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
        }

        QStringList connectionNames() {
            QStringList result{QString()};

            DWORD bufferSize = sizeof(RASENTRYNAMEW);
            DWORD count = 0;
            std::vector<RASENTRYNAMEW> entries(1);
            entries.front().dwSize = sizeof(RASENTRYNAMEW);

            auto status = RasEnumEntriesW(nullptr, nullptr, entries.data(), &bufferSize, &count);
            if (status == ERROR_BUFFER_TOO_SMALL) {
                const auto capacity = (bufferSize + sizeof(RASENTRYNAMEW) - 1) / sizeof(RASENTRYNAMEW);
                entries.clear();
                entries.resize(capacity);
                for (auto &entry : entries) entry.dwSize = sizeof(RASENTRYNAMEW);
                status = RasEnumEntriesW(nullptr, nullptr, entries.data(), &bufferSize, &count);
            }

            if (status != ERROR_SUCCESS) {
                LOG_WARN(QString("Could not enumerate RAS proxy connections (Win32 %1); LAN remains protected.")
                             .arg(status));
                return result;
            }

            for (DWORD i = 0; i < count; ++i) {
                const auto name = QString::fromWCharArray(entries[i].szEntryName);
                if (!name.isEmpty() && !result.contains(name)) result.append(name);
            }
            return result;
        }

        QJsonObject stateToJson(const State &state) {
            return {
                {"flags", static_cast<qint64>(state.flags)},
                {"autodiscovery_flags", static_cast<qint64>(state.autodiscoveryFlags)},
                {"auto_config_url", QString::fromStdWString(state.autoConfigUrl)},
                {"bypass", QString::fromStdWString(state.bypass)},
                {"server", QString::fromStdWString(state.server)},
            };
        }

        bool stateFromJson(const QJsonValue &value, State *state) {
            if (state == nullptr || !value.isObject()) return false;
            const auto object = value.toObject();
            const auto flags = object.value("flags");
            const auto autodiscoveryFlags = object.value("autodiscovery_flags");
            if (!flags.isDouble() || !autodiscoveryFlags.isDouble()
                || !object.value("auto_config_url").isString()
                || !object.value("bypass").isString()
                || !object.value("server").isString()) {
                return false;
            }

            const auto rawFlags = flags.toDouble(-1);
            const auto rawAutodiscoveryFlags = autodiscoveryFlags.toDouble(-1);
            if (rawFlags < 0 || rawFlags > UINT32_MAX
                || rawAutodiscoveryFlags < 0 || rawAutodiscoveryFlags > UINT32_MAX) {
                return false;
            }

            state->flags = static_cast<std::uint32_t>(rawFlags);
            state->autodiscoveryFlags = static_cast<std::uint32_t>(rawAutodiscoveryFlags);
            state->autoConfigUrl = object.value("auto_config_url").toString().toStdWString();
            state->bypass = object.value("bypass").toString().toStdWString();
            state->server = object.value("server").toString().toStdWString();
            return true;
        }

        bool writeJournal(const Journal &journal) {
            QJsonArray entries;
            for (const auto &entry : journal.entries) {
                entries.append(QJsonObject{
                    {"connection", entry.connection},
                    {"before", stateToJson(entry.before)},
                    {"applied", stateToJson(entry.applied)},
                });
            }

            const QJsonObject root{
                {"version", JournalVersion},
                {"owner_pid", static_cast<double>(journal.ownerPid)},
                {"owner_created_100ns", QString::number(journal.ownerIdentity.created100ns)},
                {"owner_image", QString::fromStdWString(journal.ownerIdentity.normalizedImagePath)},
                {"owner_token", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                {"created_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                {"entries", entries},
            };

            const auto path = journalPath();
            const auto directory = QFileInfo(path).absoluteDir();
            if (!directory.exists() && !QDir().mkpath(directory.absolutePath())) {
                LOG_ERROR("Refusing to change the system proxy because the ownership journal directory cannot be created: "
                          + QDir::toNativeSeparators(directory.absolutePath()));
                return false;
            }

            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly)) {
                LOG_ERROR("Refusing to change the system proxy because the ownership journal cannot be created: "
                          + QDir::toNativeSeparators(path) + ": " + file.errorString());
                return false;
            }
            file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            if (file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0 || !file.commit()) {
                LOG_ERROR("Refusing to change the system proxy because the ownership journal cannot be committed: "
                          + file.errorString());
                return false;
            }
            return true;
        }

        bool readJournal(Journal *journal) {
            if (journal == nullptr) return false;
            QFile file(journalPath());
            if (!file.open(QIODevice::ReadOnly)) {
                LOG_ERROR("Cannot open the system proxy ownership journal: " + file.errorString());
                return false;
            }

            QJsonParseError parseError{};
            const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                LOG_ERROR("System proxy ownership journal is invalid; preserving current Windows settings.");
                return false;
            }

            const auto root = document.object();
            const auto version = root.value("version").toInt(-1);
            if ((version != 1 && version != JournalVersion)
                || !root.value("owner_pid").isDouble()
                || !root.value("entries").isArray()) {
                LOG_ERROR("System proxy ownership journal has an unsupported schema; preserving current Windows settings.");
                return false;
            }

            Journal parsed;
            const auto rawOwnerPid = root.value("owner_pid").toDouble(-1);
            if (!std::isfinite(rawOwnerPid) || rawOwnerPid < 1
                || rawOwnerPid > std::numeric_limits<DWORD>::max()
                || rawOwnerPid != std::floor(rawOwnerPid)) {
                LOG_ERROR("System proxy ownership journal contains an invalid owner PID.");
                return false;
            }
            parsed.ownerPid = static_cast<qint64>(rawOwnerPid);
            if (version == JournalVersion) {
                const auto created = root.value("owner_created_100ns");
                const auto image = root.value("owner_image");
                if (!created.isString() || !image.isString() || image.toString().isEmpty()) {
                    LOG_ERROR("System proxy ownership journal contains an invalid process identity.");
                    return false;
                }
                bool createdOk = false;
                parsed.ownerIdentity.created100ns = created.toString().toULongLong(&createdOk);
                parsed.ownerIdentity.normalizedImagePath = image.toString().toStdWString();
                if (!createdOk || parsed.ownerIdentity.created100ns == 0
                    || parsed.ownerIdentity.normalizedImagePath.empty()) {
                    LOG_ERROR("System proxy ownership journal contains an invalid process identity.");
                    return false;
                }
            }
            for (const auto &value : root.value("entries").toArray()) {
                if (!value.isObject()) return false;
                const auto object = value.toObject();
                if (!object.value("connection").isString()) return false;

                JournalEntry entry;
                entry.connection = object.value("connection").toString();
                if (!stateFromJson(object.value("before"), &entry.before)
                    || !stateFromJson(object.value("applied"), &entry.applied)) {
                    LOG_ERROR("System proxy ownership journal contains an invalid state; preserving current Windows settings.");
                    return false;
                }
                parsed.entries.append(entry);
            }
            if (parsed.entries.isEmpty()) return false;

            *journal = parsed;
            return true;
        }

        bool recoverUnlocked() {
            const auto path = journalPath();
            if (!QFileInfo::exists(path)) return true;

            Journal journal;
            if (!readJournal(&journal)) return false;

            const auto currentPid = QCoreApplication::applicationPid();
            if (journal.ownerPid != currentPid) {
                ownership::ProcessIdentity actualIdentity;
                const auto ownerState = inspectProcess(journal.ownerPid, &actualIdentity);
                if (ownerState == ProcessInspection::RunningUnknown) {
                    LOG_WARN(QString("Cannot verify the live owner of the system proxy journal (PID %1); recovery skipped.")
                                 .arg(journal.ownerPid));
                    return false;
                }
                if (ownerState == ProcessInspection::RunningKnown) {
                    // V1 journals have no immutable identity, so a live PID is
                    // deliberately treated as the owner. V2 distinguishes the
                    // real owner from an unrelated process that reused its PID.
                    if (journal.ownerIdentity.created100ns == 0
                        || ownership::IsSameProcess(journal.ownerIdentity, actualIdentity)) {
                        LOG_WARN(QString("System proxy is owned by a live Throne process (PID %1); recovery skipped.")
                                     .arg(journal.ownerPid));
                        return false;
                    }
                    LOG_WARN(QString("System proxy journal PID %1 was reused by another process; recovering the stale journal.")
                                 .arg(journal.ownerPid));
                }
            }

            const auto currentConnections = connectionNames();
            bool success = true;
            bool changed = false;
            for (const auto &entry : journal.entries) {
                if (!entry.connection.isEmpty() && !currentConnections.contains(entry.connection)) {
                    LOG_INFO("Skipping removed RAS proxy connection: " + entry.connection);
                    continue;
                }

                State current;
                if (!queryState(entry.connection, &current)) {
                    success = false;
                    continue;
                }
                const auto restored = ownership::MergeForRestore(entry.before, entry.applied, current);
                if (restored != current) {
                    if (!setState(entry.connection, restored)) {
                        success = false;
                        continue;
                    }
                    changed = true;
                }
            }

            if (changed) notifyProxyChanged();
            if (!success) {
                LOG_ERROR("System proxy recovery was incomplete; the ownership journal was retained for retry.");
                return false;
            }
            if (!QFile::remove(path)) {
                LOG_ERROR("System proxy was restored but its ownership journal could not be removed.");
                return false;
            }

            LOG_INFO("System proxy settings owned by Throne were restored safely.");
            return true;
        }
    } // namespace

    bool ApplyOwnedSystemProxy(const QString &proxyServer) {
        QMutexLocker lock(&proxyMutex);
        InterprocessProxyLock processLock;
        if (!processLock.locked()) return false;
        if (proxyServer.isEmpty()) {
            LOG_ERROR("Refusing to apply an empty system proxy endpoint.");
            return false;
        }
        if (!recoverUnlocked()) {
            LOG_ERROR("Refusing to replace unresolved system proxy ownership state.");
            return false;
        }

        Journal journal;
        journal.ownerPid = QCoreApplication::applicationPid();
        if (inspectProcess(journal.ownerPid, &journal.ownerIdentity) != ProcessInspection::RunningKnown) {
            LOG_ERROR("Refusing to change the system proxy because this process identity cannot be captured.");
            return false;
        }
        for (const auto &connection : connectionNames()) {
            JournalEntry entry;
            entry.connection = connection;
            if (!queryState(connection, &entry.before)) return false;
            entry.applied = entry.before;
            entry.applied.flags = PROXY_TYPE_DIRECT | PROXY_TYPE_PROXY;
            entry.applied.server = proxyServer.toStdWString();
            journal.entries.append(entry);
        }

        if (!writeJournal(journal)) return false;

        bool success = true;
        for (const auto &entry : journal.entries) {
            if (!setState(entry.connection, entry.applied)) success = false;
        }
        if (success) {
            notifyProxyChanged();
            LOG_INFO("System proxy enabled with a crash-recovery ownership journal.");
            return true;
        }

        LOG_ERROR("System proxy apply was incomplete; rolling back owned fields.");
        recoverUnlocked();
        return false;
    }

    bool HasOwnedSystemProxyJournal() {
        QMutexLocker lock(&proxyMutex);
        return QFileInfo::exists(journalPath());
    }

    bool RecoverLegacySystemProxy(const QString &expectedProxyServer) {
        QMutexLocker lock(&proxyMutex);
        InterprocessProxyLock processLock;
        if (!processLock.locked()) return false;
        if (expectedProxyServer.isEmpty() || QFileInfo::exists(journalPath())) return true;

        bool changed = false;
        bool success = true;
        for (const auto &connection : connectionNames()) {
            State current;
            if (!queryState(connection, &current)) {
                success = false;
                continue;
            }
            if ((current.flags & PROXY_TYPE_PROXY) == 0
                || current.server != expectedProxyServer.toStdWString()) {
                continue;
            }

            // Versions before the ownership journal cannot reconstruct the
            // original state. Claim only the exact loopback endpoint generated
            // from the persisted Throne settings, remove its proxy bit/server,
            // and preserve every PAC, autodetect and bypass value still present.
            current.flags &= ~PROXY_TYPE_PROXY;
            if (current.flags == 0) current.flags = PROXY_TYPE_DIRECT;
            current.server.clear();
            if (!setState(connection, current)) {
                success = false;
                continue;
            }
            changed = true;
        }

        if (changed) {
            notifyProxyChanged();
            LOG_WARN("Recovered a legacy Throne loopback system proxy without overwriting PAC or bypass settings.");
        }
        return success;
    }

    bool RestoreOwnedSystemProxy() {
        QMutexLocker lock(&proxyMutex);
        InterprocessProxyLock processLock;
        if (!processLock.locked()) return false;
        return recoverUnlocked();
    }

    bool RecoverOwnedSystemProxy() {
        QMutexLocker lock(&proxyMutex);
        InterprocessProxyLock processLock;
        if (!processLock.locked()) return false;
        return recoverUnlocked();
    }

} // namespace Qv2ray::components::proxy

#endif // Q_OS_WIN
