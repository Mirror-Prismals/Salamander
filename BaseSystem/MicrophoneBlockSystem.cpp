#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <vector>

// Forward declarations
struct BaseSystem;
struct Entity;
struct EntityInstance;
struct LevelContext;
struct PlayerContext;
struct RayTracedAudioContext;
struct UIContext;
struct ChunkContext;

namespace BlockSelectionSystemLogic {
    bool EnsureLocalCaches(BaseSystem& baseSystem,
                           const std::vector<Entity>& prototypes,
                           const glm::vec3& cameraPosition,
                           int radius);
    void EnsureAllCaches(BaseSystem& baseSystem,
                         const std::vector<Entity>& prototypes);
}

namespace RayTracedAudioSystemLogic {
    EntityInstance* findInstanceById(LevelContext& level, int worldIndex, int instanceId);
    void buildSourceCache(BaseSystem& baseSystem,
                          int visualizerProtoID,
                          RayTracedAudioContext& rtAudio);
    void InitializeRayBatch(RayTraceBatch& batch,
                            const glm::vec3& listenerPos,
                            const glm::vec3& right,
                            const std::vector<EntityInstance*>& sources,
                            int totalRays,
                            bool debugActive,
                            int debugRayCount);
    void EmitRayStates(BaseSystem& baseSystem,
                       RayTraceBatch& batch,
                       float maxDistance,
                       const glm::vec3& earForward,
                       const glm::vec3& earRight,
                       const glm::vec3& earUp,
                       std::map<int, AudioSourceState>& states,
                       int& statesVersion,
                       int finishedRaysCount,
                       bool finalUpdate);
    void StepRayBatch(BaseSystem& baseSystem,
                      RayTraceBatch& batch,
                      float maxDistance,
                      int maxBounces,
                      int bounceStepsPerFrame,
                      std::vector<RayDebugSegment>* debugSegments,
                      const glm::vec3& whiteColor,
                      const glm::vec3& blueColor,
                      const glm::vec3& greenColor,
                      const glm::vec3& orangeColor);
}

namespace MicrophoneBlockSystemLogic {

    namespace {
        constexpr float kMatchEpsilon = 0.15f;

        glm::vec3 forwardFromYaw(float yawDegrees) {
            float yawRad = glm::radians(yawDegrees);
            glm::vec3 forward(std::cos(yawRad), 0.0f, std::sin(yawRad));
            if (glm::length(forward) < 1e-4f) {
                return glm::vec3(0.0f, 0.0f, -1.0f);
            }
            return glm::normalize(forward);
        }

