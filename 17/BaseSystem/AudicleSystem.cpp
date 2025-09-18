#pragma once

#include <algorithm> // for std::remove_if, std::find

namespace AudicleSystemLogic {
    // This is the generic logic that handles audicles which spawn other entities.
    // It is NOT responsible for the pink noise visualization.
    void ProcessAudicles(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.world || !baseSystem.instance) { 
            return; 
        }
        
        Entity* worldEntity = nullptr;
        for (auto& proto : prototypes) { 
            if (proto.isWorld) { 
                worldEntity = &proto; 
                break; 
            } 
        }
        if (!worldEntity) return;
        
        std::vector<int> finishedAudicleInstanceIDs;
        
        // Find audicles that have instance payloads to process
        for (const auto& inst : worldEntity->instances) {
            // Check if the instance's prototype is an audicle AND has instances to spawn
            if (prototypes[inst.prototypeID].isAudicle && !prototypes[inst.prototypeID].instances.empty()) {
                Entity& audicleProto = prototypes[inst.prototypeID];
                
                // Spawn the instances into the world
                for (const auto& event : audicleProto.instances) {
                    worldEntity->instances.push_back(HostLogic::CreateInstance(baseSystem, event.prototypeID, event.position, event.color));
                }
                
                // Clear the payload and mark this audicle instance as finished
                audicleProto.instances.clear();
                finishedAudicleInstanceIDs.push_back(inst.instanceID);
            }
        }
        
        // Remove the processed "spawner" audicle instances from the world
        if (!finishedAudicleInstanceIDs.empty()) {
            worldEntity->instances.erase(
                std::remove_if(worldEntity->instances.begin(), worldEntity->instances.end(),
                    [&](const EntityInstance& inst) {
                        return std::find(finishedAudicleInstanceIDs.begin(), finishedAudicleInstanceIDs.end(), inst.instanceID) != finishedAudicleInstanceIDs.end();
                    }),
                worldEntity->instances.end()
            );
        }
    }
}
