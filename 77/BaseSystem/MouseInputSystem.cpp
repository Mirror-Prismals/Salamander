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
            int middleState = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE);

            bool newRightDown = rightState == GLFW_PRESS;
            bool newLeftDown = leftState == GLFW_PRESS;
            bool newMiddleDown = middleState == GLFW_PRESS;

            player.rightMousePressed = (!player.rightMouseDown && newRightDown);
            player.leftMousePressed = (!player.leftMouseDown && newLeftDown);
            player.middleMousePressed = (!player.middleMouseDown && newMiddleDown);
            player.rightMouseReleased = (player.rightMouseDown && !newRightDown);
            player.leftMouseReleased = (player.leftMouseDown && !newLeftDown);
            player.middleMouseReleased = (player.middleMouseDown && !newMiddleDown);

            player.rightMouseDown = newRightDown;
            player.leftMouseDown = newLeftDown;
            player.middleMouseDown = newMiddleDown;
        } else {
            player.rightMousePressed = player.leftMousePressed = player.middleMousePressed = false;
            player.rightMouseReleased = player.leftMouseReleased = player.middleMouseReleased = false;
            player.rightMouseDown = player.leftMouseDown = player.middleMouseDown = false;
        }
    }
}
