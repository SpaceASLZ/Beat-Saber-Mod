#include "LrcParser.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <vector>

namespace QuestLyrics {
namespace {

std::string Trim(const std::string& value) {
    const auto first = std::find_if_not(
        value.begin(), value.end(),
        [](unsigned char c) {
            return std::isspace(c) != 0;
        });

    if (first == value.end()) {
        return {};
    }

    const auto last = std::find_if_not(
        value.rbegin(), value.rend(),
        [](unsigned char c) {
            return std::isspace(c) != 0;
        }).base();

    return std::string(first, last);
}

bool ParseTimestamp(
    const std::string& stamp,
    long long& outMs
) {
    static const std::regex pattern(
        R"(^([0-9]{1,3}):([0-5][0-9])(?:\.([0-9]{1,3}))?$)"
    );

    std::smatch match;

    if (!std::regex_match(stamp, match, pattern)) {
        return false;
    }

    const long long minutes = std::stoll(match[1].str());
    const long long seconds = std::stoll(match[2].str());

    std::string fraction =
        match[3].matched ? match[3].str() : "";

    if (fraction.size() == 1) {
        fraction += "00";
    } else if (fraction.size() == 2) {
        fraction += "0";
    }

    const long long millis =
        fraction.empty() ? 0 : std::stoll(fraction);

    outMs = (minutes * 60 + seconds) * 1000 + millis;
    return true;
}

} // namespace

std::vector<LyricLine> ParseLrc(
    const std::string& lrcText
) {
    static const std::regex timestamp(
        R"(\[([0-9]{1,3}:[0-5][0-9](?:\.[0-9]{1,3})?)\])"
    );

    std::vector<LyricLine> result;
    std::size_t lineStart = 0;

    while (lineStart <= lrcText.size()) {
        const std::size_t lineEnd =
            lrcText.find('\n', lineStart);

        const std::string raw = lrcText.substr(
            lineStart,
            lineEnd == std::string::npos
                ? std::string::npos
                : lineEnd - lineStart
        );

        const std::string line = Trim(raw);
        std::vector<long long> timestamps;
        std::smatch match;
        auto searchStart = line.cbegin();

        while (std::regex_search(
            searchStart, line.cend(), match, timestamp
        )) {
            long long timeMs = 0;

            if (ParseTimestamp(match[1].str(), timeMs)) {
                timestamps.push_back(timeMs);
            }

            searchStart = match.suffix().first;
        }

        if (!timestamps.empty()) {
            const std::string lyric = Trim(
                std::regex_replace(line, timestamp, "")
            );

            for (const long long timeMs : timestamps) {
                result.push_back({timeMs, lyric});
            }
        }

        if (lineEnd == std::string::npos) {
            break;
        }

        lineStart = lineEnd + 1;
    }

    std::stable_sort(
        result.begin(),
        result.end(),
        [](const LyricLine& a, const LyricLine& b) {
            return a.timeMs < b.timeMs;
        }
    );

    return result;
}

} // namespace QuestLyrics
