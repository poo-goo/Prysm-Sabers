#include "SaberProbe.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "UnityEngine/AssetBundle.hpp"
#include "main.hpp"

namespace PrysmSabers {
    std::string RunSaberProbe() {
        std::string report;
        std::error_code ec;
        std::filesystem::create_directories(ProbeFolder, ec);

        std::vector<std::filesystem::path> files;
        for (auto const& entry : std::filesystem::directory_iterator(ProbeFolder, ec)) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return (char) std::tolower(ch); });
            if (entry.is_regular_file() && (ext == ".saber" || ext == ".whacker" || ext == ".qsaber"))
                files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());

        if (files.empty()) {
            report = std::string("No .saber / .whacker files found in\n") + ProbeFolder + "\nCopy some there and scan again.";
        }

        for (auto const& file : files) {
            std::string name = file.filename().string();
            std::error_code sizeEc;
            auto size = std::filesystem::file_size(file, sizeEc);
            report += name + "  (" + std::to_string(size / 1024) + " KB)\n";
            logger.info("Saber probe: loading {}", name);
            auto bundle = UnityEngine::AssetBundle::LoadFromFile(file.string());
            if (!bundle) {
                report += "  LOAD FAILED (Unity refused the bundle)\n";
                continue;
            }
            auto names = bundle->GetAllAssetNames();
            report += "  loaded OK, " + std::to_string(names ? names.size() : 0) + " named assets\n";
            int shown = 0;
            if (names) {
                for (auto n : names) {
                    if (shown++ >= 6)
                        break;
                    report += "    " + static_cast<std::string>(n) + "\n";
                }
            }
            bundle->Unload(true);
        }

        std::ofstream out(std::string(ProbeFolder) + "/probe.txt");
        if (out)
            out << report;
        logger.info("Saber probe finished:\n{}", report);
        return report;
    }
}
