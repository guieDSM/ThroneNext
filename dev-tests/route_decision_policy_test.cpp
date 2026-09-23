#include "include/database/entities/RouteDecisionPolicy.hpp"

#include <cassert>

using Configs::RouteDecisionPolicy::Confidence;
using Configs::RouteDecisionPolicy::Evaluate;
using Configs::RouteDecisionPolicy::Probe;
using Configs::RouteDecisionPolicy::Rule;

namespace {
    Rule defaultProxy() {
        Rule rule;
        rule.sourceIndex = -1;
        rule.label = "Default outbound";
        rule.action = "route";
        rule.outbound = "proxy";
        return rule;
    }
}

int main() {
    {
        Rule direct;
        direct.sourceIndex = 1;
        direct.label = "Direct sites";
        direct.outbound = "direct";
        direct.domainSuffix = {"example.com"};
        const auto result = Evaluate({direct, defaultProxy()}, Probe{"cdn.Example.com.", {}, {}});
        assert(result.outbound == "direct");
        assert(result.sourceIndex == 1);
        assert(result.matchedBy.front() == "domain suffix example.com");
    }
    {
        Rule exact;
        exact.sourceIndex = 1;
        exact.outbound = "direct";
        exact.domain = {"example.com"};
        const auto result = Evaluate({exact, defaultProxy()}, Probe{"www.example.com", {}, {}});
        assert(result.outbound == "proxy");
        assert(result.sourceIndex == -1);
    }
    {
        Rule processProxy;
        processProxy.sourceIndex = 1;
        processProxy.outbound = "proxy";
        processProxy.processName = {"browser.exe"};
        Rule domainDirect;
        domainDirect.sourceIndex = 2;
        domainDirect.outbound = "direct";
        domainDirect.domainSuffix = {"example.com"};
        const auto result = Evaluate(
            {processProxy, domainDirect, defaultProxy()},
            Probe{"example.com", "BROWSER.EXE", "C:\\Apps\\browser.exe"});
        assert(result.outbound == "proxy");
        assert(result.sourceIndex == 1);
    }
    {
        Rule path;
        path.sourceIndex = 1;
        path.outbound = "direct";
        path.processPath = {"C:\\Apps\\Browser.exe"};
        const auto result = Evaluate(
            {path, defaultProxy()}, Probe{{}, "browser.exe", "c:/apps/browser.exe"});
        assert(result.outbound == "direct");
    }
    {
        Rule remote;
        remote.sourceIndex = 1;
        remote.label = "Remote rule set";
        remote.action = "reject";
        remote.outbound = "block";
        remote.unsupportedConditions = {"rule_set"};
        Rule direct;
        direct.sourceIndex = 2;
        direct.label = "Direct sites";
        direct.outbound = "direct";
        direct.domainSuffix = {"example.com"};
        const auto result = Evaluate({remote, direct, defaultProxy()}, Probe{"example.com", {}, {}});
        assert(result.outbound == "direct");
        assert(result.confidence == Confidence::Conditional);
        assert(!result.cautions.empty());
    }
    {
        Rule inverted;
        inverted.sourceIndex = 1;
        inverted.outbound = "direct";
        inverted.domain = {"internal.example"};
        inverted.invert = true;
        const auto result = Evaluate({inverted, defaultProxy()}, Probe{"public.example", {}, {}});
        assert(result.outbound == "direct");
        assert(result.matchedBy.front().find("inverted") != std::string::npos);
    }
    {
        // Domain and destination IP/rule-set conditions are alternatives in
        // sing-box. A domain miss does not prove that the earlier rule misses.
        Rule mixed;
        mixed.sourceIndex = 1;
        mixed.label = "Domain or remote list";
        mixed.outbound = "proxy";
        mixed.domain = {"other.example"};
        mixed.unsupportedDestinationAlternative = true;
        Rule direct;
        direct.sourceIndex = 2;
        direct.outbound = "direct";
        direct.domain = {"site.example"};
        const auto result = Evaluate({mixed, direct, defaultProxy()}, Probe{"site.example", {}, {}});
        assert(result.outbound == "direct");
        assert(result.confidence == Confidence::Conditional);
        assert(!result.cautions.empty());
    }
    {
        // Once one member of the OR group matches, unknown alternatives cannot
        // make the complete destination group false.
        Rule mixed;
        mixed.sourceIndex = 1;
        mixed.outbound = "direct";
        mixed.domainSuffix = {"example.com"};
        mixed.unsupportedDestinationAlternative = true;
        const auto result = Evaluate({mixed, defaultProxy()}, Probe{"cdn.example.com", {}, {}});
        assert(result.outbound == "direct");
        assert(result.confidence == Confidence::Certain);
    }
}
