#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <fstream> // For std::ifstream

// Forward declarations
struct BaseSystem;
struct EntityInstance;
struct Entity;

// --- JSON Deserialization ---
namespace glm {
    void from_json(const nlohmann::json& j, vec3& v) { j.at(0).get_to(v.x); j.at(1).get_to(v.y); j.at(2).get_to(v.z); }
}

// Forward declare from_json for Entity to be used in EntityInstance
void from_json(const nlohmann::json& j, Entity& e);

// A lightweight struct representing an INSTANCE of an entity in the world OR in an audicle.
struct EntityInstance {
    int instanceID;
    int prototypeID;
    glm::vec3 position;
    float rotation = 0.0f;
};

void from_json(const nlohmann::json& j, EntityInstance& inst) {
    // We don't load instanceID from here, it's a runtime value.
    j.at("prototypeID").get_to(inst.prototypeID);
    j.at("position").get_to(inst.position);
    if (j.contains("rotation")) {
        j.at("rotation").get_to(inst.rotation);
    }
}


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

void from_json(const nlohmann::json& j, Entity& e) {
    // We don't load prototypeID from here, it's assigned on load.
    j.at("name").get_to(e.name);
    j.at("isRenderable").get_to(e.isRenderable);
    j.at("blockType").get_to(e.blockType);
    j.at("isWorld").get_to(e.isWorld);
    j.at("isAudicle").get_to(e.isAudicle);
    j.at("isStar").get_to(e.isStar);
    if (j.contains("instances")) {
        j.at("instances").get_to(e.instances);
    }
}
