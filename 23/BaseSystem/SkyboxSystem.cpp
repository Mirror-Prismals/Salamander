#pragma once

namespace SkyboxSystemLogic {

    // Definition for getCurrentSkyColors now lives here
    void getCurrentSkyColors(float dayFraction, const std::vector<SkyColorKey>& skyKeys, glm::vec3& top, glm::vec3& bottom) {
        if (skyKeys.empty() || skyKeys.size() < 2) return;
        size_t i = 0;
        for (; i < skyKeys.size() - 1; i++) { if (dayFraction >= skyKeys[i].time && dayFraction <= skyKeys[i + 1].time) break; }
        if (i >= skyKeys.size() - 1) i = skyKeys.size() - 2;
        float t = (dayFraction - skyKeys[i].time) / (skyKeys[i + 1].time - skyKeys[i].time);
        top = glm::mix(skyKeys[i].top, skyKeys[i + 1].top, t);
        bottom = glm::mix(skyKeys[i].bottom, skyKeys[i + 1].bottom, t);
    }
    
    // We can move the skybox rendering logic here later, but for now this is all we need to fix the linker error.

}
