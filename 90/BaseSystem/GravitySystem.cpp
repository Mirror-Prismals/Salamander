#pragma once
#include "Host/PlatformInput.h"
#include "../Host.h"
#include <cmath>
#include <limits>
#include <unordered_map>

namespace AudioSystemLogic { bool TriggerGameplaySfx(BaseSystem& baseSystem, const std::string& cueName, float gain); }

namespace GravitySystemLogic {

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

        bool triggerGameplaySfx(BaseSystem& baseSystem, const char* fileName, float cooldownSeconds = 0.0f) {
            if (!fileName) return false;
            static std::unordered_map<std::string, double> s_lastTrigger;
            const double now = PlatformInput::GetTimeSeconds();
            const std::string keyName(fileName);
            auto it = s_lastTrigger.find(keyName);
            if (it != s_lastTrigger.end() && (now - it->second) < static_cast<double>(cooldownSeconds)) {
                return false;
            }

            if (AudioSystemLogic::TriggerGameplaySfx(baseSystem, keyName, 1.0f)) {
                s_lastTrigger[keyName] = now;
                return true;
            }

            if (!getRegistryBool(baseSystem, "GameplaySfxFallbackToChuck", false)) return false;
            if (!baseSystem.audio || !baseSystem.audio->chuck) return false;
            const std::string scriptPath = std::string("Procedures/chuck/gameplay/") + fileName;
            std::vector<t_CKUINT> ids;
            bool ok = baseSystem.audio->chuck->compileFile(scriptPath, "", 1, FALSE, &ids);
            if (ok && !ids.empty()) {
                s_lastTrigger[keyName] = now;
                return true;
            }
            return false;
        }

        int resolveWaterPrototypeID(const std::vector<Entity>& prototypes) {
            for (const auto& proto : prototypes) {
                if (proto.name == "Water") return proto.prototypeID;
            }
            return -1;
        }

        bool isWaterLikeName(const std::string& name) {
            return name == "Water" || name.rfind("WaterSlope", 0) == 0;
        }

        bool isWaterLikePrototypeID(const std::vector<Entity>& prototypes, uint32_t id) {
            if (id == 0 || id >= prototypes.size()) return false;
            return isWaterLikeName(prototypes[id].name);
        }

        int resolveLeafPrototypeID(const std::vector<Entity>& prototypes) {
            for (const auto& proto : prototypes) {
                if (proto.name == "Leaf") return proto.prototypeID;
            }
            return -1;
        }

        bool isInWaterCell(const VoxelWorldContext& voxelWorld,
                           const std::vector<Entity>& prototypes,
                           int waterPrototypeID,
                           const glm::vec3& point) {
            if (!voxelWorld.enabled || waterPrototypeID < 0) return false;
            glm::ivec3 cell(
                static_cast<int>(std::floor(point.x)),
                static_cast<int>(std::floor(point.y)),
                static_cast<int>(std::floor(point.z))
            );
            return isWaterLikePrototypeID(prototypes, voxelWorld.getBlockWorld(cell));
        }

        bool isInLeafCell(const VoxelWorldContext& voxelWorld, int leafPrototypeID, const glm::vec3& point) {
            if (!voxelWorld.enabled || leafPrototypeID < 0) return false;
            glm::ivec3 cell(
                static_cast<int>(std::floor(point.x)),
                static_cast<int>(std::floor(point.y)),
                static_cast<int>(std::floor(point.z))
            );
            return voxelWorld.getBlockWorld(cell) == static_cast<uint32_t>(leafPrototypeID);
        }

        bool isLilypadPrototypeName(const std::string& name) {
            return name == "StonePebbleLilypadTexX"
                || name == "StonePebbleLilypadTexZ"
                || name.rfind("GrassCoverBigLilypad", 0) == 0;
        }

