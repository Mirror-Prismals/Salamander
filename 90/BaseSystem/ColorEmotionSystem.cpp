#pragma once
#include "Host/PlatformInput.h"

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

        std::vector<int> resolveLavaPrototypeIDs(const std::vector<Entity>& prototypes) {
            std::vector<int> ids;
            ids.reserve(12);
            for (const auto& proto : prototypes) {
                if (!proto.isBlock) continue;
                const std::string& name = proto.name;
                if (name == "Lava"
                    || name == "LavaBlockTex"
                    || name.rfind("DepthLavaTile", 0) == 0) {
                    ids.push_back(proto.prototypeID);
                }
            }
            return ids;
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

        bool cameraInAnyPrototype(const VoxelWorldContext& voxelWorld,
                                  const std::vector<int>& prototypeIDs,
                                  const glm::vec3& cameraPosition) {
            if (!voxelWorld.enabled || prototypeIDs.empty()) return false;
            auto sampleAtOffset = [&](float yOffset) {
                glm::vec3 p = cameraPosition + glm::vec3(0.0f, yOffset, 0.0f);
                glm::ivec3 cell(
                    static_cast<int>(std::floor(p.x)),
                    static_cast<int>(std::floor(p.y)),
                    static_cast<int>(std::floor(p.z))
                );
                const uint32_t blockID = voxelWorld.getBlockWorld(cell);
                for (int id : prototypeIDs) {
                    if (id < 0) continue;
                    if (blockID == static_cast<uint32_t>(id)) return true;
                }
                return false;
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

        bool isPickupHandMode(BuildModeType mode) {
            return mode == BuildModeType::Pickup || mode == BuildModeType::PickupLeft;
        }

        void resolveRightClickChargeVisual(const PlayerContext& player,
                                           BuildModeType& outMode,
                                           BlockChargeAction& outAction) {
            outMode = player.buildMode;
            outAction = BlockChargeAction::None;

            if (isPickupHandMode(player.buildMode)) {
                const bool swappedControls = player.blockChargeControlsSwapped;
                outAction = swappedControls ? BlockChargeAction::Destroy : BlockChargeAction::Pickup;
                if (outAction == BlockChargeAction::Destroy) {
                    outMode = (player.buildMode == BuildModeType::PickupLeft)
                        ? BuildModeType::PickupLeft
                        : BuildModeType::Destroy;
                } else {
                    outMode = player.buildMode;
                }
                return;
            }

            if (player.buildMode == BuildModeType::Bouldering) {
                outMode = BuildModeType::Bouldering;
                outAction = player.blockChargeControlsSwapped
                    ? BlockChargeAction::BoulderSecondary
                    : BlockChargeAction::BoulderPrimary;
                return;
            }

            if (player.buildMode == BuildModeType::Fishing) {
                outMode = BuildModeType::Fishing;
                outAction = BlockChargeAction::None;
                return;
            }
        }

        glm::vec3 colorForChargeMode(BuildModeType mode, BlockChargeAction action, float t, bool fullyCharged) {
            t = glm::clamp(t, 0.0f, 1.0f);
            const glm::vec3 lime(0.10f, 0.85f, 0.20f);
            const glm::vec3 orange(1.00f, 0.55f, 0.10f);
            const glm::vec3 blue(0.16f, 0.48f, 1.00f);
            const glm::vec3 red(1.00f, 0.12f, 0.10f);
            const glm::vec3 purple(0.66f, 0.26f, 0.98f);
            glm::vec3 a = lime;
            glm::vec3 b = orange;
            if (action == BlockChargeAction::Throw) {
                // Smash-throw charge starts red and full-charge dual-tone adds red/blue swirl.
                a = red;
                b = a;
            } else if (mode == BuildModeType::Destroy) {
                a = glm::vec3(0.00f, 1.00f, 1.00f);
                b = glm::vec3(1.00f, 0.00f, 1.00f);
            } else if (mode == BuildModeType::PickupLeft) {
                if (action == BlockChargeAction::Destroy) {
                    a = red;
                    b = purple;
                } else {
                    a = orange;
                    b = blue;
                }
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
            if (fullyCharged && action != BlockChargeAction::Throw) {
                if (mode == BuildModeType::PickupLeft) {
                    return (action == BlockChargeAction::Destroy) ? red : orange;
                }
                if (mode == BuildModeType::Pickup || mode == BuildModeType::Destroy) {
                    return lime;
                }
            }
            return glm::mix(a, b, t);
        }

        bool isChargeBuildMode(BuildModeType mode) {
            return mode == BuildModeType::Pickup
                || mode == BuildModeType::PickupLeft
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

    void UpdateColorEmotions(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.player || !baseSystem.colorEmotion) return;

        PlayerContext& player = *baseSystem.player;
        ColorEmotionContext& emotion = *baseSystem.colorEmotion;

        bool enabled = getRegistryBool(baseSystem, "ColorEmotionEnabled", true);
        emotion.enabled = enabled;
        if (!enabled) {
            emotion.intensity = std::max(0.0f, emotion.intensity - dt * 8.0f);
            emotion.leafCanopyMix = std::max(0.0f, emotion.leafCanopyMix - dt * 8.0f);
            emotion.chargeFireInvertTimer = 0.0f;
            emotion.chargeFireInvertTail = 0.0f;
            emotion.proneToggleFlashTimer = 0.0f;
            emotion.modeCycleFlashTimer = 0.0f;
            emotion.modeCycleFlashPreviewInitialized = false;
            emotion.active = emotion.intensity > 0.001f;
            if (baseSystem.audio) {
                baseSystem.audio->headUnderwaterMix.store(0.0f, std::memory_order_relaxed);
            }
            return;
        }

        const float chargeMaxIntensity = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionChargeMaxIntensity", 0.86f), 0.0f, 1.0f);
        const float underwaterIntensityMax = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionUnderwaterIntensity", 0.58f), 0.0f, 1.0f);
        const float lavaIntensityMax = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionLavaIntensity", underwaterIntensityMax), 0.0f, 1.0f);
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
        const float chargeFireTailSeconds = std::max(0.05f, getRegistryFloat(baseSystem, "ColorEmotionChargeFireTailSeconds", 1.0f));
        const float chargeFireIntensity = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionChargeFireIntensity", 0.34f), 0.0f, 1.0f);
        const float proneToggleFlashTailSeconds = std::max(0.05f, getRegistryFloat(baseSystem, "ColorEmotionProneToggleFlashTailSeconds", 1.0f));
        const float proneToggleFlashIntensity = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionProneToggleFlashIntensity", 0.78f), 0.0f, 1.0f);
        const float modeCycleFlashTailSeconds = std::max(0.05f, getRegistryFloat(baseSystem, "ColorEmotionModeCycleFlashSeconds", 1.0f));
        const float headUnderwaterLowpassHz = glm::clamp(getRegistryFloat(baseSystem, "HeadSpeakerUnderwaterLowpassHz", 500.0f), 20.0f, 20000.0f);
        const float headUnderwaterLowpassStrength = glm::clamp(getRegistryFloat(baseSystem, "HeadSpeakerUnderwaterLowpassStrength", 1.0f), 0.0f, 1.0f);
        if (emotion.modeCycleFlashDuration < 0.05f) {
            emotion.modeCycleFlashDuration = modeCycleFlashTailSeconds;
        }
        if (emotion.modeCycleFlashTimer > 0.0f) {
            emotion.modeCycleFlashTimer = std::max(0.0f, emotion.modeCycleFlashTimer - dt);
        } else {
            emotion.modeCycleFlashTimer = 0.0f;
        }

        if (emotion.chargeFireInvertDuration < 0.05f) {
            emotion.chargeFireInvertDuration = chargeFireTailSeconds;
        }
        if (emotion.chargeFireInvertTimer > 0.0f) {
            emotion.chargeFireInvertTimer = std::max(0.0f, emotion.chargeFireInvertTimer - dt);
        } else {
            emotion.chargeFireInvertTimer = 0.0f;
        }
        emotion.chargeFireInvertTail = glm::clamp(
            emotion.chargeFireInvertTimer / std::max(0.001f, emotion.chargeFireInvertDuration),
            0.0f,
            1.0f
        );
        emotion.proneToggleFlashDuration = proneToggleFlashTailSeconds;
        if (emotion.proneToggleFlashTimer > 0.0f) {
            emotion.proneToggleFlashTimer = std::max(0.0f, emotion.proneToggleFlashTimer - dt);
        } else {
            emotion.proneToggleFlashTimer = 0.0f;
        }
        const float proneToggleFlashTail = glm::clamp(
            emotion.proneToggleFlashTimer / std::max(0.001f, emotion.proneToggleFlashDuration),
            0.0f,
            1.0f
        );
        const float proneToggleFlashWeight = proneToggleFlashTail * proneToggleFlashIntensity;

        bool underwater = false;
        bool inLava = false;
        bool inLeaves = false;
        float waterSurfaceY = player.cameraPosition.y + 0.85f;
        if (baseSystem.voxelWorld) {
            int waterPrototypeID = resolveWaterPrototypeID(prototypes);
            underwater = cameraInWater(*baseSystem.voxelWorld, waterPrototypeID, player.cameraPosition);
            const std::vector<int> lavaPrototypeIDs = resolveLavaPrototypeIDs(prototypes);
            inLava = cameraInAnyPrototype(*baseSystem.voxelWorld, lavaPrototypeIDs, player.cameraPosition);
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
        BuildModeType rightClickPreviewMode = player.buildMode;
        BlockChargeAction rightClickPreviewAction = BlockChargeAction::None;
        resolveRightClickChargeVisual(player, rightClickPreviewMode, rightClickPreviewAction);
        const int previewModeInt = static_cast<int>(rightClickPreviewMode);
        const int previewActionInt = static_cast<int>(rightClickPreviewAction);
        if (!emotion.modeCycleFlashPreviewInitialized) {
            emotion.modeCycleFlashPreviewInitialized = true;
            emotion.modeCycleFlashLastPreviewMode = previewModeInt;
            emotion.modeCycleFlashLastPreviewAction = previewActionInt;
        } else if (emotion.modeCycleFlashLastPreviewMode != previewModeInt
                || emotion.modeCycleFlashLastPreviewAction != previewActionInt) {
            emotion.modeCycleFlashDuration = modeCycleFlashTailSeconds;
            emotion.modeCycleFlashTimer = modeCycleFlashTailSeconds;
            emotion.modeCycleFlashLastPreviewMode = previewModeInt;
            emotion.modeCycleFlashLastPreviewAction = previewActionInt;
        }
        if (isPickupHandMode(player.buildMode)) {
            if (player.blockChargeAction == BlockChargeAction::Destroy) {
                chargeVisualMode = (player.buildMode == BuildModeType::PickupLeft)
                    ? BuildModeType::PickupLeft
                    : BuildModeType::Destroy;
            } else if (player.blockChargeAction == BlockChargeAction::Pickup) {
                chargeVisualMode = player.buildMode;
            }
        }

        const float modeCycleFlashTail = glm::clamp(
            emotion.modeCycleFlashTimer / std::max(0.001f, emotion.modeCycleFlashDuration),
            0.0f,
            1.0f
        );
        if (modeCycleFlashTail > 0.001f && !player.isChargingBlock) {
            chargeVisualMode = rightClickPreviewMode;
            chargeVisualAction = rightClickPreviewAction;
            chargeMode = isChargeBuildMode(chargeVisualMode);
            chargeValue = 1.0f;
            chargeActive = chargeMode;
        }

        const bool boulderingVisualContext =
            player.buildMode == BuildModeType::Bouldering
            || player.boulderPrimaryLatched
            || player.boulderSecondaryLatched;
        float chargeIntensity = chargeActive ? (0.08f + chargeValue * chargeMaxIntensity) : 0.0f;
        if (modeCycleFlashTail > 0.001f && !player.isChargingBlock) {
            chargeIntensity *= modeCycleFlashTail;
        }
        float underwaterIntensity = underwater ? underwaterIntensityMax : 0.0f;
        float lavaIntensity = inLava ? lavaIntensityMax : 0.0f;
        float leafCanopyIntensity = (inLeaves && !boulderingVisualContext) ? leafCanopyIntensityMax : 0.0f;
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
        glm::vec3 waterColor(0.0f, 128.0f / 255.0f, 1.0f); // #0080FF
        glm::vec3 lavaColor(244.0f / 255.0f, 85.0f / 255.0f, 15.0f / 255.0f); // #F4550F
        glm::vec3 leafCanopyColor = resolveLeafEmotionColor(baseSystem);
        glm::vec3 fishingBaseColor(0.00f, 0.88f, 1.00f);
        float totalWeight = chargeIntensity + underwaterIntensity + lavaIntensity + leafCanopyIntensity + fishingHintIntensity + proneToggleFlashWeight;
        glm::vec3 targetColor = totalWeight > 1e-5f
            ? (chargeColor * chargeIntensity
               + waterColor * underwaterIntensity
               + lavaColor * lavaIntensity
               + leafCanopyColor * leafCanopyIntensity
               + fishingBaseColor * fishingHintIntensity
               + emotion.proneToggleFlashColor * proneToggleFlashWeight) / totalWeight
            : glm::vec3(0.0f);
        float targetIntensity = glm::clamp(
            chargeIntensity
            + underwaterIntensity * (1.0f - 0.35f * chargeIntensity)
            + lavaIntensity * (1.0f - 0.15f * chargeIntensity)
            + leafCanopyIntensity * (1.0f - 0.20f * underwaterIntensity)
            + fishingHintIntensity * (1.0f - 0.15f * underwaterIntensity)
            + proneToggleFlashWeight
            + emotion.chargeFireInvertTail * chargeFireIntensity,
            0.0f,
            1.0f
        );

        float blendAlpha = glm::clamp(dt * smoothing, 0.0f, 1.0f);
        emotion.color = glm::mix(emotion.color, targetColor, blendAlpha);
        emotion.intensity = glm::mix(emotion.intensity, targetIntensity, blendAlpha);
        emotion.chargeValue = chargeValue;
        emotion.underwater = underwater;
        emotion.underwaterMix = glm::mix(emotion.underwaterMix, underwater ? 1.0f : 0.0f, blendAlpha);
        emotion.leafCanopyMix = glm::mix(emotion.leafCanopyMix, leafCanopyIntensity > 0.001f ? 1.0f : 0.0f, blendAlpha);
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
        emotion.timeSeconds = static_cast<float>(PlatformInput::GetTimeSeconds());
        emotion.active = emotion.intensity > 0.001f
            || emotion.fishingDirectionStrength > 0.001f
            || proneToggleFlashTail > 0.001f
            || modeCycleFlashTail > 0.001f
            || emotion.chargeFireInvertTail > 0.001f;
        if (baseSystem.audio) {
            baseSystem.audio->headUnderwaterMix.store(emotion.underwaterMix, std::memory_order_relaxed);
            baseSystem.audio->headUnderwaterLowpassHz.store(headUnderwaterLowpassHz, std::memory_order_relaxed);
            baseSystem.audio->headUnderwaterLowpassStrength.store(headUnderwaterLowpassStrength, std::memory_order_relaxed);
        }
    }

    void RenderColorEmotions(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt;
        if (!baseSystem.renderer || !baseSystem.colorEmotion || !baseSystem.player || !baseSystem.renderBackend) return;

        RendererContext& renderer = *baseSystem.renderer;
        ColorEmotionContext& emotion = *baseSystem.colorEmotion;
        PlayerContext& player = *baseSystem.player;
        auto& renderBackend = *baseSystem.renderBackend;
        if (!emotion.enabled || !emotion.active) return;
        if (emotion.intensity <= 0.001f && emotion.chargeFireInvertTail <= 0.001f) return;
        auto setDepthTestEnabled = [&](bool enabled) {
            renderBackend.setDepthTestEnabled(enabled);
        };
        auto setBlendEnabled = [&](bool enabled) {
            renderBackend.setBlendEnabled(enabled);
        };
        auto setBlendModeAlpha = [&]() {
            renderBackend.setBlendModeAlpha();
        };
        auto setLineWidth = [&](float width) {
            renderBackend.setLineWidth(width);
        };

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
                    setDepthTestEnabled(true);
                    setBlendEnabled(true);
                    setBlendModeAlpha();
                    renderer.audioRayShader->use();
                    renderer.audioRayShader->setMat4("view", player.viewMatrix);
                    renderer.audioRayShader->setMat4("projection", player.projectionMatrix);
                    renderBackend.bindVertexArray(renderer.audioRayVAO);
                    renderBackend.uploadArrayBufferData(
                        renderer.audioRayVBO,
                        cueVerts.data(),
                        cueVerts.size() * sizeof(CueVertex),
                        true
                    );
                    setLineWidth(cueLineWidth);
                    renderBackend.drawArraysLines(0, static_cast<int>(cueVerts.size()));
                    setLineWidth(1.0f);
                }
            }
        }

        if (!renderer.colorEmotionShader || !renderer.colorEmotionVAO) return;

        int fbWidth = 1;
        int fbHeight = 1;
        PlatformInput::GetFramebufferSize(win, fbWidth, fbHeight);
        float aspectRatio = (fbHeight > 0) ? (static_cast<float>(fbWidth) / static_cast<float>(fbHeight)) : 1.0f;

        setDepthTestEnabled(false);
        setBlendEnabled(true);
        setBlendModeAlpha();

        const float baseOpacityScale = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionOpacityScale", 0.10f), 0.0f, 1.0f);
        const float leafOpacityScale = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionLeafCanopyOpacityScale", 0.50f), 0.0f, 1.0f);
        const float underwaterOpacityScale = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionUnderwaterOpacityScale", baseOpacityScale), 0.0f, 1.0f);
        const float chargeOpacityScale = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionChargeOpacityScale", baseOpacityScale), 0.0f, 1.0f);
        const float fishingOpacityScale = glm::clamp(getRegistryFloat(baseSystem, "ColorEmotionFishingHintOpacityScale", baseOpacityScale), 0.0f, 1.0f);
        const BuildModeType emotionMode = static_cast<BuildModeType>(emotion.mode);
        const float chargeMix = isChargeBuildMode(emotionMode) ? glm::clamp(emotion.chargeValue, 0.0f, 1.0f) : 0.0f;
        const float underwaterMix = glm::clamp(emotion.underwaterMix, 0.0f, 1.0f);
        const float leafCanopyMix = glm::clamp(emotion.leafCanopyMix, 0.0f, 1.0f);
        const float fishingMix = glm::clamp(emotion.fishingDirectionStrength, 0.0f, 1.0f);
        float opacityScale = baseOpacityScale;
        // Blend per-effect opacity scales in priority order instead of taking max,
        // so one context (like leaf canopy) doesn't force all other contexts to its opacity.
        opacityScale = glm::mix(opacityScale, leafOpacityScale, leafCanopyMix);
        opacityScale = glm::mix(opacityScale, underwaterOpacityScale, underwaterMix);
        opacityScale = glm::mix(opacityScale, fishingOpacityScale, fishingMix);
        opacityScale = glm::mix(opacityScale, chargeOpacityScale, chargeMix);
        opacityScale = glm::clamp(opacityScale, 0.0f, 1.0f);
        const float fullChargeSpinSpeed = getRegistryFloat(baseSystem, "ColorEmotionChargeFullSpinSpeed", 3.0f);
        const glm::vec3 lime(0.10f, 0.85f, 0.20f);
        const glm::vec3 orange(1.00f, 0.55f, 0.10f);
        const glm::vec3 magenta(1.00f, 0.00f, 1.00f);
        const glm::vec3 violet(0.66f, 0.26f, 0.98f);
        const glm::vec3 blue(0.16f, 0.48f, 1.00f);
        const glm::vec3 blueGreen(0.00f, 0.84f, 0.78f);
        const glm::vec3 red(1.00f, 0.12f, 0.10f);
        const bool fullCharge = emotion.chargeValue >= 0.999f;
        const bool modePickupRight = emotion.mode == static_cast<int>(BuildModeType::Pickup);
        const bool modePickupLeft = emotion.mode == static_cast<int>(BuildModeType::PickupLeft);
        const bool modePickup = modePickupRight || modePickupLeft;
        const bool modeDestroy = emotion.mode == static_cast<int>(BuildModeType::Destroy);
        const bool modeBouldering = emotion.mode == static_cast<int>(BuildModeType::Bouldering);
        const bool leftPickupDestroyAction = modePickupLeft
            && emotion.chargeAction == static_cast<int>(BlockChargeAction::Destroy);
        const bool pickupThrowAction = modePickup
            && emotion.chargeAction == static_cast<int>(BlockChargeAction::Throw);
        const bool dualTonePickup = fullCharge && modePickup && !pickupThrowAction && !leftPickupDestroyAction;
        const bool dualToneDestroy = fullCharge
            && (emotion.mode == static_cast<int>(BuildModeType::Destroy) || leftPickupDestroyAction);
        const bool dualToneThrow = fullCharge && pickupThrowAction;
        const bool dualToneBoulderPrimary = fullCharge
            && modeBouldering
            && emotion.chargeAction == static_cast<int>(BlockChargeAction::BoulderPrimary);
        const bool dualToneBoulderSecondary = fullCharge
            && modeBouldering
            && emotion.chargeAction == static_cast<int>(BlockChargeAction::BoulderSecondary);
        const bool dualToneEnabled =
            dualTonePickup || dualToneDestroy || dualToneThrow || dualToneBoulderPrimary || dualToneBoulderSecondary;
        glm::vec3 dualTonePrimary = lime;
        glm::vec3 dualToneSecondary = orange;
        if (dualToneThrow || pickupThrowAction) {
            dualTonePrimary = red;
            dualToneSecondary = blue;
        } else if (dualToneDestroy || modeDestroy || leftPickupDestroyAction) {
            if (leftPickupDestroyAction) {
                dualTonePrimary = red;
                dualToneSecondary = violet;
            } else {
                dualTonePrimary = lime;
                dualToneSecondary = magenta;
            }
        } else if (dualToneBoulderPrimary) {
            dualTonePrimary = orange;
            dualToneSecondary = violet;
        } else if (dualToneBoulderSecondary) {
            dualTonePrimary = blue;
            dualToneSecondary = blueGreen;
        } else if (modePickupLeft) {
            dualTonePrimary = orange;
            dualToneSecondary = blue;
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
        renderer.colorEmotionShader->setFloat("fireInvertMix", glm::clamp(emotion.chargeFireInvertTail, 0.0f, 1.0f));
        renderer.colorEmotionShader->setInt("chargeDualToneEnabled", dualToneEnabled ? 1 : 0);
        renderer.colorEmotionShader->setVec3("chargeDualTonePrimaryColor", dualTonePrimary);
        renderer.colorEmotionShader->setVec3("chargeDualToneSecondaryColor", dualToneSecondary);
        renderer.colorEmotionShader->setFloat("chargeDualToneSpinSpeed", fullChargeSpinSpeed);

        renderBackend.bindVertexArray(renderer.colorEmotionVAO);
        renderBackend.drawArraysTriangles(0, 6);
        renderBackend.unbindVertexArray();
        setDepthTestEnabled(true);
    }
}
