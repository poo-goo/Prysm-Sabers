#include "ui.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>

#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "PrismTrail.hpp"
#include "SaberProbe.hpp"
#include "SaberImport.hpp"
#include "config.hpp"
#include "main.hpp"

using namespace UnityEngine;

DEFINE_TYPE(PrysmSabers, PrysmTabWatcher);

void PrysmSabers::PrysmTabWatcher::OnEnable() {
    if (getConfig().PreviewEnabled.GetValue())
        PrysmSabers::ShowPreview(get_transform());
}
void PrysmSabers::PrysmTabWatcher::OnDisable() {
    PrysmSabers::HidePreview();
    PrysmSabers::CloseSaberLibrary();
}
void PrysmSabers::PrysmTabWatcher::OnDestroy() {
    PrysmSabers::HidePreview();
    PrysmSabers::CloseSaberLibrary();
}

namespace PrysmSabers {
    namespace {
        // The menu is split into pages. Only one page's controls exist at a time, which keeps the menu
        // fast to open, stops long lists from running off the panel, and avoids overlapping rows.
        constexpr int SectionCount = 12;
        constexpr char const* SectionNames[SectionCount] = {"General", "Colors", "Trail", "Curve", "Ribbon", "Aura", "Blur", "Saber", "Effects", "Particles", "Import", "Preview"};
        int currentSection = 0;

        void FixHeight(UnityEngine::GameObject* go, float height) {
            auto le = go->GetComponent<UnityEngine::UI::LayoutElement*>();
            if (!le)
                le = go->AddComponent<UnityEngine::UI::LayoutElement*>();
            le->set_preferredHeight(height);
            le->set_minHeight(height);
        }

