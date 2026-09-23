#pragma once
#include <QMutex>
#include <QWaitCondition>
#include <QString>
#include <QHash>
#include <QSet>
#include <QPair>
#include <atomic>

namespace Stats
{
    constexpr int IDKEY = 242315;

    enum ConnectionSort
    {
        Default,
        ByDownload,
        ByUpload,
        ByProcess,
        ByTraffic, // total traffic = upload + download
        ByOutbound,
        ByProtocol,
        ByDownloadSpeed,
        ByUploadSpeed,
        BySpeed // total speed = uploadSpeed + downloadSpeed
    };

    class ConnectionMetadata
    {
        public:
        QString id;
        long long createdAtMs;
        long long upload;
        long long download;
        QString outbound;
        QString network;
        QString dest;
        QString protocol;
        QString domain;
        QString process;     // basename, e.g. chrome.exe
        QString processPath; // full path (icon lookup etc.)
        long long closedAtMs = 0; // 0 while live
        long long uploadSpeed = 0;   // bytes/sec, derived by the lister
        long long downloadSpeed = 0; // bytes/sec, derived by the lister
    };

    class ConnectionLister
    {
    public:
        ConnectionLister();

        bool suspend = true;

        void Loop();

        void ForceUpdate();

        // Tell the lister whether its connections view is currently visible. While
        // it is, the loop polls at 1 Hz; otherwise it relaxes to a slower cadence
        // (off-view polling only feeds the per-app traffic stats). Switching to
        // visible wakes the loop immediately so the table starts refreshing at once.
        void SetInView(bool inView);

        void stopLoop();

        void setSort(ConnectionSort newSort);

        ConnectionSort getSort() const { return sort; }

    private:
        void update();

        // Last byte/time sample per live connection id, used to derive an
        // instantaneous up/down rate by diffing cumulative counters. Self-prunes
        // each poll (rebuilt from the current active set) so it stays bounded.
        struct SpeedSample
        {
            qint64 upload = 0;
            qint64 download = 0;
            qint64 sampledAtMs = 0;
            qint64 upSpeed = 0;
            qint64 downSpeed = 0;
        };
        QHash<QString, SpeedSample> speedSamples_;

        QMutex mu;

        // Interruptible poll sleep: the loop waits on waitCond_ for the current
        // interval; SetInView(true) / stopLoop() wake it early. inView_ selects the
        // active (1 Hz) vs relaxed cadence.
        QMutex waitMu_;
        QWaitCondition waitCond_;
        std::atomic<bool> inView_{false};

        bool stop = false;

        std::shared_ptr<QSet<QString>> state;

        ConnectionSort sort = Default;

        bool asc = false;

        // Per-app traffic diffing: last seen cumulative (up, down) per live
        // connection id, and the set of closed-connection ids already counted
        // (the closed ring is non-draining, so we dedup by id). Both self-prune
        // each poll, so they stay bounded and survive core restarts cleanly.
        QHash<QString, QPair<qint64, qint64>> lastBytes_;
        QSet<QString> accountedClosed_;
    };

    extern ConnectionLister* connection_lister;
}
