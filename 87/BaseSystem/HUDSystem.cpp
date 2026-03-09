#pragma once

#include <glm/glm.hpp>

namespace HUDSystemLogic {
    void UpdateHUD(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player || !baseSystem.hud) return;
        PlayerContext& player = *baseSystem.player;
        HUDContext& hud = *baseSystem.hud;
        bool buildActive = (player.buildMode == BuildModeType::Color || player.buildMode == BuildModeType::Texture);
        bool fishingMode = player.buildMode == BuildModeType::Fishing;
        bool boulderingMode = player.buildMode == BuildModeType::Bouldering;
        if (buildActive) {
            hud.buildModeActive = true;
            hud.buildModeType = static_cast<int>(player.buildMode);
            hud.chargeValue = 1.0f;
            hud.chargeReady = true;
            // hud.showCharge managed by BuildSystem timers
        } else if (fishingMode || boulderingMode) {
            hud.buildModeActive = true;
            hud.buildModeType = static_cast<int>(player.buildMode);
            hud.chargeValue = glm::clamp(player.blockChargeValue, 0.0f, 1.0f);
            hud.chargeReady = player.blockChargeReady;
            hud.showCharge = player.isChargingBlock || hud.chargeValue > 0.0f;
        } else {
            hud.buildModeActive = false;
            hud.buildModeType = static_cast<int>(player.buildMode);
            hud.chargeValue = glm::clamp(player.blockChargeValue, 0.0f, 1.0f);
            hud.chargeReady = player.blockChargeReady;
            hud.showCharge = player.isChargingBlock || hud.chargeValue > 0.0f;
        }
        if (player.isHoldingBlock || (baseSystem.gems && baseSystem.gems->blockModeHoldingGem)) {
            hud.showCharge = false;
        }
    }
}
