#pragma once
#include "../Host.h"
#include "Host/PlatformInput.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace RenderInitSystemLogic {
    RenderBehavior BehaviorForPrototype(const Entity& proto);
    int getRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback);
    bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback);
    bool shouldRenderVoxelSection(const BaseSystem& baseSystem, const VoxelSection& section, const glm::vec3& cameraPos);
    bool shouldRenderVoxelSectionSized(const BaseSystem& baseSystem, int lod, const glm::ivec3& sectionCoord, int sectionSize, int sizeMultiplier, const glm::vec3& cameraPos);
    int FaceTileIndexFor(const WorldContext* worldCtx, const Entity& proto, int faceType);
}
namespace ExpanseBiomeSystemLogic {
    bool SampleTerrain(const WorldContext& worldCtx, float x, float z, float& outHeight);
    int ResolveBiome(const WorldContext& worldCtx, float x, float z);
}
namespace TerrainSystemLogic {
    bool IsColumnFullyReady(int lod, const glm::ivec2& columnCoord);
}
namespace FontSystemLogic {
    bool RasterizeBookTextBitmap(const BaseSystem& baseSystem,
                                 const std::string& fontName,
                                 float pixelHeight,
                                 const std::string& text,
                                 int maxCharsPerLine,
                                 int maxLines,
                                 std::vector<unsigned char>& outAlpha,
                                 int& outWidth,
                                 int& outHeight,
                                 int& outWrappedLineCount);
}
namespace BookSystemLogic {
    bool IsBookPrototypeName(const std::string& name);
    std::string ResolveBookPageText(int inspectPage);
}
namespace TreeGenerationSystemLogic {
    bool IsColumnFoliageReady(int lod, const glm::ivec2& columnCoord);
}

namespace MiniVoxelParticleSystemLogic {
    void SpawnFromBlock(BaseSystem& baseSystem,
                        const std::vector<Entity>& prototypes,
                        int worldIndex,
                        const glm::ivec3& cell,
                        int prototypeID,
                        const glm::vec3& color,
                        const glm::vec3& hitPosition,
                        const glm::vec3& hitNormal);

    void UpdateParticles(BaseSystem& baseSystem,
                         float dt,
                         std::array<std::vector<FaceInstanceRenderData>, 6>& faceInstances);
}

namespace WorldRenderSystemLogic {

    namespace {
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

        bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            return fallback;
        }

        std::string getRegistryString(const BaseSystem& baseSystem, const std::string& key, const std::string& fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            const std::string& value = std::get<std::string>(it->second);
            if (value.empty()) return fallback;
            return value;
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

        glm::vec3 resolveColor(const WorldContext& world,
                               const std::string& colorName,
                               const glm::vec3& fallback) {
            auto it = world.colorLibrary.find(colorName);
            if (it == world.colorLibrary.end()) return fallback;
            return it->second;
        }

        unsigned char toByte(float v) {
            float clamped = glm::clamp(v, 0.0f, 1.0f);
            return static_cast<unsigned char>(std::round(clamped * 255.0f));
        }

        void setPixelRgb(std::vector<unsigned char>& pixels,
                         int width,
                         int x,
                         int y,
                         const glm::vec3& color) {
            const size_t idx = static_cast<size_t>((y * width + x) * 3);
            if (idx + 2 >= pixels.size()) return;
            pixels[idx + 0] = toByte(color.r);
            pixels[idx + 1] = toByte(color.g);
            pixels[idx + 2] = toByte(color.b);
        }

        bool isMapFoliagePrototypeName(const std::string& name) {
            return name.rfind("GrassTuft", 0) == 0
                || name.rfind("Flower", 0) == 0
                || name.rfind("Leaf", 0) == 0
                || name.rfind("Cactus", 0) == 0
                || name.find("Kelp") != std::string::npos
                || name.find("SeaUrchin") != std::string::npos
                || name.find("Lilypad") != std::string::npos
                || name.find("Reed") != std::string::npos;
        }

        void uploadMapTexture(RenderHandle& ioTexture,
                              int& ioWidth,
                              int& ioHeight,
                              const BaseSystem& baseSystem,
                              int width,
                              int height,
                              const std::vector<unsigned char>& pixels) {
            if (width <= 0 || height <= 0 || pixels.empty()) return;
            if (!baseSystem.renderBackend) return;

            bool uploaded = false;
            const bool canUpdateInPlace = ioTexture != 0 && ioWidth == width && ioHeight == height;
            if (canUpdateInPlace) {
                std::vector<unsigned char> rgbaPixels;
                rgbaPixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 255u);
                const int pixelCount = width * height;
                for (int i = 0; i < pixelCount; ++i) {
                    const size_t src = static_cast<size_t>(i) * 3u;
                    const size_t dst = static_cast<size_t>(i) * 4u;
                    rgbaPixels[dst + 0] = pixels[src + 0];
                    rgbaPixels[dst + 1] = pixels[src + 1];
                    rgbaPixels[dst + 2] = pixels[src + 2];
                }
                uploaded = baseSystem.renderBackend->uploadRgbaTextureSubImage2D(
                    ioTexture,
                    0,
                    0,
                    width,
                    height,
                    rgbaPixels
                );
            }
            if (!uploaded) {
                uploaded = baseSystem.renderBackend->uploadRgbTexture2D(ioTexture, width, height, pixels);
            }
            if (uploaded) {
                ioWidth = width;
                ioHeight = height;
            }
        }

        void uploadTopDownMapTexture(RendererContext& renderer,
                                     const BaseSystem& baseSystem,
                                     int width,
                                     int height,
                                     const std::vector<unsigned char>& pixels) {
            uploadMapTexture(
                renderer.topDownMapTexture,
                renderer.topDownMapTextureWidth,
                renderer.topDownMapTextureHeight,
                baseSystem,
                width,
                height,
                pixels
            );
        }

        struct TopDownMapDomain {
            float centerX = 0.0f;
            float centerZ = 0.0f;
            float halfExtent = 1.0f;
        };

        TopDownMapDomain resolveTopDownMapDomain(const BaseSystem& baseSystem,
                                                 const WorldContext& world,
                                                 const PlayerContext& player) {
            const ExpanseConfig& cfg = world.expanse;
            TopDownMapDomain domain{};
            domain.centerX = cfg.islandCenterX;
            domain.centerZ = cfg.islandCenterZ;
            if (cfg.islandRadius <= 0.0f) {
                if (player.isometricMapOriginInitialized) {
                    domain.centerX = player.isometricMapOrigin.x;
                    domain.centerZ = player.isometricMapOrigin.z;
                } else {
                    domain.centerX = player.cameraPosition.x;
                    domain.centerZ = player.cameraPosition.z;
                }
            }

            const float islandMargin = glm::max(0.0f, getRegistryFloat(baseSystem, "TopDownMapIslandMargin", 260.0f));
            domain.halfExtent = getRegistryFloat(baseSystem, "TopDownMapHalfExtent", 0.0f);
            if (domain.halfExtent <= 0.0f) {
                if (cfg.islandRadius > 0.0f) {
                    domain.halfExtent = cfg.islandRadius + islandMargin;
                } else {
                    const float captureDiameter = glm::max(1.0f, getRegistryFloat(baseSystem, "IsometricMapCaptureDiameter", 5000.0f));
                    domain.halfExtent = 0.5f * captureDiameter;
                }
            }
            domain.halfExtent = std::max(1.0f, domain.halfExtent);
            return domain;
        }

        TopDownMapDomain resolvePrismalMapDomain(const BaseSystem& baseSystem,
                                                 const WorldContext& world,
                                                 const PlayerContext& player,
                                                 bool followPlayer,
                                                 float prismalZoom) {
            TopDownMapDomain domain = resolveTopDownMapDomain(baseSystem, world, player);
            const float zoom = glm::max(0.0001f, prismalZoom);
            const float baseHalfExtent = glm::max(1.0f, getRegistryFloat(baseSystem, "PrismalMapBaseHalfExtent", 96.0f));
            const float minHalfExtent = glm::max(1.0f, getRegistryFloat(baseSystem, "PrismalMapHalfExtentMin", 24.0f));
            const float maxHalfExtent = glm::max(
                minHalfExtent,
                getRegistryFloat(baseSystem, "PrismalMapHalfExtentMax", domain.halfExtent)
            );
            domain.halfExtent = glm::clamp(baseHalfExtent / zoom, minHalfExtent, maxHalfExtent);
            if (followPlayer) {
                domain.centerX = player.cameraPosition.x;
                domain.centerZ = player.cameraPosition.z;
            }
            return domain;
        }

