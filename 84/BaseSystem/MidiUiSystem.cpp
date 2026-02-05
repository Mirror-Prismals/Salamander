#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <string>
#include <unordered_map>
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

        const char* busLabelForIndex(int busIndex) {
            switch (busIndex) {
                case 0: return "L";
                case 1: return "S";
                case 2: return "F";
                case 3: return "R";
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

        void updateMidiButtonVisuals(BaseSystem& baseSystem, MidiContext& midi) {
            (void)baseSystem;
            int trackCount = getTrackCount(midi);
            for (auto* instPtr : midi.trackInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.actionKey == "arm") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0 && inst.controlId.rfind("midi_track_", 0) == 0) {
                        size_t start = 11;
                        size_t end = inst.controlId.find('_', start);
                        if (end != std::string::npos) {
                            trackIndex = parseTrackIndex(inst.controlId.substr(start, end - start), trackCount);
                        }
                    }
                    if (trackIndex < 0) continue;
                    int armMode = midi.tracks[trackIndex].armMode.load(std::memory_order_relaxed);
                    if (armMode == 1) inst.uiState = "overdub";
                    else if (armMode == 2) inst.uiState = "replace";
                    else inst.uiState = "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, armMode > 0);
                } else if (inst.actionKey == "solo") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0 && inst.controlId.rfind("midi_track_", 0) == 0) {
                        size_t start = 11;
                        size_t end = inst.controlId.find('_', start);
                        if (end != std::string::npos) {
                            trackIndex = parseTrackIndex(inst.controlId.substr(start, end - start), trackCount);
                        }
                    }
                    if (trackIndex < 0) continue;
                    bool active = midi.tracks[trackIndex].solo.load(std::memory_order_relaxed);
                    inst.uiState = active ? "active" : "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, active);
                } else if (inst.actionKey == "mute") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0 && inst.controlId.rfind("midi_track_", 0) == 0) {
                        size_t start = 11;
                        size_t end = inst.controlId.find('_', start);
                        if (end != std::string::npos) {
                            trackIndex = parseTrackIndex(inst.controlId.substr(start, end - start), trackCount);
                        }
                    }
                    if (trackIndex < 0) continue;
                    bool active = midi.tracks[trackIndex].mute.load(std::memory_order_relaxed);
                    inst.uiState = active ? "active" : "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, active);
                } else if (inst.actionKey == "output") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0 && inst.controlId.rfind("midi_track_", 0) == 0) {
                        size_t start = 11;
                        size_t end = inst.controlId.find('_', start);
                        if (end != std::string::npos) {
                            trackIndex = parseTrackIndex(inst.controlId.substr(start, end - start), trackCount);
                        }
                    }
                    if (trackIndex < 0) continue;
                    int currentBus = midi.tracks[trackIndex].outputBus.load(std::memory_order_relaxed);
                    inst.uiState = busStateForIndex(currentBus);
                } else if (inst.actionKey == "input" || inst.actionKey == "clear") {
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
            float inputX = clearX + spacing;
            float armX = inputX + spacing;
            float soloX = armX + spacing;
            float muteX = soloX + spacing;

            std::unordered_map<std::string, float> controlX;
            controlX.reserve(midi.trackInstances.size());
            float outputX = panelRight - 32.0f;

            for (auto* instPtr : midi.trackInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.actionKey == "clear") {
                    inst.position.x = clearX;
                } else if (inst.actionKey == "input") {
                    inst.position.x = inputX;
                } else if (inst.actionKey == "arm") {
                    inst.position.x = armX;
                } else if (inst.actionKey == "solo") {
                    inst.position.x = soloX;
                } else if (inst.actionKey == "mute") {
                    inst.position.x = muteX;
                } else if (inst.actionKey == "output"
                           || inst.actionKey.rfind("output", 0) == 0
                           || inst.controlId.find("_output") != std::string::npos) {
                    inst.position.x = outputX;
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
                    if (!inst.textKey.empty()) {
                        size_t start = inst.textKey.rfind("midi_out_");
                        if (start != std::string::npos) {
                            try {
                                int idx = std::stoi(inst.textKey.substr(start + 9));
                                trackIndex = idx - 1;
                            } catch (...) {
                                trackIndex = -1;
                            }
                        }
                    }
                    if (trackIndex < 0 || trackIndex >= trackCount) continue;
                    inst.position.x = outputX;
                    continue;
                }
                if (inst.textType != "UIOnly" || inst.text.size() != 1) continue;
                if (inst.text == "L") busIndex = 0;
                else if (inst.text == "S") busIndex = 1;
                else if (inst.text == "F") busIndex = 2;
                else if (inst.text == "R") busIndex = 3;
                if (busIndex >= 0 && busIndex < DawContext::kBusCount) {
                    inst.position.x = outputX;
                }
            }
        }

        void buildMidiUiCache(BaseSystem& baseSystem, std::vector<Entity>& prototypes, MidiContext& midi) {
            midi.trackInstances.clear();
            midi.trackLabelInstances.clear();
            midi.outputLabelInstances.clear();
            if (!baseSystem.level) return;
            for (auto& world : baseSystem.level->worlds) {
                bool isTrackControls = world.name.rfind("MidiTrackRowWorld", 0) == 0;
                for (auto& inst : world.instances) {
                    if (inst.actionType == "DawMidiTrack") {
                        midi.trackInstances.push_back(&inst);
                    }
                    if (isTrackControls) {
                        if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                        if (prototypes[inst.prototypeID].name != "Text") continue;
                        if (inst.textType == "VariableUI" && inst.textKey.rfind("midi_out_", 0) == 0) {
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

        void updateMidiInputLabels(BaseSystem& baseSystem, MidiContext& midi, AudioContext& audio) {
            if (!baseSystem.font) return;
            FontContext& fontCtx = *baseSystem.font;
            int physicalCount = static_cast<int>(audio.physicalMidiInputPorts.size());
            int trackCount = getTrackCount(midi);
            for (int i = 0; i < trackCount; ++i) {
                int idx = midi.tracks[static_cast<size_t>(i)].physicalInputIndex;
                std::string key = "midi_in_" + std::to_string(i + 1);
                if (idx < 0 || idx >= physicalCount) {
                    idx = 0;
                    midi.tracks[static_cast<size_t>(i)].physicalInputIndex = 0;
                }
                if (idx < physicalCount) {
                    fontCtx.variables[key] = "IN" + std::to_string(idx + 1);
                } else {
                    fontCtx.variables[key] = "IN1";
                }
                std::string outKey = "midi_out_" + std::to_string(i + 1);
                int busIndex = midi.tracks[static_cast<size_t>(i)].outputBus.load(std::memory_order_relaxed);
                std::string outLabel = busLabelForIndex(busIndex);
                if (outLabel.empty()) outLabel = "?";
                fontCtx.variables[outKey] = outLabel;
            }
        }
    }

    void UpdateMidiUi(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float, GLFWwindow* win) {
        if (!baseSystem.midi || !baseSystem.audio || !baseSystem.ui) return;
        MidiContext& midi = *baseSystem.midi;
        AudioContext& audio = *baseSystem.audio;
        UIContext& ui = *baseSystem.ui;

        if (!midi.initialized) return;

        if (!midi.uiCacheBuilt || midi.uiLevel != baseSystem.level.get()) {
            buildMidiUiCache(baseSystem, prototypes, midi);
        }

        if (!ui.loadingActive) {
            updateMidiInputLabels(baseSystem, midi, audio);
            updateMidiButtonVisuals(baseSystem, midi);
            updateMidiUILayout(baseSystem, midi, win);
        }
    }
}
