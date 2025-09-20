#pragma once

namespace PlayerControlSystemLogic {

    void UpdateCameraRotationFromMouse(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player) return;
        PlayerContext& player = *baseSystem.player;
        const float sensitivity = 0.1f;
        player.cameraYaw += player.mouseOffsetX * sensitivity;
        player.cameraPitch += player.mouseOffsetY * sensitivity;
        if (player.cameraPitch > 89.0f) player.cameraPitch = 89.0f;
        if (player.cameraPitch < -89.0f) player.cameraPitch = -89.0f;
    }
    
    void ProcessPlayerMovement(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player) return;
        PlayerContext& player = *baseSystem.player;
        TesseractContext& tesseract = *baseSystem.tesseract;

        bool tildePressed = (glfwGetKey(win, GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS);
        bool tesseractControlActive = 
            glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS ||
            glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS;

        // --- Tesseract Input Logic ---
        static bool keys_pressed_last_frame[6] = {false};
        bool current_keys[6] = {
            glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS && !tildePressed,
            glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS && !tildePressed,
            glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS && !tildePressed,
            glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS && !tildePressed,
            glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS && tildePressed,
            glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS && tildePressed
        };

        const float rotationAmount = glm::radians(90.0f);

        if (current_keys[0] && !keys_pressed_last_frame[0]) tesseract.targetAngleXW += rotationAmount;
        if (current_keys[1] && !keys_pressed_last_frame[1]) tesseract.targetAngleXW -= rotationAmount;
        if (current_keys[2] && !keys_pressed_last_frame[2]) tesseract.targetAngleZW += rotationAmount;
        if (current_keys[3] && !keys_pressed_last_frame[3]) tesseract.targetAngleZW -= rotationAmount;
        if (current_keys[4] && !keys_pressed_last_frame[4]) tesseract.targetAngleYW += rotationAmount;
        if (current_keys[5] && !keys_pressed_last_frame[5]) tesseract.targetAngleYW -= rotationAmount;
        
        for(int i=0; i<6; ++i) keys_pressed_last_frame[i] = current_keys[i];

        // --- Player Movement Logic ---
        if (!tesseractControlActive) {
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
}
