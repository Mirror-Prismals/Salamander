#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <map>

// Forward Declarations
struct BaseSystem;
struct Entity;
struct EntityInstance;
struct RayTracedAudioContext;
struct PlayerContext;

namespace RayTracedAudioSystemLogic {

    // Helper to get an instance at a specific grid location
    const EntityInstance* getInstanceAt(const glm::ivec3& pos, const BaseSystem& baseSystem, const std::vector<Entity>& prototypes, const Entity* worldEntity) {
        for (const auto& instance : worldEntity->instances) {
            glm::ivec3 instancePos = glm::round(instance.position);
            if (instancePos == pos) {
                return &instance;
            }
        }
        return nullptr;
    }

    void ProcessRayTracedAudio(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.rayTracedAudio || !baseSystem.world || !baseSystem.player) return;

        RayTracedAudioContext& rtAudio = *baseSystem.rayTracedAudio;
        PlayerContext& player = *baseSystem.player;
        
        Entity* worldEntity = nullptr;
        for (auto& proto : prototypes) { if (proto.isWorld) { worldEntity = &proto; break; } }
        if (!worldEntity) return;

        rtAudio.sourceStates.clear();

        int visualizerProtoID = -1;
        for (const auto& proto : prototypes) {
            if (proto.name == "AudioVisualizer") {
                visualizerProtoID = proto.prototypeID;
                break;
            }
        }
        if (visualizerProtoID == -1) return;

        // --- Ray Marching for each sound source ---
        for (const auto& sourceInstance : worldEntity->instances) {
            if (sourceInstance.prototypeID == visualizerProtoID) {
                AudioSourceState currentState;
                glm::vec3 listenerPos = player.cameraPosition;
                glm::vec3 sourcePos = sourceInstance.position;
                glm::vec3 rayDir = glm::normalize(sourcePos - listenerPos);

                // DDA Ray Marching setup
                glm::ivec3 currentVoxel = glm::floor(listenerPos);
                glm::vec3 step = glm::sign(rayDir);
                glm::vec3 nextBoundary = glm::vec3(currentVoxel) + 0.5f + step * 0.5f;
                glm::vec3 tMax = (nextBoundary - listenerPos) / rayDir;
                glm::vec3 tDelta = glm::abs(1.0f / rayDir);

                float totalDamping = 0.0f;
                float maxDistance = glm::distance(listenerPos, sourcePos);
                float distanceTraveled = 0;

                // March along the ray
                while (distanceTraveled < maxDistance) {
                    // Add a small amount of randomness to the ray direction
                    float randomness = 0.001f;
                    glm::vec3 randomVec = glm::vec3(
                        ((float)rand() / RAND_MAX - 0.5f) * randomness,
                        ((float)rand() / RAND_MAX - 0.5f) * randomness,
                        ((float)rand() / RAND_MAX - 0.5f) * randomness
                    );
                    rayDir = glm::normalize(rayDir + randomVec);

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

                    const EntityInstance* hitInstance = getInstanceAt(currentVoxel, baseSystem, prototypes, worldEntity);
                    if (hitInstance) {
                        const Entity& proto = prototypes[hitInstance->prototypeID];
                        totalDamping += proto.dampingFactor * stepLength;
                    } else {
                        // Air block
                        totalDamping += 0.25f * stepLength;
                    }
                }

                // Convert total damping to a gain multiplier.
                // Using exp() gives a more natural-sounding exponential falloff.
                currentState.distanceGain = exp(-0.05f * totalDamping);
                
                rtAudio.sourceStates[sourceInstance.instanceID] = currentState;
            }
        }
    }
}
