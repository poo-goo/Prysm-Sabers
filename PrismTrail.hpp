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

DECLARE_CLASS_CODEGEN(PrismSabers, PrismTrailLayer, UnityEngine::MonoBehaviour) {
    DECLARE_INSTANCE_FIELD(UnityW<UnityEngine::Mesh>, mesh);
    DECLARE_INSTANCE_FIELD(UnityW<UnityEngine::MeshRenderer>, meshRenderer);
    DECLARE_INSTANCE_FIELD(ArrayW<UnityEngine::Vector3>, vertices);
    DECLARE_INSTANCE_FIELD(ArrayW<UnityEngine::Color>, colors);
    DECLARE_INSTANCE_FIELD(ArrayW<UnityEngine::Vector2>, uvs);
    DECLARE_INSTANCE_FIELD(ArrayW<int>, triangles);
    DECLARE_INSTANCE_FIELD(int, granularity);
    DECLARE_INSTANCE_FIELD(int, crossCount);

   public:
    void Init(UnityEngine::Material* material, int sortingOrder);
    void Resize(int granularity, int cross);
    void Upload();
    void SetVisible(bool visible);
};

DECLARE_CLASS_CODEGEN(PrismSabers, PrismTrail, UnityEngine::MonoBehaviour) {
    DECLARE_INSTANCE_FIELD(UnityW<GlobalNamespace::Saber>, saber);
    DECLARE_INSTANCE_FIELD(UnityW<GlobalNamespace::SaberModelController>, model);
    DECLARE_INSTANCE_FIELD(UnityW<PrismTrailLayer>, mainLayer);
    DECLARE_INSTANCE_FIELD(UnityW<PrismTrailLayer>, glowLayer);
    DECLARE_INSTANCE_FIELD(UnityW<PrismTrailLayer>, blurLayer);

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

    void Push(Sample const& sample);
    Sample Evaluate(float time) const;
    void BuildLayer(PrismTrailLayer* layer, int which, float now, float speedFactor);
    UnityEngine::Color BaseColor(float now) const;
    void ApplyBladeColors(float now);
};

namespace PrismSabers {
    UnityEngine::Color HSV(float h, float s, float v);
}
