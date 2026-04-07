#pragma once
#include "Host/PlatformInput.h"
#include "../Host.h"
#include <unordered_map>
#include <unordered_set>

namespace AudioSystemLogic { bool TriggerGameplaySfx(BaseSystem& baseSystem, const std::string& cueName, float gain); }

namespace CollisionSystemLogic {

    struct AABB { glm::vec3 min; glm::vec3 max; };

    namespace {
        float getRegistryFloat(const BaseSystem& baseSystem, const std::string& key, float fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
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

        bool isNonSolidPropName(const std::string& name) {
            // Decorative utility props should not block traversal.
            return name == "LanternBlockTex";
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

        int floorDivInt(int value, int divisor) {
            if (divisor <= 0) return 0;
            if (value >= 0) return value / divisor;
            return -(((-value) + divisor - 1) / divisor);
        }

        glm::ivec3 chunkIndexFromPosition(const glm::vec3& position, const glm::ivec3& chunkSize) {
            int x = static_cast<int>(std::floor(position.x));
            int y = static_cast<int>(std::floor(position.y));
            int z = static_cast<int>(std::floor(position.z));
            return glm::ivec3(
                floorDivInt(x, chunkSize.x),
                floorDivInt(y, chunkSize.y),
               floorDivInt(z, chunkSize.z)
            );
        }

        enum class SlopeDir : int { None = 0, PosX = 1, NegX = 2, PosZ = 3, NegZ = 4 };

        SlopeDir slopeDirFromName(const std::string& name) {
            if (name == "DebugSlopeTexPosX") return SlopeDir::PosX;
            if (name == "DebugSlopeTexNegX") return SlopeDir::NegX;
            if (name == "DebugSlopeTexPosZ") return SlopeDir::PosZ;
            if (name == "DebugSlopeTexNegZ") return SlopeDir::NegZ;
            return SlopeDir::None;
        }

        bool isLilypadWalkableName(const std::string& name) {
            return name == "StonePebbleLilypadTexX"
                || name == "StonePebbleLilypadTexZ"
                || name.rfind("GrassCoverBigLilypad", 0) == 0;
        }

        bool isLilypadPrototypeID(const std::vector<Entity>& prototypes, uint32_t blockID) {
            if (blockID == 0 || blockID >= prototypes.size()) return false;
            return isLilypadWalkableName(prototypes[blockID].name);
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

        constexpr uint8_t kWaterFoliageMarkerSeaUrchinZ = 3u;
        constexpr uint8_t kWaterWaveClassUnknown = 0u;
        constexpr uint8_t kWaterWaveClassPond = 1u;
        constexpr uint8_t kWaterWaveClassLake = 2u;
        constexpr uint8_t kWaterWaveClassRiver = 3u;
        constexpr uint8_t kWaterWaveClassOcean = 4u;

        uint8_t decodeWaterWaveClass(uint32_t packedColor) {
            const uint8_t encoded = static_cast<uint8_t>((packedColor >> 24) & 0xffu);
            if (encoded <= kWaterFoliageMarkerSeaUrchinZ) {
                return kWaterWaveClassUnknown;
            }
            const uint8_t marker = static_cast<uint8_t>(encoded & 0x0fu);
            const uint8_t waveClass = static_cast<uint8_t>((encoded >> 4u) & 0x0fu);
            if (marker <= kWaterFoliageMarkerSeaUrchinZ && waveClass <= kWaterWaveClassOcean) {
                return waveClass;
            }
            return kWaterWaveClassUnknown;
        }

        uint8_t estimateWaveClassFromWaterNeighborhood(const BaseSystem& baseSystem,
                                                       int waterPrototypeID,
                                                       const glm::ivec3& centerCell) {
            if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled || waterPrototypeID < 0) {
                return kWaterWaveClassUnknown;
            }
            const VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;

            static const glm::ivec3 kProbeOffsets[] = {
                glm::ivec3(0, -1, 0),
                glm::ivec3(0, 0, 0),
                glm::ivec3(0, 1, 0),
                glm::ivec3(1, 0, 0),
                glm::ivec3(-1, 0, 0),
                glm::ivec3(0, 0, 1),
                glm::ivec3(0, 0, -1)
            };
            for (const glm::ivec3& offset : kProbeOffsets) {
                const glm::ivec3 probe = centerCell + offset;
                if (voxelWorld.getBlockWorld(probe) != static_cast<uint32_t>(waterPrototypeID)) continue;
                const uint8_t waveClass = decodeWaterWaveClass(voxelWorld.getColorWorld(probe));
                if (waveClass >= kWaterWaveClassPond && waveClass <= kWaterWaveClassOcean) {
                    return waveClass;
                }
            }

            auto spanAlong = [&](const glm::ivec3& axisStep) {
                constexpr int kMaxProbe = 40;
                int span = 1;
                const glm::ivec3 sampleCenter = centerCell + glm::ivec3(0, -1, 0);
                for (int s = 1; s <= kMaxProbe; ++s) {
                    if (voxelWorld.getBlockWorld(sampleCenter + axisStep * s) != static_cast<uint32_t>(waterPrototypeID)) break;
                    span += 1;
                }
                for (int s = 1; s <= kMaxProbe; ++s) {
                    if (voxelWorld.getBlockWorld(sampleCenter - axisStep * s) != static_cast<uint32_t>(waterPrototypeID)) break;
                    span += 1;
                }
                return span;
            };
            const int spanX = spanAlong(glm::ivec3(1, 0, 0));
            const int spanZ = spanAlong(glm::ivec3(0, 0, 1));
            const int minSpan = std::min(spanX, spanZ);
            const int maxSpan = std::max(spanX, spanZ);
            if (maxSpan <= 12 && minSpan <= 8) return kWaterWaveClassPond;
            if (minSpan <= 10 && maxSpan >= 20) return kWaterWaveClassRiver;
            if (minSpan >= 38 && maxSpan >= 38) return kWaterWaveClassOcean;
            return kWaterWaveClassLake;
        }

        float computeLilypadWaveOffsetY(const BaseSystem& baseSystem,
                                        uint8_t waveClass,
                                        const glm::vec3& center,
                                        float timeSeconds) {
            if (waveClass < kWaterWaveClassPond || waveClass > kWaterWaveClassOcean) {
                return 0.0f;
            }

            float ampScale = 2.10f;
            float wavelength = 36.0f;
            float steepness = 0.46f;
            float speedScale = 1.46f;
            glm::vec2 dirA = glm::normalize(glm::vec2(0.86f, 0.51f));
            glm::vec2 dirB = glm::normalize(glm::vec2(-0.34f, 0.94f));

            if (waveClass == kWaterWaveClassPond) {
                ampScale = 2.10f;
                wavelength = 36.0f;
                steepness = 0.46f;
                speedScale = 1.46f;
                dirA = glm::normalize(glm::vec2(0.74f, 0.67f));
                dirB = glm::normalize(glm::vec2(-0.62f, 0.78f));
            } else if (waveClass == kWaterWaveClassLake) {
                ampScale = 2.90f;
                wavelength = 44.0f;
                steepness = 0.55f;
                speedScale = 1.70f;
                dirA = glm::normalize(glm::vec2(0.88f, 0.47f));
                dirB = glm::normalize(glm::vec2(-0.29f, 0.96f));
            } else if (waveClass == kWaterWaveClassRiver) {
                ampScale = 3.80f;
                wavelength = 52.0f;
                steepness = 0.64f;
                speedScale = 1.95f;
                dirA = glm::normalize(glm::vec2(0.99f, 0.14f));
                dirB = glm::normalize(glm::vec2(0.93f, 0.37f));
            } else if (waveClass == kWaterWaveClassOcean) {
                ampScale = 5.00f;
                wavelength = 64.0f;
                steepness = 0.74f;
                speedScale = 2.20f;
                dirA = glm::normalize(glm::vec2(0.95f, 0.31f));
                dirB = glm::normalize(glm::vec2(-0.42f, 0.91f));
            }

            const float globalStrength = glm::clamp(getRegistryFloat(baseSystem, "WaterCascadeBrightnessStrength", 0.22f), 0.0f, 1.0f);
            const float globalScale = glm::clamp(getRegistryFloat(baseSystem, "WaterCascadeBrightnessScale", 0.18f) * 5.0f, 1.0f, 8.0f);
            const float waveSpeed = std::max(0.25f, getRegistryFloat(baseSystem, "WaterCascadeBrightnessSpeed", 1.1f)) * speedScale;

            const float ampBase = glm::mix(0.035f, 0.110f, globalStrength) * ampScale;
            const float ampA = ampBase * 0.65f;
            const float ampB = ampBase * 0.35f;
            const float lenA = std::max(6.0f, wavelength / globalScale);
            const float lenB = std::max(5.0f, (wavelength * 0.62f) / globalScale);
            const float kA = 6.28318530718f / lenA;
            const float kB = 6.28318530718f / lenB;
            const float qA = glm::clamp(steepness, 0.0f, 0.85f);
            const float qB = glm::clamp(steepness * 0.75f, 0.0f, 0.85f);

            const glm::vec2 xz(center.x, center.z);
            const float phaseA = glm::dot(dirA, xz) * kA + timeSeconds * waveSpeed;
            const float phaseB = glm::dot(dirB, xz) * kB - timeSeconds * waveSpeed * 0.83f;
            const float sA = std::sin(phaseA);
            const float sB = std::sin(phaseB);
            (void)qA;
            (void)qB;
            return ampA * sA + ampB * sB;
        }

        glm::ivec3 slopeHighDir(SlopeDir dir) {
            switch (dir) {
                case SlopeDir::PosX: return glm::ivec3(1, 0, 0);
                case SlopeDir::NegX: return glm::ivec3(-1, 0, 0);
                case SlopeDir::PosZ: return glm::ivec3(0, 0, 1);
                case SlopeDir::NegZ: return glm::ivec3(0, 0, -1);
                default: return glm::ivec3(0);
            }
        }

        struct IVec3Hash {
            std::size_t operator()(const glm::ivec3& v) const noexcept {
                std::size_t hx = std::hash<int>()(v.x);
                std::size_t hy = std::hash<int>()(v.y);
                std::size_t hz = std::hash<int>()(v.z);
                return hx ^ (hy << 1) ^ (hz << 2);
            }
        };

        bool IntersectsAnyBlock(const glm::vec3& position,
                                const glm::vec3& halfExtents,
                                const std::vector<AABB>& blocks) {
            AABB playerBox = {position - halfExtents, position + halfExtents};
            for (const auto& block : blocks) {
                bool overlap = (playerBox.min.x < block.max.x && playerBox.max.x > block.min.x)
                            && (playerBox.min.y < block.max.y && playerBox.max.y > block.min.y)
                            && (playerBox.min.z < block.max.z && playerBox.max.z > block.min.z);
                if (overlap) return true;
            }
            return false;
        }
    }

    AABB MakePlayerAABB(const glm::vec3& center, const glm::vec3& halfExtents) {
        return {center - halfExtents, center + halfExtents};
    }

    bool Intersects(const AABB& a, const AABB& b) {
        return (a.min.x < b.max.x && a.max.x > b.min.x) &&
               (a.min.y < b.max.y && a.max.y > b.min.y) &&
               (a.min.z < b.max.z && a.max.z > b.min.z);
    }

    void ResolveAxis(glm::vec3& position, int axis, float halfExtent, float velAxis, const std::vector<AABB>& blocks, const glm::vec3& halfExtents) {
        if (velAxis == 0.0f) return;
        position[axis] += velAxis;
        AABB playerBox = MakePlayerAABB(position, halfExtents);
        for (const auto& block : blocks) {
            if (!Intersects(playerBox, block)) continue;
            const float skin = 0.001f;
            if (velAxis > 0.0f) {
                position[axis] = block.min[axis] - halfExtent - skin;
            } else {
                position[axis] = block.max[axis] + halfExtent + skin;
            }
            playerBox = MakePlayerAABB(position, halfExtents);
        }
    }

    void ResolveCollisions(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)win;
        if (!baseSystem.player || !baseSystem.level || baseSystem.level->worlds.empty()) return;
        if (baseSystem.gamemode == "spectator") return;
        if (baseSystem.gamemode == "survival") {
            bool spawnReady = false;
            if (baseSystem.registry) {
                auto it = baseSystem.registry->find("spawn_ready");
                if (it != baseSystem.registry->end() &&
                    std::holds_alternative<bool>(it->second)) {
                    spawnReady = std::get<bool>(it->second);
                }
            }
            if (!spawnReady) {
                baseSystem.player->prevCameraPosition = baseSystem.player->cameraPosition;
                baseSystem.player->viewBobHorizontalSpeed = 0.0f;
                return;
            }
        }

        PlayerContext& player = *baseSystem.player;
        glm::vec3 prevPos = player.prevCameraPosition;
        glm::vec3 desiredPos = player.cameraPosition;
        glm::vec3 velocity = desiredPos - prevPos;
        const bool wasGrounded = player.onGround;
        const float preResolveVerticalVelocity = player.verticalVelocity;
        const int waterPrototypeID = resolveWaterPrototypeID(prototypes);
        bool playerInWater = false;
        if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
            if (waterPrototypeID >= 0) {
                playerInWater =
                    isPlayerInWater(*baseSystem.voxelWorld, prototypes, waterPrototypeID, desiredPos)
                    || isPlayerInWater(*baseSystem.voxelWorld, prototypes, waterPrototypeID, prevPos);
            }
        }
        if (playerInWater && isPlayerStandingOnLilypad(baseSystem, prototypes, desiredPos)) {
            playerInWater = false;
        }

