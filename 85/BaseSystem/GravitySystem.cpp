#pragma once
#include "../Host.h"

namespace GravitySystemLogic {

    namespace {
        int floorDivInt(int value, int divisor) {
            if (divisor <= 0) return 0;
            if (value >= 0) return value / divisor;
            return -(((-value) + divisor - 1) / divisor);
        }
    }

    void ApplyGravity(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)win;
        if (!baseSystem.player) return;
        if (baseSystem.gamemode != "survival") return;
        bool spawnReady = false;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("spawn_ready");
            if (it != baseSystem.registry->end() &&
                std::holds_alternative<bool>(it->second)) {
                spawnReady = std::get<bool>(it->second);
            }
        }
        if (!spawnReady) return;
        (void)floorDivInt;
        PlayerContext& player = *baseSystem.player;
        float stepDt = dt;
        if (stepDt < 0.0f) stepDt = 0.0f;
        if (stepDt > 0.05f) stepDt = 0.05f;
        (void)floorDivInt;
        const float gravity = -18.0f; // stronger pull
        player.verticalVelocity += gravity * stepDt;
        // Cap downward velocity to reduce tunneling
        const float maxFall = -20.0f;
        if (player.verticalVelocity < maxFall) player.verticalVelocity = maxFall;
        player.cameraPosition.y += player.verticalVelocity * stepDt;
    }
}
