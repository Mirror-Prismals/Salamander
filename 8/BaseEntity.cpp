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

    // --- Core Properties ---
    bool isRenderable = false;
    int blockType = -1;
    
    // --- Special Container Flags ---
    bool isWorld = false;
    bool isAudicle = false; // Audicles are a special type of entity container
    bool isStar = false;

    // --- Data Payload ---
    // A list of instances contained within this entity.
    // For a World, these are the blocks/mobs.
    // For an Audicle, these can be the "notes" or events on its timeline.
    std::vector<EntityInstance> instances; 
};

// A lightweight struct representing an INSTANCE of an entity in the world OR in an audicle.
struct EntityInstance {
    int instanceID;
    int prototypeID;
    glm::vec3 position;
    float rotation = 0.0f;
};
