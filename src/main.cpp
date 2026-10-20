#include "scotland2/shared/modloader.h"
#include "_config.hpp"

#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/GameplayCoreInstaller.hpp"
#include "TMPro/TextAlignmentOptions.hpp"

#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-utils.hpp"
#include "beatsaber-hook/shared/utils/logging.hpp"

#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML-Lite/Creation/Misc.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"

#include <string>

static CModInfo modInfo{MOD_ID, VERSION, 0};
constexpr auto logger = Paper::ConstLoggerContext("QuestLyrics");

static BSML::FloatingScreen* lyricsScreen = nullptr;
static HMUI::CurvedTextMeshPro* lyricsText = nullptr;
static int lastDisplayedSecond = -1;

// Create the floating panel when gameplay initializes.
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
        "QuestLyrics\nWaiting for timed lyrics...",
        3.8f,
        UnityEngine::Vector2(0.0f, 0.0f),
        UnityEngine::Vector2(61.0f, 22.0f)
    );

    lyricsText->set_alignment(TMPro::TextAlignmentOptions::Center);
    lyricsScreen->get_gameObject()->set_active(true);

    logger.info("Created QuestLyrics floating panel");
}

// Create the panel after Beat Saber initializes gameplay.
MAKE_HOOK_MATCH(
    QuestLyrics_GameplayCoreInstaller_InstallBindings,
    &GlobalNamespace::GameplayCoreInstaller::InstallBindings,
    void,
    GlobalNamespace::GameplayCoreInstaller* self
) {
    QuestLyrics_GameplayCoreInstaller_InstallBindings(self);
    CreateLyricsPanel();
}

// Read Beat Saber's playback clock.
MAKE_HOOK_MATCH(
    QuestLyrics_AudioTimeSyncController_Update,
    &GlobalNamespace::AudioTimeSyncController::Update,
    void,
    GlobalNamespace::AudioTimeSyncController* self
) {
    QuestLyrics_AudioTimeSyncController_Update(self);

    if (lyricsText == nullptr || self == nullptr) {
        return;
    }

    float songTime = self->get_songTime();

    if (songTime < 0.0f) {
        songTime = 0.0f;
    }

    const int totalSeconds = static_cast<int>(songTime);

    // Update the display once per second rather than every frame.
    if (totalSeconds == lastDisplayedSecond) {
        return;
    }

    lastDisplayedSecond = totalSeconds;

    std::string display = "QuestLyrics\nLyrics lookup is next\nPlayback: ";
    display += std::to_string(totalSeconds / 60);
    display += ":";

    if ((totalSeconds % 60) < 10) {
        display += "0";
    }

    display += std::to_string(totalSeconds % 60);

    lyricsText->set_text(il2cpp_utils::newcsstr(display));
}

// Register QuestLyrics with the mod loader.
MOD_EXTERN_FUNC void setup(CModInfo* info) noexcept {
    *info = modInfo;
}

// Initialize the UI library and install the hooks.
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
