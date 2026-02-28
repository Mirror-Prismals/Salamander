#pragma once
#include <vector>

namespace KeyboardInputSystemLogic {
    namespace {
        bool getRegistryBool(const BaseSystem& baseSystem, const char* key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<bool>(it->second)) return fallback;
            return std::get<bool>(it->second);
        }
    }

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
            const bool fishingRodPlaced = (baseSystem.fishing && baseSystem.fishing->rodPlacedInWorld);
            const bool boulderingEnabled = getRegistryBool(baseSystem, "BoulderingEnabled", true);
            std::vector<BuildModeType> cycleModes;
            cycleModes.reserve(3);
            cycleModes.push_back(BuildModeType::Pickup);
            if (!fishingRodPlaced) {
                cycleModes.push_back(BuildModeType::Fishing);
            }
            if (boulderingEnabled) {
                cycleModes.push_back(BuildModeType::Bouldering);
            }
            auto modeIndex = [&](BuildModeType mode) -> int {
                for (int i = 0; i < static_cast<int>(cycleModes.size()); ++i) {
                    if (cycleModes[static_cast<size_t>(i)] == mode) return i;
                }
                return -1;
            };
            if (modeIndex(player.buildMode) < 0) {
                player.buildMode = BuildModeType::Pickup;
            }
            static bool f_pressed_last_frame = false;
            bool f_pressed = glfwGetKey(win, GLFW_KEY_F) == GLFW_PRESS;
            if (f_pressed && !f_pressed_last_frame) {
                int idx = modeIndex(player.buildMode);
                if (idx < 0) idx = 0;
                idx = (idx + 1) % static_cast<int>(cycleModes.size());
                player.buildMode = cycleModes[static_cast<size_t>(idx)];
                player.isChargingBlock = false;
                player.blockChargeValue = 0.0f;
                player.blockChargeReady = false;
                player.blockChargeAction = BlockChargeAction::None;
                if (baseSystem.hud) {
                    baseSystem.hud->showCharge = false;
                    bool buildActive = (player.buildMode == BuildModeType::Pickup
                                     || player.buildMode == BuildModeType::Fishing
                                     || player.buildMode == BuildModeType::Bouldering);
                    baseSystem.hud->buildModeActive = buildActive;
                    baseSystem.hud->buildModeType = static_cast<int>(player.buildMode);
                    baseSystem.hud->displayTimer = buildActive ? 2.0f : 0.0f;
                }
            }
            f_pressed_last_frame = f_pressed;

            static bool e_pressed_last_frame = false;
            bool e_pressed = glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS;
            if (e_pressed && !e_pressed_last_frame && player.buildMode == BuildModeType::Pickup) {
                player.blockChargeControlsSwapped = !player.blockChargeControlsSwapped;
                player.isChargingBlock = false;
                player.blockChargeValue = 0.0f;
                player.blockChargeReady = false;
                player.blockChargeAction = BlockChargeAction::None;
                if (baseSystem.hud) {
                    baseSystem.hud->showCharge = false;
                }
            }
            e_pressed_last_frame = e_pressed;
        }
    }
}
