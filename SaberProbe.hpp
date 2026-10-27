#pragma once

#include <string>

namespace PrysmSabers {
    // Folder the probe scans (put .saber / .whacker files here)
    inline constexpr char const* ProbeFolder = "/sdcard/ModData/com.beatgames.beatsaber/Mods/PrysmSabers/Sabers";

    // Tries to load every Unity bundle in ProbeFolder and reports what happened.
    // .whacker files (ZIPs) are unpacked into ProbeFolder/extracted and their bundles are probed too.
    std::string RunSaberProbe();
}
