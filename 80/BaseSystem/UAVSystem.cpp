#pragma once

namespace UAVSystemLogic {
    void ProcessUAVMovement(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player || !baseSystem.level || baseSystem.level->worlds.empty()) return;
        if (baseSystem.gamemode == "survival") return; // only for creative/spectator
        PlayerContext& player = *baseSystem.player;
        Entity& activeWorld = baseSystem.level->worlds[baseSystem.level->activeWorldIndex];

        float speed = 5.0f * dt;
        glm::vec3 front(cos(glm::radians(player.cameraYaw)), 0.0f, sin(glm::radians(player.cameraYaw)));
        glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
        
        for (const auto& inst : activeWorld.instances) {
            const auto& proto = prototypes[inst.prototypeID];
            if (proto.audicleType == "gated") {
                if (proto.name == "UAV_W") { player.cameraPosition += front * speed; }
                if (proto.name == "UAV_S") { player.cameraPosition -= front * speed; }
                if (proto.name == "UAV_A") { player.cameraPosition -= right * speed; }
                if (proto.name == "UAV_D") { player.cameraPosition += right * speed; }
                if (proto.name == "UAV_SPACE") { player.cameraPosition.y += speed; }
                if (proto.name == "UAV_LSHIFT") { player.cameraPosition.y -= speed; }
            }
        }
    }
}
