#pragma once

#include <GLFW/glfw3.h>
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
        rtAudio.debugRays.clear();
        double now = glfwGetTime();
        if (rtAudio.lastHeatmapTime < 0.0) rtAudio.lastHeatmapTime = now;
        bool rebuildHeatmap = (now - rtAudio.lastHeatmapTime) >= 5.0; // only refresh every ~5s
        if (rebuildHeatmap) {
            rtAudio.debugVoxels.clear();
            rtAudio.lastHeatmapTime = now;
        }

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

        auto traceGain = [&](const glm::vec3& start, const glm::vec3& end, float minGain, float& outGain, bool& outOcc) {
            glm::vec3 rayDir = end - start;
            float rayLenSq = glm::dot(rayDir, rayDir);
            if (rayLenSq < 1e-6f) { outGain = 1.0f; outOcc = false; return; }
            float rayLen = sqrt(rayLenSq);
            rayDir /= rayLen;

            glm::ivec3 currentVoxel = glm::floor(start);
            glm::vec3 step = glm::sign(rayDir);
            glm::vec3 nextBoundary = glm::vec3(currentVoxel) + 0.5f + step * 0.5f;
            glm::vec3 tMax = (nextBoundary - start) / rayDir;
            glm::vec3 tDelta = glm::abs(1.0f / rayDir);

            float totalDamping = 0.0f;
            float distanceTraveled = 0.0f;
            int maxSteps = 256;
            bool encounteredOcc = false;
            glm::ivec3 sourceCell = glm::ivec3(glm::round(start));

            while (distanceTraveled < rayLen && maxSteps > 0) {
                float lastDistanceTraveled = distanceTraveled;
                --maxSteps;

                if (tMax.x < tMax.y) {
                    if (tMax.x < tMax.z) { currentVoxel.x += step.x; distanceTraveled = tMax.x; tMax.x += tDelta.x; }
                    else { currentVoxel.z += step.z; distanceTraveled = tMax.z; tMax.z += tDelta.z; }
                } else {
                    if (tMax.y < tMax.z) { currentVoxel.y += step.y; distanceTraveled = tMax.y; tMax.y += tDelta.y; }
                    else { currentVoxel.z += step.z; distanceTraveled = tMax.z; tMax.z += tDelta.z; }
                }

                float stepLength = distanceTraveled - lastDistanceTraveled;
                if (distanceTraveled >= rayLen) break;

                float damping = 0.0f;
                if (currentVoxel == sourceCell) {
                    damping = 0.0f;
                } else if (BlockSelectionSystemLogic::SampleBlockDamping(baseSystem, currentVoxel, damping)) {
                    totalDamping += damping * stepLength;
                    encounteredOcc = true;
                } else {
                    totalDamping += 0.25f * stepLength;
                }
            }

            outGain = exp(-0.05f * totalDamping);
            outOcc = encounteredOcc;
            if (outGain < minGain) outGain = 0.0f;
        };

        // 1) Listener-to-source trace for actual audio behavior.
        for (const auto* sourceInstance : sources) {
            if (!sourceInstance) continue;
            float gain = 1.0f; bool occ = false;
            traceGain(player.cameraPosition, sourceInstance->position, 0.0f, gain, occ);
            AudioSourceState st; st.distanceGain = gain; st.isOccluded = occ;
            rtAudio.sourceStates[sourceInstance->instanceID] = st;
        }

        // 2) Heatmap lattice around each source (viewer-independent).
        const size_t maxDebugVoxels = 400;
        const float minGain = 0.02f;
        const std::vector<int> radii = {4, 12, 20};
        const std::vector<int> yOffsets = {-4, 0, 4};
        const int step = 4;

        if (rebuildHeatmap) for (const auto* sourceInstance : sources) {
            if (!sourceInstance) continue;
            glm::vec3 sourcePos = sourceInstance->position;
            for (int yoff : yOffsets) {
                if (rtAudio.debugVoxels.size() >= maxDebugVoxels) break;
                for (int r : radii) {
                    if (rtAudio.debugVoxels.size() >= maxDebugVoxels) break;
                    // Perimeter points of the square ring at radius r
                    for (int x = -r; x <= r; x += step) {
                        if (rtAudio.debugVoxels.size() >= maxDebugVoxels) break;
                        for (int zSign : {-1, 1}) {
                            glm::vec3 target = sourcePos + glm::vec3(x, yoff, zSign * r);
                            float g; bool occ;
                            traceGain(sourcePos, target, minGain, g, occ);
                            if (g <= 0.0f) continue;
                            RayDebugVoxel vox; vox.pos = target; vox.gain = g; vox.occluded = occ;
                            rtAudio.debugVoxels.push_back(vox);
                        }
                    }
                    for (int z = -r + step; z <= r - step; z += step) {
                        if (rtAudio.debugVoxels.size() >= maxDebugVoxels) break;
                        for (int xSign : {-1, 1}) {
                            glm::vec3 target = sourcePos + glm::vec3(xSign * r, yoff, z);
                            float g; bool occ;
                            traceGain(sourcePos, target, minGain, g, occ);
                            if (g <= 0.0f) continue;
                            RayDebugVoxel vox; vox.pos = target; vox.gain = g; vox.occluded = occ;
                            rtAudio.debugVoxels.push_back(vox);
                        }
                    }
                }
            }
            if (rtAudio.debugVoxels.size() >= maxDebugVoxels) break;
        }
    }
}
