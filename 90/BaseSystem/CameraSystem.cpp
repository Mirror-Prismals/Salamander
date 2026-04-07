#pragma once

namespace CameraSystemLogic {
    namespace {
        bool getRegistryBool(const BaseSystem& baseSystem, const char* key, bool fallback) {
            if (!baseSystem.registry || !key) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) {
                return std::get<bool>(it->second);
            }
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            std::string value = std::get<std::string>(it->second);
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (value == "1" || value == "true" || value == "yes" || value == "on") return true;
            if (value == "0" || value == "false" || value == "no" || value == "off") return false;
            return fallback;
        }

        float getRegistryFloat(const BaseSystem& baseSystem, const char* key, float fallback) {
            if (!baseSystem.registry || !key) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stof(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        glm::vec3 cameraEyePosition(const BaseSystem& baseSystem, const PlayerContext& player) {
            if (baseSystem.gamemode == "survival") {
                return player.cameraPosition + glm::vec3(0.0f, 0.6f, 0.0f);
            }
            return player.cameraPosition;
        }

        glm::vec3 computeViewBobOffset(const BaseSystem& baseSystem,
                                       PlayerContext& player,
                                       const glm::vec3& front,
                                       float dt) {
            constexpr float kTau = 6.28318530718f;
            const glm::vec3 up(0.0f, 1.0f, 0.0f);

            if (baseSystem.gamemode != "survival"
                || player.isometricMapMode
                || player.topDownMapMode
                || !getRegistryBool(baseSystem, "WalkViewBobbingEnabled", true)) {
                player.viewBobWeight = 0.0f;
                return glm::vec3(0.0f);
            }

            const float safeDt = (dt > 1e-5f) ? dt : (1.0f / 60.0f);
            const float estimatedFrameSpeed = glm::length(
                glm::vec2(player.cameraPosition.x - player.prevCameraPosition.x,
                          player.cameraPosition.z - player.prevCameraPosition.z)
            ) / safeDt;

            const float minMoveSpeed = glm::clamp(
                getRegistryFloat(baseSystem, "WalkViewBobMinSpeed", 0.22f),
                0.0f,
                10.0f
            );
            const float speedForFullEffect = glm::max(
                0.1f,
                getRegistryFloat(baseSystem, "WalkViewBobSpeedForFullEffect", 4.8f)
            );
            const float attackSeconds = glm::clamp(
                getRegistryFloat(baseSystem, "WalkViewBobBlendInSeconds", 0.08f),
                0.01f,
                2.0f
            );
            const float releaseSeconds = glm::clamp(
                getRegistryFloat(baseSystem, "WalkViewBobBlendOutSeconds", 0.22f),
                0.01f,
                2.0f
            );
            const float cyclesPerSecondAtRef = glm::clamp(
                getRegistryFloat(baseSystem, "WalkViewBobFrequency", 1.55f),
                0.1f,
                8.0f
            );
            const float verticalAmplitude = glm::clamp(
                getRegistryFloat(baseSystem, "WalkViewBobVerticalAmplitude", 0.060f),
                0.0f,
                0.25f
            );
            const float lateralAmplitude = glm::clamp(
                getRegistryFloat(baseSystem, "WalkViewBobLateralAmplitude", 0.030f),
                0.0f,
                0.25f
            );

            const bool groundedAndWalkingMode = (player.onGround || estimatedFrameSpeed > (minMoveSpeed * 0.75f))
                && !player.boulderPrimaryLatched
                && !player.boulderSecondaryLatched;
            const float horizontalSpeed = std::max(
                std::max(0.0f, player.viewBobHorizontalSpeed),
                std::max(0.0f, estimatedFrameSpeed)
            );
            const bool moving = horizontalSpeed > minMoveSpeed;
            const float targetWeight = (groundedAndWalkingMode && moving)
                ? glm::clamp(horizontalSpeed / speedForFullEffect, 0.0f, 1.0f)
                : 0.0f;

            const float blendSeconds = (targetWeight > player.viewBobWeight) ? attackSeconds : releaseSeconds;
            const float blendAlpha = glm::clamp((blendSeconds > 1e-5f) ? (dt / blendSeconds) : 1.0f, 0.0f, 1.0f);
            player.viewBobWeight = glm::mix(player.viewBobWeight, targetWeight, blendAlpha);

            if (player.viewBobWeight <= 1e-4f) {
                player.viewBobWeight = 0.0f;
                return glm::vec3(0.0f);
            }

            const float speedRatio = glm::clamp(horizontalSpeed / speedForFullEffect, 0.35f, 2.4f);
            const float phaseAdvance = cyclesPerSecondAtRef * speedRatio * kTau * std::max(0.0f, dt);
            player.viewBobPhase = std::fmod(player.viewBobPhase + phaseAdvance, kTau);
            if (player.viewBobPhase < 0.0f) player.viewBobPhase += kTau;

            const float sideWave = std::sin(player.viewBobPhase);
            const float verticalWave = std::sin(player.viewBobPhase - 0.6f);
            glm::vec3 right = glm::cross(front, up);
            if (glm::length(right) < 1e-5f) {
                right = glm::vec3(1.0f, 0.0f, 0.0f);
            } else {
                right = glm::normalize(right);
            }

            const float weight = player.viewBobWeight;
            return right * (lateralAmplitude * sideWave * weight)
                + up * (verticalAmplitude * verticalWave * weight);
        }
    }

    void UpdateCameraMatrices(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)prototypes;
        (void)win;
        if (!baseSystem.app || !baseSystem.player) { std::cerr << "ERROR: CameraSystem cannot run without AppContext or PlayerContext." << std::endl; return; }
        AppContext& app = *baseSystem.app;
        PlayerContext& player = *baseSystem.player;
        glm::vec3 front;
        front.x = cos(glm::radians(player.cameraYaw)) * cos(glm::radians(player.cameraPitch));
        front.y = sin(glm::radians(player.cameraPitch));
        front.z = sin(glm::radians(player.cameraYaw)) * cos(glm::radians(player.cameraPitch));
        front = glm::normalize(front);
        glm::vec3 eyePos = cameraEyePosition(baseSystem, player);
        eyePos += computeViewBobOffset(baseSystem, player, front, std::max(0.0f, dt));
        player.viewMatrix = glm::lookAt(eyePos, eyePos + front, glm::vec3(0.0f, 1.0f, 0.0f));
        player.projectionMatrix = glm::perspective(glm::radians(103.0f), (float)app.windowWidth / (float)app.windowHeight, 0.1f, 2000.0f);
    }
}
