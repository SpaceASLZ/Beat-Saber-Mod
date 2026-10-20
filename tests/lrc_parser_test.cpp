#include "../src/lyrics/LrcParser.hpp"

#include <cassert>
#include <iostream>

int main() {
    using QuestLyrics::ParseLrc;

    const auto lyrics = ParseLrc(
        "[00:01.00]First line\n"
        "[00:02.50]Second line\n"
        "[00:04.00][00:05.00]Repeated line\n"
        "[ar:Test Artist]\n"
    );

    assert(lyrics.size() == 4);

    assert(lyrics[0].timeMs == 1000);
    assert(lyrics[0].text == "First line");

    assert(lyrics[1].timeMs == 2500);
    assert(lyrics[1].text == "Second line");

    assert(lyrics[2].timeMs == 4000);
    assert(lyrics[2].text == "Repeated line");

    assert(lyrics[3].timeMs == 5000);
    assert(lyrics[3].text == "Repeated line");

    std::cout << "All QuestLyrics parser tests passed!\n";
    return 0;
}
