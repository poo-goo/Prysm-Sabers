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
            int saberMode, cutMode;
            float sparkles, thickness, glowIntensity, rainbowSpeed;
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
            c.CurveTaper.SetValue(p.taper, false);
            c.BlurSnap.SetValue(p.blurSnap, false);
            c.SaberColorMode.SetValue(p.saberMode, false);
            c.CutColorMode.SetValue(p.cutMode, false);
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
        Layer off{false, 0.2f, 16, 0.0f, 1.0f, 0.5f, 1.5f, 0.0f, 0.0f, SaberColor, white};
        c.PerformanceMode.SetValue(false, false);
        c.HideVanillaTrail.SetValue(true, false);
        c.BurnSparkles.SetValue(true, false);
        c.CutExplosion.SetValue(1.0f, false);

        //            en    dur    gran base   tip    op    fade  white soft  source          tint
        switch (preset) {
            default:
            case 0:  // Prysm: balanced. Curve sheet + tip ribbon + soft aura + snap blur
                Apply({off,
                       {true, 0.16f, 24, 0.5f, 1.08f, 0.35f, 2.2f, 0.0f, 0.7f, SaberColor, white},
                       {true, 0.06f, 14, 0.0f, 1.0f, 0.55f, 1.4f, 0.2f, 0.8f, SaberColor, white},
                       {true, 0.22f, 40, 0.05f, 1.0f, 0.85f, 1.3f, 0.0f, 0.0f, SaberColor, white},
                       {true, 0.28f, 36, 0.955f, 1.0f, 1.0f, 0.9f, 0.4f, 0.0f, SaberColor, white},
                       1.8f, true, 0, 0, 1.0f, 0.65f, 1.0f, 0.35f});
                break;
            case 1:  // Neon: rainbow everywhere, as loud as it gets
                Apply({{true, 0.30f, 48, 0.0f, 1.0f, 0.75f, 1.1f, 0.05f, 0.3f, Rainbow, white},
                       {true, 0.40f, 32, 0.0f, 1.15f, 0.55f, 2.5f, 0.0f, 0.8f, Rainbow, white},
                       {true, 0.08f, 16, 0.0f, 1.0f, 0.7f, 1.0f, 0.3f, 0.8f, Rainbow, white},
                       off,
                       {true, 0.45f, 48, 0.9f, 1.0f, 1.0f, 0.7f, 0.3f, 0.0f, Rainbow, white},
                       1.8f, true, 2, 2, 2.0f, 0.75f, 1.6f, 0.7f});
                break;
            case 2:  // Silk: long, bright, flowing ribbons (made much more visible than before)
                Apply({off,
                       {true, 0.22f, 20, 0.4f, 1.1f, 0.3f, 2.0f, 0.0f, 0.8f, SaberColor, white},
                       off,
                       {true, 0.60f, 64, 0.1f, 1.0f, 0.9f, 0.7f, 0.1f, 0.0f, SaberToCustom, {1.0f, 1.0f, 1.0f, 1}},
                       {true, 0.65f, 64, 0.955f, 1.0f, 1.0f, 0.8f, 0.5f, 0.0f, SaberColor, white},
                       1.2f, true, 0, 0, 0.6f, 0.55f, 1.0f, 0.35f});
                break;
            case 3:  // Inferno: fire colours, hot tip, lots of sparks
                Apply({{true, 0.28f, 40, 0.0f, 1.0f, 0.9f, 1.3f, 0.15f, 0.2f, SaberToCustom, {1.0f, 0.35f, 0.0f, 1}},
                       {true, 0.24f, 28, 0.3f, 1.2f, 0.6f, 2.0f, 0.1f, 0.8f, CustomColor, {1.0f, 0.75f, 0.1f, 1}},
                       {true, 0.07f, 14, 0.0f, 1.0f, 0.6f, 1.2f, 0.3f, 0.8f, SaberColor, white},
                       {true, 0.20f, 32, 0.1f, 1.0f, 0.6f, 1.8f, 0.0f, 0.0f, CustomColor, {1.0f, 0.3f, 0.0f, 1}},
                       {true, 0.30f, 36, 0.95f, 1.0f, 1.0f, 0.9f, 0.6f, 0.0f, CustomColor, {1.0f, 0.9f, 0.4f, 1}},
                       2.5f, true, 0, 0, 2.0f, 0.65f, 1.3f, 0.35f});
                break;
            case 4:  // Vanilla+: stock look, all about the motion blur
                Apply({{true, 0.16f, 28, 0.0f, 1.0f, 0.6f, 1.5f, 0.1f, 0.2f, SaberColor, white},
                       off,
                       {true, 0.13f, 24, 0.0f, 1.0f, 0.9f, 0.9f, 0.25f, 0.9f, SaberColor, white},
                       off, off,
                       1.8f, true, 0, 0, 1.0f, 0.8f, 1.0f, 0.35f});
                break;
            case 5:  // Laser: just the thin tip ribbon, like a laser pointer
                Apply({off, off, off, off,
                       {true, 0.50f, 56, 0.93f, 1.0f, 1.0f, 0.6f, 0.35f, 0.0f, SaberColor, white},
                       1.8f, true, 0, 0, 0.8f, 0.5f, 1.0f, 0.35f});
                break;
            case 6:  // Whip: the saber seems to bend with speed (curve sheet)
                Apply({off, off, off,
                       {true, 0.35f, 56, 0.0f, 1.0f, 0.95f, 0.5f, 0.0f, 0.0f, SaberColor, white},
                       {true, 0.20f, 32, 0.96f, 1.0f, 1.0f, 1.0f, 0.5f, 0.0f, SaberColor, white},
                       2.2f, true, 0, 0, 1.0f, 0.6f, 1.0f, 0.35f});
                break;
            case 7:  // Potato: maximum FPS
                Apply({{true, 0.15f, 10, 0.0f, 1.0f, 0.8f, 1.0f, 0.05f, 0.0f, SaberColor, white},
                       off, off, off, off,
                       1.8f, false, 0, 0, 0.5f, 0.65f, 1.0f, 0.35f});
                c.PerformanceMode.SetValue(true, false);
                c.BurnSparkles.SetValue(false, false);
                c.CutExplosion.SetValue(0.5f, false);
                break;
        }
        getConfig().Save();
    }
}
