#pragma once

#include <unordered_map>
#include <limits>
#include <cmath>

namespace BlockSelectionSystemLogic {

    namespace {
        glm::vec3 cameraEyePosition(const BaseSystem& baseSystem, const PlayerContext& player) {
            if (baseSystem.gamemode == "survival") {
                return player.cameraPosition + glm::vec3(0.0f, 0.6f, 0.0f);
            }
            return player.cameraPosition;
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

    struct BlockEntry {
        glm::vec3 center;
        glm::vec3 min;
        glm::vec3 max;
        int worldIndex = -1;
        bool active = true;
        float dampingFactor = 0.25f;
    };

    struct WorldBlockCache {
        std::vector<BlockEntry> blocks;
        std::unordered_map<glm::ivec3, std::vector<int>, IVec3Hash> cellLookup;
        std::unordered_map<glm::ivec3, int, IVec3Hash> positionLookup;
        bool initialized = false;
    };

    static std::vector<WorldBlockCache> g_worldCaches;

    glm::ivec3 PositionKey(const glm::vec3& pos) {
        return glm::ivec3(glm::round(pos));
    }

    void AddEntry(WorldBlockCache& cache, const BlockEntry& entry) {
        int index = static_cast<int>(cache.blocks.size());
        cache.blocks.push_back(entry);
        cache.positionLookup[PositionKey(entry.center)] = index;
        glm::ivec3 minCell = glm::ivec3(glm::floor(entry.min));
        glm::ivec3 maxCell = glm::ivec3(glm::floor(entry.max - glm::vec3(1e-4f)));
        for (int x = minCell.x; x <= maxCell.x; ++x) {
            for (int y = minCell.y; y <= maxCell.y; ++y) {
                for (int z = minCell.z; z <= maxCell.z; ++z) {
                    cache.cellLookup[glm::ivec3(x, y, z)].push_back(index);
                }
            }
        }
    }

    void BuildCache(WorldBlockCache& cache, int worldIndex, const Entity& world, const std::vector<Entity>& prototypes) {
        cache.blocks.clear();
        cache.cellLookup.clear();
        cache.positionLookup.clear();
        for (const auto& inst : world.instances) {
            if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
            const Entity& proto = prototypes[inst.prototypeID];
            if (!proto.isBlock) continue;
            BlockEntry entry;
            entry.center = inst.position;
            entry.min = inst.position - glm::vec3(0.5f);
            entry.max = inst.position + glm::vec3(0.5f);
            entry.worldIndex = worldIndex;
            entry.active = true;
            entry.dampingFactor = proto.dampingFactor;
            AddEntry(cache, entry);
        }
        cache.initialized = true;
    }

    void EnsureCacheBuilt(int worldIndex, BaseSystem& baseSystem, const std::vector<Entity>& prototypes) {
        if (!baseSystem.level) return;
        if (g_worldCaches.size() <= static_cast<size_t>(worldIndex)) {
            g_worldCaches.resize(worldIndex + 1);
        }
        WorldBlockCache& cache = g_worldCaches[worldIndex];
        if (!cache.initialized) {
            BuildCache(cache, worldIndex, baseSystem.level->worlds[worldIndex], prototypes);
        }
    }

    void EnsureAllCaches(BaseSystem& baseSystem, const std::vector<Entity>& prototypes) {
        if (!baseSystem.level) return;
        if (g_worldCaches.size() < baseSystem.level->worlds.size()) {
            g_worldCaches.resize(baseSystem.level->worlds.size());
        }
        for (size_t i = 0; i < baseSystem.level->worlds.size(); ++i) {
            EnsureCacheBuilt(static_cast<int>(i), baseSystem, prototypes);
        }
    }

    bool EnsureLocalCaches(BaseSystem& baseSystem,
                           const std::vector<Entity>& prototypes,
                           const glm::vec3& cameraPosition,
                           int radius) {
        (void)cameraPosition;
        (void)radius;
        if (!baseSystem.level) return false;
        if (g_worldCaches.size() < baseSystem.level->worlds.size()) {
            g_worldCaches.resize(baseSystem.level->worlds.size());
        }
        for (size_t i = 0; i < baseSystem.level->worlds.size(); ++i) {
            WorldBlockCache& cache = g_worldCaches[i];
            if (!cache.initialized) {
                BuildCache(cache, static_cast<int>(i), baseSystem.level->worlds[i], prototypes);
            }
        }
        return true;
    }

    void InvalidateWorldCache(int worldIndex) {
        if (worldIndex < 0) return;
        if (g_worldCaches.size() <= static_cast<size_t>(worldIndex)) return;
        g_worldCaches[worldIndex].initialized = false;
    }

    void AddBlockToCache(BaseSystem& baseSystem, std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position, int prototypeID) {
        if (worldIndex < 0 || !baseSystem.level || worldIndex >= static_cast<int>(baseSystem.level->worlds.size())) return;
        EnsureCacheBuilt(worldIndex, baseSystem, prototypes);
        WorldBlockCache& cache = g_worldCaches[worldIndex];
        glm::ivec3 key = PositionKey(position);
        auto it = cache.positionLookup.find(key);
        if (it != cache.positionLookup.end()) {
            cache.blocks[it->second].active = true;
            cache.blocks[it->second].center = position;
            cache.blocks[it->second].min = position - glm::vec3(0.5f);
            cache.blocks[it->second].max = position + glm::vec3(0.5f);
            cache.blocks[it->second].dampingFactor = (prototypeID >= 0 && prototypeID < static_cast<int>(prototypes.size()))
                ? prototypes[prototypeID].dampingFactor : cache.blocks[it->second].dampingFactor;
            return;
        }
        BlockEntry entry;
        entry.center = position;
        entry.min = position - glm::vec3(0.5f);
        entry.max = position + glm::vec3(0.5f);
        entry.worldIndex = worldIndex;
        entry.active = true;
        entry.dampingFactor = (prototypeID >= 0 && prototypeID < static_cast<int>(prototypes.size()))
            ? prototypes[prototypeID].dampingFactor : 0.5f;
        AddEntry(cache, entry);
    }

    void RemoveBlockFromCache(BaseSystem& baseSystem, const std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position) {
        if (worldIndex < 0 || !baseSystem.level || worldIndex >= static_cast<int>(baseSystem.level->worlds.size())) return;
        EnsureCacheBuilt(worldIndex, baseSystem, prototypes);
        WorldBlockCache& cache = g_worldCaches[worldIndex];
        glm::ivec3 key = PositionKey(position);
        auto it = cache.positionLookup.find(key);
        if (it != cache.positionLookup.end()) {
            cache.blocks[it->second].active = false;
            cache.positionLookup.erase(it);
        }
    }

    bool HasBlockAt(BaseSystem& baseSystem, const std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position) {
        if (worldIndex < 0 || !baseSystem.level) return false;
        if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
            glm::ivec3 cell = glm::ivec3(glm::round(position));
            return baseSystem.voxelWorld->getBlockWorld(cell) != 0;
        }
        EnsureCacheBuilt(worldIndex, baseSystem, prototypes);
        WorldBlockCache& cache = g_worldCaches[worldIndex];
        glm::ivec3 key = PositionKey(position);
        auto it = cache.positionLookup.find(key);
        if (it == cache.positionLookup.end()) return false;
        int idx = it->second;
        return idx >= 0 && idx < static_cast<int>(cache.blocks.size()) && cache.blocks[idx].active;
    }

