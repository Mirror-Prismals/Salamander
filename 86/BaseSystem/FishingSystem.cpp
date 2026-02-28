#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <vector>

namespace RenderInitSystemLogic { int FaceTileIndexFor(const WorldContext* worldCtx, const Entity& proto, int faceType); }
namespace BlockSelectionSystemLogic { bool HasBlockAt(BaseSystem& baseSystem, const std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position); }

namespace FishingSystemLogic {

    namespace {
        constexpr float kPickupChargeSeconds = 1.25f;
        constexpr float kFishingChargeSecondsDefault = kPickupChargeSeconds * 2.0f;
        constexpr float kCastMinDistanceDefault = 2.0f;
        constexpr float kCastMaxDistanceDefault = 24.0f;
        constexpr float kRippleDecayPerSecond = 1.8f;
        constexpr float kNibbleIntervalMin = 2.0f;
        constexpr float kNibbleIntervalMax = 5.0f;
        constexpr int kNibbleMinDefault = 6;
        constexpr int kNibbleMaxDefault = 10;
        constexpr float kHookWindowSecondsDefault = 0.55f;
        constexpr float kHookInputThresholdDefault = 0.30f;
        constexpr float kReelDistancePerScrollDefault = 0.12f;
        constexpr float kReelCompleteDistanceDefault = 0.28f;
        constexpr float kInterestRadiusDefault = 5.25f;
        constexpr float kBobberGravityDefault = 20.0f;
        constexpr float kBobberAirDragDefault = 0.14f;
        constexpr float kBobberWaterDragDefault = 5.5f;
        constexpr float kCastSpeedScaleDefault = 1.22f;
        constexpr float kCastLobMinDefault = 2.4f;
        constexpr float kCastLobMaxDefault = 8.4f;
        constexpr float kReelPullPerScrollDefault = 1.9f;

        struct FishingVertex {
            glm::vec3 pos;
            glm::vec3 color;
        };

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

        bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            if (std::holds_alternative<std::string>(it->second)) {
                const std::string& raw = std::get<std::string>(it->second);
                if (raw == "true" || raw == "TRUE" || raw == "True" || raw == "1") return true;
                if (raw == "false" || raw == "FALSE" || raw == "False" || raw == "0") return false;
            }
            return fallback;
        }

        float nextRand01(FishingContext& fishing) {
            fishing.rngState = fishing.rngState * 1664525u + 1013904223u;
            return static_cast<float>((fishing.rngState >> 8) & 0x00ffffffu) / 16777216.0f;
        }

        void clearChargeState(PlayerContext& player) {
            player.isChargingBlock = false;
            player.blockChargeReady = false;
            player.blockChargeValue = 0.0f;
        }

        void clearFishingRuntime(FishingContext& fishing) {
            fishing.bobberActive = false;
            fishing.bobberInWater = false;
            fishing.bobberVelocity = glm::vec3(0.0f);
            fishing.hooked = false;
            fishing.biteActive = false;
            fishing.biteTimer = 0.0f;
            fishing.spookTimer = 0.0f;
            fishing.interestedFishIndex = -1;
            fishing.hookedFishIndex = -1;
            fishing.nibbleCount = 0;
            fishing.nibbleTarget = 0;
            fishing.nibbleTimer = 0.0f;
            fishing.ripplePulse = 0.0f;
            fishing.rippleTime = 0.0f;
            fishing.fishShadows.clear();
            fishing.lineAnchors.clear();
            fishing.linePath.clear();
            fishing.rodChargeBend = 0.0f;
            fishing.rodCastKick = 0.0f;
            fishing.reelRemainingDistance = 0.0f;
            fishing.reelCastDistance = 0.0f;
            fishing.previewBobberInitialized = false;
            fishing.previewBobberPosition = glm::vec3(0.0f);
            fishing.previewBobberVelocity = glm::vec3(0.0f);
        }

        void clearFishingCastState(FishingContext& fishing, bool resetRipples) {
            fishing.bobberActive = false;
            fishing.bobberInWater = false;
            fishing.bobberVelocity = glm::vec3(0.0f);
            fishing.hooked = false;
            fishing.biteActive = false;
            fishing.biteTimer = 0.0f;
            fishing.spookTimer = 0.0f;
            fishing.interestedFishIndex = -1;
            fishing.hookedFishIndex = -1;
            fishing.nibbleCount = 0;
            fishing.nibbleTarget = 0;
            fishing.nibbleTimer = 0.0f;
            fishing.lineAnchors.clear();
            fishing.linePath.clear();
            fishing.reelRemainingDistance = 0.0f;
            fishing.reelCastDistance = 0.0f;
            fishing.previewBobberInitialized = false;
            fishing.previewBobberPosition = glm::vec3(0.0f);
            fishing.previewBobberVelocity = glm::vec3(0.0f);
            if (resetRipples) {
                fishing.ripplePulse = 0.0f;
                fishing.rippleTime = 0.0f;
            }
        }

        int resolveWaterPrototypeID(const std::vector<Entity>& prototypes) {
            for (const auto& proto : prototypes) {
                if (proto.name == "Water") return proto.prototypeID;
            }
            return -1;
        }

        uint32_t mixBits(uint32_t x) {
            x ^= x >> 16;
            x *= 0x7feb352du;
            x ^= x >> 15;
            x *= 0x846ca68bu;
            x ^= x >> 16;
            return x;
        }

        void computeCameraBasis(const PlayerContext& player, glm::vec3& forward, glm::vec3& right, glm::vec3& up) {
            forward.x = std::cos(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
            forward.y = std::sin(glm::radians(player.cameraPitch));
            forward.z = std::sin(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
            if (glm::length(forward) < 1e-4f) forward = glm::vec3(0.0f, 0.0f, -1.0f);
            forward = glm::normalize(forward);

            right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
            if (glm::length(right) < 1e-4f) right = glm::vec3(1.0f, 0.0f, 0.0f);
            right = glm::normalize(right);
            up = glm::normalize(glm::cross(right, forward));
        }

        void computeRodPose(const PlayerContext& player,
                            const FishingContext* fishing,
                            glm::vec3& outBase,
                            glm::vec3& outTip,
                            glm::mat4* outModel) {
            glm::vec3 forward(0.0f), right(0.0f), up(0.0f);
            computeCameraBasis(player, forward, right, up);

            constexpr float kRodLength = 12.0f / 24.0f;
            float chargeBend = 0.0f;
            float castKick = 0.0f;
            if (fishing) {
                chargeBend = glm::clamp(fishing->rodChargeBend, 0.0f, 1.0f);
                castKick = glm::clamp(fishing->rodCastKick, 0.0f, 1.0f);
            }
            // Handle sits near the lower-right of the view; keep a stable world-down offset so
            // it doesn't drop out of frame when pitching downward.
            outBase = player.cameraPosition + forward * 0.20f + right * 0.19f + glm::vec3(0.0f, 0.05f, 0.0f);
            outBase += right * (0.025f * chargeBend - 0.010f * castKick);
            outBase -= up * (0.045f * chargeBend);
            glm::vec3 rodDir = glm::normalize(
                forward * (0.93f - 0.42f * chargeBend + 0.58f * castKick)
                + up * (0.22f + 0.36f * chargeBend - 0.28f * castKick)
                + right * (0.12f - 0.06f * chargeBend + 0.05f * castKick)
            );
            outTip = outBase + rodDir * kRodLength;

            if (!outModel) return;
            glm::vec3 center = outBase + rodDir * (kRodLength * 0.5f);
            glm::vec3 axisX = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), rodDir);
            if (glm::length(axisX) < 1e-4f) axisX = glm::cross(forward, rodDir);
            if (glm::length(axisX) < 1e-4f) axisX = right;
            axisX = glm::normalize(axisX);
            glm::vec3 axisZ = glm::normalize(glm::cross(axisX, rodDir));

            glm::mat4 rot(1.0f);
            rot[0] = glm::vec4(axisX, 0.0f);
            rot[1] = glm::vec4(rodDir, 0.0f);
            rot[2] = glm::vec4(axisZ, 0.0f);
            *outModel = glm::translate(glm::mat4(1.0f), center) * rot;
        }

        glm::vec3 normalizeOrDefault(const glm::vec3& v, const glm::vec3& fallback) {
            if (glm::length(v) < 1e-4f) return fallback;
            return glm::normalize(v);
        }

