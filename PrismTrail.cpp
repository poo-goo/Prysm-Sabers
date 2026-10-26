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
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/TextureWrapMode.hpp"
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
    static inline Vector3 Rot(Vector3 v, Vector3 n, float ang) {
        float cs = std::cos(ang), sn = std::sin(ang);
        float d = (n.x * v.x + n.y * v.y + n.z * v.z) * (1.0f - cs);
        auto cr = Cross(n, v);
        return {v.x * cs + cr.x * sn + n.x * d, v.y * cs + cr.y * sn + n.y * d, v.z * cs + cr.z * sn + n.z * d};
    }
    static inline float Hash3(int a, int b, int c) {
        float x = std::sin(a * 12.9898f + b * 78.233f + c * 37.719f) * 43758.5453f;
        return x - std::floor(x);
    }
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

void PrismTrailLayer::ResizeQuads(int n) {
    if (n == granularity && crossCount == -1 && vertices)
        return;
    granularity = n;
    crossCount = -1;
    flipV = false;
    flatU = false;
    vertices = ArrayW<Vector3>(il2cpp_array_size_t(n * 4));
    colors = ArrayW<Color>(il2cpp_array_size_t(n * 4));
    uvs = ArrayW<Vector2>(il2cpp_array_size_t(n * 4));
    triangles = ArrayW<int>(il2cpp_array_size_t(n * 6));
    for (int q = 0; q < n; q++) {
        int b = q * 4;
        uvs[b] = {0, 0};
        uvs[b + 1] = {1, 0};
        uvs[b + 2] = {0, 1};
        uvs[b + 3] = {1, 1};
        int t = q * 6;
        triangles[t] = b;
        triangles[t + 1] = b + 1;
        triangles[t + 2] = b + 2;
        triangles[t + 3] = b + 2;
        triangles[t + 4] = b + 1;
        triangles[t + 5] = b + 3;
    }
    mesh->Clear();
    mesh->set_vertices(vertices);
    mesh->set_colors(colors);
    mesh->set_uv(uvs);
    mesh->set_triangles(triangles);
}

void PrismTrailLayer::ResizeFacets(int rows, int cols, bool flip, bool flat) {
    int marker = 1000 + cols;
    if (rows == granularity && crossCount == marker && flip == flipV && flat == flatU && vertices)
        return;
    granularity = rows;
    crossCount = marker;
    flipV = flip;
    flatU = flat;
    int n = rows * cols;
    vertices = ArrayW<Vector3>(il2cpp_array_size_t(n * 4));
    colors = ArrayW<Color>(il2cpp_array_size_t(n * 4));
    uvs = ArrayW<Vector2>(il2cpp_array_size_t(n * 4));
    triangles = ArrayW<int>(il2cpp_array_size_t(n * 6));
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            int b = (i * cols + j) * 4;
            float u0 = flat ? 0.0f : (float) i / rows, u1 = flat ? 0.0f : (float) (i + 1) / rows;
            float v0 = (float) j / cols, v1 = (float) (j + 1) / cols;
            if (flip) {
                v0 = 1.0f - v0;
                v1 = 1.0f - v1;
            }
            uvs[b] = {u0, v0};
            uvs[b + 1] = {u0, v1};
            uvs[b + 2] = {u1, v0};
            uvs[b + 3] = {u1, v1};
            int t = (i * cols + j) * 6;
            triangles[t] = b;
            triangles[t + 1] = b + 1;
            triangles[t + 2] = b + 2;
            triangles[t + 3] = b + 2;
            triangles[t + 4] = b + 1;
            triangles[t + 5] = b + 3;
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
    rng = 0x9E3779B9u ^ (isLeft ? 0x1234567u : 0x7654321u);
    partTime = 0;
    emitAcc = 0;
    seeded = false;
    partLastTop = Vector3{0, 0, 0};
    for (int i = 0; i < PCap; i++)
        parts[i].life = 0;
    static constexpr char const* names[] = {"PrismMain", "PrismAura", "PrismBlur", "PrismCurve", "PrismRibbon", "PrismPreviewBlade", "PrismBurn", "PrismSparks", "PrismShards"};
    static constexpr int orders[] = {0, -2, 1, -1, 2, 3, -3, 4, 5};
    for (int i = 0; i < 9; i++) {
        if ((i == 5 && !preview) || (i == 6 && preview))
            continue;
        Material* mat = material;
        if (i == 5 && solid)
            mat = solid;
        if (i == 7)
            mat = SoftMaterial();
        if (i == 8)
            mat = SolidMaterial();
        if (!mat)
            continue;
        auto go = GameObject::New_ctor(names[i]);
        go->set_layer(layer);
        auto l = go->AddComponent<PrismTrailLayer*>();
        l->Init(mat, orders[i]);
        switch (i) {
            case 0: mainLayer = l; break;
            case 1: glowLayer = l; break;
            case 2: blurLayer = l; break;
            case 3: curveLayer = l; break;
            case 4: ribbonLayer = l; break;
            case 5: bladeLayer = l; break;
            case 6: burnLayer = l; break;
            case 7: partSoftLayer = l; break;
            default: partSharpLayer = l; break;
        }
    }
}

