#pragma once

#include <algorithm>

namespace HostLogic { EntityInstance CreateInstance(BaseSystem& baseSystem, int prototypeID, glm::vec3 position, glm::vec3 color); }
namespace BlockSelectionSystemLogic {
    void RemoveBlockFromCache(BaseSystem& baseSystem, const std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position);
    bool HasBlockAt(BaseSystem& baseSystem, const std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position);
    void AddBlockToCache(BaseSystem& baseSystem, std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position, int prototypeID);
}
namespace ChunkSystemLogic { void MarkChunkDirty(BaseSystem& baseSystem, const glm::vec3& position); }
namespace StructureCaptureSystemLogic { void NotifyBlockChanged(BaseSystem& baseSystem, int worldIndex, const glm::vec3& position); }

namespace BlockChargeSystemLogic {

    namespace {
        constexpr float CHARGE_TIME = 1.25f;
        constexpr float POSITION_EPSILON = 0.05f;

        struct RemovedBlockInfo {
            int prototypeID = -1;
            glm::vec3 color = glm::vec3(1.0f);
        };

        bool RemoveBlockAtPosition(LevelContext& level, const std::vector<Entity>& prototypes, const glm::vec3& position, int worldIndex, RemovedBlockInfo* removedInfo) {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return false;
            Entity& world = level.worlds[worldIndex];
            for (size_t i = 0; i < world.instances.size(); ++i) {
                const EntityInstance& inst = world.instances[i];
                if (glm::distance(inst.position, position) > POSITION_EPSILON) continue;
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[inst.prototypeID];
                if (!proto.isBlock || !proto.isMutable) continue;
                if (removedInfo) {
                    removedInfo->prototypeID = inst.prototypeID;
                    removedInfo->color = inst.color;
                }
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

        auto tryPlaceHeldBlock = [&](PlayerContext& playerCtx) {
            if (!playerCtx.leftMousePressed) return;
            if (!playerCtx.hasBlockTarget || glm::length(playerCtx.targetedBlockNormal) < 0.1f) return;
            if (playerCtx.targetedWorldIndex < 0 || playerCtx.targetedWorldIndex >= static_cast<int>(level.worlds.size())) return;
            if (playerCtx.heldPrototypeID < 0 || playerCtx.heldPrototypeID >= static_cast<int>(prototypes.size())) return;
            glm::vec3 placePos = playerCtx.targetedBlockPosition + playerCtx.targetedBlockNormal;
            if (BlockSelectionSystemLogic::HasBlockAt(baseSystem, prototypes, playerCtx.targetedWorldIndex, placePos)) return;
            Entity& world = level.worlds[playerCtx.targetedWorldIndex];
            world.instances.push_back(HostLogic::CreateInstance(baseSystem, playerCtx.heldPrototypeID, placePos, playerCtx.heldBlockColor));
            BlockSelectionSystemLogic::AddBlockToCache(baseSystem, prototypes, playerCtx.targetedWorldIndex, placePos, playerCtx.heldPrototypeID);
            StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, playerCtx.targetedWorldIndex, placePos);
            ChunkSystemLogic::MarkChunkDirty(baseSystem, placePos);
            playerCtx.isHoldingBlock = false;
            playerCtx.heldPrototypeID = -1;
        };

        if (player.isHoldingBlock) {
            tryPlaceHeldBlock(player);
            player.isChargingBlock = false;
            player.blockChargeReady = false;
            player.blockChargeValue = 0.0f;
            return;
        }

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

        if (player.leftMousePressed && player.rightMouseDown) {
            if (player.blockChargeReady && player.hasBlockTarget) {
                RemovedBlockInfo removedBlock;
                if (RemoveBlockAtPosition(level, prototypes, player.targetedBlockPosition, player.targetedWorldIndex, &removedBlock)) {
                    BlockSelectionSystemLogic::RemoveBlockFromCache(baseSystem, prototypes, player.targetedWorldIndex, player.targetedBlockPosition);
                    StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, player.targetedWorldIndex, player.targetedBlockPosition);
                    ChunkSystemLogic::MarkChunkDirty(baseSystem, player.targetedBlockPosition);
                    player.isHoldingBlock = true;
                    player.heldPrototypeID = removedBlock.prototypeID;
                    player.heldBlockColor = removedBlock.color;
                }
            }
            player.blockChargeValue = 0.0f;
            player.blockChargeReady = false;
            player.isChargingBlock = false;
        }

        if (!player.isHoldingBlock) {
            tryPlaceHeldBlock(player);
        }
    }
}