        void Header(Transform* parent, std::string_view text) {
            auto t = BSML::Lite::CreateText(parent, fmt::format("<b><color=#7fdcff>{}</color></b>", text), TMPro::FontStyles::Normal, 5.0f);
            t->set_enableWordWrapping(false);
            t->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
            FixHeight(t->get_gameObject(), 7.0f);  // without a set height the next row is drawn on top of the header
        }

#define LAYER_UI(p)                                                              \
    AddConfigValueToggle(parent, c.p##Enabled);                                  \
    AddConfigValueToggle(parent, c.p##Solid);                                    \
    AddConfigValueIncrementEnum(parent, c.p##ColorSource, ColorSourceNames);     \
    AddConfigValueColorPicker(parent, c.p##CustomColor);                         \
    AddConfigValueSliderIncrement(parent, c.p##Duration, 0.005f, 0.005f, 1.5f);  \
    AddConfigValueSliderIncrement(parent, c.p##Opacity, 0.05f, 0.0f, 1.0f);      \
    AddConfigValueIncrementInt(parent, c.p##Granularity, 2, 2, 96);              \
    AddConfigValueSliderIncrement(parent, c.p##BaseOffset, 0.01f, 0.0f, 1.0f);   \
    AddConfigValueSliderIncrement(parent, c.p##TipOffset, 0.01f, 0.0f, 1.5f);    \
    AddConfigValueSliderIncrement(parent, c.p##Fade, 0.05f, 0.0f, 1.0f);          \
    AddConfigValueSliderIncrement(parent, c.p##WhiteHead, 0.01f, 0.0f, 1.0f);

#define LAYER_SOFT(p) AddConfigValueSliderIncrement(parent, c.p##Softness, 0.05f, 0.0f, 1.0f);

        void Build(Transform* host);

        void Rebuild(UnityW<Transform> host) {
            if (!host)
                return;
            for (int i = host->get_childCount() - 1; i >= 0; i--) {
                auto child = host->GetChild(i)->get_gameObject();
                child->SetActive(false);  // hide immediately; Destroy only happens at end of frame
                Object::Destroy(child);
            }
            Build(host.ptr());
        }

        // ----------------------------------------------------------------------------------------------
        // Saber library: a large floating panel (grab the handle to move it) for picking and equipping sabers
        // ----------------------------------------------------------------------------------------------
        UnityW<GameObject> g_library;
        UnityW<Transform> g_libraryHost;
        int g_libPick = 0;
        int g_libPage = 0;
        std::string g_libStatus = "Pick a saber from the list.";
        constexpr int LibPerPage = 5;

        int LibPages() { return std::max(1, ((int) ListSaberBundles().size() + LibPerPage - 1) / LibPerPage); }

        void BuildLibrary(UnityW<Transform> host);

        void RebuildLibrary() {
            auto host = g_libraryHost;
            if (!host)
                return;
            for (int i = host->get_childCount() - 1; i >= 0; i--) {
                auto child = host->GetChild(i)->get_gameObject();
                child->SetActive(false);
                Object::Destroy(child);
            }
            BuildLibrary(host);
        }

        // keeps only the headline lines of a spawn report (the per-material lines are in spawn_report.txt)
        std::string ShortReport(std::string const& report) {
            std::string out, line;
            for (size_t i = 0; i <= report.size(); i++) {
                if (i == report.size() || report[i] == '\n') {
                    if (!line.empty() && line[0] != ' ' && line.rfind("File:", 0) != 0 && line.rfind("Prefab:", 0) != 0 && line.rfind("Look in front", 0) != 0)
                        out += (out.empty() ? "" : "\n") + line;
                    line.clear();
                } else {
                    line += report[i];
                }
            }
            return out.empty() ? "Done." : out;
        }

        using LibButton = std::pair<std::string, std::function<void()>>;

        void LibraryRow(Transform* parent, std::vector<LibButton> const& buttons) {
            auto row = BSML::Lite::CreateHorizontalLayoutGroup(parent);
            FixHeight(row->get_gameObject(), 11.0f);
            for (auto const& b : buttons)
                BSML::Lite::CreateUIButton(row->get_transform(), b.first, b.second);
        }

        void BuildLibrary(UnityW<Transform> host) {
            auto parent = host.ptr();
            auto& c = getConfig();
            auto bundles = ListSaberBundles();
            int count = (int) bundles.size();
            int pages = LibPages();
            g_libPick = std::clamp(g_libPick, 0, std::max(count - 1, 0));
            g_libPage = std::clamp(g_libPage, 0, pages - 1);

            auto title = BSML::Lite::CreateText(parent, "<b><color=#7fdcff>Saber Library</color></b>", TMPro::FontStyles::Normal, 7.0f);
            FixHeight(title->get_gameObject(), 10.0f);

            std::string equipped = c.SaberModel.GetValue() == 2 ? c.ImportedSaber.GetValue() : std::string();
            if (count == 0) {
                auto none = BSML::Lite::CreateText(parent, "No sabers found.\nPut .saber or .whacker files in\nModData/com.beatgames.beatsaber/Mods/PrysmSabers/Sabers\nthen tap Scan.", TMPro::FontStyles::Normal, 4.0f);
                FixHeight(none->get_gameObject(), 28.0f);
            }
            for (int i = g_libPage * LibPerPage; i < std::min(count, (g_libPage + 1) * LibPerPage); i++) {
                bool picked = i == g_libPick;
                bool inGame = !equipped.empty() && BundlePathAt(i) == equipped;
                std::string label = (picked ? "> " : "   ") + bundles[(size_t) i] + (inGame ? "   [in game]" : "");
                auto button = BSML::Lite::CreateUIButton(parent, label, [i]() {
                    g_libPick = i;
                    RebuildLibrary();
                });
                FixHeight(button->get_gameObject(), 10.0f);
            }
            if (pages > 1) {
                LibraryRow(parent, {{"< Page", []() {
                                         g_libPage = (g_libPage + LibPages() - 1) % LibPages();
                                         RebuildLibrary();
                                     }},
                                    {fmt::format("{}/{}", g_libPage + 1, pages), []() {}},
                                    {"Page >", []() {
                                         g_libPage = (g_libPage + 1) % LibPages();
                                         RebuildLibrary();
                                     }}});
            }

            LibraryRow(parent, {{"Preview", []() {
                                     g_libStatus = ShortReport(SpawnTestSaber(g_libPick));
                                     RebuildLibrary();
                                 }},
                                {"Remove preview", []() {
                                     DespawnTestSaber();
                                     g_libStatus = "Preview removed.";
                                     RebuildLibrary();
                                 }},
                                {"Use in game", []() {
                                     auto file = BundlePathAt(g_libPick);
                                     if (file.empty()) {
                                         g_libStatus = "Pick a saber first (tap Scan if the list is empty).";
                                     } else {
                                         DespawnTestSaber();
                                         getConfig().ImportedSaber.SetValue(file);
                                         getConfig().SaberModel.SetValue(2);
                                         g_libStatus = "Equipped. Start a song to see it in your hands.";
                                     }
                                     RebuildLibrary();
                                 }}});
            LibraryRow(parent, {{"Hilt: " + HiltShaderOption(), []() {
                                     NextHiltShader();
                                     if (TestSaberVisible())
                                         g_libStatus = ShortReport(SpawnTestSaber(g_libPick));
                                     RebuildLibrary();
                                 }},
                                {"Fire: " + FireShaderOption(), []() {
                                     NextFireShader();
                                     if (TestSaberVisible())
                                         g_libStatus = ShortReport(SpawnTestSaber(g_libPick));
                                     RebuildLibrary();
                                 }},
                                {"Scan", []() {
                                     RunSaberProbe();
                                     g_libStatus = "Scanned the Sabers folder.";
                                     RebuildLibrary();
                                 }},
                                {"Close", []() { CloseSaberLibrary(); }}});

            auto status = BSML::Lite::CreateText(parent, g_libStatus, TMPro::FontStyles::Normal, 3.6f);
            FixHeight(status->get_gameObject(), 22.0f);
        }

        // BSML's floating-screen helper differs between versions, so it is only used when this build has it.
        bool CreateLibraryScreen() {
            auto make = [](auto size) -> bool {
                if constexpr (requires { BSML::Lite::CreateFloatingScreen(size, Vector3{0, 1.4f, 2.0f}, Vector3{0, 0, 0}, 0.0f, true, true); }) {
                    auto screen = BSML::Lite::CreateFloatingScreen(size, Vector3{-1.1f, 1.3f, 1.9f}, Vector3{0, -25.0f, 0}, 0.0f, true, true);
                    if (!screen)
                        return false;
                    g_library = screen->get_gameObject();
                    auto layout = BSML::Lite::CreateVerticalLayoutGroup(screen->get_transform());
                    g_libraryHost = layout->get_transform();
                    return true;
                } else {
                    return false;
                }
            };
            return make(Vector2{110.0f, 95.0f});
        }

        void OpenSaberLibrary() {
            if (g_library)
                return;
            if (!CreateLibraryScreen()) {
                logger.warn("Floating screens are not available in this BSML build; saber library not shown");
                return;
            }
            BuildLibrary(g_libraryHost);
        }

        bool SaberLibraryOpen() { return (bool) g_library; }

        // The preview has its own page so its controls do not crowd the General page
        void PreviewControls(Transform* parent, UnityW<Transform> h) {
            auto& c = getConfig();
            BSML::Lite::CreateUIButton(parent, c.PreviewEnabled.GetValue() ? "Live Preview: ON" : "Live Preview: OFF", [h]() {
                bool on = !getConfig().PreviewEnabled.GetValue();
                getConfig().PreviewEnabled.SetValue(on);
                if (on)
                    ShowPreview(const_cast<Transform*>(h.ptr()));
                else
                    HidePreview();
                Rebuild(h);
            });
            {
                auto status = BSML::Lite::CreateText(parent, fmt::format("Preview: {}  |  trail material: {}", PreviewVisible() ? "showing" : "hidden", PreviewAvailable() ? "found" : "missing"), TMPro::FontStyles::Normal, 3.0f);
                FixHeight(status->get_gameObject(), 5.0f);
            }
            if (c.PreviewEnabled.GetValue() && !PreviewAvailable()) {
                auto note = BSML::Lite::CreateText(parent, "Preview needs the game's trail material.\nPlay any song once, then reopen this tab.", TMPro::FontStyles::Normal, 3.2f);
                FixHeight(note->get_gameObject(), 12.0f);
            }
            AddConfigValueSliderIncrement(parent, c.PreviewScale, 0.1f, 0.2f, 2.0f);
            AddConfigValueSliderIncrement(parent, c.PreviewX, 0.1f, -4.0f, 4.0f);
            AddConfigValueSliderIncrement(parent, c.PreviewY, 0.1f, -3.0f, 3.0f);
            AddConfigValueSliderIncrement(parent, c.PreviewZ, 0.1f, -3.0f, 3.0f);
        }

        void Build(Transform* host) {
            auto& c = getConfig();
            UnityW<Transform> h(host);
            auto scroll = BSML::Lite::CreateScrollableSettingsContainer(host);
            auto parent = scroll->get_transform();

            // page navigation
            auto row = BSML::Lite::CreateHorizontalLayoutGroup(parent);
            FixHeight(row->get_gameObject(), 9.0f);
            BSML::Lite::CreateUIButton(row->get_transform(), "< Prev", [h]() {
                currentSection = (currentSection + SectionCount - 1) % SectionCount;
                Rebuild(h);
            });
            BSML::Lite::CreateUIButton(row->get_transform(), "Next >", [h]() {
                currentSection = (currentSection + 1) % SectionCount;
                Rebuild(h);
            });
            Header(parent, fmt::format("{}  ({}/{})", SectionNames[currentSection], currentSection + 1, SectionCount));

            switch (currentSection) {
                case 0:
                    AddConfigValueToggle(parent, c.Enabled);
                    AddConfigValueToggle(parent, c.HideVanillaTrail);
                    AddConfigValueToggle(parent, c.PerformanceMode);
                    AddConfigValueToggle(parent, c.FlipTrailV);
                    Header(parent, "Presets");
                    for (int i = 0; i < (int) PresetNames.size(); i++) {
                        BSML::Lite::CreateUIButton(parent, PresetNames[i], [i, h]() {
                            ApplyPreset(i);
                            Rebuild(h);
                        });
                    }
                    break;
                case 1:
                    AddConfigValueIncrementEnum(parent, c.SaberColorMode, SaberColorModeNames);
                    AddConfigValueColorPicker(parent, c.LeftColor);
                    AddConfigValueColorPicker(parent, c.RightColor);
                    AddConfigValueToggle(parent, c.BladeGradient);
                    AddConfigValueColorPicker(parent, c.GradLeftStart);
                    AddConfigValueColorPicker(parent, c.GradLeftEnd);
                    AddConfigValueColorPicker(parent, c.GradRightStart);
                    AddConfigValueColorPicker(parent, c.GradRightEnd);
                    AddConfigValueSliderIncrement(parent, c.RainbowSpeed, 0.05f, 0.0f, 3.0f);
                    AddConfigValueSliderIncrement(parent, c.RainbowSaturation, 0.05f, 0.0f, 1.0f);
                    AddConfigValueSliderIncrement(parent, c.RainbowOffset, 0.05f, 0.0f, 1.0f);
                    AddConfigValueSliderIncrement(parent, c.GlowIntensity, 0.05f, 0.0f, 3.0f);
                    break;
                case 2:
                    AddConfigValueSliderIncrement(parent, c.CrystalAmount, 0.05f, 0.0f, 1.0f);
                    AddConfigValueIncrementInt(parent, c.CrystalFacets, 1, 3, 24);
                    LAYER_UI(Main)
                    LAYER_SOFT(Main)
                    break;
                case 3:
                    LAYER_UI(Curve)
                    AddConfigValueSliderIncrement(parent, c.CurveBend, 0.25f, 0.0f, 12.0f);
                    AddConfigValueSliderIncrement(parent, c.CurveTaper, 0.1f, 0.0f, 6.0f);
                    break;
                case 4: LAYER_UI(Ribbon) LAYER_SOFT(Ribbon) break;
                case 5: LAYER_UI(Glow) LAYER_SOFT(Glow) break;
                case 6:
                    AddConfigValueToggle(parent, c.BlurSnap);
                    LAYER_UI(Blur)
                    LAYER_SOFT(Blur)
                    AddConfigValueSliderIncrement(parent, c.BlurSpread, 0.05f, 0.0f, 0.8f);
                    AddConfigValueSliderIncrement(parent, c.BlurMinSpeed, 0.5f, 0.0f, 20.0f);
                    AddConfigValueSliderIncrement(parent, c.BlurFullSpeed, 0.5f, 0.5f, 30.0f);
                    break;
                case 7:
                    AddConfigValueIncrementEnum(parent, c.SaberModel, SaberModelNames);
                    AddConfigValueIncrementVector3(parent, c.PositionOffset, 1, 0.5);
                    AddConfigValueIncrementVector3(parent, c.RotationOffset, 0, 5.0);
                    AddConfigValueSliderIncrement(parent, c.LengthScale, 0.05f, 0.25f, 2.0f);
                    AddConfigValueSliderIncrement(parent, c.ThicknessScale, 0.05f, 0.25f, 3.0f);
                    break;
                case 8:
                    AddConfigValueSliderIncrement(parent, c.CutSparkles, 0.1f, 0.0f, 4.0f);
                    AddConfigValueSliderIncrement(parent, c.CutExplosion, 0.1f, 0.0f, 4.0f);
                    AddConfigValueSliderIncrement(parent, c.CutLifetime, 0.1f, 0.1f, 3.0f);
                    AddConfigValueIncrementEnum(parent, c.CutColorMode, CutColorModeNames);
                    AddConfigValueToggle(parent, c.ClashEffect);
                    AddConfigValueToggle(parent, c.BurnMarks);
                    AddConfigValueSliderIncrement(parent, c.BurnLife, 0.1f, 0.1f, 3.0f);
                    AddConfigValueSliderIncrement(parent, c.BurnWidth, 0.5f, 1.0f, 20.0f);
                    AddConfigValueSliderIncrement(parent, c.BurnOpacity, 0.05f, 0.0f, 1.0f);
                    AddConfigValueSliderIncrement(parent, c.BurnFloorOffset, 0.05f, -1.0f, 2.0f);
                    AddConfigValueToggle(parent, c.BurnSparkles);
                    break;
                case 9:
                    AddConfigValueIncrementEnum(parent, c.ParticleStyle, ParticleStyleNames);
                    AddConfigValueSliderIncrement(parent, c.ParticleAmount, 0.1f, 0.1f, 3.0f);
                    AddConfigValueSliderIncrement(parent, c.ParticleSize, 0.1f, 0.3f, 3.0f);
                    AddConfigValueSliderIncrement(parent, c.ParticleLife, 0.1f, 0.3f, 3.0f);
                    AddConfigValueToggle(parent, c.FlameBlade);
                    break;
                case 10: {
                    Header(parent, "Saber library");
                    auto info = BSML::Lite::CreateText(parent, "Put .saber or .whacker files in the Sabers folder, then open the library to preview and equip them.", TMPro::FontStyles::Normal, 3.2f);
                    FixHeight(info->get_gameObject(), 14.0f);
                    BSML::Lite::CreateUIButton(parent, SaberLibraryOpen() ? "Close library" : "Open saber library", [h]() {
                        if (SaberLibraryOpen())
                            CloseSaberLibrary();
                        else
                            OpenSaberLibrary();
                        Rebuild(h);
                    });
                    std::string equippedText = "No imported saber equipped";
                    if (c.SaberModel.GetValue() == 2 && !c.ImportedSaber.GetValue().empty())
                        equippedText = "In game: " + std::filesystem::path(c.ImportedSaber.GetValue()).filename().string();
                    auto equipped = BSML::Lite::CreateText(parent, equippedText, TMPro::FontStyles::Normal, 3.4f);
                    FixHeight(equipped->get_gameObject(), 8.0f);
                    break;
                }
                default: PreviewControls(parent, h); break;
            }
        }
    }

    void CloseSaberLibrary() {
        if (g_library)
            Object::Destroy(g_library.ptr());
        g_library = UnityW<GameObject>((GameObject*) nullptr);
        g_libraryHost = UnityW<Transform>((Transform*) nullptr);
    }

    void SettingsDidActivate(HMUI::ViewController* self, bool firstActivation, bool, bool) {
        if (firstActivation) {
            Build(self->get_transform());
            self->get_gameObject()->AddComponent<PrysmTabWatcher*>();
        }
    }

    void GameplayTabDidActivate(GameObject* self, bool firstActivation) {
        if (firstActivation) {
            Build(self->get_transform());
            self->AddComponent<PrysmTabWatcher*>();
        }
    }
}