    bool SampleBlockDamping(BaseSystem& baseSystem,
                            const glm::ivec3& cell,
                            float& dampingOut) {
        if (!baseSystem.level) return false;
        if (g_worldCaches.size() < baseSystem.level->worlds.size()) return false;
        for (const auto& cache : g_worldCaches) {
            if (!cache.initialized) continue;
            auto it = cache.positionLookup.find(cell);
            if (it == cache.positionLookup.end()) continue;
            int idx = it->second;
            if (idx < 0 || idx >= static_cast<int>(cache.blocks.size())) continue;
            const BlockEntry& block = cache.blocks[idx];
            if (!block.active) continue;
            dampingOut = block.dampingFactor;
            return true;
        }
        return false;
    }

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
                       glm::vec3& outCenter,
                       glm::vec3& outNormal,
                       int& outWorldIndex) {
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

        glm::ivec3 step(dir.x >= 0.0f ? 1 : -1, dir.y >= 0.0f ? 1 : -1, dir.z >= 0.0f ? 1 : -1);

        glm::vec3 sideDist;
        sideDist.x = (dir.x >= 0.0f) ? (cell.x + 1.0f - rayPos.x) : (rayPos.x - cell.x);
        sideDist.y = (dir.y >= 0.0f) ? (cell.y + 1.0f - rayPos.y) : (rayPos.y - cell.y);
        sideDist.z = (dir.z >= 0.0f) ? (cell.z + 1.0f - rayPos.z) : (rayPos.z - cell.z);
        sideDist *= deltaDist;

        glm::vec3 entryNormal(0.0f);
        float bestDistance = maxDistance;
        bool found = false;
        glm::vec3 bestCenter(0.0f);
        glm::vec3 bestNormal(0.0f);
        int bestWorld = -1;

        auto testCell = [&](const glm::ivec3& candidateCell) {
            for (const auto& cache : caches) {
                auto iter = cache.cellLookup.find(candidateCell);
                if (iter == cache.cellLookup.end()) continue;
                for (int idx : iter->second) {
                    if (idx < 0 || idx >= static_cast<int>(cache.blocks.size())) continue;
                    const BlockEntry& block = cache.blocks[idx];
                    if (!block.active) continue;
                    float tMin = 0.0f, tMax = maxDistance;
                    for (int axis = 0; axis < 3; ++axis) {
                        float invD = (axis == 0 ? dir.x : (axis == 1 ? dir.y : dir.z));
                        if (std::abs(invD) < 1e-6f) {
                            float minBound = block.min[axis];
                            float maxBound = block.max[axis];
                            if (rayPos[axis] < minBound || rayPos[axis] > maxBound) { tMin = tMax + 1.0f; break; }
                        } else {
                            float originComponent = rayPos[axis];
                            float minBound = block.min[axis];
                            float maxBound = block.max[axis];
                            float t1 = (minBound - originComponent) / invD;
                            float t2 = (maxBound - originComponent) / invD;
                            if (t1 > t2) std::swap(t1, t2);
                            tMin = std::max(tMin, t1);
                            tMax = std::min(tMax, t2);
                            if (tMin > tMax) break;
                        }
                    }
                    if (tMin <= tMax && tMin >= 0.0f) {
                        if (tMin < bestDistance) {
                            bestDistance = tMin;
                            bestCenter = block.center;
                            bestNormal = entryNormal;
                            bestWorld = block.worldIndex;
                            found = true;
                        }
                    }
                }
            }
        };

        const int maxSteps = 512;
        for (int i = 0; i < maxSteps; ++i) {
            testCell(cell);
            if (found) break;

            if (sideDist.x < sideDist.y) {
                if (sideDist.x < sideDist.z) {
                    cell.x += step.x;
                    entryNormal = glm::vec3(-step.x, 0.0f, 0.0f);
                    sideDist.x += deltaDist.x;
                } else {
                    cell.z += step.z;
                    entryNormal = glm::vec3(0.0f, 0.0f, -step.z);
                    sideDist.z += deltaDist.z;
                }
            } else {
                if (sideDist.y < sideDist.z) {
                    cell.y += step.y;
                    entryNormal = glm::vec3(0.0f, -step.y, 0.0f);
                    sideDist.y += deltaDist.y;
                } else {
                    cell.z += step.z;
                    entryNormal = glm::vec3(0.0f, 0.0f, -step.z);
                    sideDist.z += deltaDist.z;
                }
            }

            if (bestDistance < std::numeric_limits<float>::infinity() && sideDist.x > bestDistance && sideDist.y > bestDistance && sideDist.z > bestDistance) break;
        }

        if (!found) return false;
        outCell = cell;
        outCenter = bestCenter;
        if (glm::length(bestNormal) < 0.001f) bestNormal = -glm::sign(dir);
        outNormal = glm::normalize(bestNormal);
        outWorldIndex = bestWorld;
        return true;
    }

