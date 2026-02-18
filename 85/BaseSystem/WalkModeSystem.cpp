#pragma once
#include "../Host.h"

namespace WalkModeSystemLogic {

    void ProcessWalkMovement(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes;
        if (!baseSystem.player || !baseSystem.level || !win) return;
        if (baseSystem.gamemode != "survival") return;
        bool spawnReady = false;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("spawn_ready");
            if (it != baseSystem.registry->end() &&
                std::holds_alternative<bool>(it->second)) {
                spawnReady = std::get<bool>(it->second);
            }
        }
        if (!spawnReady) return;
        PlayerContext& player = *baseSystem.player;

        // Walk mode uses horizontal plane movement and jump/crouch with reduced vertical speed.
        glm::vec3 front(cos(glm::radians(player.cameraYaw)), 0.0f, sin(glm::radians(player.cameraYaw)));
        glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
        float moveSpeed = 4.5f * dt;
        float jumpVelocity = 8.0f; // fixed jump impulse

        glm::vec3 moveDelta(0.0f);
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) moveDelta += front * moveSpeed;
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) moveDelta -= front * moveSpeed;
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) moveDelta -= right * moveSpeed;
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) moveDelta += right * moveSpeed;
        // Horizontal
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) moveDelta += front * moveSpeed;
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) moveDelta -= front * moveSpeed;
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) moveDelta -= right * moveSpeed;
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) moveDelta += right * moveSpeed;

        // Jump on tap when grounded
        static bool spaceWasDown = false;
        bool spaceDown = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (spaceDown && !spaceWasDown && player.onGround) {
            player.verticalVelocity = jumpVelocity;
            player.onGround = false;
        }
        spaceWasDown = spaceDown;

        player.cameraPosition += moveDelta;
    }
}
