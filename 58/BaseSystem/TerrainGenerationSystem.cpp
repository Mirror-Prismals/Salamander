#pragma once

#include <array>
#include <numeric>
#include <algorithm>
#include <unordered_map>
#include <random>
#include <cmath>
#include <chrono>
#include <glm/glm.hpp>

namespace HostLogic { const Entity* findPrototype(const std::string& name, const std::vector<Entity>& prototypes); EntityInstance CreateInstance(BaseSystem& baseSystem, int prototypeID, glm::vec3 position, glm::vec3 color); }

namespace TerrainSystemLogic {

    enum class TerrainMode { PerlinCube, Island };

    struct PerlinCubeConfig {
        glm::ivec3 dimensions{0};
        glm::vec3 origin{0.0f};
        float frequency = 0.15f;
        float threshold = 0.0f;
        std::string blockType = "Block";
        std::string color = "Grass";
        int seed = 42;
    };

    struct IslandConfig {
        glm::ivec2 size{0};
        glm::vec3 origin{0.0f};
        float seaLevel = 2.0f;
        float amplitude = 18.0f;
        float ridgeFactor = 4.0f;
        float continentalScale = 100.0f;
        float elevationScale = 40.0f;
        float ridgeScale = 18.0f;
        float threshold = 0.35f;
        float radius = 60.0f;
        float oceanDepth = 6.0f;
        int continentalSeed = 1;
        int elevationSeed = 2;
        int ridgeSeed = 3;
        std::string surfaceBlockType = "Block";
        std::string subsurfaceBlockType = "Block";
        std::string seabedBlockType = "Block";
        std::string waterBlockType = "Water";
        std::string surfaceColor = "Grass";
        std::string subsurfaceColor = "Soil";
        std::string seabedColor = "Sand";
        std::string waterColor = "Water";
    };

    struct TerrainConfig {
        TerrainMode mode = TerrainMode::PerlinCube;
        PerlinCubeConfig cube;
        IslandConfig island;
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

