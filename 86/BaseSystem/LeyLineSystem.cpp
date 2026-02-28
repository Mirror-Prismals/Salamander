#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>

namespace LeyLineSystemLogic {

    namespace {
        constexpr int kMaxBlend = 8;
        constexpr std::array<glm::ivec2, 8> kNeighborOffsets = {
            glm::ivec2(-1, 0), glm::ivec2(-1, 1), glm::ivec2(0, 1), glm::ivec2(1, 1),
            glm::ivec2(1, 0), glm::ivec2(1, -1), glm::ivec2(0, -1), glm::ivec2(-1, -1)
        };
        constexpr float kInvSqrt2 = 0.70710678118f;
        constexpr std::array<glm::vec2, 8> kNeighborDirs = {
            glm::vec2(-1.0f, 0.0f),
            glm::vec2(-kInvSqrt2, kInvSqrt2),
            glm::vec2(0.0f, 1.0f),
            glm::vec2(kInvSqrt2, kInvSqrt2),
            glm::vec2(1.0f, 0.0f),
            glm::vec2(kInvSqrt2, -kInvSqrt2),
            glm::vec2(0.0f, -1.0f),
            glm::vec2(-kInvSqrt2, -kInvSqrt2)
        };

        struct PrototypeSample {
            float stress = 0.0f;
            float uplift = 0.0f;
        };

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
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stoi(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        float getRegistryFloat(const BaseSystem& baseSystem, const std::string& key, float fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stof(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        uint32_t hash32(uint32_t x) {
            x += 0x9e3779b9u;
            x ^= (x >> 16);
            x *= 0x7feb352du;
            x ^= (x >> 15);
            x *= 0x846ca68bu;
            x ^= (x >> 16);
            return x;
        }

        float rand01(uint32_t seed, uint32_t index) {
            uint32_t h = hash32(seed ^ (index * 0x9e3779b9u + 0x85ebca6bu));
            return static_cast<float>(h) / static_cast<float>(std::numeric_limits<uint32_t>::max());
        }

        float hashNoise(int x, int z, int seed) {
            uint32_t ux = static_cast<uint32_t>(x);
            uint32_t uz = static_cast<uint32_t>(z);
            uint32_t us = static_cast<uint32_t>(seed);
            uint32_t n = (ux * 73856093u) ^ (uz * 19349663u) ^ (us * 83492791u);
            n = (n << 13) ^ n;
            uint32_t t = n * (n * n * 15731u + 789221u) + 1376312589u;
            return 1.0f - static_cast<float>(t & 0x7fffffffu) / 1073741824.0f;
        }

        float valueNoise(float x, float z, int seed) {
            int xi = static_cast<int>(std::floor(x));
            int zi = static_cast<int>(std::floor(z));
            float xf = x - static_cast<float>(xi);
            float zf = z - static_cast<float>(zi);
            float v00 = hashNoise(xi, zi, seed);
            float v10 = hashNoise(xi + 1, zi, seed);
            float v01 = hashNoise(xi, zi + 1, seed);
            float v11 = hashNoise(xi + 1, zi + 1, seed);
            float u = xf * xf * (3.0f - 2.0f * xf);
            float v = zf * zf * (3.0f - 2.0f * zf);
            float x0 = v00 * (1.0f - u) + v10 * u;
            float x1 = v01 * (1.0f - u) + v11 * u;
            return x0 * (1.0f - v) + x1 * v;
        }

        float fbm(float x,
                  float z,
                  int seed,
                  int octaves,
                  float persistence,
                  float lacunarity) {
            int clampedOctaves = std::clamp(octaves, 1, 8);
            float amp = 1.0f;
            float freq = 1.0f;
            float sum = 0.0f;
            float maxSum = 0.0f;
            for (int i = 0; i < clampedOctaves; ++i) {
                sum += valueNoise(x * freq, z * freq, seed + i * 1013) * amp;
                maxSum += amp;
                amp *= persistence;
                freq *= lacunarity;
            }
            if (maxSum <= 1e-6f) return 0.0f;
            return sum / maxSum;
        }

        void applyLeyLineJson(LeyLineContext& ctx, const json& leyData) {
            if (!leyData.is_object()) return;
            auto readBool = [&](const char* key, bool& target) {
                if (!leyData.contains(key) || !leyData[key].is_boolean()) return;
                target = leyData[key].get<bool>();
            };
            auto readInt = [&](const char* key, int& target) {
                if (!leyData.contains(key) || !leyData[key].is_number_integer()) return;
                target = leyData[key].get<int>();
            };
            auto readFloat = [&](const char* key, float& target) {
                if (!leyData.contains(key) || !leyData[key].is_number()) return;
                target = leyData[key].get<float>();
            };

            readBool("enabled", ctx.enabled);
            readBool("precomputeField", ctx.precomputeField);
            readBool("compressionOnly", ctx.compressionOnly);
            readInt("seed", ctx.seed);
            readInt("plateCount", ctx.plateCount);
            readInt("blendCount", ctx.blendCount);
            readFloat("domainMinX", ctx.domainMinX);
            readFloat("domainMaxX", ctx.domainMaxX);
            readFloat("domainMinZ", ctx.domainMinZ);
            readFloat("domainMaxZ", ctx.domainMaxZ);
            readFloat("sampleStep", ctx.sampleStep);
            readFloat("plateInfluenceRadius", ctx.plateInfluenceRadius);
            readFloat("domeRadius", ctx.domeRadius);
            readFloat("domeHeight", ctx.domeHeight);
            readFloat("baseHeight", ctx.baseHeight);
            readFloat("stressHeightScale", ctx.stressHeightScale);
            readFloat("opposingCompressionScale", ctx.opposingCompressionScale);
            readFloat("noiseScale", ctx.noiseScale);
            readInt("noiseOctaves", ctx.noiseOctaves);
            readFloat("noisePersistence", ctx.noisePersistence);
            readFloat("noiseLacunarity", ctx.noiseLacunarity);
            readFloat("upliftGain", ctx.upliftGain);
            readFloat("upliftMax", ctx.upliftMax);
            readFloat("stressClamp", ctx.stressClamp);
        }

        void sanitizeContext(LeyLineContext& ctx) {
            if (!(ctx.domainMaxX > ctx.domainMinX)) {
                ctx.domainMinX = -1024.0f;
                ctx.domainMaxX = 1024.0f;
            }
            if (!(ctx.domainMaxZ > ctx.domainMinZ)) {
                ctx.domainMinZ = -1024.0f;
                ctx.domainMaxZ = 1024.0f;
            }

            ctx.plateCount = std::clamp(ctx.plateCount, 1, 64);
            ctx.blendCount = std::clamp(ctx.blendCount, 1, kMaxBlend);
            ctx.sampleStep = std::max(1.0f, ctx.sampleStep);
            ctx.plateInfluenceRadius = std::max(0.25f, ctx.plateInfluenceRadius);
            ctx.domeRadius = std::max(0.25f, ctx.domeRadius);
            ctx.noiseScale = std::max(1e-5f, ctx.noiseScale);
            ctx.noiseOctaves = std::clamp(ctx.noiseOctaves, 1, 8);
            ctx.noisePersistence = std::clamp(ctx.noisePersistence, 0.1f, 0.95f);
            ctx.noiseLacunarity = std::clamp(ctx.noiseLacunarity, 1.1f, 4.0f);
            ctx.opposingCompressionScale = std::clamp(ctx.opposingCompressionScale, 0.0f, 1.0f);
            ctx.upliftGain = std::max(0.0f, ctx.upliftGain);
            ctx.upliftMax = std::max(0.0f, ctx.upliftMax);
            ctx.stressClamp = std::max(1e-4f, ctx.stressClamp);
        }

        void buildPlates(LeyLineContext& ctx) {
            ctx.plates.clear();
            ctx.plates.reserve(static_cast<size_t>(ctx.plateCount));

            const float spanX = ctx.domainMaxX - ctx.domainMinX;
            const float spanZ = ctx.domainMaxZ - ctx.domainMinZ;
            const uint32_t seed = static_cast<uint32_t>(ctx.seed);
            for (int i = 0; i < ctx.plateCount; ++i) {
                float px = ctx.domainMinX + rand01(seed, static_cast<uint32_t>(i * 4 + 0)) * spanX;
                float pz = ctx.domainMinZ + rand01(seed, static_cast<uint32_t>(i * 4 + 1)) * spanZ;
                float vx = rand01(seed, static_cast<uint32_t>(i * 4 + 2)) * 2.0f - 1.0f;
                float vz = rand01(seed, static_cast<uint32_t>(i * 4 + 3)) * 2.0f - 1.0f;
                glm::vec2 v(vx, vz);
                float len = glm::length(v);
                if (len < 1e-4f) {
                    v = glm::vec2(0.7f, 0.2f);
                    len = glm::length(v);
                }
                float speed = 0.35f + 1.25f * rand01(seed ^ 0x68bc21ebu, static_cast<uint32_t>(i));
                v = (v / len) * speed;

                LeyPlate plate;
                plate.seed = glm::vec2(px, pz);
                plate.velocity = v;
                ctx.plates.push_back(plate);
            }
        }

        int nearestPlateIndex(const LeyLineContext& ctx, const glm::vec2& p) {
            int bestID = -1;
            float bestD2 = std::numeric_limits<float>::max();
            for (int i = 0; i < static_cast<int>(ctx.plates.size()); ++i) {
                glm::vec2 d = p - ctx.plates[static_cast<size_t>(i)].seed;
                float d2 = d.x * d.x + d.y * d.y;
                if (d2 < bestD2) {
                    bestD2 = d2;
                    bestID = i;
                }
            }
            return bestID;
        }

        void nearestPlates(const LeyLineContext& ctx,
                           const glm::vec2& p,
                           int blendCount,
                           int* outIDs,
                           float* outDistances) {
            for (int i = 0; i < blendCount; ++i) {
                outIDs[i] = -1;
                outDistances[i] = std::numeric_limits<float>::max();
            }

            for (int i = 0; i < static_cast<int>(ctx.plates.size()); ++i) {
                glm::vec2 d = p - ctx.plates[static_cast<size_t>(i)].seed;
                float dist = std::sqrt(d.x * d.x + d.y * d.y);
                for (int slot = 0; slot < blendCount; ++slot) {
                    if (dist >= outDistances[slot]) continue;
                    for (int shift = blendCount - 1; shift > slot; --shift) {
                        outIDs[shift] = outIDs[shift - 1];
                        outDistances[shift] = outDistances[shift - 1];
                    }
                    outIDs[slot] = i;
                    outDistances[slot] = dist;
                    break;
                }
            }
        }

        PrototypeSample computePrototypeSample(const LeyLineContext& ctx, float x, float z) {
            PrototypeSample sample;
            if (ctx.plates.empty()) return sample;

            const int blendCount = std::clamp(ctx.blendCount, 1, kMaxBlend);
            int ids[kMaxBlend];
            float ds[kMaxBlend];
            nearestPlates(ctx, glm::vec2(x, z), blendCount, ids, ds);

            const float n = fbm(x * ctx.noiseScale,
                                z * ctx.noiseScale,
                                ctx.seed + 1337,
                                ctx.noiseOctaves,
                                ctx.noisePersistence,
                                ctx.noiseLacunarity);

            const glm::vec2 p(x, z);
            const float influence = std::max(0.25f, ctx.plateInfluenceRadius);
            const float domeRadius = std::max(0.25f, ctx.domeRadius);
            float wsum = 0.0f;
            float weightedHeight = 0.0f;
            float maxAmp = 0.0f;

            for (int j = 0; j < blendCount; ++j) {
                if (ids[j] < 0) continue;

                const LeyPlate& plate = ctx.plates[static_cast<size_t>(ids[j])];
                const float d = ds[j];
                const float w = 1.0f / (0.1f + std::pow(d / influence, 2.0f));
                wsum += w;

                float r = glm::length(p - plate.seed);
                float domeMask = 1.0f - glm::clamp(r / domeRadius, 0.0f, 1.0f);
                float dome = ctx.domeHeight * domeMask * domeMask;

                float amp = 0.0f;
                for (size_t k = 0; k < kNeighborOffsets.size(); ++k) {
                    glm::vec2 q = p + glm::vec2(kNeighborOffsets[k]);
                    int neighborID = nearestPlateIndex(ctx, q);
                    if (neighborID < 0 || neighborID == ids[j]) continue;

                    const LeyPlate& neighborPlate = ctx.plates[static_cast<size_t>(neighborID)];
                    glm::vec2 rel = plate.velocity - neighborPlate.velocity;
                    float proj = glm::dot(rel, kNeighborDirs[k]);
                    amp += (proj > 0.0f) ? proj : proj * ctx.opposingCompressionScale;
                }

                const float ampWithNoise = amp * n;
                if (std::fabs(ampWithNoise) > std::fabs(maxAmp)) {
                    maxAmp = ampWithNoise;
                }

                weightedHeight += w * (ctx.baseHeight + dome + ctx.stressHeightScale * ampWithNoise);
            }

            sample.stress = maxAmp;
            if (wsum > 1e-6f) {
                sample.uplift = (weightedHeight / wsum) - ctx.baseHeight;
            }
            return sample;
        }

        void precomputeField(LeyLineContext& ctx) {
            ctx.fieldWidth = 0;
            ctx.fieldHeight = 0;
            ctx.stressField.clear();
            ctx.upliftField.clear();

            if (!ctx.precomputeField) return;

            float spanX = ctx.domainMaxX - ctx.domainMinX;
            float spanZ = ctx.domainMaxZ - ctx.domainMinZ;
            int width = static_cast<int>(std::floor(spanX / ctx.sampleStep)) + 1;
            int height = static_cast<int>(std::floor(spanZ / ctx.sampleStep)) + 1;
            width = std::max(width, 2);
            height = std::max(height, 2);

            const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
            ctx.stressField.assign(count, 0.0f);
            ctx.upliftField.assign(count, 0.0f);
            ctx.fieldWidth = width;
            ctx.fieldHeight = height;

            for (int z = 0; z < height; ++z) {
                float sampleZ = ctx.domainMinZ + static_cast<float>(z) * ctx.sampleStep;
                for (int x = 0; x < width; ++x) {
                    float sampleX = ctx.domainMinX + static_cast<float>(x) * ctx.sampleStep;
                    PrototypeSample sample = computePrototypeSample(ctx, sampleX, sampleZ);
                    size_t idx = static_cast<size_t>(z) * static_cast<size_t>(width) + static_cast<size_t>(x);
                    ctx.stressField[idx] = sample.stress;
                    ctx.upliftField[idx] = sample.uplift;
                }
            }
        }

        float sampleBilinearField(const LeyLineContext& ctx,
                                  const std::vector<float>& field,
                                  float x,
                                  float z) {
            if (ctx.fieldWidth <= 0 || ctx.fieldHeight <= 0) return 0.0f;
            if (field.size() != static_cast<size_t>(ctx.fieldWidth) * static_cast<size_t>(ctx.fieldHeight)) return 0.0f;

            float fx = (x - ctx.domainMinX) / ctx.sampleStep;
            float fz = (z - ctx.domainMinZ) / ctx.sampleStep;
            fx = glm::clamp(fx, 0.0f, static_cast<float>(ctx.fieldWidth - 1));
            fz = glm::clamp(fz, 0.0f, static_cast<float>(ctx.fieldHeight - 1));

            int x0 = static_cast<int>(std::floor(fx));
            int z0 = static_cast<int>(std::floor(fz));
            int x1 = std::min(x0 + 1, ctx.fieldWidth - 1);
            int z1 = std::min(z0 + 1, ctx.fieldHeight - 1);
            float tx = fx - static_cast<float>(x0);
            float tz = fz - static_cast<float>(z0);

            auto at = [&](int sx, int sz) -> float {
                size_t idx = static_cast<size_t>(sz) * static_cast<size_t>(ctx.fieldWidth) + static_cast<size_t>(sx);
                return field[idx];
            };

            float v00 = at(x0, z0);
            float v10 = at(x1, z0);
            float v01 = at(x0, z1);
            float v11 = at(x1, z1);
            float a = v00 * (1.0f - tx) + v10 * tx;
            float b = v01 * (1.0f - tx) + v11 * tx;
            return a * (1.0f - tz) + b * tz;
        }

        float normalizeUplift(const LeyLineContext& ctx, float rawUplift) {
            float stressLike = ctx.compressionOnly ? std::max(0.0f, rawUplift) : std::fabs(rawUplift);
            float normalized = glm::clamp(stressLike / ctx.stressClamp, 0.0f, 1.0f);
            float uplift = normalized * ctx.upliftGain;
            if (ctx.upliftMax > 0.0f) uplift = std::min(uplift, ctx.upliftMax);
            if (!std::isfinite(uplift)) return 0.0f;
            return std::max(0.0f, uplift);
        }
    } // namespace

    void LoadLeyLines(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes;
        (void)dt;
        (void)win;
        if (!baseSystem.world) return;

        LeyLineContext ctx;
        ctx.enabled = getRegistryBool(baseSystem, "LeyLineSystem", ctx.enabled);

        std::ifstream f("Procedures/expanse_terrain.json");
        if (f.is_open()) {
            try {
                json data = json::parse(f);
                if (data.contains("leyLines")) {
                    applyLeyLineJson(ctx, data["leyLines"]);
                }
            } catch (const std::exception& e) {
                std::cerr << "LeyLineSystem: failed parsing Procedures/expanse_terrain.json (" << e.what() << ")" << std::endl;
            }
        }

        ctx.precomputeField = getRegistryBool(baseSystem, "LeyLinePrecomputeField", ctx.precomputeField);
        ctx.compressionOnly = getRegistryBool(baseSystem, "LeyLineCompressionOnly", ctx.compressionOnly);
        ctx.seed = getRegistryInt(baseSystem, "LeyLineSeed", ctx.seed);
        ctx.plateCount = getRegistryInt(baseSystem, "LeyLinePlateCount", ctx.plateCount);
        ctx.blendCount = getRegistryInt(baseSystem, "LeyLineBlendCount", ctx.blendCount);
        ctx.sampleStep = getRegistryFloat(baseSystem, "LeyLineSampleStep", ctx.sampleStep);
        ctx.stressClamp = getRegistryFloat(baseSystem, "LeyLineStressClamp", ctx.stressClamp);
        ctx.upliftGain = getRegistryFloat(baseSystem, "LeyLineUpliftGain", ctx.upliftGain);
        ctx.upliftMax = getRegistryFloat(baseSystem, "LeyLineUpliftMax", ctx.upliftMax);

        sanitizeContext(ctx);
        if (!ctx.enabled) {
            ctx.loaded = false;
            baseSystem.world->leyLines = std::move(ctx);
            std::cout << "LeyLineSystem: disabled." << std::endl;
            return;
        }

        buildPlates(ctx);
        precomputeField(ctx);
        ctx.loaded = true;
        baseSystem.world->leyLines = std::move(ctx);

        const LeyLineContext& loaded = baseSystem.world->leyLines;
        if (loaded.precomputeField) {
            std::cout << "LeyLineSystem: loaded "
                      << loaded.plateCount
                      << " plates, field "
                      << loaded.fieldWidth
                      << "x"
                      << loaded.fieldHeight
                      << " (step "
                      << loaded.sampleStep
                      << ")."
                      << std::endl;
        } else {
            std::cout << "LeyLineSystem: loaded " << loaded.plateCount << " plates (on-demand sampling)." << std::endl;
        }
    }

    float SampleLeyStress(const WorldContext& worldCtx, float x, float z) {
        const LeyLineContext& ctx = worldCtx.leyLines;
        if (!ctx.enabled || !ctx.loaded) return 0.0f;

        if (ctx.precomputeField && !ctx.stressField.empty()) {
            return sampleBilinearField(ctx, ctx.stressField, x, z);
        }
        return computePrototypeSample(ctx, x, z).stress;
    }

    float SampleLeyUplift(const WorldContext& worldCtx, float x, float z) {
        const LeyLineContext& ctx = worldCtx.leyLines;
        if (!ctx.enabled || !ctx.loaded) return 0.0f;

        float rawUplift = 0.0f;
        if (ctx.precomputeField && !ctx.upliftField.empty()) {
            rawUplift = sampleBilinearField(ctx, ctx.upliftField, x, z);
        } else {
            rawUplift = computePrototypeSample(ctx, x, z).uplift;
        }
        return normalizeUplift(ctx, rawUplift);
    }
}
