#pragma once
#include "../Host.h"
#include <unordered_map>
#include <unordered_set>

namespace CollisionSystemLogic {

    struct AABB { glm::vec3 min; glm::vec3 max; };

    namespace {
        float getRegistryFloat(const BaseSystem& baseSystem, const std::string& key, float fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stof(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            return fallback;
        }

        bool triggerGameplaySfx(BaseSystem& baseSystem, const char* fileName, float cooldownSeconds = 0.0f) {
            if (!fileName || !baseSystem.audio || !baseSystem.audio->chuck) return false;
            const std::string scriptPath = std::string("Procedures/chuck/gameplay/") + fileName;
            static std::unordered_map<std::string, double> s_lastTrigger;
            const double now = glfwGetTime();
            auto it = s_lastTrigger.find(scriptPath);
            if (it != s_lastTrigger.end() && (now - it->second) < static_cast<double>(cooldownSeconds)) {
                return false;
            }
            std::vector<t_CKUINT> ids;
            bool ok = baseSystem.audio->chuck->compileFile(scriptPath, "", 1, FALSE, &ids);
            if (ok && !ids.empty()) {
                s_lastTrigger[scriptPath] = now;
                return true;
            }
            return false;
        }

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

        enum class SlopeDir : int { None = 0, PosX = 1, NegX = 2, PosZ = 3, NegZ = 4 };

        SlopeDir slopeDirFromName(const std::string& name) {
            if (name == "DebugSlopeTexPosX") return SlopeDir::PosX;
            if (name == "DebugSlopeTexNegX") return SlopeDir::NegX;
            if (name == "DebugSlopeTexPosZ") return SlopeDir::PosZ;
            if (name == "DebugSlopeTexNegZ") return SlopeDir::NegZ;
            return SlopeDir::None;
        }

        glm::ivec3 slopeHighDir(SlopeDir dir) {
            switch (dir) {
                case SlopeDir::PosX: return glm::ivec3(1, 0, 0);
                case SlopeDir::NegX: return glm::ivec3(-1, 0, 0);
                case SlopeDir::PosZ: return glm::ivec3(0, 0, 1);
                case SlopeDir::NegZ: return glm::ivec3(0, 0, -1);
                default: return glm::ivec3(0);
            }
        }

        struct IVec3Hash {
            std::size_t operator()(const glm::ivec3& v) const noexcept {
                std::size_t hx = std::hash<int>()(v.x);
                std::size_t hy = std::hash<int>()(v.y);
                std::size_t hz = std::hash<int>()(v.z);
                return hx ^ (hy << 1) ^ (hz << 2);
            }
        };

        bool IntersectsAnyBlock(const glm::vec3& position,
                                const glm::vec3& halfExtents,
                                const std::vector<AABB>& blocks) {
            AABB playerBox = {position - halfExtents, position + halfExtents};
            for (const auto& block : blocks) {
                bool overlap = (playerBox.min.x < block.max.x && playerBox.max.x > block.min.x)
                            && (playerBox.min.y < block.max.y && playerBox.max.y > block.min.y)
                            && (playerBox.min.z < block.max.z && playerBox.max.z > block.min.z);
                if (overlap) return true;
            }
            return false;
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
        if (baseSystem.gamemode == "survival") {
            bool spawnReady = false;
            if (baseSystem.registry) {
                auto it = baseSystem.registry->find("spawn_ready");
                if (it != baseSystem.registry->end() &&
                    std::holds_alternative<bool>(it->second)) {
                    spawnReady = std::get<bool>(it->second);
                }
            }
            if (!spawnReady) {
                baseSystem.player->prevCameraPosition = baseSystem.player->cameraPosition;
                return;
            }
        }

        PlayerContext& player = *baseSystem.player;
        glm::vec3 prevPos = player.prevCameraPosition;
        glm::vec3 desiredPos = player.cameraPosition;
        glm::vec3 velocity = desiredPos - prevPos;
        const bool wasGrounded = player.onGround;
        const float preResolveVerticalVelocity = player.verticalVelocity;

        // Early out if no movement
        if (glm::dot(velocity, velocity) < 1e-8f) {
            player.prevCameraPosition = player.cameraPosition;
            return;
        }

        // Two blocks tall, narrower width/depth for smoother hugging of walls
        glm::vec3 halfExtents(0.25f, 1.0f, 0.25f);

        // Gather collidable blocks (solid blocks only) across all worlds in the level.
        std::vector<AABB> blockAABBs;
        struct SlopeCollider {
            glm::vec3 center = glm::vec3(0.0f);
            SlopeDir dir = SlopeDir::None;
        };
        std::vector<SlopeCollider> slopeColliders;
        bool useVoxel = baseSystem.voxelWorld && baseSystem.voxelWorld->enabled;
        if (useVoxel) {
            glm::vec3 sweepCenter = (prevPos + desiredPos) * 0.5f;
            glm::ivec3 center = glm::ivec3(glm::floor(sweepCenter));
            int radiusXZ = 2 + static_cast<int>(std::ceil(std::max(std::abs(velocity.x), std::abs(velocity.z))));
            int radiusY = 2 + static_cast<int>(std::ceil(std::abs(velocity.y)));
            radiusXZ = std::max(2, std::min(radiusXZ, 32));
            radiusY = std::max(2, std::min(radiusY, 256));
            for (int x = center.x - radiusXZ; x <= center.x + radiusXZ; ++x) {
                for (int y = center.y - radiusY; y <= center.y + radiusY; ++y) {
                    for (int z = center.z - radiusXZ; z <= center.z + radiusXZ; ++z) {
                        glm::ivec3 cell(x, y, z);
                        uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
                        if (id == 0) continue;
                        int protoID = static_cast<int>(id);
                        if (protoID < 0 || protoID >= static_cast<int>(prototypes.size())) continue;
                        const Entity& proto = prototypes[protoID];
                        SlopeDir slopeDir = slopeDirFromName(proto.name);
                        if (slopeDir != SlopeDir::None) {
                            slopeColliders.push_back({glm::vec3(cell), slopeDir});
                            continue;
                        }
                        bool isNonColliding = proto.name == "Water" || proto.name == "AudioVisualizer";
                        if (!proto.isBlock || isNonColliding || !proto.isSolid) continue;
                        glm::vec3 pos = glm::vec3(cell);
                        blockAABBs.push_back({pos - glm::vec3(0.5f), pos + glm::vec3(0.5f)});
                    }
                }
            }
        } else {
            for (const auto& world : baseSystem.level->worlds) {
                for (const auto& inst : world.instances) {
                    if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                    const Entity& proto = prototypes[inst.prototypeID];
                    SlopeDir slopeDir = slopeDirFromName(proto.name);
                    if (slopeDir != SlopeDir::None) {
                        slopeColliders.push_back({inst.position, slopeDir});
                        continue;
                    }
                    bool isNonColliding = proto.name == "Water" || proto.name == "AudioVisualizer";
                    if (!proto.isBlock || isNonColliding || !proto.isSolid) continue;
                    blockAABBs.push_back({inst.position - glm::vec3(0.5f), inst.position + glm::vec3(0.5f)});
                }
            }
        }
        if (baseSystem.fishing && baseSystem.fishing->rodPlacedInWorld) {
            glm::vec3 rodPos = glm::vec3(baseSystem.fishing->rodPlacedCell);
            blockAABBs.push_back({rodPos - glm::vec3(0.5f), rodPos + glm::vec3(0.5f)});
        }
        if (baseSystem.player && baseSystem.player->hatchetPlacedInWorld) {
            glm::vec3 hatchetPos = glm::vec3(baseSystem.player->hatchetPlacedCell);
            blockAABBs.push_back({hatchetPos - glm::vec3(0.5f), hatchetPos + glm::vec3(0.5f)});
        }

        bool usedAutoStep = false;
        bool nearSlopeForAutoStep = false;
        if (!slopeColliders.empty()) {
            const float feetYPrev = prevPos.y - halfExtents.y;
            for (const auto& slope : slopeColliders) {
                if (std::abs(prevPos.x - slope.center.x) > 0.60f) continue;
                if (std::abs(prevPos.z - slope.center.z) > 0.60f) continue;
                const float blockBottomY = slope.center.y - 0.5f;
                const float blockTopY = slope.center.y + 0.5f;
                if (feetYPrev < blockBottomY - 0.25f) continue;
                if (feetYPrev > blockTopY + 1.25f) continue;
                nearSlopeForAutoStep = true;
                break;
            }
        }
        const float slopeHorizRadiusScale = glm::clamp(getRegistryFloat(baseSystem, "SlopeHorizontalCollisionRadiusScale", 0.82f), 0.60f, 1.0f);
        const float slopeHorizIgnoreBelowTop = glm::clamp(getRegistryFloat(baseSystem, "SlopeHorizontalIgnoreBelowTopDelta", 0.22f), 0.0f, 1.5f);
        std::vector<AABB> horizontalBlocks;
        horizontalBlocks.reserve(blockAABBs.size());
        if (nearSlopeForAutoStep) {
            const float feetYPrev = prevPos.y - halfExtents.y;
            const float topThreshold = feetYPrev + slopeHorizIgnoreBelowTop;
            for (const auto& block : blockAABBs) {
                // Ignore low floor-support side faces near slopes; keep real walls.
                if (block.max.y <= topThreshold) continue;
                horizontalBlocks.push_back(block);
            }
        } else {
            horizontalBlocks = blockAABBs;
        }
        glm::vec3 moveHalfExtents = halfExtents;
        if (nearSlopeForAutoStep) {
            moveHalfExtents.x *= slopeHorizRadiusScale;
            moveHalfExtents.z *= slopeHorizRadiusScale;
        }

        glm::vec3 resolvedPos = prevPos;
        ResolveAxis(resolvedPos, 0, moveHalfExtents.x, velocity.x, horizontalBlocks, moveHalfExtents);
        ResolveAxis(resolvedPos, 1, halfExtents.y, velocity.y, blockAABBs, halfExtents);
        ResolveAxis(resolvedPos, 2, moveHalfExtents.z, velocity.z, horizontalBlocks, moveHalfExtents);

        const bool autoStepEnabled = getRegistryBool(baseSystem, "AutoStepEnabled", true);
        const float autoStepHeight = glm::clamp(getRegistryFloat(baseSystem, "AutoStepHeight", 1.0f), 0.1f, 1.5f);
        const float horizontalRequested = glm::length(glm::vec2(velocity.x, velocity.z));
        const float horizontalResolved = glm::length(glm::vec2(resolvedPos.x - prevPos.x, resolvedPos.z - prevPos.z));
        const bool horizontallyBlocked = horizontalRequested > 1e-4f && (horizontalResolved + 1e-4f) < horizontalRequested;
        if (autoStepEnabled && !nearSlopeForAutoStep && wasGrounded && velocity.y <= 0.001f && horizontallyBlocked) {
            glm::vec3 stepPos = prevPos;
            stepPos.y += autoStepHeight;

            // Can't step into occupied headroom.
            if (!IntersectsAnyBlock(stepPos, halfExtents, blockAABBs)) {
                ResolveAxis(stepPos, 0, halfExtents.x, velocity.x, horizontalBlocks, halfExtents);
                ResolveAxis(stepPos, 2, halfExtents.z, velocity.z, horizontalBlocks, halfExtents);
                ResolveAxis(stepPos, 1, halfExtents.y, -(autoStepHeight + 0.02f), blockAABBs, halfExtents);

                float steppedHorizontal = glm::length(glm::vec2(stepPos.x - prevPos.x, stepPos.z - prevPos.z));
                float lift = stepPos.y - prevPos.y;
                bool validLift = lift >= -0.05f && lift <= (autoStepHeight + 0.05f);
                if (validLift && steppedHorizontal > (horizontalResolved + 0.02f)) {
                    resolvedPos = stepPos;
                    usedAutoStep = true;
                }
            }
        }

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

        if (!slopeColliders.empty()) {
            const float skin = 0.001f;
            const float slopeSnapDown = glm::clamp(getRegistryFloat(baseSystem, "SlopeSnapDown", 0.45f), 0.0f, 2.0f);
            const float slopeSnapUp = glm::clamp(getRegistryFloat(baseSystem, "SlopeSnapUp", 1.25f), 0.1f, 3.0f);
            const float slopeSampleMargin = glm::clamp(getRegistryFloat(baseSystem, "SlopeSampleMargin", 0.14f), 0.0f, 0.35f);
            const float slopeSeamExtraSnapDown = glm::clamp(getRegistryFloat(baseSystem, "SlopeSeamExtraSnapDown", 0.20f), 0.0f, 0.75f);
            const float slopeAscendingUnsnapVelocity = glm::clamp(getRegistryFloat(baseSystem, "SlopeAscendingUnsnapVelocity", 0.05f), 0.0f, 20.0f);
            const float effectiveSnapDown = glm::min(2.0f, slopeSnapDown + (wasGrounded ? slopeSeamExtraSnapDown : 0.0f));
            const bool slopeHighWallCollision = false;
            const bool skipSlopeSnapAscending = velocity.y > slopeAscendingUnsnapVelocity;
            bool hasSlopeSnap = false;
            bool hasUpSnap = false;
            float bestSlopeSurfaceY = 0.0f;
            float bestUpDelta = std::numeric_limits<float>::infinity();
            float bestDownDelta = -std::numeric_limits<float>::infinity();
            float bestUpDist2 = std::numeric_limits<float>::infinity();
            float bestDownDist2 = std::numeric_limits<float>::infinity();
            std::unordered_set<glm::ivec3, IVec3Hash> slopeCells;
            slopeCells.reserve(slopeColliders.size() * 2);
            for (const auto& slope : slopeColliders) {
                slopeCells.insert(glm::ivec3(
                    static_cast<int>(std::floor(slope.center.x + 0.5f)),
                    static_cast<int>(std::floor(slope.center.y + 0.5f)),
                    static_cast<int>(std::floor(slope.center.z + 0.5f))));
            }

            for (const auto& slope : slopeColliders) {
                if (skipSlopeSnapAscending) continue;
                const float blockBottomY = slope.center.y - 0.5f;
                const float blockTopY = slope.center.y + 0.5f;
                float feetY = resolvedPos.y - halfExtents.y;
                if (resolvedPos.y + halfExtents.y < blockBottomY - 0.05f) continue;
                if (feetY > blockTopY + 1.0f) continue;
                const float dx = resolvedPos.x - slope.center.x;
                const float dz = resolvedPos.z - slope.center.z;
                const glm::ivec3 slopeCell(
                    static_cast<int>(std::floor(slope.center.x + 0.5f)),
                    static_cast<int>(std::floor(slope.center.y + 0.5f)),
                    static_cast<int>(std::floor(slope.center.z + 0.5f)));
                const glm::ivec3 highDir = slopeHighDir(slope.dir);
                const bool hasUphillNeighbor =
                    slopeCells.count(slopeCell + highDir + glm::ivec3(0, 1, 0)) > 0 ||
                    slopeCells.count(slopeCell + highDir) > 0;

                // High-side face acts as a blocking wall.
                if (slopeHighWallCollision
                    && !hasUphillNeighbor
                    && resolvedPos.y + halfExtents.y > blockBottomY + skin
                    && feetY < blockTopY + skin) {
                    switch (slope.dir) {
                        case SlopeDir::PosX: {
                            if (std::abs(dz) > 0.5f) break;
                            if (dx < 0.25f) break;
                            float wallX = slope.center.x + 0.5f;
                            if (resolvedPos.x + halfExtents.x > wallX - skin) {
                                resolvedPos.x = wallX - halfExtents.x - skin;
                            }
                            break;
                        }
                        case SlopeDir::NegX: {
                            if (std::abs(dz) > 0.5f) break;
                            if (dx > -0.25f) break;
                            float wallX = slope.center.x - 0.5f;
                            if (resolvedPos.x - halfExtents.x < wallX + skin) {
                                resolvedPos.x = wallX + halfExtents.x + skin;
                            }
                            break;
                        }
                        case SlopeDir::PosZ: {
                            if (std::abs(dx) > 0.5f) break;
                            if (dz < 0.25f) break;
                            float wallZ = slope.center.z + 0.5f;
                            if (resolvedPos.z + halfExtents.z > wallZ - skin) {
                                resolvedPos.z = wallZ - halfExtents.z - skin;
                            }
                            break;
                        }
                        case SlopeDir::NegZ: {
                            if (std::abs(dx) > 0.5f) break;
                            if (dz > -0.25f) break;
                            float wallZ = slope.center.z - 0.5f;
                            if (resolvedPos.z - halfExtents.z < wallZ + skin) {
                                resolvedPos.z = wallZ + halfExtents.z + skin;
                            }
                            break;
                        }
                        default:
                            break;
                    }
                }

                // Recompute after potential wall correction.
                float sampleDxRaw = resolvedPos.x - slope.center.x;
                float sampleDzRaw = resolvedPos.z - slope.center.z;
                // Use center-in-cell test for slope sampling to avoid snapping to distant
                // neighboring ramps that only overlap by capsule width.
                if (std::abs(sampleDxRaw) > (0.5f + slopeSampleMargin)) continue;
                if (std::abs(sampleDzRaw) > (0.5f + slopeSampleMargin)) continue;

                float t = 0.0f;
                switch (slope.dir) {
                    case SlopeDir::PosX: t = sampleDxRaw + 0.5f; break;
                    case SlopeDir::NegX: t = 0.5f - sampleDxRaw; break;
                    case SlopeDir::PosZ: t = sampleDzRaw + 0.5f; break;
                    case SlopeDir::NegZ: t = 0.5f - sampleDzRaw; break;
                    default: t = -1.0f; break;
                }
                if (t < -0.05f || t > 1.05f) continue;
                t = glm::clamp(t, 0.0f, 1.0f);
                const float sampleDx = glm::clamp(sampleDxRaw, -0.5f, 0.5f);
                const float sampleDz = glm::clamp(sampleDzRaw, -0.5f, 0.5f);
                float surfaceY = blockBottomY + t;
                feetY = resolvedPos.y - halfExtents.y;
                float delta = surfaceY - feetY;
                const float lateralDist2 = sampleDx * sampleDx + sampleDz * sampleDz;
                if (delta < -effectiveSnapDown || delta > slopeSnapUp) continue;
                if (delta >= 0.0f) {
                    if (!hasUpSnap
                        || surfaceY > (bestSlopeSurfaceY + 1e-4f)
                        || (std::abs(surfaceY - bestSlopeSurfaceY) <= 1e-4f && lateralDist2 < bestUpDist2)) {
                        hasUpSnap = true;
                        hasSlopeSnap = true;
                        bestUpDelta = delta;
                        bestUpDist2 = lateralDist2;
                        bestSlopeSurfaceY = surfaceY;
                    }
                } else if (!hasUpSnap) {
                    if (!hasSlopeSnap
                        || delta > (bestDownDelta + 1e-4f)
                        || (std::abs(delta - bestDownDelta) <= 1e-4f && lateralDist2 < bestDownDist2)) {
                        hasSlopeSnap = true;
                        bestDownDelta = delta;
                        bestDownDist2 = lateralDist2;
                        bestSlopeSurfaceY = surfaceY;
                    }
                }
            }

            if (!skipSlopeSnapAscending && !hasSlopeSnap && wasGrounded && velocity.y <= 0.001f) {
                // Seam fallback: if we just crossed a slope edge, allow a second, wider snap query.
                float seamBestSurfaceY = -std::numeric_limits<float>::infinity();
                bool seamFound = false;
                const float seamMargin = slopeSampleMargin + 0.18f;
                const float seamDown = effectiveSnapDown + 0.25f;
                for (const auto& slope : slopeColliders) {
                    const float blockBottomY = slope.center.y - 0.5f;
                    const float blockTopY = slope.center.y + 0.5f;
                    float feetY = resolvedPos.y - halfExtents.y;
                    if (resolvedPos.y + halfExtents.y < blockBottomY - 0.05f) continue;
                    if (feetY > blockTopY + 1.0f) continue;

                    const float dxRaw = resolvedPos.x - slope.center.x;
                    const float dzRaw = resolvedPos.z - slope.center.z;
                    if (std::abs(dxRaw) > (0.5f + seamMargin)) continue;
                    if (std::abs(dzRaw) > (0.5f + seamMargin)) continue;

                    float t = 0.0f;
                    switch (slope.dir) {
                        case SlopeDir::PosX: t = dxRaw + 0.5f; break;
                        case SlopeDir::NegX: t = 0.5f - dxRaw; break;
                        case SlopeDir::PosZ: t = dzRaw + 0.5f; break;
                        case SlopeDir::NegZ: t = 0.5f - dzRaw; break;
                        default: t = -1.0f; break;
                    }
                    if (t < -0.05f || t > 1.05f) continue;
                    t = glm::clamp(t, 0.0f, 1.0f);
                    const float surfaceY = blockBottomY + t;
                    const float delta = surfaceY - feetY;
                    if (delta < -seamDown || delta > slopeSnapUp) continue;
                    if (!seamFound || surfaceY > seamBestSurfaceY) {
                        seamBestSurfaceY = surfaceY;
                        seamFound = true;
                    }
                }
                if (seamFound) {
                    hasSlopeSnap = true;
                    bestSlopeSurfaceY = seamBestSurfaceY;
                }
            }

            if (hasSlopeSnap) {
                resolvedPos.y = bestSlopeSurfaceY + halfExtents.y + skin;
                hitGround = true;
            }
        }

        player.cameraPosition = resolvedPos;
        // Update grounded state: if we were moving down and got clamped upward relative to desired
        hitGround = hitGround || usedAutoStep || ((velocity.y < 0.0f) && ((resolvedPos.y - desiredPos.y) > 0.001f));
        player.onGround = hitGround;
        const bool landedThisFrame = (!wasGrounded && player.onGround);
        if (landedThisFrame) {
            const bool landingSfxEnabled = getRegistryBool(baseSystem, "WalkLandingSfxEnabled", true);
            if (landingSfxEnabled) {
                const float minImpactSpeed = std::max(0.0f, getRegistryFloat(baseSystem, "WalkLandingSfxMinImpactSpeed", 4.5f));
                const float landingCooldown = std::max(0.0f, getRegistryFloat(baseSystem, "WalkLandingSfxCooldown", 0.08f));
                const float impactSpeed = std::max(0.0f, -preResolveVerticalVelocity);
                if (impactSpeed >= minImpactSpeed) {
                    triggerGameplaySfx(baseSystem, "footstep.ck", landingCooldown);
                }
            }
        }
        if (player.onGround) player.verticalVelocity = 0.0f;
        player.prevCameraPosition = resolvedPos;
    }
}