        glm::vec3 forwardFromView(const PlayerContext& player) {
            glm::vec3 forward;
            forward.x = std::cos(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
            forward.y = std::sin(glm::radians(player.cameraPitch));
            forward.z = std::sin(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
            if (glm::length(forward) < 1e-4f) {
                return glm::vec3(0.0f, 0.0f, -1.0f);
            }
            return glm::normalize(forward);
        }

        void processMicRayTracing(BaseSystem& baseSystem, std::vector<Entity>& prototypes) {
            if (!baseSystem.rayTracedAudio || !baseSystem.level || baseSystem.level->worlds.empty()) return;

            RayTracedAudioContext& rtAudio = *baseSystem.rayTracedAudio;
            if (!rtAudio.micCaptureActive || !rtAudio.micListenerValid) {
                rtAudio.micSourceStates.clear();
                rtAudio.micBatch.active = false;
                rtAudio.lastMicBatchCompleteTime = -1.0;
                return;
            }

            double now = glfwGetTime();

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
                RayTracedAudioSystemLogic::buildSourceCache(baseSystem, visualizerProtoID, rtAudio);
            }
            bool cacheValid = true;
            for (const auto& ref : rtAudio.sourceInstances) {
                EntityInstance* inst = RayTracedAudioSystemLogic::findInstanceById(level, ref.first, ref.second);
                if (!inst) { cacheValid = false; continue; }
                if (inst->prototypeID != visualizerProtoID) continue;
                sources.push_back(inst);
            }
            if (!cacheValid) {
                rtAudio.sourceCacheBuilt = false;
            }
            if (sources.empty()) {
                rtAudio.micSourceStates.clear();
                return;
            }

            bool useFullCache = rtAudio.micCaptureActive;
            if (useFullCache) {
                if (rtAudio.lastCacheEnsureFrame != baseSystem.frameIndex) {
                    BlockSelectionSystemLogic::EnsureAllCaches(baseSystem, prototypes);
                    rtAudio.lastCacheEnsureFrame = baseSystem.frameIndex;
                }
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

                glm::vec3 right = glm::normalize(glm::cross(micForward, glm::vec3(0.0f, 1.0f, 0.0f)));
                if (glm::length(right) < 1e-4f) right = glm::vec3(1.0f, 0.0f, 0.0f);
                RayTracedAudioSystemLogic::InitializeRayBatch(micBatch,
                                                              micPos,
                                                              right,
                                                              sources,
                                                              whiteRayCount,
                                                              false,
                                                              0);
            }

            if (!micBatch.active) return;

            const int micBounceStepsPerFrame = 6;
            const glm::vec3 dummyColor(0.0f);
            RayTracedAudioSystemLogic::StepRayBatch(baseSystem,
                                                    micBatch,
                                                    maxDistance,
                                                    maxBounces,
                                                    micBounceStepsPerFrame,
                                                    nullptr,
                                                    dummyColor,
                                                    dummyColor,
                                                    dummyColor,
                                                    dummyColor);

            auto emitMicStates = [&](int finishedRaysCount, bool finalUpdate) {
                glm::vec3 earForward = micForward;
                glm::vec3 earRight = glm::normalize(glm::cross(earForward, glm::vec3(0.0f, 1.0f, 0.0f)));
                if (glm::length(earRight) < 1e-4f) earRight = glm::vec3(1.0f, 0.0f, 0.0f);
                glm::vec3 earUp = glm::normalize(glm::cross(earRight, earForward));
                if (glm::length(earUp) < 1e-4f) earUp = glm::vec3(0.0f, 1.0f, 0.0f);

                RayTracedAudioSystemLogic::EmitRayStates(baseSystem,
                                                         micBatch,
                                                         maxDistance,
                                                         earForward,
                                                         earRight,
                                                         earUp,
                                                         rtAudio.micSourceStates,
                                                         rtAudio.micStatesVersion,
                                                         finishedRaysCount,
                                                         finalUpdate);
            };

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

    void UpdateMicrophoneBlocks(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt;
        (void)win;
        if (!baseSystem.level || !baseSystem.rayTracedAudio) return;

        RayTracedAudioContext& rtAudio = *baseSystem.rayTracedAudio;
        rtAudio.microphones.clear();
        rtAudio.micListenerValid = false;

        int micProtoID = -1;
        for (const auto& proto : prototypes) {
            if (proto.name == "Microphone") {
                micProtoID = proto.prototypeID;
                break;
            }
        }
        if (micProtoID >= 0) {
            bool uiActive = baseSystem.ui && baseSystem.ui->active;
            if (baseSystem.player && !uiActive) {
                PlayerContext& player = *baseSystem.player;
                if (player.leftMousePressed && player.hasBlockTarget && !player.isHoldingBlock) {
                    int targetWorld = player.targetedWorldIndex;
                    if (targetWorld >= 0 && baseSystem.level) {
                        LevelContext& level = *baseSystem.level;
                        if (targetWorld < static_cast<int>(level.worlds.size())) {
                            Entity& world = level.worlds[targetWorld];
                            for (auto& inst : world.instances) {
                                if (inst.prototypeID != micProtoID) continue;
                                if (glm::distance(inst.position, player.targetedBlockPosition) > kMatchEpsilon) continue;
                                glm::vec3 forward = forwardFromView(player);
                                rtAudio.microphoneDirections[inst.instanceID] = forward;
                                rtAudio.micActiveInstanceID = inst.instanceID;
                                break;
                            }
                        }
                    }
                }
            }

            LevelContext& level = *baseSystem.level;
            for (size_t wi = 0; wi < level.worlds.size(); ++wi) {
                Entity& world = level.worlds[wi];
                for (auto& inst : world.instances) {
                    if (inst.prototypeID != micProtoID) continue;
                    glm::vec3 forward = glm::vec3(0.0f, 0.0f, -1.0f);
                    auto it = rtAudio.microphoneDirections.find(inst.instanceID);
                    if (it != rtAudio.microphoneDirections.end()) {
                        forward = it->second;
                    } else {
                        forward = forwardFromYaw(inst.rotation);
                        rtAudio.microphoneDirections[inst.instanceID] = forward;
                    }
                    rtAudio.microphones.push_back({
                        static_cast<int>(wi),
                        inst.instanceID,
                        inst.position,
                        forward
                    });
                }
            }
        } else {
            rtAudio.micActiveInstanceID = -1;
        }

        if (!rtAudio.microphones.empty()) {
            MicrophoneInstance* chosen = nullptr;
            if (rtAudio.micActiveInstanceID >= 0) {
                for (auto& mic : rtAudio.microphones) {
                    if (mic.instanceID == rtAudio.micActiveInstanceID) {
                        chosen = &mic;
                        break;
                    }
                }
            }
            if (!chosen) {
                chosen = &rtAudio.microphones.front();
            }
            rtAudio.micListenerValid = true;
            rtAudio.micListenerPos = chosen->position;
            rtAudio.micListenerForward = chosen->forward;
            rtAudio.micActiveInstanceID = chosen->instanceID;
        } else {
            rtAudio.micActiveInstanceID = -1;
        }

        processMicRayTracing(baseSystem, prototypes);
    }
}
