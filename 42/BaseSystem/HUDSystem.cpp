#pragma once

#include <glm/glm.hpp>

namespace HUDSystemLogic {
    void UpdateHUD(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player || !baseSystem.hud) return;
        PlayerContext& player = *baseSystem.player;
        HUDContext& hud = *baseSystem.hud;
        if (player.buildMode) {
            hud.buildModeActive = true;
            hud.chargeValue = 1.0f;
            hud.chargeReady = true;
            // hud.showCharge managed by BuildSystem timers
        } else {
            hud.buildModeActive = false;
            hud.chargeValue = glm::clamp(player.blockChargeValue, 0.0f, 1.0f);
            hud.chargeReady = player.blockChargeReady;
            hud.showCharge = player.isChargingBlock || hud.chargeValue > 0.0f;
        }
        if (player.isHoldingBlock) {
            hud.showCharge = false;
        }
    }
}
