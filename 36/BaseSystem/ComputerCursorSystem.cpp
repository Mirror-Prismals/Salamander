#pragma once

#include <GLFW/glfw3.h>

namespace ComputerCursorSystemLogic {
    void UpdateComputerCursor(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!win || !baseSystem.ui) return;
        UIContext& ui = *baseSystem.ui;

        if (ui.active && !ui.cursorReleased) {
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            ui.cursorReleased = true;
            if (baseSystem.player) baseSystem.player->firstMouse = true;
        }
        if (!ui.active && ui.cursorReleased) {
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            ui.cursorReleased = false;
            if (baseSystem.player) baseSystem.player->firstMouse = true;
        }
    }
}
