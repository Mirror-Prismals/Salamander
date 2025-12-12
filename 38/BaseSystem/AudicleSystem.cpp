#pragma once
#include <algorithm>

namespace AudicleSystemLogic {
    void ProcessAudicles(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.level || baseSystem.level->worlds.empty() || !baseSystem.instance) return;
        Entity& activeWorld = baseSystem.level->worlds[baseSystem.level->activeWorldIndex];
        
        std::vector<int> finishedAudicleInstanceIDs;
        
        for (const auto& inst : activeWorld.instances) {
            const Entity& proto = prototypes[inst.prototypeID];
            
            // Handle one-shot event audicles
            if (proto.audicleType == "true") {
                // Handle "spawner" audicles
                if (!proto.instances.empty()) {
                    for (const auto& event : proto.instances) {
                        // This logic might need refinement if prototypes in instances are names
                        activeWorld.instances.push_back(HostLogic::CreateInstance(baseSystem, event.prototypeID, event.position, event.color));
                    }
                }
                finishedAudicleInstanceIDs.push_back(inst.instanceID);
            }
        }
        
        // Handle gated audicles (delete them at the end of the frame)
        for (const auto& inst : activeWorld.instances) {
            if (prototypes[inst.prototypeID].audicleType == "gated") {
                finishedAudicleInstanceIDs.push_back(inst.instanceID);
            }
        }
        
        if (!finishedAudicleInstanceIDs.empty()) {
            activeWorld.instances.erase(
                std::remove_if(activeWorld.instances.begin(), activeWorld.instances.end(),
                    [&](const EntityInstance& inst) {
                        return std::find(finishedAudicleInstanceIDs.begin(), finishedAudicleInstanceIDs.end(), inst.instanceID) != finishedAudicleInstanceIDs.end();
                    }),
                activeWorld.instances.end()
            );
        }
    }
}
