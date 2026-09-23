#pragma once
#include <QString>

namespace Subscription {
    inline int jsonObjectEnd(const QString &str, int begin) {
        if (begin < 0 || begin >= str.size() || str[begin] != '{') return -1;
        int depth = 0;
        bool quoted = false, escaped = false;
        for (int i = begin; i < str.size(); ++i) {
            const auto c = str[i];
            if (quoted) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') quoted = false;
                continue;
            }
            if (c == '"') quoted = true;
            else if (c == '{') ++depth;
            else if (c == '}' && --depth == 0) return i;
        }
        return -1;
    }
}
