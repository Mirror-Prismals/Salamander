#pragma once
#include "Host/PlatformInput.h"
#include <vector>

namespace OreMiningSystemLogic { bool IsMiningActive(const BaseSystem& baseSystem); }
namespace GroundCraftingSystemLogic { bool IsRitualActive(const BaseSystem& baseSystem); }
namespace GemChiselSystemLogic { bool IsChiselActive(const BaseSystem& baseSystem); }

namespace KeyboardInputSystemLogic {
    namespace {
        bool isPickupHandMode(BuildModeType mode) {
            return mode == BuildModeType::Pickup || mode == BuildModeType::PickupLeft;
        }

        void saveActiveHeldBlockForMode(PlayerContext& player, BuildModeType mode) {
            if (mode == BuildModeType::Pickup) {
                player.rightHandHoldingBlock = player.isHoldingBlock;
                player.rightHandHeldPrototypeID = player.heldPrototypeID;
                player.rightHandHeldBlockColor = player.heldBlockColor;
                player.rightHandHeldPackedColor = player.heldPackedColor;
                player.rightHandHeldHasSourceCell = player.heldHasSourceCell;
                player.rightHandHeldSourceCell = player.heldSourceCell;
            } else if (mode == BuildModeType::PickupLeft) {
                player.leftHandHoldingBlock = player.isHoldingBlock;
                player.leftHandHeldPrototypeID = player.heldPrototypeID;
                player.leftHandHeldBlockColor = player.heldBlockColor;
                player.leftHandHeldPackedColor = player.heldPackedColor;
                player.leftHandHeldHasSourceCell = player.heldHasSourceCell;
                player.leftHandHeldSourceCell = player.heldSourceCell;
            }
        }

        void loadActiveHeldBlockForMode(PlayerContext& player, BuildModeType mode) {
            if (mode == BuildModeType::Pickup) {
                player.isHoldingBlock = player.rightHandHoldingBlock;
                player.heldPrototypeID = player.rightHandHeldPrototypeID;
                player.heldBlockColor = player.rightHandHeldBlockColor;
                player.heldPackedColor = player.rightHandHeldPackedColor;
                player.heldHasSourceCell = player.rightHandHeldHasSourceCell;
                player.heldSourceCell = player.rightHandHeldSourceCell;
            } else if (mode == BuildModeType::PickupLeft) {
                player.isHoldingBlock = player.leftHandHoldingBlock;
                player.heldPrototypeID = player.leftHandHeldPrototypeID;
                player.heldBlockColor = player.leftHandHeldBlockColor;
                player.heldPackedColor = player.leftHandHeldPackedColor;
                player.heldHasSourceCell = player.leftHandHeldHasSourceCell;
                player.heldSourceCell = player.leftHandHeldSourceCell;
            }
        }

