#include "SaberImport.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

#include "BundleConvert.hpp"
#include "GlobalNamespace/SaberModelController.hpp"
#include "SaberProbe.hpp"
#include "UnityEngine/AssetBundle.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Renderer.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector3.hpp"
#include "config.hpp"
#include "main.hpp"

using namespace UnityEngine;

namespace PrysmSabers {
    namespace {
        namespace fs = std::filesystem;

        UnityW<GameObject> g_instance;
        UnityW<AssetBundle> g_bundle;

        std::string Str(StringW s) { return s ? static_cast<std::string>(s) : std::string(); }

        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return (char) std::tolower(ch); });
            return s;
        }

        bool IsUnityFS(fs::path const& path) {
            std::ifstream f(path, std::ios::binary);
            char m[7] = {};
            f.read(m, 7);
            return f.gcount() == 7 && std::string(m, 7) == "UnityFS";
        }

        std::vector<fs::path> BundlePaths() {
            std::vector<fs::path> out;
            std::error_code ec;
            for (auto const& dir : {fs::path(ProbeFolder), fs::path(ProbeFolder) / "extracted"}) {
                std::vector<fs::path> here;
                for (auto const& entry : fs::directory_iterator(dir, ec)) {
                    if (entry.is_regular_file() && IsUnityFS(entry.path()))
                        here.push_back(entry.path());
                }
                std::sort(here.begin(), here.end());
                out.insert(out.end(), here.begin(), here.end());
            }
            return out;
        }

        // Loads an asset by name. The generic helper exists in some bs-cordl versions, the plain one in others.
        template <class T>
        T LoadAssetAs(AssetBundle* bundle, StringW name) {
            if constexpr (requires { bundle->template LoadAsset<T>(name); }) {
                return bundle->template LoadAsset<T>(name);
            } else {
                return static_cast<T>(bundle->LoadAsset(name));
            }
        }

        GameObject* InstantiateCopy(GameObject* prefab) {
            if constexpr (requires { Object::Instantiate(prefab); }) {
                return static_cast<GameObject*>(Object::Instantiate(prefab));
            } else {
                return nullptr;
            }
        }

        // every shader currently loaded in the game (this includes ones from other mods and from the saber bundle itself)
        using ShaderList = std::vector<std::pair<std::string, Shader*>>;

        ShaderList LoadedShaders() {
            ShaderList all;
            auto shaders = Resources::FindObjectsOfTypeAll<Shader*>();
            for (auto s : shaders) {
                if (s)
                    all.emplace_back(Str(s->get_name()), s);
            }
            return all;
        }

        void WriteShaderList(ShaderList const& all) {
            std::set<std::string> lines;
            for (auto const& [name, s] : all)
                lines.insert(name + (s->get_isSupported() ? "" : "   (not supported)"));
            std::ofstream out(std::string(ProbeFolder) + "/shaders.txt");
            for (auto const& l : lines)
                out << l << "\n";
        }

        // first shader with this exact name that actually works on this device
        Shader* SupportedByName(ShaderList const& all, std::string const& name) {
            for (auto const& [n, s] : all) {
                if (n == name && s->get_isSupported())
                    return s;
            }
            return nullptr;
        }

        // options the Import page cycles through; "ORIGINAL" means "the saber's own shader, if a working copy is loaded".
        // Defaults are ORIGINAL, which falls through to SimpleLit (hilt) / Sprites/Default (fire) when no working copy exists.
        // Fire shells must stay see-through: an opaque shader there can black out the whole view.
        constexpr char const* HiltOptions[] = {"ORIGINAL", "Custom/SimpleLit", "Legacy Shaders/VertexLit", "Particles/Standard Unlit", "Custom/OpaqueNeonLight", "Sprites/Default"};
        constexpr char const* FireOptions[] = {"ORIGINAL", "Custom/CustomParticles", "Sprites/Default", "Particles/Standard Unlit", "Custom/Glowing"};
        // the shader choices are saved in the config, so gameplay uses what you picked on the Import page
        int HiltIndex() { return std::clamp(getConfig().ImportedHiltShader.GetValue(), 0, (int) std::size(HiltOptions) - 1); }
        int FireIndex() { return std::clamp(getConfig().ImportedFireShader.GetValue(), 0, (int) std::size(FireOptions) - 1); }

        // original shader name and support flag of each material, remembered before the first swap
        // (materials are shared with the cached prefab, so a second spawn would otherwise see the stand-in shader)
        std::map<void*, std::pair<std::string, bool>> g_originals;

        // Pick a working shader for a saber shader that only exists as a PC build.
        Shader* ReplacementFor(ShaderList const& all, std::string const& original, std::string& chosen) {
            auto o = Lower(original);
            bool unlit = o.find("unlit glow") != std::string::npos;
            bool hilt = !unlit && (o.find("lit glow") != std::string::npos || o.find("matcap") != std::string::npos);

            if (unlit) {
                if (auto s = SupportedByName(all, "Sprites/Default")) {
                    chosen = "Sprites/Default";
                    return s;
                }
            } else {
                int start = hilt ? HiltIndex() : FireIndex();
                auto count = hilt ? std::size(HiltOptions) : std::size(FireOptions);
                for (size_t i = 0; i < count; i++) {
                    std::string name = hilt ? HiltOptions[(start + i) % count] : FireOptions[(start + i) % count];
                    if (name == "ORIGINAL")
                        name = original;
                    if (auto s = SupportedByName(all, name)) {
                        chosen = name;
                        return s;
                    }
                }
            }
            chosen = "(none found)";
            return nullptr;
        }
    }

    std::string HiltShaderOption() { return HiltOptions[HiltIndex()]; }
    std::string FireShaderOption() { return FireOptions[FireIndex()]; }
    void NextHiltShader() { getConfig().ImportedHiltShader.SetValue((HiltIndex() + 1) % (int) std::size(HiltOptions)); }
    void NextFireShader() { getConfig().ImportedFireShader.SetValue((FireIndex() + 1) % (int) std::size(FireOptions)); }

    namespace {
        // Converts Windows bundles to Android (cached in Sabers/converted). Returns the path to load, or empty on failure.
        fs::path PrepareLoadPath(fs::path const& path, std::string& report) {
            std::error_code ec;
            auto convDir = fs::path(ProbeFolder) / "converted";
            fs::create_directories(convDir, ec);
            auto convPath = convDir / (path.stem().string() + ".android.bundle");
            if (fs::exists(convPath, ec) && fs::last_write_time(convPath, ec) >= fs::last_write_time(path, ec)) {
                report += "Convert: using the converted copy from last time\n";
                return convPath;
            }
            auto conv = ConvertBundleToAndroid(path.string(), convPath.string());
            report += "Convert: " + conv.message + "\n";
            if (!conv.ok)
                return {};
            return conv.converted ? convPath : path;
        }

        // Replaces PC-only shaders on every renderer under root with ones that work on this device.
        void SwapShaders(GameObject* root, ShaderList const& loaded, std::string& report, int& renderers, int& swapped, int& unsupported, Color const* tint = nullptr) {
            std::set<void*> done;
            auto rs = root->GetComponentsInChildren<Renderer*>(true);
            for (auto r : rs) {
                if (!r)
                    continue;
                renderers++;
                auto mats = r->get_sharedMaterials();
                for (auto m : mats) {
                    if (!m)
                        continue;
                    if (!done.insert((void*) m).second)
                        continue;
                    std::string orig;
                    bool ok;
                    if (auto it = g_originals.find((void*) m); it != g_originals.end()) {
                        orig = it->second.first;
                        ok = it->second.second;
                    } else {
                        auto shader = m->get_shader();
                        orig = shader ? Str(shader->get_name()) : "(no shader)";
                        ok = shader && shader->get_isSupported();
                        g_originals[(void*) m] = {orig, ok};
                    }
                    // saber shaders with a _CustomColors switch expect the game's saber color in _Color
                    if (tint && m->HasProperty("_CustomColors") && m->GetFloat("_CustomColors") > 0.0f)
                        m->SetColor("_Color", *tint);
                    std::string chosen;
                    auto repl = ReplacementFor(loaded, orig, chosen);
                    report += "  mat '" + Str(m->get_name()) + "'  " + orig + (ok ? "  [supported]" : "  [NOT supported]") + " -> " + chosen + "\n";
                    if (!ok)
                        unsupported++;
                    if (repl) {
                        // keeps _MainTex / _Color, which the stand-in shaders also use
                        if (m->HasProperty("_Color")) {
                            auto tint = m->get_color();
                            m->set_shader(repl);
                            if (m->HasProperty("_TintColor"))
                                m->SetColor("_TintColor", tint);
                        } else {
                            m->set_shader(repl);
                        }
                        swapped++;
                    }
                }
            }
        }

        // the imported saber bundle used during gameplay stays loaded for the whole session
        UnityW<AssetBundle> g_gameBundle;
        UnityW<GameObject> g_gamePrefab;
        std::string g_gamePath;

        // gameplay messages go to game_report.txt next to probe.txt (the file is restarted once per game session)
        void GameLog(std::string const& msg) {
            static bool first = true;
            std::ofstream out(std::string(ProbeFolder) + "/game_report.txt", first ? std::ios::trunc : std::ios::app);
            first = false;
            if (out)
                out << msg << "\n";
            logger.info("Imported saber: {}", msg);
        }

        // Unity refuses to load the same bundle twice, so only one of the two users (spawn test / gameplay) may hold it
        void ReleaseGameBundle() {
            if (g_gameBundle) {
                g_gameBundle->Unload(false);
                g_gameBundle = UnityW<AssetBundle>((AssetBundle*) nullptr);
            }
            g_gamePrefab = UnityW<GameObject>((GameObject*) nullptr);
            g_gamePath.clear();
        }
    }

    std::string BundlePathAt(int index) {
        auto paths = BundlePaths();
        return index >= 0 && index < (int) paths.size() ? paths[(size_t) index].string() : std::string();
    }

    bool ApplyImportedSaber(GlobalNamespace::SaberModelController* model, bool left, std::string const& file, UnityEngine::Color tint) {
        GameLog(std::string("Applying to the ") + (left ? "left" : "right") + " saber: " + file);
        if (file.empty() || !model) {
            GameLog("  no saber file chosen. On the Import page tap 'Use this saber in game'.");
            return false;
        }
        std::error_code ec;
        fs::path path(file);
        if (!fs::exists(path, ec)) {
            GameLog("  file not found");
            return false;
        }

        if (!g_gamePrefab || g_gamePath != file) {
            ReleaseGameBundle();
            DespawnTestSaber();  // the Import page spawn test may still hold this bundle
            g_originals.clear();

            std::string report;
            auto loadPath = PrepareLoadPath(path, report);
            GameLog("  " + report);
            if (loadPath.empty())
                return false;
            auto bundle = AssetBundle::LoadFromFile(loadPath.string());
            if (!bundle) {
                GameLog("  Unity refused the bundle: " + loadPath.string());
                return false;
            }
            std::string prefabName;
            if (auto names = bundle->GetAllAssetNames()) {
                for (auto n : names) {
                    if (Lower(Str(n)).ends_with(".prefab")) {
                        prefabName = Str(n);
                        break;
                    }
                }
            }
            auto prefab = prefabName.empty() ? nullptr : LoadAssetAs<GameObject*>(bundle, prefabName);
            if (!prefab) {
                GameLog("  no prefab found in the bundle");
                bundle->Unload(false);
                return false;
            }
            g_gameBundle = bundle;
            g_gamePrefab = prefab;
            g_gamePath = file;
            GameLog("  loaded prefab " + prefabName);
        }

        auto inst = InstantiateCopy(g_gamePrefab.ptr());
        if (!inst) {
            GameLog("  could not instantiate the prefab");
            return false;
        }
        inst->SetActive(true);
        auto instT = inst->get_transform();
        auto hand = instT->Find(left ? "LeftSaber" : "RightSaber");
        if (!hand)
            hand = instT->Find(left ? "RightSaber" : "LeftSaber");
        if (!hand)
            hand = instT;

        // hide the game's own saber meshes first, so the new model is not hidden with them
        auto modelT = model->get_transform();
        int layer = model->get_gameObject()->get_layer();
        auto vanilla = model->GetComponentsInChildren<MeshRenderer*>(true);
        for (auto r : vanilla) {
            if (r)
                r->set_enabled(false);
        }

        hand->SetParent(modelT, false);
        hand->set_localPosition(Vector3{0, 0, 0});
        hand->set_localEulerAngles(Vector3{0, 0, 0});
        hand->set_localScale(Vector3{1, 1, 1});
        for (auto t : hand->GetComponentsInChildren<Transform*>(true)) {
            if (t)
                t->get_gameObject()->set_layer(layer);
        }
        if (hand != instT)
            Object::Destroy(inst);  // the unused hand and the root go away; the chosen hand now lives under the game saber

        std::string report;
        int renderers = 0, swapped = 0, unsupported = 0;
        SwapShaders(hand->get_gameObject(), LoadedShaders(), report, renderers, swapped, unsupported, &tint);
        GameLog("  done: " + std::to_string(renderers) + " renderers, " + std::to_string(swapped) + " materials swapped, " + std::to_string(unsupported) + " unsupported\n" + report);
        return true;
    }

    std::vector<std::string> ListSaberBundles() {
        std::vector<std::string> names;
        for (auto const& p : BundlePaths())
            names.push_back(p.parent_path().filename() == "extracted" ? "extracted/" + p.filename().string() : p.filename().string());
        return names;
    }

    bool TestSaberVisible() { return (bool) g_instance; }

    void DespawnTestSaber() {
        if (g_instance)
            Object::Destroy(g_instance.ptr());
        g_instance = UnityW<GameObject>((GameObject*) nullptr);
        if (g_bundle) {
            g_bundle->Unload(false);
            g_bundle = UnityW<AssetBundle>((AssetBundle*) nullptr);
        }
    }

    std::string SpawnTestSaber(int index) {
        std::string report;
        DespawnTestSaber();
        ReleaseGameBundle();  // gameplay loads it again on the next song

        auto paths = BundlePaths();
        if (index < 0 || index >= (int) paths.size())
            return "No bundle selected. Scan the folder first.";
        auto const& path = paths[(size_t) index];
        report += "File: " + path.filename().string() + "\n";

        auto loaded = LoadedShaders();
        WriteShaderList(loaded);

        fs::path loadPath = PrepareLoadPath(path, report);
        if (loadPath.empty())
            return report;

        auto bundle = AssetBundle::LoadFromFile(loadPath.string());
        if (!bundle) {
            report += "Unity refused the bundle.\n";
            return report;
        }
        g_bundle = bundle;
        g_originals.clear();

        // find the saber prefab (lower-case asset path, e.g. assets/_customsaber.prefab)
        std::string prefabName;
        if (auto names = bundle->GetAllAssetNames()) {
            for (auto n : names) {
                auto s = Str(n);
                if (Lower(s).ends_with(".prefab")) {
                    prefabName = s;
                    break;
                }
            }
        }
        if (prefabName.empty()) {
            report += "No prefab inside the bundle.\n";
            DespawnTestSaber();
            return report;
        }
        report += "Prefab: " + prefabName + "\n";

        auto prefab = LoadAssetAs<GameObject*>(bundle, prefabName);
        if (!prefab) {
            report += "Could not load the prefab.\n";
            DespawnTestSaber();
            return report;
        }
        auto inst = InstantiateCopy(prefab);
        if (!inst) {
            report += "Could not spawn the prefab (Instantiate not available in this build).\n";
            DespawnTestSaber();
            return report;
        }
        inst->set_name("PrysmTestSaber");
        inst->SetActive(true);
        g_instance = inst;

        // stand it upright about 1.5 m in front of the head, a bit below eye level
        Vector3 pos{0, 1.0f, 1.5f};
        if (auto cam = Camera::get_main()) {
            auto t = cam->get_transform();
            auto o = t->get_position();
            auto f = t->get_forward();
            f.y = 0;
            float len = std::sqrt(f.x * f.x + f.z * f.z);
            if (len > 0.01f) {
                f.x /= len;
                f.z /= len;
                pos = Vector3{o.x + f.x * 1.5f, o.y - 0.5f, o.z + f.z * 1.5f};
            }
        }
        auto tr = inst->get_transform();
        tr->set_position(pos);
        tr->set_eulerAngles(Vector3{-90.0f, 0.0f, 0.0f});

        // shader swap
        int renderers = 0, swapped = 0, unsupported = 0;
        SwapShaders(inst, loaded, report, renderers, swapped, unsupported);
        report += "Spawned: " + std::to_string(renderers) + " renderers, " + std::to_string(swapped) + " materials swapped, " + std::to_string(unsupported) + " unsupported.\n";
        report += "Look in front of you. shaders.txt lists the game's shaders.\n";

        std::ofstream out(std::string(ProbeFolder) + "/spawn_report.txt");
        if (out)
            out << report;
        logger.info("Spawn test:\n{}", report);
        return report;
    }
}
