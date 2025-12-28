#pragma once
#include "../Host.h"

namespace GravitySystemLogic {

    void ApplyGravity(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)win;
        if (!baseSystem.player) return;
        if (baseSystem.gamemode != "survival") return;
        PlayerContext& player = *baseSystem.player;
        const float gravity = -18.0f; // stronger pull
        player.verticalVelocity += gravity * dt;
        // Cap downward velocity to reduce tunneling
        const float maxFall = -20.0f;
        if (player.verticalVelocity < maxFall) player.verticalVelocity = maxFall;
        player.cameraPosition.y += player.verticalVelocity * dt;
    }
}