    using ConfigMap = std::unordered_map<std::string, TerrainConfig>;

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
                TerrainConfig config;
                std::string mode = cfg.value("mode", "perlin_cube");
                if (mode == "island") {
                    config.mode = TerrainMode::Island;
                    IslandConfig islandCfg;
                    if (cfg.contains("size")) {
                        auto s = cfg["size"];
                        islandCfg.size = glm::ivec2(s.at(0).get<int>(), s.at(1).get<int>());
                    }
                    if (cfg.contains("origin")) {
                        auto o = cfg["origin"];
                        islandCfg.origin = glm::vec3(o.at(0).get<float>(), o.at(1).get<float>(), o.at(2).get<float>());
                    }
                    islandCfg.seaLevel = cfg.value("seaLevel", islandCfg.seaLevel);
                    islandCfg.amplitude = cfg.value("amplitude", islandCfg.amplitude);
                    islandCfg.ridgeFactor = cfg.value("ridgeFactor", islandCfg.ridgeFactor);
                    islandCfg.continentalScale = cfg.value("continentalScale", islandCfg.continentalScale);
                    islandCfg.elevationScale = cfg.value("elevationScale", islandCfg.elevationScale);
                    islandCfg.ridgeScale = cfg.value("ridgeScale", islandCfg.ridgeScale);
                    islandCfg.threshold = cfg.value("threshold", islandCfg.threshold);
                    islandCfg.radius = cfg.value("radius", islandCfg.radius);
                    islandCfg.oceanDepth = cfg.value("oceanDepth", islandCfg.oceanDepth);
                    islandCfg.continentalSeed = cfg.value("continentalSeed", islandCfg.continentalSeed);
                    islandCfg.elevationSeed = cfg.value("elevationSeed", islandCfg.elevationSeed);
                    islandCfg.ridgeSeed = cfg.value("ridgeSeed", islandCfg.ridgeSeed);
                    islandCfg.surfaceBlockType = cfg.value("surfaceBlockType", islandCfg.surfaceBlockType);
                    islandCfg.subsurfaceBlockType = cfg.value("subsurfaceBlockType", islandCfg.subsurfaceBlockType);
                    islandCfg.seabedBlockType = cfg.value("seabedBlockType", islandCfg.seabedBlockType);
                    islandCfg.waterBlockType = cfg.value("waterBlockType", islandCfg.waterBlockType);
                    islandCfg.surfaceColor = cfg.value("surfaceColor", islandCfg.surfaceColor);
                    islandCfg.subsurfaceColor = cfg.value("subsurfaceColor", islandCfg.subsurfaceColor);
                    islandCfg.seabedColor = cfg.value("seabedColor", islandCfg.seabedColor);
                    islandCfg.waterColor = cfg.value("waterColor", islandCfg.waterColor);
                    config.island = islandCfg;
                } else {
                    config.mode = TerrainMode::PerlinCube;
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
                    config.cube = worldCfg;
                }
                map[worldName] = config;
            }
        } catch (const std::exception& e) {
            std::cerr << "TerrainGenerationSystem: failed to parse terrain.json (" << e.what() << ")" << std::endl;
            map.clear();
        }
        return map;
    }

    glm::vec3 GetColor(WorldContext& worldCtx, const std::string& name, const glm::vec3& fallback) {
        if (worldCtx.colorLibrary.count(name)) return worldCtx.colorLibrary[name];
        return fallback;
    }

    void GenerateCubeWorld(BaseSystem& baseSystem, std::vector<Entity>& prototypes, Entity& world, WorldContext& worldCtx, const PerlinCubeConfig& cfg) {
        if (cfg.dimensions.x <= 0 || cfg.dimensions.y <= 0 || cfg.dimensions.z <= 0) return;

        const Entity* blockProto = HostLogic::findPrototype(cfg.blockType, prototypes);
        if (!blockProto) {
            std::cerr << "TerrainGenerationSystem: missing block prototype '" << cfg.blockType << "' for world '" << world.name << "'" << std::endl;
            return;
        }

        glm::vec3 blockColor = GetColor(worldCtx, cfg.color, glm::vec3(0.8f, 0.5f, 0.9f));

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

    void GenerateIslandWorld(BaseSystem& baseSystem, std::vector<Entity>& prototypes, Entity& world, WorldContext& worldCtx, const IslandConfig& cfg) {
        if (cfg.size.x <= 0 || cfg.size.y <= 0) return;
        int halfWidth = cfg.size.x / 2;
        int halfDepth = cfg.size.y / 2;
        float usableRadius = cfg.radius > 0.0f ? cfg.radius : static_cast<float>(std::min(cfg.size.x, cfg.size.y)) * 0.5f;

        auto surfaceProto = HostLogic::findPrototype(cfg.surfaceBlockType, prototypes);
        auto subsurfaceProto = HostLogic::findPrototype(cfg.subsurfaceBlockType, prototypes);
        auto seabedProto = HostLogic::findPrototype(cfg.seabedBlockType, prototypes);
        auto waterProto = HostLogic::findPrototype(cfg.waterBlockType, prototypes);

        if (!surfaceProto || !subsurfaceProto || !seabedProto || !waterProto) {
            std::cerr << "TerrainGenerationSystem: missing block prototypes for island world '" << world.name << "'" << std::endl;
            return;
        }

        glm::vec3 surfaceColor = GetColor(worldCtx, cfg.surfaceColor, glm::vec3(0.2f, 0.8f, 0.2f));
        glm::vec3 subsurfaceColor = GetColor(worldCtx, cfg.subsurfaceColor, glm::vec3(0.33f, 0.22f, 0.15f));
        glm::vec3 seabedColor = GetColor(worldCtx, cfg.seabedColor, glm::vec3(0.9f, 0.85f, 0.6f));
        glm::vec3 waterColor = GetColor(worldCtx, cfg.waterColor, glm::vec3(0.05f, 0.2f, 0.5f));

        PerlinNoise3D continental(cfg.continentalSeed);
        PerlinNoise3D elevation(cfg.elevationSeed);
        PerlinNoise3D ridge(cfg.ridgeSeed);

        int seaLevelY = static_cast<int>(std::floor(cfg.origin.y + cfg.seaLevel));
        int oceanFloorY = seaLevelY - static_cast<int>(std::round(cfg.oceanDepth));

        for (int dx = -halfWidth; dx <= halfWidth; ++dx) {
            for (int dz = -halfDepth; dz <= halfDepth; ++dz) {
                float worldX = cfg.origin.x + static_cast<float>(dx);
                float worldZ = cfg.origin.z + static_cast<float>(dz);
                glm::vec2 radialVec(dx, dz);
                float radialFalloff = 1.0f - glm::clamp(glm::length(radialVec) / usableRadius, 0.0f, 1.0f);

                float continentalSample = (continental.noise(worldX / cfg.continentalScale, 0.0f, worldZ / cfg.continentalScale) + 1.0f) * 0.5f;
                float islandMask = glm::clamp(0.65f * radialFalloff + 0.35f * continentalSample, 0.0f, 1.0f);
                bool isLand = islandMask > cfg.threshold;

                if (!isLand) {
                    // seabed block
                    glm::vec3 bedPos(worldX, static_cast<float>(oceanFloorY), worldZ);
                    world.instances.push_back(HostLogic::CreateInstance(baseSystem, seabedProto->prototypeID, bedPos, seabedColor));
                    for (int y = oceanFloorY + 1; y <= seaLevelY; ++y) {
                        glm::vec3 waterPos(worldX, static_cast<float>(y), worldZ);
                        world.instances.push_back(HostLogic::CreateInstance(baseSystem, waterProto->prototypeID, waterPos, waterColor));
                    }
                    continue;
                }

                float landStrength = glm::clamp((islandMask - cfg.threshold) / (1.0f - cfg.threshold), 0.0f, 1.0f);
                float elevationSample = (elevation.noise(worldX / cfg.elevationScale, 0.0f, worldZ / cfg.elevationScale) + 1.0f) * 0.5f;
                float ridgeSample = ridge.noise(worldX / cfg.ridgeScale, 0.0f, worldZ / cfg.ridgeScale);
                float height = cfg.origin.y + cfg.seaLevel + landStrength * cfg.amplitude + ridgeSample * cfg.ridgeFactor + elevationSample * (cfg.amplitude * 0.5f);
                int surfaceY = std::max(seaLevelY, static_cast<int>(std::floor(height)));

                for (int y = oceanFloorY; y <= surfaceY; ++y) {
                    glm::vec3 pos(worldX, static_cast<float>(y), worldZ);
                    if (y == surfaceY) {
                        world.instances.push_back(HostLogic::CreateInstance(baseSystem, surfaceProto->prototypeID, pos, surfaceColor));
                    } else if (y >= surfaceY - 3) {
                        world.instances.push_back(HostLogic::CreateInstance(baseSystem, subsurfaceProto->prototypeID, pos, subsurfaceColor));
                    } else {
                        world.instances.push_back(HostLogic::CreateInstance(baseSystem, seabedProto->prototypeID, pos, seabedColor));
                    }
                }
            }
        }
    }

    void GenerateTerrain(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.level || !baseSystem.instance || !baseSystem.world) return;
        static ConfigMap configs = LoadConfigs();
        if (configs.empty()) return;

        WorldContext& worldCtx = *baseSystem.world;

        for (auto& world : baseSystem.level->worlds) {
            if (!configs.count(world.name)) continue;
            if (!world.instances.empty()) continue;

            const TerrainConfig& config = configs.at(world.name);
            auto start = std::chrono::steady_clock::now();
            size_t beforeCount = world.instances.size();
            if (config.mode == TerrainMode::PerlinCube) {
                GenerateCubeWorld(baseSystem, prototypes, world, worldCtx, config.cube);
            } else if (config.mode == TerrainMode::Island) {
                GenerateIslandWorld(baseSystem, prototypes, world, worldCtx, config.island);
            }
            size_t afterCount = world.instances.size();
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start
            ).count();
            std::cout << "TerrainGenerationSystem: world '" << world.name << "' generated "
                      << (afterCount - beforeCount) << " instances in "
                      << elapsedMs << " ms." << std::endl;
        }
    }
}
