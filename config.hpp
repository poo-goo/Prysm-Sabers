#pragma once

#include "config-utils/shared/config-utils.hpp"

namespace PrismSabers {
    enum ColorSource { SaberColor = 0, CustomColor = 1, Rainbow = 2, SaberToCustom = 3 };
    inline std::vector<std::string> const ColorSourceNames = {"Saber", "Custom", "Rainbow", "Saber > Custom"};
    inline std::vector<std::string> const SaberColorModeNames = {"Game", "Custom", "Rainbow"};
    inline std::vector<std::string> const CutColorModeNames = {"Saber", "White", "Rainbow"};
}

// Labels are kept SHORT on purpose: long labels overflow the BSML row and overlap the control.
#define TRAIL_LAYER(p, label, en, dur, gran, base, tip, op, fade, white, soft, src, ...)                                       \
    CONFIG_VALUE(p##Enabled, bool, label " On", en);                                                                          \
    CONFIG_VALUE(p##Duration, float, label " Length", dur, "How long the layer persists behind the blade (seconds)");          \
    CONFIG_VALUE(p##Granularity, int, label " Detail", gran, "Spline segments. Higher = smoother, slightly more GPU work");    \
    CONFIG_VALUE(p##BaseOffset, float, label " Start", base, "Where the layer starts along the blade (0 = hilt, 1 = tip)");   \
    CONFIG_VALUE(p##TipOffset, float, label " End", tip, "Where the layer ends along the blade (1 = tip, >1 = past it)");      \
    CONFIG_VALUE(p##Opacity, float, label " Opacity", op);                                                                    \
    CONFIG_VALUE(p##Fade, float, label " Fade", fade, "Falloff exponent. 1 = linear, higher = faster fade");                  \
    CONFIG_VALUE(p##WhiteHead, float, label " White", white, "Fraction near the blade that blends to white-hot");              \
    CONFIG_VALUE(p##Softness, float, label " Soft", soft, "Feathers the edges of the layer. 0 = hard edge, 1 = very soft");    \
    CONFIG_VALUE(p##ColorSource, int, label " Color", src);                                                                   \
    CONFIG_VALUE(p##CustomColor, UnityEngine::Color, label " Tint", UnityEngine::Color(__VA_ARGS__));

DECLARE_CONFIG(Config) {
    CONFIG_VALUE(Enabled, bool, "Enable PrismSabers", true);
    CONFIG_VALUE(HideVanillaTrail, bool, "Hide Vanilla Trail", true, "Replace the stock trail with PrismSabers layers");
    CONFIG_VALUE(PerformanceMode, bool, "Performance Mode", false, "Caps detail at 12, disables blur and softness");

    // Saber colors
    CONFIG_VALUE(SaberColorMode, int, "Color Mode", 0);
    CONFIG_VALUE(LeftColor, UnityEngine::Color, "Left Color", UnityEngine::Color(1.0f, 0.1f, 0.35f, 1.0f));
    CONFIG_VALUE(RightColor, UnityEngine::Color, "Right Color", UnityEngine::Color(0.1f, 0.6f, 1.0f, 1.0f));
    CONFIG_VALUE(RainbowSpeed, float, "Rainbow Speed", 0.35f, "Hue cycles per second");
    CONFIG_VALUE(RainbowSaturation, float, "Rainbow Sat", 0.9f);
    CONFIG_VALUE(RainbowOffset, float, "Rainbow Offset", 0.5f, "Hue offset between left and right saber");
    CONFIG_VALUE(GlowIntensity, float, "Glow", 1.0f, "Blade glow intensity");

    // Layers: (prefix, label, enabled, length, detail, start, end, opacity, fade, white, softness, colorSource, tint rgba)
    TRAIL_LAYER(Main, "Trail", true, 0.30f, 40, 0.0f, 1.0f, 0.9f, 1.6f, 0.06f, 0.0f, 0, 1.0f, 1.0f, 1.0f, 1.0f)
    TRAIL_LAYER(Glow, "Aura", true, 0.18f, 24, 0.55f, 1.06f, 0.35f, 2.4f, 0.0f, 0.6f, 0, 1.0f, 0.85f, 0.4f, 1.0f)
    TRAIL_LAYER(Blur, "Blur", true, 0.07f, 16, 0.0f, 1.0f, 0.6f, 1.2f, 0.25f, 0.7f, 0, 1.0f, 1.0f, 1.0f, 1.0f)
    CONFIG_VALUE(BlurMinSpeed, float, "Blur Min Speed", 2.0f, "Tip speed (m/s) where blur starts to appear");
    CONFIG_VALUE(BlurFullSpeed, float, "Blur Full Speed", 9.0f, "Tip speed (m/s) where blur reaches full strength");

    // Saber model
    CONFIG_VALUE(PositionOffset, UnityEngine::Vector3, "Pos Offset (cm)", UnityEngine::Vector3(0, 0, 0));
    CONFIG_VALUE(RotationOffset, UnityEngine::Vector3, "Rot Offset (deg)", UnityEngine::Vector3(0, 0, 0));
    CONFIG_VALUE(LengthScale, float, "Blade Length", 1.0f, "Visual only, hitboxes are unchanged");
    CONFIG_VALUE(ThicknessScale, float, "Blade Width", 1.0f, "Visual only, hitboxes are unchanged");

    // Effects
    CONFIG_VALUE(CutSparkles, float, "Sparkles", 1.0f, "Multiplier for sparkle particles on note cuts");
    CONFIG_VALUE(CutExplosion, float, "Explosions", 1.0f, "Multiplier for explosion particles on note cuts");
    CONFIG_VALUE(CutLifetime, float, "Particle Life", 1.0f);
    CONFIG_VALUE(CutColorMode, int, "Particle Color", 0);
    CONFIG_VALUE(ClashEffect, bool, "Clash Effect", true);
    CONFIG_VALUE(BurnMarks, bool, "Burn Marks", true);
    CONFIG_VALUE(BurnSparkles, bool, "Burn Sparkles", true);
};

#undef TRAIL_LAYER

namespace PrismSabers {
    void ApplyPreset(int preset);
    inline std::vector<std::string> const PresetNames = {"Prism (default)", "Neon", "Silk", "Inferno", "Vanilla+", "Potato (max FPS)"};
}
