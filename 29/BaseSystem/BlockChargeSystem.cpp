#pragma once

#include <algorithm>

namespace BlockChargeSystemLogic {

    namespace {
        constexpr float CHARGE_TIME = 1.25f;
        constexpr float POSITION_EPSILON = 0.05f;

        bool RemoveBlockAtPosition(LevelContext& level, const std::vector<Entity>& prototypes, const glm::vec3& position, int worldIndex) {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return false;
            Entity& world = level.worlds[worldIndex];
            for (size_t i = 0; i < world.instances.size(); ++i) {
                const EntityInstance& inst = world.instances[i];
                if (glm::distance(inst.position, position) > POSITION_EPSILON) continue;
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[inst.prototypeID];
                if (!proto.isBlock) continue;
                world.instances[i] = world.instances.back();
                world.instances.pop_back();
                return true;
            }
            return false;
        }
    }

    void UpdateBlockCharge(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player || !baseSystem.level) return;
        PlayerContext& player = *baseSystem.player;
        LevelContext& level = *baseSystem.level;

        bool wantsCharge = player.rightMouseDown;

        if (wantsCharge) {
            if (!player.isChargingBlock) {
                player.blockChargeValue = 0.0f;
            }
            player.isChargingBlock = true;
            player.blockChargeValue += dt / CHARGE_TIME;
            if (player.blockChargeValue >= 1.0f) {
                player.blockChargeValue = 1.0f;
                player.blockChargeReady = true;
            }
        } else {
            player.isChargingBlock = false;
            player.blockChargeReady = false;
            player.blockChargeValue = 0.0f;
        }

        if (player.blockChargeReady && player.leftMousePressed && player.rightMouseDown && player.hasBlockTarget) {
            if (RemoveBlockAtPosition(level, prototypes, player.targetedBlockPosition, player.targetedWorldIndex)) {
                BlockSelectionSystemLogic::MarkBlockInactive(player.targetedWorldIndex, player.targetedBlockCacheIndex);
                player.blockChargeValue = 0.0f;
                player.blockChargeReady = false;
                player.isChargingBlock = false;
            }
        }
    }
}
