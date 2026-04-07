#pragma once

#include "Host/PlatformInput.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace SecurityCameraSystemLogic {

    namespace {
        struct CameraCandidate {
            int worldIndex = -1;
            int instanceID = -1;
            EntityInstance* instance = nullptr;
        };

        bool getRegistryBool(const BaseSystem& baseSystem, const char* key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<bool>(it->second)) return fallback;
            return std::get<bool>(it->second);
        }

        float getRegistryFloat(const BaseSystem& baseSystem, const char* key, float fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stof(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        EntityInstance* findInstanceById(LevelContext& level, int worldIndex, int instanceId) {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return nullptr;
            Entity& world = level.worlds[static_cast<size_t>(worldIndex)];
            for (auto& inst : world.instances) {
                if (inst.instanceID == instanceId) return &inst;
            }
            return nullptr;
        }

        int resolveSecurityCameraPrototypeID(const std::vector<Entity>& prototypes) {
            for (const Entity& proto : prototypes) {
                if (proto.name == "SecurityCamera") return proto.prototypeID;
            }
            return -1;
        }

        void buildCameraCache(BaseSystem& baseSystem, int cameraPrototypeID, SecurityCameraContext& securityCamera) {
            securityCamera.cameraInstances.clear();
            if (!baseSystem.level || cameraPrototypeID < 0) {
                securityCamera.cacheBuilt = true;
                return;
            }
            for (size_t wi = 0; wi < baseSystem.level->worlds.size(); ++wi) {
                const Entity& world = baseSystem.level->worlds[wi];
                for (const EntityInstance& inst : world.instances) {
                    if (inst.prototypeID != cameraPrototypeID) continue;
                    securityCamera.cameraInstances.emplace_back(static_cast<int>(wi), inst.instanceID);
                }
            }
            securityCamera.cacheBuilt = true;
        }

        void collectCameras(BaseSystem& baseSystem,
                            int cameraPrototypeID,
                            SecurityCameraContext& securityCamera,
                            std::vector<CameraCandidate>& outCameras) {
            outCameras.clear();
            if (!baseSystem.level || cameraPrototypeID < 0) return;
            buildCameraCache(baseSystem, cameraPrototypeID, securityCamera);

            auto appendFromCache = [&](bool* outCacheValid) {
                bool cacheValid = true;
                for (const auto& ref : securityCamera.cameraInstances) {
                    EntityInstance* inst = findInstanceById(*baseSystem.level, ref.first, ref.second);
                    if (!inst || inst->prototypeID != cameraPrototypeID) {
                        cacheValid = false;
                        continue;
                    }
                    outCameras.push_back(CameraCandidate{ref.first, ref.second, inst});
                }
                if (outCacheValid) *outCacheValid = cacheValid;
            };

            bool cacheValid = true;
            appendFromCache(&cacheValid);
            if (!cacheValid) {
                buildCameraCache(baseSystem, cameraPrototypeID, securityCamera);
                outCameras.clear();
                appendFromCache(nullptr);
            }
        }

        glm::vec3 directionFromYawPitch(float yawDegrees, float pitchDegrees) {
            glm::vec3 forward;
            forward.x = std::cos(glm::radians(yawDegrees)) * std::cos(glm::radians(pitchDegrees));
            forward.y = std::sin(glm::radians(pitchDegrees));
            forward.z = std::sin(glm::radians(yawDegrees)) * std::cos(glm::radians(pitchDegrees));
            if (glm::length(forward) < 1e-4f) {
                return glm::vec3(0.0f, 0.0f, -1.0f);
            }
            return glm::normalize(forward);
        }

        glm::vec3 resolveUiAnchorPosition(BaseSystem& baseSystem,
                                          const std::vector<Entity>& prototypes,
                                          const UIContext& ui) {
            if (baseSystem.level
                && ui.activeWorldIndex >= 0
                && ui.activeInstanceID >= 0
                && ui.activeWorldIndex < static_cast<int>(baseSystem.level->worlds.size())) {
                EntityInstance* inst = findInstanceById(*baseSystem.level, ui.activeWorldIndex, ui.activeInstanceID);
                if (inst
                    && inst->prototypeID >= 0
                    && inst->prototypeID < static_cast<int>(prototypes.size())
                    && prototypes[static_cast<size_t>(inst->prototypeID)].name == "Computer") {
                    return inst->position;
                }
            }
            if (baseSystem.player) return baseSystem.player->cameraPosition;
            return glm::vec3(0.0f);
        }

        CameraCandidate chooseBestCamera(const std::vector<CameraCandidate>& cameras, const glm::vec3& anchor) {
            if (cameras.empty()) return CameraCandidate{};
            size_t bestIndex = 0;
            float bestDist2 = std::numeric_limits<float>::max();
            for (size_t i = 0; i < cameras.size(); ++i) {
                const CameraCandidate& candidate = cameras[i];
                if (!candidate.instance) continue;
                const glm::vec3 delta = candidate.instance->position - anchor;
                const float dist2 = glm::dot(delta, delta);
                if (dist2 < bestDist2) {
                    bestDist2 = dist2;
                    bestIndex = i;
                }
            }
            return cameras[bestIndex];
        }

        CameraCandidate findActiveCamera(const std::vector<CameraCandidate>& cameras,
                                         int activeWorldIndex,
                                         int activeInstanceID) {
            for (const CameraCandidate& candidate : cameras) {
                if (candidate.worldIndex == activeWorldIndex && candidate.instanceID == activeInstanceID) {
                    return candidate;
                }
            }
            return CameraCandidate{};
        }

        size_t findCameraIndex(const std::vector<CameraCandidate>& cameras,
                               int activeWorldIndex,
                               int activeInstanceID) {
            for (size_t i = 0; i < cameras.size(); ++i) {
                if (cameras[i].worldIndex == activeWorldIndex && cameras[i].instanceID == activeInstanceID) {
                    return i;
                }
            }
            return cameras.size();
        }
    }

    void UpdateSecurityCamera(BaseSystem& baseSystem,
                              std::vector<Entity>& prototypes,
                              float dt,
                              PlatformWindowHandle win) {
        if (!baseSystem.securityCamera || !baseSystem.level || !baseSystem.ui) return;

        SecurityCameraContext& securityCamera = *baseSystem.securityCamera;
        UIContext& ui = *baseSystem.ui;
        securityCamera.dawViewActive = false;

        if (!getRegistryBool(baseSystem, "SecurityCameraSystem", true)) {
            securityCamera.wasUiActive = (ui.active && ui.fullscreenActive);
            securityCamera.cycleKeyDownLast = false;
            return;
        }

        const int cameraPrototypeID = resolveSecurityCameraPrototypeID(prototypes);
        if (cameraPrototypeID < 0) {
            securityCamera.activeWorldIndex = -1;
            securityCamera.activeInstanceID = -1;
            securityCamera.wasUiActive = (ui.active && ui.fullscreenActive);
            securityCamera.cycleKeyDownLast = false;
            return;
        }

        std::vector<CameraCandidate> cameras;
        collectCameras(baseSystem, cameraPrototypeID, securityCamera, cameras);

        const bool dawActive = ui.active && ui.fullscreenActive;
        const bool enteredDaw = dawActive && !securityCamera.wasUiActive;
        const int previousActiveInstance = securityCamera.activeInstanceID;

        if (!dawActive) {
            securityCamera.wasUiActive = false;
            securityCamera.cycleKeyDownLast = false;
            return;
        }

        if (cameras.empty()) {
            securityCamera.activeWorldIndex = -1;
            securityCamera.activeInstanceID = -1;
            securityCamera.wasUiActive = true;
            securityCamera.cycleKeyDownLast = false;
            return;
        }

        CameraCandidate activeCamera = findActiveCamera(cameras, securityCamera.activeWorldIndex, securityCamera.activeInstanceID);
        if (!activeCamera.instance || enteredDaw) {
            const glm::vec3 anchor = resolveUiAnchorPosition(baseSystem, prototypes, ui);
            activeCamera = chooseBestCamera(cameras, anchor);
            securityCamera.activeWorldIndex = activeCamera.worldIndex;
            securityCamera.activeInstanceID = activeCamera.instanceID;
        }

        if (!activeCamera.instance) {
            securityCamera.wasUiActive = true;
            securityCamera.cycleKeyDownLast = false;
            return;
        }

        bool cycleKeyDown = false;
        if (win) {
            cycleKeyDown = PlatformInput::IsKeyDown(win, PlatformInput::Key::C);
        }
        const bool cyclePressed = cycleKeyDown && !securityCamera.cycleKeyDownLast;
        securityCamera.cycleKeyDownLast = cycleKeyDown;
        if (cyclePressed && cameras.size() > 1) {
            const size_t currentIndex = findCameraIndex(
                cameras,
                securityCamera.activeWorldIndex,
                securityCamera.activeInstanceID
            );
            const size_t nextIndex = (currentIndex < cameras.size())
                ? ((currentIndex + 1) % cameras.size())
                : 0;
            activeCamera = cameras[nextIndex];
            securityCamera.activeWorldIndex = activeCamera.worldIndex;
            securityCamera.activeInstanceID = activeCamera.instanceID;
        }

        const bool activeCameraChanged = (securityCamera.activeInstanceID != previousActiveInstance);
        if (enteredDaw || activeCameraChanged) {
            auto saved = securityCamera.orientationByInstance.find(activeCamera.instanceID);
            if (saved != securityCamera.orientationByInstance.end()) {
                securityCamera.viewYaw = saved->second.x;
                securityCamera.viewPitch = saved->second.y;
            } else {
                securityCamera.viewYaw = activeCamera.instance->rotation;
                securityCamera.viewPitch = 0.0f;
            }
        }

        const float yawSpeed = std::max(1.0f, getRegistryFloat(baseSystem, "SecurityCameraPanYawSpeedDeg", 84.0f));
        const float pitchSpeed = std::max(1.0f, getRegistryFloat(baseSystem, "SecurityCameraPanPitchSpeedDeg", 66.0f));
        if (win) {
            if (PlatformInput::IsKeyDown(win, PlatformInput::Key::A)) securityCamera.viewYaw -= yawSpeed * dt;
            if (PlatformInput::IsKeyDown(win, PlatformInput::Key::D)) securityCamera.viewYaw += yawSpeed * dt;
            if (PlatformInput::IsKeyDown(win, PlatformInput::Key::W)) securityCamera.viewPitch += pitchSpeed * dt;
            if (PlatformInput::IsKeyDown(win, PlatformInput::Key::S)) securityCamera.viewPitch -= pitchSpeed * dt;
        }

        const float pitchMin = std::clamp(getRegistryFloat(baseSystem, "SecurityCameraPitchMinDeg", -70.0f), -89.0f, 89.0f);
        const float pitchMax = std::clamp(getRegistryFloat(baseSystem, "SecurityCameraPitchMaxDeg", 70.0f), -89.0f, 89.0f);
        const float minPitch = std::min(pitchMin, pitchMax);
        const float maxPitch = std::max(pitchMin, pitchMax);
        securityCamera.viewPitch = std::clamp(securityCamera.viewPitch, minPitch, maxPitch);

        const glm::vec3 forward = directionFromYawPitch(securityCamera.viewYaw, securityCamera.viewPitch);
        const float forwardOffset = getRegistryFloat(baseSystem, "SecurityCameraLensForwardOffset", 0.58f);
        const float heightOffset = getRegistryFloat(baseSystem, "SecurityCameraLensHeightOffset", 0.18f);

        securityCamera.viewForward = forward;
        securityCamera.viewPosition = activeCamera.instance->position
            + glm::vec3(0.0f, heightOffset, 0.0f)
            + forward * forwardOffset;
        securityCamera.dawViewActive = true;
        securityCamera.wasUiActive = true;
        securityCamera.orientationByInstance[activeCamera.instanceID] = glm::vec2(securityCamera.viewYaw, securityCamera.viewPitch);

        // Mirror current pan direction in world-space orientation when the block is visible.
        activeCamera.instance->rotation = securityCamera.viewYaw;
    }
}
