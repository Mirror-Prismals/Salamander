#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <vector>

#include "BaseSystem/Vst3Host.h"
#include "pluginterfaces/base/ustring.h"

namespace AutomationTrackSystemLogic {

    namespace {
        struct AutomationLaneRef {
            int type = 0;   // 0 audio, 1 midi
            int track = 0;
            std::string label;
        };

        struct AutomationDeviceRef {
            Vst3Plugin* plugin = nullptr;
            std::string label;
        };

        struct AutomationParamRef {
            int id = -1;
            std::string label;
        };

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

        int getTrackCount(const DawContext& daw) {
            return static_cast<int>(daw.automationTracks.size());
        }

        bool parseTrackAndSlot(const std::string& value, int trackCount, int& outTrack, int& outSlot) {
            outTrack = -1;
            outSlot = -1;
            if (value.empty()) return false;
            size_t sep = value.find(':');
            if (sep == std::string::npos) return false;
            int track = parseTrackIndex(value.substr(0, sep), trackCount);
            if (track < 0) return false;
            try {
                int slot = std::stoi(value.substr(sep + 1));
                if (slot < 0) return false;
                outTrack = track;
                outSlot = slot;
                return true;
            } catch (...) {
                return false;
            }
        }

        std::string toCompactLabel(const std::string& raw, size_t maxChars) {
            if (raw.empty()) return "NONE";
            std::string out;
            out.reserve(raw.size());
            for (char ch : raw) {
                if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
                if (ch == ' ') {
                    if (!out.empty() && out.back() == ' ') continue;
                }
                out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
                if (out.size() >= maxChars) break;
            }
            while (!out.empty() && out.back() == ' ') out.pop_back();
            if (out.empty()) out = "NONE";
            return out;
        }

