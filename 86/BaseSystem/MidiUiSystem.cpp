#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ButtonSystemLogic {
    void SetButtonToggled(int instanceID, bool toggled);
}

namespace MidiUiSystemLogic {

    namespace {
        constexpr float kLaneLeftMargin = 40.0f;
        constexpr float kLaneRightMargin = 40.0f;

        int getTrackCount(const MidiContext& midi) {
            return static_cast<int>(midi.tracks.size());
        }

        int parseTrackIndex(const std::string& value, int trackCount) {
            if (value.empty()) return -1;
            try {
                int idx = std::stoi(value);
                if (idx < 0 || idx >= trackCount) return -1;
                return idx;
            } catch (...) {
                return -1;
            }
        }

        int parseTrackIndexFromControlId(const std::string& controlId, int trackCount) {
            if (controlId.rfind("midi_track_", 0) != 0) return -1;
            size_t start = 11;
            size_t end = controlId.find('_', start);
            if (end == std::string::npos) return -1;
            return parseTrackIndex(controlId.substr(start, end - start), trackCount);
        }

        int parseTrackIndexUncheckedFromControlId(const std::string& controlId) {
            if (controlId.rfind("midi_track_", 0) != 0) return -1;
            size_t start = 11;
            size_t end = controlId.find('_', start);
            if (end == std::string::npos) return -1;
            try {
                int idx = std::stoi(controlId.substr(start, end - start));
                return idx >= 0 ? idx : -1;
            } catch (...) {
                return -1;
            }
        }

        const char* busLabelForIndex(int busIndex) {
            switch (busIndex) {
                case 0: return "L";
                case 1: return "S";
                case 2: return "F";
                case 3: return "R";
                case -1: return "OFF";
                default: return "";
            }
        }

        const char* busStateForIndex(int busIndex) {
            switch (busIndex) {
                case 0: return "bus_L";
                case 1: return "bus_S";
                case 2: return "bus_F";
                case 3: return "bus_R";
                default: return "idle";
            }
        }

