#pragma once

#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <memory>

#include "include/configs/generate.h"

namespace Stats
{
    // One member of a running auto-selector group, as the core last reported it,
    // with the tag resolved back to the profile it came from.
    struct AutoSelectorMemberView
    {
        QString tag;
        int profileID = -1;
        QString name;
        int rank = 0;
        QString state; // ok | degraded | untested | dead | cooldown
        bool selected = false;
        bool selectedUDP = false;
        bool pinned = false;
        bool qualified = false;
        bool active = false;
        int averageMs = 0;
        int deviationMs = 0;
        int minMs = 0;
        int maxMs = 0;
        int samples = 0;
        int failures = 0;
        int probes = 0;
        int dialTotal = 0;
        int dialFail = 0;
        qint64 lastOKms = 0;
        qint64 lastProbeMs = 0;
        qint64 cooldownUntilMs = 0;
        QString lastError;

        [[nodiscard]] bool isDead() const { return state == "dead"; }
        [[nodiscard]] bool isUsable() const { return state == "ok" || state == "degraded"; }

        // Something is actually wrong with this member: it is failing, being
        // backed off after a failed connection, or losing some of its checks.
        // "untested" is not a problem — the prober simply has not reached it.
        [[nodiscard]] bool hasProblem() const
        {
            return state == "dead" || state == "cooldown" || state == "degraded";
        }
    };

    struct AutoSelectorView
    {
        bool valid = false;
        QString groupTag;
        int profileID = -1;
        QString phase; // starting | probing | ready | suspended
        QString selectedTag;
        QString selectedName;
        int selectedProfileID = -1;
        // Set when the user pinned a member by hand. Differs from selectedTag
        // whenever that member is not currently healthy — the pin is a
        // preference, and the ranking still takes over when it has to.
        QString pinnedTag;
        QString pinnedName;
        bool balance = false;
        QString balanceMode;
        // The core believes the LOCAL network is down and has frozen its
        // ranking. Nothing may be judged broken while this is set.
        bool suspended = false;
        qint64 suspendedSinceMs = 0;
        int membersTotal = 0;
        int membersProbed = 0;
        int membersAlive = 0;
        int membersQualified = 0;
        int membersCooldown = 0;
        int probesInFlight = 0;
        int roundsCompleted = 0;
        qint64 lastRoundMs = 0;
        qint64 nextRoundMs = 0;
        qint64 lastSwitchMs = 0;
        QString lastSwitchReason;
        qint64 updatedAtMs = 0;
        // Set while the monitor is counting down to declaring the pool dead.
        qint64 exhaustedSinceMs = 0;
        QList<AutoSelectorMemberView> members;

        // A one-line description of what the selector is doing right now — the
        // point being that there is never a silent gap where the user cannot
        // tell what it is up to.
        [[nodiscard]] QString summary() const;

        // Second line: the numbers behind the summary.
        [[nodiscard]] QString detail() const;
    };

    // Polls the core for auto-selector state while a selector profile is
    // running. Measurement lives in the core (it is the one dialling); the
    // decision to give up on a pool and rebuild lives here.
    class AutoSelectorMonitor : public QObject
    {
        Q_OBJECT

    public:
        // Called when a profile starts. `infos` is empty for ordinary profiles,
        // which puts the monitor back to sleep.
        void SetBuild(const QList<Configs::AutoSelectorBuildInfo> &infos);

        void Clear();

        // Runs on its own thread; polls only while a selector is running.
        void Loop();

        [[nodiscard]] AutoSelectorView Snapshot() const;

        [[nodiscard]] bool Active() const;

        // Force a full re-check of every member now.
        void RequestRecheck() const;

        // Pins the group to one member, or hands it back to automatic selection
        // when `tag` is empty. Returns the core's error, empty on success. The
        // choice is stored on the profile so it outlives a restart.
        [[nodiscard]] QString RequestSelect(const QString &tag);

        // Writes what the core has measured back onto the member profiles, so a
        // restart can hand it straight back instead of re-measuring the pool.
        void PersistHealth();

    signals:
        // Fresh state arrived from the core.
        void updated();
        // Every built member has been unusable for long enough, and the local
        // network is demonstrably fine. The pool needs rebuilding from the next
        // slice of the ranked list.
        void poolExhausted(int profileID);

    private:
        void poll();

        mutable QMutex mutex;
        AutoSelectorView view;
        QHash<QString, int> tagToProfile;
        QHash<QString, QString> tagToName;
        QString groupTag;
        int profileID = -1;
        bool active = false;

        qint64 exhaustedSince = 0;
        qint64 lastRebuildRequest = 0;
        int rebuildBackoffSecs = 0;
        qint64 lastHealthPersist = 0;
    };

    extern AutoSelectorMonitor *autoSelectorMonitor;

    // How long every member must stay unusable before the pool is declared
    // exhausted. Long enough that a brief upstream hiccup cannot trigger a
    // reconnect, short enough that a genuinely dead pool is replaced quickly.
    constexpr int kPoolExhaustedGraceSecs = 20;
    // Floor between two rebuilds, doubled on each consecutive rebuild up to the
    // ceiling, so a subscription that is entirely dead cannot spin.
    constexpr int kRebuildBackoffMinSecs = 60;
    constexpr int kRebuildBackoffMaxSecs = 600;
    // How often the core's measurements are written back to the member profiles.
    // Often enough that a crash loses little, rare enough that a 300-member pool
    // is not writing rows every couple of seconds.
    constexpr int kHealthPersistSecs = 60;
} // namespace Stats