        bool buildTopDownMapPixels(const BaseSystem& baseSystem,
                                   const WorldContext& world,
                                   const std::vector<Entity>& prototypes,
                                   const TopDownMapDomain& domain,
                                   int mapSize,
                                   std::vector<unsigned char>& outPixels) {
            if (!world.expanse.loaded) return false;
            if (mapSize <= 0) return false;

            const ExpanseConfig& cfg = world.expanse;
            const float centerX = domain.centerX;
            const float centerZ = domain.centerZ;
            const float halfExtent = std::max(1.0f, domain.halfExtent);
            const glm::vec3 grassColor = glm::mix(
                resolveColor(world, cfg.colorGrass, glm::vec3(0.20f, 0.62f, 0.24f)),
                glm::vec3(0.19f, 0.66f, 0.32f),
                0.45f
            );
            const glm::vec3 meadowColor = glm::mix(grassColor, glm::vec3(0.47f, 0.88f, 0.33f), 0.42f);
            const glm::vec3 jungleColor = glm::mix(
                resolveColor(world, cfg.colorLeaf, glm::vec3(0.18f, 0.45f, 0.19f)),
                glm::vec3(0.15f, 0.52f, 0.24f),
                0.35f
            );
            const glm::vec3 coniferColor = glm::mix(grassColor, jungleColor, 0.30f);
            const glm::vec3 sandColor = glm::mix(
                resolveColor(world, cfg.colorSand, glm::vec3(0.88f, 0.75f, 0.48f)),
                glm::vec3(0.93f, 0.79f, 0.69f),
                0.55f
            );
            const glm::vec3 waterColor = glm::mix(
                resolveColor(world, cfg.colorWater, glm::vec3(0.10f, 0.45f, 0.82f)),
                glm::vec3(0.0f, 0.5f, 0.5f),
                0.70f
            );
            const glm::vec3 deepWaterColor = resolveColor(world, "DeepWater", waterColor * 0.65f);
            const float depthRange = std::max(1.0f, cfg.waterSurface - cfg.waterFloor);
            const float worldMinX = centerX - halfExtent;
            const float worldMinZ = centerZ - halfExtent;
            const float worldMaxX = centerX + halfExtent;
            const float worldMaxZ = centerZ + halfExtent;
            const float worldSpan = halfExtent * 2.0f;
            const float pixelsPerWorldUnit = static_cast<float>(mapSize) / worldSpan;
            // Match temp-map behavior: retain per-block detail when zoomed in,
            // then scale cell size only when zoomed far out to keep work bounded.
            const float detailCellWorldSize = std::max(1.0f, worldSpan / static_cast<float>(mapSize));
            const int cellMinX = static_cast<int>(std::floor(worldMinX / detailCellWorldSize));
            const int cellMaxX = static_cast<int>(std::ceil(worldMaxX / detailCellWorldSize)) - 1;
            const int cellMinZ = static_cast<int>(std::floor(worldMinZ / detailCellWorldSize));
            const int cellMaxZ = static_cast<int>(std::ceil(worldMaxZ / detailCellWorldSize)) - 1;
            int waterPrototypeID = -1;
            std::unordered_set<int> foliagePrototypeIDs;
            foliagePrototypeIDs.reserve(64);
            for (const Entity& proto : prototypes) {
                if (proto.name == "Water") {
                    waterPrototypeID = proto.prototypeID;
                }
                if (isMapFoliagePrototypeName(proto.name)) {
                    foliagePrototypeIDs.insert(proto.prototypeID);
                }
            }
            const VoxelWorldContext* voxelWorld = baseSystem.voxelWorld.get();
            const bool voxelOverlayEnabled = voxelWorld
                && voxelWorld->enabled
                && !voxelWorld->sections.empty()
                && (waterPrototypeID >= 0 || !foliagePrototypeIDs.empty())
                && detailCellWorldSize <= glm::max(1.0f, getRegistryFloat(baseSystem, "MapVoxelOverlayMaxCellWorldSize", 8.0f));
            const int mapWaterBaseY = static_cast<int>(std::floor(cfg.waterSurface));

            outPixels.assign(static_cast<size_t>(mapSize) * static_cast<size_t>(mapSize) * 3u, 0u);
            for (int cellZ = cellMinZ; cellZ <= cellMaxZ; ++cellZ) {
                const float cellWorldMinZ = static_cast<float>(cellZ) * detailCellWorldSize;
                const float cellWorldMaxZ = cellWorldMinZ + detailCellWorldSize;
                int py0 = static_cast<int>(std::floor((cellWorldMinZ - worldMinZ) * pixelsPerWorldUnit));
                int py1 = static_cast<int>(std::ceil((cellWorldMaxZ - worldMinZ) * pixelsPerWorldUnit));
                py0 = std::clamp(py0, 0, mapSize);
                py1 = std::clamp(py1, 0, mapSize);
                if (py1 <= py0) py1 = std::min(mapSize, py0 + 1);

                for (int cellX = cellMinX; cellX <= cellMaxX; ++cellX) {
                    const float cellWorldMinX = static_cast<float>(cellX) * detailCellWorldSize;
                    const float cellWorldMaxX = cellWorldMinX + detailCellWorldSize;
                    int px0 = static_cast<int>(std::floor((cellWorldMinX - worldMinX) * pixelsPerWorldUnit));
                    int px1 = static_cast<int>(std::ceil((cellWorldMaxX - worldMinX) * pixelsPerWorldUnit));
                    px0 = std::clamp(px0, 0, mapSize);
                    px1 = std::clamp(px1, 0, mapSize);
                    if (px1 <= px0) px1 = std::min(mapSize, px0 + 1);

                    const float sampleX = cellWorldMinX + detailCellWorldSize * 0.5f;
                    const float sampleZ = cellWorldMinZ + detailCellWorldSize * 0.5f;
                    float terrainHeight = cfg.waterFloor;
                    const bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(world, sampleX, sampleZ, terrainHeight);
                    const int biome = ExpanseBiomeSystemLogic::ResolveBiome(world, sampleX, sampleZ);

                    glm::vec3 color = waterColor;
                    if (!isLand) {
                        const float depthNorm = glm::clamp((cfg.waterSurface - terrainHeight) / depthRange, 0.0f, 1.0f);
                        color = glm::mix(waterColor, deepWaterColor, depthNorm * 0.65f);
                    } else if (terrainHeight <= cfg.waterSurface + cfg.beachHeight + 1.0f) {
                        color = sandColor;
                    } else {
                        switch (biome) {
                            case 2: color = sandColor; break;                     // desert
                            case 3: color = jungleColor; break;                   // jungle
                            case 4: color = glm::vec3(0.47f, 0.44f, 0.38f); break; // winter bare forest
                            case 1: color = meadowColor; break;                   // meadow
                            default: color = coniferColor; break;                 // conifer
                        }
                    }

                    if (voxelOverlayEnabled) {
                        const int wx = static_cast<int>(std::floor(sampleX));
                        const int wz = static_cast<int>(std::floor(sampleZ));
                        const int surfaceY = static_cast<int>(std::floor(terrainHeight)) + 1;

                        if (waterPrototypeID >= 0) {
                            int topWaterY = std::numeric_limits<int>::min();
                            for (int y = mapWaterBaseY + 10; y >= mapWaterBaseY - 6; --y) {
                                if (voxelWorld->getBlockWorld(glm::ivec3(wx, y, wz)) == static_cast<uint32_t>(waterPrototypeID)) {
                                    topWaterY = y;
                                    break;
                                }
                            }
                            if (topWaterY != std::numeric_limits<int>::min()) {
                                const float elev = static_cast<float>(topWaterY) - cfg.waterSurface;
                                const float elevNorm = glm::clamp(elev / 10.0f, -1.0f, 1.0f);
                                if (elevNorm > 0.0f) {
                                    color = glm::mix(color, glm::vec3(0.55f, 0.90f, 0.98f), elevNorm * 0.40f);
                                } else {
                                    color = glm::mix(color, deepWaterColor, -elevNorm * 0.35f);
                                }
                            }
                        }

                        if (isLand && !foliagePrototypeIDs.empty()) {
                            bool hasFoliage = false;
                            for (int y = surfaceY + 1; y <= surfaceY + 5; ++y) {
                                const uint32_t id = voxelWorld->getBlockWorld(glm::ivec3(wx, y, wz));
                                if (id == 0u || id == static_cast<uint32_t>(waterPrototypeID)) continue;
                                if (foliagePrototypeIDs.count(static_cast<int>(id)) > 0) {
                                    hasFoliage = true;
                                    break;
                                }
                            }
                            if (hasFoliage) {
                                color = glm::mix(color, glm::vec3(0.16f, 0.74f, 0.29f), 0.45f);
                            }
                        }
                    }

                    for (int py = py0; py < py1; ++py) {
                        for (int px = px0; px < px1; ++px) {
                            setPixelRgb(outPixels, mapSize, px, (mapSize - 1 - py), color);
                        }
                    }
                }
            }
            return true;
        }

    }

        bool buildPrismalMapTexture(const BaseSystem& baseSystem,
                                    const WorldContext& world,
                                    const std::vector<Entity>& prototypes,
                                    const TopDownMapDomain& domain,
                                    RendererContext& renderer,
                                    double nowSeconds) {
            if (!world.expanse.loaded) return false;
            const int requestedMapSize = std::clamp(
                getRegistryInt(baseSystem, "PrismalMapTextureSize", 768),
                128,
                2048
            );
            const int perfCapSize = std::clamp(
                getRegistryInt(baseSystem, "PrismalMapTextureSizePerfCap", 640),
                128,
                2048
            );
            const int mapSize = std::min(requestedMapSize, perfCapSize);
            const double rebuildIntervalSeconds = glm::clamp(
                static_cast<double>(getRegistryFloat(baseSystem, "PrismalMapRebuildIntervalSeconds", 0.10f)),
                0.0,
                2.0
            );
            const double rebuildMaxAgeSeconds = std::max(
                0.0,
                static_cast<double>(getRegistryFloat(baseSystem, "PrismalMapRebuildMaxAgeSeconds", 1.0f))
            );
            const float moveThreshold = glm::max(0.01f, getRegistryFloat(baseSystem, "PrismalMapRebuildMoveThreshold", 6.0f));
            const float extentThreshold = glm::max(0.01f, getRegistryFloat(baseSystem, "PrismalMapRebuildExtentThreshold", 0.5f));
            const double slowBuildIntervalScale = glm::clamp(
                static_cast<double>(getRegistryFloat(baseSystem, "PrismalMapSlowBuildIntervalScale", 6.0f)),
                1.0,
                60.0
            );
            const double maxAgeBuildCostLimitSeconds = glm::clamp(
                static_cast<double>(getRegistryFloat(baseSystem, "PrismalMapMaxAgeRefreshBuildCostSeconds", 0.050f)),
                0.0,
                10.0
            );
            const bool textureReady = renderer.prismalMapTexture != 0
                && renderer.prismalMapTextureWidth == mapSize
                && renderer.prismalMapTextureHeight == mapSize;

            bool needBuild = false;
            if (!textureReady) {
                needBuild = true;
            } else {
                const glm::vec2 centerXz(domain.centerX, domain.centerZ);
                const float movedDist = glm::distance(centerXz, renderer.prismalMapLastCenterXz);
                const float extentDelta = std::fabs(domain.halfExtent - renderer.prismalMapLastHalfExtent);
                const double elapsed = nowSeconds - renderer.prismalMapLastBuildTime;
                const bool changed = movedDist >= moveThreshold || extentDelta >= extentThreshold;
                const double adaptiveIntervalSeconds = std::max(
                    rebuildIntervalSeconds,
                    renderer.prismalMapLastBuildDuration * slowBuildIntervalScale
                );
                const bool allowAgeRefresh = rebuildMaxAgeSeconds > 0.0
                    && (renderer.prismalMapLastBuildDuration <= 0.0
                        || renderer.prismalMapLastBuildDuration <= maxAgeBuildCostLimitSeconds);
                if ((changed && elapsed >= adaptiveIntervalSeconds)
                    || (allowAgeRefresh && elapsed >= rebuildMaxAgeSeconds)) {
                    needBuild = true;
                }
            }
            if (!needBuild) return renderer.prismalMapTexture != 0;

            const double buildStartSeconds = PlatformInput::GetTimeSeconds();
            std::vector<unsigned char> pixels;
            if (!buildTopDownMapPixels(baseSystem, world, prototypes, domain, mapSize, pixels)) return false;
            uploadMapTexture(
                renderer.prismalMapTexture,
                renderer.prismalMapTextureWidth,
                renderer.prismalMapTextureHeight,
                baseSystem,
                mapSize,
                mapSize,
                pixels
            );
            const double buildEndSeconds = PlatformInput::GetTimeSeconds();
            renderer.prismalMapLastCenterXz = glm::vec2(domain.centerX, domain.centerZ);
            renderer.prismalMapLastHalfExtent = domain.halfExtent;
            renderer.prismalMapLastBuildTime = buildEndSeconds;
            renderer.prismalMapLastBuildDuration = std::max(0.0, buildEndSeconds - buildStartSeconds);
            return renderer.prismalMapTexture != 0;
        }

        bool buildTopDownMapTexture(const BaseSystem& baseSystem,
                                    const WorldContext& world,
                                    const std::vector<Entity>& prototypes,
                                    const PlayerContext& player,
                                    RendererContext& renderer) {
            if (!world.expanse.loaded) return false;

            const int mapSize = std::clamp(
                getRegistryInt(baseSystem, "TopDownMapTextureSize", 1024),
                128,
                4096
            );
            if (renderer.topDownMapTexture != 0
                && renderer.topDownMapTextureWidth == mapSize
                && renderer.topDownMapTextureHeight == mapSize) {
                return true;
            }

            const TopDownMapDomain domain = resolveTopDownMapDomain(baseSystem, world, player);
            std::vector<unsigned char> pixels;
            if (!buildTopDownMapPixels(baseSystem, world, prototypes, domain, mapSize, pixels)) return false;

            uploadTopDownMapTexture(renderer, baseSystem, mapSize, mapSize, pixels);
            std::cout << "[Map] built 2D terrain map " << mapSize << "x" << mapSize
                      << " (extent " << (domain.halfExtent * 2.0f) << " blocks)" << std::endl;
            return renderer.topDownMapTexture != 0;
        }

        void renderTopDownMapOverlay(const BaseSystem& baseSystem,
                                     RendererContext& renderer,
                                     RenderHandle mapTexture,
                                     float localMapZoom,
                                     glm::vec2 localMapCenter,
                                     int framebufferWidth,
                                     int framebufferHeight,
                                     const glm::ivec4* scissorRectPixels) {
            if (mapTexture == 0 || !renderer.godrayCompositeShader || renderer.godrayQuadVAO == 0) return;
            if (!baseSystem.renderBackend) return;
            IRenderBackend& renderBackend = *baseSystem.renderBackend;

            bool useScissor = false;
            int sx = 0;
            int sy = 0;
            int sw = 0;
            int sh = 0;
            float mapZoom = glm::max(0.0001f, localMapZoom);
            glm::vec2 mapCenter = localMapCenter;
            if (scissorRectPixels && framebufferWidth > 0 && framebufferHeight > 0) {
                sx = std::max(0, scissorRectPixels->x);
                sy = std::max(0, scissorRectPixels->y);
                sw = std::max(0, scissorRectPixels->z);
                sh = std::max(0, scissorRectPixels->w);
                sw = std::min(sw, std::max(0, framebufferWidth - sx));
                sh = std::min(sh, std::max(0, framebufferHeight - sy));
                if (sw > 0 && sh > 0) {
                    useScissor = true;
                    const float rectScaleX = static_cast<float>(sw) / static_cast<float>(framebufferWidth);
                    const float rectScaleY = static_cast<float>(sh) / static_cast<float>(framebufferHeight);
                    const float rectScale = 0.5f * (rectScaleX + rectScaleY);
                    mapZoom = glm::max(0.0001f, mapZoom * rectScale);
                    const glm::vec2 rectCenter(
                        (static_cast<float>(sx) + static_cast<float>(sw) * 0.5f) / static_cast<float>(framebufferWidth),
                        (static_cast<float>(sy) + static_cast<float>(sh) * 0.5f) / static_cast<float>(framebufferHeight)
                    );
                    mapCenter -= (rectCenter - glm::vec2(0.5f, 0.5f)) / mapZoom;
                }
            }

            auto setDepthTestEnabled = [&](bool enabled) { renderBackend.setDepthTestEnabled(enabled); };
            auto setDepthWriteEnabled = [&](bool enabled) { renderBackend.setDepthWriteEnabled(enabled); };
            auto setBlendEnabled = [&](bool enabled) { renderBackend.setBlendEnabled(enabled); };

            setDepthTestEnabled(false);
            setDepthWriteEnabled(false);
            setBlendEnabled(false);
            if (useScissor) {
                renderBackend.setScissorEnabled(true);
                renderBackend.setScissorRect(sx, sy, sw, sh);
            }
            renderBackend.bindVertexArray(renderer.godrayQuadVAO);
            renderer.godrayCompositeShader->use();
            renderer.godrayCompositeShader->setInt("godrayTex", 0);
            renderer.godrayCompositeShader->setFloat("mapZoom", mapZoom);
            renderer.godrayCompositeShader->setVec2("mapCenter", mapCenter);
            renderBackend.bindTexture2D(mapTexture, 0);
            renderBackend.drawArraysTriangles(0, 6);
            if (useScissor) {
                renderBackend.setScissorEnabled(false);
            }
            setDepthWriteEnabled(true);
            setDepthTestEnabled(true);
        }

        void uploadCachedMapTexture(RendererContext& renderer,
                                    const BaseSystem& baseSystem,
                                    int width,
                                    int height,
                                    const std::vector<unsigned char>& pixels) {
            if (width <= 0 || height <= 0 || pixels.empty()) return;
            if (baseSystem.renderBackend
                && baseSystem.renderBackend->uploadRgbTexture2D(renderer.isometricMapCachedTexture, width, height, pixels)) {
                renderer.isometricMapCachedWidth = width;
                renderer.isometricMapCachedHeight = height;
            }
        }

        bool captureFramebufferToPpm(const BaseSystem& baseSystem,
                                     PlatformWindowHandle win,
                                     int captureIndex,
                                     std::string& outPath,
                                     RendererContext* rendererCacheTarget) {
            if (!win) return false;
            int width = 0;
            int height = 0;
            std::vector<unsigned char> pixels;
            if (!baseSystem.renderBackend) return false;
            const bool readbackOk = baseSystem.renderBackend->readDefaultFramebufferRgb(win, pixels, width, height);
            if (!readbackOk) return false;
            if (rendererCacheTarget) uploadCachedMapTexture(*rendererCacheTarget, baseSystem, width, height, pixels);

            const std::string captureDir = getRegistryString(baseSystem, "IsometricMapCaptureDir", "Procedures/captures");
            const std::string capturePrefix = getRegistryString(baseSystem, "IsometricMapCapturePrefix", "isometric_map");
            try {
                std::filesystem::create_directories(captureDir);
            } catch (...) {
                return false;
            }

            std::ostringstream fileName;
            fileName << capturePrefix << "_" << std::setw(5) << std::setfill('0') << captureIndex << ".ppm";
            const std::filesystem::path capturePath = std::filesystem::path(captureDir) / fileName.str();
            std::ofstream out(capturePath, std::ios::binary);
            if (!out.is_open()) return false;

            out << "P6\n" << width << " " << height << "\n255\n";
            const size_t rowBytes = static_cast<size_t>(width) * 3u;
            for (int y = height - 1; y >= 0; --y) {
                const unsigned char* row = pixels.data() + static_cast<size_t>(y) * rowBytes;
                out.write(reinterpret_cast<const char*>(row), static_cast<std::streamsize>(rowBytes));
            }
            out.close();
            if (!out.good()) return false;
            outPath = capturePath.string();
            return true;
        }

        int resolveLeafPrototypeID(const std::vector<Entity>& prototypes) {
            for (const auto& proto : prototypes) {
                if (proto.name == "Leaf") return proto.prototypeID;
            }
            return -1;
        }

        bool cameraInLeaves(const VoxelWorldContext& voxelWorld, int leafPrototypeID, const glm::vec3& cameraPosition) {
            if (!voxelWorld.enabled || leafPrototypeID < 0) return false;
            static const glm::vec3 kOffsets[] = {
                glm::vec3(0.0f,  0.0f,  0.0f),
                glm::vec3(0.0f,  0.9f,  0.0f),
                glm::vec3(0.0f, -0.9f,  0.0f),
                glm::vec3(0.34f, 0.2f,  0.0f),
                glm::vec3(-0.34f,0.2f,  0.0f),
                glm::vec3(0.0f,  0.2f,  0.34f),
                glm::vec3(0.0f,  0.2f, -0.34f),
                glm::vec3(0.34f, 0.9f,  0.0f),
                glm::vec3(-0.34f,0.9f,  0.0f),
                glm::vec3(0.0f,  0.9f,  0.34f),
                glm::vec3(0.0f,  0.9f, -0.34f)
            };
            for (const glm::vec3& offset : kOffsets) {
                glm::ivec3 cell(
                    static_cast<int>(std::floor(cameraPosition.x + offset.x)),
                    static_cast<int>(std::floor(cameraPosition.y + offset.y)),
                    static_cast<int>(std::floor(cameraPosition.z + offset.z))
                );
                if (voxelWorld.getBlockWorld(cell) == static_cast<uint32_t>(leafPrototypeID)) return true;
            }
            return false;
        }

        bool isStonePebbleXName(const std::string& name) {
            if (name == "StonePebbleCavePotTexX") return false;
            return name == "StonePebbleTexX"
                || (name.rfind("StonePebble", 0) == 0 && name.size() >= 4 && name.compare(name.size() - 4, 4, "TexX") == 0);
        }

        bool isStonePebbleZName(const std::string& name) {
            if (name == "StonePebbleCavePotTexZ") return false;
            return name == "StonePebbleTexZ"
                || (name.rfind("StonePebble", 0) == 0 && name.size() >= 4 && name.compare(name.size() - 4, 4, "TexZ") == 0);
        }

        bool isGrassCoverXName(const std::string& name) {
            return name == "GrassCoverTexX"
                || (name.rfind("GrassCover", 0) == 0 && name.size() >= 4 && name.compare(name.size() - 4, 4, "TexX") == 0);
        }

        bool isGrassCoverZName(const std::string& name) {
            return name == "GrassCoverTexZ"
                || (name.rfind("GrassCover", 0) == 0 && name.size() >= 4 && name.compare(name.size() - 4, 4, "TexZ") == 0);
        }

        bool isPetalPileName(const std::string& name) {
            if (BookSystemLogic::IsBookPrototypeName(name)) return false;
            return name.rfind("StonePebblePetals", 0) == 0;
        }

        constexpr int kSurfaceStonePileMin = 1;
        constexpr int kSurfaceStonePileMax = 8;

        struct NarrowHalfExtents {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        struct StonePebblePilePieces {
            int count = 0;
            std::array<glm::vec2, kSurfaceStonePileMax> offsets{};
            std::array<NarrowHalfExtents, kSurfaceStonePileMax> halfExtents{};
        };

        struct GrassCoverDots {
            int count = 0;
            std::array<glm::vec2, 48> offsets{};
        };

        uint32_t hashCell3D(int x, int y, int z) {
            uint32_t h = static_cast<uint32_t>(x) * 73856093u;
            h ^= static_cast<uint32_t>(y) * 19349663u;
            h ^= static_cast<uint32_t>(z) * 83492791u;
            h ^= (h >> 13);
            h *= 1274126177u;
            h ^= (h >> 16);
            return h;
        }

        int decodeSurfaceStonePileCount(uint32_t packedColor) {
            const int encoded = static_cast<int>((packedColor >> 24) & 0xffu);
            if (encoded <= 0) return kSurfaceStonePileMin;
            return std::clamp(encoded, kSurfaceStonePileMin, kSurfaceStonePileMax);
        }

        StonePebblePilePieces stonePebblePilePiecesForCell(const glm::ivec3& cell, int requestedCount) {
            StonePebblePilePieces out;
            out.count = std::clamp(requestedCount, kSurfaceStonePileMin, kSurfaceStonePileMax);
            int placed = 0;
            constexpr float kPlacementPad = 1.0f / 96.0f;
            for (int i = 0; i < out.count; ++i) {
                const uint32_t sizeHash = hashCell3D(
                    cell.x + i * 83,
                    cell.y - i * 47,
                    cell.z + i * 59
                );
                NarrowHalfExtents ext;
                ext.x = (2.0f + static_cast<float>(sizeHash & 3u)) / 48.0f;
                ext.z = (2.0f + static_cast<float>((sizeHash >> 2u) & 3u)) / 48.0f;
                ext.y = (1.0f + static_cast<float>((sizeHash >> 4u) % 3u)) / 48.0f;
                if ((sizeHash >> 6u) & 1u) {
                    std::swap(ext.x, ext.z);
                }
                if (out.count == 1) {
                    ext.x *= 1.35f;
                    ext.z *= 1.35f;
                    ext.y *= 1.20f;
                }

                bool placedThis = false;
                for (int attempt = 0; attempt < 12; ++attempt) {
                    const uint32_t h = hashCell3D(
                        cell.x + i * 37 + attempt * 11,
                        cell.y + i * 19 - attempt * 7,
                        cell.z - i * 53 + attempt * 13
                    );
                    float ox = (static_cast<float>((h >> 8u) & 0xffu) / 255.0f - 0.5f) * 0.72f;
                    float oz = (static_cast<float>((h >> 16u) & 0xffu) / 255.0f - 0.5f) * 0.72f;
                    ox = std::clamp(ox, -0.5f + ext.x + kPlacementPad, 0.5f - ext.x - kPlacementPad);
                    oz = std::clamp(oz, -0.5f + ext.z + kPlacementPad, 0.5f - ext.z - kPlacementPad);

                    bool overlaps = false;
                    for (int j = 0; j < placed; ++j) {
                        const glm::vec2 prevOffset = out.offsets[static_cast<size_t>(j)];
                        const NarrowHalfExtents prevExt = out.halfExtents[static_cast<size_t>(j)];
                        if (std::abs(ox - prevOffset.x) < (ext.x + prevExt.x + kPlacementPad)
                            && std::abs(oz - prevOffset.y) < (ext.z + prevExt.z + kPlacementPad)) {
                            overlaps = true;
                            break;
                        }
                    }
                    if (overlaps) continue;

                    out.offsets[static_cast<size_t>(placed)] = glm::vec2(ox, oz);
                    out.halfExtents[static_cast<size_t>(placed)] = ext;
                    ++placed;
                    placedThis = true;
                    break;
                }

                if (!placedThis) {
                    const uint32_t h = hashCell3D(
                        cell.x - i * 71,
                        cell.y + i * 43,
                        cell.z + i * 29
                    );
                    const float angle = (static_cast<float>(h & 1023u) / 1023.0f) * 6.2831853f + static_cast<float>(i) * 0.71f;
                    const float radius = 0.09f + 0.03f * static_cast<float>(i % 4);
                    float ox = std::cos(angle) * radius;
                    float oz = std::sin(angle) * radius;
                    ox = std::clamp(ox, -0.5f + ext.x + kPlacementPad, 0.5f - ext.x - kPlacementPad);
                    oz = std::clamp(oz, -0.5f + ext.z + kPlacementPad, 0.5f - ext.z - kPlacementPad);
                    out.offsets[static_cast<size_t>(placed)] = glm::vec2(ox, oz);
                    out.halfExtents[static_cast<size_t>(placed)] = ext;
                    ++placed;
                }
            }

            out.count = std::max(placed, 1);
            return out;
        }

        GrassCoverDots grassCoverDotsForCell(const glm::ivec3& cell) {
            GrassCoverDots out;
            constexpr int kMinDots = 36;
            constexpr int kMaxDots = 48;
            constexpr int kGridCellsPerAxis = 24;
            constexpr float kGridUnit = 1.0f / 24.0f;
            const uint32_t seed = hashCell3D(cell.x + 913, cell.y + 37, cell.z - 211);
            out.count = kMinDots + static_cast<int>(seed % static_cast<uint32_t>(kMaxDots - kMinDots + 1));
            for (int i = 0; i < out.count; ++i) {
                const uint32_t h = hashCell3D(
                    cell.x + i * 31,
                    cell.y - i * 17,
                    cell.z + i * 13
                );
                const int oxSlot = static_cast<int>(h % static_cast<uint32_t>(kGridCellsPerAxis));
                const int ozSlot = static_cast<int>((h >> 8u) % static_cast<uint32_t>(kGridCellsPerAxis));
                const float ox = (static_cast<float>(oxSlot) + 0.5f) * kGridUnit - 0.5f;
                const float oz = (static_cast<float>(ozSlot) + 0.5f) * kGridUnit - 0.5f;
                out.offsets[static_cast<size_t>(i)] = glm::vec2(ox, oz);
            }
            return out;
        }

        enum class DebugSlopeDir : int { PosX = 0, NegX = 1, PosZ = 2, NegZ = 3 };

        bool tryParseDebugSlopeDir(const std::string& name, DebugSlopeDir& outDir) {
            if (name == "DebugSlopeTexPosX") { outDir = DebugSlopeDir::PosX; return true; }
            if (name == "DebugSlopeTexNegX") { outDir = DebugSlopeDir::NegX; return true; }
            if (name == "DebugSlopeTexPosZ") { outDir = DebugSlopeDir::PosZ; return true; }
            if (name == "DebugSlopeTexNegZ") { outDir = DebugSlopeDir::NegZ; return true; }
            return false;
        }

    
    void RenderWorld(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.player || !baseSystem.level || !baseSystem.renderBackend) return;
        IRenderBackend& renderBackend = *baseSystem.renderBackend;
        renderBackend.clearDefaultFramebuffer(0.0f, 0.0f, 0.0f, 1.0f, true);
        PlayerContext& player = *baseSystem.player;
        WorldContext& world = *baseSystem.world;
        RendererContext& renderer = *baseSystem.renderer;
        LevelContext& level = *baseSystem.level;

        float time = static_cast<float>(PlatformInput::GetTimeSeconds());
        const bool isometricMapSystemEnabled = getRegistryBool(baseSystem, "IsometricMapSystem", true);
        const bool isometricMapActive = isometricMapSystemEnabled && player.isometricMapMode;
        const bool topDownMapActive = isometricMapSystemEnabled && player.topDownMapMode;
        const int prismalMapMode = glm::clamp(player.prismalMapMode, 0, 2);
        const bool prismalMapMinimapActive = isometricMapSystemEnabled && prismalMapMode == 1;
        const bool prismalMapFullscreenActive = isometricMapSystemEnabled && prismalMapMode == 2;
        if (topDownMapActive) {
            if (buildTopDownMapTexture(baseSystem, world, prototypes, player, renderer)) {
                int framebufferWidth = 0;
                int framebufferHeight = 0;
                if (win) {
                    renderBackend.getFramebufferSize(win, framebufferWidth, framebufferHeight);
                }
                const float centerX = glm::clamp(getRegistryFloat(baseSystem, "TopDownMapCenterX", 0.5f), 0.0f, 1.0f);
                const float centerY = glm::clamp(getRegistryFloat(baseSystem, "TopDownMapCenterY", 0.5f), 0.0f, 1.0f);
                renderTopDownMapOverlay(
                    baseSystem,
                    renderer,
                    renderer.topDownMapTexture,
                    glm::max(0.0001f, player.topDownMapZoom),
                    glm::vec2(centerX, centerY),
                    framebufferWidth,
                    framebufferHeight,
                    nullptr
                );
            }
            return;
        }
        if (prismalMapFullscreenActive) {
            int framebufferWidth = 0;
            int framebufferHeight = 0;
            if (win) {
                renderBackend.getFramebufferSize(win, framebufferWidth, framebufferHeight);
            }
            const bool followPlayer = getRegistryBool(baseSystem, "PrismalMapFollowPlayer", true);
            const TopDownMapDomain prismalDomain = resolvePrismalMapDomain(
                baseSystem,
                world,
                player,
                followPlayer,
                player.prismalMapZoom
            );
            if (buildPrismalMapTexture(baseSystem, world, prototypes, prismalDomain, renderer, PlatformInput::GetTimeSeconds())) {
                const float centerX = glm::clamp(getRegistryFloat(baseSystem, "PrismalMapCenterX", 0.5f), 0.0f, 1.0f);
                const float centerY = glm::clamp(getRegistryFloat(baseSystem, "PrismalMapCenterY", 0.5f), 0.0f, 1.0f);
                renderTopDownMapOverlay(
                    baseSystem,
                    renderer,
                    renderer.prismalMapTexture,
                    1.0f,
                    glm::vec2(centerX, centerY),
                    framebufferWidth,
                    framebufferHeight,
                    nullptr
                );
            }
            return;
        }
        const bool mapViewActive = isometricMapActive;
        auto setDepthTestEnabled = [&](bool enabled) { renderBackend.setDepthTestEnabled(enabled); };
        auto setDepthWriteEnabled = [&](bool enabled) { renderBackend.setDepthWriteEnabled(enabled); };
        auto setBlendEnabled = [&](bool enabled) { renderBackend.setBlendEnabled(enabled); };
        auto setBlendModeAlpha = [&]() { renderBackend.setBlendModeAlpha(); };
        auto setCullEnabled = [&](bool enabled) { renderBackend.setCullEnabled(enabled); };
        auto setCullBackFaceCCW = [&]() { renderBackend.setCullBackFaceCCW(); };
        auto setCullBackFaceCCWEnabled = [&](bool enabled) {
            setCullEnabled(enabled);
            if (enabled) setCullBackFaceCCW();
        };
        glm::mat4 view = player.viewMatrix;
        glm::mat4 projection = player.projectionMatrix;
        glm::vec3 playerPos = player.cameraPosition;
        glm::vec3 cameraForward;
        cameraForward.x = cos(glm::radians(player.cameraYaw)) * cos(glm::radians(player.cameraPitch));
        cameraForward.y = sin(glm::radians(player.cameraPitch));
        cameraForward.z = sin(glm::radians(player.cameraYaw)) * cos(glm::radians(player.cameraPitch));
        cameraForward = glm::normalize(cameraForward);
        if (isometricMapActive) {
            const bool fullIslandCapture = player.isometricMapCaptureFullIsland;
            const float mapZoom = glm::max(0.0001f, player.isometricMapZoom);
            int framebufferWidth = 0;
            int framebufferHeight = 0;
            if (win) {
                renderBackend.getFramebufferSize(win, framebufferWidth, framebufferHeight);
            }
            const float aspect = (framebufferWidth > 0 && framebufferHeight > 0)
                ? (static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight))
                : (16.0f / 9.0f);
            const float pitchDeg = getRegistryFloat(baseSystem, "IsometricMapPitchDeg", 35.264f);
            const float yawDeg = getRegistryFloat(baseSystem, "IsometricMapYawDeg", 45.0f);
            float distance = glm::max(1.0f, getRegistryFloat(baseSystem, "IsometricMapDistance", 320.0f));
            const float targetHeightOffset = getRegistryFloat(baseSystem, "IsometricMapTargetHeightOffset", 0.0f);
            const bool useOrthographic = getRegistryBool(baseSystem, "IsometricMapUseOrthographic", true);
            float orthoHalfSize = glm::max(1.0f, getRegistryFloat(baseSystem, "IsometricMapOrthoHalfSize", 170.0f));
            const float perspectiveFovDeg = glm::clamp(
                getRegistryFloat(baseSystem, "IsometricMapPerspectiveFovDeg", 50.0f),
                5.0f,
                140.0f
            );
            const float nearPlane = glm::max(0.001f, getRegistryFloat(baseSystem, "IsometricMapNear", 0.1f));
            const float farPlane = glm::max(nearPlane + 1.0f, getRegistryFloat(baseSystem, "IsometricMapFar", 5000.0f));

            glm::vec3 isoForward;
            isoForward.x = std::cos(glm::radians(yawDeg)) * std::cos(glm::radians(pitchDeg));
            isoForward.y = std::sin(glm::radians(pitchDeg));
            isoForward.z = std::sin(glm::radians(yawDeg)) * std::cos(glm::radians(pitchDeg));
            isoForward = glm::normalize(isoForward);

            glm::vec3 target = player.cameraPosition + glm::vec3(0.0f, targetHeightOffset, 0.0f);
            if (fullIslandCapture) {
                float halfDiameter = 0.5f * glm::max(1.0f, getRegistryFloat(baseSystem, "IsometricMapCaptureDiameter", 5000.0f));
                if (world.expanse.islandRadius > 0.0f) halfDiameter = glm::max(halfDiameter, world.expanse.islandRadius);
                if (player.isometricMapOriginInitialized) {
                    target.x = player.isometricMapOrigin.x;
                    target.y = player.isometricMapOrigin.y + targetHeightOffset;
                    target.z = player.isometricMapOrigin.z;
                }
                const float islandMargin = glm::max(0.0f, getRegistryFloat(baseSystem, "IsometricMapIslandMargin", 260.0f));
                const float captureHalfExtent = halfDiameter + islandMargin;
                orthoHalfSize = glm::max(orthoHalfSize, captureHalfExtent);
                distance = glm::max(distance, captureHalfExtent * 2.5f);
            } else {
                orthoHalfSize /= mapZoom;
                distance /= mapZoom;
            }
            const glm::vec3 mapCameraPos = target + isoForward * distance;
            view = glm::lookAt(mapCameraPos, target, glm::vec3(0.0f, 1.0f, 0.0f));
            if (useOrthographic) {
                projection = glm::ortho(
                    -orthoHalfSize * aspect,
                     orthoHalfSize * aspect,
                    -orthoHalfSize,
                     orthoHalfSize,
                     nearPlane,
                     farPlane
                );
            } else {
                projection = glm::perspective(glm::radians(perspectiveFovDeg), aspect, nearPlane, farPlane);
            }
            playerPos = mapCameraPos;
            cameraForward = glm::normalize(target - mapCameraPos);
        }
        if (baseSystem.securityCamera && baseSystem.securityCamera->dawViewActive) {
            const SecurityCameraContext& securityCamera = *baseSystem.securityCamera;
            if (glm::length(securityCamera.viewForward) > 1e-4f) {
                playerPos = securityCamera.viewPosition;
                cameraForward = glm::normalize(securityCamera.viewForward);
                view = glm::lookAt(playerPos, playerPos + cameraForward, glm::vec3(0.0f, 1.0f, 0.0f));
            }
        }
        
        auto now = std::chrono::system_clock::now();
        double epochSeconds = std::chrono::duration<double>(now.time_since_epoch()).count();
        time_t ct = static_cast<time_t>(std::floor(epochSeconds));
        double subSecond = epochSeconds - static_cast<double>(ct);
        tm lt;
        #ifdef _WIN32
        localtime_s(&lt, &ct);
        #else
        localtime_r(&ct, &lt);
        #endif
        double daySeconds = static_cast<double>(lt.tm_hour) * 3600.0
                          + static_cast<double>(lt.tm_min) * 60.0
                          + static_cast<double>(lt.tm_sec)
                          + subSecond;
        float dayFraction = static_cast<float>(daySeconds / 86400.0);
        const float hour = dayFraction * 24.0f;
        const float sunU = (hour - 6.0f) / 12.0f;
        const float moonHour = (hour < 6.0f) ? (hour + 24.0f) : hour;
        const float moonU = (moonHour - 18.0f) / 12.0f;
        const float sunY = glm::max(0.0f, std::sin(sunU * 3.14159265359f));
        const float moonY = glm::max(0.0f, std::sin(moonU * 3.14159265359f));
        const float moonStrength = glm::clamp(getRegistryFloat(baseSystem, "TimeOfDayMoonlightStrength", 0.20f), 0.0f, 1.0f);
        const float dayLightFactor = glm::clamp(glm::max(sunY, moonY * moonStrength), 0.0f, 1.0f);
        const float ambientNight = glm::clamp(getRegistryFloat(baseSystem, "TimeOfDayAmbientNight", 0.08f), 0.0f, 1.0f);
        const float ambientDay = glm::clamp(getRegistryFloat(baseSystem, "TimeOfDayAmbientDay", 0.40f), ambientNight, 1.0f);
        const float diffuseNight = glm::clamp(getRegistryFloat(baseSystem, "TimeOfDayDiffuseNight", 0.10f), 0.0f, 1.0f);
        const float diffuseDay = glm::clamp(getRegistryFloat(baseSystem, "TimeOfDayDiffuseDay", 0.60f), diffuseNight, 1.0f);
        const float ambientScalar = ambientNight + (ambientDay - ambientNight) * dayLightFactor;
        const float diffuseScalar = diffuseNight + (diffuseDay - diffuseNight) * dayLightFactor;
        const glm::vec3 ambientLightColor(ambientScalar);
        const glm::vec3 diffuseLightColor(diffuseScalar);
        std::vector<glm::vec3> starPositions;
        std::vector<std::vector<InstanceData>> behaviorInstances(static_cast<int>(RenderBehavior::COUNT));
        std::vector<BranchInstanceData> branchInstances;
        std::array<std::vector<FaceInstanceRenderData>, 6> faceInstances;
        struct DebugSlopeRenderInstance {
            glm::vec3 position;
            int prototypeID = -1;
            glm::vec3 color = glm::vec3(1.0f);
            DebugSlopeDir dir = DebugSlopeDir::PosX;
        };
        std::vector<DebugSlopeRenderInstance> debugSlopeInstances;
        int voxelGreedyMaxLod = RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelGreedyMaxLod", 1);
        const bool isWebGpuBackend = std::string(renderBackend.name()) == "WebGPU";
        const bool disableGreedyForWebGpu = isWebGpuBackend
            && RenderInitSystemLogic::getRegistryBool(baseSystem, "webgpuDisableVoxelGreedy", false);
        if (disableGreedyForWebGpu) {
            voxelGreedyMaxLod = -1;
        }
        const bool twoSidedAlphaFaces = RenderInitSystemLogic::getRegistryBool(baseSystem, "WaterSurfaceDoubleSided", true);
        const bool leafOpaqueOutsideLod0 = RenderInitSystemLogic::getRegistryBool(baseSystem, "LeafOpaqueOutsideLod0", true);
        const bool leafBackfacesWhenInsideEnabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "LeafBackfacesWhenInside", true);
        const bool foliageWindAnimationEnabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "FoliageWindAnimationEnabled", true);
        const bool leafFanAlignEnabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "LeafFanAlignEnabled", true);
        const bool voxelLightingEnabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "VoxelLightingEnabled", true);
        const bool foliageLightingEnabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "FoliageLightingEnabled", false);
        const bool leafDirectionalLightingEnabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "LeafDirectionalLightingEnabled", true);
        const float leafDirectionalLightingIntensity = glm::clamp(
            getRegistryFloat(baseSystem, "LeafDirectionalLightingIntensity", 1.0f),
            0.0f,
            1.0f
        );
        bool renderOpaqueFacesTwoSided = false;
        if (leafBackfacesWhenInsideEnabled && baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
            const int leafPrototypeID = resolveLeafPrototypeID(prototypes);
            // Keep leaf neighbor-face culling intact, but show the existing canopy shell from inside.
            renderOpaqueFacesTwoSided = cameraInLeaves(*baseSystem.voxelWorld, leafPrototypeID, playerPos);
        }
        const bool waterCascadeBrightnessEnabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "WaterCascadeBrightnessEnabled", true);
        constexpr int waterPaletteVariant = 0;
        const float waterCascadeBrightnessStrength = glm::clamp(
            getRegistryFloat(baseSystem, "WaterCascadeBrightnessStrength", 0.22f),
            0.0f,
            1.0f
        );
        const float waterCascadeBrightnessSpeed = glm::clamp(
            getRegistryFloat(baseSystem, "WaterCascadeBrightnessSpeed", 1.1f),
            0.01f,
            8.0f
        );
        const float waterCascadeBrightnessScale = glm::clamp(
            getRegistryFloat(baseSystem, "WaterCascadeBrightnessScale", 0.18f),
            0.005f,
            3.0f
        );
        const bool waterPlanarReflectionsEnabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "WaterPlanarReflectionsEnabled", false);
        const bool waterUnderwaterCausticsEnabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "WaterUnderwaterCausticsEnabled", false);
        const float waterReflectionPlaneY = getRegistryFloat(
            baseSystem,
            "WaterPlanarReflectionPlaneY",
            world.expanse.waterSurface
        );
        const bool wallStoneUvJitterEnabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "WallStoneUvJitterEnabled", true);
        const bool voxelGridLinesEnabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "VoxelGridLinesEnabled", true);
        const bool voxelGridLineInvertColorEnabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "VoxelGridLineInvertColorEnabled", false);
        const float wallStoneUvJitterMinPixels = glm::clamp(
            getRegistryFloat(baseSystem, "WallStoneUvJitterMinPixels", 1.0f),
            0.0f,
            24.0f
        );
        const float wallStoneUvJitterMaxPixels = glm::clamp(
            getRegistryFloat(baseSystem, "WallStoneUvJitterMaxPixels", 5.0f),
            wallStoneUvJitterMinPixels,
            24.0f
        );
        bool useVoxelGreedy = baseSystem.voxelWorld && baseSystem.voxelWorld->enabled && baseSystem.voxelGreedy
            && renderer.faceShader && renderer.faceVAO && voxelGreedyMaxLod >= 0;
        bool useVoxelRendering = baseSystem.voxelWorld && baseSystem.voxelWorld->enabled && baseSystem.voxelRender
            && (!useVoxelGreedy || (baseSystem.voxelWorld && voxelGreedyMaxLod < baseSystem.voxelWorld->maxLod));
        const bool columnStorageMode = getRegistryString(baseSystem, "voxelStorageMode", "cubic") == "column";
        const bool voxelColumnRequireFoliageReady = getRegistryBool(baseSystem, "voxelColumnRequireFoliageReady", true);
        const bool voxelColumnFoliageRequireAllSubcolumns = getRegistryBool(baseSystem, "voxelColumnFoliageRequireAllSubcolumns", false);
        const int voxelColumnFoliageMaxLod = std::max(0, getRegistryInt(baseSystem, "voxelColumnFoliageMaxLod", 0));
        const bool renderChunkablesViaVoxel = useVoxelGreedy || useVoxelRendering;
        const bool hideHeadVisualizer = RenderInitSystemLogic::getRegistryBool(baseSystem, "PlayerHeadAudioVisualizerHidden", true);
        const int hiddenSparkleVisualizerID = (baseSystem.audio ? baseSystem.audio->sparkleRayEmitterInstanceID : -1);
        const int hiddenSparkleVisualizerWorld = (baseSystem.audio ? baseSystem.audio->sparkleRayEmitterWorldIndex : -1);
        const bool hideActiveSecurityCamera = baseSystem.securityCamera
            && baseSystem.securityCamera->dawViewActive
            && baseSystem.ui
            && baseSystem.ui->active;

        struct VisibleVoxelRenderSection {
            const ChunkRenderBuffers* buffers = nullptr;
            int lod = 0;
        };
        struct VisibleGreedyRenderSection {
            const VoxelGreedyRenderBuffers* buffers = nullptr;
            int lod = 0;
        };
        std::vector<VisibleVoxelRenderSection> visibleVoxelRenderSections;
        std::vector<VisibleGreedyRenderSection> visibleGreedySections;


        for (size_t worldIndex = 0; worldIndex < level.worlds.size(); ++worldIndex) {
            const auto& worldProto = level.worlds[worldIndex];
            for (const auto& instance : worldProto.instances) {
                if (instance.prototypeID < 0 || instance.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[instance.prototypeID];
                if (hideHeadVisualizer
                    && worldProto.name == "PlayerHeadAudioVisualizerWorld"
                    && (proto.name == "AudioVisualizer" || instance.name == "AudioVisualizer")) {
                    continue;
                }
                if (hiddenSparkleVisualizerID > 0
                    && instance.instanceID == hiddenSparkleVisualizerID
                    && (hiddenSparkleVisualizerWorld < 0 || hiddenSparkleVisualizerWorld == static_cast<int>(worldIndex))) {
                    continue;
                }
                if (hideActiveSecurityCamera
                    && proto.name == "SecurityCamera"
                    && static_cast<int>(worldIndex) == baseSystem.securityCamera->activeWorldIndex
                    && instance.instanceID == baseSystem.securityCamera->activeInstanceID) {
                    continue;
                }
                if (proto.isStar) {
                    starPositions.push_back(instance.position);
                }
                // Chunkable terrain/foliage should render from voxel meshes when voxel mode is enabled.
                // Keeping the legacy instance draw active causes stale "ghost" visuals after voxel edits.
                const bool isThrownHeldVisual = instance.name == "__ThrownHeldBlockVisual";
                if (renderChunkablesViaVoxel && proto.isBlock && proto.isChunkable && !isThrownHeldVisual) {
                    continue;
                }
                if (isThrownHeldVisual && proto.isBlock) {
                    static const std::array<glm::vec3, 6> kThrownFaceNormals = {
                        glm::vec3(1.0f, 0.0f, 0.0f),
                        glm::vec3(-1.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f),
                        glm::vec3(0.0f, -1.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, 1.0f),
                        glm::vec3(0.0f, 0.0f, -1.0f)
                    };
                    for (int faceType = 0; faceType < 6; ++faceType) {
                        FaceInstanceRenderData thrownFace;
                        thrownFace.position = instance.position + kThrownFaceNormals[static_cast<size_t>(faceType)] * 0.5f;
                        thrownFace.tileIndex = RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), proto, faceType);
                        thrownFace.color = (thrownFace.tileIndex >= 0) ? glm::vec3(1.0f) : instance.color;
                        thrownFace.alpha = 1.0f;
                        thrownFace.ao = glm::vec4(1.0f);
                        thrownFace.scale = glm::vec2(1.0f);
                        thrownFace.uvScale = glm::vec2(1.0f);
                        faceInstances[faceType].push_back(thrownFace);
                    }
                    continue;
                }
                if (proto.name == "Face_PosX") { faceInstances[0].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.name == "Face_NegX") { faceInstances[1].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.name == "Face_PosY") { faceInstances[2].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.name == "Face_NegY") { faceInstances[3].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.name == "Face_PosZ") { faceInstances[4].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.name == "Face_NegZ") { faceInstances[5].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.name == "Computer" || proto.name == "SecurityCamera") {
                    static const std::array<glm::vec3, 6> kComputerFaceNormals = {
                        glm::vec3(1.0f, 0.0f, 0.0f),
                        glm::vec3(-1.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f),
                        glm::vec3(0.0f, -1.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, 1.0f),
                        glm::vec3(0.0f, 0.0f, -1.0f)
                    };
                    for (int faceType = 0; faceType < 6; ++faceType) {
                        FaceInstanceRenderData face;
                        face.position = instance.position + kComputerFaceNormals[static_cast<size_t>(faceType)] * 0.5f;
                        face.color = glm::vec3(1.0f);
                        face.tileIndex = RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), proto, faceType);
                        face.alpha = 1.0f;
                        face.ao = glm::vec4(1.0f);
                        face.scale = glm::vec2(1.0f);
                        face.uvScale = glm::vec2(1.0f);
                        faceInstances[faceType].push_back(face);
                    }
                    continue;
                }
                DebugSlopeDir slopeDir = DebugSlopeDir::PosX;
                if (tryParseDebugSlopeDir(proto.name, slopeDir)) {
                    debugSlopeInstances.push_back({instance.position, instance.prototypeID, instance.color, slopeDir});
                    continue;
                }
                if (proto.isRenderable && proto.isBlock) {
                    RenderBehavior behavior = RenderBehavior::STATIC_DEFAULT;
                    if (proto.name == "Branch") behavior = RenderBehavior::STATIC_BRANCH;
                    else if (proto.name == "Water") behavior = RenderBehavior::ANIMATED_WATER;
                    else if (proto.name == "TransparentWave") behavior = RenderBehavior::ANIMATED_TRANSPARENT_WAVE;
                    else if (proto.hasWireframe && proto.isAnimated) behavior = RenderBehavior::ANIMATED_WIREFRAME;
                    if (behavior == RenderBehavior::STATIC_BRANCH) branchInstances.push_back({instance.position, instance.rotation, instance.color});
                    else behaviorInstances[static_cast<int>(behavior)].push_back({instance.position, instance.color});
                }
            }
        }

        if (RenderInitSystemLogic::getRegistryBool(baseSystem, "DebugVoxelRender", false) && baseSystem.voxelWorld) {
            size_t sectionCount = baseSystem.voxelWorld->sections.size();
            size_t renderCount = baseSystem.voxelRender ? baseSystem.voxelRender->renderBuffers.size() : 0;
            size_t greedyCount = baseSystem.voxelGreedy ? baseSystem.voxelGreedy->renderBuffers.size() : 0;
            std::cout << "[DebugVoxelRender] sections=" << sectionCount
                      << " renderBuffers=" << renderCount
                      << " greedyBuffers=" << greedyCount
                      << " useVoxelRendering=" << (useVoxelRendering ? 1 : 0)
                      << " useVoxelGreedy=" << (useVoxelGreedy ? 1 : 0)
                      << std::endl;
        }
        glm::vec3 skyTop(0.52f, 0.66f, 0.95f);
        glm::vec3 skyBottom(0.03f, 0.06f, 0.18f);
        SkyboxSystemLogic::getCurrentSkyColors(dayFraction, world.skyKeys, skyTop, skyBottom);
        glm::vec3 lightDir;
        SkyboxSystemLogic::RenderSkyAndCelestials(baseSystem, prototypes, starPositions, time, dayFraction, view, projection, playerPos, lightDir);
        if (dt > 0.0f) {
            MiniVoxelParticleSystemLogic::UpdateParticles(baseSystem, dt, faceInstances);
        }
        // Aurora and cloud passes are intentionally removed from the active render path.

        // Establish deterministic world-state defaults before voxel/block passes.
        // WebGPU render-state starts from backend defaults each frame, so relying
        // on previous systems can leave depth testing disabled and cause severe
        // overdraw artifacts (e.g. only certain face orientations appearing).
        setDepthTestEnabled(true);
        setDepthWriteEnabled(true);
        setBlendEnabled(false);
        setCullEnabled(false);

        renderer.blockShader->use();
        renderer.blockShader->setMat4("view", view);
        renderer.blockShader->setMat4("projection", projection);
        renderer.blockShader->setVec3("cameraPos", playerPos);
        renderer.blockShader->setFloat("time", time);
        renderer.blockShader->setFloat("instanceScale", 1.0f);
        renderer.blockShader->setVec3("lightDir",lightDir);
        renderer.blockShader->setVec3("ambientLight", ambientLightColor);
        renderer.blockShader->setVec3("diffuseLight", diffuseLightColor);
        renderer.blockShader->setInt("voxelGridLinesEnabled", voxelGridLinesEnabled ? 1 : 0);
        renderer.blockShader->setInt("voxelGridLineInvertColorEnabled", voxelGridLineInvertColorEnabled ? 1 : 0);
        renderer.blockShader->setMat4("model", glm::mat4(1.0f));
        BlockChargeSystemLogic::ApplyBlockDamageMaskUniforms(baseSystem, prototypes, *renderer.blockShader, true);
        setBlendEnabled(true);
        setBlendModeAlpha();

        if (useVoxelRendering) {
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            VoxelRenderContext& voxelRender = *baseSystem.voxelRender;
            visibleVoxelRenderSections.reserve(voxelRender.renderBuffers.size());
            for (const auto& [sectionKey, buffers] : voxelRender.renderBuffers) {
                auto secIt = voxelWorld.sections.find(sectionKey);
                if (secIt == voxelWorld.sections.end()) continue;
                const VoxelSection& section = secIt->second;
                if (!mapViewActive
                    && !RenderInitSystemLogic::shouldRenderVoxelSection(baseSystem, section, playerPos)) {
                    continue;
                }
                visibleVoxelRenderSections.push_back({&buffers, section.lod});
            }
        }

        for (int i = 0; i < static_cast<int>(RenderBehavior::COUNT); ++i) {
            RenderBehavior currentBehavior = static_cast<RenderBehavior>(i);
            bool translucent = (currentBehavior == RenderBehavior::ANIMATED_WATER || currentBehavior == RenderBehavior::ANIMATED_TRANSPARENT_WAVE);
            if (translucent) {
                // Let translucent passes read depth but avoid writing it so surfaces beneath stay visible.
                setDepthWriteEnabled(false);
            }
            if (useVoxelRendering) {
                for (const auto& section : visibleVoxelRenderSections) {
                    int count = section.buffers->counts[i];
                    if (count <= 0) continue;
                    renderer.blockShader->setFloat("instanceScale", static_cast<float>(1 << section.lod));
                    renderer.blockShader->setInt("behaviorType", i);
                    if (section.buffers->vaos[i] == 0) continue;
                    renderBackend.bindVertexArray(section.buffers->vaos[i]);
                    renderBackend.drawArraysTrianglesInstanced(0, 36, count);
                }
                renderer.blockShader->setFloat("instanceScale", 1.0f);
            }
            if (currentBehavior == RenderBehavior::STATIC_BRANCH) {
                if (!branchInstances.empty()) {
                    renderer.blockShader->setInt("behaviorType", i);
                    renderBackend.bindVertexArray(renderer.behaviorVAOs[i]);
                    renderBackend.uploadArrayBufferData(
                        renderer.behaviorInstanceVBOs[i],
                        branchInstances.data(),
                        branchInstances.size() * sizeof(BranchInstanceData),
                        true
                    );
                    renderBackend.drawArraysTrianglesInstanced(0, 36, static_cast<int>(branchInstances.size()));
                }
            } else {
                if (!behaviorInstances[i].empty()) {
                    renderer.blockShader->setInt("behaviorType", i);
                    renderBackend.bindVertexArray(renderer.behaviorVAOs[i]);
                    renderBackend.uploadArrayBufferData(
                        renderer.behaviorInstanceVBOs[i],
                        behaviorInstances[i].data(),
                        behaviorInstances[i].size() * sizeof(InstanceData),
                        true
                    );
                    renderBackend.drawArraysTrianglesInstanced(0, 36, static_cast<int>(behaviorInstances[i].size()));
                }
            }
            if (translucent) {
                setDepthWriteEnabled(true);
            }
        }

        auto bindTextureUnit2D = [&](int unit, RenderHandle texture) {
            renderBackend.bindTexture2D(texture, unit);
        };

        auto bindFaceTextureUniforms = [&](Shader& shader, bool allowPlanarReflection = true){
            auto resolveAtlasTile = [&](const std::string& textureKey) -> int {
                auto it = world.atlasMappings.find(textureKey);
                if (it == world.atlasMappings.end()) return -1;
                const FaceTextureSet& set = it->second;
                if (set.all >= 0) return set.all;
                if (set.side >= 0) return set.side;
                if (set.top >= 0) return set.top;
                if (set.bottom >= 0) return set.bottom;
                return -1;
            };
            const int wallStoneUvJitterTile0 = resolveAtlasTile("CobblestoneRYBBlue");
            const int wallStoneUvJitterTile1 = resolveAtlasTile("CobblestoneRYBRed");
            const int wallStoneUvJitterTile2 = resolveAtlasTile("CobblestoneRYBYellow");
            int grassAtlasShortBaseTile = resolveAtlasTile("24x24Zt1ConiferShortGrassSideV001");
            if (grassAtlasShortBaseTile < 0) grassAtlasShortBaseTile = resolveAtlasTile("ShortGrassV001");
            int grassAtlasTallBaseTile = resolveAtlasTile("24x24Zt1ConiferTallGrassSideV001");
            if (grassAtlasTallBaseTile < 0) grassAtlasTallBaseTile = resolveAtlasTile("TallGrassV001");

            shader.setInt("atlasEnabled", (renderer.atlasTexture != 0 && renderer.atlasTilesPerRow > 0 && renderer.atlasTilesPerCol > 0) ? 1 : 0);
            shader.setVec2("atlasTileSize", glm::vec2(renderer.atlasTileSize));
            shader.setVec2("atlasTextureSize", glm::vec2(renderer.atlasTextureSize));
            shader.setInt("tilesPerRow", renderer.atlasTilesPerRow);
            shader.setInt("tilesPerCol", renderer.atlasTilesPerCol);
            shader.setInt("grassAtlasShortBaseTile", grassAtlasShortBaseTile);
            shader.setInt("grassAtlasTallBaseTile", grassAtlasTallBaseTile);
            shader.setInt("wallStoneUvJitterEnabled", wallStoneUvJitterEnabled ? 1 : 0);
            shader.setInt("wallStoneUvJitterTile0", wallStoneUvJitterTile0);
            shader.setInt("wallStoneUvJitterTile1", wallStoneUvJitterTile1);
            shader.setInt("wallStoneUvJitterTile2", wallStoneUvJitterTile2);
            shader.setFloat("wallStoneUvJitterMinPixels", wallStoneUvJitterMinPixels);
            shader.setFloat("wallStoneUvJitterMaxPixels", wallStoneUvJitterMaxPixels);
            shader.setInt("foliageLightingEnabled", foliageLightingEnabled ? 1 : 0);
            shader.setInt("leafDirectionalLightingEnabled", leafDirectionalLightingEnabled ? 1 : 0);
            shader.setFloat("leafDirectionalLightingIntensity", leafDirectionalLightingIntensity);
            shader.setInt("voxelGridLinesEnabled", voxelGridLinesEnabled ? 1 : 0);
            shader.setInt("voxelGridLineInvertColorEnabled", voxelGridLineInvertColorEnabled ? 1 : 0);
            shader.setInt("leafFanAlignEnabled", leafFanAlignEnabled ? 1 : 0);
            shader.setInt("waterPaletteVariant", waterPaletteVariant);
            shader.setInt("waterUnderwaterCausticsEnabled", waterUnderwaterCausticsEnabled ? 1 : 0);
            shader.setVec3("topColor", skyTop);
            shader.setVec3("bottomColor", skyBottom);
            shader.setInt("atlasTexture", 0);
            const bool hasAllGrassTextures =
                renderer.grassTextureCount >= 3
                && renderer.grassTextures[0] != 0
                && renderer.grassTextures[1] != 0
                && renderer.grassTextures[2] != 0;
            const bool hasAllShortGrassTextures =
                renderer.shortGrassTextureCount >= 3
                && renderer.shortGrassTextures[0] != 0
                && renderer.shortGrassTextures[1] != 0
                && renderer.shortGrassTextures[2] != 0;
            const bool hasAllOreTextures =
                renderer.oreTextureCount >= 4
                && renderer.oreTextures[0] != 0
                && renderer.oreTextures[1] != 0
                && renderer.oreTextures[2] != 0
                && renderer.oreTextures[3] != 0;
            const bool hasAllTerrainTextures =
                renderer.terrainTextureCount >= 2
                && renderer.terrainTextures[0] != 0
                && renderer.terrainTextures[1] != 0;
            const bool waterOverlayEnabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "WaterOverlayTextureEnabled", false);
            const bool hasWaterOverlayTexture = waterOverlayEnabled && renderer.waterOverlayTexture != 0;
            const bool hasWaterReflectionTexture = waterPlanarReflectionsEnabled
                && allowPlanarReflection
                && renderer.waterReflectionTex != 0;
            static int sMaxTextureUnits = -1;
            if (sMaxTextureUnits < 0) {
                sMaxTextureUnits = renderBackend.getMaxTextureImageUnits();
            }
            const bool enoughUnitsForDualGrassSets = (sMaxTextureUnits >= 7);
            const bool enoughUnitsForOreSet = (sMaxTextureUnits >= 11);
            const bool enoughUnitsForTerrainSet = (sMaxTextureUnits >= 13);
            const bool enoughUnitsForWaterOverlay = (sMaxTextureUnits >= 14);
            const bool enoughUnitsForWaterReflection = (sMaxTextureUnits >= 15);
            shader.setInt("grassTextureEnabled", hasAllGrassTextures ? 1 : 0);
            shader.setInt("grassTexture0", 1);
            shader.setInt("grassTexture1", 2);
            shader.setInt("grassTexture2", 3);
            shader.setInt("shortGrassTextureEnabled", (hasAllShortGrassTextures && enoughUnitsForDualGrassSets) ? 1 : 0);
            shader.setInt("shortGrassTexture0", 4);
            shader.setInt("shortGrassTexture1", 5);
            shader.setInt("shortGrassTexture2", 6);
            shader.setInt("oreTextureEnabled", (hasAllOreTextures && enoughUnitsForOreSet) ? 1 : 0);
            shader.setInt("oreTexture0", 7);
            shader.setInt("oreTexture1", 8);
            shader.setInt("oreTexture2", 9);
            shader.setInt("oreTexture3", 10);
            shader.setInt("terrainTextureEnabled", (hasAllTerrainTextures && enoughUnitsForTerrainSet) ? 1 : 0);
            shader.setInt("terrainTextureDirt", 11);
            shader.setInt("terrainTextureStone", 12);
            shader.setInt("waterOverlayTextureEnabled", (hasWaterOverlayTexture && enoughUnitsForWaterOverlay) ? 1 : 0);
            shader.setInt("waterOverlayTexture", 13);
            shader.setInt(
                "waterPlanarReflectionEnabled",
                (hasWaterReflectionTexture && enoughUnitsForWaterReflection) ? 1 : 0
            );
            shader.setFloat("waterReflectionPlaneY", waterReflectionPlaneY);
            shader.setInt("waterReflectionTexture", 14);
            if (hasAllGrassTextures) {
                bindTextureUnit2D(1, renderer.grassTextures[0]);
                bindTextureUnit2D(2, renderer.grassTextures[1]);
                bindTextureUnit2D(3, renderer.grassTextures[2]);
            }
            if (hasAllShortGrassTextures && enoughUnitsForDualGrassSets) {
                bindTextureUnit2D(4, renderer.shortGrassTextures[0]);
                bindTextureUnit2D(5, renderer.shortGrassTextures[1]);
                bindTextureUnit2D(6, renderer.shortGrassTextures[2]);
            }
            if (hasAllOreTextures && enoughUnitsForOreSet) {
                bindTextureUnit2D(7, renderer.oreTextures[0]);
                bindTextureUnit2D(8, renderer.oreTextures[1]);
                bindTextureUnit2D(9, renderer.oreTextures[2]);
                bindTextureUnit2D(10, renderer.oreTextures[3]);
            }
            if (hasAllTerrainTextures && enoughUnitsForTerrainSet) {
                bindTextureUnit2D(11, renderer.terrainTextures[0]);
                bindTextureUnit2D(12, renderer.terrainTextures[1]);
            }
            if (hasWaterOverlayTexture && enoughUnitsForWaterOverlay) {
                bindTextureUnit2D(13, renderer.waterOverlayTexture);
            }
            if (hasWaterReflectionTexture && enoughUnitsForWaterReflection) {
                bindTextureUnit2D(14, renderer.waterReflectionTex);
            }
            bindTextureUnit2D(0, renderer.atlasTexture);
        };

        auto drawFaceBatches = [&](const std::array<std::vector<FaceInstanceRenderData>, 6>& batches, bool depthWrite){
            if (!renderer.faceShader || !renderer.faceVAO) return;
            if (!depthWrite) setDepthWriteEnabled(false);
            bool enableCull = depthWrite ? !renderOpaqueFacesTwoSided : !twoSidedAlphaFaces;
            setCullBackFaceCCWEnabled(enableCull);

            renderer.faceShader->use();
            renderer.faceShader->setMat4("view", view);
            renderer.faceShader->setMat4("projection", projection);
            renderer.faceShader->setMat4("model", glm::mat4(1.0f));
            renderer.faceShader->setVec3("cameraPos", playerPos);
            renderer.faceShader->setFloat("time", time);
            renderer.faceShader->setVec3("lightDir", lightDir);
            renderer.faceShader->setVec3("ambientLight", ambientLightColor);
            renderer.faceShader->setVec3("diffuseLight", diffuseLightColor);
            renderer.faceShader->setInt("faceType", 0);
            renderer.faceShader->setInt("sectionLod", 0);
            renderer.faceShader->setInt("leafOpaqueOutsideLod0", leafOpaqueOutsideLod0 ? 1 : 0);
            renderer.faceShader->setInt("leafBackfacesWhenInside", 0);
            renderer.faceShader->setInt("foliageWindEnabled", foliageWindAnimationEnabled ? 1 : 0);
            renderer.faceShader->setInt("waterCascadeBrightnessEnabled", waterCascadeBrightnessEnabled ? 1 : 0);
            renderer.faceShader->setFloat("waterCascadeBrightnessStrength", waterCascadeBrightnessStrength);
            renderer.faceShader->setFloat("waterCascadeBrightnessSpeed", waterCascadeBrightnessSpeed);
            renderer.faceShader->setFloat("waterCascadeBrightnessScale", waterCascadeBrightnessScale);
            bindFaceTextureUniforms(*renderer.faceShader);
            BlockChargeSystemLogic::ApplyBlockDamageMaskUniforms(baseSystem, prototypes, *renderer.faceShader, true);
            renderBackend.bindVertexArray(renderer.faceVAO);
            for (int faceType = 0; faceType < 6; ++faceType) {
                const auto& instances = batches[faceType];
                if (instances.empty()) continue;
                renderer.faceShader->setInt("faceType", faceType);
                renderBackend.uploadArrayBufferData(
                    renderer.faceInstanceVBO,
                    instances.data(),
                    instances.size() * sizeof(FaceInstanceRenderData),
                    true
                );
                renderBackend.drawArraysTrianglesInstanced(0, 6, static_cast<int>(instances.size()));
            }

            setCullEnabled(false);
            if (!depthWrite) setDepthWriteEnabled(true);
        };
        auto isWaterSlopeAlpha = [](float alpha) {
            return alpha <= -23.5f && alpha > -33.5f;
        };
        auto isFoliageTaggedAlpha = [](float alpha) {
            // Sentinel-tagged translucent foliage/card ranges authored by mesher:
            // short/tall grass (-2.x), flower (-3.x), cave pot (-10.x).
            // Keep leaf blocks (-1.x) on opaque path for stable depth behavior.
            if (alpha <= -1.5f && alpha > -3.5f) return true;
            if (alpha <= -9.5f && alpha > -13.5f) return true;
            return false;
        };
        auto isWaterLikeTranslucentAlpha = [&](float alpha) {
            return (alpha >= 0.0f && alpha < 0.999f) || isWaterSlopeAlpha(alpha);
        };

        const bool waterReflectionTargetReady = waterPlanarReflectionsEnabled
            && renderer.waterReflectionFBO != 0
            && renderer.waterReflectionTex != 0
            && renderer.waterReflectionWidth > 0
            && renderer.waterReflectionHeight > 0;
        if (waterReflectionTargetReady) {
            const glm::mat4 reflectionTransform = glm::translate(
                glm::mat4(1.0f),
                glm::vec3(0.0f, 2.0f * waterReflectionPlaneY, 0.0f)
            ) * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f));
            const glm::mat4 reflectionView = view * reflectionTransform;
            glm::vec3 reflectionCameraPos = playerPos;
            reflectionCameraPos.y = 2.0f * waterReflectionPlaneY - playerPos.y;

            renderBackend.beginOffscreenColorPass(
                renderer.waterReflectionFBO,
                renderer.waterReflectionWidth,
                renderer.waterReflectionHeight,
                skyBottom.r,
                skyBottom.g,
                skyBottom.b,
                1.0f
            );
            setDepthTestEnabled(true);
            setDepthWriteEnabled(true);
            setBlendEnabled(false);
            setCullEnabled(false);
            // Avoid read/write hazard: reflection target must not be bound as sampled input
            // during the offscreen pass that writes it.
            bindTextureUnit2D(14, 0);

            renderer.blockShader->use();
            renderer.blockShader->setMat4("view", reflectionView);
            renderer.blockShader->setMat4("projection", projection);
            renderer.blockShader->setVec3("cameraPos", reflectionCameraPos);
            renderer.blockShader->setFloat("time", time);
            renderer.blockShader->setFloat("instanceScale", 1.0f);
            renderer.blockShader->setVec3("lightDir", lightDir);
            renderer.blockShader->setVec3("ambientLight", ambientLightColor);
            renderer.blockShader->setVec3("diffuseLight", diffuseLightColor);
            renderer.blockShader->setInt("voxelGridLinesEnabled", voxelGridLinesEnabled ? 1 : 0);
            renderer.blockShader->setInt("voxelGridLineInvertColorEnabled", voxelGridLineInvertColorEnabled ? 1 : 0);
            renderer.blockShader->setMat4("model", glm::mat4(1.0f));
            BlockChargeSystemLogic::ApplyBlockDamageMaskUniforms(baseSystem, prototypes, *renderer.blockShader, true);

            for (int i = 0; i < static_cast<int>(RenderBehavior::COUNT); ++i) {
                RenderBehavior currentBehavior = static_cast<RenderBehavior>(i);
                if (currentBehavior == RenderBehavior::ANIMATED_WATER
                    || currentBehavior == RenderBehavior::ANIMATED_TRANSPARENT_WAVE) {
                    continue;
                }
                if (useVoxelRendering && baseSystem.voxelWorld && baseSystem.voxelRender) {
                    VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
                    VoxelRenderContext& voxelRender = *baseSystem.voxelRender;
                    for (const auto& [sectionKey, buffers] : voxelRender.renderBuffers) {
                        auto secIt = voxelWorld.sections.find(sectionKey);
                        if (secIt == voxelWorld.sections.end()) continue;
                        const VoxelSection& section = secIt->second;
                        if (!mapViewActive
                            && !RenderInitSystemLogic::shouldRenderVoxelSection(baseSystem, section, reflectionCameraPos)) {
                            continue;
                        }
                        int count = buffers.counts[i];
                        if (count <= 0) continue;
                        renderer.blockShader->setFloat("instanceScale", static_cast<float>(1 << section.lod));
                        renderer.blockShader->setInt("behaviorType", i);
                        if (buffers.vaos[i] == 0) continue;
                        renderBackend.bindVertexArray(buffers.vaos[i]);
                        renderBackend.drawArraysTrianglesInstanced(0, 36, count);
                    }
                    renderer.blockShader->setFloat("instanceScale", 1.0f);
                }
                if (currentBehavior == RenderBehavior::STATIC_BRANCH) {
                    if (!branchInstances.empty()) {
                        renderer.blockShader->setInt("behaviorType", i);
                        renderBackend.bindVertexArray(renderer.behaviorVAOs[i]);
                        renderBackend.uploadArrayBufferData(
                            renderer.behaviorInstanceVBOs[i],
                            branchInstances.data(),
                            branchInstances.size() * sizeof(BranchInstanceData),
                            true
                        );
                        renderBackend.drawArraysTrianglesInstanced(0, 36, static_cast<int>(branchInstances.size()));
                    }
                } else if (!behaviorInstances[i].empty()) {
                    renderer.blockShader->setInt("behaviorType", i);
                    renderBackend.bindVertexArray(renderer.behaviorVAOs[i]);
                    renderBackend.uploadArrayBufferData(
                        renderer.behaviorInstanceVBOs[i],
                        behaviorInstances[i].data(),
                        behaviorInstances[i].size() * sizeof(InstanceData),
                        true
                    );
                    renderBackend.drawArraysTrianglesInstanced(0, 36, static_cast<int>(behaviorInstances[i].size()));
                }
            }

            if (renderer.faceShader && renderer.faceVAO) {
                std::array<std::vector<FaceInstanceRenderData>, 6> faceInstancesOpaque;
                for (int f = 0; f < 6; ++f) {
                    for (const auto& inst : faceInstances[f]) {
                        if (((inst.alpha < 0.0f)
                                && !isWaterSlopeAlpha(inst.alpha)
                                && !isFoliageTaggedAlpha(inst.alpha))
                            || inst.alpha >= 0.999f) {
                            faceInstancesOpaque[f].push_back(inst);
                        }
                    }
                }

                renderer.faceShader->use();
                renderer.faceShader->setMat4("view", reflectionView);
                renderer.faceShader->setMat4("projection", projection);
                renderer.faceShader->setMat4("model", glm::mat4(1.0f));
                renderer.faceShader->setVec3("cameraPos", reflectionCameraPos);
                renderer.faceShader->setFloat("time", time);
                renderer.faceShader->setVec3("lightDir", lightDir);
                renderer.faceShader->setVec3("ambientLight", ambientLightColor);
                renderer.faceShader->setVec3("diffuseLight", diffuseLightColor);
                renderer.faceShader->setInt("faceType", 0);
                renderer.faceShader->setInt("sectionLod", 0);
                renderer.faceShader->setInt("leafOpaqueOutsideLod0", leafOpaqueOutsideLod0 ? 1 : 0);
                renderer.faceShader->setInt("leafBackfacesWhenInside", 0);
                renderer.faceShader->setInt("foliageWindEnabled", foliageWindAnimationEnabled ? 1 : 0);
                renderer.faceShader->setInt("waterCascadeBrightnessEnabled", waterCascadeBrightnessEnabled ? 1 : 0);
                renderer.faceShader->setFloat("waterCascadeBrightnessStrength", waterCascadeBrightnessStrength);
                renderer.faceShader->setFloat("waterCascadeBrightnessSpeed", waterCascadeBrightnessSpeed);
                renderer.faceShader->setFloat("waterCascadeBrightnessScale", waterCascadeBrightnessScale);
                bindFaceTextureUniforms(*renderer.faceShader, false);
                BlockChargeSystemLogic::ApplyBlockDamageMaskUniforms(baseSystem, prototypes, *renderer.faceShader, true);
                renderBackend.bindVertexArray(renderer.faceVAO);
                if (renderOpaqueFacesTwoSided) {
                    setCullEnabled(false);
                } else {
                    setCullBackFaceCCWEnabled(true);
                }
                for (int faceType = 0; faceType < 6; ++faceType) {
                    const auto& instances = faceInstancesOpaque[faceType];
                    if (instances.empty()) continue;
                    renderer.faceShader->setInt("faceType", faceType);
                    renderBackend.uploadArrayBufferData(
                        renderer.faceInstanceVBO,
                        instances.data(),
                        instances.size() * sizeof(FaceInstanceRenderData),
                        true
                    );
                    renderBackend.drawArraysTrianglesInstanced(0, 6, static_cast<int>(instances.size()));
                }
                setCullEnabled(false);
            }

            if (useVoxelGreedy && renderer.faceShader && renderer.faceVAO) {
                VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
                VoxelGreedyContext& voxelGreedy = *baseSystem.voxelGreedy;
                int superChunkMinLod = RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkMinLod", 3);
                int superChunkMaxLod = RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkMaxLod", 3);
                int superChunkSize = RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkSize", 1);
                if (superChunkSize < 1) superChunkSize = 1;

                renderer.faceShader->use();
                renderer.faceShader->setMat4("view", reflectionView);
                renderer.faceShader->setMat4("projection", projection);
                renderer.faceShader->setMat4("model", glm::mat4(1.0f));
                renderer.faceShader->setVec3("cameraPos", reflectionCameraPos);
                renderer.faceShader->setFloat("time", time);
                renderer.faceShader->setVec3("lightDir", lightDir);
                renderer.faceShader->setVec3("ambientLight", ambientLightColor);
                renderer.faceShader->setVec3("diffuseLight", diffuseLightColor);
                renderer.faceShader->setInt("faceType", 0);
                renderer.faceShader->setInt("leafOpaqueOutsideLod0", leafOpaqueOutsideLod0 ? 1 : 0);
                renderer.faceShader->setInt("leafBackfacesWhenInside", 0);
                renderer.faceShader->setInt("foliageWindEnabled", foliageWindAnimationEnabled ? 1 : 0);
                renderer.faceShader->setInt("waterCascadeBrightnessEnabled", waterCascadeBrightnessEnabled ? 1 : 0);
                renderer.faceShader->setFloat("waterCascadeBrightnessStrength", waterCascadeBrightnessStrength);
                renderer.faceShader->setFloat("waterCascadeBrightnessSpeed", waterCascadeBrightnessSpeed);
                renderer.faceShader->setFloat("waterCascadeBrightnessScale", waterCascadeBrightnessScale);
                bindFaceTextureUniforms(*renderer.faceShader, false);
                BlockChargeSystemLogic::ApplyBlockDamageMaskUniforms(baseSystem, prototypes, *renderer.faceShader, true);

                auto columnReadyForRenderKey = [&](int lod, const glm::ivec3& coord, int sizeMultiplier) {
                    if (!columnStorageMode) return true;
                    const int clampedMult = std::max(1, sizeMultiplier);
                    for (int oz = 0; oz < clampedMult; ++oz) {
                        for (int ox = 0; ox < clampedMult; ++ox) {
                            const glm::ivec2 columnCoord(coord.x + ox, coord.z + oz);
                            if (!TerrainSystemLogic::IsColumnFullyReady(lod, columnCoord)) {
                                return false;
                            }
                            const bool shouldCheckFoliageSubcolumn =
                                voxelColumnRequireFoliageReady
                                && lod <= voxelColumnFoliageMaxLod
                                && (voxelColumnFoliageRequireAllSubcolumns || clampedMult == 1 || (ox == 0 && oz == 0));
                            if (shouldCheckFoliageSubcolumn
                                && !TreeGenerationSystemLogic::IsColumnFoliageReady(lod, columnCoord)) {
                                return false;
                            }
                        }
                    }
                    return true;
                };

                if (renderOpaqueFacesTwoSided) {
                    setCullEnabled(false);
                } else {
                    setCullBackFaceCCWEnabled(true);
                }
                for (const auto& [sectionKey, buffers] : voxelGreedy.renderBuffers) {
                    auto secIt = voxelWorld.sections.find(sectionKey);
                    if (secIt == voxelWorld.sections.end()) continue;
                    const VoxelSection& section = secIt->second;
                    if (section.lod > voxelGreedyMaxLod) continue;
                    int mult = (sectionKey.lod >= superChunkMinLod
                                && sectionKey.lod <= superChunkMaxLod
                                && superChunkSize > 1) ? superChunkSize : 1;
                    if (!columnReadyForRenderKey(section.lod, sectionKey.coord, mult)) continue;
                    if (!mapViewActive
                        && !RenderInitSystemLogic::shouldRenderVoxelSectionSized(
                            baseSystem,
                            sectionKey.lod,
                            sectionKey.coord,
                            section.size,
                            mult,
                            reflectionCameraPos
                        )) {
                        continue;
                    }

                    renderer.faceShader->setInt("sectionLod", section.lod);
                    for (int faceType = 0; faceType < 6; ++faceType) {
                        int count = buffers.opaqueCounts[faceType];
                        if (count > 0 && buffers.opaqueVaos[faceType] != 0) {
                            renderer.faceShader->setInt("faceType", faceType);
                            renderBackend.bindVertexArray(buffers.opaqueVaos[faceType]);
                            renderBackend.drawArraysTrianglesInstanced(0, 6, count);
                        }
                    }
                }
                setCullEnabled(false);
            }

            renderBackend.endOffscreenColorPass();
            setDepthTestEnabled(true);
            setDepthWriteEnabled(true);
            setBlendEnabled(false);
            setCullEnabled(false);
        }

        if (renderer.faceShader && renderer.faceVAO) {
            std::array<std::vector<FaceInstanceRenderData>, 6> faceInstancesOpaque;
            std::array<std::vector<FaceInstanceRenderData>, 6> faceInstancesAlpha;
            std::array<std::vector<FaceInstanceRenderData>, 6> faceInstancesAlphaWaterLike;
            for (int f = 0; f < 6; ++f) {
                for (const auto& inst : faceInstances[f]) {
                    if (inst.alpha < 0.0f
                        && !isWaterSlopeAlpha(inst.alpha)
                        && !isFoliageTaggedAlpha(inst.alpha)) {
                        faceInstancesOpaque[f].push_back(inst);
                    }
                    else if (isWaterLikeTranslucentAlpha(inst.alpha)) {
                        // Draw water-like faces last so submerged foliage does not overdraw water.
                        faceInstancesAlphaWaterLike[f].push_back(inst);
                    } else if (inst.alpha < 0.999f) {
                        faceInstancesAlpha[f].push_back(inst);
                    } else {
                        faceInstancesOpaque[f].push_back(inst);
                    }
                }
            }
            drawFaceBatches(faceInstancesOpaque, true);
            drawFaceBatches(faceInstancesAlpha, false);
            drawFaceBatches(faceInstancesAlphaWaterLike, false);
        }

        if (useVoxelGreedy && renderer.faceShader && renderer.faceVAO) {
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            VoxelGreedyContext& voxelGreedy = *baseSystem.voxelGreedy;
            int superChunkMinLod = RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkMinLod", 3);
            int superChunkMaxLod = RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkMaxLod", 3);
            int superChunkSize = RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkSize", 1);
            if (superChunkSize < 1) superChunkSize = 1;

            renderer.faceShader->use();
            renderer.faceShader->setMat4("view", view);
            renderer.faceShader->setMat4("projection", projection);
            renderer.faceShader->setMat4("model", glm::mat4(1.0f));
            renderer.faceShader->setVec3("cameraPos", playerPos);
            renderer.faceShader->setFloat("time", time);
            renderer.faceShader->setVec3("lightDir", lightDir);
            renderer.faceShader->setVec3("ambientLight", ambientLightColor);
            renderer.faceShader->setVec3("diffuseLight", diffuseLightColor);
            renderer.faceShader->setInt("faceType", 0);
            renderer.faceShader->setInt("leafOpaqueOutsideLod0", leafOpaqueOutsideLod0 ? 1 : 0);
            renderer.faceShader->setInt("leafBackfacesWhenInside", 0);
            renderer.faceShader->setInt("foliageWindEnabled", foliageWindAnimationEnabled ? 1 : 0);
            renderer.faceShader->setInt("waterCascadeBrightnessEnabled", waterCascadeBrightnessEnabled ? 1 : 0);
            renderer.faceShader->setFloat("waterCascadeBrightnessStrength", waterCascadeBrightnessStrength);
            renderer.faceShader->setFloat("waterCascadeBrightnessSpeed", waterCascadeBrightnessSpeed);
            renderer.faceShader->setFloat("waterCascadeBrightnessScale", waterCascadeBrightnessScale);
            bindFaceTextureUniforms(*renderer.faceShader);
            BlockChargeSystemLogic::ApplyBlockDamageMaskUniforms(baseSystem, prototypes, *renderer.faceShader, true);

            if (renderOpaqueFacesTwoSided) {
                setCullEnabled(false);
            } else {
                setCullBackFaceCCWEnabled(true);
            }
            auto columnReadyForRenderKey = [&](int lod, const glm::ivec3& coord, int sizeMultiplier) {
                if (!columnStorageMode) return true;
                const int clampedMult = std::max(1, sizeMultiplier);
                for (int oz = 0; oz < clampedMult; ++oz) {
                    for (int ox = 0; ox < clampedMult; ++ox) {
                        const glm::ivec2 columnCoord(coord.x + ox, coord.z + oz);
                        if (!TerrainSystemLogic::IsColumnFullyReady(
                                lod,
                                columnCoord)) {
                            return false;
                        }
                        const bool shouldCheckFoliageSubcolumn =
                            voxelColumnRequireFoliageReady
                            && lod <= voxelColumnFoliageMaxLod
                            && (voxelColumnFoliageRequireAllSubcolumns || clampedMult == 1 || (ox == 0 && oz == 0));
                        if (shouldCheckFoliageSubcolumn
                            && !TreeGenerationSystemLogic::IsColumnFoliageReady(lod, columnCoord)) {
                            return false;
                        }
                    }
                }
                return true;
            };

            visibleGreedySections.clear();
            visibleGreedySections.reserve(voxelGreedy.renderBuffers.size());
            for (const auto& [sectionKey, buffers] : voxelGreedy.renderBuffers) {
                auto secIt = voxelWorld.sections.find(sectionKey);
                if (secIt == voxelWorld.sections.end()) continue;
                const VoxelSection& section = secIt->second;
                if (section.lod > voxelGreedyMaxLod) continue;
                int mult = (sectionKey.lod >= superChunkMinLod
                            && sectionKey.lod <= superChunkMaxLod
                            && superChunkSize > 1) ? superChunkSize : 1;
                if (!columnReadyForRenderKey(section.lod, sectionKey.coord, mult)) continue;
                if (!mapViewActive
                    && !RenderInitSystemLogic::shouldRenderVoxelSectionSized(baseSystem,
                                                       sectionKey.lod,
                                                       sectionKey.coord,
                                                       section.size,
                                                       mult,
                                                       playerPos)) {
                    continue;
                }
                visibleGreedySections.push_back({&buffers, section.lod});
            }

            for (const auto& section : visibleGreedySections) {
                renderer.faceShader->setInt("sectionLod", section.lod);
                for (int faceType = 0; faceType < 6; ++faceType) {
                    int count = section.buffers->opaqueCounts[faceType];
                    if (count > 0 && section.buffers->opaqueVaos[faceType] != 0) {
                        renderer.faceShader->setInt("faceType", faceType);
                        renderBackend.bindVertexArray(section.buffers->opaqueVaos[faceType]);
                        renderBackend.drawArraysTrianglesInstanced(0, 6, count);
                    }
                }
            }

            setDepthWriteEnabled(false);
            if (twoSidedAlphaFaces) {
                setCullEnabled(false);
            } else {
                setCullBackFaceCCWEnabled(true);
            }
            for (const auto& section : visibleGreedySections) {
                renderer.faceShader->setInt("sectionLod", section.lod);
                for (int faceType = 0; faceType < 6; ++faceType) {
                    int count = section.buffers->alphaCounts[faceType];
                    if (count > 0 && section.buffers->alphaVaos[faceType] != 0) {
                        renderer.faceShader->setInt("faceType", faceType);
                        renderBackend.bindVertexArray(section.buffers->alphaVaos[faceType]);
                        renderBackend.drawArraysTrianglesInstanced(0, 6, count);
                    }
                }
            }
            setDepthWriteEnabled(true);
            setCullEnabled(false);
        }

        // Ensure block-damage masking does not leak into unrelated face/block draws
        // that may run later in this frame or in other systems.
        if (renderer.faceShader) {
            renderer.faceShader->use();
            BlockChargeSystemLogic::ApplyBlockDamageMaskUniforms(baseSystem, prototypes, *renderer.faceShader, false);
        }
        if (renderer.blockShader) {
            renderer.blockShader->use();
            BlockChargeSystemLogic::ApplyBlockDamageMaskUniforms(baseSystem, prototypes, *renderer.blockShader, false);
        }

        if (!debugSlopeInstances.empty() && renderer.faceShader && renderer.faceVAO) {
            renderer.faceShader->use();
            renderer.faceShader->setMat4("view", view);
            renderer.faceShader->setMat4("projection", projection);
            renderer.faceShader->setMat4("model", glm::mat4(1.0f));
            renderer.faceShader->setVec3("cameraPos", playerPos);
            renderer.faceShader->setFloat("time", time);
            renderer.faceShader->setVec3("lightDir", lightDir);
            renderer.faceShader->setVec3("ambientLight", ambientLightColor);
            renderer.faceShader->setVec3("diffuseLight", diffuseLightColor);
            renderer.faceShader->setInt("faceType", 0);
            renderer.faceShader->setInt("sectionLod", 0);
            renderer.faceShader->setInt("leafOpaqueOutsideLod0", leafOpaqueOutsideLod0 ? 1 : 0);
            renderer.faceShader->setInt("leafBackfacesWhenInside", 0);
            renderer.faceShader->setInt("foliageWindEnabled", foliageWindAnimationEnabled ? 1 : 0);
            renderer.faceShader->setInt("waterCascadeBrightnessEnabled", waterCascadeBrightnessEnabled ? 1 : 0);
            renderer.faceShader->setFloat("waterCascadeBrightnessStrength", waterCascadeBrightnessStrength);
            renderer.faceShader->setFloat("waterCascadeBrightnessSpeed", waterCascadeBrightnessSpeed);
            renderer.faceShader->setFloat("waterCascadeBrightnessScale", waterCascadeBrightnessScale);
            renderer.faceShader->setInt("wireframeDebug", 0);
            bindFaceTextureUniforms(*renderer.faceShader);
            BlockChargeSystemLogic::ApplyBlockDamageMaskUniforms(baseSystem, prototypes, *renderer.faceShader, false);

            setCullBackFaceCCWEnabled(true);
            renderBackend.bindVertexArray(renderer.faceVAO);

            auto drawSlopeFace = [&](const Entity& proto,
                                     int faceType,
                                     const glm::mat4& modelMat,
                                     const glm::vec3& facePosition,
                                     const glm::vec2& faceScale,
                                     const glm::vec2& faceUvScale,
                                     float faceAlpha,
                                     const glm::vec3& tintColor) {
                FaceInstanceRenderData face;
                face.position = facePosition;
                face.color = tintColor;
                int tile = RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), proto, faceType);
                face.tileIndex = tile;
                face.alpha = faceAlpha;
                face.ao = glm::vec4(1.0f);
                face.scale = faceScale;
                face.uvScale = faceUvScale;
                renderer.faceShader->setMat4("model", modelMat);
                renderer.faceShader->setInt("faceType", faceType);
                renderBackend.uploadArrayBufferData(renderer.faceInstanceVBO, &face, sizeof(FaceInstanceRenderData), true);
                renderBackend.drawArraysTrianglesInstanced(0, 6, 1);
            };

            for (const auto& slopeInst : debugSlopeInstances) {
                if (slopeInst.prototypeID < 0 || slopeInst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const Entity& slopeProto = prototypes[slopeInst.prototypeID];
                glm::mat4 baseModel = glm::translate(glm::mat4(1.0f), slopeInst.position);

                constexpr float kSlopeCapA = -4.0f;
                constexpr float kSlopeCapB = -5.0f;
                constexpr float kSlopeTopPosX = -6.0f;
                constexpr float kSlopeTopNegX = -7.0f;
                constexpr float kSlopeTopPosZ = -8.0f;
                constexpr float kSlopeTopNegZ = -9.0f;

                auto faceCenterOffset = [](int faceType) -> glm::vec3 {
                    switch (faceType) {
                        case 0: return glm::vec3(0.5f, 0.0f, 0.0f);
                        case 1: return glm::vec3(-0.5f, 0.0f, 0.0f);
                        case 2: return glm::vec3(0.0f, 0.5f, 0.0f);
                        case 3: return glm::vec3(0.0f, -0.5f, 0.0f);
                        case 4: return glm::vec3(0.0f, 0.0f, 0.5f);
                        case 5: return glm::vec3(0.0f, 0.0f, -0.5f);
                        default: return glm::vec3(0.0f);
                    }
                };

                // Bottom face.
                drawSlopeFace(slopeProto, 3, baseModel, faceCenterOffset(3), glm::vec2(1.0f), glm::vec2(1.0f), 1.0f, slopeInst.color);

                int tallFaceType = 0;
                int capFaceA = 4;
                int capFaceB = 5;
                float capAlphaA = kSlopeCapA;
                float capAlphaB = kSlopeCapB;
                float topAlpha = kSlopeTopPosX;
                switch (slopeInst.dir) {
                    case DebugSlopeDir::PosX:
                        tallFaceType = 0;
                        capFaceA = 4; capAlphaA = kSlopeCapA;
                        capFaceB = 5; capAlphaB = kSlopeCapB;
                        topAlpha = kSlopeTopPosX;
                        break;
                    case DebugSlopeDir::NegX:
                        tallFaceType = 1;
                        capFaceA = 4; capAlphaA = kSlopeCapB;
                        capFaceB = 5; capAlphaB = kSlopeCapA;
                        topAlpha = kSlopeTopNegX;
                        break;
                    case DebugSlopeDir::PosZ:
                        tallFaceType = 4;
                        capFaceA = 0; capAlphaA = kSlopeCapB;
                        capFaceB = 1; capAlphaB = kSlopeCapA;
                        topAlpha = kSlopeTopPosZ;
                        break;
                    case DebugSlopeDir::NegZ:
                        tallFaceType = 5;
                        capFaceA = 0; capAlphaA = kSlopeCapA;
                        capFaceB = 1; capAlphaB = kSlopeCapB;
                        topAlpha = kSlopeTopNegZ;
                        break;
                }

                // Tall side.
                drawSlopeFace(slopeProto, tallFaceType, baseModel, faceCenterOffset(tallFaceType), glm::vec2(1.0f), glm::vec2(1.0f), 1.0f, slopeInst.color);
                // Triangular side caps.
                drawSlopeFace(slopeProto, capFaceA, baseModel, faceCenterOffset(capFaceA), glm::vec2(1.0f), glm::vec2(1.0f), capAlphaA, slopeInst.color);
                drawSlopeFace(slopeProto, capFaceB, baseModel, faceCenterOffset(capFaceB), glm::vec2(1.0f), glm::vec2(1.0f), capAlphaB, slopeInst.color);
                // Sloped top.
                drawSlopeFace(slopeProto, 2, baseModel, faceCenterOffset(2), glm::vec2(1.0f), glm::vec2(1.0f), topAlpha, slopeInst.color);
            }

            setCullEnabled(false);
            renderer.faceShader->setMat4("model", glm::mat4(1.0f));
        }

        auto renderHeldItemForCurrentState = [&](bool renderLeftHand, bool& renderedAny) {
            if (mapViewActive || !player.isHoldingBlock || player.heldPrototypeID < 0) return;
            const float heldItemForward = glm::clamp(
                getRegistryFloat(baseSystem, "HeldItemViewForward", 0.58f),
                0.2f,
                2.5f
            );
            const float heldItemVertical = glm::clamp(
                getRegistryFloat(baseSystem, "HeldItemViewVertical", -0.24f),
                -1.0f,
                1.0f
            );
            const float heldItemSide = glm::clamp(
                getRegistryFloat(baseSystem, "HeldItemViewSide", 0.34f),
                0.0f,
                1.5f
            );
            glm::vec3 cameraRight = glm::cross(cameraForward, glm::vec3(0.0f, 1.0f, 0.0f));
            if (glm::length(cameraRight) < 1e-4f) {
                cameraRight = glm::vec3(1.0f, 0.0f, 0.0f);
            } else {
                cameraRight = glm::normalize(cameraRight);
            }
            glm::vec3 cameraUp = glm::cross(cameraRight, cameraForward);
            if (glm::length(cameraUp) < 1e-4f) {
                cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
            } else {
                cameraUp = glm::normalize(cameraUp);
            }
            const float handSideSign = renderLeftHand ? -1.0f : 1.0f;
            glm::vec3 heldPos(0.0f);
            const bool heldStickMode = getRegistryBool(baseSystem, "HeldItemStickMode", true);
            if (heldStickMode) {
                const float stickLength = glm::clamp(
                    getRegistryFloat(baseSystem, "HeldItemStickLength", 0.64f),
                    0.2f,
                    2.5f
                );
                const float stickYawDeg = glm::clamp(
                    getRegistryFloat(baseSystem, "HeldItemStickYawDeg", 32.0f),
                    -89.0f,
                    89.0f
                );
                const float stickPitchDeg = glm::clamp(
                    getRegistryFloat(baseSystem, "HeldItemStickPitchDeg", -16.0f),
                    -89.0f,
                    89.0f
                );
                const float yawRad = glm::radians(stickYawDeg * handSideSign);
                const float pitchRad = glm::radians(stickPitchDeg);
                const float cosPitch = std::cos(pitchRad);
                glm::vec3 stickDir = cameraForward * (cosPitch * std::cos(yawRad))
                    + cameraRight * (cosPitch * std::sin(yawRad))
                    + cameraUp * std::sin(pitchRad);
                if (glm::length(stickDir) < 1e-4f) {
                    stickDir = cameraForward;
                } else {
                    stickDir = glm::normalize(stickDir);
                }
                heldPos = player.cameraPosition + stickDir * stickLength;
            } else {
                heldPos = player.cameraPosition
                    + cameraForward * heldItemForward
                    + cameraUp * heldItemVertical
                    + cameraRight * (heldItemSide * handSideSign);
            }
            if (baseSystem.gamemode == "survival"
                && !player.isometricMapMode
                && !player.topDownMapMode
                && getRegistryBool(baseSystem, "WalkViewBobbingEnabled", true)
                && getRegistryBool(baseSystem, "HeldItemViewBobbingEnabled", true)) {
                const float bobWeight = glm::clamp(player.viewBobWeight, 0.0f, 1.0f);
                if (bobWeight > 1e-4f) {
                    const float sideWave = std::sin(player.viewBobPhase);
                    const float verticalWave = std::sin(player.viewBobPhase - 0.6f);
                    const float bobLateralAmp = glm::clamp(
                        getRegistryFloat(baseSystem, "WalkViewBobLateralAmplitude", 0.030f),
                        0.0f,
                        0.25f
                    ) * glm::clamp(getRegistryFloat(baseSystem, "HeldItemViewBobLateralScale", 1.0f), -4.0f, 4.0f);
                    const float bobVerticalAmp = glm::clamp(
                        getRegistryFloat(baseSystem, "WalkViewBobVerticalAmplitude", 0.060f),
                        0.0f,
                        0.25f
                    ) * glm::clamp(getRegistryFloat(baseSystem, "HeldItemViewBobVerticalScale", 1.0f), -4.0f, 4.0f);
                    heldPos += cameraRight * (bobLateralAmp * sideWave * bobWeight)
                        + cameraUp * (bobVerticalAmp * verticalWave * bobWeight);
                }
            }
            float heldLightFactor = 1.0f;
            if (voxelLightingEnabled && baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const float voxelLightingStrength = glm::clamp(
                    getRegistryFloat(baseSystem, "VoxelLightingStrength", 1.0f),
                    0.0f,
                    1.0f
                );
                const float voxelLightingMinBrightness = glm::clamp(
                    getRegistryFloat(baseSystem, "VoxelLightingMinBrightness", 0.08f),
                    0.0f,
                    1.0f
                );
                const float voxelLightingGamma = glm::clamp(
                    getRegistryFloat(baseSystem, "VoxelLightingGamma", 1.35f),
                    0.25f,
                    4.0f
                );
                const glm::ivec3 lightCell = glm::ivec3(glm::round(heldPos));
                const uint8_t sky = baseSystem.voxelWorld->getSkyLightWorld(lightCell);
                const uint8_t block = baseSystem.voxelWorld->getBlockLightWorld(lightCell);
                const uint8_t level = static_cast<uint8_t>(std::max<int>(sky, block));
                const float normalized = glm::clamp(static_cast<float>(level) / 15.0f, 0.0f, 1.0f);
                const float curve = std::pow(normalized, voxelLightingGamma);
                const float factor = voxelLightingMinBrightness + (1.0f - voxelLightingMinBrightness) * curve;
                heldLightFactor = 1.0f + (factor - 1.0f) * voxelLightingStrength;
            }
            const glm::vec4 heldAo = glm::vec4(heldLightFactor);
            bool drewTextured = false;
            if (player.heldPrototypeID < static_cast<int>(prototypes.size())) {
                const Entity& heldProto = prototypes[player.heldPrototypeID];
                const bool heldIsLeaf = (heldProto.name == "Leaf");
                const bool heldIsTallGrass = (heldProto.name.rfind("GrassTuft", 0) == 0)
                    && (heldProto.name.rfind("GrassTuftShort", 0) != 0);
                const bool heldIsShortGrass = (heldProto.name.rfind("GrassTuftShort", 0) == 0);
                const bool heldIsFlower = (heldProto.name.rfind("Flower", 0) == 0);
                const bool heldIsCavePot = (heldProto.name == "StonePebbleCavePotTexX"
                    || heldProto.name == "StonePebbleCavePotTexZ");
                const bool heldIsPlant = heldIsTallGrass || heldIsShortGrass || heldIsFlower || heldIsCavePot;
                const bool heldIsStick = (heldProto.name == "StickTexX"
                    || heldProto.name == "StickTexZ"
                    || heldProto.name == "StickWinterTexX"
                    || heldProto.name == "StickWinterTexZ");
                const bool heldIsPetalPile = isPetalPileName(heldProto.name);
                const bool heldIsBook = BookSystemLogic::IsBookPrototypeName(heldProto.name);
                const bool heldIsWallStone = (heldProto.name == "WallStoneTexPosX"
                    || heldProto.name == "WallStoneTexNegX"
                    || heldProto.name == "WallStoneTexPosZ"
                    || heldProto.name == "WallStoneTexNegZ"
                    || heldProto.name == "WallBranchLongTexPosX"
                    || heldProto.name == "WallBranchLongTexNegX"
                    || heldProto.name == "WallBranchLongTexPosZ"
                    || heldProto.name == "WallBranchLongTexNegZ"
                    || heldProto.name == "WallBranchLongTipTexPosX"
                    || heldProto.name == "WallBranchLongTipTexNegX"
                    || heldProto.name == "WallBranchLongTipTexPosZ"
                    || heldProto.name == "WallBranchLongTipTexNegZ");
                const bool heldIsCeilingStone = (heldProto.name.rfind("CeilingStoneTex", 0) == 0);
                const bool heldIsSurfaceStonePebble = (isStonePebbleXName(heldProto.name)
                    || isStonePebbleZName(heldProto.name));
                const bool heldIsGrassCover = (isGrassCoverXName(heldProto.name)
                    || isGrassCoverZName(heldProto.name));
                const bool heldIsStonePebble = (isStonePebbleXName(heldProto.name)
                    || isStonePebbleZName(heldProto.name)
                    || heldIsWallStone
                    || heldIsCeilingStone);
                const bool heldIsNarrowProp = heldIsStick || heldIsStonePebble || heldIsGrassCover;
                const bool drawAsFace = heldProto.useTexture || heldIsLeaf || heldIsPlant;
                if (drawAsFace && renderer.faceShader && renderer.faceVAO) {
                    const glm::ivec3 heldSeedCell = player.heldHasSourceCell
                        ? player.heldSourceCell
                        : glm::ivec3(
                            player.heldPrototypeID * 37 + static_cast<int>((player.heldPackedColor >> 4u) & 0xffu),
                            player.heldPrototypeID * -23 + static_cast<int>((player.heldPackedColor >> 12u) & 0xffu),
                            player.heldPrototypeID * 53 + static_cast<int>((player.heldPackedColor >> 20u) & 0xffu)
                        );
                    static const std::array<glm::vec3, 6> kFaceOffsets = {
                        glm::vec3(0.5f, 0.0f, 0.0f),  glm::vec3(-0.5f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 0.5f, 0.0f),  glm::vec3(0.0f, -0.5f, 0.0f),
                        glm::vec3(0.0f, 0.0f, 0.5f),  glm::vec3(0.0f, 0.0f, -0.5f)
                    };
                    renderer.faceShader->use();
                    renderer.faceShader->setMat4("view", view);
                    renderer.faceShader->setMat4("projection", projection);
                    renderer.faceShader->setMat4("model", glm::mat4(1.0f));
                    renderer.faceShader->setVec3("cameraPos", playerPos);
                    renderer.faceShader->setFloat("time", time);
                    renderer.faceShader->setVec3("lightDir", lightDir);
                    renderer.faceShader->setVec3("ambientLight", ambientLightColor);
                    renderer.faceShader->setVec3("diffuseLight", diffuseLightColor);
                    renderer.faceShader->setInt("faceType", 0);
                    renderer.faceShader->setInt("sectionLod", 0);
                    renderer.faceShader->setInt("leafOpaqueOutsideLod0", leafOpaqueOutsideLod0 ? 1 : 0);
                    renderer.faceShader->setInt("leafBackfacesWhenInside", 0);
                    renderer.faceShader->setInt("foliageWindEnabled", foliageWindAnimationEnabled ? 1 : 0);
                    renderer.faceShader->setInt("waterCascadeBrightnessEnabled", waterCascadeBrightnessEnabled ? 1 : 0);
                    renderer.faceShader->setFloat("waterCascadeBrightnessStrength", waterCascadeBrightnessStrength);
                    renderer.faceShader->setFloat("waterCascadeBrightnessSpeed", waterCascadeBrightnessSpeed);
                    renderer.faceShader->setFloat("waterCascadeBrightnessScale", waterCascadeBrightnessScale);
                    renderer.faceShader->setInt("wireframeDebug", 0);
                    bindFaceTextureUniforms(*renderer.faceShader);
                    BlockChargeSystemLogic::ApplyBlockDamageMaskUniforms(baseSystem, prototypes, *renderer.faceShader, false);
                    setCullBackFaceCCWEnabled(true);
                    renderBackend.bindVertexArray(renderer.faceVAO);

                    if (heldIsBook) {
                        auto safeNormalize = [](const glm::vec3& v, const glm::vec3& fallback) -> glm::vec3 {
                            if (glm::length(v) < 1e-4f) return fallback;
                            return glm::normalize(v);
                        };
                        auto makeBasisModel = [&](const glm::vec3& center,
                                                  const glm::vec3& xAxis,
                                                  const glm::vec3& yAxis,
                                                  const glm::vec3& zAxis) -> glm::mat4 {
                            glm::mat4 m(1.0f);
                            m[0] = glm::vec4(xAxis, 0.0f);
                            m[1] = glm::vec4(yAxis, 0.0f);
                            m[2] = glm::vec4(zAxis, 0.0f);
                            m[3] = glm::vec4(center, 1.0f);
                            return m;
                        };
                        auto drawBookFace = [&](const glm::mat4& modelMat,
                                                int faceType,
                                                const glm::vec3& localCenter,
                                                const glm::vec2& faceScale,
                                                int tileIndex,
                                                const glm::vec3& tintColor,
                                                float faceAlpha = 1.0f) {
                            FaceInstanceRenderData heldFace;
                            heldFace.position = localCenter;
                            heldFace.color = tintColor;
                            heldFace.tileIndex = tileIndex;
                            heldFace.alpha = faceAlpha;
                            heldFace.ao = heldAo;
                            heldFace.scale = faceScale;
                            heldFace.uvScale = faceScale;
                            renderer.faceShader->setMat4("model", modelMat);
                            renderer.faceShader->setInt("faceType", faceType);
                            renderBackend.uploadArrayBufferData(renderer.faceInstanceVBO, &heldFace, sizeof(FaceInstanceRenderData), true);
                            renderBackend.drawArraysTrianglesInstanced(0, 6, 1);
                        };
                        auto drawBookCuboid = [&](const glm::mat4& modelMat,
                                                  const glm::vec3& halfExtents,
                                                  int tileIndex,
                                                  const glm::vec3& tintColor,
                                                  float faceAlpha = 1.0f) {
                            for (int faceType = 0; faceType < 6; ++faceType) {
                                const glm::vec3 normal = (faceType == 0) ? glm::vec3(1.0f, 0.0f, 0.0f)
                                    : (faceType == 1) ? glm::vec3(-1.0f, 0.0f, 0.0f)
                                    : (faceType == 2) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                    : (faceType == 3) ? glm::vec3(0.0f, -1.0f, 0.0f)
                                    : (faceType == 4) ? glm::vec3(0.0f, 0.0f, 1.0f)
                                    : glm::vec3(0.0f, 0.0f, -1.0f);
                                const float halfExtent = (faceType == 0 || faceType == 1) ? halfExtents.x
                                    : (faceType == 2 || faceType == 3) ? halfExtents.y
                                    : halfExtents.z;
                                glm::vec2 faceScale(1.0f);
                                if (faceType == 0 || faceType == 1) {
                                    faceScale = glm::vec2(halfExtents.z * 2.0f, halfExtents.y * 2.0f);
                                } else if (faceType == 2 || faceType == 3) {
                                    faceScale = glm::vec2(halfExtents.x * 2.0f, halfExtents.z * 2.0f);
                                } else {
                                    faceScale = glm::vec2(halfExtents.x * 2.0f, halfExtents.y * 2.0f);
                                }
                                drawBookFace(
                                    modelMat,
                                    faceType,
                                    normal * halfExtent,
                                    faceScale,
                                    tileIndex,
                                    tintColor,
                                    faceAlpha
                                );
                            }
                        };
                        auto drawBookTextRows = [&](const glm::mat4& modelMat,
                                                    const std::string& text,
                                                    float pageCenterZ,
                                                    float pageHalfDepth,
                                                    float jitterX) {
                            const std::string fontName = getRegistryString(
                                baseSystem,
                                "BookInspectFontName",
                                "AlegreyaSans-Regular.ttf"
                            );
                            const float fontPixelHeight = glm::clamp(
                                getRegistryFloat(baseSystem, "BookInspectFontPixelHeight", 28.0f),
                                10.0f,
                                72.0f
                            );
                            const int maxChars = std::clamp(
                                getRegistryInt(baseSystem, "BookInspectFontMaxCharsPerLine", 24),
                                8,
                                64
                            );
                            const int maxLines = std::clamp(
                                getRegistryInt(baseSystem, "BookInspectFontMaxLines", 11),
                                2,
                                32
                            );

                            struct BookTextBitmap {
                                std::vector<unsigned char> alpha;
                                int width = 0;
                                int height = 0;
                                int wrappedLines = 0;
                            };
                            static std::unordered_map<std::string, BookTextBitmap> s_bookTextBitmapCache;

                            const std::string cacheKey = fontName + "|" + std::to_string(static_cast<int>(std::round(fontPixelHeight)))
                                + "|" + std::to_string(maxChars)
                                + "|" + std::to_string(maxLines)
                                + "|" + text;
                            BookTextBitmap* bitmap = nullptr;
                            auto cacheIt = s_bookTextBitmapCache.find(cacheKey);
                            if (cacheIt == s_bookTextBitmapCache.end()) {
                                BookTextBitmap generated;
                                if (FontSystemLogic::RasterizeBookTextBitmap(
                                    baseSystem,
                                    fontName,
                                    fontPixelHeight,
                                    text,
                                    maxChars,
                                    maxLines,
                                    generated.alpha,
                                    generated.width,
                                    generated.height,
                                    generated.wrappedLines
                                ) && generated.width > 0 && generated.height > 0 && !generated.alpha.empty()) {
                                    cacheIt = s_bookTextBitmapCache.emplace(cacheKey, std::move(generated)).first;
                                }
                            }
                            if (cacheIt != s_bookTextBitmapCache.end()) {
                                bitmap = &cacheIt->second;
                            }
                            if (!bitmap || bitmap->width <= 0 || bitmap->height <= 0 || bitmap->alpha.empty()) {
                                return;
                            }

                            const glm::vec3 inkColor(0.19f, 0.16f, 0.13f);
                            const float textSurfaceOffset = 0.0011f;
                            const float frontZ = pageCenterZ + pageHalfDepth + textSurfaceOffset;
                            const float backZ = pageCenterZ - pageHalfDepth - textSurfaceOffset;

                            const float textAreaWidth = 0.188f;
                            const float textAreaHeight = 0.300f;
                            const float leftX = (-textAreaWidth * 0.5f) + jitterX;
                            const float topY = textAreaHeight * 0.5f - 0.004f;
                            const float pixelW = textAreaWidth / static_cast<float>(bitmap->width);
                            const float pixelH = textAreaHeight / static_cast<float>(bitmap->height);
                            if (pixelW <= 0.0f || pixelH <= 0.0f) return;

                            std::vector<FaceInstanceRenderData> frontInstances;
                            std::vector<FaceInstanceRenderData> backInstances;
                            const size_t reserveHint = static_cast<size_t>(bitmap->height) * 8u;
                            frontInstances.reserve(reserveHint);
                            backInstances.reserve(reserveHint);

                            for (int py = 0; py < bitmap->height; ++py) {
                                int runStart = -1;
                                for (int px = 0; px <= bitmap->width; ++px) {
                                    const bool filled = (px < bitmap->width)
                                        && (bitmap->alpha[static_cast<size_t>(py * bitmap->width + px)] > 48u);
                                    if (filled && runStart < 0) {
                                        runStart = px;
                                    }
                                    if ((!filled || px == bitmap->width) && runStart >= 0) {
                                        const int runEnd = px;
                                        const float runX0 = leftX + static_cast<float>(runStart) * pixelW;
                                        const float runX1 = leftX + static_cast<float>(runEnd) * pixelW;
                                        const float cx = (runX0 + runX1) * 0.5f;
                                        const float cy = topY - (static_cast<float>(py) + 0.5f) * pixelH;
                                        const glm::vec2 runScale(
                                            std::max(0.002f, (runX1 - runX0) * 0.94f),
                                            std::max(0.002f, pixelH * 0.92f)
                                        );
                                        FaceInstanceRenderData front{};
                                        front.position = glm::vec3(cx, cy, frontZ);
                                        front.color = inkColor;
                                        front.tileIndex = -1;
                                        front.alpha = -40.0f;
                                        front.ao = heldAo;
                                        front.scale = runScale;
                                        front.uvScale = runScale;
                                        frontInstances.push_back(front);

                                        FaceInstanceRenderData back = front;
                                        back.position.z = backZ;
                                        backInstances.push_back(back);
                                        runStart = -1;
                                    }
                                }
                            }

                            renderer.faceShader->setMat4("model", modelMat);
                            if (!frontInstances.empty()) {
                                renderer.faceShader->setInt("faceType", 4);
                                renderBackend.uploadArrayBufferData(
                                    renderer.faceInstanceVBO,
                                    frontInstances.data(),
                                    frontInstances.size() * sizeof(FaceInstanceRenderData),
                                    true
                                );
                                renderBackend.drawArraysTrianglesInstanced(0, 6, static_cast<int>(frontInstances.size()));
                            }
                            if (!backInstances.empty()) {
                                renderer.faceShader->setInt("faceType", 5);
                                renderBackend.uploadArrayBufferData(
                                    renderer.faceInstanceVBO,
                                    backInstances.data(),
                                    backInstances.size() * sizeof(FaceInstanceRenderData),
                                    true
                                );
                                renderBackend.drawArraysTrianglesInstanced(0, 6, static_cast<int>(backInstances.size()));
                            }
                        };
                        auto rotateAroundUp = [&](const glm::vec3& axisX,
                                                  const glm::vec3& axisY,
                                                  const glm::vec3& axisZ,
                                                  float radians,
                                                  glm::vec3& outX,
                                                  glm::vec3& outY,
                                                  glm::vec3& outZ) {
                            const float c = std::cos(radians);
                            const float s = std::sin(radians);
                            outX = safeNormalize(axisX * c + axisZ * s, axisX);
                            outY = axisY;
                            outZ = safeNormalize(-axisX * s + axisZ * c, axisZ);
                        };

                        const float inspectForward = glm::clamp(
                            getRegistryFloat(baseSystem, "BookInspectViewForward", 0.30f),
                            0.10f,
                            1.4f
                        );
                        const float inspectActiveForward = glm::clamp(
                            getRegistryFloat(baseSystem, "BookInspectActiveViewForward", 0.12f),
                            0.05f,
                            1.4f
                        );
                        const float inspectVertical = glm::clamp(
                            getRegistryFloat(baseSystem, "BookInspectViewVertical", -0.03f),
                            -0.8f,
                            0.8f
                        );
                        const float inspectSide = glm::clamp(
                            getRegistryFloat(baseSystem, "BookInspectViewSide", 0.04f),
                            -0.5f,
                            0.5f
                        );
                        const int maxSpread = std::max(1, getRegistryInt(baseSystem, "BookInspectMaxSpread", 8));
                        const int inspectPage = std::clamp(player.bookInspectPage, 0, maxSpread);
                        const bool inspectReadingActive = player.bookInspectActive && inspectPage > 0;
                        const float openAngle = glm::clamp(
                            getRegistryFloat(baseSystem, "BookInspectOpenAngle", 0.0f),
                            0.0f,
                            1.70f
                        );

                        const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
                        const glm::vec3 inspectRight = cameraRight;
                        const glm::vec3 inspectUp = cameraUp;
                        const glm::vec3 inspectForwardAxis = -cameraForward;
                        const float inspectForwardDistance = player.bookInspectActive
                            ? inspectActiveForward
                            : inspectForward;
                        glm::vec3 inspectCenter = player.cameraPosition
                            + cameraForward * inspectForwardDistance
                            + inspectUp * inspectVertical
                            + inspectRight * (inspectSide * handSideSign);

                        glm::vec3 flatForward(cameraForward.x, 0.0f, cameraForward.z);
                        if (glm::length(flatForward) < 1e-4f) {
                            flatForward = glm::vec3(0.0f, 0.0f, -1.0f);
                        } else {
                            flatForward = glm::normalize(flatForward);
                        }
                        glm::vec3 flatRight = glm::cross(flatForward, worldUp);
                        if (glm::length(flatRight) < 1e-4f) {
                            flatRight = cameraRight;
                        } else {
                            flatRight = glm::normalize(flatRight);
                        }
                        const float heldBookForwardOffset = glm::clamp(
                            getRegistryFloat(baseSystem, "BookHeldViewForward", 0.00f),
                            -0.8f,
                            0.8f
                        );
                        const float heldBookVerticalOffset = glm::clamp(
                            getRegistryFloat(baseSystem, "BookHeldViewVertical", 0.00f),
                            -0.8f,
                            0.8f
                        );
                        const float heldBookSideOffset = glm::clamp(
                            getRegistryFloat(baseSystem, "BookHeldViewSide", 0.00f),
                            -0.8f,
                            0.8f
                        );
                        const float heldBookYawDeg = glm::clamp(
                            getRegistryFloat(baseSystem, "BookHeldYawDeg", 0.0f),
                            -80.0f,
                            80.0f
                        );
                        glm::vec3 heldBookX(0.0f), heldBookY(0.0f), heldBookZ(0.0f);
                        rotateAroundUp(
                            flatRight,
                            worldUp,
                            -flatForward,
                            glm::radians(heldBookYawDeg * handSideSign),
                            heldBookX,
                            heldBookY,
                            heldBookZ
                        );
                        const glm::vec3 heldBookCenter = heldPos
                            + flatForward * heldBookForwardOffset
                            + worldUp * heldBookVerticalOffset
                            + flatRight * (heldBookSideOffset * handSideSign);

                        const glm::vec3 baseRight = inspectReadingActive ? inspectRight : heldBookX;
                        const glm::vec3 baseUp = inspectReadingActive ? inspectUp : heldBookY;
                        const glm::vec3 baseForwardAxis = inspectReadingActive ? inspectForwardAxis : heldBookZ;
                        const glm::vec3 baseCenter = inspectReadingActive ? inspectCenter : heldBookCenter;

                        const int coverTile = RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), heldProto, 2);
                        const int coverTileIndex = coverTile >= 0 ? coverTile : -1;
                        const int pageTileIndex = 56;
                        const float bookPageAlphaTag = -40.0f;
                        const glm::vec3 coverTint = coverTile >= 0 ? glm::vec3(1.0f) : glm::vec3(0.30f, 0.23f, 0.18f);
                        const glm::vec3 pageTint(1.0f, 1.0f, 1.0f);
                        const glm::vec3 spineTint(0.22f, 0.16f, 0.13f);

                        const glm::mat4 inspectBaseModel = makeBasisModel(
                            baseCenter,
                            baseRight,
                            baseUp,
                            baseForwardAxis
                        );

                        if (!inspectReadingActive) {
                            constexpr float kPxHalf = 1.0f / 48.0f;
                            constexpr float kBinderHalf = 1.0f * kPxHalf;
                            constexpr float kPageHalf = 2.0f * kPxHalf;
                            const float layerCenterA = -5.0f * kPxHalf; // binder
                            const float layerCenterB = -2.0f * kPxHalf; // page
                            const float layerCenterC =  2.0f * kPxHalf; // page
                            const float layerCenterD =  5.0f * kPxHalf; // binder

                            drawBookCuboid(
                                glm::translate(inspectBaseModel, glm::vec3(0.0f, 0.0f, layerCenterA)),
                                glm::vec3(0.182f, 0.224f, kBinderHalf),
                                coverTileIndex,
                                coverTint,
                                1.0f
                            );
                            drawBookCuboid(
                                glm::translate(inspectBaseModel, glm::vec3(0.0f, 0.0f, layerCenterB)),
                                glm::vec3(0.172f, 0.212f, kPageHalf),
                                pageTileIndex,
                                pageTint,
                                bookPageAlphaTag
                            );
                            drawBookCuboid(
                                glm::translate(inspectBaseModel, glm::vec3(0.0f, 0.0f, layerCenterC)),
                                glm::vec3(0.172f, 0.212f, kPageHalf),
                                pageTileIndex,
                                pageTint,
                                bookPageAlphaTag
                            );
                            drawBookCuboid(
                                glm::translate(inspectBaseModel, glm::vec3(0.0f, 0.0f, layerCenterD)),
                                glm::vec3(0.182f, 0.224f, kBinderHalf),
                                coverTileIndex,
                                coverTint,
                                1.0f
                            );
                        } else {
                            drawBookCuboid(inspectBaseModel, glm::vec3(0.015f, 0.220f, 0.033f), -1, spineTint, 1.0f);

                            glm::vec3 leftX(0.0f), leftY(0.0f), leftZ(0.0f);
                            glm::vec3 rightX(0.0f), rightY(0.0f), rightZ(0.0f);
                            rotateAroundUp(baseRight, baseUp, baseForwardAxis, -openAngle, leftX, leftY, leftZ);
                            rotateAroundUp(baseRight, baseUp, baseForwardAxis, openAngle, rightX, rightY, rightZ);

                            const glm::vec3 leftCenter = baseCenter - baseRight * 0.122f + baseForwardAxis * 0.006f;
                            const glm::vec3 rightCenter = baseCenter + baseRight * 0.122f + baseForwardAxis * 0.006f;
                            const glm::mat4 leftModel = makeBasisModel(leftCenter, leftX, leftY, leftZ);
                            const glm::mat4 rightModel = makeBasisModel(rightCenter, rightX, rightY, rightZ);

                            constexpr float kOpenPxHalf = 1.0f / 48.0f;
                            constexpr float kPageCenterZ = 1.5f * kOpenPxHalf;
                            drawBookCuboid(
                                glm::translate(leftModel, glm::vec3(0.0f, 0.0f, -kPageCenterZ)),
                                glm::vec3(0.126f, 0.206f, kOpenPxHalf),
                                coverTileIndex,
                                coverTint,
                                1.0f
                            );
                            drawBookCuboid(
                                glm::translate(rightModel, glm::vec3(0.0f, 0.0f, -kPageCenterZ)),
                                glm::vec3(0.126f, 0.206f, kOpenPxHalf),
                                coverTileIndex,
                                coverTint,
                                1.0f
                            );
                            drawBookCuboid(
                                glm::translate(leftModel, glm::vec3(0.0f, 0.0f, kPageCenterZ)),
                                glm::vec3(0.118f, 0.196f, kOpenPxHalf),
                                pageTileIndex,
                                pageTint,
                                bookPageAlphaTag
                            );
                            drawBookCuboid(
                                glm::translate(rightModel, glm::vec3(0.0f, 0.0f, kPageCenterZ)),
                                glm::vec3(0.118f, 0.196f, kOpenPxHalf),
                                pageTileIndex,
                                pageTint,
                                bookPageAlphaTag
                            );

                            const std::string pageText = BookSystemLogic::ResolveBookPageText(inspectPage);
                            drawBookTextRows(leftModel, pageText, kPageCenterZ, kOpenPxHalf, 0.000f);
                            drawBookTextRows(rightModel, pageText, kPageCenterZ, kOpenPxHalf, 0.002f);
                        }

                        setCullEnabled(false);
                        renderer.faceShader->setMat4("model", glm::mat4(1.0f));
                        drewTextured = true;
                    } else
                    // Match in-world petal-pile profile while held: a thin floor mat, not crossed foliage cards.
                    if (heldIsPetalPile) {
                        constexpr float kHalf1 = 1.0f / 48.0f;
                        constexpr float kHalf24 = 24.0f / 48.0f;
                        const glm::vec3 petalHalfExtents(kHalf24, kHalf1, kHalf24);
                        const float petalCenterYOffset = glm::clamp(
                            getRegistryFloat(baseSystem, "HeldPetalPileViewVertical", -0.02f),
                            -1.0f,
                            1.0f
                        );
                        const glm::vec3 petalCenter = heldPos + glm::vec3(0.0f, petalCenterYOffset, 0.0f);

                        for (int faceType = 0; faceType < 6; ++faceType) {
                            const glm::vec3 normal = (faceType == 0) ? glm::vec3(1.0f, 0.0f, 0.0f)
                                : (faceType == 1) ? glm::vec3(-1.0f, 0.0f, 0.0f)
                                : (faceType == 2) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                : (faceType == 3) ? glm::vec3(0.0f, -1.0f, 0.0f)
                                : (faceType == 4) ? glm::vec3(0.0f, 0.0f, 1.0f)
                                : glm::vec3(0.0f, 0.0f, -1.0f);

                            float halfExtent = 0.5f;
                            if (faceType == 0 || faceType == 1) halfExtent = petalHalfExtents.x;
                            else if (faceType == 2 || faceType == 3) halfExtent = petalHalfExtents.y;
                            else if (faceType == 4 || faceType == 5) halfExtent = petalHalfExtents.z;

                            float uScale = 1.0f;
                            float vScale = 1.0f;
                            if (faceType == 0 || faceType == 1) {
                                uScale = petalHalfExtents.z * 2.0f;
                                vScale = petalHalfExtents.y * 2.0f;
                            } else if (faceType == 2 || faceType == 3) {
                                uScale = petalHalfExtents.x * 2.0f;
                                vScale = petalHalfExtents.z * 2.0f;
                            } else if (faceType == 4 || faceType == 5) {
                                uScale = petalHalfExtents.x * 2.0f;
                                vScale = petalHalfExtents.y * 2.0f;
                            }

                            FaceInstanceRenderData heldFace;
                            heldFace.position = petalCenter + normal * halfExtent;
                            heldFace.tileIndex = RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), heldProto, faceType);
                            heldFace.color = (heldFace.tileIndex >= 0) ? glm::vec3(1.0f) : player.heldBlockColor;
                            heldFace.alpha = 1.0f;
                            heldFace.ao = heldAo;
                            heldFace.scale = glm::vec2(uScale, vScale);
                            heldFace.uvScale = heldFace.scale;
                            renderer.faceShader->setInt("faceType", faceType);
                            renderBackend.uploadArrayBufferData(renderer.faceInstanceVBO, &heldFace, sizeof(FaceInstanceRenderData), true);
                            renderBackend.drawArraysTrianglesInstanced(0, 6, 1);
                        }
                        setCullEnabled(false);
                        drewTextured = true;
                    } else if (heldIsPlant) {
                        static const std::array<int, 4> kPlantFaces = {0, 1, 4, 5};
                        float alphaMode = -2.0f;
                        if (heldIsCavePot) alphaMode = -10.0f;
                        else if (heldIsFlower) alphaMode = -3.0f;
                        else if (heldIsShortGrass) alphaMode = -2.3f;
                        const int resolvedHeldPlantTile = RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), heldProto, 2);
                        const int heldPlantBaseTile = (heldIsFlower && resolvedHeldPlantTile < 0)
                            ? -1
                            : resolvedHeldPlantTile;
                        const glm::vec3 heldPlantTint = (heldPlantBaseTile >= 0) ? glm::vec3(1.0f) : player.heldBlockColor;
                        glm::vec2 plantScale = glm::vec2(1.0f);
                        if (heldIsFlower && heldPlantBaseTile < 0) plantScale = glm::vec2(0.86f, 0.92f);
                        for (int faceType : kPlantFaces) {
                            FaceInstanceRenderData heldFace;
                            heldFace.position = heldPos;
                            heldFace.color = heldPlantTint;
                            heldFace.tileIndex = heldPlantBaseTile;
                            heldFace.alpha = alphaMode;
                            heldFace.ao = heldAo;
                            heldFace.scale = plantScale;
                            heldFace.uvScale = glm::vec2(1.0f);
                            renderer.faceShader->setInt("faceType", faceType);
                            renderBackend.uploadArrayBufferData(renderer.faceInstanceVBO, &heldFace, sizeof(FaceInstanceRenderData), true);
                            renderBackend.drawArraysTrianglesInstanced(0, 6, 1);
                        }
                        setCullEnabled(false);
                        drewTextured = true;
                    } else if (heldIsStonePebble && heldIsSurfaceStonePebble) {
                        const int pileCount = decodeSurfaceStonePileCount(player.heldPackedColor);
                        const StonePebblePilePieces pile = stonePebblePilePiecesForCell(heldSeedCell, pileCount);
                        for (int piece = 0; piece < pile.count; ++piece) {
                            const glm::vec2 pieceOffset = pile.offsets[static_cast<size_t>(piece)];
                            const NarrowHalfExtents pieceExt = pile.halfExtents[static_cast<size_t>(piece)];
                            glm::vec3 pieceCenter = heldPos;
                            pieceCenter.x += pieceOffset.x;
                            pieceCenter.z += pieceOffset.y;
                            pieceCenter.y += (-0.5f + pieceExt.y + 0.01f);
                            for (int faceType = 0; faceType < 6; ++faceType) {
                                const glm::vec3 normal = (faceType == 0) ? glm::vec3(1.0f, 0.0f, 0.0f)
                                    : (faceType == 1) ? glm::vec3(-1.0f, 0.0f, 0.0f)
                                    : (faceType == 2) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                    : (faceType == 3) ? glm::vec3(0.0f, -1.0f, 0.0f)
                                    : (faceType == 4) ? glm::vec3(0.0f, 0.0f, 1.0f)
                                    : glm::vec3(0.0f, 0.0f, -1.0f);
                                float halfExtent = 0.5f;
                                if (faceType == 0 || faceType == 1) halfExtent = pieceExt.x;
                                else if (faceType == 2 || faceType == 3) halfExtent = pieceExt.y;
                                else if (faceType == 4 || faceType == 5) halfExtent = pieceExt.z;
                                glm::vec2 faceScale(1.0f);
                                if (faceType == 0 || faceType == 1) {
                                    faceScale = glm::vec2(pieceExt.z * 2.0f, pieceExt.y * 2.0f);
                                } else if (faceType == 2 || faceType == 3) {
                                    faceScale = glm::vec2(pieceExt.x * 2.0f, pieceExt.z * 2.0f);
                                } else {
                                    faceScale = glm::vec2(pieceExt.x * 2.0f, pieceExt.y * 2.0f);
                                }

                                FaceInstanceRenderData heldFace;
                                heldFace.position = pieceCenter + normal * halfExtent;
                                heldFace.tileIndex = RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), heldProto, faceType);
                                heldFace.color = (heldFace.tileIndex >= 0) ? glm::vec3(1.0f) : player.heldBlockColor;
                                heldFace.alpha = 1.0f;
                                heldFace.ao = heldAo;
                                heldFace.scale = faceScale;
                                heldFace.uvScale = faceScale;
                                renderer.faceShader->setInt("faceType", faceType);
                                renderBackend.uploadArrayBufferData(renderer.faceInstanceVBO, &heldFace, sizeof(FaceInstanceRenderData), true);
                                renderBackend.drawArraysTrianglesInstanced(0, 6, 1);
                            }
                        }
                        setCullEnabled(false);
                        drewTextured = true;
                    } else if (heldIsGrassCover) {
                        constexpr float kDotHalf = 1.0f / 48.0f;
                        constexpr float kGrassCoverAlpha = -14.0f;
                        const GrassCoverDots dots = grassCoverDotsForCell(heldSeedCell);
                        for (int dot = 0; dot < dots.count; ++dot) {
                            const glm::vec2 offset = dots.offsets[static_cast<size_t>(dot)];
                            glm::vec3 dotCenter = heldPos;
                            dotCenter.x += offset.x;
                            dotCenter.y += (-0.5f + kDotHalf + 0.01f);
                            dotCenter.z += offset.y;
                            for (int faceType = 0; faceType < 6; ++faceType) {
                                const glm::vec3 normal = (faceType == 0) ? glm::vec3(1.0f, 0.0f, 0.0f)
                                    : (faceType == 1) ? glm::vec3(-1.0f, 0.0f, 0.0f)
                                    : (faceType == 2) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                    : (faceType == 3) ? glm::vec3(0.0f, -1.0f, 0.0f)
                                    : (faceType == 4) ? glm::vec3(0.0f, 0.0f, 1.0f)
                                    : glm::vec3(0.0f, 0.0f, -1.0f);
                                FaceInstanceRenderData heldFace;
                                heldFace.position = dotCenter + normal * kDotHalf;
                                heldFace.tileIndex = RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), heldProto, faceType);
                                heldFace.color = (heldFace.tileIndex >= 0) ? glm::vec3(1.0f) : player.heldBlockColor;
                                heldFace.alpha = kGrassCoverAlpha;
                                heldFace.ao = heldAo;
                                heldFace.scale = glm::vec2(kDotHalf * 2.0f);
                                heldFace.uvScale = heldFace.scale;
                                renderer.faceShader->setInt("faceType", faceType);
                                renderBackend.uploadArrayBufferData(renderer.faceInstanceVBO, &heldFace, sizeof(FaceInstanceRenderData), true);
                                renderBackend.drawArraysTrianglesInstanced(0, 6, 1);
                            }
                        }
                        setCullEnabled(false);
                        drewTextured = true;
                    } else {
                        const bool ceilingAlongX = (heldProto.name == "CeilingStoneTexX"
                            || heldProto.name == "CeilingStoneTexPosX"
                            || heldProto.name == "CeilingStoneTexNegX");
                        const bool ceilingAlongZ = (heldProto.name == "CeilingStoneTexZ"
                            || heldProto.name == "CeilingStoneTexPosZ"
                            || heldProto.name == "CeilingStoneTexNegZ");
                        const bool narrowAlongX = (heldProto.name == "StickTexX"
                            || heldProto.name == "StickWinterTexX"
                            || isGrassCoverXName(heldProto.name)
                            || isStonePebbleXName(heldProto.name)
                            || ceilingAlongX);
                        const bool narrowAlongZ = (heldProto.name == "StickTexZ"
                            || heldProto.name == "StickWinterTexZ"
                            || isGrassCoverZName(heldProto.name)
                            || isStonePebbleZName(heldProto.name)
                            || ceilingAlongZ);
                        const float half1 = 1.0f / 48.0f;
                        const float half2 = 2.0f / 48.0f;
                        const float half6 = 6.0f / 48.0f;
                        const float half12 = 12.0f / 48.0f;
                        glm::vec3 narrowHalfExtents(0.5f);
                        if (heldIsStick) {
                            narrowHalfExtents = narrowAlongX
                                ? glm::vec3(half12, half1, half1)
                                : (narrowAlongZ ? glm::vec3(half1, half1, half12) : glm::vec3(0.5f));
                        } else if (heldIsGrassCover) {
                            narrowHalfExtents = glm::vec3(0.5f, half1, 0.5f);
                        } else if (heldIsStonePebble) {
                            if (heldIsWallStone) {
                                // Wall-stone variant is a rotated pebble profile.
                                narrowHalfExtents = glm::vec3(half2, half6, half2);
                            } else {
                                narrowHalfExtents = narrowAlongX
                                    ? glm::vec3(half6, half2, half2)
                                    : (narrowAlongZ ? glm::vec3(half2, half2, half6) : glm::vec3(0.5f));
                            }
                        }
                        for (int faceType = 0; faceType < 6; ++faceType) {
                            glm::vec3 facePos = heldPos + kFaceOffsets[faceType];
                            glm::vec2 faceScale(1.0f);
                            glm::vec2 faceUvScale(1.0f);
                            if (heldIsNarrowProp) {
                                float halfExtent = 0.5f;
                                if (faceType == 0 || faceType == 1) halfExtent = narrowHalfExtents.x;
                                else if (faceType == 2 || faceType == 3) halfExtent = narrowHalfExtents.y;
                                else if (faceType == 4 || faceType == 5) halfExtent = narrowHalfExtents.z;
                                const glm::vec3 normal = (faceType == 0) ? glm::vec3(1.0f, 0.0f, 0.0f)
                                    : (faceType == 1) ? glm::vec3(-1.0f, 0.0f, 0.0f)
                                    : (faceType == 2) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                    : (faceType == 3) ? glm::vec3(0.0f, -1.0f, 0.0f)
                                    : (faceType == 4) ? glm::vec3(0.0f, 0.0f, 1.0f)
                                    : glm::vec3(0.0f, 0.0f, -1.0f);
                                facePos = heldPos + normal * halfExtent;

                                float uScale = 1.0f;
                                float vScale = 1.0f;
                                if (faceType == 0 || faceType == 1) {
                                    uScale = narrowHalfExtents.z * 2.0f;
                                    vScale = narrowHalfExtents.y * 2.0f;
                                } else if (faceType == 2 || faceType == 3) {
                                    uScale = narrowHalfExtents.x * 2.0f;
                                    vScale = narrowHalfExtents.z * 2.0f;
                                } else if (faceType == 4 || faceType == 5) {
                                    uScale = narrowHalfExtents.x * 2.0f;
                                    vScale = narrowHalfExtents.y * 2.0f;
                                }
                                faceScale = glm::vec2(uScale, vScale);
                                faceUvScale = faceScale;
                            }

                            FaceInstanceRenderData heldFace;
                            heldFace.position = facePos;
                            int heldTileIndex = heldIsLeaf ? -1 : RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), heldProto, faceType);
                            heldFace.color = (heldTileIndex >= 0) ? glm::vec3(1.0f) : player.heldBlockColor;
                            heldFace.tileIndex = heldTileIndex;
                            heldFace.alpha = heldIsLeaf ? -1.0f : 1.0f;
                            heldFace.ao = heldAo;
                            heldFace.scale = faceScale;
                            heldFace.uvScale = faceUvScale;
                            renderer.faceShader->setInt("faceType", faceType);
                            renderBackend.uploadArrayBufferData(renderer.faceInstanceVBO, &heldFace, sizeof(FaceInstanceRenderData), true);
                            renderBackend.drawArraysTrianglesInstanced(0, 6, 1);
                        }
                        setCullEnabled(false);
                        drewTextured = true;
                    }
                }
            }
            if (!drewTextured) {
                InstanceData heldInstance;
                heldInstance.position = heldPos;
                heldInstance.color = player.heldBlockColor * heldLightFactor;
                int behaviorIndex = static_cast<int>(RenderBehavior::STATIC_DEFAULT);
                renderer.blockShader->use();
                renderer.blockShader->setMat4("view", view);
                renderer.blockShader->setMat4("projection", projection);
                renderer.blockShader->setVec3("cameraPos", playerPos);
                renderer.blockShader->setFloat("time", time);
                renderer.blockShader->setFloat("instanceScale", 1.0f);
                renderer.blockShader->setVec3("lightDir", lightDir);
                renderer.blockShader->setVec3("ambientLight", ambientLightColor);
                renderer.blockShader->setVec3("diffuseLight", diffuseLightColor);
                renderer.blockShader->setInt("voxelGridLinesEnabled", voxelGridLinesEnabled ? 1 : 0);
                renderer.blockShader->setInt("voxelGridLineInvertColorEnabled", voxelGridLineInvertColorEnabled ? 1 : 0);
                renderer.blockShader->setMat4("model", glm::mat4(1.0f));
                renderer.blockShader->setInt("behaviorType", behaviorIndex);
                BlockChargeSystemLogic::ApplyBlockDamageMaskUniforms(baseSystem, prototypes, *renderer.blockShader, false);
                renderBackend.bindVertexArray(renderer.behaviorVAOs[behaviorIndex]);
                renderBackend.uploadArrayBufferData(renderer.behaviorInstanceVBOs[behaviorIndex], &heldInstance, sizeof(InstanceData), true);
                renderBackend.drawArraysTrianglesInstanced(0, 36, 1);
            }
            renderedAny = true;
        };

        if (!mapViewActive) {
            const bool savedHolding = player.isHoldingBlock;
            const int savedPrototypeID = player.heldPrototypeID;
            const glm::vec3 savedColor = player.heldBlockColor;
            const uint32_t savedPackedColor = player.heldPackedColor;
            const bool savedHasSourceCell = player.heldHasSourceCell;
            const glm::ivec3 savedSourceCell = player.heldSourceCell;

            bool renderedAnyHeld = false;

            player.isHoldingBlock = player.rightHandHoldingBlock;
            player.heldPrototypeID = player.rightHandHeldPrototypeID;
            player.heldBlockColor = player.rightHandHeldBlockColor;
            player.heldPackedColor = player.rightHandHeldPackedColor;
            player.heldHasSourceCell = player.rightHandHeldHasSourceCell;
            player.heldSourceCell = player.rightHandHeldSourceCell;
            renderHeldItemForCurrentState(false, renderedAnyHeld);

            player.isHoldingBlock = player.leftHandHoldingBlock;
            player.heldPrototypeID = player.leftHandHeldPrototypeID;
            player.heldBlockColor = player.leftHandHeldBlockColor;
            player.heldPackedColor = player.leftHandHeldPackedColor;
            player.heldHasSourceCell = player.leftHandHeldHasSourceCell;
            player.heldSourceCell = player.leftHandHeldSourceCell;
            renderHeldItemForCurrentState(true, renderedAnyHeld);

            if (!renderedAnyHeld && savedHolding && savedPrototypeID >= 0) {
                player.isHoldingBlock = savedHolding;
                player.heldPrototypeID = savedPrototypeID;
                player.heldBlockColor = savedColor;
                player.heldPackedColor = savedPackedColor;
                player.heldHasSourceCell = savedHasSourceCell;
                player.heldSourceCell = savedSourceCell;
                renderHeldItemForCurrentState(player.buildMode == BuildModeType::PickupLeft, renderedAnyHeld);
            }

            player.isHoldingBlock = savedHolding;
            player.heldPrototypeID = savedPrototypeID;
            player.heldBlockColor = savedColor;
            player.heldPackedColor = savedPackedColor;
            player.heldHasSourceCell = savedHasSourceCell;
            player.heldSourceCell = savedSourceCell;
        }

        const bool renderHeldPickaxe = player.pickaxeHeld;
        if (renderHeldPickaxe
            && renderer.faceShader
            && renderer.faceVAO
            && renderer.faceInstanceVBO) {
            const Entity* pickaxeStickProto = nullptr;
            std::array<const Entity*, 4> pickaxeHeadByKind = {nullptr, nullptr, nullptr, nullptr};
            const Entity* pickaxeHeadFallbackProto = nullptr;
            for (const auto& proto : prototypes) {
                if (!proto.useTexture) continue;
                if (!pickaxeStickProto && (proto.name == "StickTexX" || proto.name == "FirLog1Tex")) {
                    pickaxeStickProto = &proto;
                }
                if (!pickaxeHeadByKind[0] && proto.name == "StonePebbleRubyTexX") {
                    pickaxeHeadByKind[0] = &proto;
                }
                if (!pickaxeHeadByKind[1] && proto.name == "StonePebbleAmethystTexX") {
                    pickaxeHeadByKind[1] = &proto;
                }
                if (!pickaxeHeadByKind[2] && proto.name == "StonePebbleFlouriteTexX") {
                    pickaxeHeadByKind[2] = &proto;
                }
                if (!pickaxeHeadByKind[3] && proto.name == "StonePebbleSilverTexX") {
                    pickaxeHeadByKind[3] = &proto;
                }
                if (!pickaxeHeadFallbackProto && proto.name == "StonePebbleTexX") {
                    pickaxeHeadFallbackProto = &proto;
                }
            }
            if (!pickaxeStickProto) pickaxeStickProto = pickaxeHeadFallbackProto;
            if (!pickaxeHeadFallbackProto) pickaxeHeadFallbackProto = pickaxeStickProto;
            for (const Entity*& proto : pickaxeHeadByKind) {
                if (!proto) proto = pickaxeHeadFallbackProto;
            }

            auto normalizeOrDefault = [](const glm::vec3& v, const glm::vec3& fallback) -> glm::vec3 {
                if (glm::length(v) < 1e-4f) return fallback;
                return glm::normalize(v);
            };
            auto projectDirectionOnSurface = [&](const glm::vec3& direction, const glm::vec3& surfaceNormal) -> glm::vec3 {
                glm::vec3 n = normalizeOrDefault(surfaceNormal, glm::vec3(0.0f, 1.0f, 0.0f));
                glm::vec3 projected = direction - n * glm::dot(direction, n);
                if (glm::length(projected) < 1e-4f) {
                    projected = glm::cross(n, glm::vec3(0.0f, 1.0f, 0.0f));
                    if (glm::length(projected) < 1e-4f) {
                        projected = glm::cross(n, glm::vec3(1.0f, 0.0f, 0.0f));
                    }
                }
                return normalizeOrDefault(projected, glm::vec3(1.0f, 0.0f, 0.0f));
            };
            auto buildOrientedModel = [&](const glm::vec3& center,
                                          const glm::vec3& axisY,
                                          const glm::vec3& upHint) -> glm::mat4 {
                glm::vec3 yAxis = normalizeOrDefault(axisY, glm::vec3(1.0f, 0.0f, 0.0f));
                glm::vec3 xAxis = glm::cross(upHint, yAxis);
                if (glm::length(xAxis) < 1e-4f) xAxis = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), yAxis);
                xAxis = normalizeOrDefault(xAxis, glm::vec3(0.0f, 0.0f, 1.0f));
                glm::vec3 zAxis = normalizeOrDefault(glm::cross(xAxis, yAxis), glm::vec3(0.0f, 1.0f, 0.0f));
                glm::mat4 rot(1.0f);
                rot[0] = glm::vec4(xAxis, 0.0f);
                rot[1] = glm::vec4(yAxis, 0.0f);
                rot[2] = glm::vec4(zAxis, 0.0f);
                return glm::translate(glm::mat4(1.0f), center) * rot;
            };

            renderer.faceShader->use();
            renderer.faceShader->setMat4("view", view);
            renderer.faceShader->setMat4("projection", projection);
            renderer.faceShader->setMat4("model", glm::mat4(1.0f));
            renderer.faceShader->setVec3("cameraPos", playerPos);
            renderer.faceShader->setFloat("time", time);
            renderer.faceShader->setVec3("lightDir", lightDir);
            renderer.faceShader->setVec3("ambientLight", ambientLightColor);
            renderer.faceShader->setVec3("diffuseLight", diffuseLightColor);
            renderer.faceShader->setInt("faceType", 0);
            renderer.faceShader->setInt("sectionLod", 0);
            renderer.faceShader->setInt("leafOpaqueOutsideLod0", leafOpaqueOutsideLod0 ? 1 : 0);
            renderer.faceShader->setInt("leafBackfacesWhenInside", 0);
            renderer.faceShader->setInt("foliageWindEnabled", foliageWindAnimationEnabled ? 1 : 0);
            renderer.faceShader->setInt("waterCascadeBrightnessEnabled", waterCascadeBrightnessEnabled ? 1 : 0);
            renderer.faceShader->setFloat("waterCascadeBrightnessStrength", waterCascadeBrightnessStrength);
            renderer.faceShader->setFloat("waterCascadeBrightnessSpeed", waterCascadeBrightnessSpeed);
            renderer.faceShader->setFloat("waterCascadeBrightnessScale", waterCascadeBrightnessScale);
            renderer.faceShader->setInt("wireframeDebug", 0);
            bindFaceTextureUniforms(*renderer.faceShader);
            BlockChargeSystemLogic::ApplyBlockDamageMaskUniforms(baseSystem, prototypes, *renderer.faceShader, false);

            setCullBackFaceCCWEnabled(true);
            renderBackend.bindVertexArray(renderer.faceVAO);

            auto drawCuboid = [&](const glm::mat4& model,
                                  const Entity* textureProto,
                                  int fallbackTileIndex,
                                  const glm::vec3& localCenter,
                                  const glm::vec3& halfExtents,
                                  const glm::vec3& tintColor = glm::vec3(1.0f)) {
                if (!textureProto) return;
                renderer.faceShader->setMat4("model", model);
                for (int faceType = 0; faceType < 6; ++faceType) {
                    glm::vec3 normal = (faceType == 0) ? glm::vec3(1.0f, 0.0f, 0.0f)
                        : (faceType == 1) ? glm::vec3(-1.0f, 0.0f, 0.0f)
                        : (faceType == 2) ? glm::vec3(0.0f, 1.0f, 0.0f)
                        : (faceType == 3) ? glm::vec3(0.0f, -1.0f, 0.0f)
                        : (faceType == 4) ? glm::vec3(0.0f, 0.0f, 1.0f)
                        : glm::vec3(0.0f, 0.0f, -1.0f);
                    float normalExtent = (faceType == 0 || faceType == 1) ? halfExtents.x
                        : (faceType == 2 || faceType == 3) ? halfExtents.y
                        : halfExtents.z;
                    glm::vec3 facePos = localCenter + normal * normalExtent;
                    glm::vec2 faceScale(1.0f);
                    if (faceType == 0 || faceType == 1) {
                        faceScale = glm::vec2(halfExtents.z * 2.0f, halfExtents.y * 2.0f);
                    } else if (faceType == 2 || faceType == 3) {
                        faceScale = glm::vec2(halfExtents.x * 2.0f, halfExtents.z * 2.0f);
                    } else {
                        faceScale = glm::vec2(halfExtents.x * 2.0f, halfExtents.y * 2.0f);
                    }
                    FaceInstanceRenderData face;
                    face.position = facePos;
                    face.color = tintColor;
                    int tile = RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), *textureProto, faceType);
                    face.tileIndex = (tile >= 0) ? tile : fallbackTileIndex;
                    face.alpha = 1.0f;
                    face.ao = glm::vec4(1.0f);
                    face.scale = faceScale;
                    face.uvScale = faceScale;
                    renderer.faceShader->setInt("faceType", faceType);
                    renderBackend.uploadArrayBufferData(renderer.faceInstanceVBO, &face, sizeof(FaceInstanceRenderData), true);
                    renderBackend.drawArraysTrianglesInstanced(0, 6, 1);
                }
            };

            constexpr float kHandleLength = 12.0f / 24.0f;
            constexpr glm::vec3 kHandleHalf = glm::vec3(1.0f / 48.0f, kHandleLength * 0.5f, 1.0f / 48.0f);
            constexpr float kHeadVoxelHalf = 0.5f / 24.0f;
            auto resolvePickaxeHeadProto = [&](int gemKind) -> const Entity* {
                const int clampedKind = glm::clamp(gemKind, 0, 3);
                const Entity* proto = pickaxeHeadByKind[static_cast<size_t>(clampedKind)];
                return proto ? proto : pickaxeHeadFallbackProto;
            };

            glm::vec3 forward(0.0f), right(0.0f), up(0.0f);
            forward.x = std::cos(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
            forward.y = std::sin(glm::radians(player.cameraPitch));
            forward.z = std::sin(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
            forward = normalizeOrDefault(forward, glm::vec3(0.0f, 0.0f, -1.0f));
            right = normalizeOrDefault(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(1.0f, 0.0f, 0.0f));
            up = normalizeOrDefault(glm::cross(right, forward), glm::vec3(0.0f, 1.0f, 0.0f));

            float chargePull = 0.0f;
            if (player.isChargingBlock && player.blockChargeAction == BlockChargeAction::Destroy) {
                chargePull = glm::clamp(player.blockChargeValue, 0.0f, 1.0f);
            }

            glm::vec3 handleBase = player.cameraPosition
                + forward * (0.20f - 0.15f * chargePull)
                + right * (0.17f + 0.02f * chargePull)
                + glm::vec3(0.0f, 0.13f - 0.04f * chargePull, 0.0f);
            glm::vec3 handleDir = normalizeOrDefault(
                up * (0.95f + 0.05f * chargePull)
                + forward * (0.30f - 0.18f * chargePull)
                + right * (0.09f + 0.02f * chargePull),
                up
            );
            glm::vec3 center = handleBase + handleDir * (kHandleLength * 0.5f);
            glm::mat4 model = buildOrientedModel(center, handleDir, right);
            const float heldPickaxeHeadShortOffset = glm::clamp(
                getRegistryFloat(baseSystem, "HeldPickaxeHeadShortOffset", -1.0f / 48.0f),
                -6.0f / 48.0f,
                6.0f / 48.0f
            );
            const float headAnchorY = glm::clamp(
                getRegistryFloat(baseSystem, "HeldPickaxeHeadAnchorY", 3.0f / 24.0f),
                -0.5f,
                0.5f
            );

            const Entity* headProto = resolvePickaxeHeadProto(player.pickaxeGemKind);
            const std::vector<glm::ivec3>& headVoxelsSrc = player.pickaxeHeadVoxels;
            std::vector<glm::ivec3> fallbackHead;
            if (headVoxelsSrc.empty()) {
                fallbackHead.emplace_back(0, 0, 0);
            }
            const std::vector<glm::ivec3>& headVoxels = headVoxelsSrc.empty() ? fallbackHead : headVoxelsSrc;
            int minX = std::numeric_limits<int>::max();
            int minY = std::numeric_limits<int>::max();
            int minZ = std::numeric_limits<int>::max();
            int maxX = std::numeric_limits<int>::min();
            int maxY = std::numeric_limits<int>::min();
            int maxZ = std::numeric_limits<int>::min();
            for (const glm::ivec3& c : headVoxels) {
                minX = std::min(minX, c.x); maxX = std::max(maxX, c.x);
                minY = std::min(minY, c.y); maxY = std::max(maxY, c.y);
                minZ = std::min(minZ, c.z); maxZ = std::max(maxZ, c.z);
            }
            const float centerX = 0.5f * static_cast<float>(minX + maxX);
            const float centerY = 0.5f * static_cast<float>(minY + maxY);
            const float centerZ = 0.5f * static_cast<float>(minZ + maxZ);

            setDepthTestEnabled(false);
            drawCuboid(model, pickaxeStickProto, 12, glm::vec3(0.0f), kHandleHalf);
            for (const glm::ivec3& cell : headVoxels) {
                const glm::vec3 centeredCell(
                    -(static_cast<float>(cell.x) - centerX),
                    (static_cast<float>(cell.y) - centerY),
                    (static_cast<float>(cell.z) - centerZ)
                );
                const glm::vec3 localCenter = centeredCell * (1.0f / 24.0f)
                    + glm::vec3(0.0f, headAnchorY, heldPickaxeHeadShortOffset);
                drawCuboid(
                    model,
                    headProto,
                    6,
                    localCenter,
                    glm::vec3(kHeadVoxelHalf),
                    glm::vec3(1.0f)
                );
            }
            setDepthTestEnabled(true);

            setCullEnabled(false);
            renderer.faceShader->setMat4("model", glm::mat4(1.0f));
        }

        bool blockSelectionVisualEnabled = true;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("BlockSelectionVisualEnabled");
            if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second)) {
                blockSelectionVisualEnabled = std::get<bool>(it->second);
            }
        }
        if (!mapViewActive
            && blockSelectionVisualEnabled
            && player.hasBlockTarget
            && renderer.selectionShader
            && renderer.selectionVAO
            && renderer.selectionVertexCount > 0) {
            renderer.selectionShader->use();
            glm::mat4 selectionModel = glm::translate(glm::mat4(1.0f), player.targetedBlockPosition);
            selectionModel = glm::scale(selectionModel, glm::vec3(1.02f));
            renderer.selectionShader->setMat4("model", selectionModel);
            renderer.selectionShader->setMat4("view", view);
            renderer.selectionShader->setMat4("projection", projection);
            renderer.selectionShader->setVec3("cameraPos", playerPos);
            renderer.selectionShader->setFloat("time", time);
            renderBackend.bindVertexArray(renderer.selectionVAO);
            renderBackend.drawArraysLines(0, renderer.selectionVertexCount);
        }

        if (!mapViewActive && renderer.audioRayShader && renderer.audioRayVAO && renderer.audioRayVertexCount > 0) {
            setBlendEnabled(true);
            renderer.audioRayShader->use();
            renderer.audioRayShader->setMat4("view", view);
            renderer.audioRayShader->setMat4("projection", projection);
            renderBackend.bindVertexArray(renderer.audioRayVAO);
            renderBackend.setLineWidth(1.6f);
            renderBackend.drawArraysLines(0, renderer.audioRayVertexCount);
            renderBackend.setLineWidth(1.0f);
        }

        bool crosshairEnabled = true;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("CrosshairEnabled");
            if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second)) {
                crosshairEnabled = std::get<bool>(it->second);
            }
        }
        if (!mapViewActive && crosshairEnabled && renderer.crosshairShader && renderer.crosshairVAO && renderer.crosshairVertexCount > 0) {
            setDepthTestEnabled(false);
            renderer.crosshairShader->use();
            renderBackend.bindVertexArray(renderer.crosshairVAO);
            renderBackend.setLineWidth(1.0f);
            renderBackend.drawArraysLines(0, renderer.crosshairVertexCount);
            renderBackend.setLineWidth(1.0f);
            setDepthTestEnabled(true);
        }

        bool legacyMeterEnabled = false;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("LegacyChargeMeterEnabled");
            if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second)) {
                legacyMeterEnabled = std::get<bool>(it->second);
            }
        }
        if (!mapViewActive && legacyMeterEnabled && baseSystem.hud && renderer.hudShader && renderer.hudVAO) {
            HUDContext& hud = *baseSystem.hud;
            if (hud.showCharge) {
                setDepthTestEnabled(false);
                renderer.hudShader->use();
                renderer.hudShader->setFloat("fillAmount", glm::clamp(hud.chargeValue, 0.0f, 1.0f));
                renderer.hudShader->setInt("ready", hud.chargeReady ? 1 : 0);
                renderer.hudShader->setInt("buildModeType", hud.buildModeType);
                renderer.hudShader->setVec3("previewColor", hud.buildPreviewColor);
                renderer.hudShader->setInt("channelIndex", hud.buildChannel);
                renderer.hudShader->setInt("previewTileIndex", hud.buildPreviewTileIndex);
                bindFaceTextureUniforms(*renderer.hudShader);
                renderBackend.bindVertexArray(renderer.hudVAO);
                renderBackend.drawArraysTriangles(0, 6);
                setDepthTestEnabled(true);
            }
        }

        if (prismalMapMinimapActive) {
            int framebufferWidth = 0;
            int framebufferHeight = 0;
            if (win) {
                renderBackend.getFramebufferSize(win, framebufferWidth, framebufferHeight);
            }
            if (framebufferWidth > 0 && framebufferHeight > 0) {
                const int marginPx = std::clamp(getRegistryInt(baseSystem, "PrismalMapMinimapMarginPx", 20), 0, 4096);
                const int desiredSizePx = std::clamp(getRegistryInt(baseSystem, "PrismalMapMinimapSizePx", 220), 48, 4096);
                const int maxFit = std::max(48, std::min(framebufferWidth, framebufferHeight) - marginPx * 2);
                const int mapSizePx = std::clamp(desiredSizePx, 48, maxFit);
                const int mapX = std::max(0, framebufferWidth - marginPx - mapSizePx);
                const int mapY = std::max(0, framebufferHeight - marginPx - mapSizePx);
                const bool followPlayer = getRegistryBool(
                    baseSystem,
                    "PrismalMapMinimapFollowPlayer",
                    getRegistryBool(baseSystem, "PrismalMapFollowPlayer", true)
                );
                const TopDownMapDomain prismalDomain = resolvePrismalMapDomain(
                    baseSystem,
                    world,
                    player,
                    followPlayer,
                    player.prismalMapZoom
                );
                if (buildPrismalMapTexture(baseSystem, world, prototypes, prismalDomain, renderer, PlatformInput::GetTimeSeconds())) {
                    const glm::ivec4 scissorRect(mapX, mapY, mapSizePx, mapSizePx);
                    const glm::vec2 mapCenter(
                        glm::clamp(getRegistryFloat(baseSystem, "PrismalMapCenterX", 0.5f), 0.0f, 1.0f),
                        glm::clamp(getRegistryFloat(baseSystem, "PrismalMapCenterY", 0.5f), 0.0f, 1.0f)
                    );
                    renderTopDownMapOverlay(
                        baseSystem,
                        renderer,
                        renderer.prismalMapTexture,
                        1.0f,
                        mapCenter,
                        framebufferWidth,
                        framebufferHeight,
                        &scissorRect
                    );
                }
            }
        }

        if (player.isometricMapCaptureRequested) {
            std::string capturePath;
            const int captureIndex = (player.isometricMapCaptureCounter < 0) ? 0 : player.isometricMapCaptureCounter;
            const bool captureOk = captureFramebufferToPpm(baseSystem, win, captureIndex, capturePath, &renderer);
            if (captureOk) {
                player.isometricMapCaptureCounter = captureIndex + 1;
                std::cout << "[Map] captured " << capturePath << std::endl;
            } else {
                if (renderer.isometricMapCachedTexture == 0) {
                    player.isometricMapShowCached = false;
                }
                std::cout << "[Map] capture failed" << std::endl;
            }
            player.isometricMapCaptureRequested = false;

            if (player.isometricMapCaptureOneShot) {
                player.isometricMapCaptureOneShot = false;
                player.isometricMapCaptureFullIsland = false;
                player.isometricMapMode = false;
                if (baseSystem.registry) {
                    (*baseSystem.registry)["IsometricMapEnabled"] = false;
                }
                std::cout << "[Map] isometric mode disabled (one-shot complete)" << std::endl;
            }
        }
    }
}

