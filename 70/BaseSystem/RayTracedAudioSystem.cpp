#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <map>
#include <cmath>
#include <utility>
#include <algorithm>

// Forward Declarations
struct BaseSystem;
struct Entity;
struct EntityInstance;
struct RayTracedAudioContext;
struct PlayerContext;
struct ChunkContext;

namespace BlockSelectionSystemLogic {
    bool EnsureLocalCaches(BaseSystem& baseSystem,
                           const std::vector<Entity>& prototypes,
                           const glm::vec3& cameraPosition,
                           int radius);
    void EnsureAllCaches(BaseSystem& baseSystem,
                         const std::vector<Entity>& prototypes);
    bool SampleBlockDamping(BaseSystem& baseSystem,
                            const glm::ivec3& cell,
                            float& dampingOut);
}

namespace RayTracedAudioSystemLogic {

    namespace {
        EntityInstance* findInstanceById(LevelContext& level, int worldIndex, int instanceId) {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return nullptr;
            Entity& world = level.worlds[worldIndex];
            for (auto& inst : world.instances) {
                if (inst.instanceID == instanceId) return &inst;
            }
            return nullptr;
        }

        void buildSourceCache(BaseSystem& baseSystem,
                              int visualizerProtoID,
                              RayTracedAudioContext& rtAudio) {
            if (!baseSystem.level) return;
            rtAudio.sourceInstances.clear();
            for (size_t wi = 0; wi < baseSystem.level->worlds.size(); ++wi) {
                const auto& world = baseSystem.level->worlds[wi];
                for (const auto& inst : world.instances) {
                    if (inst.prototypeID == visualizerProtoID) {
                        rtAudio.sourceInstances.emplace_back(static_cast<int>(wi), inst.instanceID);
                    }
                }
            }
            rtAudio.sourceCacheBuilt = true;
        }

        glm::vec3 fibonacciSphereDirection(int index, int count) {
            if (count <= 0) return glm::vec3(0.0f, 1.0f, 0.0f);
            const float goldenAngle = 2.39996323f;
            float t = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
            float z = 1.0f - 2.0f * t;
            float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            float phi = goldenAngle * static_cast<float>(index);
            return glm::vec3(r * std::cos(phi), r * std::sin(phi), z);
        }

        bool traceToBlock(BaseSystem& baseSystem,
                          const glm::vec3& start,
                          const glm::vec3& dir,
                          float maxDist,
                          glm::vec3& outHitPos,
                          glm::vec3& outNormal,
                          float& outHitDist) {
            float dirLenSq = glm::dot(dir, dir);
            if (dirLenSq < 1e-6f) return false;
            glm::vec3 rayDir = dir / std::sqrt(dirLenSq);

            glm::ivec3 currentVoxel = glm::floor(start);
            glm::vec3 step = glm::sign(rayDir);
            glm::vec3 nextBoundary = glm::vec3(currentVoxel) + 0.5f + step * 0.5f;
            glm::vec3 tMax = (nextBoundary - start) / rayDir;
            glm::vec3 tDelta = glm::abs(1.0f / rayDir);

            float distanceTraveled = 0.0f;
            int maxSteps = 4096;
            int hitAxis = -1;

            while (distanceTraveled < maxDist && maxSteps > 0) {
                --maxSteps;
                if (tMax.x < tMax.y) {
                    if (tMax.x < tMax.z) {
                        currentVoxel.x += static_cast<int>(step.x);
                        distanceTraveled = tMax.x;
                        tMax.x += tDelta.x;
                        hitAxis = 0;
                    } else {
                        currentVoxel.z += static_cast<int>(step.z);
                        distanceTraveled = tMax.z;
                        tMax.z += tDelta.z;
                        hitAxis = 2;
                    }
                } else {
                    if (tMax.y < tMax.z) {
                        currentVoxel.y += static_cast<int>(step.y);
                        distanceTraveled = tMax.y;
                        tMax.y += tDelta.y;
                        hitAxis = 1;
                    } else {
                        currentVoxel.z += static_cast<int>(step.z);
                        distanceTraveled = tMax.z;
                        tMax.z += tDelta.z;
                        hitAxis = 2;
                    }
                }

                if (distanceTraveled >= maxDist) break;

                float dummy = 0.0f;
                if (BlockSelectionSystemLogic::SampleBlockDamping(baseSystem, currentVoxel, dummy)) {
                    outHitDist = distanceTraveled;
                    outHitPos = start + rayDir * distanceTraveled;
                    outNormal = glm::vec3(0.0f);
                    if (hitAxis == 0) outNormal.x = -step.x;
                    if (hitAxis == 1) outNormal.y = -step.y;
                    if (hitAxis == 2) outNormal.z = -step.z;
                    return true;
                }
            }
            return false;
        }

        bool hasLineOfSight(BaseSystem& baseSystem, const glm::vec3& start, const glm::vec3& end, float maxDistance) {
            glm::vec3 rayDir = end - start;
            float rayLenSq = glm::dot(rayDir, rayDir);
            if (rayLenSq < 1e-6f) return true;
            float rayLen = std::sqrt(rayLenSq);
            if (rayLen > maxDistance) rayLen = maxDistance;
            rayDir /= rayLen;

            glm::ivec3 currentVoxel = glm::floor(start);
            glm::vec3 step = glm::sign(rayDir);
            glm::vec3 nextBoundary = glm::vec3(currentVoxel) + 0.5f + step * 0.5f;
            glm::vec3 tMax = (nextBoundary - start) / rayDir;
            glm::vec3 tDelta = glm::abs(1.0f / rayDir);
            glm::ivec3 endCell = glm::ivec3(glm::floor(end));

            float distanceTraveled = 0.0f;
            int maxSteps = 2048;
            while (distanceTraveled < rayLen && maxSteps > 0) {
                float lastDistance = distanceTraveled;
                --maxSteps;
                if (tMax.x < tMax.y) {
                    if (tMax.x < tMax.z) { currentVoxel.x += static_cast<int>(step.x); distanceTraveled = tMax.x; tMax.x += tDelta.x; }
                    else { currentVoxel.z += static_cast<int>(step.z); distanceTraveled = tMax.z; tMax.z += tDelta.z; }
                } else {
                    if (tMax.y < tMax.z) { currentVoxel.y += static_cast<int>(step.y); distanceTraveled = tMax.y; tMax.y += tDelta.y; }
                    else { currentVoxel.z += static_cast<int>(step.z); distanceTraveled = tMax.z; tMax.z += tDelta.z; }
                }

                if (distanceTraveled >= rayLen) break;

                if (currentVoxel == endCell) continue;

                float dummy = 0.0f;
                if (BlockSelectionSystemLogic::SampleBlockDamping(baseSystem, currentVoxel, dummy)) {
                    return false;
                }

                if (distanceTraveled <= lastDistance + 1e-6f) break;
            }
            return true;
        }

