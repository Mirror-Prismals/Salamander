#pragma once

namespace AudicleSystemLogic {
    void ProcessAudicles(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.world || !baseSystem.instance) { std::cerr << "ERROR: AudicleSystem cannot run without WorldContext or InstanceContext." << std::endl; return; }
        Entity* worldEntity = nullptr;
        for (auto& proto : prototypes) { if (proto.isWorld) { worldEntity = &proto; break; } }
        if (!worldEntity) return;
        std::vector<int> finishedAudicleInstanceIDs;
        for (const auto& inst : worldEntity->instances) {
            if (prototypes[inst.prototypeID].isAudicle) {
                Entity& audicleProto = prototypes[inst.prototypeID];
                for (const auto& event : audicleProto.instances) {
                    worldEntity->instances.push_back(HostLogic::CreateInstance(baseSystem, event.prototypeID, event.position, event.color));
                }
                audicleProto.instances.clear();
                finishedAudicleInstanceIDs.push_back(inst.instanceID);
            }
        }
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
