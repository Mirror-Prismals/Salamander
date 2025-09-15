#pragma once

class InstanceSystem : public ISystem {
private:
    int nextInstanceID = 0;
public:
    EntityInstance createInstance(int prototypeID, glm::vec3 position) {
        EntityInstance inst; inst.instanceID = nextInstanceID++; inst.prototypeID = prototypeID; inst.position = position; return inst;
    }
    void update(std::vector<Entity>& prototypes, BaseSystem& baseSystem, float deltaTime, GLFWwindow* window) override {}
};
