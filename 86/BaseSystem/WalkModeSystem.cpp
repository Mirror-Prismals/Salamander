#pragma once
#include "../Host.h"

#include <cmath>
#include <string>
#include <unordered_map>

namespace VoxelMeshingSystemLogic { void RequestPriorityVoxelRemesh(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const glm::ivec3& worldCell); }
namespace StructureCaptureSystemLogic { void NotifyBlockChanged(BaseSystem& baseSystem, int worldIndex, const glm::vec3& position); }

namespace WalkModeSystemLogic {

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

        bool isInWaterCell(const VoxelWorldContext& voxelWorld, int waterPrototypeID, const glm::vec3& point) {
            if (!voxelWorld.enabled || waterPrototypeID < 0) return false;
            glm::ivec3 cell(
                static_cast<int>(std::floor(point.x)),
                static_cast<int>(std::floor(point.y)),
                static_cast<int>(std::floor(point.z))
            );
            return voxelWorld.getBlockWorld(cell) == static_cast<uint32_t>(waterPrototypeID);
        }

        bool isPlayerInWater(const BaseSystem& baseSystem,
                             const std::vector<Entity>& prototypes,
                             const glm::vec3& playerPos) {
            if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return false;
            int waterPrototypeID = resolveWaterPrototypeID(prototypes);
            if (waterPrototypeID < 0) return false;
            const VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
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
                if (isInWaterCell(voxelWorld, waterPrototypeID, playerPos + offset)) {
                    return true;
                }
            }
            return false;
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

