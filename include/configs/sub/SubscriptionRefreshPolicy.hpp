#pragma once
#include <cstdint>

namespace Subscription {
    constexpr int DefaultRefreshMinutes = 6 * 60;
    constexpr std::int64_t RefreshRetrySeconds = 5 * 60;

    // Success timestamps survive app restarts; attempt timestamps use a
    // monotonic in-process clock so wall-clock corrections cannot disable retry.
    inline bool refreshDue(std::int64_t now, std::int64_t lastSuccess, int intervalMinutes,
                           std::int64_t monotonicNow, std::int64_t lastAttempt = -1) {
        if (intervalMinutes < 30) return false;
        if (lastAttempt >= 0 && monotonicNow >= lastAttempt
            && monotonicNow - lastAttempt < RefreshRetrySeconds) return false;
        return lastSuccess <= 0 || lastSuccess > now
            || now - lastSuccess >= static_cast<std::int64_t>(intervalMinutes) * 60;
    }
}
