#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Configs::xhttp_validation {

    struct Range {
        std::int32_t from = 0;
        std::int32_t to = 0;
    };

    struct Input {
        std::string mode;
        std::vector<std::string> headerNames;
        std::string xPaddingBytes;
        std::string xPaddingPlacement;
        std::string xPaddingMethod;
        std::string uplinkHTTPMethod;
        std::string sessionIDPlacement;
        std::string sessionIDTable;
        std::string sessionIDLength;
        std::string seqPlacement;
        std::string uplinkDataPlacement;
        std::string uplinkChunkSize;
        std::string scMaxEachPostBytes;
        std::string scMinPostsIntervalMs;
        std::string scStreamUpServerSecs;
        std::int64_t scMaxBufferedPosts = 0;
        std::int32_t serverMaxHeaderBytes = 0;
        std::string maxConcurrency;
        std::string maxConnections;
        std::string cMaxReuseTimes;
        std::string hMaxRequestTimes;
        std::string hMaxReusableSecs;
        bool downloadSettingsPresent = false;
        bool downloadSettingsValid = true;
    };

    struct Result {
        bool ok = true;
        std::string message;
    };

    inline std::optional<std::int32_t> parseInt32(std::string_view value) {
        if (value.empty()) return std::nullopt;
        std::int64_t parsed = 0;
        const auto *begin = value.data();
        const auto *end = begin + value.size();
        const auto [ptr, error] = std::from_chars(begin, end, parsed);
        if (error != std::errc{} || ptr != end
            || parsed < std::numeric_limits<std::int32_t>::min()
            || parsed > std::numeric_limits<std::int32_t>::max()) {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(parsed);
    }

    // Matches Xray's Int32Range syntax: 1, 1-2, -1, -2-4 and -4--2.
    inline std::optional<Range> parseRange(std::string_view value) {
        if (value.empty()) return Range{};
        if (const auto single = parseInt32(value)) return Range{*single, *single};

        const auto separator = value.find('-', value.front() == '-' ? 1 : 0);
        if (separator == std::string_view::npos) return std::nullopt;
        const auto left = parseInt32(value.substr(0, separator));
        const auto right = parseInt32(value.substr(separator + 1));
        if (!left || !right) return std::nullopt;
        return Range{std::min(*left, *right), std::max(*left, *right)};
    }

    inline std::string upperAscii(std::string value) {
        for (auto &character : value) {
            if (character >= 'a' && character <= 'z') character = static_cast<char>(character - ('a' - 'A'));
        }
        return value;
    }

    inline bool contains(const std::vector<std::string_view> &allowed, std::string_view value) {
        return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
    }

    inline std::size_t sessionTableSize(const std::string &table) {
        if (table == "number") return 10;
        if (table == "hex" || table == "HEX") return 16;
        if (table == "alphabet" || table == "ALPHABET") return 26;
        if (table == "base36" || table == "BASE36") return 36;
        if (table == "Alphabet") return 52;
        if (table == "Base62") return 62;
        return table.size();
    }

    inline bool hasEnoughSessionIDs(std::size_t tableSize, const Range &length) {
        if (tableSize == 0 || length.from <= 0 || length.to < length.from) return false;
        constexpr long double minimumRoom = 2147483648.0L; // Xray: 2 << 30

        long double term = std::pow(static_cast<long double>(tableSize), length.from);
        long double total = 0;
        for (std::int64_t current = length.from; current <= length.to; ++current) {
            total += term;
            if (!std::isfinite(total) || total >= minimumRoom) return true;
            term *= static_cast<long double>(tableSize);
            if (!std::isfinite(term)) return true;
            // Even base 2 reaches the threshold by length 31, so this is only
            // a guard against malformed billion-wide ranges.
            if (current - length.from > 64) break;
        }
        return total >= minimumRoom;
    }

    inline Result invalid(std::string message) {
        return {false, std::move(message)};
    }

    inline Result Validate(const Input &input) {
        const auto mode = input.mode.empty() ? std::string("auto") : input.mode;
        if (!contains({"auto", "packet-up", "stream-up", "stream-one"}, mode)) {
            return invalid("Unsupported XHTTP mode: " + mode);
        }

        for (const auto &header : input.headerNames) {
            std::string lower = header;
            for (auto &character : lower) {
                if (character >= 'A' && character <= 'Z') character = static_cast<char>(character + ('a' - 'A'));
            }
            if (lower == "host") return invalid("XHTTP headers cannot contain Host; use the dedicated Host field.");
        }

        const auto validateRange = [](const std::string &name, const std::string &value) -> Result {
            if (!parseRange(value)) return invalid(name + " must be an integer or an integer range such as 100-200.");
            return {};
        };
        const std::vector<std::pair<std::string, const std::string *>> ranges = {
            {"xPaddingBytes", &input.xPaddingBytes},
            {"sessionIDLength", &input.sessionIDLength},
            {"uplinkChunkSize", &input.uplinkChunkSize},
            {"scMaxEachPostBytes", &input.scMaxEachPostBytes},
            {"scMinPostsIntervalMs", &input.scMinPostsIntervalMs},
            {"scStreamUpServerSecs", &input.scStreamUpServerSecs},
            {"maxConcurrency", &input.maxConcurrency},
            {"maxConnections", &input.maxConnections},
            {"cMaxReuseTimes", &input.cMaxReuseTimes},
            {"hMaxRequestTimes", &input.hMaxRequestTimes},
            {"hMaxReusableSecs", &input.hMaxReusableSecs},
        };
        for (const auto &[name, value] : ranges) {
            if (const auto result = validateRange(name, *value); !result.ok) return result;
        }

        if (!input.xPaddingBytes.empty()) {
            const auto range = *parseRange(input.xPaddingBytes);
            if (range.from <= 0 || range.to <= 0) return invalid("xPaddingBytes must have positive bounds.");
        }

        if (!contains({"", "cookie", "header", "query", "queryInHeader"}, input.xPaddingPlacement)) {
            return invalid("Unsupported XHTTP padding placement: " + input.xPaddingPlacement);
        }
        if (!contains({"", "repeat-x", "tokenish"}, input.xPaddingMethod)) {
            return invalid("Unsupported XHTTP padding method: " + input.xPaddingMethod);
        }
        if (!contains({"", "path", "cookie", "header", "query"}, input.sessionIDPlacement)) {
            return invalid("Unsupported XHTTP session ID placement: " + input.sessionIDPlacement);
        }
        if (!contains({"", "path", "cookie", "header", "query"}, input.seqPlacement)) {
            return invalid("Unsupported XHTTP sequence placement: " + input.seqPlacement);
        }
        if (!contains({"", "auto", "body", "cookie", "header"}, input.uplinkDataPlacement)) {
            return invalid("Unsupported XHTTP uplink data placement: " + input.uplinkDataPlacement);
        }
        if ((input.uplinkDataPlacement == "cookie" || input.uplinkDataPlacement == "header")
            && mode != "packet-up") {
            return invalid("XHTTP cookie/header uplink data placement requires packet-up mode.");
        }

        if (upperAscii(input.uplinkHTTPMethod) == "GET" && mode != "packet-up") {
            return invalid("XHTTP uplink HTTP method GET requires packet-up mode.");
        }

        if (!input.sessionIDTable.empty()) {
            for (const auto byte : input.sessionIDTable) {
                if (static_cast<unsigned char>(byte) >= 0x80) {
                    return invalid("XHTTP sessionIDTable must contain only ASCII characters.");
                }
            }
            const auto length = *parseRange(input.sessionIDLength);
            if (length.from <= 0) return invalid("XHTTP sessionIDLength.from must be greater than 0.");
            if (!hasEnoughSessionIDs(sessionTableSize(input.sessionIDTable), length)) {
                return invalid("XHTTP sessionIDTable or sessionIDLength is too small.");
            }
        }

        if (input.serverMaxHeaderBytes < 0) {
            return invalid("XHTTP serverMaxHeaderBytes cannot be negative.");
        }

        const auto maxConnections = *parseRange(input.maxConnections);
        const auto maxConcurrency = *parseRange(input.maxConcurrency);
        if (maxConnections.to > 0 && maxConcurrency.to > 0) {
            return invalid("XHTTP maxConnections cannot be specified together with maxConcurrency.");
        }

        if (input.downloadSettingsPresent && !input.downloadSettingsValid) {
            return invalid("XHTTP downloadSettings must be a valid JSON object.");
        }
        if (input.downloadSettingsPresent && mode == "stream-one") {
            return invalid("XHTTP downloadSettings cannot be used in stream-one mode.");
        }

        return {};
    }

} // namespace Configs::xhttp_validation