        // Early out if no movement
        if (glm::dot(velocity, velocity) < 1e-8f && !wasGrounded) {
            player.prevCameraPosition = player.cameraPosition;
            player.viewBobHorizontalSpeed = 0.0f;
            return;
        }

        // Player collision capsule: standing is 1.8 tall; prone is shorter for crawl/slide.
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
        const float activeHalfHeight = player.proneActive ? proneHalfHeight : standingHalfHeight;
        glm::vec3 halfExtents(0.25f, activeHalfHeight, 0.25f);

        // Gather collidable blocks (solid blocks only) across all worlds in the level.
        std::vector<AABB> blockAABBs;
        struct SlopeCollider {
            glm::vec3 center = glm::vec3(0.0f);
            SlopeDir dir = SlopeDir::None;
        };
        struct OneWayPlatform {
            float minX = 0.0f;
            float maxX = 0.0f;
            float minZ = 0.0f;
            float maxZ = 0.0f;
            float topY = 0.0f;
        };
        std::vector<SlopeCollider> slopeColliders;
        std::vector<OneWayPlatform> oneWayPlatforms;
        oneWayPlatforms.reserve(64);
        const float waveTimeSeconds = static_cast<float>(PlatformInput::GetTimeSeconds());
        bool useVoxel = baseSystem.voxelWorld && baseSystem.voxelWorld->enabled;
        if (useVoxel) {
            glm::vec3 sweepCenter = (prevPos + desiredPos) * 0.5f;
            glm::ivec3 center = glm::ivec3(glm::floor(sweepCenter));
            int radiusXZ = 2 + static_cast<int>(std::ceil(std::max(std::abs(velocity.x), std::abs(velocity.z))));
            int radiusY = 2 + static_cast<int>(std::ceil(std::abs(velocity.y)));
            radiusXZ = std::max(2, std::min(radiusXZ, 32));
            radiusY = std::max(2, std::min(radiusY, 256));
            for (int x = center.x - radiusXZ; x <= center.x + radiusXZ; ++x) {
                for (int y = center.y - radiusY; y <= center.y + radiusY; ++y) {
                    for (int z = center.z - radiusXZ; z <= center.z + radiusXZ; ++z) {
                        glm::ivec3 cell(x, y, z);
                        uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
                        if (id == 0) continue;
                        int protoID = static_cast<int>(id);
                        if (protoID < 0 || protoID >= static_cast<int>(prototypes.size())) continue;
                        const Entity& proto = prototypes[protoID];
                        SlopeDir slopeDir = slopeDirFromName(proto.name);
                        if (slopeDir != SlopeDir::None) {
                            slopeColliders.push_back({glm::vec3(cell), slopeDir});
                            continue;
                        }
                        if (isLilypadWalkableName(proto.name)) {
                            const glm::vec3 pos = glm::vec3(cell);
                            constexpr float kLilypadThickness = 1.0f / 12.0f;
                            const uint8_t waveClass = estimateWaveClassFromWaterNeighborhood(baseSystem, waterPrototypeID, cell);
                            const float waveOffsetY = computeLilypadWaveOffsetY(baseSystem, waveClass, pos, waveTimeSeconds);
                            const float minY = pos.y - 0.5f + waveOffsetY;
                            oneWayPlatforms.push_back({
                                pos.x - 0.5f,
                                pos.x + 0.5f,
                                pos.z - 0.5f,
                                pos.z + 0.5f,
                                minY + kLilypadThickness
                            });
                            continue;
                        }
                        bool isNonColliding = isWaterLikeName(proto.name)
                            || isNonSolidPropName(proto.name)
                            || proto.name == "AudioVisualizer";
                        if (!proto.isBlock || isNonColliding || !proto.isSolid) continue;
                        glm::vec3 pos = glm::vec3(cell);
                        blockAABBs.push_back({pos - glm::vec3(0.5f), pos + glm::vec3(0.5f)});
                    }
                }
            }
        } else {
            for (const auto& world : baseSystem.level->worlds) {
                for (const auto& inst : world.instances) {
                    if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                    const Entity& proto = prototypes[inst.prototypeID];
                    SlopeDir slopeDir = slopeDirFromName(proto.name);
                    if (slopeDir != SlopeDir::None) {
                        slopeColliders.push_back({inst.position, slopeDir});
                        continue;
                    }
                    if (isLilypadWalkableName(proto.name)) {
                        const glm::vec3 pos = inst.position;
                        constexpr float kLilypadThickness = 1.0f / 12.0f;
                        const glm::ivec3 cell = glm::ivec3(glm::round(pos));
                        const uint8_t waveClass = estimateWaveClassFromWaterNeighborhood(baseSystem, waterPrototypeID, cell);
                        const float waveOffsetY = computeLilypadWaveOffsetY(baseSystem, waveClass, pos, waveTimeSeconds);
                        const float minY = pos.y - 0.5f + waveOffsetY;
                        oneWayPlatforms.push_back({
                            pos.x - 0.5f,
                            pos.x + 0.5f,
                            pos.z - 0.5f,
                            pos.z + 0.5f,
                            minY + kLilypadThickness
                        });
                        continue;
                    }
                    bool isNonColliding = isWaterLikeName(proto.name)
                        || isNonSolidPropName(proto.name)
                        || proto.name == "AudioVisualizer";
                    if (!proto.isBlock || isNonColliding || !proto.isSolid) continue;
                    blockAABBs.push_back({inst.position - glm::vec3(0.5f), inst.position + glm::vec3(0.5f)});
                }
            }
        }
        // Fishing rod remains targetable/interactive, but should not block movement.
        // Placed hatchets are surface props (like sticks/pebbles), so they should be targetable
        // but not block player movement.
        // Gems remain interactable/targetable, but no longer contribute collision
        // against player movement.