        glm::vec3 projectDirectionOnSurface(const glm::vec3& direction,
                                            const glm::vec3& surfaceNormal) {
            glm::vec3 n = normalizeOrDefault(surfaceNormal, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::vec3 projected = direction - n * glm::dot(direction, n);
            if (glm::length(projected) < 1e-4f) {
                projected = glm::cross(n, glm::vec3(0.0f, 1.0f, 0.0f));
                if (glm::length(projected) < 1e-4f) {
                    projected = glm::cross(n, glm::vec3(1.0f, 0.0f, 0.0f));
                }
            }
            return normalizeOrDefault(projected, glm::vec3(1.0f, 0.0f, 0.0f));
        }

        void computePlacedRodModel(const FishingContext& fishing,
                                   glm::mat4& outModel,
                                   glm::vec3* outBase = nullptr,
                                   glm::vec3* outTip = nullptr) {
            constexpr float kRodLength = 12.0f / 24.0f;
            glm::vec3 surfaceNormal = normalizeOrDefault(fishing.rodPlacedNormal, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::vec3 rodDir = projectDirectionOnSurface(fishing.rodPlacedDirection, surfaceNormal);
            glm::vec3 axisX = glm::cross(surfaceNormal, rodDir);
            if (glm::length(axisX) < 1e-4f) axisX = glm::cross(rodDir, glm::vec3(0.0f, 1.0f, 0.0f));
            axisX = normalizeOrDefault(axisX, glm::vec3(1.0f, 0.0f, 0.0f));
            glm::vec3 axisZ = normalizeOrDefault(glm::cross(axisX, rodDir), surfaceNormal);

            glm::mat4 rot(1.0f);
            rot[0] = glm::vec4(axisX, 0.0f);
            rot[1] = glm::vec4(rodDir, 0.0f);
            rot[2] = glm::vec4(axisZ, 0.0f);
            outModel = glm::translate(glm::mat4(1.0f), fishing.rodPlacedPosition) * rot;
            if (outBase) *outBase = fishing.rodPlacedPosition - rodDir * (kRodLength * 0.5f);
            if (outTip) *outTip = fishing.rodPlacedPosition + rodDir * (kRodLength * 0.5f);
        }

        const Entity* resolveRodPrototype(const std::vector<Entity>& prototypes) {
            static const std::array<const char*, 4> kNames = {"FirLog1Tex", "FirLog2Tex", "SpruceLog1Tex", "OakLogTex"};
            for (const auto* name : kNames) {
                for (const auto& proto : prototypes) {
                    if (proto.name == name && proto.useTexture) return &proto;
                }
            }
            return nullptr;
        }

        bool triggerFishingSfx(BaseSystem& baseSystem, const std::string& scriptPath, float cooldownSeconds = 0.0f) {
            if (!baseSystem.audio || !baseSystem.audio->chuck) return false;
            static std::unordered_map<std::string, double> s_lastTrigger;
            double now = glfwGetTime();
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

        int currentLocalDayKey() {
            auto now = std::chrono::system_clock::now();
            std::time_t tt = std::chrono::system_clock::to_time_t(now);
            std::tm lt{};
#ifdef _WIN32
            localtime_s(&lt, &tt);
#else
            localtime_r(&tt, &lt);
#endif
            return (lt.tm_year + 1900) * 1000 + lt.tm_yday;
        }

        bool findWaterSurfaceInColumn(const VoxelWorldContext& voxelWorld,
                                      int waterPrototypeID,
                                      int x,
                                      int z,
                                      int minY,
                                      int maxY,
                                      glm::vec3& outWaterSurfacePoint) {
            if (maxY < minY) std::swap(minY, maxY);
            for (int y = maxY; y >= minY; --y) {
                glm::ivec3 cell(x, y, z);
                if (voxelWorld.getBlockWorld(cell) != static_cast<uint32_t>(waterPrototypeID)) continue;
                glm::ivec3 above(x, y + 1, z);
                if (voxelWorld.getBlockWorld(above) == static_cast<uint32_t>(waterPrototypeID)) continue;
                if (voxelWorld.getBlockWorld(above) != 0) continue;
                outWaterSurfacePoint = glm::vec3(
                    static_cast<float>(x) + 0.5f,
                    static_cast<float>(y) + 1.02f,
                    static_cast<float>(z) + 0.5f
                );
                return true;
            }
            return false;
        }

        void ensureDailySchoolLocation(BaseSystem& baseSystem,
                                       const VoxelWorldContext& voxelWorld,
                                       int waterPrototypeID,
                                       FishingContext& fishing) {
            int dayKey = currentLocalDayKey();
            if (fishing.dailySchoolDayKey == dayKey && fishing.dailySchoolValid) return;

            fishing.dailySchoolDayKey = dayKey;
            fishing.dailySchoolValid = false;
            fishing.dailyFishRemaining = 0;
            if (!voxelWorld.enabled || waterPrototypeID < 0) return;

            int fishMin = std::max(0, getRegistryInt(baseSystem, "FishingDailyFishMin", 4));
            int fishMax = std::max(fishMin, getRegistryInt(baseSystem, "FishingDailyFishMax", 7));
            uint32_t fishHash = mixBits(static_cast<uint32_t>(dayKey) * 2166136261u + 0x27d4eb2du);
            fishing.dailyFishRemaining = fishMin + static_cast<int>(fishHash % static_cast<uint32_t>(fishMax - fishMin + 1));

            const int searchRadius = std::max(16, getRegistryInt(baseSystem, "FishingDailySearchRadius", 256));
            const int attempts = std::max(8, getRegistryInt(baseSystem, "FishingDailySearchAttempts", 160));
            const int minY = getRegistryInt(baseSystem, "FishingDailySearchMinY", -24);
            const int maxY = getRegistryInt(baseSystem, "FishingDailySearchMaxY", 48);
            const uint32_t span = static_cast<uint32_t>(searchRadius * 2 + 1);

            for (int i = 0; i < attempts; ++i) {
                uint32_t hX = mixBits(static_cast<uint32_t>(dayKey) * 747796405u + static_cast<uint32_t>(i) * 2891336453u + 0x9e3779b9u);
                uint32_t hZ = mixBits(static_cast<uint32_t>(dayKey) * 277803737u + static_cast<uint32_t>(i) * 1442695041u + 0x85ebca6bu);
                int x = static_cast<int>(hX % span) - searchRadius;
                int z = static_cast<int>(hZ % span) - searchRadius;

                glm::vec3 candidate(0.0f);
                if (findWaterSurfaceInColumn(voxelWorld, waterPrototypeID, x, z, minY, maxY, candidate)) {
                    fishing.dailySchoolPosition = candidate;
                    fishing.dailySchoolValid = true;
                    return;
                }
            }
        }

        bool isSolidForBobber(uint32_t blockID, int waterPrototypeID) {
            if (blockID == 0) return false;
            if (waterPrototypeID >= 0 && blockID == static_cast<uint32_t>(waterPrototypeID)) return false;
            return true;
        }

        bool isPointInsideSolid(const VoxelWorldContext& voxelWorld,
                                int waterPrototypeID,
                                const glm::vec3& point) {
            glm::ivec3 cell(
                static_cast<int>(std::floor(point.x)),
                static_cast<int>(std::floor(point.y)),
                static_cast<int>(std::floor(point.z))
            );
            return isSolidForBobber(voxelWorld.getBlockWorld(cell), waterPrototypeID);
        }

        bool isPointInsideWater(const VoxelWorldContext& voxelWorld,
                                int waterPrototypeID,
                                const glm::vec3& point) {
            if (!voxelWorld.enabled || waterPrototypeID < 0) return true;
            glm::ivec3 cell(
                static_cast<int>(std::floor(point.x)),
                static_cast<int>(std::floor(point.y)),
                static_cast<int>(std::floor(point.z))
            );
            return voxelWorld.getBlockWorld(cell) == static_cast<uint32_t>(waterPrototypeID);
        }

        glm::vec2 applyFishWaterBoundarySteering(const VoxelWorldContext& voxelWorld,
                                                 int waterPrototypeID,
                                                 const glm::vec3& fishPos,
                                                 const glm::vec2& desiredDir,
                                                 float lookAhead,
                                                 float strength) {
            if (!voxelWorld.enabled || waterPrototypeID < 0) return desiredDir;
            if (glm::length(desiredDir) < 1e-4f) return desiredDir;
            glm::vec2 dir = glm::normalize(desiredDir);
            float probeDist = std::max(0.2f, lookAhead);

            auto probe = [&](const glm::vec2& d) -> bool {
                glm::vec3 p = fishPos + glm::vec3(d.x, 0.0f, d.y) * probeDist;
                return isPointInsideWater(voxelWorld, waterPrototypeID, p);
            };

            if (probe(dir)) return desiredDir;

            glm::vec2 left(-dir.y, dir.x);
            glm::vec2 right = -left;
            bool leftOpen = probe(left);
            bool rightOpen = probe(right);

            glm::vec2 push(0.0f);
            if (leftOpen && !rightOpen) {
                push = left;
            } else if (rightOpen && !leftOpen) {
                push = right;
            } else if (leftOpen && rightOpen) {
                bool leftForwardOpen = probe(glm::normalize(dir * 0.45f + left * 0.55f));
                bool rightForwardOpen = probe(glm::normalize(dir * 0.45f + right * 0.55f));
                if (leftForwardOpen && !rightForwardOpen) push = left;
                else if (rightForwardOpen && !leftForwardOpen) push = right;
                else push = left;
            } else {
                push = -dir;
            }

            float steer = std::max(0.1f, strength);
            return desiredDir + glm::normalize(push) * steer;
        }

        void resolveFishWaterCollision(const VoxelWorldContext& voxelWorld,
                                       int waterPrototypeID,
                                       const glm::vec3& previousPos,
                                       glm::vec3& ioPos,
                                       glm::vec2& ioVelocity) {
            if (!voxelWorld.enabled || waterPrototypeID < 0) return;
            if (isPointInsideWater(voxelWorld, waterPrototypeID, ioPos)) return;

            glm::vec3 xOnly(ioPos.x, previousPos.y, previousPos.z);
            glm::vec3 zOnly(previousPos.x, previousPos.y, ioPos.z);
            bool canX = isPointInsideWater(voxelWorld, waterPrototypeID, xOnly);
            bool canZ = isPointInsideWater(voxelWorld, waterPrototypeID, zOnly);

            if (canX && canZ) {
                float xErr = std::abs(ioPos.x - xOnly.x) + std::abs(ioPos.z - xOnly.z);
                float zErr = std::abs(ioPos.x - zOnly.x) + std::abs(ioPos.z - zOnly.z);
                ioPos = (xErr <= zErr) ? xOnly : zOnly;
            } else if (canX) {
                ioPos = xOnly;
            } else if (canZ) {
                ioPos = zOnly;
            } else {
                ioPos = previousPos;
            }

            float probe = 0.32f;
            bool blockedX = false;
            bool blockedZ = false;
            if (std::abs(ioVelocity.x) > 1e-4f) {
                float stepX = (ioVelocity.x > 0.0f) ? probe : -probe;
                blockedX = !isPointInsideWater(voxelWorld, waterPrototypeID, previousPos + glm::vec3(stepX, 0.0f, 0.0f));
            }
            if (std::abs(ioVelocity.y) > 1e-4f) {
                float stepZ = (ioVelocity.y > 0.0f) ? probe : -probe;
                blockedZ = !isPointInsideWater(voxelWorld, waterPrototypeID, previousPos + glm::vec3(0.0f, 0.0f, stepZ));
            }

            if (blockedX) ioVelocity.x = -ioVelocity.x * 0.86f;
            if (blockedZ) ioVelocity.y = -ioVelocity.y * 0.86f;
            if (!blockedX && !blockedZ) ioVelocity = -ioVelocity * 0.70f;

            if (glm::length(ioVelocity) > 1e-4f) {
                ioVelocity = glm::normalize(ioVelocity);
            } else {
                ioVelocity = glm::vec2(1.0f, 0.0f);
            }
        }

        bool segmentHitsSolid(const VoxelWorldContext& voxelWorld,
                              int waterPrototypeID,
                              const glm::vec3& a,
                              const glm::vec3& b,
                              float sampleStep) {
            float step = std::max(0.05f, sampleStep);
            glm::vec3 delta = b - a;
            float len = glm::length(delta);
            if (len <= 1e-4f) return isPointInsideSolid(voxelWorld, waterPrototypeID, a);

            int samples = std::max(1, static_cast<int>(std::ceil(len / step)));
            for (int i = 1; i <= samples; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(samples);
                glm::vec3 p = glm::mix(a, b, t);
                if (isPointInsideSolid(voxelWorld, waterPrototypeID, p)) return true;
            }
            return false;
        }

        bool findFirstSolidHitOnSegment(const VoxelWorldContext& voxelWorld,
                                        int waterPrototypeID,
                                        const glm::vec3& a,
                                        const glm::vec3& b,
                                        float sampleStep,
                                        glm::vec3& outHitPoint,
                                        glm::ivec3& outHitCell) {
            float step = std::max(0.05f, sampleStep);
            glm::vec3 delta = b - a;
            float len = glm::length(delta);
            if (len <= 1e-5f) return false;

            int samples = std::max(1, static_cast<int>(std::ceil(len / step)));
            glm::vec3 prev = a;
            for (int i = 1; i <= samples; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(samples);
                glm::vec3 p = glm::mix(a, b, t);
                glm::ivec3 cell(
                    static_cast<int>(std::floor(p.x)),
                    static_cast<int>(std::floor(p.y)),
                    static_cast<int>(std::floor(p.z))
                );
                if (isSolidForBobber(voxelWorld.getBlockWorld(cell), waterPrototypeID)) {
                    outHitPoint = prev;
                    outHitCell = cell;
                    return true;
                }
                prev = p;
            }
            return false;
        }

        bool chooseAnchorPointForHit(const VoxelWorldContext& voxelWorld,
                                     int waterPrototypeID,
                                     const glm::vec3& from,
                                     const glm::vec3& to,
                                     const glm::ivec3& hitCell,
                                     float probeStep,
                                     float stepUpHeight,
                                     glm::vec3& outAnchor) {
            glm::vec3 center = glm::vec3(hitCell) + glm::vec3(0.5f);
            glm::vec3 toDir = to - from;
            if (glm::length(toDir) < 1e-4f) toDir = glm::vec3(1.0f, 0.0f, 0.0f);
            toDir = glm::normalize(toDir);
            glm::vec3 flat(toDir.x, 0.0f, toDir.z);
            if (glm::length(flat) < 1e-4f) flat = glm::vec3(1.0f, 0.0f, 0.0f);
            flat = glm::normalize(flat);
            glm::vec3 side = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), flat);
            if (glm::length(side) < 1e-4f) side = glm::vec3(1.0f, 0.0f, 0.0f);
            side = glm::normalize(side);

            float around = 0.62f;
            float lift = 0.53f;
            float stepLift = glm::clamp(stepUpHeight, 0.0f, 2.0f);
            std::array<glm::vec3, 7> candidates{
                center + side * around + glm::vec3(0.0f, lift, 0.0f),
                center - side * around + glm::vec3(0.0f, lift, 0.0f),
                center + flat * around + glm::vec3(0.0f, lift, 0.0f),
                center - flat * around + glm::vec3(0.0f, lift, 0.0f),
                center + glm::vec3(0.0f, 1.04f + stepLift * 0.5f, 0.0f),
                center + side * (around * 0.55f) + glm::vec3(0.0f, 1.02f + stepLift, 0.0f),
                center - side * (around * 0.55f) + glm::vec3(0.0f, 1.02f + stepLift, 0.0f)
            };

            bool found = false;
            float bestScore = std::numeric_limits<float>::max();
            glm::vec3 best(0.0f);
            for (const glm::vec3& c : candidates) {
                if (isPointInsideSolid(voxelWorld, waterPrototypeID, c)) continue;
                if (segmentHitsSolid(voxelWorld, waterPrototypeID, from, c, probeStep)) continue;
                if (segmentHitsSolid(voxelWorld, waterPrototypeID, c, to, probeStep)) continue;
                float score = glm::length(c - from) + glm::length(to - c) + std::abs(c.y - center.y) * 0.18f;
                if (!found || score < bestScore) {
                    found = true;
                    bestScore = score;
                    best = c;
                }
            }

            if (!found) return false;
            outAnchor = best;
            return true;
        }

        void sanitizeAnchorChain(const VoxelWorldContext& voxelWorld,
                                 int waterPrototypeID,
                                 const glm::vec3& rodTip,
                                 const glm::vec3& bobberPos,
                                 float probeStep,
                                 std::vector<glm::vec3>& anchors) {
            bool changed = true;
            while (changed) {
                changed = false;
                for (size_t i = 0; i < anchors.size(); ++i) {
                    glm::vec3 prev = (i == 0) ? rodTip : anchors[i - 1];
                    glm::vec3 next = (i + 1 < anchors.size()) ? anchors[i + 1] : bobberPos;
                    if (!segmentHitsSolid(voxelWorld, waterPrototypeID, prev, next, probeStep)) {
                        anchors.erase(anchors.begin() + static_cast<long>(i));
                        changed = true;
                        break;
                    }
                }
            }
        }

        void extendAnchorChain(const VoxelWorldContext& voxelWorld,
                               int waterPrototypeID,
                               const glm::vec3& rodTip,
                               const glm::vec3& bobberPos,
                               float probeStep,
                               float stepUpHeight,
                               int maxAnchors,
                               std::vector<glm::vec3>& anchors) {
            maxAnchors = std::max(0, maxAnchors);
            if (maxAnchors == 0) {
                anchors.clear();
                return;
            }

            int guard = 0;
            while (static_cast<int>(anchors.size()) < maxAnchors && guard < maxAnchors * 3) {
                guard += 1;
                bool inserted = false;
                size_t segmentCount = anchors.size() + 1;
                for (size_t seg = 0; seg < segmentCount; ++seg) {
                    glm::vec3 from = (seg == 0) ? rodTip : anchors[seg - 1];
                    glm::vec3 to = (seg < anchors.size()) ? anchors[seg] : bobberPos;
                    if (!segmentHitsSolid(voxelWorld, waterPrototypeID, from, to, probeStep)) continue;

                    glm::vec3 hitPoint(0.0f);
                    glm::ivec3 hitCell(0);
                    if (!findFirstSolidHitOnSegment(voxelWorld, waterPrototypeID, from, to, probeStep, hitPoint, hitCell)) {
                        continue;
                    }

                    glm::vec3 anchor(0.0f);
                    if (!chooseAnchorPointForHit(voxelWorld, waterPrototypeID, from, to, hitCell, probeStep, stepUpHeight, anchor)) {
                        continue;
                    }

                    if (seg > 0 && glm::length(anchor - anchors[seg - 1]) < 0.10f) continue;
                    if (seg < anchors.size() && glm::length(anchor - anchors[seg]) < 0.10f) continue;
                    anchors.insert(anchors.begin() + static_cast<long>(seg), anchor);
                    inserted = true;
                    break;
                }
                if (!inserted) break;
            }

            sanitizeAnchorChain(voxelWorld, waterPrototypeID, rodTip, bobberPos, probeStep, anchors);
            if (static_cast<int>(anchors.size()) > maxAnchors) {
                anchors.resize(static_cast<size_t>(maxAnchors));
            }
        }

        void buildAnchorPath(const glm::vec3& rodTip,
                             const glm::vec3& bobberPos,
                             const std::vector<glm::vec3>& anchors,
                             std::vector<glm::vec3>& outPath) {
            outPath.clear();
            outPath.reserve(anchors.size() + 2);
            outPath.push_back(rodTip);
            for (const glm::vec3& anchor : anchors) outPath.push_back(anchor);
            outPath.push_back(bobberPos);
        }

        float polylineLength(const std::vector<glm::vec3>& points) {
            if (points.size() < 2) return 0.0f;
            float length = 0.0f;
            for (size_t i = 1; i < points.size(); ++i) {
                length += glm::length(points[i] - points[i - 1]);
            }
            return length;
        }

        void updateWrappedLinePath(const VoxelWorldContext& voxelWorld,
                                   int waterPrototypeID,
                                   const glm::vec3& rodTip,
                                   const glm::vec3& bobberPos,
                                   float segmentLength,
                                   int maxPoints,
                                   float probeStep,
                                   float stepUpHeight,
                                   std::vector<glm::vec3>& outPath) {
            outPath.clear();
            outPath.reserve(static_cast<size_t>(std::max(2, maxPoints)));
            outPath.push_back(rodTip);

            glm::vec3 current = rodTip;
            glm::vec3 toBobber = bobberPos - current;
            float totalDist = glm::length(toBobber);
            if (totalDist <= 1e-4f) {
                outPath.push_back(bobberPos);
                return;
            }

            segmentLength = std::max(0.08f, segmentLength);
            maxPoints = std::max(2, maxPoints);
            probeStep = std::max(0.05f, probeStep);
            stepUpHeight = glm::clamp(stepUpHeight, 0.0f, 2.0f);

            if (!segmentHitsSolid(voxelWorld, waterPrototypeID, rodTip, bobberPos, probeStep)) {
                outPath.push_back(bobberPos);
                return;
            }

            constexpr float kTau = 6.28318530718f;
            for (int i = 0; i < maxPoints - 1; ++i) {
                glm::vec3 toEnd = bobberPos - current;
                float distToEnd = glm::length(toEnd);
                if (distToEnd <= segmentLength) {
                    outPath.push_back(bobberPos);
                    return;
                }

                glm::vec3 dir = toEnd / std::max(distToEnd, 1e-5f);
                glm::vec3 dirFlat(dir.x, 0.0f, dir.z);
                if (glm::length(dirFlat) < 1e-4f) dirFlat = glm::vec3(1.0f, 0.0f, 0.0f);
                dirFlat = glm::normalize(dirFlat);
                glm::vec3 side = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), dirFlat));
                if (glm::length(side) < 1e-4f) side = glm::vec3(1.0f, 0.0f, 0.0f);

