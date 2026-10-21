#include "scotland2/shared/modloader.h"
#include "_config.hpp"

#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/GameplayCoreInstaller.hpp"
#include "GlobalNamespace/GameplayCoreSceneSetupData.hpp"
#include "TMPro/TextAlignmentOptions.hpp"

#include "UnityEngine/Networking/DownloadHandler.hpp"
#include "UnityEngine/Networking/UnityWebRequest.hpp"

#include "beatsaber-hook/shared/config/rapidjson-utils.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-utils.hpp"
#include "beatsaber-hook/shared/utils/logging.hpp"

#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML-Lite/Creation/Misc.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"

#include "lyrics/LrcParser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

static CModInfo modInfo{MOD_ID, VERSION, 0};
constexpr auto logger = Paper::ConstLoggerContext("QuestLyrics");

static BSML::FloatingScreen* lyricsScreen = nullptr;
static HMUI::CurvedTextMeshPro* lyricsText = nullptr;

static UnityEngine::Networking::UnityWebRequest* lyricsRequest = nullptr;

static std::string currentSongTitle;
static std::string currentSongArtist;
static std::string lastPanelText;

static std::vector<QuestLyrics::LyricLine> timedLyrics;

static bool lyricsLookupComplete = false;
static bool lyricsLoaded = false;
static int lastDisplayedSecond = -1;

// Keep the panel at the position and size you approved.
static void CreateLyricsPanel() {
    if (lyricsScreen != nullptr) {
        return;
    }

    lyricsScreen = BSML::Lite::CreateFloatingScreen(
        UnityEngine::Vector2(65.0f, 26.0f),
        UnityEngine::Vector3(-1.15f, 2.0f, 3.0f),
        UnityEngine::Vector3(0.0f, 0.0f, 0.0f),
        0.0f,
        true,
        false
    );

    lyricsText = BSML::Lite::CreateText(
        lyricsScreen->get_transform(),
        "QuestLyrics\nWaiting for song...",
        3.8f,
        UnityEngine::Vector2(0.0f, 0.0f),
        UnityEngine::Vector2(61.0f, 22.0f)
    );

    lyricsText->set_alignment(TMPro::TextAlignmentOptions::Center);
    lyricsScreen->get_gameObject()->set_active(true);

    logger.info("Created QuestLyrics floating panel");
}

static void SetPanelText(const std::string& text) {
    if (lyricsText == nullptr || text == lastPanelText) {
        return;
    }

    lastPanelText = text;
    lyricsText->set_text(il2cpp_utils::newcsstr(text));
}

static std::string UrlEncode(const std::string& value) {
    static const char hex[] = "0123456789ABCDEF";

    std::string result;

    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            result += static_cast<char>(c);
        } else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 0x0F];
        }
    }

    return result;
}

static std::string NormalizeForMatch(const std::string& value) {
    std::string result;

    for (unsigned char c : value) {
        if (std::isalnum(c) || c >= 128) {
            result += static_cast<char>(std::tolower(c));
        }
    }

    return result;
}

static std::string ReadJsonString(
    const rapidjson::Value& object,
    const char* key
) {
    if (!object.IsObject()) {
        return {};
    }

    const auto member = object.FindMember(key);

    if (member == object.MemberEnd() || !member->value.IsString()) {
        return {};
    }

    return std::string(
        member->value.GetString(),
        member->value.GetStringLength()
    );
}

