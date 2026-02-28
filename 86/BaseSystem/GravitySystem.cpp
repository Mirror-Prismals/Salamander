#pragma once
#include "../Host.h"
#include <cmath>
#include <limits>
#include <unordered_map>

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
            if (!fileName || !baseSystem.audio || !baseSystem.audio->chuck) return false;
            const std::string scriptPath = std::string("Procedures/chuck/gameplay/") + fileName;
            static std::unordered_map<std::string, double> s_lastTrigger;
            const double now = glfwGetTime();
            auto it = s_lastTrigger.find(scriptPath);
            if (it != s_lastTrigger.end() && (now - it->second) < static_cast<double>(cooldownSeconds)) {
                return false;
            }
            std::vector<t_CKUINT> ids;
            bool ok = baseSystem.audio->chuck->compileFile(scriptPath, "", 1, FALSE, &ids);
            if (ok && !ids.empty()) {
                s_lastTrigger[scriptPath] = now;
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

        int resolveLeafPrototypeID(const std::vector<Entity>& prototypes) {
            for (const auto& proto : prototypes) {
                if (proto.name == "Leaf") return proto.prototypeID;
            }
            return -1;
        }

        bool isInWaterCell(const VoxelWorldContext& voxelWorld, int waterPrototypeID, const glm::vec3& point) {
            if (!voxelWorld.enabled || waterPrototypeID < 0) return false;
            glm::ivec3 cell(
                static_cast<int>(std::floor(point.x)),
                static_cast<int>(std::floor(point.y)),
                static_cast<int>(std::floor(point.z))
            );
            return voxelWorld.getBlockWorld(cell) == static_cast<uint32_t>(waterPrototypeID);
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

        bool isPlayerInWater(const VoxelWorldContext& voxelWorld, int waterPrototypeID, const glm::vec3& playerPos) {
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
                if (isInWaterCell(voxelWorld, waterPrototypeID, playerPos + offset)) return true;
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

        bool hasWallStoneAnchor(const BaseSystem& baseSystem,
                                const std::vector<Entity>& prototypes,
                                const glm::ivec3& cell,
                                int worldIndexHint) {
            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
                if (id > 0 && id < prototypes.size() && isWallStonePrototypeID(prototypes, static_cast<int>(id))) {
                    return true;
                }
            }
            if (!baseSystem.level) return false;
            if (worldIndexHint < 0 || worldIndexHint >= static_cast<int>(baseSystem.level->worlds.size())) return false;
            const Entity& world = baseSystem.level->worlds[static_cast<size_t>(worldIndexHint)];
            const glm::vec3 targetPos = glm::vec3(cell);
            for (const auto& inst : world.instances) {
                if (glm::distance(inst.position, targetPos) > 0.05f) continue;
                if (isWallStonePrototypeID(prototypes, inst.prototypeID)) return true;
            }
            return false;
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

        bool findLocalWaterSurface(const VoxelWorldContext& voxelWorld,
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
                        if (!findWaterSurfaceInColumn(voxelWorld, waterPrototypeID, cx + dx, cz + dz, minY, maxY, surfaceY)) continue;
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

    void ApplyGravity(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
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
        const bool spaceDown = win && (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS);
        const bool shiftDown = win && (
            glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
            || glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS
        );

        const bool leafClimbEnabled = getRegistryBool(baseSystem, "LeafClimbEnabled", true);
        bool playerInLeaves = false;
        if (leafClimbEnabled && baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
            int leafPrototypeID = resolveLeafPrototypeID(prototypes);
            if (leafPrototypeID >= 0) {
                playerInLeaves = isPlayerTouchingLeaves(*baseSystem.voxelWorld, leafPrototypeID, player.cameraPosition);
            }
        }

        const bool swimmingEnabled = getRegistryBool(baseSystem, "SwimmingEnabled", true);
        bool playerInWater = false;
        float waterSurfaceY = player.cameraPosition.y + 0.85f;
        if (swimmingEnabled && baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
            int waterPrototypeID = resolveWaterPrototypeID(prototypes);
            if (waterPrototypeID >= 0) {
                playerInWater = isPlayerInWater(*baseSystem.voxelWorld, waterPrototypeID, player.cameraPosition);
                if (playerInWater) {
                    findLocalWaterSurface(*baseSystem.voxelWorld, waterPrototypeID, player.cameraPosition, waterSurfaceY);
                }
            }
        }
        if (player.boulderPrimaryLatched
            && !hasWallStoneAnchor(baseSystem, prototypes, player.boulderPrimaryCell, player.boulderPrimaryWorldIndex)) {
            player.boulderPrimaryLatched = false;
            player.boulderPrimaryRestLength = 0.0f;
            player.boulderPrimaryWorldIndex = -1;
        }
        if (player.boulderSecondaryLatched
            && !hasWallStoneAnchor(baseSystem, prototypes, player.boulderSecondaryCell, player.boulderSecondaryWorldIndex)) {
            player.boulderSecondaryLatched = false;
            player.boulderSecondaryRestLength = 0.0f;
            player.boulderSecondaryWorldIndex = -1;
        }
        const bool boulderingLatched = (player.boulderPrimaryLatched || player.boulderSecondaryLatched);

        static bool s_prevPlayerInWater = false;
        static bool s_prevPlayerInLeaves = false;
        static bool s_leafClimbBlockUntilSpaceRelease = false;
        static float s_swimSlowdownDelayTimer = 0.0f;
        static float s_fallTime = 0.0f;

        if (playerInLeaves && !s_prevPlayerInLeaves) {
            // Must press Space after contacting leaves; holding Space during entry does not start climb.
            s_leafClimbBlockUntilSpaceRelease = spaceDown;
        } else if (!playerInLeaves) {
            s_leafClimbBlockUntilSpaceRelease = false;
        }
        if (playerInLeaves && s_leafClimbBlockUntilSpaceRelease && !spaceDown) {
            s_leafClimbBlockUntilSpaceRelease = false;
        }

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

        if (playerInWater) {
            s_fallTime = 0.0f;
            const float baseGravity = (getRegistryFloat(baseSystem, "GravityStrength", -21.0f) > 0.0f)
                ? -getRegistryFloat(baseSystem, "GravityStrength", -21.0f)
                : getRegistryFloat(baseSystem, "GravityStrength", -21.0f);
            const float swimDescendAcceleration = std::max(0.0f, getRegistryFloat(baseSystem, "SwimDescendAcceleration", 22.0f));

            if (s_swimSlowdownDelayTimer > 0.0f) {
                const float entryGravityScale = glm::clamp(getRegistryFloat(baseSystem, "SwimEntryGravityScale", 1.0f), 0.0f, 2.5f);
                const float entryMaxFallSpeed = -std::abs(getRegistryFloat(baseSystem, "SwimEntryMaxFallSpeed", 24.0f));
                const float entryAscendAcceleration = std::max(0.0f, getRegistryFloat(baseSystem, "SwimEntryAscendAcceleration", 8.0f));
                const float swimAscendSpeed = std::max(0.4f, getRegistryFloat(baseSystem, "SwimAscendSpeed", 4.8f));

                player.verticalVelocity += baseGravity * entryGravityScale * stepDt;
                if (spaceDown && !shiftDown) {
                    player.verticalVelocity += entryAscendAcceleration * stepDt;
                }
                if (shiftDown) {
                    player.verticalVelocity -= swimDescendAcceleration * stepDt;
                }
                if (player.verticalVelocity < entryMaxFallSpeed) player.verticalVelocity = entryMaxFallSpeed;
                if (player.verticalVelocity > swimAscendSpeed) player.verticalVelocity = swimAscendSpeed;
                player.cameraPosition.y += player.verticalVelocity * stepDt;
                s_swimSlowdownDelayTimer = std::max(0.0f, s_swimSlowdownDelayTimer - stepDt);
                s_prevPlayerInWater = true;
                s_prevPlayerInLeaves = playerInLeaves;
                return;
            }

            float swimGravity = getRegistryFloat(baseSystem, "SwimGravity", -5.2f);
            if (swimGravity > 0.0f) swimGravity = -swimGravity;
            float swimMaxFallSpeed = -std::abs(getRegistryFloat(baseSystem, "SwimMaxFallSpeed", 3.0f));
            float swimAscendAcceleration = std::max(0.0f, getRegistryFloat(baseSystem, "SwimAscendAcceleration", 16.0f));
            float swimAscendSpeed = std::max(0.4f, getRegistryFloat(baseSystem, "SwimAscendSpeed", 4.8f));
            float swimVerticalDamping = glm::clamp(getRegistryFloat(baseSystem, "SwimVerticalDamping", 2.8f), 0.0f, 40.0f);
            float swimSurfaceDepth = glm::clamp(getRegistryFloat(baseSystem, "SwimSurfaceDepth", 0.45f), 0.05f, 2.5f);
            float swimSurfaceBobAmplitude = glm::clamp(getRegistryFloat(baseSystem, "SwimSurfaceBobAmplitude", 0.05f), 0.0f, 0.35f);
            float swimSurfaceBobFrequency = glm::clamp(getRegistryFloat(baseSystem, "SwimSurfaceBobFrequency", 1.3f), 0.0f, 8.0f);
            float swimSurfaceBuoyancy = std::max(0.0f, getRegistryFloat(baseSystem, "SwimSurfaceBuoyancy", 10.5f));
            const float swimDescendSpeed = std::max(std::abs(swimMaxFallSpeed), std::abs(getRegistryFloat(baseSystem, "SwimDescendSpeed", 8.5f)));
            player.verticalVelocity += swimGravity * stepDt;
            if (spaceDown) {
                player.verticalVelocity += swimAscendAcceleration * stepDt;
            }
            if (shiftDown) {
                player.verticalVelocity -= swimDescendAcceleration * stepDt;
            }

            float depthBelowSurface = waterSurfaceY - player.cameraPosition.y;
            float nearSurface = glm::clamp(1.0f - (depthBelowSurface / (swimSurfaceDepth + 1.2f)), 0.0f, 1.0f);
            if (nearSurface > 0.0f) {
                const float kTau = 6.28318530718f;
                float bob = std::sin(static_cast<float>(glfwGetTime()) * swimSurfaceBobFrequency * kTau) * swimSurfaceBobAmplitude;
                float targetDepth = swimSurfaceDepth + bob;
                float depthError = depthBelowSurface - targetDepth;
                player.verticalVelocity += depthError * swimSurfaceBuoyancy * nearSurface * stepDt;
            }

            if (swimVerticalDamping > 0.0f) {
                float damping = std::exp(-swimVerticalDamping * stepDt);
                player.verticalVelocity *= damping;
            }

            float activeMaxFallSpeed = shiftDown ? -swimDescendSpeed : swimMaxFallSpeed;
            if (player.verticalVelocity < activeMaxFallSpeed) player.verticalVelocity = activeMaxFallSpeed;
            if (player.verticalVelocity > swimAscendSpeed) player.verticalVelocity = swimAscendSpeed;
            player.cameraPosition.y += player.verticalVelocity * stepDt;
            s_prevPlayerInWater = true;
            s_prevPlayerInLeaves = playerInLeaves;
            return;
        }

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
        bool leafClimbAscending = leafClimbEnabled
            && playerInLeaves
            && !playerInWater
            && !s_leafClimbBlockUntilSpaceRelease
            && spaceDown;
        if (leafClimbEnabled && playerInLeaves && !playerInWater && leafClimbAscending) {
            const float leafAscendAcceleration = std::max(0.0f, getRegistryFloat(baseSystem, "LeafClimbAscendAcceleration", 20.0f));
            const float leafAscendSpeed = std::max(0.4f, getRegistryFloat(baseSystem, "LeafClimbAscendSpeed", 5.4f));
            player.verticalVelocity += leafAscendAcceleration * stepDt;
            if (player.verticalVelocity > leafAscendSpeed) player.verticalVelocity = leafAscendSpeed;
            player.cameraPosition.y += player.verticalVelocity * stepDt;
            s_fallTime = 0.0f;
            s_prevPlayerInWater = false;
            s_prevPlayerInLeaves = playerInLeaves;
            return;
        }

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
        s_prevPlayerInLeaves = playerInLeaves;
    }
}
