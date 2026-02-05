#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace ButtonSystemLogic {
    void SetButtonToggled(int instanceID, bool toggled);
}

namespace DawUiSystemLogic {

    namespace {
        constexpr double kTimelineScrollSeconds = 5.0;
        constexpr float kLaneLeftMargin = 40.0f;
        constexpr float kLaneRightMargin = 40.0f;

        int getTrackCount(const DawContext& daw) {
            return static_cast<int>(daw.tracks.size());
        }

        int getMidiTrackCount(const BaseSystem& baseSystem) {
            if (!baseSystem.midi) return 0;
            return static_cast<int>(baseSystem.midi->tracks.size());
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

        int parseBus(const std::string& value) {
            if (value == "L") return 0;
            if (value == "S" || value == "SUB") return 1;
            if (value == "FF" || value == "F") return 2;
            if (value == "R") return 3;
            return -1;
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

        bool parseTrackIndexFromKey(const std::string& key,
                                    const std::string& prefix,
                                    bool oneBased,
                                    int trackCount,
                                    int& outTrack) {
            if (key.rfind(prefix, 0) != 0) return false;
            std::string suffix = key.substr(prefix.size());
            if (suffix.empty()) return false;
            try {
                int idx = std::stoi(suffix);
                if (oneBased) idx -= 1;
                if (idx < 0 || idx >= trackCount) return false;
                outTrack = idx;
                return true;
            } catch (...) {
                return false;
            }
        }

        bool parseTrackAndBus(const std::string& value, int trackCount, int& outTrack, int& outBus) {
            size_t sep = value.find(':');
            if (sep == std::string::npos) return false;
            int trackIdx = parseTrackIndex(value.substr(0, sep), trackCount);
            int busIdx = parseBus(value.substr(sep + 1));
            if (trackIdx < 0 || busIdx < 0) return false;
            outTrack = trackIdx;
            outBus = busIdx;
            return true;
        }

        uint64_t maxTimelineSamples(const DawContext& daw) {
            uint64_t maxSamples = daw.playheadSample.load(std::memory_order_relaxed);
            for (const auto& track : daw.tracks) {
                maxSamples = std::max<uint64_t>(maxSamples, static_cast<uint64_t>(track.audio.size()));
            }
            return maxSamples;
        }

        void clampTimelineOffset(DawContext& daw) {
            double secondsPerScreen = (daw.timelineSecondsPerScreen > 0.0) ? daw.timelineSecondsPerScreen : 10.0;
            int64_t windowSamples = static_cast<int64_t>(secondsPerScreen * daw.sampleRate);
            if (windowSamples < 0) windowSamples = 0;
            uint64_t maxSamples = maxTimelineSamples(daw);
            int64_t maxOffset = (maxSamples > static_cast<uint64_t>(windowSamples))
                ? static_cast<int64_t>(maxSamples - static_cast<uint64_t>(windowSamples))
                : 0;
            if (daw.timelineOffsetSamples < 0) daw.timelineOffsetSamples = 0;
            if (daw.timelineOffsetSamples > maxOffset) daw.timelineOffsetSamples = maxOffset;
        }

        bool isTrackRowWorld(const std::string& name) {
            if (name == "TrackRowWorld") return true;
            return name.rfind("TrackRowWorld_", 0) == 0;
        }

        int findWorldIndex(const LevelContext& level, const std::string& name) {
            for (size_t i = 0; i < level.worlds.size(); ++i) {
                if (level.worlds[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        void ensureTimelineLabelCapacity(BaseSystem& baseSystem, DawContext& daw, int desiredTime, int desiredBar) {
            if (!baseSystem.level || !baseSystem.instance) return;
            LevelContext& level = *baseSystem.level;
            int screenWorldIndex = findWorldIndex(level, "DAWScreenWorld");
            if (screenWorldIndex < 0 || screenWorldIndex >= static_cast<int>(level.worlds.size())) return;
            auto& insts = level.worlds[screenWorldIndex].instances;

            const EntityInstance* timeTemplate = nullptr;
            const EntityInstance* barTemplate = nullptr;
            int timeCount = 0;
            int barCount = 0;
            for (auto& inst : insts) {
                if (inst.controlRole == "timeline_label") {
                    if (!timeTemplate) timeTemplate = &inst;
                    timeCount += 1;
                } else if (inst.controlRole == "timeline_bar_label") {
                    if (!barTemplate) barTemplate = &inst;
                    barCount += 1;
                }
            }
            if (!timeTemplate && !barTemplate) return;
            EntityInstance timeTemplateCopy{};
            EntityInstance barTemplateCopy{};
            if (timeTemplate) timeTemplateCopy = *timeTemplate;
            if (barTemplate) barTemplateCopy = *barTemplate;

            bool added = false;
            while (timeTemplate && timeCount < desiredTime) {
                EntityInstance inst = timeTemplateCopy;
                inst.instanceID = baseSystem.instance->nextInstanceID++;
                inst.controlId = "daw_time_" + std::to_string(timeCount);
                inst.textKey = "daw_time_" + std::to_string(timeCount);
                inst.controlRole = "timeline_label";
                inst.position = timeTemplateCopy.position;
                insts.push_back(std::move(inst));
                timeCount += 1;
                added = true;
            }
            while (barTemplate && barCount < desiredBar) {
                EntityInstance inst = barTemplateCopy;
                inst.instanceID = baseSystem.instance->nextInstanceID++;
                inst.controlId = "daw_bar_" + std::to_string(barCount);
                inst.textKey = "daw_bar_" + std::to_string(barCount);
                inst.controlRole = "timeline_bar_label";
                inst.position = barTemplateCopy.position;
                insts.push_back(std::move(inst));
                barCount += 1;
                added = true;
            }
            if (added) {
                if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
                if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
                daw.uiCacheBuilt = false;
            }
        }

        void updateTrackButtonVisuals(BaseSystem& baseSystem, DawContext& daw) {
            (void)baseSystem;
            int trackCount = getTrackCount(daw);
            for (auto* instPtr : daw.trackInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.actionKey == "arm") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0 && inst.controlId.rfind("track_", 0) == 0) {
                        size_t start = 6;
                        size_t end = inst.controlId.find('_', start);
                        if (end != std::string::npos) {
                            trackIndex = parseTrackIndex(inst.controlId.substr(start, end - start), trackCount);
                        }
                    }
                    if (trackIndex < 0) continue;
                    int armMode = daw.tracks[trackIndex].armMode.load(std::memory_order_relaxed);
                    if (armMode == 1) inst.uiState = "overdub";
                    else if (armMode == 2) inst.uiState = "replace";
                    else inst.uiState = "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, armMode > 0);
                } else if (inst.actionKey == "solo") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0 && inst.controlId.rfind("track_", 0) == 0) {
                        size_t start = 6;
                        size_t end = inst.controlId.find('_', start);
                        if (end != std::string::npos) {
                            trackIndex = parseTrackIndex(inst.controlId.substr(start, end - start), trackCount);
                        }
                    }
                    if (trackIndex < 0) continue;
                    bool active = daw.tracks[trackIndex].solo.load(std::memory_order_relaxed);
                    inst.uiState = active ? "active" : "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, active);
                } else if (inst.actionKey == "mute") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0 && inst.controlId.rfind("track_", 0) == 0) {
                        size_t start = 6;
                        size_t end = inst.controlId.find('_', start);
                        if (end != std::string::npos) {
                            trackIndex = parseTrackIndex(inst.controlId.substr(start, end - start), trackCount);
                        }
                    }
                    if (trackIndex < 0) continue;
                    bool active = daw.tracks[trackIndex].mute.load(std::memory_order_relaxed);
                    inst.uiState = active ? "active" : "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, active);
                } else if (inst.actionKey == "output") {
                    int trackIndex = -1;
                    int busIndex = -1;
                    if (parseTrackAndBus(inst.actionValue, trackCount, trackIndex, busIndex)) {
                        int currentBus = daw.tracks[trackIndex].outputBus.load(std::memory_order_relaxed);
                        inst.uiState = (currentBus == busIndex) ? "selected" : "idle";
                    } else {
                        trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                        if (trackIndex < 0 && inst.controlId.rfind("track_", 0) == 0) {
                            size_t start = 6;
                            size_t end = inst.controlId.find('_', start);
                            if (end != std::string::npos) {
                                trackIndex = parseTrackIndex(inst.controlId.substr(start, end - start), trackCount);
                            }
                        }
                        if (trackIndex < 0) continue;
                        int currentBus = daw.tracks[trackIndex].outputBus.load(std::memory_order_relaxed);
                        inst.uiState = busStateForIndex(currentBus);
                    }
                } else if (inst.actionKey == "input" || inst.actionKey == "clear") {
                    inst.uiState = "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, false);
                }
            }
        }

