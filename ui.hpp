#pragma once

#include "HMUI/FlowCoordinator.hpp"
#include "HMUI/ViewController.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

// Kept for older code paths; the menu no longer attaches it.
DECLARE_CLASS_CODEGEN(PrysmSabers, PrysmTabWatcher, UnityEngine::MonoBehaviour) {
    DECLARE_INSTANCE_METHOD(void, OnEnable);
    DECLARE_INSTANCE_METHOD(void, OnDisable);
    DECLARE_INSTANCE_METHOD(void, OnDestroy);
};

// Left column of a category screen: the sub-tab list (or the saber list on the Custom sabers screen).
DECLARE_CLASS_CODEGEN(PrysmSabers, PrysmNavVC, HMUI::ViewController) {
    DECLARE_OVERRIDE_METHOD_MATCH(void, DidActivate, &HMUI::ViewController::DidActivate, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling);
};

// Main area of a category screen: the controls of the selected sub-tab.
DECLARE_CLASS_CODEGEN(PrysmSabers, PrysmPageVC, HMUI::ViewController) {
    DECLARE_OVERRIDE_METHOD_MATCH(void, DidActivate, &HMUI::ViewController::DidActivate, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling);
    DECLARE_OVERRIDE_METHOD_MATCH(void, DidDeactivate, &HMUI::ViewController::DidDeactivate, bool removedFromHierarchy, bool screenSystemDisabling);
};

// A full-screen menu like the game's Settings: title bar, back button, nav column and content.
DECLARE_CLASS_CODEGEN(PrysmSabers, PrysmFlow, HMUI::FlowCoordinator) {
    DECLARE_OVERRIDE_METHOD_MATCH(void, DidActivate, &HMUI::FlowCoordinator::DidActivate, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling);
    DECLARE_OVERRIDE_METHOD_MATCH(void, BackButtonWasPressed, &HMUI::FlowCoordinator::BackButtonWasPressed, HMUI::ViewController* topViewController);
};

namespace PrysmSabers {
    // Mod Settings page (fallback if the Gameplay Setup tab API is unavailable): shows the category hub
    void SettingsDidActivate(HMUI::ViewController* self, bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling);
    // Player Options > Mods tab: shows the category hub
    void GameplayTabDidActivate(UnityEngine::GameObject* self, bool firstActivation);
}
