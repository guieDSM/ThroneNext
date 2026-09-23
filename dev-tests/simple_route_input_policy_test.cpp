#include <cassert>

#include "include/database/entities/SimpleRouteInputPolicy.hpp"

using Configs::SimpleRouteInput::Kind;
using Configs::SimpleRouteInput::canonicalText;
using Configs::SimpleRouteInput::conflictKey;
using Configs::SimpleRouteInput::parseLine;

int main() {
    {
        const auto parsed = parseLine("  domain: Example.COM  ");
        assert(parsed.kind == Kind::ExactDomain);
        assert(parsed.value == "example.com");
        assert(canonicalText(parsed) == "domain:example.com");
    }
    {
        const auto parsed = parseLine("example.com");
        assert(parsed.kind == Kind::DomainSuffix);
        assert(parsed.value == "example.com");
    }
    {
        const auto parsed = parseLine("*.Example.com.");
        assert(parsed.kind == Kind::DomainSuffix);
        assert(parsed.value == "example.com");
    }
    {
        const auto parsed = parseLine("suffix: .Example.com ");
        assert(parsed.kind == Kind::DomainSuffix);
        assert(parsed.value == "example.com");
    }
    {
        const auto parsed = parseLine("processPath: C:\\Program Files\\Browser\\browser.exe ");
        assert(parsed.kind == Kind::ProcessPath);
        assert(parsed.value == "C:\\Program Files\\Browser\\browser.exe");
    }
    {
        const auto parsed = parseLine(" PROCESS_NAME : browser.exe ");
        assert(parsed.kind == Kind::ProcessName);
        assert(parsed.value == "browser.exe");
    }
    {
        const auto parsed = parseLine("# domain:ignored.example");
        assert(parsed.isIgnored());
    }
    {
        const auto parsed = parseLine("domain:*.example.com");
        assert(!parsed.isValid());
        assert(parsed.error.contains("suffix"));
    }
    {
        const auto parsed = parseLine("https://example.com/path");
        assert(!parsed.isValid());
    }
    {
        const auto parsed = parseLine("unknown:value");
        assert(!parsed.isValid());
        assert(parsed.error.contains("unknown rule prefix"));
    }
    {
        const auto parsed = parseLine(QString::fromUtf8("пример.рф"));
        assert(parsed.kind == Kind::DomainSuffix);
        assert(parsed.value == "xn--e1afmkfd.xn--p1ai");
    }
    {
        const auto direct = parseLine("Example.com");
        const auto proxy = parseLine("suffix:example.com");
        assert(conflictKey(direct) == conflictKey(proxy));
        assert(canonicalText(direct) == "suffix:example.com");
    }
    {
        const auto first = parseLine("processPath:C:\\Apps\\Browser.exe");
        const auto second = parseLine("process_path:c:\\apps\\browser.exe");
        assert(conflictKey(first) == conflictKey(second));
    }
    return 0;
}
