#pragma once
#include "../Host.h"

namespace SpawnSystemLogic {

    struct SpawnConfig {
        glm::vec3 position{0.0f, 4.0f, 0.0f};
        float yaw = -90.0f;
        float pitch = 0.0f;
    };

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

    void SetPlayerSpawn(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt; (void)win;
        if (!baseSystem.player || !baseSystem.level) return;

        std::string spawnKey = baseSystem.level->spawnKey.empty() ? "frog_spawn" : baseSystem.level->spawnKey;

        SpawnConfig cfg;
        loadSpawnConfig(spawnKey, cfg);

        PlayerContext& player = *baseSystem.player;
        player.cameraPosition = cfg.position;
        player.prevCameraPosition = cfg.position;
        player.cameraYaw = cfg.yaw;
        player.cameraPitch = cfg.pitch;
    }
}
