#pragma once

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

namespace UIStampingSystemLogic {
    namespace {
        constexpr float kRowSpacing = 72.0f;
        constexpr float kScrollSpeed = 24.0f;

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

        void parseUiColor(const json& j,
                          const char* key,
                          bool& hasColor,
                          std::string& name,
                          glm::vec3& value) {
            if (!j.contains(key)) return;
            const auto& entry = j.at(key);
            if (entry.is_string()) {
                name = entry.get<std::string>();
                hasColor = true;
                return;
            }
            if (entry.is_array() && entry.size() >= 3) {
                value = glm::vec3(entry[0].get<float>(), entry[1].get<float>(), entry[2].get<float>());
                name.clear();
                hasColor = true;
            }
        }

        UiStateColors parseUiStateEntry(const std::string& name, const json& data) {
            UiStateColors state;
            state.name = name;
            if (!data.is_object()) return state;
            parseUiColor(data, "color", state.hasFrontColor, state.frontColorName, state.frontColor);
            parseUiColor(data, "topColor", state.hasTopColor, state.topColorName, state.topColor);
            parseUiColor(data, "sideColor", state.hasSideColor, state.sideColorName, state.sideColor);
            return state;
        }

        void parseUiStates(const json& data, std::vector<UiStateColors>& out) {
            out.clear();
            if (data.is_object()) {
                for (auto& [key, value] : data.items()) {
                    out.push_back(parseUiStateEntry(key, value));
                }
            } else if (data.is_array()) {
                for (const auto& entry : data) {
                    if (!entry.is_object()) continue;
                    std::string name;
                    if (entry.contains("name")) name = entry["name"].get<std::string>();
                    if (name.empty()) continue;
                    out.push_back(parseUiStateEntry(name, entry));
                }
            }
        }

        void mergeUiStates(std::vector<UiStateColors>& target, const std::vector<UiStateColors>& overrides) {
            for (const auto& state : overrides) {
                auto it = std::find_if(target.begin(), target.end(),
                                       [&](const UiStateColors& existing) { return existing.name == state.name; });
                if (it != target.end()) {
                    *it = state;
                } else {
                    target.push_back(state);
                }
            }
        }

        void applyTokens(EntityInstance& target, const EntityInstance& source, int trackIndex) {
            target.actionType = replaceTrackTokens(source.actionType, trackIndex);
            target.actionKey = replaceTrackTokens(source.actionKey, trackIndex);
            target.actionValue = replaceTrackTokens(source.actionValue, trackIndex);
            target.textKey = replaceTrackTokens(source.textKey, trackIndex);
            target.text = replaceTrackTokens(source.text, trackIndex);
            target.controlId = replaceTrackTokens(source.controlId, trackIndex);
            target.controlRole = replaceTrackTokens(source.controlRole, trackIndex);
            target.styleId = replaceTrackTokens(source.styleId, trackIndex);
            target.uiState = replaceTrackTokens(source.uiState, trackIndex);
        }

        bool matchesRowOverride(const EntityInstance& inst, const MirrorRowOverride& ov, int trackIndex) {
            if (!ov.matchControlId.empty()) {
                if (inst.controlId != replaceTrackTokens(ov.matchControlId, trackIndex)) return false;
            }
            if (!ov.matchControlRole.empty()) {
                if (inst.controlRole != replaceTrackTokens(ov.matchControlRole, trackIndex)) return false;
            }
            if (!ov.matchName.empty()) {
                if (inst.name != replaceTrackTokens(ov.matchName, trackIndex)) return false;
            }
            return true;
        }

