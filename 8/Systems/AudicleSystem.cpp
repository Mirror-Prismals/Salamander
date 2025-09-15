#pragma once

class AudicleSystem : public ISystem {
private:
    InstanceSystem instance_factory; 
public:
    void update(std::vector<Entity>& prototypes, BaseSystem& baseSystem, float deltaTime, GLFWwindow* window) override {
        Entity* worldEntity = nullptr;
        for (auto& proto : prototypes) { if (proto.isWorld) { worldEntity = &proto; break; } }
        if (!worldEntity) return;
        std::vector<int> finishedAudicleInstanceIDs;
        for (const auto& inst : worldEntity->instances) {
            if (prototypes[inst.prototypeID].isAudicle) {
                Entity& audicleProto = prototypes[inst.prototypeID];
                for (const auto& event : audicleProto.instances) {
                    bool blockExists = false;
                    for (const auto& worldInst : worldEntity->instances) {
                        if (worldInst.prototypeID != audicleProto.prototypeID && glm::distance(worldInst.position, event.position) < 0.1f) {
                            blockExists = true; break;
                        }
                    }
                    if (!blockExists) { worldEntity->instances.push_back(instance_factory.createInstance(event.prototypeID, event.position)); }
                }
                audicleProto.instances.clear();
                finishedAudicleInstanceIDs.push_back(inst.instanceID);
            }
        }
        if (!finishedAudicleInstanceIDs.empty()) {
            worldEntity->instances.erase(
                std::remove_if(worldEntity->instances.begin(), worldEntity->instances.end(),
                    [&](const EntityInstance& inst) {
                        for (int finishedID : finishedAudicleInstanceIDs) { if (inst.instanceID == finishedID) return true; }
                        return false;
                    }),
                worldEntity->instances.end()
            );
        }
    }
};
