#include "include/configs/sub/SubscriptionRefreshPolicy.hpp"
#include <cassert>

int main() {
    using namespace Subscription;
    constexpr std::int64_t now = 100000;
    constexpr auto period = DefaultRefreshMinutes * 60;
    assert(refreshDue(now, 0, 360, 0)); // first launch, never updated
    assert(!refreshDue(now, now - period + 1, 360, 0)); // fresh at restart
    assert(refreshDue(now, now - period, 360, 0)); // exact six-hour boundary
    assert(refreshDue(now, now - 2 * period, 360, 0)); // catch up after sleep
    assert(!refreshDue(now, now, 360, 0)); // successful manual update postpones auto
    assert(!refreshDue(now, 0, -360, 1000)); // explicit opt-out
    assert(!refreshDue(now, 0, 0, 1000));
    assert(!refreshDue(now, 0, 29, 1000));
    assert(!refreshDue(now, 0, 360, 100, 0)); // failure does not hammer the provider
    assert(refreshDue(now, 0, 360, 300, 0)); // retry after five minutes
    assert(refreshDue(now, now + 3600, 360, 300)); // system clock moved backwards
    assert(!refreshDue(now, now + 3600, 360, 100, 0)); // still respect backoff
    assert(!refreshDue(now, now - 3599, 60, 0)); // settings remain editable
    assert(refreshDue(now, now - 3600, 60, 0));
}
