#include "scotland2/shared/modloader.h"
#include "_config.hpp"

static CModInfo modInfo{MOD_ID, VERSION, 0};

// Register QuestLyrics with the mod loader.
MOD_EXTERN_FUNC void setup(CModInfo* info) noexcept {
    *info = modInfo;
}

// Called later during game loading.
MOD_EXTERN_FUNC void late_load() noexcept {
}
