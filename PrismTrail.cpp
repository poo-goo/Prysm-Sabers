#include <utility>
#include <vector>

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
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Mesh.hpp"
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
        bool solid;
        int source;
        Color custom;
    };

#define READ_LAYER(p) \
    LayerConfig { c.p##Enabled.GetValue(), c.p##Duration.GetValue(), c.p##Granularity.GetValue(), c.p##BaseOffset.GetValue(), \
                  c.p##TipOffset.GetValue(), c.p##Opacity.GetValue(), c.p##Fade.GetValue(), c.p##WhiteHead.GetValue(), c.p##Softness.GetValue(), \
                  c.p##Solid.GetValue(), c.p##ColorSource.GetValue(), c.p##CustomColor.GetValue() }

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
    gameMat = material;
    useSolid = false;
    meshRenderer->set_shadowCastingMode(Rendering::ShadowCastingMode::Off);
    meshRenderer->set_receiveShadows(false);
    meshRenderer->set_sortingOrder(sortingOrder);
    granularity = 0;
    crossCount = 0;
    flipV = false;
    flatU = false;
}

void PrismTrailLayer::Resize(int gran, int cross, bool flip, bool flat) {
    if (gran == granularity && cross == crossCount && flip == flipV && flat == flatU && vertices)
        return;
    granularity = gran;
    crossCount = cross;
    flipV = flip;
    flatU = flat;
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
        float u = flat ? 0.0f : (float) i / gran;
        for (int j = 0; j < cross; j++) {
            float t = (float) j / (cross - 1);
            uvs[i * cross + j] = {u, flip ? 1.0f - t : t};
        }
    }
    mesh->Clear();
    mesh->set_vertices(vertices);
    mesh->set_colors(colors);
    mesh->set_uv(uvs);
    mesh->set_triangles(triangles);
}