void PrismTrail::OnEnable() {
    count = 0;
    for (auto l : {mainLayer, glowLayer, blurLayer, curveLayer, ribbonLayer, bladeLayer, burnLayer, partSoftLayer, partSharpLayer})
        if (l)
            l->get_gameObject()->SetActive(true);
}

void PrismTrail::OnDisable() {
    for (auto l : {mainLayer, glowLayer, blurLayer, curveLayer, ribbonLayer, bladeLayer, burnLayer, partSoftLayer, partSharpLayer})
        if (l)
            l->get_gameObject()->SetActive(false);
}

void PrismTrail::OnDestroy() {
    for (auto l : {mainLayer, glowLayer, blurLayer, curveLayer, ribbonLayer, bladeLayer, burnLayer, partSoftLayer, partSharpLayer})
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
    static constexpr int CurveCross = 16;  // more points across the blade so the exponential edge looks smooth
    auto cfg = ReadLayer(which);
    auto& c = getConfig();
    bool perf = c.PerformanceMode.GetValue();
    bool isBlur = which == 2, isCurve = which == 3;
    float alphaScale = isBlur ? blurFactor : 1.0f;
    if (!cfg.enabled || cfg.opacity <= 0.001f || alphaScale <= 0.001f || (perf && (isBlur || isCurve)) || count < 2) {
        layer->SetVisible(false);
        return;
    }
    float crystal = (isBlur || perf) ? 0.0f : std::clamp(c.CrystalAmount.GetValue(), 0.0f, 1.0f);
    bool facet = crystal > 0.01f;
    int facets = std::clamp(c.CrystalFacets.GetValue(), 3, 24);
    int gran = facet ? facets : std::clamp(cfg.granularity, 2, perf ? 12 : 96);
    float soft = perf ? 0.0f : std::clamp(cfg.soft, 0.0f, 1.0f);
    int cross = isCurve ? (facet ? std::clamp(facets / 2 + 2, 4, 9) : CurveCross) : (soft > 0.01f ? SoftCross : 2);
    layer->SetVisible(true);
    layer->SetSolid(cfg.solid, SolidMaterial());
    bool solidMode = layer->useSolid;
    // Solid layers ignore the game texture, so U does not matter. The game's glowing material needs the real U, because its
    // texture is what shapes the glow (flattening U made those layers invisible).
    bool flatU = solidMode;
    if (facet)
        layer->ResizeFacets(gran, cross - 1, c.FlipTrailV.GetValue(), flatU);
    else
        layer->Resize(gran, cross, c.FlipTrailV.GetValue(), flatU);

    Color base = BaseColor(now);
    float duration = std::max(cfg.duration, 0.005f);
    // velocity based: the blur stretches out as the blade speeds up
    if (isBlur)
        duration *= 0.6f + 0.8f * speedFactor;

    // Positions across the blade (fraction of blade length) and their opacity weights.
    float span = std::max(cfg.tip - cfg.base, 0.1f);
    float f = soft * span * 0.3f;
    float tpos[CurveCross], wgt[CurveCross], lag[CurveCross];
    if (isCurve) {
        // "Curve": every point along the blade trails behind by its own amount of time. The hilt does not lag at all,
        // the tip lags the most, and the lag grows exponentially, so the swept sheet is bounded by an exponential
        // curve (zero width at the hilt, long at the tip) - exactly the shape of an exponential graph.
        float taper = std::clamp(c.CurveTaper.GetValue(), 0.0f, 6.0f);
        float bend = std::clamp(c.CurveBend.GetValue(), 0.0f, 12.0f);
        for (int j = 0; j < cross; j++) {
            float fr = (float) j / (cross - 1);
            tpos[j] = cfg.base + (cfg.tip - cfg.base) * fr;
            wgt[j] = taper < 0.01f ? 1.0f : std::pow(fr, taper);
            lag[j] = bend < 0.05f ? fr : (std::exp(bend * fr) - 1.0f) / (std::exp(bend) - 1.0f);
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

    // faceted layers are computed on a shared lattice first, then copied into unshared, flat-coloured facets
    static std::vector<Vector3> latV;
    static std::vector<Color> latC;
    if (facet) {
        latV.assign((size_t) (gran + 1) * cross, Vector3{0, 0, 0});
        latC.assign((size_t) (gran + 1) * cross, Color{0, 0, 0, 0});
    }
    auto verts = layer->vertices;
    auto cols = layer->colors;
    auto putV = [&](int idx, Vector3 pos) {
        if (facet)
            latV[idx] = pos;
        else
            verts[idx] = pos;
    };
    auto putC = [&](int idx, Color col) {
        if (facet)
            latC[idx] = col;
        else
            cols[idx] = col;
    };
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
            Vector3 pos;
            if (isCurve) {
                auto sj = Evaluate(now - duration * u * lag[j]);
                pos = Add(sj.bottom, Mul(Sub(sj.top, sj.bottom), tpos[j]));
            } else {
                pos = Add(s.bottom, Mul(blade, tpos[j]));
            }
            if (facet && i > 0 && i < gran && j > 0 && j < cross - 1)
                pos = Add(pos, Mul(blade, (Hash3(i, j, which + 3) - 0.5f) * 0.07f * crystal));  // irregular facets
            putV(idx, pos);
            float aj = a * wgt[j];
            putC(idx, solidMode ? Color{col.r, col.g, col.b, aj} : Color{col.r * aj, col.g * aj, col.b * aj, aj});
        }
    }
    if (facet) {
        auto fv = layer->vertices;
        auto fc = layer->colors;
        int fcols = cross - 1;
        for (int i = 0; i < gran; i++) {
            for (int j = 0; j < fcols; j++) {
                int ia = i * cross + j, ib = ia + 1, ic = ia + cross, id = ic + 1;
                int q = (i * fcols + j) * 4;
                fv[q] = latV[ia];
                fv[q + 1] = latV[ib];
                fv[q + 2] = latV[ic];
                fv[q + 3] = latV[id];
                Color avg{(latC[ia].r + latC[ib].r + latC[ic].r + latC[id].r) * 0.25f, (latC[ia].g + latC[ib].g + latC[ic].g + latC[id].g) * 0.25f,
                          (latC[ia].b + latC[ib].b + latC[ic].b + latC[id].b) * 0.25f, (latC[ia].a + latC[ib].a + latC[ic].a + latC[id].a) * 0.25f};
                // flat shading: each facet is one brightness, a few catch the light
                float shade = 1.0f - crystal * 0.55f * Hash3(i, j, which + 11);
                float glint = Hash3(i * 3 + 1, j * 5 + 2, which + 23) > 0.82f ? crystal * 0.65f : 0.0f;
                float white = solidMode ? 1.0f : avg.a;  // "white" in premultiplied terms is just alpha
                Color out{(avg.r * shade) * (1.0f - glint) + white * glint, (avg.g * shade) * (1.0f - glint) + white * glint,
                          (avg.b * shade) * (1.0f - glint) + white * glint, avg.a};
                for (int k = 0; k < 4; k++)
                    fc[q + k] = out;
            }
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
        for (auto l : {mainLayer, glowLayer, blurLayer, curveLayer, ribbonLayer, bladeLayer, burnLayer, partSoftLayer, partSharpLayer})
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
        if (bladeMat) {
            Color tint = c.BladeGradient.GetValue() ? Color(1, 1, 1, 1) : BaseColor(now);
            if (c.FlameBlade.GetValue()) {
                // flickering flame: two out-of-phase sines modulate the blade brightness
                float ph = isLeft ? 0.0f : 2.0f;
                float f = 0.74f + 0.26f * (0.5f + 0.5f * std::sin(now * 43.0f + ph)) * (0.6f + 0.4f * std::sin(now * 17.3f + 1.3f + ph));
                tint = Color(tint.r * f, tint.g * f, tint.b * f, 1.0f);
            }
            bladeMat->set_color(tint);
        }
    }
    UpdateParticles(now, bottom, top);
}



// ---------------------------------------------------------------------------------------------
// Particles: fire (flame wisps + embers) and orbiting glass shards. CPU simulated, drawn as quads.
// ---------------------------------------------------------------------------------------------

float PrismTrail::Rand01() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (rng & 0xFFFFFF) / 16777216.0f;
}

