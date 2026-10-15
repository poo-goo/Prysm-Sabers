#include "main.hpp"

#include "bsml/shared/BSML.hpp"
#include "config.hpp"
#include "custom-types/shared/register.hpp"
#include "hooks.hpp"
#include "scotland2/shared/modloader.h"
#include "ui.hpp"

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

// Registers the menu as a tab in Player Options (Gameplay Setup > Mods). The BSML call is probed at compile time:
// if this BSML version has a different signature the mod falls back to the Mod Settings page instead of failing to build.
template <class Callback>
static bool TryRegisterGameplayTab(Callback callback) {
    using namespace BSML::Register;
    if constexpr (requires { RegisterGameplaySetupTab(std::string_view("Prysm Sabers"), callback); }) {
        RegisterGameplaySetupTab(std::string_view("Prysm Sabers"), callback);
        return true;
    } else {
        return false;
    }
}

extern "C" __attribute__((visibility("default"))) void setup(CModInfo* info) noexcept {
    *info = modInfo.to_c();
    getConfig().Init(modInfo);
    logger.info("Completed setup!");
}

extern "C" __attribute__((visibility("default"))) void late_load() noexcept {
    custom_types::Register::AutoRegister();
    BSML::Init();
    if (TryRegisterGameplayTab(&PrysmSabers::GameplayTabDidActivate)) {
        logger.info("Registered Prysm Sabers in Player Options");
    } else {
        logger.warn("Gameplay Setup tab API not available in this BSML; using Mod Settings instead");
        BSML::Register::RegisterSettingsMenu("Prysm Sabers", PrysmSabers::SettingsDidActivate, false);
    }
    PrysmSabers::InstallHooks();
    logger.info("Completed load!");
}
