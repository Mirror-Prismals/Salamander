#pragma once
#include <iostream>

namespace VolumeFillSystemLogic {

    void ProcessVolumeFills(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.level || !baseSystem.world || !baseSystem.instance) return;

        for (auto& worldProto : baseSystem.level->worlds) {
            if (!worldProto.isVolume) continue;

            const Entity* blockProto = HostLogic::findPrototype(worldProto.fillBlockType, prototypes);
            if (!blockProto) {
                std::cerr << "VolumeFillSystem: missing block type '" << worldProto.fillBlockType << "' for world '" << worldProto.name << "'" << std::endl;
                continue;
            }

            glm::vec3 color = baseSystem.world->colorLibrary.count(worldProto.fillColor)
                ? baseSystem.world->colorLibrary[worldProto.fillColor]
                : glm::vec3(1, 0, 1);

            for (int x = 0; x < worldProto.fillDimensions.x; ++x) {
                for (int y = 0; y < worldProto.fillDimensions.y; ++y) {
                    for (int z = 0; z < worldProto.fillDimensions.z; ++z) {
                        glm::vec3 pos = worldProto.fillOrigin + glm::vec3(x, y, z);
                        worldProto.instances.push_back(HostLogic::CreateInstance(baseSystem, blockProto->prototypeID, pos, color));
                    }
                }
            }
        }
    }

}
