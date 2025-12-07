#pragma once

#include <unordered_set>
#include <limits>
#include <cmath>

namespace BlockSelectionSystemLogic {

    struct IVec3Hash {
        std::size_t operator()(const glm::ivec3& v) const noexcept {
            std::size_t hx = std::hash<int>()(v.x);
            std::size_t hy = std::hash<int>()(v.y);
            std::size_t hz = std::hash<int>()(v.z);
            return hx ^ (hy << 1) ^ (hz << 2);
        }
    };

    struct BlockBounds {
        glm::vec3 center;
        glm::vec3 min;
        glm::vec3 max;
        int worldIndex = -1;
        bool active = true;
        int cacheIndex = -1;
    };

    struct WorldBlockCache {
        std::vector<BlockBounds> blocks;
        std::unordered_map<glm::ivec3, std::vector<int>, IVec3Hash> cellLookup;
        bool initialized = false;
    };

    static std::vector<WorldBlockCache> g_worldCaches;

    glm::vec3 GetCameraDirection(const PlayerContext& player) {
        glm::vec3 dir;
        dir.x = cos(glm::radians(player.cameraYaw)) * cos(glm::radians(player.cameraPitch));
        dir.y = sin(glm::radians(player.cameraPitch));
        dir.z = sin(glm::radians(player.cameraYaw)) * cos(glm::radians(player.cameraPitch));
        return glm::normalize(dir);
    }

    bool RaycastBlocks(const std::vector<WorldBlockCache>& caches,
                       const glm::vec3& origin,
                       const glm::vec3& direction,
                       glm::ivec3& outCell,
                       glm::vec3& outPosition,
                       glm::vec3& outCenter,
                       int& outWorldIndex,
                       int& outBlockIndex) {
        const float maxDistance = 5.0f;
        glm::vec3 dir = glm::normalize(direction);
        if (glm::length(dir) < 0.0001f) return false;

        glm::vec3 rayPos = origin;
        glm::ivec3 cell = glm::ivec3(glm::floor(rayPos));

        glm::vec3 deltaDist(
            dir.x != 0.0f ? std::abs(1.0f / dir.x) : std::numeric_limits<float>::infinity(),
            dir.y != 0.0f ? std::abs(1.0f / dir.y) : std::numeric_limits<float>::infinity(),
            dir.z != 0.0f ? std::abs(1.0f / dir.z) : std::numeric_limits<float>::infinity()
        );

        glm::ivec3 step(
            dir.x >= 0.0f ? 1 : -1,
            dir.y >= 0.0f ? 1 : -1,
            dir.z >= 0.0f ? 1 : -1
        );

        glm::vec3 sideDist;
        sideDist.x = (dir.x >= 0.0f) ? (cell.x + 1.0f - rayPos.x) : (rayPos.x - cell.x);
        sideDist.y = (dir.y >= 0.0f) ? (cell.y + 1.0f - rayPos.y) : (rayPos.y - cell.y);
        sideDist.z = (dir.z >= 0.0f) ? (cell.z + 1.0f - rayPos.z) : (rayPos.z - cell.z);
        sideDist *= deltaDist;

        float distance = 0.0f;
        const int maxSteps = 1024;
        auto testCell = [&](const glm::ivec3& candidateCell, float& currentBestDist, glm::vec3& hitPos, glm::vec3& hitCenter, int& hitWorld, int& hitBlock) -> bool {
            bool hit = false;
            for (const auto& cache : caches) {
                auto iter = cache.cellLookup.find(candidateCell);
                if (iter == cache.cellLookup.end()) continue;
                for (int idx : iter->second) {
                    if (idx < 0 || idx >= static_cast<int>(cache.blocks.size())) continue;
                    const BlockBounds& block = cache.blocks[idx];
                    if (!block.active) continue;
                    float tMin = 0.0f, tMax = maxDistance;
                    for (int axis = 0; axis < 3; ++axis) {
                        float invD = (axis == 0 ? dir.x : (axis == 1 ? dir.y : dir.z));
                        if (std::abs(invD) < 1e-6f) {
                            float minBound = block.min[axis];
                            float maxBound = block.max[axis];
                            if (rayPos[axis] < minBound || rayPos[axis] > maxBound) { tMin = tMax + 1.0f; break; }
                        } else {
                            float o = rayPos[axis];
                            float minBound = block.min[axis];
                            float maxBound = block.max[axis];
                            float t1 = (minBound - o) / dir[axis];
                            float t2 = (maxBound - o) / dir[axis];
                            if (t1 > t2) std::swap(t1, t2);
                            tMin = std::max(tMin, t1);
                            tMax = std::min(tMax, t2);
                            if (tMin > tMax) break;
                        }
                    }
                    if (tMin <= tMax && tMin >= 0.0f) {
                        float dist = tMin;
                        if (dist < currentBestDist) {
                            currentBestDist = dist;
                            hitPos = rayPos + dir * dist;
                            hitCenter = block.center;
                            hitWorld = block.worldIndex;
                            hitBlock = idx;
                            hit = true;
                        }
                    }
                }
            }
            return hit;
        };

        float bestDistance = maxDistance;
        bool found = false;
        glm::vec3 bestHitPos(0.0f), bestHitCenter(0.0f);
        int bestWorldIndex = -1;
        int bestBlockIndex = -1;

        for (int stepIndex = 0; stepIndex < maxSteps; ++stepIndex) {
            if (testCell(cell, bestDistance, bestHitPos, bestHitCenter, bestWorldIndex, bestBlockIndex)) {
                outCell = cell;
                outPosition = bestHitPos;
                outCenter = bestHitCenter;
                outWorldIndex = bestWorldIndex;
                outBlockIndex = bestBlockIndex;
                found = true;
                break;
            }

            if (sideDist.x < sideDist.y) {
                if (sideDist.x < sideDist.z) {
                    cell.x += step.x;
                    distance = sideDist.x;
                    sideDist.x += deltaDist.x;
                } else {
                    cell.z += step.z;
                    distance = sideDist.z;
                    sideDist.z += deltaDist.z;
                }
            } else {
                if (sideDist.y < sideDist.z) {
                    cell.y += step.y;
                    distance = sideDist.y;
                    sideDist.y += deltaDist.y;
                } else {
                    cell.z += step.z;
                    distance = sideDist.z;
                    sideDist.z += deltaDist.z;
                }
            }

            if (distance > bestDistance) break;
        }

        return found;
    }