                glm::vec3 candidate = current + dir * segmentLength;
                bool blocked = segmentHitsSolid(voxelWorld, waterPrototypeID, current, candidate, probeStep)
                    || isPointInsideSolid(voxelWorld, waterPrototypeID, candidate);

                if (blocked) {
                    bool found = false;
                    std::array<glm::vec3, 8> options{
                        current + dir * segmentLength + glm::vec3(0.0f, stepUpHeight, 0.0f),
                        current + glm::normalize(dir + side * 0.8f) * segmentLength,
                        current + glm::normalize(dir - side * 0.8f) * segmentLength,
                        current + side * segmentLength,
                        current - side * segmentLength,
                        current + glm::normalize(dir + side * 0.55f) * segmentLength + glm::vec3(0.0f, stepUpHeight * 0.5f, 0.0f),
                        current + glm::normalize(dir - side * 0.55f) * segmentLength + glm::vec3(0.0f, stepUpHeight * 0.5f, 0.0f),
                        current + glm::vec3(std::cos(static_cast<float>(i) * 0.5f * kTau), 0.0f, std::sin(static_cast<float>(i) * 0.5f * kTau)) * (segmentLength * 0.65f)
                    };
                    for (glm::vec3 option : options) {
                        if (segmentHitsSolid(voxelWorld, waterPrototypeID, current, option, probeStep)) continue;
                        if (isPointInsideSolid(voxelWorld, waterPrototypeID, option)) continue;
                        candidate = option;
                        found = true;
                        break;
                    }
                    if (!found) {
                        outPath.push_back(bobberPos);
                        return;
                    }
                }

