#include "ui.hpp"

#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "PrismTrail.hpp"
#include "config.hpp"

using namespace UnityEngine;

DEFINE_TYPE(PrysmSabers, PrysmTabWatcher);

void PrysmSabers::PrysmTabWatcher::OnEnable() {
    if (getConfig().PreviewEnabled.GetValue())
        PrysmSabers::ShowPreview();
}
void PrysmSabers::PrysmTabWatcher::OnDisable() { PrysmSabers::HidePreview(); }
void PrysmSabers::PrysmTabWatcher::OnDestroy() { PrysmSabers::HidePreview(); }

namespace PrysmSabers {
    namespace {
        // The menu is split into pages. Only one page's controls exist at a time, which keeps the menu
        // fast to open, stops long lists from running off the panel, and avoids overlapping rows.
        constexpr int SectionCount = 9;
        constexpr char const* SectionNames[SectionCount] = {"General", "Colors", "Trail", "Curve", "Ribbon", "Aura", "Blur", "Saber", "Effects"};
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
    AddConfigValueIncrementEnum(parent, c.p##ColorSource, ColorSourceNames);     \
    AddConfigValueColorPicker(parent, c.p##CustomColor);                         \
    AddConfigValueSliderIncrement(parent, c.p##Duration, 0.005f, 0.005f, 1.5f);  \
    AddConfigValueSliderIncrement(parent, c.p##Opacity, 0.05f, 0.0f, 1.0f);      \
    AddConfigValueIncrementInt(parent, c.p##Granularity, 2, 2, 96);              \
    AddConfigValueSliderIncrement(parent, c.p##BaseOffset, 0.01f, 0.0f, 1.0f);   \
    AddConfigValueSliderIncrement(parent, c.p##TipOffset, 0.01f, 0.0f, 1.5f);    \
    AddConfigValueSliderIncrement(parent, c.p##Fade, 0.1f, 0.1f, 6.0f);          \
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
                    BSML::Lite::CreateUIButton(parent, c.PreviewEnabled.GetValue() ? "Live Preview: ON" : "Live Preview: OFF", [h]() {
                        bool on = !getConfig().PreviewEnabled.GetValue();
                        getConfig().PreviewEnabled.SetValue(on);
                        if (on)
                            ShowPreview();
                        else
                            HidePreview();
                        Rebuild(h);
                    });
                    if (c.PreviewEnabled.GetValue() && !PreviewAvailable()) {
                        auto note = BSML::Lite::CreateText(parent, "Preview needs the game's trail material.\nPlay any song once, then reopen this tab.", TMPro::FontStyles::Normal, 3.2f);
                        FixHeight(note->get_gameObject(), 12.0f);
                    }
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
                    AddConfigValueSliderIncrement(parent, c.RainbowSpeed, 0.05f, 0.0f, 3.0f);
                    AddConfigValueSliderIncrement(parent, c.RainbowSaturation, 0.05f, 0.0f, 1.0f);
                    AddConfigValueSliderIncrement(parent, c.RainbowOffset, 0.05f, 0.0f, 1.0f);
                    AddConfigValueSliderIncrement(parent, c.GlowIntensity, 0.05f, 0.0f, 3.0f);
                    break;
                case 2: LAYER_UI(Main) LAYER_SOFT(Main) break;
                case 3:
                    LAYER_UI(Curve)
                    AddConfigValueSliderIncrement(parent, c.CurveTaper, 0.1f, 0.2f, 6.0f);
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
                    AddConfigValueIncrementVector3(parent, c.PositionOffset, 1, 0.5);
                    AddConfigValueIncrementVector3(parent, c.RotationOffset, 0, 5.0);
                    AddConfigValueSliderIncrement(parent, c.LengthScale, 0.05f, 0.25f, 2.0f);
                    AddConfigValueSliderIncrement(parent, c.ThicknessScale, 0.05f, 0.25f, 3.0f);
                    break;
                default:
                    AddConfigValueSliderIncrement(parent, c.CutSparkles, 0.1f, 0.0f, 4.0f);
                    AddConfigValueSliderIncrement(parent, c.CutExplosion, 0.1f, 0.0f, 4.0f);
                    AddConfigValueSliderIncrement(parent, c.CutLifetime, 0.1f, 0.1f, 3.0f);
                    AddConfigValueIncrementEnum(parent, c.CutColorMode, CutColorModeNames);
                    AddConfigValueToggle(parent, c.ClashEffect);
                    AddConfigValueToggle(parent, c.BurnMarks);
                    AddConfigValueToggle(parent, c.BurnSparkles);
                    break;
            }
        }
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