namespace MiniVoxelParticleSystemLogic {
namespace {
    constexpr std::array<glm::vec3, 6> kMiniVoxelFaceNormals = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, -1.0f)
    };

    struct MiningParticleAtlasCache {
        bool valid = false;
        RenderHandle atlasTexture = 0;
        int atlasWidth = 0;
        int atlasHeight = 0;
        int tilesPerRow = 0;
        int tilesPerCol = 0;
        glm::ivec2 tileSize = glm::ivec2(24, 24);
        std::vector<unsigned char> atlasPixels;
    };

    MiningParticleAtlasCache& miningParticleAtlasCache() {
        static MiningParticleAtlasCache cache;
        return cache;
    }

    bool ensureMiningParticleAtlasCacheLoaded(const BaseSystem& baseSystem) {
        MiningParticleAtlasCache& cache = miningParticleAtlasCache();
        if (!baseSystem.renderer || !baseSystem.renderBackend) {
            cache.valid = false;
            cache.atlasPixels.clear();
            return false;
        }

        const RendererContext& renderer = *baseSystem.renderer;
        if (renderer.atlasTexture == 0
            || renderer.atlasTextureSize.x <= 0
            || renderer.atlasTextureSize.y <= 0
            || renderer.atlasTilesPerRow <= 0
            || renderer.atlasTilesPerCol <= 0
            || renderer.atlasTileSize.x <= 0
            || renderer.atlasTileSize.y <= 0) {
            cache.valid = false;
            cache.atlasPixels.clear();
            return false;
        }

        const bool refresh = !cache.valid
            || cache.atlasTexture != renderer.atlasTexture
            || cache.atlasWidth != renderer.atlasTextureSize.x
            || cache.atlasHeight != renderer.atlasTextureSize.y
            || cache.tilesPerRow != renderer.atlasTilesPerRow
            || cache.tilesPerCol != renderer.atlasTilesPerCol
            || cache.tileSize != renderer.atlasTileSize;
        if (!refresh) return true;

        const size_t pixelCount = static_cast<size_t>(renderer.atlasTextureSize.x)
            * static_cast<size_t>(renderer.atlasTextureSize.y) * 4u;
        if (pixelCount == 0u) {
            cache.valid = false;
            cache.atlasPixels.clear();
            return false;
        }

        const bool readbackOk = baseSystem.renderBackend->readTexture2DRgba(
            renderer.atlasTexture,
            renderer.atlasTextureSize.x,
            renderer.atlasTextureSize.y,
            cache.atlasPixels
        );
        if (!readbackOk) {
            cache.valid = false;
            cache.atlasPixels.clear();
            return false;
        }

        cache.atlasTexture = renderer.atlasTexture;
        cache.atlasWidth = renderer.atlasTextureSize.x;
        cache.atlasHeight = renderer.atlasTextureSize.y;
        cache.tilesPerRow = renderer.atlasTilesPerRow;
        cache.tilesPerCol = renderer.atlasTilesPerCol;
        cache.tileSize = renderer.atlasTileSize;
        cache.valid = true;
        return true;
    }

    int chooseFaceIndexFromNormal(const glm::vec3& normal) {
        int bestFace = 2; // +Y fallback
        float bestDot = -std::numeric_limits<float>::infinity();
        for (int face = 0; face < static_cast<int>(kMiniVoxelFaceNormals.size()); ++face) {
            const float d = glm::dot(normal, kMiniVoxelFaceNormals[face]);
            if (d > bestDot) {
                bestDot = d;
                bestFace = face;
            }
        }
        return bestFace;
    }

    int firstValidTileIndex(const std::array<int, 6>& tileIndices) {
        for (int tile : tileIndices) {
            if (tile >= 0) return tile;
        }
        return -1;
    }

    glm::vec3 sampleAtlasTileColor(const MiningParticleAtlasCache& cache,
                                   int tileIndex,
                                   float u,
                                   float v,
                                   const glm::vec3& fallback) {
        if (!cache.valid || cache.atlasPixels.empty()) return fallback;
        if (tileIndex < 0
            || cache.tilesPerRow <= 0
            || cache.tilesPerCol <= 0
            || cache.tileSize.x <= 0
            || cache.tileSize.y <= 0) {
            return fallback;
        }

        const int tileCount = cache.tilesPerRow * cache.tilesPerCol;
        if (tileIndex >= tileCount) return fallback;

        float uu = u - std::floor(u);
        float vv = v - std::floor(v);
        if (uu < 0.0f) uu += 1.0f;
        if (vv < 0.0f) vv += 1.0f;

        const int localX = std::clamp(static_cast<int>(std::floor(uu * static_cast<float>(cache.tileSize.x))), 0, cache.tileSize.x - 1);
        const int localY = std::clamp(static_cast<int>(std::floor(vv * static_cast<float>(cache.tileSize.y))), 0, cache.tileSize.y - 1);
        const int tileX = (tileIndex % cache.tilesPerRow) * cache.tileSize.x;
        const int tileRowTop = tileIndex / cache.tilesPerRow;
        const int tileRowBottom = cache.tilesPerCol - 1 - tileRowTop;
        const int tileY = tileRowBottom * cache.tileSize.y;
        const int px = tileX + localX;
        const int py = tileY + (cache.tileSize.y - 1 - localY);
        if (px < 0 || px >= cache.atlasWidth || py < 0 || py >= cache.atlasHeight) return fallback;

        const size_t idx = static_cast<size_t>((py * cache.atlasWidth + px) * 4);
        if (idx + 3u >= cache.atlasPixels.size()) return fallback;

        const float alpha = static_cast<float>(cache.atlasPixels[idx + 3u]) / 255.0f;
        if (alpha <= 0.001f) return fallback;

        const float r = static_cast<float>(cache.atlasPixels[idx + 0u]) / 255.0f;
        const float g = static_cast<float>(cache.atlasPixels[idx + 1u]) / 255.0f;
        const float b = static_cast<float>(cache.atlasPixels[idx + 2u]) / 255.0f;
        return glm::vec3(r, g, b);
    }

    bool appendOpaqueAtlasTileColors(const MiningParticleAtlasCache& cache,
                                     int tileIndex,
                                     std::unordered_set<uint32_t>& seenColors,
                                     std::vector<glm::vec3>& outColors) {
        if (!cache.valid || cache.atlasPixels.empty()) return false;
        if (tileIndex < 0
            || cache.tilesPerRow <= 0
            || cache.tilesPerCol <= 0
            || cache.tileSize.x <= 0
            || cache.tileSize.y <= 0) {
            return false;
        }

        const int tileCount = cache.tilesPerRow * cache.tilesPerCol;
        if (tileIndex >= tileCount) return false;

        const int tileX = (tileIndex % cache.tilesPerRow) * cache.tileSize.x;
        const int tileRowTop = tileIndex / cache.tilesPerRow;
        const int tileRowBottom = cache.tilesPerCol - 1 - tileRowTop;
        const int tileY = tileRowBottom * cache.tileSize.y;
        if (tileX < 0
            || tileY < 0
            || tileX + cache.tileSize.x > cache.atlasWidth
            || tileY + cache.tileSize.y > cache.atlasHeight) {
            return false;
        }

        outColors.reserve(outColors.size() + static_cast<size_t>(cache.tileSize.x * cache.tileSize.y));
        for (int localY = 0; localY < cache.tileSize.y; ++localY) {
            for (int localX = 0; localX < cache.tileSize.x; ++localX) {
                const int px = tileX + localX;
                const int py = tileY + (cache.tileSize.y - 1 - localY);
                const size_t idx = static_cast<size_t>((py * cache.atlasWidth + px) * 4);
                if (idx + 3u >= cache.atlasPixels.size()) continue;
                const float alpha = static_cast<float>(cache.atlasPixels[idx + 3u]) / 255.0f;
                if (alpha <= 0.001f) continue;
                const unsigned char r8 = cache.atlasPixels[idx + 0u];
                const unsigned char g8 = cache.atlasPixels[idx + 1u];
                const unsigned char b8 = cache.atlasPixels[idx + 2u];
                const uint32_t packed = (static_cast<uint32_t>(r8) << 16u)
                    | (static_cast<uint32_t>(g8) << 8u)
                    | static_cast<uint32_t>(b8);
                if (!seenColors.insert(packed).second) continue;
                outColors.emplace_back(
                    static_cast<float>(r8) / 255.0f,
                    static_cast<float>(g8) / 255.0f,
                    static_cast<float>(b8) / 255.0f
                );
            }
        }
        return true;
    }

    bool gatherOpaqueAtlasFaceColors(const MiningParticleAtlasCache& cache,
                                     const std::array<int, 6>& tileIndices,
                                     std::vector<glm::vec3>& outColors) {
        outColors.clear();
        std::unordered_set<uint32_t> seenColors;
        seenColors.reserve(24u * 24u * tileIndices.size());
        for (int tile : tileIndices) {
            (void)appendOpaqueAtlasTileColors(cache, tile, seenColors, outColors);
        }
        return !outColors.empty();
    }

    uint32_t hash3D(int x, int y, int z) {
        uint32_t ux = static_cast<uint32_t>(x) * 73856093u;
        uint32_t uy = static_cast<uint32_t>(y) * 19349663u;
        uint32_t uz = static_cast<uint32_t>(z) * 83492791u;
        uint32_t h = ux ^ uy ^ uz;
        h ^= (h >> 13);
        h *= 1274126177u;
        h ^= (h >> 16);
        return h;
    }

    inline float nextMiniVoxelRange(uint32_t& state, float minValue, float maxValue) {
        state = state * 1664525u + 1013904223u;
        const float normalized = static_cast<float>(state & 0x00ffffffu) / 16777216.0f;
        return glm::mix(minValue, maxValue, normalized);
    }

    glm::ivec3 pointToVoxelCell(const glm::vec3& p) {
        return glm::ivec3(
            static_cast<int>(std::floor(p.x + 0.5f)),
            static_cast<int>(std::floor(p.y + 0.5f)),
            static_cast<int>(std::floor(p.z + 0.5f))
        );
    }

    bool pointInsideSolidVoxel(const BaseSystem& baseSystem, const glm::vec3& p) {
        if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return false;
        const glm::ivec3 cell = pointToVoxelCell(p);
        return baseSystem.voxelWorld->getBlockWorld(cell) != 0u;
    }

    glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
        const float len = glm::length(v);
        if (len <= 1e-5f) return fallback;
        return v / len;
    }

    void addMiniVoxelFace(std::array<std::vector<FaceInstanceRenderData>, 6>& faceInstances,
                          int faceType,
                          const MiniVoxelParticle& particle,
                          float sizeScale,
                          float alpha) {
        FaceInstanceRenderData face;
        face.position = particle.position + kMiniVoxelFaceNormals[faceType] * (sizeScale * 0.5f);
        face.color = particle.color;
        face.tileIndex = particle.tileIndices[faceType];
        face.alpha = alpha;
        face.ao = glm::vec4(1.0f);
        face.scale = glm::vec2(sizeScale, sizeScale);
        face.uvScale = glm::vec2(1.0f);
        faceInstances[faceType].push_back(face);
    }
} // namespace

