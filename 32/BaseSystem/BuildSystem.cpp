#pragma once

#include <GLFW/glfw3.h>

namespace BlockSelectionSystemLogic {
    bool HasBlockAt(BaseSystem& baseSystem, const std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position);
    void AddBlockToCache(BaseSystem& baseSystem, std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position, int prototypeID);
}
namespace HostLogic { const Entity* findPrototype(const std::string& name, const std::vector<Entity>& prototypes); }
namespace BlockSelectionSystemLogic { void InvalidateWorldCache(int worldIndex); }

namespace BuildSystemLogic {

    bool BlockExistsAt(const Entity& world, const glm::vec3& position) {
        for (const auto& inst : world.instances) {
            if (glm::distance(inst.position, position) < 0.01f) return true;
        }
        return false;
    }

    void UpdateBuildMode(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player || !baseSystem.level || !baseSystem.hud) return;
        PlayerContext& player = *baseSystem.player;
        HUDContext& hud = *baseSystem.hud;
        if (!player.buildMode) {
            hud.buildModeActive = false;
            hud.showCharge = false;
            return;
        }

        bool hudRefreshed = false;

        if (player.rightMousePressed) {
            player.buildChannel = (player.buildChannel + 1) % 3;
            hudRefreshed = true;
        }

        double scrollDelta = player.scrollYOffset;
        player.scrollYOffset = 0.0;
        if (scrollDelta != 0.0) {
            float delta = static_cast<float>(scrollDelta) * 0.05f;
            player.buildColor[player.buildChannel] = glm::clamp(player.buildColor[player.buildChannel] + delta, 0.0f, 1.0f);
            hudRefreshed = true;
        }

        if (player.leftMousePressed && player.hasBlockTarget && glm::length(player.targetedBlockNormal) > 0.001f) {
            if (player.targetedWorldIndex >= 0 && player.targetedWorldIndex < static_cast<int>(baseSystem.level->worlds.size())) {
                Entity& world = baseSystem.level->worlds[player.targetedWorldIndex];
                const Entity* blockProto = HostLogic::findPrototype("Block", prototypes);
                if (blockProto) {
                    glm::vec3 placePos = player.targetedBlockPosition + player.targetedBlockNormal;
                    if (!BlockSelectionSystemLogic::HasBlockAt(baseSystem, prototypes, player.targetedWorldIndex, placePos)) {
                        world.instances.push_back(HostLogic::CreateInstance(baseSystem, blockProto->prototypeID, placePos, player.buildColor));
                        BlockSelectionSystemLogic::AddBlockToCache(baseSystem, prototypes, player.targetedWorldIndex, placePos, blockProto->prototypeID);
                    }
                }
            }
            hudRefreshed = true;
        }

        if (hudRefreshed) {
            hud.displayTimer = 2.0f;
        } else if (hud.displayTimer > 0.0f) {
            hud.displayTimer = std::max(0.0f, hud.displayTimer - dt);
        }

        hud.buildModeActive = true;
        hud.buildPreviewColor = player.buildColor;
        hud.buildChannel = player.buildChannel;
        hud.chargeValue = 1.0f;
        hud.chargeReady = true;
        hud.showCharge = hud.displayTimer > 0.0f;
    }
}