        int findWorldIndex(const LevelContext& level, const std::string& name) {
            for (size_t i = 0; i < level.worlds.size(); ++i) {
                if (level.worlds[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        bool isTrackRowWorldName(const std::string& worldName) {
            return worldName.rfind("TrackRowWorld", 0) == 0
                || worldName.rfind("MidiTrackRowWorld", 0) == 0;
        }

        void buildActiveTrackWorldSet(const BaseSystem& baseSystem, std::unordered_set<int>& out) {
            out.clear();
            if (!baseSystem.uiStamp || !baseSystem.level) return;
            const auto& indices = baseSystem.uiStamp->rowWorldIndices;
            out.reserve(indices.size());
            for (int idx : indices) {
                if (idx >= 0 && idx < static_cast<int>(baseSystem.level->worlds.size())) {
                    out.insert(idx);
                }
            }
        }

        bool shouldSkipTrackWorldByIndex(const BaseSystem& baseSystem,
                                         int worldIndex,
                                         const Entity& world,
                                         const std::unordered_set<int>& activeTrackWorlds) {
            if (!isTrackRowWorldName(world.name)) return false;
            if (!baseSystem.uiStamp) return false;
            if (activeTrackWorlds.empty()) return false;
            return activeTrackWorlds.find(worldIndex) == activeTrackWorlds.end();
        }

        void updateMidiButtonVisuals(BaseSystem& baseSystem, MidiContext& midi) {
            (void)baseSystem;
            int trackCount = getTrackCount(midi);
            for (auto* instPtr : midi.trackInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.actionKey == "arm") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0) trackIndex = parseTrackIndexFromControlId(inst.controlId, trackCount);
                    if (trackIndex < 0) continue;
                    int armMode = midi.tracks[trackIndex].armMode.load(std::memory_order_relaxed);
                    if (armMode == 1) inst.uiState = "overdub";
                    else if (armMode == 2) inst.uiState = "replace";
                    else inst.uiState = "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, armMode > 0);
                } else if (inst.actionKey == "solo") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0) trackIndex = parseTrackIndexFromControlId(inst.controlId, trackCount);
                    if (trackIndex < 0) continue;
                    bool active = midi.tracks[trackIndex].solo.load(std::memory_order_relaxed);
                    inst.uiState = active ? "active" : "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, active);
                } else if (inst.actionKey == "mute") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0) trackIndex = parseTrackIndexFromControlId(inst.controlId, trackCount);
                    if (trackIndex < 0) continue;
                    bool active = midi.tracks[trackIndex].mute.load(std::memory_order_relaxed);
                    inst.uiState = active ? "active" : "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, active);
                } else if (inst.actionKey == "output") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0) trackIndex = parseTrackIndexFromControlId(inst.controlId, trackCount);
                    if (trackIndex < 0) continue;
                    int currentBus = midi.tracks[trackIndex].outputBus.load(std::memory_order_relaxed);
                    inst.uiState = busStateForIndex(currentBus);
                } else if (inst.actionKey == "output_l") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0) trackIndex = parseTrackIndexFromControlId(inst.controlId, trackCount);
                    if (trackIndex < 0) continue;
                    int currentBus = midi.tracks[trackIndex].outputBusL.load(std::memory_order_relaxed);
                    inst.uiState = busStateForIndex(currentBus);
                } else if (inst.actionKey == "output_r") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0) trackIndex = parseTrackIndexFromControlId(inst.controlId, trackCount);
                    if (trackIndex < 0) continue;
                    int currentBus = midi.tracks[trackIndex].outputBusR.load(std::memory_order_relaxed);
                    inst.uiState = busStateForIndex(currentBus);
                } else if (inst.actionKey == "clear") {
                    inst.uiState = "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, false);
                }
            }
        }

        void updateMidiUILayout(BaseSystem& baseSystem, MidiContext& midi, GLFWwindow* win) {
            if (!win) return;
            int windowWidth = 0, windowHeight = 0;
            glfwGetWindowSize(win, &windowWidth, &windowHeight);
            (void)windowHeight;
            double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
            float panelLeft = static_cast<float>(screenWidth) - 220.0f;
            float panelRight = static_cast<float>(screenWidth);
            if (baseSystem.panel) {
                const PanelRect& rect = (baseSystem.panel->rightRenderRect.w > 0.0f)
                    ? baseSystem.panel->rightRenderRect
                    : baseSystem.panel->rightRect;
                if (rect.w > 0.0f) {
                    panelLeft = rect.x;
                    panelRight = rect.x + rect.w;
                }
            }
            float leftMargin = 32.0f;
            float spacing = 44.0f;
            float clearX = panelLeft + leftMargin;
            float armX = clearX + spacing;
            float soloX = armX + spacing;
            float muteX = soloX + spacing;

            std::unordered_map<std::string, float> controlX;
            controlX.reserve(midi.trackInstances.size());
            float outputLX = panelRight - 58.0f;
            float outputRX = panelRight - 24.0f;

            for (auto* instPtr : midi.trackInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.actionKey == "clear") {
                    inst.position.x = clearX;
                } else if (inst.actionKey == "arm") {
                    inst.position.x = armX;
                } else if (inst.actionKey == "solo") {
                    inst.position.x = soloX;
                } else if (inst.actionKey == "mute") {
                    inst.position.x = muteX;
                } else if (inst.actionKey == "output_l"
                           || inst.controlId.find("_output_l") != std::string::npos) {
                    inst.position.x = outputLX;
                } else if (inst.actionKey == "output_r"
                           || inst.controlId.find("_output_r") != std::string::npos) {
                    inst.position.x = outputRX;
                } else if (inst.actionKey == "output"
                           || inst.actionKey.rfind("output", 0) == 0
                           || inst.controlId.find("_output") != std::string::npos) {
                    inst.position.x = outputRX;
                }
                if (!inst.controlId.empty()) {
                    controlX[inst.controlId] = inst.position.x;
                }
            }
            for (auto* instPtr : midi.trackLabelInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.controlId.find("_output") != std::string::npos) continue;
                auto it = controlX.find(inst.controlId);
                if (it != controlX.end()) {
                    inst.position.x = it->second;
                }
            }
            for (auto* instPtr : midi.outputLabelInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.controlId.find("_output") == std::string::npos) continue;
                int busIndex = -1;
                if (inst.textType == "VariableUI") {
                    int trackIndex = -1;
                    int trackCount = getTrackCount(midi);
                    auto parseSuffix = [&](const char* prefix) -> int {
                        if (inst.textKey.rfind(prefix, 0) != 0) return -1;
                        try {
                            int idx = std::stoi(inst.textKey.substr(std::strlen(prefix)));
                            return idx - 1;
                        } catch (...) {
                            return -1;
                        }
                    };
                    trackIndex = parseSuffix("midi_out_l_");
                    if (trackIndex >= 0 && trackIndex < trackCount) {
                        inst.position.x = outputLX;
                        continue;
                    }
                    trackIndex = parseSuffix("midi_out_r_");
                    if (trackIndex >= 0 && trackIndex < trackCount) {
                        inst.position.x = outputRX;
                        continue;
                    }
                    trackIndex = parseSuffix("midi_out_");
                    if (trackIndex < 0 || trackIndex >= trackCount) continue;
                    inst.position.x = outputRX;
                    continue;
                }
                if (inst.textType != "UIOnly" || inst.text.size() != 1) continue;
                if (inst.text == "L") busIndex = 0;
                else if (inst.text == "S") busIndex = 1;
                else if (inst.text == "F") busIndex = 2;
                else if (inst.text == "R") busIndex = 3;
                if (busIndex >= 0 && busIndex < DawContext::kBusCount) {
                    if (inst.controlId.find("_output_l") != std::string::npos) {
                        inst.position.x = outputLX;
                    } else {
                        inst.position.x = outputRX;
                    }
                }
            }
        }

        void buildMidiUiCache(BaseSystem& baseSystem, std::vector<Entity>& prototypes, MidiContext& midi) {
            midi.trackInstances.clear();
            midi.trackLabelInstances.clear();
            midi.outputLabelInstances.clear();
            if (!baseSystem.level) return;
            std::unordered_set<int> activeTrackWorlds;
            buildActiveTrackWorldSet(baseSystem, activeTrackWorlds);

            std::unordered_map<int, DawContext::LaneEntry> worldLaneEntry;
            if (baseSystem.uiStamp && baseSystem.daw && !baseSystem.daw->laneOrder.empty()) {
                const auto& rows = baseSystem.uiStamp->rowWorldIndices;
                const auto& lanes = baseSystem.daw->laneOrder;
                int count = std::min(static_cast<int>(rows.size()), static_cast<int>(lanes.size()));
                worldLaneEntry.reserve(static_cast<size_t>(count));
                for (int row = 0; row < count; ++row) {
                    int worldIndex = rows[static_cast<size_t>(row)];
                    if (worldIndex < 0 || worldIndex >= static_cast<int>(baseSystem.level->worlds.size())) continue;
                    worldLaneEntry[worldIndex] = lanes[static_cast<size_t>(row)];
                }
            }

            for (int worldIndex = 0; worldIndex < static_cast<int>(baseSystem.level->worlds.size()); ++worldIndex) {
                auto& world = baseSystem.level->worlds[worldIndex];
                if (shouldSkipTrackWorldByIndex(baseSystem, worldIndex, world, activeTrackWorlds)) continue;
                bool isTrackControls = world.name.rfind("MidiTrackRowWorld", 0) == 0;
                auto laneIt = worldLaneEntry.find(worldIndex);
                for (auto& inst : world.instances) {
                    if (inst.actionType == "DawMidiTrack") {
                        int mappedTrackIndex = -1;
                        if (laneIt != worldLaneEntry.end() && laneIt->second.type == 1) {
                            mappedTrackIndex = laneIt->second.trackIndex;
                        }
                        if (mappedTrackIndex < 0) {
                            mappedTrackIndex = parseTrackIndexUncheckedFromControlId(inst.controlId);
                        }
                        if (mappedTrackIndex >= 0) {
                            inst.actionValue = std::to_string(mappedTrackIndex);
                        }
                        midi.trackInstances.push_back(&inst);
                    }
                    if (isTrackControls) {
                        if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                        if (prototypes[inst.prototypeID].name != "Text") continue;
                        if (inst.textType == "VariableUI"
                            && (inst.textKey.rfind("midi_out_", 0) == 0
                                || inst.textKey.rfind("midi_out_l_", 0) == 0
                                || inst.textKey.rfind("midi_out_r_", 0) == 0)) {
                            if (inst.controlId.find("_output") != std::string::npos) {
                                midi.outputLabelInstances.push_back(&inst);
                            }
                        } else if (inst.textType == "UIOnly" && inst.text.size() == 1) {
                            if ((inst.text == "L" || inst.text == "S" || inst.text == "F" || inst.text == "R")
                                && inst.controlId.find("_output") != std::string::npos) {
                                midi.outputLabelInstances.push_back(&inst);
                            }
                        }
                        if (inst.controlRole == "label") {
                            midi.trackLabelInstances.push_back(&inst);
                        }
                    }
                }
            }
            midi.uiCacheBuilt = true;
            midi.uiLevel = baseSystem.level.get();
        }

        void updateMidiOutputLabels(BaseSystem& baseSystem, MidiContext& midi) {
            if (!baseSystem.font) return;
            FontContext& fontCtx = *baseSystem.font;
            int trackCount = getTrackCount(midi);
            for (int i = 0; i < trackCount; ++i) {
                std::string outKey = "midi_out_" + std::to_string(i + 1);
                std::string outLKey = "midi_out_l_" + std::to_string(i + 1);
                std::string outRKey = "midi_out_r_" + std::to_string(i + 1);
                int busIndexL = midi.tracks[static_cast<size_t>(i)].outputBusL.load(std::memory_order_relaxed);
                int busIndexR = midi.tracks[static_cast<size_t>(i)].outputBusR.load(std::memory_order_relaxed);
                std::string outLabelL = busLabelForIndex(busIndexL);
                std::string outLabelR = busLabelForIndex(busIndexR);
                if (outLabelL.empty()) outLabelL = "?";
                if (outLabelR.empty()) outLabelR = "?";
                fontCtx.variables[outLKey] = outLabelL;
                fontCtx.variables[outRKey] = outLabelR;
                if (busIndexL == busIndexR) {
                    fontCtx.variables[outKey] = outLabelL;
                } else {
                    fontCtx.variables[outKey] = outLabelL + "/" + outLabelR;
                }
            }
        }
    }

    void UpdateMidiUi(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float, GLFWwindow* win) {
        if (!baseSystem.midi || !baseSystem.ui) return;
        MidiContext& midi = *baseSystem.midi;
        UIContext& ui = *baseSystem.ui;

        if (!midi.initialized) return;

        if (!midi.uiCacheBuilt || midi.uiLevel != baseSystem.level.get()) {
            buildMidiUiCache(baseSystem, prototypes, midi);
        }

        if (!ui.loadingActive) {
            updateMidiOutputLabels(baseSystem, midi);
            updateMidiButtonVisuals(baseSystem, midi);
            updateMidiUILayout(baseSystem, midi, win);
        }
    }
}
