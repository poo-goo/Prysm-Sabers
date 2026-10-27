#pragma once

#include <string>

namespace PrysmSabers {
    // Result of a conversion attempt.
    struct ConvertResult {
        bool ok = false;         // true: outPath now holds a bundle that targets Android (or inPath already did)
        bool converted = false;  // true: the file had to be changed (false: it was already an Android bundle)
        std::string message;
    };

    // Turns a PC (Windows) UnityFS bundle into an uncompressed Android one by decompressing it and switching the
    // target platform. Writes outPath only when a change was needed.
    ConvertResult ConvertBundleToAndroid(std::string const& inPath, std::string const& outPath);
}