    void UpdateBlockSelection(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player || !baseSystem.level) return;
        PlayerContext& player = *baseSystem.player;
        LevelContext& level = *baseSystem.level;
        if (level.worlds.empty()) { player.hasBlockTarget = false; return; }

        if (g_worldCaches.size() != level.worlds.size()) g_worldCaches.resize(level.worlds.size());

        for (size_t i = 0; i < level.worlds.size(); ++i) {
            Entity& world = level.worlds[i];
            auto& cache = g_worldCaches[i];
            if (cache.initialized) continue;
            cache.blocks.clear();
            cache.cellLookup.clear();
            cache.blocks.reserve(world.instances.size());
            for (const auto& inst : world.instances) {
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[inst.prototypeID];
                if (!proto.isBlock) continue;
                BlockBounds bounds;
                bounds.center = inst.position;
                bounds.min = inst.position - glm::vec3(0.5f);
                bounds.max = inst.position + glm::vec3(0.5f);
                bounds.worldIndex = static_cast<int>(i);
                int blockIndex = static_cast<int>(cache.blocks.size());
                cache.blocks.push_back(bounds);
                glm::ivec3 minCell = glm::ivec3(glm::floor(bounds.min));
                glm::ivec3 maxCell = glm::ivec3(glm::floor(bounds.max - glm::vec3(1e-4f))) + glm::ivec3(0);
                for (int cx = minCell.x; cx <= maxCell.x; ++cx) {
                    for (int cy = minCell.y; cy <= maxCell.y; ++cy) {
                        for (int cz = minCell.z; cz <= maxCell.z; ++cz) {
                            cache.cellLookup[glm::ivec3(cx, cy, cz)].push_back(blockIndex);
                        }
                    }
                }
            }
            cache.initialized = true;
        }

        glm::vec3 dir = GetCameraDirection(player);
        glm::ivec3 hitCell;
        glm::vec3 hitPos;
        glm::vec3 hitCenter;
        int hitWorld = -1;
        int hitBlockIndex = -1;
        if (RaycastBlocks(g_worldCaches, player.cameraPosition, dir, hitCell, hitPos, hitCenter, hitWorld, hitBlockIndex)) {
            player.hasBlockTarget = true;
            player.targetedBlock = hitCell;
            player.targetedBlockPosition = hitCenter;
            player.targetedWorldIndex = hitWorld;
            player.targetedBlockCacheIndex = hitBlockIndex;
        } else {
            player.hasBlockTarget = false;
            player.targetedWorldIndex = -1;
            player.targetedBlockCacheIndex = -1;
        }
    }

    void MarkBlockInactive(int worldIndex, int blockIndex) {
        if (worldIndex < 0 || worldIndex >= static_cast<int>(g_worldCaches.size())) return;
        WorldBlockCache& cache = g_worldCaches[worldIndex];
        if (blockIndex < 0 || blockIndex >= static_cast<int>(cache.blocks.size())) return;
        cache.blocks[blockIndex].active = false;
    }
}
