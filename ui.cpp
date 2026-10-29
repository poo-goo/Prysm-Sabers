#include "ui.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "PrismTrail.hpp"
#include "SaberImport.hpp"
#include "SaberProbe.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/BSML.hpp"
#include "bsml/shared/Helpers/creation.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "config.hpp"
#include "main.hpp"

using namespace UnityEngine;

DEFINE_TYPE(PrysmSabers, PrysmTabWatcher);
DEFINE_TYPE(PrysmSabers, PrysmNavVC);
DEFINE_TYPE(PrysmSabers, PrysmPageVC);
DEFINE_TYPE(PrysmSabers, PrysmFlow);

void PrysmSabers::PrysmTabWatcher::OnEnable() {}
void PrysmSabers::PrysmTabWatcher::OnDisable() { PrysmSabers::HidePreview(); }
void PrysmSabers::PrysmTabWatcher::OnDestroy() { PrysmSabers::HidePreview(); }

namespace PrysmSabers {
    namespace {
        // ------------------------------------------------------------------------------------------------
        // Layout of the menu: a hub of categories, each category a screen with sub-tabs on the left
        // ------------------------------------------------------------------------------------------------
        enum PageId {
            PresetsPage, MainPage, CurvePage, RibbonPage, AuraPage, BlurPage,
            ModelPage, SizePage, PositionPage,
            GeneralPage, QualityPage, PreviewPage,
            ColorsPage, GradientPage, RainbowPage,
            CutPage, BurnPage, ParticlesPage,
            SaberListPage
        };

        struct PageDef {
            char const* name;
            int id;
        };
        struct CategoryDef {
            char const* name;
            std::vector<PageDef> pages;
        };

        std::vector<CategoryDef> const& Categories() {
            static std::vector<CategoryDef> const cats = {
                {"Trails", {{"Presets", PresetsPage}, {"Main", MainPage}, {"Curve", CurvePage}, {"Ribbon", RibbonPage}, {"Aura", AuraPage}, {"Blur", BlurPage}}},
                {"Saber settings", {{"Model", ModelPage}, {"Size", SizePage}, {"Position", PositionPage}}},
                {"Overrides", {{"General", GeneralPage}, {"Quality", QualityPage}, {"Preview", PreviewPage}}},
                {"Color settings", {{"Colors", ColorsPage}, {"Gradient", GradientPage}, {"Rainbow", RainbowPage}}},
                {"Effects", {{"Cut effects", CutPage}, {"Burn marks", BurnPage}, {"Particles", ParticlesPage}}},
                {"Custom sabers", {{"Sabers", SaberListPage}}},
            };
            return cats;
        }

        int g_cat = 0;
        int g_page = 0;
        int g_pick = 0;  // selected saber on the Custom sabers screen
        std::string g_status = "Pick a saber from the list.";
        UnityW<Transform> g_navHost;
        UnityW<Transform> g_pageHost;

        bool IsSaberCategory() { return g_cat == (int) Categories().size() - 1; }

        void FixHeight(GameObject* go, float height) {
            auto le = go->GetComponent<UI::LayoutElement*>();
            if (!le)
                le = go->AddComponent<UI::LayoutElement*>();
            le->set_preferredHeight(height);
            le->set_minHeight(height);
        }

        void Header(Transform* parent, std::string_view text) {
            auto t = BSML::Lite::CreateText(parent, fmt::format("<b><color=#7fdcff>{}</color></b>", text), TMPro::FontStyles::Normal, 5.5f);
            t->set_enableWordWrapping(false);
            t->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
            FixHeight(t->get_gameObject(), 8.0f);  // without a set height the next row is drawn on top of the header
        }

        void Note(Transform* parent, std::string const& text, float height, float size = 3.4f) {
            auto t = BSML::Lite::CreateText(parent, text, TMPro::FontStyles::Normal, size);
            FixHeight(t->get_gameObject(), height);
        }

        UnityEngine::UI::Button* BigButton(Transform* parent, std::string const& text, std::function<void()> onClick, float height = 11.0f) {
            auto b = BSML::Lite::CreateUIButton(parent, text, std::move(onClick));
            FixHeight(b->get_gameObject(), height);
            return b;
        }

