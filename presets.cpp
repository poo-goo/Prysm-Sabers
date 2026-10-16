#include "config.hpp"

namespace PrysmSabers {
    namespace {
        struct Layer {
            bool en;
            float dur;
            int gran;
            float base, tip, op, fade, white, soft;
            int src;
            UnityEngine::Color col;
        };

        struct Preset {
            Layer main, glow, blur, curve, ribbon;
            float taper;
            bool blurSnap;
            float blurMin, blurSpread;
            int saberMode, cutMode, model;
            float sparkles, thickness, glowIntensity, rainbowSpeed;
            bool bladeGradient = false;
            float bend = 4.0f;
        };

        // clang-format off
#define SET_LAYER(p, l) \
        c.p##Enabled.SetValue(l.en, false); c.p##Duration.SetValue(l.dur, false); c.p##Granularity.SetValue(l.gran, false); \
        c.p##BaseOffset.SetValue(l.base, false); c.p##TipOffset.SetValue(l.tip, false); c.p##Opacity.SetValue(l.op, false); \
        c.p##Fade.SetValue(l.fade, false); c.p##WhiteHead.SetValue(l.white, false); c.p##Softness.SetValue(l.soft, false); \
        c.p##ColorSource.SetValue(l.src, false); c.p##CustomColor.SetValue(l.col, false);
        // clang-format on

        void Apply(Preset const& p) {
            auto& c = getConfig();
            SET_LAYER(Main, p.main);
            SET_LAYER(Glow, p.glow);
            SET_LAYER(Blur, p.blur);
            SET_LAYER(Curve, p.curve);
            SET_LAYER(Ribbon, p.ribbon);
            // flat opaque layers for the sheet-like ones, the game's glowing material for the soft glow layers
            c.MainSolid.SetValue(true, false);
            c.CurveSolid.SetValue(true, false);
            c.RibbonSolid.SetValue(true, false);
            c.GlowSolid.SetValue(false, false);
            c.BlurSolid.SetValue(false, false);
            c.BladeGradient.SetValue(p.bladeGradient, false);
            c.CurveTaper.SetValue(p.taper, false);
            c.CurveBend.SetValue(p.bend, false);
            c.BlurSnap.SetValue(p.blurSnap, false);
            c.BlurMinSpeed.SetValue(p.blurMin, false);
            c.BlurSpread.SetValue(p.blurSpread, false);
            c.SaberColorMode.SetValue(p.saberMode, false);
            c.CutColorMode.SetValue(p.cutMode, false);
            c.SaberModel.SetValue(p.model, false);
            c.CutSparkles.SetValue(p.sparkles, false);
            c.ThicknessScale.SetValue(p.thickness, false);
            c.GlowIntensity.SetValue(p.glowIntensity, false);
            c.RainbowSpeed.SetValue(p.rainbowSpeed, false);
        }
    }