        bool usedAutoStep = false;
        bool nearSlopeForAutoStep = false;
        if (!slopeColliders.empty()) {
            const float feetYPrev = prevPos.y - halfExtents.y;
            for (const auto& slope : slopeColliders) {
                if (std::abs(prevPos.x - slope.center.x) > 0.60f) continue;
                if (std::abs(prevPos.z - slope.center.z) > 0.60f) continue;
                const float blockBottomY = slope.center.y - 0.5f;
                const float blockTopY = slope.center.y + 0.5f;
                if (feetYPrev < blockBottomY - 0.25f) continue;
                if (feetYPrev > blockTopY + 1.25f) continue;
                nearSlopeForAutoStep = true;
                break;
            }
        }
        const bool slopeHorizontalAssistEnabled = getRegistryBool(baseSystem, "SlopeHorizontalAssistEnabled", true);
        const float slopeHorizRadiusScale = glm::clamp(getRegistryFloat(baseSystem, "SlopeHorizontalCollisionRadiusScale", 0.90f), 0.70f, 1.0f);
        const float slopeHorizIgnoreBelowTop = glm::clamp(getRegistryFloat(baseSystem, "SlopeHorizontalIgnoreBelowTopDelta", 0.30f), 0.0f, 1.5f);
        std::vector<AABB> horizontalBlocks;
        horizontalBlocks.reserve(blockAABBs.size());
        glm::vec3 moveHalfExtents = halfExtents;
        const bool useSlopeHorizontalAssist =
            slopeHorizontalAssistEnabled
            && nearSlopeForAutoStep
            && wasGrounded
            && velocity.y <= 0.001f;
        if (useSlopeHorizontalAssist) {
            const float feetYPrev = prevPos.y - halfExtents.y;
            const float topThreshold = feetYPrev + slopeHorizIgnoreBelowTop;
            for (const auto& block : blockAABBs) {
                // Ignore low floor-support side faces near slopes; keep real walls.
                if (block.max.y <= topThreshold) continue;
                horizontalBlocks.push_back(block);
            }
            moveHalfExtents.x *= slopeHorizRadiusScale;
            moveHalfExtents.z *= slopeHorizRadiusScale;
        } else {
            horizontalBlocks = blockAABBs;
        }

