#include "scotland2/shared/modloader.h"
#include "_config.hpp"

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

// Register QuestLyrics with the mod loader.
MOD_EXTERN_FUNC void setup(CModInfo* info) noexcept {
    *info = modInfo.to_c();
}

// Called later during game loading.
MOD_EXTERN_FUNC void late_load() noexcept {
}
