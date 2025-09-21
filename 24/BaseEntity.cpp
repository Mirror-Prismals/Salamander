#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <fstream>

struct BaseSystem; struct EntityInstance; struct Entity;

namespace glm {
    void from_json(const nlohmann::json& j, vec3& v) { j.at(0).get_to(v.x); j.at(1).get_to(v.y); j.at(2).get_to(v.z); }
}

void from_json(const nlohmann::json& j, Entity& e);

struct EntityInstance {
    int instanceID;
    int prototypeID;
    std::string name; // For looking up prototypeID during level load
    glm::vec3 position;
    float rotation = 0.0f;
    glm::vec3 color = glm::vec3(1.0f, 0.0f, 1.0f);
};

void from_json(const nlohmann::json& j, EntityInstance& inst) {
    if (j.contains("name")) j.at("name").get_to(inst.name);
    if (j.contains("prototypeID")) j.at("prototypeID").get_to(inst.prototypeID);
    if (j.contains("position")) j.at("position").get_to(inst.position);
    if (j.contains("rotation")) j.at("rotation").get_to(inst.rotation);
}

struct Entity {
    int prototypeID;
    std::string name;
    bool isRenderable = false; bool isSolid = false; bool hasWireframe = false;
    bool isAnimated = false; bool isOccluder = false; float dampingFactor = 0.10f;
    bool isBlock = false; bool isWorld = false; std::string audicleType = "false";
    bool isStar = false; bool isVolume = false;
    glm::vec3 fillOrigin; glm::vec3 fillDimensions;
    std::string fillBlockType; std::string fillColor;
    int count = 1;
    std::vector<EntityInstance> instances;
};

void from_json(const nlohmann::json& j, Entity& e) {
    j.at("name").get_to(e.name);
    if (j.contains("isBlock") && j.at("isBlock").get<bool>()) { e.isBlock = true; e.isRenderable = true; e.isSolid = true; }
    if (j.contains("isRenderable")) j.at("isRenderable").get_to(e.isRenderable);
    if (j.contains("isSolid")) j.at("isSolid").get_to(e.isSolid);
    if (j.contains("hasWireframe")) j.at("hasWireframe").get_to(e.hasWireframe);
    if (j.contains("isAnimated")) j.at("isAnimated").get_to(e.isAnimated);
    if (j.contains("isWorld")) j.at("isWorld").get_to(e.isWorld);
    if (j.contains("audicleType")) j.at("audicleType").get_to(e.audicleType);
    if (j.contains("isStar")) j.at("isStar").get_to(e.isStar);
    if (j.contains("isVolume")) j.at("isVolume").get_to(e.isVolume);
    if (j.contains("isOccluder")) j.at("isOccluder").get_to(e.isOccluder);
    if (j.contains("dampingFactor")) j.at("dampingFactor").get_to(e.dampingFactor);
    if (j.contains("fillOrigin")) j.at("fillOrigin").get_to(e.fillOrigin);
    if (j.contains("fillDimensions")) j.at("fillDimensions").get_to(e.fillDimensions);
    if (j.contains("fillBlockType")) j.at("fillBlockType").get_to(e.fillBlockType);
    if (j.contains("fillColor")) j.at("fillColor").get_to(e.fillColor);
    if (j.contains("count")) j.at("count").get_to(e.count);
    if (j.contains("instances")) j.at("instances").get_to(e.instances);
}