        glm::vec3 resolvedPos = prevPos;
        ResolveAxis(resolvedPos, 0, moveHalfExtents.x, velocity.x, horizontalBlocks, moveHalfExtents);
        ResolveAxis(resolvedPos, 1, halfExtents.y, velocity.y, blockAABBs, halfExtents);
        ResolveAxis(resolvedPos, 2, moveHalfExtents.z, velocity.z, horizontalBlocks, moveHalfExtents);

        const bool autoStepEnabled = getRegistryBool(baseSystem, "AutoStepEnabled", true);
        const float autoStepHeight = glm::clamp(getRegistryFloat(baseSystem, "AutoStepHeight", 1.0f), 0.1f, 1.5f);
        const bool autoStepNearSlopesEnabled = getRegistryBool(baseSystem, "AutoStepNearSlopesEnabled", true);
        const float autoStepNearSlopesHeight = glm::clamp(
            getRegistryFloat(baseSystem, "AutoStepNearSlopesHeight", 0.65f),
            0.1f,
            1.5f
        );
        const float horizontalRequested = glm::length(glm::vec2(velocity.x, velocity.z));
        const float horizontalResolved = glm::length(glm::vec2(resolvedPos.x - prevPos.x, resolvedPos.z - prevPos.z));
        const bool horizontallyBlocked = horizontalRequested > 1e-4f && (horizontalResolved + 1e-4f) < horizontalRequested;
        const bool canAutoStepHere = !nearSlopeForAutoStep || autoStepNearSlopesEnabled;
        if (autoStepEnabled && canAutoStepHere && wasGrounded && velocity.y <= 0.001f && horizontallyBlocked) {
            const float stepHeight = nearSlopeForAutoStep
                ? std::min(autoStepHeight, autoStepNearSlopesHeight)
                : autoStepHeight;
            glm::vec3 stepPos = prevPos;
            stepPos.y += stepHeight;

            // Can't step into occupied headroom.
            if (!IntersectsAnyBlock(stepPos, halfExtents, blockAABBs)) {
                ResolveAxis(stepPos, 0, halfExtents.x, velocity.x, horizontalBlocks, halfExtents);
                ResolveAxis(stepPos, 2, halfExtents.z, velocity.z, horizontalBlocks, halfExtents);
                ResolveAxis(stepPos, 1, halfExtents.y, -(stepHeight + 0.02f), blockAABBs, halfExtents);

                float steppedHorizontal = glm::length(glm::vec2(stepPos.x - prevPos.x, stepPos.z - prevPos.z));
                float lift = stepPos.y - prevPos.y;
                bool validLift = lift >= -0.05f && lift <= (stepHeight + 0.05f);
                if (validLift && steppedHorizontal > (horizontalResolved + 0.02f)) {
                    resolvedPos = stepPos;
                    usedAutoStep = true;
                }
            }
        }

