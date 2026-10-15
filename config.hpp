#pragma once

#include "config-utils/shared/config-utils.hpp"

namespace PrysmSabers {
    enum ColorSource { SaberColor = 0, CustomColor = 1, Rainbow = 2, SaberToCustom = 3, GradientColor = 4 };
    inline std::vector<std::string> const ColorSourceNames = {"Saber", "Custom", "Rainbow", "Saber > Custom", "Gradient"};
    inline std::vector<std::string> const SaberModelNames = {"Vanilla", "Prysm"};
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
    CONFIG_VALUE(Enabled, bool, "Enable Prysm Sabers", true);
    CONFIG_VALUE(HideVanillaTrail, bool, "Hide Vanilla Trail", true, "Replace the stock trail with Prysm Sabers layers");
    CONFIG_VALUE(PreviewEnabled, bool, "Live Preview", true, "Show two swinging preview sabers in front of you while the Prysm menu is open");
    CONFIG_VALUE(FlipTrailV, bool, "Flip Trail Tex", true, "Flips the trail texture across the blade. Turn off if trails look upside down");
    CONFIG_VALUE(PreviewX, float, "Preview Left/Right", -1.5f, "Meters, relative to the menu panel (negative = left)");
    CONFIG_VALUE(PreviewY, float, "Preview Height", -0.7f, "Meters, relative to the menu panel");
    CONFIG_VALUE(PreviewZ, float, "Preview Depth", -0.35f, "Meters toward you (negative) or away from you (positive)");
    CONFIG_VALUE(PerformanceMode, bool, "Performance Mode", false, "Caps detail at 12, disables blur and softness");

    // Saber colors
    CONFIG_VALUE(SaberColorMode, int, "Color Mode", 0);
    CONFIG_VALUE(LeftColor, UnityEngine::Color, "Left Color", UnityEngine::Color(1.0f, 0.1f, 0.35f, 1.0f));
    CONFIG_VALUE(RightColor, UnityEngine::Color, "Right Color", UnityEngine::Color(0.1f, 0.6f, 1.0f, 1.0f));
    // Gradient colours (used when a layer's colour source is "Gradient"): blade side -> tail
    CONFIG_VALUE(GradLeftStart, UnityEngine::Color, "Grad L Start", UnityEngine::Color(1.0f, 0.1f, 0.0f, 1.0f));
    CONFIG_VALUE(GradLeftEnd, UnityEngine::Color, "Grad L End", UnityEngine::Color(1.0f, 0.6f, 0.05f, 1.0f));
    CONFIG_VALUE(GradRightStart, UnityEngine::Color, "Grad R Start", UnityEngine::Color(0.3f, 0.15f, 0.9f, 1.0f));
    CONFIG_VALUE(GradRightEnd, UnityEngine::Color, "Grad R End", UnityEngine::Color(0.45f, 0.85f, 1.0f, 1.0f));
    CONFIG_VALUE(RainbowSpeed, float, "Rainbow Speed", 0.35f, "Hue cycles per second");
    CONFIG_VALUE(RainbowSaturation, float, "Rainbow Sat", 0.9f);
    CONFIG_VALUE(RainbowOffset, float, "Rainbow Offset", 0.5f, "Hue offset between left and right saber");
    CONFIG_VALUE(GlowIntensity, float, "Glow", 1.0f, "Blade glow intensity");

    // Layers: (prefix, label, enabled, length, detail, start, end, opacity, fade, white, softness, colorSource, tint rgba)
    // Defaults = the "Prism" preset: curve sheet + tip ribbon + soft aura + snap blur.
    TRAIL_LAYER(Main, "Trail", false, 0.30f, 40, 0.0f, 1.0f, 0.8f, 1.5f, 0.05f, 0.0f, 0, 1.0f, 1.0f, 1.0f, 1.0f)
    TRAIL_LAYER(Glow, "Aura", true, 0.14f, 24, 0.4f, 1.06f, 0.30f, 2.4f, 0.0f, 0.7f, 0, 1.0f, 1.0f, 1.0f, 1.0f)
    TRAIL_LAYER(Blur, "Blur", true, 0.06f, 14, 0.0f, 1.0f, 0.55f, 1.4f, 0.2f, 0.8f, 0, 1.0f, 1.0f, 1.0f, 1.0f)
    TRAIL_LAYER(Curve, "Curve", true, 0.16f, 48, 0.0f, 1.0f, 0.95f, 0.15f, 0.0f, 0.0f, 0, 1.0f, 1.0f, 1.0f, 1.0f)
    TRAIL_LAYER(Ribbon, "Ribbon", true, 0.10f, 28, 0.965f, 1.0f, 1.0f, 0.8f, 0.5f, 0.0f, 0, 1.0f, 1.0f, 1.0f, 1.0f)
    CONFIG_VALUE(CurveTaper, float, "Curve Taper", 0.0f, "How strongly the curve sheet narrows toward the hilt (higher = more whip-like)");
    CONFIG_VALUE(BlurSnap, bool, "Blur Snap", true, "Blur switches on/off instantly with speed (like ReeSabers) instead of fading");
    CONFIG_VALUE(BlurSpread, float, "Blur Spread", 0.35f, "How much of the blur extends ahead of the saber (0 = trailing only, higher = blurs the saber itself)");
    CONFIG_VALUE(BlurMinSpeed, float, "Blur Min Speed", 3.0f, "Tip speed (m/s) where blur switches on");
    CONFIG_VALUE(BlurFullSpeed, float, "Blur Full Speed", 9.0f, "Tip speed (m/s) where blur reaches full length");

    // Saber model
    CONFIG_VALUE(SaberModel, int, "Saber Model", 1, "0 = game saber, 1 = Prysm saber (black handle, simple thin blade)");
    CONFIG_VALUE(PositionOffset, UnityEngine::Vector3, "Pos Offset (cm)", UnityEngine::Vector3(0, 0, 0));
    CONFIG_VALUE(RotationOffset, UnityEngine::Vector3, "Rot Offset (deg)", UnityEngine::Vector3(0, 0, 0));
    CONFIG_VALUE(LengthScale, float, "Blade Length", 1.0f, "Visual only, hitboxes are unchanged");
    CONFIG_VALUE(ThicknessScale, float, "Blade Width", 0.65f, "Visual only, hitboxes are unchanged");

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

namespace PrysmSabers {
    void ApplyPreset(int preset);
    inline std::vector<std::string> const PresetNames = {"Prysm (default)", "Neon", "Silk", "Inferno", "Vanilla+", "Laser", "Whip", "Prysm Low (max FPS)"};
}