        void applyOverrideSet(EntityInstance& inst,
                              const json& setData,
                              int trackIndex,
                              const std::vector<Entity>& prototypes) {
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
            std::string prototypeName;
            setString("prototype", prototypeName);
            if (prototypeName.empty()) {
                setString("name", prototypeName);
            }
            if (!prototypeName.empty()) {
                if (const Entity* proto = HostLogic::findPrototype(prototypeName, prototypes)) {
                    inst.prototypeID = proto->prototypeID;
                    inst.name = prototypeName;
                }
            }
            if (setData.contains("prototypeID") && setData["prototypeID"].is_number_integer()) {
                int protoId = setData["prototypeID"].get<int>();
                if (protoId >= 0 && protoId < static_cast<int>(prototypes.size())) {
                    inst.prototypeID = protoId;
                    inst.name = prototypes[protoId].name;
                }
            }

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

            if (setData.contains("uiStates")) {
                std::vector<UiStateColors> overrideStates;
                parseUiStates(setData["uiStates"], overrideStates);
                if (!overrideStates.empty()) {
                    mergeUiStates(inst.uiStates, overrideStates);
                }
            }
        }

        void applyRowOverrides(EntityInstance& inst,
                               const std::vector<MirrorRowOverride>& overrides,
                               int row,
                               const std::vector<Entity>& prototypes) {
            for (const auto& ov : overrides) {
                if (ov.row != row) continue;
                if (!matchesRowOverride(inst, ov, row)) continue;
                applyOverrideSet(inst, ov.set, row, prototypes);
            }
        }