        const bool swimWallClimbEnabled = getRegistryBool(baseSystem, "SwimWallClimbEnabled", true);
        if (swimWallClimbEnabled && playerInWater && dt > 0.0f) {
            const float swimWallClimbAssistGrace = glm::clamp(
                getRegistryFloat(baseSystem, "SwimWallClimbAssistGrace", 0.18f),
                0.0f,
                1.5f
            );
            const float horizontalResolvedNow = glm::length(glm::vec2(resolvedPos.x - prevPos.x, resolvedPos.z - prevPos.z));
            const bool stillBlocked = horizontalRequested > 1e-4f && (horizontalResolvedNow + 1e-4f) < horizontalRequested;
            if (stillBlocked) {
                const float climbSpeed = glm::clamp(getRegistryFloat(baseSystem, "SwimWallClimbSpeed", 2.2f), 0.0f, 30.0f);
                const float maxRisePerFrame = glm::clamp(getRegistryFloat(baseSystem, "SwimWallClimbMaxRisePerFrame", 0.08f), 0.0f, 1.0f);
                const float riseAmount = std::min(maxRisePerFrame, climbSpeed * dt);
                if (riseAmount > 0.0f) {
                    glm::vec3 climbPos = resolvedPos;
                    climbPos.y += riseAmount;
                    if (!IntersectsAnyBlock(climbPos, halfExtents, blockAABBs)) {
                        resolvedPos = climbPos;
                        player.swimWallClimbAssistTimer = std::max(player.swimWallClimbAssistTimer, swimWallClimbAssistGrace);
                        const float riseVelocity = riseAmount / std::max(1e-5f, dt);
                        if (player.verticalVelocity < riseVelocity) {
                            player.verticalVelocity = riseVelocity;
                        }
                    }
                }
            }
        }

        const glm::vec3 preGroundingPos = resolvedPos;
        const float preGroundingY = resolvedPos.y;

        // Extra sweep for downward motion to catch thin crossings
        bool hitGround = false;
        if (velocity.y < 0.0f) {
            const float skin = 0.001f;
            float highestY = -std::numeric_limits<float>::infinity();
            for (const auto& block : blockAABBs) {
                // Horizontal overlap
                bool overlapX = !(resolvedPos.x + halfExtents.x < block.min.x || resolvedPos.x - halfExtents.x > block.max.x);
                bool overlapZ = !(resolvedPos.z + halfExtents.z < block.min.z || resolvedPos.z - halfExtents.z > block.max.z);
                if (!overlapX || !overlapZ) continue;
                // Crossing top face?
                float bottomBefore = prevPos.y - halfExtents.y;
                float bottomAfter = resolvedPos.y - halfExtents.y;
                if (bottomBefore >= block.max.y - skin && bottomAfter <= block.max.y + skin) {
                    if (block.max.y > highestY) highestY = block.max.y;
                }
            }
            if (highestY != -std::numeric_limits<float>::infinity()) {
                resolvedPos.y = highestY + halfExtents.y + skin;
                hitGround = true;
            }
        }

