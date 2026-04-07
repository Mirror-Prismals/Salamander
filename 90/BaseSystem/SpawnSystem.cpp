#pragma once
#include "../Host.h"

namespace SpawnSystemLogic {

    struct SpawnConfig {
        glm::vec3 position{0.0f, 4.0f, 0.0f};
        float yaw = -90.0f;
        float pitch = 0.0f;
    };

}

namespace ExpanseBiomeSystemLogic {
    bool SampleTerrain(const WorldContext& worldCtx, float x, float z, float& outHeight);
    int ResolveBiome(const WorldContext& worldCtx, float x, float z);
}
namespace VoxelMeshingSystemLogic {
    void RequestPriorityVoxelRemesh(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const glm::ivec3& worldCell);
}
namespace BlockSelectionSystemLogic {
    void AddBlockToCache(BaseSystem& baseSystem,
                         std::vector<Entity>& prototypes,
                         int worldIndex,
                         const glm::vec3& position,
                         int prototypeID);
}
namespace BookSystemLogic {
    void PlaceSpawnBookCluster(BaseSystem& baseSystem,
                               std::vector<Entity>& prototypes,
                               const glm::ivec2& centerXZ,
                               int nominalSurfaceY);
}

namespace SpawnSystemLogic {

    bool loadSpawnConfig(const std::string& key, SpawnConfig& outConfig) {
        std::ifstream f("Procedures/spawns.json");
        if (!f.is_open()) {
            std::cerr << "SpawnSystem: could not open Procedures/spawns.json" << std::endl;
            return false;
        }
        json data;
        try { data = json::parse(f); }
        catch (...) {
            std::cerr << "SpawnSystem: failed to parse spawns.json" << std::endl;
            return false;
        }
        if (!data.contains(key)) {
            std::cerr << "SpawnSystem: spawn key '" << key << "' not found, using defaults." << std::endl;
            return false;
        }
        const auto& cfg = data[key];
        if (cfg.contains("position")) cfg.at("position").get_to(outConfig.position);
        if (cfg.contains("yaw")) cfg.at("yaw").get_to(outConfig.yaw);
        if (cfg.contains("pitch")) cfg.at("pitch").get_to(outConfig.pitch);
        return true;
    }

    bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
        if (!baseSystem.registry) return fallback;
        auto it = baseSystem.registry->find(key);
        if (it == baseSystem.registry->end()) return fallback;
        if (!std::holds_alternative<bool>(it->second)) return fallback;
        return std::get<bool>(it->second);
    }

    int getRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback) {
        if (!baseSystem.registry) return fallback;
        auto it = baseSystem.registry->find(key);
        if (it == baseSystem.registry->end()) return fallback;
        if (!std::holds_alternative<std::string>(it->second)) return fallback;
        try {
            return std::stoi(std::get<std::string>(it->second));
        } catch (...) {
            return fallback;
        }
    }

    bool isLandSurface(const WorldContext& worldCtx, float x, float z, int* outSurfaceY) {
        float sampledHeight = 0.0f;
        const bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(worldCtx, x, z, sampledHeight);
        if (!isLand) return false;
        const int surfaceY = static_cast<int>(std::floor(sampledHeight));
        if (outSurfaceY) *outSurfaceY = surfaceY;
        return true;
    }

    bool hasNearbyOceanWater(const WorldContext& worldCtx, float x, float z, float probeStep) {
        const std::array<glm::vec2, 8> probeDirs{
            glm::vec2(1.0f, 0.0f),
            glm::vec2(-1.0f, 0.0f),
            glm::vec2(0.0f, 1.0f),
            glm::vec2(0.0f, -1.0f),
            glm::vec2(0.7071f, 0.7071f),
            glm::vec2(-0.7071f, 0.7071f),
            glm::vec2(0.7071f, -0.7071f),
            glm::vec2(-0.7071f, -0.7071f)
        };
        for (const glm::vec2& dir : probeDirs) {
            float neighborHeight = 0.0f;
            const bool neighborIsLand = ExpanseBiomeSystemLogic::SampleTerrain(
                worldCtx,
                x + dir.x * probeStep,
                z + dir.y * probeStep,
                neighborHeight
            );
            if (!neighborIsLand) return true;
        }
        return false;
    }

    bool findOceanEdgeSpawnTarget(const BaseSystem& baseSystem,
                                  const glm::vec3& origin,
                                  glm::vec2* outXZ,
                                  int* outSurfaceY) {
        if (!baseSystem.world || !baseSystem.world->expanse.loaded || !outXZ || !outSurfaceY) return false;
        const WorldContext& worldCtx = *baseSystem.world;
        const float islandRadius = std::max(0.0f, worldCtx.expanse.islandRadius);
        if (islandRadius < 32.0f) return false;

        const glm::vec2 islandCenter(worldCtx.expanse.islandCenterX, worldCtx.expanse.islandCenterZ);
        glm::vec2 outwardDir(origin.x - islandCenter.x, origin.z - islandCenter.y);
        if (glm::dot(outwardDir, outwardDir) < 0.0001f) {
            outwardDir = glm::vec2(1.0f, 0.0f);
        } else {
            outwardDir = glm::normalize(outwardDir);
        }

        const float shorelineProbeStep = 14.0f;
        const float edgeRadius = std::max(8.0f, islandRadius - 2.0f);
        const float inwardSearchMax = std::min(std::max(48.0f, worldCtx.expanse.beachHeight + 48.0f), islandRadius - 8.0f);
        const float inwardStep = 4.0f;
        const float maxAngleOffset = glm::radians(32.0f);
        const int angleSamples = 15;
        const float tau = 6.28318530718f;
        const int beachMaxSurfaceY = static_cast<int>(std::floor(
            worldCtx.expanse.waterSurface + std::max(0.0f, worldCtx.expanse.beachHeight)
        ));

        auto tryCandidate = [&](const glm::vec2& dir, bool desertBeachOnly) -> bool {
            for (float inward = 0.0f; inward <= inwardSearchMax; inward += inwardStep) {
                const float sampleRadius = edgeRadius - inward;
                if (sampleRadius < 8.0f) break;
                const float sx = islandCenter.x + dir.x * sampleRadius;
                const float sz = islandCenter.y + dir.y * sampleRadius;
                int surfaceY = 0;
                if (!isLandSurface(worldCtx, sx, sz, &surfaceY)) continue;
                if (desertBeachOnly) {
                    const int biomeID = ExpanseBiomeSystemLogic::ResolveBiome(worldCtx, sx, sz);
                    if (biomeID != 2) continue;
                    if (surfaceY > beachMaxSurfaceY) continue;
                }
                if (!hasNearbyOceanWater(worldCtx, sx, sz, shorelineProbeStep)) continue;
                outXZ->x = std::round(sx);
                outXZ->y = std::round(sz);
                *outSurfaceY = surfaceY;
                return true;
            }
            return false;
        };

        // Prefer the edge direction from configured spawn -> island center.
        for (int i = 0; i < angleSamples; ++i) {
            const float t = (angleSamples <= 1) ? 0.0f : (static_cast<float>(i) / static_cast<float>(angleSamples - 1));
            const float angle = (t * 2.0f - 1.0f) * maxAngleOffset;
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            const glm::vec2 dir(
                outwardDir.x * c - outwardDir.y * s,
                outwardDir.x * s + outwardDir.y * c
            );
            if (tryCandidate(dir, true)) return true;
        }

        // Desert-ring fallback: keep the player near the beach/desert transition if possible.
        const int desertRingSamples = 128;
        for (int i = 0; i < desertRingSamples; ++i) {
            const float angle = (static_cast<float>(i) / static_cast<float>(desertRingSamples)) * tau;
            const glm::vec2 dir(std::cos(angle), std::sin(angle));
            if (tryCandidate(dir, true)) return true;
        }

        // Full-ring fallback: pick any shoreline point if the desert shoreline is unavailable.
        const int ringSamples = 96;
        for (int i = 0; i < ringSamples; ++i) {
            const float angle = (static_cast<float>(i) / static_cast<float>(ringSamples)) * tau;
            const glm::vec2 dir(std::cos(angle), std::sin(angle));
            if (tryCandidate(dir, false)) return true;
        }
        return false;
    }

    bool isSolidVoxelId(const std::vector<Entity>& prototypes, uint32_t id) {
        if (id == 0) return false;
        int protoID = static_cast<int>(id);
        if (protoID < 0 || protoID >= static_cast<int>(prototypes.size())) return false;
        const Entity& proto = prototypes[protoID];
        if (!proto.isBlock || !proto.isSolid) return false;
        if (proto.name == "Water" || proto.name == "AudioVisualizer") return false;
        return true;
    }

    struct SpawnSupportHit {
        glm::ivec2 cellXZ{0};
        int topY = std::numeric_limits<int>::min();
    };

    bool findTopSolidAtCell(const BaseSystem& baseSystem,
                            const std::vector<Entity>& prototypes,
                            int cellX,
                            int cellZ,
                            int minY,
                            int maxY,
                            int* outTopSolidY) {
        if (!baseSystem.voxelWorld || !outTopSolidY) return false;
        for (int y = maxY; y >= minY; --y) {
            uint32_t id = baseSystem.voxelWorld->getBlockWorld(glm::ivec3(cellX, y, cellZ));
            if (isSolidVoxelId(prototypes, id)) {
                *outTopSolidY = y;
                return true;
            }
        }
        return false;
    }

    bool chooseSpawnSupport(const BaseSystem& baseSystem,
                            const std::vector<Entity>& prototypes,
                            const glm::vec3& spawnPosition,
                            int minY,
                            int maxY,
                            SpawnSupportHit* outHit) {
        if (!baseSystem.voxelWorld || !outHit) return false;
        int cx = static_cast<int>(std::floor(spawnPosition.x));
        int cz = static_cast<int>(std::floor(spawnPosition.z));

        // Prefer exact center support first.
        int centerTop = std::numeric_limits<int>::min();
        if (findTopSolidAtCell(baseSystem, prototypes, cx, cz, minY, maxY, &centerTop)) {
            outHit->cellXZ = glm::ivec2(cx, cz);
            outHit->topY = centerTop;
            return true;
        }

        // Fallback: nearest neighboring support in a 3x3 area.
        bool found = false;
        int bestDist2 = std::numeric_limits<int>::max();
        int bestY = std::numeric_limits<int>::min();
        glm::ivec2 bestCell(cx, cz);

        for (int z = cz - 1; z <= cz + 1; ++z) {
            for (int x = cx - 1; x <= cx + 1; ++x) {
                int topY = std::numeric_limits<int>::min();
                if (!findTopSolidAtCell(baseSystem, prototypes, x, z, minY, maxY, &topY)) continue;
                int dx = x - cx;
                int dz = z - cz;
                int dist2 = dx * dx + dz * dz;
                if (!found || dist2 < bestDist2 || (dist2 == bestDist2 && topY > bestY)) {
                    found = true;
                    bestDist2 = dist2;
                    bestY = topY;
                    bestCell = glm::ivec2(x, z);
                }
            }
        }

        if (!found) return false;
        outHit->cellXZ = bestCell;
        outHit->topY = bestY;
        return true;
    }

    int findPrototypeIDByName(const std::vector<Entity>& prototypes, const std::string& name) {
        for (size_t i = 0; i < prototypes.size(); ++i) {
            if (prototypes[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }

    uint32_t packWhiteColor() {
        return (255u << 16) | (255u << 8) | 255u;
    }

    bool canReplaceForSpawnLantern(const std::vector<Entity>& prototypes, uint32_t id) {
        if (id == 0u) return true;
        if (id >= prototypes.size()) return false;
        const Entity& proto = prototypes[static_cast<size_t>(id)];
        if (!proto.isBlock) return false;
        if (proto.name == "Water" || proto.name.rfind("WaterSlope", 0) == 0) return false;
        return !proto.isSolid;
    }

    void placeSpawnLanternCluster(BaseSystem& baseSystem,
                                  std::vector<Entity>& prototypes,
                                  const glm::ivec2& centerXZ,
                                  int nominalSurfaceY) {
        if (!baseSystem.voxelWorld) return;
        const int lanternPrototypeID = findPrototypeIDByName(prototypes, "LanternBlockTex");
        if (lanternPrototypeID < 0) return;

        static const std::array<glm::ivec2, 8> kLanternOffsets = {
            glm::ivec2(2, 0),
            glm::ivec2(-2, 0),
            glm::ivec2(0, 2),
            glm::ivec2(0, -2),
            glm::ivec2(3, 2),
            glm::ivec2(-3, -2),
            glm::ivec2(2, -3),
            glm::ivec2(-2, 3)
        };

        int placedCount = 0;
        for (const glm::ivec2& offset : kLanternOffsets) {
            const int cellX = centerXZ.x + offset.x;
            const int cellZ = centerXZ.y + offset.y;
            int supportY = std::numeric_limits<int>::min();
            if (!findTopSolidAtCell(baseSystem,
                                    prototypes,
                                    cellX,
                                    cellZ,
                                    nominalSurfaceY - 8,
                                    nominalSurfaceY + 8,
                                    &supportY)) {
                continue;
            }

            const glm::ivec3 placeCell(cellX, supportY + 1, cellZ);
            const uint32_t currentId = baseSystem.voxelWorld->getBlockWorld(placeCell);
            if (!canReplaceForSpawnLantern(prototypes, currentId)) continue;

            baseSystem.voxelWorld->setBlockWorld(
                placeCell,
                static_cast<uint32_t>(lanternPrototypeID),
                packWhiteColor()
            );
            VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, placeCell);
            placedCount += 1;
            if (placedCount >= 6) break;
        }
    }

    void placeSpawnLeafFanPreview(BaseSystem& baseSystem,
                                  std::vector<Entity>& prototypes,
                                  const glm::ivec2& centerXZ,
                                  int nominalSurfaceY,
                                  const std::string& spawnKey) {
        if (!baseSystem.voxelWorld) return;
        if (spawnKey != "frog_spawn") return;

        const int leafFanOakID = findPrototypeIDByName(prototypes, "GrassTuftLeafFanOak");
        const int leafFanPineID = findPrototypeIDByName(prototypes, "GrassTuftLeafFanPine");
        if (leafFanOakID < 0 && leafFanPineID < 0) return;

        // Keep previews in the air near spawn so they're easy to inspect and don't blend into terrain foliage.
        static const std::array<glm::ivec3, 2> kPreviewOffsets = {
            glm::ivec3(5, 11, -2),
            glm::ivec3(8, 11, -2)
        };

        auto placePreview = [&](int prototypeID, const glm::ivec3& offset) {
            if (prototypeID < 0) return;
            glm::ivec3 placeCell(
                centerXZ.x + offset.x,
                nominalSurfaceY + offset.y,
                centerXZ.y + offset.z
            );
            const uint32_t currentId = baseSystem.voxelWorld->getBlockWorld(placeCell);
            if (!canReplaceForSpawnLantern(prototypes, currentId)) return;
            baseSystem.voxelWorld->setBlockWorld(
                placeCell,
                static_cast<uint32_t>(prototypeID),
                packWhiteColor()
            );
            VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, placeCell);
        };

        placePreview(leafFanOakID, kPreviewOffsets[0]);
        placePreview(leafFanPineID, kPreviewOffsets[1]);
    }

    void placeSpawnSecurityCamera(BaseSystem& baseSystem,
                                  std::vector<Entity>& prototypes,
                                  const glm::ivec2& centerXZ,
                                  int nominalSurfaceY) {
        if (!baseSystem.level || !baseSystem.world) return;
        if (!getRegistryBool(baseSystem, "SpawnSecurityCameraEnabled", true)) return;

        const int cameraPrototypeID = findPrototypeIDByName(prototypes, "SecurityCamera");
        if (cameraPrototypeID < 0) return;

        int targetWorldIndex = baseSystem.level->activeWorldIndex;
        if (!baseSystem.world->expanse.terrainWorld.empty()) {
            for (size_t i = 0; i < baseSystem.level->worlds.size(); ++i) {
                if (baseSystem.level->worlds[i].name == baseSystem.world->expanse.terrainWorld) {
                    targetWorldIndex = static_cast<int>(i);
                    break;
                }
            }
        }
        if (targetWorldIndex < 0 || targetWorldIndex >= static_cast<int>(baseSystem.level->worlds.size())) return;
        Entity& world = baseSystem.level->worlds[static_cast<size_t>(targetWorldIndex)];
        const int targetCount = std::max(1, getRegistryInt(baseSystem, "SpawnSecurityCameraCount", 2));

        const glm::vec3 spawnCenter(
            static_cast<float>(centerXZ.x),
            static_cast<float>(nominalSurfaceY + 1),
            static_cast<float>(centerXZ.y)
        );
        int nearbyCount = 0;
        std::vector<glm::vec3> existingCameraPositions;
        existingCameraPositions.reserve(16);
        for (const EntityInstance& inst : world.instances) {
            if (inst.prototypeID != cameraPrototypeID) continue;
            existingCameraPositions.push_back(inst.position);
            if (glm::length(inst.position - spawnCenter) <= 42.0f) {
                nearbyCount += 1;
            }
        }
        if (nearbyCount >= targetCount) return;

        static const std::array<glm::ivec2, 24> kOffsets = {
            glm::ivec2(5, 0),
            glm::ivec2(-5, 0),
            glm::ivec2(0, 5),
            glm::ivec2(0, -5),
            glm::ivec2(6, 3),
            glm::ivec2(-6, -3),
            glm::ivec2(3, -6),
            glm::ivec2(-3, 6),
            glm::ivec2(8, 0),
            glm::ivec2(-8, 0),
            glm::ivec2(0, 8),
            glm::ivec2(0, -8),
            glm::ivec2(8, 4),
            glm::ivec2(-8, -4),
            glm::ivec2(4, -8),
            glm::ivec2(-4, 8),
            glm::ivec2(10, 6),
            glm::ivec2(-10, -6),
            glm::ivec2(6, -10),
            glm::ivec2(-6, 10),
            glm::ivec2(12, 0),
            glm::ivec2(-12, 0),
            glm::ivec2(0, 12),
            glm::ivec2(0, -12)
        };

        auto isInstanceOccupied = [&](const glm::ivec3& cell) -> bool {
            for (const EntityInstance& inst : world.instances) {
                if (glm::ivec3(glm::round(inst.position)) == cell) return true;
            }
            return false;
        };
        auto tooCloseToCamera = [&](const glm::vec3& position) -> bool {
            for (const glm::vec3& existingPos : existingCameraPositions) {
                if (glm::length(existingPos - position) < 4.5f) return true;
            }
            return false;
        };
        auto cellHasSolidVoxel = [&](const glm::ivec3& cell) -> bool {
            if (!baseSystem.voxelWorld) return false;
            const uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
            return isSolidVoxelId(prototypes, id);
        };

        int placedCount = 0;
        for (const glm::ivec2& offset : kOffsets) {
            if (nearbyCount + placedCount >= targetCount) break;
            const int cellX = centerXZ.x + offset.x;
            const int cellZ = centerXZ.y + offset.y;
            int supportY = std::numeric_limits<int>::min();
            if (!findTopSolidAtCell(baseSystem,
                                    prototypes,
                                    cellX,
                                    cellZ,
                                    nominalSurfaceY - 24,
                                    nominalSurfaceY + 24,
                                    &supportY)) {
                supportY = nominalSurfaceY - 1;
            }

            glm::ivec3 placeCell(cellX, supportY + 1, cellZ);
            bool foundPlacement = false;
            for (int yLift = 0; yLift <= 6; ++yLift) {
                const glm::ivec3 candidate(cellX, placeCell.y + yLift, cellZ);
                if (isInstanceOccupied(candidate)) continue;
                if (cellHasSolidVoxel(candidate)) continue;
                const glm::vec3 candidatePos = glm::vec3(candidate);
                if (tooCloseToCamera(candidatePos)) continue;
                placeCell = candidate;
                foundPlacement = true;
                break;
            }
            if (!foundPlacement) continue;

            EntityInstance cameraInst = HostLogic::CreateInstance(
                baseSystem,
                cameraPrototypeID,
                glm::vec3(placeCell),
                glm::vec3(1.0f)
            );
            if (cameraInst.instanceID <= 0) return;
            cameraInst.rotation = glm::degrees(std::atan2(
                static_cast<float>(centerXZ.y - placeCell.z),
                static_cast<float>(centerXZ.x - placeCell.x)
            ));
            world.instances.push_back(cameraInst);
            BlockSelectionSystemLogic::AddBlockToCache(
                baseSystem,
                prototypes,
                targetWorldIndex,
                cameraInst.position,
                cameraPrototypeID
            );
            existingCameraPositions.push_back(cameraInst.position);
            placedCount += 1;
        }
    }

    void SetPlayerSpawn(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt; (void)win;
        if (!baseSystem.player || !baseSystem.level) return;
        if (getRegistryBool(baseSystem, "DimensionArrivalPending", false)) return;
        if (getRegistryBool(baseSystem, "spawn_ready", false)) return;

        std::string spawnKey = baseSystem.level->spawnKey.empty() ? "frog_spawn" : baseSystem.level->spawnKey;

        SpawnConfig cfg;
        loadSpawnConfig(spawnKey, cfg);
        float surfaceY = cfg.position.y - 1.001f;
        if (baseSystem.world && baseSystem.world->expanse.loaded) {
            struct OceanEdgeSpawnCache {
                const WorldContext* world = nullptr;
                std::string spawnKey;
                bool attempted = false;
                bool found = false;
                glm::vec2 xz{0.0f};
                int surfaceY = 0;
            };
            static OceanEdgeSpawnCache oceanEdgeCache{};

            if (oceanEdgeCache.world != baseSystem.world.get() || oceanEdgeCache.spawnKey != spawnKey) {
                oceanEdgeCache = OceanEdgeSpawnCache{};
                oceanEdgeCache.world = baseSystem.world.get();
                oceanEdgeCache.spawnKey = spawnKey;
            }

            const bool preferOceanEdgeSpawn = getRegistryBool(baseSystem, "SpawnPreferOceanEdge", true);
            const bool useOceanEdgeSearch = preferOceanEdgeSpawn && (spawnKey == "frog_spawn");

            bool appliedOceanEdgeTarget = false;
            if (useOceanEdgeSearch) {
                if (!oceanEdgeCache.attempted) {
                    oceanEdgeCache.attempted = true;
                    oceanEdgeCache.found = findOceanEdgeSpawnTarget(baseSystem, cfg.position, &oceanEdgeCache.xz, &oceanEdgeCache.surfaceY);
                }
                if (oceanEdgeCache.found) {
                    cfg.position.x = oceanEdgeCache.xz.x;
                    cfg.position.z = oceanEdgeCache.xz.y;
                    surfaceY = static_cast<float>(oceanEdgeCache.surfaceY);
                    appliedOceanEdgeTarget = true;
                }
            }

            if (!appliedOceanEdgeTarget) {
                float height = 0.0f;
                bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(*baseSystem.world,
                                                                    cfg.position.x,
                                                                    cfg.position.z,
                                                                    height);
                surfaceY = isLand ? height : baseSystem.world->expanse.waterSurface;
            }
            // Collision uses half-height 1.0 and blocks are centered on integer coords.
            // Standing camera Y should be blockY + 1.5 (+ small skin).
            cfg.position.y = surfaceY + 1.501f;
        }

        PlayerContext& player = *baseSystem.player;
        player.cameraPosition = cfg.position;
        player.prevCameraPosition = cfg.position;
        player.cameraYaw = cfg.yaw;
        player.cameraPitch = cfg.pitch;
        player.verticalVelocity = 0.0f;
        player.onGround = false;

        bool useVoxelLOD = getRegistryBool(baseSystem, "useVoxelLOD", false);
        if (!useVoxelLOD) {
            if (baseSystem.registry) (*baseSystem.registry)["spawn_ready"] = true;
            return;
        }

        if (!baseSystem.world || !baseSystem.world->expanse.loaded) return;
        int nominalSurface = static_cast<int>(std::floor(surfaceY));
        SpawnSupportHit hit;
        if (!chooseSpawnSupport(baseSystem,
                                prototypes,
                                cfg.position,
                                nominalSurface - 3,
                                nominalSurface + 3,
                                &hit)) return;
        cfg.position.x = static_cast<float>(hit.cellXZ.x);
        cfg.position.z = static_cast<float>(hit.cellXZ.y);
        cfg.position.y = static_cast<float>(hit.topY) + 1.501f;
        player.cameraPosition = cfg.position;
        player.prevCameraPosition = cfg.position;
        placeSpawnLanternCluster(baseSystem, prototypes, hit.cellXZ, hit.topY);
        placeSpawnSecurityCamera(baseSystem, prototypes, hit.cellXZ, hit.topY);
        if (getRegistryBool(baseSystem, "SpawnBookPrototypeEnabled", true)) {
            BookSystemLogic::PlaceSpawnBookCluster(baseSystem, prototypes, hit.cellXZ, hit.topY);
        }
        placeSpawnLeafFanPreview(baseSystem, prototypes, hit.cellXZ, hit.topY, spawnKey);
        if (baseSystem.registry) (*baseSystem.registry)["spawn_ready"] = true;
    }
}