        int findWorldIndex(const LevelContext& level, const std::string& name) {
            for (size_t i = 0; i < level.worlds.size(); ++i) {
                if (level.worlds[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        bool loadWorldTemplateByPath(const std::string& path, Entity& outWorld) {
            std::ifstream worldFile(path);
            if (!worldFile.is_open()) return false;
            json data = json::parse(worldFile);
            outWorld = data.get<Entity>();
            return true;
        }

        void loadFallbackSource(const std::string& worldName,
                                std::vector<EntityInstance>& outInstances,
                                std::vector<float>& outBaseY) {
            std::string path;
            if (worldName == "TrackRowWorld") {
                path = "Entities/Worlds/daw_track_controls_world.json";
            } else if (worldName == "MidiTrackRowWorld") {
                path = "Entities/Worlds/midi_track_controls_world.json";
            } else {
                return;
            }
            Entity world;
            if (!loadWorldTemplateByPath(path, world)) return;
            outInstances = world.instances;
            outBaseY.clear();
            outBaseY.reserve(outInstances.size());
            for (const auto& inst : outInstances) {
                outBaseY.push_back(inst.position.y);
            }
        }

        float clampScroll(float scrollY, int rowCount) {
            int clampedRows = std::max(0, rowCount - 1);
            float minScroll = -kRowSpacing * static_cast<float>(clampedRows);
            if (scrollY < minScroll) return minScroll;
            if (scrollY > 0.0f) return 0.0f;
            return scrollY;
        }

        void stampRow(Entity& world,
                      const std::vector<EntityInstance>& sourceInstances,
                      const std::vector<float>& sourceBaseY,
                      const std::vector<MirrorRowOverride>& rowOverrides,
                      int row,
                      int tokenIndex,
                      bool preserveIds,
                      BaseSystem& baseSystem,
                      const std::vector<Entity>& prototypes) {
            world.instances.resize(sourceInstances.size());
            for (size_t i = 0; i < sourceInstances.size(); ++i) {
                EntityInstance inst = sourceInstances[i];
                if (preserveIds && i < world.instances.size()) {
                    inst.instanceID = world.instances[i].instanceID;
                    if (inst.instanceID <= 0 && baseSystem.instance) {
                        inst.instanceID = baseSystem.instance->nextInstanceID++;
                    }
                } else if (baseSystem.instance) {
                    inst.instanceID = baseSystem.instance->nextInstanceID++;
                }
                inst.position.y = sourceBaseY[i] + static_cast<float>(row) * baseSystem.uiStamp->rowSpacing;
                applyTokens(inst, sourceInstances[i], tokenIndex);
                applyRowOverrides(inst, rowOverrides, row, prototypes);
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())
                    || prototypes[inst.prototypeID].name != inst.name) {
                    if (const Entity* proto = HostLogic::findPrototype(inst.name, prototypes)) {
                        inst.prototypeID = proto->prototypeID;
                    }
                }
                world.instances[i] = std::move(inst);
            }
        }
    }

    void UpdateUIStamping(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt; (void)win;
        if (!baseSystem.level || !baseSystem.daw || !baseSystem.instance || !baseSystem.uiStamp) return;

        LevelContext& level = *baseSystem.level;
        DawContext& daw = *baseSystem.daw;
        UIStampingContext& stamp = *baseSystem.uiStamp;
        static bool g_debugPrinted = false;

        if (stamp.level != &level) {
            std::vector<MirrorRowOverride> rowOverrides = std::move(stamp.rowOverrides);
            stamp = UIStampingContext{};
            stamp.level = &level;
            stamp.rowOverrides = std::move(rowOverrides);
        }

        if (!stamp.cacheBuilt) {
            stamp.sourceWorldIndex = findWorldIndex(level, stamp.sourceWorldName);
            if (stamp.sourceWorldIndex < 0) {
                return;
            }
            Entity& sourceWorld = level.worlds[stamp.sourceWorldIndex];
            stamp.sourceInstances = sourceWorld.instances;
            stamp.sourceBaseY.clear();
            stamp.sourceBaseY.reserve(stamp.sourceInstances.size());
            for (const auto& inst : stamp.sourceInstances) {
                stamp.sourceBaseY.push_back(inst.position.y);
            }
            if (stamp.sourceInstances.empty()) {
                loadFallbackSource(stamp.sourceWorldName, stamp.sourceInstances, stamp.sourceBaseY);
            }
            stamp.midiSourceWorldIndex = findWorldIndex(level, stamp.midiSourceWorldName);
            stamp.midiSourceInstances.clear();
            stamp.midiSourceBaseY.clear();
            if (stamp.midiSourceWorldIndex >= 0 && stamp.midiSourceWorldIndex < static_cast<int>(level.worlds.size())) {
                Entity& midiWorld = level.worlds[stamp.midiSourceWorldIndex];
                stamp.midiSourceInstances = midiWorld.instances;
                stamp.midiSourceBaseY.reserve(stamp.midiSourceInstances.size());
                for (const auto& inst : stamp.midiSourceInstances) {
                    stamp.midiSourceBaseY.push_back(inst.position.y);
                }
            }
            if (stamp.midiSourceInstances.empty()) {
                loadFallbackSource(stamp.midiSourceWorldName, stamp.midiSourceInstances, stamp.midiSourceBaseY);
            }
            stamp.rowSpacing = kRowSpacing;
            stamp.rowWorldIndices.clear();
            stamp.stampedRows = 0;
            stamp.cacheBuilt = true;
        }

        int desiredRows = static_cast<int>(daw.laneOrder.size());
        if (desiredRows <= 0) {
            desiredRows = std::max(0, static_cast<int>(daw.tracks.size()));
        }
        if (!g_debugPrinted && desiredRows > 0) {
            g_debugPrinted = true;
            std::cerr << "[UIStamping] laneOrder=" << daw.laneOrder.size()
                      << " desiredRows=" << desiredRows
                      << " sourceWorldIndex=" << stamp.sourceWorldIndex
                      << " midiSourceWorldIndex=" << stamp.midiSourceWorldIndex
                      << " sourceInstances=" << stamp.sourceInstances.size()
                      << " midiSourceInstances=" << stamp.midiSourceInstances.size()
                      << std::endl;
            if (!daw.laneOrder.empty()) {
                for (int row = 0; row < desiredRows; ++row) {
                    int type = daw.laneOrder[static_cast<size_t>(row)].type;
                    size_t srcCount = (type == 1) ? stamp.midiSourceInstances.size() : stamp.sourceInstances.size();
                    std::cerr << "  row " << row << " type=" << type << " srcCount=" << srcCount << std::endl;
                }
            }
        }
        if (!daw.laneOrder.empty()) {
            int row0Type = daw.laneOrder.empty() ? 0 : daw.laneOrder[0].type;
            int row0Index = daw.laneOrder.empty() ? 0 : daw.laneOrder[0].trackIndex;
            if (row0Type == 0) {
                if (stamp.sourceWorldIndex >= 0 && stamp.sourceWorldIndex < static_cast<int>(level.worlds.size())) {
                    Entity& sourceWorld = level.worlds[stamp.sourceWorldIndex];
                    if (desiredRows > 0) {
                        stampRow(sourceWorld, stamp.sourceInstances, stamp.sourceBaseY, stamp.rowOverrides, 0, row0Index, true, baseSystem, prototypes);
                        if (stamp.rowWorldIndices.empty()) {
                            stamp.rowWorldIndices.push_back(stamp.sourceWorldIndex);
                        } else {
                            stamp.rowWorldIndices[0] = stamp.sourceWorldIndex;
                        }
                    } else {
                        sourceWorld.instances.clear();
                    }
                }
                if (stamp.midiSourceWorldIndex >= 0 && stamp.midiSourceWorldIndex < static_cast<int>(level.worlds.size())) {
                    level.worlds[stamp.midiSourceWorldIndex].instances.clear();
                }
            } else {
                if (stamp.midiSourceWorldIndex >= 0 && stamp.midiSourceWorldIndex < static_cast<int>(level.worlds.size())) {
                    Entity& midiWorld = level.worlds[stamp.midiSourceWorldIndex];
                    if (desiredRows > 0) {
                        stampRow(midiWorld, stamp.midiSourceInstances, stamp.midiSourceBaseY, stamp.rowOverrides, 0, row0Index, true, baseSystem, prototypes);
                        if (stamp.rowWorldIndices.empty()) {
                            stamp.rowWorldIndices.push_back(stamp.midiSourceWorldIndex);
                        } else {
                            stamp.rowWorldIndices[0] = stamp.midiSourceWorldIndex;
                        }
                    } else {
                        midiWorld.instances.clear();
                    }
                }
                if (stamp.sourceWorldIndex >= 0 && stamp.sourceWorldIndex < static_cast<int>(level.worlds.size())) {
                    level.worlds[stamp.sourceWorldIndex].instances.clear();
                }
            }
        }
        if (stamp.sourceWorldIndex >= 0 && stamp.sourceWorldIndex < static_cast<int>(level.worlds.size())) {
            Entity& sourceWorld = level.worlds[stamp.sourceWorldIndex];
            if (desiredRows > 0 && daw.laneOrder.empty()) {
                stampRow(sourceWorld, stamp.sourceInstances, stamp.sourceBaseY, stamp.rowOverrides, 0, 0, true, baseSystem, prototypes);
            } else if (daw.laneOrder.empty()) {
                sourceWorld.instances.clear();
            }
        }
        if (desiredRows > static_cast<int>(stamp.rowWorldIndices.size())) {
            for (int row = static_cast<int>(stamp.rowWorldIndices.size()); row < desiredRows; ++row) {
                bool useLane = !daw.laneOrder.empty();
                int type = useLane ? daw.laneOrder[static_cast<size_t>(row)].type : 0;
                int tokenIndex = useLane ? daw.laneOrder[static_cast<size_t>(row)].trackIndex : row;
                const auto& src = (type == 1) ? stamp.midiSourceInstances : stamp.sourceInstances;
                const auto& srcY = (type == 1) ? stamp.midiSourceBaseY : stamp.sourceBaseY;
                Entity newWorld;
                if (!src.empty()) {
                    int sourceIndex = (type == 1 && stamp.midiSourceWorldIndex >= 0)
                        ? stamp.midiSourceWorldIndex
                        : stamp.sourceWorldIndex;
                    newWorld = level.worlds[sourceIndex];
                } else if (stamp.sourceWorldIndex >= 0 && stamp.sourceWorldIndex < static_cast<int>(level.worlds.size())) {
                    newWorld = level.worlds[stamp.sourceWorldIndex];
                }
                newWorld.name = (type == 1 ? stamp.midiSourceWorldName : stamp.sourceWorldName) + "_" + std::to_string(row);
                stampRow(newWorld, src, srcY, stamp.rowOverrides, row, tokenIndex, false, baseSystem, prototypes);
                level.worlds.push_back(std::move(newWorld));
                stamp.rowWorldIndices.push_back(static_cast<int>(level.worlds.size() - 1));
            }
            stamp.stampedRows = static_cast<int>(stamp.rowWorldIndices.size());
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            if (baseSystem.daw) baseSystem.daw->uiCacheBuilt = false;
            if (baseSystem.midi) baseSystem.midi->uiCacheBuilt = false;
        }
        if (desiredRows < static_cast<int>(stamp.rowWorldIndices.size())) {
            for (int row = desiredRows; row < static_cast<int>(stamp.rowWorldIndices.size()); ++row) {
                int worldIndex = stamp.rowWorldIndices[row];
                if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) continue;
                level.worlds[worldIndex].instances.clear();
            }
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            if (baseSystem.daw) baseSystem.daw->uiCacheBuilt = false;
            if (baseSystem.midi) baseSystem.midi->uiCacheBuilt = false;
        }

        if (baseSystem.ui) {
            UIContext& ui = *baseSystem.ui;
            if (ui.active && !ui.loadingActive) {
                if (ui.mainScrollDelta != 0.0) {
                    stamp.scrollY += static_cast<float>(ui.mainScrollDelta) * kScrollSpeed;
                    stamp.scrollY = clampScroll(stamp.scrollY, desiredRows);
                }
                if (ui.panelScrollDelta != 0.0) {
                    stamp.panelScrollY += static_cast<float>(ui.panelScrollDelta) * kScrollSpeed;
                    stamp.panelScrollY = clampScroll(stamp.panelScrollY, desiredRows);
                }
            }
        }

        for (int row = 0; row < desiredRows; ++row) {
            if (row < 0 || row >= static_cast<int>(stamp.rowWorldIndices.size())) continue;
            int worldIndex = stamp.rowWorldIndices[row];
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) continue;
            Entity& world = level.worlds[worldIndex];
            bool preserveIds = (row == 0);
            bool useLane = !daw.laneOrder.empty();
            int type = useLane ? daw.laneOrder[static_cast<size_t>(row)].type : 0;
            int tokenIndex = useLane ? daw.laneOrder[static_cast<size_t>(row)].trackIndex : row;
            const auto& src = (type == 1) ? stamp.midiSourceInstances : stamp.sourceInstances;
            const auto& srcY = (type == 1) ? stamp.midiSourceBaseY : stamp.sourceBaseY;
            world.name = (type == 1 ? stamp.midiSourceWorldName : stamp.sourceWorldName) + "_" + std::to_string(row);
            if (useLane) {
                stampRow(world, src, srcY, stamp.rowOverrides, row, tokenIndex, preserveIds, baseSystem, prototypes);
            } else if (world.instances.size() != src.size()) {
                stampRow(world, src, srcY, stamp.rowOverrides, row, tokenIndex, preserveIds, baseSystem, prototypes);
            } else {
                for (size_t i = 0; i < src.size(); ++i) {
                    applyTokens(world.instances[i], src[i], tokenIndex);
                    applyRowOverrides(world.instances[i], stamp.rowOverrides, row, prototypes);
                }
            }
            size_t count = std::min(world.instances.size(), srcY.size());
            float rowOffset = static_cast<float>(row) * stamp.rowSpacing + stamp.panelScrollY;
            for (size_t i = 0; i < count; ++i) {
                world.instances[i].position.y = srcY[i] + rowOffset;
            }
        }
    }
}