        bool getRegistryBool(const BaseSystem& baseSystem, const char* key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<bool>(it->second)) return fallback;
            return std::get<bool>(it->second);
        }

    }

    void ProcessKeyboardInput(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.level || baseSystem.level->worlds.empty()) return;
        if (baseSystem.ui && baseSystem.ui->active) return;
        if (OreMiningSystemLogic::IsMiningActive(baseSystem)) return;
        if (GroundCraftingSystemLogic::IsRitualActive(baseSystem)) return;
        if (GemChiselSystemLogic::IsChiselActive(baseSystem)) return;
        LevelContext& level = *baseSystem.level;
        Entity& activeWorld = level.worlds[level.activeWorldIndex];

        // --- UAV (Player Movement) Controls ---
        if (PlatformInput::IsKeyDown(win, PlatformInput::Key::W)) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_W", {}, {}));
        if (PlatformInput::IsKeyDown(win, PlatformInput::Key::A)) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_A", {}, {}));
        if (PlatformInput::IsKeyDown(win, PlatformInput::Key::S)) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_S", {}, {}));
        if (PlatformInput::IsKeyDown(win, PlatformInput::Key::D)) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_D", {}, {}));
        if (PlatformInput::IsKeyDown(win, PlatformInput::Key::Space)) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_SPACE", {}, {}));
        if (PlatformInput::IsKeyDown(win, PlatformInput::Key::LeftShift)) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_LSHIFT", {}, {}));
    
        // --- World Switching ---
        static bool tab_pressed_last_frame = false;
        bool tab_pressed = PlatformInput::IsKeyDown(win, PlatformInput::Key::Tab);
        if (tab_pressed && !tab_pressed_last_frame) {
            level.activeWorldIndex = (level.activeWorldIndex + 1) % level.worlds.size();
        }
        tab_pressed_last_frame = tab_pressed;

        if (baseSystem.player) {
            PlayerContext& player = *baseSystem.player;
            const bool fishingRodPlaced = (baseSystem.fishing && baseSystem.fishing->rodPlacedInWorld);
            const bool fishingUnlocked = (!baseSystem.fishing) || baseSystem.fishing->starterRodSpawned;
            const bool boulderingEnabled = getRegistryBool(baseSystem, "BoulderingEnabled", true);
            std::vector<BuildModeType> cycleModes;
            cycleModes.reserve(4);
            cycleModes.push_back(BuildModeType::Pickup);
            cycleModes.push_back(BuildModeType::PickupLeft);
            if (fishingUnlocked && !fishingRodPlaced) {
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
                loadActiveHeldBlockForMode(player, player.buildMode);
            }
            static bool f_pressed_last_frame = false;
            bool f_pressed = PlatformInput::IsKeyDown(win, PlatformInput::Key::F);
            if (f_pressed && !f_pressed_last_frame) {
                const BuildModeType previousMode = player.buildMode;
                saveActiveHeldBlockForMode(player, previousMode);
                int idx = modeIndex(player.buildMode);
                if (idx < 0) idx = 0;
                idx = (idx + 1) % static_cast<int>(cycleModes.size());
                player.buildMode = cycleModes[static_cast<size_t>(idx)];
                loadActiveHeldBlockForMode(player, player.buildMode);
                player.isChargingBlock = false;
                player.blockChargeValue = 0.0f;
                player.blockChargeReady = false;
                player.blockChargeAction = BlockChargeAction::None;
                player.blockChargeDecayTimer = 0.0f;
                player.blockChargeExecuteGraceTimer = 0.0f;
                if (baseSystem.hud) {
                    baseSystem.hud->showCharge = false;
                    bool buildActive = (isPickupHandMode(player.buildMode)
                                     || player.buildMode == BuildModeType::Fishing
                                     || player.buildMode == BuildModeType::Bouldering);
                    baseSystem.hud->buildModeActive = buildActive;
                    baseSystem.hud->buildModeType = static_cast<int>(player.buildMode);
                    baseSystem.hud->displayTimer = buildActive ? 2.0f : 0.0f;
                }
            }
            f_pressed_last_frame = f_pressed;

            static bool e_pressed_last_frame = false;
            bool e_pressed = PlatformInput::IsKeyDown(win, PlatformInput::Key::E);
            if (e_pressed
                && !e_pressed_last_frame
                && (isPickupHandMode(player.buildMode) || player.buildMode == BuildModeType::Bouldering)) {
                player.blockChargeControlsSwapped = !player.blockChargeControlsSwapped;
                player.isChargingBlock = false;
                player.blockChargeValue = 0.0f;
                player.blockChargeReady = false;
                player.blockChargeAction = BlockChargeAction::None;
                player.blockChargeDecayTimer = 0.0f;
                player.blockChargeExecuteGraceTimer = 0.0f;
                if (baseSystem.hud) {
                    baseSystem.hud->showCharge = false;
                }
            }
            e_pressed_last_frame = e_pressed;

            static bool g_pressed_last_frame = false;
            bool g_pressed = PlatformInput::IsKeyDown(win, PlatformInput::Key::G);
            if (g_pressed && !g_pressed_last_frame && baseSystem.gems && baseSystem.gems->blockModeHoldingGem) {
                baseSystem.gems->heldDrop.upsideDown = !baseSystem.gems->heldDrop.upsideDown;
            }
            g_pressed_last_frame = g_pressed;
        }
    }
}
