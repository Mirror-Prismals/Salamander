#pragma once
#include <random>

namespace HostLogic {

    inline auto findPrototype(const std::string& name, const std::vector<Entity>& prototypes) -> const Entity* {
        for (const auto& proto : prototypes) {
            if (proto.name == name) return &proto;
        }
        return nullptr;
    };

    glm::vec3 hexToVec3(const std::string& hex) {
        std::string fullHex = hex.substr(1);
        if (fullHex.length() == 3) {
            char r = fullHex[0], g = fullHex[1], b = fullHex[2];
            fullHex = {r, r, g, g, b, b};
        }
        unsigned int hexValue = std::stoul(fullHex, nullptr, 16);
        return glm::vec3(((hexValue >> 16) & 0xFF) / 255.0f, ((hexValue >> 8) & 0xFF) / 255.0f, (hexValue & 0xFF) / 255.0f);
    }
    
    // Create by ID
    EntityInstance CreateInstance(BaseSystem& baseSystem, int prototypeID, glm::vec3 position, glm::vec3 color) {
        if (!baseSystem.instance) return {};
        EntityInstance inst;
        inst.instanceID = baseSystem.instance->nextInstanceID++;
        inst.prototypeID = prototypeID;
        inst.position = position;
        inst.color = color;
        return inst;
    }
    
    // Create by name
    EntityInstance CreateInstance(BaseSystem& baseSystem, const std::vector<Entity>& prototypes, const std::string& name, glm::vec3 position, glm::vec3 color) {
        const Entity* proto = findPrototype(name, prototypes);
        if (proto) {
            return CreateInstance(baseSystem, proto->prototypeID, position, color);
        }
        std::cerr << "ERROR: Could not find prototype with name '" << name << "' to create instance." << std::endl;
        return {};
    }
}
