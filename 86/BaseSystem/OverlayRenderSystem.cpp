#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace ExpanseBiomeSystemLogic { bool SampleTerrain(const WorldContext& worldCtx, float x, float z, float& outHeight); }
namespace LeyLineSystemLogic { float SampleLeyStress(const WorldContext& worldCtx, float x, float z); float SampleLeyUplift(const WorldContext& worldCtx, float x, float z); }

namespace OverlayRenderSystemLogic {
    namespace {
        struct LeyLineDebugVertex {
            glm::vec3 position;
            glm::vec3 color;
        };

        bool readRegistryBool(const BaseSystem& baseSystem, const char* key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<bool>(it->second)) return fallback;
            return std::get<bool>(it->second);
        }

        int readRegistryInt(const BaseSystem& baseSystem, const char* key, int fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stoi(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        float readRegistryFloat(const BaseSystem& baseSystem, const char* key, float fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stof(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        int alignDownToStep(int value, int step) {
            if (step <= 0) return value;
            int r = value % step;
            if (r < 0) r += step;
            return value - r;
        }

        glm::vec3 stressColor(float stressNorm, float upliftNorm) {
            float s = glm::clamp(std::fabs(stressNorm), 0.0f, 1.0f);
            glm::vec3 color = (stressNorm >= 0.0f)
                ? glm::mix(glm::vec3(0.95f, 0.78f, 0.25f), glm::vec3(1.00f, 0.25f, 0.08f), s)
                : glm::mix(glm::vec3(0.30f, 0.85f, 1.00f), glm::vec3(0.15f, 0.40f, 1.00f), s);
            float brightness = 0.55f + 0.45f * glm::clamp(upliftNorm, 0.0f, 1.0f);
            return glm::clamp(color * brightness, glm::vec3(0.0f), glm::vec3(1.0f));
        }

        void pushLine(std::vector<LeyLineDebugVertex>& vertices,
                      const glm::vec3& a,
                      const glm::vec3& b,
                      const glm::vec3& color) {
            vertices.push_back({a, color});
            vertices.push_back({b, color});
        }

        std::vector<LeyLineDebugVertex> buildLeyLineDebugVertices(const BaseSystem& baseSystem) {
            std::vector<LeyLineDebugVertex> vertices;
            if (!baseSystem.world || !baseSystem.player) return vertices;

            const WorldContext& world = *baseSystem.world;
            const LeyLineContext& ley = world.leyLines;
            if (!ley.enabled || !ley.loaded) return vertices;

            const int radius = std::clamp(readRegistryInt(baseSystem, "LeyLineDebugRadius", 56), 8, 256);
            const int defaultStep = std::max(4, static_cast<int>(std::round(std::max(2.0f, ley.sampleStep))));
            const int step = std::clamp(readRegistryInt(baseSystem, "LeyLineDebugStep", defaultStep), 2, 64);
            const float yOffset = glm::clamp(readRegistryFloat(baseSystem, "LeyLineDebugYOffset", 1.25f), -8.0f, 32.0f);
            const float heightScale = glm::clamp(readRegistryFloat(baseSystem, "LeyLineDebugHeightScale", 7.5f), 0.25f, 64.0f);
            const float maxHeight = glm::clamp(readRegistryFloat(baseSystem, "LeyLineDebugMaxHeight", 16.0f), 0.25f, 128.0f);
            const bool showGrid = readRegistryBool(baseSystem, "LeyLineDebugGrid", true);
            const float stressClamp = std::max(1e-4f, ley.stressClamp);
            const float upliftMax = std::max(1e-4f, ley.upliftMax);

            const glm::vec3 cameraPos = baseSystem.player->cameraPosition;
            const int centerX = alignDownToStep(static_cast<int>(std::floor(cameraPos.x)), step);
            const int centerZ = alignDownToStep(static_cast<int>(std::floor(cameraPos.z)), step);
            const int minX = centerX - radius;
            const int maxX = centerX + radius;
            const int minZ = centerZ - radius;
            const int maxZ = centerZ + radius;
            const int countX = ((maxX - minX) / step) + 1;
            const int countZ = ((maxZ - minZ) / step) + 1;
            if (countX <= 0 || countZ <= 0) return vertices;

            struct SampleNode {
                bool valid = false;
                glm::vec3 tip = glm::vec3(0.0f);
                glm::vec3 color = glm::vec3(1.0f);
            };
            std::vector<SampleNode> nodes(static_cast<size_t>(countX * countZ));
            auto nodeAt = [&](int gx, int gz) -> SampleNode& {
                return nodes[static_cast<size_t>(gz * countX + gx)];
            };

            vertices.reserve(static_cast<size_t>(countX * countZ) * 8u);
            for (int gz = 0; gz < countZ; ++gz) {
                int z = minZ + gz * step;
                for (int gx = 0; gx < countX; ++gx) {
                    int x = minX + gx * step;
                    float terrainHeight = 0.0f;
                    bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(world, static_cast<float>(x), static_cast<float>(z), terrainHeight);
                    if (!isLand) terrainHeight = world.expanse.waterSurface;

                    float stress = LeyLineSystemLogic::SampleLeyStress(world, static_cast<float>(x), static_cast<float>(z));
                    float uplift = LeyLineSystemLogic::SampleLeyUplift(world, static_cast<float>(x), static_cast<float>(z));
                    float stressNorm = glm::clamp(stress / stressClamp, -1.0f, 1.0f);
                    float upliftNorm = glm::clamp(uplift / upliftMax, 0.0f, 1.0f);
                    glm::vec3 color = stressColor(stressNorm, upliftNorm);

                    float barHeight = 0.35f + (std::fabs(stressNorm) * heightScale) + (upliftNorm * heightScale * 0.45f);
                    barHeight = glm::clamp(barHeight, 0.35f, maxHeight);
                    glm::vec3 base(static_cast<float>(x), terrainHeight + yOffset, static_cast<float>(z));
                    glm::vec3 tip = base + glm::vec3(0.0f, barHeight, 0.0f);

                    pushLine(vertices, base, tip, color);
                    float crossHalf = glm::clamp(static_cast<float>(step) * 0.22f, 0.30f, 1.8f);
                    pushLine(vertices, tip + glm::vec3(-crossHalf, 0.0f, 0.0f), tip + glm::vec3(crossHalf, 0.0f, 0.0f), color * 0.85f);
                    pushLine(vertices, tip + glm::vec3(0.0f, 0.0f, -crossHalf), tip + glm::vec3(0.0f, 0.0f, crossHalf), color * 0.85f);

                    SampleNode& node = nodeAt(gx, gz);
                    node.valid = true;
                    node.tip = tip;
                    node.color = color;
                }
            }

            if (showGrid) {
                for (int gz = 0; gz < countZ; ++gz) {
                    for (int gx = 0; gx < countX; ++gx) {
                        const SampleNode& a = nodeAt(gx, gz);
                        if (!a.valid) continue;
                        if (gx + 1 < countX) {
                            const SampleNode& b = nodeAt(gx + 1, gz);
                            if (b.valid) {
                                pushLine(vertices, a.tip, b.tip, glm::mix(a.color, b.color, 0.5f) * 0.72f);
                            }
                        }
                        if (gz + 1 < countZ) {
                            const SampleNode& b = nodeAt(gx, gz + 1);
                            if (b.valid) {
                                pushLine(vertices, a.tip, b.tip, glm::mix(a.color, b.color, 0.5f) * 0.72f);
                            }
                        }
                    }
                }
            }

            return vertices;
        }
    } // namespace

    void RenderOverlays(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt; (void)win;
        if (!baseSystem.renderer || !baseSystem.player) return;
        RendererContext& renderer = *baseSystem.renderer;
        PlayerContext& player = *baseSystem.player;

        float time = static_cast<float>(glfwGetTime());
        glm::mat4 view = player.viewMatrix;
        glm::mat4 projection = player.projectionMatrix;
        glm::vec3 playerPos = player.cameraPosition;

        bool blockSelectionVisualEnabled = true;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("BlockSelectionVisualEnabled");
            if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second)) {
                blockSelectionVisualEnabled = std::get<bool>(it->second);
            }
        }
        if (blockSelectionVisualEnabled
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

        if (renderer.audioRayShader && renderer.audioRayVAO && renderer.audioRayVertexCount > 0) {
            glEnable(GL_BLEND);
            renderer.audioRayShader->use();
            renderer.audioRayShader->setMat4("view", view);
            renderer.audioRayShader->setMat4("projection", projection);
            glBindVertexArray(renderer.audioRayVAO);
            glLineWidth(1.6f);
            glDrawArrays(GL_LINES, 0, renderer.audioRayVertexCount);
            glLineWidth(1.0f);
        }

        const bool leyLineDebugVisualizerEnabled = readRegistryBool(baseSystem, "LeyLineDebugVisualizerEnabled", false);
        if (leyLineDebugVisualizerEnabled
            && renderer.audioRayShader
            && renderer.leyLineDebugVAO
            && renderer.leyLineDebugVBO) {
            std::vector<LeyLineDebugVertex> vertices = buildLeyLineDebugVertices(baseSystem);
            renderer.leyLineDebugVertexCount = static_cast<int>(vertices.size());
            if (!vertices.empty()) {
                glBindBuffer(GL_ARRAY_BUFFER, renderer.leyLineDebugVBO);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(vertices.size() * sizeof(LeyLineDebugVertex)),
                             vertices.data(),
                             GL_DYNAMIC_DRAW);
                glDepthMask(GL_FALSE);
                glEnable(GL_BLEND);
                renderer.audioRayShader->use();
                renderer.audioRayShader->setMat4("view", view);
                renderer.audioRayShader->setMat4("projection", projection);
                glBindVertexArray(renderer.leyLineDebugVAO);
                glLineWidth(1.4f);
                glDrawArrays(GL_LINES, 0, renderer.leyLineDebugVertexCount);
                glLineWidth(1.0f);
                glDepthMask(GL_TRUE);
            }
        } else {
            renderer.leyLineDebugVertexCount = 0;
        }

        GemSystemLogic::RenderGems(baseSystem, prototypes, dt, win);
        FishingSystemLogic::RenderFishing(baseSystem, prototypes, dt, win);
        ColorEmotionSystemLogic::RenderColorEmotions(baseSystem, prototypes, dt, win);

        bool crosshairEnabled = true;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("CrosshairEnabled");
            if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second)) {
                crosshairEnabled = std::get<bool>(it->second);
            }
        }
        if (crosshairEnabled && renderer.crosshairShader && renderer.crosshairVAO && renderer.crosshairVertexCount > 0) {
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
        if (legacyMeterEnabled && baseSystem.hud && renderer.hudShader && renderer.hudVAO) {
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
                renderer.hudShader->setInt("atlasEnabled", (renderer.atlasTexture != 0 && renderer.atlasTilesPerRow > 0 && renderer.atlasTilesPerCol > 0) ? 1 : 0);
                renderer.hudShader->setVec2("atlasTileSize", glm::vec2(renderer.atlasTileSize));
                renderer.hudShader->setVec2("atlasTextureSize", glm::vec2(renderer.atlasTextureSize));
                renderer.hudShader->setInt("tilesPerRow", renderer.atlasTilesPerRow);
                renderer.hudShader->setInt("tilesPerCol", renderer.atlasTilesPerCol);
                renderer.hudShader->setInt("atlasTexture", 0);
                if (renderer.atlasTexture != 0) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, renderer.atlasTexture);
                }
                glBindVertexArray(renderer.hudVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glEnable(GL_DEPTH_TEST);
            }
        }
    }
}
