#pragma once

namespace KeyboardInputSystemLogic {
    void ProcessKeyboardInput(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.level || baseSystem.level->worlds.empty()) return;
        if (baseSystem.ui && baseSystem.ui->active) return;
        LevelContext& level = *baseSystem.level;
        Entity& activeWorld = level.worlds[level.activeWorldIndex];

        // --- UAV (Player Movement) Controls ---
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_W", {}, {}));
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_A", {}, {}));
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_S", {}, {}));
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_D", {}, {}));
        if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_SPACE", {}, {}));
        if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_LSHIFT", {}, {}));
    
        // --- World Switching ---
        static bool tab_pressed_last_frame = false;
        bool tab_pressed = glfwGetKey(win, GLFW_KEY_TAB) == GLFW_PRESS;
        if (tab_pressed && !tab_pressed_last_frame) {
            level.activeWorldIndex = (level.activeWorldIndex + 1) % level.worlds.size();
        }
        tab_pressed_last_frame = tab_pressed;

        if (baseSystem.player) {
            PlayerContext& player = *baseSystem.player;
            static bool f_pressed_last_frame = false;
            bool f_pressed = glfwGetKey(win, GLFW_KEY_F) == GLFW_PRESS;
            if (f_pressed && !f_pressed_last_frame) {
                int nextMode = (static_cast<int>(player.buildMode) + 1) % 4;
                player.buildMode = static_cast<BuildModeType>(nextMode);
                player.isChargingBlock = false;
                player.blockChargeValue = 0.0f;
                player.blockChargeReady = false;
                player.isHoldingBlock = false;
                player.heldPrototypeID = -1;
                if (baseSystem.hud) {
                    baseSystem.hud->showCharge = false;
                    bool buildActive = (player.buildMode == BuildModeType::Color || player.buildMode == BuildModeType::Texture);
                    baseSystem.hud->buildModeActive = buildActive;
                    baseSystem.hud->buildModeType = static_cast<int>(player.buildMode);
                    baseSystem.hud->displayTimer = buildActive ? 2.0f : 0.0f;
                }
            }
            f_pressed_last_frame = f_pressed;
        }
    }
}
