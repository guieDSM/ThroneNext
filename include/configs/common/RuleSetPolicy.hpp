#pragma once

namespace Configs {
    inline bool includeAdblockRuleSet(bool enabled, bool rawRoute, bool rawReferencesAdblock) {
        return enabled && (!rawRoute || rawReferencesAdblock);
    }
}