        float accumulateWallDistance(BaseSystem& baseSystem, const glm::vec3& start, const glm::vec3& end, float maxDistance) {
            glm::vec3 rayDir = end - start;
            float rayLenSq = glm::dot(rayDir, rayDir);
            if (rayLenSq < 1e-6f) return 0.0f;
            float rayLen = std::sqrt(rayLenSq);
            if (rayLen > maxDistance) rayLen = maxDistance;
            rayDir /= rayLen;

            glm::ivec3 currentVoxel = glm::floor(start);
            glm::vec3 step = glm::sign(rayDir);
            glm::vec3 nextBoundary = glm::vec3(currentVoxel) + 0.5f + step * 0.5f;
            glm::vec3 tMax = (nextBoundary - start) / rayDir;
            glm::vec3 tDelta = glm::abs(1.0f / rayDir);
            glm::ivec3 endCell = glm::ivec3(glm::floor(end));

            float distanceTraveled = 0.0f;
            float wallDistance = 0.0f;
            int maxSteps = 2048;
            while (distanceTraveled < rayLen && maxSteps > 0) {
                float lastDistance = distanceTraveled;
                --maxSteps;
                if (tMax.x < tMax.y) {
                    if (tMax.x < tMax.z) { currentVoxel.x += static_cast<int>(step.x); distanceTraveled = tMax.x; tMax.x += tDelta.x; }
                    else { currentVoxel.z += static_cast<int>(step.z); distanceTraveled = tMax.z; tMax.z += tDelta.z; }
                } else {
                    if (tMax.y < tMax.z) { currentVoxel.y += static_cast<int>(step.y); distanceTraveled = tMax.y; tMax.y += tDelta.y; }
                    else { currentVoxel.z += static_cast<int>(step.z); distanceTraveled = tMax.z; tMax.z += tDelta.z; }
                }

                float stepLength = distanceTraveled - lastDistance;
                if (distanceTraveled >= rayLen) break;

                if (currentVoxel != endCell) {
                    float dummy = 0.0f;
                    if (BlockSelectionSystemLogic::SampleBlockDamping(baseSystem, currentVoxel, dummy)) {
                        wallDistance += stepLength;
                    }
                }

                if (distanceTraveled <= lastDistance + 1e-6f) break;
            }
            return wallDistance;
        }
    }

    void InvalidateSourceCache(BaseSystem& baseSystem) {
        if (!baseSystem.rayTracedAudio) return;
        RayTracedAudioContext& rtAudio = *baseSystem.rayTracedAudio;
        rtAudio.sourceCacheBuilt = false;
        rtAudio.sourceInstances.clear();
        rtAudio.batch.active = false;
        rtAudio.lastBatchCompleteTime = -1.0;
        rtAudio.micSourceStates.clear();
        rtAudio.micBatch.active = false;
        rtAudio.lastMicBatchCompleteTime = -1.0;
    }

    void ProcessRayTracedAudio(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.rayTracedAudio || !baseSystem.world || !baseSystem.player || !baseSystem.level || baseSystem.level->worlds.empty()) return;

        RayTracedAudioContext& rtAudio = *baseSystem.rayTracedAudio;
        PlayerContext& player = *baseSystem.player;

        double now = glfwGetTime();

        bool rebuildDebug = false;
        const double kDebugInterval = 0.5;
        if (rtAudio.lastDebugTime < 0.0 || (now - rtAudio.lastDebugTime) >= kDebugInterval) {
            rebuildDebug = true;
            rtAudio.lastDebugTime = now;
        }

        int cacheRadius = 2;
        float maxDistance = 64.0f;
        if (baseSystem.chunk && baseSystem.chunk->initialized) {
            ChunkContext& chunk = *baseSystem.chunk;
            int renderDist = chunk.renderDistanceChunks > 0 ? chunk.renderDistanceChunks : 6;
            float maxDim = static_cast<float>(std::max(chunk.chunkSize.x, std::max(chunk.chunkSize.y, chunk.chunkSize.z)));
            maxDistance = static_cast<float>(renderDist) * maxDim;
        }

        const int whiteRayCount = 20;
        const int maxBounces = 6;
        const int debugRayCount = 2;

        const glm::vec3 whiteColor(1.0f, 1.0f, 1.0f);
        const glm::vec3 blueColor(0.25f, 0.7f, 1.0f);
        const glm::vec3 greenColor(0.2f, 0.95f, 0.35f);
        const glm::vec3 orangeColor(1.0f, 0.65f, 0.1f);

        int visualizerProtoID = -1;
        for (const auto& proto : prototypes) {
            if (proto.name == "AudioVisualizer") {
                visualizerProtoID = proto.prototypeID;
                break;
            }
        }
        if (visualizerProtoID == -1) return;

        LevelContext& level = *baseSystem.level;
        std::vector<EntityInstance*> sources;
        if (!rtAudio.sourceCacheBuilt) {
            buildSourceCache(baseSystem, visualizerProtoID, rtAudio);
        }
        bool cacheValid = true;
        for (const auto& ref : rtAudio.sourceInstances) {
            EntityInstance* inst = findInstanceById(level, ref.first, ref.second);
            if (!inst) { cacheValid = false; continue; }
            if (inst->prototypeID != visualizerProtoID) continue;
            sources.push_back(inst);
        }
        if (!cacheValid) {
            rtAudio.sourceCacheBuilt = false;
        }
        if (sources.empty()) {
            rtAudio.sourceStates.clear();
            rtAudio.micSourceStates.clear();
            return;
        }

