#include "config.hpp"

namespace PrismSabers {
    namespace {
        struct Layer {
            bool en;
            float dur;
            int gran;
            float base, tip, op, fade, white, soft;
            int src;
            UnityEngine::Color col;
        };

        // clang-format off
#define SET_LAYER(p, l) \
        c.p##Enabled.SetValue(l.en, false); c.p##Duration.SetValue(l.dur, false); c.p##Granularity.SetValue(l.gran, false); \
        c.p##BaseOffset.SetValue(l.base, false); c.p##TipOffset.SetValue(l.tip, false); c.p##Opacity.SetValue(l.op, false); \
        c.p##Fade.SetValue(l.fade, false); c.p##WhiteHead.SetValue(l.white, false); c.p##Softness.SetValue(l.soft, false); \
        c.p##ColorSource.SetValue(l.src, false); c.p##CustomColor.SetValue(l.col, false);
        // clang-format on

        void Set(Layer main, Layer glow, Layer blur, int saberMode, int cutMode, float sparkles) {
            auto& c = getConfig();
            SET_LAYER(Main, main);
            SET_LAYER(Glow, glow);
            SET_LAYER(Blur, blur);
            c.SaberColorMode.SetValue(saberMode, false);
            c.CutColorMode.SetValue(cutMode, false);
            c.CutSparkles.SetValue(sparkles, false);
        }
    }

    void ApplyPreset(int preset) {
        auto& c = getConfig();
        UnityEngine::Color white(1, 1, 1, 1);
        c.PerformanceMode.SetValue(false, false);
        c.HideVanillaTrail.SetValue(true, false);
        //   en    dur    gran base  tip   op    fade  white soft  src  tint
        switch (preset) {
            default:
            case 0:  // Prism
                Set({true, 0.30f, 40, 0.0f, 1.0f, 0.9f, 1.6f, 0.06f, 0.0f, SaberColor, white},
                    {true, 0.18f, 24, 0.55f, 1.06f, 0.35f, 2.4f, 0.0f, 0.6f, SaberColor, {1.0f, 0.85f, 0.4f, 1}},
                    {true, 0.07f, 16, 0.0f, 1.0f, 0.6f, 1.2f, 0.25f, 0.7f, SaberColor, white}, 0, 0, 1.0f);
                break;
            case 1:  // Neon
                Set({true, 0.22f, 36, 0.1f, 1.0f, 1.0f, 1.2f, 0.15f, 0.0f, SaberColor, white},
                    {true, 0.35f, 28, 0.0f, 1.12f, 0.45f, 3.0f, 0.0f, 0.6f, Rainbow, white},
                    {true, 0.08f, 16, 0.0f, 1.0f, 0.65f, 1.0f, 0.4f, 0.7f, SaberColor, white}, 0, 2, 1.5f);
                break;
            case 2:  // Silk
                Set({true, 0.55f, 56, 0.35f, 1.0f, 0.7f, 2.2f, 0.0f, 0.25f, SaberToCustom, {1, 1, 1, 1}},
                    {false, 0.2f, 16, 0.6f, 1.0f, 0.2f, 2.0f, 0.0f, 0.6f, SaberColor, white},
                    {true, 0.06f, 12, 0.0f, 1.0f, 0.4f, 1.4f, 0.1f, 0.8f, SaberColor, white}, 0, 0, 0.6f);
                break;
            case 3:  // Inferno
                Set({true, 0.28f, 40, 0.0f, 1.0f, 0.95f, 1.4f, 0.2f, 0.0f, SaberToCustom, {1.0f, 0.35f, 0.0f, 1}},
                    {true, 0.22f, 24, 0.4f, 1.15f, 0.5f, 2.0f, 0.1f, 0.6f, CustomColor, {1.0f, 0.75f, 0.1f, 1}},
                    {true, 0.08f, 16, 0.0f, 1.0f, 0.6f, 1.0f, 0.3f, 0.7f, SaberColor, white}, 0, 1, 2.0f);
                break;
            case 4:  // Vanilla+
                Set({true, 0.2f, 30, 0.0f, 1.0f, 0.85f, 1.0f, 0.1f, 0.0f, SaberColor, white},
                    {false, 0.2f, 16, 0.6f, 1.0f, 0.2f, 2.0f, 0.0f, 0.5f, SaberColor, white},
                    {true, 0.05f, 12, 0.0f, 1.0f, 0.4f, 1.2f, 0.2f, 0.6f, SaberColor, white}, 0, 0, 1.0f);
                break;
            case 5:  // Potato
                Set({true, 0.15f, 10, 0.0f, 1.0f, 0.8f, 1.0f, 0.05f, 0.0f, SaberColor, white},
                    {false, 0.2f, 8, 0.6f, 1.0f, 0.2f, 2.0f, 0.0f, 0.0f, SaberColor, white},
                    {false, 0.04f, 8, 0.0f, 1.0f, 0.4f, 1.2f, 0.2f, 0.0f, SaberColor, white}, 0, 0, 0.5f);
                c.PerformanceMode.SetValue(true, false);
                c.BurnSparkles.SetValue(false, false);
                c.CutExplosion.SetValue(0.5f, false);
                break;
        }
        getConfig().Save();
    }
}