        bool removeWallStoneAtCell(BaseSystem& baseSystem,
                                   LevelContext& level,
                                   std::vector<Entity>& prototypes,
                                   const glm::ivec3& cell,
                                   int worldIndexHint) {
            bool removed = false;
            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
                if (id != 0 && id < prototypes.size() && isWallStonePrototypeID(prototypes, static_cast<int>(id))) {
                    baseSystem.voxelWorld->setBlockWorld(cell, 0, 0);
                    VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, cell);
                    removed = true;
                }
            }

            if (worldIndexHint >= 0 && worldIndexHint < static_cast<int>(level.worlds.size())) {
                Entity& world = level.worlds[static_cast<size_t>(worldIndexHint)];
                const glm::vec3 cellPos = glm::vec3(cell);
                for (size_t i = 0; i < world.instances.size();) {
                    const EntityInstance& inst = world.instances[i];
                    if (glm::distance(inst.position, cellPos) <= 0.05f
                        && isWallStonePrototypeID(prototypes, inst.prototypeID)) {
                        world.instances[i] = world.instances.back();
                        world.instances.pop_back();
                        removed = true;
                        continue;
                    }
                    ++i;
                }
            }

            if (removed) {
                int notifyWorldIndex = worldIndexHint;
                if (notifyWorldIndex < 0 || notifyWorldIndex >= static_cast<int>(level.worlds.size())) {
                    notifyWorldIndex = level.worlds.empty() ? -1 : 0;
                }
                if (notifyWorldIndex >= 0) {
                    StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, notifyWorldIndex, glm::vec3(cell));
                }
            }
            return removed;
        }
    }

    void ProcessWalkMovement(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player || !baseSystem.level || !win) return;
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
        LevelContext& level = *baseSystem.level;
        const bool swimmingEnabled = getRegistryBool(baseSystem, "SwimmingEnabled", true);
        const bool playerInWater = swimmingEnabled && isPlayerInWater(baseSystem, prototypes, player.cameraPosition);
        const bool shiftDown = (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
                            || (glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
        const bool keyW = glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS;
        const bool keyS = glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS;
        const bool keyA = glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS;
        const bool keyD = glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS;
        const bool spaceDown = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;

        // Walk mode uses horizontal plane movement. Sprint can only begin while grounded.
        glm::vec3 front(cos(glm::radians(player.cameraYaw)), 0.0f, sin(glm::radians(player.cameraYaw)));
        if (glm::length(front) < 0.0001f) front = glm::vec3(0.0f, 0.0f, -1.0f);
        front = glm::normalize(front);
        glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 moveInput(0.0f);
        if (keyW) moveInput += front;
        if (keyS) moveInput -= front;
        if (keyA) moveInput -= right;
        if (keyD) moveInput += right;
        bool hasMoveInput = glm::length(moveInput) > 0.0001f;
        if (hasMoveInput) moveInput = glm::normalize(moveInput);
        bool boulderingLatched = (player.boulderPrimaryLatched || player.boulderSecondaryLatched);

        const float legacyMoveSpeed = std::max(0.1f, getRegistryFloat(baseSystem, "WalkMoveSpeed", 4.5f));
        const bool sprintEnabled = getRegistryBool(baseSystem, "WalkSprintEnabled", true);
        const float sprintSpeed = std::max(
            0.1f,
            getRegistryFloat(baseSystem, "WalkSprintMoveSpeed", legacyMoveSpeed * 2.0f)
        );
        float baseSpeed = getRegistryFloat(baseSystem, "WalkBaseMoveSpeed", sprintSpeed * 0.56f);
        baseSpeed = glm::clamp(baseSpeed, 0.1f, sprintSpeed);
        const float walkStartSpeed = glm::clamp(getRegistryFloat(baseSystem, "WalkStartMoveSpeed", baseSpeed * 0.46f), 0.05f, baseSpeed);
        const float walkAccelSeconds = std::max(0.05f, getRegistryFloat(baseSystem, "WalkAccelerationSeconds", 1.05f));
        const float walkDecelSeconds = std::max(0.02f, getRegistryFloat(baseSystem, "WalkDecelerationSeconds", 0.22f));
        const float walkAccelCurveExponent = glm::clamp(getRegistryFloat(baseSystem, "WalkAccelerationCurveExponent", 1.6f), 0.5f, 5.0f);
        const float airControlScale = glm::clamp(getRegistryFloat(baseSystem, "WalkAirControlScale", 0.16f), 0.0f, 1.0f);
        const float airWalkSpeedScale = glm::clamp(getRegistryFloat(baseSystem, "WalkAirWalkSpeedScale", airControlScale), 0.0f, 1.0f);
        const float airSprintSpeedScale = glm::clamp(getRegistryFloat(baseSystem, "WalkAirSprintSpeedScale", 1.0f), 0.0f, 1.0f);
        const float airSpeedCarrySeconds = std::max(0.05f, getRegistryFloat(baseSystem, "WalkAirSpeedCarrySeconds", 0.55f));
        const float sprintChargeSeconds = std::max(0.05f, getRegistryFloat(baseSystem, "WalkSprintChargeSeconds", 1.45f));
        const float sprintReleaseSeconds = std::max(0.05f, getRegistryFloat(baseSystem, "WalkSprintReleaseSeconds", 0.30f));
        const float sprintCurveExponent = glm::clamp(getRegistryFloat(baseSystem, "WalkSprintCurveExponent", 2.5f), 1.0f, 8.0f);
        const bool bhopEnabled = getRegistryBool(baseSystem, "WalkBhopEnabled", true);
        const float bhopTimingWindow = glm::clamp(getRegistryFloat(baseSystem, "WalkBhopTimingWindowSeconds", 0.20f), 0.02f, 0.8f);
        const float bhopStackBonus = glm::clamp(getRegistryFloat(baseSystem, "WalkBhopStackSpeedBonus", 0.5f), 0.0f, 3.0f);
        const int bhopMaxStacks = std::max(1, getRegistryInt(baseSystem, "WalkBhopMaxStacks", 3));
        const float bhopBonusMax = std::max(0.0f, getRegistryFloat(baseSystem, "WalkBhopBonusMax", bhopStackBonus * static_cast<float>(bhopMaxStacks)));
        const float bhopAbsoluteMax = std::max(
            sprintSpeed,
            getRegistryFloat(baseSystem, "WalkBhopAbsoluteMaxSpeed", sprintSpeed + bhopBonusMax)
        );

        static bool s_sprintActive = false;
        static float s_sprintCharge = 0.0f;
        static float s_airborneSpeedScale = 1.0f;
        static float s_walkRamp = 0.0f;
        static bool s_prevOnGround = false;
        static int s_bhopStacks = 0;
        static float s_bhopWindowRemaining = 0.0f;
        static bool s_spaceWasDown = false;
        const bool spacePressed = spaceDown && !s_spaceWasDown;

        const bool shouldAccelerateWalk = hasMoveInput && !playerInWater;
        if (shouldAccelerateWalk) {
            s_walkRamp = std::min(1.0f, s_walkRamp + (dt / walkAccelSeconds));
        } else {
            s_walkRamp = std::max(0.0f, s_walkRamp - (dt / walkDecelSeconds));
        }
        const float walkT = std::pow(glm::clamp(s_walkRamp, 0.0f, 1.0f), walkAccelCurveExponent);
        const float walkSpeed = walkStartSpeed + (baseSpeed - walkStartSpeed) * walkT;
        const bool walkAtMaxSpeed = walkT >= 0.999f;

        if (!sprintEnabled || playerInWater || !shiftDown) {
            s_sprintActive = false;
        } else if (!s_sprintActive && player.onGround && hasMoveInput && walkAtMaxSpeed) {
            // Sprint start is gated to grounded state and full walking speed.
            s_sprintActive = true;
        }
        if (s_sprintActive && shiftDown && !playerInWater && hasMoveInput) {
            s_sprintCharge = std::min(1.0f, s_sprintCharge + (dt / sprintChargeSeconds));
        } else {
            s_sprintCharge = std::max(0.0f, s_sprintCharge - (dt / sprintReleaseSeconds));
        }
        float sprintT = std::pow(glm::clamp(s_sprintCharge, 0.0f, 1.0f), sprintCurveExponent);

        const bool landedThisFrame = (!s_prevOnGround && player.onGround);
        if (landedThisFrame) {
            s_bhopWindowRemaining = bhopTimingWindow;
        }
        if (s_bhopWindowRemaining > 0.0f) {
            s_bhopWindowRemaining = std::max(0.0f, s_bhopWindowRemaining - dt);
        }

        const bool bhopCanApply = bhopEnabled && s_sprintActive && shiftDown && hasMoveInput && !playerInWater;
        if (!bhopCanApply) {
            s_bhopStacks = 0;
            s_bhopWindowRemaining = 0.0f;
        } else if (player.onGround && s_bhopStacks > 0 && s_bhopWindowRemaining <= 0.0f) {
            // Missed the timing window after landing: drop back to base sprint speed.
            s_bhopStacks = 0;
        }

        float moveSpeed = walkSpeed + (sprintSpeed - baseSpeed) * sprintT;
        if (bhopCanApply && s_bhopStacks > 0) {
            const float bonusFromStacks = static_cast<float>(s_bhopStacks) * bhopStackBonus;
            const float bhopBonus = std::min(bhopBonusMax, bonusFromStacks);
            moveSpeed = std::min(bhopAbsoluteMax, moveSpeed + bhopBonus);
        }
        if (!player.onGround && !playerInWater) {
            float minAirScale = s_sprintActive ? airSprintSpeedScale : airWalkSpeedScale;
            s_airborneSpeedScale = std::max(minAirScale, s_airborneSpeedScale - (dt / airSpeedCarrySeconds));
            moveSpeed *= s_airborneSpeedScale;
        } else {
            s_airborneSpeedScale = 1.0f;
        }
        if (playerInWater) {
            float swimMoveScale = glm::clamp(getRegistryFloat(baseSystem, "SwimMoveSpeedScale", 0.78f), 0.1f, 2.0f);
            moveSpeed *= swimMoveScale;
        }
        moveSpeed *= dt;
        float jumpVelocity = 8.0f; // fixed jump impulse

        glm::vec3 moveDelta = moveInput * moveSpeed;
        bool consumedSpaceForVault = false;

        if (boulderingLatched) {
            // Latched traversal uses wall-relative axes instead of world-forward sprinting.
            s_sprintActive = false;
            s_sprintCharge = 0.0f;
            s_airborneSpeedScale = 1.0f;
            s_walkRamp = 0.0f;
            s_bhopStacks = 0;
            s_bhopWindowRemaining = 0.0f;

            glm::vec3 normalAccum(0.0f);
            auto accumulateAnchor = [&](bool latched, const glm::vec3& anchor, const glm::vec3& normal) {
                if (!latched) return;
                (void)anchor;
                normalAccum += normal;
            };
            accumulateAnchor(player.boulderPrimaryLatched, player.boulderPrimaryAnchor, player.boulderPrimaryNormal);
            accumulateAnchor(player.boulderSecondaryLatched, player.boulderSecondaryAnchor, player.boulderSecondaryNormal);

            glm::vec3 wallNormal = normalAccum;
            if (glm::length(wallNormal) < 0.001f) wallNormal = glm::vec3(0.0f, 0.0f, 1.0f);
            wallNormal = glm::normalize(wallNormal);

            glm::vec3 wallUp = glm::vec3(0.0f, 1.0f, 0.0f) - wallNormal * glm::dot(glm::vec3(0.0f, 1.0f, 0.0f), wallNormal);
            if (glm::length(wallUp) < 0.001f) wallUp = glm::vec3(0.0f, 1.0f, 0.0f);
            wallUp = glm::normalize(wallUp);
            glm::vec3 wallRight = glm::cross(wallUp, wallNormal);
            if (glm::length(wallRight) < 0.001f) wallRight = right;
            wallRight = glm::normalize(wallRight);

            glm::vec3 wallInput(0.0f);
            if (keyA) wallInput -= wallRight;
            if (keyD) wallInput += wallRight;
            if (glm::length(wallInput) > 0.0001f) wallInput = glm::normalize(wallInput);

            const float wallMoveSpeed = std::max(0.05f, getRegistryFloat(baseSystem, "BoulderingWallMoveSpeed", 3.9f));
            moveDelta = wallInput * (wallMoveSpeed * dt);

            if (!playerInWater && spacePressed) {
                const float vaultSpeed = std::max(0.0f, getRegistryFloat(baseSystem, "BoulderingVaultLaunchSpeed", 9.5f));
                const float extraHeightBlocks = std::max(0.0f, getRegistryFloat(baseSystem, "BoulderingVaultExtraHeightBlocks", 1.0f));
                const float gravityMagnitude = std::abs(getRegistryFloat(baseSystem, "GravityStrength", -21.0f));
                float launchSpeed = vaultSpeed;
                if (gravityMagnitude > 0.0001f && extraHeightBlocks > 0.0f) {
                    // Add a deterministic vertical boost equal to the requested extra apex height.
                    launchSpeed = std::sqrt(vaultSpeed * vaultSpeed + 2.0f * gravityMagnitude * extraHeightBlocks);
                }
                player.verticalVelocity = launchSpeed;
                player.boulderLaunchVelocity = glm::vec3(0.0f);

                const glm::ivec3 primaryCell = player.boulderPrimaryCell;
                const int primaryWorld = player.boulderPrimaryWorldIndex;
                const glm::ivec3 secondaryCell = player.boulderSecondaryCell;
                const int secondaryWorld = player.boulderSecondaryWorldIndex;
                const bool hadPrimary = player.boulderPrimaryLatched;
                const bool hadSecondary = player.boulderSecondaryLatched;

                player.boulderPrimaryLatched = false;
                player.boulderSecondaryLatched = false;
                player.boulderPrimaryRestLength = 0.0f;
                player.boulderSecondaryRestLength = 0.0f;
                player.boulderPrimaryWorldIndex = -1;
                player.boulderSecondaryWorldIndex = -1;
                player.boulderPrimaryNormal = glm::vec3(0.0f, 0.0f, 1.0f);
                player.boulderSecondaryNormal = glm::vec3(0.0f, 0.0f, 1.0f);
                player.onGround = false;

                const bool breakHoldOnVault = getRegistryBool(baseSystem, "BoulderingBreakHoldOnVault", false);
                if (breakHoldOnVault) {
                    if (hadPrimary) {
                        removeWallStoneAtCell(baseSystem, level, prototypes, primaryCell, primaryWorld);
                    }
                    if (hadSecondary) {
                        const bool sameAnchor = hadPrimary
                            && primaryWorld == secondaryWorld
                            && primaryCell == secondaryCell;
                        if (!sameAnchor) {
                            removeWallStoneAtCell(baseSystem, level, prototypes, secondaryCell, secondaryWorld);
                        }
                    }
                    triggerGameplaySfx(baseSystem, "break_stone.ck", 0.02f);
                }
                consumedSpaceForVault = true;
                boulderingLatched = false;
            }
        }

        const float launchDrag = glm::clamp(getRegistryFloat(baseSystem, "BoulderingVaultHorizontalDrag", 4.2f), 0.0f, 40.0f);
        const float launchMinSpeed = std::max(0.0f, getRegistryFloat(baseSystem, "BoulderingVaultHorizontalMinSpeed", 0.05f));
        if (glm::length(player.boulderLaunchVelocity) > launchMinSpeed) {
            moveDelta += player.boulderLaunchVelocity * dt;
            float decay = 1.0f - launchDrag * dt;
            if (decay < 0.0f) decay = 0.0f;
            player.boulderLaunchVelocity *= decay;
            if (glm::length(player.boulderLaunchVelocity) <= launchMinSpeed) {
                player.boulderLaunchVelocity = glm::vec3(0.0f);
            }
        } else if (!boulderingLatched) {
            player.boulderLaunchVelocity = glm::vec3(0.0f);
        }

        // Jump on tap when grounded
        const bool boulderingLatchedNow = (player.boulderPrimaryLatched || player.boulderSecondaryLatched);
        if (!playerInWater && !boulderingLatchedNow && !consumedSpaceForVault && spacePressed && player.onGround) {
            const bool jumpStartsOrAdvancesBhop = bhopEnabled && s_sprintActive && shiftDown && hasMoveInput;
            if (jumpStartsOrAdvancesBhop) {
                if (s_bhopStacks <= 0) {
                    // First jump starts the b-hop chain.
                    s_bhopStacks = 1;
                } else if (s_bhopWindowRemaining > 0.0f) {
                    // Timed jump near landing extends the chain.
                    s_bhopStacks = std::min(bhopMaxStacks, s_bhopStacks + 1);
                } else {
                    // Late jump restarts chain from the first hop bonus.
                    s_bhopStacks = 1;
                }
                s_bhopWindowRemaining = 0.0f;
            } else {
                s_bhopStacks = 0;
                s_bhopWindowRemaining = 0.0f;
            }
            player.verticalVelocity = jumpVelocity;
            player.onGround = false;
        }
        s_spaceWasDown = spaceDown;
        s_prevOnGround = player.onGround;

        const bool footstepEnabled = getRegistryBool(baseSystem, "WalkFootstepEnabled", true);
        const float footstepStepDistance = glm::clamp(getRegistryFloat(baseSystem, "WalkFootstepStepDistance", 2.20f), 0.1f, 4.0f);
        const float footstepCooldown = std::max(0.0f, getRegistryFloat(baseSystem, "WalkFootstepCooldown", 0.08f));
        static float s_footstepDistance = 0.0f;
        float horizontalStepDist = glm::length(glm::vec2(moveDelta.x, moveDelta.z));
        if (footstepEnabled && player.onGround && !playerInWater && !boulderingLatchedNow && horizontalStepDist > 1e-5f) {
            s_footstepDistance += horizontalStepDist;
            if (s_footstepDistance >= footstepStepDistance) {
                triggerGameplaySfx(baseSystem, "footstep.ck", footstepCooldown);
                s_footstepDistance = std::fmod(s_footstepDistance, footstepStepDistance);
            }
        } else if (horizontalStepDist <= 1e-5f || !player.onGround || playerInWater || boulderingLatchedNow) {
            s_footstepDistance = 0.0f;
        }

        player.cameraPosition += moveDelta;
    }
}
