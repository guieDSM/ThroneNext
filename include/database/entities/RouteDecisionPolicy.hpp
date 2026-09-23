#pragma once

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace Configs::RouteDecisionPolicy {

    struct Probe {
        std::string domain;
        std::string processName;
        std::string processPath;
    };

    struct Rule {
        int sourceIndex = -1;
        std::string label;
        std::string action = "route";
        std::string outbound;
        std::vector<std::string> domain;
        std::vector<std::string> domainSuffix;
        std::vector<std::string> domainKeyword;
        std::vector<std::string> domainRegex;
        std::vector<std::string> processName;
        std::vector<std::string> processPath;
        std::vector<std::string> processPathRegex;
        // sing-box treats domain, destination IP and rule-set matchers as one
        // OR group. The preview cannot inspect IP/rule-set contents, but it
        // must not incorrectly combine them with a known domain using AND.
        bool unsupportedDestinationAlternative = false;
        bool invert = false;
        std::vector<std::string> unsupportedConditions;
    };

    enum class Confidence {
        Certain,
        Conditional,
    };

    struct Result {
        Confidence confidence = Confidence::Certain;
        int sourceIndex = -1;
        std::string label;
        std::string action;
        std::string outbound;
        std::vector<std::string> matchedBy;
        std::vector<std::string> cautions;
    };

    namespace detail {
        inline std::string asciiLower(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return value;
        }

        inline std::string normalizeDomain(std::string value) {
            value = asciiLower(std::move(value));
            while (!value.empty() && value.back() == '.') value.pop_back();
            return value;
        }

        inline std::string normalizePath(std::string value) {
            value = asciiLower(std::move(value));
            std::replace(value.begin(), value.end(), '\\', '/');
            while (value.size() > 3 && value.back() == '/') value.pop_back();
            return value;
        }

        struct GroupMatch {
            bool present = false;
            bool matched = false;
            bool invalidPattern = false;
            std::string reason;
        };

        inline GroupMatch matchDomain(const Rule& rule, const std::string& rawDomain) {
            GroupMatch result;
            result.present = !rule.domain.empty() || !rule.domainSuffix.empty()
                             || !rule.domainKeyword.empty() || !rule.domainRegex.empty();
            if (!result.present) return result;

            const std::string domain = normalizeDomain(rawDomain);
            if (domain.empty()) return result;

            for (const auto& candidateRaw : rule.domain) {
                const auto candidate = normalizeDomain(candidateRaw);
                if (!candidate.empty() && domain == candidate) {
                    result.matched = true;
                    result.reason = "exact domain " + candidate;
                    return result;
                }
            }
            for (const auto& candidateRaw : rule.domainSuffix) {
                const auto candidate = normalizeDomain(candidateRaw);
                if (!candidate.empty()
                    && (domain == candidate
                        || (domain.size() > candidate.size()
                            && domain.ends_with("." + candidate)))) {
                    result.matched = true;
                    result.reason = "domain suffix " + candidate;
                    return result;
                }
            }
            for (const auto& candidateRaw : rule.domainKeyword) {
                const auto candidate = asciiLower(candidateRaw);
                if (!candidate.empty() && domain.find(candidate) != std::string::npos) {
                    result.matched = true;
                    result.reason = "domain keyword " + candidateRaw;
                    return result;
                }
            }
            for (const auto& pattern : rule.domainRegex) {
                try {
                    if (std::regex_search(domain, std::regex(pattern, std::regex::ECMAScript | std::regex::icase))) {
                        result.matched = true;
                        result.reason = "domain regex " + pattern;
                        return result;
                    }
                } catch (const std::regex_error&) {
                    result.invalidPattern = true;
                }
            }
            return result;
        }

        inline GroupMatch matchExactGroup(const std::vector<std::string>& candidates,
                                          const std::string& value,
                                          const std::string& label,
                                          const bool path) {
            GroupMatch result;
            result.present = !candidates.empty();
            if (!result.present || value.empty()) return result;
            const auto normalizedValue = path ? normalizePath(value) : asciiLower(value);
            for (const auto& candidateRaw : candidates) {
                const auto candidate = path ? normalizePath(candidateRaw) : asciiLower(candidateRaw);
                if (!candidate.empty() && normalizedValue == candidate) {
                    result.matched = true;
                    result.reason = label + " " + candidateRaw;
                    return result;
                }
            }
            return result;
        }

        inline GroupMatch matchPathRegex(const std::vector<std::string>& patterns,
                                         const std::string& rawPath) {
            GroupMatch result;
            result.present = !patterns.empty();
            if (!result.present || rawPath.empty()) return result;
            const auto path = normalizePath(rawPath);
            for (const auto& pattern : patterns) {
                try {
                    if (std::regex_search(path, std::regex(pattern, std::regex::ECMAScript | std::regex::icase))) {
                        result.matched = true;
                        result.reason = "process path regex " + pattern;
                        return result;
                    }
                } catch (const std::regex_error&) {
                    result.invalidPattern = true;
                }
            }
            return result;
        }
    }

    inline Result Evaluate(const std::vector<Rule>& rules, const Probe& probe) {
        std::vector<std::string> possibleEarlierRules;

        for (const auto& rule : rules) {
            const auto domain = detail::matchDomain(rule, probe.domain);
            const auto processName = detail::matchExactGroup(
                rule.processName, probe.processName, "process name", false);
            const auto processPath = detail::matchExactGroup(
                rule.processPath, probe.processPath, "process path", true);
            const auto processPathRegex = detail::matchPathRegex(
                rule.processPathRegex, probe.processPath);

            const std::vector<detail::GroupMatch> groups = {
                processName, processPath, processPathRegex,
            };
            bool knownMismatch = false;
            bool uncertain = false;
            std::vector<std::string> reasons;
            const bool destinationPresent = domain.present
                || rule.unsupportedDestinationAlternative;
            const bool destinationUncertain = !domain.matched
                && (domain.invalidPattern || rule.unsupportedDestinationAlternative);
            if (destinationPresent && !domain.matched && !destinationUncertain) {
                knownMismatch = true;
            }
            if (destinationUncertain) uncertain = true;
            if (domain.matched && !domain.reason.empty()) reasons.push_back(domain.reason);
            for (const auto& group : groups) {
                if (group.present && !group.matched) knownMismatch = true;
                if (group.invalidPattern) uncertain = true;
                if (group.matched && !group.reason.empty()) reasons.push_back(group.reason);
            }

            uncertain = uncertain || !rule.unsupportedConditions.empty();
            bool definiteMatch = false;
            bool possibleMatch = false;
            if (!rule.invert) {
                definiteMatch = !knownMismatch && !uncertain;
                possibleMatch = !knownMismatch && uncertain;
            } else {
                // An already-false known condition makes the inverted rule true even
                // when another condition cannot be previewed.
                definiteMatch = knownMismatch;
                possibleMatch = !knownMismatch && uncertain;
                if (definiteMatch) reasons = {"inverted rule (its original conditions did not all match)"};
            }

            if (possibleMatch) {
                possibleEarlierRules.push_back(rule.label.empty()
                    ? ("rule " + std::to_string(rule.sourceIndex))
                    : rule.label);
                continue;
            }
            if (!definiteMatch) continue;

            Result result;
            result.sourceIndex = rule.sourceIndex;
            result.label = rule.label;
            result.action = rule.action;
            result.outbound = rule.outbound;
            result.matchedBy = std::move(reasons);
            if (!possibleEarlierRules.empty()) {
                result.confidence = Confidence::Conditional;
                result.cautions.push_back("Earlier rules with external or unsupported conditions may override: "
                    + [&possibleEarlierRules] {
                        std::string joined;
                        for (const auto& label : possibleEarlierRules) {
                            if (!joined.empty()) joined += ", ";
                            joined += label;
                        }
                        return joined;
                    }());
            }
            return result;
        }

        Result result;
        result.confidence = possibleEarlierRules.empty()
            ? Confidence::Certain : Confidence::Conditional;
        result.label = "No final rule";
        result.action = "route";
        result.outbound = "unknown";
        if (!possibleEarlierRules.empty()) {
            result.cautions.push_back("Only rules with external or unsupported conditions could match.");
        }
        return result;
    }
}
