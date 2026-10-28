#pragma once

#include "HMUI/ViewController.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

// Sits on the menu's root object: the live preview exists only while the menu is visible.
DECLARE_CLASS_CODEGEN(PrysmSabers, PrysmTabWatcher, UnityEngine::MonoBehaviour) {
    DECLARE_INSTANCE_METHOD(void, OnEnable);
    DECLARE_INSTANCE_METHOD(void, OnDisable);
    DECLARE_INSTANCE_METHOD(void, OnDestroy);
};

namespace PrysmSabers {
    // Closes the floating saber library panel if it is open
    void CloseSaberLibrary();
    // Mod Settings page (fallback if the Gameplay Setup tab API is unavailable)
    void SettingsDidActivate(HMUI::ViewController* self, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling);
    // Player Options > Mods tab
    void GameplayTabDidActivate(UnityEngine::GameObject* self, bool firstActivation);
}
