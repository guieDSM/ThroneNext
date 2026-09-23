#include "3rdparty/qv2ray/v2/proxy/SystemProxyOwnershipPolicy.hpp"

#include <cassert>

using Qv2ray::components::proxy::ownership::MergeForRestore;
using Qv2ray::components::proxy::ownership::IsSameProcess;
using Qv2ray::components::proxy::ownership::ProcessIdentity;
using Qv2ray::components::proxy::ownership::State;

namespace {
    constexpr std::uint32_t direct = 1;
    constexpr std::uint32_t proxy = 2;
    constexpr std::uint32_t autoUrl = 4;
    constexpr std::uint32_t autoDetect = 8;

    State appApplied(const State &before) {
        State applied = before;
        applied.flags = direct | proxy;
        applied.server = L"http://127.0.0.1:2080";
        return applied;
    }
}

int main() {
    {
        const ProcessIdentity owner{123456, L"c:\\apps\\throne.exe"};
        assert(IsSameProcess(owner, owner));
        assert(!IsSameProcess(owner, ProcessIdentity{123457, owner.normalizedImagePath}));
        assert(!IsSameProcess(owner, ProcessIdentity{owner.created100ns, L"c:\\other.exe"}));
        assert(!IsSameProcess(ProcessIdentity{}, ProcessIdentity{}));
    }

    {
        const State before{direct, 0, L"", L"localhost", L""};
        const State applied = appApplied(before);
        assert(MergeForRestore(before, applied, applied) == before);
    }

    {
        const State before{direct | autoUrl, 7, L"https://example.test/proxy.pac", L"localhost", L""};
        const State applied = appApplied(before);
        assert(MergeForRestore(before, applied, applied) == before);
    }

    {
        const State before{direct | autoUrl, 7, L"https://old.test/proxy.pac", L"localhost", L""};
        const State applied = appApplied(before);
        State current = applied;
        current.flags |= autoUrl;
        current.autoConfigUrl = L"https://new.test/proxy.pac";

        const State restored = MergeForRestore(before, applied, current);
        assert(restored.flags == (direct | autoUrl));
        assert(restored.server == before.server);
        assert(restored.autoConfigUrl == current.autoConfigUrl);
    }

    {
        const State before{direct, 0, L"", L"localhost", L""};
        const State applied = appApplied(before);
        State current = applied;
        current.flags &= ~proxy;

        const State restored = MergeForRestore(before, applied, current);
        assert((restored.flags & proxy) == 0);
        assert(restored.server == before.server);
    }

    {
        const State before{direct, 0, L"", L"localhost", L""};
        const State applied = appApplied(before);
        State current = applied;
        current.server = L"127.0.0.1:9090";
        current.flags |= autoDetect;

        const State restored = MergeForRestore(before, applied, current);
        assert(restored.server == current.server);
        assert(restored.flags == current.flags);
    }

    {
        const State before{direct, 0, L"", L"localhost", L""};
        const State applied = appApplied(before);
        State current = applied;
        current.bypass = L"*.internal.test";
        current.flags |= autoDetect;

        const State restored = MergeForRestore(before, applied, current);
        assert(restored.server == before.server);
        assert(restored.bypass == current.bypass);
        assert(restored.flags == (direct | autoDetect));
    }

    {
        const State before{direct, 3, L"", L"", L""};
        const State applied = appApplied(before);
        State current = applied;
        current.autodiscoveryFlags = 11;

        const State restored = MergeForRestore(before, applied, current);
        assert(restored.autodiscoveryFlags == 11);
    }
}
