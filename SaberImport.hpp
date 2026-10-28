#pragma once

#include <string>
#include <vector>

#include "GlobalNamespace/SaberModelController.hpp"
#include "UnityEngine/Color.hpp"

namespace PrysmSabers {
    // Unity bundles found in the Sabers folder (and its extracted/ subfolder), as display names.
    std::vector<std::string> ListSaberBundles();

    // Loads bundle #index, spawns its saber prefab in front of the player, swaps PC-only shaders for ones the
    // game has, and returns a report of what happened. Also writes spawn_report.txt and shaders.txt next to probe.txt.
    std::string SpawnTestSaber(int index);

    // shader stand-ins the Import page cycles through (hilt = lit parts, fire = author effect shaders)
    std::string HiltShaderOption();
    std::string FireShaderOption();
    void NextHiltShader();
    void NextFireShader();

    // Full path of bundle #index (as listed by ListSaberBundles), or empty.
    std::string BundlePathAt(int index);

    // Gameplay: puts the imported saber (Left/Right hand model) on a game saber and hides the vanilla meshes.
    // Returns false if anything fails; the game saber is then left alone.
    bool ApplyImportedSaber(GlobalNamespace::SaberModelController* model, bool left, std::string const& file, UnityEngine::Color tint);

    void DespawnTestSaber();
    bool TestSaberVisible();
}