void PrismTrailLayer::SetSolid(bool solid, Material* solidMaterial) {
    if (solid && !solidMaterial)
        solid = false;
    if (solid == useSolid || !meshRenderer)
        return;
    useSolid = solid;
    meshRenderer->set_sharedMaterial(solid ? solidMaterial : gameMat.ptr());
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

void PrismTrail::SetupPreview(Material* material, Material* solid, int layer, bool left, Vector3 origin, Vector3 right, Vector3 forward) {
    preview = true;
    isLeft = left;
    gameColor = left ? Color(1.0f, 0.1f, 0.2f, 1.0f) : Color(0.1f, 0.6f, 1.0f, 1.0f);
    pvOrigin = origin;
    pvRight = right;
    pvForward = forward;
    InitCommon(material, layer, solid);
}

void PrismTrail::InitCommon(Material* material, int layer, Material* solid) {
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

    burnHead = 0;
    burnCount = 0;
    static constexpr char const* names[] = {"PrismMain", "PrismAura", "PrismBlur", "PrismCurve", "PrismRibbon", "PrismPreviewBlade", "PrismBurn"};
    static constexpr int orders[] = {0, -2, 1, -1, 2, 3, -3};
    for (int i = 0; i < 7; i++) {
        if ((i == 5 && !preview) || (i == 6 && preview))
            continue;
        auto go = GameObject::New_ctor(names[i]);
        go->set_layer(layer);
        auto l = go->AddComponent<PrismTrailLayer*>();
        l->Init(i == 5 && solid ? solid : material, orders[i]);
        switch (i) {
            case 0: mainLayer = l; break;
            case 1: glowLayer = l; break;
            case 2: blurLayer = l; break;
            case 3: curveLayer = l; break;
            case 4: ribbonLayer = l; break;
            case 5: bladeLayer = l; break;
            default: burnLayer = l; break;
        }
    }
}

void PrismTrail::OnEnable() {
    count = 0;
    for (auto l : {mainLayer, glowLayer, blurLayer, curveLayer, ribbonLayer, bladeLayer, burnLayer})
        if (l)
            l->get_gameObject()->SetActive(true);
}

void PrismTrail::OnDisable() {
    for (auto l : {mainLayer, glowLayer, blurLayer, curveLayer, ribbonLayer, bladeLayer, burnLayer})
        if (l)
            l->get_gameObject()->SetActive(false);
}

void PrismTrail::OnDestroy() {
    for (auto l : {mainLayer, glowLayer, blurLayer, curveLayer, ribbonLayer, bladeLayer, burnLayer})
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
    layer->Resize(gran, cross, c.FlipTrailV.GetValue(), !c.TrailTextureFade.GetValue());
    layer->SetVisible(true);
    layer->SetSolid(cfg.solid, SolidMaterial());
    bool solidMode = layer->useSolid;

    Color base = BaseColor(now);
    float duration = std::max(cfg.duration, 0.005f);
    // velocity based: the blur stretches out as the blade speeds up
    if (isBlur)
        duration *= 0.6f + 0.8f * speedFactor;

    // Positions across the blade (fraction of blade length) and their opacity weights.
    float span = std::max(cfg.tip - cfg.base, 0.1f);
    float f = soft * span * 0.3f;
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
        // 3 feather vertices at each end, all inside [start, end]: the edges fade out smoothly and
        // nothing extends past the hilt side or past the tip of the blade
        static constexpr float fo[3] = {1.0f, 0.66f, 0.33f};
        static constexpr float fw[3] = {0.0f, 0.268f, 0.745f};  // smoothstep falloff
        float lo = cfg.base + f, hi = cfg.tip - f;
        for (int k = 0; k < 3; k++) {
            tpos[k] = lo - f * fo[k];
            wgt[k] = fw[k];
            tpos[7 - k] = hi + f * fo[k];
            wgt[7 - k] = fw[k];
        }
        tpos[3] = lo;
        tpos[4] = hi;
        wgt[3] = wgt[4] = 1.0f;
    }

    auto verts = layer->vertices;
    auto cols = layer->colors;
    // Blur is centred on the blade: part of the window is ahead of the saber, the rest behind it,
    // and both sides fade smoothly. That makes it read as the saber itself being smeared, not as a trail.
    float spread = isBlur ? std::clamp(c.BlurSpread.GetValue(), 0.0f, 0.8f) : 0.0f;
    float lead = duration * spread;
    float fadeExp = std::clamp(cfg.fade, 0.0f, 1.0f) * 4.0f;  // 0 = no fade at all, 1 = very strong fade
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
            case GradientColor:
                col = isLeft ? Lerp(c.GradLeftStart.GetValue(), c.GradLeftEnd.GetValue(), u) : Lerp(c.GradRightStart.GetValue(), c.GradRightEnd.GetValue(), u);
                break;
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
            a = std::clamp(cfg.opacity * alphaScale * std::pow(w, fadeExp), 0.0f, 1.0f);
        } else {
            if (cfg.white > 0.0f && u < cfg.white)
                col = Lerp(col, Color(1, 1, 1, 1), 1.0f - u / cfg.white);
            a = std::clamp(cfg.opacity * alphaScale * std::pow(1.0f - u, fadeExp), 0.0f, 1.0f);
        }

        for (int j = 0; j < cross; j++) {
            int idx = i * cross + j;
            verts[idx] = Add(s.bottom, Mul(blade, tpos[j]));
            float aj = a * wgt[j];
            cols[idx] = solidMode ? Color{col.r, col.g, col.b, aj} : Color{col.r * aj, col.g * aj, col.b * aj, aj};
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

    if (!preview && enabled)
        UpdateBurn(now, bottom, top);

    if (!enabled) {
        for (auto l : {mainLayer, glowLayer, blurLayer, curveLayer, ribbonLayer, bladeLayer, burnLayer})
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
    else {
        ApplyBladeColors(now);
        if (bladeMat)
            bladeMat->set_color(BaseColor(now));
    }
}


// ---------------------------------------------------------------------------------------------
// Floor burn marks (the Quest game does not draw these, so Prysm Sabers does)
// ---------------------------------------------------------------------------------------------

void PrismTrail::PushBurn(Vector3 pos, float time, bool valid) {
    burnHead = (burnHead + 1) % BurnCap;
    burnPos[burnHead] = pos;
    burnTime[burnHead] = time;
    burnValid[burnHead] = valid;
    burnCount = std::min(burnCount + 1, (int) BurnCap);
}

void PrismTrail::UpdateBurn(float now, Vector3 bottom, Vector3 top) {
    auto layer = burnLayer ? burnLayer.ptr() : nullptr;
    if (!layer)
        return;
    auto& c = getConfig();
    if (!c.BurnMarks.GetValue() || c.PerformanceMode.GetValue()) {
        layer->SetVisible(false);
        burnCount = 0;
        return;
    }
    float floorY = c.BurnFloorOffset.GetValue();
    float by = bottom.y - floorY, ty = top.y - floorY;
    bool contact = (by > 0.0f && ty < 0.0f) || (by < 0.0f && ty > 0.0f);
    bool lastValid = burnCount > 0 && burnValid[burnHead];
    if (contact) {
        float t = by / (by - ty);
        Vector3 p = Add(bottom, Mul(Sub(top, bottom), t));
        p.y = floorY + 0.012f;
        if (!lastValid)
            PushBurn(p, now, false);  // zero-alpha breaker, so a new touch is not joined to an old one
        PushBurn(p, now, true);
    } else if (lastValid) {
        PushBurn(burnPos[burnHead], now, false);
    }

    float life = std::max(c.BurnLife.GetValue(), 0.05f);
    if (burnCount == 0 || now - burnTime[burnHead] > life) {
        layer->SetVisible(false);
        return;
    }

    layer->Resize(BurnCap - 1, 2, c.FlipTrailV.GetValue(), true);
    layer->SetVisible(true);
    auto verts = layer->vertices;
    auto cols = layer->colors;
    float halfW = std::max(c.BurnWidth.GetValue(), 0.5f) * 0.005f;  // cm -> m, half width
    float opacity = c.BurnOpacity.GetValue();
    Color base = BaseColor(now);
    Vector3 lateral{1, 0, 0};
    Vector3 lastPos = burnPos[burnHead];
    for (int k = 0; k < BurnCap; k++) {
        bool have = k < burnCount;
        int idx = (burnHead - k + BurnCap) % BurnCap;
        Vector3 pos = have ? burnPos[idx] : lastPos;
        float age = have ? now - burnTime[idx] : life;
        // direction of travel -> width across it, flat on the floor
        if (have && k + 1 < burnCount) {
            auto d = Sub(pos, burnPos[(idx - 1 + BurnCap) % BurnCap]);
            d.y = 0.0f;
            if (Len(d) > 1e-4f)
                lateral = Norm(Cross(Vector3{0, 1, 0}, d));
        }
        float a = 0.0f;
        if (have && burnValid[idx] && age < life) {
            float x = std::clamp(1.0f - age / life, 0.0f, 1.0f);
            a = opacity * x * x;
        }
        float hot = std::clamp(1.0f - age / (life * 0.2f), 0.0f, 1.0f);  // white-hot right after contact
        Color col = Lerp(base, Color(1, 1, 1, 1), hot);
        verts[k * 2] = Sub(pos, Mul(lateral, halfW));
        verts[k * 2 + 1] = Add(pos, Mul(lateral, halfW));
        cols[k * 2] = {col.r * a, col.g * a, col.b * a, a};
        cols[k * 2 + 1] = cols[k * 2];
        lastPos = pos;
    }
    layer->Upload();
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
    Vector3 anchor = Add(Add(Add(pvOrigin, Mul(pvRight, c.PreviewX.GetValue())), Mul(up, c.PreviewY.GetValue())), Mul(pvForward, c.PreviewZ.GetValue()));
    Vector3 hand = Add(Add(anchor, Mul(pvRight, 0.30f * s)), Mul(up, 0.08f * std::sin(2.0f * a)));
    bottom = Add(hand, Mul(dir, 0.16f));
    top = Add(bottom, Mul(dir, 1.0f * std::max(c.LengthScale.GetValue(), 0.1f)));
    float sc = std::clamp(c.PreviewScale.GetValue(), 0.2f, 2.0f);
    bottom = Add(anchor, Mul(Sub(bottom, anchor), sc));
    top = Add(anchor, Mul(Sub(top, anchor), sc));
}

void PrismTrail::DrawPreviewBlade(float now, Vector3 bottom, Vector3 top) {
    auto layer = bladeLayer ? bladeLayer.ptr() : nullptr;
    if (!layer)
        return;
    auto& c = getConfig();
    layer->Resize(3, 2, false, true);
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
    Color hilt{0.03f, 0.03f, 0.035f, 1.0f};
    Color blade{col.r, col.g, col.b, 1.0f};
    Color bladeEnd = blade;
    if (c.BladeGradient.GetValue()) {
        blade = isLeft ? c.GradLeftStart.GetValue() : c.GradRightStart.GetValue();
        bladeEnd = isLeft ? c.GradLeftEnd.GetValue() : c.GradRightEnd.GetValue();
        blade.a = bladeEnd.a = 1.0f;
    }

    auto verts = layer->vertices;
    auto cols = layer->colors;
    // sample 0-1: hilt, sample 2-3: blade (1 and 2 share a position, so the strip between them has no area)
    Vector3 centers[4] = {hiltBottom, bottom, bottom, top};
    float widths[4] = {hiltW, hiltW, bladeW, bladeW};
    Color colors[4] = {hilt, hilt, blade, bladeEnd};
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

    static UnityW<Material> g_solidMaterial;

    // flat, unlit, alpha-blended material for solid parts (black handle, blade); built-in shader, so it is always there
    Material* SolidMaterial() {
        if (g_solidMaterial)
            return g_solidMaterial.ptr();
        auto shader = Shader::Find("Sprites/Default");
        if (!shader)
            return nullptr;
        g_solidMaterial = Material::New_ctor(shader);
        return g_solidMaterial.ptr();
    }

    void ShowPreview(Transform* anchor) {
        if (PreviewVisible())
            return;
        auto material = FindTrailMaterial();
        if (!material)
            return;
        // Fixed spot measured from YOUR HEAD at the moment the tab opens (not from the menu panel, whose position
        // is unreliable): a bit to the left, below eye level, closer than the menu so it can never end up behind it.
        Vector3 origin{0, 1.6f, 0}, right{1, 0, 0}, fwd{0, 0, 1};
        if (auto cam = Camera::get_main()) {
            auto t = cam->get_transform();
            origin = t->get_position();
            auto f = t->get_forward();
            f.y = 0;
            if (Len(f) > 0.01f) {
                fwd = Norm(f);
                right = Norm(Cross(Vector3{0, 1, 0}, fwd));
            }
        } else if (anchor) {
            origin = anchor->get_position();
        }
        auto solid = SolidMaterial();
        for (int i = 0; i < 2; i++) {
            auto go = GameObject::New_ctor(i == 0 ? "PrysmPreviewLeft" : "PrysmPreviewRight");
            auto trail = go->AddComponent<PrismTrail*>();
            trail->SetupPreview(material, solid, 0, i == 0, origin, right, fwd);
            g_previewRoots[i] = go;
        }
    }

    // ----------------------------------------------------------------------------------------
    // Prysm saber model: black handle + simple thin blade, generated in code (no asset bundle needed)
    // ----------------------------------------------------------------------------------------

    static Mesh* MakeTube(Vector3 origin, Vector3 dir, Vector3 u, Vector3 v, std::vector<std::pair<float, float>> const& rings, int seg, std::vector<Color> const* ringColors = nullptr) {
        int n = (int) rings.size();
        int vcount = n * seg + 2;
        auto verts = ArrayW<Vector3>(il2cpp_array_size_t(vcount));
        auto tris = ArrayW<int>(il2cpp_array_size_t((n - 1) * seg * 6 + seg * 6));
        for (int r = 0; r < n; r++) {
            for (int s = 0; s < seg; s++) {
                float ang = 6.2831853f * s / seg;
                auto offset = Add(Mul(u, std::cos(ang) * rings[r].second), Mul(v, std::sin(ang) * rings[r].second));
                verts[r * seg + s] = Add(Add(origin, Mul(dir, rings[r].first)), offset);
            }
        }
        int bottomCenter = n * seg, topCenter = n * seg + 1;
        verts[bottomCenter] = Add(origin, Mul(dir, rings[0].first));
        verts[topCenter] = Add(origin, Mul(dir, rings[n - 1].first));
        int t = 0;
        for (int r = 0; r < n - 1; r++) {
            for (int s = 0; s < seg; s++) {
                int a = r * seg + s, b = r * seg + (s + 1) % seg, c2 = (r + 1) * seg + s, d = (r + 1) * seg + (s + 1) % seg;
                tris[t++] = a; tris[t++] = b; tris[t++] = c2;
                tris[t++] = c2; tris[t++] = b; tris[t++] = d;
            }
        }
        for (int s = 0; s < seg; s++) {
            int s2 = (s + 1) % seg;
            tris[t++] = bottomCenter; tris[t++] = s2; tris[t++] = s;
            tris[t++] = topCenter; tris[t++] = (n - 1) * seg + s; tris[t++] = (n - 1) * seg + s2;
        }
        auto mesh = Mesh::New_ctor();
        mesh->set_vertices(verts);
        mesh->set_triangles(tris);
        if (ringColors && (int) ringColors->size() == n) {
            auto cols = ArrayW<Color>(il2cpp_array_size_t(vcount));
            for (int r = 0; r < n; r++)
                for (int s = 0; s < seg; s++)
                    cols[r * seg + s] = (*ringColors)[r];
            cols[bottomCenter] = (*ringColors)[0];
            cols[topCenter] = (*ringColors)[n - 1];
            mesh->set_colors(cols);
        }
        mesh->RecalculateBounds();
        return mesh;
    }

    void ApplyCustomModel(GlobalNamespace::SaberModelController* model, GlobalNamespace::Saber* saber, PrismTrail* trail) {
        auto solid = SolidMaterial();
        if (!solid) {
            logger.warn("Sprites/Default shader not found; keeping the game saber model");
            return;
        }
        auto bottomT = saber->_saberBladeBottomTransform;
        auto topT = saber->_saberBladeTopTransform;
        if (!bottomT || !topT)
            return;
        auto& c = getConfig();
        auto root = model->get_transform();
        Vector3 b = root->InverseTransformPoint(bottomT->get_position());
        Vector3 tp = root->InverseTransformPoint(topT->get_position());
        Vector3 dir = Norm(Sub(tp, b));
        float bladeLen = Len(Sub(tp, b)) * std::max(c.LengthScale.GetValue(), 0.1f);
        Vector3 helper = std::abs(dir.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
        Vector3 u = Norm(Cross(dir, helper));
        Vector3 v = Cross(dir, u);

        // hide the game's handle and blade meshes, but keep the fake-glow sprites (they are the saber's soft glow)
        int hidden = 0;
        auto renderers = model->GetComponentsInChildren<MeshRenderer*>(true);
        for (auto r : renderers) {
            if (!r)
                continue;
            if (r->get_gameObject()->GetComponent<GlobalNamespace::Parametric3SliceSpriteController*>())
                continue;
            r->set_enabled(false);
            hidden++;
        }

        float thick = std::clamp(c.ThicknessScale.GetValue(), 0.2f, 3.0f);
        float r = 0.0105f * thick;
        auto hiltMesh = MakeTube(b, dir, u, v, {{-0.17f, 0.0135f}, {-0.163f, 0.0172f}, {-0.03f, 0.0172f}, {0.0f, 0.0145f}}, 10);
        bool gradient = c.BladeGradient.GetValue();
        std::vector<std::pair<float, float>> bladeRings = {{0.0f, r}, {bladeLen - 0.025f, r}, {bladeLen - 0.008f, r * 0.8f}, {bladeLen, r * 0.4f}};
        std::vector<Color> bladeColors;
        if (gradient) {
            bool left = trail->IsLeft();
            Color g0 = left ? c.GradLeftStart.GetValue() : c.GradRightStart.GetValue();
            Color g1 = left ? c.GradLeftEnd.GetValue() : c.GradRightEnd.GetValue();
            for (auto const& ring : bladeRings)
                bladeColors.push_back(Lerp(g0, g1, std::clamp(ring.first / std::max(bladeLen, 0.01f), 0.0f, 1.0f)));
        }
        auto bladeMesh = MakeTube(b, dir, u, v, bladeRings, 8, gradient ? &bladeColors : nullptr);

        auto hiltMat = Material::New_ctor(solid);
        hiltMat->set_color(Color(0.02f, 0.02f, 0.025f, 1.0f));
        auto bladeMat = Material::New_ctor(solid);
        bladeMat->set_color(Color(1, 1, 1, 1));

        int layer = model->get_gameObject()->get_layer();
        auto addPart = [&](char const* name, Mesh* mesh, Material* mat) {
            auto go = GameObject::New_ctor(name);
            go->set_layer(layer);
            go->get_transform()->SetParent(root, false);
            go->AddComponent<MeshFilter*>()->set_sharedMesh(mesh);
            auto mr = go->AddComponent<MeshRenderer*>();
            mr->set_sharedMaterial(mat);
            mr->set_shadowCastingMode(Rendering::ShadowCastingMode::Off);
            mr->set_receiveShadows(false);
        };
        addPart("PrysmHilt", hiltMesh, hiltMat);
        addPart("PrysmBlade", bladeMesh, bladeMat);
        trail->SetBladeMaterial(gradient ? nullptr : bladeMat);
        logger.info("Prysm saber model applied (hid {} game renderers, blade length {})", hidden, bladeLen);
    }
}
