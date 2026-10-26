#pragma once

#include <string>

namespace PrysmSabers {
    // Folder the probe scans (put .saber / .whacker files here)
    inline constexpr char const* ProbeFolder = "/sdcard/ModData/com.beatgames.beatsaber/Mods/PrysmSabers/Sabers";

    // Tries to load every .saber / .whacker in ProbeFolder as a Unity asset bundle and reports what happened.
    // Used to find out whether PC (.saber) bundles can load on Quest at all, and what is inside them.
    std::string RunSaberProbe();
}