void SpawnFromBlock(BaseSystem& baseSystem,
                    const std::vector<Entity>& prototypes,
                    int worldIndex,
                    const glm::ivec3& cell,
                    int prototypeID,
                    const glm::vec3& color,
                    const glm::vec3& hitPosition,
                    const glm::vec3& hitNormal) {
    if (!baseSystem.renderer || !baseSystem.world) return;
    if (prototypeID < 0 || prototypeID >= static_cast<int>(prototypes.size())) return;
    RendererContext& renderer = *baseSystem.renderer;
    const WorldContext* worldCtx = baseSystem.world.get();
    if (!worldCtx) return;
    const Entity& proto = prototypes[static_cast<size_t>(prototypeID)];
    std::array<int, 6> tileIndices;
    for (int face = 0; face < 6; ++face) {
        tileIndices[face] = RenderInitSystemLogic::FaceTileIndexFor(worldCtx, proto, face);
    }
    uint32_t rng = hash3D(cell.x, cell.y, cell.z) ^ static_cast<uint32_t>(baseSystem.frameIndex * 31u);
    const int count = glm::clamp(
        RenderInitSystemLogic::getRegistryInt(baseSystem, "MiningParticleCount", 12),
        4,
        64
    );
    const float lifetime = glm::clamp(
        RenderInitSystemLogic::getRegistryFloat(baseSystem, "MiningParticleLifetimeSeconds", 0.55f),
        0.1f,
        3.0f
    );
    const float baseSize = 1.0f / 24.0f;
    const glm::vec3 fallbackCellCenter = glm::vec3(cell) + glm::vec3(0.5f);
    const glm::vec3 baseNormal = (glm::length(hitNormal) > 0.001f)
        ? glm::normalize(hitNormal)
        : glm::vec3(0.0f, 1.0f, 0.0f);
    const int preferredFace = chooseFaceIndexFromNormal(baseNormal);
    int colorSampleTile = (preferredFace >= 0 && preferredFace < static_cast<int>(tileIndices.size()))
        ? tileIndices[preferredFace]
        : firstValidTileIndex(tileIndices);
    std::array<int, 6> solidColorTileIndices;
    solidColorTileIndices.fill(-1);
    const bool atlasSampleReady = ensureMiningParticleAtlasCacheLoaded(baseSystem);
    const MiningParticleAtlasCache& atlasCache = miningParticleAtlasCache();
    const glm::vec3 fallbackParticleColor = (glm::length(color) > 0.001f) ? color : glm::vec3(1.0f);
    std::vector<glm::vec3> opaqueTileColors;
    if (atlasSampleReady) {
        gatherOpaqueAtlasFaceColors(atlasCache, tileIndices, opaqueTileColors);
    }
    const glm::vec3 tangentA = (std::abs(baseNormal.y) < 0.99f)
        ? glm::normalize(glm::cross(baseNormal, glm::vec3(0.0f, 1.0f, 0.0f)))
        : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 tangentB = glm::normalize(glm::cross(baseNormal, tangentA));
    const glm::vec3 spawnBase = (glm::length(hitPosition - glm::vec3(cell)) < 3.0f)
        ? (hitPosition + baseNormal * 0.02f)
        : fallbackCellCenter;
    for (int i = 0; i < count; ++i) {
        MiniVoxelParticle particle;
        // Start on a thin shell around the impact point, biased outward from the hit face.
        const float radialA = nextMiniVoxelRange(rng, -1.0f, 1.0f);
        const float radialB = nextMiniVoxelRange(rng, -1.0f, 1.0f);
        const float radialN = nextMiniVoxelRange(rng, 0.25f, 1.0f);
        glm::vec3 radial = safeNormalize(
            tangentA * radialA + tangentB * radialB + baseNormal * radialN,
            baseNormal
        );
        if (glm::dot(radial, baseNormal) < 0.0f) {
            radial = safeNormalize(radial + baseNormal * 0.75f, baseNormal);
        }
        const float shellRadius = nextMiniVoxelRange(rng, 0.03f, 0.16f);
        particle.position = spawnBase
            + radial * shellRadius
            + baseNormal * 0.015f;

        // If initial shell point is still inside solid geometry, push it out.
        if (pointInsideSolidVoxel(baseSystem, particle.position)) {
            for (int escape = 0; escape < 6 && pointInsideSolidVoxel(baseSystem, particle.position); ++escape) {
                particle.position += baseNormal * 0.06f;
            }
        }

        const float speed = nextMiniVoxelRange(rng, 0.55f, 2.1f);
        const float spreadA = nextMiniVoxelRange(rng, -0.45f, 0.45f);
        const float spreadB = nextMiniVoxelRange(rng, -0.45f, 0.45f);
        particle.velocity = radial * speed
            + tangentA * spreadA
            + tangentB * spreadB
            + baseNormal * nextMiniVoxelRange(rng, 0.2f, 0.9f);
        particle.tileIndices = solidColorTileIndices;
        particle.lifetime = lifetime;
        particle.baseSize = baseSize;
        if (!opaqueTileColors.empty()) {
            const float pick = nextMiniVoxelRange(rng, 0.0f, static_cast<float>(opaqueTileColors.size()));
            const size_t randomIndex = std::min(
                opaqueTileColors.size() - 1u,
                static_cast<size_t>(pick)
            );
            const size_t colorIndex = (randomIndex + static_cast<size_t>(i)) % opaqueTileColors.size();
            particle.color = opaqueTileColors[colorIndex] * fallbackParticleColor;
        } else if (atlasSampleReady && colorSampleTile >= 0) {
            const float sampleU = nextMiniVoxelRange(rng, 0.05f, 0.95f);
            const float sampleV = nextMiniVoxelRange(rng, 0.05f, 0.95f);
            particle.color = sampleAtlasTileColor(atlasCache, colorSampleTile, sampleU, sampleV, fallbackParticleColor) * fallbackParticleColor;
        } else {
            particle.color = fallbackParticleColor;
        }
        renderer.miniVoxelParticles.push_back(particle);
    }
}

