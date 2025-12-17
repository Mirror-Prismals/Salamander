#pragma once

#include <GLFW/glfw3.h>

namespace MouseInputSystemLogic {
    void UpdateCameraRotationFromMouse(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player) return;
        PlayerContext& player = *baseSystem.player;
        bool uiActive = baseSystem.ui && baseSystem.ui->active;
        if (!uiActive) {
            const float sensitivity = 0.1f;
            player.cameraYaw += player.mouseOffsetX * sensitivity;
            player.cameraPitch += player.mouseOffsetY * sensitivity;
            if (player.cameraPitch > 89.0f) player.cameraPitch = 89.0f;
            if (player.cameraPitch < -89.0f) player.cameraPitch = -89.0f;
        }

        if (win) {
            int rightState = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT);
            int leftState = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT);

            bool newRightDown = rightState == GLFW_PRESS;
            bool newLeftDown = leftState == GLFW_PRESS;

            player.rightMousePressed = (!player.rightMouseDown && newRightDown);
            player.leftMousePressed = (!player.leftMouseDown && newLeftDown);
            player.rightMouseReleased = (player.rightMouseDown && !newRightDown);
            player.leftMouseReleased = (player.leftMouseDown && !newLeftDown);

            player.rightMouseDown = newRightDown;
            player.leftMouseDown = newLeftDown;
        } else {
            player.rightMousePressed = player.leftMousePressed = false;
            player.rightMouseReleased = player.leftMouseReleased = false;
            player.rightMouseDown = player.leftMouseDown = false;
        }
    }
}