        bool useFullCache = rtAudio.micCaptureActive;
        if (useFullCache) {
            BlockSelectionSystemLogic::EnsureAllCaches(baseSystem, prototypes);
        }

        RayTraceBatch& batch = rtAudio.batch;
        if (!batch.active) {
            const double kUpdateInterval = 0.4;
            bool startBatch = true;
            if (rtAudio.lastBatchCompleteTime >= 0.0 && (now - rtAudio.lastBatchCompleteTime) < kUpdateInterval) {
                startBatch = false;
            }
            if (startBatch) {
                if (!useFullCache) {
                    if (!BlockSelectionSystemLogic::EnsureLocalCaches(baseSystem, prototypes, player.cameraPosition, cacheRadius)) {
                        return;
                    }
                }

                batch = RayTraceBatch{};
                batch.active = true;
                batch.debugActive = rebuildDebug;
                batch.totalRays = whiteRayCount;
                batch.listenerPos = player.cameraPosition;

                glm::vec3 forward;
                forward.x = std::cos(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
                forward.y = std::sin(glm::radians(player.cameraPitch));
                forward.z = std::sin(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
                forward = glm::normalize(forward);
                glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
                if (glm::length(right) < 1e-4f) right = glm::vec3(1.0f, 0.0f, 0.0f);
                batch.right = right;

                batch.sourceIds.reserve(sources.size());
                batch.sourcePositions.reserve(sources.size());
                for (const auto* src : sources) {
                    batch.sourceIds.push_back(src->instanceID);
                    batch.sourcePositions.push_back(src->position);
                }
                batch.accum.assign(sources.size(), {});
                batch.rays.resize(batch.totalRays);
                for (int rayIndex = 0; rayIndex < batch.totalRays; ++rayIndex) {
                    RayTraceRayState& ray = batch.rays[rayIndex];
                    ray.pos = batch.listenerPos;
                    ray.dir = fibonacciSphereDirection(rayIndex, batch.totalRays);
                    ray.bounceIndex = 0;
                    ray.initDone = false;
                    ray.finished = false;
                    ray.escaped = false;
                    ray.debug = batch.debugActive && rayIndex < debugRayCount;
                    ray.blueLenSum = 0.0f;
                    ray.blueLenCount = 0;
                    ray.blueLenMax = 0.0f;
                    ray.lastDirPerSource.assign(batch.sourcePositions.size(), glm::vec3(0.0f));
                    ray.hasLastDirPerSource.assign(batch.sourcePositions.size(), false);
                }

                if (batch.debugActive) {
                    rtAudio.debugSegments.clear();
                    if (rtAudio.micListenerValid) {
                        glm::vec3 micDir = rtAudio.micListenerForward;
                        if (glm::length(micDir) < 1e-4f) {
                            micDir = glm::vec3(0.0f, 0.0f, -1.0f);
                        } else {
                            micDir = glm::normalize(micDir);
                        }
                        rtAudio.debugSegments.push_back({rtAudio.micListenerPos,
                                                         rtAudio.micListenerPos + micDir * 1.5f,
                                                         glm::vec3(1.0f, 0.9f, 0.2f)});
                    }
                }
            }
        }

        auto updateHeadResponse = [&]() {
            if (!baseSystem.player || !baseSystem.audio) return;
            if (rtAudio.sourceStates.empty()) return;
            PlayerContext& player = *baseSystem.player;
            glm::vec3 forward;
            forward.x = std::cos(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
            forward.y = std::sin(glm::radians(player.cameraPitch));
            forward.z = std::sin(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
            forward = glm::normalize(forward);
            glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
            if (glm::length(right) < 1e-4f) right = glm::vec3(1.0f, 0.0f, 0.0f);
            float sampleRate = baseSystem.audio->sampleRate;
            const float cutoffMin = 2000.0f;
            const float cutoffMax = 18000.0f;
            const float distCutoffMin = 7500.0f;
            const float distCutoffMax = cutoffMax;
            const float distStart = 4.0f;
            const float distEnd = 60.0f;
            const float widthNear = 2.0f;
            const float widthFar = 7.0f;

            for (auto& [id, st] : rtAudio.sourceStates) {
                glm::vec3 dir = st.direction;
                float dirLen = glm::length(dir);
                float dotForward = 1.0f;
                if (dirLen > 1e-4f) {
                    dir /= dirLen;
                    st.pan = std::clamp(glm::dot(dir, right), -1.0f, 1.0f);
                    dotForward = std::clamp(glm::dot(dir, forward), -1.0f, 1.0f);
                } else {
                    st.pan = 0.0f;
                }
                float dist = glm::length(st.sourcePos - player.cameraPosition);
                float width = 1.0f;
                if (widthFar > widthNear) {
                    float t = std::clamp((dist - widthNear) / (widthFar - widthNear), 0.0f, 1.0f);
                    width = t * t * (3.0f - 2.0f * t);
                }
                st.pan *= width;
                if (sampleRate > 0.0f) {
                    float t = std::clamp((1.0f - dotForward) * 0.5f, 0.0f, 1.0f);
                    float headCutoff = cutoffMax * std::pow(cutoffMin / cutoffMax, t);
                    float distT = 0.0f;
                    if (distEnd > distStart) {
                        distT = std::clamp((dist - distStart) / (distEnd - distStart), 0.0f, 1.0f);
                    }
                    float distCutoff = distCutoffMax * std::pow(distCutoffMin / distCutoffMax, distT);
                    float cutoff = std::min(headCutoff, distCutoff);
                    float alpha = 1.0f - std::exp(-2.0f * 3.14159265359f * cutoff / sampleRate);
                    st.hfAlpha = std::clamp(alpha, 0.0f, 1.0f);
                } else {
                    st.hfAlpha = 0.0f;
                }
            }
        };

        if (batch.active) {
            const int bounceStepsPerFrame = 6;
            int stepsThisFrame = 0;
            int attempts = 0;

        auto emitStates = [&](int finishedRaysCount, bool finalUpdate) {
            if (batch.sourceIds.empty()) return;
            float escapeRatio = (finishedRaysCount > 0)
                ? static_cast<float>(batch.escapeCount) / static_cast<float>(finishedRaysCount)
                : 0.0f;
            float avgBlueLen = (batch.blueLenCount > 0)
                ? (batch.blueLenSum / static_cast<float>(batch.blueLenCount))
                : 0.0f;

            const float metersPerBlock = 0.762f; // ~2.5 feet
            const float speedOfSound = 343.0f;
            float echoDelaySeconds = (batch.blueLenMax > 0.0f)
                ? (batch.blueLenMax * metersPerBlock / speedOfSound)
                : 0.0f;

            float echoGain = 0.0f;
            if (avgBlueLen > 0.0f) {
                float intensity = avgBlueLen / (avgBlueLen + 6.0f);
                echoGain = std::clamp(intensity * (1.0f - escapeRatio), 0.0f, 1.0f);
            }

            glm::vec3 earRight(1.0f, 0.0f, 0.0f);
            glm::vec3 earForward(0.0f, 0.0f, -1.0f);
            glm::vec3 earUp(0.0f, 1.0f, 0.0f);
            if (baseSystem.player) {
                PlayerContext& player = *baseSystem.player;
                glm::vec3 forward;
                forward.x = std::cos(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
                forward.y = std::sin(glm::radians(player.cameraPitch));
                forward.z = std::sin(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
                forward = glm::normalize(forward);
                earRight = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
                if (glm::length(earRight) < 1e-4f) earRight = glm::vec3(1.0f, 0.0f, 0.0f);
                earForward = forward;
                earUp = glm::normalize(glm::cross(earRight, earForward));
                if (glm::length(earUp) < 1e-4f) earUp = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            const float earOffset = 0.6f;
            const float probeOffset = 0.4f;

            std::map<int, AudioSourceState> newStates;
            for (size_t s = 0; s < batch.sourceIds.size(); ++s) {
                float directRatio = 0.0f;
                if (earOffset > 0.0f) {
                    glm::vec3 leftEar = batch.listenerPos - earRight * earOffset;
                    glm::vec3 rightEar = batch.listenerPos + earRight * earOffset;
                    bool leftLos = hasLineOfSight(baseSystem, leftEar, batch.sourcePositions[s], maxDistance);
                    bool rightLos = hasLineOfSight(baseSystem, rightEar, batch.sourcePositions[s], maxDistance);
                    directRatio = 0.5f * (static_cast<float>(leftLos) + static_cast<float>(rightLos));
                }
                float probeRatio = 0.0f;
                if (probeOffset > 0.0f) {
                    glm::vec3 rightProbe = batch.listenerPos + earRight * probeOffset;
                    glm::vec3 leftProbe = batch.listenerPos - earRight * probeOffset;
                    glm::vec3 forwardProbe = batch.listenerPos + earForward * probeOffset;
                    glm::vec3 backProbe = batch.listenerPos - earForward * probeOffset;
                    glm::vec3 upProbe = batch.listenerPos + earUp * probeOffset;
                    glm::vec3 downProbe = batch.listenerPos - earUp * probeOffset;
                    int probeHits = 0;
                    probeHits += hasLineOfSight(baseSystem, rightProbe, batch.sourcePositions[s], maxDistance) ? 1 : 0;
                    probeHits += hasLineOfSight(baseSystem, leftProbe, batch.sourcePositions[s], maxDistance) ? 1 : 0;
                    probeHits += hasLineOfSight(baseSystem, forwardProbe, batch.sourcePositions[s], maxDistance) ? 1 : 0;
                    probeHits += hasLineOfSight(baseSystem, backProbe, batch.sourcePositions[s], maxDistance) ? 1 : 0;
                    probeHits += hasLineOfSight(baseSystem, upProbe, batch.sourcePositions[s], maxDistance) ? 1 : 0;
                    probeHits += hasLineOfSight(baseSystem, downProbe, batch.sourcePositions[s], maxDistance) ? 1 : 0;
                    probeRatio = static_cast<float>(probeHits) / 6.0f;
                }
                AudioSourceState st;
                float greenRatio = (batch.accum[s].greenChecks > 0)
                    ? static_cast<float>(batch.accum[s].greenHits) / static_cast<float>(batch.accum[s].greenChecks)
                    : 0.0f;
                float orangeVisibleAvg = (batch.accum[s].orangeVisibleChecks > 0)
                    ? (batch.accum[s].orangeVisibleWallSum / static_cast<float>(batch.accum[s].orangeVisibleChecks))
                    : 0.0f;
                float orangeOccludedAvg = (batch.accum[s].orangeOccludedChecks > 0)
                    ? (batch.accum[s].orangeOccludedWallSum / static_cast<float>(batch.accum[s].orangeOccludedChecks))
                    : 0.0f;
                float orangeVisibleGain = std::exp(-0.05f * orangeVisibleAvg);
                float orangeOccludedGain = std::exp(-0.05f * orangeOccludedAvg);
                bool directVisible = probeRatio > 0.0f;
                float direct = probeRatio;
                const float leakScale = 0.5f;
                float openTerm = greenRatio * orangeVisibleGain;
                float leakTerm = (1.0f - greenRatio) * leakScale * orangeOccludedGain;
                float indirect = (1.0f - probeRatio) * (openTerm + leakTerm);
                float transmissionGain = std::clamp(direct + indirect, 0.0f, 1.0f);
                float occlusionMetric = transmissionGain;
                if (!directVisible) {
                    float diffractionFloor = std::clamp(escapeRatio * 0.3f, 0.0f, 0.3f);
                    transmissionGain = std::max(transmissionGain, diffractionFloor);
                }
                float dist = glm::length(batch.listenerPos - batch.sourcePositions[s]);
                const float refDist = 4.0f;
                const float falloffPower = 1.5f;
                float clampedDist = std::max(dist, refDist);
                float airLoss = std::pow(refDist / clampedDist, falloffPower);
                transmissionGain = std::clamp(transmissionGain * airLoss, 0.0f, 1.0f);

                st.distanceGain = transmissionGain;
                st.isOccluded = occlusionMetric < 0.2f;
                st.echoDelaySeconds = echoDelaySeconds;
                st.echoGain = echoGain;
                st.escapeRatio = escapeRatio;
                st.sourcePos = batch.sourcePositions[s];

                glm::vec3 newDir(0.0f);
                bool hasNewDir = false;
                bool useDirectDir = false;
                if (batch.accum[s].dirCount > 0) {
                    newDir = batch.accum[s].dirSum / static_cast<float>(batch.accum[s].dirCount);
                    float len = glm::length(newDir);
                    if (len > 1e-4f) {
                        newDir /= len;
                        hasNewDir = true;
                    } else {
                        newDir = glm::vec3(0.0f);
                    }
                }
                if (finalUpdate) {
                    if (directRatio > 0.0f) {
                        glm::vec3 direct = batch.sourcePositions[s] - batch.listenerPos;
                        float len = glm::length(direct);
                        if (len > 1e-4f) {
                            newDir = direct / len;
                            hasNewDir = true;
                            useDirectDir = true;
                        }
                    }
                }

                glm::vec3 outDir = newDir;
                auto prevIt = rtAudio.sourceStates.find(batch.sourceIds[s]);
                if (!finalUpdate) {
                    if (prevIt != rtAudio.sourceStates.end()) {
                        outDir = prevIt->second.direction;
                    }
                } else {
                    if (prevIt != rtAudio.sourceStates.end()) {
                        glm::vec3 oldDir = prevIt->second.direction;
                        if (glm::length(oldDir) < 1e-4f) {
                            oldDir = newDir;
                        }
                        if (hasNewDir) {
                            if (useDirectDir) {
                                outDir = newDir;
                            } else {
                                const float blend = 0.25f;
                                glm::vec3 mixed = glm::mix(oldDir, newDir, blend);
                                float len = glm::length(mixed);
                                if (len > 1e-4f) mixed /= len;
                                outDir = mixed;
                            }
                        } else {
                            outDir = oldDir;
                        }
                    }
                }
                st.direction = outDir;
                st.pan = 0.0f;
                newStates[batch.sourceIds[s]] = st;
            }
            rtAudio.sourceStates = std::move(newStates);
        };

        auto finalizeRay = [&](RayTraceRayState& ray) {
            if (ray.finished) return;
            ray.finished = true;
            batch.finishedRays += 1;
            if (ray.escaped) {
                batch.escapeCount += 1;
            } else if (ray.blueLenCount > 0) {
                batch.blueLenSum += ray.blueLenSum;
                batch.blueLenCount += ray.blueLenCount;
                batch.blueLenMax = std::max(batch.blueLenMax, ray.blueLenMax);
            }
            for (size_t s = 0; s < batch.sourcePositions.size(); ++s) {
                if (ray.hasLastDirPerSource[s]) {
                    batch.accum[s].dirSum += ray.lastDirPerSource[s];
                    batch.accum[s].dirCount += 1;
                }
            }
        };

        while (stepsThisFrame < bounceStepsPerFrame && batch.finishedRays < batch.totalRays && attempts < batch.totalRays) {
            int rayIndex = batch.nextRayIndex;
            batch.nextRayIndex = (batch.nextRayIndex + 1) % batch.totalRays;
            attempts += 1;
            RayTraceRayState& ray = batch.rays[rayIndex];
            if (ray.finished) continue;

            if (ray.bounceIndex >= maxBounces) {
                finalizeRay(ray);
                stepsThisFrame += 1;
                continue;
            }

            if (!ray.initDone) {
                for (size_t s = 0; s < batch.sourcePositions.size(); ++s) {
                    batch.accum[s].greenChecks += 1;
                    if (hasLineOfSight(baseSystem, ray.pos, batch.sourcePositions[s], maxDistance)) {
                        batch.accum[s].greenHits += 1;
                    }
                }
                ray.initDone = true;
            }

            glm::vec3 hitPos(0.0f);
            glm::vec3 hitNormal(0.0f);
            float hitDist = 0.0f;
            if (!traceToBlock(baseSystem, ray.pos, ray.dir, maxDistance, hitPos, hitNormal, hitDist)) {
                ray.escaped = true;
                finalizeRay(ray);
                stepsThisFrame += 1;
                continue;
            }

            if (ray.debug) {
                rtAudio.debugSegments.push_back({ray.pos, hitPos, whiteColor});
            }

            bool hasBlue = hasLineOfSight(baseSystem, hitPos, batch.listenerPos, maxDistance);
            glm::vec3 blueDir(0.0f);
            if (hasBlue) {
                float blueLen = glm::length(hitPos - batch.listenerPos);
                ray.blueLenSum += blueLen;
                ray.blueLenCount += 1;
                ray.blueLenMax = std::max(ray.blueLenMax, blueLen);
                blueDir = glm::normalize(hitPos - batch.listenerPos);
                if (ray.debug) {
                    rtAudio.debugSegments.push_back({hitPos, batch.listenerPos, blueColor});
                }
            }

            for (size_t s = 0; s < batch.sourcePositions.size(); ++s) {
                bool greenOk = hasLineOfSight(baseSystem, hitPos, batch.sourcePositions[s], maxDistance);
                batch.accum[s].greenChecks += 1;
                if (greenOk) {
                    batch.accum[s].greenHits += 1;
                    if (hasBlue) {
                        ray.lastDirPerSource[s] = blueDir;
                        ray.hasLastDirPerSource[s] = true;
                    }
                    if (ray.debug && s == 0) {
                        rtAudio.debugSegments.push_back({hitPos, batch.sourcePositions[s], greenColor});
                    }
                }

                float wallDistance = accumulateWallDistance(baseSystem, hitPos, batch.sourcePositions[s], maxDistance);
                if (greenOk) {
                    batch.accum[s].orangeVisibleChecks += 1;
                    batch.accum[s].orangeVisibleWallSum += wallDistance;
                } else {
                    batch.accum[s].orangeOccludedChecks += 1;
                    batch.accum[s].orangeOccludedWallSum += wallDistance;
                }
                if (ray.debug && s == 0) {
                    rtAudio.debugSegments.push_back({hitPos, batch.sourcePositions[s], orangeColor});
                }
            }

            ray.dir = glm::normalize(ray.dir - 2.0f * glm::dot(ray.dir, hitNormal) * hitNormal);
            ray.pos = hitPos + hitNormal * 0.02f;
            ray.bounceIndex += 1;
            stepsThisFrame += 1;
        }

        if (batch.finishedRays > 0 && batch.finishedRays != batch.lastPublishedFinishedRays) {
            emitStates(batch.finishedRays, false);
            batch.lastPublishedFinishedRays = batch.finishedRays;
        }

        bool batchComplete = batch.finishedRays >= batch.totalRays;
        if (batchComplete) {
            emitStates(batch.finishedRays, true);
            batch.active = false;
            rtAudio.lastBatchCompleteTime = now;
        }

        }

        updateHeadResponse();
        // Continue to mic pass even if the player batch is still in flight.

        if (!rtAudio.micCaptureActive || !rtAudio.micListenerValid) {
            rtAudio.micSourceStates.clear();
            rtAudio.micBatch.active = false;
            rtAudio.lastMicBatchCompleteTime = -1.0;
            return;
        }

        RayTraceBatch& micBatch = rtAudio.micBatch;
        glm::vec3 micPos = rtAudio.micListenerPos;
        glm::vec3 micForward = rtAudio.micListenerForward;
        if (glm::length(micForward) < 1e-4f) {
            micForward = glm::vec3(0.0f, 0.0f, -1.0f);
        } else {
            micForward = glm::normalize(micForward);
        }

        if (!micBatch.active) {
            const double kUpdateInterval = 0.4;
            if (rtAudio.lastMicBatchCompleteTime >= 0.0 && (now - rtAudio.lastMicBatchCompleteTime) < kUpdateInterval) {
                return;
            }
            if (!useFullCache) {
                if (!BlockSelectionSystemLogic::EnsureLocalCaches(baseSystem, prototypes, micPos, cacheRadius)) {
                    return;
                }
            }

            micBatch = RayTraceBatch{};
            micBatch.active = true;
            micBatch.debugActive = false;
            micBatch.totalRays = whiteRayCount;
            micBatch.listenerPos = micPos;

            glm::vec3 right = glm::normalize(glm::cross(micForward, glm::vec3(0.0f, 1.0f, 0.0f)));
            if (glm::length(right) < 1e-4f) right = glm::vec3(1.0f, 0.0f, 0.0f);
            micBatch.right = right;

            micBatch.sourceIds.reserve(sources.size());
            micBatch.sourcePositions.reserve(sources.size());
            for (const auto* src : sources) {
                micBatch.sourceIds.push_back(src->instanceID);
                micBatch.sourcePositions.push_back(src->position);
            }
            micBatch.accum.assign(sources.size(), {});
            micBatch.rays.resize(micBatch.totalRays);
            for (int rayIndex = 0; rayIndex < micBatch.totalRays; ++rayIndex) {
                RayTraceRayState& ray = micBatch.rays[rayIndex];
                ray.pos = micBatch.listenerPos;
                ray.dir = fibonacciSphereDirection(rayIndex, micBatch.totalRays);
                ray.bounceIndex = 0;
                ray.initDone = false;
                ray.finished = false;
                ray.escaped = false;
                ray.debug = false;
                ray.blueLenSum = 0.0f;
                ray.blueLenCount = 0;
                ray.blueLenMax = 0.0f;
                ray.lastDirPerSource.assign(micBatch.sourcePositions.size(), glm::vec3(0.0f));
                ray.hasLastDirPerSource.assign(micBatch.sourcePositions.size(), false);
            }
        }

        if (!micBatch.active) return;

        const int micBounceStepsPerFrame = 6;
        int micStepsThisFrame = 0;
        int micAttempts = 0;

        auto emitMicStates = [&](int finishedRaysCount, bool finalUpdate) {
            if (micBatch.sourceIds.empty()) return;
            float escapeRatio = (finishedRaysCount > 0)
                ? static_cast<float>(micBatch.escapeCount) / static_cast<float>(finishedRaysCount)
                : 0.0f;
            float avgBlueLen = (micBatch.blueLenCount > 0)
                ? (micBatch.blueLenSum / static_cast<float>(micBatch.blueLenCount))
                : 0.0f;

            const float metersPerBlock = 0.762f;
            const float speedOfSound = 343.0f;
            float echoDelaySeconds = (micBatch.blueLenMax > 0.0f)
                ? (micBatch.blueLenMax * metersPerBlock / speedOfSound)
                : 0.0f;

            float echoGain = 0.0f;
            if (avgBlueLen > 0.0f) {
                float intensity = avgBlueLen / (avgBlueLen + 6.0f);
                echoGain = std::clamp(intensity * (1.0f - escapeRatio), 0.0f, 1.0f);
            }

            glm::vec3 earForward = micForward;
            glm::vec3 earRight = glm::normalize(glm::cross(earForward, glm::vec3(0.0f, 1.0f, 0.0f)));
            if (glm::length(earRight) < 1e-4f) earRight = glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 earUp = glm::normalize(glm::cross(earRight, earForward));
            if (glm::length(earUp) < 1e-4f) earUp = glm::vec3(0.0f, 1.0f, 0.0f);
            const float earOffset = 0.6f;
            const float probeOffset = 0.4f;

            std::map<int, AudioSourceState> newStates;
            float sampleRate = baseSystem.audio ? baseSystem.audio->sampleRate : 0.0f;
            const float distCutoffMin = 7500.0f;
            const float distCutoffMax = 18000.0f;
            const float distStart = 4.0f;
            const float distEnd = 60.0f;

            for (size_t s = 0; s < micBatch.sourceIds.size(); ++s) {
                float directRatio = 0.0f;
                if (earOffset > 0.0f) {
                    glm::vec3 leftEar = micBatch.listenerPos - earRight * earOffset;
                    glm::vec3 rightEar = micBatch.listenerPos + earRight * earOffset;
                    bool leftLos = hasLineOfSight(baseSystem, leftEar, micBatch.sourcePositions[s], maxDistance);
                    bool rightLos = hasLineOfSight(baseSystem, rightEar, micBatch.sourcePositions[s], maxDistance);
                    directRatio = 0.5f * (static_cast<float>(leftLos) + static_cast<float>(rightLos));
                }
                float probeRatio = 0.0f;
                if (probeOffset > 0.0f) {
                    glm::vec3 rightProbe = micBatch.listenerPos + earRight * probeOffset;
                    glm::vec3 leftProbe = micBatch.listenerPos - earRight * probeOffset;
                    glm::vec3 forwardProbe = micBatch.listenerPos + earForward * probeOffset;
                    glm::vec3 backProbe = micBatch.listenerPos - earForward * probeOffset;
                    glm::vec3 upProbe = micBatch.listenerPos + earUp * probeOffset;
                    glm::vec3 downProbe = micBatch.listenerPos - earUp * probeOffset;
                    int probeHits = 0;
                    probeHits += hasLineOfSight(baseSystem, rightProbe, micBatch.sourcePositions[s], maxDistance) ? 1 : 0;
                    probeHits += hasLineOfSight(baseSystem, leftProbe, micBatch.sourcePositions[s], maxDistance) ? 1 : 0;
                    probeHits += hasLineOfSight(baseSystem, forwardProbe, micBatch.sourcePositions[s], maxDistance) ? 1 : 0;
                    probeHits += hasLineOfSight(baseSystem, backProbe, micBatch.sourcePositions[s], maxDistance) ? 1 : 0;
                    probeHits += hasLineOfSight(baseSystem, upProbe, micBatch.sourcePositions[s], maxDistance) ? 1 : 0;
                    probeHits += hasLineOfSight(baseSystem, downProbe, micBatch.sourcePositions[s], maxDistance) ? 1 : 0;
                    probeRatio = static_cast<float>(probeHits) / 6.0f;
                }
                AudioSourceState st;
                float greenRatio = (micBatch.accum[s].greenChecks > 0)
                    ? static_cast<float>(micBatch.accum[s].greenHits) / static_cast<float>(micBatch.accum[s].greenChecks)
                    : 0.0f;
                float orangeVisibleAvg = (micBatch.accum[s].orangeVisibleChecks > 0)
                    ? (micBatch.accum[s].orangeVisibleWallSum / static_cast<float>(micBatch.accum[s].orangeVisibleChecks))
                    : 0.0f;
                float orangeOccludedAvg = (micBatch.accum[s].orangeOccludedChecks > 0)
                    ? (micBatch.accum[s].orangeOccludedWallSum / static_cast<float>(micBatch.accum[s].orangeOccludedChecks))
                    : 0.0f;
                float orangeVisibleGain = std::exp(-0.05f * orangeVisibleAvg);
                float orangeOccludedGain = std::exp(-0.05f * orangeOccludedAvg);
                bool directVisible = probeRatio > 0.0f;
                float direct = probeRatio;
                const float leakScale = 0.5f;
                float openTerm = greenRatio * orangeVisibleGain;
                float leakTerm = (1.0f - greenRatio) * leakScale * orangeOccludedGain;
                float indirect = (1.0f - probeRatio) * (openTerm + leakTerm);
                float transmissionGain = std::clamp(direct + indirect, 0.0f, 1.0f);
                float occlusionMetric = transmissionGain;
                if (!directVisible) {
                    float diffractionFloor = std::clamp(escapeRatio * 0.3f, 0.0f, 0.3f);
                    transmissionGain = std::max(transmissionGain, diffractionFloor);
                }
                float dist = glm::length(micBatch.listenerPos - micBatch.sourcePositions[s]);
                const float refDist = 4.0f;
                const float falloffPower = 1.5f;
                float clampedDist = std::max(dist, refDist);
                float airLoss = std::pow(refDist / clampedDist, falloffPower);
                transmissionGain = std::clamp(transmissionGain * airLoss, 0.0f, 1.0f);

                st.isOccluded = occlusionMetric < 0.2f;
                st.echoDelaySeconds = echoDelaySeconds;
                st.echoGain = echoGain;
                st.escapeRatio = escapeRatio;
                st.sourcePos = micBatch.sourcePositions[s];

                glm::vec3 newDir(0.0f);
                bool hasNewDir = false;
                bool useDirectDir = false;
                if (micBatch.accum[s].dirCount > 0) {
                    newDir = micBatch.accum[s].dirSum / static_cast<float>(micBatch.accum[s].dirCount);
                    float len = glm::length(newDir);
                    if (len > 1e-4f) {
                        newDir /= len;
                        hasNewDir = true;
                    } else {
                        newDir = glm::vec3(0.0f);
                    }
                }
                if (finalUpdate) {
                    if (directRatio > 0.0f) {
                        glm::vec3 directVec = micBatch.sourcePositions[s] - micBatch.listenerPos;
                        float len = glm::length(directVec);
                        if (len > 1e-4f) {
                            newDir = directVec / len;
                            hasNewDir = true;
                            useDirectDir = true;
                        }
                    }
                }

                glm::vec3 outDir = newDir;
                auto prevIt = rtAudio.micSourceStates.find(micBatch.sourceIds[s]);
                if (!finalUpdate) {
                    if (prevIt != rtAudio.micSourceStates.end()) {
                        outDir = prevIt->second.direction;
                    }
                } else {
                    if (prevIt != rtAudio.micSourceStates.end()) {
                        glm::vec3 oldDir = prevIt->second.direction;
                        if (glm::length(oldDir) < 1e-4f) {
                            oldDir = newDir;
                        }
                        if (hasNewDir) {
                            if (useDirectDir) {
                                outDir = newDir;
                            } else {
                                const float blend = 0.25f;
                                glm::vec3 mixed = glm::mix(oldDir, newDir, blend);
                                float len = glm::length(mixed);
                                if (len > 1e-4f) mixed /= len;
                                outDir = mixed;
                            }
                        } else {
                            outDir = oldDir;
                        }
                    }
                }
                glm::vec3 dirForMic = outDir;
                if (glm::length(dirForMic) < 1e-4f) {
                    dirForMic = micBatch.sourcePositions[s] - micBatch.listenerPos;
                }
                float dirLen = glm::length(dirForMic);
                if (dirLen > 1e-4f) {
                    dirForMic /= dirLen;
                    float dotForward = glm::dot(dirForMic, earForward);
                    float cardioid = std::clamp(0.5f * (1.0f + dotForward), 0.0f, 1.0f);
                    transmissionGain *= cardioid;
                }
                st.distanceGain = transmissionGain;
                st.direction = outDir;
                st.pan = 0.0f;
                if (sampleRate > 0.0f) {
                    float distT = 0.0f;
                    if (distEnd > distStart) {
                        distT = std::clamp((dist - distStart) / (distEnd - distStart), 0.0f, 1.0f);
                    }
                    float distCutoff = distCutoffMax * std::pow(distCutoffMin / distCutoffMax, distT);
                    float alpha = 1.0f - std::exp(-2.0f * 3.14159265359f * distCutoff / sampleRate);
                    st.hfAlpha = std::clamp(alpha, 0.0f, 1.0f);
                } else {
                    st.hfAlpha = 0.0f;
                }
                newStates[micBatch.sourceIds[s]] = st;
            }
            rtAudio.micSourceStates = std::move(newStates);
        };

        auto finalizeMicRay = [&](RayTraceRayState& ray) {
            if (ray.finished) return;
            ray.finished = true;
            micBatch.finishedRays += 1;
            if (ray.escaped) {
                micBatch.escapeCount += 1;
            } else if (ray.blueLenCount > 0) {
                micBatch.blueLenSum += ray.blueLenSum;
                micBatch.blueLenCount += ray.blueLenCount;
                micBatch.blueLenMax = std::max(micBatch.blueLenMax, ray.blueLenMax);
            }
            for (size_t s = 0; s < micBatch.sourcePositions.size(); ++s) {
                if (ray.hasLastDirPerSource[s]) {
                    micBatch.accum[s].dirSum += ray.lastDirPerSource[s];
                    micBatch.accum[s].dirCount += 1;
                }
            }
        };

        while (micStepsThisFrame < micBounceStepsPerFrame &&
               micBatch.finishedRays < micBatch.totalRays &&
               micAttempts < micBatch.totalRays) {
            int rayIndex = micBatch.nextRayIndex;
            micBatch.nextRayIndex = (micBatch.nextRayIndex + 1) % micBatch.totalRays;
            micAttempts += 1;
            RayTraceRayState& ray = micBatch.rays[rayIndex];
            if (ray.finished) continue;

            if (ray.bounceIndex >= maxBounces) {
                finalizeMicRay(ray);
                micStepsThisFrame += 1;
                continue;
            }

            if (!ray.initDone) {
                for (size_t s = 0; s < micBatch.sourcePositions.size(); ++s) {
                    micBatch.accum[s].greenChecks += 1;
                    if (hasLineOfSight(baseSystem, ray.pos, micBatch.sourcePositions[s], maxDistance)) {
                        micBatch.accum[s].greenHits += 1;
                    }
                }
                ray.initDone = true;
            }

            glm::vec3 hitPos(0.0f);
            glm::vec3 hitNormal(0.0f);
            float hitDist = 0.0f;
            if (!traceToBlock(baseSystem, ray.pos, ray.dir, maxDistance, hitPos, hitNormal, hitDist)) {
                ray.escaped = true;
                finalizeMicRay(ray);
                micStepsThisFrame += 1;
                continue;
            }

            bool hasBlue = hasLineOfSight(baseSystem, hitPos, micBatch.listenerPos, maxDistance);
            glm::vec3 blueDir(0.0f);
            if (hasBlue) {
                float blueLen = glm::length(hitPos - micBatch.listenerPos);
                ray.blueLenSum += blueLen;
                ray.blueLenCount += 1;
                ray.blueLenMax = std::max(ray.blueLenMax, blueLen);
                blueDir = glm::normalize(hitPos - micBatch.listenerPos);
            }

            for (size_t s = 0; s < micBatch.sourcePositions.size(); ++s) {
                bool greenOk = hasLineOfSight(baseSystem, hitPos, micBatch.sourcePositions[s], maxDistance);
                micBatch.accum[s].greenChecks += 1;
                if (greenOk) {
                    micBatch.accum[s].greenHits += 1;
                    if (hasBlue) {
                        ray.lastDirPerSource[s] = blueDir;
                        ray.hasLastDirPerSource[s] = true;
                    }
                }

                float wallDistance = accumulateWallDistance(baseSystem, hitPos, micBatch.sourcePositions[s], maxDistance);
                if (greenOk) {
                    micBatch.accum[s].orangeVisibleChecks += 1;
                    micBatch.accum[s].orangeVisibleWallSum += wallDistance;
                } else {
                    micBatch.accum[s].orangeOccludedChecks += 1;
                    micBatch.accum[s].orangeOccludedWallSum += wallDistance;
                }
            }

            ray.dir = glm::normalize(ray.dir - 2.0f * glm::dot(ray.dir, hitNormal) * hitNormal);
            ray.pos = hitPos + hitNormal * 0.02f;
            ray.bounceIndex += 1;
            micStepsThisFrame += 1;
        }

        if (micBatch.finishedRays > 0 && micBatch.finishedRays != micBatch.lastPublishedFinishedRays) {
            emitMicStates(micBatch.finishedRays, false);
            micBatch.lastPublishedFinishedRays = micBatch.finishedRays;
        }

        bool micBatchComplete = micBatch.finishedRays >= micBatch.totalRays;
        if (micBatchComplete) {
            emitMicStates(micBatch.finishedRays, true);
            micBatch.active = false;
            rtAudio.lastMicBatchCompleteTime = now;
        }
    }
}