    void ApplyPreset(int preset) {
        auto& c = getConfig();
        using UnityEngine::Color;
        Color white(1, 1, 1, 1);
        Layer off{false, 0.1f, 16, 0.0f, 1.0f, 0.5f, 1.0f, 0.0f, 0.0f, SaberColor, white};
        c.PerformanceMode.SetValue(false, false);
        c.HideVanillaTrail.SetValue(true, false);
        c.BurnSparkles.SetValue(true, false);
        c.CutExplosion.SetValue(1.0f, false);
        // fire gradient palette (only used by layers set to "Gradient")
        c.GradLeftStart.SetValue(Color(1.0f, 0.08f, 0.0f, 1.0f), false);
        c.GradLeftEnd.SetValue(Color(1.0f, 0.6f, 0.05f, 1.0f), false);
        c.GradRightStart.SetValue(Color(0.28f, 0.12f, 0.95f, 1.0f), false);
        c.GradRightEnd.SetValue(Color(0.5f, 0.88f, 1.0f, 1.0f), false);

        // fade: 0 = none, 1 = very strong.  durations are short on purpose so the layers read as blur, not as trails
        //            en    dur    gran base   tip    op    fade  white soft  source          tint
        switch (preset) {
            default:
            case 0:  // Prysm: sleek. Sharp opaque curve sheet, short tip ribbon, soft aura, snap blur
                Apply({off,
                       {true, 0.09f, 24, 0.4f, 1.06f, 0.30f, 0.6f, 0.0f, 0.7f, SaberColor, white},
                       {true, 0.035f, 14, 0.0f, 1.0f, 0.6f, 0.3f, 0.25f, 0.85f, SaberColor, white},
                       {true, 0.11f, 48, 0.0f, 1.0f, 0.95f, 0.0f, 0.0f, 0.0f, SaberColor, white},
                       {true, 0.07f, 28, 0.965f, 1.0f, 1.0f, 0.2f, 0.5f, 0.0f, SaberColor, white},
                       0.0f, true, 3.0f, 0.35f, 0, 0, 1, 1.0f, 0.8f, 0.9f, 0.35f});
                break;
            case 1:  // Neon: rainbow everywhere, as loud as it gets
                Apply({{true, 0.18f, 48, 0.0f, 1.0f, 0.75f, 0.3f, 0.05f, 0.3f, Rainbow, white},
                       {true, 0.22f, 32, 0.0f, 1.15f, 0.55f, 0.6f, 0.0f, 0.8f, Rainbow, white},
                       {true, 0.04f, 16, 0.0f, 1.0f, 0.7f, 0.25f, 0.3f, 0.8f, Rainbow, white},
                       off,
                       {true, 0.14f, 48, 0.9f, 1.0f, 1.0f, 0.2f, 0.3f, 0.0f, Rainbow, white},
                       0.0f, true, 3.0f, 0.35f, 2, 2, 1, 2.0f, 0.9f, 1.6f, 0.7f});
                break;
            case 2:  // Silk: very smooth, long, flowing, clearly visible
                Apply({off,
                       {true, 0.20f, 40, 0.3f, 1.1f, 0.35f, 0.4f, 0.0f, 0.9f, SaberColor, white},
                       off,
                       {true, 0.35f, 96, 0.0f, 1.0f, 0.8f, 0.2f, 0.1f, 0.0f, SaberToCustom, {1.0f, 1.0f, 1.0f, 1}},
                       {true, 0.30f, 96, 0.96f, 1.0f, 1.0f, 0.2f, 0.5f, 0.0f, SaberColor, white},
                       0.6f, true, 3.0f, 0.35f, 0, 0, 1, 0.6f, 0.8f, 1.0f, 0.35f, false, 3.0f});
                break;
            case 3:  // Inferno: flames. Red saber red->orange, blue saber deep violet-blue->light blue
                Apply({{true, 0.16f, 56, 0.0f, 1.05f, 0.6f, 0.25f, 0.1f, 0.6f, GradientColor, white},
                       {true, 0.20f, 32, 0.25f, 1.2f, 0.5f, 0.4f, 0.0f, 0.9f, GradientColor, white},
                       {true, 0.04f, 16, 0.0f, 1.0f, 0.55f, 0.3f, 0.3f, 0.85f, GradientColor, white},
                       {true, 0.18f, 64, 0.0f, 1.0f, 0.9f, 0.1f, 0.0f, 0.0f, GradientColor, white},
                       {true, 0.10f, 32, 0.95f, 1.0f, 1.0f, 0.2f, 0.6f, 0.0f, GradientColor, white},
                       0.0f, true, 3.0f, 0.35f, 1, 0, 1, 2.0f, 0.9f, 1.4f, 0.35f, true, 4.5f});
                // blade colours match the flames
                c.LeftColor.SetValue(Color(1.0f, 0.12f, 0.0f, 1.0f), false);
                c.RightColor.SetValue(Color(0.35f, 0.25f, 1.0f, 1.0f), false);
                break;
            case 4:  // Vanilla+: game saber, stock look, all about the motion blur
                Apply({{true, 0.12f, 28, 0.0f, 1.0f, 0.6f, 0.35f, 0.1f, 0.2f, SaberColor, white},
                       off,
                       {true, 0.06f, 24, 0.0f, 1.0f, 0.9f, 0.2f, 0.25f, 0.9f, SaberColor, white},
                       off, off,
                       0.0f, true, 2.5f, 0.5f, 0, 0, 0, 1.0f, 0.8f, 1.0f, 0.35f});
                break;
            case 5:  // Laser: a razor-thin bright line at the tip, like a laser pointer
                Apply({off, off, off, off,
                       {true, 0.25f, 64, 0.985f, 1.0f, 1.0f, 0.15f, 0.6f, 0.0f, SaberColor, white},
                       0.0f, true, 3.0f, 0.35f, 0, 0, 1, 0.8f, 0.5f, 0.7f, 0.35f});
                break;
            case 6:  // Whip: tapered, snappy sheet that cracks at the tip
                Apply({off, off,
                       {true, 0.03f, 12, 0.0f, 1.0f, 0.5f, 0.25f, 0.5f, 0.8f, SaberColor, white},
                       {true, 0.20f, 56, 0.0f, 1.0f, 0.95f, 0.15f, 0.0f, 0.0f, SaberColor, white},
                       {true, 0.14f, 32, 0.94f, 1.0f, 1.0f, 0.25f, 0.5f, 0.0f, SaberColor, white},
                       2.2f, true, 5.0f, 0.2f, 0, 0, 1, 1.0f, 0.8f, 0.9f, 0.35f, false, 7.0f});
                break;
            case 7:  // Prysm Low: maximum FPS
                Apply({{true, 0.12f, 10, 0.0f, 1.0f, 0.8f, 0.25f, 0.05f, 0.0f, SaberColor, white},
                       off, off, off, off,
                       0.0f, false, 3.0f, 0.35f, 0, 0, 1, 0.5f, 0.8f, 0.6f, 0.35f});
                c.PerformanceMode.SetValue(true, false);
                c.BurnSparkles.SetValue(false, false);
                c.CutExplosion.SetValue(0.5f, false);
                break;
        }
        getConfig().Save();
    }
}
