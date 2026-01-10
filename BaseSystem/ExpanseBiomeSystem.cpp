#pragma once

#include <array>
#include <numeric>
#include <algorithm>
#include <random>
#include <fstream>
#include <iostream>
#include <limits>
#include <cmath>

namespace ExpanseBiomeSystemLogic {

    class PerlinNoise3D {
    public:
        PerlinNoise3D() { reseed(0); }
        explicit PerlinNoise3D(int seed) { reseed(seed); }

        void reseed(int seed) {
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

    struct NoiseState {
        PerlinNoise3D continental;
        PerlinNoise3D elevation;
        PerlinNoise3D ridge;
        int continentalSeed = std::numeric_limits<int>::min();
        int elevationSeed = std::numeric_limits<int>::min();
        int ridgeSeed = std::numeric_limits<int>::min();
    };

    static NoiseState g_noise;

    static bool isInOceanBand(const ExpanseConfig& cfg, float z) {
        for (const auto& band : cfg.oceanBands) {
            float minZ = std::min(band.minZ, band.maxZ);
            float maxZ = std::max(band.minZ, band.maxZ);
            if (z >= minZ && z < maxZ) return true;
        }
        return false;
    }

    static void ensureNoise(const ExpanseConfig& cfg) {
        if (g_noise.continentalSeed != cfg.continentalSeed) {
            g_noise.continental.reseed(cfg.continentalSeed);
            g_noise.continentalSeed = cfg.continentalSeed;
        }
        if (g_noise.elevationSeed != cfg.elevationSeed) {
            g_noise.elevation.reseed(cfg.elevationSeed);
            g_noise.elevationSeed = cfg.elevationSeed;
        }
        if (g_noise.ridgeSeed != cfg.ridgeSeed) {
            g_noise.ridge.reseed(cfg.ridgeSeed);
            g_noise.ridgeSeed = cfg.ridgeSeed;
        }
    }

    void LoadExpanseConfig(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt; (void)win;
        if (!baseSystem.world) return;
        WorldContext& world = *baseSystem.world;
        ExpanseConfig cfg;

        std::ifstream f("Procedures/expanse_terrain.json");
        if (!f.is_open()) {
            std::cerr << "ExpanseBiomeSystem: could not open Procedures/expanse_terrain.json" << std::endl;
            world.expanse = cfg;
            world.expanse.loaded = false;
            return;
        }

        try {
            json data = json::parse(f);
            if (data.contains("worlds")) {
                const auto& worlds = data["worlds"];
                cfg.terrainWorld = worlds.value("terrain", cfg.terrainWorld);
                cfg.waterWorld = worlds.value("water", cfg.waterWorld);
                cfg.treesWorld = worlds.value("trees", cfg.treesWorld);
            }
            if (data.contains("noise")) {
                const auto& noise = data["noise"];
                cfg.continentalSeed = noise.value("continentalSeed", cfg.continentalSeed);
                cfg.elevationSeed = noise.value("elevationSeed", cfg.elevationSeed);
                cfg.ridgeSeed = noise.value("ridgeSeed", cfg.ridgeSeed);
                cfg.continentalScale = noise.value("continentalScale", cfg.continentalScale);
                cfg.elevationScale = noise.value("elevationScale", cfg.elevationScale);
                cfg.ridgeScale = noise.value("ridgeScale", cfg.ridgeScale);
            }
            if (data.contains("terrain")) {
                const auto& terrain = data["terrain"];
                cfg.landThreshold = terrain.value("landThreshold", cfg.landThreshold);
                cfg.waterSurface = terrain.value("waterSurface", cfg.waterSurface);
                cfg.waterFloor = terrain.value("waterFloor", cfg.waterFloor);
                cfg.minY = terrain.value("minY", cfg.minY);
                cfg.baseElevation = terrain.value("baseElevation", cfg.baseElevation);
                cfg.baseRidge = terrain.value("baseRidge", cfg.baseRidge);
                cfg.mountainElevation = terrain.value("mountainElevation", cfg.mountainElevation);
                cfg.mountainRidge = terrain.value("mountainRidge", cfg.mountainRidge);
                cfg.mountainMinX = terrain.value("mountainMinX", cfg.mountainMinX);
                cfg.mountainMaxX = terrain.value("mountainMaxX", cfg.mountainMaxX);
                cfg.desertStartX = terrain.value("desertStartX", cfg.desertStartX);
                cfg.snowStartZ = terrain.value("snowStartZ", cfg.snowStartZ);
                cfg.soilDepth = terrain.value("soilDepth", cfg.soilDepth);
                cfg.stoneDepth = terrain.value("stoneDepth", cfg.stoneDepth);
                cfg.oceanBands.clear();
                if (terrain.contains("oceanBands") && terrain["oceanBands"].is_array()) {
                    for (const auto& band : terrain["oceanBands"]) {
                        ExpanseOceanBand entry;
                        entry.minZ = band.value("minZ", entry.minZ);
                        entry.maxZ = band.value("maxZ", entry.maxZ);
                        cfg.oceanBands.push_back(entry);
                    }
                }
            }
            if (data.contains("colors")) {
                const auto& colors = data["colors"];
                cfg.colorGrass = colors.value("grass", cfg.colorGrass);
                cfg.colorSand = colors.value("sand", cfg.colorSand);
                cfg.colorSnow = colors.value("snow", cfg.colorSnow);
                cfg.colorSoil = colors.value("soil", cfg.colorSoil);
                cfg.colorStone = colors.value("stone", cfg.colorStone);
                cfg.colorWater = colors.value("water", cfg.colorWater);
                cfg.colorWood = colors.value("wood", cfg.colorWood);
                cfg.colorLeaf = colors.value("leaf", cfg.colorLeaf);
                cfg.colorSeabed = colors.value("seabed", cfg.colorSeabed);
            }
        } catch (const std::exception& e) {
            std::cerr << "ExpanseBiomeSystem: failed to parse expanse_terrain.json (" << e.what() << ")" << std::endl;
            world.expanse = cfg;
            world.expanse.loaded = false;
            return;
        }

        cfg.loaded = true;
        world.expanse = cfg;
        ensureNoise(world.expanse);
    }

    bool SampleTerrain(const WorldContext& worldCtx, float x, float z, float& outHeight) {
        if (!worldCtx.expanse.loaded) return false;
        const ExpanseConfig& cfg = worldCtx.expanse;

        if (isInOceanBand(cfg, z)) {
            outHeight = cfg.waterFloor;
            return false;
        }

        ensureNoise(cfg);
        float continental = (g_noise.continental.noise(x / cfg.continentalScale, 0.0f, z / cfg.continentalScale) + 1.0f) * 0.5f;
        if (continental <= cfg.landThreshold) {
            outHeight = cfg.waterFloor;
            return false;
        }

        float elevation = (g_noise.elevation.noise(x / cfg.elevationScale, 0.0f, z / cfg.elevationScale) + 1.0f) * 0.5f;
        float ridge = g_noise.ridge.noise(x / cfg.ridgeScale, 0.0f, z / cfg.ridgeScale);
        float height = elevation * cfg.baseElevation + ridge * cfg.baseRidge;

        if (x >= cfg.mountainMinX && x < cfg.mountainMaxX) {
            height = elevation * cfg.mountainElevation + ridge * cfg.mountainRidge;
        }

        outHeight = height;
        return true;
    }
}
