#include "SaberProbe.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "UnityEngine/AssetBundle.hpp"
#include "main.hpp"

namespace PrysmSabers {
    namespace {
        namespace fs = std::filesystem;

        uint16_t Rd16(uint8_t const* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
        uint32_t Rd32(uint8_t const* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }

        bool ReadFile(fs::path const& path, std::vector<uint8_t>& out) {
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (!f)
                return false;
            auto size = (size_t) f.tellg();
            if (size > 256u * 1024u * 1024u)
                return false;
            out.resize(size);
            f.seekg(0);
            f.read((char*) out.data(), (std::streamsize) size);
            return (size_t) f.gcount() == size;
        }

        std::string Magic(fs::path const& path) {
            std::ifstream f(path, std::ios::binary);
            char m[7] = {};
            f.read(m, 7);
            return std::string(m, (size_t) f.gcount());
        }

        std::string SafeName(std::string name) {
            auto slash = name.find_last_of("/\\");
            if (slash != std::string::npos)
                name = name.substr(slash + 1);
            for (auto& ch : name)
                if (!(std::isalnum((unsigned char) ch) || ch == '.' || ch == '_' || ch == '-'))
                    ch = '_';
            return name;
        }

        // Inflates one ZIP entry (stored or deflated) into out. Returns false on any problem.
        bool ExtractEntry(std::vector<uint8_t> const& zip, uint32_t localOff, uint16_t method, uint32_t csize, uint32_t usize, std::vector<uint8_t>& out) {
            if ((size_t) localOff + 30 > zip.size() || Rd32(&zip[localOff]) != 0x04034b50)
                return false;
            size_t dataOff = (size_t) localOff + 30 + Rd16(&zip[localOff + 26]) + Rd16(&zip[localOff + 28]);
            if (dataOff + csize > zip.size())
                return false;
            if (method == 0) {
                out.assign(zip.begin() + (long) dataOff, zip.begin() + (long) (dataOff + csize));
                return true;
            }
            if (method != 8)
                return false;
            out.resize(usize);
            z_stream s = {};
            if (inflateInit2(&s, -15) != Z_OK)
                return false;
            s.next_in = const_cast<Bytef*>(&zip[dataOff]);
            s.avail_in = csize;
            s.next_out = out.data();
            s.avail_out = usize;
            int rc = inflate(&s, Z_FINISH);
            inflateEnd(&s);
            return rc == Z_STREAM_END && s.total_out == usize;
        }

        // Loads one bundle file with Unity and appends the result to report.
        void ProbeBundle(fs::path const& file, std::string const& label, std::string& report) {
            std::error_code ec;
            auto size = fs::file_size(file, ec);
            report += label + "  (" + std::to_string(size / 1024) + " KB)\n";
            logger.info("Saber probe: loading {}", label);
            auto bundle = UnityEngine::AssetBundle::LoadFromFile(file.string());
            if (!bundle) {
                report += "  LOAD FAILED (Unity refused the bundle)\n";
                return;
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

        // Unpacks a .whacker (ZIP): writes every Unity bundle inside to ProbeFolder/extracted and probes it.
        void ProbeWhacker(fs::path const& file, std::string& report) {
            std::string name = file.filename().string();
            report += name + "  (ZIP / .whacker)\n";

            std::vector<uint8_t> zip;
            if (!ReadFile(file, zip) || zip.size() < 22) {
                report += "  could not read the file\n";
                return;
            }

            // find the end-of-central-directory record
            size_t eocd = std::string::npos;
            size_t lowest = zip.size() > 65557 ? zip.size() - 65557 : 0;
            for (size_t i = zip.size() - 22 + 1; i-- > lowest;) {
                if (Rd32(&zip[i]) == 0x06054b50) {
                    eocd = i;
                    break;
                }
            }
            if (eocd == std::string::npos) {
                report += "  not a valid ZIP (no central directory)\n";
                return;
            }

            uint16_t count = Rd16(&zip[eocd + 10]);
            size_t p = Rd32(&zip[eocd + 16]);
            fs::path outDir = fs::path(ProbeFolder) / "extracted";
            std::error_code ec;
            fs::create_directories(outDir, ec);

            std::vector<std::pair<fs::path, std::string>> bundles;
            for (uint16_t i = 0; i < count; i++) {
                if (p + 46 > zip.size() || Rd32(&zip[p]) != 0x02014b50) {
                    report += "  central directory is damaged\n";
                    break;
                }
                uint16_t method = Rd16(&zip[p + 10]);
                uint32_t csize = Rd32(&zip[p + 20]);
                uint32_t usize = Rd32(&zip[p + 24]);
                uint16_t nameLen = Rd16(&zip[p + 28]);
                uint16_t extraLen = Rd16(&zip[p + 30]);
                uint16_t commentLen = Rd16(&zip[p + 32]);
                uint32_t localOff = Rd32(&zip[p + 42]);
                if (p + 46 + nameLen > zip.size())
                    break;
                std::string entry((char const*) &zip[p + 46], nameLen);
                p += 46 + (size_t) nameLen + extraLen + commentLen;

                if (entry.empty() || entry.back() == '/')
                    continue;
                if (csize == 0xFFFFFFFFu || usize == 0xFFFFFFFFu) {
                    report += "  " + entry + ": ZIP64 entry, not supported\n";
                    continue;
                }

                std::vector<uint8_t> data;
                if (!ExtractEntry(zip, localOff, method, csize, usize, data)) {
                    report += "  " + entry + ": could not extract (method " + std::to_string(method) + ")\n";
                    continue;
                }

                std::string lower = SafeName(entry);
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return (char) std::tolower(ch); });
                if (lower == "package.json") {
                    report += "  package.json: " + std::string(data.begin(), data.begin() + (long) std::min<size_t>(data.size(), 300)) + "\n";
                } else if (data.size() >= 7 && std::string((char const*) data.data(), 7) == "UnityFS") {
                    auto outPath = outDir / (fs::path(name).stem().string() + "__" + SafeName(entry));
                    std::ofstream o(outPath, std::ios::binary);
                    o.write((char const*) data.data(), (std::streamsize) data.size());
                    o.close();
                    report += "  extracted " + entry + " (" + std::to_string(data.size() / 1024) + " KB)\n";
                    bundles.emplace_back(outPath, entry);
                }
            }

            if (bundles.empty())
                report += "  no Unity bundles found inside\n";
            for (auto const& [path, entry] : bundles)
                ProbeBundle(path, "  -> " + entry, report);
        }
    }

    std::string RunSaberProbe() {
        std::string report;
        std::error_code ec;
        fs::create_directories(ProbeFolder, ec);

        std::vector<fs::path> files;
        for (auto const& entry : fs::directory_iterator(ProbeFolder, ec)) {
            if (entry.is_regular_file() && entry.path().filename() != "probe.txt")
                files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());

        if (files.empty())
            report = std::string("No files found in\n") + ProbeFolder + "\nCopy some .saber / .whacker files there and scan again.";

        for (auto const& file : files) {
            auto m = Magic(file);
            if (m == "UnityFS")
                ProbeBundle(file, file.filename().string(), report);
            else if (m.size() >= 2 && m[0] == 'P' && m[1] == 'K')
                ProbeWhacker(file, report);
            else
                report += file.filename().string() + ": not a Unity bundle or ZIP, skipped\n";
        }

        std::ofstream out(std::string(ProbeFolder) + "/probe.txt");
        if (out)
            out << report;
        logger.info("Saber probe finished:\n{}", report);
        return report;
    }
}
