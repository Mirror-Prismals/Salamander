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

namespace HostLogic { const Entity* findPrototype(const std::string& name, const std::vector<Entity>& prototypes); EntityInstance CreateInstance(BaseSystem& baseSystem, int prototypeID, glm::vec3 position, glm::vec3 color); }
namespace ExpanseBiomeSystemLogic { bool SampleTerrain(const WorldContext& worldCtx, float x, float z, float& outHeight); }

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
        };

        static VoxelStreamingState g_voxelStreaming;
        static std::chrono::steady_clock::time_point g_lastVoxelPerf = std::chrono::steady_clock::now();
        static std::string g_voxelLevelKey;
        static CaveField g_caveField;
        

        void ensureCaveField(const ExpanseConfig& cfg) {
            if (g_caveField.ready) return;
            const int step = 4;
            const int sizeXZ = 2304;
            const int halfXZ = sizeXZ / 2;
            const int minY = -96;
            const int heightY = 256; // -96..160
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

        int resolveBiome(const ExpanseConfig& cfg, float x, float z) {
            if (x >= cfg.desertStartX) return 1;
            if (z <= cfg.snowStartZ) return 2;
            return 0;
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

        int sectionSizeForLod(const VoxelWorldContext& voxelWorld, int lod) {
            int size = voxelWorld.sectionSize >> lod;
            return size > 0 ? size : 1;
        }

        glm::ivec3 floorDivVec(const glm::ivec3& v, int divisor) {
            return glm::ivec3(
                floorDivInt(v.x, divisor),
                floorDivInt(v.y, divisor),
                floorDivInt(v.z, divisor)
            );
        }

        void GenerateExpanseSectionVoxel(BaseSystem& baseSystem,
                                         std::vector<Entity>& prototypes,
                                         WorldContext& worldCtx,
                                         const ExpanseConfig& cfg,
                                         int lod,
                                         const glm::ivec3& sectionCoord) {
            if (!baseSystem.voxelWorld) return;
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            int size = sectionSizeForLod(voxelWorld, lod);
            int scale = 1 << lod;
            auto pickBlockProto = [&](std::initializer_list<const char*> names) -> const Entity* {
                for (const char* name : names) {
                    const Entity* proto = HostLogic::findPrototype(name, prototypes);
                    if (proto && proto->prototypeID != 0) return proto;
                }
                return findNonZeroBlockProto(prototypes);
            };
            const Entity* surfaceProto = pickBlockProto({"ScaffoldBlock", "GrassBlockTex"});
            const Entity* sandProto = pickBlockProto({"ScaffoldBlock", "SandBlockTex"});
            const Entity* soilProto = pickBlockProto({"ScaffoldBlock", "GrassBlockTex"});
            const Entity* stoneProto = pickBlockProto({"ScaffoldBlock", "StoneBlockTex"});
            const Entity* waterProto = HostLogic::findPrototype("Water", prototypes);
            if (!surfaceProto || !waterProto) return;
            if (!sandProto) sandProto = surfaceProto;
            if (!soilProto) soilProto = surfaceProto;
            if (!stoneProto) stoneProto = surfaceProto;

            if (lod == 0) {
                ensureCaveField(cfg);
            }

            glm::vec3 grassColor = GetColor(worldCtx, cfg.colorGrass, glm::vec3(0.2f, 0.8f, 0.2f));
            glm::vec3 sandColor = GetColor(worldCtx, cfg.colorSand, glm::vec3(0.9f, 0.8f, 0.4f));
            glm::vec3 soilColor = GetColor(worldCtx, cfg.colorSoil, glm::vec3(0.33f, 0.22f, 0.15f));
            glm::vec3 stoneColor = GetColor(worldCtx, cfg.colorStone, glm::vec3(0.4f, 0.4f, 0.4f));
            glm::vec3 waterColor = GetColor(worldCtx, cfg.colorWater, glm::vec3(0.05f, 0.2f, 0.5f));
            glm::vec3 seabedColor = GetColor(worldCtx, cfg.colorSeabed, sandColor);

            int waterSurfaceY = static_cast<int>(std::floor(cfg.waterSurface));
            int waterFloorY = static_cast<int>(std::floor(cfg.waterFloor));
            int sectionMinY = sectionCoord.y * size * scale;
            int sectionMaxY = sectionMinY + size * scale - 1;
            int minY = std::min(cfg.minY, waterFloorY);
            if (lod == 0) {
                minY = std::min(minY, -96);
            }
            int maxY = static_cast<int>(std::ceil(cfg.baseElevation + cfg.mountainElevation));
            if (cfg.islandRadius > 0.0f) {
                maxY = static_cast<int>(std::ceil(cfg.waterSurface + cfg.islandMaxHeight + cfg.islandNoiseAmp));
            }
            maxY = std::max(maxY, waterSurfaceY);
            if (sectionMaxY < minY || sectionMinY > maxY) return;

            bool wroteAny = false;
            for (int z = 0; z < size; ++z) {
                for (int x = 0; x < size; ++x) {
                    float worldX = static_cast<float>((sectionCoord.x * size + x) * scale);
                    float worldZ = static_cast<float>((sectionCoord.z * size + z) * scale);
                    float height = 0.0f;
                    bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(worldCtx, worldX, worldZ, height);
                    int surfaceY = static_cast<int>(std::floor(height));
                    bool isBeach = isLand && (surfaceY <= waterSurfaceY + static_cast<int>(cfg.beachHeight));
                    bool inIsland = false;
                    if (cfg.islandRadius > 0.0f) {
                        float dx = worldX - cfg.islandCenterX;
                        float dz = worldZ - cfg.islandCenterZ;
                        float dist = std::sqrt(dx * dx + dz * dz);
                        inIsland = dist < cfg.islandRadius;
                    }

                    bool carveSurface = false;
                    if (isLand && !isBeach) {
                        float sv1 = 0.0f;
                        float sv2 = 0.0f;
                        if (sampleCaveField(worldX, static_cast<float>(surfaceY), worldZ, sv1, sv2)) {
                            // Stronger threshold at the surface to reduce openings.
                            if (sv1 > 0.75f || sv2 > 0.70f) carveSurface = true;
                        }
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
                            trySetCell(floorDivInt(waterFloorY, scale), sandProto->prototypeID, packColor(seabedColor));
                            if (waterSurfaceY > waterFloorY) {
                                trySetCell(floorDivInt(waterSurfaceY, scale), waterProto->prototypeID, packColor(waterColor));
                            }
                        } else {
                            if (carveSurface) continue;
                            trySetCell(floorDivInt(surfaceY, scale),
                                       (isBeach ? sandProto->prototypeID : surfaceProto->prototypeID),
                                       packColor(isBeach ? sandColor : grassColor));
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
                                float t = std::clamp(depth / 24.0f, 0.0f, 1.0f);
                                float thrA = 0.72f + (0.62f - 0.72f) * t;
                                float thrB = 0.68f + (0.58f - 0.68f) * t;
                                if (v1 > thrA || v2 > thrB) carve = true;
                            } else {
                                if (v1 > 0.62f || v2 > 0.58f) carve = true;
                            }
                        }

                        if (!isLand) {
                            if (carve) {
                                if (worldY <= waterSurfaceY) {
                                    voxelWorld.setBlockLod(lod, lodCoord, waterProto->prototypeID, packColor(waterColor), false);
                                    wroteAny = true;
                                }
                                continue;
                            }
                            if (rangeContains(waterFloorY)) {
                                voxelWorld.setBlockLod(lod, lodCoord, sandProto->prototypeID, packColor(seabedColor), false);
                                wroteAny = true;
                            } else if (waterSurfaceY > waterFloorY) {
                                int waterMin = waterFloorY + 1;
                                int waterMax = waterSurfaceY;
                                if (rangeOverlaps(waterMin, waterMax)) {
                                    voxelWorld.setBlockLod(lod, lodCoord, waterProto->prototypeID, packColor(waterColor), false);
                                    wroteAny = true;
                                }
                            }
                            continue;
                        }

                        if ((carveSurface && worldY <= surfaceY && worldY >= surfaceY - 2) || carve) {
                            if (worldY <= waterSurfaceY) {
                                voxelWorld.setBlockLod(lod, lodCoord, waterProto->prototypeID, packColor(waterColor), false);
                                wroteAny = true;
                            }
                            continue;
                        }

                        if (rangeContains(surfaceY)) {
                            glm::vec3 topColor = isBeach ? sandColor : grassColor;
                            voxelWorld.setBlockLod(lod,
                                                   lodCoord,
                                                   (isBeach ? sandProto->prototypeID : surfaceProto->prototypeID),
                                                   packColor(topColor),
                                                   false);
                            wroteAny = true;
                            continue;
                        }

                        if (worldY < surfaceY) {
                            int soilMin = surfaceY - cfg.soilDepth;
                            int stoneMin = surfaceY - cfg.soilDepth - cfg.stoneDepth;
                            if (rangeContains(waterFloorY)) {
                                voxelWorld.setBlockLod(lod, lodCoord, sandProto->prototypeID, packColor(seabedColor), false);
                                wroteAny = true;
                                continue;
                            }
                            if (rangeOverlaps(soilMin, surfaceY - 1)) {
                                voxelWorld.setBlockLod(lod, lodCoord, soilProto->prototypeID, packColor(soilColor), false);
                                wroteAny = true;
                                continue;
                            }
                            if (rangeOverlaps(cfg.minY, stoneMin)) {
                                voxelWorld.setBlockLod(lod, lodCoord, stoneProto->prototypeID, packColor(stoneColor), false);
                                wroteAny = true;
                                continue;
                            }
                            if (worldY < stoneMin) {
                                voxelWorld.setBlockLod(lod, lodCoord, stoneProto->prototypeID, packColor(stoneColor), false);
                                wroteAny = true;
                                continue;
                            }
                        }
                    }
                }
            }
            if (wroteAny) {
                VoxelSectionKey dirtyKey{lod, sectionCoord};
                auto it = voxelWorld.sections.find(dirtyKey);
                if (it != voxelWorld.sections.end()) {
                    it->second.editVersion += 1;
                    it->second.dirty = true;
                    voxelWorld.dirtySections.insert(dirtyKey);
                }
            }
        }

        void UpdateExpanseVoxelWorld(BaseSystem& baseSystem,
                                     std::vector<Entity>& prototypes,
                                     WorldContext& worldCtx,
                                     const ExpanseConfig& cfg) {
            if (!baseSystem.voxelWorld || !baseSystem.player) return;
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            glm::vec3 cameraPos = baseSystem.player->cameraPosition;
            int maxLod = voxelWorld.maxLod;

            int prevRadius = 0;
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
                if (g_voxelStreaming.lastCenterSections[lod] != centerSection ||
                    g_voxelStreaming.lastRadii[lod] != radius) {
                    rebuildDesired = true;
                }
                g_voxelStreaming.lastCenterSections[lod] = centerSection;
                g_voxelStreaming.lastRadii[lod] = radius;
            }

            if (rebuildDesired) {
                g_voxelStreaming.desired.clear();
                g_voxelStreaming.desired.reserve(2048);
                g_voxelStreaming.pending.clear();
                g_voxelStreaming.pendingSet.clear();
            }

            if (rebuildDesired) {
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

                    int minY = cfg.minY;
                    if (lod == 0) {
                        minY = std::min(minY, -96);
                    }
                    int maxY = static_cast<int>(std::ceil(cfg.baseElevation + cfg.mountainElevation));
                    int minSectionY = floorDivInt(minY, scale * size);
                    int maxSectionY = floorDivInt(maxY, scale * size);

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
                                for (int sy = minSectionY; sy <= maxSectionY; ++sy) {
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
                                                if (voxelWorld.sections.find(key) == voxelWorld.sections.end()
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
                                        if (voxelWorld.sections.find(key) == voxelWorld.sections.end()
                                            && g_voxelStreaming.pendingSet.count(key) == 0) {
                                            g_voxelStreaming.pending.push_back(key);
                                            g_voxelStreaming.pendingSet.insert(key);
                                        }
                                    }
                                }
                            }
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
                }
            }

            // Drop pending entries that are no longer desired.
            if (rebuildDesired && !g_voxelStreaming.pending.empty()) {
                std::vector<VoxelSectionKey> filtered;
                filtered.reserve(g_voxelStreaming.pending.size());
                for (const auto& key : g_voxelStreaming.pending) {
                    if (g_voxelStreaming.desired.count(key) > 0) {
                        filtered.push_back(key);
                    } else {
                        g_voxelStreaming.pendingSet.erase(key);
                    }
                }
                g_voxelStreaming.pending.swap(filtered);
            }

            int generationBudget = getRegistryInt(baseSystem, "voxelSectionsPerFrame", 0);
            int count = generationBudget > 0
                ? std::min(static_cast<int>(g_voxelStreaming.pending.size()), generationBudget)
                : static_cast<int>(g_voxelStreaming.pending.size());
            auto genStart = std::chrono::steady_clock::now();
            for (int i = 0; i < count; ++i) {
                const auto& key = g_voxelStreaming.pending[i];
                GenerateExpanseSectionVoxel(baseSystem, prototypes, worldCtx, cfg, key.lod, key.coord);
                g_voxelStreaming.pendingSet.erase(key);
            }
            if (count > 0) {
                g_voxelStreaming.pending.erase(g_voxelStreaming.pending.begin(),
                                               g_voxelStreaming.pending.begin() + count);
            }

            auto now = std::chrono::steady_clock::now();
            if (now - g_lastVoxelPerf >= std::chrono::seconds(1)) {
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - genStart).count();
                std::cout << "TerrainGeneration: voxel sections generated "
                          << count << " in " << elapsedMs << " ms. Pending "
                          << g_voxelStreaming.pending.size() << "." << std::endl;
                g_lastVoxelPerf = now;
            }

        }

    }

    void UpdateExpanseTerrain(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt; (void)win;
        if (!baseSystem.level || !baseSystem.instance || !baseSystem.world || !baseSystem.player) return;
        WorldContext& worldCtx = *baseSystem.world;
        if (!worldCtx.expanse.loaded) return;
        if (baseSystem.voxelWorld && baseSystem.registry) {
            auto it = baseSystem.registry->find("useVoxelLOD");
            baseSystem.voxelWorld->enabled = (it != baseSystem.registry->end() &&
                                              std::holds_alternative<bool>(it->second) &&
                                              std::get<bool>(it->second));
        }
        if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return;

        std::string levelKey;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("level");
            if (it != baseSystem.registry->end() && std::holds_alternative<std::string>(it->second)) {
                levelKey = std::get<std::string>(it->second);
            }
        }
        if (g_voxelLevelKey != levelKey) {
            g_voxelLevelKey = levelKey;
            baseSystem.voxelWorld->reset();
            g_voxelStreaming.pending.clear();
            g_voxelStreaming.pendingSet.clear();
            g_voxelStreaming.desired.clear();
            g_voxelStreaming.lastCenterSections.clear();
            g_voxelStreaming.lastRadii.clear();
            g_caveField.ready = false;
            g_caveField.a.clear();
            g_caveField.b.clear();
        }

        UpdateExpanseVoxelWorld(baseSystem, prototypes, worldCtx, worldCtx.expanse);
    }
}
