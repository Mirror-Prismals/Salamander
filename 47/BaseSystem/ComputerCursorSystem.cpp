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

        if (!ui.active) {
            ui.uiLeftPressed = false;
            ui.uiLeftReleased = false;
            ui.uiLeftDown = false;
            return;
        }

        double mx = 0.0, my = 0.0;
        glfwGetCursorPos(win, &mx, &my);
        ui.cursorX = mx;
        ui.cursorY = my;

        int windowWidth = baseSystem.app ? static_cast<int>(baseSystem.app->windowWidth) : 0;
        int windowHeight = baseSystem.app ? static_cast<int>(baseSystem.app->windowHeight) : 0;
        if (win) {
            int actualW = 0, actualH = 0;
            glfwGetWindowSize(win, &actualW, &actualH);
            if (actualW > 0) windowWidth = actualW;
            if (actualH > 0) windowHeight = actualH;
        }
        double w = windowWidth > 0 ? static_cast<double>(windowWidth) : 1.0;
        double h = windowHeight > 0 ? static_cast<double>(windowHeight) : 1.0;
        ui.cursorNDCX = (mx / w) * 2.0 - 1.0;
        ui.cursorNDCY = 1.0 - (my / h) * 2.0;

        static bool lastDown = false;
        int state = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT);
        bool down = state == GLFW_PRESS;
        ui.uiLeftPressed = (!lastDown && down);
        ui.uiLeftReleased = (lastDown && !down);
        ui.uiLeftDown = down;
        lastDown = down;
    }
}
