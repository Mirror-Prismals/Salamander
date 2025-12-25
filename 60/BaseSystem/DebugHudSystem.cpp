#pragma once

#include <cmath>
#include <string>

namespace DebugHudSystemLogic {
    void UpdateDebugHud(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.font) return;
        FontContext& font = *baseSystem.font;

        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("DebugHudSystem");
            if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second) && !std::get<bool>(it->second)) {
                font.variables.erase("fps");
                return;
            }
        }

        static float smoothedFps = 0.0f;
        if (dt > 0.0f) {
            float fps = 1.0f / dt;
            smoothedFps = (smoothedFps <= 0.0f) ? fps : (smoothedFps * 0.9f + fps * 0.1f);
        }

        int fpsInt = static_cast<int>(std::round(smoothedFps));
        font.variables["fps"] = "FPS: " + std::to_string(fpsInt);
    }
}
