#pragma once

#include <array>
#include <numeric>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <random>
#include <cmath>
#include <chrono>
#include <limits>
#include <glm/glm.hpp>
#include "Structures/VoxelColumnModel.h"

namespace HostLogic { const Entity* findPrototype(const std::string& name, const std::vector<Entity>& prototypes); EntityInstance CreateInstance(BaseSystem& baseSystem, int prototypeID, glm::vec3 position, glm::vec3 color); }
namespace ExpanseBiomeSystemLogic {
    bool SampleTerrain(const WorldContext& worldCtx, float x, float z, float& outHeight);
    int ResolveBiome(const WorldContext& worldCtx, float x, float z);
}

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

    struct CaveField {
        bool ready = false;
        glm::vec3 origin{0.0f};
        int step = 4;
        int dimX = 0;
        int dimY = 0;
        int dimZ = 0;
        std::vector<uint8_t> a;
        std::vector<uint8_t> b;
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

    void GenerateTerrain(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
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

    namespace {
        int floorDivInt(int value, int divisor) {
            if (divisor <= 0) return 0;
            if (value >= 0) return value / divisor;
            return -(((-value) + divisor - 1) / divisor);
        }

        struct VoxelStreamingState {
            std::vector<VoxelSectionKey> pending;
            std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> pendingSet;
            std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> desired;
            std::vector<glm::ivec3> lastCenterSections;
            std::vector<int> lastRadii;
            uint64_t frameCounter = 0;
        };

        struct VoxelColumnStreamingState {
            std::unordered_set<VoxelColumnModel::ColumnKey, VoxelColumnModel::ColumnKeyHash> desired;
            std::vector<glm::ivec2> lastCenterColumns;
            std::vector<int> lastRadii;
            uint64_t frameCounter = 0;
        };

        struct VoxelSectionGenerationJob {
            int nextColumn = 0;
            bool wroteAny = false;
        };

        struct VoxelStreamingPerfStats {
            size_t pending = 0;
            size_t desired = 0;
            size_t generated = 0;
            size_t jobs = 0;
            int stepped = 0;
            int built = 0;
            int consumed = 0;
            int skippedExisting = 0;
            int filteredOut = 0;
            int rescueSurfaceQueued = 0;
            int rescueMissingQueued = 0;
            int droppedByCap = 0;
            int reprioritized = 0;
            float generationMs = 0.0f;
        };

        static VoxelStreamingState g_voxelStreaming;
        static VoxelColumnStreamingState g_voxelColumnStreaming;
        static std::unordered_map<VoxelSectionKey, VoxelSectionGenerationJob, VoxelSectionKeyHash> g_voxelSectionJobs;
        static VoxelStreamingPerfStats g_voxelStreamingPerfStats;
        static std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> g_voxelTerrainGenerated;
        static std::unordered_set<VoxelColumnModel::ColumnKey, VoxelColumnModel::ColumnKeyHash> g_voxelColumnSurfaceReady;
        static std::unordered_map<VoxelColumnModel::ColumnKey, int, VoxelColumnModel::ColumnKeyHash> g_voxelColumnDesiredMissing;
        static std::chrono::steady_clock::time_point g_lastVoxelPerf = std::chrono::steady_clock::now();
        static std::string g_voxelLevelKey;
        static CaveField g_caveField;
        

        void ensureCaveField(const ExpanseConfig& cfg, int requestedMinY, int requestedMaxY) {
            const int step = 4;
            const int targetMinY = std::min(requestedMinY, requestedMaxY);
            const int targetMaxY = std::max(requestedMinY, requestedMaxY);
            if (g_caveField.ready) {
                const int fieldMinY = static_cast<int>(std::floor(g_caveField.origin.y));
                const int fieldMaxY = fieldMinY + (g_caveField.dimY - 1) * g_caveField.step;
                if (fieldMinY <= targetMinY && fieldMaxY >= targetMaxY) {
                    return;
                }
            }
            const int sizeXZ = 2304;
            const int halfXZ = sizeXZ / 2;
            const int minY = targetMinY - step;
            const int maxY = targetMaxY + step;
            const int heightY = std::max(step, maxY - minY);
            g_caveField.step = step;
            g_caveField.origin = glm::vec3(-halfXZ, minY, -halfXZ);
            g_caveField.dimX = sizeXZ / step + 1;
            g_caveField.dimZ = sizeXZ / step + 1;
            g_caveField.dimY = heightY / step + 1;
            const size_t count = static_cast<size_t>(g_caveField.dimX)
                * static_cast<size_t>(g_caveField.dimY)
                * static_cast<size_t>(g_caveField.dimZ);
            g_caveField.a.assign(count, 0);
            g_caveField.b.assign(count, 0);

            PerlinNoise3D caveNoiseA(cfg.elevationSeed + 1337);
            PerlinNoise3D caveNoiseB(cfg.ridgeSeed + 7331);
            auto idx = [&](int x, int y, int z) {
                return (static_cast<size_t>(x) * g_caveField.dimY + static_cast<size_t>(y)) * g_caveField.dimZ + static_cast<size_t>(z);
            };
            for (int x = 0; x < g_caveField.dimX; ++x) {
                float wx = g_caveField.origin.x + static_cast<float>(x * step);
                for (int z = 0; z < g_caveField.dimZ; ++z) {
                    float wz = g_caveField.origin.z + static_cast<float>(z * step);
                    for (int y = 0; y < g_caveField.dimY; ++y) {
                        float wy = g_caveField.origin.y + static_cast<float>(y * step);
                        float v1 = (caveNoiseA.noise(wx / 64.0f, wy / 48.0f, wz / 64.0f) + 1.0f) * 0.5f;
                        float v2 = (caveNoiseB.noise(wx / 128.0f, wy / 128.0f, wz / 128.0f) + 1.0f) * 0.5f;
                        uint8_t q1 = static_cast<uint8_t>(std::clamp(v1, 0.0f, 1.0f) * 255.0f);
                        uint8_t q2 = static_cast<uint8_t>(std::clamp(v2, 0.0f, 1.0f) * 255.0f);
                        g_caveField.a[idx(x, y, z)] = q1;
                        g_caveField.b[idx(x, y, z)] = q2;
                    }
                }
            }
            g_caveField.ready = true;
            std::cout << "TerrainGeneration: precomputed cave field "
                      << g_caveField.dimX << "x" << g_caveField.dimY << "x" << g_caveField.dimZ
                      << " step=" << step << std::endl;
        }

        inline bool sampleCaveField(float worldX, float worldY, float worldZ, float& outA, float& outB) {
            if (!g_caveField.ready) return false;
            float fx = (worldX - g_caveField.origin.x) / static_cast<float>(g_caveField.step);
            float fy = (worldY - g_caveField.origin.y) / static_cast<float>(g_caveField.step);
            float fz = (worldZ - g_caveField.origin.z) / static_cast<float>(g_caveField.step);
            int ix = static_cast<int>(std::round(fx));
            int iy = static_cast<int>(std::round(fy));
            int iz = static_cast<int>(std::round(fz));
            if (ix < 0 || iy < 0 || iz < 0 || ix >= g_caveField.dimX || iy >= g_caveField.dimY || iz >= g_caveField.dimZ) {
                return false;
            }
            size_t idx = (static_cast<size_t>(ix) * g_caveField.dimY + static_cast<size_t>(iy)) * g_caveField.dimZ + static_cast<size_t>(iz);
            outA = static_cast<float>(g_caveField.a[idx]) / 255.0f;
            outB = static_cast<float>(g_caveField.b[idx]) / 255.0f;
            return true;
        }

        

        int findWorldIndexByName(const LevelContext& level, const std::string& name) {
            for (size_t i = 0; i < level.worlds.size(); ++i) {
                if (level.worlds[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        glm::ivec3 instanceToBlockPos(const EntityInstance& inst) {
            return glm::ivec3(
                static_cast<int>(std::round(inst.position.x)),
                static_cast<int>(std::round(inst.position.y)),
                static_cast<int>(std::round(inst.position.z))
            );
        }

        uint32_t packColor(const glm::vec3& color) {
            auto clampByte = [](float v) {
                int iv = static_cast<int>(std::round(v * 255.0f));
                if (iv < 0) iv = 0;
                if (iv > 255) iv = 255;
                return static_cast<uint32_t>(iv);
            };
            uint32_t r = clampByte(color.r);
            uint32_t g = clampByte(color.g);
            uint32_t b = clampByte(color.b);
            return (r << 16) | (g << 8) | b;
        }

        constexpr uint8_t kWaterWaveClassUnknown = 0u;
        constexpr uint8_t kWaterWaveClassPond = 1u;
        constexpr uint8_t kWaterWaveClassLake = 2u;
        constexpr uint8_t kWaterWaveClassRiver = 3u;
        constexpr uint8_t kWaterWaveClassOcean = 4u;
        constexpr uint8_t kWaterFoliageMarkerNone = 0u;
        constexpr uint8_t kWaterFoliageMarkerSandDollarX = 4u;
        constexpr uint8_t kWaterFoliageMarkerSandDollarZ = 5u;

        uint32_t withWaterWaveClass(uint32_t packedColorRgb, uint8_t waveClass) {
            const uint32_t rgb = packedColorRgb & 0x00ffffffu;
            const uint8_t encoded = static_cast<uint8_t>((waveClass & 0x0fu) << 4u);
            return rgb | (static_cast<uint32_t>(encoded) << 24u);
        }

        uint8_t waterWaveClassFromPackedColor(uint32_t packedColor) {
            const uint8_t encoded = static_cast<uint8_t>((packedColor >> 24) & 0xffu);
            const uint8_t marker = static_cast<uint8_t>(encoded & 0x0fu);
            const uint8_t waveClass = static_cast<uint8_t>((encoded >> 4u) & 0x0fu);
            if (marker <= kWaterFoliageMarkerSandDollarZ && waveClass <= kWaterWaveClassOcean) {
                return waveClass;
            }
            return kWaterWaveClassUnknown;
        }

        uint8_t waterFoliageMarkerFromPackedColor(uint32_t packedColor) {
            const uint8_t encoded = static_cast<uint8_t>((packedColor >> 24) & 0xffu);
            const uint8_t marker = static_cast<uint8_t>(encoded & 0x0fu);
            const uint8_t waveClass = static_cast<uint8_t>((encoded >> 4u) & 0x0fu);
            if (marker <= kWaterFoliageMarkerSandDollarZ && waveClass <= kWaterWaveClassOcean) {
                return marker;
            }
            return kWaterFoliageMarkerNone;
        }

        uint32_t withWaterFoliageMarker(uint32_t packedColor, uint8_t marker) {
            const uint32_t rgb = packedColor & 0x00ffffffu;
            const uint8_t waveClass = waterWaveClassFromPackedColor(packedColor);
            const uint8_t encoded = static_cast<uint8_t>(((waveClass & 0x0fu) << 4u) | (marker & 0x0fu));
            return rgb | (static_cast<uint32_t>(encoded) << 24u);
        }

        const Entity* findNonZeroBlockProto(const std::vector<Entity>& prototypes) {
            for (const auto& proto : prototypes) {
                if (!proto.isBlock) continue;
                if (proto.prototypeID == 0) continue;
                if (proto.name == "Water") continue;
                return &proto;
            }
            return nullptr;
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

        bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<bool>(it->second)) return fallback;
            return std::get<bool>(it->second);
        }

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

        std::string getRegistryString(const BaseSystem& baseSystem,
                                      const std::string& key,
                                      const std::string& fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            return std::get<std::string>(it->second);
        }

        uint32_t hash2DInt(int x, int z) {
            uint32_t ux = static_cast<uint32_t>(x) * 73856093u;
            uint32_t uz = static_cast<uint32_t>(z) * 19349663u;
            uint32_t h = ux ^ uz;
            h ^= (h >> 13);
            h *= 1274126177u;
            h ^= (h >> 16);
            return h;
        }

        uint32_t hash3DInt(int x, int y, int z) {
            uint32_t ux = static_cast<uint32_t>(x) * 73856093u;
            uint32_t uy = static_cast<uint32_t>(y) * 19349663u;
            uint32_t uz = static_cast<uint32_t>(z) * 83492791u;
            uint32_t h = ux ^ uy ^ uz;
            h ^= (h >> 13);
            h *= 1274126177u;
            h ^= (h >> 16);
            return h;
        }

        float hashToUnitFloat01(uint32_t h) {
            return static_cast<float>(h & 0x00ffffffu) / 16777215.0f;
        }

        int positiveMod(int value, int modulus) {
            if (modulus <= 0) return 0;
            const int m = value % modulus;
            return (m < 0) ? (m + modulus) : m;
        }

        float smoothstep01(float t) {
            t = std::clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float valueNoise2D(int seed, float x, float z) {
            const int ix = static_cast<int>(std::floor(x));
            const int iz = static_cast<int>(std::floor(z));
            const float fx = x - static_cast<float>(ix);
            const float fz = z - static_cast<float>(iz);
            const float u = smoothstep01(fx);
            const float v = smoothstep01(fz);
            auto sample = [&](int gx, int gz) -> float {
                return hashToUnitFloat01(hash2DInt(gx + seed * 37, gz - seed * 53));
            };
            const float n00 = sample(ix, iz);
            const float n10 = sample(ix + 1, iz);
            const float n01 = sample(ix, iz + 1);
            const float n11 = sample(ix + 1, iz + 1);
            const float nx0 = n00 + (n10 - n00) * u;
            const float nx1 = n01 + (n11 - n01) * u;
            return nx0 + (nx1 - nx0) * v;
        }

        float fbmValueNoise2D(int seed,
                              float x,
                              float z,
                              int octaves,
                              float lacunarity = 2.0f,
                              float gain = 0.5f) {
            const int oct = std::max(1, octaves);
            float sum = 0.0f;
            float amplitude = 1.0f;
            float frequency = 1.0f;
            float norm = 0.0f;
            for (int i = 0; i < oct; ++i) {
                sum += valueNoise2D(seed + i * 911, x * frequency, z * frequency) * amplitude;
                norm += amplitude;
                amplitude *= gain;
                frequency *= lacunarity;
            }
            if (norm <= 0.0f) return 0.0f;
            return sum / norm;
        }

        int sectionSizeForLod(const VoxelWorldContext& voxelWorld, int lod) {
            int size = voxelWorld.sectionSize >> lod;
            return size > 0 ? size : 1;
        }

        int computeExpanseMaxY(const BaseSystem& baseSystem,
                               const WorldContext& worldCtx,
                               const ExpanseConfig& cfg) {
            int maxY = static_cast<int>(std::ceil(cfg.baseElevation + cfg.mountainElevation));
            if (cfg.islandRadius > 0.0f) {
                // Island height uses a blended elevation + ridge term that can exceed islandNoiseAmp.
                // Use a conservative bound so high ridges never clip top sections out of streaming.
                maxY = static_cast<int>(std::ceil(cfg.waterSurface + cfg.islandMaxHeight + (cfg.islandNoiseAmp * 2.0f)));
            }
            maxY = std::max(maxY, static_cast<int>(std::ceil(cfg.waterSurface)));
            if (worldCtx.leyLines.enabled && worldCtx.leyLines.loaded) {
                float upliftBudget = std::max(0.0f, worldCtx.leyLines.upliftMax);
                if (worldCtx.leyLines.mountainLayerEnabled && worldCtx.leyLines.mountainLayerStrength > 0.0f) {
                    upliftBudget += upliftBudget * worldCtx.leyLines.mountainLayerStrength;
                }
                maxY += static_cast<int>(std::ceil(upliftBudget));
            }
            // Keep vertical streaming/generation range above terrain so tall pines can exist
            // on uplifted ridges without clipped tops or missing foliage passes.
            maxY += std::max(0, getRegistryInt(baseSystem, "ExpanseVerticalHeadroom", 48));
            maxY = std::max(maxY, getRegistryInt(baseSystem, "ExpanseAbsoluteMaxY", 320));
            return maxY;
        }

        glm::ivec3 floorDivVec(const glm::ivec3& v, int divisor) {
            return glm::ivec3(
                floorDivInt(v.x, divisor),
                floorDivInt(v.y, divisor),
                floorDivInt(v.z, divisor)
            );
        }

        bool GenerateExpanseSectionVoxel(BaseSystem& baseSystem,
                                         std::vector<Entity>& prototypes,
                                         WorldContext& worldCtx,
                                         const ExpanseConfig& cfg,
                                         int lod,
                                         const glm::ivec3& sectionCoord,
                                         int startColumn,
                                         int maxColumns,
                                         bool& inOutWroteAny,
                                         int& outNextColumn,
                                         bool& outCompleted) {
            if (!baseSystem.voxelWorld) {
                outNextColumn = startColumn;
                outCompleted = true;
                return false;
            }
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            int size = sectionSizeForLod(voxelWorld, lod);
            int scale = 1 << lod;
            const std::string currentLevel = getRegistryString(baseSystem, "level", "the_expanse");
            const bool isDepthLevel = (currentLevel == "the_depths");
            const bool isExpanseLevel = (currentLevel == "the_expanse");
            outNextColumn = startColumn;
            outCompleted = false;
            auto pickBlockProto = [&](std::initializer_list<const char*> names) -> const Entity* {
                for (const char* name : names) {
                    const Entity* proto = HostLogic::findPrototype(name, prototypes);
                    if (proto && proto->prototypeID != 0) return proto;
                }
                return findNonZeroBlockProto(prototypes);
            };
            const Entity* surfaceProtoConifer = pickBlockProto({"GrassBlockTex", "ScaffoldBlock"});
            const Entity* surfaceProtoMeadow = pickBlockProto({"GrassBlockMeadowTex", "GrassBlockTex", "ScaffoldBlock"});
            const Entity* surfaceProtoJungle = pickBlockProto({"GrassBlockJungleTex", "GrassBlockTex", "ScaffoldBlock"});
            const Entity* surfaceProtoBareWinter = pickBlockProto({"GrassBlockBareWinterTex", "GrassBlockTemperateTex", "GrassBlockTex", "ScaffoldBlock"});
            const Entity* surfaceProtoSnow = pickBlockProto({"SnowBlockTex", "GrassBlockTex", "ScaffoldBlock"});
            const Entity* sandDesertProto = pickBlockProto({"SandBlockDesertTex", "SandBlockTex", "ScaffoldBlock"});
            const Entity* sandDesertProto404 = pickBlockProto({"SandBlockDesertTex404", "SandBlockDesertTex", "SandBlockTex", "ScaffoldBlock"});
            const Entity* sandDesertProto405 = pickBlockProto({"SandBlockDesertTex405", "SandBlockDesertTex", "SandBlockTex", "ScaffoldBlock"});
            const Entity* sandDesertProto406 = pickBlockProto({"SandBlockDesertTex406", "SandBlockDesertTex", "SandBlockTex", "ScaffoldBlock"});
            const Entity* sandDesertProto407 = pickBlockProto({"SandBlockDesertTex407", "SandBlockDesertTex", "SandBlockTex", "ScaffoldBlock"});
            const Entity* sandDesertProto408 = pickBlockProto({"SandBlockDesertTex408", "SandBlockDesertTex", "SandBlockTex", "ScaffoldBlock"});
            const Entity* sandBeachProto = pickBlockProto({"SandBlockBeachTex", "SandBlockTex", "ScaffoldBlock"});
            const Entity* sandSeabedProto = pickBlockProto({"SandBlockSeabedTex", "SandBlockTex", "ScaffoldBlock"});
            const Entity* soilProto = pickBlockProto({"DirtBlockTex", "GrassBlockTex", "ScaffoldBlock"});
            const Entity* stoneProto = pickBlockProto({"StoneBlockTex", "ScaffoldBlock"});
            const Entity* depthStoneProto = pickBlockProto({"DepthStoneBlockTex", "StoneBlockTex", "ScaffoldBlock"});
            // Portals are disabled; depths are now a contiguous underground band in the expanse.
            const Entity* voidPortalProto = nullptr;
            const Entity* rubyOreProto = HostLogic::findPrototype("RubyOreTex", prototypes);
            const Entity* silverOreProto = HostLogic::findPrototype("SilverOreTex", prototypes);
            const Entity* amethystOreProto = HostLogic::findPrototype("AmethystOreTex", prototypes);
            const Entity* flouriteOreProto = HostLogic::findPrototype("FlouriteOreTex", prototypes);
            const Entity* graniteProto = HostLogic::findPrototype("GraniteBlockTex", prototypes);
            const Entity* chalkProto = HostLogic::findPrototype("ChalkBlockTex", prototypes);
            const Entity* clayProto = HostLogic::findPrototype("ClayBlockTex", prototypes);
            const Entity* chalkStickProtoX = HostLogic::findPrototype("StonePebbleChalkTexX", prototypes);
            const Entity* chalkStickProtoZ = HostLogic::findPrototype("StonePebbleChalkTexZ", prototypes);
            const Entity* waterProto = HostLogic::findPrototype("Water", prototypes);
            const Entity* waterSlopeProtoPosX = HostLogic::findPrototype("WaterSlopePosX", prototypes);
            const Entity* waterSlopeProtoNegX = HostLogic::findPrototype("WaterSlopeNegX", prototypes);
            const Entity* waterSlopeProtoPosZ = HostLogic::findPrototype("WaterSlopePosZ", prototypes);
            const Entity* waterSlopeProtoNegZ = HostLogic::findPrototype("WaterSlopeNegZ", prototypes);
            const Entity* waterSlopeCornerProtoPosXPosZ = HostLogic::findPrototype("WaterSlopeCornerPosXPosZ", prototypes);
            const Entity* waterSlopeCornerProtoPosXNegZ = HostLogic::findPrototype("WaterSlopeCornerPosXNegZ", prototypes);
            const Entity* waterSlopeCornerProtoNegXPosZ = HostLogic::findPrototype("WaterSlopeCornerNegXPosZ", prototypes);
            const Entity* waterSlopeCornerProtoNegXNegZ = HostLogic::findPrototype("WaterSlopeCornerNegXNegZ", prototypes);
            const Entity* obsidianProto = pickBlockProto({"ObsidianBlockTex", "StoneBlockTex", "ScaffoldBlock"});
            const std::array<const Entity*, 9> depthLavaTileProtos = {
                pickBlockProto({"DepthLavaTileR01C01", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"DepthLavaTileR01C02", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"DepthLavaTileR01C03", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"DepthLavaTileR02C01", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"DepthLavaTileR02C02", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"DepthLavaTileR02C03", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"DepthLavaTileR03C01", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"DepthLavaTileR03C02", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"DepthLavaTileR03C03", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"})
            };
            const std::array<const Entity*, 7> depthLodestoneOreProtos = {
                HostLogic::findPrototype("DepthLodestoneOreV001", prototypes),
                HostLogic::findPrototype("DepthLodestoneOreV002", prototypes),
                HostLogic::findPrototype("DepthLodestoneOreV003", prototypes),
                HostLogic::findPrototype("DepthLodestoneOreV004", prototypes),
                HostLogic::findPrototype("DepthLodestoneOreV005", prototypes),
                HostLogic::findPrototype("DepthLodestoneOreV006", prototypes),
                HostLogic::findPrototype("DepthLodestoneOreV007", prototypes)
            };
            const Entity* depthCopperSulfateOreProto = HostLogic::findPrototype("DepthCopperSulfateOreTex", prototypes);
            const Entity* depthPurpleDirtProto = HostLogic::findPrototype("DepthPurpleDirtTex", prototypes);
            const Entity* depthRustBeamProto = HostLogic::findPrototype("DepthRustBeamTex", prototypes);
            const std::array<const Entity*, 4> depthBigLilypadProtosX = {
                HostLogic::findPrototype("GrassCoverBigLilypadR01C01TexX", prototypes),
                HostLogic::findPrototype("GrassCoverBigLilypadR01C02TexX", prototypes),
                HostLogic::findPrototype("GrassCoverBigLilypadR02C01TexX", prototypes),
                HostLogic::findPrototype("GrassCoverBigLilypadR02C02TexX", prototypes)
            };
            const std::array<const Entity*, 4> depthBigLilypadProtosZ = {
                HostLogic::findPrototype("GrassCoverBigLilypadR01C01TexZ", prototypes),
                HostLogic::findPrototype("GrassCoverBigLilypadR01C02TexZ", prototypes),
                HostLogic::findPrototype("GrassCoverBigLilypadR02C01TexZ", prototypes),
                HostLogic::findPrototype("GrassCoverBigLilypadR02C02TexZ", prototypes)
            };
            const Entity* depthMossWallProtoPosX = HostLogic::findPrototype("DepthMossWallTexPosX", prototypes);
            const Entity* depthMossWallProtoNegX = HostLogic::findPrototype("DepthMossWallTexNegX", prototypes);
            const Entity* depthMossWallProtoPosZ = HostLogic::findPrototype("DepthMossWallTexPosZ", prototypes);
            const Entity* depthMossWallProtoNegZ = HostLogic::findPrototype("DepthMossWallTexNegZ", prototypes);
            const Entity* depthCrystalProto = HostLogic::findPrototype("DepthCrystalClusterTex", prototypes);
            const Entity* depthCrystalBlueProto = HostLogic::findPrototype("DepthCrystalClusterBlueTex", prototypes);
            const Entity* depthCrystalBlueBigProto = HostLogic::findPrototype("DepthCrystalClusterBlueBigTex", prototypes);
            const Entity* depthCrystalMagentaBigProto = HostLogic::findPrototype("DepthCrystalClusterMagentaBigTex", prototypes);
            if (!surfaceProtoConifer || !waterProto) {
                outNextColumn = startColumn;
                outCompleted = true;
                return false;
            }
            if (!surfaceProtoMeadow) surfaceProtoMeadow = surfaceProtoConifer;
            if (!surfaceProtoJungle) surfaceProtoJungle = surfaceProtoConifer;
            if (!surfaceProtoBareWinter) surfaceProtoBareWinter = surfaceProtoConifer;
            if (!surfaceProtoSnow) surfaceProtoSnow = surfaceProtoConifer;
            if (!sandDesertProto) sandDesertProto = surfaceProtoConifer;
            std::array<const Entity*, 5> sandDesertVariantProtos = {
                sandDesertProto404,
                sandDesertProto405,
                sandDesertProto406,
                sandDesertProto407,
                sandDesertProto408
            };
            for (const Entity*& variant : sandDesertVariantProtos) {
                if (!variant) variant = sandDesertProto;
            }
            if (!sandBeachProto) sandBeachProto = sandDesertProto;
            if (!sandSeabedProto) sandSeabedProto = sandBeachProto;
            if (!soilProto) soilProto = surfaceProtoConifer;
            if (!stoneProto) stoneProto = surfaceProtoConifer;
            if (isDepthLevel && depthStoneProto) {
                stoneProto = depthStoneProto;
                soilProto = depthStoneProto;
            }

            if (lod == 0) {
                int caveMinY = -96;
                if (isDepthLevel) {
                    caveMinY = std::min(caveMinY, cfg.minY - 8);
                }
                if (isExpanseLevel && getRegistryBool(baseSystem, "UnifiedDepthsEnabled", true)) {
                    caveMinY = std::min(
                        caveMinY,
                        getRegistryInt(baseSystem, "UnifiedDepthsMinY", -200) - 8
                    );
                }
                const int caveMaxY = std::max(
                    192,
                    static_cast<int>(std::ceil(cfg.waterSurface + cfg.islandMaxHeight + (cfg.islandNoiseAmp * 2.0f)))
                );
                ensureCaveField(cfg, caveMinY, caveMaxY);
            }

            glm::vec3 grassColor = GetColor(worldCtx, cfg.colorGrass, glm::vec3(0.2f, 0.8f, 0.2f));
            glm::vec3 sandColor = GetColor(worldCtx, cfg.colorSand, glm::vec3(0.9f, 0.8f, 0.4f));
            glm::vec3 snowColor = GetColor(worldCtx, cfg.colorSnow, glm::vec3(0.93f, 0.94f, 0.96f));
            glm::vec3 soilColor = GetColor(worldCtx, cfg.colorSoil, glm::vec3(0.33f, 0.22f, 0.15f));
            glm::vec3 stoneColor = GetColor(worldCtx, cfg.colorStone, glm::vec3(0.4f, 0.4f, 0.4f));
            glm::vec3 waterColor = GetColor(worldCtx, cfg.colorWater, glm::vec3(0.05f, 0.2f, 0.5f));
            glm::vec3 lavaColor = GetColor(worldCtx, cfg.colorLava, glm::vec3(0.96f, 0.33f, 0.08f));
            glm::vec3 seabedColor = GetColor(worldCtx, cfg.colorSeabed, sandColor);
            const uint32_t packedWaterColorOcean = withWaterWaveClass(packColor(waterColor), kWaterWaveClassOcean);
            const uint32_t packedWaterColorLake = withWaterWaveClass(packColor(waterColor), kWaterWaveClassLake);
            const uint32_t packedWaterColorPond = withWaterWaveClass(packColor(waterColor), kWaterWaveClassPond);
            const uint32_t packedWaterColorRiver = withWaterWaveClass(packColor(waterColor), kWaterWaveClassRiver);
            const uint32_t packedWaterColorUnknown = withWaterWaveClass(packColor(waterColor), kWaterWaveClassUnknown);
            const glm::vec3 graniteColor = glm::vec3(1.0f, 1.0f, 1.0f);
            const glm::vec3 chalkColor = glm::vec3(0.94f, 0.95f, 0.92f);
            const glm::vec3 clayColor = glm::vec3(1.0f, 1.0f, 1.0f);
            const bool islandQuadrants = (cfg.islandRadius > 0.0f) && cfg.secondaryBiomeEnabled;
            const float volcanoCenterFactorX = std::clamp(cfg.jungleVolcanoCenterFactorX, 0.0f, 1.0f);
            const float volcanoCenterFactorZ = std::clamp(cfg.jungleVolcanoCenterFactorZ, 0.0f, 1.0f);
            const float volcanoCenterX = cfg.islandCenterX + cfg.islandRadius * volcanoCenterFactorX;
            const float volcanoCenterZ = cfg.islandCenterZ + cfg.islandRadius * volcanoCenterFactorZ;
            const float volcanoOuterRadius = std::max(8.0f, cfg.jungleVolcanoOuterRadius);
            const float volcanoCraterRadius = std::clamp(cfg.jungleVolcanoCraterRadius, 4.0f, volcanoOuterRadius * 0.95f);
            const float volcanoLavaRadius = volcanoCraterRadius;
            const float volcanoPeakY = cfg.waterSurface + cfg.islandMaxHeight + cfg.jungleVolcanoHeight;
            const float volcanoCraterFloorY = volcanoPeakY - std::max(0.0f, cfg.jungleVolcanoCraterDepth);
            const int volcanoLavaSurfaceY = static_cast<int>(std::floor(
                volcanoCraterFloorY + std::max(2.0f, cfg.jungleVolcanoCraterDepth * 0.08f)
            ));
            const int volcanoLavaDepth = std::max(
                64,
                static_cast<int>(std::round(std::max(24.0f, cfg.jungleVolcanoCraterDepth * 1.05f)))
            );
            const int volcanoChamberDepth = std::max(
                48,
                static_cast<int>(std::round(std::max(20.0f, cfg.jungleVolcanoCraterDepth * 0.9f)))
            );
            const float volcanoChamberRadius = std::max(8.0f, volcanoCraterRadius * 0.74f);
            const std::array<const Entity*, 4> oreProtos = {rubyOreProto, silverOreProto, amethystOreProto, flouriteOreProto};
            const std::array<uint32_t, 4> oreColors = {
                packColor(glm::vec3(0.78f, 0.19f, 0.22f)), // ruby
                packColor(glm::vec3(0.72f, 0.74f, 0.78f)), // silver
                packColor(glm::vec3(0.67f, 0.44f, 0.82f)), // amethyst
                packColor(glm::vec3(0.38f, 0.78f, 0.62f))  // flourite / fluorite
            };
            const bool oreEnabled = getRegistryBool(baseSystem, "OreGenerationEnabled", true);
            const int oreSeed = getRegistryInt(baseSystem, "OreGenerationSeed", 4242);
            const int oreVeinCellSize = std::max(6, getRegistryInt(baseSystem, "OreVeinCellSize", 14));
            const float oreVeinRadiusMin = std::max(0.5f, getRegistryFloat(baseSystem, "OreVeinRadiusMin", 2.0f));
            const float oreVeinRadiusMax = std::max(oreVeinRadiusMin, getRegistryFloat(baseSystem, "OreVeinRadiusMax", 4.5f));
            const float oreVeinChance = glm::clamp(getRegistryFloat(baseSystem, "OreBaseChance", 0.18f), 0.0f, 1.0f);
            const float oreSoilReplaceChance = glm::clamp(getRegistryFloat(baseSystem, "OreSoilReplaceChance", 0.45f), 0.0f, 1.0f);
            const float oreStoneReplaceChance = glm::clamp(getRegistryFloat(baseSystem, "OreStoneReplaceChance", 0.60f), 0.0f, 1.0f);
            const int oreMinDepthFromSurface = std::max(1, getRegistryInt(baseSystem, "OreMinDepthFromSurface", 4));
            const float oreCaveAdjacencyBoost = glm::clamp(getRegistryFloat(baseSystem, "OreCaveAdjacencyBoost", 0.35f), 0.0f, 1.0f);
            const bool chalkEnabled = getRegistryBool(baseSystem, "ChalkGenerationEnabled", true);
            const int chalkSeed = getRegistryInt(baseSystem, "ChalkGenerationSeed", 9185);
            const int chalkVeinCellSize = std::max(5, getRegistryInt(baseSystem, "ChalkVeinCellSize", 12));
            const float chalkVeinRadiusMin = std::max(0.5f, getRegistryFloat(baseSystem, "ChalkVeinRadiusMin", 1.5f));
            const float chalkVeinRadiusMax = std::max(chalkVeinRadiusMin, getRegistryFloat(baseSystem, "ChalkVeinRadiusMax", 3.5f));
            const float chalkVeinChance = glm::clamp(getRegistryFloat(baseSystem, "ChalkVeinChance", 0.28f), 0.0f, 1.0f);
            const float chalkReplaceChance = glm::clamp(getRegistryFloat(baseSystem, "ChalkReplaceChance", 0.80f), 0.0f, 1.0f);
            const int chalkStickSpawnPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "ChalkStickSpawnPercent", 18)));
            const bool clayEnabled = getRegistryBool(baseSystem, "ClayGenerationEnabled", true);
            const int claySeed = getRegistryInt(baseSystem, "ClayGenerationSeed", 12553);
            const int clayVeinCellSize = std::max(5, getRegistryInt(baseSystem, "ClayVeinCellSize", 12));
            const float clayVeinRadiusMin = std::max(0.5f, getRegistryFloat(baseSystem, "ClayVeinRadiusMin", 1.5f));
            const float clayVeinRadiusMax = std::max(clayVeinRadiusMin, getRegistryFloat(baseSystem, "ClayVeinRadiusMax", 3.5f));
            const float clayVeinChance = glm::clamp(getRegistryFloat(baseSystem, "ClayVeinChance", 0.28f), 0.0f, 1.0f);
            const float clayReplaceChance = glm::clamp(getRegistryFloat(baseSystem, "ClayReplaceChance", 0.80f), 0.0f, 1.0f);
            const bool graniteEnabled = getRegistryBool(baseSystem, "GraniteGenerationEnabled", true);
            const int graniteSeed = getRegistryInt(baseSystem, "GraniteGenerationSeed", 10273);
            const int graniteVeinCellSize = std::max(5, getRegistryInt(baseSystem, "GraniteVeinCellSize", 12));
            const float graniteVeinRadiusMin = std::max(0.5f, getRegistryFloat(baseSystem, "GraniteVeinRadiusMin", 1.5f));
            const float graniteVeinRadiusMax = std::max(graniteVeinRadiusMin, getRegistryFloat(baseSystem, "GraniteVeinRadiusMax", 3.5f));
            const float graniteVeinChance = glm::clamp(getRegistryFloat(baseSystem, "GraniteVeinChance", 0.28f), 0.0f, 1.0f);
            const float graniteReplaceChance = glm::clamp(getRegistryFloat(baseSystem, "GraniteReplaceChance", 0.80f), 0.0f, 1.0f);
            const bool surfaceRiverEnabled = getRegistryBool(baseSystem, "SurfaceRiverGenerationEnabled", true);
            const int surfaceRiverSeed = getRegistryInt(baseSystem, "SurfaceRiverSeed", 2701);
            const float surfaceRiverScale = std::max(32.0f, getRegistryFloat(baseSystem, "SurfaceRiverScale", 180.0f));
            const float surfaceRiverWarpScale = std::max(16.0f, getRegistryFloat(baseSystem, "SurfaceRiverWarpScale", 72.0f));
            const float surfaceRiverWarpStrength = std::max(0.0f, getRegistryFloat(baseSystem, "SurfaceRiverWarpStrength", 58.0f));
            const float surfaceRiverThresholdMin = glm::clamp(getRegistryFloat(baseSystem, "SurfaceRiverThresholdMin", 0.045f), 0.001f, 0.45f);
            const float surfaceRiverThresholdMax = glm::clamp(
                getRegistryFloat(baseSystem, "SurfaceRiverThresholdMax", 0.085f),
                surfaceRiverThresholdMin,
                0.75f
            );
            const int surfaceRiverDepthMin = std::max(1, getRegistryInt(baseSystem, "SurfaceRiverDepthMin", 3));
            const int surfaceRiverDepthMax = std::max(surfaceRiverDepthMin, getRegistryInt(baseSystem, "SurfaceRiverDepthMax", 9));
            const int surfaceRiverMinAboveSea = std::max(1, getRegistryInt(baseSystem, "SurfaceRiverMinAboveSea", 2));
            const int surfaceRiverChannelLowerMin = std::max(0, getRegistryInt(baseSystem, "SurfaceRiverChannelLowerMin", 3));
            const int surfaceRiverChannelLowerMax = std::max(
                surfaceRiverChannelLowerMin,
                getRegistryInt(baseSystem, "SurfaceRiverChannelLowerMax", 5)
            );
            const int surfaceRiverWaterlineExtraLower = std::max(
                0,
                getRegistryInt(baseSystem, "SurfaceRiverWaterlineExtraLower", 3)
            );
            const float surfaceRiverDepthMultiplier = std::max(
                1.0f,
                getRegistryFloat(baseSystem, "SurfaceRiverDepthMultiplier", 3.0f)
            );
            const bool waterfallEnabled = getRegistryBool(baseSystem, "WaterfallGenerationEnabled", true);
            const int waterfallMaxDrop = std::max(1, getRegistryInt(baseSystem, "WaterfallMaxDrop", 96));
            const int waterfallCascadeBudget = std::max(16, getRegistryInt(baseSystem, "WaterfallCascadeBudget", 2048));
            const bool surfaceLakeEnabled = (!isDepthLevel)
                && getRegistryBool(baseSystem, "SurfaceLakeGenerationEnabled", true);
            const int surfaceLakeSeed = getRegistryInt(baseSystem, "SurfaceLakeSeed", 9103);
            const int surfaceLakeCellSize = std::max(48, getRegistryInt(baseSystem, "SurfaceLakeCellSize", 360));
            const float surfaceLakeChance = glm::clamp(getRegistryFloat(baseSystem, "SurfaceLakeChance", 0.08f), 0.0f, 1.0f);
            const float surfaceLakeRadiusMin = std::max(6.0f, getRegistryFloat(baseSystem, "SurfaceLakeRadiusMin", 50.0f));
            const float surfaceLakeRadiusMax = std::max(surfaceLakeRadiusMin, getRegistryFloat(baseSystem, "SurfaceLakeRadiusMax", 150.0f));
            const int surfaceLakeDepthMin = std::max(2, getRegistryInt(baseSystem, "SurfaceLakeDepthMin", 24));
            const int surfaceLakeDepthMax = std::max(surfaceLakeDepthMin, getRegistryInt(baseSystem, "SurfaceLakeDepthMax", 34));
            const int surfaceLakeDepthExtra = std::max(0, getRegistryInt(baseSystem, "SurfaceLakeDepthExtra", 10));
            const int surfaceLakeMinAboveSea = std::max(1, getRegistryInt(baseSystem, "SurfaceLakeMinAboveSea", 4));
            const int surfaceLakeChannelLower = std::max(0, getRegistryInt(baseSystem, "SurfaceLakeChannelLower", 3));
            const bool surfacePondEnabled = (!isDepthLevel)
                && getRegistryBool(baseSystem, "SurfacePondGenerationEnabled", true);
            const int surfacePondSeed = getRegistryInt(baseSystem, "SurfacePondSeed", 1337);
            const int surfacePondCellSize = std::max(24, getRegistryInt(baseSystem, "SurfacePondCellSize", 40));
            const float surfacePondChance = glm::clamp(getRegistryFloat(baseSystem, "SurfacePondChance", 0.70f), 0.0f, 1.0f);
            const float surfacePondRadiusMin = std::max(2.0f, getRegistryFloat(baseSystem, "SurfacePondRadiusMin", 10.0f));
            const float surfacePondRadiusMax = std::max(surfacePondRadiusMin, getRegistryFloat(baseSystem, "SurfacePondRadiusMax", 18.0f));
            const int surfacePondDepthMin = std::max(1, getRegistryInt(baseSystem, "SurfacePondDepthMin", 3));
            const int surfacePondDepthMax = std::max(surfacePondDepthMin, getRegistryInt(baseSystem, "SurfacePondDepthMax", 7));
            const int surfacePondMinAboveSea = std::max(1, getRegistryInt(baseSystem, "SurfacePondMinAboveSea", 1));
            const int surfacePondChannelLower = std::max(0, getRegistryInt(baseSystem, "SurfacePondChannelLower", 3));
            struct PondCellInfo {
                bool valid = false;
                bool centerIsLand = false;
                int centerSurfaceY = 0;
                float centerX = 0.0f;
                float centerZ = 0.0f;
                float radius = 0.0f;
                int depth = 0;
            };
            std::unordered_map<uint64_t, PondCellInfo> pondCellCache;
            auto pondCellKey = [](int cellX, int cellZ) -> uint64_t {
                return (static_cast<uint64_t>(static_cast<uint32_t>(cellX)) << 32)
                    | static_cast<uint64_t>(static_cast<uint32_t>(cellZ));
            };
            std::unordered_map<uint64_t, PondCellInfo> lakeCellCache;
            auto lakeCellInfoFor = [&](int cellX, int cellZ) -> const PondCellInfo& {
                const uint64_t key = pondCellKey(cellX, cellZ);
                auto found = lakeCellCache.find(key);
                if (found != lakeCellCache.end()) {
                    return found->second;
                }
                PondCellInfo info;
                if (surfaceLakeEnabled) {
                    const uint32_t seed = hash2DInt(cellX + surfaceLakeSeed * 61, cellZ - surfaceLakeSeed * 47);
                    const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
                    if (chanceRoll <= surfaceLakeChance) {
                        const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                        const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                        info.centerX = (static_cast<float>(cellX) + offsetX) * static_cast<float>(surfaceLakeCellSize);
                        info.centerZ = (static_cast<float>(cellZ) + offsetZ) * static_cast<float>(surfaceLakeCellSize);
                        const uint32_t radiusSeed = hash2DInt(cellX * 139 + surfaceLakeSeed, cellZ * 191 - surfaceLakeSeed);
                        const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                        info.radius = surfaceLakeRadiusMin + (surfaceLakeRadiusMax - surfaceLakeRadiusMin) * radiusT;
                        const uint32_t depthSeed = hash2DInt(cellX * 331 + surfaceLakeSeed * 7, cellZ * 587 - surfaceLakeSeed * 11);
                        const float depthT = static_cast<float>((depthSeed >> 8u) & 0xffu) / 255.0f;
                        info.depth = surfaceLakeDepthMin
                            + static_cast<int>(std::round((surfaceLakeDepthMax - surfaceLakeDepthMin) * depthT));
                        float centerHeight = 0.0f;
                        info.centerIsLand = ExpanseBiomeSystemLogic::SampleTerrain(
                            worldCtx,
                            info.centerX,
                            info.centerZ,
                            centerHeight
                        );
                        info.centerSurfaceY = static_cast<int>(std::floor(centerHeight));
                        info.valid = true;
                    }
                }
                auto inserted = lakeCellCache.emplace(key, info);
                return inserted.first->second;
            };
            auto pondCellInfoFor = [&](int cellX, int cellZ) -> const PondCellInfo& {
                const uint64_t key = pondCellKey(cellX, cellZ);
                auto found = pondCellCache.find(key);
                if (found != pondCellCache.end()) {
                    return found->second;
                }
                PondCellInfo info;
                if (surfacePondEnabled) {
                    const uint32_t seed = hash2DInt(cellX + surfacePondSeed * 37, cellZ - surfacePondSeed * 53);
                    const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
                    if (chanceRoll <= surfacePondChance) {
                        const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                        const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                        info.centerX = (static_cast<float>(cellX) + offsetX) * static_cast<float>(surfacePondCellSize);
                        info.centerZ = (static_cast<float>(cellZ) + offsetZ) * static_cast<float>(surfacePondCellSize);
                        const uint32_t radiusSeed = hash2DInt(cellX * 131 + surfacePondSeed, cellZ * 173 - surfacePondSeed);
                        const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                        info.radius = surfacePondRadiusMin + (surfacePondRadiusMax - surfacePondRadiusMin) * radiusT;
                        const uint32_t depthSeed = hash2DInt(cellX * 313 + surfacePondSeed * 7, cellZ * 571 - surfacePondSeed * 11);
                        const float depthT = static_cast<float>((depthSeed >> 8u) & 0xffu) / 255.0f;
                        info.depth = surfacePondDepthMin
                            + static_cast<int>(std::round((surfacePondDepthMax - surfacePondDepthMin) * depthT));
                        float centerHeight = 0.0f;
                        info.centerIsLand = ExpanseBiomeSystemLogic::SampleTerrain(
                            worldCtx,
                            info.centerX,
                            info.centerZ,
                            centerHeight
                        );
                        info.centerSurfaceY = static_cast<int>(std::floor(centerHeight));
                        info.valid = true;
                    }
                }
                auto inserted = pondCellCache.emplace(key, info);
                return inserted.first->second;
            };
            auto oreVariantForColumn = [&](int worldXi, int worldZi) -> int {
                if (!oreEnabled) return -1;
                const int cellX = floorDivInt(worldXi, oreVeinCellSize);
                const int cellZ = floorDivInt(worldZi, oreVeinCellSize);
                int bestVariant = -1;
                float bestDist2 = std::numeric_limits<float>::max();
                for (int oz = -1; oz <= 1; ++oz) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int vx = cellX + ox;
                        const int vz = cellZ + oz;
                        const uint32_t seed = hash2DInt(vx + oreSeed * 17, vz - oreSeed * 23);
                        const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
                        if (chanceRoll > oreVeinChance) continue;
                        const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                        const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                        const float centerX = (static_cast<float>(vx) + offsetX) * static_cast<float>(oreVeinCellSize);
                        const float centerZ = (static_cast<float>(vz) + offsetZ) * static_cast<float>(oreVeinCellSize);
                        const uint32_t radiusSeed = hash2DInt(vx * 131 + oreSeed, vz * 197 - oreSeed);
                        const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                        const float radius = oreVeinRadiusMin + (oreVeinRadiusMax - oreVeinRadiusMin) * radiusT;
                        const float dx = static_cast<float>(worldXi) - centerX;
                        const float dz = static_cast<float>(worldZi) - centerZ;
                        const float dist2 = dx * dx + dz * dz;
                        if (dist2 > radius * radius) continue;
                        if (dist2 < bestDist2) {
                            bestDist2 = dist2;
                            const uint32_t oreSeedValue = hash2DInt(vx * 313 + oreSeed * 7, vz * 571 - oreSeed * 11);
                            bestVariant = static_cast<int>((oreSeedValue >> 5u) & 0x3u);
                        }
                    }
                }
                return bestVariant;
            };
            auto chalkVeinForColumn = [&](int worldXi, int worldZi) -> bool {
                if (!chalkEnabled || !chalkProto) return false;
                const int cellX = floorDivInt(worldXi, chalkVeinCellSize);
                const int cellZ = floorDivInt(worldZi, chalkVeinCellSize);
                float bestDist2 = std::numeric_limits<float>::max();
                bool inVein = false;
                for (int oz = -1; oz <= 1; ++oz) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int vx = cellX + ox;
                        const int vz = cellZ + oz;
                        const uint32_t seed = hash2DInt(vx + chalkSeed * 29, vz - chalkSeed * 31);
                        const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
                        if (chanceRoll > chalkVeinChance) continue;
                        const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                        const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                        const float centerX = (static_cast<float>(vx) + offsetX) * static_cast<float>(chalkVeinCellSize);
                        const float centerZ = (static_cast<float>(vz) + offsetZ) * static_cast<float>(chalkVeinCellSize);
                        const uint32_t radiusSeed = hash2DInt(vx * 157 + chalkSeed, vz * 271 - chalkSeed);
                        const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                        const float radius = chalkVeinRadiusMin + (chalkVeinRadiusMax - chalkVeinRadiusMin) * radiusT;
                        const float dx = static_cast<float>(worldXi) - centerX;
                        const float dz = static_cast<float>(worldZi) - centerZ;
                        const float dist2 = dx * dx + dz * dz;
                        if (dist2 > radius * radius) continue;
                        if (!inVein || dist2 < bestDist2) {
                            inVein = true;
                            bestDist2 = dist2;
                        }
                    }
                }
                return inVein;
            };
            auto graniteVeinForColumn = [&](int worldXi, int worldZi) -> bool {
                if (!graniteEnabled || !graniteProto) return false;
                const int cellX = floorDivInt(worldXi, graniteVeinCellSize);
                const int cellZ = floorDivInt(worldZi, graniteVeinCellSize);
                float bestDist2 = std::numeric_limits<float>::max();
                bool inVein = false;
                for (int oz = -1; oz <= 1; ++oz) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int vx = cellX + ox;
                        const int vz = cellZ + oz;
                        const uint32_t seed = hash2DInt(vx + graniteSeed * 37, vz - graniteSeed * 41);
                        const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
                        if (chanceRoll > graniteVeinChance) continue;
                        const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                        const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                        const float centerX = (static_cast<float>(vx) + offsetX) * static_cast<float>(graniteVeinCellSize);
                        const float centerZ = (static_cast<float>(vz) + offsetZ) * static_cast<float>(graniteVeinCellSize);
                        const uint32_t radiusSeed = hash2DInt(vx * 163 + graniteSeed, vz * 283 - graniteSeed);
                        const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                        const float radius = graniteVeinRadiusMin + (graniteVeinRadiusMax - graniteVeinRadiusMin) * radiusT;
                        const float dx = static_cast<float>(worldXi) - centerX;
                        const float dz = static_cast<float>(worldZi) - centerZ;
                        const float dist2 = dx * dx + dz * dz;
                        if (dist2 > radius * radius) continue;
                        if (!inVein || dist2 < bestDist2) {
                            inVein = true;
                            bestDist2 = dist2;
                        }
                    }
                }
                return inVein;
            };
            auto clayVeinForColumn = [&](int worldXi, int worldZi) -> bool {
                if (!clayEnabled || !clayProto) return false;
                const int cellX = floorDivInt(worldXi, clayVeinCellSize);
                const int cellZ = floorDivInt(worldZi, clayVeinCellSize);
                float bestDist2 = std::numeric_limits<float>::max();
                bool inVein = false;
                for (int oz = -1; oz <= 1; ++oz) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int vx = cellX + ox;
                        const int vz = cellZ + oz;
                        const uint32_t seed = hash2DInt(vx + claySeed * 29, vz - claySeed * 31);
                        const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
                        if (chanceRoll > clayVeinChance) continue;
                        const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                        const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                        const float centerX = (static_cast<float>(vx) + offsetX) * static_cast<float>(clayVeinCellSize);
                        const float centerZ = (static_cast<float>(vz) + offsetZ) * static_cast<float>(clayVeinCellSize);
                        const uint32_t radiusSeed = hash2DInt(vx * 157 + claySeed, vz * 271 - claySeed);
                        const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                        const float radius = clayVeinRadiusMin + (clayVeinRadiusMax - clayVeinRadiusMin) * radiusT;
                        const float dx = static_cast<float>(worldXi) - centerX;
                        const float dz = static_cast<float>(worldZi) - centerZ;
                        const float dist2 = dx * dx + dz * dz;
                        if (dist2 > radius * radius) continue;
                        if (!inVein || dist2 < bestDist2) {
                            inVein = true;
                            bestDist2 = dist2;
                        }
                    }
                }
                return inVein;
            };
            auto caveCarvedAt = [&](float sampleX, float sampleY, float sampleZ, int surfaceY, bool allowCaves) -> bool {
                if (!allowCaves) return false;
                if (sampleY > static_cast<float>(surfaceY)) return false;
                float v1 = 0.0f;
                float v2 = 0.0f;
                if (!sampleCaveField(sampleX, sampleY, sampleZ, v1, v2)) return false;
                // Match cave carving profile used for land so ore can bias toward visible cave walls.
                float depth = static_cast<float>(surfaceY) - sampleY;
                if (depth <= 3.0f) return false;
                float t = std::clamp(depth / 24.0f, 0.0f, 1.0f);
                float thrA = 0.72f + (0.62f - 0.72f) * t;
                float thrB = 0.68f + (0.58f - 0.68f) * t;
                if (isDepthLevel) {
                    thrA -= 0.10f;
                    thrB -= 0.10f;
                }
                return (v1 > thrA) || (v2 > thrB);
            };

            int waterSurfaceY = static_cast<int>(std::floor(cfg.waterSurface));
            int waterFloorY = static_cast<int>(std::floor(cfg.waterFloor));
            const int waterFloorYLower = waterFloorY - 1;
            const int seabedPortalY = waterFloorY - 2;
            // Hard split for expanse generation: legacy expanse cave stone must never appear below -98.
            const int expanseDepthSplitY = isExpanseLevel ? -98 : seabedPortalY;
            const int depthPortalY = cfg.minY + 1;
            const bool unifiedDepthsEnabled = isExpanseLevel && getRegistryBool(baseSystem, "UnifiedDepthsEnabled", true);
            const int unifiedDepthsTopY = expanseDepthSplitY - 1;
            const int unifiedDepthsMinY = std::min(
                unifiedDepthsTopY,
                getRegistryInt(baseSystem, "UnifiedDepthsMinY", -200)
            );
            const int unifiedDepthRiverFloorGuard = std::max(
                1,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverFloorGuard", 2)
            );
            const int unifiedDepthRiverRoofGuard = std::max(
                1,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverRoofGuard", 2)
            );
            const int unifiedDepthRiverSeed = getRegistryInt(baseSystem, "UnifiedDepthsRiverSeed", 9117);
            const float unifiedDepthRiverScale = std::max(24.0f, getRegistryFloat(baseSystem, "UnifiedDepthsRiverScale", 128.0f));
            const float unifiedDepthRiverTerrainScale = std::max(
                24.0f,
                getRegistryFloat(baseSystem, "UnifiedDepthsRiverTerrainScale", 220.0f)
            );
            const float unifiedDepthRiverTerrainRelief = std::max(
                0.0f,
                getRegistryFloat(baseSystem, "UnifiedDepthsRiverTerrainRelief", 24.0f)
            );
            const float unifiedDepthRiverThresholdMin = glm::clamp(
                getRegistryFloat(baseSystem, "UnifiedDepthsRiverThresholdMin", 0.038f),
                0.004f,
                0.35f
            );
            const float unifiedDepthRiverThresholdMax = glm::clamp(
                getRegistryFloat(baseSystem, "UnifiedDepthsRiverThresholdMax", 0.085f),
                unifiedDepthRiverThresholdMin,
                0.55f
            );
            const int unifiedDepthRiverMinWaterY = unifiedDepthsMinY + unifiedDepthRiverFloorGuard + 2;
            const int unifiedDepthRiverMaxWaterY = unifiedDepthsTopY - unifiedDepthRiverRoofGuard - 1;
            const bool unifiedDepthRiverBandValid = (unifiedDepthRiverMinWaterY <= unifiedDepthRiverMaxWaterY);
            const int unifiedDepthRiverWaterY = unifiedDepthRiverBandValid
                ? std::clamp(
                    getRegistryInt(baseSystem, "UnifiedDepthsRiverWaterY", -150),
                    unifiedDepthRiverMinWaterY,
                    unifiedDepthRiverMaxWaterY
                )
                : unifiedDepthsMinY + 2;
            const int unifiedDepthRiverDepthMin = std::max(2, getRegistryInt(baseSystem, "UnifiedDepthsRiverDepthMin", 3));
            const int unifiedDepthRiverDepthMax = std::max(
                unifiedDepthRiverDepthMin,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverDepthMax", 9)
            );
            const float unifiedDepthRiverDepthMultiplier = std::max(
                1.0f,
                getRegistryFloat(baseSystem, "UnifiedDepthsRiverDepthMultiplier", 3.0f)
            );
            const int unifiedDepthRiverChannelLowerMin = std::max(
                0,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverChannelLowerMin", 2)
            );
            const int unifiedDepthRiverChannelLowerMax = std::max(
                unifiedDepthRiverChannelLowerMin,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverChannelLowerMax", 6)
            );
            const int unifiedDepthRiverSeatExtra = std::max(
                0,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverSeatExtra", 10)
            );
            const int unifiedDepthRiverWaterlineExtraLower = std::max(
                0,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverWaterlineExtraLower", 3)
            );
            const int unifiedDepthRiverCeilingCarveMin = std::max(
                0,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverCeilingCarveMin", 10)
            );
            const int unifiedDepthRiverCeilingCarveMax = std::max(
                unifiedDepthRiverCeilingCarveMin,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverCeilingCarveMax", 15)
            );
            const bool depthLavaFloorEnabled = getRegistryBool(baseSystem, "DepthLavaFloorEnabled", true);
            const int depthLavaTopY = getRegistryInt(baseSystem, "DepthLavaTopY", -195);
            const int depthLavaBottomY = getRegistryInt(baseSystem, "DepthLavaBottomY", -199);
            const int depthLodestoneSeed = getRegistryInt(baseSystem, "DepthLodestoneSeed", 12037);
            const int depthLodestoneVeinCellSize = std::max(6, getRegistryInt(baseSystem, "DepthLodestoneVeinCellSize", 16));
            const float depthLodestoneVeinChance = glm::clamp(getRegistryFloat(baseSystem, "DepthLodestoneVeinChance", 0.23f), 0.0f, 1.0f);
            const float depthLodestoneReplaceChance = glm::clamp(getRegistryFloat(baseSystem, "DepthLodestoneReplaceChance", 0.42f), 0.0f, 1.0f);
            const float depthLodestoneRadiusMin = std::max(0.5f, getRegistryFloat(baseSystem, "DepthLodestoneRadiusMin", 2.0f));
            const float depthLodestoneRadiusMax = std::max(depthLodestoneRadiusMin, getRegistryFloat(baseSystem, "DepthLodestoneRadiusMax", 4.5f));
            const float depthLodestoneVerticalRadiusScale = glm::clamp(
                getRegistryFloat(baseSystem, "DepthLodestoneVerticalRadiusScale", 0.68f),
                0.2f,
                3.0f
            );
            const int depthCopperSeed = getRegistryInt(baseSystem, "DepthCopperSulfateSeed", 17011);
            const int depthCopperVeinCellSize = std::max(6, getRegistryInt(baseSystem, "DepthCopperSulfateVeinCellSize", 14));
            const float depthCopperVeinChance = glm::clamp(getRegistryFloat(baseSystem, "DepthCopperSulfateVeinChance", 0.17f), 0.0f, 1.0f);
            const float depthCopperReplaceChance = glm::clamp(getRegistryFloat(baseSystem, "DepthCopperSulfateReplaceChance", 0.34f), 0.0f, 1.0f);
            const float depthCopperRadiusMin = std::max(0.5f, getRegistryFloat(baseSystem, "DepthCopperSulfateRadiusMin", 1.8f));
            const float depthCopperRadiusMax = std::max(depthCopperRadiusMin, getRegistryFloat(baseSystem, "DepthCopperSulfateRadiusMax", 3.6f));
            const float depthCopperVerticalRadiusScale = glm::clamp(
                getRegistryFloat(baseSystem, "DepthCopperSulfateVerticalRadiusScale", 0.68f),
                0.2f,
                3.0f
            );
            const int depthOreCenterYPadding = std::max(
                0,
                getRegistryInt(baseSystem, "DepthOreCenterYPadding", 2)
            );
            const int depthPurplePatchPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "DepthPurplePatchPercent", 16)));
            const int depthRustBeamSeed = getRegistryInt(baseSystem, "DepthRustBeamSeed", 24611);
            const int depthRustBeamPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "DepthRustBeamPercent", 4)));
            const int depthMossPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "DepthMossPercent", 9)));
            const int depthBigLilypadPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "DepthBigLilypadPercent", 3)));
            const int depthRiverCrystalPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "DepthRiverCrystalPercent", 85)));
            const int depthRiverCrystalBankSearchDown = std::max(1, getRegistryInt(baseSystem, "DepthRiverCrystalBankSearchDown", 40));
            const int depthRiverCrystalBankSearchRadius = std::max(0, std::min(4, getRegistryInt(baseSystem, "DepthRiverCrystalBankSearchRadius", 2)));
            auto depthVeinCenterY = [&](uint32_t seedValue) -> int {
                const int minCenterY = unifiedDepthsMinY + depthOreCenterYPadding;
                const int maxCenterY = unifiedDepthsTopY - depthOreCenterYPadding;
                if (maxCenterY <= minCenterY) {
                    return (unifiedDepthsMinY + unifiedDepthsTopY) / 2;
                }
                const uint32_t span = static_cast<uint32_t>(maxCenterY - minCenterY + 1);
                return minCenterY + static_cast<int>(seedValue % span);
            };
            auto depthLodestoneVariantAt = [&](int worldXi, int worldYi, int worldZi) -> int {
                if (!unifiedDepthsEnabled) return -1;
                const int cellX = floorDivInt(worldXi, depthLodestoneVeinCellSize);
                const int cellZ = floorDivInt(worldZi, depthLodestoneVeinCellSize);
                int bestVariant = -1;
                float bestScore = std::numeric_limits<float>::max();
                for (int oz = -1; oz <= 1; ++oz) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int vx = cellX + ox;
                        const int vz = cellZ + oz;
                        const uint32_t seed = hash2DInt(vx + depthLodestoneSeed * 19, vz - depthLodestoneSeed * 23);
                        const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
                        if (chanceRoll > depthLodestoneVeinChance) continue;
                        const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                        const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                        const float centerX = (static_cast<float>(vx) + offsetX) * static_cast<float>(depthLodestoneVeinCellSize);
                        const float centerZ = (static_cast<float>(vz) + offsetZ) * static_cast<float>(depthLodestoneVeinCellSize);
                        const uint32_t radiusSeed = hash2DInt(vx * 179 + depthLodestoneSeed, vz * 211 - depthLodestoneSeed);
                        const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                        const float radius = depthLodestoneRadiusMin + (depthLodestoneRadiusMax - depthLodestoneRadiusMin) * radiusT;
                        const float radiusY = std::max(1.0f, radius * depthLodestoneVerticalRadiusScale);
                        const uint32_t ySeed = hash2DInt(
                            vx * 241 + depthLodestoneSeed * 13,
                            vz * 337 - depthLodestoneSeed * 17
                        );
                        const int centerY = depthVeinCenterY(ySeed);
                        const float dx = static_cast<float>(worldXi) - centerX;
                        const float dz = static_cast<float>(worldZi) - centerZ;
                        const float dy = static_cast<float>(worldYi - centerY);
                        const float score = (dx * dx + dz * dz) / std::max(0.0001f, radius * radius)
                            + (dy * dy) / std::max(0.0001f, radiusY * radiusY);
                        if (score > 1.0f) continue;
                        if (score < bestScore) {
                            bestScore = score;
                            const uint32_t variantSeed = hash2DInt(vx * 313 + depthLodestoneSeed * 7, vz * 571 - depthLodestoneSeed * 11);
                            bestVariant = static_cast<int>(variantSeed % static_cast<uint32_t>(depthLodestoneOreProtos.size()));
                        }
                    }
                }
                return bestVariant;
            };
            auto depthCopperVeinAt = [&](int worldXi, int worldYi, int worldZi) -> bool {
                if (!unifiedDepthsEnabled) return false;
                const int cellX = floorDivInt(worldXi, depthCopperVeinCellSize);
                const int cellZ = floorDivInt(worldZi, depthCopperVeinCellSize);
                float bestScore = std::numeric_limits<float>::max();
                bool inVein = false;
                for (int oz = -1; oz <= 1; ++oz) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int vx = cellX + ox;
                        const int vz = cellZ + oz;
                        const uint32_t seed = hash2DInt(vx + depthCopperSeed * 17, vz - depthCopperSeed * 29);
                        const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
                        if (chanceRoll > depthCopperVeinChance) continue;
                        const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                        const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                        const float centerX = (static_cast<float>(vx) + offsetX) * static_cast<float>(depthCopperVeinCellSize);
                        const float centerZ = (static_cast<float>(vz) + offsetZ) * static_cast<float>(depthCopperVeinCellSize);
                        const uint32_t radiusSeed = hash2DInt(vx * 137 + depthCopperSeed, vz * 263 - depthCopperSeed);
                        const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                        const float radius = depthCopperRadiusMin + (depthCopperRadiusMax - depthCopperRadiusMin) * radiusT;
                        const float radiusY = std::max(1.0f, radius * depthCopperVerticalRadiusScale);
                        const uint32_t ySeed = hash2DInt(
                            vx * 223 + depthCopperSeed * 19,
                            vz * 311 - depthCopperSeed * 23
                        );
                        const int centerY = depthVeinCenterY(ySeed);
                        const float dx = static_cast<float>(worldXi) - centerX;
                        const float dz = static_cast<float>(worldZi) - centerZ;
                        const float dy = static_cast<float>(worldYi - centerY);
                        const float score = (dx * dx + dz * dz) / std::max(0.0001f, radius * radius)
                            + (dy * dy) / std::max(0.0001f, radiusY * radiusY);
                        if (score > 1.0f) continue;
                        if (!inVein || score < bestScore) {
                            inVein = true;
                            bestScore = score;
                        }
                    }
                }
                return inVein;
            };
            int sectionMinY = sectionCoord.y * size * scale;
            int sectionMaxY = sectionMinY + size * scale - 1;
            int minY = std::min(cfg.minY, waterFloorY);
            if (isExpanseLevel) {
                minY = std::min(minY, seabedPortalY);
                if (unifiedDepthsEnabled) {
                    minY = std::min(minY, unifiedDepthsMinY);
                }
            } else if (isDepthLevel) {
                minY = std::min(minY, depthPortalY);
            }
            if (islandQuadrants && cfg.jungleVolcanoEnabled) {
                const int volcanoChamberBottomY = volcanoLavaSurfaceY - volcanoLavaDepth - volcanoChamberDepth;
                minY = std::min(minY, volcanoChamberBottomY - 8);
            }
            if (lod == 0) {
                minY = std::min(minY, -96);
            }
            int maxY = computeExpanseMaxY(baseSystem, worldCtx, cfg);
            const int totalColumns = size * size;
            if (sectionMaxY < minY || sectionMinY > maxY) {
                outNextColumn = totalColumns;
                outCompleted = true;
                return true;
            }

            int clampedStartColumn = std::max(0, std::min(startColumn, totalColumns));
            int clampedEndColumn = totalColumns;
            if (maxColumns > 0) {
                clampedEndColumn = std::min(totalColumns, clampedStartColumn + maxColumns);
            }
            bool wroteAny = false;
            std::vector<glm::ivec3> pendingChalkPlacements;
            pendingChalkPlacements.reserve(static_cast<size_t>(totalColumns));
            for (int column = clampedStartColumn; column < clampedEndColumn; ++column) {
                int z = column / size;
                int x = column - z * size;
                    float worldX = static_cast<float>((sectionCoord.x * size + x) * scale);
                    float worldZ = static_cast<float>((sectionCoord.z * size + z) * scale);
                    const int worldXi = static_cast<int>(std::floor(worldX));
                    const int worldZi = static_cast<int>(std::floor(worldZ));
                    float height = 0.0f;
                    bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(worldCtx, worldX, worldZ, height);
                    int surfaceY = static_cast<int>(std::floor(height));
                    if (isDepthLevel) {
                        // Depth dimension is intentionally all cave-terrain land columns;
                        // do not generate expanse-style seabed/ocean bands here.
                        isLand = true;
                    }
                    bool isBeach = isLand && (surfaceY <= waterSurfaceY + static_cast<int>(cfg.beachHeight));
                    const int biomeID = ExpanseBiomeSystemLogic::ResolveBiome(worldCtx, worldX, worldZ);
                    const Entity* biomeSurfaceProto = surfaceProtoConifer;
                    glm::vec3 biomeSurfaceColor = grassColor;
                    if (biomeID == 1) {
                        biomeSurfaceProto = surfaceProtoMeadow;
                        biomeSurfaceColor = glm::mix(grassColor, glm::vec3(0.24f, 0.86f, 0.22f), 0.45f);
                    } else if (biomeID == 2) {
                        biomeSurfaceProto = sandDesertProto;
                        biomeSurfaceColor = sandColor;
                    } else if (biomeID == 3) {
                        if (cfg.islandRadius > 0.0f) {
                            biomeSurfaceProto = surfaceProtoJungle;
                            biomeSurfaceColor = glm::mix(grassColor, glm::vec3(0.13f, 0.56f, 0.20f), 0.35f);
                        } else {
                            biomeSurfaceProto = surfaceProtoSnow;
                            biomeSurfaceColor = snowColor;
                        }
                    } else if (biomeID == 4) {
                        biomeSurfaceProto = surfaceProtoBareWinter;
                        biomeSurfaceColor = glm::mix(glm::vec3(0.43f, 0.40f, 0.34f), grassColor, 0.15f);
                    }
                    float volcanoDist = std::numeric_limits<float>::max();
                    bool volcanoArea = false;
                    bool craterArea = false;
                    if (islandQuadrants && cfg.jungleVolcanoEnabled && biomeID == 3) {
                        const float volcanoDx = worldX - volcanoCenterX;
                        const float volcanoDz = worldZ - volcanoCenterZ;
                        volcanoDist = std::sqrt(volcanoDx * volcanoDx + volcanoDz * volcanoDz);
                        volcanoArea = (volcanoDist <= volcanoOuterRadius);
                        craterArea = (volcanoDist <= volcanoCraterRadius);
                    }
                    if (volcanoArea) {
                        // Volcano footprint uses rock instead of grass/soil surfaces.
                        biomeSurfaceProto = stoneProto;
                        biomeSurfaceColor = glm::mix(stoneColor, glm::vec3(0.12f, 0.10f, 0.08f), 0.35f);
                        isBeach = false;
                    }
                    const Entity* topSurfaceProto = isBeach ? sandBeachProto : biomeSurfaceProto;
                    if (biomeID == 2 && !isBeach) {
                        const uint32_t desertVariantSeed = hash2DInt(worldXi + 4049, worldZi - 8081);
                        const size_t desertVariantIndex = static_cast<size_t>(
                            desertVariantSeed % static_cast<uint32_t>(sandDesertVariantProtos.size())
                        );
                        topSurfaceProto = sandDesertVariantProtos[desertVariantIndex];
                    }
                    if (isDepthLevel) {
                        isBeach = false;
                        biomeSurfaceProto = stoneProto;
                        biomeSurfaceColor = stoneColor;
                        topSurfaceProto = stoneProto;
                    }
                    bool lavaColumnActive = false;
                    bool chamberCarveColumn = false;
                    int lavaFillMinY = 0;
                    int lavaSurfaceY = volcanoLavaSurfaceY;
                    int lavaBottomY = lavaSurfaceY - volcanoLavaDepth + 1;
                    int chamberBottomY = lavaBottomY - volcanoChamberDepth;
                    if (craterArea) {
                        lavaFillMinY = lavaBottomY;
                        lavaColumnActive = true;
                        chamberCarveColumn = (volcanoDist <= volcanoChamberRadius);
                    }
                    bool inIsland = false;
                    if (cfg.islandRadius > 0.0f) {
                        float dx = worldX - cfg.islandCenterX;
                        float dz = worldZ - cfg.islandCenterZ;
                        float dist = std::sqrt(dx * dx + dz * dz);
                        inIsland = dist < cfg.islandRadius;
                    }
                    bool lakeColumn = false;
                    int lakeWaterY = surfaceY;
                    if (surfaceLakeEnabled
                        && isLand
                        && !isBeach
                        && !volcanoArea
                        && surfaceY >= (waterSurfaceY + surfaceLakeMinAboveSea)) {
                        const int lakeCellX = floorDivInt(worldXi, surfaceLakeCellSize);
                        const int lakeCellZ = floorDivInt(worldZi, surfaceLakeCellSize);
                        float bestWeight = 0.0f;
                        const PondCellInfo* bestLake = nullptr;
                        for (int oz = -1; oz <= 1; ++oz) {
                            for (int ox = -1; ox <= 1; ++ox) {
                                const PondCellInfo& lake = lakeCellInfoFor(lakeCellX + ox, lakeCellZ + oz);
                                if (!lake.valid || !lake.centerIsLand) continue;
                                if (lake.centerSurfaceY < (waterSurfaceY + surfaceLakeMinAboveSea)) continue;
                                const float dx = worldX - lake.centerX;
                                const float dz = worldZ - lake.centerZ;
                                const float dist2 = dx * dx + dz * dz;
                                const float radius2 = lake.radius * lake.radius;
                                if (dist2 > radius2) continue;
                                const float dist = std::sqrt(dist2);
                                const float weight = 1.0f - (dist / lake.radius);
                                if (!bestLake || weight > bestWeight) {
                                    bestWeight = weight;
                                    bestLake = &lake;
                                }
                            }
                        }
                        if (bestLake) {
                            const int centerSurfaceY = bestLake->centerSurfaceY;
                            const int lakeDepthAllowance = bestLake->depth + 6;
                            if (std::abs(surfaceY - centerSurfaceY) <= lakeDepthAllowance) {
                                const float innerT = std::clamp((bestWeight - 0.06f) / 0.94f, 0.0f, 1.0f);
                                if (innerT > 0.0f) {
                                    lakeWaterY = centerSurfaceY - 1 - surfaceLakeChannelLower;
                                    if (lakeWaterY < surfaceY) {
                                        const int lakeDepthHere = std::max(
                                            2,
                                            static_cast<int>(std::round(static_cast<float>(bestLake->depth + surfaceLakeDepthExtra) * innerT * innerT))
                                        );
                                        const int lakeFloorY = lakeWaterY - lakeDepthHere;
                                        if (lakeFloorY < surfaceY && lakeWaterY > waterSurfaceY) {
                                            surfaceY = lakeFloorY;
                                            lakeColumn = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    bool pondColumn = false;
                    int pondWaterY = surfaceY;
                    if (surfacePondEnabled
                        && !lakeColumn
                        && isLand
                        && !volcanoArea
                        && surfaceY >= (waterSurfaceY + surfacePondMinAboveSea)) {
                        const int pondCellX = floorDivInt(worldXi, surfacePondCellSize);
                        const int pondCellZ = floorDivInt(worldZi, surfacePondCellSize);
                        float bestWeight = 0.0f;
                        const PondCellInfo* bestPond = nullptr;
                        for (int oz = -1; oz <= 1; ++oz) {
                            for (int ox = -1; ox <= 1; ++ox) {
                                const PondCellInfo& pond = pondCellInfoFor(pondCellX + ox, pondCellZ + oz);
                                if (!pond.valid || !pond.centerIsLand) continue;
                                if (pond.centerSurfaceY < (waterSurfaceY + surfacePondMinAboveSea)) continue;
                                const float dx = worldX - pond.centerX;
                                const float dz = worldZ - pond.centerZ;
                                const float dist2 = dx * dx + dz * dz;
                                const float radius2 = pond.radius * pond.radius;
                                if (dist2 > radius2) continue;
                                const float dist = std::sqrt(dist2);
                                const float weight = 1.0f - (dist / pond.radius);
                                if (!bestPond || weight > bestWeight) {
                                    bestWeight = weight;
                                    bestPond = &pond;
                                }
                            }
                        }
                        if (bestPond) {
                            const int centerSurfaceY = bestPond->centerSurfaceY;
                            const int pondDepthAllowance = bestPond->depth + 3;
                            if (std::abs(surfaceY - centerSurfaceY) <= pondDepthAllowance) {
                                const float innerT = std::clamp((bestWeight - 0.12f) / 0.88f, 0.0f, 1.0f);
                                if (innerT > 0.0f) {
                                    pondWaterY = centerSurfaceY - 1 - surfacePondChannelLower;
                                    if (pondWaterY < surfaceY) {
                                        const int pondDepthHere = std::max(
                                            1,
                                            static_cast<int>(std::round(static_cast<float>(bestPond->depth) * innerT * innerT))
                                        );
                                        const int pondFloorY = pondWaterY - pondDepthHere;
                                        if (pondFloorY < surfaceY && pondWaterY > waterSurfaceY) {
                                            surfaceY = pondFloorY;
                                            pondColumn = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    const bool basinColumn = lakeColumn || pondColumn;
                    const int basinWaterY = lakeColumn ? lakeWaterY : pondWaterY;
                    bool riverColumn = false;
                    int riverWaterY = surfaceY;
                    if (surfaceRiverEnabled
                        && !basinColumn
                        && isLand
                        && !volcanoArea
                        && surfaceY >= (waterSurfaceY + surfaceRiverMinAboveSea)) {
                        const float warpSampleX = worldX / surfaceRiverWarpScale;
                        const float warpSampleZ = worldZ / surfaceRiverWarpScale;
                        const float warpNoiseX =
                            fbmValueNoise2D(surfaceRiverSeed * 13 + 7, warpSampleX, warpSampleZ, 3) * 2.0f - 1.0f;
                        const float warpNoiseZ =
                            fbmValueNoise2D(surfaceRiverSeed * 17 - 5, warpSampleX + 19.4f, warpSampleZ - 11.2f, 3) * 2.0f - 1.0f;
                        const float riverSampleX = (worldX + warpNoiseX * surfaceRiverWarpStrength) / surfaceRiverScale;
                        const float riverSampleZ = (worldZ + warpNoiseZ * surfaceRiverWarpStrength) / surfaceRiverScale;
                        const float primary = fbmValueNoise2D(surfaceRiverSeed, riverSampleX, riverSampleZ, 4);
                        const float ridge = std::abs(primary * 2.0f - 1.0f);
                        const float widthNoise = fbmValueNoise2D(
                            surfaceRiverSeed * 23 + 101,
                            riverSampleX * 0.65f + 3.7f,
                            riverSampleZ * 0.65f - 2.1f,
                            2
                        );
                        const float ridgeThreshold = surfaceRiverThresholdMin
                            + (surfaceRiverThresholdMax - surfaceRiverThresholdMin) * widthNoise;
                        if (ridge < ridgeThreshold) {
                            const float innerT = std::clamp(1.0f - (ridge / ridgeThreshold), 0.0f, 1.0f);
                            const float depthNoise = fbmValueNoise2D(
                                surfaceRiverSeed * 31 + 29,
                                riverSampleX * 0.8f - 4.2f,
                                riverSampleZ * 0.8f + 5.3f,
                                2
                            );
                            const int channelLower = surfaceRiverChannelLowerMin
                                + static_cast<int>(std::round(
                                    static_cast<float>(surfaceRiverChannelLowerMax - surfaceRiverChannelLowerMin) * depthNoise
                                ));
                            const int baseRiverWaterY = surfaceY - channelLower;
                            if (baseRiverWaterY > waterSurfaceY) {
                                const int depthBase = surfaceRiverDepthMin
                                    + static_cast<int>(std::round((surfaceRiverDepthMax - surfaceRiverDepthMin) * depthNoise));
                                const int depthBaseBoosted = std::max(
                                    1,
                                    static_cast<int>(std::round(static_cast<float>(depthBase) * surfaceRiverDepthMultiplier))
                                );
                                const int depthHere = std::max(
                                    2,
                                    static_cast<int>(std::round(static_cast<float>(depthBaseBoosted) * innerT * innerT))
                                );
                                const int baseRiverFloorY = baseRiverWaterY - depthHere;
                                riverWaterY = std::max(
                                    baseRiverFloorY + 2,
                                    baseRiverWaterY - surfaceRiverWaterlineExtraLower
                                );
                                if (riverWaterY > waterSurfaceY && baseRiverFloorY < surfaceY) {
                                    surfaceY = baseRiverFloorY;
                                    riverColumn = true;
                                }
                            }
                        }
                    }
                    const bool waterFeatureColumn = basinColumn || riverColumn;
                    const int waterFeatureWaterY = basinColumn ? basinWaterY : riverWaterY;
                    const uint32_t packedFeatureWaterColor = riverColumn
                        ? packedWaterColorRiver
                        : (lakeColumn ? packedWaterColorLake
                                      : (pondColumn ? packedWaterColorPond : packedWaterColorUnknown));
                    bool unifiedDepthRiverColumn = false;
                    int unifiedDepthRiverBottomY = unifiedDepthRiverWaterY - unifiedDepthRiverDepthMin;
                    int unifiedDepthRiverTopY = unifiedDepthRiverWaterY;
                    int unifiedDepthRiverCeilingCarveTopY = unifiedDepthRiverWaterY;
                    if (unifiedDepthsEnabled && unifiedDepthRiverBandValid) {
                        const float riverSampleX = worldX / unifiedDepthRiverScale;
                        const float riverSampleZ = worldZ / unifiedDepthRiverScale;
                        const float primary = fbmValueNoise2D(unifiedDepthRiverSeed, riverSampleX, riverSampleZ, 4);
                        const float ridge = std::abs(primary * 2.0f - 1.0f);
                        const float widthNoise = fbmValueNoise2D(
                            unifiedDepthRiverSeed * 19 + 31,
                            riverSampleX * 0.77f + 3.1f,
                            riverSampleZ * 0.77f - 2.4f,
                            2
                        );
                        const float ridgeThreshold = unifiedDepthRiverThresholdMin
                            + (unifiedDepthRiverThresholdMax - unifiedDepthRiverThresholdMin) * widthNoise;
                        if (ridge < ridgeThreshold) {
                            const float innerT = std::clamp(1.0f - (ridge / ridgeThreshold), 0.0f, 1.0f);
                            const float terrainNoise = fbmValueNoise2D(
                                unifiedDepthRiverSeed * 41 + 73,
                                worldX / unifiedDepthRiverTerrainScale,
                                worldZ / unifiedDepthRiverTerrainScale,
                                3
                            );
                            const int riverRoofY = unifiedDepthsTopY - unifiedDepthRiverRoofGuard;
                            const int riverFloorY = unifiedDepthsMinY + unifiedDepthRiverFloorGuard;
                            const int terrainBandMinY = riverFloorY + 3;
                            const int terrainBandMaxY = riverRoofY - 1;
                            const float terrainSigned = terrainNoise * 2.0f - 1.0f;
                            const int terrainCenterY = std::clamp(
                                unifiedDepthRiverWaterY + unifiedDepthRiverSeatExtra + unifiedDepthRiverChannelLowerMin,
                                terrainBandMinY,
                                terrainBandMaxY
                            );
                            const int reliefOffset = static_cast<int>(std::round(unifiedDepthRiverTerrainRelief * terrainSigned));
                            const int localDepthTerrainY = std::clamp(
                                terrainCenterY + reliefOffset,
                                terrainBandMinY,
                                terrainBandMaxY
                            );
                            const float depthNoise = fbmValueNoise2D(
                                unifiedDepthRiverSeed * 29 + 17,
                                riverSampleX * 0.9f - 7.5f,
                                riverSampleZ * 0.9f + 4.7f,
                                2
                            );
                            const int channelLowerBase = unifiedDepthRiverChannelLowerMin
                                + static_cast<int>(std::round(
                                    static_cast<float>(unifiedDepthRiverChannelLowerMax - unifiedDepthRiverChannelLowerMin) * depthNoise
                                ));
                            const int channelLower = channelLowerBase + unifiedDepthRiverSeatExtra;
                            const int baseRiverWaterY = localDepthTerrainY - channelLower;
                            if (baseRiverWaterY < (riverFloorY + 1)) {
                                continue;
                            }
                            const int depthBase = unifiedDepthRiverDepthMin
                                + static_cast<int>(std::round(
                                    static_cast<float>(unifiedDepthRiverDepthMax - unifiedDepthRiverDepthMin) * depthNoise
                                ));
                            const int depthBaseBoosted = std::max(
                                1,
                                static_cast<int>(std::round(static_cast<float>(depthBase) * unifiedDepthRiverDepthMultiplier))
                            );
                            const int depthHere = std::max(
                                2,
                                static_cast<int>(std::round(static_cast<float>(depthBaseBoosted) * innerT * innerT))
                            );
                            const int baseRiverFloorY = baseRiverWaterY - depthHere;
                            const int riverWaterY = std::max(
                                baseRiverFloorY + 2,
                                baseRiverWaterY - unifiedDepthRiverWaterlineExtraLower
                            );
                            const int clampedWaterY = std::clamp(riverWaterY, riverFloorY + 1, riverRoofY - 1);
                            const int maxDepthHere = std::max(1, clampedWaterY - riverFloorY);
                            const int clampedDepthHere = std::clamp(depthHere, 1, maxDepthHere);
                            unifiedDepthRiverTopY = clampedWaterY;
                            unifiedDepthRiverBottomY = clampedWaterY - clampedDepthHere;
                            unifiedDepthRiverColumn = (unifiedDepthRiverBottomY <= unifiedDepthRiverTopY);
                            if (unifiedDepthRiverColumn && unifiedDepthRiverCeilingCarveMax > 0) {
                                const int carveSpan = unifiedDepthRiverCeilingCarveMax - unifiedDepthRiverCeilingCarveMin;
                                int carveUp = unifiedDepthRiverCeilingCarveMin;
                                if (carveSpan > 0) {
                                    const uint32_t carveSeed = hash3DInt(
                                        worldXi + unifiedDepthRiverSeed * 131,
                                        unifiedDepthRiverTopY + unifiedDepthRiverSeed * 193,
                                        worldZi - unifiedDepthRiverSeed * 157
                                    );
                                    carveUp += static_cast<int>(carveSeed % static_cast<uint32_t>(carveSpan + 1));
                                }
                                unifiedDepthRiverCeilingCarveTopY = std::min(
                                    unifiedDepthsTopY - 1,
                                    unifiedDepthRiverTopY + carveUp
                                );
                            }
                        }
                    }
                    const bool waterFeatureColumnForPortal = isLand && waterFeatureColumn && (waterFeatureWaterY > surfaceY);
                    const bool portalColumnExpanse = isExpanseLevel && (!isLand || waterFeatureColumnForPortal);
                    int oreVariant = -1;
                    if (lod == 0 && oreEnabled && isLand && !isBeach) {
                        oreVariant = oreVariantForColumn(worldXi, worldZi);
                    }
                    const int depthLavaBandTop = std::max(depthLavaTopY, depthLavaBottomY);
                    const int depthLavaBandBottom = std::min(depthLavaTopY, depthLavaBottomY);
                    auto depthBandCarveAtY = [&](int sampleY) {
                        float dv1 = 0.0f;
                        float dv2 = 0.0f;
                        if (!sampleCaveField(worldX, static_cast<float>(sampleY), worldZ, dv1, dv2)) {
                            return false;
                        }
                        const float bandT = std::clamp(
                            static_cast<float>(sampleY - unifiedDepthsMinY)
                                / static_cast<float>(std::max(1, unifiedDepthsTopY - unifiedDepthsMinY)),
                            0.0f,
                            1.0f
                        );
                        const float thrA = 0.57f + (0.64f - 0.57f) * bandT;
                        const float thrB = 0.53f + (0.61f - 0.53f) * bandT;
                        return (dv1 > thrA) || (dv2 > thrB);
                    };
                    int depthLavaColumnTopY = std::numeric_limits<int>::min();
                    if (lod == 0
                        && isExpanseLevel
                        && unifiedDepthsEnabled
                        && depthLavaFloorEnabled
                        && depthLavaBandBottom <= depthLavaBandTop) {
                        const int depthLavaSeedScanTop = std::min(unifiedDepthsTopY - 1, depthLavaBandTop + 6);
                        for (int sampleY = depthLavaBandBottom; sampleY <= depthLavaSeedScanTop; ++sampleY) {
                            if (sampleY <= unifiedDepthsMinY || sampleY >= unifiedDepthsTopY) continue;
                            if (depthBandCarveAtY(sampleY)) {
                                depthLavaColumnTopY = std::max(depthLavaColumnTopY, sampleY);
                            }
                        }
                        if (depthLavaColumnTopY > unifiedDepthsMinY) {
                            // Guarantee a meaningful lava depth once seeded.
                            depthLavaColumnTopY = std::max(depthLavaColumnTopY, depthLavaBandTop);
                        }
                    }
                    const bool chalkColumnCandidate = (lod == 0)
                        && chalkEnabled
                        && (chalkProto != nullptr)
                        && isLand
                        && chalkVeinForColumn(worldXi, worldZi);
                    const bool clayColumnCandidate = (lod == 0)
                        && clayEnabled
                        && (clayProto != nullptr)
                        && isLand
                        && clayVeinForColumn(worldXi, worldZi);
                    const bool graniteColumnCandidate = (lod == 0)
                        && graniteEnabled
                        && (graniteProto != nullptr)
                        && isLand
                        && (biomeID != 2)
                        && graniteVeinForColumn(worldXi, worldZi);
                    bool chalkReplaceRollPass = false;
                    if (chalkColumnCandidate) {
                        const uint32_t chalkSeedValue = hash3DInt(
                            worldXi + chalkSeed * 97,
                            surfaceY + chalkSeed * 131,
                            worldZi - chalkSeed * 151
                        );
                        const float chalkRoll = static_cast<float>((chalkSeedValue >> 8u) & 0xffu) / 255.0f;
                        chalkReplaceRollPass = chalkRoll <= chalkReplaceChance;
                    }
                    bool clayReplaceRollPass = false;
                    if (clayColumnCandidate) {
                        const uint32_t claySeedValue = hash3DInt(
                            worldXi + claySeed * 97,
                            surfaceY + claySeed * 131,
                            worldZi - claySeed * 151
                        );
                        const float clayRoll = static_cast<float>((claySeedValue >> 8u) & 0xffu) / 255.0f;
                        clayReplaceRollPass = clayRoll <= clayReplaceChance;
                    }


                    if (lod > 0) {
                        auto trySetCell = [&](int cellY, uint32_t id, uint32_t color) {
                            int localY = cellY - sectionCoord.y * size;
                            if (localY < 0 || localY >= size) return;
                            glm::ivec3 lodCoord(sectionCoord.x * size + x,
                                                cellY,
                                                sectionCoord.z * size + z);
                            voxelWorld.setBlockLod(lod, lodCoord, id, color, false);
                            wroteAny = true;
                        };
                        if (!isLand) {
                            trySetCell(floorDivInt(waterFloorY, scale), sandSeabedProto->prototypeID, packColor(seabedColor));
                            if (!isExpanseLevel) {
                                trySetCell(floorDivInt(waterFloorY - 3, scale), stoneProto->prototypeID, packColor(stoneColor));
                            }
                            if (waterSurfaceY > waterFloorY) {
                                trySetCell(floorDivInt(waterSurfaceY, scale), waterProto->prototypeID, packedWaterColorOcean);
                            }
                        } else if (lavaColumnActive) {
                            // At coarse LOD, prefer a clean crater silhouette with lava cap.
                            trySetCell(floorDivInt(lavaSurfaceY, scale), waterProto->prototypeID, packColor(lavaColor));
                        } else if (waterFeatureColumn && waterFeatureWaterY > surfaceY) {
                            const bool placeClayRiverBed = riverColumn
                                && clayColumnCandidate
                                && clayReplaceRollPass
                                && clayProto;
                            trySetCell(floorDivInt(surfaceY, scale),
                                       placeClayRiverBed
                                           ? static_cast<uint32_t>(clayProto->prototypeID)
                                           : static_cast<uint32_t>(soilProto->prototypeID),
                                       placeClayRiverBed
                                           ? packColor(clayColor)
                                           : packColor(soilColor));
                            trySetCell(floorDivInt(waterFeatureWaterY, scale), waterProto->prototypeID, packedFeatureWaterColor);
                        } else {
                            trySetCell(floorDivInt(surfaceY, scale),
                                       topSurfaceProto->prototypeID,
                                       packColor(isBeach ? sandColor : biomeSurfaceColor));
                        }
                        if (isExpanseLevel && unifiedDepthsEnabled) {
                            const uint32_t depthStoneId = static_cast<uint32_t>((depthStoneProto ? depthStoneProto : stoneProto)->prototypeID);
                            trySetCell(floorDivInt(unifiedDepthsTopY, scale), depthStoneId, packColor(stoneColor));
                            trySetCell(floorDivInt(unifiedDepthsMinY, scale), depthStoneId, packColor(stoneColor));
                        }
                        continue;
                    }

                    for (int y = 0; y < size; ++y) {
                        int worldY = (sectionCoord.y * size + y) * scale;
                        glm::ivec3 lodCoord(sectionCoord.x * size + x,
                                            sectionCoord.y * size + y,
                                            sectionCoord.z * size + z);
                        int cellMinY = worldY;
                        int cellMaxY = worldY + scale - 1;
                        auto rangeContains = [&](int y) {
                            return y >= cellMinY && y <= cellMaxY;
                        };
                        auto rangeOverlaps = [&](int minY, int maxY) {
                            return cellMaxY >= minY && cellMinY <= maxY;
                        };

                        bool carve = false;
                        if (lod == 0 && inIsland && worldY <= (isLand ? surfaceY : waterFloorY)) {
                            float v1 = 0.0f;
                            float v2 = 0.0f;
                            if (!sampleCaveField(worldX, static_cast<float>(worldY), worldZ, v1, v2)) {
                                v1 = 0.0f;
                                v2 = 0.0f;
                            }
                            if (isLand) {
                                // Taper caves near the surface to reduce openings.
                                float depth = static_cast<float>(surfaceY - worldY);
                                if (depth > 3.0f) {
                                    float t = std::clamp(depth / 24.0f, 0.0f, 1.0f);
                                    float thrA = 0.72f + (0.62f - 0.72f) * t;
                                    float thrB = 0.68f + (0.58f - 0.68f) * t;
                                    if (isDepthLevel) {
                                        thrA -= 0.10f;
                                        thrB -= 0.10f;
                                    }
                                    if (v1 > thrA || v2 > thrB) carve = true;
                                }
                            } else {
                                if (v1 > 0.62f || v2 > 0.58f) carve = true;
                            }
                        }

                        if (voidPortalProto && voidPortalProto->prototypeID > 0) {
                            const bool isPortalLayer =
                                (portalColumnExpanse && rangeContains(seabedPortalY))
                                || (isDepthLevel && rangeContains(depthPortalY));
                            if (isPortalLayer) {
                                voxelWorld.setBlockLod(
                                    lod,
                                    lodCoord,
                                    static_cast<uint32_t>(voidPortalProto->prototypeID),
                                    packColor(glm::vec3(1.0f)),
                                    false
                                );
                                wroteAny = true;
                                continue;
                            }
                        }

                        if (isExpanseLevel && worldY < expanseDepthSplitY) {
                            // Strict split:
                            // - unified depths band (-99..UnifiedDepthsMinY): generate depth stone/caves/rivers
                            // - below UnifiedDepthsMinY: empty (never regular expanse cave stone)
                            if (unifiedDepthsEnabled && worldY >= unifiedDepthsMinY) {
                                if (worldY == unifiedDepthsTopY || worldY == unifiedDepthsMinY) {
                                    const uint32_t depthStoneId = static_cast<uint32_t>((depthStoneProto ? depthStoneProto : stoneProto)->prototypeID);
                                    voxelWorld.setBlockLod(lod, lodCoord, depthStoneId, packColor(stoneColor), false);
                                    wroteAny = true;
                                    continue;
                                }
                                const bool inDepthLavaBand = depthLavaFloorEnabled
                                    && (worldY >= depthLavaBandBottom)
                                    && (worldY <= depthLavaBandTop);
                                if (unifiedDepthRiverColumn && rangeOverlaps(unifiedDepthRiverBottomY, unifiedDepthRiverTopY)) {
                                    voxelWorld.setBlockLod(lod, lodCoord, waterProto->prototypeID, packedWaterColorRiver, false);
                                    wroteAny = true;
                                    continue;
                                }
                                if (unifiedDepthRiverColumn
                                    && unifiedDepthRiverCeilingCarveTopY > unifiedDepthRiverTopY
                                    && rangeOverlaps(unifiedDepthRiverTopY + 1, unifiedDepthRiverCeilingCarveTopY)) {
                                    // Upward erosion for visibility: clear the channel ceiling while preserving roof cap.
                                    continue;
                                }
                                const bool extendDepthLavaToFloor = depthLavaFloorEnabled
                                    && (depthLavaColumnTopY > unifiedDepthsMinY)
                                    && (worldY > unifiedDepthsMinY)
                                    && (worldY <= depthLavaColumnTopY);
                                if (extendDepthLavaToFloor) {
                                    const int tx = positiveMod(worldXi, 3);
                                    const int tz = positiveMod(worldZi, 3);
                                    const int tileIdx = tz * 3 + tx;
                                    const Entity* lavaTileProto = depthLavaTileProtos[static_cast<size_t>(tileIdx)];
                                    if (lavaTileProto && lavaTileProto->prototypeID > 0) {
                                        voxelWorld.setBlockLod(lod, lodCoord, static_cast<uint32_t>(lavaTileProto->prototypeID), packColor(lavaColor), false);
                                        wroteAny = true;
                                    }
                                    continue;
                                }
                                const bool depthCarve = depthBandCarveAtY(worldY);
                                if (depthCarve) {
                                    if (inDepthLavaBand) {
                                        const int tx = positiveMod(worldXi, 3);
                                        const int tz = positiveMod(worldZi, 3);
                                        const int tileIdx = tz * 3 + tx;
                                        const Entity* lavaTileProto = depthLavaTileProtos[static_cast<size_t>(tileIdx)];
                                        if (lavaTileProto && lavaTileProto->prototypeID > 0) {
                                            voxelWorld.setBlockLod(lod, lodCoord, static_cast<uint32_t>(lavaTileProto->prototypeID), packColor(lavaColor), false);
                                            wroteAny = true;
                                        }
                                    }
                                    continue;
                                }
                                const int depthLodestoneVariant = depthLodestoneVariantAt(worldXi, worldY, worldZi);
                                const bool depthCopperColumn = depthCopperVeinAt(worldXi, worldY, worldZi);
                                uint32_t placeId = static_cast<uint32_t>((depthStoneProto ? depthStoneProto : stoneProto)->prototypeID);
                                uint32_t placeColor = packColor(stoneColor);
                                if (depthLodestoneVariant >= 0
                                    && depthLodestoneVariant < static_cast<int>(depthLodestoneOreProtos.size())
                                    && depthLodestoneOreProtos[static_cast<size_t>(depthLodestoneVariant)] != nullptr) {
                                    const uint32_t oreCellSeed = hash3DInt(
                                        worldXi + depthLodestoneSeed * 97,
                                        worldY + depthLodestoneSeed * 131,
                                        worldZi - depthLodestoneSeed * 151
                                    );
                                    const float oreCellRoll = static_cast<float>((oreCellSeed >> 8u) & 0xffu) / 255.0f;
                                    if (oreCellRoll <= depthLodestoneReplaceChance) {
                                        placeId = static_cast<uint32_t>(depthLodestoneOreProtos[static_cast<size_t>(depthLodestoneVariant)]->prototypeID);
                                        placeColor = packColor(glm::vec3(0.33f, 0.35f, 0.36f));
                                    }
                                } else if (depthCopperColumn && depthCopperSulfateOreProto) {
                                    const uint32_t oreCellSeed = hash3DInt(
                                        worldXi + depthCopperSeed * 97,
                                        worldY + depthCopperSeed * 131,
                                        worldZi - depthCopperSeed * 151
                                    );
                                    const float oreCellRoll = static_cast<float>((oreCellSeed >> 8u) & 0xffu) / 255.0f;
                                    if (oreCellRoll <= depthCopperReplaceChance) {
                                        placeId = static_cast<uint32_t>(depthCopperSulfateOreProto->prototypeID);
                                        placeColor = packColor(glm::vec3(0.15f, 0.62f, 0.86f));
                                    }
                                }
                                voxelWorld.setBlockLod(lod, lodCoord, placeId, placeColor, false);
                                wroteAny = true;
                                continue;
                            }
                            // Below configured depths band: keep empty.
                            continue;
                        }

                        if (!isLand) {
                            if (rangeContains(waterFloorY)) {
                                voxelWorld.setBlockLod(lod, lodCoord, sandSeabedProto->prototypeID, packColor(seabedColor), false);
                                wroteAny = true;
                                continue;
                            }
                            if (worldY < waterFloorY) {
                                voxelWorld.setBlockLod(lod, lodCoord, stoneProto->prototypeID, packColor(stoneColor), false);
                                wroteAny = true;
                                continue;
                            }
                            if (waterSurfaceY > waterFloorY) {
                                int waterMin = waterFloorY + 1;
                                int waterMax = waterSurfaceY;
                                if (rangeOverlaps(waterMin, waterMax)) {
                                    voxelWorld.setBlockLod(lod, lodCoord, waterProto->prototypeID, packedWaterColorOcean, false);
                                    wroteAny = true;
                                }
                            }
                            continue;
                        }

                        if (carve) {
                            if (worldY <= waterSurfaceY) {
                                voxelWorld.setBlockLod(lod, lodCoord, waterProto->prototypeID, packedWaterColorOcean, false);
                                wroteAny = true;
                            }
                            continue;
                        }

                        if (waterFeatureColumn && waterFeatureWaterY > surfaceY) {
                            const int featureWaterMinY = surfaceY + 1;
                            if (rangeOverlaps(featureWaterMinY, waterFeatureWaterY)) {
                                voxelWorld.setBlockLod(lod, lodCoord, waterProto->prototypeID, packedFeatureWaterColor, false);
                                wroteAny = true;
                                continue;
                            }
                        }

                        if (lavaColumnActive) {
                            // Keep crater interior open above lava so the bowl reaches the edge.
                            if (worldY > lavaSurfaceY && worldY <= surfaceY) {
                                continue;
                            }
                            if (rangeOverlaps(lavaFillMinY, lavaSurfaceY)) {
                                voxelWorld.setBlockLod(lod, lodCoord, waterProto->prototypeID, packColor(lavaColor), false);
                                wroteAny = true;
                                continue;
                            }
                            // Carve a magma chamber under the lake to push lava/cavity deep underground.
                            if (chamberCarveColumn && rangeOverlaps(chamberBottomY, lavaFillMinY - 1)) {
                                continue;
                            }
                        }

                        if (rangeContains(surfaceY)) {
                            if (waterFeatureColumn && waterFeatureWaterY > surfaceY) {
                                const bool placeClayRiverBed = riverColumn
                                    && clayColumnCandidate
                                    && clayReplaceRollPass
                                    && clayProto;
                                voxelWorld.setBlockLod(
                                    lod,
                                    lodCoord,
                                    placeClayRiverBed
                                        ? static_cast<uint32_t>(clayProto->prototypeID)
                                        : static_cast<uint32_t>(soilProto->prototypeID),
                                    placeClayRiverBed
                                        ? packColor(clayColor)
                                        : packColor(soilColor),
                                    false
                                );
                                wroteAny = true;
                                continue;
                            }
                            if (chalkColumnCandidate && chalkReplaceRollPass && chalkProto) {
                                pendingChalkPlacements.emplace_back(
                                    sectionCoord.x * size + x,
                                    surfaceY,
                                    sectionCoord.z * size + z
                                );
                            }

                            glm::vec3 topColor = isBeach ? sandColor : biomeSurfaceColor;
                            voxelWorld.setBlockLod(
                                lod,
                                lodCoord,
                                topSurfaceProto->prototypeID,
                                packColor(topColor),
                                false
                            );
                            wroteAny = true;
                            continue;
                        }

                        if (worldY < surfaceY) {
                            int soilMin = surfaceY - cfg.soilDepth;
                            int stoneMin = surfaceY - cfg.soilDepth - cfg.stoneDepth;
                            if (rangeContains(waterFloorY)) {
                                voxelWorld.setBlockLod(lod, lodCoord, sandSeabedProto->prototypeID, packColor(seabedColor), false);
                                wroteAny = true;
                                continue;
                            }
                            bool inSoilLayer = rangeOverlaps(soilMin, surfaceY - 1);
                            bool inStoneLayer = rangeOverlaps(cfg.minY, stoneMin) || (worldY < stoneMin);
                            if (inSoilLayer || inStoneLayer) {
                                bool caveAdjacent = false;
                                if (lod == 0 && inIsland) {
                                    caveAdjacent =
                                        caveCarvedAt(worldX + 1.0f, static_cast<float>(worldY), worldZ, surfaceY, true) ||
                                        caveCarvedAt(worldX - 1.0f, static_cast<float>(worldY), worldZ, surfaceY, true) ||
                                        caveCarvedAt(worldX, static_cast<float>(worldY), worldZ + 1.0f, surfaceY, true) ||
                                        caveCarvedAt(worldX, static_cast<float>(worldY), worldZ - 1.0f, surfaceY, true) ||
                                        caveCarvedAt(worldX, static_cast<float>(worldY) + 1.0f, worldZ, surfaceY, true) ||
                                        caveCarvedAt(worldX, static_cast<float>(worldY) - 1.0f, worldZ, surfaceY, true);
                                }
                                bool placeOre = false;
                                if (oreVariant >= 0
                                    && oreVariant < static_cast<int>(oreProtos.size())
                                    && oreProtos[static_cast<size_t>(oreVariant)] != nullptr) {
                                    int depthBelowSurface = surfaceY - worldY;
                                    float oreChance = inStoneLayer ? oreStoneReplaceChance : oreSoilReplaceChance;
                                    if (caveAdjacent) {
                                        oreChance = std::min(1.0f, oreChance + oreCaveAdjacencyBoost);
                                    }
                                    if (depthBelowSurface >= oreMinDepthFromSurface) {
                                        const uint32_t oreCellSeed = hash3DInt(
                                            worldXi + oreSeed * 97,
                                            worldY + oreSeed * 131,
                                            worldZi - oreSeed * 151
                                        );
                                        const float oreCellRoll = static_cast<float>((oreCellSeed >> 8u) & 0xffu) / 255.0f;
                                        placeOre = (oreCellRoll <= oreChance);
                                    }
                                }
                                bool placeGranite = false;
                                if (!placeOre
                                    && inStoneLayer
                                    && caveAdjacent
                                    && (worldY <= waterSurfaceY)
                                    && graniteColumnCandidate
                                    && graniteProto) {
                                    const uint32_t graniteSeedValue = hash3DInt(
                                        worldXi + graniteSeed * 103,
                                        worldY + graniteSeed * 127,
                                        worldZi - graniteSeed * 149
                                    );
                                    const float graniteRoll = static_cast<float>((graniteSeedValue >> 8u) & 0xffu) / 255.0f;
                                    placeGranite = graniteRoll <= graniteReplaceChance;
                                }
                                if (placeOre) {
                                    voxelWorld.setBlockLod(lod,
                                                           lodCoord,
                                                           oreProtos[static_cast<size_t>(oreVariant)]->prototypeID,
                                                           oreColors[static_cast<size_t>(oreVariant)],
                                                           false);
                                } else if (placeGranite) {
                                    voxelWorld.setBlockLod(
                                        lod,
                                        lodCoord,
                                        graniteProto->prototypeID,
                                        packColor(graniteColor),
                                        false
                                    );
                                } else {
                                    if (inSoilLayer) {
                                        voxelWorld.setBlockLod(lod, lodCoord, soilProto->prototypeID, packColor(soilColor), false);
                                    } else {
                                        voxelWorld.setBlockLod(lod, lodCoord, stoneProto->prototypeID, packColor(stoneColor), false);
                                    }
                                }
                                wroteAny = true;
                                continue;
                            }
                        }
                    }
            }
            outNextColumn = clampedEndColumn;
            outCompleted = (clampedEndColumn >= totalColumns);
            if (outCompleted && lod == 0 && isExpanseLevel && unifiedDepthsEnabled) {
                const int sectionMinX = sectionCoord.x * size;
                const int sectionMaxX = sectionMinX + size - 1;
                const int sectionMinYDepth = sectionCoord.y * size;
                const int sectionMaxYDepth = sectionMinYDepth + size - 1;
                const int sectionMinZ = sectionCoord.z * size;
                const int sectionMaxZ = sectionMinZ + size - 1;
                const uint32_t depthStoneId = static_cast<uint32_t>((depthStoneProto ? depthStoneProto : stoneProto)->prototypeID);
                const uint32_t fallbackStoneId = static_cast<uint32_t>(stoneProto->prototypeID);
                const uint32_t depthPurpleDirtId = depthPurpleDirtProto ? static_cast<uint32_t>(depthPurpleDirtProto->prototypeID) : 0u;
                const uint32_t depthRustBeamId = depthRustBeamProto ? static_cast<uint32_t>(depthRustBeamProto->prototypeID) : 0u;
                const uint32_t waterId = static_cast<uint32_t>(waterProto->prototypeID);
                auto inCurrentSection = [&](const glm::ivec3& cell) {
                    return cell.x >= sectionMinX && cell.x <= sectionMaxX
                        && cell.y >= sectionMinYDepth && cell.y <= sectionMaxYDepth
                        && cell.z >= sectionMinZ && cell.z <= sectionMaxZ;
                };
                auto isSolidSupport = [&](uint32_t id) {
                    if (id == 0u) return false;
                    if (id >= static_cast<uint32_t>(prototypes.size())) return false;
                    return prototypes[static_cast<size_t>(id)].isSolid;
                };
                auto isDepthRiverWaterCell = [&](const glm::ivec3& cell) {
                    const uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, cell);
                    if (id != waterId) return false;
                    if (cell.y > unifiedDepthsTopY) return false;
                    const uint32_t packed = VoxelMeshInitSystemLogic::GetVoxelColorAtLod(voxelWorld, 0, cell);
                    return waterWaveClassFromPackedColor(packed) == kWaterWaveClassRiver;
                };
                auto isAnyWaterCell = [&](const glm::ivec3& cell) {
                    const uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, cell);
                    return id == waterId;
                };
                auto isDepthLavaId = [&](uint32_t id) {
                    if (id == 0u) return false;
                    for (const Entity* lavaProto : depthLavaTileProtos) {
                        if (lavaProto && lavaProto->prototypeID > 0
                            && id == static_cast<uint32_t>(lavaProto->prototypeID)) {
                            return true;
                        }
                    }
                    return false;
                };
                auto hasNearbyDepthLava = [&](const glm::ivec3& cell, int radiusXZ, int radiusY) {
                    for (int dy = -radiusY; dy <= radiusY; ++dy) {
                        const int y = cell.y + dy;
                        if (y > unifiedDepthsTopY || y <= unifiedDepthsMinY) continue;
                        for (int dz = -radiusXZ; dz <= radiusXZ; ++dz) {
                            for (int dx = -radiusXZ; dx <= radiusXZ; ++dx) {
                                if (dx == 0 && dy == 0 && dz == 0) continue;
                                const glm::ivec3 n(cell.x + dx, y, cell.z + dz);
                                const uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, n);
                                if (isDepthLavaId(id)) {
                                    return true;
                                }
                            }
                        }
                    }
                    return false;
                };

                // Purple dirt patches on cave floors only (never walls/ceilings).
                if (depthPurpleDirtProto && depthPurplePatchPercent > 0) {
                    constexpr int kDepthPurplePatchCellSize = 12;
                    constexpr int kDepthPurplePatchMinRadius = 2;
                    constexpr int kDepthPurplePatchMaxRadius = 5;
                    constexpr int kDepthPurplePatchSeed = 911;
                    for (int z = sectionMinZ; z <= sectionMaxZ; ++z) {
                        for (int y = sectionMinYDepth; y <= sectionMaxYDepth; ++y) {
                            if (y > unifiedDepthsTopY || y <= unifiedDepthsMinY) continue;
                            for (int x = sectionMinX; x <= sectionMaxX; ++x) {
                                const glm::ivec3 cell(x, y, z);
                                const uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, cell);
                                if (id != depthStoneId && id != fallbackStoneId) continue;
                                const uint32_t aboveId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, cell + glm::ivec3(0, 1, 0));
                                if (aboveId != 0u) continue;
                                const uint32_t belowId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, cell + glm::ivec3(0, -1, 0));
                                if (!isSolidSupport(belowId)) continue;
                                const int patchCellX = floorDivInt(x, kDepthPurplePatchCellSize);
                                const int patchCellZ = floorDivInt(z, kDepthPurplePatchCellSize);
                                bool inPatch = false;
                                for (int oz = -1; oz <= 1 && !inPatch; ++oz) {
                                    for (int ox = -1; ox <= 1 && !inPatch; ++ox) {
                                        const int cellX = patchCellX + ox;
                                        const int cellZ = patchCellZ + oz;
                                        const uint32_t spawnSeed = hash2DInt(
                                            cellX + kDepthPurplePatchSeed * 37,
                                            cellZ - kDepthPurplePatchSeed * 53
                                        );
                                        if (static_cast<int>(spawnSeed % 100u) >= depthPurplePatchPercent) continue;
                                        const uint32_t centerSeed = hash2DInt(
                                            cellX * 131 + kDepthPurplePatchSeed,
                                            cellZ * 173 - kDepthPurplePatchSeed
                                        );
                                        const int centerBaseX = cellX * kDepthPurplePatchCellSize;
                                        const int centerBaseZ = cellZ * kDepthPurplePatchCellSize;
                                        const int centerX = centerBaseX
                                            + static_cast<int>((centerSeed & 0xffu) * static_cast<uint32_t>(kDepthPurplePatchCellSize) / 256u);
                                        const int centerZ = centerBaseZ
                                            + static_cast<int>(((centerSeed >> 8u) & 0xffu) * static_cast<uint32_t>(kDepthPurplePatchCellSize) / 256u);
                                        const int radiusRange = std::max(1, kDepthPurplePatchMaxRadius - kDepthPurplePatchMinRadius + 1);
                                        const int radius = kDepthPurplePatchMinRadius
                                            + static_cast<int>((centerSeed >> 16u) % static_cast<uint32_t>(radiusRange));
                                        const int dx = x - centerX;
                                        const int dz = z - centerZ;
                                        if ((dx * dx + dz * dz) <= (radius * radius)) {
                                            inPatch = true;
                                        }
                                    }
                                }
                                if (!inPatch) continue;
                                voxelWorld.setBlockLod(0, cell, depthPurpleDirtId, packColor(glm::vec3(0.36f, 0.23f, 0.45f)), false);
                                wroteAny = true;
                            }
                        }
                    }
                }

                // Rusted beams: short vein-like linear runs on exposed depth stone.
                if (depthRustBeamProto && depthRustBeamPercent > 0) {
                    static const std::array<glm::ivec3, 4> kBeamDirs = {
                        glm::ivec3(1, 0, 0),
                        glm::ivec3(-1, 0, 0),
                        glm::ivec3(0, 0, 1),
                        glm::ivec3(0, 0, -1)
                    };
                    int beamStartsPlaced = 0;
                    const int maxBeamStarts = 24;
                    for (int z = sectionMinZ; z <= sectionMaxZ && beamStartsPlaced < maxBeamStarts; ++z) {
                        for (int y = sectionMinYDepth; y <= sectionMaxYDepth && beamStartsPlaced < maxBeamStarts; ++y) {
                            if (y > unifiedDepthsTopY || y <= unifiedDepthsMinY) continue;
                            for (int x = sectionMinX; x <= sectionMaxX && beamStartsPlaced < maxBeamStarts; ++x) {
                                const glm::ivec3 startCell(x, y, z);
                                const uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, startCell);
                                if (id != depthStoneId && id != fallbackStoneId && id != depthPurpleDirtId) continue;
                                const uint32_t seed = hash3DInt(x + depthRustBeamSeed * 11, y - depthRustBeamSeed * 7, z + depthRustBeamSeed * 13);
                                if (static_cast<int>(seed % 100u) >= depthRustBeamPercent) continue;
                                if (!hasNearbyDepthLava(startCell, 8, 6)) continue;
                                const int dirIndex = static_cast<int>((seed >> 8u) & 3u);
                                const glm::ivec3 dir = kBeamDirs[static_cast<size_t>(dirIndex)];
                                const int beamLength = 3 + static_cast<int>((seed >> 12u) % 5u);
                                bool placedAny = false;
                                for (int step = 0; step < beamLength; ++step) {
                                    const glm::ivec3 beamCell = startCell + dir * step;
                                    if (!inCurrentSection(beamCell)) break;
                                    uint32_t beamId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, beamCell);
                                    if (beamId != depthStoneId && beamId != fallbackStoneId && beamId != depthPurpleDirtId && beamId != depthRustBeamId) {
                                        break;
                                    }
                                    voxelWorld.setBlockLod(0, beamCell, depthRustBeamId, packColor(glm::vec3(0.50f, 0.34f, 0.26f)), false);
                                    placedAny = true;
                                    wroteAny = true;
                                }
                                if (placedAny) {
                                    beamStartsPlaced += 1;
                                }
                            }
                        }
                    }
                }

                // Big 2x2 lilypads on depth rivers.
                const bool hasBigLilypadX =
                    depthBigLilypadProtosX[0] && depthBigLilypadProtosX[1] && depthBigLilypadProtosX[2] && depthBigLilypadProtosX[3];
                const bool hasBigLilypadZ =
                    depthBigLilypadProtosZ[0] && depthBigLilypadProtosZ[1] && depthBigLilypadProtosZ[2] && depthBigLilypadProtosZ[3];
                if ((hasBigLilypadX || hasBigLilypadZ) && depthBigLilypadPercent > 0) {
                    std::unordered_set<uint64_t> occupiedPads;
                    for (int z = sectionMinZ; z <= sectionMaxZ - 1; ++z) {
                        for (int y = sectionMinYDepth; y <= sectionMaxYDepth - 1; ++y) {
                            if (y > unifiedDepthsTopY || y <= unifiedDepthsMinY) continue;
                            for (int x = sectionMinX; x <= sectionMaxX - 1; ++x) {
                                const glm::ivec3 waterNW(x, y, z);
                                const glm::ivec3 waterNE(x + 1, y, z);
                                const glm::ivec3 waterSW(x, y, z + 1);
                                const glm::ivec3 waterSE(x + 1, y, z + 1);
                                if (!isDepthRiverWaterCell(waterNW)
                                    || !isDepthRiverWaterCell(waterNE)
                                    || !isDepthRiverWaterCell(waterSW)
                                    || !isDepthRiverWaterCell(waterSE)) {
                                    continue;
                                }
                                const glm::ivec3 topNW = waterNW + glm::ivec3(0, 1, 0);
                                const glm::ivec3 topNE = waterNE + glm::ivec3(0, 1, 0);
                                const glm::ivec3 topSW = waterSW + glm::ivec3(0, 1, 0);
                                const glm::ivec3 topSE = waterSE + glm::ivec3(0, 1, 0);
                                if (!inCurrentSection(topNW) || !inCurrentSection(topNE) || !inCurrentSection(topSW) || !inCurrentSection(topSE)) continue;
                                if (VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, topNW) != 0u
                                    || VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, topNE) != 0u
                                    || VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, topSW) != 0u
                                    || VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, topSE) != 0u) {
                                    continue;
                                }
                                const uint64_t padKey = (static_cast<uint64_t>(hash3DInt(topNW.x + 409, topNW.y - 733, topNW.z + 997)) << 32u)
                                    | static_cast<uint64_t>(hash3DInt(topNW.x - 149, topNW.y + 503, topNW.z - 181));
                                if (occupiedPads.count(padKey) > 0) continue;
                                const uint32_t seed = hash3DInt(x + 901, y - 617, z + 433);
                                if (static_cast<int>(seed % 100u) >= depthBigLilypadPercent) continue;
                                // Prefer Z-authored set when both exist; its orientation matches
                                // the intended 2x2 texture layout for big lilypads.
                                const auto& protoSet = hasBigLilypadZ ? depthBigLilypadProtosZ : depthBigLilypadProtosX;
                                // Top-face UV on floor mats is mirrored on X in this pipeline, so stamp the
                                // 2x2 quadrants mirrored across X to preserve intended visual ordering.
                                // Desired art order is:
                                // 428 429
                                // 430 431
                                voxelWorld.setBlockLod(0, topNW, static_cast<uint32_t>(protoSet[3]->prototypeID), packColor(glm::vec3(1.0f)), false);
                                voxelWorld.setBlockLod(0, topNE, static_cast<uint32_t>(protoSet[2]->prototypeID), packColor(glm::vec3(1.0f)), false);
                                voxelWorld.setBlockLod(0, topSW, static_cast<uint32_t>(protoSet[1]->prototypeID), packColor(glm::vec3(1.0f)), false);
                                voxelWorld.setBlockLod(0, topSE, static_cast<uint32_t>(protoSet[0]->prototypeID), packColor(glm::vec3(1.0f)), false);
                                occupiedPads.insert(padKey);
                                wroteAny = true;
                            }
                        }
                    }
                }

                // Crystal clusters on depth river banks only.
                if (depthRiverCrystalPercent > 0) {
                    struct CrystalChoice {
                        uint32_t prototypeID = 0u;
                        int weight = 0;
                    };
                    std::vector<CrystalChoice> crystalChoices;
                    crystalChoices.reserve(4);
                    auto addCrystalChoice = [&](const Entity* proto, int weight) {
                        if (!proto || proto->prototypeID <= 0 || weight <= 0) return;
                        crystalChoices.push_back(CrystalChoice{
                            static_cast<uint32_t>(proto->prototypeID),
                            weight
                        });
                    };
                    // Favor small clusters slightly so banks do not become overcrowded with large cards.
                    addCrystalChoice(depthCrystalProto, 36);
                    addCrystalChoice(depthCrystalBlueProto, 30);
                    addCrystalChoice(depthCrystalBlueBigProto, 18);
                    addCrystalChoice(depthCrystalMagentaBigProto, 16);

                    if (!crystalChoices.empty()) {
                        int totalCrystalWeight = 0;
                        for (const CrystalChoice& choice : crystalChoices) totalCrystalWeight += choice.weight;
                        const std::array<glm::ivec3, 4> kSideDirs = {
                            glm::ivec3(1, 0, 0),
                            glm::ivec3(-1, 0, 0),
                            glm::ivec3(0, 0, 1),
                            glm::ivec3(0, 0, -1)
                        };
                        for (int z = sectionMinZ; z <= sectionMaxZ; ++z) {
                            for (int y = sectionMinYDepth; y <= sectionMaxYDepth; ++y) {
                                if (y > unifiedDepthsTopY || y <= unifiedDepthsMinY) continue;
                                for (int x = sectionMinX; x <= sectionMaxX; ++x) {
                                    const glm::ivec3 bankCell(x, y, z);
                                    const uint32_t bankId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, bankCell);
                                    if (!isSolidSupport(bankId)) continue;
                                    if (bankId == waterId || isDepthLavaId(bankId)) continue;

                                    const glm::ivec3 placeCell = bankCell + glm::ivec3(0, 1, 0);
                                    if (!inCurrentSection(placeCell)) continue;
                                    if (VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, placeCell) != 0u) continue;
                                    const glm::ivec3 placeAboveCell = placeCell + glm::ivec3(0, 1, 0);
                                    if (!inCurrentSection(placeAboveCell)) continue;
                                    if (VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, placeAboveCell) != 0u) continue;
                                    // Keep crystals out of submerged placements: the crystal cell and its headroom
                                    // must not be in or directly side-touching water at that same height.
                                    if (isAnyWaterCell(placeCell) || isAnyWaterCell(placeAboveCell)) continue;
                                    bool sideTouchesWaterAtPlacementHeight = false;
                                    for (const glm::ivec3& side : kSideDirs) {
                                        if (isAnyWaterCell(placeCell + side) || isAnyWaterCell(placeAboveCell + side)) {
                                            sideTouchesWaterAtPlacementHeight = true;
                                            break;
                                        }
                                    }
                                    if (sideTouchesWaterAtPlacementHeight) continue;

                                    bool touchesDepthRiver = false;
                                    for (int dz = -depthRiverCrystalBankSearchRadius; dz <= depthRiverCrystalBankSearchRadius && !touchesDepthRiver; ++dz) {
                                        for (int dx = -depthRiverCrystalBankSearchRadius; dx <= depthRiverCrystalBankSearchRadius && !touchesDepthRiver; ++dx) {
                                            for (int down = 0; down <= depthRiverCrystalBankSearchDown; ++down) {
                                                const glm::ivec3 depthOffset(dx, -down, dz);
                                                if (isDepthRiverWaterCell(bankCell + depthOffset)
                                                    || isDepthRiverWaterCell(placeCell + depthOffset)) {
                                                    touchesDepthRiver = true;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                    if (!touchesDepthRiver) continue;

                                    const uint32_t spawnSeed = hash3DInt(x + 1249, y - 811, z + 947);
                                    if (static_cast<int>(spawnSeed % 100u) >= depthRiverCrystalPercent) continue;

                                    int pick = static_cast<int>((spawnSeed >> 8u) % static_cast<uint32_t>(totalCrystalWeight));
                                    uint32_t crystalId = crystalChoices.front().prototypeID;
                                    for (const CrystalChoice& choice : crystalChoices) {
                                        if (pick < choice.weight) {
                                            crystalId = choice.prototypeID;
                                            break;
                                        }
                                        pick -= choice.weight;
                                    }

                                    voxelWorld.setBlockLod(0, placeCell, crystalId, packColor(glm::vec3(1.0f)), false);
                                    wroteAny = true;
                                }
                            }
                        }
                    }
                }

                // Moss decals on cave walls near depth rivers.
                if (depthMossPercent > 0
                    && depthMossWallProtoPosX && depthMossWallProtoNegX
                    && depthMossWallProtoPosZ && depthMossWallProtoNegZ) {
                    static const std::array<glm::ivec3, 4> kSideDirs = {
                        glm::ivec3(1, 0, 0),
                        glm::ivec3(-1, 0, 0),
                        glm::ivec3(0, 0, 1),
                        glm::ivec3(0, 0, -1)
                    };
                    for (int z = sectionMinZ; z <= sectionMaxZ; ++z) {
                        for (int y = sectionMinYDepth; y <= sectionMaxYDepth; ++y) {
                            if (y > unifiedDepthsTopY || y <= unifiedDepthsMinY) continue;
                            for (int x = sectionMinX; x <= sectionMaxX; ++x) {
                                const glm::ivec3 cell(x, y, z);
                                if (VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, cell) != 0u) continue;
                                bool nearRiverWater = false;
                                for (const glm::ivec3& d : kSideDirs) {
                                    if (isDepthRiverWaterCell(cell + d)) {
                                        nearRiverWater = true;
                                        break;
                                    }
                                }
                                if (!nearRiverWater) continue;
                                const uint32_t seed = hash3DInt(x + 701, y - 359, z + 1013);
                                if (static_cast<int>(seed % 100u) >= depthMossPercent) continue;
                                std::array<int, 4> mossCandidates = {-1, -1, -1, -1};
                                int candidateCount = 0;
                                auto trySupport = [&](const glm::ivec3& offset, const Entity* mossProto) {
                                    if (!mossProto) return;
                                    const uint32_t supportId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, cell + offset);
                                    if (!isSolidSupport(supportId)) return;
                                    mossCandidates[static_cast<size_t>(candidateCount)] = mossProto->prototypeID;
                                    candidateCount += 1;
                                };
                                trySupport(glm::ivec3(1, 0, 0), depthMossWallProtoPosX);
                                trySupport(glm::ivec3(-1, 0, 0), depthMossWallProtoNegX);
                                trySupport(glm::ivec3(0, 0, 1), depthMossWallProtoPosZ);
                                trySupport(glm::ivec3(0, 0, -1), depthMossWallProtoNegZ);
                                if (candidateCount <= 0) continue;
                                const int pick = static_cast<int>((seed >> 9u) % static_cast<uint32_t>(candidateCount));
                                const int mossId = mossCandidates[static_cast<size_t>(pick)];
                                if (mossId < 0) continue;
                                voxelWorld.setBlockLod(0, cell, static_cast<uint32_t>(mossId), packColor(glm::vec3(0.56f, 0.72f, 0.48f)), false);
                                wroteAny = true;
                            }
                        }
                    }
                }
            }
            if (outCompleted && lod == 0 && waterfallEnabled) {
                const bool hasWaterSlopeProtos =
                    (waterSlopeProtoPosX && waterSlopeProtoPosX->prototypeID > 0)
                    || (waterSlopeProtoNegX && waterSlopeProtoNegX->prototypeID > 0)
                    || (waterSlopeProtoPosZ && waterSlopeProtoPosZ->prototypeID > 0)
                    || (waterSlopeProtoNegZ && waterSlopeProtoNegZ->prototypeID > 0)
                    || (waterSlopeCornerProtoPosXPosZ && waterSlopeCornerProtoPosXPosZ->prototypeID > 0)
                    || (waterSlopeCornerProtoPosXNegZ && waterSlopeCornerProtoPosXNegZ->prototypeID > 0)
                    || (waterSlopeCornerProtoNegXPosZ && waterSlopeCornerProtoNegXPosZ->prototypeID > 0)
                    || (waterSlopeCornerProtoNegXNegZ && waterSlopeCornerProtoNegXNegZ->prototypeID > 0);
                if (hasWaterSlopeProtos) {
                    const int sectionMinX = sectionCoord.x * size;
                    const int sectionMaxX = sectionMinX + size - 1;
                    const int sectionMinZ = sectionCoord.z * size;
                    const int sectionMaxZ = sectionMinZ + size - 1;
                    auto slopePrototypeForExposedAir = [&](const glm::ivec3& dir) -> int {
                        // Exposed air marks the downhill side. Match runtime slope semantics:
                        // PosX/PosZ slope names are downhill toward -X/-Z respectively.
                        if (dir.x > 0) return waterSlopeProtoNegX ? waterSlopeProtoNegX->prototypeID : -1;
                        if (dir.x < 0) return waterSlopeProtoPosX ? waterSlopeProtoPosX->prototypeID : -1;
                        if (dir.z > 0) return waterSlopeProtoNegZ ? waterSlopeProtoNegZ->prototypeID : -1;
                        if (dir.z < 0) return waterSlopeProtoPosZ ? waterSlopeProtoPosZ->prototypeID : -1;
                        return -1;
                    };
                    auto cornerSlopePrototypeForAirPair = [&](const glm::ivec3& dirA, const glm::ivec3& dirB) -> int {
                        const int sumX = dirA.x + dirB.x;
                        const int sumZ = dirA.z + dirB.z;
                        if (sumX > 0 && sumZ > 0) {
                            return waterSlopeCornerProtoNegXNegZ ? waterSlopeCornerProtoNegXNegZ->prototypeID : -1;
                        }
                        if (sumX > 0 && sumZ < 0) {
                            return waterSlopeCornerProtoNegXPosZ ? waterSlopeCornerProtoNegXPosZ->prototypeID : -1;
                        }
                        if (sumX < 0 && sumZ > 0) {
                            return waterSlopeCornerProtoPosXNegZ ? waterSlopeCornerProtoPosXNegZ->prototypeID : -1;
                        }
                        if (sumX < 0 && sumZ < 0) {
                            return waterSlopeCornerProtoPosXPosZ ? waterSlopeCornerProtoPosXPosZ->prototypeID : -1;
                        }
                        return -1;
                    };
                    auto inCurrentSectionXZ = [&](const glm::ivec3& cell) {
                        return cell.x >= sectionMinX && cell.x <= sectionMaxX
                            && cell.z >= sectionMinZ && cell.z <= sectionMaxZ;
                    };
                    auto stopsWaterfallDrop = [&](uint32_t id) {
                        if (id == 0u) return false;
                        if (id >= static_cast<uint32_t>(prototypes.size())) return true;
                        return prototypes[static_cast<size_t>(id)].isSolid;
                    };
                    auto isWaterOrWaterSlopeId = [&](uint32_t id) {
                        if (id == static_cast<uint32_t>(waterProto->prototypeID)) return true;
                        if (waterSlopeProtoPosX && id == static_cast<uint32_t>(waterSlopeProtoPosX->prototypeID)) return true;
                        if (waterSlopeProtoNegX && id == static_cast<uint32_t>(waterSlopeProtoNegX->prototypeID)) return true;
                        if (waterSlopeProtoPosZ && id == static_cast<uint32_t>(waterSlopeProtoPosZ->prototypeID)) return true;
                        if (waterSlopeProtoNegZ && id == static_cast<uint32_t>(waterSlopeProtoNegZ->prototypeID)) return true;
                        if (waterSlopeCornerProtoPosXPosZ && id == static_cast<uint32_t>(waterSlopeCornerProtoPosXPosZ->prototypeID)) return true;
                        if (waterSlopeCornerProtoPosXNegZ && id == static_cast<uint32_t>(waterSlopeCornerProtoPosXNegZ->prototypeID)) return true;
                        if (waterSlopeCornerProtoNegXPosZ && id == static_cast<uint32_t>(waterSlopeCornerProtoNegXPosZ->prototypeID)) return true;
                        if (waterSlopeCornerProtoNegXNegZ && id == static_cast<uint32_t>(waterSlopeCornerProtoNegXNegZ->prototypeID)) return true;
                        return false;
                    };

                    auto carveWaterfallImpactPool = [&](const glm::ivec3& solidImpactCell,
                                                        const glm::ivec3& lastWaterCell,
                                                        uint32_t waterColor,
                                                        int totalFallDistance,
                                                        std::vector<glm::ivec3>* outCascadeSources) {
                        if (totalFallDistance <= 5) return false;
                        const int extraFall = totalFallDistance - 5;
                        const int poolRadius = std::max(1, std::min(8, 1 + extraFall / 10));
                        const int poolDepthBase = std::max(1, std::min(5, 1 + extraFall / 14));
                        // Start pool one block into terrain (at impact-solid height),
                        // not at the previous water surface cell.
                        const int surfaceY = std::min(lastWaterCell.y - 1, solidImpactCell.y);
                        bool carvedAny = false;
                        std::vector<glm::ivec3> carvedCells;
                        carvedCells.reserve(static_cast<size_t>((poolRadius * 2 + 1) * (poolRadius * 2 + 1)));
                        for (int dz = -poolRadius; dz <= poolRadius; ++dz) {
                            for (int dx = -poolRadius; dx <= poolRadius; ++dx) {
                                const float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
                                if (dist > static_cast<float>(poolRadius) + 0.25f) continue;
                                const int localDepth = std::max(
                                    1,
                                    poolDepthBase - static_cast<int>(std::floor(dist * 0.85f))
                                );
                                for (int depthStep = 0; depthStep < localDepth; ++depthStep) {
                                    const glm::ivec3 carveCell(
                                        solidImpactCell.x + dx,
                                        surfaceY - depthStep,
                                        solidImpactCell.z + dz
                                    );
                                    const uint32_t carveId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, carveCell);
                                    if (isWaterOrWaterSlopeId(carveId)) continue;
                                    bool replaceCell = false;
                                    if (carveId == 0u) {
                                        replaceCell = true;
                                    } else if (carveId < static_cast<uint32_t>(prototypes.size())) {
                                        replaceCell = prototypes[static_cast<size_t>(carveId)].isSolid;
                                    }
                                    if (!replaceCell) continue;
                                    voxelWorld.setBlockLod(
                                        0,
                                        carveCell,
                                        static_cast<uint32_t>(waterProto->prototypeID),
                                        waterColor,
                                        false
                                    );
                                    wroteAny = true;
                                    carvedAny = true;
                                    carvedCells.push_back(carveCell);
                                }
                            }
                        }
                        if (carvedAny && outCascadeSources) {
                            static const std::array<glm::ivec3, 4> kPoolSideDirs = {
                                glm::ivec3(1, 0, 0),
                                glm::ivec3(-1, 0, 0),
                                glm::ivec3(0, 0, 1),
                                glm::ivec3(0, 0, -1)
                            };
                            std::unordered_set<uint64_t> seen;
                            seen.reserve(carvedCells.size());
                            for (const glm::ivec3& cell : carvedCells) {
                                if (!inCurrentSectionXZ(cell)) continue;
                                if (cell.y < sectionMinY || cell.y > sectionMaxY) continue;
                                const uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, cell);
                                if (id != static_cast<uint32_t>(waterProto->prototypeID)) continue;
                                bool hasAirSide = false;
                                for (const glm::ivec3& dir : kPoolSideDirs) {
                                    const glm::ivec3 sideCell = cell + dir;
                                    if (!inCurrentSectionXZ(sideCell)) continue;
                                    if (sideCell.y < sectionMinY || sideCell.y > sectionMaxY) continue;
                                    const uint32_t sideId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, sideCell);
                                    if (sideId == 0u && slopePrototypeForExposedAir(dir) > 0) {
                                        hasAirSide = true;
                                        break;
                                    }
                                }
                                if (!hasAirSide) continue;
                                const uint32_t h0 = hash3DInt(cell.x + 991, cell.y - 1543, cell.z + 2713);
                                const uint32_t h1 = hash3DInt(cell.x - 3119, cell.y + 733, cell.z - 4723);
                                const uint64_t key = (static_cast<uint64_t>(h0) << 32u) | static_cast<uint64_t>(h1);
                                if (!seen.insert(key).second) continue;
                                outCascadeSources->push_back(cell);
                            }
                        }
                        return carvedAny;
                    };

                    static const std::array<glm::ivec3, 4> kSideDirs = {
                        glm::ivec3(1, 0, 0),
                        glm::ivec3(-1, 0, 0),
                        glm::ivec3(0, 0, 1),
                        glm::ivec3(0, 0, -1)
                    };
                    auto makeCascadeSourceKey = [&](const glm::ivec3& cell) -> uint64_t {
                        const uint32_t h0 = hash3DInt(cell.x + 1021, cell.y - 4093, cell.z + 7993);
                        const uint32_t h1 = hash3DInt(cell.x - 5153, cell.y + 1237, cell.z - 6949);
                        return (static_cast<uint64_t>(h0) << 32u) | static_cast<uint64_t>(h1);
                    };
                    struct CascadeSourceSeed {
                        glm::ivec3 cell = glm::ivec3(0);
                        int totalFallDistance = 0;
                        bool hasPendingPool = false;
                        glm::ivec3 pendingPoolImpactCell = glm::ivec3(0);
                        glm::ivec3 pendingPoolLastWaterCell = glm::ivec3(0);
                        uint32_t pendingPoolWaterColor = 0u;
                    };
                    std::unordered_map<uint64_t, int> cascadeBestFallForSource;
                    cascadeBestFallForSource.reserve(static_cast<size_t>(size * size));
                    auto runCascadeFromSource = [&](const glm::ivec3& initialSource,
                                                    uint32_t fallbackColor,
                                                    int initialFallDistance,
                                                    bool hasPendingPool,
                                                    const glm::ivec3& pendingPoolImpactCell,
                                                    const glm::ivec3& pendingPoolLastWaterCell,
                                                    uint32_t pendingPoolWaterColor) {
                        std::vector<CascadeSourceSeed> queue;
                        queue.reserve(32);
                        queue.push_back({
                            initialSource,
                            std::max(0, initialFallDistance),
                            hasPendingPool,
                            pendingPoolImpactCell,
                            pendingPoolLastWaterCell,
                            pendingPoolWaterColor
                        });
                        size_t head = 0;
                        int budgetRemaining = waterfallCascadeBudget;
                        while (head < queue.size() && budgetRemaining > 0) {
                            const CascadeSourceSeed sourceSeed = queue[head++];
                            const glm::ivec3 sourceCell = sourceSeed.cell;
                            const int sourceTotalFallDistance = std::max(0, sourceSeed.totalFallDistance);
                            budgetRemaining -= 1;
                            if (!inCurrentSectionXZ(sourceCell)) continue;
                            if (sourceCell.y < sectionMinY || sourceCell.y > sectionMaxY) continue;
                            const uint64_t sourceKey = makeCascadeSourceKey(sourceCell);
                            auto bestIt = cascadeBestFallForSource.find(sourceKey);
                            if (bestIt != cascadeBestFallForSource.end() && bestIt->second >= sourceTotalFallDistance) continue;
                            cascadeBestFallForSource[sourceKey] = sourceTotalFallDistance;
                            const uint32_t sourceId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, sourceCell);
                            if (sourceId != static_cast<uint32_t>(waterProto->prototypeID)) {
                                if (sourceSeed.hasPendingPool) {
                                    const uint32_t poolColor = (sourceSeed.pendingPoolWaterColor & 0x00ffffffu) != 0u
                                        ? sourceSeed.pendingPoolWaterColor
                                        : fallbackColor;
                                    std::vector<glm::ivec3> poolCascadeSources;
                                    (void)carveWaterfallImpactPool(
                                        sourceSeed.pendingPoolImpactCell,
                                        sourceSeed.pendingPoolLastWaterCell,
                                        poolColor,
                                        sourceTotalFallDistance,
                                        &poolCascadeSources
                                    );
                                    for (const glm::ivec3& poolSource : poolCascadeSources) {
                                        queue.push_back({
                                            poolSource,
                                            sourceTotalFallDistance,
                                            false,
                                            glm::ivec3(0),
                                            glm::ivec3(0),
                                            0u
                                        });
                                    }
                                }
                                continue;
                            }
                            uint32_t sourceWaterColor = VoxelMeshInitSystemLogic::GetVoxelColorAtLod(voxelWorld, 0, sourceCell);
                            if ((sourceWaterColor & 0x00ffffffu) == 0u) sourceWaterColor = fallbackColor;
                            bool emittedCascade = false;
                            auto emitCascadeSlope = [&](const glm::ivec3& slopeCell, int slopePrototypeID) -> bool {
                                if (!inCurrentSectionXZ(slopeCell)) return false;
                                if (slopeCell.y < sectionMinY || slopeCell.y > sectionMaxY) return false;
                                if (slopePrototypeID <= 0) return false;
                                if (VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, slopeCell) != 0u) return false;

                                glm::ivec3 dropCell = slopeCell + glm::ivec3(0, -1, 0);
                                glm::ivec3 terminationCell = sourceCell;
                                bool terminatedBySolid = false;
                                int placedCount = 0;
                                int traversedCount = 0;
                                for (int dropStep = 0; dropStep < waterfallMaxDrop && dropCell.y >= sectionMinY; ++dropStep) {
                                    const uint32_t dropId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, dropCell);
                                    if (isWaterOrWaterSlopeId(dropId)) {
                                        terminationCell = dropCell;
                                        traversedCount += 1;
                                        dropCell.y -= 1;
                                        continue;
                                    }
                                    if (stopsWaterfallDrop(dropId)) {
                                        terminatedBySolid = true;
                                        break;
                                    }
                                    voxelWorld.setBlockLod(
                                        0,
                                        dropCell,
                                        static_cast<uint32_t>(waterProto->prototypeID),
                                        sourceWaterColor,
                                        false
                                    );
                                    wroteAny = true;
                                    placedCount += 1;
                                    terminationCell = dropCell;
                                    traversedCount += 1;
                                    dropCell.y -= 1;
                                }

                                if (placedCount <= 0) return false;

                                const int totalFallDistance = sourceTotalFallDistance + traversedCount;

                                voxelWorld.setBlockLod(
                                    0,
                                    slopeCell,
                                    static_cast<uint32_t>(slopePrototypeID),
                                    sourceWaterColor,
                                    false
                                );
                                wroteAny = true;

                                if (terminatedBySolid
                                    && terminationCell.y >= sectionMinY && terminationCell.y <= sectionMaxY
                                    && inCurrentSectionXZ(terminationCell)) {
                                    queue.push_back({
                                        terminationCell,
                                        totalFallDistance,
                                        true,
                                        dropCell,
                                        terminationCell,
                                        sourceWaterColor
                                    });
                                }
                                return true;
                            };

                            std::array<glm::ivec3, 2> cornerAirDirs{
                                glm::ivec3(0),
                                glm::ivec3(0)
                            };
                            int cornerAirCount = 0;
                            int cornerSolidCount = 0;
                            for (const glm::ivec3& dir : kSideDirs) {
                                const glm::ivec3 sideCell = sourceCell + dir;
                                if (!inCurrentSectionXZ(sideCell)) continue;
                                if (sideCell.y < sectionMinY || sideCell.y > sectionMaxY) continue;
                                const uint32_t sideId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, sideCell);
                                if (sideId == 0u && slopePrototypeForExposedAir(dir) > 0) {
                                    if (cornerAirCount < 2) cornerAirDirs[static_cast<size_t>(cornerAirCount)] = dir;
                                    cornerAirCount += 1;
                                } else if (stopsWaterfallDrop(sideId)) {
                                    cornerSolidCount += 1;
                                }
                            }

                            bool usedCornerSlope = false;
                            if (cornerAirCount == 2 && cornerSolidCount == 2) {
                                const glm::ivec3 dirA = cornerAirDirs[0];
                                const glm::ivec3 dirB = cornerAirDirs[1];
                                const int dot = dirA.x * dirB.x + dirA.z * dirB.z;
                                if (dot == 0) {
                                    const int cornerSlopePrototypeID = cornerSlopePrototypeForAirPair(dirA, dirB);
                                    if (cornerSlopePrototypeID > 0) {
                                        const glm::ivec3 cornerCell = sourceCell + dirA + dirB;
                                        usedCornerSlope = emitCascadeSlope(cornerCell, cornerSlopePrototypeID);
                                    }
                                }
                            }

                            emittedCascade = emittedCascade || usedCornerSlope;

                            if (!usedCornerSlope) {
                                for (const glm::ivec3& dir : kSideDirs) {
                                    const glm::ivec3 slopeCell = sourceCell + dir;
                                    const int slopePrototypeID = slopePrototypeForExposedAir(dir);
                                    if (emitCascadeSlope(slopeCell, slopePrototypeID)) {
                                        emittedCascade = true;
                                    }
                                }
                            }

                            if (sourceSeed.hasPendingPool && !emittedCascade) {
                                const uint32_t poolColor = (sourceSeed.pendingPoolWaterColor & 0x00ffffffu) != 0u
                                    ? sourceSeed.pendingPoolWaterColor
                                    : sourceWaterColor;
                                std::vector<glm::ivec3> poolCascadeSources;
                                (void)carveWaterfallImpactPool(
                                    sourceSeed.pendingPoolImpactCell,
                                    sourceSeed.pendingPoolLastWaterCell,
                                    poolColor,
                                    sourceTotalFallDistance,
                                    &poolCascadeSources
                                );
                                for (const glm::ivec3& poolSource : poolCascadeSources) {
                                    queue.push_back({
                                        poolSource,
                                        sourceTotalFallDistance,
                                        false,
                                        glm::ivec3(0),
                                        glm::ivec3(0),
                                        0u
                                    });
                                }
                            }
                        }
                    };

                    std::vector<CascadeSourceSeed> continuationTerminationSeeds;
                    continuationTerminationSeeds.reserve(static_cast<size_t>(size * size / 2));

                    // Continue waterfalls entering from the section above.
                    // This prevents hanging columns when lower sections generate after upper ones.
                    for (int worldZ = sectionMinZ; worldZ <= sectionMaxZ; ++worldZ) {
                        for (int worldX = sectionMinX; worldX <= sectionMaxX; ++worldX) {
                            const glm::ivec3 aboveCell(worldX, sectionMaxY + 1, worldZ);
                            const uint32_t aboveId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, aboveCell);
                            if (!isWaterOrWaterSlopeId(aboveId)) continue;
                            uint32_t carryWaterColor = VoxelMeshInitSystemLogic::GetVoxelColorAtLod(voxelWorld, 0, aboveCell);
                            if ((carryWaterColor & 0x00ffffffu) == 0u) {
                                carryWaterColor = packedWaterColorUnknown;
                            }

                            glm::ivec3 dropCell(worldX, sectionMaxY, worldZ);
                            glm::ivec3 terminationCell(worldX, sectionMaxY, worldZ);
                            bool touchedSectionWater = false;
                            bool terminatedBySolid = false;
                            int traversedCount = 0;
                            for (int dropStep = 0; dropStep < waterfallMaxDrop && dropCell.y >= sectionMinY; ++dropStep) {
                                const uint32_t dropId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, dropCell);
                                if (isWaterOrWaterSlopeId(dropId)) {
                                    terminationCell = dropCell;
                                    touchedSectionWater = true;
                                    traversedCount += 1;
                                    dropCell.y -= 1;
                                    continue;
                                }
                                if (stopsWaterfallDrop(dropId)) {
                                    terminatedBySolid = true;
                                    break;
                                }
                                voxelWorld.setBlockLod(
                                    0,
                                    dropCell,
                                    static_cast<uint32_t>(waterProto->prototypeID),
                                    carryWaterColor,
                                    false
                                );
                                wroteAny = true;
                                terminationCell = dropCell;
                                touchedSectionWater = true;
                                traversedCount += 1;
                                dropCell.y -= 1;
                            }

                            if (terminatedBySolid && touchedSectionWater
                                && terminationCell.y >= sectionMinY && terminationCell.y <= sectionMaxY) {
                                continuationTerminationSeeds.push_back({
                                    terminationCell,
                                    std::max(0, traversedCount),
                                    true,
                                    dropCell,
                                    terminationCell,
                                    carryWaterColor
                                });
                            }
                        }
                    }

                    // Cascade sources:
                    // - top-exposed water in this section
                    for (int worldY = sectionMinY; worldY <= sectionMaxY; ++worldY) {
                        for (int worldZ = sectionMinZ; worldZ <= sectionMaxZ; ++worldZ) {
                            for (int worldX = sectionMinX; worldX <= sectionMaxX; ++worldX) {
                                const glm::ivec3 sourceCell(worldX, worldY, worldZ);
                                const uint32_t sourceId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, sourceCell);
                                if (sourceId != static_cast<uint32_t>(waterProto->prototypeID)) continue;
                                const uint32_t aboveId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(
                                    voxelWorld,
                                    0,
                                    sourceCell + glm::ivec3(0, 1, 0)
                                );
                                const bool topExposed = !isWaterOrWaterSlopeId(aboveId);
                                if (!topExposed) continue;
                                uint32_t sourceWaterColor = VoxelMeshInitSystemLogic::GetVoxelColorAtLod(voxelWorld, 0, sourceCell);
                                if ((sourceWaterColor & 0x00ffffffu) == 0u) sourceWaterColor = packedWaterColorUnknown;
                                runCascadeFromSource(
                                    sourceCell,
                                    sourceWaterColor,
                                    0,
                                    false,
                                    glm::ivec3(0),
                                    glm::ivec3(0),
                                    0u
                                );
                            }
                        }
                    }

                    for (const CascadeSourceSeed& seed : continuationTerminationSeeds) {
                        uint32_t seedWaterColor = VoxelMeshInitSystemLogic::GetVoxelColorAtLod(voxelWorld, 0, seed.cell);
                        if ((seedWaterColor & 0x00ffffffu) == 0u) seedWaterColor = packedWaterColorUnknown;
                        runCascadeFromSource(
                            seed.cell,
                            seedWaterColor,
                            seed.totalFallDistance,
                            seed.hasPendingPool,
                            seed.pendingPoolImpactCell,
                            seed.pendingPoolLastWaterCell,
                            seed.pendingPoolWaterColor
                        );
                    }
                }
            }
            if (outCompleted
                && lod == 0
                && chalkEnabled
                && chalkProto
                && chalkProto->prototypeID > 0
                && !pendingChalkPlacements.empty()) {
                const int sectionMinX = sectionCoord.x * size;
                const int sectionMaxX = sectionMinX + size - 1;
                const int sectionMinY = sectionCoord.y * size;
                const int sectionMaxY = sectionMinY + size - 1;
                const int sectionMinZ = sectionCoord.z * size;
                const int sectionMaxZ = sectionMinZ + size - 1;

                auto inCurrentSection = [&](const glm::ivec3& cell) {
                    return cell.x >= sectionMinX && cell.x <= sectionMaxX
                        && cell.y >= sectionMinY && cell.y <= sectionMaxY
                        && cell.z >= sectionMinZ && cell.z <= sectionMaxZ;
                };
                auto isWaterSlopeId = [&](uint32_t id) {
                    if (id == 0u) return false;
                    if (waterSlopeProtoPosX && id == static_cast<uint32_t>(waterSlopeProtoPosX->prototypeID)) return true;
                    if (waterSlopeProtoNegX && id == static_cast<uint32_t>(waterSlopeProtoNegX->prototypeID)) return true;
                    if (waterSlopeProtoPosZ && id == static_cast<uint32_t>(waterSlopeProtoPosZ->prototypeID)) return true;
                    if (waterSlopeProtoNegZ && id == static_cast<uint32_t>(waterSlopeProtoNegZ->prototypeID)) return true;
                    if (waterSlopeCornerProtoPosXPosZ && id == static_cast<uint32_t>(waterSlopeCornerProtoPosXPosZ->prototypeID)) return true;
                    if (waterSlopeCornerProtoPosXNegZ && id == static_cast<uint32_t>(waterSlopeCornerProtoPosXNegZ->prototypeID)) return true;
                    if (waterSlopeCornerProtoNegXPosZ && id == static_cast<uint32_t>(waterSlopeCornerProtoNegXPosZ->prototypeID)) return true;
                    if (waterSlopeCornerProtoNegXNegZ && id == static_cast<uint32_t>(waterSlopeCornerProtoNegXNegZ->prototypeID)) return true;
                    return false;
                };
                auto isWaterOrSlopeId = [&](uint32_t id) {
                    if (id == static_cast<uint32_t>(waterProto->prototypeID)) return true;
                    return isWaterSlopeId(id);
                };
                static const std::array<glm::ivec3, 4> kHorizontalDirs = {
                    glm::ivec3(1, 0, 0),
                    glm::ivec3(-1, 0, 0),
                    glm::ivec3(0, 0, 1),
                    glm::ivec3(0, 0, -1)
                };
                auto isFallingWaterCell = [&](const glm::ivec3& cell) {
                    const uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, cell);
                    if (isWaterSlopeId(id)) return true;
                    if (id != static_cast<uint32_t>(waterProto->prototypeID)) return false;
                    const uint32_t belowId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, cell + glm::ivec3(0, -1, 0));
                    if (!isWaterOrSlopeId(belowId)) return false;
                    for (const glm::ivec3& dir : kHorizontalDirs) {
                        const uint32_t sideId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, cell + dir);
                        if (sideId == 0u) return true;
                    }
                    return false;
                };
                auto hasNearbyFallingWater = [&](const glm::ivec3& groundCell) {
                    constexpr int kSearchRadiusXZ = 2;
                    constexpr int kSearchMinDY = -6;
                    constexpr int kSearchMaxDY = 6;
                    for (int dy = kSearchMinDY; dy <= kSearchMaxDY; ++dy) {
                        for (int dz = -kSearchRadiusXZ; dz <= kSearchRadiusXZ; ++dz) {
                            for (int dx = -kSearchRadiusXZ; dx <= kSearchRadiusXZ; ++dx) {
                                const glm::ivec3 sampleCell = groundCell + glm::ivec3(dx, dy, dz);
                                if (!inCurrentSection(sampleCell)) continue;
                                if (isFallingWaterCell(sampleCell)) return true;
                            }
                        }
                    }
                    return false;
                };

                for (const glm::ivec3& chalkCell : pendingChalkPlacements) {
                    if (!inCurrentSection(chalkCell)) continue;
                    const glm::ivec3 aboveCell = chalkCell + glm::ivec3(0, 1, 0);
                    const uint32_t currentId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, chalkCell);
                    const uint32_t aboveId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, aboveCell);
                    if (currentId == 0u || currentId == static_cast<uint32_t>(waterProto->prototypeID)) continue;
                    if (aboveId != 0u) continue;
                    if (!hasNearbyFallingWater(chalkCell)) continue;

                    voxelWorld.setBlockLod(
                        0,
                        chalkCell,
                        static_cast<uint32_t>(chalkProto->prototypeID),
                        packColor(chalkColor),
                        false
                    );
                    wroteAny = true;

                    if (chalkStickSpawnPercent <= 0) continue;
                    if (!inCurrentSection(aboveCell)) continue;
                    if (VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, aboveCell) != 0u) continue;
                    const uint32_t stickSeed = hash3DInt(
                        chalkCell.x + chalkSeed * 11,
                        aboveCell.y + chalkSeed * 13,
                        chalkCell.z - chalkSeed * 17
                    );
                    if (static_cast<int>((stickSeed >> 3u) % 100u) >= chalkStickSpawnPercent) continue;
                    int stickID = (((stickSeed >> 9u) & 1u) == 0u)
                        ? (chalkStickProtoX ? chalkStickProtoX->prototypeID : -1)
                        : (chalkStickProtoZ ? chalkStickProtoZ->prototypeID : -1);
                    if (stickID < 0) {
                        stickID = chalkStickProtoX ? chalkStickProtoX->prototypeID
                            : (chalkStickProtoZ ? chalkStickProtoZ->prototypeID : -1);
                    }
                    if (stickID < 0) continue;
                    voxelWorld.setBlockLod(
                        0,
                        aboveCell,
                        static_cast<uint32_t>(stickID),
                        packColor(chalkColor),
                        false
                    );
                    wroteAny = true;
                }
            }
            if (outCompleted
                && lod == 0
                && isExpanseLevel
                && unifiedDepthsEnabled
                && depthLavaFloorEnabled
                && !depthLavaTileProtos.empty()) {
                const bool depthLavaCascadeEnabled = getRegistryBool(baseSystem, "DepthLavaCascadeEnabled", true);
                if (depthLavaCascadeEnabled) {
                    const int depthLavaCascadeMaxDrop = std::max(
                        8,
                        getRegistryInt(baseSystem, "DepthLavaCascadeMaxDrop", 300)
                    );
                    const int sectionMinX = sectionCoord.x * size;
                    const int sectionMaxX = sectionMinX + size - 1;
                    const int sectionMinY = sectionCoord.y * size;
                    const int sectionMaxY = sectionMinY + size - 1;
                    const int sectionMinZ = sectionCoord.z * size;
                    const int sectionMaxZ = sectionMinZ + size - 1;
                    const uint32_t packedDepthLavaColor = packColor(lavaColor);

                    auto inCurrentSectionXZ = [&](const glm::ivec3& cell) {
                        return cell.x >= sectionMinX && cell.x <= sectionMaxX
                            && cell.z >= sectionMinZ && cell.z <= sectionMaxZ;
                    };
                    auto isDepthLavaId = [&](uint32_t id) {
                        if (id == 0u) return false;
                        for (const Entity* lavaProto : depthLavaTileProtos) {
                            if (lavaProto && lavaProto->prototypeID > 0
                                && id == static_cast<uint32_t>(lavaProto->prototypeID)) {
                                return true;
                            }
                        }
                        return false;
                    };
                    auto canReplaceWithDepthLava = [&](uint32_t id) {
                        if (id == 0u) return true;
                        if (isDepthLavaId(id)) return false;
                        if (id >= static_cast<uint32_t>(prototypes.size())) return false;
                        return prototypes[static_cast<size_t>(id)].isBlock;
                    };
                    auto depthLavaTileIdFor = [&](int wx, int wz) -> uint32_t {
                        const int tx = positiveMod(wx, 3);
                        const int tz = positiveMod(wz, 3);
                        const int tileIdx = tz * 3 + tx;
                        const Entity* lavaTileProto = depthLavaTileProtos[static_cast<size_t>(tileIdx)];
                        if (lavaTileProto && lavaTileProto->prototypeID > 0) {
                            return static_cast<uint32_t>(lavaTileProto->prototypeID);
                        }
                        return static_cast<uint32_t>(waterProto->prototypeID);
                    };
                    auto dropDepthLavaFromSource = [&](const glm::ivec3& sourceCell, uint32_t fallbackColor) {
                        if (!inCurrentSectionXZ(sourceCell)) return;
                        if (sourceCell.y < sectionMinY || sourceCell.y > sectionMaxY) return;
                        glm::ivec3 dropCell = sourceCell + glm::ivec3(0, -1, 0);
                        uint32_t useColor = (fallbackColor & 0x00ffffffu) != 0u
                            ? fallbackColor
                            : packedDepthLavaColor;
                        for (int dropStep = 0; dropStep < depthLavaCascadeMaxDrop && dropCell.y >= sectionMinY; ++dropStep) {
                            if (dropCell.y <= unifiedDepthsMinY) break;
                            if (dropCell.y >= unifiedDepthsTopY) {
                                dropCell.y -= 1;
                                continue;
                            }
                            const uint32_t dropId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, dropCell);
                            if (isDepthLavaId(dropId)) {
                                dropCell.y -= 1;
                                continue;
                            }
                            if (!canReplaceWithDepthLava(dropId)) {
                                break;
                            }
                            voxelWorld.setBlockLod(
                                0,
                                dropCell,
                                depthLavaTileIdFor(dropCell.x, dropCell.z),
                                useColor,
                                false
                            );
                            wroteAny = true;
                            dropCell.y -= 1;
                        }
                    };

                    // Continue depth-lava drops entering from the section above.
                    for (int worldZ = sectionMinZ; worldZ <= sectionMaxZ; ++worldZ) {
                        for (int worldX = sectionMinX; worldX <= sectionMaxX; ++worldX) {
                            const glm::ivec3 aboveCell(worldX, sectionMaxY + 1, worldZ);
                            const uint32_t aboveId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, aboveCell);
                            if (!isDepthLavaId(aboveId)) continue;
                            uint32_t carryColor = VoxelMeshInitSystemLogic::GetVoxelColorAtLod(voxelWorld, 0, aboveCell);
                            if ((carryColor & 0x00ffffffu) == 0u) carryColor = packedDepthLavaColor;
                            dropDepthLavaFromSource(glm::ivec3(worldX, sectionMaxY, worldZ), carryColor);
                        }
                    }

                    // Start drops from the highest depth-lava source in each X/Z column.
                    for (int worldZ = sectionMinZ; worldZ <= sectionMaxZ; ++worldZ) {
                        for (int worldX = sectionMinX; worldX <= sectionMaxX; ++worldX) {
                            for (int worldY = sectionMaxY; worldY >= sectionMinY; --worldY) {
                                if (worldY <= unifiedDepthsMinY || worldY >= unifiedDepthsTopY) continue;
                                const glm::ivec3 sourceCell(worldX, worldY, worldZ);
                                const uint32_t sourceId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, sourceCell);
                                if (!isDepthLavaId(sourceId)) continue;
                                uint32_t sourceColor = VoxelMeshInitSystemLogic::GetVoxelColorAtLod(voxelWorld, 0, sourceCell);
                                if ((sourceColor & 0x00ffffffu) == 0u) sourceColor = packedDepthLavaColor;
                                dropDepthLavaFromSource(sourceCell, sourceColor);
                                break;
                            }
                        }
                    }
                }
            }
            if (outCompleted
                && lod == 0
                && obsidianProto
                && obsidianProto->prototypeID > 0
                && waterProto
                && waterProto->prototypeID > 0) {
                const int sectionMinX = sectionCoord.x * size;
                const int sectionMaxX = sectionMinX + size - 1;
                const int sectionMinY = sectionCoord.y * size;
                const int sectionMaxY = sectionMinY + size - 1;
                const int sectionMinZ = sectionCoord.z * size;
                const int sectionMaxZ = sectionMinZ + size - 1;

                auto isWaterLikeId = [&](uint32_t id) {
                    if (id == 0u) return false;
                    if (id == static_cast<uint32_t>(waterProto->prototypeID)) return true;
                    if (waterSlopeProtoPosX && id == static_cast<uint32_t>(waterSlopeProtoPosX->prototypeID)) return true;
                    if (waterSlopeProtoNegX && id == static_cast<uint32_t>(waterSlopeProtoNegX->prototypeID)) return true;
                    if (waterSlopeProtoPosZ && id == static_cast<uint32_t>(waterSlopeProtoPosZ->prototypeID)) return true;
                    if (waterSlopeProtoNegZ && id == static_cast<uint32_t>(waterSlopeProtoNegZ->prototypeID)) return true;
                    if (waterSlopeCornerProtoPosXPosZ && id == static_cast<uint32_t>(waterSlopeCornerProtoPosXPosZ->prototypeID)) return true;
                    if (waterSlopeCornerProtoPosXNegZ && id == static_cast<uint32_t>(waterSlopeCornerProtoPosXNegZ->prototypeID)) return true;
                    if (waterSlopeCornerProtoNegXPosZ && id == static_cast<uint32_t>(waterSlopeCornerProtoNegXPosZ->prototypeID)) return true;
                    if (waterSlopeCornerProtoNegXNegZ && id == static_cast<uint32_t>(waterSlopeCornerProtoNegXNegZ->prototypeID)) return true;
                    return false;
                };
                auto isLavaLikeId = [&](uint32_t id) {
                    if (id == 0u || id >= static_cast<uint32_t>(prototypes.size())) return false;
                    const std::string& name = prototypes[static_cast<size_t>(id)].name;
                    return name == "LavaBlockTex"
                        || name == "Lava"
                        || name.rfind("DepthLavaTile", 0) == 0;
                };
                static const std::array<glm::ivec3, 6> kNeighborDirs = {
                    glm::ivec3(1, 0, 0),
                    glm::ivec3(-1, 0, 0),
                    glm::ivec3(0, 1, 0),
                    glm::ivec3(0, -1, 0),
                    glm::ivec3(0, 0, 1),
                    glm::ivec3(0, 0, -1)
                };

                std::vector<glm::ivec3> toObsidian;
                toObsidian.reserve(static_cast<size_t>(size * size));
                for (int worldY = sectionMinY; worldY <= sectionMaxY; ++worldY) {
                    for (int worldZ = sectionMinZ; worldZ <= sectionMaxZ; ++worldZ) {
                        for (int worldX = sectionMinX; worldX <= sectionMaxX; ++worldX) {
                            const glm::ivec3 cell(worldX, worldY, worldZ);
                            const uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, cell);
                            const bool waterLike = isWaterLikeId(id);
                            const bool lavaLike = isLavaLikeId(id);
                            if (!waterLike && !lavaLike) continue;
                            bool adjacentOpposite = false;
                            for (const glm::ivec3& d : kNeighborDirs) {
                                const uint32_t neighborId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, 0, cell + d);
                                if ((waterLike && isLavaLikeId(neighborId))
                                    || (lavaLike && isWaterLikeId(neighborId))) {
                                    adjacentOpposite = true;
                                    break;
                                }
                            }
                            if (adjacentOpposite) {
                                toObsidian.push_back(cell);
                            }
                        }
                    }
                }

                for (const glm::ivec3& cell : toObsidian) {
                    voxelWorld.setBlockLod(
                        0,
                        cell,
                        static_cast<uint32_t>(obsidianProto->prototypeID),
                        packColor(glm::vec3(1.0f)),
                        false
                    );
                    wroteAny = true;
                }
            }
            inOutWroteAny = inOutWroteAny || wroteAny;
            if (outCompleted && inOutWroteAny) {
                auto markSectionDirty = [&](const glm::ivec3& coord, bool bumpVersion) {
                    VoxelSectionKey dirtyKey{lod, coord};
                    auto it = voxelWorld.sections.find(dirtyKey);
                    if (it == voxelWorld.sections.end()) return;
                    // Only bump version for the section whose voxel data changed.
                    // Neighbor sections should remesh, but not invalidate in-flight meshes repeatedly.
                    if (bumpVersion) it->second.editVersion += 1;
                    it->second.dirty = true;
                    voxelWorld.dirtySections.insert(dirtyKey);
                };

                markSectionDirty(sectionCoord, true);
                markSectionDirty(sectionCoord + glm::ivec3(1, 0, 0), false);
                markSectionDirty(sectionCoord + glm::ivec3(-1, 0, 0), false);
                markSectionDirty(sectionCoord + glm::ivec3(0, 1, 0), false);
                markSectionDirty(sectionCoord + glm::ivec3(0, -1, 0), false);
                markSectionDirty(sectionCoord + glm::ivec3(0, 0, 1), false);
                markSectionDirty(sectionCoord + glm::ivec3(0, 0, -1), false);
            }
            return outCompleted;
        }

        void UpdateExpanseVoxelWorld(BaseSystem& baseSystem,
                                     std::vector<Entity>& prototypes,
                                     WorldContext& worldCtx,
                                     const ExpanseConfig& cfg) {
            if (!baseSystem.voxelWorld || !baseSystem.player) return;
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            glm::vec3 cameraPos = baseSystem.player->cameraPosition;
            int maxLod = voxelWorld.maxLod;
            const bool columnMode = getRegistryString(baseSystem, "voxelStorageMode", "cubic") == "column";
            const std::string currentLevel = getRegistryString(baseSystem, "level", "the_expanse");
            const bool isExpanseLevel = (currentLevel == "the_expanse");
            const bool isDepthLevel = (currentLevel == "the_depths");
            const bool unifiedDepthsEnabled = isExpanseLevel && getRegistryBool(baseSystem, "UnifiedDepthsEnabled", true);
            const int unifiedDepthsMinY = getRegistryInt(baseSystem, "UnifiedDepthsMinY", -200);
            const int waterFloorY = static_cast<int>(std::floor(cfg.waterFloor));
            const int streamPortalY = isDepthLevel ? (cfg.minY + 1) : (waterFloorY - 2);
            int streamMinYLod0 = std::min(std::min(cfg.minY, waterFloorY), streamPortalY);
            if (unifiedDepthsEnabled) {
                streamMinYLod0 = std::min(streamMinYLod0, unifiedDepthsMinY);
            }
            g_voxelStreaming.frameCounter += 1;
            if (columnMode) {
                g_voxelColumnStreaming.frameCounter += 1;
            } else if (!g_voxelColumnDesiredMissing.empty()) {
                g_voxelColumnDesiredMissing.clear();
            }

            int prevRadius = 0;
            // Keep completion markers independent from section allocation.
            // Some generated sections are intentionally empty and never allocate voxel storage.
            // They still must remain "generated" so we do not keep re-queuing them every frame.
            // Do not purge in-progress jobs just because their backing section is currently absent.
            // Sections are materialized lazily when the first non-air cell is written; clearing the
            // job early resets nextColumn to 0 every frame and can stall generation indefinitely.
            auto shouldQueueKey = [&](const VoxelSectionKey& key) {
                return g_voxelTerrainGenerated.count(key) == 0;
            };
            auto onGeneratedSectionAvailable = [&](const VoxelSectionKey& key) {
                if (!columnMode) return;
                if (g_voxelStreaming.desired.count(key) == 0) return;
                const VoxelColumnModel::ColumnKey columnKey{key.lod, glm::ivec2(key.coord.x, key.coord.z)};
                auto it = g_voxelColumnDesiredMissing.find(columnKey);
                if (it != g_voxelColumnDesiredMissing.end() && it->second > 0) {
                    it->second -= 1;
                }
            };
            auto onGeneratedSectionUnavailable = [&](const VoxelSectionKey& key) {
                if (!columnMode) return;
                if (g_voxelStreaming.desired.count(key) == 0) return;
                const VoxelColumnModel::ColumnKey columnKey{key.lod, glm::ivec2(key.coord.x, key.coord.z)};
                auto it = g_voxelColumnDesiredMissing.find(columnKey);
                if (it != g_voxelColumnDesiredMissing.end()) {
                    it->second += 1;
                }
            };
            auto minDistToAabbXZ = [&](const glm::vec2& p, const glm::vec2& minB, const glm::vec2& maxB) {
                float dx = 0.0f;
                if (p.x < minB.x) dx = minB.x - p.x;
                else if (p.x > maxB.x) dx = p.x - maxB.x;
                float dz = 0.0f;
                if (p.y < minB.y) dz = minB.y - p.y;
                else if (p.y > maxB.y) dz = p.y - maxB.y;
                return std::sqrt(dx * dx + dz * dz);
            };
            auto maxDistToAabbXZ = [&](const glm::vec2& p, const glm::vec2& minB, const glm::vec2& maxB) {
                float dx = std::max(std::abs(p.x - minB.x), std::abs(p.x - maxB.x));
                float dz = std::max(std::abs(p.y - minB.y), std::abs(p.y - maxB.y));
                return std::sqrt(dx * dx + dz * dz);
            };

            if (g_voxelStreaming.lastCenterSections.size() != static_cast<size_t>(maxLod + 1)) {
                g_voxelStreaming.lastCenterSections.assign(static_cast<size_t>(maxLod + 1), glm::ivec3(std::numeric_limits<int>::min()));
                g_voxelStreaming.lastRadii.assign(static_cast<size_t>(maxLod + 1), std::numeric_limits<int>::min());
                g_voxelStreaming.desired.clear();
            }
            if (columnMode && g_voxelColumnStreaming.lastCenterColumns.size() != static_cast<size_t>(maxLod + 1)) {
                g_voxelColumnStreaming.lastCenterColumns.assign(
                    static_cast<size_t>(maxLod + 1),
                    glm::ivec2(std::numeric_limits<int>::min())
                );
                g_voxelColumnStreaming.lastRadii.assign(static_cast<size_t>(maxLod + 1), std::numeric_limits<int>::min());
                g_voxelColumnStreaming.desired.clear();
            }

            int superChunkMinLod = getRegistryInt(baseSystem, "voxelSuperChunkMinLod", 3);
            int superChunkMaxLod = getRegistryInt(baseSystem, "voxelSuperChunkMaxLod", 3);
            int superChunkSize = getRegistryInt(baseSystem, "voxelSuperChunkSize", 1);
            if (superChunkSize < 1) superChunkSize = 1;
            bool rebuildDesired = false;
            for (int lod = 0; lod <= maxLod; ++lod) {
                int radius = getRegistryInt(baseSystem, "voxelLod" + std::to_string(lod) + "Radius", 0);
                int size = sectionSizeForLod(voxelWorld, lod);
                int scale = 1 << lod;
                glm::ivec3 cameraCell = glm::ivec3(glm::floor(cameraPos / static_cast<float>(scale)));
                glm::ivec3 centerSection = floorDivVec(cameraCell, size);
                if (columnMode) {
                    centerSection.y = 0;
                }
                if (g_voxelStreaming.lastCenterSections[lod] != centerSection ||
                    g_voxelStreaming.lastRadii[lod] != radius) {
                    rebuildDesired = true;
                }
                g_voxelStreaming.lastCenterSections[lod] = centerSection;
                g_voxelStreaming.lastRadii[lod] = radius;
                if (columnMode) {
                    g_voxelColumnStreaming.lastCenterColumns[static_cast<size_t>(lod)] =
                        glm::ivec2(centerSection.x, centerSection.z);
                    g_voxelColumnStreaming.lastRadii[static_cast<size_t>(lod)] = radius;
                }
            }

            if (rebuildDesired) {
                g_voxelStreaming.desired.clear();
                g_voxelStreaming.desired.reserve(2048);
            }

            if (rebuildDesired && !columnMode) {
                for (int lod = 0; lod <= maxLod; ++lod) {
                    int radius = getRegistryInt(baseSystem, "voxelLod" + std::to_string(lod) + "Radius", 0);
                    if (radius <= 0) {
                        prevRadius = radius;
                        continue;
                    }
                    int size = sectionSizeForLod(voxelWorld, lod);
                    int scale = 1 << lod;
                    glm::ivec3 cameraCell = glm::ivec3(glm::floor(cameraPos / static_cast<float>(scale)));
                    glm::ivec3 centerSection = floorDivVec(cameraCell, size);
                    int sectionRadius = static_cast<int>(std::ceil(static_cast<float>(radius) / static_cast<float>(size * scale)));
                    int lodSurfaceCenterY = centerSection.y;
                    if (lod == 0) {
                        float cameraSurface = 0.0f;
                        bool cameraOnLand = ExpanseBiomeSystemLogic::SampleTerrain(worldCtx, cameraPos.x, cameraPos.z, cameraSurface);
                        int targetY = cameraOnLand
                            ? static_cast<int>(std::floor(cameraSurface))
                            : static_cast<int>(std::floor(cfg.waterSurface));
                        lodSurfaceCenterY = floorDivInt(targetY, scale * size);
                    }

                    int minY = cfg.minY;
                    if (lod == 0) {
                        minY = streamMinYLod0;
                    }
                    int maxY = computeExpanseMaxY(baseSystem, worldCtx, cfg);
                    int minSectionY = floorDivInt(minY, scale * size);
                    int maxSectionY = floorDivInt(maxY, scale * size);
                    std::vector<int> sectionYOrder;
                    sectionYOrder.reserve(static_cast<size_t>(std::max(0, maxSectionY - minSectionY + 1)));
                    for (int sy = minSectionY; sy <= maxSectionY; ++sy) {
                        sectionYOrder.push_back(sy);
                    }
                    std::sort(sectionYOrder.begin(), sectionYOrder.end(), [centerY = centerSection.y](int a, int b) {
                        int da = std::abs(a - centerY);
                        int db = std::abs(b - centerY);
                        if (da != db) return da < db;
                        return a < b;
                    });
                    if (lod == 0) {
                        std::sort(sectionYOrder.begin(), sectionYOrder.end(), [lodSurfaceCenterY](int a, int b) {
                            int da = std::abs(a - lodSurfaceCenterY);
                            int db = std::abs(b - lodSurfaceCenterY);
                            if (da != db) return da < db;
                            return a < b;
                        });
                    }

                    std::vector<glm::ivec3> sectionOrderXZ;
                    sectionOrderXZ.reserve(static_cast<size_t>((sectionRadius * 2 + 1) * (sectionRadius * 2 + 1)));
                    for (int ring = 0; ring <= sectionRadius; ++ring) {
                        for (int dz = -ring; dz <= ring; ++dz) {
                            for (int dx = -ring; dx <= ring; ++dx) {
                                if (std::max(std::abs(dx), std::abs(dz)) != ring) continue;
                                glm::ivec3 sectionCoord = centerSection + glm::ivec3(dx, 0, dz);
                                glm::vec2 minB = glm::vec2(sectionCoord.x * size * scale, sectionCoord.z * size * scale);
                                glm::vec2 maxB = minB + glm::vec2(size * scale);
                                glm::vec2 camXZ(cameraPos.x, cameraPos.z);
                                float minDist = minDistToAabbXZ(camXZ, minB, maxB);
                                float maxDist = maxDistToAabbXZ(camXZ, minB, maxB);
                                if (minDist > static_cast<float>(radius)) continue;
                                if (prevRadius > 0 && maxDist <= static_cast<float>(prevRadius)) continue;
                                sectionOrderXZ.push_back(sectionCoord);
                            }
                        }
                    }

                    auto enqueueDesiredSection = [&](const glm::ivec3& sectionCoord, int sy) {
                        if (lod >= superChunkMinLod && lod <= superChunkMaxLod && superChunkSize > 1) {
                            glm::ivec3 anchorCoord(
                                floorDivInt(sectionCoord.x, superChunkSize) * superChunkSize,
                                sy,
                                floorDivInt(sectionCoord.z, superChunkSize) * superChunkSize
                            );
                            for (int oz = 0; oz < superChunkSize; ++oz) {
                                for (int ox = 0; ox < superChunkSize; ++ox) {
                                    glm::ivec3 fullCoord(anchorCoord.x + ox, sy, anchorCoord.z + oz);
                                    VoxelSectionKey key{lod, fullCoord};
                                    g_voxelStreaming.desired.insert(key);
                                    if (shouldQueueKey(key)
                                        && g_voxelStreaming.pendingSet.count(key) == 0) {
                                        g_voxelStreaming.pending.push_back(key);
                                        g_voxelStreaming.pendingSet.insert(key);
                                    }
                                }
                            }
                        } else {
                            glm::ivec3 fullCoord(sectionCoord.x, sy, sectionCoord.z);
                            VoxelSectionKey key{lod, fullCoord};
                            g_voxelStreaming.desired.insert(key);
                            if (shouldQueueKey(key)
                                && g_voxelStreaming.pendingSet.count(key) == 0) {
                                g_voxelStreaming.pending.push_back(key);
                                g_voxelStreaming.pendingSet.insert(key);
                            }
                        }
                    };

                    // LOD0 should prioritize each column's own surface section first (and immediate
                    // neighbors), with a bounded vertical span. Unbounded vertical enqueue can
                    // starve near-surface sections while the camera moves, which presents as
                    // persistent checkerboard holes.
                    if (lod == 0) {
                        const int lod0SurfaceDepthSections = std::max(
                            1,
                            getRegistryInt(baseSystem, "voxelLod0SurfaceDepthSections", 4)
                        );
                        const int lod0SurfaceUpSections = std::max(
                            0,
                            getRegistryInt(baseSystem, "voxelLod0SurfaceUpSections", 1)
                        );
                        const int lod0CameraVerticalPadSections = std::max(
                            0,
                            getRegistryInt(baseSystem, "voxelLod0CameraVerticalPadSections", 1)
                        );

                        std::vector<std::vector<int>> columnYOrders;
                        columnYOrders.resize(sectionOrderXZ.size());
                        size_t maxDepth = 0;
                        auto pushUniqueInRange = [&](std::vector<int>& dst,
                                                     int sy,
                                                     int minSy,
                                                     int maxSy) {
                            if (sy < minSy || sy > maxSy) return;
                            for (int existing : dst) {
                                if (existing == sy) return;
                            }
                            dst.push_back(sy);
                        };

                        for (size_t ci = 0; ci < sectionOrderXZ.size(); ++ci) {
                            const glm::ivec3& sectionCoord = sectionOrderXZ[ci];
                            const float minWX = static_cast<float>(sectionCoord.x * size * scale);
                            const float minWZ = static_cast<float>(sectionCoord.z * size * scale);
                            const float maxWX = minWX + static_cast<float>(size * scale) - 1.0f;
                            const float maxWZ = minWZ + static_cast<float>(size * scale) - 1.0f;
                            const std::array<glm::vec2, 5> terrainSamples = {
                                glm::vec2((minWX + maxWX) * 0.5f, (minWZ + maxWZ) * 0.5f),
                                glm::vec2(minWX + 0.5f, minWZ + 0.5f),
                                glm::vec2(maxWX - 0.5f, minWZ + 0.5f),
                                glm::vec2(minWX + 0.5f, maxWZ - 0.5f),
                                glm::vec2(maxWX - 0.5f, maxWZ - 0.5f)
                            };

                            std::vector<int> yOrder;
                            yOrder.reserve(static_cast<size_t>(
                                (lod0SurfaceDepthSections + lod0SurfaceUpSections + 1) * 5
                                + (lod0CameraVerticalPadSections * 2 + 1)
                            ));

                            for (const glm::vec2& sampleXZ : terrainSamples) {
                                float terrainHeight = 0.0f;
                                bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(worldCtx, sampleXZ.x, sampleXZ.y, terrainHeight);
                                int targetY = isLand
                                    ? static_cast<int>(std::floor(terrainHeight))
                                    : static_cast<int>(std::floor(cfg.waterSurface));
                                int surfaceSectionY = floorDivInt(targetY, scale * size);

                                pushUniqueInRange(yOrder, surfaceSectionY, minSectionY, maxSectionY);
                                for (int up = 1; up <= lod0SurfaceUpSections; ++up) {
                                    pushUniqueInRange(yOrder, surfaceSectionY + up, minSectionY, maxSectionY);
                                }
                                for (int down = 1; down <= lod0SurfaceDepthSections; ++down) {
                                    pushUniqueInRange(yOrder, surfaceSectionY - down, minSectionY, maxSectionY);
                                }
                            }

                            for (int pad = -lod0CameraVerticalPadSections; pad <= lod0CameraVerticalPadSections; ++pad) {
                                pushUniqueInRange(yOrder, centerSection.y + pad, minSectionY, maxSectionY);
                            }

                            const int waterSurfaceSectionY = floorDivInt(
                                static_cast<int>(std::floor(cfg.waterSurface)),
                                scale * size
                            );
                            const int waterFloorSectionY = floorDivInt(waterFloorY, scale * size);
                            const int portalSectionY = floorDivInt(streamPortalY, scale * size);
                            pushUniqueInRange(yOrder, waterSurfaceSectionY, minSectionY, maxSectionY);
                            pushUniqueInRange(yOrder, waterFloorSectionY, minSectionY, maxSectionY);
                            pushUniqueInRange(yOrder, portalSectionY, minSectionY, maxSectionY);
                            if (unifiedDepthsEnabled) {
                                const int unifiedTopSectionY = floorDivInt(streamPortalY - 1, scale * size);
                                const int unifiedMinSectionY = floorDivInt(unifiedDepthsMinY, scale * size);
                                for (int sy = unifiedTopSectionY; sy >= unifiedMinSectionY; --sy) {
                                    pushUniqueInRange(yOrder, sy, minSectionY, maxSectionY);
                                }
                            }

                            maxDepth = std::max(maxDepth, yOrder.size());
                            columnYOrders[ci] = std::move(yOrder);
                        }

                        for (size_t yi = 0; yi < maxDepth; ++yi) {
                            for (size_t ci = 0; ci < sectionOrderXZ.size(); ++ci) {
                                const auto& yOrder = columnYOrders[ci];
                                if (yi >= yOrder.size()) continue;
                                enqueueDesiredSection(sectionOrderXZ[ci], yOrder[yi]);
                            }
                        }
                    } else {
                        const bool farLodSurfaceOnly = getRegistryBool(baseSystem, "voxelFarLodSurfaceOnly", true);
                        const int farLodSurfaceDepthSections = std::max(
                            0,
                            getRegistryInt(baseSystem, "voxelFarLodSurfaceDepthSections", 1)
                        );
                        const int farLodSurfaceUpSections = std::max(
                            0,
                            getRegistryInt(baseSystem, "voxelFarLodSurfaceUpSections", 0)
                        );
                        const int farLodWaterBandSections = std::max(
                            0,
                            getRegistryInt(baseSystem, "voxelFarLodWaterBandSections", 1)
                        );

                        auto pushUniqueInRange = [&](std::vector<int>& dst,
                                                     int sy,
                                                     int minSy,
                                                     int maxSy) {
                            if (sy < minSy || sy > maxSy) return;
                            for (int existing : dst) {
                                if (existing == sy) return;
                            }
                            dst.push_back(sy);
                        };

                        if (farLodSurfaceOnly) {
                            const int waterSurfaceSectionY = floorDivInt(
                                static_cast<int>(std::floor(cfg.waterSurface)),
                                scale * size
                            );
                            const int waterFloorSectionY = floorDivInt(
                                waterFloorY,
                                scale * size
                            );
                            const int portalSectionY = floorDivInt(streamPortalY, scale * size);
                            for (const glm::ivec3& sectionCoord : sectionOrderXZ) {
                                std::vector<int> yOrder;
                                yOrder.reserve(static_cast<size_t>(
                                    1 + farLodSurfaceDepthSections + farLodSurfaceUpSections + farLodWaterBandSections + 2
                                ));

                                // Far LOD streaming keeps only a thin surface/water band.
                                const float minWX = static_cast<float>(sectionCoord.x * size * scale);
                                const float minWZ = static_cast<float>(sectionCoord.z * size * scale);
                                const float maxWX = minWX + static_cast<float>(size * scale) - 1.0f;
                                const float maxWZ = minWZ + static_cast<float>(size * scale) - 1.0f;
                                const glm::vec2 sampleXZ((minWX + maxWX) * 0.5f, (minWZ + maxWZ) * 0.5f);
                                float terrainHeight = 0.0f;
                                const bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(
                                    worldCtx,
                                    sampleXZ.x,
                                    sampleXZ.y,
                                    terrainHeight
                                );
                                const int targetY = isLand
                                    ? static_cast<int>(std::floor(terrainHeight))
                                    : static_cast<int>(std::floor(cfg.waterSurface));
                                const int surfaceSectionY = floorDivInt(targetY, scale * size);

                                pushUniqueInRange(yOrder, surfaceSectionY, minSectionY, maxSectionY);
                                for (int up = 1; up <= farLodSurfaceUpSections; ++up) {
                                    pushUniqueInRange(yOrder, surfaceSectionY + up, minSectionY, maxSectionY);
                                }
                                for (int down = 1; down <= farLodSurfaceDepthSections; ++down) {
                                    pushUniqueInRange(yOrder, surfaceSectionY - down, minSectionY, maxSectionY);
                                }

                                pushUniqueInRange(yOrder, waterSurfaceSectionY, minSectionY, maxSectionY);
                                for (int down = 1; down <= farLodWaterBandSections; ++down) {
                                    pushUniqueInRange(yOrder, waterSurfaceSectionY - down, minSectionY, maxSectionY);
                                }
                                pushUniqueInRange(yOrder, waterFloorSectionY, minSectionY, maxSectionY);
                                pushUniqueInRange(yOrder, portalSectionY, minSectionY, maxSectionY);
                                if (unifiedDepthsEnabled) {
                                    const int unifiedTopSectionY = floorDivInt(streamPortalY - 1, scale * size);
                                    const int unifiedMinSectionY = floorDivInt(unifiedDepthsMinY, scale * size);
                                    for (int sy = unifiedTopSectionY; sy >= unifiedMinSectionY; --sy) {
                                        pushUniqueInRange(yOrder, sy, minSectionY, maxSectionY);
                                    }
                                }

                                if (yOrder.empty()) {
                                    for (int sy : sectionYOrder) {
                                        enqueueDesiredSection(sectionCoord, sy);
                                    }
                                } else {
                                    std::sort(yOrder.begin(), yOrder.end(), [lodSurfaceCenterY](int a, int b) {
                                        int da = std::abs(a - lodSurfaceCenterY);
                                        int db = std::abs(b - lodSurfaceCenterY);
                                        if (da != db) return da < db;
                                        return a < b;
                                    });
                                    for (int sy : yOrder) {
                                        enqueueDesiredSection(sectionCoord, sy);
                                    }
                                }
                            }
                        } else {
                            for (const glm::ivec3& sectionCoord : sectionOrderXZ) {
                                for (int sy : sectionYOrder) {
                                    enqueueDesiredSection(sectionCoord, sy);
                                }
                            }
                        }
                    }
                    prevRadius = radius;
                }

                for (auto it = g_voxelColumnSurfaceReady.begin(); it != g_voxelColumnSurfaceReady.end(); ) {
                    if (g_voxelColumnStreaming.desired.count(*it) == 0) {
                        it = g_voxelColumnSurfaceReady.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            if (columnMode && rebuildDesired) {
                g_voxelColumnStreaming.desired.clear();
                g_voxelColumnStreaming.desired.reserve(2048);
                g_voxelColumnDesiredMissing.clear();
                g_voxelColumnDesiredMissing.reserve(4096);

                g_voxelStreaming.desired.clear();
                g_voxelStreaming.desired.reserve(2048);

                for (int lod = 0; lod <= maxLod; ++lod) {
                    const int radius = getRegistryInt(baseSystem, "voxelLod" + std::to_string(lod) + "Radius", 0);
                    if (radius <= 0) {
                        prevRadius = radius;
                        continue;
                    }

                    const int size = sectionSizeForLod(voxelWorld, lod);
                    const int scale = 1 << lod;
                    const glm::ivec3 cameraCell = glm::ivec3(glm::floor(cameraPos / static_cast<float>(scale)));
                    glm::ivec3 centerSection = floorDivVec(cameraCell, size);
                    centerSection.y = 0;
                    const int sectionRadius = static_cast<int>(std::ceil(
                        static_cast<float>(radius) / static_cast<float>(size * scale)
                    ));

                    const int minY = (lod == 0) ? streamMinYLod0 : cfg.minY;
                    const int maxY = computeExpanseMaxY(baseSystem, worldCtx, cfg);
                    const int minSectionY = floorDivInt(minY, scale * size);
                    const int maxSectionY = floorDivInt(maxY, scale * size);
                    if (minSectionY > maxSectionY) {
                        prevRadius = radius;
                        continue;
                    }

                    std::vector<glm::ivec2> columnOrderXZ;
                    columnOrderXZ.reserve(static_cast<size_t>((sectionRadius * 2 + 1) * (sectionRadius * 2 + 1)));
                    for (int ring = 0; ring <= sectionRadius; ++ring) {
                        for (int dz = -ring; dz <= ring; ++dz) {
                            for (int dx = -ring; dx <= ring; ++dx) {
                                if (std::max(std::abs(dx), std::abs(dz)) != ring) continue;
                                const glm::ivec2 columnCoord(centerSection.x + dx, centerSection.z + dz);
                                const glm::vec2 minB = glm::vec2(
                                    static_cast<float>(columnCoord.x * size * scale),
                                    static_cast<float>(columnCoord.y * size * scale)
                                );
                                const glm::vec2 maxB = minB + glm::vec2(static_cast<float>(size * scale));
                                const glm::vec2 camXZ(cameraPos.x, cameraPos.z);
                                const float minDist = minDistToAabbXZ(camXZ, minB, maxB);
                                const float maxDist = maxDistToAabbXZ(camXZ, minB, maxB);
                                if (minDist > static_cast<float>(radius)) continue;
                                if (prevRadius > 0 && maxDist <= static_cast<float>(prevRadius)) continue;
                                columnOrderXZ.push_back(columnCoord);
                            }
                        }
                    }

                    auto enqueueDesiredSection = [&](const glm::ivec2& columnCoord, int sy) {
                        if (sy < minSectionY || sy > maxSectionY) return;
                        auto enqueueKey = [&](const glm::ivec3& fullCoord) {
                            const VoxelSectionKey key{lod, fullCoord};
                            const bool inserted = g_voxelStreaming.desired.insert(key).second;
                            if (!inserted) return;
                            const VoxelColumnModel::ColumnKey columnKey{
                                lod,
                                glm::ivec2(fullCoord.x, fullCoord.z)
                            };
                            auto [missingIt, _] = g_voxelColumnDesiredMissing.try_emplace(columnKey, 0);
                            if (g_voxelTerrainGenerated.count(key) == 0) {
                                missingIt->second += 1;
                            }
                            if (shouldQueueKey(key)
                                && g_voxelStreaming.pendingSet.count(key) == 0) {
                                g_voxelStreaming.pending.push_back(key);
                                g_voxelStreaming.pendingSet.insert(key);
                            }
                        };
                        if (lod >= superChunkMinLod && lod <= superChunkMaxLod && superChunkSize > 1) {
                            const glm::ivec2 anchorCoord(
                                floorDivInt(columnCoord.x, superChunkSize) * superChunkSize,
                                floorDivInt(columnCoord.y, superChunkSize) * superChunkSize
                            );
                            for (int oz = 0; oz < superChunkSize; ++oz) {
                                for (int ox = 0; ox < superChunkSize; ++ox) {
                                    enqueueKey(glm::ivec3(anchorCoord.x + ox, sy, anchorCoord.y + oz));
                                }
                            }
                        } else {
                            enqueueKey(glm::ivec3(columnCoord.x, sy, columnCoord.y));
                        }
                    };

                    for (const glm::ivec2& columnCoord : columnOrderXZ) {
                        g_voxelColumnStreaming.desired.insert(VoxelColumnModel::ColumnKey{lod, columnCoord});
                        for (int sy = minSectionY; sy <= maxSectionY; ++sy) {
                            enqueueDesiredSection(columnCoord, sy);
                        }
                    }

                    prevRadius = radius;
                }
            }

            if (rebuildDesired) {
                std::vector<VoxelSectionKey> toRemove;
                toRemove.reserve(voxelWorld.sections.size());
                glm::vec2 camXZ(cameraPos.x, cameraPos.z);
                for (const auto& [key, _] : voxelWorld.sections) {
                    if (g_voxelStreaming.desired.count(key) > 0) continue;
                    int radius = getRegistryInt(baseSystem, "voxelLod" + std::to_string(key.lod) + "Radius", 0);
                    if (radius <= 0) {
                        toRemove.push_back(key);
                        continue;
                    }
                    int size = sectionSizeForLod(voxelWorld, key.lod);
                    int scale = 1 << key.lod;
                    float keepRadius = static_cast<float>(radius + size * scale);
                    glm::vec2 minB = glm::vec2(key.coord.x * size * scale, key.coord.z * size * scale);
                    glm::vec2 maxB = minB + glm::vec2(size * scale);
                    float minDist = minDistToAabbXZ(camXZ, minB, maxB);
                    if (minDist > keepRadius) {
                        toRemove.push_back(key);
                    }
                }
                for (const auto& key : toRemove) {
                    voxelWorld.releaseSection(key);
                    g_voxelTerrainGenerated.erase(key);
                    g_voxelSectionJobs.erase(key);
                }
            }

            if (rebuildDesired && !g_voxelTerrainGenerated.empty()) {
                for (auto it = g_voxelTerrainGenerated.begin(); it != g_voxelTerrainGenerated.end(); ) {
                    if (g_voxelStreaming.desired.count(*it) == 0) {
                        it = g_voxelTerrainGenerated.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            int filteredOut = 0;
            int rescueSurfaceQueued = 0;
            int rescueMissingQueued = 0;

            // Drop pending entries that are no longer desired.
            if (rebuildDesired && !g_voxelStreaming.pending.empty()) {
                std::vector<VoxelSectionKey> filtered;
                filtered.reserve(g_voxelStreaming.pending.size());
                for (const auto& key : g_voxelStreaming.pending) {
                    if (g_voxelStreaming.desired.count(key) > 0
                        && shouldQueueKey(key)) {
                        filtered.push_back(key);
                    } else {
                        g_voxelStreaming.pendingSet.erase(key);
                        g_voxelSectionJobs.erase(key);
                        filteredOut += 1;
                    }
                }
                g_voxelStreaming.pending.swap(filtered);
            }

            // Additional LOD0 surface rescue: explicitly queue missing near-camera surface bands.
            // This protects against persistent checkerboard holes when the player is moving while
            // generation budgets are tight.
            {
                const int lod0SurfaceRescuePerFrame = std::max(0, getRegistryInt(baseSystem, "voxelLod0SurfaceRescuePerFrame", 24));
                if (lod0SurfaceRescuePerFrame > 0) {
                    const int lod = 0;
                    const int size = sectionSizeForLod(voxelWorld, lod);
                    const int scale = 1 << lod;
                    const int radius = getRegistryInt(baseSystem, "voxelLod0Radius", 0);
                    if (radius > 0) {
                        const int sectionRadius = static_cast<int>(std::ceil(static_cast<float>(radius) / static_cast<float>(size * scale)));
                        glm::ivec3 cameraCell = glm::ivec3(glm::floor(cameraPos / static_cast<float>(scale)));
                        glm::ivec3 centerSection = floorDivVec(cameraCell, size);
                        const int lod0SurfaceDepthSections = std::max(
                            1,
                            getRegistryInt(baseSystem, "voxelLod0SurfaceDepthSections", 4)
                        );
                        const int lod0SurfaceUpSections = std::max(
                            0,
                            getRegistryInt(baseSystem, "voxelLod0SurfaceUpSections", 1)
                        );
                        const int lod0CameraVerticalPadSections = std::max(
                            0,
                            getRegistryInt(baseSystem, "voxelLod0CameraVerticalPadSections", 1)
                        );
                        const int minY = streamMinYLod0;
                        const int maxY = computeExpanseMaxY(baseSystem, worldCtx, cfg);
                        const int minSectionY = floorDivInt(minY, scale * size);
                        const int maxSectionY = floorDivInt(maxY, scale * size);
                        glm::vec2 camXZ(cameraPos.x, cameraPos.z);
                        struct RescueCandidate {
                            VoxelSectionKey key;
                            float dist2 = 0.0f;
                            int yPriority = 0;
                        };
                        std::vector<RescueCandidate> candidates;
                        candidates.reserve(256);

                        auto enqueueRescue = [&](const glm::ivec3& sectionCoord, int sy, float dist2, int yPriority) {
                            if (sy < minSectionY || sy > maxSectionY) return;
                            VoxelSectionKey key{lod, glm::ivec3(sectionCoord.x, sy, sectionCoord.z)};
                            if (!shouldQueueKey(key)) return;
                            if (g_voxelStreaming.pendingSet.count(key) > 0) return;
                            candidates.push_back(RescueCandidate{key, dist2, yPriority});
                        };

                        for (int dz = -sectionRadius; dz <= sectionRadius; ++dz) {
                            for (int dx = -sectionRadius; dx <= sectionRadius; ++dx) {
                                glm::ivec3 sectionCoord = centerSection + glm::ivec3(dx, 0, dz);
                                glm::vec2 minB = glm::vec2(sectionCoord.x * size * scale, sectionCoord.z * size * scale);
                                glm::vec2 maxB = minB + glm::vec2(size * scale);
                                float minDist = minDistToAabbXZ(camXZ, minB, maxB);
                                if (minDist > static_cast<float>(radius)) continue;

                                float centerX = (static_cast<float>(sectionCoord.x) + 0.5f) * static_cast<float>(size * scale);
                                float centerZ = (static_cast<float>(sectionCoord.z) + 0.5f) * static_cast<float>(size * scale);
                                float dxC = centerX - cameraPos.x;
                                float dzC = centerZ - cameraPos.z;
                                float dist2 = dxC * dxC + dzC * dzC;

                                const float minWX = static_cast<float>(sectionCoord.x * size * scale);
                                const float minWZ = static_cast<float>(sectionCoord.z * size * scale);
                                const float maxWX = minWX + static_cast<float>(size * scale) - 1.0f;
                                const float maxWZ = minWZ + static_cast<float>(size * scale) - 1.0f;
                                const std::array<glm::vec2, 5> terrainSamples = {
                                    glm::vec2((minWX + maxWX) * 0.5f, (minWZ + maxWZ) * 0.5f),
                                    glm::vec2(minWX + 0.5f, minWZ + 0.5f),
                                    glm::vec2(maxWX - 0.5f, minWZ + 0.5f),
                                    glm::vec2(minWX + 0.5f, maxWZ - 0.5f),
                                    glm::vec2(maxWX - 0.5f, maxWZ - 0.5f)
                                };

                                for (const glm::vec2& sampleXZ : terrainSamples) {
                                    float terrainHeight = 0.0f;
                                    bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(worldCtx, sampleXZ.x, sampleXZ.y, terrainHeight);
                                    int targetY = isLand
                                        ? static_cast<int>(std::floor(terrainHeight))
                                        : static_cast<int>(std::floor(cfg.waterSurface));
                                    int surfaceSectionY = floorDivInt(targetY, scale * size);

                                    enqueueRescue(sectionCoord, surfaceSectionY, dist2, 0);
                                    for (int up = 1; up <= lod0SurfaceUpSections; ++up) {
                                        enqueueRescue(sectionCoord, surfaceSectionY + up, dist2, up);
                                    }
                                    for (int down = 1; down <= lod0SurfaceDepthSections; ++down) {
                                        enqueueRescue(sectionCoord, surfaceSectionY - down, dist2, down);
                                    }
                                }

                                for (int pad = -lod0CameraVerticalPadSections; pad <= lod0CameraVerticalPadSections; ++pad) {
                                    enqueueRescue(sectionCoord, centerSection.y + pad, dist2, std::abs(pad) + 1);
                                }
                            }
                        }

                        if (!candidates.empty()) {
                            std::sort(candidates.begin(), candidates.end(), [](const RescueCandidate& a, const RescueCandidate& b) {
                                if (a.dist2 != b.dist2) return a.dist2 < b.dist2;
                                if (a.yPriority != b.yPriority) return a.yPriority < b.yPriority;
                                if (a.key.coord.x != b.key.coord.x) return a.key.coord.x < b.key.coord.x;
                                if (a.key.coord.y != b.key.coord.y) return a.key.coord.y < b.key.coord.y;
                                return a.key.coord.z < b.key.coord.z;
                            });

                            const int rescueCount = std::min<int>(lod0SurfaceRescuePerFrame, static_cast<int>(candidates.size()));
                            for (int i = 0; i < rescueCount; ++i) {
                                const VoxelSectionKey key = candidates[static_cast<size_t>(i)].key;
                                if (g_voxelStreaming.pendingSet.count(key) > 0) continue;
                                g_voxelStreaming.pending.push_back(key);
                                g_voxelStreaming.pendingSet.insert(key);
                                g_voxelStreaming.desired.insert(key);
                                rescueSurfaceQueued += 1;
                            }
                        }
                    }
                }
            }

            // Safety net: if near-camera LOD0 sections are missing and not queued, re-queue them
            // even when desired-set rebuild is not triggered this frame.
            {
                const int rescueBudget = std::max(0, getRegistryInt(baseSystem, "voxelLod0RescuePerFrame", 96));
                if (rescueBudget > 0 && !g_voxelStreaming.desired.empty()) {
                    struct MissingLod0 {
                        VoxelSectionKey key;
                        float dist2 = 0.0f;
                        float yDist = 0.0f;
                    };
                    std::vector<MissingLod0> missing;
                    missing.reserve(256);

                    const int lod = 0;
                    const int size = sectionSizeForLod(voxelWorld, lod);
                    const int scale = 1 << lod;
                    glm::ivec3 cameraCell = glm::ivec3(glm::floor(cameraPos / static_cast<float>(scale)));
                    glm::ivec3 centerSection = floorDivVec(cameraCell, size);
                    int lodSurfaceCenterY = centerSection.y;
                    {
                        float cameraSurface = 0.0f;
                        bool cameraOnLand = ExpanseBiomeSystemLogic::SampleTerrain(worldCtx, cameraPos.x, cameraPos.z, cameraSurface);
                        int targetY = cameraOnLand
                            ? static_cast<int>(std::floor(cameraSurface))
                            : static_cast<int>(std::floor(cfg.waterSurface));
                        lodSurfaceCenterY = floorDivInt(targetY, scale * size);
                    }
                    const int radius = getRegistryInt(baseSystem, "voxelLod0Radius", 0);

                    for (const auto& key : g_voxelStreaming.desired) {
                        if (key.lod != 0) continue;
                        if (!shouldQueueKey(key)) continue;
                        if (g_voxelStreaming.pendingSet.count(key) > 0) continue;

                        glm::vec2 minB = glm::vec2(key.coord.x * size * scale, key.coord.z * size * scale);
                        glm::vec2 maxB = minB + glm::vec2(size * scale);
                        glm::vec2 camXZ(cameraPos.x, cameraPos.z);
                        float minDist = minDistToAabbXZ(camXZ, minB, maxB);
                        if (radius > 0 && minDist > static_cast<float>(radius)) continue;

                        float centerX = (static_cast<float>(key.coord.x) + 0.5f) * static_cast<float>(size * scale);
                        float centerZ = (static_cast<float>(key.coord.z) + 0.5f) * static_cast<float>(size * scale);
                        float dx = centerX - cameraPos.x;
                        float dz = centerZ - cameraPos.z;
                        missing.push_back(MissingLod0{
                            key,
                            dx * dx + dz * dz,
                            static_cast<float>(std::abs(key.coord.y - lodSurfaceCenterY))
                        });
                    }

                    if (!missing.empty()) {
                        std::sort(missing.begin(), missing.end(), [](const MissingLod0& a, const MissingLod0& b) {
                            if (a.dist2 != b.dist2) return a.dist2 < b.dist2;
                            if (a.yDist != b.yDist) return a.yDist < b.yDist;
                            if (a.key.coord.x != b.key.coord.x) return a.key.coord.x < b.key.coord.x;
                            if (a.key.coord.y != b.key.coord.y) return a.key.coord.y < b.key.coord.y;
                            return a.key.coord.z < b.key.coord.z;
                        });

                        const int limit = std::min<int>(rescueBudget, static_cast<int>(missing.size()));
                        for (int i = 0; i < limit; ++i) {
                            const VoxelSectionKey key = missing[static_cast<size_t>(i)].key;
                            if (g_voxelStreaming.pendingSet.count(key) > 0) continue;
                            g_voxelStreaming.pending.push_back(key);
                            g_voxelStreaming.pendingSet.insert(key);
                            rescueMissingQueued += 1;
                        }
                    }
                }
            }

            auto pendingNearCameraLess = [&](const VoxelSectionKey& a, const VoxelSectionKey& b) {
                if (a.lod != b.lod) return a.lod < b.lod;
                int aSize = sectionSizeForLod(voxelWorld, a.lod);
                int bSize = sectionSizeForLod(voxelWorld, b.lod);
                int aScale = 1 << a.lod;
                int bScale = 1 << b.lod;

                float aCenterX = (static_cast<float>(a.coord.x) + 0.5f) * static_cast<float>(aSize * aScale);
                float aCenterZ = (static_cast<float>(a.coord.z) + 0.5f) * static_cast<float>(aSize * aScale);
                float bCenterX = (static_cast<float>(b.coord.x) + 0.5f) * static_cast<float>(bSize * bScale);
                float bCenterZ = (static_cast<float>(b.coord.z) + 0.5f) * static_cast<float>(bSize * bScale);

                float aDx = aCenterX - cameraPos.x;
                float aDz = aCenterZ - cameraPos.z;
                float bDx = bCenterX - cameraPos.x;
                float bDz = bCenterZ - cameraPos.z;
                float aDist2 = aDx * aDx + aDz * aDz;
                float bDist2 = bDx * bDx + bDz * bDz;
                if (aDist2 != bDist2) return aDist2 < bDist2;

                float aCenterY = (static_cast<float>(a.coord.y) + 0.5f) * static_cast<float>(aSize * aScale);
                float bCenterY = (static_cast<float>(b.coord.y) + 0.5f) * static_cast<float>(bSize * bScale);
                float aDy = std::abs(aCenterY - cameraPos.y);
                float bDy = std::abs(bCenterY - cameraPos.y);
                if (aDy != bDy) return aDy < bDy;

                if (a.coord.y != b.coord.y) return a.coord.y < b.coord.y;
                if (a.coord.x != b.coord.x) return a.coord.x < b.coord.x;
                return a.coord.z < b.coord.z;
            };

            int reprioritizedCount = 0;
            int droppedByCap = 0;
            const int pendingHardCap = std::max(0, getRegistryInt(baseSystem, "voxelPendingHardCap", 8192));
            const int pendingResortIntervalFrames = std::max(1, getRegistryInt(baseSystem, "voxelPendingResortIntervalFrames", 6));
            const int pendingResortWhenAbove = std::max(0, getRegistryInt(baseSystem, "voxelPendingResortWhenAbove", 1024));
            const int pendingResortWindow = std::max(0, getRegistryInt(baseSystem, "voxelPendingResortWindow", 4096));
            const bool shouldResortPending =
                rebuildDesired
                || (pendingHardCap > 0 && static_cast<int>(g_voxelStreaming.pending.size()) > pendingHardCap)
                || ((static_cast<int>(g_voxelStreaming.pending.size()) > pendingResortWhenAbove)
                    && (g_voxelStreaming.frameCounter % static_cast<uint64_t>(pendingResortIntervalFrames) == 0u));
            if (shouldResortPending && !g_voxelStreaming.pending.empty()) {
                size_t sortCount = g_voxelStreaming.pending.size();
                if (pendingResortWindow > 0 && sortCount > static_cast<size_t>(pendingResortWindow)) {
                    sortCount = static_cast<size_t>(pendingResortWindow);
                }
                if (sortCount == g_voxelStreaming.pending.size()) {
                    std::stable_sort(g_voxelStreaming.pending.begin(), g_voxelStreaming.pending.end(), pendingNearCameraLess);
                } else {
                    std::stable_sort(
                        g_voxelStreaming.pending.begin(),
                        g_voxelStreaming.pending.begin() + static_cast<std::ptrdiff_t>(sortCount),
                        pendingNearCameraLess
                    );
                }
                reprioritizedCount = static_cast<int>(sortCount);
            }
            if (pendingHardCap > 0 && static_cast<int>(g_voxelStreaming.pending.size()) > pendingHardCap) {
                auto begin = g_voxelStreaming.pending.begin();
                auto keepEnd = begin + pendingHardCap;
                auto end = g_voxelStreaming.pending.end();
                std::nth_element(begin, keepEnd, end, pendingNearCameraLess);
                std::sort(begin, keepEnd, pendingNearCameraLess);
                for (auto it = keepEnd; it != end; ++it) {
                    g_voxelStreaming.pendingSet.erase(*it);
                    g_voxelSectionJobs.erase(*it);
                    if (g_voxelTerrainGenerated.erase(*it) > 0) {
                        onGeneratedSectionUnavailable(*it);
                    }
                    voxelWorld.releaseSection(*it);
                }
                droppedByCap = static_cast<int>(std::distance(keepEnd, end));
                g_voxelStreaming.pending.erase(keepEnd, end);
            }

            int generationBudget = getRegistryInt(baseSystem, "voxelSectionsPerFrame", 0);
            const int minSectionsBeforeTimeCap = std::max(0, getRegistryInt(baseSystem, "voxelSectionGenMinSectionsPerFrame", 1));
            const float generationTimeBudgetMs = std::max(0.0f, getRegistryFloat(baseSystem, "voxelSectionGenMaxMsPerFrame", 6.0f));
            const int sectionColumnsPerStep = std::max(1, getRegistryInt(baseSystem, "voxelSectionGenColumnsPerStep", 1024));
            const bool resumeInProgressFirst = getRegistryBool(baseSystem, "voxelSectionGenResumeInProgressFirst", true);
            auto genStart = std::chrono::steady_clock::now();
            int stepped = 0;
            int built = 0;
            int skippedExisting = 0;
            int consumed = 0;
            std::vector<VoxelSectionKey> requeueFront;
            std::vector<VoxelSectionKey> requeueBack;
            const int pendingCountAtStart = static_cast<int>(g_voxelStreaming.pending.size());
            while (consumed < pendingCountAtStart
                   && (generationBudget <= 0 || stepped < generationBudget)) {
                if (generationTimeBudgetMs > 0.0f && consumed >= minSectionsBeforeTimeCap) {
                    float elapsedMs = std::chrono::duration<float, std::milli>(
                        std::chrono::steady_clock::now() - genStart
                    ).count();
                    if (elapsedMs >= generationTimeBudgetMs) {
                        break;
                    }
                }
                const auto key = g_voxelStreaming.pending[static_cast<size_t>(consumed)];
                g_voxelStreaming.pendingSet.erase(key);
                if (shouldQueueKey(key)) {
                    auto jobIt = g_voxelSectionJobs.find(key);
                    if (jobIt == g_voxelSectionJobs.end()) {
                        voxelWorld.releaseSection(key);
                        auto inserted = g_voxelSectionJobs.emplace(key, VoxelSectionGenerationJob{});
                        jobIt = inserted.first;
                    }
                    bool jobWroteAny = jobIt->second.wroteAny;
                    int nextColumn = jobIt->second.nextColumn;
                    bool completed = false;
                    const bool resolved = GenerateExpanseSectionVoxel(
                        baseSystem,
                        prototypes,
                        worldCtx,
                        cfg,
                        key.lod,
                        key.coord,
                        jobIt->second.nextColumn,
                        sectionColumnsPerStep,
                        jobWroteAny,
                        nextColumn,
                        completed
                    );
                    stepped += 1;
                    if (completed) {
                        g_voxelSectionJobs.erase(key);
                        if (resolved) {
                            const bool insertedGenerated = g_voxelTerrainGenerated.insert(key).second;
                            if (insertedGenerated) {
                                onGeneratedSectionAvailable(key);
                            }
                            if (columnMode) {
                                const int size = sectionSizeForLod(voxelWorld, key.lod);
                                const int scale = 1 << key.lod;
                                const float sampleWX = (static_cast<float>(key.coord.x) + 0.5f) * static_cast<float>(size * scale);
                                const float sampleWZ = (static_cast<float>(key.coord.z) + 0.5f) * static_cast<float>(size * scale);
                                float terrainHeight = 0.0f;
                                const bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(
                                    worldCtx,
                                    sampleWX,
                                    sampleWZ,
                                    terrainHeight
                                );
                                const int targetY = isLand
                                    ? static_cast<int>(std::floor(terrainHeight))
                                    : static_cast<int>(std::floor(cfg.waterSurface));
                                const int surfaceSectionY = floorDivInt(targetY, scale * size);
                                const int surfaceDepth = std::max(
                                    1,
                                    getRegistryInt(baseSystem, "voxelColumnSurfaceDepthBands", 4)
                                );
                                const int surfaceUp = std::max(
                                    0,
                                    getRegistryInt(baseSystem, "voxelColumnSurfaceUpBands", 2)
                                );
                                if (key.coord.y >= (surfaceSectionY - surfaceDepth)
                                    && key.coord.y <= (surfaceSectionY + surfaceUp)) {
                                    g_voxelColumnSurfaceReady.insert(
                                        VoxelColumnModel::ColumnKey{key.lod, glm::ivec2(key.coord.x, key.coord.z)}
                                    );
                                }
                            }
                        } else {
                            if (g_voxelTerrainGenerated.erase(key) > 0) {
                                onGeneratedSectionUnavailable(key);
                            }
                        }
                        built += 1;
                    } else {
                        jobIt = g_voxelSectionJobs.find(key);
                        if (jobIt != g_voxelSectionJobs.end()) {
                            jobIt->second.nextColumn = nextColumn;
                            jobIt->second.wroteAny = jobWroteAny;
                        }
                        if (g_voxelStreaming.pendingSet.insert(key).second) {
                            if (resumeInProgressFirst) {
                                requeueFront.push_back(key);
                            } else {
                                requeueBack.push_back(key);
                            }
                        }
                    }
                } else {
                    g_voxelSectionJobs.erase(key);
                    skippedExisting += 1;
                }
                consumed += 1;
            }
            if (consumed > 0) {
                g_voxelStreaming.pending.erase(g_voxelStreaming.pending.begin(),
                                               g_voxelStreaming.pending.begin() + consumed);
            }
            if (!requeueFront.empty()) {
                g_voxelStreaming.pending.insert(g_voxelStreaming.pending.begin(),
                                                requeueFront.begin(),
                                                requeueFront.end());
            }
            if (!requeueBack.empty()) {
                g_voxelStreaming.pending.insert(g_voxelStreaming.pending.end(),
                                                requeueBack.begin(),
                                                requeueBack.end());
            }
            float generationMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - genStart
            ).count();

            g_voxelStreamingPerfStats.pending = g_voxelStreaming.pending.size();
            g_voxelStreamingPerfStats.desired = g_voxelStreaming.desired.size();
            g_voxelStreamingPerfStats.generated = g_voxelTerrainGenerated.size();
            g_voxelStreamingPerfStats.jobs = g_voxelSectionJobs.size();
            g_voxelStreamingPerfStats.stepped = stepped;
            g_voxelStreamingPerfStats.built = built;
            g_voxelStreamingPerfStats.consumed = consumed;
            g_voxelStreamingPerfStats.skippedExisting = skippedExisting;
            g_voxelStreamingPerfStats.filteredOut = filteredOut;
            g_voxelStreamingPerfStats.rescueSurfaceQueued = rescueSurfaceQueued;
            g_voxelStreamingPerfStats.rescueMissingQueued = rescueMissingQueued;
            g_voxelStreamingPerfStats.droppedByCap = droppedByCap;
            g_voxelStreamingPerfStats.reprioritized = reprioritizedCount;
            g_voxelStreamingPerfStats.generationMs = generationMs;

            auto now = std::chrono::steady_clock::now();
            if (now - g_lastVoxelPerf >= std::chrono::seconds(1)) {
                std::cout << "TerrainGeneration: voxel sections generated "
                          << built << " (stepped " << stepped
                          << ", consumed " << consumed
                          << ", skipped " << skippedExisting
                          << ", filtered " << filteredOut
                          << ") in " << static_cast<int>(std::round(generationMs))
                          << " ms. Pending " << g_voxelStreaming.pending.size()
                          << " desired " << g_voxelStreaming.desired.size()
                          << " generated " << g_voxelTerrainGenerated.size()
                          << " jobs " << g_voxelSectionJobs.size()
                          << " rescue(surface/missing)=" << rescueSurfaceQueued << "/" << rescueMissingQueued
                          << " reprio " << reprioritizedCount
                          << " capDrop " << droppedByCap
                          << "." << std::endl;
                g_lastVoxelPerf = now;
            }

        }

    }

    void GetVoxelStreamingPerfStats(size_t& pending,
                                    size_t& desired,
                                    size_t& generated,
                                    size_t& jobs,
                                    int& stepped,
                                    int& built,
                                    int& consumed,
                                    int& skippedExisting,
                                    int& filteredOut,
                                    int& rescueSurfaceQueued,
                                    int& rescueMissingQueued,
                                    int& droppedByCap,
                                    int& reprioritized,
                                    float& generationMs) {
        pending = g_voxelStreamingPerfStats.pending;
        desired = g_voxelStreamingPerfStats.desired;
        generated = g_voxelStreamingPerfStats.generated;
        jobs = g_voxelStreamingPerfStats.jobs;
        stepped = g_voxelStreamingPerfStats.stepped;
        built = g_voxelStreamingPerfStats.built;
        consumed = g_voxelStreamingPerfStats.consumed;
        skippedExisting = g_voxelStreamingPerfStats.skippedExisting;
        filteredOut = g_voxelStreamingPerfStats.filteredOut;
        rescueSurfaceQueued = g_voxelStreamingPerfStats.rescueSurfaceQueued;
        rescueMissingQueued = g_voxelStreamingPerfStats.rescueMissingQueued;
        droppedByCap = g_voxelStreamingPerfStats.droppedByCap;
        reprioritized = g_voxelStreamingPerfStats.reprioritized;
        generationMs = g_voxelStreamingPerfStats.generationMs;
    }

    bool IsSectionTerrainReady(const VoxelSectionKey& key) {
        return (g_voxelSectionJobs.count(key) == 0)
            && (g_voxelTerrainGenerated.count(key) > 0);
    }

    bool IsColumnSurfaceReady(int lod, const glm::ivec2& columnCoord) {
        return g_voxelColumnSurfaceReady.count(VoxelColumnModel::ColumnKey{lod, columnCoord}) > 0;
    }

    bool IsColumnFullyReady(int lod, const glm::ivec2& columnCoord) {
        auto it = g_voxelColumnDesiredMissing.find(VoxelColumnModel::ColumnKey{lod, columnCoord});
        if (it == g_voxelColumnDesiredMissing.end()) return false;
        return it->second <= 0;
    }

    void UpdateExpanseTerrain(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt; (void)win;
        if (!baseSystem.level || !baseSystem.instance || !baseSystem.world || !baseSystem.player) return;
        WorldContext& worldCtx = *baseSystem.world;
        if (!worldCtx.expanse.loaded) return;
        auto resetVoxelStreamingState = [&]() {
            if (!baseSystem.voxelWorld) return;
            baseSystem.voxelWorld->reset();
            g_voxelStreaming.pending.clear();
            g_voxelStreaming.pendingSet.clear();
            g_voxelStreaming.desired.clear();
            g_voxelSectionJobs.clear();
            g_voxelTerrainGenerated.clear();
            g_voxelStreaming.lastCenterSections.clear();
            g_voxelStreaming.lastRadii.clear();
            g_voxelColumnStreaming.desired.clear();
            g_voxelColumnStreaming.lastCenterColumns.clear();
            g_voxelColumnStreaming.lastRadii.clear();
            g_voxelColumnSurfaceReady.clear();
            g_voxelColumnDesiredMissing.clear();
            g_caveField.ready = false;
            g_caveField.a.clear();
            g_caveField.b.clear();
        };
        if (baseSystem.voxelWorld && baseSystem.registry) {
            auto it = baseSystem.registry->find("useVoxelLOD");
            baseSystem.voxelWorld->enabled = (it != baseSystem.registry->end() &&
                                              std::holds_alternative<bool>(it->second) &&
                                              std::get<bool>(it->second));
        }
        if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return;
        if (baseSystem.player
            && baseSystem.player->prismalMapMode == 2
            && getRegistryBool(baseSystem, "PrismalMapPauseWorldStreaming", true)) {
            return;
        }

        if (baseSystem.registry) {
            const std::string terrainSchemaVersion = "terrain_schema_portal_seabed_v21";
            auto it = baseSystem.registry->find("TerrainSchemaVersion");
            bool needsReset = true;
            if (it != baseSystem.registry->end() && std::holds_alternative<std::string>(it->second)) {
                needsReset = (std::get<std::string>(it->second) != terrainSchemaVersion);
            }
            if (needsReset) {
                resetVoxelStreamingState();
                (*baseSystem.registry)["TerrainSchemaVersion"] = terrainSchemaVersion;
            }
        }

        int desiredMaxLod = getRegistryInt(baseSystem, "voxelMaxLod", baseSystem.voxelWorld->maxLod);
        if (getRegistryBool(baseSystem, "voxelForceLod0Only", false)) {
            desiredMaxLod = 0;
        }
        if (desiredMaxLod < 0) desiredMaxLod = 0;
        while (desiredMaxLod > 0 && (baseSystem.voxelWorld->sectionSize >> desiredMaxLod) <= 0) {
            --desiredMaxLod;
        }
        if (desiredMaxLod != baseSystem.voxelWorld->maxLod) {
            baseSystem.voxelWorld->maxLod = desiredMaxLod;
            resetVoxelStreamingState();
        }

        std::string levelKey;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("level");
            if (it != baseSystem.registry->end() && std::holds_alternative<std::string>(it->second)) {
                levelKey = std::get<std::string>(it->second);
            }
        }
        if (g_voxelLevelKey != levelKey) {
            g_voxelLevelKey = levelKey;
            resetVoxelStreamingState();
        }

        UpdateExpanseVoxelWorld(baseSystem, prototypes, worldCtx, worldCtx.expanse);
    }
}