// Select a result that matches the song and actually has timed lyrics.
static std::string FindBestSyncedLyrics(const std::string& response) {
    rapidjson::Document document;
    document.Parse(response.c_str());

    if (document.HasParseError() || !document.IsArray()) {
        return {};
    }

    const std::string wantedTitle =
        NormalizeForMatch(currentSongTitle);

    const std::string wantedArtist =
        NormalizeForMatch(currentSongArtist);

    int bestScore = -1000;
    std::string bestLyrics;

    for (const auto& item : document.GetArray()) {
        if (!item.IsObject()) {
            continue;
        }

        const std::string candidateLyrics =
            ReadJsonString(item, "syncedLyrics");

        if (candidateLyrics.empty()) {
            continue;
        }

        const std::string candidateTitle =
            NormalizeForMatch(ReadJsonString(item, "trackName"));

        const std::string candidateArtist =
            NormalizeForMatch(ReadJsonString(item, "artistName"));

                int score = 1;

        if (!wantedTitle.empty() && !candidateTitle.empty()) {
            if (candidateTitle == wantedTitle) {
                score += 6;
            } else if (
                candidateTitle.find(wantedTitle) != std::string::npos ||
                wantedTitle.find(candidateTitle) != std::string::npos
            ) {
                score += 3;
            } else {
                score -= 6;
            }
        }

        if (!wantedArtist.empty() && !candidateArtist.empty()) {
            if (candidateArtist == wantedArtist) {
                score += 4;
            } else if (
                candidateArtist.find(wantedArtist) != std::string::npos ||
                wantedArtist.find(candidateArtist) != std::string::npos
            ) {
                score += 2;
            } else {
                score -= 2;
            }
        }

        if (score > bestScore) {
            bestScore = score;
            bestLyrics = candidateLyrics;
        }
    }

    if (bestScore < 3) {
        return {};
    }

    return bestLyrics;
}

static float currentSongDuration = 0.0f;

static void StartLyricsLookup(
    GlobalNamespace::GameplayCoreInstaller* installer
) {
    if (installer == nullptr) {
        return;
    }

    auto* setupData = installer->__cordl_internal_get__sceneSetupData();

    if (setupData == nullptr) {
        return;
    }

    GlobalNamespace::BeatmapLevel* level = setupData->beatmapLevel;

    if (level == nullptr) {
        lyricsLookupComplete = true;
        lyricsLoaded = false;
        SetPanelText("QuestLyrics\nCouldn't read song info");
        return;
    }

    const std::string title =
        static_cast<std::string>(level->songName);

    const std::string artist =
        static_cast<std::string>(level->songAuthorName);

    if (title.empty()) {
        lyricsLookupComplete = true;
        lyricsLoaded = false;
        SetPanelText("QuestLyrics\nSong title unavailable");
        return;
    }

    // Don't repeat the request for the same song.
    if (title == currentSongTitle &&
        artist == currentSongArtist &&
        (lyricsRequest != nullptr || lyricsLookupComplete)) {
        return;
    }

    if (lyricsRequest != nullptr) {
        lyricsRequest->Abort();
        lyricsRequest->Dispose();
        lyricsRequest = nullptr;
    }

    currentSongTitle = title;
    currentSongArtist = artist;
    currentSongDuration = level->songDuration;

    timedLyrics.clear();
    lyricsLoaded = false;
    lyricsLookupComplete = false;
    lastDisplayedSecond = -1;
    lastPanelText.clear();

    SetPanelText("QuestLyrics\nSearching LRCLIB...");

    std::string url =
        "https://lrclib.net/api/search?track_name=" +
        UrlEncode(currentSongTitle);

    if (!currentSongArtist.empty()) {
        url += "&artist_name=" + UrlEncode(currentSongArtist);
    }

    lyricsRequest =
        UnityEngine::Networking::UnityWebRequest::Get(
            il2cpp_utils::newcsstr(url)
        );

    lyricsRequest->set_timeout(12);
    lyricsRequest->SendWebRequest();

    logger.info("Started LRCLIB lyrics lookup");
}