        if (!slopeColliders.empty()) {
            const float skin = 0.001f;
            const float slopeSnapDown = glm::clamp(getRegistryFloat(baseSystem, "SlopeSnapDown", 0.45f), 0.0f, 2.0f);
            const float slopeSnapUp = glm::clamp(getRegistryFloat(baseSystem, "SlopeSnapUp", 1.25f), 0.1f, 3.0f);
            const float slopeSampleMargin = glm::clamp(getRegistryFloat(baseSystem, "SlopeSampleMargin", 0.14f), 0.0f, 0.35f);
            const float slopeSampleInset = glm::clamp(getRegistryFloat(baseSystem, "SlopeSampleInset", 0.08f), 0.0f, 0.20f);
            const float slopeSeamExtraSnapDown = glm::clamp(getRegistryFloat(baseSystem, "SlopeSeamExtraSnapDown", 0.20f), 0.0f, 0.75f);
            const float slopeAscendingUnsnapVelocity = glm::clamp(getRegistryFloat(baseSystem, "SlopeAscendingUnsnapVelocity", 0.05f), 0.0f, 20.0f);
            const float effectiveSnapDown = glm::min(2.0f, slopeSnapDown + (wasGrounded ? slopeSeamExtraSnapDown : 0.0f));
            const bool slopeHighWallCollision = false;
            const bool skipSlopeSnapAscending = velocity.y > slopeAscendingUnsnapVelocity;
            bool hasSlopeSnap = false;
            bool hasUpSnap = false;
            float bestSlopeSurfaceY = 0.0f;
            float bestUpDelta = std::numeric_limits<float>::infinity();
            float bestDownDelta = -std::numeric_limits<float>::infinity();
            float bestUpDist2 = std::numeric_limits<float>::infinity();
            float bestDownDist2 = std::numeric_limits<float>::infinity();
            const float slopeSampleOffsetX = std::max(0.0f, halfExtents.x - slopeSampleInset);
            const float slopeSampleOffsetZ = std::max(0.0f, halfExtents.z - slopeSampleInset);
            const glm::vec2 slopeSampleOffsets[5] = {
                glm::vec2(0.0f, 0.0f),
                glm::vec2(slopeSampleOffsetX, 0.0f),
                glm::vec2(-slopeSampleOffsetX, 0.0f),
                glm::vec2(0.0f, slopeSampleOffsetZ),
                glm::vec2(0.0f, -slopeSampleOffsetZ)
            };
            std::unordered_set<glm::ivec3, IVec3Hash> slopeCells;
            slopeCells.reserve(slopeColliders.size() * 2);
            for (const auto& slope : slopeColliders) {
                slopeCells.insert(glm::ivec3(
                    static_cast<int>(std::floor(slope.center.x + 0.5f)),
                    static_cast<int>(std::floor(slope.center.y + 0.5f)),
                    static_cast<int>(std::floor(slope.center.z + 0.5f))));
            }

            for (const auto& slope : slopeColliders) {
                if (skipSlopeSnapAscending) continue;
                const float blockBottomY = slope.center.y - 0.5f;
                const float blockTopY = slope.center.y + 0.5f;
                float feetY = resolvedPos.y - halfExtents.y;
                if (resolvedPos.y + halfExtents.y < blockBottomY - 0.05f) continue;
                if (feetY > blockTopY + 1.0f) continue;
                const float dx = resolvedPos.x - slope.center.x;
                const float dz = resolvedPos.z - slope.center.z;
                const glm::ivec3 slopeCell(
                    static_cast<int>(std::floor(slope.center.x + 0.5f)),
                    static_cast<int>(std::floor(slope.center.y + 0.5f)),
                    static_cast<int>(std::floor(slope.center.z + 0.5f)));
                const glm::ivec3 highDir = slopeHighDir(slope.dir);
                const bool hasUphillNeighbor =
                    slopeCells.count(slopeCell + highDir + glm::ivec3(0, 1, 0)) > 0 ||
                    slopeCells.count(slopeCell + highDir) > 0;

                // High-side face acts as a blocking wall.
                if (slopeHighWallCollision
                    && !hasUphillNeighbor
                    && resolvedPos.y + halfExtents.y > blockBottomY + skin
                    && feetY < blockTopY + skin) {
                    switch (slope.dir) {
                        case SlopeDir::PosX: {
                            if (std::abs(dz) > 0.5f) break;
                            if (dx < 0.25f) break;
                            float wallX = slope.center.x + 0.5f;
                            if (resolvedPos.x + halfExtents.x > wallX - skin) {
                                resolvedPos.x = wallX - halfExtents.x - skin;
                            }
                            break;
                        }
                        case SlopeDir::NegX: {
                            if (std::abs(dz) > 0.5f) break;
                            if (dx > -0.25f) break;
                            float wallX = slope.center.x - 0.5f;
                            if (resolvedPos.x - halfExtents.x < wallX + skin) {
                                resolvedPos.x = wallX + halfExtents.x + skin;
                            }
                            break;
                        }
                        case SlopeDir::PosZ: {
                            if (std::abs(dx) > 0.5f) break;
                            if (dz < 0.25f) break;
                            float wallZ = slope.center.z + 0.5f;
                            if (resolvedPos.z + halfExtents.z > wallZ - skin) {
                                resolvedPos.z = wallZ - halfExtents.z - skin;
                            }
                            break;
                        }
                        case SlopeDir::NegZ: {
                            if (std::abs(dx) > 0.5f) break;
                            if (dz > -0.25f) break;
                            float wallZ = slope.center.z - 0.5f;
                            if (resolvedPos.z - halfExtents.z < wallZ + skin) {
                                resolvedPos.z = wallZ + halfExtents.z + skin;
                            }
                            break;
                        }
                        default:
                            break;
                    }
                }

                // Sample multiple support points around the player's feet to avoid
                // center-only slope snapping drops and side-entry jitter.
                for (const glm::vec2& offset : slopeSampleOffsets) {
                    const float sampleDxRaw = (resolvedPos.x + offset.x) - slope.center.x;
                    const float sampleDzRaw = (resolvedPos.z + offset.y) - slope.center.z;
                    if (std::abs(sampleDxRaw) > (0.5f + slopeSampleMargin)) continue;
                    if (std::abs(sampleDzRaw) > (0.5f + slopeSampleMargin)) continue;

                    float t = 0.0f;
                    switch (slope.dir) {
                        case SlopeDir::PosX: t = sampleDxRaw + 0.5f; break;
                        case SlopeDir::NegX: t = 0.5f - sampleDxRaw; break;
                        case SlopeDir::PosZ: t = sampleDzRaw + 0.5f; break;
                        case SlopeDir::NegZ: t = 0.5f - sampleDzRaw; break;
                        default: t = -1.0f; break;
                    }
                    if (t < -0.05f || t > 1.05f) continue;
                    t = glm::clamp(t, 0.0f, 1.0f);
                    const float sampleDx = glm::clamp(sampleDxRaw, -0.5f, 0.5f);
                    const float sampleDz = glm::clamp(sampleDzRaw, -0.5f, 0.5f);
                    const float surfaceY = blockBottomY + t;
                    feetY = resolvedPos.y - halfExtents.y;
                    const float delta = surfaceY - feetY;
                    const float lateralDist2 = sampleDx * sampleDx + sampleDz * sampleDz;
                    if (delta < -effectiveSnapDown || delta > slopeSnapUp) continue;
                    if (delta >= 0.0f) {
                        if (!hasUpSnap
                            || surfaceY > (bestSlopeSurfaceY + 1e-4f)
                            || (std::abs(surfaceY - bestSlopeSurfaceY) <= 1e-4f && lateralDist2 < bestUpDist2)) {
                            hasUpSnap = true;
                            hasSlopeSnap = true;
                            bestUpDelta = delta;
                            bestUpDist2 = lateralDist2;
                            bestSlopeSurfaceY = surfaceY;
                        }
                    } else if (!hasUpSnap) {
                        if (!hasSlopeSnap
                            || delta > (bestDownDelta + 1e-4f)
                            || (std::abs(delta - bestDownDelta) <= 1e-4f && lateralDist2 < bestDownDist2)) {
                            hasSlopeSnap = true;
                            bestDownDelta = delta;
                            bestDownDist2 = lateralDist2;
                            bestSlopeSurfaceY = surfaceY;
                        }
                    }
                }
            }

            if (!skipSlopeSnapAscending && !hasSlopeSnap && wasGrounded && velocity.y <= 0.001f) {
                // Seam fallback: if we just crossed a slope edge, allow a second, wider snap query.
                float seamBestSurfaceY = -std::numeric_limits<float>::infinity();
                bool seamFound = false;
                const float seamMargin = slopeSampleMargin + 0.18f;
                const float seamDown = effectiveSnapDown + 0.25f;
                for (const auto& slope : slopeColliders) {
                    const float blockBottomY = slope.center.y - 0.5f;
                    const float blockTopY = slope.center.y + 0.5f;
                    float feetY = resolvedPos.y - halfExtents.y;
                    if (resolvedPos.y + halfExtents.y < blockBottomY - 0.05f) continue;
                    if (feetY > blockTopY + 1.0f) continue;

                    for (const glm::vec2& offset : slopeSampleOffsets) {
                        const float dxRaw = (resolvedPos.x + offset.x) - slope.center.x;
                        const float dzRaw = (resolvedPos.z + offset.y) - slope.center.z;
                        if (std::abs(dxRaw) > (0.5f + seamMargin)) continue;
                        if (std::abs(dzRaw) > (0.5f + seamMargin)) continue;

                        float t = 0.0f;
                        switch (slope.dir) {
                            case SlopeDir::PosX: t = dxRaw + 0.5f; break;
                            case SlopeDir::NegX: t = 0.5f - dxRaw; break;
                            case SlopeDir::PosZ: t = dzRaw + 0.5f; break;
                            case SlopeDir::NegZ: t = 0.5f - dzRaw; break;
                            default: t = -1.0f; break;
                        }
                        if (t < -0.05f || t > 1.05f) continue;
                        t = glm::clamp(t, 0.0f, 1.0f);
                        const float surfaceY = blockBottomY + t;
                        const float delta = surfaceY - feetY;
                        if (delta < -seamDown || delta > slopeSnapUp) continue;
                        if (!seamFound || surfaceY > seamBestSurfaceY) {
                            seamBestSurfaceY = surfaceY;
                            seamFound = true;
                        }
                    }
                }
                if (seamFound) {
                    hasSlopeSnap = true;
                    bestSlopeSurfaceY = seamBestSurfaceY;
                }
            }

            if (hasSlopeSnap) {
                glm::vec3 snappedPos = resolvedPos;
                snappedPos.y = bestSlopeSurfaceY + halfExtents.y + skin;
                // Prevent slope snap from moving us into solid geometry, which can
                // cause frame-to-frame pushback teleporting.
                if (!IntersectsAnyBlock(snappedPos, halfExtents, blockAABBs)) {
                    resolvedPos = snappedPos;
                    hitGround = true;
                }
            }
        }

