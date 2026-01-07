#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace MirrorSystemLogic {
    namespace {
        void replaceAll(std::string& value, const std::string& token, const std::string& replacement) {
            if (value.empty() || token.empty()) return;
            size_t pos = 0;
            while ((pos = value.find(token, pos)) != std::string::npos) {
                value.replace(pos, token.size(), replacement);
                pos += replacement.size();
            }
        }

        std::string replaceTrackTokens(const std::string& value, int trackIndex) {
            std::string result = value;
            replaceAll(result, "{track+1}", std::to_string(trackIndex + 1));
            replaceAll(result, "{track}", std::to_string(trackIndex));
            return result;
        }

        void applyTokenReplacement(EntityInstance& inst, int trackIndex) {
            inst.actionType = replaceTrackTokens(inst.actionType, trackIndex);
            inst.actionKey = replaceTrackTokens(inst.actionKey, trackIndex);
            inst.actionValue = replaceTrackTokens(inst.actionValue, trackIndex);
            inst.text = replaceTrackTokens(inst.text, trackIndex);
            inst.textKey = replaceTrackTokens(inst.textKey, trackIndex);
            inst.controlId = replaceTrackTokens(inst.controlId, trackIndex);
            inst.controlRole = replaceTrackTokens(inst.controlRole, trackIndex);
            inst.styleId = replaceTrackTokens(inst.styleId, trackIndex);
            inst.uiState = replaceTrackTokens(inst.uiState, trackIndex);
        }

        bool matchesOverride(const EntityInstance& inst, const MirrorOverride& ov) {
            if (!ov.matchControlId.empty() && inst.controlId != ov.matchControlId) return false;
            if (!ov.matchControlRole.empty() && inst.controlRole != ov.matchControlRole) return false;
            if (!ov.matchName.empty() && inst.name != ov.matchName) return false;
            return true;
        }

        void applySet(EntityInstance& inst, const json& setData, int trackIndex) {
            if (!setData.is_object()) return;
            auto setString = [&](const char* key, std::string& target) {
                if (setData.contains(key) && setData[key].is_string()) {
                    target = replaceTrackTokens(setData[key].get<std::string>(), trackIndex);
                }
            };
            auto setVec3 = [&](const char* key, glm::vec3& target, std::string& nameTarget) {
                if (!setData.contains(key)) return;
                const auto& value = setData[key];
                if (value.is_string()) {
                    nameTarget = value.get<std::string>();
                } else if (value.is_array() && value.size() >= 3) {
                    target = glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
                    nameTarget.clear();
                }
            };

            setString("text", inst.text);
            setString("textType", inst.textType);
            setString("textKey", inst.textKey);
            setString("font", inst.font);
            setString("action", inst.actionType);
            setString("actionKey", inst.actionKey);
            setString("actionValue", inst.actionValue);
            setString("buttonMode", inst.buttonMode);
            setString("controlId", inst.controlId);
            setString("controlRole", inst.controlRole);
            setString("styleId", inst.styleId);
            setString("uiState", inst.uiState);

            if (setData.contains("rotation") && setData["rotation"].is_number()) {
                inst.rotation = setData["rotation"].get<float>();
            }
            if (setData.contains("position") && setData["position"].is_array() && setData["position"].size() >= 3) {
                inst.position = glm::vec3(setData["position"][0].get<float>(),
                                          setData["position"][1].get<float>(),
                                          setData["position"][2].get<float>());
            }
            if (setData.contains("size")) {
                const auto& value = setData["size"];
                if (value.is_number()) {
                    float s = value.get<float>();
                    inst.size = glm::vec3(s);
                } else if (value.is_array() && value.size() >= 3) {
                    inst.size = glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
                }
            }

            setVec3("color", inst.color, inst.colorName);
            setVec3("topColor", inst.topColor, inst.topColorName);
            setVec3("sideColor", inst.sideColor, inst.sideColorName);
        }

        void applyOverrides(EntityInstance& inst, const std::vector<MirrorOverride>& overrides, int trackIndex) {
            for (const auto& ov : overrides) {
                if (!matchesOverride(inst, ov)) continue;
                applySet(inst, ov.set, trackIndex);
            }
        }

        bool loadWorldTemplate(const std::string& worldName, Entity& outWorld) {
            std::vector<std::string> searchPaths = {
                "Entities/Worlds/" + worldName,
                "Entities/Audicles/Worlds/" + worldName,
                "Entities/Worlds/Audicles/" + worldName
            };
            std::ifstream worldFile;
            std::string foundPath;
            for (const auto& path : searchPaths) {
                worldFile.open(path);
                if (worldFile.is_open()) {
                    foundPath = path;
                    break;
                }
            }
            if (!worldFile.is_open()) return false;
            json data = json::parse(worldFile);
            outWorld = data.get<Entity>();
            return true;
        }

        Entity buildWorldInstance(BaseSystem& baseSystem,
                                  std::vector<Entity>& prototypes,
                                  const Entity& templateWorld,
                                  const MirrorWorldInstance& mirrorInst,
                                  int repeatIndex) {
            Entity worldProto = templateWorld;
            if (repeatIndex > 0) {
                worldProto.name = templateWorld.name + "_" + std::to_string(repeatIndex);
            }

            if (worldProto.isVolume || worldProto.instances.empty()) return worldProto;

            std::vector<EntityInstance> processedInstances;
            std::vector<EntityInstance> templates = worldProto.instances;
            bool preserveTokens = baseSystem.uiStamp
                && templateWorld.name == baseSystem.uiStamp->sourceWorldName
                && mirrorInst.repeatCount <= 1;

            for (const auto& instTemplate : templates) {
                EntityInstance working = instTemplate;
                if (!preserveTokens) {
                    applyTokenReplacement(working, repeatIndex);
                    applyOverrides(working, mirrorInst.overrides, repeatIndex);
                }
                if (repeatIndex > 0) {
                    working.position += mirrorInst.repeatOffset * static_cast<float>(repeatIndex);
                }

                const Entity* entityProto = HostLogic::findPrototype(working.name, prototypes);
                if (!entityProto) {
                    std::cerr << "MirrorSystem: missing prototype '" << working.name
                              << "' for world '" << worldProto.name << "'." << std::endl;
                    continue;
                }

                int count = entityProto->count > 1 ? entityProto->count : 1;
                if (entityProto->name == "Star") count = 2000;

                for (int i = 0; i < count; ++i) {
                    glm::vec3 pos = working.position;
                    glm::vec3 instColor = working.color;
                    if (baseSystem.world && !working.colorName.empty()) {
                        auto it = baseSystem.world->colorLibrary.find(working.colorName);
                        if (it != baseSystem.world->colorLibrary.end()) instColor = it->second;
                    }
                    if (entityProto->isStar) {
                        std::random_device rd;
                        std::mt19937 gen(rd());
                        std::uniform_real_distribution<> distrib(0.0, 1.0);
                        float theta = distrib(gen) * 2.0f * 3.14159f;
                        float phi = distrib(gen) * 3.14159f;
                        pos = glm::vec3(sin(phi) * cos(theta),
                                        cos(phi),
                                        sin(phi) * sin(theta)) * 200.0f;
                    }
                    EntityInstance inst = HostLogic::CreateInstance(baseSystem, entityProto->prototypeID, pos, instColor);
                    inst.size = working.size;
                    inst.rotation = working.rotation;
                    inst.text = working.text;
                    inst.textType = working.textType;
                    inst.textKey = working.textKey;
                    inst.font = working.font;
                    inst.colorName = working.colorName;
                    inst.topColor = working.topColor;
                    inst.topColorName = working.topColorName;
                    inst.sideColor = working.sideColor;
                    inst.sideColorName = working.sideColorName;
                    inst.actionType = working.actionType;
                    inst.actionKey = working.actionKey;
                    inst.actionValue = working.actionValue;
                    inst.buttonMode = working.buttonMode;
                    inst.controlId = working.controlId;
                    inst.controlRole = working.controlRole;
                    inst.styleId = working.styleId;
                    inst.uiState = working.uiState;
                    inst.uiStates = working.uiStates;
                    processedInstances.push_back(inst);
                }
            }
            worldProto.instances = std::move(processedInstances);
            return worldProto;
        }

        void clearExpandedWorlds(BaseSystem& baseSystem, MirrorContext& mirror) {
            if (!baseSystem.level) return;
            LevelContext& level = *baseSystem.level;
            std::vector<int> indices = mirror.expandedWorldIndices;
            if (baseSystem.uiStamp && baseSystem.uiStamp->level == &level) {
                indices.insert(indices.end(),
                               baseSystem.uiStamp->rowWorldIndices.begin(),
                               baseSystem.uiStamp->rowWorldIndices.end());
                baseSystem.uiStamp->rowWorldIndices.clear();
            }
            if (indices.empty()) {
                mirror.expandedWorldIndices.clear();
                return;
            }
            std::sort(indices.begin(), indices.end());
            indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
            std::sort(indices.begin(), indices.end(), std::greater<int>());
            for (int index : indices) {
                if (index < 0 || index >= static_cast<int>(level.worlds.size())) continue;
                level.worlds.erase(level.worlds.begin() + index);
            }
            mirror.expandedWorldIndices.clear();
        }

        void resetUiCaches(BaseSystem& baseSystem) {
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            if (baseSystem.daw) baseSystem.daw->uiCacheBuilt = false;
            if (baseSystem.uiStamp) {
                baseSystem.uiStamp->cacheBuilt = false;
                baseSystem.uiStamp->level = nullptr;
                baseSystem.uiStamp->rowWorldIndices.clear();
                baseSystem.uiStamp->rowOverrides.clear();
            }
        }
    }

    void UpdateMirrors(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt;
        if (!baseSystem.mirror || !baseSystem.level || !baseSystem.instance) return;
        MirrorContext& mirror = *baseSystem.mirror;
        LevelContext& level = *baseSystem.level;

        if (mirror.mirrors.empty()) return;
        if (mirror.activeMirrorIndex < 0 || mirror.activeMirrorIndex >= static_cast<int>(mirror.mirrors.size())) {
            mirror.activeMirrorIndex = 0;
        }

        bool mirrorChanged = false;
        if (baseSystem.ui) {
            UIContext& ui = *baseSystem.ui;
            if (ui.active) {
                int deviceId = ui.activeInstanceID;
                if (deviceId != mirror.activeDeviceInstanceID) {
                    mirror.activeDeviceInstanceID = deviceId;
                    int desired = mirror.activeMirrorIndex;
                    auto it = mirror.deviceMirrorIndex.find(deviceId);
                    if (it != mirror.deviceMirrorIndex.end()) {
                        desired = it->second;
                    }
                    if (desired < 0 || desired >= static_cast<int>(mirror.mirrors.size())) {
                        desired = 0;
                    }
                    mirror.activeMirrorIndex = desired;
                    mirror.deviceMirrorIndex[deviceId] = desired;
                    mirrorChanged = true;
                }

                static bool upLast = false;
                static bool downLast = false;
                bool up = win && glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS;
                bool down = win && glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS;
                if (up && !upLast) {
                    int count = static_cast<int>(mirror.mirrors.size());
                    mirror.activeMirrorIndex = (mirror.activeMirrorIndex - 1 + count) % count;
                    mirror.deviceMirrorIndex[deviceId] = mirror.activeMirrorIndex;
                    mirrorChanged = true;
                }
                if (down && !downLast) {
                    int count = static_cast<int>(mirror.mirrors.size());
                    mirror.activeMirrorIndex = (mirror.activeMirrorIndex + 1) % count;
                    mirror.deviceMirrorIndex[deviceId] = mirror.activeMirrorIndex;
                    mirrorChanged = true;
                }
                upLast = up;
                downLast = down;
            }
        }

        if (mirrorChanged) {
            mirror.expanded = false;
            mirror.expandedMirrorIndex = -1;
        }

        if (mirror.activeMirrorIndex < 0) return;

        if (mirror.expanded && mirror.expandedMirrorIndex == mirror.activeMirrorIndex) return;

        clearExpandedWorlds(baseSystem, mirror);
        resetUiCaches(baseSystem);

        const MirrorDefinition& def = mirror.mirrors[mirror.activeMirrorIndex];
        mirror.uiOffset = def.uiOffset;
        mirror.uiScale = def.uiScale;
        if (baseSystem.uiStamp) {
            baseSystem.uiStamp->rowOverrides.clear();
        }

        for (const auto& worldInst : def.worldInstances) {
            Entity templateWorld;
            if (!loadWorldTemplate(worldInst.worldName, templateWorld)) {
                std::cerr << "MirrorSystem: missing world template '" << worldInst.worldName << "'" << std::endl;
                continue;
            }
            if (baseSystem.uiStamp
                && templateWorld.name == baseSystem.uiStamp->sourceWorldName
                && worldInst.repeatCount <= 1) {
                baseSystem.uiStamp->rowOverrides = worldInst.rowOverrides;
            }
            int repeatCount = std::max(1, worldInst.repeatCount);
            for (int i = 0; i < repeatCount; ++i) {
                Entity newWorld = buildWorldInstance(baseSystem, prototypes, templateWorld, worldInst, i);
                mirror.expandedWorldIndices.push_back(static_cast<int>(level.worlds.size()));
                level.worlds.push_back(std::move(newWorld));
            }
        }

        mirror.expanded = true;
        mirror.expandedMirrorIndex = mirror.activeMirrorIndex;
        resetUiCaches(baseSystem);
    }
}