        bool isLilypadPrototypeID(const std::vector<Entity>& prototypes, uint32_t blockID) {
            if (blockID == 0 || blockID >= prototypes.size()) return false;
            return isLilypadPrototypeName(prototypes[blockID].name);
        }

        bool isPlayerStandingOnLilypad(const BaseSystem& baseSystem,
                                       const std::vector<Entity>& prototypes,
                                       const glm::vec3& playerPos) {
            if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return false;
            const VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            const float feetY = playerPos.y - 0.9f;
            const int sampleY = static_cast<int>(std::floor(feetY + 0.5f));
            static const glm::vec2 kXZOffsets[] = {
                glm::vec2(0.0f, 0.0f),
                glm::vec2(0.24f, 0.0f),
                glm::vec2(-0.24f, 0.0f),
                glm::vec2(0.0f, 0.24f),
                glm::vec2(0.0f, -0.24f)
            };
            static const int kYOffsets[] = {0, -1, 1, -2};
            for (int yOffset : kYOffsets) {
                const int y = sampleY + yOffset;
                const float lilypadTopY = static_cast<float>(y) - 0.5f + (1.0f / 12.0f);
                if (feetY < (lilypadTopY - 0.45f) || feetY > (lilypadTopY + 1.00f)) continue;
                for (const glm::vec2& offset : kXZOffsets) {
                    const glm::ivec3 cell(
                        static_cast<int>(std::floor(playerPos.x + offset.x)),
                        y,
                        static_cast<int>(std::floor(playerPos.z + offset.y))
                    );
                    if (isLilypadPrototypeID(prototypes, voxelWorld.getBlockWorld(cell))) {
                        return true;
                    }
                }
            }
            return false;
        }

        bool isPlayerInWater(const VoxelWorldContext& voxelWorld,
                             const std::vector<Entity>& prototypes,
                             int waterPrototypeID,
                             const glm::vec3& playerPos) {
            static const glm::vec3 kOffsets[] = {
                glm::vec3(0.0f,  0.0f,  0.0f),
                glm::vec3(0.0f,  0.9f,  0.0f),
                glm::vec3(0.0f, -0.9f,  0.0f),
                glm::vec3(0.28f, 0.0f,  0.0f),
                glm::vec3(-0.28f,0.0f,  0.0f),
                glm::vec3(0.0f,  0.0f,  0.28f),
                glm::vec3(0.0f,  0.0f, -0.28f)
            };
            for (const glm::vec3& offset : kOffsets) {
                if (isInWaterCell(voxelWorld, prototypes, waterPrototypeID, playerPos + offset)) return true;
            }
            return false;
        }

