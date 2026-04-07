#pragma once
#include "Host/PlatformInput.h"
#include "../Host.h"

#include <cmath>
#include <string>
#include <unordered_map>

namespace VoxelMeshingSystemLogic { void RequestPriorityVoxelRemesh(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const glm::ivec3& worldCell); }
namespace StructureCaptureSystemLogic { void NotifyBlockChanged(BaseSystem& baseSystem, int worldIndex, const glm::vec3& position); }
namespace AudioSystemLogic { bool TriggerGameplaySfx(BaseSystem& baseSystem, const std::string& cueName, float gain); }

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

        bool isWaterLikeName(const std::string& name) {
            return name == "Water" || name.rfind("WaterSlope", 0) == 0;
        }

        bool isWaterLikePrototypeID(const std::vector<Entity>& prototypes, uint32_t id) {
            if (id == 0 || id >= prototypes.size()) return false;
            return isWaterLikeName(prototypes[id].name);
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
                if (isInWaterCell(voxelWorld, prototypes, waterPrototypeID, playerPos + offset)) {
                    return true;
                }
            }
            return false;
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

        enum class SlopeDir : int { None = 0, PosX = 1, NegX = 2, PosZ = 3, NegZ = 4 };

        SlopeDir slopeDirFromPrototypeName(const std::string& name) {
            if (name == "DebugSlopeTexPosX" || name == "WaterSlopePosX") return SlopeDir::PosX;
            if (name == "DebugSlopeTexNegX" || name == "WaterSlopeNegX") return SlopeDir::NegX;
            if (name == "DebugSlopeTexPosZ" || name == "WaterSlopePosZ") return SlopeDir::PosZ;
            if (name == "DebugSlopeTexNegZ" || name == "WaterSlopeNegZ") return SlopeDir::NegZ;
            return SlopeDir::None;
        }

        glm::vec3 slopeDownDirection(SlopeDir dir) {
            switch (dir) {
                case SlopeDir::PosX: return glm::vec3(-1.0f, 0.0f, 0.0f);
                case SlopeDir::NegX: return glm::vec3(1.0f, 0.0f, 0.0f);
                case SlopeDir::PosZ: return glm::vec3(0.0f, 0.0f, -1.0f);
                case SlopeDir::NegZ: return glm::vec3(0.0f, 0.0f, 1.0f);
                default: return glm::vec3(0.0f);
            }
        }

        bool sampleSlopeDownDirection(const BaseSystem& baseSystem,
                                      const std::vector<Entity>& prototypes,
                                      const glm::vec3& playerPos,
                                      float halfHeight,
                                      glm::vec3& outDownDir) {
            if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return false;
            const VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            const float feetY = playerPos.y - halfHeight;
            const int sampleBaseY = static_cast<int>(std::floor(feetY + 0.5f));
            static const glm::vec2 kOffsets[] = {
                glm::vec2(0.0f, 0.0f),
                glm::vec2(0.22f, 0.0f),
                glm::vec2(-0.22f, 0.0f),
                glm::vec2(0.0f, 0.22f),
                glm::vec2(0.0f, -0.22f)
            };
            static const int kYOffsets[] = {0, -1, 1};
            for (int yOffset : kYOffsets) {
                const int y = sampleBaseY + yOffset;
                for (const glm::vec2& offset : kOffsets) {
                    const glm::ivec3 cell(
                        static_cast<int>(std::floor(playerPos.x + offset.x)),
                        y,
                        static_cast<int>(std::floor(playerPos.z + offset.y))
                    );
                    const uint32_t id = voxelWorld.getBlockWorld(cell);
                    if (id == 0 || id >= prototypes.size()) continue;
                    const SlopeDir dir = slopeDirFromPrototypeName(prototypes[static_cast<size_t>(id)].name);
                    if (dir == SlopeDir::None) continue;
                    outDownDir = slopeDownDirection(dir);
                    return glm::length(outDownDir) > 0.001f;
                }
            }
            return false;
        }

        bool sampleSlopeDownDirectionCenter(const BaseSystem& baseSystem,
                                            const std::vector<Entity>& prototypes,
                                            const glm::vec3& playerPos,
                                            float halfHeight,
                                            glm::vec3& outDownDir) {
            if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return false;
            const VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            const float feetY = playerPos.y - halfHeight;
            const int sampleBaseY = static_cast<int>(std::floor(feetY + 0.5f));
            static const int kYOffsets[] = {0, -1, 1};
            const int cx = static_cast<int>(std::floor(playerPos.x));
            const int cz = static_cast<int>(std::floor(playerPos.z));
            for (int yOffset : kYOffsets) {
                const glm::ivec3 cell(cx, sampleBaseY + yOffset, cz);
                const uint32_t id = voxelWorld.getBlockWorld(cell);
                if (id == 0 || id >= prototypes.size()) continue;
                const SlopeDir dir = slopeDirFromPrototypeName(prototypes[static_cast<size_t>(id)].name);
                if (dir == SlopeDir::None) continue;
                outDownDir = slopeDownDirection(dir);
                return glm::length(outDownDir) > 0.001f;
            }
            return false;
        }

        bool isSolidBlockingPrototype(const Entity& proto) {
            if (!proto.isBlock || !proto.isSolid) return false;
            if (proto.name == "Water" || proto.name == "AudioVisualizer") return false;
            return true;
        }

        bool canStandFromProne(const BaseSystem& baseSystem,
                               const std::vector<Entity>& prototypes,
                               const glm::vec3& playerPos,
                               float proneHalfHeight,
                               float standingHalfHeight) {
            if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return true;
            if (standingHalfHeight <= proneHalfHeight + 0.0001f) return true;
            const VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            const float feetY = playerPos.y - proneHalfHeight;
            const float proneTopY = feetY + (2.0f * proneHalfHeight);
            const float standingTopY = feetY + (2.0f * standingHalfHeight);
            int minY = static_cast<int>(std::floor(proneTopY + 0.001f));
            int maxY = static_cast<int>(std::floor(standingTopY - 0.001f));
            if (maxY < minY) return true;
            static const glm::vec2 kXZOffsets[] = {
                glm::vec2(0.0f, 0.0f),
                glm::vec2(0.24f, 0.0f),
                glm::vec2(-0.24f, 0.0f),
                glm::vec2(0.0f, 0.24f),
                glm::vec2(0.0f, -0.24f)
            };
            for (int y = minY; y <= maxY; ++y) {
                for (const glm::vec2& offset : kXZOffsets) {
                    const glm::ivec3 cell(
                        static_cast<int>(std::floor(playerPos.x + offset.x)),
                        y,
                        static_cast<int>(std::floor(playerPos.z + offset.y))
                    );
                    const uint32_t id = voxelWorld.getBlockWorld(cell);
                    if (id == 0 || id >= prototypes.size()) continue;
                    if (isSolidBlockingPrototype(prototypes[static_cast<size_t>(id)])) {
                        return false;
                    }
                }
            }
            return true;
        }
    }

    void ProcessWalkMovement(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.player || !baseSystem.level || !win) return;
        if (baseSystem.gamemode != "survival") return;
        if (baseSystem.ui && baseSystem.ui->active) return;
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
        const bool standingOnLilypad = isPlayerStandingOnLilypad(baseSystem, prototypes, player.cameraPosition);
        bool playerInWater = swimmingEnabled && isPlayerInWater(baseSystem, prototypes, player.cameraPosition);
        if (playerInWater && standingOnLilypad) {
            playerInWater = false;
        }
        const bool shiftDown = PlatformInput::IsKeyDown(win, PlatformInput::Key::LeftShift)
                            || PlatformInput::IsKeyDown(win, PlatformInput::Key::RightShift);
        const bool keyW = PlatformInput::IsKeyDown(win, PlatformInput::Key::W);
        const bool keyS = PlatformInput::IsKeyDown(win, PlatformInput::Key::S);
        const bool keyA = PlatformInput::IsKeyDown(win, PlatformInput::Key::A);
        const bool keyD = PlatformInput::IsKeyDown(win, PlatformInput::Key::D);
        const bool spaceDown = PlatformInput::IsKeyDown(win, PlatformInput::Key::Space);
        const bool keyZ = PlatformInput::IsKeyDown(win, PlatformInput::Key::Z);

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
        glm::vec3 framePlanarVelocity(0.0f);
        if (dt > 1e-5f) {
            framePlanarVelocity = (player.cameraPosition - player.prevCameraPosition) / dt;
        }
        framePlanarVelocity.y = 0.0f;
        glm::vec3 lastKnownMoveDir = moveInput;
        if (glm::length(lastKnownMoveDir) < 0.0001f) {
            if (glm::length(framePlanarVelocity) > 0.0001f) {
                lastKnownMoveDir = glm::normalize(framePlanarVelocity);
            } else {
                lastKnownMoveDir = front;
            }
        }
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
        static bool s_proneToggleWasDown = false;
        static bool s_lockSprintChargeInAir = false;
        static glm::vec3 s_proneSlopeDownFiltered = glm::vec3(0.0f);
        static float s_proneSlopeContactTimer = 0.0f;
        const bool spacePressed = spaceDown && !s_spaceWasDown;
        const bool proneTogglePressed = keyZ && !s_proneToggleWasDown;
        s_proneToggleWasDown = keyZ;

        const bool proneEnabled = getRegistryBool(baseSystem, "ProneSlideEnabled", true);
        const float standingHalfHeight = glm::clamp(
            getRegistryFloat(baseSystem, "PlayerStandingHalfHeight", 0.90f),
            0.45f,
            1.40f
        );
        const float proneHalfHeight = glm::clamp(
            getRegistryFloat(baseSystem, "ProneHalfHeight", 0.45f),
            0.25f,
            standingHalfHeight
        );
        const float proneEnterMinSpeed = std::max(0.0f, getRegistryFloat(baseSystem, "ProneSlideEnterMinSpeed", 1.0f));
        const float proneMaxSlideSpeed = std::max(0.1f, getRegistryFloat(baseSystem, "ProneSlideMaxSpeed", sprintSpeed * 1.40f));
        const float proneEnterBoost = glm::clamp(getRegistryFloat(baseSystem, "ProneSlideEnterBoost", 1.0f), 0.1f, 3.0f);
        const float proneSlideEntryBoostSpeed = std::max(0.0f, getRegistryFloat(baseSystem, "ProneSlideEntryBoostSpeed", 1.0f));
        const bool proneSlideRequiresSprint = getRegistryBool(baseSystem, "ProneSlideRequiresSprint", true);
        const float proneSlideSprintChargeMin = glm::clamp(getRegistryFloat(baseSystem, "ProneSlideSprintChargeMin", 0.30f), 0.0f, 1.0f);
        const float proneSlideSprintEntryMinSpeed = std::max(0.0f, getRegistryFloat(baseSystem, "ProneSlideSprintEntryMinSpeed", sprintSpeed * 0.90f));
        const float proneCrawlSpeedScale = glm::clamp(getRegistryFloat(baseSystem, "ProneCrawlSpeedScale", 0.48f), 0.05f, 2.0f);
        const float proneSlideFrictionGround = glm::clamp(getRegistryFloat(baseSystem, "ProneSlideFrictionGround", 2.6f), 0.0f, 25.0f);
        const float proneSlideFrictionAir = glm::clamp(getRegistryFloat(baseSystem, "ProneSlideFrictionAir", 0.65f), 0.0f, 10.0f);
        const float proneSlideFrictionFlatScale = glm::clamp(getRegistryFloat(baseSystem, "ProneSlideFlatFrictionScale", 0.85f), 0.05f, 2.0f);
        const float proneSlideFrictionSlopeScale = glm::clamp(getRegistryFloat(baseSystem, "ProneSlideSlopeFrictionScale", 0.70f), 0.05f, 2.0f);
        const float proneSlideControlAccel = glm::clamp(getRegistryFloat(baseSystem, "ProneSlideControlAccel", 6.0f), 0.0f, 60.0f);
        const float proneSlopeAccel = glm::clamp(getRegistryFloat(baseSystem, "ProneSlideSlopeAccel", 12.0f), 0.0f, 80.0f);
        const float proneSlopeVerticalTransfer = glm::clamp(getRegistryFloat(baseSystem, "ProneSlideVerticalTransfer", 0.90f), 0.0f, 3.0f);
        const float proneRampLandingBoost = glm::clamp(getRegistryFloat(baseSystem, "ProneSlideRampLandingBoost", 2.2f), 0.0f, 5.0f);
        const float proneTransferStartImpact = std::max(0.0f, getRegistryFloat(baseSystem, "ProneSlideTransferStartImpact", 9.0f));
        const bool proneSlopeAutoStart = getRegistryBool(baseSystem, "ProneSlideSlopeAutoStart", true);
        const float proneSlopeAutoStartSpeed = std::max(0.0f, getRegistryFloat(baseSystem, "ProneSlideSlopeAutoStartSpeed", 1.4f));
        const float proneWaterCarryScale = glm::clamp(getRegistryFloat(baseSystem, "ProneSlideWaterCarryScale", 0.90f), 0.0f, 2.0f);
        const float proneWaterCarryDiveScale = glm::clamp(getRegistryFloat(baseSystem, "ProneSlideWaterCarryDiveScale", 0.40f), 0.0f, 2.0f);
        const float proneSlopeContactGraceSeconds = glm::clamp(getRegistryFloat(baseSystem, "ProneSlideSlopeContactGraceSeconds", 0.12f), 0.0f, 0.6f);
        const float proneSlopeDirectionSmoothing = glm::clamp(getRegistryFloat(baseSystem, "ProneSlideSlopeDirectionSmoothing", 18.0f), 1.0f, 80.0f);
        const float proneJumpCarryScale = glm::clamp(getRegistryFloat(baseSystem, "ProneSlideJumpCarryScale", 1.0f), 0.0f, 3.0f);
        const float proneSlideStopSpeed = std::max(0.0f, getRegistryFloat(baseSystem, "ProneSlideStopSpeed", 0.65f));
        const bool landedThisFrame = (!s_prevOnGround && player.onGround);
        const bool airborneOnLand = (!player.onGround && !playerInWater);
        const bool tookOffFromGround = airborneOnLand && s_prevOnGround;
        const float landingImpactForSlide = std::max(0.0f, player.lastLandingImpactSpeed);
        const float landingTransferImpact = std::max(0.0f, landingImpactForSlide - proneTransferStartImpact);
        auto triggerProneToggleEmotionFlash = [&](const glm::vec3& flashColor) {
            if (!baseSystem.colorEmotion) return;
            ColorEmotionContext& emotion = *baseSystem.colorEmotion;
            const float tailSeconds = std::max(0.05f, getRegistryFloat(baseSystem, "ColorEmotionProneToggleFlashTailSeconds", 1.0f));
            emotion.proneToggleFlashColor = flashColor;
            emotion.proneToggleFlashDuration = tailSeconds;
            emotion.proneToggleFlashTimer = tailSeconds;
            emotion.active = true;
        };

        if (!airborneOnLand) {
            s_lockSprintChargeInAir = false;
        } else if (tookOffFromGround && s_sprintCharge > 0.001f) {
            // Preserve takeoff sprint speed while airborne even if Shift is released.
            s_lockSprintChargeInAir = true;
        }

        if (!proneEnabled || boulderingLatched) {
            player.proneActive = false;
            player.proneSliding = false;
            player.proneSlideVelocity = glm::vec3(0.0f);
        } else if (proneTogglePressed) {
            if (!player.proneActive) {
                player.proneActive = true;
                triggerProneToggleEmotionFlash(glm::vec3(1.0f, 0.95f, 0.20f)); // yellow when prone enabled
                const float measuredEntrySpeed = std::max(player.viewBobHorizontalSpeed, glm::length(framePlanarVelocity));
                glm::vec3 entryVelocity = lastKnownMoveDir * measuredEntrySpeed;
                entryVelocity *= proneEnterBoost;
                float entrySpeed = glm::length(entryVelocity);
                const bool sprintQualified = (!proneSlideRequiresSprint)
                    || (s_sprintCharge >= proneSlideSprintChargeMin)
                    || (shiftDown && measuredEntrySpeed >= (baseSpeed * 1.05f));
                if (sprintQualified && entrySpeed > 1e-5f) {
                    entryVelocity = (entryVelocity / entrySpeed) * (entrySpeed + proneSlideEntryBoostSpeed);
                    entrySpeed += proneSlideEntryBoostSpeed;
                    const float entryFloor = std::min(proneMaxSlideSpeed, proneSlideSprintEntryMinSpeed);
                    if (entrySpeed < entryFloor) {
                        entryVelocity = (entryVelocity / entrySpeed) * entryFloor;
                        entrySpeed = entryFloor;
                    }
                }
                if (entrySpeed > proneMaxSlideSpeed) {
                    entryVelocity = (entryVelocity / std::max(1e-5f, entrySpeed)) * proneMaxSlideSpeed;
                    entrySpeed = proneMaxSlideSpeed;
                }
                if (sprintQualified && entrySpeed >= proneEnterMinSpeed) {
                    player.proneSlideVelocity = entryVelocity;
                    player.proneSliding = true;
                } else {
                    player.proneSlideVelocity = glm::vec3(0.0f);
                    player.proneSliding = false;
                }
            } else if (canStandFromProne(baseSystem, prototypes, player.cameraPosition, proneHalfHeight, standingHalfHeight)) {
                triggerProneToggleEmotionFlash(glm::vec3(0.18f, 0.52f, 1.0f)); // blue when prone disabled
                player.proneActive = false;
                player.proneSliding = false;
                player.proneSlideVelocity = glm::vec3(0.0f);
            }
        }

        const bool shouldAccelerateWalk = hasMoveInput && !playerInWater;
        if (shouldAccelerateWalk) {
            s_walkRamp = std::min(1.0f, s_walkRamp + (dt / walkAccelSeconds));
        } else {
            s_walkRamp = std::max(0.0f, s_walkRamp - (dt / walkDecelSeconds));
        }
        const float walkT = std::pow(glm::clamp(s_walkRamp, 0.0f, 1.0f), walkAccelCurveExponent);
        const float walkSpeed = walkStartSpeed + (baseSpeed - walkStartSpeed) * walkT;
        const bool walkAtMaxSpeed = walkT >= 0.999f;

        if (player.proneActive) {
            s_sprintActive = false;
            s_sprintCharge = 0.0f;
            s_bhopStacks = 0;
            s_bhopWindowRemaining = 0.0f;
        }

        if (!sprintEnabled || !shiftDown) {
            s_sprintActive = false;
        } else if (!s_sprintActive && hasMoveInput && (playerInWater || (player.onGround && walkAtMaxSpeed))) {
            // On land, sprint is gated to grounded state and full walking speed.
            // In water, allow sprinting while swimming with the same Shift hold behavior.
            s_sprintActive = true;
        }
        if (airborneOnLand && s_lockSprintChargeInAir) {
            // Hold sprint charge through the jump arc so sprint doesn't "turn off" in midair.
        } else if (s_sprintActive && shiftDown && hasMoveInput) {
            s_sprintCharge = std::min(1.0f, s_sprintCharge + (dt / sprintChargeSeconds));
        } else {
            s_sprintCharge = std::max(0.0f, s_sprintCharge - (dt / sprintReleaseSeconds));
        }
        float sprintT = std::pow(glm::clamp(s_sprintCharge, 0.0f, 1.0f), sprintCurveExponent);

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
            const bool useSprintAirScale = s_sprintActive || s_lockSprintChargeInAir;
            float minAirScale = useSprintAirScale ? airSprintSpeedScale : airWalkSpeedScale;
            s_airborneSpeedScale = std::max(minAirScale, s_airborneSpeedScale - (dt / airSpeedCarrySeconds));
            moveSpeed *= s_airborneSpeedScale;
        } else {
            s_airborneSpeedScale = 1.0f;
        }
        if (playerInWater) {
            float swimMoveScale = glm::clamp(getRegistryFloat(baseSystem, "SwimMoveSpeedScale", 0.78f), 0.1f, 2.0f);
            moveSpeed *= swimMoveScale;
        }
        const bool throwChargeIntent =
            (player.buildMode == BuildModeType::Pickup || player.buildMode == BuildModeType::PickupLeft)
            && player.isHoldingBlock
            && player.blockChargeAction == BlockChargeAction::Throw
            && player.isChargingBlock;
        if (throwChargeIntent) {
            const float throwChargeMoveScale = glm::clamp(
                getRegistryFloat(baseSystem, "HeldThrowChargeMoveScale", 0.03f),
                0.0f,
                0.35f
            );
            moveSpeed *= throwChargeMoveScale;
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

        if (player.proneActive) {
            // Prone doubles as a slide: preserve momentum, apply friction, and
            // accelerate downhill while prone on ramps.
            player.boulderLaunchVelocity = glm::vec3(0.0f);
            glm::vec3 slideVelocity = player.proneSlideVelocity;
            slideVelocity.y = 0.0f;
            if (player.proneSliding) {
                // Slide steering changes heading, but does not add energy on flat ground.
                if (hasMoveInput && glm::length(slideVelocity) > 0.001f) {
                    const float steerLerp = glm::clamp(proneSlideControlAccel * dt, 0.0f, 1.0f);
                    const glm::vec3 currentDir = glm::normalize(slideVelocity);
                    const glm::vec3 blendedDir = glm::mix(currentDir, moveInput, steerLerp);
                    if (glm::length(blendedDir) > 0.001f) {
                        slideVelocity = glm::normalize(blendedDir) * glm::length(slideVelocity);
                    }
                }

                glm::vec3 slopeDownRaw(0.0f);
                const bool onSlopeNow = player.onGround
                    && sampleSlopeDownDirection(baseSystem, prototypes, player.cameraPosition, proneHalfHeight, slopeDownRaw);
                if (onSlopeNow) {
                    if (glm::length(s_proneSlopeDownFiltered) < 0.001f) {
                        s_proneSlopeDownFiltered = slopeDownRaw;
                    } else {
                        const float blend = 1.0f - std::exp(-proneSlopeDirectionSmoothing * dt);
                        const glm::vec3 blended = glm::mix(s_proneSlopeDownFiltered, slopeDownRaw, blend);
                        if (glm::length(blended) > 0.001f) {
                            s_proneSlopeDownFiltered = glm::normalize(blended);
                        } else {
                            s_proneSlopeDownFiltered = slopeDownRaw;
                        }
                    }
                    s_proneSlopeContactTimer = proneSlopeContactGraceSeconds;
                } else {
                    s_proneSlopeContactTimer = std::max(0.0f, s_proneSlopeContactTimer - dt);
                }

                const bool onSlope = player.onGround
                    && (onSlopeNow || s_proneSlopeContactTimer > 0.0f)
                    && glm::length(s_proneSlopeDownFiltered) > 0.001f;
                const glm::vec3 slopeDown = onSlope ? s_proneSlopeDownFiltered : glm::vec3(0.0f);
                if (onSlope) {
                    if (landingTransferImpact > 0.0f) {
                        slideVelocity += slopeDown * (landingTransferImpact * proneSlopeVerticalTransfer * proneRampLandingBoost);
                    }
                    slideVelocity += slopeDown * (proneSlopeAccel * dt);
                }

                float friction = player.onGround ? proneSlideFrictionGround : proneSlideFrictionAir;
                if (player.onGround) {
                    friction *= onSlope ? proneSlideFrictionSlopeScale : proneSlideFrictionFlatScale;
                }
                if (friction > 0.0f) {
                    const float decay = std::exp(-friction * dt);
                    slideVelocity *= decay;
                }

                const float speed = glm::length(slideVelocity);
                if (speed > proneMaxSlideSpeed) {
                    slideVelocity = (slideVelocity / std::max(1e-5f, speed)) * proneMaxSlideSpeed;
                }
                if (glm::length(slideVelocity) < proneSlideStopSpeed) {
                    player.proneSliding = false;
                    slideVelocity = glm::vec3(0.0f);
                }

                player.proneSlideVelocity = slideVelocity;
                moveDelta = glm::vec3(slideVelocity.x, 0.0f, slideVelocity.z) * dt;
            } else {
                // Non-sliding prone is crawl-only: no residual glide.
                const float crawlSpeed = walkSpeed * proneCrawlSpeedScale;
                glm::vec3 landingSlopeDown(0.0f);
                const bool landedProneOnSlope = player.onGround
                    && landingTransferImpact > 0.0f
                    && sampleSlopeDownDirectionCenter(baseSystem, prototypes, player.cameraPosition, proneHalfHeight, landingSlopeDown);
                if (landedProneOnSlope) {
                    const float downslopeCarry = std::max(0.0f, glm::dot(framePlanarVelocity, landingSlopeDown));
                    float landingBoostSpeed = (landingTransferImpact * proneSlopeVerticalTransfer * proneRampLandingBoost) + downslopeCarry;
                    landingBoostSpeed = std::min(landingBoostSpeed, proneMaxSlideSpeed);
                    player.proneSlideVelocity = landingSlopeDown * landingBoostSpeed;
                    player.proneSliding = true;
                    s_proneSlopeDownFiltered = landingSlopeDown;
                    s_proneSlopeContactTimer = proneSlopeContactGraceSeconds;
                    moveDelta = glm::vec3(player.proneSlideVelocity.x, 0.0f, player.proneSlideVelocity.z) * dt;
                } else {
                    glm::vec3 autoSlopeDown(0.0f);
                    const bool canAutoSlopeSlide = proneSlopeAutoStart
                        && player.onGround
                        && sampleSlopeDownDirectionCenter(baseSystem, prototypes, player.cameraPosition, proneHalfHeight, autoSlopeDown);
                    if (canAutoSlopeSlide) {
                        float autoStartSpeed = std::max(proneSlopeAutoStartSpeed, std::max(0.0f, glm::dot(framePlanarVelocity, autoSlopeDown)));
                        autoStartSpeed = std::min(autoStartSpeed, proneMaxSlideSpeed);
                        player.proneSlideVelocity = autoSlopeDown * autoStartSpeed;
                        player.proneSliding = true;
                        s_proneSlopeDownFiltered = autoSlopeDown;
                        s_proneSlopeContactTimer = proneSlopeContactGraceSeconds;
                        moveDelta = glm::vec3(player.proneSlideVelocity.x, 0.0f, player.proneSlideVelocity.z) * dt;
                    } else {
                        player.proneSlideVelocity = glm::vec3(0.0f);
                        s_proneSlopeDownFiltered = glm::vec3(0.0f);
                        s_proneSlopeContactTimer = 0.0f;
                        moveDelta = hasMoveInput ? (moveInput * (crawlSpeed * dt)) : glm::vec3(0.0f);
                    }
                }
            }
        } else {
            player.proneSliding = false;
            player.proneSlideVelocity = glm::vec3(0.0f);
            s_proneSlopeDownFiltered = glm::vec3(0.0f);
            s_proneSlopeContactTimer = 0.0f;
        }

        // Jump on tap when grounded
        const bool boulderingLatchedNow = (player.boulderPrimaryLatched || player.boulderSecondaryLatched);
        if (!playerInWater && !boulderingLatchedNow && !consumedSpaceForVault && spacePressed && player.onGround) {
            bool canJumpNow = true;
            bool jumpedFromProne = false;
            glm::vec3 proneJumpCarryVelocity(0.0f);

            if (player.proneActive) {
                if (!canStandFromProne(baseSystem, prototypes, player.cameraPosition, proneHalfHeight, standingHalfHeight)) {
                    canJumpNow = false;
                } else {
                    jumpedFromProne = true;
                    proneJumpCarryVelocity = glm::vec3(player.proneSlideVelocity.x, 0.0f, player.proneSlideVelocity.z) * proneJumpCarryScale;
                    player.proneActive = false;
                    player.proneSliding = false;
                    player.proneSlideVelocity = glm::vec3(0.0f);
                }
            }

            if (canJumpNow) {
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
                float appliedJumpVelocity = jumpVelocity;
                const bool lilypadSuperJumpEnabled = getRegistryBool(baseSystem, "LilypadSuperJumpEnabled", true);
                if (lilypadSuperJumpEnabled && standingOnLilypad) {
                    const float gravityMagnitude = std::max(
                        0.001f,
                        std::abs(getRegistryFloat(baseSystem, "GravityStrength", -21.0f))
                    );
                    const float lilypadJumpHeightBlocks = glm::clamp(
                        getRegistryFloat(baseSystem, "LilypadSuperJumpHeightBlocks", 3.0f),
                        1.1f,
                        16.0f
                    );
                    const float lilypadJumpVelocity = std::sqrt(2.0f * gravityMagnitude * lilypadJumpHeightBlocks);
                    appliedJumpVelocity = std::max(jumpVelocity + 0.1f, lilypadJumpVelocity);
                }
                player.verticalVelocity = appliedJumpVelocity;
                player.onGround = false;
                s_lockSprintChargeInAir = true;

                if (jumpedFromProne) {
                    const float carrySpeed = glm::length(proneJumpCarryVelocity);
                    if (carrySpeed > 0.001f) {
                        player.boulderLaunchVelocity = proneJumpCarryVelocity;
                        const float carryScale = carrySpeed / std::max(0.1f, sprintSpeed);
                        s_airborneSpeedScale = std::max(s_airborneSpeedScale, carryScale);
                    }
                }
            }
        }
        s_spaceWasDown = spaceDown;
        s_prevOnGround = player.onGround;

        const bool footstepEnabled = getRegistryBool(baseSystem, "WalkFootstepEnabled", true);
        const float footstepStepDistance = glm::clamp(getRegistryFloat(baseSystem, "WalkFootstepStepDistance", 2.20f), 0.1f, 4.0f);
        const float footstepCooldown = std::max(0.0f, getRegistryFloat(baseSystem, "WalkFootstepCooldown", 0.08f));
        static float s_footstepDistance = 0.0f;
        float horizontalStepDist = glm::length(glm::vec2(moveDelta.x, moveDelta.z));
        if (footstepEnabled && player.onGround && !playerInWater && !player.proneActive && !boulderingLatchedNow && horizontalStepDist > 1e-5f) {
            s_footstepDistance += horizontalStepDist;
            if (s_footstepDistance >= footstepStepDistance) {
                triggerGameplaySfx(baseSystem, "footstep.ck", footstepCooldown);
                s_footstepDistance = std::fmod(s_footstepDistance, footstepStepDistance);
            }
        } else if (horizontalStepDist <= 1e-5f || !player.onGround || playerInWater || player.proneActive || boulderingLatchedNow) {
            s_footstepDistance = 0.0f;
        }

        player.cameraPosition += moveDelta;
    }
}
