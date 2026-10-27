#include "SaberImport.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

#include "SaberProbe.hpp"
#include "UnityEngine/AssetBundle.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Renderer.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector3.hpp"
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

        // options the Import page cycles through; "ORIGINAL" means "the saber's own shader, if a working copy is loaded"
        constexpr char const* HiltOptions[] = {"ORIGINAL", "Custom/SimpleLit", "Legacy Shaders/VertexLit", "Particles/Standard Unlit", "Custom/OpaqueNeonLight", "Sprites/Default"};
        constexpr char const* FireOptions[] = {"ORIGINAL", "Particles/Standard Unlit", "Sprites/Default", "Custom/TransparentNeonLight", "Custom/CustomParticles"};
        int g_hiltOption = 1;
        int g_fireOption = 1;

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
                int start = hilt ? g_hiltOption : g_fireOption;
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

    std::string HiltShaderOption() { return HiltOptions[g_hiltOption]; }
    std::string FireShaderOption() { return FireOptions[g_fireOption]; }
    void NextHiltShader() { g_hiltOption = (g_hiltOption + 1) % (int) std::size(HiltOptions); }
    void NextFireShader() { g_fireOption = (g_fireOption + 1) % (int) std::size(FireOptions); }

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

        auto paths = BundlePaths();
        if (index < 0 || index >= (int) paths.size())
            return "No bundle selected. Scan the folder first.";
        auto const& path = paths[(size_t) index];
        report += "File: " + path.filename().string() + "\n";

        auto loaded = LoadedShaders();
        WriteShaderList(loaded);

        auto bundle = AssetBundle::LoadFromFile(path.string());
        if (!bundle) {
            report += "Unity refused the bundle.\n";
            return report;
        }
        g_bundle = bundle;

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
        std::set<void*> done;
        int renderers = 0, swapped = 0, unsupported = 0;
        auto rs = inst->GetComponentsInChildren<Renderer*>(true);
        for (auto r : rs) {
            if (!r)
                continue;
            renderers++;
            auto mats = r->get_sharedMaterials();
            for (auto m : mats) {
                if (!m)
                    continue;
                auto shader = m->get_shader();
                std::string orig = shader ? Str(shader->get_name()) : "(no shader)";
                bool ok = shader && shader->get_isSupported();
                if (!done.insert((void*) m).second)
                    continue;
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
        report += "Spawned: " + std::to_string(renderers) + " renderers, " + std::to_string(swapped) + " materials swapped, " + std::to_string(unsupported) + " unsupported.\n";
        report += "Look in front of you. shaders.txt lists the game's shaders.\n";

        std::ofstream out(std::string(ProbeFolder) + "/spawn_report.txt");
        if (out)
            out << report;
        logger.info("Spawn test:\n{}", report);
        return report;
    }
}