        if (!oneWayPlatforms.empty()) {
            const float skin = 0.001f;
            const float passThroughTolerance = glm::clamp(
                getRegistryFloat(baseSystem, "LilypadOneWayPassThroughTolerance", 0.06f),
                0.0f,
                0.5f
            );
            const float groundedFollowTolerance = glm::clamp(
                getRegistryFloat(baseSystem, "LilypadGroundedFollowTolerance", 0.55f),
                0.05f,
                1.25f
            );
            const float feetBefore = prevPos.y - halfExtents.y;
            const float feetAfter = resolvedPos.y - halfExtents.y;
            const bool descending = (velocity.y <= 0.0f) || (preResolveVerticalVelocity <= 0.0f);

            bool havePlatformSnap = false;
            float bestPlatformTop = -std::numeric_limits<float>::infinity();
            for (const auto& platform : oneWayPlatforms) {
                const bool overlapX = (resolvedPos.x + halfExtents.x) > platform.minX
                    && (resolvedPos.x - halfExtents.x) < platform.maxX;
                const bool overlapZ = (resolvedPos.z + halfExtents.z) > platform.minZ
                    && (resolvedPos.z - halfExtents.z) < platform.maxZ;
                if (!overlapX || !overlapZ) continue;

                const bool crossFromAbove = descending
                    && (feetBefore >= (platform.topY - passThroughTolerance))
                    && (feetAfter <= (platform.topY + passThroughTolerance));
                const bool followGroundedPlatform = wasGrounded
                    && std::abs(feetBefore - platform.topY) <= groundedFollowTolerance;
                if (!(crossFromAbove || followGroundedPlatform)) continue;

                if (!havePlatformSnap || platform.topY > bestPlatformTop) {
                    havePlatformSnap = true;
                    bestPlatformTop = platform.topY;
                }
            }

            if (havePlatformSnap) {
                resolvedPos.y = bestPlatformTop + halfExtents.y + skin;
                hitGround = true;
            }
        }

