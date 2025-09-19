#pragma once

namespace PlayerControlSystemLogic {
    void UpdateCameraRotationFromMouse(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player) { std::cerr << "ERROR: PlayerControlSystem cannot run without PlayerContext." << std::endl; return; }
        PlayerContext& player = *baseSystem.player;
        const float sensitivity = 0.1f;
        player.cameraYaw += player.mouseOffsetX * sensitivity;
        player.cameraPitch += player.mouseOffsetY * sensitivity;
        if (player.cameraPitch > 89.0f) player.cameraPitch = 89.0f;
        if (player.cameraPitch < -89.0f) player.cameraPitch = -89.0f;
    }
    
    void ProcessPlayerMovement(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player) { std::cerr << "ERROR: PlayerControlSystem cannot run without PlayerContext." << std::endl; return; }
        PlayerContext& player = *baseSystem.player;
        float speed = 5.0f * dt;
        glm::vec3 front(cos(glm::radians(player.cameraYaw)), 0.0f, sin(glm::radians(player.cameraYaw)));
        glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) player.cameraPosition += front * speed;
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) player.cameraPosition -= front * speed;
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) player.cameraPosition -= right * speed;
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) player.cameraPosition += right * speed;
        if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) player.cameraPosition.y += speed;
        if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) player.cameraPosition.y -= speed;
    }
}
