#pragma once

namespace CameraSystemLogic {
    void UpdateCameraMatrices(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.app || !baseSystem.player) { std::cerr << "ERROR: CameraSystem cannot run without AppContext or PlayerContext." << std::endl; return; }
        AppContext& app = *baseSystem.app;
        PlayerContext& player = *baseSystem.player;
        glm::vec3 front;
        front.x = cos(glm::radians(player.cameraYaw)) * cos(glm::radians(player.cameraPitch));
        front.y = sin(glm::radians(player.cameraPitch));
        front.z = sin(glm::radians(player.cameraYaw)) * cos(glm::radians(player.cameraPitch));
        front = glm::normalize(front);
        player.viewMatrix = glm::lookAt(player.cameraPosition, player.cameraPosition + front, glm::vec3(0.0f, 1.0f, 0.0f));
        player.projectionMatrix = glm::perspective(glm::radians(103.0f), (float)app.windowWidth / (float)app.windowHeight, 0.1f, 2000.0f);
    }
}
