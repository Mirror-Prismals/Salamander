#pragma once

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace DebugHudSystemLogic {
    void UpdateDebugHud(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.font) return;
        FontContext& font = *baseSystem.font;

        bool showFps = true;
        bool showCoords = true;
        bool showFishTodayCoords = false;
        auto readRegistryToggle = [&](const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) {
                return std::get<bool>(it->second);
            }
            if (std::holds_alternative<std::string>(it->second)) {
                const std::string& raw = std::get<std::string>(it->second);
                try {
                    return std::stoi(raw) != 0;
                } catch (...) {
                    if (raw == "true" || raw == "TRUE" || raw == "True") return true;
                    if (raw == "false" || raw == "FALSE" || raw == "False") return false;
                }
            }
            return fallback;
        };
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("DebugHudSystem");
            if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second) && !std::get<bool>(it->second)) {
                font.variables.erase("fps");
                font.variables.erase("coords");
                font.variables.erase("fish_catch");
                font.variables.erase("gem_drop");
                font.variables.erase("fish_today_coords");
                return;
            }
            auto fpsIt = baseSystem.registry->find("FPSMeterEnabled");
            if (fpsIt != baseSystem.registry->end() && std::holds_alternative<bool>(fpsIt->second) && !std::get<bool>(fpsIt->second)) {
                showFps = false;
            }
            auto coordsIt = baseSystem.registry->find("CoordinatesMeterEnabled");
            if (coordsIt != baseSystem.registry->end() && std::holds_alternative<bool>(coordsIt->second) && !std::get<bool>(coordsIt->second)) {
                showCoords = false;
            }
            showFishTodayCoords = readRegistryToggle("TodaysFishLocationMeterEnabled", false);
        }

        if (showFps) {
            static float smoothedFps = 0.0f;
            if (dt > 0.0f) {
                float fps = 1.0f / dt;
                smoothedFps = (smoothedFps <= 0.0f) ? fps : (smoothedFps * 0.9f + fps * 0.1f);
            }

            int fpsInt = static_cast<int>(std::round(smoothedFps));
            font.variables["fps"] = "FPS: " + std::to_string(fpsInt);
        } else {
            font.variables.erase("fps");
        }

        if (showCoords && baseSystem.player) {
            const glm::vec3& p = baseSystem.player->cameraPosition;
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2)
               << "XYZ: " << p.x << ", " << p.y << ", " << p.z;
            font.variables["coords"] = ss.str();
        } else {
            font.variables.erase("coords");
        }

        if (baseSystem.fishing && baseSystem.fishing->lastCatchTimer > 0.0f && !baseSystem.fishing->lastCatchText.empty()) {
            font.variables["fish_catch"] = "CATCH: " + baseSystem.fishing->lastCatchText;
        } else {
            font.variables.erase("fish_catch");
        }

        if (baseSystem.gems && baseSystem.gems->lastDropTimer > 0.0f && !baseSystem.gems->lastDropText.empty()) {
            font.variables["gem_drop"] = baseSystem.gems->lastDropText;
        } else {
            font.variables.erase("gem_drop");
        }

        if (showFishTodayCoords && baseSystem.fishing && baseSystem.fishing->dailySchoolValid) {
            const glm::vec3& p = baseSystem.fishing->dailySchoolPosition;
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2)
               << "FISH TODAY XYZ: " << p.x << ", " << p.y << ", " << p.z;
            font.variables["fish_today_coords"] = ss.str();
        } else if (showFishTodayCoords) {
            font.variables["fish_today_coords"] = "FISH TODAY XYZ: unavailable";
        } else {
            font.variables.erase("fish_today_coords");
        }
    }
}