        void updateTransportButtonVisuals(BaseSystem& baseSystem, const DawContext& daw) {
            (void)baseSystem;
            for (auto* instPtr : daw.transportInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                bool toggled = false;
                if (inst.actionKey == "stop") {
                    toggled = (daw.transportLatch == 1);
                } else if (inst.actionKey == "play") {
                    toggled = (daw.transportLatch == 2);
                } else if (inst.actionKey == "record") {
                    toggled = (daw.transportLatch == 3);
                }
                ButtonSystemLogic::SetButtonToggled(inst.instanceID, toggled);
                inst.uiState = toggled ? "active" : "idle";
            }
        }

        void updateTempoButtonVisuals(BaseSystem& baseSystem, const DawContext& daw) {
            (void)baseSystem;
            for (auto* instPtr : daw.tempoInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.actionKey == "metronome") {
                    bool enabled = daw.metronomeEnabled.load(std::memory_order_relaxed);
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, enabled);
                    inst.uiState = enabled ? "active" : "idle";
                }
            }
        }

        void updateLoopButtonVisuals(BaseSystem& baseSystem, const DawContext& daw) {
            (void)baseSystem;
            for (auto* instPtr : daw.loopInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.actionKey == "toggle") {
                    bool enabled = daw.loopEnabled.load(std::memory_order_relaxed);
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, enabled);
                    inst.uiState = enabled ? "active" : "idle";
                }
            }
        }

        void updateDawUILayout(BaseSystem& baseSystem, DawContext& daw, GLFWwindow* win) {
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
            controlX.reserve(daw.trackInstances.size());
            float outputX = panelRight - 32.0f;

            for (auto* instPtr : daw.trackInstances) {
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
            for (auto* instPtr : daw.trackLabelInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.controlId.find("_output") != std::string::npos) continue;
                auto it = controlX.find(inst.controlId);
                if (it != controlX.end()) {
                    inst.position.x = it->second;
                }
            }
            for (auto* instPtr : daw.outputLabelInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.controlId.find("_output") == std::string::npos) continue;
                int busIndex = -1;
                if (inst.textType == "VariableUI") {
                    int trackIndex = -1;
                    if (!parseTrackIndexFromKey(inst.textKey, "daw_out_", true, getTrackCount(daw), trackIndex)) continue;
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

        void buildDawUiCache(BaseSystem& baseSystem, std::vector<Entity>& prototypes, DawContext& daw) {
            daw.trackInstances.clear();
            daw.trackLabelInstances.clear();
            daw.transportInstances.clear();
            daw.tempoInstances.clear();
            daw.loopInstances.clear();
            daw.outputLabelInstances.clear();
            daw.timelineLabelInstances.clear();
            daw.timelineBarLabelInstances.clear();
            if (!baseSystem.level) return;
            static bool g_debugPrinted = false;
            int dawTrackActionCount = 0;
            int trackRowWorldCount = 0;
            for (auto& world : baseSystem.level->worlds) {
                bool isTrackControls = isTrackRowWorld(world.name);
                if (isTrackControls) {
                    trackRowWorldCount += 1;
                }
                for (auto& inst : world.instances) {
                    if (inst.actionType == "DawTrack") {
                        daw.trackInstances.push_back(&inst);
                        dawTrackActionCount += 1;
                    } else if (inst.actionType == "DawTransport") {
                        daw.transportInstances.push_back(&inst);
                    } else if (inst.actionType == "DawTempo") {
                        daw.tempoInstances.push_back(&inst);
                    } else if (inst.actionType == "DawLoop") {
                        daw.loopInstances.push_back(&inst);
                    }
                    if (inst.controlRole == "timeline_label") {
                        daw.timelineLabelInstances.push_back(&inst);
                    }
                    if (inst.controlRole == "timeline_bar_label") {
                        daw.timelineBarLabelInstances.push_back(&inst);
                    }
                    if (isTrackControls) {
                        if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                        if (prototypes[inst.prototypeID].name != "Text") continue;
                        if (inst.textType == "VariableUI" && inst.textKey.rfind("daw_out_", 0) == 0) {
                            if (inst.controlId.find("_output") != std::string::npos) {
                                daw.outputLabelInstances.push_back(&inst);
                            }
                        } else if (inst.textType == "UIOnly" && inst.text.size() == 1) {
                            if ((inst.text == "L" || inst.text == "S" || inst.text == "F" || inst.text == "R")
                                && inst.controlId.find("_output") != std::string::npos) {
                                daw.outputLabelInstances.push_back(&inst);
                            }
                        }
                        if (inst.controlRole == "label") {
                            daw.trackLabelInstances.push_back(&inst);
                        }
                    }
                }
            }
            if (!g_debugPrinted) {
                g_debugPrinted = true;
                std::cerr << "[DawUiCache] trackRowWorlds=" << trackRowWorldCount
                          << " DawTrackActions=" << dawTrackActionCount
                          << " trackInstances=" << daw.trackInstances.size()
                          << " trackLabels=" << daw.trackLabelInstances.size()
                          << " outputLabels=" << daw.outputLabelInstances.size()
                          << std::endl;
                int dumpCount = 0;
                for (auto* instPtr : daw.trackInstances) {
                    if (!instPtr) continue;
                    const EntityInstance& inst = *instPtr;
                    std::string protoName = "<invalid>";
                    bool isUIButton = false;
                    if (inst.prototypeID >= 0 && inst.prototypeID < static_cast<int>(prototypes.size())) {
                        protoName = prototypes[inst.prototypeID].name;
                        isUIButton = prototypes[inst.prototypeID].isUIButton;
                    }
                    std::cerr << "  [DawUiCache] inst name='" << inst.name
                              << "' action='" << inst.actionType
                              << "' key='" << inst.actionKey
                              << "' controlId='" << inst.controlId
                              << "' proto=" << inst.prototypeID
                              << " protoName='" << protoName
                              << "' isUIButton=" << (isUIButton ? "true" : "false")
                              << std::endl;
                    if (++dumpCount >= 3) break;
                }
            }
            daw.uiCacheBuilt = true;
            daw.uiLevel = baseSystem.level.get();
        }

        void updateInputLabels(BaseSystem& baseSystem, DawContext& daw, AudioContext& audio) {
            if (!baseSystem.font) return;
            FontContext& fontCtx = *baseSystem.font;
            int physicalCount = static_cast<int>(audio.physicalInputPorts.size());
            int totalInputs = physicalCount + 1;
            int trackCount = getTrackCount(daw);
            for (int i = 0; i < trackCount; ++i) {
                int idx = daw.tracks[static_cast<size_t>(i)].physicalInputIndex;
                std::string key = "daw_in_" + std::to_string(i + 1);
                if (idx < 0 || idx >= totalInputs) {
                    idx = 0;
                    daw.tracks[static_cast<size_t>(i)].physicalInputIndex = 0;
                }
                if (idx < physicalCount) {
                    fontCtx.variables[key] = "IN" + std::to_string(idx + 1);
                } else {
                    fontCtx.variables[key] = "VM1";
                }
                daw.tracks[static_cast<size_t>(i)].useVirtualInput.store(idx >= physicalCount, std::memory_order_relaxed);
                std::string outKey = "daw_out_" + std::to_string(i + 1);
                int busIndex = daw.tracks[static_cast<size_t>(i)].outputBus.load(std::memory_order_relaxed);
                std::string outLabel = busLabelForIndex(busIndex);
                if (outLabel.empty()) outLabel = "?";
                fontCtx.variables[outKey] = outLabel;
            }
        }

        void updateTimelineLabels(BaseSystem& baseSystem, DawContext& daw, GLFWwindow* win) {
            if (!baseSystem.font || !win) return;
            if (daw.timelineLabelInstances.empty()) return;
            FontContext& fontCtx = *baseSystem.font;

            clampTimelineOffset(daw);

            int windowWidth = 0, windowHeight = 0;
            glfwGetWindowSize(win, &windowWidth, &windowHeight);
            double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
            double screenHeight = windowHeight > 0 ? static_cast<double>(windowHeight) : 1080.0;

            const float laneHeight = 60.0f;
            const float laneHalfH = laneHeight * 0.5f;
            float laneLeft = kLaneLeftMargin;
            float laneRight = static_cast<float>(screenWidth) - kLaneRightMargin;
            if (laneRight < laneLeft + 200.0f) {
                laneRight = laneLeft + 200.0f;
            }
            float scrollY = 0.0f;
            if (baseSystem.uiStamp) {
                scrollY = baseSystem.uiStamp->scrollY;
            }
            float startY = 100.0f + scrollY;
            float labelY = startY - laneHalfH - 18.0f;
            int laneCount = static_cast<int>(daw.laneOrder.size());
            if (laneCount == 0) {
                laneCount = getTrackCount(daw) + getMidiTrackCount(baseSystem);
            }
            float rowSpan = laneHeight + 12.0f;
            float laneBottomBound = (laneCount > 0)
                ? (startY + (laneCount - 1) * rowSpan + laneHalfH)
                : (startY - laneHalfH + 1.0f);
            float visualBottomBound = std::max(laneBottomBound, static_cast<float>(screenHeight) - 40.0f);
            float barLabelY = std::min(visualBottomBound + 12.0f, static_cast<float>(screenHeight) - 6.0f);

            double secondsPerScreen = (daw.timelineSecondsPerScreen > 0.0) ? daw.timelineSecondsPerScreen : 10.0;
            if (secondsPerScreen <= 0.0) secondsPerScreen = 10.0;
            double offsetSec = (daw.sampleRate > 0.0)
                ? static_cast<double>(daw.timelineOffsetSamples) / static_cast<double>(daw.sampleRate)
                : 0.0;
            double gridSeconds = kTimelineScrollSeconds;
            double firstTick = std::floor(offsetSec / gridSeconds) * gridSeconds;
            double endSec = offsetSec + secondsPerScreen;

            for (size_t i = 0; i < daw.timelineLabelInstances.size(); ++i) {
                EntityInstance* inst = daw.timelineLabelInstances[i];
                if (!inst) continue;
                double tick = firstTick + static_cast<double>(i) * gridSeconds;
                if (tick < offsetSec - 0.001 || tick > endSec + 0.001) {
                    if (!inst->textKey.empty()) {
                        fontCtx.variables[inst->textKey] = "";
                    }
                    continue;
                }
                float t = static_cast<float>((tick - offsetSec) / secondsPerScreen);
                float x = laneLeft + (laneRight - laneLeft) * t;
                inst->position.x = x;
                inst->position.y = labelY;
                inst->position.z = -1.0f;
                int seconds = static_cast<int>(std::round(tick));
                if (!inst->textKey.empty()) {
                    fontCtx.variables[inst->textKey] = std::to_string(seconds);
                }
            }

            if (!daw.timelineBarLabelInstances.empty()) {
                double bpm = daw.bpm.load(std::memory_order_relaxed);
                if (bpm <= 0.0) bpm = 120.0;
                double secondsPerBeat = 60.0 / bpm;
                if (secondsPerBeat <= 0.0) secondsPerBeat = 0.5;
                double gridSeconds = secondsPerBeat;
                if (secondsPerScreen > 64.0) {
                    gridSeconds = secondsPerBeat * 4.0;
                } else if (secondsPerScreen > 32.0) {
                    gridSeconds = secondsPerBeat * 2.0;
                } else if (secondsPerScreen > 16.0) {
                    gridSeconds = secondsPerBeat;
                } else if (secondsPerScreen > 8.0) {
                    gridSeconds = secondsPerBeat * 0.5;
                } else if (secondsPerScreen > 4.0) {
                    gridSeconds = secondsPerBeat * 0.25;
                } else {
                    gridSeconds = secondsPerBeat * 0.125;
                }
                double labelStep = gridSeconds;
                double firstLabel = std::floor(offsetSec / labelStep) * labelStep;
                int subPerBeat = static_cast<int>(std::round(secondsPerBeat / gridSeconds));
                if (subPerBeat < 1) subPerBeat = 1;

                for (size_t i = 0; i < daw.timelineBarLabelInstances.size(); ++i) {
                    EntityInstance* inst = daw.timelineBarLabelInstances[i];
                    if (!inst) continue;
                    double tick = firstLabel + static_cast<double>(i) * labelStep;
                    if (tick < offsetSec - 0.001 || tick > endSec + 0.001) {
                        if (!inst->textKey.empty()) {
                            fontCtx.variables[inst->textKey] = "";
                        }
                        continue;
                    }
                    float t = static_cast<float>((tick - offsetSec) / secondsPerScreen);
                    float x = laneLeft + (laneRight - laneLeft) * t;
                    inst->position.x = x;
                    inst->position.y = barLabelY;
                    inst->position.z = -1.0f;
                    double beatStart = std::floor(tick / secondsPerBeat) * secondsPerBeat;
                    int beatIndex = static_cast<int>(std::floor(tick / secondsPerBeat)) + 1;
                    int barIndex = (beatIndex - 1) / 4 + 1;
                    int beatInBar = (beatIndex - 1) % 4 + 1;
                    int subIndex = static_cast<int>(std::floor((tick - beatStart) / gridSeconds)) + 1;
                    if (subIndex < 1) subIndex = 1;
                    if (subIndex > subPerBeat) subIndex = subPerBeat;
                    if (!inst->textKey.empty()) {
                        if (gridSeconds >= secondsPerBeat) {
                            fontCtx.variables[inst->textKey] = std::to_string(barIndex) + "." + std::to_string(beatInBar);
                        } else {
                            fontCtx.variables[inst->textKey] = std::to_string(barIndex) + "." + std::to_string(beatInBar)
                                + "." + std::to_string(subIndex);
                        }
                    }
                }
            }
        }

        void updateBpmLabel(BaseSystem& baseSystem, DawContext& daw) {
            if (!baseSystem.font) return;
            FontContext& fontCtx = *baseSystem.font;
            double bpm = daw.bpm.load(std::memory_order_relaxed);
            int bpmInt = static_cast<int>(std::round(bpm));
            fontCtx.variables["daw_bpm"] = std::to_string(bpmInt) + " BPM";
        }
    }

    void ClampTimelineOffset(DawContext& daw) {
        clampTimelineOffset(daw);
    }

    void UpdateDawUi(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float, GLFWwindow* win) {
        if (!baseSystem.daw || !baseSystem.audio || !baseSystem.ui) return;
        DawContext& daw = *baseSystem.daw;
        AudioContext& audio = *baseSystem.audio;
        UIContext& ui = *baseSystem.ui;

        if (!daw.initialized) return;

        if (ui.active) {
            if (!daw.uiCacheBuilt || daw.uiLevel != baseSystem.level.get()) {
                buildDawUiCache(baseSystem, prototypes, daw);
            }
            clampTimelineOffset(daw);
            {
                double secondsPerScreen = (daw.timelineSecondsPerScreen > 0.0) ? daw.timelineSecondsPerScreen : 10.0;
                if (secondsPerScreen <= 0.0) secondsPerScreen = 10.0;
                int desiredTime = static_cast<int>(std::ceil(secondsPerScreen / kTimelineScrollSeconds)) + 2;
                desiredTime = std::clamp(desiredTime, 8, 64);
                double bpm = daw.bpm.load(std::memory_order_relaxed);
                if (bpm <= 0.0) bpm = 120.0;
                double secondsPerBeat = 60.0 / bpm;
                if (secondsPerBeat <= 0.0) secondsPerBeat = 0.5;
                int desiredBar = static_cast<int>(std::ceil(secondsPerScreen / secondsPerBeat)) + 2;
                desiredBar = std::clamp(desiredBar, 8, 256);
                ensureTimelineLabelCapacity(baseSystem, daw, desiredTime, desiredBar);
                if (!daw.uiCacheBuilt || daw.uiLevel != baseSystem.level.get()) {
                    buildDawUiCache(baseSystem, prototypes, daw);
                }
            }
            updateDawUILayout(baseSystem, daw, win);
            updateTransportButtonVisuals(baseSystem, daw);
            updateTempoButtonVisuals(baseSystem, daw);
            updateLoopButtonVisuals(baseSystem, daw);
            updateTrackButtonVisuals(baseSystem, daw);
            updateInputLabels(baseSystem, daw, audio);
            updateTimelineLabels(baseSystem, daw, win);
            updateBpmLabel(baseSystem, daw);
        }
    }
}
