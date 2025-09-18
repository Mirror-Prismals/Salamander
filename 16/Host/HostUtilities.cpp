#pragma once

namespace HostLogic {
    glm::vec3 hexToVec3(const std::string& hex) {
        std::string fullHex = hex.substr(1);
        if (fullHex.length() == 3) {
            char r = fullHex[0], g = fullHex[1], b = fullHex[2];
            fullHex = {r, r, g, g, b, b};
        }
        unsigned int hexValue = std::stoul(fullHex, nullptr, 16);
        return glm::vec3(((hexValue >> 16) & 0xFF) / 255.0f, ((hexValue >> 8) & 0xFF) / 255.0f, (hexValue & 0xFF) / 255.0f);
    }
    
    EntityInstance CreateInstance(BaseSystem& baseSystem, int prototypeID, glm::vec3 position, glm::vec3 color) {
        if (!baseSystem.instance) return {};
        EntityInstance inst;
        inst.instanceID = baseSystem.instance->nextInstanceID++;
        inst.prototypeID = prototypeID;
        inst.position = position;
        inst.color = color;
        return inst;
    }
}