void UpdateParticles(BaseSystem& baseSystem,
                     float dt,
                     std::array<std::vector<FaceInstanceRenderData>, 6>& faceInstances) {
    if (!baseSystem.renderer) return;
    if (dt <= 0.0f) return;
    RendererContext& renderer = *baseSystem.renderer;
    if (renderer.miniVoxelParticles.empty()) return;
    const float gravity = -std::abs(RenderInitSystemLogic::getRegistryFloat(baseSystem, "MiningParticleGravity", 18.0f));
    size_t writeIndex = 0;
    for (size_t i = 0; i < renderer.miniVoxelParticles.size(); ++i) {
        MiniVoxelParticle& particle = renderer.miniVoxelParticles[i];
        const glm::vec3 prevPos = particle.position;
        particle.velocity.y += gravity * dt;
        const glm::vec3 step = particle.velocity * dt;
        glm::vec3 resolvedPos = prevPos;

        // Axis-separated collision keeps particles from tunneling through solid voxels.
        glm::vec3 candidate = resolvedPos;
        candidate.x += step.x;
        if (!pointInsideSolidVoxel(baseSystem, candidate)) {
            resolvedPos.x = candidate.x;
        } else {
            particle.velocity.x *= -0.25f;
        }

        candidate = resolvedPos;
        candidate.y += step.y;
        if (!pointInsideSolidVoxel(baseSystem, candidate)) {
            resolvedPos.y = candidate.y;
        } else {
            if (particle.velocity.y < 0.0f) {
                particle.velocity.x *= 0.72f;
                particle.velocity.z *= 0.72f;
            }
            particle.velocity.y *= -0.2f;
        }

        candidate = resolvedPos;
        candidate.z += step.z;
        if (!pointInsideSolidVoxel(baseSystem, candidate)) {
            resolvedPos.z = candidate.z;
        } else {
            particle.velocity.z *= -0.25f;
        }

        if (pointInsideSolidVoxel(baseSystem, resolvedPos)) {
            resolvedPos = prevPos;
            particle.velocity *= 0.25f;
        }

        particle.position = resolvedPos;
        particle.age += dt;
        if (particle.age >= particle.lifetime) continue;
        const float alpha = 1.0f;
        const float scale = particle.baseSize;
        for (int faceType = 0; faceType < 6; ++faceType) {
            addMiniVoxelFace(faceInstances, faceType, particle, scale, alpha);
        }
        renderer.miniVoxelParticles[writeIndex++] = particle;
    }
    if (writeIndex < renderer.miniVoxelParticles.size()) {
        renderer.miniVoxelParticles.resize(writeIndex);
    }
}
} // namespace MiniVoxelParticleSystemLogic
