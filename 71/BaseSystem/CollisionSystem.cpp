#pragma once
#include "../Host.h"

namespace CollisionSystemLogic {

    struct AABB { glm::vec3 min; glm::vec3 max; };

    namespace {
        int floorDivInt(int value, int divisor) {
            if (divisor <= 0) return 0;
            if (value >= 0) return value / divisor;
            return -(((-value) + divisor - 1) / divisor);
        }

        glm::ivec3 chunkIndexFromPosition(const glm::vec3& position, const glm::ivec3& chunkSize) {
            int x = static_cast<int>(std::floor(position.x));
            int y = static_cast<int>(std::floor(position.y));
            int z = static_cast<int>(std::floor(position.z));
            return glm::ivec3(
                floorDivInt(x, chunkSize.x),
                floorDivInt(y, chunkSize.y),
                floorDivInt(z, chunkSize.z)
            );
        }
    }

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
        if (baseSystem.gamemode == "spectator") return;

        PlayerContext& player = *baseSystem.player;
        // Two blocks tall, narrower width/depth for smoother hugging of walls
        glm::vec3 halfExtents(0.25f, 1.0f, 0.25f);

        // Gather collidable blocks (solid blocks only) across all worlds in the level.
        std::vector<AABB> blockAABBs;
        bool useChunkCollision = baseSystem.chunk && baseSystem.chunk->initialized;
        if (useChunkCollision) {
            const ChunkContext& chunkCtx = *baseSystem.chunk;
            glm::ivec3 playerChunk = chunkIndexFromPosition(player.cameraPosition, chunkCtx.chunkSize);
            // Keep collision broadphase tight around the player to avoid scanning distant terrain.
            int collisionChunkRadius = 2; // configurable if needed
            for (const auto& [chunkKey, chunk] : chunkCtx.chunks) {
                glm::ivec3 delta = chunkKey.chunkIndex - playerChunk;
                if (std::abs(delta.x) > collisionChunkRadius ||
                    std::abs(delta.y) > collisionChunkRadius ||
                    std::abs(delta.z) > collisionChunkRadius) continue;
                for (size_t i = 0; i < chunk.positions.size(); ++i) {
                    if (i >= chunk.prototypeIDs.size()) continue;
                    int protoID = chunk.prototypeIDs[i];
                    if (protoID < 0 || protoID >= static_cast<int>(prototypes.size())) continue;
                    const Entity& proto = prototypes[protoID];
                    bool isNonColliding = proto.name == "Water" || proto.name == "AudioVisualizer";
                    if (!proto.isBlock || isNonColliding || !proto.isSolid) continue;
                    blockAABBs.push_back({chunk.positions[i] - glm::vec3(0.5f), chunk.positions[i] + glm::vec3(0.5f)});
                }
            }
        }
        for (const auto& world : baseSystem.level->worlds) {
            for (const auto& inst : world.instances) {
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[inst.prototypeID];
                if (!proto.isBlock || proto.isChunkable) continue;
                bool isNonColliding = proto.name == "Water" || proto.name == "AudioVisualizer";
                if (isNonColliding || !proto.isSolid) continue;
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

        // Extra sweep for downward motion to catch thin crossings
        bool hitGround = false;
        if (velocity.y < 0.0f) {
            const float skin = 0.001f;
            float highestY = -std::numeric_limits<float>::infinity();
            for (const auto& block : blockAABBs) {
                // Horizontal overlap
                bool overlapX = !(resolvedPos.x + halfExtents.x < block.min.x || resolvedPos.x - halfExtents.x > block.max.x);
                bool overlapZ = !(resolvedPos.z + halfExtents.z < block.min.z || resolvedPos.z - halfExtents.z > block.max.z);
                if (!overlapX || !overlapZ) continue;
                // Crossing top face?
                float bottomBefore = prevPos.y - halfExtents.y;
                float bottomAfter = resolvedPos.y - halfExtents.y;
                if (bottomBefore >= block.max.y - skin && bottomAfter <= block.max.y + skin) {
                    if (block.max.y > highestY) highestY = block.max.y;
                }
            }
            if (highestY != -std::numeric_limits<float>::infinity()) {
                resolvedPos.y = highestY + halfExtents.y + skin;
                hitGround = true;
            }
        }

        player.cameraPosition = resolvedPos;
        // Update grounded state: if we were moving down and got clamped upward relative to desired
        hitGround = hitGround || ((velocity.y < 0.0f) && ((resolvedPos.y - desiredPos.y) > 0.001f));
        player.onGround = hitGround;
        if (player.onGround) player.verticalVelocity = 0.0f;
        player.prevCameraPosition = resolvedPos;
    }
}
