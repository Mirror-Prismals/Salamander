#pragma once

namespace MouseInputSystemLogic {
    void UpdateCameraRotationFromMouse(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player) return;
        PlayerContext& player = *baseSystem.player;
        const float sensitivity = 0.1f;
        player.cameraYaw += player.mouseOffsetX * sensitivity;
        player.cameraPitch += player.mouseOffsetY * sensitivity;
        if (player.cameraPitch > 89.0f) player.cameraPitch = 89.0f;
        if (player.cameraPitch < -89.0f) player.cameraPitch = -89.0f;
    }
}
