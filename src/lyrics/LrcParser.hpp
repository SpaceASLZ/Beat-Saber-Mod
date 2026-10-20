#pragma once

#include <string>
#include <vector>

namespace QuestLyrics {

struct LyricLine {
    // Timestamp from the beginning of the song, in milliseconds.
    long long timeMs = 0;
    std::string text;
};

// Parse timed LRC lyrics into timestamped lines.
std::vector<LyricLine> ParseLrc(const std::string& lrcText);

} // namespace QuestLyrics
