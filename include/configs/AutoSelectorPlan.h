#pragma once

#include <QList>
#include <QString>
#include <memory>

namespace Configs
{
    class Profile;
    class autoSelector;

    // Why a profile in the tracked group did not become a member.
    enum class AutoSelectorSkip
    {
        None = 0,
        Missing,         // dangling id in the group
        MetaType,        // chain / another auto selector — no nesting
        CoreTransitions, // group's landing/front proxies push it past one core handoff
        ExtraCore,       // runs its own process
        FullConfig,      // sing-box full config: its own box
        Malformed,       // config does not parse; would break the shared build
        Tailscale,
        NameFilter,
        CountryFilter,
        Unavailable,      // last test failed and the profile excludes those
        XrayFullChained,  // Xray full config + the group's landing/front proxies
    };

    QString AutoSelectorSkipReason(AutoSelectorSkip skip);

    // The membership decision for one auto-selector profile: everything the
    // build needs, plus the counts the UI shows so a filtered-out server is
    // never silently dropped.
    struct AutoSelectorPlan
    {
        // Eligible members, best first, capped at the profile's poolCap.
        QList<int> pool;
        // The prefix of `pool` that actually enters the config (buildLimit).
        QList<int> build;

        int membersInGroup = 0;
        int eligible = 0;
        int rankedByTest = 0; // members carrying a real latency measurement
        QList<QPair<AutoSelectorSkip, int>> skipped;
        // True when the pool had to be truncated, so which members get built is
        // a real decision rather than "all of them".
        bool truncated = false;
        int poolCapUsed = 0;
        int buildLimitUsed = 0;
        // True when the ordering cannot be trusted yet: members are unranked and
        // there are more of them than the build limit. The caller should run a
        // client-side URL test before building.
        bool needsRanking = false;

        QString error;

        [[nodiscard]] int skippedCount() const
        {
            int total = 0;
            for (const auto &entry : skipped) total += entry.second;
            return total;
        }
    };

    // Resolve membership for an auto-selector profile from the group it tracks.
    // Ordering prefers the profile's persisted ranking, then measured latency,
    // then untested members, and puts known-failing members last.
    AutoSelectorPlan PlanAutoSelector(const std::shared_ptr<Profile> &ent);

    // Every eligible member, regardless of the build limit.
    QList<int> AutoSelectorRankingCandidates(const std::shared_ptr<Profile> &ent);

    // The subset a client-side URL test actually has to measure: members with no
    // stored latency at all, plus anything in `stale` (the members that just
    // failed, whose stored result no longer reflects reality). Results the user
    // already produced by testing the group are reused as-is, so starting a
    // selector right after a manual URL test runs no second sweep.
    QList<int> AutoSelectorUnmeasuredCandidates(const std::shared_ptr<Profile> &ent,
                                                const QList<int> &stale = {});

    // Re-order and persist the pool from current DB latencies. Returns the ids
    // that made the cut, best first.
    QList<int> RerankAutoSelectorPool(const std::shared_ptr<Profile> &ent);
}
