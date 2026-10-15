#pragma once

#include <array>

#include "GlobalNamespace/Saber.hpp"
#include "GlobalNamespace/SaberModelController.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Mesh.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(PrysmSabers, PrismTrailLayer, UnityEngine::MonoBehaviour) {
    DECLARE_INSTANCE_FIELD(UnityW<UnityEngine::Mesh>, mesh);
    DECLARE_INSTANCE_FIELD(UnityW<UnityEngine::MeshRenderer>, meshRenderer);
    DECLARE_INSTANCE_FIELD(ArrayW<UnityEngine::Vector3>, vertices);
    DECLARE_INSTANCE_FIELD(ArrayW<UnityEngine::Color>, colors);
    DECLARE_INSTANCE_FIELD(ArrayW<UnityEngine::Vector2>, uvs);
    DECLARE_INSTANCE_FIELD(ArrayW<int>, triangles);
    DECLARE_INSTANCE_FIELD(int, granularity);
    DECLARE_INSTANCE_FIELD(int, crossCount);
    DECLARE_INSTANCE_FIELD(bool, flipV);
    DECLARE_INSTANCE_FIELD(bool, flatU);
    DECLARE_INSTANCE_FIELD(bool, useSolid);
    DECLARE_INSTANCE_FIELD(UnityW<UnityEngine::Material>, gameMat);

   public:
    void Init(UnityEngine::Material* material, int sortingOrder);
    void Resize(int granularity, int cross, bool flipV, bool flatU);
    void SetSolid(bool solid, UnityEngine::Material* solidMaterial);
    void Upload();
    void SetVisible(bool visible);
};

DECLARE_CLASS_CODEGEN(PrysmSabers, PrismTrail, UnityEngine::MonoBehaviour) {
    DECLARE_INSTANCE_FIELD(UnityW<GlobalNamespace::Saber>, saber);
    DECLARE_INSTANCE_FIELD(UnityW<GlobalNamespace::SaberModelController>, model);
    DECLARE_INSTANCE_FIELD(UnityW<PrismTrailLayer>, mainLayer);
    DECLARE_INSTANCE_FIELD(UnityW<PrismTrailLayer>, glowLayer);
    DECLARE_INSTANCE_FIELD(UnityW<PrismTrailLayer>, blurLayer);
    DECLARE_INSTANCE_FIELD(UnityW<PrismTrailLayer>, curveLayer);
    DECLARE_INSTANCE_FIELD(UnityW<PrismTrailLayer>, ribbonLayer);
    DECLARE_INSTANCE_FIELD(UnityW<PrismTrailLayer>, bladeLayer);
    DECLARE_INSTANCE_FIELD(UnityW<PrismTrailLayer>, burnLayer);
    DECLARE_INSTANCE_FIELD(UnityW<UnityEngine::Material>, bladeMat);

    DECLARE_INSTANCE_METHOD(void, LateUpdate);
    DECLARE_INSTANCE_METHOD(void, OnEnable);
    DECLARE_INSTANCE_METHOD(void, OnDisable);
    DECLARE_INSTANCE_METHOD(void, OnDestroy);

   public:
    static constexpr int HistorySize = 256;
    struct Sample {
        UnityEngine::Vector3 bottom;
        UnityEngine::Vector3 top;
        float time;
    };

    void Setup(GlobalNamespace::Saber* saber, GlobalNamespace::SaberModelController* model, UnityEngine::Material* material, int layer);
    // Menu preview: no game saber, a scripted swing instead
    void SetupPreview(UnityEngine::Material* material, UnityEngine::Material* solid, int layer, bool left, UnityEngine::Vector3 origin, UnityEngine::Vector3 right, UnityEngine::Vector3 forward);
    void SetBladeMaterial(UnityEngine::Material* material) { bladeMat = material; }
    bool IsLeft() const { return isLeft; }

   private:
    std::array<Sample, HistorySize> history;
    int head;
    int count;
    int frames;
    float smoothedSpeed;
    float lastTime;
    UnityEngine::Vector3 lastTop;
    UnityEngine::Color gameColor;
    bool isLeft;
    UnityEngine::Color lastColor;
    float lastIntensity;
    int lastMode;
    bool modified;
    bool blurOn;
    bool preview;
    static constexpr int BurnCap = 64;
    UnityEngine::Vector3 burnPos[64];
    float burnTime[64];
    bool burnValid[64];
    int burnHead;
    int burnCount;
    void PushBurn(UnityEngine::Vector3 pos, float time, bool valid);
    void UpdateBurn(float now, UnityEngine::Vector3 bottom, UnityEngine::Vector3 top);
    UnityEngine::Vector3 pvOrigin;
    UnityEngine::Vector3 pvRight;
    UnityEngine::Vector3 pvForward;

    void InitCommon(UnityEngine::Material* material, int layer, UnityEngine::Material* solid = nullptr);
    void PreviewPose(float now, UnityEngine::Vector3& bottom, UnityEngine::Vector3& top);
    void DrawPreviewBlade(float now, UnityEngine::Vector3 bottom, UnityEngine::Vector3 top);

    void Push(Sample const& sample);
    Sample Evaluate(float time) const;
    void BuildLayer(PrismTrailLayer* layer, int which, float now, float speedFactor, float blurFactor);
    UnityEngine::Color BaseColor(float now) const;
    void ApplyBladeColors(float now);
};

namespace PrysmSabers {
    UnityEngine::Color HSV(float h, float s, float v);

    // Menu preview helpers
    void CacheTrailMaterial(UnityEngine::Material* material, int layer);
    // Replaces the game saber visuals with the Prysm saber (black handle, thin simple blade)
    void ApplyCustomModel(GlobalNamespace::SaberModelController* model, GlobalNamespace::Saber* saber, PrismTrail* trail);
    UnityEngine::Material* SolidMaterial();
    bool PreviewAvailable();
    void ShowPreview(UnityEngine::Transform* anchor);
    void HidePreview();
    bool PreviewVisible();
}
