#include "PrismTrail.hpp"

#include <algorithm>
#include <cmath>

#include "GlobalNamespace/ColorManager.hpp"
#include "GlobalNamespace/Parametric3SliceSpriteController.hpp"
#include "GlobalNamespace/SetSaberFakeGlowColor.hpp"
#include "GlobalNamespace/SetSaberGlowColor.hpp"
#include "GlobalNamespace/TubeBloomPrePassLight.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/MaterialPropertyBlock.hpp"
#include "UnityEngine/MeshFilter.hpp"
#include "UnityEngine/Rendering/ShadowCastingMode.hpp"
#include "UnityEngine/Time.hpp"
#include "config.hpp"
#include "main.hpp"

DEFINE_TYPE(PrismSabers, PrismTrailLayer);
DEFINE_TYPE(PrismSabers, PrismTrail);

using namespace UnityEngine;

namespace PrismSabers {
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
            default: return READ_LAYER(Blur);
        }
    }
}

using namespace PrismSabers;

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
    isLeft = saber->get_saberType() == GlobalNamespace::SaberType::SaberA;
    gameColor = model->_colorManager ? model->_colorManager->ColorForSaberType(saber->get_saberType()) : Color(1, 1, 1, 1);
    head = 0;
    count = 0;
    frames = 0;
    smoothedSpeed = 0;
    lastColor = Color(0, 0, 0, 0);
    lastIntensity = 1.0f;
    lastMode = 0;
    modified = false;

    static constexpr char const* names[] = {"PrismMain", "PrismAura", "PrismBlur"};
    for (int i = 0; i < 3; i++) {
        auto go = GameObject::New_ctor(names[i]);
        go->set_layer(layer);
        // world-space vertices; keep the mesh object at the origin
        auto l = go->AddComponent<PrismTrailLayer*>();
        l->Init(material, i == 1 ? -1 : 0);
        (i == 0 ? mainLayer : i == 1 ? glowLayer : blurLayer) = l;
    }
}

void PrismTrail::OnEnable() {
    count = 0;
    for (auto l : {mainLayer, glowLayer, blurLayer})
        if (l)
            l->get_gameObject()->SetActive(true);
}

void PrismTrail::OnDisable() {
    for (auto l : {mainLayer, glowLayer, blurLayer})
        if (l)
            l->get_gameObject()->SetActive(false);
}

void PrismTrail::OnDestroy() {
    for (auto l : {mainLayer, glowLayer, blurLayer})
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

void PrismTrail::BuildLayer(PrismTrailLayer* layer, int which, float now, float speedFactor) {
    static constexpr int SoftCross = 8;
    auto cfg = ReadLayer(which);
    auto& c = getConfig();
    bool perf = c.PerformanceMode.GetValue();
    float alphaScale = which == 2 ? speedFactor : 1.0f;
    if (!cfg.enabled || cfg.opacity <= 0.001f || alphaScale <= 0.001f || (perf && which == 2) || count < 2) {
        layer->SetVisible(false);
        return;
    }
    int gran = std::clamp(cfg.granularity, 2, perf ? 12 : 96);
    float soft = perf ? 0.0f : std::clamp(cfg.soft, 0.0f, 1.0f);
    int cross = soft > 0.01f ? SoftCross : 2;
    layer->Resize(gran, cross);
    layer->SetVisible(true);

    Color base = BaseColor(now);
    float duration = std::max(cfg.duration, 0.005f);
    // velocity based: the blur stretches out as the blade speeds up
    if (which == 2)
        duration *= 0.6f + 0.8f * speedFactor;

    // Positions across the blade (fraction of blade length) and their opacity weights.
    // With softness, 3 feather vertices sit outside each end of the blade, fading smoothly to 0,
    // so the swept area has no hard edge at the hilt side, the tip side or the tail.
    float span = std::max(cfg.tip - cfg.base, 0.1f);
    float f = soft * span * 0.4f;
    float tpos[SoftCross], wgt[SoftCross];
    if (cross == 2) {
        tpos[0] = cfg.base;
        tpos[1] = cfg.tip;
        wgt[0] = wgt[1] = 1.0f;
    } else {
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
    for (int i = 0; i <= gran; i++) {
        float u = (float) i / gran;
        auto s = Evaluate(now - duration * u);
        auto blade = Sub(s.top, s.bottom);

        Color col;
        switch (cfg.source) {
            case CustomColor: col = cfg.custom; break;
            case Rainbow: col = HSV(now * c.RainbowSpeed.GetValue() + u * 0.6f + (isLeft ? 0.0f : c.RainbowOffset.GetValue()), c.RainbowSaturation.GetValue(), 1.0f); break;
            case SaberToCustom: col = Lerp(base, cfg.custom, u); break;
            default: col = base; break;
        }
        if (cfg.white > 0.0f && u < cfg.white)
            col = Lerp(col, Color(1, 1, 1, 1), 1.0f - u / cfg.white);
        float a = std::clamp(cfg.opacity * alphaScale * std::pow(1.0f - u, std::max(cfg.fade, 0.05f)), 0.0f, 1.0f);

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
    if (!saber || !model)
        return;
    auto& c = getConfig();
    bool enabled = c.Enabled.GetValue();
    float now = Time::get_time();
    frames++;

    auto bottomT = saber->_saberBladeBottomTransform;
    auto topT = saber->_saberBladeTopTransform;
    if (!bottomT || !topT)
        return;
    auto bottom = bottomT->get_position();
    auto top = topT->get_position();
    float length = c.LengthScale.GetValue();
    if (std::abs(length - 1.0f) > 0.001f)
        top = Add(bottom, Mul(Sub(top, bottom), length));

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
        for (auto l : {mainLayer, glowLayer, blurLayer})
            if (l)
                l->SetVisible(false);
        return;
    }

    float minS = c.BlurMinSpeed.GetValue(), fullS = std::max(c.BlurFullSpeed.GetValue(), minS + 0.01f);
    float speedFactor = std::clamp((smoothedSpeed - minS) / (fullS - minS), 0.0f, 1.0f);

    if (mainLayer) BuildLayer(mainLayer.ptr(), 0, now, speedFactor);
    if (glowLayer) BuildLayer(glowLayer.ptr(), 1, now, speedFactor);
    if (blurLayer) BuildLayer(blurLayer.ptr(), 2, now, speedFactor);
    ApplyBladeColors(now);
}