void PrismTrail::UpdateParticles(float now, Vector3 bottom, Vector3 top) {
    auto soft = partSoftLayer ? partSoftLayer.ptr() : nullptr;
    auto sharp = partSharpLayer ? partSharpLayer.ptr() : nullptr;
    if (!soft || !sharp)
        return;
    auto& c = getConfig();
    int style = c.PerformanceMode.GetValue() ? 0 : c.ParticleStyle.GetValue();
    float dt = partTime > 0.0f ? std::clamp(now - partTime, 0.0f, 0.05f) : 0.0f;
    partTime = now > 0.0f ? now : 0.001f;

    bool any = false;
    for (int i = 0; i < PCap; i++) {
        if (parts[i].life > 0.0f) {
            any = true;
            break;
        }
    }
    if (style == 0 && !any) {
        soft->SetVisible(false);
        sharp->SetVisible(false);
        seeded = false;
        return;
    }

    Vector3 bladeVel{0, 0, 0};
    if (dt > 1e-4f) {
        bladeVel = Mul(Sub(top, partLastTop), 1.0f / dt);
        float sp = Len(bladeVel);
        if (sp > 12.0f)
            bladeVel = Mul(bladeVel, 12.0f / sp);
    }
    partLastTop = top;

    Vector3 camRight{1, 0, 0}, camUp{0, 1, 0};
    if (auto cam = Camera::get_main()) {
        auto t = cam->get_transform();
        camRight = t->get_right();
        camUp = t->get_up();
    }
    Vector3 dirV = Sub(top, bottom);
    Vector3 dir = Norm(dirV);
    Vector3 helper = std::abs(dir.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
    Vector3 u = Norm(Cross(dir, helper));
    Vector3 v = Cross(dir, u);

    Color g0 = isLeft ? c.GradLeftStart.GetValue() : c.GradRightStart.GetValue();
    Color g1 = isLeft ? c.GradLeftEnd.GetValue() : c.GradRightEnd.GetValue();
    Color saberCol = BaseColor(now);
    float amount = std::clamp(c.ParticleAmount.GetValue(), 0.1f, 3.0f);
    float sizeS = std::clamp(c.ParticleSize.GetValue(), 0.3f, 3.0f);
    float lifeS = std::clamp(c.ParticleLife.GetValue(), 0.3f, 3.0f);

    auto slot = [&]() -> Particle* {
        for (int i = 0; i < PCap; i++)
            if (parts[i].life <= 0.0f)
                return &parts[i];
        return nullptr;
    };

    if (style == 1) {
        // fire: more sparks the faster the blade moves
        float speed = std::min(Len(bladeVel), 10.0f);
        emitAcc += amount * (30.0f + 12.0f * speed) * dt;
        int guard = 0;
        while (emitAcc >= 1.0f && guard++ < 8) {
            emitAcc -= 1.0f;
            auto p = slot();
            if (!p)
                break;
            bool ember = Rand01() < 0.4f;
            float t = ember ? 0.25f + 0.75f * Rand01() : 0.08f + 0.92f * Rand01();
            Vector3 off = Add(Mul(u, (Rand01() - 0.5f) * 0.03f), Mul(v, (Rand01() - 0.5f) * 0.03f));
            p->pos = Add(Add(bottom, Mul(dirV, t)), off);
            Vector3 jit{Rand01() - 0.5f, Rand01() - 0.5f, Rand01() - 0.5f};
            if (ember) {
                p->vel = Add(Add(Mul(jit, 1.2f), Vector3{0, 0.35f, 0}), Mul(bladeVel, 0.15f));
                p->life = (0.5f + 0.7f * Rand01()) * lifeS;
                p->size = (0.006f + 0.008f * Rand01()) * sizeS;
                p->kind = 1;
            } else {
                p->vel = Add(Add(Mul(jit, 0.25f), Vector3{0, 0.55f, 0}), Mul(bladeVel, 0.1f));
                p->life = (0.18f + 0.27f * Rand01()) * lifeS;
                p->size = (0.03f + 0.04f * Rand01()) * sizeS;
                p->kind = 0;
            }
            p->age = 0.0f;
            p->alpha = 0.5f + 0.5f * Rand01();
            p->t = Rand01();
        }
    } else if (style == 2) {
        // shards: keep a fixed population orbiting the blade
        int target = std::clamp((int) std::round(24.0f * amount), 1, 90);
        int alive = 0;
        for (int i = 0; i < PCap; i++)
            if (parts[i].life > 0.0f && parts[i].kind == 2)
                alive++;
        int spawned = 0;
        while (alive < target && spawned < 6) {
            auto p = slot();
            if (!p)
                break;
            p->kind = 2;
            p->t = 0.1f + 0.9f * Rand01();
            p->r = 0.010f + 0.030f * Rand01();  // close to the blade
            p->lag = 0.02f + 0.09f * Rand01();   // each shard follows where the blade WAS a moment ago
            p->w = (Rand01() < 0.5f ? -1.0f : 1.0f) * (1.5f + 3.0f * Rand01());
            p->phase = 6.2831853f * Rand01();
            p->life = (1.2f + 2.3f * Rand01()) * lifeS;
            p->age = seeded ? 0.0f : p->life * Rand01() * 0.9f;  // first fill starts at random ages
            p->size = (0.007f + 0.011f * Rand01()) * sizeS;
            p->alpha = 0.35f + 0.65f * Rand01();
            p->blur = Rand01();
            for (int k = 0; k < 8; k++)
                p->jit[k] = (Rand01() - 0.5f) * 0.7f;
            Vector3 n = Norm(Vector3{Rand01() - 0.5f, Rand01() - 0.5f, Rand01() - 0.5f});
            Vector3 hp = std::abs(n.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
            p->n = n;
            p->e1 = Norm(Cross(n, hp));
            p->e2 = Cross(n, p->e1);
            p->rot = 6.2831853f * Rand01();
            p->spin = (Rand01() - 0.5f) * 8.0f;
            alive++;
            spawned++;
        }
        seeded = true;
    }

    soft->ResizeQuads(PCap * 2);
    sharp->ResizeQuads(PCap);
    soft->SetVisible(true);
    sharp->SetVisible(true);
    auto sv = soft->vertices;
    auto sc = soft->colors;
    auto hv = sharp->vertices;
    auto hc = sharp->colors;
    int sq = 0, hq = 0;
    auto softQuad = [&](Vector3 ctr, Vector3 rx, Vector3 ry, Color col) {
        if (sq >= PCap * 2)
            return;
        int b = sq * 4;
        sv[b] = Sub(Sub(ctr, rx), ry);
        sv[b + 1] = Sub(Add(ctr, rx), ry);
        sv[b + 2] = Add(Sub(ctr, rx), ry);
        sv[b + 3] = Add(Add(ctr, rx), ry);
        for (int k = 0; k < 4; k++)
            sc[b + k] = col;
        sq++;
    };

    static constexpr float sx[4] = {-1, 1, -1, 1};
    static constexpr float sy[4] = {-1, -1, 1, 1};
    for (int i = 0; i < PCap; i++) {
        auto& p = parts[i];
        if (p.life <= 0.0f)
            continue;
        p.age += dt;
        if (p.age >= p.life) {
            p.life = 0.0f;
            continue;
        }
        float x = p.age / p.life;
        if (p.kind == 0) {  // flame wisp: soft, tall, rises and shrinks
            p.vel = Mul(p.vel, 1.0f - std::min(2.0f * dt, 0.9f));
            p.pos = Add(p.pos, Mul(p.vel, dt));
            Color col = Lerp(g0, g1, x);
            float a = p.alpha * 0.7f * (1.0f - x) * std::min(1.0f, x * 10.0f);
            float sz = p.size * (1.0f - 0.45f * x);
            softQuad(p.pos, Mul(camRight, sz), Vector3{0, sz * 1.9f, 0}, Color{col.r, col.g, col.b, a});
        } else if (p.kind == 1) {  // ember: tiny, hot, flickers
            p.vel = Mul(p.vel, 1.0f - std::min(1.2f * dt, 0.9f));
            p.vel.y += 0.25f * dt;
            p.pos = Add(p.pos, Mul(p.vel, dt));
            Color hot{1.0f, 0.92f, 0.65f, 1.0f};
            Color col = Lerp(hot, g0, std::min(1.0f, x * 1.5f));
            float flick = 0.55f + 0.45f * std::sin(p.t * 61.0f + now * 35.0f);
            float a = p.alpha * (1.0f - x) * flick;
            softQuad(p.pos, Mul(camRight, p.size), Mul(camUp, p.size), Color{col.r, col.g, col.b, a});
        } else {  // shard: orbits the blade, tumbles, glassy facets
            float th = p.phase + p.w * p.age;
            float rr = p.r * (1.0f + 0.15f * std::sin(p.age * 3.0f + p.phase));
            Vector3 sb = bottom, st = top;
            if (count >= 2) {
                auto past = Evaluate(now - p.lag);
                sb = past.bottom;
                st = past.top;
            }
            Vector3 bp = Add(sb, Mul(Sub(st, sb), p.t));
            p.pos = Add(bp, Add(Mul(u, std::cos(th) * rr), Mul(v, std::sin(th) * rr)));
            float ph = p.rot + p.spin * p.age;
            Vector3 e1 = Rot(p.e1, p.n, ph), e2 = Rot(p.e2, p.n, ph);
            float fade = std::min(1.0f, p.age / 0.15f) * std::min(1.0f, (p.life - p.age) / (0.35f * p.life));
            float a = p.alpha * fade;
            Color base = Lerp(saberCol, Color(1, 1, 1, 1), 0.55f);
            Color ice = Lerp(base, Color(0.55f, 0.95f, 1.0f, 1.0f), 0.35f);
            if (hq < PCap) {
                int b = hq * 4;
                float as = a * (1.0f - 0.45f * p.blur);  // blurrier shards are fainter, their halo carries them
                for (int k = 0; k < 4; k++) {
                    Vector3 off = Add(Mul(e1, sx[k] * (1.0f + p.jit[2 * k]) * p.size), Mul(e2, sy[k] * (1.0f + p.jit[2 * k + 1]) * p.size));
                    hv[b + k] = Add(p.pos, off);
                }
                hc[b] = Color{base.r, base.g, base.b, as};
                hc[b + 1] = Color{ice.r, ice.g, ice.b, as * 0.65f};
                hc[b + 2] = Color{ice.r, ice.g, ice.b, as * 0.85f};
                hc[b + 3] = Color{1.0f, 1.0f, 1.0f, as};
                hq++;
            }
            if (p.blur > 0.35f) {
                float hs = p.size * (2.0f + 2.0f * p.blur);
                softQuad(p.pos, Mul(camRight, hs), Mul(camUp, hs), Color{base.r, base.g, base.b, a * 0.3f * p.blur});
            }
        }
    }
    // park unused quads (invisible)
    for (; sq < PCap * 2; sq++) {
        int b = sq * 4;
        for (int k = 0; k < 4; k++) {
            sv[b + k] = bottom;
            sc[b + k] = Color{0, 0, 0, 0};
        }
    }
    for (; hq < PCap; hq++) {
        int b = hq * 4;
        for (int k = 0; k < 4; k++) {
            hv[b + k] = bottom;
            hc[b + k] = Color{0, 0, 0, 0};
        }
    }
    soft->Upload();
    sharp->Upload();
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
    static UnityW<Material> g_softMaterial;

    // soft round sprite (generated radial gradient), additive if the built-in additive particle shader is available
    Material* SoftMaterial() {
        if (g_softMaterial)
            return g_softMaterial.ptr();
        auto shader = Shader::Find("Legacy Shaders/Particles/Additive");
        if (!shader)
            shader = Shader::Find("Sprites/Default");
        if (!shader)
            return nullptr;
        auto mat = Material::New_ctor(shader);
        auto tex = Texture2D::New_ctor(32, 32);
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 32; x++) {
                float dx = (x - 15.5f) / 15.5f, dy = (y - 15.5f) / 15.5f;
                float a = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
                a = a * a * (3.0f - 2.0f * a);
                tex->SetPixel(x, y, Color(1, 1, 1, a));
            }
        }
        tex->Apply();
        tex->set_wrapMode(TextureWrapMode::Clamp);
        mat->set_mainTexture(tex);
        g_softMaterial = mat;
        return mat;
    }

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
        trail->SetBladeMaterial(bladeMat);
        logger.info("Prysm saber model applied (hid {} game renderers, blade length {})", hidden, bladeLen);
    }
}
