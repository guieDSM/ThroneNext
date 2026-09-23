#pragma once

#include "Database.h"
#include <QString>
#include <QList>
#include <mutex>
#include <string>

namespace Configs {
    // One time-bucketed usage row. bucket_start is a unix epoch second aligned
    // to its tier (minute tier = multiple of 60, hour tier = multiple of 3600).
    struct ConfigTrafficRow {
        long long bucket_start = 0;
        int profile_id = 0;
        long long up = 0;
        long long down = 0;
    };

    struct AppTrafficRow {
        long long bucket_start = 0;
        QString process_name;
        long long up = 0;
        long long down = 0;
    };

    // Aggregated usage over a queried window (summed across both tiers).
    struct ConfigUsage {
        int profile_id = 0;
        long long up = 0;
        long long down = 0;
    };

    struct AppUsage {
        QString process_name;
        long long up = 0;
        long long down = 0;
    };

    // One point of a time series: total up/down across all configs (or apps) in
    // the bucket that starts at bucket_start (epoch secs, aligned to the queried
    // bucket size). Used to drive the dashboard chart.
    struct TrafficSeriesPoint {
        long long bucket_start = 0;
        long long up = 0;
        long long down = 0;
    };

    // Reference metadata, kept so deleted/renamed configs and moved apps still
    // resolve to something meaningful in the dashboard.
    struct ConfigMetaRow {
        int profile_id = 0;
        QString name;
        QString group_name;
        QString type;
        QString server_address;
        long long first_seen = 0;
        long long last_seen = 0;
    };

    struct AppMetaRow {
        QString process_name;
        QString last_path;
        long long first_seen = 0;
        long long last_seen = 0;
    };

    // Owns the separate traffic-statistics database (throne_stats.db): a tiered
    // per-config / per-app time series plus reference metadata. Kept apart from
    // the main database so its write volume never contends with vital profile
    // operations.
    //
    // Every public method is internally serialized by `mu`, so a single shared
    // instance is safe to call concurrently from the traffic-looper thread, the
    // background rollup thread, and the UI thread.
    class TrafficStatsRepo {
    public:
        explicit TrafficStatsRepo(Database& database);

        // --- writes: upsert-add into the minute (fine) tier ---
        void UpsertConfigMinuteBatch(const QList<ConfigTrafficRow>& rows);
        void UpsertAppMinuteBatch(const QList<AppTrafficRow>& rows);

        // --- reference metadata ---
        void UpsertConfigMeta(const ConfigMetaRow& meta);
        void UpsertAppMeta(const QString& processName, const QString& lastPath, long long nowSecs);

        // --- maintenance ---
        // Aggregate every minute-tier row with bucket_start < olderThanSecs into
        // its hour bucket, then drop those minute rows. Atomic per call, so a
        // crash never double-counts.
        void RollupMinuteToHour(long long olderThanSecs);
        // Drop hour-tier rows older than the retention cutoff.
        void PruneHour(long long olderThanSecs);

        // --- reads: sum across both tiers over [fromSecs, toSecs) ---
        QList<ConfigUsage> QueryConfigUsage(long long fromSecs, long long toSecs);
        QList<AppUsage> QueryAppUsage(long long fromSecs, long long toSecs);
        QList<ConfigMetaRow> GetAllConfigMeta();
        QList<AppMetaRow> GetAllAppMeta();

        // --- reads: time series totalled across configs/apps over [fromSecs,
        // toSecs), grouped into buckets of bucketSecs (e.g. 3600 for hourly,
        // 86400 for daily). Buckets with no traffic are omitted; the caller fills
        // gaps. Ordered by bucket_start ascending. Rows are stored on UTC-aligned
        // boundaries; utcOffsetSecs (the viewer's offset east of UTC) shifts the
        // grouping so a "day"/"hour" bucket lands on the local calendar boundary,
        // and the returned bucket_start is the epoch of that local boundary. ---
        QList<TrafficSeriesPoint> QueryConfigSeries(long long fromSecs, long long toSecs, long long bucketSecs, long long utcOffsetSecs);
        QList<TrafficSeriesPoint> QueryAppSeries(long long fromSecs, long long toSecs, long long bucketSecs, long long utcOffsetSecs);

    private:
        Database& db;
        std::mutex mu;

        void createTables() const;
        // SQL fragment computing a row's local-aligned bucket_start (see the
        // QuerySeries doc above). A member rather than a file-local helper so the
        // unity build can't collide its symbol with another translation unit's.
        static std::string bucketExpr(long long bucketSecs, long long utcOffsetSecs);
    };
}
