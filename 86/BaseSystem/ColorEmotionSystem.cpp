#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace ColorEmotionSystemLogic {

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

        int resolveWaterPrototypeID(const std::vector<Entity>& prototypes) {
            for (const auto& proto : prototypes) {
                if (proto.name == "Water") return proto.prototypeID;
            }
            return -1;
        }

        int resolveLeafPrototypeID(const std::vector<Entity>& prototypes) {
            for (const auto& proto : prototypes) {
                if (proto.name == "Leaf") return proto.prototypeID;
            }
            return -1;
        }

        bool cameraInWater(const VoxelWorldContext& voxelWorld, int waterPrototypeID, const glm::vec3& cameraPosition) {
            if (!voxelWorld.enabled || waterPrototypeID < 0) return false;
            auto sampleAtOffset = [&](float yOffset) {
                glm::vec3 p = cameraPosition + glm::vec3(0.0f, yOffset, 0.0f);
                glm::ivec3 cell(
                    static_cast<int>(std::floor(p.x)),
                    static_cast<int>(std::floor(p.y)),
                    static_cast<int>(std::floor(p.z))
                );
                return voxelWorld.getBlockWorld(cell) == static_cast<uint32_t>(waterPrototypeID);
            };
            return sampleAtOffset(0.00f) || sampleAtOffset(0.18f) || sampleAtOffset(-0.18f);
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

        glm::vec3 resolveLeafEmotionColor(const BaseSystem& baseSystem) {
            (void)baseSystem;
            // Match generated pine leaf tint exactly (#127557).
            return glm::vec3(18.0f / 255.0f, 117.0f / 255.0f, 87.0f / 255.0f);
        }

        bool findWaterSurfaceInColumn(const VoxelWorldContext& voxelWorld,
                                      int waterPrototypeID,
                                      int x,
                                      int z,
                                      int minY,
                                      int maxY,
                                      float& outSurfaceY) {
            if (!voxelWorld.enabled || waterPrototypeID < 0) return false;
            if (maxY < minY) std::swap(minY, maxY);
            for (int y = maxY; y >= minY; --y) {
                glm::ivec3 cell(x, y, z);
                if (voxelWorld.getBlockWorld(cell) != static_cast<uint32_t>(waterPrototypeID)) continue;
                glm::ivec3 above(x, y + 1, z);
                if (voxelWorld.getBlockWorld(above) == static_cast<uint32_t>(waterPrototypeID)) continue;
                if (voxelWorld.getBlockWorld(above) != 0) continue;
                outSurfaceY = static_cast<float>(y) + 1.02f;
                return true;
            }
            return false;
        }

        bool findLocalWaterSurfaceNearCamera(const VoxelWorldContext& voxelWorld,
                                             int waterPrototypeID,
                                             const glm::vec3& cameraPosition,
                                             bool preferAboveSurface,
                                             float& outSurfaceY) {
            int cx = static_cast<int>(std::floor(cameraPosition.x));
            int cz = static_cast<int>(std::floor(cameraPosition.z));
            int minY = static_cast<int>(std::floor(cameraPosition.y)) - 24;
            int maxY = static_cast<int>(std::floor(cameraPosition.y)) + 24;
            float bestScore = std::numeric_limits<float>::max();
            float bestSurfaceY = 0.0f;
            bool found = false;

            for (int ring = 0; ring <= 2; ++ring) {
                for (int dz = -ring; dz <= ring; ++dz) {
                    for (int dx = -ring; dx <= ring; ++dx) {
                        if (ring > 0 && std::abs(dx) != ring && std::abs(dz) != ring) continue;
                        float candidateY = 0.0f;
                        if (!findWaterSurfaceInColumn(voxelWorld, waterPrototypeID, cx + dx, cz + dz, minY, maxY, candidateY)) continue;
                        float vertical = candidateY - cameraPosition.y;
                        if (preferAboveSurface && vertical < -0.60f) continue;
                        float horiz = std::sqrt(static_cast<float>(dx * dx + dz * dz));
                        float score = horiz * 0.65f + std::abs(vertical);
                        if (preferAboveSurface && vertical >= 0.0f) score -= 0.25f;
                        if (!found || score < bestScore) {
                            found = true;
                            bestScore = score;
                            bestSurfaceY = candidateY;
                        }
                    }
                }
            }

            if (!found) return false;
            outSurfaceY = bestSurfaceY;
            return true;
        }

        float estimateWaterlineUV(const PlayerContext& player, float surfaceY, float depthBelowSurface) {
            glm::vec3 forward(
                std::cos(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch)),
                std::sin(glm::radians(player.cameraPitch)),
                std::sin(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch))
            );
            if (glm::length(forward) < 1e-4f) forward = glm::vec3(0.0f, 0.0f, -1.0f);
            forward = glm::normalize(forward);
            glm::vec3 forwardFlat(forward.x, 0.0f, forward.z);
            if (glm::length(forwardFlat) < 1e-4f) forwardFlat = glm::vec3(0.0f, 0.0f, -1.0f);
            forwardFlat = glm::normalize(forwardFlat);

            float sampleDistance = glm::clamp(7.0f + depthBelowSurface * 6.5f, 6.0f, 22.0f);
            glm::vec3 samplePoint = player.cameraPosition + forwardFlat * sampleDistance;
            samplePoint.y = surfaceY;

            glm::vec4 clip = player.projectionMatrix * player.viewMatrix * glm::vec4(samplePoint, 1.0f);
            if (clip.w <= 1e-4f) return 0.62f;
            float ndcY = clip.y / clip.w;
            return glm::clamp(0.5f + 0.5f * ndcY, 0.05f, 0.95f);
        }

        bool findWaterSurfaceCellNearY(const VoxelWorldContext& voxelWorld,
                                       int waterPrototypeID,
                                       int x,
                                       int z,
                                       int centerY,
                                       int halfRange,
                                       int& outWaterTopCellY) {
            float surfaceY = 0.0f;
            if (!findWaterSurfaceInColumn(voxelWorld, waterPrototypeID, x, z, centerY - halfRange, centerY + halfRange, surfaceY)) {
                return false;
            }
            outWaterTopCellY = static_cast<int>(std::floor(surfaceY - 1.0f));
            return true;
        }

        glm::vec3 colorForChargeMode(BuildModeType mode, BlockChargeAction action, float t, bool fullyCharged) {
            t = glm::clamp(t, 0.0f, 1.0f);
            const glm::vec3 lime(0.10f, 0.85f, 0.20f);
            glm::vec3 a = lime;
            glm::vec3 b(1.00f, 0.55f, 0.10f);
            if (mode == BuildModeType::Destroy) {
                a = glm::vec3(0.00f, 1.00f, 1.00f);
                b = glm::vec3(1.00f, 0.00f, 1.00f);
            } else if (mode == BuildModeType::Fishing) {
                a = glm::vec3(0.00f, 0.88f, 1.00f);
                b = glm::vec3(1.00f, 0.52f, 0.00f);
            } else if (mode == BuildModeType::Bouldering) {
                if (action == BlockChargeAction::BoulderSecondary) {
                    a = glm::vec3(0.15f, 0.86f, 0.22f);  // green
                    b = glm::vec3(0.16f, 0.48f, 1.00f);  // blue
                } else {
                    a = glm::vec3(1.00f, 0.55f, 0.10f);  // orange
                    b = glm::vec3(0.66f, 0.26f, 0.98f);  // violet
                }
            }
            if (fullyCharged && (mode == BuildModeType::Pickup || mode == BuildModeType::Destroy)) {
                return lime;
            }
            return glm::mix(a, b, t);
        }

        bool isChargeBuildMode(BuildModeType mode) {
            return mode == BuildModeType::Pickup
                || mode == BuildModeType::Destroy
                || mode == BuildModeType::Fishing
                || mode == BuildModeType::Bouldering;
        }

        float normalizeAngleRad(float angle) {
            const float kTau = 6.28318530718f;
            angle = std::fmod(angle, kTau);
            if (angle < 0.0f) angle += kTau;
            return angle;
        }

        float shortestAngleDelta(float from, float to) {
            const float kPi = 3.14159265359f;
            const float kTau = 6.28318530718f;
            float delta = std::fmod((to - from) + kPi, kTau);
            if (delta < 0.0f) delta += kTau;
            return delta - kPi;
        }
    }

    void UpdateColorEmotions(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)win;
        if (!baseSystem.player || !baseSystem.colorEmotion) return;

        PlayerContext& player = *baseSystem.player;
        ColorEmotionContext& emotion = *baseSystem.colorEmotion;

        bool enabled = getRegistryBool(baseSystem, "ColorEmotionEnabled", true);
        emotion.enabled = enabled;
        if (!enabled) {
            emotion.intensity = std::max(0.0f, emotion.intensity - dt * 8.0f);
            emotion.active = emotion.intensity > 0.001f;
            if (baseSystem.audio) {
                baseSystem.audio->headUnderwaterMix.store(0.0f, std::memory_order_relaxed);
            }
            return;
        }

        const float chargeMaxIntensity = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionChargeMaxIntensity", 0.86f), 0.0f, 1.0f);
        const float underwaterIntensityMax = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionUnderwaterIntensity", 0.58f), 0.0f, 1.0f);
        const bool underwaterWaterlineEnabled = getRegistryBool(baseSystem, "ColorEmotionUnderwaterWaterlineEnabled", false);
        const float underwaterLineStrengthMax = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionUnderwaterLineStrength", 0.95f), 0.0f, 2.0f);
        const float underwaterHazeStrengthMax = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionUnderwaterHazeStrength", 0.65f), 0.0f, 2.0f);
        const bool leafCanopyEnabled = getRegistryBool(baseSystem, "ColorEmotionLeafCanopyEnabled", true);
        const float leafCanopyIntensityMax = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionLeafCanopyIntensity", 0.34f), 0.0f, 1.0f);
        const float smoothing = std::max(0.1f, getRegistryFloat(baseSystem, "ColorEmotionSmoothing", 8.5f));
        const float pulseSpeed = std::max(0.0f, getRegistryFloat(baseSystem, "ColorEmotionPulseSpeed", 2.2f));
        const bool fishingHintEnabled = getRegistryBool(baseSystem, "FishingDirectionHintEnabled", true);
        const float fishingHintStrengthMax = glm::clamp(getRegistryFloat(baseSystem, "FishingDirectionHintStrength", 0.88f), 0.0f, 1.0f);
        const float fishingHintIntensityMax = glm::clamp(getRegistryFloat(baseSystem, "FishingDirectionHintIntensity", 0.56f), 0.0f, 1.0f);
        const float fishingHintSmoothing = std::max(0.1f, getRegistryFloat(baseSystem, "FishingDirectionHintSmoothing", 9.0f));
        const float headUnderwaterLowpassHz = glm::clamp(getRegistryFloat(baseSystem, "HeadSpeakerUnderwaterLowpassHz", 500.0f), 20.0f, 20000.0f);
        const float headUnderwaterLowpassStrength = glm::clamp(getRegistryFloat(baseSystem, "HeadSpeakerUnderwaterLowpassStrength", 1.0f), 0.0f, 1.0f);

        bool underwater = false;
        bool inLeaves = false;
        float waterSurfaceY = player.cameraPosition.y + 0.85f;
        if (baseSystem.voxelWorld) {
            int waterPrototypeID = resolveWaterPrototypeID(prototypes);
            underwater = cameraInWater(*baseSystem.voxelWorld, waterPrototypeID, player.cameraPosition);
            int leafPrototypeID = resolveLeafPrototypeID(prototypes);
            inLeaves = leafCanopyEnabled && cameraInLeaves(*baseSystem.voxelWorld, leafPrototypeID, player.cameraPosition);
            if (underwater && underwaterWaterlineEnabled) {
                bool foundSurface = findLocalWaterSurfaceNearCamera(*baseSystem.voxelWorld, waterPrototypeID, player.cameraPosition, true, waterSurfaceY);
                if (!foundSurface) {
                    waterSurfaceY = player.cameraPosition.y + 0.85f;
                }
            }
        }

        bool chargeMode = isChargeBuildMode(player.buildMode);
        float chargeValue = glm::clamp(player.blockChargeValue, 0.0f, 1.0f);
        bool chargeActive = chargeMode && (player.isChargingBlock || chargeValue > 0.001f);
        BuildModeType chargeVisualMode = player.buildMode;
        BlockChargeAction chargeVisualAction = player.blockChargeAction;
        if (player.buildMode == BuildModeType::Pickup) {
            if (player.blockChargeAction == BlockChargeAction::Destroy) {
                chargeVisualMode = BuildModeType::Destroy;
            } else if (player.blockChargeAction == BlockChargeAction::Pickup) {
                chargeVisualMode = BuildModeType::Pickup;
            }
        }

        float chargeIntensity = chargeActive ? (0.08f + chargeValue * chargeMaxIntensity) : 0.0f;
        float underwaterIntensity = underwater ? underwaterIntensityMax : 0.0f;
        float leafCanopyIntensity = inLeaves ? leafCanopyIntensityMax : 0.0f;
        bool fishingHintActive = false;
        float fishingHintAngleTarget = emotion.fishingDirectionAngle;
        if (fishingHintEnabled
            && player.buildMode == BuildModeType::Fishing
            && player.rightMouseDown
            && baseSystem.fishing
            && baseSystem.fishing->dailySchoolValid) {
            glm::vec3 toHole = baseSystem.fishing->dailySchoolPosition - player.cameraPosition;
            toHole.y = 0.0f;
            if (glm::length(toHole) > 0.01f) {
                toHole = glm::normalize(toHole);
                glm::vec3 forward(
                    std::cos(glm::radians(player.cameraYaw)),
                    0.0f,
                    std::sin(glm::radians(player.cameraYaw))
                );
                if (glm::length(forward) < 1e-4f) forward = glm::vec3(0.0f, 0.0f, -1.0f);
                forward = glm::normalize(forward);
                float crossY = forward.x * toHole.z - forward.z * toHole.x;
                float dotFT = glm::clamp(glm::dot(forward, toHole), -1.0f, 1.0f);
                float yawDelta = std::atan2(crossY, dotFT);
                fishingHintAngleTarget = 1.5707963f - yawDelta; // top of vignette means "forward"
                fishingHintActive = true;
            }
        }
        float fishingHintIntensity = fishingHintActive ? fishingHintIntensityMax : 0.0f;
        float fishingHintStrengthTarget = fishingHintActive ? fishingHintStrengthMax : 0.0f;

        const bool fullyCharged = player.blockChargeReady || chargeValue >= 0.999f;
        glm::vec3 chargeColor = colorForChargeMode(chargeVisualMode, chargeVisualAction, chargeValue, fullyCharged);
        glm::vec3 waterColor(0.10f, 0.46f, 0.96f);
        glm::vec3 leafCanopyColor = resolveLeafEmotionColor(baseSystem);
        glm::vec3 fishingBaseColor(0.00f, 0.88f, 1.00f);
        float totalWeight = chargeIntensity + underwaterIntensity + leafCanopyIntensity + fishingHintIntensity;
        glm::vec3 targetColor = totalWeight > 1e-5f
            ? (chargeColor * chargeIntensity
               + waterColor * underwaterIntensity
               + leafCanopyColor * leafCanopyIntensity
               + fishingBaseColor * fishingHintIntensity) / totalWeight
            : glm::vec3(0.0f);
        float targetIntensity = glm::clamp(
            chargeIntensity
            + underwaterIntensity * (1.0f - 0.35f * chargeIntensity)
            + leafCanopyIntensity * (1.0f - 0.20f * underwaterIntensity)
            + fishingHintIntensity * (1.0f - 0.15f * underwaterIntensity),
            0.0f,
            1.0f
        );

        float blendAlpha = glm::clamp(dt * smoothing, 0.0f, 1.0f);
        emotion.color = glm::mix(emotion.color, targetColor, blendAlpha);
        emotion.intensity = glm::mix(emotion.intensity, targetIntensity, blendAlpha);
        emotion.chargeValue = chargeValue;
        emotion.underwater = underwater;
        emotion.underwaterMix = glm::mix(emotion.underwaterMix, underwater ? 1.0f : 0.0f, blendAlpha);
        float depthTarget = 0.0f;
        float lineUvTarget = 0.62f;
        float lineStrengthTarget = 0.0f;
        float hazeStrengthTarget = 0.0f;
        if (underwater && underwaterWaterlineEnabled) {
            depthTarget = glm::max(0.0f, waterSurfaceY - player.cameraPosition.y);
            lineUvTarget = estimateWaterlineUV(player, waterSurfaceY, depthTarget);
            float depthMix = glm::clamp(depthTarget / 2.2f, 0.0f, 1.0f);
            lineStrengthTarget = underwaterLineStrengthMax * (0.62f + 0.38f * depthMix);
            hazeStrengthTarget = underwaterHazeStrengthMax * (0.52f + 0.48f * depthMix);
        }
        emotion.underwaterDepth = glm::mix(emotion.underwaterDepth, depthTarget, blendAlpha);
        float surfaceTarget = underwater ? waterSurfaceY : (player.cameraPosition.y + 0.85f);
        emotion.underwaterSurfaceY = glm::mix(emotion.underwaterSurfaceY, surfaceTarget, blendAlpha);
        emotion.underwaterLineUV = glm::mix(emotion.underwaterLineUV, lineUvTarget, blendAlpha);
        emotion.underwaterLineStrength = glm::mix(emotion.underwaterLineStrength, lineStrengthTarget, blendAlpha);
        emotion.underwaterHazeStrength = glm::mix(emotion.underwaterHazeStrength, hazeStrengthTarget, blendAlpha);
        if (fishingHintActive) {
            float angleBlend = glm::clamp(dt * fishingHintSmoothing, 0.0f, 1.0f);
            emotion.fishingDirectionAngle = normalizeAngleRad(
                emotion.fishingDirectionAngle + shortestAngleDelta(emotion.fishingDirectionAngle, fishingHintAngleTarget) * angleBlend
            );
        }
        emotion.fishingDirectionHint = fishingHintActive;
        emotion.fishingDirectionStrength = glm::mix(emotion.fishingDirectionStrength, fishingHintStrengthTarget, blendAlpha);
        emotion.mode = static_cast<int>(chargeActive ? chargeVisualMode : player.buildMode);
        emotion.chargeAction = static_cast<int>(chargeVisualAction);
        emotion.pulse += dt * pulseSpeed;
        if (emotion.pulse > 1000.0f) emotion.pulse = std::fmod(emotion.pulse, 1000.0f);
        emotion.timeSeconds = static_cast<float>(glfwGetTime());
        emotion.active = emotion.intensity > 0.001f || emotion.fishingDirectionStrength > 0.001f;
        if (baseSystem.audio) {
            baseSystem.audio->headUnderwaterMix.store(emotion.underwaterMix, std::memory_order_relaxed);
            baseSystem.audio->headUnderwaterLowpassHz.store(headUnderwaterLowpassHz, std::memory_order_relaxed);
            baseSystem.audio->headUnderwaterLowpassStrength.store(headUnderwaterLowpassStrength, std::memory_order_relaxed);
        }
    }

    void RenderColorEmotions(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt;
        if (!baseSystem.renderer || !baseSystem.colorEmotion || !baseSystem.player) return;

        RendererContext& renderer = *baseSystem.renderer;
        ColorEmotionContext& emotion = *baseSystem.colorEmotion;
        PlayerContext& player = *baseSystem.player;
        if (!emotion.enabled || !emotion.active || emotion.intensity <= 0.001f) return;

        const bool underwaterWaterlineEnabled = getRegistryBool(baseSystem, "ColorEmotionUnderwaterWaterlineEnabled", false);
        if (underwaterWaterlineEnabled
            && emotion.underwaterMix > 0.01f
            && baseSystem.voxelWorld
            && renderer.audioRayShader
            && renderer.audioRayVAO
            && renderer.audioRayVBO) {
            const int waterPrototypeID = resolveWaterPrototypeID(prototypes);
            if (waterPrototypeID >= 0) {
                const VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
                const int cueRadius = std::clamp(getRegistryInt(baseSystem, "ColorEmotionUnderwaterSurfaceCueRadius", 16), 3, 48);
                const int cueYRange = std::clamp(getRegistryInt(baseSystem, "ColorEmotionUnderwaterSurfaceCueYSearchRange", 72), 4, 256);
                const float cueHeightOffset = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionUnderwaterSurfaceCueHeightOffset", 0.035f), -0.2f, 0.2f);
                const float cueLineWidth = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionUnderwaterSurfaceCueLineWidth", 2.2f), 1.0f, 5.0f);
                const int gridSize = cueRadius * 2 + 1;
                const int centerX = static_cast<int>(std::floor(player.cameraPosition.x));
                const int centerZ = static_cast<int>(std::floor(player.cameraPosition.z));
                const int centerY = static_cast<int>(std::floor(emotion.underwaterSurfaceY - 0.02f));

                std::vector<int> topWaterY(static_cast<size_t>(gridSize * gridSize), std::numeric_limits<int>::min());
                auto at = [&](int gx, int gz) -> int& { return topWaterY[static_cast<size_t>(gz * gridSize + gx)]; };
                for (int gz = 0; gz < gridSize; ++gz) {
                    for (int gx = 0; gx < gridSize; ++gx) {
                        int wx = centerX + (gx - cueRadius);
                        int wz = centerZ + (gz - cueRadius);
                        int yTop = 0;
                        if (findWaterSurfaceCellNearY(voxelWorld, waterPrototypeID, wx, wz, centerY, cueYRange, yTop)) {
                            at(gx, gz) = yTop;
                        }
                    }
                }

                struct CueVertex { glm::vec3 pos; glm::vec3 color; };
                std::vector<CueVertex> cueVerts;
                cueVerts.reserve(static_cast<size_t>(gridSize * gridSize * 8));

                auto emitEdge = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
                    cueVerts.push_back({a, c});
                    cueVerts.push_back({b, c});
                };
                auto neighborTop = [&](int gx, int gz) -> int {
                    if (gx < 0 || gz < 0 || gx >= gridSize || gz >= gridSize) return std::numeric_limits<int>::min();
                    return at(gx, gz);
                };

                for (int gz = 0; gz < gridSize; ++gz) {
                    for (int gx = 0; gx < gridSize; ++gx) {
                        int topY = at(gx, gz);
                        if (topY == std::numeric_limits<int>::min()) continue;

                        int wx = centerX + (gx - cueRadius);
                        int wz = centerZ + (gz - cueRadius);
                        float y = static_cast<float>(topY) + 1.02f + cueHeightOffset;
                        float glow = 0.75f + 0.25f * std::sin(emotion.pulse * 2.1f + static_cast<float>(wx + wz) * 0.27f);
                        glm::vec3 c(0.55f * glow, 0.82f * glow, 1.0f * glow);

                        if (neighborTop(gx - 1, gz) != topY) {
                            emitEdge(glm::vec3(static_cast<float>(wx), y, static_cast<float>(wz)),
                                     glm::vec3(static_cast<float>(wx), y, static_cast<float>(wz + 1)),
                                     c);
                        }
                        if (neighborTop(gx + 1, gz) != topY) {
                            emitEdge(glm::vec3(static_cast<float>(wx + 1), y, static_cast<float>(wz)),
                                     glm::vec3(static_cast<float>(wx + 1), y, static_cast<float>(wz + 1)),
                                     c);
                        }
                        if (neighborTop(gx, gz - 1) != topY) {
                            emitEdge(glm::vec3(static_cast<float>(wx), y, static_cast<float>(wz)),
                                     glm::vec3(static_cast<float>(wx + 1), y, static_cast<float>(wz)),
                                     c);
                        }
                        if (neighborTop(gx, gz + 1) != topY) {
                            emitEdge(glm::vec3(static_cast<float>(wx), y, static_cast<float>(wz + 1)),
                                     glm::vec3(static_cast<float>(wx + 1), y, static_cast<float>(wz + 1)),
                                     c);
                        }
                    }
                }

                if (!cueVerts.empty()) {
                    glEnable(GL_DEPTH_TEST);
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    renderer.audioRayShader->use();
                    renderer.audioRayShader->setMat4("view", player.viewMatrix);
                    renderer.audioRayShader->setMat4("projection", player.projectionMatrix);
                    glBindVertexArray(renderer.audioRayVAO);
                    glBindBuffer(GL_ARRAY_BUFFER, renderer.audioRayVBO);
                    glBufferData(GL_ARRAY_BUFFER,
                                 static_cast<GLsizeiptr>(cueVerts.size() * sizeof(CueVertex)),
                                 cueVerts.data(),
                                 GL_DYNAMIC_DRAW);
                    glLineWidth(cueLineWidth);
                    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(cueVerts.size()));
                    glLineWidth(1.0f);
                }
            }
        }

        if (!renderer.colorEmotionShader || !renderer.colorEmotionVAO) return;

        int fbWidth = 1;
        int fbHeight = 1;
        if (win) glfwGetFramebufferSize(win, &fbWidth, &fbHeight);
        float aspectRatio = (fbHeight > 0) ? (static_cast<float>(fbWidth) / static_cast<float>(fbHeight)) : 1.0f;

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        const float opacityScale = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionOpacityScale", 0.62f), 0.0f, 1.0f);
        const float fullChargeSpinSpeed = getRegistryFloat(baseSystem, "ColorEmotionChargeFullSpinSpeed", 3.0f);
        const glm::vec3 lime(0.10f, 0.85f, 0.20f);
        const glm::vec3 orange(1.00f, 0.55f, 0.10f);
        const glm::vec3 magenta(1.00f, 0.00f, 1.00f);
        const glm::vec3 violet(0.66f, 0.26f, 0.98f);
        const glm::vec3 blue(0.16f, 0.48f, 1.00f);
        const glm::vec3 blueGreen(0.00f, 0.84f, 0.78f);
        const bool fullCharge = emotion.chargeValue >= 0.999f;
        const bool modePickup = emotion.mode == static_cast<int>(BuildModeType::Pickup);
        const bool modeDestroy = emotion.mode == static_cast<int>(BuildModeType::Destroy);
        const bool modeBouldering = emotion.mode == static_cast<int>(BuildModeType::Bouldering);
        const bool dualTonePickup = fullCharge && emotion.mode == static_cast<int>(BuildModeType::Pickup);
        const bool dualToneDestroy = fullCharge && emotion.mode == static_cast<int>(BuildModeType::Destroy);
        const bool dualToneBoulderPrimary = fullCharge
            && modeBouldering
            && emotion.chargeAction == static_cast<int>(BlockChargeAction::BoulderPrimary);
        const bool dualToneBoulderSecondary = fullCharge
            && modeBouldering
            && emotion.chargeAction == static_cast<int>(BlockChargeAction::BoulderSecondary);
        const bool dualToneEnabled = dualTonePickup || dualToneDestroy || dualToneBoulderPrimary || dualToneBoulderSecondary;
        glm::vec3 dualTonePrimary = lime;
        glm::vec3 dualToneSecondary = orange;
        if (dualToneDestroy || modeDestroy) {
            dualTonePrimary = lime;
            dualToneSecondary = magenta;
        } else if (dualToneBoulderPrimary) {
            dualTonePrimary = orange;
            dualToneSecondary = violet;
        } else if (dualToneBoulderSecondary) {
            dualTonePrimary = blue;
            dualToneSecondary = blueGreen;
        } else if (modePickup) {
            dualTonePrimary = lime;
            dualToneSecondary = orange;
        }

        renderer.colorEmotionShader->use();
        renderer.colorEmotionShader->setVec3("emotionColor", emotion.color);
        renderer.colorEmotionShader->setFloat("emotionIntensity", glm::clamp(emotion.intensity, 0.0f, 1.0f));
        renderer.colorEmotionShader->setFloat("pulse", emotion.pulse);
        renderer.colorEmotionShader->setFloat("chargeAmount", glm::clamp(emotion.chargeValue, 0.0f, 1.0f));
        renderer.colorEmotionShader->setFloat("underwaterMix", glm::clamp(emotion.underwaterMix, 0.0f, 1.0f));
        renderer.colorEmotionShader->setFloat("underwaterDepth", glm::max(0.0f, emotion.underwaterDepth));
        renderer.colorEmotionShader->setFloat("underwaterLineUV", glm::clamp(emotion.underwaterLineUV, 0.0f, 1.0f));
        renderer.colorEmotionShader->setFloat("underwaterLineStrength", glm::max(0.0f, emotion.underwaterLineStrength));
        renderer.colorEmotionShader->setFloat("underwaterHazeStrength", glm::max(0.0f, emotion.underwaterHazeStrength));
        renderer.colorEmotionShader->setFloat("directionHintStrength", glm::clamp(emotion.fishingDirectionStrength, 0.0f, 1.0f));
        renderer.colorEmotionShader->setFloat("directionHintAngle", normalizeAngleRad(emotion.fishingDirectionAngle));
        renderer.colorEmotionShader->setFloat("directionHintWidth", glm::clamp(getRegistryFloat(baseSystem, "FishingDirectionHintWidth", 0.26f), 0.05f, 1.2f));
        renderer.colorEmotionShader->setVec3("directionHintBaseColor", glm::vec3(0.00f, 0.88f, 1.00f));
        renderer.colorEmotionShader->setVec3("directionHintAccentColor", glm::vec3(1.00f, 0.52f, 0.00f));
        renderer.colorEmotionShader->setFloat("timeSeconds", emotion.timeSeconds);
        renderer.colorEmotionShader->setFloat("aspectRatio", std::max(0.01f, aspectRatio));
        renderer.colorEmotionShader->setFloat("opacityScale", opacityScale);
        renderer.colorEmotionShader->setInt("chargeDualToneEnabled", dualToneEnabled ? 1 : 0);
        renderer.colorEmotionShader->setVec3("chargeDualTonePrimaryColor", dualTonePrimary);
        renderer.colorEmotionShader->setVec3("chargeDualToneSecondaryColor", dualToneSecondary);
        renderer.colorEmotionShader->setFloat("chargeDualToneSpinSpeed", fullChargeSpinSpeed);

        glBindVertexArray(renderer.colorEmotionVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
    }
}
