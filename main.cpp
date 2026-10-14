#include "main.hpp"

#include "bsml/shared/BSML.hpp"
#include "config.hpp"
#include "custom-types/shared/register.hpp"
#include "hooks.hpp"
#include "scotland2/shared/modloader.h"
#include "ui.hpp"

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

extern "C" __attribute__((visibility("default"))) void setup(CModInfo* info) noexcept {
    *info = modInfo.to_c();
    getConfig().Init(modInfo);
    logger.info("Completed setup!");
}

extern "C" __attribute__((visibility("default"))) void late_load() noexcept {
    custom_types::Register::AutoRegister();
    BSML::Init();
    BSML::Register::RegisterSettingsMenu("PrismSabers", PrismSabers::SettingsDidActivate, false);
    PrismSabers::InstallHooks();
    logger.info("Completed load!");
}
