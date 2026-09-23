#pragma once

#include <cstdint>
#include <string>

namespace Qv2ray::components::proxy::ownership {

    struct ProcessIdentity {
        std::uint64_t created100ns = 0;
        std::wstring normalizedImagePath;

        bool operator==(const ProcessIdentity &) const = default;
    };

    inline bool IsSameProcess(const ProcessIdentity &expected, const ProcessIdentity &actual) {
        return expected.created100ns != 0
               && !expected.normalizedImagePath.empty()
               && expected == actual;
    }

    struct State {
        std::uint32_t flags = 0;
        std::uint32_t autodiscoveryFlags = 0;
        std::wstring autoConfigUrl;
        std::wstring bypass;
        std::wstring server;

        bool operator==(const State &) const = default;
    };

    // Restore only fields that still contain the value applied by this app. If
    // another program or the user changed a field while Throne was running, the
    // current value wins. Flags are restored bit-by-bit. Endpoint-specific bits
    // are left alone when their proxy/PAC/autodetect value changed externally.
    inline State MergeForRestore(const State &before, const State &applied, const State &current) {
        State restored = current;

        constexpr std::uint32_t proxyFlag = 2;
        constexpr std::uint32_t autoUrlFlag = 4;
        constexpr std::uint32_t autoDetectFlag = 8;

        const auto ownedFlagMask = before.flags ^ applied.flags;
        // A bit remains ours only while it still equals the value we applied.
        auto restorableFlagMask = ownedFlagMask & ~(current.flags ^ applied.flags);
        if (current.server != applied.server) restorableFlagMask &= ~proxyFlag;
        if (current.autoConfigUrl != applied.autoConfigUrl) restorableFlagMask &= ~autoUrlFlag;
        if (current.autodiscoveryFlags != applied.autodiscoveryFlags) {
            restorableFlagMask &= ~autoDetectFlag;
        }
        restored.flags = (current.flags & ~restorableFlagMask)
                         | (before.flags & restorableFlagMask);

        if (current.autodiscoveryFlags == applied.autodiscoveryFlags) {
            restored.autodiscoveryFlags = before.autodiscoveryFlags;
        }
        if (current.autoConfigUrl == applied.autoConfigUrl) {
            restored.autoConfigUrl = before.autoConfigUrl;
        }
        if (current.bypass == applied.bypass) {
            restored.bypass = before.bypass;
        }
        if (current.server == applied.server) {
            restored.server = before.server;
        }

        return restored;
    }

} // namespace Qv2ray::components::proxy::ownership
