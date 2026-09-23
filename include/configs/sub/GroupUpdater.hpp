#pragma once

#include <include/database/entities/Profile.h>

#include <QStringList>

namespace Subscription {
    enum class SingBoxSubType {
        fullConfig,
        outboundInJson,
        outboundJsonArray,
        outboundObject,
        invalid,
    };
    enum class XraySubType {
        outboundInJson,
        outboundJsonArray,
        outboundObject,
        configJsonArray,
        invalid,
    };
    class RawUpdater {
    public:
        void update(const QString &str, bool needParse = true, bool isBase64Decoded = false);

        void updateSingBox(const QJsonDocument &doc, SingBoxSubType type);

        void updateXray(const QJsonDocument &doc, XraySubType type);

        void updateClash(const QString &str);

        void updateWireguardFileConfig(const QString &str);

        void updateSIP008(const QString &str);

        int gid_add_to = -1;

        QList<std::shared_ptr<Configs::Profile>> updated_order;
    };

    class GroupUpdater : public QObject {
        Q_OBJECT

    public:
        // showDiff: pop up the added/removed profile diff when a single existing
        // group is refreshed by hand (manual "Update subscription"). Automatic
        // paths (update-all, auto-update, imports) leave it false and only log.
        void AsyncUpdate(const QString &str, int _sub_gid = -1, const std::function<void()> &finish = nullptr, bool showDiff = false, bool background = false);

        // Import several independent payloads (a multi-file selection or drop) at
        // once. Each payload keeps its own format detection, but they share one
        // RawUpdater pass so the user gets a single import result instead of one
        // per file, and the files are not raced against each other on N threads.
        void AsyncImportBatch(const QStringList &payloads, const std::function<void()> &finish = nullptr);

        void Update(const QString &_str, int _sub_gid = -1, bool _not_sub_as_url = false, bool showDiff = false, bool background = false);

    signals:

        void asyncUpdateCallback(int gid);
    };

    extern GroupUpdater *groupUpdater;
} // namespace Subscription

void UI_update_all_groups(bool onlyAllowed = false, bool onlyDue = false);