// Check for completed network requests without blocking gameplay.
static void PollLyricsRequest() {
    if (lyricsRequest == nullptr ||
        !lyricsRequest->get_isDone()) {
        return;
    }

    auto* finishedRequest = lyricsRequest;
    lyricsRequest = nullptr;

    const auto responseCode = finishedRequest->get_responseCode();

    if (responseCode >= 200 && responseCode < 300 &&
        finishedRequest->get_downloadHandler() != nullptr) {
        const std::string response = static_cast<std::string>(
            finishedRequest->get_downloadHandler()->get_text()
        );

        const std::string lrc = FindBestSyncedLyrics(response);

        if (!lrc.empty()) {
            timedLyrics = QuestLyrics::ParseLrc(lrc);
            lyricsLoaded = !timedLyrics.empty();
        }
    }

    finishedRequest->Dispose();

    lyricsLookupComplete = true;

    if (lyricsLoaded) {
        logger.info("Loaded synchronized lyrics");
    } else {
        logger.info("No matching synchronized lyrics found");
    }
}

// Create the panel and look up the selected song.
MAKE_HOOK_MATCH(
    QuestLyrics_GameplayCoreInstaller_InstallBindings,
    &GlobalNamespace::GameplayCoreInstaller::InstallBindings,
    void,
    GlobalNamespace::GameplayCoreInstaller* self
) {
    QuestLyrics_GameplayCoreInstaller_InstallBindings(self);

    CreateLyricsPanel();
    StartLyricsLookup(self);
}

// Synchronize the displayed lines with Beat Saber's song clock.
MAKE_HOOK_MATCH(
    QuestLyrics_AudioTimeSyncController_Update,
    &GlobalNamespace::AudioTimeSyncController::Update,
    void,
    GlobalNamespace::AudioTimeSyncController* self
) {
    QuestLyrics_AudioTimeSyncController_Update(self);

    PollLyricsRequest();

    if (lyricsText == nullptr || self == nullptr) {
        return;
    }

    float songTime = self->get_songTime();

    if (songTime < 0.0f) {
        songTime = 0.0f;
    }

    const int totalSeconds = static_cast<int>(songTime);

    if (!lyricsLookupComplete) {
        if (totalSeconds != lastDisplayedSecond) {
            lastDisplayedSecond = totalSeconds;

            std::string display =
                "QuestLyrics\nSearching lyrics...\nPlayback: ";

            display += std::to_string(totalSeconds / 60);
            display += ":";

            if ((totalSeconds % 60) < 10) {
                display += "0";
            }

            display += std::to_string(totalSeconds % 60);

            SetPanelText(display);
        }

        return;
    }

    if (!lyricsLoaded || timedLyrics.empty()) {
        SetPanelText(
            "QuestLyrics\nNo timed lyrics found"
        );
        return;
    }

    const long long songTimeMs =
        static_cast<long long>(songTime * 1000.0f) - 7000;

    const auto next = std::upper_bound(
        timedLyrics.begin(),
        timedLyrics.end(),
        songTimeMs,
        [](long long time, const QuestLyrics::LyricLine& line) {
            return time < line.timeMs;
        }
    );

    const int currentIndex =
        next == timedLyrics.begin()
            ? -1
            : static_cast<int>(
                std::distance(timedLyrics.begin(), next) - 1
            );

    std::string display;

    if (currentIndex < 0) {
        display = "♪\n" + timedLyrics.front().text;
    } else {
        display = timedLyrics[currentIndex].text;

        const auto upcomingIndex = currentIndex + 1;

        if (upcomingIndex < static_cast<int>(timedLyrics.size())) {
            display += "\n";
            display += timedLyrics[upcomingIndex].text;
        }
    }

    SetPanelText(display);
}

// Register QuestLyrics with the mod loader.
MOD_EXTERN_FUNC void setup(CModInfo* info) noexcept {
    *info = modInfo;
}

// Initialize UI and install hooks.
MOD_EXTERN_FUNC void late_load() noexcept {
    il2cpp_functions::Init();
    BSML::Init();

    INSTALL_HOOK(
        logger,
        QuestLyrics_GameplayCoreInstaller_InstallBindings
    );

    INSTALL_HOOK(
        logger,
        QuestLyrics_AudioTimeSyncController_Update
    );

    logger.info("QuestLyrics initialized");
}
