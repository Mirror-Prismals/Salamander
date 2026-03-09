#pragma once

#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
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

        void uploadTopDownMapTexture(RendererContext& renderer,
                                     int width,
                                     int height,
                                     const std::vector<unsigned char>& pixels) {
            if (width <= 0 || height <= 0 || pixels.empty()) return;
            if (renderer.topDownMapTexture == 0) {
                glGenTextures(1, &renderer.topDownMapTexture);
            }
            glBindTexture(GL_TEXTURE_2D, renderer.topDownMapTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGB8,
                width,
                height,
                0,
                GL_RGB,
                GL_UNSIGNED_BYTE,
                pixels.data()
            );
            glBindTexture(GL_TEXTURE_2D, 0);
            renderer.topDownMapTextureWidth = width;
            renderer.topDownMapTextureHeight = height;
        }

        bool buildTopDownMapTexture(const BaseSystem& baseSystem,
                                    const WorldContext& world,
                                    const PlayerContext& player,
                                    RendererContext& renderer) {
            if (!world.expanse.loaded) return false;
            const ExpanseConfig& cfg = world.expanse;

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

            float centerX = cfg.islandCenterX;
            float centerZ = cfg.islandCenterZ;
            if (cfg.islandRadius <= 0.0f) {
                if (player.isometricMapOriginInitialized) {
                    centerX = player.isometricMapOrigin.x;
                    centerZ = player.isometricMapOrigin.z;
                } else {
                    centerX = player.cameraPosition.x;
                    centerZ = player.cameraPosition.z;
                }
            }

            const float islandMargin = glm::max(0.0f, getRegistryFloat(baseSystem, "TopDownMapIslandMargin", 260.0f));
            float halfExtent = getRegistryFloat(baseSystem, "TopDownMapHalfExtent", 0.0f);
            if (halfExtent <= 0.0f) {
                if (cfg.islandRadius > 0.0f) {
                    halfExtent = cfg.islandRadius + islandMargin;
                } else {
                    const float captureDiameter = glm::max(1.0f, getRegistryFloat(baseSystem, "IsometricMapCaptureDiameter", 5000.0f));
                    halfExtent = 0.5f * captureDiameter;
                }
            }
            halfExtent = std::max(1.0f, halfExtent);

            const glm::vec3 grassColor = resolveColor(world, cfg.colorGrass, glm::vec3(0.20f, 0.62f, 0.24f));
            const glm::vec3 sandColor = resolveColor(world, cfg.colorSand, glm::vec3(0.88f, 0.75f, 0.48f));
            const glm::vec3 stoneColor = resolveColor(world, cfg.colorStone, glm::vec3(0.35f, 0.30f, 0.26f));
            const glm::vec3 leafColor = resolveColor(world, cfg.colorLeaf, glm::vec3(0.18f, 0.45f, 0.19f));
            const glm::vec3 waterColor = resolveColor(world, cfg.colorWater, glm::vec3(0.10f, 0.45f, 0.82f));
            const glm::vec3 deepWaterColor = resolveColor(world, "DeepWater", waterColor * 0.55f);
            const glm::vec3 lavaColor = resolveColor(world, cfg.colorLava, glm::vec3(0.96f, 0.38f, 0.12f));
            const float heightRange = std::max(1.0f, cfg.islandMaxHeight + cfg.islandNoiseAmp + cfg.jungleVolcanoHeight);
            const float depthRange = std::max(1.0f, cfg.waterSurface - cfg.waterFloor);
            const float invSize = 1.0f / static_cast<float>(mapSize);

            const float volcanoCenterX = cfg.islandCenterX + cfg.islandRadius * std::clamp(cfg.jungleVolcanoCenterFactorX, 0.0f, 1.0f);
            const float volcanoCenterZ = cfg.islandCenterZ + cfg.islandRadius * std::clamp(cfg.jungleVolcanoCenterFactorZ, 0.0f, 1.0f);
            const float volcanoOuterRadius = std::max(8.0f, cfg.jungleVolcanoOuterRadius);
            const float volcanoCraterRadius = std::clamp(cfg.jungleVolcanoCraterRadius, 4.0f, volcanoOuterRadius * 0.95f);

            std::vector<unsigned char> pixels(static_cast<size_t>(mapSize) * static_cast<size_t>(mapSize) * 3u, 0u);
            for (int y = 0; y < mapSize; ++y) {
                const float nz = (static_cast<float>(y) + 0.5f) * invSize * 2.0f - 1.0f;
                const float worldZ = centerZ + nz * halfExtent;
                for (int x = 0; x < mapSize; ++x) {
                    const float nx = (static_cast<float>(x) + 0.5f) * invSize * 2.0f - 1.0f;
                    const float worldX = centerX + nx * halfExtent;

                    float terrainHeight = cfg.waterFloor;
                    const bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(world, worldX, worldZ, terrainHeight);
                    const int biome = ExpanseBiomeSystemLogic::ResolveBiome(world, worldX, worldZ);
                    glm::vec3 color(0.0f);

                    const float volcanoDx = worldX - volcanoCenterX;
                    const float volcanoDz = worldZ - volcanoCenterZ;
                    const float volcanoDist = std::sqrt(volcanoDx * volcanoDx + volcanoDz * volcanoDz);
                    const bool inVolcano = cfg.secondaryBiomeEnabled
                        && cfg.jungleVolcanoEnabled
                        && cfg.islandRadius > 0.0f
                        && biome == 3
                        && volcanoDist <= volcanoOuterRadius;
                    const bool inCrater = inVolcano && volcanoDist <= volcanoCraterRadius;

                    if (!isLand) {
                        float depthNorm = glm::clamp((cfg.waterSurface - terrainHeight) / depthRange, 0.0f, 1.0f);
                        color = glm::mix(waterColor, deepWaterColor, depthNorm);
                        if (inCrater) {
                            color = glm::mix(lavaColor, stoneColor, depthNorm * 0.25f);
                        }
                    } else {
                        if (terrainHeight <= cfg.waterSurface + cfg.beachHeight) {
                            color = sandColor;
                        } else {
                            switch (biome) {
                                case 2: // desert
                                    color = glm::mix(sandColor, stoneColor, 0.12f);
                                    break;
                                case 3: // jungle
                                    color = glm::mix(grassColor, leafColor, 0.58f);
                                    break;
                                case 1: // meadow
                                    color = glm::mix(grassColor, glm::vec3(0.47f, 0.88f, 0.33f), 0.42f);
                                    break;
                                default: // conifer
                                    color = glm::mix(grassColor, leafColor, 0.26f);
                                    break;
                            }
                        }

                        if (inVolcano) {
                            const float volcanicBlend = glm::clamp(1.0f - (volcanoDist / volcanoOuterRadius), 0.0f, 1.0f);
                            const glm::vec3 volcanicStone = glm::mix(stoneColor, glm::vec3(0.20f, 0.18f, 0.17f), 0.55f);
                            color = glm::mix(color, volcanicStone, volcanicBlend * 0.68f);
                            if (inCrater && terrainHeight <= cfg.waterSurface + 4.0f) {
                                color = glm::mix(lavaColor, volcanicStone, 0.18f);
                            }
                        }

                        const float elevNorm = glm::clamp((terrainHeight - cfg.waterSurface) / heightRange, 0.0f, 1.0f);
                        color *= (0.78f + elevNorm * 0.24f);
                    }

                    setPixelRgb(pixels, mapSize, x, (mapSize - 1 - y), color);
                }
            }

            uploadTopDownMapTexture(renderer, mapSize, mapSize, pixels);
            std::cout << "[Map] built 2D terrain map " << mapSize << "x" << mapSize
                      << " (extent " << (halfExtent * 2.0f) << " blocks)" << std::endl;
            return renderer.topDownMapTexture != 0;
        }

        void renderTopDownMapOverlay(const BaseSystem& baseSystem,
                                     const PlayerContext& player,
                                     RendererContext& renderer) {
            if (renderer.topDownMapTexture == 0 || !renderer.godrayCompositeShader || renderer.godrayQuadVAO == 0) return;

            const float mapZoom = glm::max(0.0001f, player.topDownMapZoom);
            const float centerX = glm::clamp(getRegistryFloat(baseSystem, "TopDownMapCenterX", 0.5f), 0.0f, 1.0f);
            const float centerY = glm::clamp(getRegistryFloat(baseSystem, "TopDownMapCenterY", 0.5f), 0.0f, 1.0f);

            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glDisable(GL_BLEND);
            glBindVertexArray(renderer.godrayQuadVAO);
            renderer.godrayCompositeShader->use();
            renderer.godrayCompositeShader->setInt("godrayTex", 0);
            renderer.godrayCompositeShader->setFloat("mapZoom", mapZoom);
            renderer.godrayCompositeShader->setVec2("mapCenter", glm::vec2(centerX, centerY));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, renderer.topDownMapTexture);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
        }

        void uploadCachedMapTexture(RendererContext& renderer,
                                    int width,
                                    int height,
                                    const std::vector<unsigned char>& pixels) {
            if (width <= 0 || height <= 0 || pixels.empty()) return;
            if (renderer.isometricMapCachedTexture == 0) {
                glGenTextures(1, &renderer.isometricMapCachedTexture);
            }
            glBindTexture(GL_TEXTURE_2D, renderer.isometricMapCachedTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGB8,
                width,
                height,
                0,
                GL_RGB,
                GL_UNSIGNED_BYTE,
                pixels.data()
            );
            glBindTexture(GL_TEXTURE_2D, 0);
            renderer.isometricMapCachedWidth = width;
            renderer.isometricMapCachedHeight = height;
        }

        bool captureFramebufferToPpm(const BaseSystem& baseSystem,
                                     GLFWwindow* win,
                                     int captureIndex,
                                     std::string& outPath,
                                     RendererContext* rendererCacheTarget) {
            if (!win) return false;
            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(win, &width, &height);
            if (width <= 0 || height <= 0) return false;

            std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 3u);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            if (rendererCacheTarget) {
                uploadCachedMapTexture(*rendererCacheTarget, width, height, pixels);
            }

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
            return name == "StonePebbleTexX"
                || (name.rfind("StonePebble", 0) == 0 && name.size() >= 4 && name.compare(name.size() - 4, 4, "TexX") == 0);
        }

        bool isStonePebbleZName(const std::string& name) {
            return name == "StonePebbleTexZ"
                || (name.rfind("StonePebble", 0) == 0 && name.size() >= 4 && name.compare(name.size() - 4, 4, "TexZ") == 0);
        }

        bool isPetalPileName(const std::string& name) {
            return name.rfind("StonePebblePetals", 0) == 0;
        }

        enum class DebugSlopeDir : int { PosX = 0, NegX = 1, PosZ = 2, NegZ = 3 };

        bool tryParseDebugSlopeDir(const std::string& name, DebugSlopeDir& outDir) {
            if (name == "DebugSlopeTexPosX") { outDir = DebugSlopeDir::PosX; return true; }
            if (name == "DebugSlopeTexNegX") { outDir = DebugSlopeDir::NegX; return true; }
            if (name == "DebugSlopeTexPosZ") { outDir = DebugSlopeDir::PosZ; return true; }
            if (name == "DebugSlopeTexNegZ") { outDir = DebugSlopeDir::NegZ; return true; }
            return false;
        }

    }

    void RenderWorld(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.player || !baseSystem.level) return;
        PlayerContext& player = *baseSystem.player;
        WorldContext& world = *baseSystem.world;
        RendererContext& renderer = *baseSystem.renderer;
        LevelContext& level = *baseSystem.level;

        float time = static_cast<float>(glfwGetTime());
        const bool isometricMapSystemEnabled = getRegistryBool(baseSystem, "IsometricMapSystem", true);
        const bool isometricMapActive = isometricMapSystemEnabled && player.isometricMapMode;
        const bool topDownMapActive = isometricMapSystemEnabled && player.topDownMapMode;
        if (topDownMapActive) {
            if (buildTopDownMapTexture(baseSystem, world, player, renderer)) {
                renderTopDownMapOverlay(baseSystem, player, renderer);
            }
            return;
        }
        const bool mapViewActive = isometricMapActive;
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
            if (win) glfwGetFramebufferSize(win, &framebufferWidth, &framebufferHeight);
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
        const bool twoSidedAlphaFaces = RenderInitSystemLogic::getRegistryBool(baseSystem, "WaterSurfaceDoubleSided", true);
        const bool leafOpaqueOutsideLod0 = RenderInitSystemLogic::getRegistryBool(baseSystem, "LeafOpaqueOutsideLod0", true);
        const bool leafBackfacesWhenInsideEnabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "LeafBackfacesWhenInside", true);
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
        const bool wallStoneUvJitterEnabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "WallStoneUvJitterEnabled", true);
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
        const bool renderChunkablesViaVoxel = useVoxelGreedy || useVoxelRendering;
        const bool hideHeadVisualizer = RenderInitSystemLogic::getRegistryBool(baseSystem, "PlayerHeadAudioVisualizerHidden", true);


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
                if (proto.isStar) {
                    starPositions.push_back(instance.position);
                }
                // Chunkable terrain/foliage should render from voxel meshes when voxel mode is enabled.
                // Keeping the legacy instance draw active causes stale "ghost" visuals after voxel edits.
                if (renderChunkablesViaVoxel && proto.isBlock && proto.isChunkable) {
                    continue;
                }
                if (proto.name == "Face_PosX") { faceInstances[0].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.name == "Face_NegX") { faceInstances[1].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.name == "Face_PosY") { faceInstances[2].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.name == "Face_NegY") { faceInstances[3].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.name == "Face_PosZ") { faceInstances[4].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.name == "Face_NegZ") { faceInstances[5].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
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
        glm::vec3 lightDir;
        SkyboxSystemLogic::RenderSkyAndCelestials(baseSystem, prototypes, starPositions, time, dayFraction, view, projection, playerPos, lightDir);
        bool auroraEnabled = true;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("AuroraSystem");
            if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second)) {
                auroraEnabled = std::get<bool>(it->second);
            }
        }
        if (auroraEnabled) {
            AuroraSystemLogic::RenderAuroras(baseSystem, time, view, projection);
        }
        // Clouds disabled by request.
        // CloudSystemLogic::RenderClouds(baseSystem, lightDir, time, dayFraction);

        renderer.blockShader->use();
        renderer.blockShader->setMat4("view", view);
        renderer.blockShader->setMat4("projection", projection);
        renderer.blockShader->setVec3("cameraPos", playerPos);
        renderer.blockShader->setFloat("time", time);
        renderer.blockShader->setFloat("instanceScale", 1.0f);
        renderer.blockShader->setVec3("lightDir",lightDir);
        renderer.blockShader->setVec3("ambientLight",glm::vec3(0.4f));
        renderer.blockShader->setVec3("diffuseLight",glm::vec3(0.6f));
        renderer.blockShader->setMat4("model", glm::mat4(1.0f));
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        for (int i = 0; i < static_cast<int>(RenderBehavior::COUNT); ++i) {
            RenderBehavior currentBehavior = static_cast<RenderBehavior>(i);
            bool translucent = (currentBehavior == RenderBehavior::ANIMATED_WATER || currentBehavior == RenderBehavior::ANIMATED_TRANSPARENT_WAVE);
            if (translucent) {
                // Let translucent passes read depth but avoid writing it so surfaces beneath stay visible.
                glDepthMask(GL_FALSE);
            }
            if (useVoxelRendering) {
                VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
                VoxelRenderContext& voxelRender = *baseSystem.voxelRender;
                for (const auto& [sectionKey, buffers] : voxelRender.renderBuffers) {
                    auto secIt = voxelWorld.sections.find(sectionKey);
                    if (secIt == voxelWorld.sections.end()) continue;
                    const VoxelSection& section = secIt->second;
                    if (!mapViewActive
                        && !RenderInitSystemLogic::shouldRenderVoxelSection(baseSystem, section, playerPos)) continue;
                    int count = buffers.counts[i];
                    if (count <= 0) continue;
                    renderer.blockShader->setFloat("instanceScale", static_cast<float>(1 << section.lod));
                    renderer.blockShader->setInt("behaviorType", i);
                    if (buffers.vaos[i] == 0) continue;
                    glBindVertexArray(buffers.vaos[i]);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, count);
                }
                renderer.blockShader->setFloat("instanceScale", 1.0f);
            }
            if (currentBehavior == RenderBehavior::STATIC_BRANCH) {
                if (!branchInstances.empty()) {
                    renderer.blockShader->setInt("behaviorType", i);
                    glBindVertexArray(renderer.behaviorVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, renderer.behaviorInstanceVBOs[i]);
                    glBufferData(GL_ARRAY_BUFFER, branchInstances.size() * sizeof(BranchInstanceData), branchInstances.data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, branchInstances.size());
                }
            } else {
                if (!behaviorInstances[i].empty()) {
                    renderer.blockShader->setInt("behaviorType", i);
                    glBindVertexArray(renderer.behaviorVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, renderer.behaviorInstanceVBOs[i]);
                    glBufferData(GL_ARRAY_BUFFER, behaviorInstances[i].size() * sizeof(InstanceData), behaviorInstances[i].data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, behaviorInstances[i].size());
                }
            }
            if (translucent) {
                glDepthMask(GL_TRUE);
            }
        }

        auto bindFaceTextureUniforms = [&](Shader& shader){
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
            shader.setInt("leafDirectionalLightingEnabled", leafDirectionalLightingEnabled ? 1 : 0);
            shader.setFloat("leafDirectionalLightingIntensity", leafDirectionalLightingIntensity);
            shader.setInt("atlasTexture", 0);
            if (renderer.atlasTexture != 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, renderer.atlasTexture);
            }
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
            const bool hasWaterOverlayTexture = false;
            static int sMaxTextureUnits = -1;
            if (sMaxTextureUnits < 0) {
                glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &sMaxTextureUnits);
            }
            const bool enoughUnitsForDualGrassSets = (sMaxTextureUnits >= 7);
            const bool enoughUnitsForOreSet = (sMaxTextureUnits >= 11);
            const bool enoughUnitsForTerrainSet = (sMaxTextureUnits >= 13);
            const bool enoughUnitsForWaterOverlay = (sMaxTextureUnits >= 14);
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
            shader.setInt("waterOverlayTextureEnabled", 0);
            shader.setInt("waterOverlayTexture", 13);
            if (hasAllGrassTextures) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, renderer.grassTextures[0]);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, renderer.grassTextures[1]);
                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D, renderer.grassTextures[2]);
            }
            if (hasAllShortGrassTextures && enoughUnitsForDualGrassSets) {
                glActiveTexture(GL_TEXTURE4);
                glBindTexture(GL_TEXTURE_2D, renderer.shortGrassTextures[0]);
                glActiveTexture(GL_TEXTURE5);
                glBindTexture(GL_TEXTURE_2D, renderer.shortGrassTextures[1]);
                glActiveTexture(GL_TEXTURE6);
                glBindTexture(GL_TEXTURE_2D, renderer.shortGrassTextures[2]);
            }
            if (hasAllOreTextures && enoughUnitsForOreSet) {
                glActiveTexture(GL_TEXTURE7);
                glBindTexture(GL_TEXTURE_2D, renderer.oreTextures[0]);
                glActiveTexture(GL_TEXTURE8);
                glBindTexture(GL_TEXTURE_2D, renderer.oreTextures[1]);
                glActiveTexture(GL_TEXTURE9);
                glBindTexture(GL_TEXTURE_2D, renderer.oreTextures[2]);
                glActiveTexture(GL_TEXTURE10);
                glBindTexture(GL_TEXTURE_2D, renderer.oreTextures[3]);
            }
            if (hasAllTerrainTextures && enoughUnitsForTerrainSet) {
                glActiveTexture(GL_TEXTURE11);
                glBindTexture(GL_TEXTURE_2D, renderer.terrainTextures[0]);
                glActiveTexture(GL_TEXTURE12);
                glBindTexture(GL_TEXTURE_2D, renderer.terrainTextures[1]);
            }
            if (hasWaterOverlayTexture && enoughUnitsForWaterOverlay) {
                glActiveTexture(GL_TEXTURE13);
                glBindTexture(GL_TEXTURE_2D, renderer.waterOverlayTexture);
            }
            glActiveTexture(GL_TEXTURE0);
        };

        auto drawFaceBatches = [&](const std::array<std::vector<FaceInstanceRenderData>, 6>& batches, bool depthWrite){
            if (!renderer.faceShader || !renderer.faceVAO) return;
            if (!depthWrite) glDepthMask(GL_FALSE);
            bool enableCull = depthWrite ? !renderOpaqueFacesTwoSided : !twoSidedAlphaFaces;
            if (enableCull) {
                glEnable(GL_CULL_FACE);
                glFrontFace(GL_CCW);
                glCullFace(GL_BACK);
            } else {
                glDisable(GL_CULL_FACE);
            }

            renderer.faceShader->use();
            renderer.faceShader->setMat4("view", view);
            renderer.faceShader->setMat4("projection", projection);
            renderer.faceShader->setMat4("model", glm::mat4(1.0f));
            renderer.faceShader->setVec3("cameraPos", playerPos);
            renderer.faceShader->setFloat("time", time);
            renderer.faceShader->setVec3("lightDir", lightDir);
            renderer.faceShader->setVec3("ambientLight", glm::vec3(0.4f));
            renderer.faceShader->setVec3("diffuseLight", glm::vec3(0.6f));
            renderer.faceShader->setInt("faceType", 0);
            renderer.faceShader->setInt("sectionLod", 0);
            renderer.faceShader->setInt("leafOpaqueOutsideLod0", leafOpaqueOutsideLod0 ? 1 : 0);
            renderer.faceShader->setInt("leafBackfacesWhenInside", renderOpaqueFacesTwoSided ? 1 : 0);
            renderer.faceShader->setInt("waterCascadeBrightnessEnabled", waterCascadeBrightnessEnabled ? 1 : 0);
            renderer.faceShader->setFloat("waterCascadeBrightnessStrength", waterCascadeBrightnessStrength);
            renderer.faceShader->setFloat("waterCascadeBrightnessSpeed", waterCascadeBrightnessSpeed);
            renderer.faceShader->setFloat("waterCascadeBrightnessScale", waterCascadeBrightnessScale);
            bindFaceTextureUniforms(*renderer.faceShader);
            glBindVertexArray(renderer.faceVAO);
            for (int faceType = 0; faceType < 6; ++faceType) {
                const auto& instances = batches[faceType];
                if (instances.empty()) continue;
                renderer.faceShader->setInt("faceType", faceType);
                glBindBuffer(GL_ARRAY_BUFFER, renderer.faceInstanceVBO);
                glBufferData(GL_ARRAY_BUFFER, instances.size() * sizeof(FaceInstanceRenderData), instances.data(), GL_DYNAMIC_DRAW);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, instances.size());
            }

            glDisable(GL_CULL_FACE);
            if (!depthWrite) glDepthMask(GL_TRUE);
        };

        if (renderer.faceShader && renderer.faceVAO) {
            std::array<std::vector<FaceInstanceRenderData>, 6> faceInstancesOpaque;
            std::array<std::vector<FaceInstanceRenderData>, 6> faceInstancesAlpha;
            for (int f = 0; f < 6; ++f) {
                for (const auto& inst : faceInstances[f]) {
                    if (inst.alpha < 0.0f) faceInstancesOpaque[f].push_back(inst);
                    else if (inst.alpha < 0.999f) faceInstancesAlpha[f].push_back(inst);
                    else faceInstancesOpaque[f].push_back(inst);
                }
            }
            drawFaceBatches(faceInstancesOpaque, true);
            drawFaceBatches(faceInstancesAlpha, false);
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
            renderer.faceShader->setVec3("ambientLight", glm::vec3(0.4f));
            renderer.faceShader->setVec3("diffuseLight", glm::vec3(0.6f));
            renderer.faceShader->setInt("faceType", 0);
            renderer.faceShader->setInt("leafOpaqueOutsideLod0", leafOpaqueOutsideLod0 ? 1 : 0);
            renderer.faceShader->setInt("leafBackfacesWhenInside", renderOpaqueFacesTwoSided ? 1 : 0);
            renderer.faceShader->setInt("waterCascadeBrightnessEnabled", waterCascadeBrightnessEnabled ? 1 : 0);
            renderer.faceShader->setFloat("waterCascadeBrightnessStrength", waterCascadeBrightnessStrength);
            renderer.faceShader->setFloat("waterCascadeBrightnessSpeed", waterCascadeBrightnessSpeed);
            renderer.faceShader->setFloat("waterCascadeBrightnessScale", waterCascadeBrightnessScale);
            bindFaceTextureUniforms(*renderer.faceShader);

            if (renderOpaqueFacesTwoSided) {
                glDisable(GL_CULL_FACE);
            } else {
                glEnable(GL_CULL_FACE);
                glFrontFace(GL_CCW);
                glCullFace(GL_BACK);
            }

            for (const auto& [sectionKey, buffers] : voxelGreedy.renderBuffers) {
                auto secIt = voxelWorld.sections.find(sectionKey);
                if (secIt == voxelWorld.sections.end()) continue;
                if (secIt->second.lod > voxelGreedyMaxLod) continue;
                int mult = (sectionKey.lod >= superChunkMinLod
                            && sectionKey.lod <= superChunkMaxLod
                            && superChunkSize > 1) ? superChunkSize : 1;
                if (!mapViewActive
                    && !RenderInitSystemLogic::shouldRenderVoxelSectionSized(baseSystem,
                                                       sectionKey.lod,
                                                       sectionKey.coord,
                                                       secIt->second.size,
                                                       mult,
                                                       playerPos)) {
                    continue;
                }
                renderer.faceShader->setInt("sectionLod", secIt->second.lod);
                for (int faceType = 0; faceType < 6; ++faceType) {
                    int count = buffers.opaqueCounts[faceType];
                    if (count > 0 && buffers.opaqueVaos[faceType] != 0) {
                        renderer.faceShader->setInt("faceType", faceType);
                        glBindVertexArray(buffers.opaqueVaos[faceType]);
                        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
                    }
                }
            }

            glDepthMask(GL_FALSE);
            if (twoSidedAlphaFaces) {
                glDisable(GL_CULL_FACE);
            } else {
                glEnable(GL_CULL_FACE);
                glFrontFace(GL_CCW);
                glCullFace(GL_BACK);
            }
            for (const auto& [sectionKey, buffers] : voxelGreedy.renderBuffers) {
                auto secIt = voxelWorld.sections.find(sectionKey);
                if (secIt == voxelWorld.sections.end()) continue;
                if (secIt->second.lod > voxelGreedyMaxLod) continue;
                int mult = (sectionKey.lod >= superChunkMinLod
                            && sectionKey.lod <= superChunkMaxLod
                            && superChunkSize > 1) ? superChunkSize : 1;
                if (!mapViewActive
                    && !RenderInitSystemLogic::shouldRenderVoxelSectionSized(baseSystem,
                                                       sectionKey.lod,
                                                       sectionKey.coord,
                                                       secIt->second.size,
                                                       mult,
                                                       playerPos)) {
                    continue;
                }
                renderer.faceShader->setInt("sectionLod", secIt->second.lod);
                for (int faceType = 0; faceType < 6; ++faceType) {
                    int count = buffers.alphaCounts[faceType];
                    if (count > 0 && buffers.alphaVaos[faceType] != 0) {
                        renderer.faceShader->setInt("faceType", faceType);
                        glBindVertexArray(buffers.alphaVaos[faceType]);
                        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
                    }
                }
            }
            glDepthMask(GL_TRUE);
            glDisable(GL_CULL_FACE);
        }

        if (!debugSlopeInstances.empty() && renderer.faceShader && renderer.faceVAO) {
            renderer.faceShader->use();
            renderer.faceShader->setMat4("view", view);
            renderer.faceShader->setMat4("projection", projection);
            renderer.faceShader->setMat4("model", glm::mat4(1.0f));
            renderer.faceShader->setVec3("cameraPos", playerPos);
            renderer.faceShader->setFloat("time", time);
            renderer.faceShader->setVec3("lightDir", lightDir);
            renderer.faceShader->setVec3("ambientLight", glm::vec3(0.4f));
            renderer.faceShader->setVec3("diffuseLight", glm::vec3(0.6f));
            renderer.faceShader->setInt("faceType", 0);
            renderer.faceShader->setInt("sectionLod", 0);
            renderer.faceShader->setInt("leafOpaqueOutsideLod0", leafOpaqueOutsideLod0 ? 1 : 0);
            renderer.faceShader->setInt("leafBackfacesWhenInside", renderOpaqueFacesTwoSided ? 1 : 0);
            renderer.faceShader->setInt("waterCascadeBrightnessEnabled", waterCascadeBrightnessEnabled ? 1 : 0);
            renderer.faceShader->setFloat("waterCascadeBrightnessStrength", waterCascadeBrightnessStrength);
            renderer.faceShader->setFloat("waterCascadeBrightnessSpeed", waterCascadeBrightnessSpeed);
            renderer.faceShader->setFloat("waterCascadeBrightnessScale", waterCascadeBrightnessScale);
            renderer.faceShader->setInt("wireframeDebug", 0);
            bindFaceTextureUniforms(*renderer.faceShader);

            glEnable(GL_CULL_FACE);
            glFrontFace(GL_CCW);
            glCullFace(GL_BACK);
            glBindVertexArray(renderer.faceVAO);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.faceInstanceVBO);

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
                glBufferData(GL_ARRAY_BUFFER, sizeof(FaceInstanceRenderData), &face, GL_DYNAMIC_DRAW);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 1);
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
                        capFaceA = 0; capAlphaA = kSlopeCapA;
                        capFaceB = 1; capAlphaB = kSlopeCapB;
                        topAlpha = kSlopeTopPosZ;
                        break;
                    case DebugSlopeDir::NegZ:
                        tallFaceType = 5;
                        capFaceA = 0; capAlphaA = kSlopeCapB;
                        capFaceB = 1; capAlphaB = kSlopeCapA;
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

            glDisable(GL_CULL_FACE);
            renderer.faceShader->setMat4("model", glm::mat4(1.0f));
        }

        if (!mapViewActive && player.isHoldingBlock && player.heldPrototypeID >= 0) {
            const float heldItemForward = glm::clamp(
                getRegistryFloat(baseSystem, "HeldItemViewForward", 0.8f),
                0.2f,
                2.5f
            );
            const float heldItemVertical = glm::clamp(
                getRegistryFloat(baseSystem, "HeldItemViewVertical", 0.08f),
                -1.0f,
                1.0f
            );
            glm::vec3 heldPos = player.cameraPosition
                + cameraForward * heldItemForward
                + glm::vec3(0.0f, heldItemVertical, 0.0f);
            bool drewTextured = false;
            if (player.heldPrototypeID < static_cast<int>(prototypes.size())) {
                const Entity& heldProto = prototypes[player.heldPrototypeID];
                const bool heldIsLeaf = (heldProto.name == "Leaf");
                const bool heldIsTallGrass = (heldProto.name.rfind("GrassTuft", 0) == 0)
                    && (heldProto.name.rfind("GrassTuftShort", 0) != 0);
                const bool heldIsShortGrass = (heldProto.name.rfind("GrassTuftShort", 0) == 0);
                const bool heldIsFlower = (heldProto.name.rfind("Flower", 0) == 0);
                const bool heldIsPlant = heldIsTallGrass || heldIsShortGrass || heldIsFlower;
                const bool heldIsStick = (heldProto.name == "StickTexX" || heldProto.name == "StickTexZ");
                const bool heldIsPetalPile = isPetalPileName(heldProto.name);
                const bool heldIsWallStone = (heldProto.name == "WallStoneTexPosX"
                    || heldProto.name == "WallStoneTexNegX"
                    || heldProto.name == "WallStoneTexPosZ"
                    || heldProto.name == "WallStoneTexNegZ");
                const bool heldIsCeilingStone = (heldProto.name == "CeilingStoneTexX"
                    || heldProto.name == "CeilingStoneTexZ");
                const bool heldIsStonePebble = (isStonePebbleXName(heldProto.name)
                    || isStonePebbleZName(heldProto.name)
                    || heldIsWallStone
                    || heldIsCeilingStone);
                const bool heldIsNarrowProp = heldIsStick || heldIsStonePebble;
                const bool drawAsFace = heldProto.useTexture || heldIsLeaf || heldIsPlant;
                if (drawAsFace && renderer.faceShader && renderer.faceVAO) {
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
                    renderer.faceShader->setVec3("ambientLight", glm::vec3(0.4f));
                    renderer.faceShader->setVec3("diffuseLight", glm::vec3(0.6f));
                    renderer.faceShader->setInt("faceType", 0);
                    renderer.faceShader->setInt("sectionLod", 0);
                    renderer.faceShader->setInt("leafOpaqueOutsideLod0", leafOpaqueOutsideLod0 ? 1 : 0);
                    renderer.faceShader->setInt("leafBackfacesWhenInside", renderOpaqueFacesTwoSided ? 1 : 0);
                    renderer.faceShader->setInt("waterCascadeBrightnessEnabled", waterCascadeBrightnessEnabled ? 1 : 0);
                    renderer.faceShader->setFloat("waterCascadeBrightnessStrength", waterCascadeBrightnessStrength);
                    renderer.faceShader->setFloat("waterCascadeBrightnessSpeed", waterCascadeBrightnessSpeed);
                    renderer.faceShader->setFloat("waterCascadeBrightnessScale", waterCascadeBrightnessScale);
                    renderer.faceShader->setInt("wireframeDebug", 0);
                    bindFaceTextureUniforms(*renderer.faceShader);
                    glEnable(GL_CULL_FACE);
                    glFrontFace(GL_CCW);
                    glCullFace(GL_BACK);
                    glBindVertexArray(renderer.faceVAO);
                    glBindBuffer(GL_ARRAY_BUFFER, renderer.faceInstanceVBO);

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
                            heldFace.color = player.heldBlockColor;
                            heldFace.tileIndex = RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), heldProto, faceType);
                            heldFace.alpha = 1.0f;
                            heldFace.ao = glm::vec4(1.0f);
                            heldFace.scale = glm::vec2(uScale, vScale);
                            heldFace.uvScale = heldFace.scale;
                            renderer.faceShader->setInt("faceType", faceType);
                            glBufferData(GL_ARRAY_BUFFER, sizeof(FaceInstanceRenderData), &heldFace, GL_DYNAMIC_DRAW);
                            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 1);
                        }
                        glDisable(GL_CULL_FACE);
                        drewTextured = true;
                    } else if (heldIsPlant) {
                        static const std::array<int, 4> kPlantFaces = {0, 1, 4, 5};
                        float alphaMode = -2.0f;
                        if (heldIsFlower) alphaMode = -3.0f;
                        else if (heldIsShortGrass) alphaMode = -2.3f;
                        const int heldPlantBaseTile = heldIsFlower
                            ? -1
                            : RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), heldProto, 2);
                        glm::vec2 plantScale = glm::vec2(1.0f);
                        if (heldIsFlower) plantScale = glm::vec2(0.86f, 0.92f);
                        for (int faceType : kPlantFaces) {
                            FaceInstanceRenderData heldFace;
                            heldFace.position = heldPos;
                            heldFace.color = player.heldBlockColor;
                            heldFace.tileIndex = heldPlantBaseTile;
                            heldFace.alpha = alphaMode;
                            heldFace.ao = glm::vec4(1.0f);
                            heldFace.scale = plantScale;
                            heldFace.uvScale = glm::vec2(1.0f);
                            renderer.faceShader->setInt("faceType", faceType);
                            glBufferData(GL_ARRAY_BUFFER, sizeof(FaceInstanceRenderData), &heldFace, GL_DYNAMIC_DRAW);
                            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 1);
                        }
                        glDisable(GL_CULL_FACE);
                        drewTextured = true;
                    } else {
                        const bool narrowAlongX = (heldProto.name == "StickTexX"
                            || isStonePebbleXName(heldProto.name)
                            || heldProto.name == "CeilingStoneTexX");
                        const bool narrowAlongZ = (heldProto.name == "StickTexZ"
                            || isStonePebbleZName(heldProto.name)
                            || heldProto.name == "CeilingStoneTexZ");
                        const float half1 = 1.0f / 48.0f;
                        const float half2 = 2.0f / 48.0f;
                        const float half6 = 6.0f / 48.0f;
                        const float half12 = 12.0f / 48.0f;
                        glm::vec3 narrowHalfExtents(0.5f);
                        if (heldIsStick) {
                            narrowHalfExtents = narrowAlongX
                                ? glm::vec3(half12, half1, half1)
                                : (narrowAlongZ ? glm::vec3(half1, half1, half12) : glm::vec3(0.5f));
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
                            heldFace.color = player.heldBlockColor;
                            int heldTileIndex = heldIsLeaf ? -1 : RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), heldProto, faceType);
                            heldFace.tileIndex = heldTileIndex;
                            heldFace.alpha = heldIsLeaf ? -1.0f : 1.0f;
                            heldFace.ao = glm::vec4(1.0f);
                            heldFace.scale = faceScale;
                            heldFace.uvScale = faceUvScale;
                            renderer.faceShader->setInt("faceType", faceType);
                            glBufferData(GL_ARRAY_BUFFER, sizeof(FaceInstanceRenderData), &heldFace, GL_DYNAMIC_DRAW);
                            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 1);
                        }
                        glDisable(GL_CULL_FACE);
                        drewTextured = true;
                    }
                }
            }
            if (!drewTextured) {
                InstanceData heldInstance;
                heldInstance.position = heldPos;
                heldInstance.color = player.heldBlockColor;
                int behaviorIndex = static_cast<int>(RenderBehavior::STATIC_DEFAULT);
                renderer.blockShader->use();
                renderer.blockShader->setMat4("view", view);
                renderer.blockShader->setMat4("projection", projection);
                renderer.blockShader->setVec3("cameraPos", playerPos);
                renderer.blockShader->setFloat("time", time);
                renderer.blockShader->setFloat("instanceScale", 1.0f);
                renderer.blockShader->setVec3("lightDir", lightDir);
                renderer.blockShader->setVec3("ambientLight", glm::vec3(0.4f));
                renderer.blockShader->setVec3("diffuseLight", glm::vec3(0.6f));
                renderer.blockShader->setMat4("model", glm::mat4(1.0f));
                renderer.blockShader->setInt("behaviorType", behaviorIndex);
                glBindVertexArray(renderer.behaviorVAOs[behaviorIndex]);
                glBindBuffer(GL_ARRAY_BUFFER, renderer.behaviorInstanceVBOs[behaviorIndex]);
                glBufferData(GL_ARRAY_BUFFER, sizeof(InstanceData), &heldInstance, GL_DYNAMIC_DRAW);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 36, 1);
            }
        }

        const bool renderHeldHatchet = !mapViewActive
            && player.hatchetHeld
            && !player.isHoldingBlock
            && !(baseSystem.gems && baseSystem.gems->blockModeHoldingGem)
            && player.buildMode == BuildModeType::Pickup;
        const bool renderPlacedHatchet = player.hatchetPlacedInWorld;
        if ((renderHeldHatchet || renderPlacedHatchet)
            && renderer.faceShader
            && renderer.faceVAO
            && renderer.faceInstanceVBO) {
            const Entity* hatchetStickProto = nullptr;
            const Entity* hatchetStoneProto = nullptr;
            const Entity* hatchetStoneFallbackProto = nullptr;
            std::array<const Entity*, 5> hatchetStoneByMaterial = {nullptr, nullptr, nullptr, nullptr, nullptr};
            for (const auto& proto : prototypes) {
                if (!proto.useTexture) continue;
                if (!hatchetStickProto && (proto.name == "StickTexX" || proto.name == "FirLog1Tex")) {
                    hatchetStickProto = &proto;
                }
                if (!hatchetStoneProto && (proto.name == "StonePebbleTexX" || proto.name == "CobblestoneBlockTex")) {
                    hatchetStoneProto = &proto;
                }
                if (!hatchetStoneByMaterial[1] && proto.name == "StonePebbleRubyTexX") {
                    hatchetStoneByMaterial[1] = &proto;
                }
                if (!hatchetStoneByMaterial[2] && proto.name == "StonePebbleAmethystTexX") {
                    hatchetStoneByMaterial[2] = &proto;
                }
                if (!hatchetStoneByMaterial[3] && proto.name == "StonePebbleFlouriteTexX") {
                    hatchetStoneByMaterial[3] = &proto;
                }
                if (!hatchetStoneByMaterial[4] && proto.name == "StonePebbleSilverTexX") {
                    hatchetStoneByMaterial[4] = &proto;
                }
                if (!hatchetStoneFallbackProto && proto.name == "StoneBlockTex") {
                    hatchetStoneFallbackProto = &proto;
                }
            }
            if (!hatchetStickProto) hatchetStickProto = hatchetStoneProto;
            if (!hatchetStoneProto) hatchetStoneProto = hatchetStoneFallbackProto ? hatchetStoneFallbackProto : hatchetStickProto;
            hatchetStoneByMaterial[0] = hatchetStoneProto;

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
            renderer.faceShader->setVec3("ambientLight", glm::vec3(0.4f));
            renderer.faceShader->setVec3("diffuseLight", glm::vec3(0.6f));
            renderer.faceShader->setInt("faceType", 0);
            renderer.faceShader->setInt("sectionLod", 0);
            renderer.faceShader->setInt("leafOpaqueOutsideLod0", leafOpaqueOutsideLod0 ? 1 : 0);
            renderer.faceShader->setInt("leafBackfacesWhenInside", renderOpaqueFacesTwoSided ? 1 : 0);
            renderer.faceShader->setInt("waterCascadeBrightnessEnabled", waterCascadeBrightnessEnabled ? 1 : 0);
            renderer.faceShader->setFloat("waterCascadeBrightnessStrength", waterCascadeBrightnessStrength);
            renderer.faceShader->setFloat("waterCascadeBrightnessSpeed", waterCascadeBrightnessSpeed);
            renderer.faceShader->setFloat("waterCascadeBrightnessScale", waterCascadeBrightnessScale);
            renderer.faceShader->setInt("wireframeDebug", 0);
            bindFaceTextureUniforms(*renderer.faceShader);

            glEnable(GL_CULL_FACE);
            glFrontFace(GL_CCW);
            glCullFace(GL_BACK);
            glBindVertexArray(renderer.faceVAO);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.faceInstanceVBO);

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
                    glBufferData(GL_ARRAY_BUFFER, sizeof(FaceInstanceRenderData), &face, GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 1);
                }
            };

            constexpr float kHandleLength = 12.0f / 24.0f;
            constexpr glm::vec3 kHandleHalf = glm::vec3(1.0f / 48.0f, kHandleLength * 0.5f, 1.0f / 48.0f);
            // Match surface stone-pebble profile (6x2x2 in 24-unit block space).
            constexpr glm::vec3 kHeadHalf = glm::vec3(6.0f / 48.0f, 2.0f / 48.0f, 2.0f / 48.0f);
            constexpr glm::vec3 kHeadCenter = glm::vec3(3.5f / 48.0f, 3.0f / 24.0f, 0.0f);
            auto hatchetMaterialColor = [](int material) -> glm::vec3 {
                switch (material) {
                    case 1: return glm::vec3(0.86f, 0.18f, 0.20f); // ruby
                    case 2: return glm::vec3(0.64f, 0.48f, 0.88f); // amethyst
                    case 3: return glm::vec3(0.38f, 0.67f, 0.96f); // flourite
                    case 4: return glm::vec3(0.92f, 0.93f, 0.95f); // silver
                    default: return glm::vec3(1.0f);               // stone
                }
            };
            auto resolveHatchetHeadProtoAndTint = [&](int material, glm::vec3& outTint) -> const Entity* {
                int clampedMaterial = glm::clamp(material, 0, 4);
                const Entity* byMaterial = hatchetStoneByMaterial[static_cast<size_t>(clampedMaterial)];
                if (byMaterial) {
                    outTint = glm::vec3(1.0f);
                    return byMaterial;
                }
                outTint = hatchetMaterialColor(clampedMaterial);
                return hatchetStoneByMaterial[0];
            };

            if (renderPlacedHatchet) {
                glm::vec3 surfaceNormal = normalizeOrDefault(player.hatchetPlacedNormal, glm::vec3(0.0f, 1.0f, 0.0f));
                glm::vec3 handleDir = projectDirectionOnSurface(player.hatchetPlacedDirection, surfaceNormal);
                glm::mat4 model = buildOrientedModel(player.hatchetPlacedPosition, handleDir, surfaceNormal);
                glm::vec3 headTint(1.0f);
                const Entity* headProto = resolveHatchetHeadProtoAndTint(player.hatchetPlacedMaterial, headTint);
                glEnable(GL_DEPTH_TEST);
                drawCuboid(model, hatchetStickProto, 12, glm::vec3(0.0f), kHandleHalf);
                drawCuboid(model, headProto, 6, kHeadCenter, kHeadHalf, headTint);
            }

            if (renderHeldHatchet) {
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
                // Use camera-right as upHint so hatchet head (+X local) points forward in view.
                glm::mat4 model = buildOrientedModel(center, handleDir, right);
                const float heldHatchetHeadShortOffset = glm::clamp(
                    getRegistryFloat(baseSystem, "HeldHatchetHeadShortOffset", -1.0f / 48.0f),
                    -6.0f / 48.0f,
                    6.0f / 48.0f
                );
                // Keep long-axis hatchet shape while correcting short-axis alignment in first-person.
                const glm::vec3 heldHeadCenter(-kHeadCenter.x, kHeadCenter.y, heldHatchetHeadShortOffset);
                glm::vec3 headTint(1.0f);
                const Entity* headProto = resolveHatchetHeadProtoAndTint(player.hatchetSelectedMaterial, headTint);
                glDisable(GL_DEPTH_TEST);
                drawCuboid(model, hatchetStickProto, 12, glm::vec3(0.0f), kHandleHalf);
                drawCuboid(model, headProto, 6, heldHeadCenter, kHeadHalf, headTint);
                glEnable(GL_DEPTH_TEST);
            }

            glDisable(GL_CULL_FACE);
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
            glBindVertexArray(renderer.selectionVAO);
            glDrawArrays(GL_LINES, 0, renderer.selectionVertexCount);
        }

        if (!mapViewActive && renderer.audioRayShader && renderer.audioRayVAO && renderer.audioRayVertexCount > 0) {
            glEnable(GL_BLEND);
            renderer.audioRayShader->use();
            renderer.audioRayShader->setMat4("view", view);
            renderer.audioRayShader->setMat4("projection", projection);
            glBindVertexArray(renderer.audioRayVAO);
            glLineWidth(1.6f);
            glDrawArrays(GL_LINES, 0, renderer.audioRayVertexCount);
            glLineWidth(1.0f);
        }

        bool crosshairEnabled = true;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("CrosshairEnabled");
            if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second)) {
                crosshairEnabled = std::get<bool>(it->second);
            }
        }
        if (!mapViewActive && crosshairEnabled && renderer.crosshairShader && renderer.crosshairVAO && renderer.crosshairVertexCount > 0) {
            glDisable(GL_DEPTH_TEST);
            renderer.crosshairShader->use();
            glBindVertexArray(renderer.crosshairVAO);
            glLineWidth(1.0f);
            glDrawArrays(GL_LINES, 0, renderer.crosshairVertexCount);
            glLineWidth(1.0f);
            glEnable(GL_DEPTH_TEST);
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
                glDisable(GL_DEPTH_TEST);
                renderer.hudShader->use();
                renderer.hudShader->setFloat("fillAmount", glm::clamp(hud.chargeValue, 0.0f, 1.0f));
                renderer.hudShader->setInt("ready", hud.chargeReady ? 1 : 0);
                renderer.hudShader->setInt("buildModeType", hud.buildModeType);
                renderer.hudShader->setVec3("previewColor", hud.buildPreviewColor);
                renderer.hudShader->setInt("channelIndex", hud.buildChannel);
                renderer.hudShader->setInt("previewTileIndex", hud.buildPreviewTileIndex);
                bindFaceTextureUniforms(*renderer.hudShader);
                glBindVertexArray(renderer.hudVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glEnable(GL_DEPTH_TEST);
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
