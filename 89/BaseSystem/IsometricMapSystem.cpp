#pragma once

#include <cmath>
#include "Host/PlatformInput.h"
#include <iostream>

namespace IsometricMapSystemLogic {

    namespace {
        bool getRegistryBool(const BaseSystem& baseSystem, const char* key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            return fallback;
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
    }

    void UpdateIsometricMap(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)prototypes;
        if (!baseSystem.player || !win) return;

        PlayerContext& player = *baseSystem.player;
        if (!player.isometricMapOriginInitialized) {
            player.isometricMapOrigin = player.cameraPosition;
            player.isometricMapOriginInitialized = true;
        }
        const bool isometricEnabledFromRegistry = getRegistryBool(baseSystem, "IsometricMapEnabled", player.isometricMapMode);
        const bool topDownEnabledFromRegistry = getRegistryBool(baseSystem, "TopDownMapEnabled", player.topDownMapMode);
        if (isometricEnabledFromRegistry != player.isometricMapMode) {
            player.isometricMapMode = isometricEnabledFromRegistry;
        }
        if (topDownEnabledFromRegistry != player.topDownMapMode) {
            player.topDownMapMode = topDownEnabledFromRegistry;
        }
        if (player.isometricMapMode && player.topDownMapMode) {
            // Keep map modes mutually exclusive. Preserve legacy isometric flag priority.
            player.topDownMapMode = false;
            if (baseSystem.registry) {
                (*baseSystem.registry)["TopDownMapEnabled"] = false;
            }
        }

        static bool s_numpad6WasDown = false;
        static bool s_numpad7WasDown = false;

        const bool numpad6Down =
            PlatformInput::IsKeyDown(win, PlatformInput::Key::Kp6)
            || PlatformInput::IsKeyDown(win, PlatformInput::Key::Key6);
        if (numpad6Down && !s_numpad6WasDown) {
            const bool enableIsometric = !player.isometricMapMode;
            player.isometricMapMode = enableIsometric;
            player.topDownMapMode = false;
            player.isometricMapShowCached = false;
            player.isometricMapCaptureRequested = false;
            player.isometricMapCaptureOneShot = false;
            player.isometricMapCaptureFullIsland = false;
            if (baseSystem.registry) {
                (*baseSystem.registry)["IsometricMapEnabled"] = player.isometricMapMode;
                (*baseSystem.registry)["TopDownMapEnabled"] = false;
            }
            std::cout << "[Map] live isometric mode "
                      << (player.isometricMapMode ? "enabled" : "disabled")
                      << " (6 toggle, mousewheel/+/- zoom)" << std::endl;
        }
        s_numpad6WasDown = numpad6Down;

        const bool numpad7Down =
            PlatformInput::IsKeyDown(win, PlatformInput::Key::Kp7)
            || PlatformInput::IsKeyDown(win, PlatformInput::Key::Key7);
        if (numpad7Down && !s_numpad7WasDown) {
            const bool enableTopDown = !player.topDownMapMode;
            player.topDownMapMode = enableTopDown;
            player.isometricMapMode = false;
            player.isometricMapShowCached = false;
            player.isometricMapCaptureRequested = false;
            player.isometricMapCaptureOneShot = false;
            player.isometricMapCaptureFullIsland = false;
            if (baseSystem.registry) {
                (*baseSystem.registry)["TopDownMapEnabled"] = player.topDownMapMode;
                (*baseSystem.registry)["IsometricMapEnabled"] = false;
            }
            std::cout << "[Map] 2D top-down mode "
                      << (player.topDownMapMode ? "enabled" : "disabled")
                      << " (7 toggle, mousewheel/+/- zoom)" << std::endl;
        }
        s_numpad7WasDown = numpad7Down;

        if (player.isometricMapMode || player.topDownMapMode) {
            const bool topDownActive = player.topDownMapMode;
            const char* zoomMinKey = topDownActive ? "TopDownMapZoomMin" : "IsometricMapZoomMin";
            const char* zoomMaxKey = topDownActive ? "TopDownMapZoomMax" : "IsometricMapZoomMax";
            const char* zoomKeyRateKey = topDownActive ? "TopDownMapZoomKeyRate" : "IsometricMapZoomKeyRate";
            const char* zoomScrollStepKey = topDownActive ? "TopDownMapZoomScrollStep" : "IsometricMapZoomScrollStep";
            const float zoomMin = glm::max(0.05f, getRegistryFloat(baseSystem, zoomMinKey, 0.35f));
            const float zoomMax = glm::max(zoomMin, getRegistryFloat(baseSystem, zoomMaxKey, 6.0f));
            const float zoomKeyRate = glm::max(0.0f, getRegistryFloat(baseSystem, zoomKeyRateKey, 1.8f));
            const float zoomScrollStep = glm::max(0.0f, getRegistryFloat(baseSystem, zoomScrollStepKey, 0.18f));
            float& mapZoom = topDownActive ? player.topDownMapZoom : player.isometricMapZoom;

            const bool zoomInDown =
                PlatformInput::IsKeyDown(win, PlatformInput::Key::KpAdd)
                || PlatformInput::IsKeyDown(win, PlatformInput::Key::Equal);
            const bool zoomOutDown =
                PlatformInput::IsKeyDown(win, PlatformInput::Key::KpSubtract)
                || PlatformInput::IsKeyDown(win, PlatformInput::Key::Minus);

            if (zoomInDown) {
                mapZoom += zoomKeyRate * dt;
            }
            if (zoomOutDown) {
                mapZoom -= zoomKeyRate * dt;
            }
            if (std::fabs(player.scrollYOffset) > 0.0) {
                mapZoom += static_cast<float>(player.scrollYOffset) * zoomScrollStep;
                player.scrollYOffset = 0.0;
            }
            mapZoom = glm::clamp(mapZoom, zoomMin, zoomMax);
        }
    }
}
