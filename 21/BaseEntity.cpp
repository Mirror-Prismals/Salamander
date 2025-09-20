// BaseEntity.cpp
#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <fstream>

// Forward declarations
struct BaseSystem;
struct EntityInstance;
struct Entity;

// --- JSON Deserialization ---
namespace glm {
    void from_json(const nlohmann::json& j, vec3& v) { j.at(0).get_to(v.x); j.at(1).get_to(v.y); j.at(2).get_to(v.z); }
}

void from_json(const nlohmann::json& j, Entity& e);

struct EntityInstance {
    int instanceID;
    int prototypeID;
    glm::vec3 position;
    float rotation = 0.0f;
    glm::vec3 color = glm::vec3(1.0f, 0.0f, 1.0f); // Default: Bright Pink for debugging
};

void from_json(const nlohmann::json& j, EntityInstance& inst) {
    j.at("prototypeID").get_to(inst.prototypeID);
    j.at("position").get_to(inst.position);
    if (j.contains("rotation")) {
        j.at("rotation").get_to(inst.rotation);
    }
}

struct Entity {
    int prototypeID;
    std::string name;

    // --- Core Properties (These now define behavior) ---
    bool isRenderable = false;
    bool isSolid = false;
    bool hasWireframe = false;
    bool isAnimated = false;
    bool isOccluder = false;
    float dampingFactor = 0.10f;

    // --- Special Container Flags ---
    bool isBlock = false;
    bool isWorld = false;
    bool isAudicle = false;
    bool isStar = false;
    bool isTesseract = false; // --- ADDED ---

    // --- Data Payload (for Audicles) ---
    std::vector<EntityInstance> instances;
};

void from_json(const nlohmann::json& j, Entity& e) {
    j.at("name").get_to(e.name);

    if (j.contains("isBlock") && j.at("isBlock").get<bool>()) {
        e.isBlock = true;
        e.isRenderable = true;
        e.isSolid = true;
        e.hasWireframe = false;
        e.isAnimated = false;
        e.isWorld = false;
        e.isAudicle = false;
        e.isStar = false;
    }

    if (j.contains("isRenderable")) j.at("isRenderable").get_to(e.isRenderable);
    if (j.contains("isSolid")) j.at("isSolid").get_to(e.isSolid);
    if (j.contains("hasWireframe")) j.at("hasWireframe").get_to(e.hasWireframe);
    if (j.contains("isAnimated")) j.at("isAnimated").get_to(e.isAnimated);
    if (j.contains("isWorld")) j.at("isWorld").get_to(e.isWorld);
    if (j.contains("isAudicle")) j.at("isAudicle").get_to(e.isAudicle);
    if (j.contains("isStar")) j.at("isStar").get_to(e.isStar);
    if (j.contains("isOccluder")) j.at("isOccluder").get_to(e.isOccluder);
    if (j.contains("dampingFactor")) j.at("dampingFactor").get_to(e.dampingFactor);
    if (j.contains("isTesseract")) j.at("isTesseract").get_to(e.isTesseract); // --- ADDED ---
                                        
    if (j.contains("instances")) {
        j.at("instances").get_to(e.instances);
    }
}
