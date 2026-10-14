#include "hooks.hpp"

#include <cmath>

#include "GlobalNamespace/NoteCutParticlesEffect.hpp"
#include "GlobalNamespace/Saber.hpp"
#include "GlobalNamespace/SaberBurnMarkArea.hpp"
#include "GlobalNamespace/SaberBurnMarkSparkles.hpp"
#include "GlobalNamespace/SaberClashEffect.hpp"
#include "GlobalNamespace/SaberModelController.hpp"
#include "GlobalNamespace/SaberTrail.hpp"
#include "GlobalNamespace/SaberTrailRenderer.hpp"
#include "PrismTrail.hpp"
#include "UnityEngine/Color32.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Time.hpp"
#include "beatsaber-hook/shared/hooking.hpp"
#include "config.hpp"
#include "main.hpp"

using namespace GlobalNamespace;
using namespace UnityEngine;

MAKE_HOOK_MATCH(
    SaberModelController_Init,
    &SaberModelController::Init,
    void,
    SaberModelController* self,
    Transform* parent,
    Saber* saber,
    Color trailTintColor
) {
    // 1.40.8 signature: Init(Transform*, Saber*, Color)
    SaberModelController_Init(self, parent, saber, trailTintColor);
    auto& c = getConfig();
    if (!c.Enabled.GetValue() || !saber)
        return;

    auto t = self->get_transform();
    auto pos = c.PositionOffset.GetValue();
    auto rot = c.RotationOffset.GetValue();
    float thick = c.ThicknessScale.GetValue(), length = c.LengthScale.GetValue();
    auto lp = t->get_localPosition();
    t->set_localPosition(Vector3(lp.x + pos.x / 100.0f, lp.y + pos.y / 100.0f, lp.z + pos.z / 100.0f));
    t->set_localRotation(Quaternion::op_Multiply(t->get_localRotation(), Quaternion::Euler(rot.x, rot.y, rot.z)));
    auto scale = t->get_localScale();
    t->set_localScale(Vector3(scale.x * thick, scale.y * thick, scale.z * length));

    Material* material = nullptr;
    int layer = self->get_gameObject()->get_layer();
    auto trail = self->_saberTrail;
    if (trail) {
        if (auto prefab = trail->_trailRendererPrefab) {
            if (auto renderer = prefab->_meshRenderer)
                material = renderer->get_sharedMaterial();
            layer = prefab->get_gameObject()->get_layer();
        }
    }
    if (!material) {
        // Leave the vanilla trail alone: otherwise the player would end up with no trail at all.
        logger.error("Could not find a trail material; PrismSabers trails disabled for this saber");
        return;
    }

    static bool loggedShader = false;
    if (!loggedShader) {
        loggedShader = true;
        if (auto shader = material->get_shader())
            logger.info("Trail material shader: {}", std::string(shader->get_name()));
    }

    auto prism = self->get_gameObject()->AddComponent<PrismSabers::PrismTrail*>();
    prism->Setup(saber, self, material, layer);

    // Only hide the vanilla trail once ours exists.
    if (trail && c.HideVanillaTrail.GetValue()) {
        trail->set_enabled(false);
        if (auto renderer = trail->_trailRenderer)
            renderer->get_gameObject()->SetActive(false);
    }
}

MAKE_HOOK_MATCH(
    NoteCutParticlesEffect_SpawnParticles,
    &NoteCutParticlesEffect::SpawnParticles,
    void,
    NoteCutParticlesEffect* self,
    Vector3 cutPoint,
    Vector3 cutNormal,
    Vector3 saberDir,
    float saberSpeed,
    Vector3 noteMovementVec,
    Color32 color,
    int sparkleParticlesCount,
    int explosionParticlesCount,
    float lifetimeMultiplier
) {
    auto& c = getConfig();
    if (c.Enabled.GetValue()) {
        sparkleParticlesCount = (int) std::round(sparkleParticlesCount * c.CutSparkles.GetValue());
        explosionParticlesCount = (int) std::round(explosionParticlesCount * c.CutExplosion.GetValue());
        lifetimeMultiplier *= c.CutLifetime.GetValue();
        switch (c.CutColorMode.GetValue()) {
            case 1: color = Color32(0, 255, 255, 255, 255); break;
            case 2: {
                auto rgb = PrismSabers::HSV(Time::get_time() * c.RainbowSpeed.GetValue(), c.RainbowSaturation.GetValue(), 1.0f);
                color = Color32(0, (uint8_t) (rgb.r * 255), (uint8_t) (rgb.g * 255), (uint8_t) (rgb.b * 255), color.a);
                break;
            }
            default: break;
        }
    }
    NoteCutParticlesEffect_SpawnParticles(
        self, cutPoint, cutNormal, saberDir, saberSpeed, noteMovementVec, color, sparkleParticlesCount, explosionParticlesCount, lifetimeMultiplier
    );
}

MAKE_HOOK_MATCH(SaberClashEffect_LateUpdate, &SaberClashEffect::LateUpdate, void, SaberClashEffect* self) {
    auto& c = getConfig();
    if (c.Enabled.GetValue() && !c.ClashEffect.GetValue())
        return;
    SaberClashEffect_LateUpdate(self);
}

MAKE_HOOK_MATCH(SaberBurnMarkArea_LateUpdate, &SaberBurnMarkArea::LateUpdate, void, SaberBurnMarkArea* self) {
    auto& c = getConfig();
    if (c.Enabled.GetValue() && !c.BurnMarks.GetValue())
        return;
    SaberBurnMarkArea_LateUpdate(self);
}

MAKE_HOOK_MATCH(SaberBurnMarkSparkles_LateUpdate, &SaberBurnMarkSparkles::LateUpdate, void, SaberBurnMarkSparkles* self) {
    auto& c = getConfig();
    if (c.Enabled.GetValue() && !c.BurnSparkles.GetValue())
        return;
    SaberBurnMarkSparkles_LateUpdate(self);
}

void PrismSabers::InstallHooks() {
    INSTALL_HOOK(logger, SaberModelController_Init);
    INSTALL_HOOK(logger, NoteCutParticlesEffect_SpawnParticles);
    INSTALL_HOOK(logger, SaberClashEffect_LateUpdate);
    INSTALL_HOOK(logger, SaberBurnMarkArea_LateUpdate);
    INSTALL_HOOK(logger, SaberBurnMarkSparkles_LateUpdate);
}
