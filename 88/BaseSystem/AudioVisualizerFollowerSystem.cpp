#pragma once

#include <cmath>
#include <string>

namespace AudioVisualizerFollowerSystemLogic {

    namespace {
        constexpr const char* kFollowerWorldName = "PlayerHeadAudioVisualizerWorld";
        constexpr float kDefaultHeightOffset = 11.75f;
        constexpr float kDefaultForwardOffset = 0.0f;

        bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<bool>(it->second)) return fallback;
            return std::get<bool>(it->second);
        }

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
    }

    void UpdateAudioVisualizerFollower(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt;
        (void)win;

        if (!baseSystem.player || !baseSystem.level) return;
        if (!getRegistryBool(baseSystem, "PlayerHeadAudioVisualizerEnabled", true)) return;

        int visualizerProtoID = -1;
        for (const auto& proto : prototypes) {
            if (proto.name == "AudioVisualizer") {
                visualizerProtoID = proto.prototypeID;
                break;
            }
        }
        if (visualizerProtoID < 0) return;

        PlayerContext& player = *baseSystem.player;
        LevelContext& level = *baseSystem.level;

        float heightOffset = getRegistryFloat(baseSystem, "PlayerHeadAudioVisualizerHeight", kDefaultHeightOffset);
        float forwardOffset = getRegistryFloat(baseSystem, "PlayerHeadAudioVisualizerForwardOffset", kDefaultForwardOffset);
        const bool trackLook = getRegistryBool(baseSystem, "PlayerHeadAudioVisualizerTrackLook", true);
        const bool trackPitch = getRegistryBool(baseSystem, "PlayerHeadAudioVisualizerTrackPitch", true);

        glm::vec3 forward(0.0f);
        if (trackLook) {
            const float pitch = trackPitch ? player.cameraPitch : 0.0f;
            forward.x = std::cos(glm::radians(player.cameraYaw)) * std::cos(glm::radians(pitch));
            forward.y = std::sin(glm::radians(pitch));
            forward.z = std::sin(glm::radians(player.cameraYaw)) * std::cos(glm::radians(pitch));
            if (glm::length(forward) < 1e-4f) {
                forward = glm::vec3(0.0f, 0.0f, -1.0f);
            } else {
                forward = glm::normalize(forward);
            }
        }

        const glm::vec3 targetPosition = player.cameraPosition
                                       + glm::vec3(0.0f, heightOffset, 0.0f)
                                       + forward * forwardOffset;

        for (auto& world : level.worlds) {
            if (world.name != kFollowerWorldName) continue;
            for (auto& instance : world.instances) {
                const bool isVisualizer = (instance.prototypeID == visualizerProtoID) || (instance.name == "AudioVisualizer");
                if (!isVisualizer) continue;
                instance.position = targetPosition;
            }
        }
    }
}
