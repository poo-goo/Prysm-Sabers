#include "PrismTrail.hpp"

#include <algorithm>
#include <cmath>

#include "GlobalNamespace/ColorManager.hpp"
#include "GlobalNamespace/Parametric3SliceSpriteController.hpp"
#include "GlobalNamespace/SaberTrail.hpp"
#include "GlobalNamespace/SaberTrailRenderer.hpp"
#include "GlobalNamespace/SetSaberFakeGlowColor.hpp"
#include "GlobalNamespace/SetSaberGlowColor.hpp"
#include "GlobalNamespace/TubeBloomPrePassLight.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/MaterialPropertyBlock.hpp"
#include "UnityEngine/MeshFilter.hpp"
#include "UnityEngine/Rendering/ShadowCastingMode.hpp"
#include "UnityEngine/Time.hpp"
#include "config.hpp"
#include "main.hpp"

DEFINE_TYPE(PrysmSabers, PrismTrailLayer);
DEFINE_TYPE(PrysmSabers, PrismTrail);

using namespace UnityEngine;

namespace PrysmSabers {
    Color HSV(float h, float s, float v) {
        h = h - std::floor(h);
        float r = std::clamp(std::abs(h * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
        float g = std::clamp(2.0f - std::abs(h * 6.0f - 2.0f), 0.0f, 1.0f);
        float b = std::clamp(2.0f - std::abs(h * 6.0f - 4.0f), 0.0f, 1.0f);
        auto mix = [&](float c) { return v * (1.0f + s * (c - 1.0f)); };
        return {mix(r), mix(g), mix(b), 1.0f};
    }

    static inline Vector3 Add(Vector3 a, Vector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    static inline Vector3 Sub(Vector3 a, Vector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    static inline Vector3 Mul(Vector3 a, float f) { return {a.x * f, a.y * f, a.z * f}; }
    static inline Vector3 Cross(Vector3 a, Vector3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
    static inline Vector3 Norm(Vector3 a) {
        float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
        return l > 1e-6f ? Vector3{a.x / l, a.y / l, a.z / l} : Vector3{0, 1, 0};
    }
    static inline float Len(Vector3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
    static inline Color Lerp(Color a, Color b, float t) {
        return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    }
    static inline Vector3 CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
        float t2 = t * t, t3 = t2 * t;
        auto a = Mul(p1, 2.0f);
        auto b = Mul(Sub(p2, p0), t);
        auto c = Mul(Add(Sub(Add(Mul(p0, 2.0f), Mul(p2, 4.0f)), Mul(p1, 5.0f)), Mul(p3, -1.0f)), t2);
        auto d = Mul(Add(Sub(Mul(p1, 3.0f), p0), Sub(p3, Mul(p2, 3.0f))), t3);
        return Mul(Add(Add(a, b), Add(c, d)), 0.5f);
    }

    struct LayerConfig {
        bool enabled;
        float duration;
        int granularity;
        float base, tip, opacity, fade, white, soft;
        int source;
        Color custom;
    };

#define READ_LAYER(p) \
    LayerConfig { c.p##Enabled.GetValue(), c.p##Duration.GetValue(), c.p##Granularity.GetValue(), c.p##BaseOffset.GetValue(), \
                  c.p##TipOffset.GetValue(), c.p##Opacity.GetValue(), c.p##Fade.GetValue(), c.p##WhiteHead.GetValue(), c.p##Softness.GetValue(), \
                  c.p##ColorSource.GetValue(), c.p##CustomColor.GetValue() }

    static LayerConfig ReadLayer(int which) {
        auto& c = getConfig();
        switch (which) {
            case 0: return READ_LAYER(Main);
            case 1: return READ_LAYER(Glow);
            case 2: return READ_LAYER(Blur);
            case 3: return READ_LAYER(Curve);
            default: return READ_LAYER(Ribbon);
        }
    }
}

using namespace PrysmSabers;

void PrismTrailLayer::Init(Material* material, int sortingOrder) {
    auto go = get_gameObject();
    auto filter = go->AddComponent<MeshFilter*>();
    meshRenderer = go->AddComponent<MeshRenderer*>();
    mesh = Mesh::New_ctor();
    mesh->set_name("PrismTrailMesh");
    mesh->MarkDynamic();
    filter->set_sharedMesh(mesh);
    meshRenderer->set_sharedMaterial(material);
    meshRenderer->set_shadowCastingMode(Rendering::ShadowCastingMode::Off);
    meshRenderer->set_receiveShadows(false);
    meshRenderer->set_sortingOrder(sortingOrder);
    granularity = 0;
    crossCount = 0;
}

void PrismTrailLayer::Resize(int gran, int cross) {
    if (gran == granularity && cross == crossCount && vertices)
        return;
    granularity = gran;
    crossCount = cross;
    // cross = vertices across the blade for each time sample (2 = classic hard strip, 8 = feathered edges)
    int vertCount = (gran + 1) * cross;
    vertices = ArrayW<Vector3>(il2cpp_array_size_t(vertCount));
    colors = ArrayW<Color>(il2cpp_array_size_t(vertCount));
    uvs = ArrayW<Vector2>(il2cpp_array_size_t(vertCount));
    triangles = ArrayW<int>(il2cpp_array_size_t(gran * (cross - 1) * 6));
    int t = 0;
    for (int i = 0; i < gran; i++) {
        for (int j = 0; j < cross - 1; j++) {
            int a = i * cross + j, b = a + 1, c2 = a + cross, d = c2 + 1;
            triangles[t++] = a;
            triangles[t++] = b;
            triangles[t++] = c2;
            triangles[t++] = c2;
            triangles[t++] = b;
            triangles[t++] = d;
        }
    }
    for (int i = 0; i <= gran; i++) {
        float u = (float) i / gran;
        for (int j = 0; j < cross; j++)
            uvs[i * cross + j] = {u, (float) j / (cross - 1)};
    }
    mesh->Clear();
    mesh->set_vertices(vertices);
    mesh->set_colors(colors);
    mesh->set_uv(uvs);
    mesh->set_triangles(triangles);
}

void PrismTrailLayer::Upload() {
    mesh->set_vertices(vertices);
    mesh->set_colors(colors);
    mesh->RecalculateBounds();
}

void PrismTrailLayer::SetVisible(bool visible) {
    if (meshRenderer && meshRenderer->get_enabled() != visible)
        meshRenderer->set_enabled(visible);
}

void PrismTrail::Setup(GlobalNamespace::Saber* saber, GlobalNamespace::SaberModelController* model, Material* material, int layer) {
    this->saber = saber;
    this->model = model;
    preview = false;
    isLeft = saber->get_saberType() == GlobalNamespace::SaberType::SaberA;
    gameColor = model->_colorManager ? model->_colorManager->ColorForSaberType(saber->get_saberType()) : Color(1, 1, 1, 1);
    InitCommon(material, layer);
}

void PrismTrail::SetupPreview(Material* material, int layer, bool left, Vector3 origin, Vector3 right) {
    preview = true;
    isLeft = left;
    gameColor = left ? Color(1.0f, 0.1f, 0.2f, 1.0f) : Color(0.1f, 0.6f, 1.0f, 1.0f);
    pvOrigin = origin;
    pvRight = right;
    InitCommon(material, layer);
}

void PrismTrail::InitCommon(Material* material, int layer) {
    head = 0;
    count = 0;
    frames = 0;
    smoothedSpeed = 0;
    lastTime = 0;
    lastColor = Color(0, 0, 0, 0);
    lastIntensity = 1.0f;
    lastMode = 0;
    modified = false;
    blurOn = false;

    static constexpr char const* names[] = {"PrismMain", "PrismAura", "PrismBlur", "PrismCurve", "PrismRibbon", "PrismPreviewBlade"};
    static constexpr int orders[] = {0, -2, 1, -1, 2, 3};
    int total = preview ? 6 : 5;
    for (int i = 0; i < total; i++) {
        auto go = GameObject::New_ctor(names[i]);
        go->set_layer(layer);
        auto l = go->AddComponent<PrismTrailLayer*>();
        l->Init(material, orders[i]);
        switch (i) {
            case 0: mainLayer = l; break;
            case 1: glowLayer = l; break;
            case 2: blurLayer = l; break;
            case 3: curveLayer = l; break;
            case 4: ribbonLayer = l; break;
            default: bladeLayer = l; break;
        }
    }
}

void PrismTrail::OnEnable() {
    count = 0;
    for (auto l : {mainLayer, glowLayer, blurLayer, curveLayer, ribbonLayer, bladeLayer})
        if (l)
            l->get_gameObject()->SetActive(true);
}

void PrismTrail::OnDisable() {
    for (auto l : {mainLayer, glowLayer, blurLayer, curveLayer, ribbonLayer, bladeLayer})
        if (l)
            l->get_gameObject()->SetActive(false);
}

void PrismTrail::OnDestroy() {
    for (auto l : {mainLayer, glowLayer, blurLayer, curveLayer, ribbonLayer, bladeLayer})
        if (l)
            Object::Destroy(l->get_gameObject());
}

void PrismTrail::Push(Sample const& sample) {
    head = (head + 1) % HistorySize;
    history[head] = sample;
    count = std::min(count + 1, HistorySize);
}

PrismTrail::Sample PrismTrail::Evaluate(float time) const {
    auto at = [&](int age) -> Sample const& { return history[(head - std::min(age, count - 1) + HistorySize) % HistorySize]; };
    if (count >= 2 && time > at(0).time) {
        // ahead of the blade: continue the current motion for a few ms (capped), so blur can be centred on the saber
        auto const& a = at(0);
        auto const& b = at(1);
        float dt = a.time - b.time;
        if (dt > 1e-6f) {
            float k = std::min(time - a.time, 0.06f) / dt;
            return {Add(a.bottom, Mul(Sub(a.bottom, b.bottom), k)), Add(a.top, Mul(Sub(a.top, b.top), k)), time};
        }
        return a;
    }
    if (count < 2 || time >= at(0).time)
        return at(0);
    int k = 0;
    while (k < count - 2 && at(k + 1).time > time)
        k++;
    auto const& newer = at(k);
    auto const& older = at(k + 1);
    if (time <= older.time)
        return older;
    float span = newer.time - older.time;
    float t = span > 1e-6f ? (time - older.time) / span : 0.0f;
    auto const& p0 = at(k + 2);
    auto const& p3 = at(std::max(k - 1, 0));
    return {CatmullRom(p0.bottom, older.bottom, newer.bottom, p3.bottom, t), CatmullRom(p0.top, older.top, newer.top, p3.top, t), time};
}

Color PrismTrail::BaseColor(float now) const {
    auto& c = getConfig();
    switch (c.SaberColorMode.GetValue()) {
        case 1: return isLeft ? c.LeftColor.GetValue() : c.RightColor.GetValue();
        case 2: return HSV(now * c.RainbowSpeed.GetValue() + (isLeft ? 0.0f : c.RainbowOffset.GetValue()), c.RainbowSaturation.GetValue(), 1.0f);
        default: return gameColor;
    }
}

void PrismTrail::BuildLayer(PrismTrailLayer* layer, int which, float now, float speedFactor, float blurFactor) {
    static constexpr int SoftCross = 8;
    auto cfg = ReadLayer(which);
    auto& c = getConfig();
    bool perf = c.PerformanceMode.GetValue();
    bool isBlur = which == 2, isCurve = which == 3;
    float alphaScale = isBlur ? blurFactor : 1.0f;
    if (!cfg.enabled || cfg.opacity <= 0.001f || alphaScale <= 0.001f || (perf && (isBlur || isCurve)) || count < 2) {
        layer->SetVisible(false);
        return;
    }
    int gran = std::clamp(cfg.granularity, 2, perf ? 12 : 96);
    float soft = perf ? 0.0f : std::clamp(cfg.soft, 0.0f, 1.0f);
    int cross = (isCurve || soft > 0.01f) ? SoftCross : 2;
    layer->Resize(gran, cross);
    layer->SetVisible(true);

    Color base = BaseColor(now);
    float duration = std::max(cfg.duration, 0.005f);
    // velocity based: the blur stretches out as the blade speeds up
    if (isBlur)
        duration *= 0.6f + 0.8f * speedFactor;

    // Positions across the blade (fraction of blade length) and their opacity weights.
    float span = std::max(cfg.tip - cfg.base, 0.1f);
    float f = soft * span * 0.4f;
    float tpos[SoftCross], wgt[SoftCross];
    if (isCurve) {
        // "Curve": a sheet that is solid at the tip and fades toward the hilt, so the blade looks like it bends with speed
        float taper = std::clamp(c.CurveTaper.GetValue(), 0.2f, 6.0f);
        for (int j = 0; j < SoftCross; j++) {
            float fr = (float) j / (SoftCross - 1);
            tpos[j] = cfg.base + (cfg.tip - cfg.base) * fr;
            wgt[j] = std::pow(fr, taper);
        }
    } else if (cross == 2) {
        tpos[0] = cfg.base;
        tpos[1] = cfg.tip;
        wgt[0] = wgt[1] = 1.0f;
    } else {
        // 3 feather vertices outside each end of the blade, fading smoothly to 0 (no hard edges)
        static constexpr float fo[3] = {1.0f, 0.66f, 0.33f};
        static constexpr float fw[3] = {0.0f, 0.268f, 0.745f};  // smoothstep falloff
        for (int k = 0; k < 3; k++) {
            tpos[k] = cfg.base - f * fo[k];
            wgt[k] = fw[k];
            tpos[7 - k] = cfg.tip + f * fo[k];
            wgt[7 - k] = fw[k];
        }
        tpos[3] = cfg.base;
        tpos[4] = cfg.tip;
        wgt[3] = wgt[4] = 1.0f;
    }

    auto verts = layer->vertices;
    auto cols = layer->colors;
    // Blur is centred on the blade: part of the window is ahead of the saber, the rest behind it,
    // and both sides fade smoothly. That makes it read as the saber itself being smeared, not as a trail.
    float spread = isBlur ? std::clamp(c.BlurSpread.GetValue(), 0.0f, 0.8f) : 0.0f;
    float lead = duration * spread;
    float window = duration + lead;
    for (int i = 0; i <= gran; i++) {
        float u = (float) i / gran;
        float sampleTime = isBlur ? now + lead - window * u : now - duration * u;
        auto s = Evaluate(sampleTime);
        auto blade = Sub(s.top, s.bottom);
        float dist = sampleTime - now;  // >0 ahead of the blade, <0 behind it

        Color col;
        switch (cfg.source) {
            case CustomColor: col = cfg.custom; break;
            case Rainbow: col = HSV(now * c.RainbowSpeed.GetValue() + u * 0.6f + (isLeft ? 0.0f : c.RainbowOffset.GetValue()), c.RainbowSaturation.GetValue(), 1.0f); break;
            case SaberToCustom: col = Lerp(base, cfg.custom, u); break;
            default: col = base; break;
        }
        float a;
        if (isBlur) {
            float w = dist >= 0.0f ? (lead > 1e-4f ? 1.0f - dist / lead : 0.0f) : 1.0f - (-dist) / duration;
            w = std::clamp(w, 0.0f, 1.0f);
            w = w * w * (3.0f - 2.0f * w);  // smoothstep: soft shoulders, no hard start or end
            float hot = std::abs(dist) / duration;  // white-hot core right at the blade
            if (cfg.white > 0.0f && hot < cfg.white)
                col = Lerp(col, Color(1, 1, 1, 1), 1.0f - hot / cfg.white);
            a = std::clamp(cfg.opacity * alphaScale * std::pow(w, std::max(cfg.fade, 0.05f)), 0.0f, 1.0f);
        } else {
            if (cfg.white > 0.0f && u < cfg.white)
                col = Lerp(col, Color(1, 1, 1, 1), 1.0f - u / cfg.white);
            a = std::clamp(cfg.opacity * alphaScale * std::pow(1.0f - u, std::max(cfg.fade, 0.05f)), 0.0f, 1.0f);
        }

        for (int j = 0; j < cross; j++) {
            int idx = i * cross + j;
            verts[idx] = Add(s.bottom, Mul(blade, tpos[j]));
            float aj = a * wgt[j];
            cols[idx] = {col.r * aj, col.g * aj, col.b * aj, aj};
        }
    }
    layer->Upload();
}

void PrismTrail::ApplyBladeColors(float now) {
    auto& c = getConfig();
    int mode = c.SaberColorMode.GetValue();
    float intensity = c.GlowIntensity.GetValue();
    bool vanilla = mode == 0 && std::abs(intensity - 1.0f) < 0.001f;
    if (vanilla && !modified)
        return;
    Color color = BaseColor(now);
    bool changed = mode != lastMode || std::abs(intensity - lastIntensity) > 0.001f || std::abs(color.r - lastColor.r) > 0.002f ||
                   std::abs(color.g - lastColor.g) > 0.002f || std::abs(color.b - lastColor.b) > 0.002f;
    // keep re-applying for the first second so the game's own colouring cannot override ours
    if (mode != 2 && frames > 60 && !changed)
        return;
    lastMode = mode;
    lastIntensity = intensity;
    lastColor = color;
    modified = !vanilla;
    Color glow = {color.r * intensity, color.g * intensity, color.b * intensity, color.a};

    if (auto glows = model->_setSaberGlowColors) {
        for (auto g : glows) {
            if (!g || !g->_meshRenderer)
                continue;
            if (!g->_materialPropertyBlock)
                g->_materialPropertyBlock = MaterialPropertyBlock::New_ctor();
            auto block = g->_materialPropertyBlock;
            if (auto pairs = g->_propertyTintColorPairs) {
                for (auto pair : pairs) {
                    auto tint = pair->tintColor;
                    block->SetColor(pair->property, Color(glow.r * tint.r, glow.g * tint.g, glow.b * tint.b, tint.a));
                }
            }
            g->_meshRenderer->SetPropertyBlock(block);
        }
    }
    if (auto fakes = model->_setSaberFakeGlowColors) {
        for (auto f : fakes) {
            if (!f || !f->_parametric3SliceSprite)
                continue;
            auto tint = f->_tintColor;
            f->_parametric3SliceSprite->color = Color(glow.r * tint.r, glow.g * tint.g, glow.b * tint.b, tint.a);
            f->_parametric3SliceSprite->Refresh();
        }
    }
    if (auto light = model->_saberLight) {
        auto old = light->get_color();
        light->set_color(Color(glow.r, glow.g, glow.b, old.a));
    }
}

void PrismTrail::LateUpdate() {
    if (!preview && (!saber || !model))
        return;
    auto& c = getConfig();
    bool enabled = c.Enabled.GetValue();
    float now = Time::get_unscaledTime();  // the menu preview must keep animating even when the game is paused
    if (!preview)
        now = Time::get_time();
    frames++;

    Vector3 bottom, top;
    if (preview) {
        PreviewPose(now, bottom, top);
    } else {
        auto bottomT = saber->_saberBladeBottomTransform;
        auto topT = saber->_saberBladeTopTransform;
        if (!bottomT || !topT)
            return;
        bottom = bottomT->get_position();
        top = topT->get_position();
        float length = c.LengthScale.GetValue();
        if (std::abs(length - 1.0f) > 0.001f)
            top = Add(bottom, Mul(Sub(top, bottom), length));
    }

    float dt = now - lastTime;
    if (count > 0 && dt > 1e-5f) {
        float speed = Len(Sub(top, lastTop)) / dt;
        smoothedSpeed += (speed - smoothedSpeed) * std::min(1.0f, dt * 20.0f);
    }
    lastTime = now;
    lastTop = top;
    if (count == 0 || now > history[head].time)
        Push({bottom, top, now});

    if (!enabled) {
        for (auto l : {mainLayer, glowLayer, blurLayer, curveLayer, ribbonLayer, bladeLayer})
            if (l)
                l->SetVisible(false);
        return;
    }

    float minS = c.BlurMinSpeed.GetValue(), fullS = std::max(c.BlurFullSpeed.GetValue(), minS + 0.01f);
    float speedFactor = std::clamp((smoothedSpeed - minS) / (fullS - minS), 0.0f, 1.0f);
    float blurFactor = speedFactor;
    if (c.BlurSnap.GetValue()) {
        // ReeSabers-style: blur is either fully on or fully off. Small hysteresis stops flicker at the threshold.
        if (smoothedSpeed >= minS)
            blurOn = true;
        else if (smoothedSpeed < minS * 0.85f)
            blurOn = false;
        blurFactor = blurOn ? 1.0f : 0.0f;
        speedFactor = std::max(speedFactor, 0.35f);  // keep a sensible blur length right at the threshold
    }

    if (mainLayer) BuildLayer(mainLayer.ptr(), 0, now, speedFactor, blurFactor);
    if (glowLayer) BuildLayer(glowLayer.ptr(), 1, now, speedFactor, blurFactor);
    if (blurLayer) BuildLayer(blurLayer.ptr(), 2, now, speedFactor, blurFactor);
    if (curveLayer) BuildLayer(curveLayer.ptr(), 3, now, speedFactor, blurFactor);
    if (ribbonLayer) BuildLayer(ribbonLayer.ptr(), 4, now, speedFactor, blurFactor);
    if (preview)
        DrawPreviewBlade(now, bottom, top);
    else
        ApplyBladeColors(now);
}

// ---------------------------------------------------------------------------------------------
// Menu preview
// ---------------------------------------------------------------------------------------------

void PrismTrail::PreviewPose(float now, Vector3& bottom, Vector3& top) {
    auto& c = getConfig();
    float s = isLeft ? -1.0f : 1.0f;
    float a = 6.2831853f * (now / 1.15f) + (isLeft ? 0.0f : 1.7f);
    float theta = 1.25f * std::sin(a);  // wide, fast sweep so the blur and curve layers have something to show
    Vector3 up{0, 1, 0};
    Vector3 dir = Add(Mul(up, std::cos(theta)), Mul(pvRight, std::sin(theta) * s));
    Vector3 hand = Add(Add(pvOrigin, Mul(pvRight, 0.30f * s)), Mul(up, 0.08f * std::sin(2.0f * a)));
    bottom = Add(hand, Mul(dir, 0.16f));
    top = Add(bottom, Mul(dir, 1.0f * std::max(c.LengthScale.GetValue(), 0.1f)));
}

void PrismTrail::DrawPreviewBlade(float now, Vector3 bottom, Vector3 top) {
    auto layer = bladeLayer ? bladeLayer.ptr() : nullptr;
    if (!layer)
        return;
    auto& c = getConfig();
    layer->Resize(3, 2);
    layer->SetVisible(true);

    auto cam = Camera::get_main();
    Vector3 camPos = cam ? cam->get_transform()->get_position() : Vector3{0, 1.6f, 0};
    auto dir = Norm(Sub(top, bottom));
    auto mid = Mul(Add(top, bottom), 0.5f);
    auto side = Norm(Cross(dir, Sub(camPos, mid)));  // keeps the ribbon facing the viewer

    float thick = std::clamp(c.ThicknessScale.GetValue(), 0.2f, 3.0f);
    float hiltW = 0.016f, bladeW = 0.014f * thick;
    auto hiltBottom = Sub(bottom, Mul(dir, 0.16f));
    auto col = BaseColor(now);
    Color hilt{0.35f, 0.35f, 0.38f, 1.0f};
    Color blade{col.r, col.g, col.b, 1.0f};

    auto verts = layer->vertices;
    auto cols = layer->colors;
    // sample 0-1: hilt, sample 2-3: blade (1 and 2 share a position, so the strip between them has no area)
    Vector3 centers[4] = {hiltBottom, bottom, bottom, top};
    float widths[4] = {hiltW, hiltW, bladeW, bladeW};
    Color colors[4] = {hilt, hilt, blade, blade};
    for (int i = 0; i < 4; i++) {
        verts[i * 2] = Sub(centers[i], Mul(side, widths[i]));
        verts[i * 2 + 1] = Add(centers[i], Mul(side, widths[i]));
        cols[i * 2] = colors[i];
        cols[i * 2 + 1] = colors[i];
    }
    layer->Upload();
}

namespace PrysmSabers {
    static UnityW<Material> g_trailMaterial;
    static UnityW<GameObject> g_previewRoots[2];

    void CacheTrailMaterial(Material* material, int) {
        if (material)
            g_trailMaterial = material;
    }

    static Material* FindTrailMaterial() {
        if (g_trailMaterial)
            return g_trailMaterial.ptr();
        // not captured yet: look for a loaded saber trail prefab (exists once the game has loaded a song)
        auto trails = Resources::FindObjectsOfTypeAll<GlobalNamespace::SaberTrail*>();
        for (auto t : trails) {
            if (!t)
                continue;
            auto prefab = t->_trailRendererPrefab;
            if (!prefab || !prefab->_meshRenderer)
                continue;
            if (auto m = prefab->_meshRenderer->get_sharedMaterial()) {
                g_trailMaterial = m;
                return m;
            }
        }
        return nullptr;
    }

    bool PreviewAvailable() { return FindTrailMaterial() != nullptr; }

    bool PreviewVisible() { return g_previewRoots[0] && g_previewRoots[1]; }

    void HidePreview() {
        for (auto& root : g_previewRoots) {
            if (root)
                Object::Destroy(root.ptr());
            root = UnityW<GameObject>((GameObject*) nullptr);
        }
    }

    void ShowPreview() {
        if (PreviewVisible())
            return;
        auto material = FindTrailMaterial();
        if (!material)
            return;
        // place the preview in front of the player, a little below eye level
        Vector3 origin{0, 1.0f, 1.7f}, right{1, 0, 0};
        if (auto cam = Camera::get_main()) {
            auto t = cam->get_transform();
            auto fwd = t->get_forward();
            fwd.y = 0;
            fwd = Norm(fwd);
            auto pos = t->get_position();
            origin = Add(Add(pos, Mul(fwd, 1.7f)), Vector3{0, -0.55f, 0});
            right = Norm(Cross(Vector3{0, 1, 0}, fwd));
        }
        for (int i = 0; i < 2; i++) {
            auto go = GameObject::New_ctor(i == 0 ? "PrysmPreviewLeft" : "PrysmPreviewRight");
            auto trail = go->AddComponent<PrismTrail*>();
            trail->SetupPreview(material, 0, i == 0, origin, right);
            g_previewRoots[i] = go;
        }
    }
}
