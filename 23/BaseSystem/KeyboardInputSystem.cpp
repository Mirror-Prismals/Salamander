#pragma once

namespace KeyboardInputSystemLogic {
    void ProcessKeyboardInput(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.level || baseSystem.level->worlds.empty()) return;
        LevelContext& level = *baseSystem.level;
        Entity& activeWorld = level.worlds[level.activeWorldIndex];

        // --- Tesseract Controls ---
        bool tildePressed = (glfwGetKey(win, GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS);
        if (!tildePressed) {
            if (glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "TESS_FORWARD", {}, {}));
            if (glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "TESS_BACKWARD", {}, {}));
            if (glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "TESS_LEFT", {}, {}));
            if (glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "TESS_RIGHT", {}, {}));
        } else {
            if (glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "TESS_UP", {}, {}));
            if (glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "TESS_DOWN", {}, {}));
        }

        // --- UAV (Player Movement) Controls ---
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_W", {}, {}));
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_A", {}, {}));
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_S", {}, {}));
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_D", {}, {}));
        if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_SPACE", {}, {}));
        if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, prototypes, "UAV_LSHIFT", {}, {}));
    
        // --- World Switching ---
        static bool tab_pressed_last_frame = false;
        bool tab_pressed = glfwGetKey(win, GLFW_KEY_TAB) == GLFW_PRESS;
        if (tab_pressed && !tab_pressed_last_frame) {
            level.activeWorldIndex = (level.activeWorldIndex + 1) % level.worlds.size();
        }
        tab_pressed_last_frame = tab_pressed;
    }
}