        void ClearChildren(Transform* host) {
            for (int i = host->get_childCount() - 1; i >= 0; i--) {
                auto child = host->GetChild(i)->get_gameObject();
                child->SetActive(false);  // hide immediately; Destroy only happens at end of frame
                Object::Destroy(child);
            }
        }

        void BuildNav(Transform* host);
        void BuildPageContent(Transform* host);

        void RebuildNav() {
            if (g_navHost) {
                ClearChildren(g_navHost.ptr());
                BuildNav(g_navHost.ptr());
            }
        }
        void RebuildPage() {
            if (g_pageHost) {
                ClearChildren(g_pageHost.ptr());
                BuildPageContent(g_pageHost.ptr());
            }
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

        // ------------------------------------------------------------------------------------------------
        // Settings pages (each one is a small group of controls)
        // ------------------------------------------------------------------------------------------------
        void PreviewControls(Transform* parent, Transform* anchor) {
            auto& c = getConfig();
            BigButton(parent, c.PreviewEnabled.GetValue() ? "Live preview: ON" : "Live preview: OFF", [anchor]() {
                bool on = !getConfig().PreviewEnabled.GetValue();
                getConfig().PreviewEnabled.SetValue(on);
                if (on)
                    ShowPreview(anchor);
                else
                    HidePreview();
                RebuildPage();
            });
            if (c.PreviewEnabled.GetValue() && !PreviewAvailable())
                Note(parent, "The preview needs the game's trail material.\nPlay any song once, then reopen this screen.", 12.0f);
            AddConfigValueSliderIncrement(parent, c.PreviewScale, 0.1f, 0.2f, 2.0f);
            AddConfigValueSliderIncrement(parent, c.PreviewX, 0.1f, -4.0f, 4.0f);
            AddConfigValueSliderIncrement(parent, c.PreviewY, 0.1f, -3.0f, 3.0f);
            AddConfigValueSliderIncrement(parent, c.PreviewZ, 0.1f, -3.0f, 3.0f);
        }

        void BuildPage(Transform* parent, int id) {
            auto& c = getConfig();
            switch (id) {
                case PresetsPage:
                    for (int i = 0; i < (int) PresetNames.size(); i++) {
                        BigButton(parent, PresetNames[i], [i]() {
                            ApplyPreset(i);
                            RebuildPage();
                        }, 10.0f);
                    }
                    break;
                case MainPage:
                    AddConfigValueSliderIncrement(parent, c.CrystalAmount, 0.05f, 0.0f, 1.0f);
                    AddConfigValueIncrementInt(parent, c.CrystalFacets, 1, 3, 24);
                    LAYER_UI(Main)
                    LAYER_SOFT(Main)
                    break;
                case CurvePage:
                    LAYER_UI(Curve)
                    AddConfigValueSliderIncrement(parent, c.CurveBend, 0.25f, 0.0f, 12.0f);
                    AddConfigValueSliderIncrement(parent, c.CurveTaper, 0.1f, 0.0f, 6.0f);
                    break;
                case RibbonPage: LAYER_UI(Ribbon) LAYER_SOFT(Ribbon) break;
                case AuraPage: LAYER_UI(Glow) LAYER_SOFT(Glow) break;
                case BlurPage:
                    AddConfigValueToggle(parent, c.BlurSnap);
                    LAYER_UI(Blur)
                    LAYER_SOFT(Blur)
                    AddConfigValueSliderIncrement(parent, c.BlurSpread, 0.05f, 0.0f, 0.8f);
                    AddConfigValueSliderIncrement(parent, c.BlurMinSpeed, 0.5f, 0.0f, 20.0f);
                    AddConfigValueSliderIncrement(parent, c.BlurFullSpeed, 0.5f, 0.5f, 30.0f);
                    break;
                case ModelPage:
                    AddConfigValueIncrementEnum(parent, c.SaberModel, SaberModelNames);
                    Note(parent, "Imported sabers are chosen on the Custom sabers screen.", 8.0f);
                    break;
                case SizePage:
                    AddConfigValueSliderIncrement(parent, c.LengthScale, 0.05f, 0.25f, 2.0f);
                    AddConfigValueSliderIncrement(parent, c.ThicknessScale, 0.05f, 0.25f, 3.0f);
                    break;
                case PositionPage:
                    AddConfigValueIncrementVector3(parent, c.PositionOffset, 1, 0.5);
                    AddConfigValueIncrementVector3(parent, c.RotationOffset, 0, 5.0);
                    break;
                case GeneralPage:
                    AddConfigValueToggle(parent, c.Enabled);
                    AddConfigValueToggle(parent, c.HideVanillaTrail);
                    AddConfigValueToggle(parent, c.FlipTrailV);
                    break;
                case QualityPage:
                    AddConfigValueToggle(parent, c.PerformanceMode);
                    Note(parent, "Performance mode lowers trail detail for a steadier frame rate.", 10.0f);
                    break;
                case PreviewPage: PreviewControls(parent, g_pageHost ? g_pageHost.ptr() : parent); break;
                case ColorsPage:
                    AddConfigValueIncrementEnum(parent, c.SaberColorMode, SaberColorModeNames);
                    AddConfigValueColorPicker(parent, c.LeftColor);
                    AddConfigValueColorPicker(parent, c.RightColor);
                    AddConfigValueSliderIncrement(parent, c.GlowIntensity, 0.05f, 0.0f, 3.0f);
                    break;
                case GradientPage:
                    AddConfigValueToggle(parent, c.BladeGradient);
                    AddConfigValueColorPicker(parent, c.GradLeftStart);
                    AddConfigValueColorPicker(parent, c.GradLeftEnd);
                    AddConfigValueColorPicker(parent, c.GradRightStart);
                    AddConfigValueColorPicker(parent, c.GradRightEnd);
                    break;
                case RainbowPage:
                    AddConfigValueSliderIncrement(parent, c.RainbowSpeed, 0.05f, 0.0f, 3.0f);
                    AddConfigValueSliderIncrement(parent, c.RainbowSaturation, 0.05f, 0.0f, 1.0f);
                    AddConfigValueSliderIncrement(parent, c.RainbowOffset, 0.05f, 0.0f, 1.0f);
                    break;
                case CutPage:
                    AddConfigValueSliderIncrement(parent, c.CutSparkles, 0.1f, 0.0f, 4.0f);
                    AddConfigValueSliderIncrement(parent, c.CutExplosion, 0.1f, 0.0f, 4.0f);
                    AddConfigValueSliderIncrement(parent, c.CutLifetime, 0.1f, 0.1f, 3.0f);
                    AddConfigValueIncrementEnum(parent, c.CutColorMode, CutColorModeNames);
                    AddConfigValueToggle(parent, c.ClashEffect);
                    break;
                case BurnPage:
                    AddConfigValueToggle(parent, c.BurnMarks);
                    AddConfigValueSliderIncrement(parent, c.BurnLife, 0.1f, 0.1f, 3.0f);
                    AddConfigValueSliderIncrement(parent, c.BurnWidth, 0.5f, 1.0f, 20.0f);
                    AddConfigValueSliderIncrement(parent, c.BurnOpacity, 0.05f, 0.0f, 1.0f);
                    AddConfigValueSliderIncrement(parent, c.BurnFloorOffset, 0.05f, -1.0f, 2.0f);
                    AddConfigValueToggle(parent, c.BurnSparkles);
                    break;
                case ParticlesPage:
                    AddConfigValueIncrementEnum(parent, c.ParticleStyle, ParticleStyleNames);
                    AddConfigValueSliderIncrement(parent, c.ParticleAmount, 0.1f, 0.1f, 3.0f);
                    AddConfigValueSliderIncrement(parent, c.ParticleSize, 0.1f, 0.3f, 3.0f);
                    AddConfigValueSliderIncrement(parent, c.ParticleLife, 0.1f, 0.3f, 3.0f);
                    AddConfigValueToggle(parent, c.FlameBlade);
                    break;
                default: break;
            }
        }

        // ------------------------------------------------------------------------------------------------
        // Custom sabers screen: list on the left, details and actions on the right
        // ------------------------------------------------------------------------------------------------
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

        bool IsEquipped(int index) {
            auto& c = getConfig();
            return c.SaberModel.GetValue() == 2 && !c.ImportedSaber.GetValue().empty() && BundlePathAt(index) == c.ImportedSaber.GetValue();
        }

        void BuildSaberNav(Transform* parent) {
            auto bundles = ListSaberBundles();
            Header(parent, "Your sabers");
            if (bundles.empty())
                Note(parent, "No sabers found yet.\nTap Scan folder on the right.", 14.0f);
            g_pick = std::clamp(g_pick, 0, std::max((int) bundles.size() - 1, 0));
            for (int i = 0; i < (int) bundles.size(); i++) {
                std::string label = (i == g_pick ? "> " : "   ") + bundles[(size_t) i] + (IsEquipped(i) ? "  [in game]" : "");
                BigButton(parent, label, [i]() {
                    g_pick = i;
                    RebuildNav();
                    RebuildPage();
                }, 10.0f);
            }
        }

        void BuildSaberDetails(Transform* parent) {
            auto bundles = ListSaberBundles();
            if (bundles.empty()) {
                Note(parent, "Put .saber or .whacker files in\nModData/com.beatgames.beatsaber/Mods/PrysmSabers/Sabers\nthen tap Scan folder.", 24.0f, 3.8f);
            } else {
                g_pick = std::clamp(g_pick, 0, (int) bundles.size() - 1);
                Header(parent, bundles[(size_t) g_pick]);
                Note(parent, IsEquipped(g_pick) ? "In game: yes" : "In game: no", 7.0f, 4.0f);
                BigButton(parent, "Preview", []() {
                    g_status = ShortReport(SpawnTestSaber(g_pick));
                    RebuildPage();
                });
                BigButton(parent, "Remove preview", []() {
                    DespawnTestSaber();
                    g_status = "Preview removed.";
                    RebuildPage();
                });
                BigButton(parent, "Use in game", []() {
                    auto file = BundlePathAt(g_pick);
                    if (file.empty()) {
                        g_status = "Pick a saber first.";
                    } else {
                        DespawnTestSaber();
                        getConfig().ImportedSaber.SetValue(file);
                        getConfig().SaberModel.SetValue(2);
                        g_status = "Equipped. Start a song to see it in your hands.";
                    }
                    RebuildNav();
                    RebuildPage();
                });
                BigButton(parent, "Hilt shader: " + HiltShaderOption(), []() {
                    NextHiltShader();
                    if (TestSaberVisible())
                        g_status = ShortReport(SpawnTestSaber(g_pick));
                    RebuildPage();
                }, 9.0f);
                BigButton(parent, "Fire shader: " + FireShaderOption(), []() {
                    NextFireShader();
                    if (TestSaberVisible())
                        g_status = ShortReport(SpawnTestSaber(g_pick));
                    RebuildPage();
                }, 9.0f);
            }
            BigButton(parent, "Scan folder", []() {
                RunSaberProbe();
                g_status = "Scanned the Sabers folder.";
                RebuildNav();
                RebuildPage();
            }, 9.0f);
            Note(parent, g_status, 22.0f, 3.6f);
        }

        // ------------------------------------------------------------------------------------------------
        // Nav column and content area
        // ------------------------------------------------------------------------------------------------
        void BuildNav(Transform* host) {
            auto scroll = BSML::Lite::CreateScrollableSettingsContainer(host);
            auto parent = scroll->get_transform();
            if (IsSaberCategory()) {
                BuildSaberNav(parent);
                return;
            }
            auto const& cat = Categories()[(size_t) g_cat];
            Header(parent, cat.name);
            for (int i = 0; i < (int) cat.pages.size(); i++) {
                BigButton(parent, (i == g_page ? "> " : "   ") + std::string(cat.pages[(size_t) i].name), [i]() {
                    g_page = i;
                    RebuildNav();
                    RebuildPage();
                });
            }
        }

        void BuildPageContent(Transform* host) {
            auto scroll = BSML::Lite::CreateScrollableSettingsContainer(host);
            auto parent = scroll->get_transform();
            if (IsSaberCategory()) {
                BuildSaberDetails(parent);
                return;
            }
            auto const& cat = Categories()[(size_t) g_cat];
            g_page = std::clamp(g_page, 0, (int) cat.pages.size() - 1);
            Header(parent, cat.pages[(size_t) g_page].name);
            BuildPage(parent, cat.pages[(size_t) g_page].id);
        }

        // ------------------------------------------------------------------------------------------------
        // Hub (in the Gameplay Setup tab) and opening a category screen
        // ------------------------------------------------------------------------------------------------
        // The flow-coordinator calls differ a little between game versions, so each is probed at compile time.
        void PresentFlow(HMUI::FlowCoordinator* parent, HMUI::FlowCoordinator* flow) {
            auto present = [](auto* p, auto* f) {
                using Dir = HMUI::ViewController::AnimationDirection;
                if constexpr (requires { p->PresentFlowCoordinator(f, nullptr, Dir::Horizontal, false, false); }) {
                    p->PresentFlowCoordinator(f, nullptr, Dir::Horizontal, false, false);
                } else if constexpr (requires { p->PresentFlowCoordinator(f, nullptr, Dir::Horizontal, false); }) {
                    p->PresentFlowCoordinator(f, nullptr, Dir::Horizontal, false);
                } else {
                    logger.error("PresentFlowCoordinator has an unexpected signature in this game version");
                }
            };
            present(parent, flow);
        }

        void OpenCategory(int index) {
            g_cat = index;
            g_page = 0;
            auto flow = BSML::Helpers::CreateFlowCoordinator<PrysmFlow*>();
            auto parent = BSML::Helpers::GetMainFlowCoordinator()->YoungestChildFlowCoordinatorOrSelf();
            PresentFlow(parent, flow);
        }

        void BuildHub(Transform* host) {
            auto& c = getConfig();
            auto scroll = BSML::Lite::CreateScrollableSettingsContainer(host);
            auto parent = scroll->get_transform();
            Header(parent, "Prysm Sabers");
            AddConfigValueToggle(parent, c.Enabled);
            auto const& cats = Categories();
            for (int i = 0; i < (int) cats.size(); i++)
                BigButton(parent, cats[(size_t) i].name, [i]() { OpenCategory(i); }, 12.0f);
        }
    }
}

// ----------------------------------------------------------------------------------------------------
// View controllers and flow coordinator
// ----------------------------------------------------------------------------------------------------
void PrysmSabers::PrysmNavVC::DidActivate(bool firstActivation, bool, bool) {
    if (firstActivation) {
        g_navHost = get_transform();
        BuildNav(get_transform());
    }
}

void PrysmSabers::PrysmPageVC::DidActivate(bool firstActivation, bool, bool) {
    if (firstActivation) {
        g_pageHost = get_transform();
        BuildPageContent(get_transform());
    }
    if (!IsSaberCategory() && getConfig().PreviewEnabled.GetValue())
        ShowPreview(get_transform());
}

void PrysmSabers::PrysmPageVC::DidDeactivate(bool, bool) {
    HidePreview();
    DespawnTestSaber();
}

void PrysmSabers::PrysmFlow::DidActivate(bool firstActivation, bool, bool) {
    if (firstActivation) {
        SetTitle(Categories()[(size_t) g_cat].name, HMUI::ViewController::AnimationType::In);
        set_showBackButton(true);
        auto page = BSML::Helpers::CreateViewController<PrysmPageVC*>();
        auto nav = BSML::Helpers::CreateViewController<PrysmNavVC*>();
        ProvideInitialViewControllers(page, nav, nullptr, nullptr, nullptr);
    }
}

void PrysmSabers::PrysmFlow::BackButtonWasPressed(HMUI::ViewController*) {
    HidePreview();
    DespawnTestSaber();
    auto dismiss = [](auto* self) {
        using Dir = HMUI::ViewController::AnimationDirection;
        if constexpr (requires { self->_parentFlowCoordinator->DismissFlowCoordinator(self, Dir::Horizontal, nullptr, false); }) {
            self->_parentFlowCoordinator->DismissFlowCoordinator(self, Dir::Horizontal, nullptr, false);
        } else {
            logger.error("DismissFlowCoordinator has an unexpected signature in this game version");
        }
    };
    dismiss(this);
}

namespace PrysmSabers {
    void SettingsDidActivate(HMUI::ViewController* self, bool firstActivation, bool, bool) {
        if (firstActivation)
            BuildHub(self->get_transform());
    }

    void GameplayTabDidActivate(GameObject* self, bool firstActivation) {
        if (firstActivation)
            BuildHub(self->get_transform());
    }
}
