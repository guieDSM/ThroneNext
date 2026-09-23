#include "include/configs/sub/SubscriptionJson.hpp"
#include <cassert>

int main() {
    using Subscription::jsonObjectEnd;
    for (const auto &str : {
        QString(R"({"path":"/}","nested":{"value":1}})"),
        QString(R"({"path":"/{","nested":[{},{}]})"),
        QString(R"({"quoted":"a\"}b","slash":"\\"})"),
        QString("{}")}) {
        assert(jsonObjectEnd(str + "\n{\"next\":1}", 0) == str.size() - 1);
        assert(jsonObjectEnd("\n" + str, 1) == str.size());
        assert(jsonObjectEnd(str.left(str.size() - 1), 0) == -1);
    }
    assert(jsonObjectEnd("[]", 0) == -1);
    assert(jsonObjectEnd("{}", -1) == -1);
}
