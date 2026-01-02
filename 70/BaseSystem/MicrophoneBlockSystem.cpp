#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>

// Forward declarations
struct BaseSystem;
struct Entity;
struct EntityInstance;
struct LevelContext;
struct PlayerContext;
struct RayTracedAudioContext;
struct UIContext;

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
        if (micProtoID < 0) {
            rtAudio.micActiveInstanceID = -1;
            return;
        }

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
    }
}