                outPath.push_back(candidate);
                current = candidate;
                if (!segmentHitsSolid(voxelWorld, waterPrototypeID, current, bobberPos, probeStep)) {
                    outPath.push_back(bobberPos);
                    return;
                }
            }

            if (outPath.empty() || glm::length(outPath.back() - bobberPos) > 0.01f) {
                outPath.push_back(bobberPos);
            }
        }

        bool queryWaterSurfaceNearPoint(const VoxelWorldContext& voxelWorld,
                                        int waterPrototypeID,
                                        const glm::vec3& point,
                                        float minBelowSurface,
                                        float maxAboveSurface,
                                        float& outSurfaceY) {
            if (!voxelWorld.enabled || waterPrototypeID < 0) return false;
            int x = static_cast<int>(std::floor(point.x));
            int z = static_cast<int>(std::floor(point.z));
            int y = static_cast<int>(std::floor(point.y));
            glm::vec3 surface(0.0f);
            if (!findWaterSurfaceInColumn(voxelWorld, waterPrototypeID, x, z, y - 16, y + 16, surface)) {
                return false;
            }
            if (point.y < surface.y - minBelowSurface) return false;
            if (point.y > surface.y + maxAboveSurface) return false;
            outSurfaceY = surface.y;
            return true;
        }

        void resolveBobberSolidCollision(const VoxelWorldContext& voxelWorld,
                                         int waterPrototypeID,
                                         const glm::vec3& previousPos,
                                         glm::vec3& ioPosition,
                                         glm::vec3& ioVelocity,
                                         bool allowStepUp,
                                         float stepUpHeight) {
            glm::ivec3 cell(
                static_cast<int>(std::floor(ioPosition.x)),
                static_cast<int>(std::floor(ioPosition.y)),
                static_cast<int>(std::floor(ioPosition.z))
            );
            if (!isSolidForBobber(voxelWorld.getBlockWorld(cell), waterPrototypeID)) return;

            float topY = static_cast<float>(cell.y) + 1.03f;
            if (previousPos.y >= topY - 0.03f && ioVelocity.y <= 0.0f) {
                ioPosition.y = topY;
                ioVelocity.y = 0.0f;
                ioVelocity.x *= 0.55f;
                ioVelocity.z *= 0.55f;
                return;
            }

            if (allowStepUp && stepUpHeight > 0.01f) {
                float requiredLift = topY - previousPos.y;
                if (requiredLift >= -0.08f && requiredLift <= stepUpHeight) {
                    glm::ivec3 above(cell.x, cell.y + 1, cell.z);
                    if (!isSolidForBobber(voxelWorld.getBlockWorld(above), waterPrototypeID)) {
                        ioPosition.y = topY;
                        ioVelocity.y = std::max(0.0f, ioVelocity.y);
                        ioVelocity.x *= 0.72f;
                        ioVelocity.z *= 0.72f;
                        return;
                    }
                }
            }

            ioPosition = previousPos;
            ioVelocity *= 0.25f;
        }

        void spawnSchoolAroundBobber(FishingContext& fishing,
                                     const glm::vec3& bobberPos,
                                     const VoxelWorldContext& voxelWorld,
                                     int waterPrototypeID,
                                     int fishCountOverride = -1) {
            fishing.fishShadows.clear();
            int fishCount = fishCountOverride;
            if (fishCount <= 0) {
                fishCount = 8 + static_cast<int>(nextRand01(fishing) * 5.0f);
            }
            fishCount = std::clamp(fishCount, 1, 16);
            fishing.fishShadows.reserve(static_cast<size_t>(fishCount));

            for (int i = 0; i < fishCount; ++i) {
                float angle = nextRand01(fishing) * 6.28318530718f;
                float radius = 4.0f + nextRand01(fishing) * 9.0f;
                float sizeRoll = nextRand01(fishing);

                FishingShadowState shadow;
                shadow.position = bobberPos + glm::vec3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
                shadow.depth = 0.04f + nextRand01(fishing) * 0.08f;
                shadow.position.y = bobberPos.y - shadow.depth;
                shadow.orbitRadius = radius + 1.5f + nextRand01(fishing) * 4.5f;
                shadow.phase = nextRand01(fishing) * 6.28318530718f;
                float spreadAngle = nextRand01(fishing) * 6.28318530718f;
                float spreadRadius = 2.0f + nextRand01(fishing) * 10.0f;
                shadow.schoolOffset = glm::vec2(std::cos(spreadAngle), std::sin(spreadAngle)) * spreadRadius;

                if (sizeRoll < 0.50f) {
                    shadow.length = 1.05f + nextRand01(fishing) * 0.45f;
                    shadow.thickness = 0.22f + nextRand01(fishing) * 0.08f;
                    shadow.speed = 1.10f + nextRand01(fishing) * 0.28f;
                } else if (sizeRoll < 0.86f) {
                    shadow.length = 1.45f + nextRand01(fishing) * 0.50f;
                    shadow.thickness = 0.30f + nextRand01(fishing) * 0.11f;
                    shadow.speed = 0.82f + nextRand01(fishing) * 0.22f;
                } else {
                    shadow.length = 2.10f + nextRand01(fishing) * 0.85f;
                    shadow.thickness = 0.40f + nextRand01(fishing) * 0.14f;
                    shadow.speed = 0.62f + nextRand01(fishing) * 0.18f;
                }

                glm::vec2 tangent(-std::sin(angle), std::cos(angle));
                float jitter = (nextRand01(fishing) - 0.5f) * 0.8f;
                tangent += glm::vec2(jitter, -jitter * 0.5f);
                if (glm::length(tangent) < 1e-4f) tangent = glm::vec2(1.0f, 0.0f);
                shadow.velocity = glm::normalize(tangent);
                shadow.facing = shadow.velocity;

                if (voxelWorld.enabled && waterPrototypeID >= 0 && !isPointInsideWater(voxelWorld, waterPrototypeID, shadow.position)) {
                    bool foundWater = false;
                    const int rings = 10;
                    const int slices = 18;
                    const float step = 0.65f;
                    for (int ring = 1; ring <= rings && !foundWater; ++ring) {
                        float rr = ring * step;
                        for (int s = 0; s < slices; ++s) {
                            float th = (static_cast<float>(s) / static_cast<float>(slices)) * 6.28318530718f;
                            glm::vec3 candidate = shadow.position + glm::vec3(std::cos(th) * rr, 0.0f, std::sin(th) * rr);
                            candidate.y = bobberPos.y - shadow.depth;
                            if (!isPointInsideWater(voxelWorld, waterPrototypeID, candidate)) continue;
                            shadow.position = candidate;
                            foundWater = true;
                            break;
                        }
                    }
                    if (!foundWater) {
                        continue;
                    }
                }
                fishing.fishShadows.push_back(shadow);
            }
        }

        bool isFishIndexValid(const FishingContext& fishing, int index) {
            return index >= 0 && index < static_cast<int>(fishing.fishShadows.size());
        }

        int nearestFishToPoint(const FishingContext& fishing, const glm::vec3& point, float* outBestDist2 = nullptr) {
            if (fishing.fishShadows.empty()) return -1;
            int best = 0;
            float bestDist2 = std::numeric_limits<float>::max();
            for (int i = 0; i < static_cast<int>(fishing.fishShadows.size()); ++i) {
                const auto& fish = fishing.fishShadows[static_cast<size_t>(i)];
                glm::vec2 d(fish.position.x - point.x, fish.position.z - point.z);
                float dist2 = glm::dot(d, d);
                if (dist2 < bestDist2) {
                    bestDist2 = dist2;
                    best = i;
                }
            }
            if (outBestDist2) *outBestDist2 = bestDist2;
            return best;
        }

        int nearestFishToPointWithinRadius(const FishingContext& fishing, const glm::vec3& point, float radius) {
            float bestDist2 = std::numeric_limits<float>::max();
            int best = nearestFishToPoint(fishing, point, &bestDist2);
            if (best < 0) return -1;
            float r2 = radius * radius;
            if (bestDist2 > r2) return -1;
            return best;
        }

        void updateSchoolMotion(FishingContext& fishing,
                                const glm::vec3& center3,
                                float dt,
                                int skipFishIndex,
                                const glm::vec3* avoidPoint,
                                float avoidStrength,
                                const VoxelWorldContext& voxelWorld,
                                int waterPrototypeID) {
            glm::vec2 center(center3.x, center3.z);
            constexpr float kSeparationRadius = 3.2f;
            constexpr float kSeparationRadius2 = kSeparationRadius * kSeparationRadius;
            for (int i = 0; i < static_cast<int>(fishing.fishShadows.size()); ++i) {
                if (i == skipFishIndex) continue;
                auto& fish = fishing.fishShadows[static_cast<size_t>(i)];
                glm::vec2 drift(
                    std::sin(fish.phase * 0.33f + fish.orbitRadius * 0.41f),
                    std::cos(fish.phase * 0.27f + fish.speed * 3.2f)
                );
                glm::vec2 localCenter = center + fish.schoolOffset + drift * 1.3f;
                glm::vec2 pos(fish.position.x, fish.position.z);
                glm::vec2 toCenter = localCenter - pos;
                float dist = glm::length(toCenter);
                glm::vec2 tangent(-toCenter.y, toCenter.x);
                if (glm::length(tangent) < 1e-4f) tangent = glm::vec2(1.0f, 0.0f);
                tangent = glm::normalize(tangent);
                glm::vec2 radial = (dist > 1e-4f) ? (toCenter / dist) : glm::vec2(0.0f);
                float orbitError = (dist - fish.orbitRadius);
                float individuality = 0.45f + 0.55f * std::abs(std::sin(fish.orbitRadius * 0.37f + static_cast<float>(i) * 0.91f));
                float radialWeight = std::clamp(orbitError / std::max(fish.orbitRadius, 0.1f), -0.9f, 0.9f) * individuality;
                fish.phase += dt * (1.2f + fish.speed * 1.1f);
                float wiggle = std::sin(fish.phase) * 0.25f;
                glm::vec2 wander(
                    std::sin(fish.phase * 0.73f + fish.orbitRadius * 0.61f),
                    std::cos(fish.phase * 0.91f + fish.speed * 2.7f)
                );
                glm::vec2 desired = tangent * (0.75f + individuality * 0.55f)
                    + radial * (-radialWeight)
                    + glm::vec2(wiggle, -wiggle * 0.5f) * 0.18f
                    + wander * 0.22f;

                glm::vec2 separation(0.0f);
                for (int j = 0; j < static_cast<int>(fishing.fishShadows.size()); ++j) {
                    if (j == i || j == skipFishIndex) continue;
                    const auto& other = fishing.fishShadows[static_cast<size_t>(j)];
                    glm::vec2 delta = pos - glm::vec2(other.position.x, other.position.z);
                    float d2 = glm::dot(delta, delta);
                    if (d2 <= 1e-6f || d2 > kSeparationRadius2) continue;
                    float invDist = 1.0f / std::sqrt(d2);
                    float strength = (kSeparationRadius - (1.0f / invDist)) / kSeparationRadius;
                    separation += delta * invDist * strength;
                }
                if (glm::length(separation) > 1e-4f) {
                    desired += glm::normalize(separation) * (0.85f + individuality * 0.45f);
                }
                if (avoidPoint && avoidStrength > 0.001f) {
                    glm::vec2 away = pos - glm::vec2(avoidPoint->x, avoidPoint->z);
                    float awayLen = glm::length(away);
                    if (awayLen > 1e-4f) {
                        desired += (away / awayLen) * (1.6f * avoidStrength);
                    }
                }
                desired = applyFishWaterBoundarySteering(
                    voxelWorld,
                    waterPrototypeID,
                    fish.position,
                    desired,
                    0.65f + fish.length * 0.24f,
                    2.3f
                );
                if (glm::length(desired) < 1e-4f) desired = tangent;
                desired = glm::normalize(desired);
                float blendGain = 2.8f + avoidStrength * 3.4f;
                glm::vec2 blended = glm::mix(fish.velocity, desired, std::clamp(dt * blendGain, 0.0f, 1.0f));
                if (glm::length(blended) < 1e-4f) blended = desired;
                fish.velocity = glm::normalize(blended);
                float swimBoost = 1.0f + avoidStrength * 2.4f;
                glm::vec3 previousPos = fish.position;
                fish.position.x += fish.velocity.x * fish.speed * swimBoost * dt;
                fish.position.z += fish.velocity.y * fish.speed * swimBoost * dt;
                fish.position.y = center3.y - fish.depth + std::sin(fish.phase * 2.2f) * 0.01f;
                resolveFishWaterCollision(voxelWorld, waterPrototypeID, previousPos, fish.position, fish.velocity);
                fish.facing = fish.velocity;
            }
        }

        void updateInterestedFishMotion(FishingContext& fishing,
                                        float dt,
                                        const VoxelWorldContext& voxelWorld,
                                        int waterPrototypeID) {
            int fishIndex = fishing.hooked ? fishing.hookedFishIndex : fishing.interestedFishIndex;
            if (!isFishIndexValid(fishing, fishIndex)) return;
            auto& fish = fishing.fishShadows[static_cast<size_t>(fishIndex)];

            fish.phase += dt * (1.25f + fish.speed * 1.0f);

            glm::vec2 toBobber(fishing.bobberPosition.x - fish.position.x, fishing.bobberPosition.z - fish.position.z);
            float dist = glm::length(toBobber);
            glm::vec2 toward = (dist > 1e-4f) ? (toBobber / dist) : glm::vec2(1.0f, 0.0f);
            fish.facing = toward;

            if (fishing.hooked) {
                glm::vec3 target = fishing.bobberPosition + glm::vec3(0.0f, -fish.depth, 0.0f);
                fish.position = glm::mix(fish.position, target, std::clamp(dt * 8.5f, 0.0f, 1.0f));
                fish.velocity = toward;
                return;
            }

            glm::vec2 desired = toward;
            float blendRate = 3.0f;
            float speedScale = 1.05f;
            float nibblePulse = 0.5f + 0.5f * std::sin(fish.phase * 2.35f);
            float desiredDist = 0.50f + nibblePulse * 0.95f;
            float approachDeadband = 0.08f;
            if (dist > desiredDist + approachDeadband) {
                desired = toward;
            } else if (dist < desiredDist - approachDeadband) {
                desired = -toward;
            } else {
                desired = glm::vec2(0.0f);
            }
            if (glm::length(desired) < 1e-4f) {
                desired = toward * 0.15f;
            }
            // Keep nibble fish mostly on-axis with slight micro-jitter, but always visually facing bobber.
            glm::vec2 jitter(std::sin(fish.phase * 1.7f), std::cos(fish.phase * 1.9f));
            desired += jitter * 0.05f;
            desired = applyFishWaterBoundarySteering(
                voxelWorld,
                waterPrototypeID,
                fish.position,
                desired,
                0.70f + fish.length * 0.25f,
                2.6f
            );
            if (glm::length(desired) < 1e-4f) desired = toward;
            desired = glm::normalize(desired);

            glm::vec2 blended = glm::mix(fish.velocity, desired, std::clamp(dt * blendRate, 0.0f, 1.0f));
            if (glm::length(blended) < 1e-4f) blended = desired;
            fish.velocity = glm::normalize(blended);

            glm::vec3 previousPos = fish.position;
            fish.position.x += fish.velocity.x * fish.speed * speedScale * dt;
            fish.position.z += fish.velocity.y * fish.speed * speedScale * dt;
            fish.position.y = fishing.bobberPosition.y - fish.depth + std::sin(fish.phase * 2.2f) * 0.01f;
            resolveFishWaterCollision(voxelWorld, waterPrototypeID, previousPos, fish.position, fish.velocity);
        }

        void finalizeCatch(FishingContext& fishing, int fishIndex) {
            if (fishIndex < 0 || fishIndex >= static_cast<int>(fishing.fishShadows.size())) return;
            const FishingShadowState& fish = fishing.fishShadows[static_cast<size_t>(fishIndex)];
            const bool large = fish.length > 1.6f;
            const bool medium = !large && fish.length > 1.0f;

            const std::array<const char*, 4> smallNames = {"Silver Minnow", "River Darter", "Glass Fry", "Blue Sprat"};
            const std::array<const char*, 4> mediumNames = {"Golden Perch", "Stream Bass", "Island Pikelet", "Coral Runner"};
            const std::array<const char*, 4> largeNames = {"Thunder Koi", "Ancient Snapper", "Deep River Pike", "Moonfin"};

            float rName = nextRand01(fishing);
            float rWeight = nextRand01(fishing);
            const char* fishName = smallNames[static_cast<size_t>(rName * smallNames.size()) % smallNames.size()];
            float weight = 0.25f + rWeight * 1.25f;
            if (medium) {
                fishName = mediumNames[static_cast<size_t>(rName * mediumNames.size()) % mediumNames.size()];
                weight = 1.40f + rWeight * 3.80f;
            } else if (large) {
                fishName = largeNames[static_cast<size_t>(rName * largeNames.size()) % largeNames.size()];
                weight = 5.20f + rWeight * 9.40f;
            }

            std::ostringstream ss;
            ss << fishName << " - " << std::fixed << std::setprecision(2) << weight << " kg";
            fishing.lastCatchText = ss.str();
            fishing.lastCatchTimer = 4.0f;
            std::cout << "[Fishing] Caught " << fishing.lastCatchText << std::endl;
        }

        void pushLine(std::vector<FishingVertex>& verts,
                      const glm::vec3& a,
                      const glm::vec3& b,
                      const glm::vec3& color) {
            verts.push_back({a, color});
            verts.push_back({b, color});
        }

        void pushTri(std::vector<FishingVertex>& verts,
                     const glm::vec3& a,
                     const glm::vec3& b,
                     const glm::vec3& c,
                     const glm::vec3& color) {
            verts.push_back({a, color});
            verts.push_back({b, color});
            verts.push_back({c, color});
        }

        void appendCircleXZ(std::vector<FishingVertex>& verts,
                            const glm::vec3& center,
                            float radius,
                            int segments,
                            const glm::vec3& color) {
            if (segments < 3 || radius <= 0.0f) return;
            for (int i = 0; i < segments; ++i) {
                float t0 = (static_cast<float>(i) / static_cast<float>(segments)) * 6.28318530718f;
                float t1 = (static_cast<float>(i + 1) / static_cast<float>(segments)) * 6.28318530718f;
                glm::vec3 a(center.x + std::cos(t0) * radius, center.y, center.z + std::sin(t0) * radius);
                glm::vec3 b(center.x + std::cos(t1) * radius, center.y, center.z + std::sin(t1) * radius);
                pushLine(verts, a, b, color);
            }
        }

        void appendFishShadow(std::vector<FishingVertex>& lineVerts,
                              std::vector<FishingVertex>& fillVerts,
                              const FishingShadowState& fish) {
            glm::vec2 dir = fish.facing;
            if (glm::length(dir) < 1e-4f) dir = glm::vec2(1.0f, 0.0f);
            dir = glm::normalize(dir);
            // Whole-body wag: rotate the full fish silhouette side-to-side instead of
            // applying a traveling body wave (eel-like bend).
            float wagAngle = std::sin(fish.phase * 4.2f) * 0.44f;
            float wagCos = std::cos(wagAngle);
            float wagSin = std::sin(wagAngle);
            glm::vec2 wagDir(
                dir.x * wagCos - dir.y * wagSin,
                dir.x * wagSin + dir.y * wagCos
            );
            if (glm::length(wagDir) < 1e-4f) wagDir = dir;
            wagDir = glm::normalize(wagDir);
            glm::vec2 wagRight(-wagDir.y, wagDir.x);
            const float visualLengthScale = 1.24f;
            const float visualThicknessScale = 1.34f;
            float halfLen = fish.length * visualLengthScale * 0.5f;
            const int segments = 24;
            glm::vec3 outlineColor = glm::vec3(0.02f, 0.02f, 0.02f);
            glm::vec3 fillColor = glm::vec3(0.0f, 0.0f, 0.0f);

            auto samplePoint = [&](float t, float side) {
                float clampedT = glm::clamp(t, -1.0f, 1.0f);
                float x = clampedT * halfLen;
                float profile = std::max(0.0f, 1.0f - clampedT * clampedT);
                float headBulge = 1.0f + 0.65f * std::pow(std::max(0.0f, clampedT), 1.65f);
                float tailTaper = 0.70f + 0.30f * ((clampedT + 1.0f) * 0.5f);
                float localHalfWidth = fish.thickness * visualThicknessScale * profile * headBulge * tailTaper;
                float y = localHalfWidth * side;
                glm::vec2 offset = wagDir * x + wagRight * y;
                return glm::vec3(fish.position.x + offset.x, fish.position.y, fish.position.z + offset.y);
            };

            for (int i = 0; i < segments; ++i) {
                float t0 = -1.0f + (2.0f * static_cast<float>(i) / static_cast<float>(segments));
                float t1 = -1.0f + (2.0f * static_cast<float>(i + 1) / static_cast<float>(segments));
                glm::vec3 aTop = samplePoint(t0, 1.0f);
                glm::vec3 bTop = samplePoint(t1, 1.0f);
                glm::vec3 aBottom = samplePoint(t0, -1.0f);
                glm::vec3 bBottom = samplePoint(t1, -1.0f);
                pushLine(lineVerts, aTop, bTop, outlineColor);
                pushLine(lineVerts, aBottom, bBottom, outlineColor);
                if (i == 0 || i == segments - 1) {
                    pushLine(lineVerts, aTop, aBottom, outlineColor);
                }
                // Solid shadow fill via triangle strip segments.
                pushTri(fillVerts, aTop, bTop, bBottom, fillColor);
                pushTri(fillVerts, aTop, bBottom, aBottom, fillColor);
            }
        }
    }

    void UpdateFishing(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player || !baseSystem.voxelWorld || !baseSystem.fishing || !baseSystem.world) return;

        PlayerContext& player = *baseSystem.player;
        VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
        FishingContext& fishing = *baseSystem.fishing;
        auto playSfx = [&](const char* fileName, float cooldown = 0.0f) {
            triggerFishingSfx(baseSystem, std::string("Procedures/chuck/fishing/") + fileName, cooldown);
        };

        static bool bPressedLastFrame = false;
        const bool bPressed = (win && glfwGetKey(win, GLFW_KEY_B) == GLFW_PRESS);
        const bool bJustPressed = bPressed && !bPressedLastFrame;
        bPressedLastFrame = bPressed;

        if (fishing.lastCatchTimer > 0.0f) {
            fishing.lastCatchTimer = std::max(0.0f, fishing.lastCatchTimer - dt);
            if (fishing.lastCatchTimer <= 0.0f) fishing.lastCatchText.clear();
        }

        const int waterPrototypeID = resolveWaterPrototypeID(prototypes);
        ensureDailySchoolLocation(baseSystem, voxelWorld, waterPrototypeID, fishing);

        if (bJustPressed) {
            if (fishing.rodPlacedInWorld) {
                const bool hasTarget = player.hasBlockTarget && player.targetedWorldIndex >= 0;
                const glm::ivec3 targetCell = glm::ivec3(glm::round(player.targetedBlockPosition));
                const bool worldMatches = (fishing.rodPlacedWorldIndex < 0) || (player.targetedWorldIndex == fishing.rodPlacedWorldIndex);
                const bool cellMatches = (targetCell == fishing.rodPlacedCell);
                if (hasTarget && worldMatches && cellMatches) {
                    fishing.rodPlacedInWorld = false;
                    fishing.rodPlacedWorldIndex = -1;
                    player.buildMode = BuildModeType::Fishing;
                    clearChargeState(player);
                    if (baseSystem.hud) {
                        baseSystem.hud->showCharge = false;
                        baseSystem.hud->buildModeActive = true;
                        baseSystem.hud->buildModeType = static_cast<int>(BuildModeType::Fishing);
                        baseSystem.hud->displayTimer = 2.0f;
                    }
                }
            } else if (player.buildMode == BuildModeType::Fishing
                    && player.hasBlockTarget
                    && glm::length(player.targetedBlockNormal) > 0.1f) {
                glm::vec3 forward(0.0f), right(0.0f), up(0.0f);
                computeCameraBasis(player, forward, right, up);
                (void)right;
                (void)up;
                glm::vec3 placeNormal = normalizeOrDefault(player.targetedBlockNormal, glm::vec3(0.0f, 1.0f, 0.0f));
                const glm::ivec3 placeCell = glm::ivec3(glm::round(player.targetedBlockPosition + placeNormal));
                const int placeWorld = player.targetedWorldIndex;
                if (placeWorld < 0) {
                    return;
                }
                if (BlockSelectionSystemLogic::HasBlockAt(baseSystem, prototypes, placeWorld, glm::vec3(placeCell))) {
                    return;
                }
                fishing.rodPlacedInWorld = true;
                fishing.rodPlacedCell = placeCell;
                fishing.rodPlacedWorldIndex = placeWorld;
                fishing.rodPlacedNormal = placeNormal;
                fishing.rodPlacedDirection = projectDirectionOnSurface(forward, placeNormal);
                fishing.rodPlacedPosition = glm::vec3(placeCell) - placeNormal * 0.47f;
                clearFishingCastState(fishing, false);
                clearChargeState(player);
                player.buildMode = BuildModeType::Pickup;
                if (baseSystem.hud) {
                    baseSystem.hud->showCharge = false;
                    baseSystem.hud->buildModeActive = true;
                    baseSystem.hud->buildModeType = static_cast<int>(BuildModeType::Pickup);
                    baseSystem.hud->displayTimer = 2.0f;
                }
                return;
            }
        }

        const bool fishingMode = (player.buildMode == BuildModeType::Fishing);
        const bool controlsBlockedByUi = (baseSystem.ui && baseSystem.ui->active);
        const bool fishingControlsEnabled = fishingMode && !controlsBlockedByUi;
        const float spookDuration = std::max(0.1f, getRegistryFloat(baseSystem, "FishingSpookDuration", 1.4f));
        const float rodBendSmoothing = std::max(0.1f, getRegistryFloat(baseSystem, "FishingRodBendSmoothing", 12.0f));
        const float rodKickDecay = std::max(0.1f, getRegistryFloat(baseSystem, "FishingRodCastKickDecay", 8.0f));
        fishing.rodCastKick = std::max(0.0f, fishing.rodCastKick - dt * rodKickDecay);
        const float rodChargeTarget = (fishingControlsEnabled && !fishing.bobberActive && player.rightMouseDown)
            ? glm::clamp(player.blockChargeValue, 0.0f, 1.0f)
            : 0.0f;
        fishing.rodChargeBend = glm::mix(fishing.rodChargeBend, rodChargeTarget, glm::clamp(dt * rodBendSmoothing, 0.0f, 1.0f));

        const float interestRadius = std::max(0.1f, getRegistryFloat(baseSystem, "FishingInterestRadius", kInterestRadiusDefault));
        const float reelDistancePerScroll = std::max(0.01f, getRegistryFloat(baseSystem, "FishingReelDistancePerScroll", kReelDistancePerScrollDefault));
        const float reelCompleteDistance = std::max(0.05f, getRegistryFloat(baseSystem, "FishingReelCompleteDistance", kReelCompleteDistanceDefault));
        const float reelMaxDistance = std::max(
            reelCompleteDistance,
            getRegistryFloat(baseSystem, "FishingReelMaxDistance", 128.0f)
        );
        const float reelPullPerScroll = std::max(0.05f, getRegistryFloat(baseSystem, "FishingReelPullPerScroll", kReelPullPerScrollDefault + reelDistancePerScroll * 8.0f));
        const float hookWindowSeconds = std::max(0.05f, getRegistryFloat(baseSystem, "FishingHookWindowSeconds", kHookWindowSecondsDefault));
        const float hookInputThreshold = std::max(0.01f, getRegistryFloat(baseSystem, "FishingHookInputThreshold", kHookInputThresholdDefault));
        const float earlyReelThreshold = std::max(0.01f, getRegistryFloat(baseSystem, "FishingEarlyReelThreshold", 0.12f));
        const float bobberGravity = std::max(0.0f, getRegistryFloat(baseSystem, "FishingBobberGravity", kBobberGravityDefault));
        const float bobberAirDrag = std::max(0.0f, getRegistryFloat(baseSystem, "FishingBobberAirDrag", kBobberAirDragDefault));
        const float bobberWaterDrag = std::max(0.0f, getRegistryFloat(baseSystem, "FishingBobberWaterDrag", kBobberWaterDragDefault));
        const float castSpeedScale = std::max(0.1f, getRegistryFloat(baseSystem, "FishingCastSpeedScale", kCastSpeedScaleDefault));
        const float castLobMin = getRegistryFloat(baseSystem, "FishingCastLobMin", kCastLobMinDefault);
        const float castLobMax = getRegistryFloat(baseSystem, "FishingCastLobMax", kCastLobMaxDefault);
        const bool reelStepUpEnabled = getRegistryBool(baseSystem, "FishingReelStepUpEnabled", true);
        const float reelStepHeight = glm::clamp(getRegistryFloat(baseSystem, "FishingReelStepHeight", 1.15f), 0.0f, 2.0f);
        const bool lineWrapEnabled = getRegistryBool(baseSystem, "FishingLineWrapEnabled", true);
        const float lineWrapSegmentLength = glm::clamp(getRegistryFloat(baseSystem, "FishingLineWrapSegmentLength", 0.34f), 0.08f, 2.0f);
        const int lineWrapMaxPoints = std::clamp(getRegistryInt(baseSystem, "FishingLineWrapMaxPoints", 96), 2, 256);
        const float lineWrapProbeStep = glm::clamp(getRegistryFloat(baseSystem, "FishingLineWrapProbeStep", 0.22f), 0.05f, 1.5f);
        const bool invertScrollDirection = getRegistryBool(baseSystem, "FishingInvertScrollDirection", true);
        double reelDeltaRaw = 0.0;
        if (fishingControlsEnabled) {
            reelDeltaRaw = player.scrollYOffset;
            player.scrollYOffset = 0.0;
        }
        const float reelInputSigned = invertScrollDirection
            ? -static_cast<float>(reelDeltaRaw)
            : static_cast<float>(reelDeltaRaw);
        const float reelInInput = std::max(0.0f, -reelInputSigned);   // wheel down
        const float reelOutInput = std::max(0.0f, reelInputSigned);   // wheel up
        const float reelInput = reelInInput;
        glm::vec3 rodBase(0.0f), rodTip(0.0f);
        computeRodPose(player, &fishing, rodBase, rodTip, nullptr);

        auto refreshLinePath = [&]() {
            if (!fishing.bobberActive) {
                fishing.lineAnchors.clear();
                fishing.linePath.clear();
                return;
            }
            if (lineWrapEnabled) {
                float directDist = glm::length(fishing.bobberPosition - rodTip);
                int distanceBudget = std::max(2, static_cast<int>(std::ceil(directDist / std::max(0.08f, lineWrapSegmentLength))) + 8);
                int maxAnchors = std::max(0, std::min(lineWrapMaxPoints - 2, distanceBudget));
                extendAnchorChain(
                    voxelWorld,
                    waterPrototypeID,
                    rodTip,
                    fishing.bobberPosition,
                    lineWrapProbeStep,
                    reelStepHeight,
                    maxAnchors,
                    fishing.lineAnchors
                );
                buildAnchorPath(rodTip, fishing.bobberPosition, fishing.lineAnchors, fishing.linePath);
            } else {
                fishing.lineAnchors.clear();
                fishing.linePath.clear();
                fishing.linePath.push_back(rodTip);
                fishing.linePath.push_back(fishing.bobberPosition);
            }
        };

        fishing.spookTimer = std::max(0.0f, fishing.spookTimer - dt);

        fishing.rippleTime += dt;
        fishing.ripplePulse = std::max(0.0f, fishing.ripplePulse - dt * kRippleDecayPerSecond);

        auto beginCharge = [&](float chargeSeconds) {
            if (!player.isChargingBlock) player.blockChargeValue = 0.0f;
            player.isChargingBlock = true;
            player.blockChargeValue += dt / std::max(0.01f, chargeSeconds);
            if (player.blockChargeValue >= 1.0f) {
                player.blockChargeValue = 1.0f;
                player.blockChargeReady = true;
            }
        };

        auto endCharge = [&]() {
            clearChargeState(player);
        };

        if (fishing.dailyFishRemaining <= 0) {
            fishing.fishShadows.clear();
            fishing.interestedFishIndex = -1;
        } else if (fishing.fishShadows.empty() && fishing.dailySchoolValid) {
            int visibleFish = std::min(7, fishing.dailyFishRemaining);
            spawnSchoolAroundBobber(fishing, fishing.dailySchoolPosition, voxelWorld, waterPrototypeID, visibleFish);
        }

        if (!fishingMode && fishing.bobberActive) {
            clearFishingCastState(fishing, false);
        }

        if (fishing.bobberActive) {
            refreshLinePath();
            if (reelInInput > 0.001f) {
                fishing.reelRemainingDistance = std::max(0.0f, fishing.reelRemainingDistance - reelInInput * reelDistancePerScroll);
                if (fishing.bobberInWater) {
                    fishing.ripplePulse = std::max(fishing.ripplePulse, 0.35f);
                }
            }
            if (reelOutInput > 0.001f) {
                fishing.reelRemainingDistance = std::min(
                    reelMaxDistance,
                    fishing.reelRemainingDistance + reelOutInput * reelDistancePerScroll
                );
            }

            bool splashOnStep = false;
            float remaining = std::min(dt, 0.1f);
            while (remaining > 1e-5f) {
                float step = std::min(remaining, 1.0f / 90.0f);
                remaining -= step;
                if (!fishing.bobberInWater) {
                    fishing.bobberVelocity.y -= bobberGravity * step;
                }

                float drag = fishing.bobberInWater ? bobberWaterDrag : bobberAirDrag;
                fishing.bobberVelocity *= std::max(0.0f, 1.0f - drag * step);

                glm::vec3 previousPos = fishing.bobberPosition;
                glm::vec3 nextPos = previousPos + fishing.bobberVelocity * step;
                const bool wasInWater = fishing.bobberInWater;

                float waterSurfaceY = 0.0f;
                bool nowInWater = queryWaterSurfaceNearPoint(voxelWorld, waterPrototypeID, nextPos, 2.5f, 0.40f, waterSurfaceY);
                if (nowInWater) {
                    if (!fishing.bobberInWater && fishing.bobberVelocity.y < -0.15f) {
                        splashOnStep = true;
                        fishing.ripplePulse = 1.0f;
                    }
                    fishing.bobberInWater = true;
                    float bobPhase = fishing.rippleTime * 4.3f + nextPos.x * 0.21f + nextPos.z * 0.19f;
                    float bobOffset = std::sin(bobPhase) * 0.012f;
                    nextPos.y = waterSurfaceY + bobOffset;
                    fishing.bobberVelocity.y = 0.0f;
                    fishing.bobberVelocity.x *= 0.86f;
                    fishing.bobberVelocity.z *= 0.86f;
                } else {
                    fishing.bobberInWater = false;
                    // Prevent abrupt "pop out of water then launch" behavior when reeling against a bank.
                    // Step-up is useful for dry-land retrieval, but should not kick in right as we leave water.
                    bool allowStepUp = reelStepUpEnabled && reelInInput > 0.001f && !wasInWater;
                    resolveBobberSolidCollision(
                        voxelWorld,
                        waterPrototypeID,
                        previousPos,
                        nextPos,
                        fishing.bobberVelocity,
                        allowStepUp,
                        reelStepHeight
                    );
                }

                // Reel behaves like a winch/line-length constraint.
                // Keep fixed line length unless the player explicitly reels.
                glm::vec3 toBobber = nextPos - rodTip;
                float distToRod = glm::length(toBobber);
                float maxLineLength = std::max(reelCompleteDistance, fishing.reelRemainingDistance + reelCompleteDistance);
                if (distToRod > maxLineLength && distToRod > 1e-5f) {
                    glm::vec3 lineDir = toBobber / distToRod;
                    nextPos = rodTip + lineDir * maxLineLength;
                    float radialOut = glm::dot(fishing.bobberVelocity, lineDir);
                    if (radialOut > 0.0f) {
                        fishing.bobberVelocity -= lineDir * radialOut;
                    }
                    fishing.bobberVelocity *= fishing.bobberInWater ? 0.92f : 0.97f;
                }

                fishing.bobberPosition = nextPos;
            }

            if (splashOnStep) {
                playSfx("splash.ck", 0.03f);
            }
            refreshLinePath();
        }

        glm::vec3 schoolCenter = fishing.dailySchoolValid
            ? fishing.dailySchoolPosition
            : (fishing.bobberActive ? fishing.bobberPosition : player.cameraPosition);
        const float spookStrength = (fishing.bobberActive && fishing.spookTimer > 0.0f)
            ? std::clamp(fishing.spookTimer / std::max(spookDuration, 0.01f), 0.0f, 1.0f)
            : 0.0f;
        int skipIndex = (fishing.bobberActive && fishing.bobberInWater && spookStrength <= 0.001f && isFishIndexValid(fishing, fishing.interestedFishIndex))
            ? fishing.interestedFishIndex
            : -1;
        const glm::vec3* avoidPoint = (fishing.bobberInWater && spookStrength > 0.001f) ? &fishing.bobberPosition : nullptr;
        updateSchoolMotion(fishing, schoolCenter, dt, skipIndex, avoidPoint, spookStrength, voxelWorld, waterPrototypeID);
        if (fishing.bobberActive && (fishing.hooked || (fishing.bobberInWater && spookStrength <= 0.001f))) {
            updateInterestedFishMotion(fishing, dt, voxelWorld, waterPrototypeID);
        }

        if (!fishingMode) {
            fishing.previewBobberInitialized = false;
            return;
        }

        if (!fishing.bobberActive) {
            fishing.lineAnchors.clear();
            fishing.linePath.clear();
            {
                const float previewLineLength = glm::clamp(getRegistryFloat(baseSystem, "FishingPreviewMaxLineLength", 0.58f), 0.15f, 2.5f);
                glm::vec3 anchor = rodTip;

                if (!fishing.previewBobberInitialized) {
                    fishing.previewBobberInitialized = true;
                }
                glm::vec3 previewPos = anchor + glm::vec3(0.0f, -previewLineLength, 0.0f);
                if (isPointInsideSolid(voxelWorld, waterPrototypeID, previewPos)) {
                    glm::vec3 forward(0.0f), right(0.0f), up(0.0f);
                    computeCameraBasis(player, forward, right, up);
                    previewPos = anchor + forward * 0.20f - up * 0.04f;
                    if (isPointInsideSolid(voxelWorld, waterPrototypeID, previewPos)) {
                        previewPos = anchor + glm::vec3(0.0f, 0.04f, 0.0f);
                    }
                }
                fishing.previewBobberPosition = previewPos;
                fishing.previewBobberVelocity = glm::vec3(0.0f);

                fishing.bobberPosition = fishing.previewBobberPosition;
                fishing.bobberInWater = false;
                fishing.linePath.push_back(rodTip);
                fishing.linePath.push_back(fishing.bobberPosition);
            }
            if (!fishingControlsEnabled) {
                endCharge();
                return;
            }
            const float chargeSeconds = std::max(0.1f, getRegistryFloat(baseSystem, "FishingChargeSeconds", kFishingChargeSecondsDefault));
            if (player.rightMouseDown) {
                if (!player.isChargingBlock) {
                    playSfx("cast_charge.ck", 0.03f);
                }
                beginCharge(chargeSeconds);
            } else {
                endCharge();
            }

            if (player.leftMousePressed && player.rightMouseDown && player.blockChargeValue > 0.02f) {
                float castMin = getRegistryFloat(baseSystem, "FishingCastMinDistance", kCastMinDistanceDefault);
                float castMax = getRegistryFloat(baseSystem, "FishingCastMaxDistance", kCastMaxDistanceDefault);
                if (castMax < castMin) std::swap(castMin, castMax);
                float chargeNorm = glm::clamp(player.blockChargeValue, 0.0f, 1.0f);
                float castDistance = castMin + (castMax - castMin) * chargeNorm;
                float castSpeed = castDistance * castSpeedScale;
                float lobMin = std::min(castLobMin, castLobMax);
                float lobMax = std::max(castLobMin, castLobMax);
                float castLob = lobMin + (lobMax - lobMin) * chargeNorm;

                glm::vec3 forward(0.0f), right(0.0f), up(0.0f);
                computeCameraBasis(player, forward, right, up);

                fishing.bobberActive = true;
                fishing.bobberInWater = false;
                fishing.bobberPosition = rodTip + forward * 0.34f;
                fishing.bobberVelocity = forward * castSpeed + up * castLob;
                fishing.previewBobberInitialized = false;
                fishing.hooked = false;
                fishing.biteActive = false;
                fishing.biteTimer = 0.0f;
                fishing.interestedFishIndex = -1;
                fishing.hookedFishIndex = -1;
                fishing.nibbleCount = 0;
                int nibbleMin = std::max(1, getRegistryInt(baseSystem, "FishingNibbleMin", kNibbleMinDefault));
                int nibbleMax = std::max(nibbleMin, getRegistryInt(baseSystem, "FishingNibbleMax", kNibbleMaxDefault));
                fishing.nibbleTarget = nibbleMin + static_cast<int>(nextRand01(fishing) * static_cast<float>(nibbleMax - nibbleMin + 1));
                fishing.nibbleTimer = kNibbleIntervalMin + nextRand01(fishing) * (kNibbleIntervalMax - kNibbleIntervalMin);
                fishing.ripplePulse = 0.0f;
                fishing.rodChargeBend = 0.0f;
                fishing.rodCastKick = 1.0f;
                fishing.reelCastDistance = castDistance;
                fishing.reelRemainingDistance = castDistance;

                if (fishing.fishShadows.empty() && fishing.dailyFishRemaining > 0 && fishing.dailySchoolValid) {
                    int visibleFish = std::min(7, fishing.dailyFishRemaining);
                    spawnSchoolAroundBobber(fishing, fishing.dailySchoolPosition, voxelWorld, waterPrototypeID, visibleFish);
                }

                if (baseSystem.hud) baseSystem.hud->displayTimer = 2.0f;
                playSfx("cast.ck", 0.03f);
                refreshLinePath();
                {
                    float initialLineDist = fishing.linePath.size() >= 2
                        ? polylineLength(fishing.linePath)
                        : glm::length(rodTip - fishing.bobberPosition);
                    float initialNeed = std::max(0.0f, initialLineDist - reelCompleteDistance);
                    fishing.reelCastDistance = std::max(fishing.reelCastDistance, initialNeed);
                    fishing.reelRemainingDistance = std::max(fishing.reelRemainingDistance, initialNeed);
                }
                endCharge();
            }
            return;
        }

        if (!fishing.hooked) {
            if (!fishing.bobberInWater) {
                fishing.biteActive = false;
                fishing.biteTimer = 0.0f;
                fishing.interestedFishIndex = -1;
                fishing.nibbleCount = 0;
            } else if (fishing.biteActive) {
                if (reelInput >= hookInputThreshold && isFishIndexValid(fishing, fishing.interestedFishIndex)) {
                    fishing.hooked = true;
                    fishing.hookedFishIndex = fishing.interestedFishIndex;
                    fishing.biteActive = false;
                    fishing.biteTimer = 0.0f;
                    fishing.ripplePulse = 1.0f;
                    playSfx("hook_success.ck", 0.03f);
                } else {
                    fishing.biteTimer -= dt;
                    if (fishing.biteTimer <= 0.0f) {
                        playSfx("hook_fail.ck", 0.03f);
                        clearFishingCastState(fishing, true);
                        endCharge();
                        return;
                    }
                }
            } else {
                if (reelInput >= earlyReelThreshold) {
                    fishing.spookTimer = spookDuration;
                    fishing.biteActive = false;
                    fishing.biteTimer = 0.0f;
                    fishing.interestedFishIndex = -1;
                    fishing.nibbleCount = 0;
                    fishing.nibbleTimer = 1.4f + nextRand01(fishing) * 2.2f;
                    fishing.ripplePulse = 0.0f;
                    playSfx("hook_fail.ck", 0.12f);
                    endCharge();
                    return;
                }
                fishing.nibbleTimer -= dt;
                if (fishing.nibbleTimer <= 0.0f) {
                    if (!isFishIndexValid(fishing, fishing.interestedFishIndex)) {
                        fishing.interestedFishIndex = nearestFishToPointWithinRadius(fishing, fishing.bobberPosition, interestRadius);
                    }
                    if (!isFishIndexValid(fishing, fishing.interestedFishIndex)) {
                        fishing.nibbleCount = 0;
                        fishing.nibbleTimer = kNibbleIntervalMin + nextRand01(fishing) * (kNibbleIntervalMax - kNibbleIntervalMin);
                        endCharge();
                        return;
                    }
                    fishing.nibbleCount += 1;
                    fishing.ripplePulse = 1.0f;
                    fishing.nibbleTimer = kNibbleIntervalMin + nextRand01(fishing) * (kNibbleIntervalMax - kNibbleIntervalMin);
                    playSfx("nibble.ck", 0.03f);
                    if (fishing.nibbleCount >= fishing.nibbleTarget) {
                        fishing.biteActive = true;
                        fishing.biteTimer = hookWindowSeconds;
                        fishing.ripplePulse = 1.0f;
                        playSfx("bite.ck", 0.03f);
                        playSfx("hook_ready.ck", 0.03f);
                        if (baseSystem.hud) baseSystem.hud->displayTimer = 2.0f;
                    }
                }
            }
        }
        endCharge();

        refreshLinePath();
        float lineDistToTip = fishing.linePath.size() >= 2
            ? polylineLength(fishing.linePath)
            : glm::length(rodTip - fishing.bobberPosition);
        const bool linePhysicallyReeledIn = lineDistToTip <= reelCompleteDistance;
        if (linePhysicallyReeledIn) {
            int caughtIndex = fishing.hookedFishIndex;
            if (fishing.hooked && isFishIndexValid(fishing, fishing.hookedFishIndex)) {
                finalizeCatch(fishing, fishing.hookedFishIndex);
                if (fishing.dailyFishRemaining > 0) {
                    fishing.dailyFishRemaining -= 1;
                }
                if (caughtIndex >= 0 && caughtIndex < static_cast<int>(fishing.fishShadows.size())) {
                    fishing.fishShadows.erase(fishing.fishShadows.begin() + caughtIndex);
                }
            }
            playSfx("reel_complete.ck", 0.03f);
            clearFishingCastState(fishing, false);
            endCharge();
        }
    }

    void RenderFishing(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt;
        (void)win;
        if (!baseSystem.renderer || !baseSystem.player || !baseSystem.world || !baseSystem.fishing) return;

        RendererContext& renderer = *baseSystem.renderer;
        PlayerContext& player = *baseSystem.player;
        WorldContext& world = *baseSystem.world;
        FishingContext& fishing = *baseSystem.fishing;
        const bool fishingMode = player.buildMode == BuildModeType::Fishing;
        const bool rodPlacedInWorld = fishing.rodPlacedInWorld;
        const bool renderHeldRod = fishingMode && !rodPlacedInWorld;
        const bool renderPlacedRod = rodPlacedInWorld;

        glm::vec3 rodBase(0.0f), rodTip(0.0f);
        glm::mat4 rodModel(1.0f);
        if (renderHeldRod) {
            computeRodPose(player, &fishing, rodBase, rodTip, &rodModel);
        } else if (renderPlacedRod) {
            computePlacedRodModel(fishing, rodModel, &rodBase, &rodTip);
        }

        if ((renderHeldRod || renderPlacedRod) && renderer.faceShader && renderer.faceVAO && renderer.faceInstanceVBO) {
            const Entity* rodProto = resolveRodPrototype(prototypes);
            if (rodProto) {
                auto bindAtlasUniforms = [&](Shader& shader) {
                    shader.setInt("atlasEnabled", (renderer.atlasTexture != 0 && renderer.atlasTilesPerRow > 0 && renderer.atlasTilesPerCol > 0) ? 1 : 0);
                    shader.setVec2("atlasTileSize", glm::vec2(renderer.atlasTileSize));
                    shader.setVec2("atlasTextureSize", glm::vec2(renderer.atlasTextureSize));
                    shader.setInt("tilesPerRow", renderer.atlasTilesPerRow);
                    shader.setInt("tilesPerCol", renderer.atlasTilesPerCol);
                    shader.setInt("atlasTexture", 0);
                    if (renderer.atlasTexture != 0) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, renderer.atlasTexture);
                    }
                };

                renderer.faceShader->use();
                renderer.faceShader->setMat4("view", player.viewMatrix);
                renderer.faceShader->setMat4("projection", player.projectionMatrix);
                renderer.faceShader->setMat4("model", rodModel);
                renderer.faceShader->setVec3("cameraPos", player.cameraPosition);
                renderer.faceShader->setFloat("time", static_cast<float>(glfwGetTime()));
                renderer.faceShader->setVec3("lightDir", glm::normalize(glm::vec3(-0.35f, -1.0f, -0.25f)));
                renderer.faceShader->setVec3("ambientLight", glm::vec3(0.45f));
                renderer.faceShader->setVec3("diffuseLight", glm::vec3(0.55f));
                renderer.faceShader->setInt("wireframeDebug", 0);
                bindAtlasUniforms(*renderer.faceShader);

                constexpr float kRodX = 1.0f / 24.0f;
                constexpr float kRodY = 12.0f / 24.0f;
                constexpr float kRodZ = 1.0f / 24.0f;
                constexpr float kHalfX = kRodX * 0.5f;
                constexpr float kHalfY = kRodY * 0.5f;
                constexpr float kHalfZ = kRodZ * 0.5f;

                if (renderHeldRod) glDisable(GL_DEPTH_TEST);
                else glEnable(GL_DEPTH_TEST);
                glEnable(GL_CULL_FACE);
                glFrontFace(GL_CCW);
                glCullFace(GL_BACK);
                glBindVertexArray(renderer.faceVAO);
                glBindBuffer(GL_ARRAY_BUFFER, renderer.faceInstanceVBO);

                auto drawRodFace = [&](int faceType, const glm::vec3& offset, const glm::vec2& scale) {
                    FaceInstanceRenderData face;
                    face.position = offset;
                    face.color = glm::vec3(1.0f);
                    face.tileIndex = RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), *rodProto, faceType);
                    if (face.tileIndex < 0) face.tileIndex = 12;
                    face.alpha = 1.0f;
                    face.ao = glm::vec4(1.0f);
                    face.scale = scale;
                    face.uvScale = scale;
                    renderer.faceShader->setInt("faceType", faceType);
                    glBufferData(GL_ARRAY_BUFFER, sizeof(FaceInstanceRenderData), &face, GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 1);
                };

                drawRodFace(0, glm::vec3( kHalfX, 0.0f,   0.0f), glm::vec2(kRodZ, kRodY));
                drawRodFace(1, glm::vec3(-kHalfX, 0.0f,   0.0f), glm::vec2(kRodZ, kRodY));
                drawRodFace(2, glm::vec3( 0.0f,   kHalfY, 0.0f), glm::vec2(kRodX, kRodZ));
                drawRodFace(3, glm::vec3( 0.0f,  -kHalfY, 0.0f), glm::vec2(kRodX, kRodZ));
                drawRodFace(4, glm::vec3( 0.0f,   0.0f,   kHalfZ), glm::vec2(kRodX, kRodY));
                drawRodFace(5, glm::vec3( 0.0f,   0.0f,  -kHalfZ), glm::vec2(kRodX, kRodY));

                glBindVertexArray(0);
                glDisable(GL_CULL_FACE);
                glEnable(GL_DEPTH_TEST);
            }
        }

        if (!fishingMode && !renderPlacedRod && !fishing.bobberActive && fishing.fishShadows.empty()) {
            renderer.fishingVertexCount = 0;
            return;
        }

        static bool shaderWarned = false;
        if (!renderer.audioRayShader) {
            if (world.shaders.count("AUDIORAY_VERTEX_SHADER") && world.shaders.count("AUDIORAY_FRAGMENT_SHADER")) {
                renderer.audioRayShader = std::make_unique<Shader>(
                    world.shaders["AUDIORAY_VERTEX_SHADER"].c_str(),
                    world.shaders["AUDIORAY_FRAGMENT_SHADER"].c_str()
                );
            } else if (!shaderWarned) {
                shaderWarned = true;
                std::cerr << "FishingSystem: audio-ray shader sources not found." << std::endl;
            }
        }
        if (!renderer.audioRayShader) {
            renderer.fishingVertexCount = 0;
            return;
        }

        if (renderer.fishingVAO == 0) {
            glGenVertexArrays(1, &renderer.fishingVAO);
            glGenBuffers(1, &renderer.fishingVBO);
            glBindVertexArray(renderer.fishingVAO);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.fishingVBO);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
            glBindVertexArray(0);
        }

        std::vector<FishingVertex> lineVerts;
        std::vector<FishingVertex> fillVerts;
        lineVerts.reserve(fishing.fishShadows.size() * 80 + 320);
        fillVerts.reserve(fishing.fishShadows.size() * 120);

        for (const auto& fish : fishing.fishShadows) {
            appendFishShadow(lineVerts, fillVerts, fish);
        }

        const bool drawPlacedIdleBobber = renderPlacedRod && !fishing.bobberActive;
        const bool drawBobber = fishing.bobberActive
            || (fishingMode && !fishing.bobberActive && fishing.previewBobberInitialized)
            || drawPlacedIdleBobber;
        if (drawBobber) {
            glm::vec3 renderBobberPosition = fishing.bobberPosition;
            if (drawPlacedIdleBobber) {
                const float idleLineLength = glm::clamp(getRegistryFloat(baseSystem, "FishingPreviewMaxLineLength", 0.58f), 0.15f, 2.5f);
                glm::vec3 rodDir = normalizeOrDefault(rodTip - rodBase, glm::vec3(1.0f, 0.0f, 0.0f));
                glm::vec3 surfaceNormal = normalizeOrDefault(fishing.rodPlacedNormal, glm::vec3(0.0f, 1.0f, 0.0f));
                glm::vec3 side = glm::cross(surfaceNormal, rodDir);
                if (glm::length(side) < 1e-4f) side = glm::cross(rodDir, glm::vec3(0.0f, 1.0f, 0.0f));
                side = normalizeOrDefault(side, glm::vec3(1.0f, 0.0f, 0.0f));
                renderBobberPosition = rodTip + side * idleLineLength;
                // Keep the idle bobber on the same surface plane as the placed rod.
                float planeOffset = glm::dot(renderBobberPosition - fishing.rodPlacedPosition, surfaceNormal);
                renderBobberPosition -= surfaceNormal * planeOffset;
                renderBobberPosition += surfaceNormal * 0.03f;
            }
            const glm::vec3 bobberBase = renderBobberPosition + glm::vec3(0.0f, 0.03f, 0.0f);
            const glm::vec3 bobberTop = bobberBase + glm::vec3(0.0f, 0.08f, 0.0f);
            appendCircleXZ(lineVerts, bobberBase, 0.12f, 18, glm::vec3(0.98f, 0.98f, 0.98f));
            appendCircleXZ(lineVerts, bobberTop, 0.06f, 14, glm::vec3(1.0f, 0.52f, 0.0f));
            pushLine(lineVerts, bobberBase, bobberTop, glm::vec3(1.0f, 0.85f, 0.85f));
            if (fishingMode && fishing.linePath.size() >= 2) {
                glm::vec3 prev = rodTip;
                for (size_t i = 1; i < fishing.linePath.size(); ++i) {
                    glm::vec3 next = fishing.linePath[i];
                    if (i + 1 == fishing.linePath.size()) next = bobberTop;
                    pushLine(lineVerts, prev, next, glm::vec3(0.88f, 0.90f, 0.92f));
                    prev = next;
                }
            } else {
                pushLine(lineVerts, rodTip, bobberTop, glm::vec3(0.88f, 0.90f, 0.92f));
            }

            if (fishing.bobberActive && fishing.bobberInWater && fishing.ripplePulse > 0.01f) {
                float rippleRadius = 0.35f + (1.0f - fishing.ripplePulse) * 1.15f;
                glm::vec3 rippleColor = glm::mix(glm::vec3(0.0f, 0.85f, 1.0f), glm::vec3(0.2f, 0.6f, 0.75f), 1.0f - fishing.ripplePulse);
                appendCircleXZ(lineVerts, fishing.bobberPosition + glm::vec3(0.0f, 0.01f, 0.0f), rippleRadius, 28, rippleColor);
            }
        }

        renderer.fishingVertexCount = static_cast<int>(lineVerts.size() + fillVerts.size());
        if (renderer.fishingVertexCount <= 0) return;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glEnable(GL_DEPTH_TEST);

        renderer.audioRayShader->use();
        renderer.audioRayShader->setMat4("view", player.viewMatrix);
        renderer.audioRayShader->setMat4("projection", player.projectionMatrix);
        glBindVertexArray(renderer.fishingVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.fishingVBO);
        if (!fillVerts.empty()) {
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(fillVerts.size() * sizeof(FishingVertex)),
                         fillVerts.data(),
                         GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(fillVerts.size()));
        }
        if (!lineVerts.empty()) {
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(lineVerts.size() * sizeof(FishingVertex)),
                         lineVerts.data(),
                         GL_DYNAMIC_DRAW);
            glLineWidth(2.0f);
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVerts.size()));
            glLineWidth(1.0f);
        }

        glDepthMask(GL_TRUE);
        glBindVertexArray(0);
    }
}