        std::string utf16ToAsciiLabel(const Steinberg::Vst::TChar* src, int32_t size) {
            if (!src || size <= 0) return {};
            Steinberg::UString u(const_cast<Steinberg::Vst::TChar*>(src), size);
            std::array<char, 256> ascii{};
            u.toAscii(ascii.data(), static_cast<int32_t>(ascii.size()));
            std::string out(ascii.data());
            // Trim edge whitespace so empty-name plugins cleanly fall back.
            while (!out.empty() && std::isspace(static_cast<unsigned char>(out.front()))) out.erase(out.begin());
            while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back()))) out.pop_back();
            return out;
        }

        void removeLaneEntryForTrack(DawContext& daw, int type, int trackIndex) {
            int removedIndex = -1;
            for (auto it = daw.laneOrder.begin(); it != daw.laneOrder.end(); ) {
                if (it->type == type && it->trackIndex == trackIndex) {
                    if (removedIndex < 0) {
                        removedIndex = static_cast<int>(std::distance(daw.laneOrder.begin(), it));
                    }
                    it = daw.laneOrder.erase(it);
                } else {
                    ++it;
                }
            }
            for (auto& entry : daw.laneOrder) {
                if (entry.type == type && entry.trackIndex > trackIndex) {
                    entry.trackIndex -= 1;
                }
            }
            if (removedIndex >= 0) {
                if (daw.selectedLaneIndex == removedIndex) {
                    daw.selectedLaneIndex = -1;
                    daw.selectedLaneType = -1;
                    daw.selectedLaneTrack = -1;
                } else if (daw.selectedLaneIndex > removedIndex) {
                    daw.selectedLaneIndex -= 1;
                }
            } else if (daw.selectedLaneIndex >= static_cast<int>(daw.laneOrder.size())) {
                daw.selectedLaneIndex = static_cast<int>(daw.laneOrder.size()) - 1;
            }
        }

        void insertLaneEntry(DawContext& daw, int laneIndex, int type, int trackIndex) {
            if (laneIndex < 0) laneIndex = 0;
            if (laneIndex > static_cast<int>(daw.laneOrder.size())) {
                laneIndex = static_cast<int>(daw.laneOrder.size());
            }
            DawContext::LaneEntry entry{type, trackIndex};
            daw.laneOrder.insert(daw.laneOrder.begin() + laneIndex, entry);
            if (daw.selectedLaneIndex >= laneIndex && daw.selectedLaneIndex >= 0) {
                daw.selectedLaneIndex += 1;
            }
        }

        void buildLaneRefs(const BaseSystem& baseSystem, std::vector<AutomationLaneRef>& out) {
            out.clear();
            if (!baseSystem.daw) return;
            const DawContext& daw = *baseSystem.daw;
            int audioCount = static_cast<int>(daw.tracks.size());
            int midiCount = baseSystem.midi ? static_cast<int>(baseSystem.midi->tracks.size()) : 0;
            out.reserve(static_cast<size_t>(audioCount + midiCount));
            for (int i = 0; i < audioCount; ++i) {
                out.push_back({0, i, "AUD" + std::to_string(i + 1)});
            }
            for (int i = 0; i < midiCount; ++i) {
                out.push_back({1, i, "MID" + std::to_string(i + 1)});
            }
        }

        void buildDeviceRefs(const BaseSystem& baseSystem,
                             const AutomationTrack& track,
                             std::vector<AutomationDeviceRef>& out) {
            out.clear();
            if (!baseSystem.vst3) return;
            const Vst3Context& vst3 = *baseSystem.vst3;

            if (track.targetLaneType == 0) {
                int audioTrack = track.targetLaneTrack;
                if (audioTrack < 0 || audioTrack >= static_cast<int>(vst3.audioTracks.size())) return;
                const Vst3TrackChain& chain = vst3.audioTracks[static_cast<size_t>(audioTrack)];
                out.reserve(chain.effects.size());
                for (size_t i = 0; i < chain.effects.size(); ++i) {
                    Vst3Plugin* plugin = chain.effects[i];
                    if (!plugin) continue;
                    out.push_back({plugin, toCompactLabel(plugin->name, 10)});
                }
                return;
            }

            if (track.targetLaneType == 1) {
                int midiTrack = track.targetLaneTrack;
                if (midiTrack < 0 || midiTrack >= static_cast<int>(vst3.midiTracks.size())) return;
                if (midiTrack >= 0 && midiTrack < static_cast<int>(vst3.midiInstruments.size())) {
                    Vst3Plugin* inst = vst3.midiInstruments[static_cast<size_t>(midiTrack)];
                    if (inst) {
                        out.push_back({inst, "I:" + toCompactLabel(inst->name, 8)});
                    }
                }
                const Vst3TrackChain& chain = vst3.midiTracks[static_cast<size_t>(midiTrack)];
                for (size_t i = 0; i < chain.effects.size(); ++i) {
                    Vst3Plugin* plugin = chain.effects[i];
                    if (!plugin) continue;
                    out.push_back({plugin, toCompactLabel(plugin->name, 10)});
                }
            }
        }

        void buildParamRefs(Vst3Plugin* plugin, std::vector<AutomationParamRef>& out) {
            out.clear();
            if (!plugin || !plugin->controller) return;
            int32_t count = plugin->controller->getParameterCount();
            if (count <= 0) return;
            out.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i) {
                Steinberg::Vst::ParameterInfo info{};
                if (plugin->controller->getParameterInfo(i, info) != Steinberg::kResultOk) continue;
                std::string title = utf16ToAsciiLabel(info.title, static_cast<int32_t>(std::size(info.title)));
                if (title.empty()) {
                    title = utf16ToAsciiLabel(info.shortTitle, static_cast<int32_t>(std::size(info.shortTitle)));
                }
                if (title.empty()) {
                    title = "P" + std::to_string(i + 1);
                }
                out.push_back({static_cast<int>(info.id), toCompactLabel(title, 10)});
            }
        }

        void refreshTrackTargetLabels(BaseSystem& baseSystem, AutomationTrack& track) {
            std::vector<AutomationLaneRef> lanes;
            buildLaneRefs(baseSystem, lanes);
            if (lanes.empty()) {
                track.targetLaneType = 0;
                track.targetLaneTrack = 0;
                track.targetLaneLabel = "NONE";
                track.targetDeviceSlot = 0;
                track.targetDeviceLabel = "NONE";
                track.targetParameterSlot = 0;
                track.targetParameterId = -1;
                track.targetParameterLabel = "NONE";
                return;
            }

            int laneCursor = 0;
            for (int i = 0; i < static_cast<int>(lanes.size()); ++i) {
                if (lanes[static_cast<size_t>(i)].type == track.targetLaneType
                    && lanes[static_cast<size_t>(i)].track == track.targetLaneTrack) {
                    laneCursor = i;
                    break;
                }
            }
            laneCursor = std::clamp(laneCursor, 0, static_cast<int>(lanes.size()) - 1);
            track.targetLaneType = lanes[static_cast<size_t>(laneCursor)].type;
            track.targetLaneTrack = lanes[static_cast<size_t>(laneCursor)].track;
            track.targetLaneLabel = lanes[static_cast<size_t>(laneCursor)].label;

            std::vector<AutomationDeviceRef> devices;
            buildDeviceRefs(baseSystem, track, devices);
            if (devices.empty()) {
                track.targetDeviceSlot = 0;
                track.targetDeviceLabel = "NONE";
                track.targetParameterSlot = 0;
                track.targetParameterId = -1;
                track.targetParameterLabel = "NONE";
                return;
            }

            track.targetDeviceSlot = std::clamp(track.targetDeviceSlot, 0, static_cast<int>(devices.size()) - 1);
            Vst3Plugin* selectedDevice = devices[static_cast<size_t>(track.targetDeviceSlot)].plugin;
            track.targetDeviceLabel = devices[static_cast<size_t>(track.targetDeviceSlot)].label;

            std::vector<AutomationParamRef> params;
            buildParamRefs(selectedDevice, params);
            if (params.empty()) {
                track.targetParameterSlot = 0;
                track.targetParameterId = -1;
                track.targetParameterLabel = "NONE";
                return;
            }

            track.targetParameterSlot = std::clamp(track.targetParameterSlot, 0, static_cast<int>(params.size()) - 1);
            track.targetParameterId = params[static_cast<size_t>(track.targetParameterSlot)].id;
            track.targetParameterLabel = params[static_cast<size_t>(track.targetParameterSlot)].label;
        }

        void refreshAllTargetLabels(BaseSystem& baseSystem, DawContext& daw) {
            for (auto& track : daw.automationTracks) {
                refreshTrackTargetLabels(baseSystem, track);
            }
        }

        void closeParamMenu(DawContext& daw) {
            daw.automationParamMenuOpen = false;
            daw.automationParamMenuTrack = -1;
            daw.automationParamMenuHoverIndex = -1;
            daw.automationParamMenuLabels.clear();
        }

        bool openParamMenuForTrack(BaseSystem& baseSystem, DawContext& daw, int trackIndex) {
            if (trackIndex < 0 || trackIndex >= static_cast<int>(daw.automationTracks.size())) {
                closeParamMenu(daw);
                return false;
            }
            AutomationTrack& track = daw.automationTracks[static_cast<size_t>(trackIndex)];
            std::vector<AutomationDeviceRef> devices;
            buildDeviceRefs(baseSystem, track, devices);
            if (devices.empty()) {
                closeParamMenu(daw);
                return false;
            }

            track.targetDeviceSlot = std::clamp(track.targetDeviceSlot, 0, static_cast<int>(devices.size()) - 1);
            std::vector<AutomationParamRef> params;
            buildParamRefs(devices[static_cast<size_t>(track.targetDeviceSlot)].plugin, params);
            if (params.empty()) {
                closeParamMenu(daw);
                return false;
            }

            daw.automationParamMenuLabels.clear();
            daw.automationParamMenuLabels.reserve(params.size());
            for (const auto& param : params) {
                daw.automationParamMenuLabels.push_back(param.label);
            }
            daw.automationParamMenuTrack = trackIndex;
            track.targetParameterSlot = std::clamp(track.targetParameterSlot, 0, static_cast<int>(params.size()) - 1);
            daw.automationParamMenuHoverIndex = track.targetParameterSlot;
            daw.automationParamMenuOpen = true;
            return true;
        }

        void syncParamMenuForTrack(BaseSystem& baseSystem, DawContext& daw) {
            if (!daw.automationParamMenuOpen) return;
            if (daw.automationParamMenuTrack < 0
                || daw.automationParamMenuTrack >= static_cast<int>(daw.automationTracks.size())) {
                closeParamMenu(daw);
                return;
            }
            if (!openParamMenuForTrack(baseSystem, daw, daw.automationParamMenuTrack)) {
                closeParamMenu(daw);
            }
        }

        bool addTrack(BaseSystem& baseSystem, DawContext& daw) {
            int index = getTrackCount(daw);
            (void)index;
            daw.automationTracks.emplace_back();
            daw.automationTrackCount = getTrackCount(daw);
            insertLaneEntry(daw, static_cast<int>(daw.laneOrder.size()), 2, daw.automationTrackCount - 1);
            refreshAllTargetLabels(baseSystem, daw);
            syncParamMenuForTrack(baseSystem, daw);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            daw.uiCacheBuilt = false;
            return true;
        }

        bool insertTrackAt(BaseSystem& baseSystem, DawContext& daw, int laneIndex) {
            daw.automationTracks.emplace_back();
            daw.automationTrackCount = getTrackCount(daw);
            insertLaneEntry(daw, laneIndex, 2, daw.automationTrackCount - 1);
            refreshAllTargetLabels(baseSystem, daw);
            syncParamMenuForTrack(baseSystem, daw);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            daw.uiCacheBuilt = false;
            return true;
        }

        bool removeTrackAt(BaseSystem& baseSystem, DawContext& daw, int trackIndex) {
            int current = getTrackCount(daw);
            if (trackIndex < 0 || trackIndex >= current) return false;
            daw.automationTracks.erase(daw.automationTracks.begin() + trackIndex);
            daw.automationTrackCount = getTrackCount(daw);
            removeLaneEntryForTrack(daw, 2, trackIndex);
            if (daw.selectedAutomationClipTrack == trackIndex) {
                daw.selectedAutomationClipTrack = -1;
                daw.selectedAutomationClipIndex = -1;
            } else if (daw.selectedAutomationClipTrack > trackIndex) {
                daw.selectedAutomationClipTrack -= 1;
            }
            if (daw.automationParamMenuOpen) {
                if (daw.automationParamMenuTrack == trackIndex) {
                    closeParamMenu(daw);
                } else if (daw.automationParamMenuTrack > trackIndex) {
                    daw.automationParamMenuTrack -= 1;
                }
            }
            refreshAllTargetLabels(baseSystem, daw);
            syncParamMenuForTrack(baseSystem, daw);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            daw.uiCacheBuilt = false;
            return true;
        }

        bool moveTrack(BaseSystem& baseSystem, DawContext& daw, int fromIndex, int toIndex) {
            int count = static_cast<int>(daw.laneOrder.size());
            if (fromIndex < 0 || fromIndex >= count) return false;
            if (toIndex < 0) toIndex = 0;
            if (toIndex >= count) toIndex = count - 1;
            if (fromIndex == toIndex) return false;
            DawContext::LaneEntry moved = daw.laneOrder[static_cast<size_t>(fromIndex)];
            daw.laneOrder.erase(daw.laneOrder.begin() + fromIndex);
            daw.laneOrder.insert(daw.laneOrder.begin() + toIndex, moved);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            daw.uiCacheBuilt = false;
            return true;
        }
    }

    void UpdateAutomationTracks(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow*) {
        if (!baseSystem.daw || !baseSystem.ui) return;
        DawContext& daw = *baseSystem.daw;
        UIContext& ui = *baseSystem.ui;

        daw.automationTrackCount = getTrackCount(daw);

        if (ui.active && ui.actionDelayFrames == 0 && !ui.pendingActionType.empty()) {
            if (ui.pendingActionType == "DawAutomationTrack") {
                const std::string key = ui.pendingActionKey;
                int trackCount = getTrackCount(daw);
                int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                int pickedParamSlot = -1;
                if (key == "target_param_pick") {
                    if (!parseTrackAndSlot(ui.pendingActionValue, trackCount, trackIndex, pickedParamSlot)) {
                        trackIndex = -1;
                    }
                }
                if (key == "add") {
                    if (!daw.transportPlaying.load(std::memory_order_relaxed)
                        && !daw.transportRecording.load(std::memory_order_relaxed)) {
                        addTrack(baseSystem, daw);
                    }
                } else if (key == "remove") {
                    if (trackIndex < 0 && trackCount > 0) trackIndex = trackCount - 1;
                    if (trackIndex >= 0
                        && !daw.transportPlaying.load(std::memory_order_relaxed)
                        && !daw.transportRecording.load(std::memory_order_relaxed)) {
                        removeTrackAt(baseSystem, daw, trackIndex);
                    }
                } else if (trackIndex >= 0 && trackIndex < trackCount) {
                    AutomationTrack& track = daw.automationTracks[static_cast<size_t>(trackIndex)];
                    if (key == "arm") {
                        track.armMode = (track.armMode > 0) ? 0 : 1;
                    } else if (key == "clear") {
                        track.clearPending = true;
                        closeParamMenu(daw);
                    } else if (key == "target_lane") {
                        std::vector<AutomationLaneRef> lanes;
                        buildLaneRefs(baseSystem, lanes);
                        if (!lanes.empty()) {
                            int laneCursor = 0;
                            for (int i = 0; i < static_cast<int>(lanes.size()); ++i) {
                                if (lanes[static_cast<size_t>(i)].type == track.targetLaneType
                                    && lanes[static_cast<size_t>(i)].track == track.targetLaneTrack) {
                                    laneCursor = i;
                                    break;
                                }
                            }
                            laneCursor = (laneCursor + 1) % static_cast<int>(lanes.size());
                            track.targetLaneType = lanes[static_cast<size_t>(laneCursor)].type;
                            track.targetLaneTrack = lanes[static_cast<size_t>(laneCursor)].track;
                            track.targetDeviceSlot = 0;
                            track.targetParameterSlot = 0;
                        }
                        closeParamMenu(daw);
                    } else if (key == "target_device") {
                        std::vector<AutomationDeviceRef> devices;
                        buildDeviceRefs(baseSystem, track, devices);
                        if (!devices.empty()) {
                            track.targetDeviceSlot = (track.targetDeviceSlot + 1) % static_cast<int>(devices.size());
                            track.targetParameterSlot = 0;
                        }
                        closeParamMenu(daw);
                    } else if (key == "target_param") {
                        if (daw.automationParamMenuOpen && daw.automationParamMenuTrack == trackIndex) {
                            closeParamMenu(daw);
                        } else {
                            openParamMenuForTrack(baseSystem, daw, trackIndex);
                        }
                    } else if (key == "target_param_pick") {
                        std::vector<AutomationDeviceRef> devices;
                        buildDeviceRefs(baseSystem, track, devices);
                        if (!devices.empty()) {
                            track.targetDeviceSlot = std::clamp(track.targetDeviceSlot, 0, static_cast<int>(devices.size()) - 1);
                            std::vector<AutomationParamRef> params;
                            buildParamRefs(devices[static_cast<size_t>(track.targetDeviceSlot)].plugin, params);
                            if (!params.empty()) {
                                track.targetParameterSlot = std::clamp(pickedParamSlot, 0, static_cast<int>(params.size()) - 1);
                            }
                        }
                        closeParamMenu(daw);
                    }
                }

                ui.pendingActionType.clear();
                ui.pendingActionKey.clear();
                ui.pendingActionValue.clear();
            }
        }

        bool clearedAny = false;
        for (int i = 0; i < getTrackCount(daw); ++i) {
            AutomationTrack& track = daw.automationTracks[static_cast<size_t>(i)];
            if (!track.clearPending) continue;
            track.clips.clear();
            track.clearPending = false;
            if (daw.selectedAutomationClipTrack == i) {
                daw.selectedAutomationClipTrack = -1;
                daw.selectedAutomationClipIndex = -1;
            }
            clearedAny = true;
        }

        if (clearedAny) {
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            daw.uiCacheBuilt = false;
        }

        refreshAllTargetLabels(baseSystem, daw);
        syncParamMenuForTrack(baseSystem, daw);
    }

    bool InsertTrackAt(BaseSystem& baseSystem, int trackIndex) {
        if (!baseSystem.daw) return false;
        return insertTrackAt(baseSystem, *baseSystem.daw, trackIndex);
    }

    bool RemoveTrackAt(BaseSystem& baseSystem, int trackIndex) {
        if (!baseSystem.daw) return false;
        return removeTrackAt(baseSystem, *baseSystem.daw, trackIndex);
    }

    bool MoveTrack(BaseSystem& baseSystem, int fromIndex, int toIndex) {
        if (!baseSystem.daw) return false;
        return moveTrack(baseSystem, *baseSystem.daw, fromIndex, toIndex);
    }
}
