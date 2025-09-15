#pragma once

#include <memory>
#include <ctime>
#include <algorithm> 
#include <fstream>      
#include <sstream>
#include <string>
#include "json.hpp"

using json = nlohmann::json;

// --- Fix for JSON errors: Teach the library about GLM ---
namespace glm {
    void from_json(const json& j, vec3& v) {
        j.at(0).get_to(v.x);
        j.at(1).get_to(v.y);
        j.at(2).get_to(v.z);
    }
}

// --- Helper function for sky colors ---
void getCurrentSkyColors(float dayFraction, const std::vector<SkyColorKey>& skyKeys, glm::vec3& top, glm::vec3& bottom) {
    if (skyKeys.empty() || skyKeys.size() < 2) return;
    size_t i = 0;
    for (; i < skyKeys.size() - 1; i++) {
        if (dayFraction >= skyKeys[i].time && dayFraction <= skyKeys[i + 1].time) break;
    }
    if (i >= skyKeys.size() - 1) i = skyKeys.size() - 2;

    float t = (dayFraction - skyKeys[i].time) / (skyKeys[i + 1].time - skyKeys[i].time);
    top = glm::mix(skyKeys[i].top, skyKeys[i + 1].top, t);
    bottom = glm::mix(skyKeys[i].bottom, skyKeys[i + 1].bottom, t);
}


// --- System Interface ---
class ISystem {
public:
    virtual ~ISystem() {}
    virtual void update(std::vector<Entity>& prototypes, BaseSystem& baseSystem, float deltaTime, GLFWwindow* window) = 0;
};
