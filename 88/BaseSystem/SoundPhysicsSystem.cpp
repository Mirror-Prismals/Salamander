#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

// Forward declarations
struct BaseSystem;
struct Entity;
struct RayTracedAudioContext;
struct PlayerContext;
struct AudioContext;

namespace SoundPhysicsSystemLogic {

    namespace {
        void applyDistanceFalloff(std::map<int, AudioSourceState>& states, const glm::vec3& listenerPos) {
            const float refDist = 4.0f;
            const float falloffPower = 1.5f;
            for (auto& [id, st] : states) {
                float dist = glm::length(listenerPos - st.sourcePos);
                float clampedDist = std::max(dist, refDist);
                float airLoss = std::pow(refDist / clampedDist, falloffPower);
                st.distanceGain = std::clamp(st.preFalloffGain * airLoss, 0.0f, 1.0f);
            }
        }

        void applyMicCardioid(std::map<int, AudioSourceState>& states,
                              const glm::vec3& listenerPos,
                              const glm::vec3& micForward) {
            glm::vec3 earForward = micForward;
            if (glm::length(earForward) < 1e-4f) {
                earForward = glm::vec3(0.0f, 0.0f, -1.0f);
            } else {
                earForward = glm::normalize(earForward);
            }

            for (auto& [id, st] : states) {
                glm::vec3 dirForMic = st.direction;
                if (glm::length(dirForMic) < 1e-4f) {
                    dirForMic = st.sourcePos - listenerPos;
                }
                float dirLen = glm::length(dirForMic);
                if (dirLen > 1e-4f) {
                    dirForMic /= dirLen;
                    float dotForward = glm::dot(dirForMic, earForward);
                    float cardioid = std::clamp(0.5f * (1.0f + dotForward), 0.0f, 1.0f);
                    st.distanceGain *= cardioid;
                }
            }
        }

        void applyMicDistanceHf(std::map<int, AudioSourceState>& states,
                                const glm::vec3& listenerPos,
                                float sampleRate) {
            const float distCutoffMin = 7500.0f;
            const float distCutoffMax = 18000.0f;
            const float distStart = 4.0f;
            const float distEnd = 60.0f;

            for (auto& [id, st] : states) {
                float dist = glm::length(listenerPos - st.sourcePos);
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
            }
        }

        void updateHeadResponse(BaseSystem& baseSystem, RayTracedAudioContext& rtAudio) {
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
        }
    }

    void UpdateSoundPhysics(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes;
        (void)dt;
        (void)win;
        if (!baseSystem.rayTracedAudio) return;

        RayTracedAudioContext& rtAudio = *baseSystem.rayTracedAudio;
        static int lastSourceVersion = -1;
        static int lastMicVersion = -1;

        if (!rtAudio.sourceStates.empty() &&
            rtAudio.sourceStatesVersion != lastSourceVersion) {
            applyDistanceFalloff(rtAudio.sourceStates, rtAudio.batch.listenerPos);
            lastSourceVersion = rtAudio.sourceStatesVersion;
        }

        if (!rtAudio.micSourceStates.empty() &&
            rtAudio.micStatesVersion != lastMicVersion) {
            applyDistanceFalloff(rtAudio.micSourceStates, rtAudio.micBatch.listenerPos);
            applyMicCardioid(rtAudio.micSourceStates, rtAudio.micBatch.listenerPos, rtAudio.micListenerForward);
            float sampleRate = baseSystem.audio ? baseSystem.audio->sampleRate : 0.0f;
            applyMicDistanceHf(rtAudio.micSourceStates, rtAudio.micBatch.listenerPos, sampleRate);
            lastMicVersion = rtAudio.micStatesVersion;
        }

        updateHeadResponse(baseSystem, rtAudio);
    }
}
