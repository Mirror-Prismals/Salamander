#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <map>
#include <limits>

// Forward Declarations
struct BaseSystem;
struct Entity;
struct EntityInstance;
struct RayTracedAudioContext;
struct PlayerContext;

namespace RayTracedAudioSystemLogic {

    bool rayIntersectsAABB(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& boxMin, const glm::vec3& boxMax, float& t) {
        glm::vec3 invDir = 1.0f / rayDir;
        glm::vec3 tMin = (boxMin - rayOrigin) * invDir;
        glm::vec3 tMax = (boxMax - rayOrigin) * invDir;

        glm::vec3 t1 = glm::min(tMin, tMax);
        glm::vec3 t2 = glm::max(tMin, tMax);

        float tNear = glm::max(glm::max(t1.x, t1.y), t1.z);
        float tFar = glm::min(glm::min(t2.x, t2.y), t2.z);

        if (tNear < tFar && tFar > 0.0f) {
            t = tNear;
            return true;
        }
        return false;
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

        for (const auto& sourceInstance : worldEntity->instances) {
            if (sourceInstance.prototypeID == visualizerProtoID) {
                AudioSourceState currentState;
                glm::vec3 listenerPos = player.cameraPosition;
                glm::vec3 sourcePos = sourceInstance.position;
                
                // --- 1. Calculate Distance Attenuation ---
                float distance = glm::distance(listenerPos, sourcePos);
                float max_audible_distance = 50.0f;
                currentState.distanceGain = glm::clamp(1.0f - (distance / max_audible_distance), 0.0f, 1.0f);

                // --- 2. Calculate Occlusion ---
                glm::vec3 rayDir = glm::normalize(sourcePos - listenerPos);
                float distanceToSource = distance;
                bool occluded = false;

                for (const auto& occluderInstance : worldEntity->instances) {
                    if (occluderInstance.instanceID == sourceInstance.instanceID) continue;
                    const Entity& proto = prototypes[occluderInstance.prototypeID];

                    if (proto.isOccluder) {
                        glm::vec3 boxMin = occluderInstance.position - 0.5f;
                        glm::vec3 boxMax = occluderInstance.position + 0.5f;
                        float t; // Intersection distance
                        if (rayIntersectsAABB(listenerPos, rayDir, boxMin, boxMax, t)) {
                            if (t < distanceToSource) {
                                occluded = true;
                                break;
                            }
                        }
                    }
                }
                currentState.isOccluded = occluded;
                
                // Store the final calculated state
                rtAudio.sourceStates[sourceInstance.instanceID] = currentState;
            }
        }
    }
}
