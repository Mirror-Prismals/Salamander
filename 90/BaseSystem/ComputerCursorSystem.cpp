#pragma once

#include "Host/PlatformInput.h"

namespace ComputerCursorSystemLogic {
    void UpdateComputerCursor(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!win || !baseSystem.ui) return;
        UIContext& ui = *baseSystem.ui;

        if (ui.active && !ui.cursorReleased) {
            PlatformInput::SetCursorMode(win, PlatformInput::CursorMode::Normal);
            ui.cursorReleased = true;
            if (baseSystem.player) baseSystem.player->firstMouse = true;
        }
        if (!ui.active && ui.cursorReleased) {
            PlatformInput::SetCursorMode(win, PlatformInput::CursorMode::Disabled);
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
        PlatformInput::GetCursorPosition(win, mx, my);
        ui.cursorX = mx;
        ui.cursorY = my;

        int windowWidth = baseSystem.app ? static_cast<int>(baseSystem.app->windowWidth) : 0;
        int windowHeight = baseSystem.app ? static_cast<int>(baseSystem.app->windowHeight) : 0;
        if (win) {
            int actualW = 0, actualH = 0;
            PlatformInput::GetWindowSize(win, actualW, actualH);
            if (actualW > 0) windowWidth = actualW;
            if (actualH > 0) windowHeight = actualH;
        }
        double w = windowWidth > 0 ? static_cast<double>(windowWidth) : 1.0;
        double h = windowHeight > 0 ? static_cast<double>(windowHeight) : 1.0;
        ui.cursorNDCX = (mx / w) * 2.0 - 1.0;
        ui.cursorNDCY = 1.0 - (my / h) * 2.0;

        static bool lastDown = false;
        bool down = PlatformInput::IsMouseButtonDown(win, PlatformInput::MouseButton::Left);
        ui.uiLeftPressed = (!lastDown && down);
        ui.uiLeftReleased = (lastDown && !down);
        ui.uiLeftDown = down;
        lastDown = down;
    }
}