        bool isPlayerTouchingLeaves(const VoxelWorldContext& voxelWorld, int leafPrototypeID, const glm::vec3& playerPos) {
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
                if (isInLeafCell(voxelWorld, leafPrototypeID, playerPos + offset)) return true;
            }
            return false;
        }

        bool isWallStonePrototypeName(const std::string& name) {
            return name == "WallStoneTexPosX"
                || name == "WallStoneTexNegX"
                || name == "WallStoneTexPosZ"
                || name == "WallStoneTexNegZ";
        }

        bool isWallStonePrototypeID(const std::vector<Entity>& prototypes, int prototypeID) {
            if (prototypeID < 0 || prototypeID >= static_cast<int>(prototypes.size())) return false;
            return isWallStonePrototypeName(prototypes[static_cast<size_t>(prototypeID)].name);
        }

        bool isLeafPrototypeID(const std::vector<Entity>& prototypes, int prototypeID, int leafPrototypeID) {
            if (prototypeID < 0 || prototypeID >= static_cast<int>(prototypes.size())) return false;
            if (leafPrototypeID >= 0 && prototypeID == leafPrototypeID) return true;
            return prototypes[static_cast<size_t>(prototypeID)].name == "Leaf";
        }

        bool isBoulderingAnchorPrototypeID(const std::vector<Entity>& prototypes,
                                           int prototypeID,
                                           bool allowLeafAnchors,
                                           int leafPrototypeID) {
            if (isWallStonePrototypeID(prototypes, prototypeID)) return true;
            if (allowLeafAnchors && isLeafPrototypeID(prototypes, prototypeID, leafPrototypeID)) return true;
            return false;
        }

        bool hasBoulderingAnchor(const BaseSystem& baseSystem,
                                 const std::vector<Entity>& prototypes,
                                 const glm::ivec3& cell,
                                 int worldIndexHint,
                                 bool allowLeafAnchors,
                                 int leafPrototypeID) {
            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
                if (id > 0
                    && id < prototypes.size()
                    && isBoulderingAnchorPrototypeID(
                        prototypes,
                        static_cast<int>(id),
                        allowLeafAnchors,
                        leafPrototypeID)) {
                    return true;
                }
            }
            if (!baseSystem.level) return false;
            if (worldIndexHint < 0 || worldIndexHint >= static_cast<int>(baseSystem.level->worlds.size())) return false;
            const Entity& world = baseSystem.level->worlds[static_cast<size_t>(worldIndexHint)];
            const glm::vec3 targetPos = glm::vec3(cell);
            for (const auto& inst : world.instances) {
                if (glm::distance(inst.position, targetPos) > 0.05f) continue;
                if (isBoulderingAnchorPrototypeID(prototypes, inst.prototypeID, allowLeafAnchors, leafPrototypeID)) return true;
            }
            return false;
        }

        bool findWaterSurfaceInColumn(const VoxelWorldContext& voxelWorld,
                                      const std::vector<Entity>& prototypes,
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
                if (!isWaterLikePrototypeID(prototypes, voxelWorld.getBlockWorld(cell))) continue;
                glm::ivec3 above(x, y + 1, z);
                if (isWaterLikePrototypeID(prototypes, voxelWorld.getBlockWorld(above))) continue;
                if (voxelWorld.getBlockWorld(above) != 0) continue;
                outSurfaceY = static_cast<float>(y) + 1.02f;
                return true;
            }
            return false;
        }

        bool findLocalWaterSurface(const VoxelWorldContext& voxelWorld,
                                   const std::vector<Entity>& prototypes,
                                   int waterPrototypeID,
                                   const glm::vec3& playerPos,
                                   float& outSurfaceY) {
            int cx = static_cast<int>(std::floor(playerPos.x));
            int cz = static_cast<int>(std::floor(playerPos.z));
            int minY = static_cast<int>(std::floor(playerPos.y)) - 24;
            int maxY = static_cast<int>(std::floor(playerPos.y)) + 24;
            float bestScore = std::numeric_limits<float>::max();
            bool found = false;

            for (int ring = 0; ring <= 2; ++ring) {
                for (int dz = -ring; dz <= ring; ++dz) {
                    for (int dx = -ring; dx <= ring; ++dx) {
                        if (ring > 0 && std::abs(dx) != ring && std::abs(dz) != ring) continue;
                        float surfaceY = 0.0f;
                        if (!findWaterSurfaceInColumn(voxelWorld, prototypes, waterPrototypeID, cx + dx, cz + dz, minY, maxY, surfaceY)) continue;
                        float vertical = std::abs(surfaceY - playerPos.y);
                        float horiz = std::sqrt(static_cast<float>(dx * dx + dz * dz));
                        float score = horiz * 0.8f + vertical;
                        if (!found || score < bestScore) {
                            bestScore = score;
                            outSurfaceY = surfaceY;
                            found = true;
                        }
                    }
                }
            }
            return found;
        }
    }

    void ApplyGravity(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.player) return;
        if (baseSystem.gamemode != "survival") return;
        bool spawnReady = false;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("spawn_ready");
            if (it != baseSystem.registry->end() &&
                std::holds_alternative<bool>(it->second)) {
                spawnReady = std::get<bool>(it->second);
            }
        }
        if (!spawnReady) return;
        PlayerContext& player = *baseSystem.player;
        float stepDt = dt;
        if (stepDt < 0.0f) stepDt = 0.0f;
        if (stepDt > 0.05f) stepDt = 0.05f;
        const bool movementInputEnabled = win && !(baseSystem.ui && baseSystem.ui->active);
        const bool spaceDown = movementInputEnabled && PlatformInput::IsKeyDown(win, PlatformInput::Key::Space);
        const bool descendDown = movementInputEnabled && (
            PlatformInput::IsKeyDown(win, PlatformInput::Key::LeftControl)
            || PlatformInput::IsKeyDown(win, PlatformInput::Key::RightControl)
        );
        const bool keyW = movementInputEnabled && PlatformInput::IsKeyDown(win, PlatformInput::Key::W);
        const bool keyS = movementInputEnabled && PlatformInput::IsKeyDown(win, PlatformInput::Key::S);
        const bool keyA = movementInputEnabled && PlatformInput::IsKeyDown(win, PlatformInput::Key::A);
        const bool keyD = movementInputEnabled && PlatformInput::IsKeyDown(win, PlatformInput::Key::D);
        const bool keyR = movementInputEnabled && PlatformInput::IsKeyDown(win, PlatformInput::Key::R);
        static bool s_swimPaddleWasDown = false;
        if (!movementInputEnabled) {
            s_swimPaddleWasDown = false;
        }
        const bool swimPaddlePressed = keyR && !s_swimPaddleWasDown;
        s_swimPaddleWasDown = keyR;

        const bool leafClimbEnabled = getRegistryBool(baseSystem, "LeafClimbEnabled", true);
        const bool leafBoulderingAnchorsEnabled = getRegistryBool(
            baseSystem,
            "LeafClimbBoulderingAnchorsEnabled",
            leafClimbEnabled);
        const int leafPrototypeID = leafBoulderingAnchorsEnabled ? resolveLeafPrototypeID(prototypes) : -1;

        const bool swimmingEnabled = getRegistryBool(baseSystem, "SwimmingEnabled", true);
        bool playerInWater = false;
        int waterPrototypeID = -1;
        float waterSurfaceY = player.cameraPosition.y + 1024.0f;
        bool hasLocalWaterSurface = false;
        if (swimmingEnabled && baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
            waterPrototypeID = resolveWaterPrototypeID(prototypes);
            if (waterPrototypeID >= 0) {
                playerInWater = isPlayerInWater(*baseSystem.voxelWorld, prototypes, waterPrototypeID, player.cameraPosition);
                if (playerInWater) {
                    hasLocalWaterSurface = findLocalWaterSurface(
                        *baseSystem.voxelWorld,
                        prototypes,
                        waterPrototypeID,
                        player.cameraPosition,
                        waterSurfaceY
                    );
                }
            }
        }
        if (playerInWater && isPlayerStandingOnLilypad(baseSystem, prototypes, player.cameraPosition)) {
            playerInWater = false;
        }
        if (player.boulderPrimaryLatched
            && !hasBoulderingAnchor(
                baseSystem,
                prototypes,
                player.boulderPrimaryCell,
                player.boulderPrimaryWorldIndex,
                leafBoulderingAnchorsEnabled,
                leafPrototypeID)) {
            player.boulderPrimaryLatched = false;
            player.boulderPrimaryRestLength = 0.0f;
            player.boulderPrimaryWorldIndex = -1;
        }
        if (player.boulderSecondaryLatched
            && !hasBoulderingAnchor(
                baseSystem,
                prototypes,
                player.boulderSecondaryCell,
                player.boulderSecondaryWorldIndex,
                leafBoulderingAnchorsEnabled,
                leafPrototypeID)) {
            player.boulderSecondaryLatched = false;
            player.boulderSecondaryRestLength = 0.0f;
            player.boulderSecondaryWorldIndex = -1;
        }
        const bool boulderingLatched = (player.boulderPrimaryLatched || player.boulderSecondaryLatched);

        static bool s_prevPlayerInWater = false;
        static float s_swimSlowdownDelayTimer = 0.0f;
        static float s_fallTime = 0.0f;
        static glm::vec3 s_swimStrokeVelocity(0.0f);
        static float s_swimStrokeCooldown = 0.0f;

        const float swimEntrySlowdownDelay = glm::clamp(getRegistryFloat(baseSystem, "SwimEntrySlowdownDelay", 0.22f), 0.0f, 2.5f);
        if (playerInWater && !s_prevPlayerInWater) {
            s_swimSlowdownDelayTimer = swimEntrySlowdownDelay;
        } else if (!playerInWater) {
            s_swimSlowdownDelayTimer = 0.0f;
        }

        if (playerInWater && !s_prevPlayerInWater) {
            const float splashMinFallSpeed = std::max(0.0f, getRegistryFloat(baseSystem, "WaterSplashMinFallSpeed", 2.1f));
            if (player.verticalVelocity <= -splashMinFallSpeed) {
                triggerGameplaySfx(baseSystem, "water_splash.ck", 0.05f);
            }
        }
        if (player.swimWallClimbAssistTimer > 0.0f) {
            player.swimWallClimbAssistTimer = std::max(0.0f, player.swimWallClimbAssistTimer - stepDt);
        }
        const bool swimWallClimbAssistActive = player.swimWallClimbAssistTimer > 0.0f;

        if (playerInWater) {
            s_fallTime = 0.0f;
            const float baseGravity = (getRegistryFloat(baseSystem, "GravityStrength", -21.0f) > 0.0f)
                ? -getRegistryFloat(baseSystem, "GravityStrength", -21.0f)
                : getRegistryFloat(baseSystem, "GravityStrength", -21.0f);
            const float swimDescendAcceleration = std::max(0.0f, getRegistryFloat(baseSystem, "SwimDescendAcceleration", 22.0f));
            const float swimStrokeSpeed = std::max(0.0f, getRegistryFloat(baseSystem, "SwimStrokeBurstSpeed", 9.8f));
            const float swimStrokeDamping = glm::clamp(getRegistryFloat(baseSystem, "SwimStrokeBurstDamping", 7.4f), 0.0f, 60.0f);
            const float swimStrokeCooldownSeconds = glm::clamp(getRegistryFloat(baseSystem, "SwimStrokeBurstCooldown", 0.20f), 0.0f, 2.0f);
            const float swimTreadHeadAboveSurface = glm::clamp(getRegistryFloat(baseSystem, "SwimTreadHeadAboveSurface", 0.005f), 0.001f, 0.5f);
            const float swimTreadLowerByBlocks = glm::clamp(getRegistryFloat(baseSystem, "SwimTreadLowerByBlocks", 1.0f), 0.0f, 3.0f);
            const float swimSurfaceBobAmplitude = glm::clamp(getRegistryFloat(baseSystem, "SwimSurfaceBobAmplitude", 0.05f), 0.0f, 0.35f);
            const float swimSurfaceBobFrequency = glm::clamp(getRegistryFloat(baseSystem, "SwimSurfaceBobFrequency", 1.3f), 0.0f, 8.0f);
            const float kTau = 6.28318530718f;
            const float swimTreadBob = std::sin(static_cast<float>(PlatformInput::GetTimeSeconds()) * swimSurfaceBobFrequency * kTau) * swimSurfaceBobAmplitude;
            const float swimTreadTargetDepth = (-swimTreadHeadAboveSurface + swimTreadLowerByBlocks) + std::max(0.0f, swimTreadBob);
            const float swimMaxTreadY = hasLocalWaterSurface
                ? (waterSurfaceY - swimTreadTargetDepth)
                : (player.cameraPosition.y + 1024.0f);

            auto computeSwimStrokeDirection = [&]() -> glm::vec3 {
                const float yaw = glm::radians(player.cameraYaw);
                const float pitch = glm::radians(player.cameraPitch);
                glm::vec3 viewDir(
                    std::cos(yaw) * std::cos(pitch),
                    std::sin(pitch),
                    std::sin(yaw) * std::cos(pitch)
                );
                if (glm::length(viewDir) < 0.0001f) {
                    viewDir = glm::vec3(0.0f, 0.0f, -1.0f);
                } else {
                    viewDir = glm::normalize(viewDir);
                }
                glm::vec3 flatForward(viewDir.x, 0.0f, viewDir.z);
                if (glm::length(flatForward) < 0.0001f) {
                    flatForward = glm::vec3(std::cos(yaw), 0.0f, std::sin(yaw));
                }
                if (glm::length(flatForward) < 0.0001f) {
                    flatForward = glm::vec3(0.0f, 0.0f, -1.0f);
                } else {
                    flatForward = glm::normalize(flatForward);
                }
                glm::vec3 flatRight = glm::cross(flatForward, glm::vec3(0.0f, 1.0f, 0.0f));
                if (glm::length(flatRight) < 0.0001f) {
                    flatRight = glm::vec3(1.0f, 0.0f, 0.0f);
                } else {
                    flatRight = glm::normalize(flatRight);
                }

                glm::vec3 dir(0.0f);
                if (keyW) dir += viewDir;
                if (keyS) dir -= viewDir;
                if (keyA) dir -= flatRight;
                if (keyD) dir += flatRight;
                if (glm::length(dir) < 0.0001f) dir = viewDir;
                return glm::normalize(dir);
            };

            if (s_swimStrokeCooldown > 0.0f) {
                s_swimStrokeCooldown = std::max(0.0f, s_swimStrokeCooldown - stepDt);
            }
            if (swimPaddlePressed && !descendDown && swimStrokeSpeed > 0.0f && s_swimStrokeCooldown <= 0.0f) {
                s_swimStrokeVelocity = computeSwimStrokeDirection() * swimStrokeSpeed;
                s_swimStrokeCooldown = swimStrokeCooldownSeconds;
            }
            if (swimStrokeDamping > 0.0f) {
                const float strokeDecay = std::exp(-swimStrokeDamping * stepDt);
                s_swimStrokeVelocity *= strokeDecay;
            }

            if (s_swimSlowdownDelayTimer > 0.0f) {
                const float entryGravityScale = glm::clamp(getRegistryFloat(baseSystem, "SwimEntryGravityScale", 1.0f), 0.0f, 2.5f);
                const float entryMaxFallSpeed = -std::abs(getRegistryFloat(baseSystem, "SwimEntryMaxFallSpeed", 24.0f));
                const float entryAscendAcceleration = std::max(0.0f, getRegistryFloat(baseSystem, "SwimEntryAscendAcceleration", 8.0f));
                const float swimAscendSpeed = std::max(0.4f, getRegistryFloat(baseSystem, "SwimAscendSpeed", 4.8f));

                if (spaceDown && !descendDown) {
                    player.verticalVelocity += entryAscendAcceleration * stepDt;
                }
                if (descendDown) {
                    player.verticalVelocity += baseGravity * entryGravityScale * stepDt;
                    player.verticalVelocity -= swimDescendAcceleration * stepDt;
                }
                if (player.verticalVelocity < entryMaxFallSpeed) player.verticalVelocity = entryMaxFallSpeed;
                if (player.verticalVelocity > swimAscendSpeed) player.verticalVelocity = swimAscendSpeed;
                player.cameraPosition.y += player.verticalVelocity * stepDt;
                player.cameraPosition += s_swimStrokeVelocity * stepDt;
                if (!swimWallClimbAssistActive && hasLocalWaterSurface && player.cameraPosition.y > swimMaxTreadY) {
                    player.cameraPosition.y = swimMaxTreadY;
                    if (player.verticalVelocity > 0.0f) player.verticalVelocity = 0.0f;
                }
                s_swimSlowdownDelayTimer = std::max(0.0f, s_swimSlowdownDelayTimer - stepDt);
                s_prevPlayerInWater = true;
                return;
            }

            float swimGravity = getRegistryFloat(baseSystem, "SwimGravity", -5.2f);
            if (swimGravity > 0.0f) swimGravity = -swimGravity;
            float swimMaxFallSpeed = -std::abs(getRegistryFloat(baseSystem, "SwimMaxFallSpeed", 3.0f));
            float swimAscendAcceleration = std::max(0.0f, getRegistryFloat(baseSystem, "SwimAscendAcceleration", 16.0f));
            float swimAscendSpeed = std::max(0.4f, getRegistryFloat(baseSystem, "SwimAscendSpeed", 4.8f));
            float swimVerticalDamping = glm::clamp(getRegistryFloat(baseSystem, "SwimVerticalDamping", 2.8f), 0.0f, 40.0f);
            float swimSurfaceDepth = glm::clamp(getRegistryFloat(baseSystem, "SwimSurfaceDepth", 0.45f), 0.05f, 2.5f);
            float swimSurfaceBuoyancy = std::max(0.0f, getRegistryFloat(baseSystem, "SwimSurfaceBuoyancy", 10.5f));
            float swimPassiveSinkSpeed = glm::clamp(getRegistryFloat(baseSystem, "SwimPassiveSinkSpeed", 0.9f), 0.0f, 4.0f);
            const float swimDescendSpeed = std::max(std::abs(swimMaxFallSpeed), std::abs(getRegistryFloat(baseSystem, "SwimDescendSpeed", 8.5f)));
            if (spaceDown && !descendDown) {
                player.verticalVelocity += swimAscendAcceleration * stepDt;
            }
            if (descendDown) {
                player.verticalVelocity += swimGravity * stepDt;
                player.verticalVelocity -= swimDescendAcceleration * stepDt;
            }

            float nearSurface = 0.0f;
            if (hasLocalWaterSurface) {
                float depthBelowSurface = waterSurfaceY - player.cameraPosition.y;
                nearSurface = glm::clamp(1.0f - (depthBelowSurface / (swimSurfaceDepth + 1.2f)), 0.0f, 1.0f);
            }
            if (nearSurface > 0.0f) {
                // Keep camera just above water while idle-treading.
                // Bob only pushes downward so we do not float unnaturally high at the top of the wave.
                float depthBelowSurface = waterSurfaceY - player.cameraPosition.y;
                float targetDepth = (-swimTreadHeadAboveSurface + swimTreadLowerByBlocks) + std::max(0.0f, swimTreadBob);
                float depthError = depthBelowSurface - targetDepth;
                player.verticalVelocity += depthError * swimSurfaceBuoyancy * nearSurface * stepDt;
            }

            if (swimVerticalDamping > 0.0f) {
                float damping = std::exp(-swimVerticalDamping * stepDt);
                player.verticalVelocity *= damping;
            }

            float activeMaxFallSpeed = descendDown ? -swimDescendSpeed : -swimPassiveSinkSpeed;
            if (player.verticalVelocity < activeMaxFallSpeed) player.verticalVelocity = activeMaxFallSpeed;
            if (player.verticalVelocity > swimAscendSpeed) player.verticalVelocity = swimAscendSpeed;
            player.cameraPosition.y += player.verticalVelocity * stepDt;
            player.cameraPosition += s_swimStrokeVelocity * stepDt;
            if (!swimWallClimbAssistActive && hasLocalWaterSurface && player.cameraPosition.y > swimMaxTreadY) {
                player.cameraPosition.y = swimMaxTreadY;
                if (player.verticalVelocity > 0.0f) player.verticalVelocity = 0.0f;
            }
            s_prevPlayerInWater = true;
            return;
        }

        s_swimStrokeVelocity = glm::vec3(0.0f);
        s_swimStrokeCooldown = 0.0f;

        const float gravitySetting = getRegistryFloat(baseSystem, "GravityStrength", -21.0f);
        const float gravity = (gravitySetting > 0.0f) ? -gravitySetting : gravitySetting;
        const float terminalFall = -std::abs(
            getRegistryFloat(
                baseSystem,
                "TerminalFallSpeed",
                getRegistryFloat(baseSystem, "MaxFallSpeed", 22.0f)
            )
        );
        const float fallRampSeconds = std::max(0.05f, getRegistryFloat(baseSystem, "GravityFallRampSeconds", 1.35f));
        const float fallRampMultiplier = glm::clamp(getRegistryFloat(baseSystem, "GravityFallRampMultiplier", 2.6f), 1.0f, 8.0f);

        if (boulderingLatched) {
            const float springStrength = glm::clamp(getRegistryFloat(baseSystem, "BoulderingSpringStrength", 90.0f), 0.0f, 600.0f);
            const float springDamping = glm::clamp(getRegistryFloat(baseSystem, "BoulderingSpringDamping", 8.0f), 0.0f, 120.0f);
            const float springMaxAccel = std::max(0.0f, getRegistryFloat(baseSystem, "BoulderingSpringMaxAccel", 180.0f));
            glm::vec3 playerVelocity(0.0f);
            if (stepDt > 1e-5f) {
                playerVelocity = (player.cameraPosition - player.prevCameraPosition) / stepDt;
            }
            glm::vec3 totalAccel(0.0f);
            auto accumulateAnchor = [&](bool latched, const glm::vec3& anchor, float restLength) {
                if (!latched) return;
                glm::vec3 toPlayer = player.cameraPosition - anchor;
                float distance = glm::length(toPlayer);
                if (distance < 1e-4f) return;
                float extension = distance - std::max(0.05f, restLength);
                if (extension <= 0.0f) return;
                glm::vec3 awayDir = toPlayer / distance;
                float velAway = glm::max(0.0f, glm::dot(playerVelocity, awayDir));
                float accelMag = springStrength * extension + springDamping * velAway;
                totalAccel += (-awayDir) * accelMag;
            };
            accumulateAnchor(player.boulderPrimaryLatched, player.boulderPrimaryAnchor, player.boulderPrimaryRestLength);
            accumulateAnchor(player.boulderSecondaryLatched, player.boulderSecondaryAnchor, player.boulderSecondaryRestLength);
            float accelLen = glm::length(totalAccel);
            if (springMaxAccel > 0.0f && accelLen > springMaxAccel) {
                totalAccel = (totalAccel / accelLen) * springMaxAccel;
                accelLen = springMaxAccel;
            }
            if (accelLen > 1e-4f) {
                player.cameraPosition += totalAccel * (stepDt * stepDt);
                player.verticalVelocity += totalAccel.y * stepDt;
                player.onGround = false;
            }
        }

        if (player.onGround) {
            s_fallTime = 0.0f;
        } else if (player.verticalVelocity < -0.25f) {
            s_fallTime += stepDt;
        }
        float rampT = glm::clamp(s_fallTime / fallRampSeconds, 0.0f, 1.0f);
        float gravityScale = 1.0f + (fallRampMultiplier - 1.0f) * (rampT * rampT);

        float gravityMul = 1.0f;
        if (boulderingLatched) {
            gravityMul = glm::clamp(getRegistryFloat(baseSystem, "BoulderingGravityScale", 0.30f), 0.0f, 2.0f);
        }
        player.verticalVelocity += gravity * gravityScale * gravityMul * stepDt;
        if (player.verticalVelocity < terminalFall) player.verticalVelocity = terminalFall;
        player.cameraPosition.y += player.verticalVelocity * stepDt;
        s_prevPlayerInWater = false;
    }
}
