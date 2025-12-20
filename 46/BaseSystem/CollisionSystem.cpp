#pragma once
#include "../Host.h"

namespace CollisionSystemLogic {

    struct AABB { glm::vec3 min; glm::vec3 max; };

    AABB MakePlayerAABB(const glm::vec3& center, const glm::vec3& halfExtents) {
        return {center - halfExtents, center + halfExtents};
    }

    bool Intersects(const AABB& a, const AABB& b) {
        return (a.min.x < b.max.x && a.max.x > b.min.x) &&
               (a.min.y < b.max.y && a.max.y > b.min.y) &&
               (a.min.z < b.max.z && a.max.z > b.min.z);
    }

    void ResolveAxis(glm::vec3& position, int axis, float halfExtent, float velAxis, const std::vector<AABB>& blocks, const glm::vec3& halfExtents) {
        if (velAxis == 0.0f) return;
        position[axis] += velAxis;
        AABB playerBox = MakePlayerAABB(position, halfExtents);
        for (const auto& block : blocks) {
            if (!Intersects(playerBox, block)) continue;
            const float skin = 0.001f;
            if (velAxis > 0.0f) {
                position[axis] = block.min[axis] - halfExtent - skin;
            } else {
                position[axis] = block.max[axis] + halfExtent + skin;
            }
            playerBox = MakePlayerAABB(position, halfExtents);
        }
    }

    void ResolveCollisions(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt; (void)win;
        if (!baseSystem.player || !baseSystem.level || baseSystem.level->worlds.empty()) return;

        PlayerContext& player = *baseSystem.player;
        // Two blocks tall, narrower width/depth for smoother hugging of walls
        glm::vec3 halfExtents(0.25f, 1.0f, 0.25f);

        // Gather nearby collidable blocks (solid blocks only) across all worlds in the level.
        std::vector<AABB> blockAABBs;
        const float queryRadius = 6.0f; // search neighborhood to avoid faraway worlds interfering
        const glm::vec3 queryCenter = player.cameraPosition;
        for (const auto& world : baseSystem.level->worlds) {
            for (const auto& inst : world.instances) {
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[inst.prototypeID];
                bool isNonColliding = proto.name == "Water" || proto.name == "AudioVisualizer";
                if (!proto.isBlock || isNonColliding) continue; // e.g., water/non-solid visualizers
                // Only include blocks near the player
                if (glm::length(inst.position - queryCenter) > queryRadius) continue;
                blockAABBs.push_back({inst.position - glm::vec3(0.5f), inst.position + glm::vec3(0.5f)});
            }
        }

        glm::vec3 prevPos = player.prevCameraPosition;
        glm::vec3 desiredPos = player.cameraPosition;
        glm::vec3 velocity = desiredPos - prevPos;

        // Early out if no movement
        if (glm::dot(velocity, velocity) < 1e-8f) {
            player.prevCameraPosition = player.cameraPosition;
            return;
        }

        glm::vec3 resolvedPos = prevPos;
        ResolveAxis(resolvedPos, 0, halfExtents.x, velocity.x, blockAABBs, halfExtents);
        ResolveAxis(resolvedPos, 1, halfExtents.y, velocity.y, blockAABBs, halfExtents);
        ResolveAxis(resolvedPos, 2, halfExtents.z, velocity.z, blockAABBs, halfExtents);

        player.cameraPosition = resolvedPos;
        player.prevCameraPosition = resolvedPos;
    }
}
