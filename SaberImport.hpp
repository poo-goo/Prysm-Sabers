#pragma once

#include <string>
#include <vector>

namespace PrysmSabers {
    // Unity bundles found in the Sabers folder (and its extracted/ subfolder), as display names.
    std::vector<std::string> ListSaberBundles();

    // Loads bundle #index, spawns its saber prefab in front of the player, swaps PC-only shaders for ones the
    // game has, and returns a report of what happened. Also writes spawn_report.txt and shaders.txt next to probe.txt.
    std::string SpawnTestSaber(int index);

    void DespawnTestSaber();
    bool TestSaberVisible();
}
