#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>

// Forward declaration
struct EntityInstance;

// The "Base Entity" struct. This is a PROTOTYPE.
struct Entity {
    int prototypeID;
    std::string name;
    bool isRenderable = false;
    int blockType = -1;
    bool isWorld = false;
    std::vector<EntityInstance> instances;
    bool isSkybox = false;
    bool isSun = false;
    bool isMoon = false;
    bool isStar = false;
};

// A lightweight struct representing an INSTANCE of an entity in the world.
struct EntityInstance {
    int instanceID;
    int prototypeID;
    glm::vec3 position;
    float rotation = 0.0f;
};
