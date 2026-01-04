#pragma once

#include <GLFW/glfw3.h>

namespace RegistryEditorSystemLogic {
    void UpdateRegistry(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt; (void)win;
        if (!baseSystem.ui || !baseSystem.registry || !baseSystem.reloadRequested || !baseSystem.reloadTarget) return;
        UIContext& ui = *baseSystem.ui;

        std::string currentLevel;
        auto regIt = baseSystem.registry->find("level");
        if (regIt != baseSystem.registry->end() && std::holds_alternative<std::string>(regIt->second)) {
            currentLevel = std::get<std::string>(regIt->second);
        }
        bool isMenuLevel = (currentLevel == "menu");
        if (isMenuLevel) ui.active = true;

        if (ui.actionDelayFrames > 0) {
            ui.actionDelayFrames -= 1;
            if (ui.actionDelayFrames == 0 && !ui.pendingActionType.empty()) {
                if (ui.pendingActionType == "SetRegistry" && !ui.pendingActionKey.empty()) {
                    (*baseSystem.registry)[ui.pendingActionKey] = ui.pendingActionValue;
                    if (ui.pendingActionKey == "level") {
                        ui.levelSwitchPending = true;
                        ui.levelSwitchTarget = ui.pendingActionValue;
                    }
                    ui.pendingActionType.clear();
                    ui.pendingActionKey.clear();
                    ui.pendingActionValue.clear();
                }
            }
        }
    }
}
