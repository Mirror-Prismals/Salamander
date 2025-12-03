#pragma once

#include <array>
#include <numeric>
#include <algorithm>
#include <unordered_map>
#include <random>
#include <glm/glm.hpp>

namespace HostLogic { const Entity* findPrototype(const std::string& name, const std::vector<Entity>& prototypes); }

namespace TerrainSystemLogic {

    struct PerlinCubeConfig {
        glm::ivec3 dimensions{0};
        glm::vec3 origin{0.0f};
        float frequency = 0.15f;
        float threshold = 0.0f;
        std::string blockType = "Block";
        std::string color = "Grass";
        int seed = 42;
    };

    class PerlinNoise3D {
    public:
        explicit PerlinNoise3D(int seed) {
            std::iota(permutation.begin(), permutation.begin() + 256, 0);
            std::mt19937 rng(seed);
            std::shuffle(permutation.begin(), permutation.begin() + 256, rng);
            for (int i = 0; i < 256; ++i) permutation[256 + i] = permutation[i];
        }

        float noise(float x, float y, float z) const {
            int X = static_cast<int>(std::floor(x)) & 255;
            int Y = static_cast<int>(std::floor(y)) & 255;
            int Z = static_cast<int>(std::floor(z)) & 255;

            x -= std::floor(x);
            y -= std::floor(y);
            z -= std::floor(z);

            float u = fade(x);
            float v = fade(y);
            float w = fade(z);

            int A = permutation[X] + Y;
            int AA = permutation[A] + Z;
            int AB = permutation[A + 1] + Z;
            int B = permutation[X + 1] + Y;
            int BA = permutation[B] + Z;
            int BB = permutation[B + 1] + Z;

            float res = lerp(w,
                lerp(v,
                    lerp(u, grad(permutation[AA], x, y, z),
                            grad(permutation[BA], x - 1, y, z)),
                    lerp(u, grad(permutation[AB], x, y - 1, z),
                            grad(permutation[BB], x - 1, y - 1, z))),
                lerp(v,
                    lerp(u, grad(permutation[AA + 1], x, y, z - 1),
                            grad(permutation[BA + 1], x - 1, y, z - 1)),
                    lerp(u, grad(permutation[AB + 1], x, y - 1, z - 1),
                            grad(permutation[BB + 1], x - 1, y - 1, z - 1))));

            return res;
        }

    private:
        std::array<int, 512> permutation{};

        static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
        static float lerp(float t, float a, float b) { return a + t * (b - a); }
        static float grad(int hash, float x, float y, float z) {
            int h = hash & 15;
            float u = h < 8 ? x : y;
            float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
            float res = ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
            return res;
        }
    };

    using ConfigMap = std::unordered_map<std::string, PerlinCubeConfig>;

    ConfigMap LoadConfigs() {
        ConfigMap map;
        std::ifstream f("Procedures/terrain.json");
        if (!f.is_open()) {
            std::cerr << "TerrainGenerationSystem: Procedures/terrain.json not found." << std::endl;
            return map;
        }
        try {
            json data = json::parse(f);
            if (!data.contains("worlds") || !data["worlds"].is_object()) return map;
            for (auto& [worldName, cfg] : data["worlds"].items()) {
                PerlinCubeConfig worldCfg;
                if (cfg.contains("dimensions")) {
                    auto d = cfg["dimensions"];
                    worldCfg.dimensions = glm::ivec3(d.at(0).get<int>(), d.at(1).get<int>(), d.at(2).get<int>());
                }
                if (cfg.contains("origin")) {
                    auto o = cfg["origin"];
                    worldCfg.origin = glm::vec3(o.at(0).get<float>(), o.at(1).get<float>(), o.at(2).get<float>());
                }
                worldCfg.frequency = cfg.value("frequency", worldCfg.frequency);
                worldCfg.threshold = cfg.value("threshold", worldCfg.threshold);
                worldCfg.blockType = cfg.value("blockType", worldCfg.blockType);
                worldCfg.color = cfg.value("color", worldCfg.color);
                worldCfg.seed = cfg.value("seed", worldCfg.seed);
                map[worldName] = worldCfg;
            }
        } catch (const std::exception& e) {
            std::cerr << "TerrainGenerationSystem: failed to parse terrain.json (" << e.what() << ")" << std::endl;
            map.clear();
        }
        return map;
    }

    void GenerateTerrain(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.level || !baseSystem.instance || !baseSystem.world) return;
        static ConfigMap configs = LoadConfigs();
        if (configs.empty()) return;

        WorldContext& worldCtx = *baseSystem.world;

        for (auto& world : baseSystem.level->worlds) {
            if (!configs.count(world.name)) continue;
            if (!world.instances.empty()) continue;

            const PerlinCubeConfig& cfg = configs.at(world.name);
            if (cfg.dimensions.x <= 0 || cfg.dimensions.y <= 0 || cfg.dimensions.z <= 0) continue;

            const Entity* blockProto = HostLogic::findPrototype(cfg.blockType, prototypes);
            if (!blockProto) {
                std::cerr << "TerrainGenerationSystem: missing block prototype '" << cfg.blockType << "' for world '" << world.name << "'" << std::endl;
                continue;
            }

            glm::vec3 blockColor = worldCtx.colorLibrary.count(cfg.color)
                ? worldCtx.colorLibrary[cfg.color]
                : glm::vec3(0.8f, 0.5f, 0.9f);

            PerlinNoise3D noise(cfg.seed);
            for (int x = 0; x < cfg.dimensions.x; ++x) {
                for (int y = 0; y < cfg.dimensions.y; ++y) {
                    for (int z = 0; z < cfg.dimensions.z; ++z) {
                        float sample = noise.noise(
                            (cfg.origin.x + static_cast<float>(x)) * cfg.frequency,
                            (cfg.origin.y + static_cast<float>(y)) * cfg.frequency,
                            (cfg.origin.z + static_cast<float>(z)) * cfg.frequency
                        );
                        if (sample < cfg.threshold) continue;

                        glm::vec3 position = cfg.origin + glm::vec3(x, y, z);
                        world.instances.push_back(
                            HostLogic::CreateInstance(baseSystem, blockProto->prototypeID, position, blockColor)
                        );
                    }
                }
            }
        }
    }
}
