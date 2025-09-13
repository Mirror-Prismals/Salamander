#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// The "Base System" data object. Holds all data shared between systems.
struct BaseSystem {
    // --- Player & Camera State ---
    float cameraYaw = -90.0f;
    float cameraPitch = 0.0f;
    glm::vec3 cameraPosition = glm::vec3(6.0f, 5.0f, 15.0f);
    float mouseOffsetX = 0.0f;
    float mouseOffsetY = 0.0f;
    float lastX = WINDOW_WIDTH / 2.0f;
    float lastY = WINDOW_HEIGHT / 2.0f;
    bool firstMouse = true;
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
};
