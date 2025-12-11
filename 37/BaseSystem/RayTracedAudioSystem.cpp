#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <map>
#include <cmath>

// Forward Declarations
struct BaseSystem;
struct Entity;
struct EntityInstance;
struct RayTracedAudioContext;
struct PlayerContext;

namespace RayTracedAudioSystemLogic {

    void ProcessRayTracedAudio(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.rayTracedAudio || !baseSystem.world || !baseSystem.player || !baseSystem.level || baseSystem.level->worlds.empty()) return;

        RayTracedAudioContext& rtAudio = *baseSystem.rayTracedAudio;
        PlayerContext& player = *baseSystem.player;

        rtAudio.sourceStates.clear();

        int visualizerProtoID = -1;
        for (const auto& proto : prototypes) {
            if (proto.name == "AudioVisualizer") {
                visualizerProtoID = proto.prototypeID;
                break;
            }
        }
        if (visualizerProtoID == -1) return;

        LevelContext& level = *baseSystem.level;
        BlockSelectionSystemLogic::EnsureAllCaches(baseSystem, prototypes);

        std::vector<EntityInstance*> sources;
        for (auto& world : level.worlds) {
            for (auto& instance : world.instances) {
                if (instance.prototypeID == visualizerProtoID) {
                    sources.push_back(&instance);
                }
            }
        }

        if (sources.empty()) return;

        // --- Ray Marching for each sound source ---
        for (const auto* sourceInstance : sources) {
            if (sourceInstance && sourceInstance->prototypeID == visualizerProtoID) {
                AudioSourceState currentState;
                glm::vec3 listenerPos = player.cameraPosition;
                glm::vec3 sourcePos = sourceInstance->position;
                glm::ivec3 sourceCell = glm::ivec3(glm::round(sourcePos));
                glm::vec3 rayDir = sourcePos - listenerPos;
                float rayLengthSq = glm::dot(rayDir, rayDir);
                if (rayLengthSq < 1e-6f) {
                    currentState.distanceGain = 1.0f;
                    rtAudio.sourceStates[sourceInstance->instanceID] = currentState;
                    continue;
                }
                rayDir = glm::normalize(rayDir);

                // DDA Ray Marching setup
                glm::ivec3 currentVoxel = glm::floor(listenerPos);
                glm::vec3 step = glm::sign(rayDir);
                glm::vec3 nextBoundary = glm::vec3(currentVoxel) + 0.5f + step * 0.5f;
                glm::vec3 tMax = (nextBoundary - listenerPos) / rayDir;
                glm::vec3 tDelta = glm::abs(1.0f / rayDir);

                float totalDamping = 0.0f;
                float maxDistance = glm::distance(listenerPos, sourcePos);
                float distanceTraveled = 0;
                bool encounteredOccluder = false;

                // March along the ray
                while (distanceTraveled < maxDistance) {
                    float lastDistanceTraveled = distanceTraveled;

                    // Advance to the next voxel
                    if (tMax.x < tMax.y) {
                        if (tMax.x < tMax.z) {
                            currentVoxel.x += step.x;
                            distanceTraveled = tMax.x;
                            tMax.x += tDelta.x;
                        } else {
                            currentVoxel.z += step.z;
                            distanceTraveled = tMax.z;
                            tMax.z += tDelta.z;
                        }
                    } else {
                        if (tMax.y < tMax.z) {
                            currentVoxel.y += step.y;
                            distanceTraveled = tMax.y;
                            tMax.y += tDelta.y;
                        } else {
                            currentVoxel.z += step.z;
                            distanceTraveled = tMax.z;
                            tMax.z += tDelta.z;
                        }
                    }

                    float stepLength = distanceTraveled - lastDistanceTraveled;
                    if (distanceTraveled >= maxDistance) break;

                    float damping = 0.0f;
                    if (currentVoxel == sourceCell) {
                        // Do not treat the source itself as an occluder.
                        damping = 0.0f;
                    } else if (BlockSelectionSystemLogic::SampleBlockDamping(baseSystem, currentVoxel, damping)) {
                        totalDamping += damping * stepLength;
                        encounteredOccluder = true;
                    } else {
                        // Air block
                        totalDamping += 0.25f * stepLength;
                    }
                }

                // Convert total damping to a gain multiplier.
                // Using exp() gives a more natural-sounding exponential falloff.
                currentState.distanceGain = exp(-0.05f * totalDamping);
                currentState.isOccluded = encounteredOccluder;
                
                rtAudio.sourceStates[sourceInstance->instanceID] = currentState;
            }
        }
    }
}
