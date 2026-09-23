#include "include/configs/common/RuleSetPolicy.hpp"
#include <cassert>

int main() {
    using Configs::includeAdblockRuleSet;
    assert(!includeAdblockRuleSet(false, false, false));
    assert(includeAdblockRuleSet(true, false, false));
    assert(!includeAdblockRuleSet(true, true, false));
    assert(includeAdblockRuleSet(true, true, true));
}