        // If grounding/slope/platform logic changed Y after horizontal resolution,
        // run horizontal collision again at the final Y to prevent side-wall phasing.
        const bool movedDownAfterGrounding = resolvedPos.y < (preGroundingY - 1e-4f);
        if (movedDownAfterGrounding) {
            glm::vec3 groundedHorizPos(prevPos.x, resolvedPos.y, prevPos.z);
            ResolveAxis(groundedHorizPos, 0, moveHalfExtents.x, velocity.x, horizontalBlocks, moveHalfExtents);
            ResolveAxis(groundedHorizPos, 2, moveHalfExtents.z, velocity.z, horizontalBlocks, moveHalfExtents);
            resolvedPos.x = groundedHorizPos.x;
            resolvedPos.z = groundedHorizPos.z;
        }

        // Fail-safe: never keep a resolved position embedded in solids.
        // Rewind to previous safe frame position instead of allowing tunneling cascades.
        if (IntersectsAnyBlock(resolvedPos, halfExtents, blockAABBs)) {
            bool recoveredFromEmbed = false;
            if (!slopeColliders.empty()) {
                // First try keeping pre-grounding horizontal placement; this avoids
                // false-positive side blocking when climbing chained ramps.
                glm::vec3 keepHorizontalPos(preGroundingPos.x, resolvedPos.y, preGroundingPos.z);
                if (!IntersectsAnyBlock(keepHorizontalPos, halfExtents, blockAABBs)) {
                    resolvedPos = keepHorizontalPos;
                    recoveredFromEmbed = true;
                } else {
                    // As a last resort near slopes, nudge upward to the nearest free spot.
                    const float rescueMaxUp = glm::clamp(
                        getRegistryFloat(baseSystem, "SlopeEmbedRescueMaxUp", 0.75f),
                        0.05f,
                        2.0f
                    );
                    constexpr float kRescueStep = 0.05f;
                    for (float lift = kRescueStep; lift <= rescueMaxUp + 1e-4f; lift += kRescueStep) {
                        glm::vec3 liftPos = resolvedPos;
                        liftPos.y += lift;
                        if (!IntersectsAnyBlock(liftPos, halfExtents, blockAABBs)) {
                            resolvedPos = liftPos;
                            // Small lifts should still count as grounded.
                            if (lift <= 0.15f) hitGround = true;
                            recoveredFromEmbed = true;
                            break;
                        }
                    }
                }
            }
            if (!recoveredFromEmbed) {
                resolvedPos = prevPos;
                hitGround = wasGrounded;
            }
        }

        player.cameraPosition = resolvedPos;
        // Update grounded state: if we were moving down and got clamped upward relative to desired
        hitGround = hitGround || usedAutoStep || ((velocity.y < 0.0f) && ((resolvedPos.y - desiredPos.y) > 0.001f));
        player.onGround = hitGround;
        const bool landedThisFrame = (!wasGrounded && player.onGround);
        const float impactSpeed = std::max(0.0f, -preResolveVerticalVelocity);
        player.lastLandingImpactSpeed = landedThisFrame ? impactSpeed : 0.0f;
        if (landedThisFrame) {
            const bool landingSfxEnabled = getRegistryBool(baseSystem, "WalkLandingSfxEnabled", true);
            if (landingSfxEnabled) {
                const float minImpactSpeed = std::max(0.0f, getRegistryFloat(baseSystem, "WalkLandingSfxMinImpactSpeed", 4.5f));
                const float landingCooldown = std::max(0.0f, getRegistryFloat(baseSystem, "WalkLandingSfxCooldown", 0.08f));
                if (impactSpeed >= minImpactSpeed) {
                    triggerGameplaySfx(baseSystem, "footstep.ck", landingCooldown);
                }
            }
        }
        if (player.onGround) player.verticalVelocity = 0.0f;
        const float safeDt = (dt > 1e-5f) ? dt : (1.0f / 60.0f);
        const glm::vec3 resolvedDelta = resolvedPos - prevPos;
        player.viewBobHorizontalSpeed = glm::length(glm::vec2(resolvedDelta.x, resolvedDelta.z)) / safeDt;
        player.prevCameraPosition = resolvedPos;
    }
}
