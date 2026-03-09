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

    void SetPlayerSpawn(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt; (void)win;
        if (!baseSystem.player || !baseSystem.level) return;
        if (getRegistryBool(baseSystem, "spawn_ready", false)) return;

        std::string spawnKey = baseSystem.level->spawnKey.empty() ? "frog_spawn" : baseSystem.level->spawnKey;

        SpawnConfig cfg;
        loadSpawnConfig(spawnKey, cfg);
        float surfaceY = cfg.position.y - 1.001f;
        if (baseSystem.world && baseSystem.world->expanse.loaded) {
            float height = 0.0f;
            bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(*baseSystem.world,
                                                                cfg.position.x,
                                                                cfg.position.z,
                                                                height);
            surfaceY = isLand ? height : baseSystem.world->expanse.waterSurface;
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
        if (baseSystem.registry) (*baseSystem.registry)["spawn_ready"] = true;
    }
}