    void UpdateBlockSelection(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player || !baseSystem.level) return;
        PlayerContext& player = *baseSystem.player;
        LevelContext& level = *baseSystem.level;
        if (level.worlds.empty()) { player.hasBlockTarget = false; return; }

        const int kSelectionCacheRadius = 1;
        glm::vec3 eyePos = cameraEyePosition(baseSystem, player);
        if (!EnsureLocalCaches(baseSystem, prototypes, eyePos, kSelectionCacheRadius)) {
            player.hasBlockTarget = false;
            player.targetedWorldIndex = -1;
            player.targetedBlockNormal = glm::vec3(0.0f);
            return;
        }

        glm::vec3 dir = GetCameraDirection(player);
        glm::ivec3 hitCell;
        glm::vec3 hitCenter;
        glm::vec3 hitNormal(0.0f);
        int hitWorld = -1;
        if (RaycastBlocks(g_worldCaches, eyePos, dir, hitCell, hitCenter, hitNormal, hitWorld)) {
            player.hasBlockTarget = true;
            player.targetedBlock = hitCell;
            player.targetedBlockPosition = hitCenter;
            player.targetedBlockNormal = hitNormal;
            player.targetedWorldIndex = hitWorld;
        } else {
            player.hasBlockTarget = false;
            player.targetedWorldIndex = -1;
            player.targetedBlockNormal = glm::vec3(0.0f);
        }
    }
}
