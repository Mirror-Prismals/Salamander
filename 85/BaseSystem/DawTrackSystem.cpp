#pragma once

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <iostream>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vst3SystemLogic {
    void EnsureAudioTrackCount(Vst3Context& ctx, int trackCount);
    void RemoveAudioTrackChain(Vst3Context& ctx, int trackIndex);
    void MoveAudioTrackChain(Vst3Context& ctx, int fromIndex, int toIndex);
}
namespace MidiTrackSystemLogic {
    bool RemoveTrackAt(BaseSystem& baseSystem, int trackIndex);
}
namespace AutomationTrackSystemLogic {
    bool RemoveTrackAt(BaseSystem& baseSystem, int trackIndex);
}
namespace DawIOSystemLogic {
    bool ResolveMirrorPath(DawContext& daw);
    void LoadTracksIfAvailable(DawContext& daw);
    void LoadMetronomeSample(DawContext& daw);
    void WriteTrackAt(DawContext& daw, int trackIndex);
}

namespace DawTrackSystemLogic {

    namespace {
        constexpr int kRecordRingSeconds = 2;

        enum TransportLatch {
            kTransportNone = 0,
            kTransportStop = 1,
            kTransportPlay = 2,
            kTransportRecord = 3
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

        int parseBus(const std::string& value) {
            if (value == "L") return 0;
            if (value == "S" || value == "SUB") return 1;
            if (value == "FF" || value == "F") return 2;
            if (value == "R") return 3;
            if (value == "OFF" || value == "O") return -1;
            return -99;
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

        int nextBusIndex(int current) {
            if (current < -1 || current >= DawContext::kBusCount) current = DawContext::kBusCount - 1;
            if (current == DawContext::kBusCount - 1) return -1;
            return current + 1;
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
            if (trackIdx < 0 || busIdx < -1) return false;
            outTrack = trackIdx;
            outBus = busIdx;
            return true;
        }

        int getTrackCount(const DawContext& daw) {
            return static_cast<int>(daw.tracks.size());
        }

        int getMidiTrackCount(const BaseSystem& baseSystem) {
            if (!baseSystem.midi) return 0;
            return static_cast<int>(baseSystem.midi->tracks.size());
        }

        int getAutomationTrackCount(const DawContext& daw) {
            return static_cast<int>(daw.automationTracks.size());
        }

        void rebuildLaneOrder(DawContext& daw, int audioCount, int midiCount, int automationCount) {
            daw.laneOrder.clear();
            daw.laneOrder.reserve(static_cast<size_t>(audioCount + midiCount + automationCount));
            for (int i = 0; i < audioCount; ++i) {
                daw.laneOrder.push_back({0, i});
            }
            for (int i = 0; i < midiCount; ++i) {
                daw.laneOrder.push_back({1, i});
            }
            for (int i = 0; i < automationCount; ++i) {
                daw.laneOrder.push_back({2, i});
            }
        }

        void ensureLaneOrder(DawContext& daw, int audioCount, int midiCount, int automationCount) {
            int total = audioCount + midiCount + automationCount;
            bool rebuild = daw.laneOrder.empty() || static_cast<int>(daw.laneOrder.size()) != total;
            if (!rebuild) {
                std::vector<int> audioSeen(audioCount, 0);
                std::vector<int> midiSeen(midiCount, 0);
                std::vector<int> automationSeen(automationCount, 0);
                for (const auto& entry : daw.laneOrder) {
                    if (entry.type == 0) {
                        if (entry.trackIndex < 0 || entry.trackIndex >= audioCount) { rebuild = true; break; }
                        audioSeen[entry.trackIndex] += 1;
                    } else if (entry.type == 1) {
                        if (entry.trackIndex < 0 || entry.trackIndex >= midiCount) { rebuild = true; break; }
                        midiSeen[entry.trackIndex] += 1;
                    } else if (entry.type == 2) {
                        if (entry.trackIndex < 0 || entry.trackIndex >= automationCount) { rebuild = true; break; }
                        automationSeen[entry.trackIndex] += 1;
                    } else {
                        rebuild = true;
                        break;
                    }
                }
                if (!rebuild) {
                    for (int c : audioSeen) { if (c != 1) { rebuild = true; break; } }
                    if (!rebuild) {
                        for (int c : midiSeen) { if (c != 1) { rebuild = true; break; } }
                    }
                    if (!rebuild) {
                        for (int c : automationSeen) { if (c != 1) { rebuild = true; break; } }
                    }
                }
            }
            if (rebuild) {
                rebuildLaneOrder(daw, audioCount, midiCount, automationCount);
            }
            if (daw.selectedLaneIndex >= total) {
                daw.selectedLaneIndex = total - 1;
            }
            if (daw.selectedLaneIndex < 0 && total > 0 && !daw.allowEmptySelection) {
                daw.selectedLaneIndex = 0;
            }
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

        void reconnectTrackInput(AudioContext& audio, DawTrack& track) {
            if (!audio.client) return;
            if (track.inputIndex < 0 || track.inputIndex >= static_cast<int>(audio.input_ports.size())) return;
            jack_port_t* inputPort = audio.input_ports[track.inputIndex];
            if (!inputPort) return;
            std::string inputName = jack_port_name(inputPort);
            for (const auto& physical : audio.physicalInputPorts) {
                int rc = jack_disconnect(audio.client, physical.c_str(), inputName.c_str());
                if (rc != 0 && rc != EEXIST && rc != ENOENT) {
                    std::cerr << "JACK disconnect failed: " << physical << " -> " << inputName << " (" << rc << ")\n";
                }
            }
            int physicalCount = static_cast<int>(audio.physicalInputPorts.size());
            bool stereoPair12 = track.stereoInputPair12 && physicalCount >= 2;
            bool useVirtual = !stereoPair12
                && (track.physicalInputIndex < 0 || track.physicalInputIndex >= physicalCount);
            track.useVirtualInput.store(useVirtual, std::memory_order_relaxed);
            if (!useVirtual) {
                int srcIndex = stereoPair12 ? 0 : track.physicalInputIndex;
                srcIndex = std::clamp(srcIndex, 0, std::max(0, physicalCount - 1));
                const std::string& src = audio.physicalInputPorts[static_cast<size_t>(srcIndex)];
                int rc = jack_connect(audio.client, src.c_str(), inputName.c_str());
                if (rc != 0 && rc != EEXIST) {
                    std::cerr << "JACK connect failed: " << src << " -> " << inputName << " (" << rc << ")\n";
                }
            }
        }

        void refreshPhysicalInputs(AudioContext& audio) {
            if (!audio.client) return;
            audio.physicalInputPorts.clear();
            if (const char** capturePorts = jack_get_ports(audio.client, nullptr, JACK_DEFAULT_AUDIO_TYPE,
                                                           JackPortIsOutput | JackPortIsPhysical)) {
                for (size_t i = 0; capturePorts[i]; ++i) {
                    audio.physicalInputPorts.emplace_back(capturePorts[i]);
                }
                jack_free(capturePorts);
            }
        }

        void initTrack(DawTrack& track, int index, AudioContext& audio, float sampleRate) {
            track.inputIndex = index;
            track.outputBus.store(2, std::memory_order_relaxed);
            track.outputBusL.store(track.outputBus.load(std::memory_order_relaxed), std::memory_order_relaxed);
            track.outputBusR.store(track.outputBus.load(std::memory_order_relaxed), std::memory_order_relaxed);
            track.stereoInputPair12 = false;
            int physicalCount = static_cast<int>(audio.physicalInputPorts.size());
            if (physicalCount > 0) {
                track.physicalInputIndex = index % physicalCount;
            } else {
                track.physicalInputIndex = 0;
            }
            track.useVirtualInput.store(physicalCount == 0, std::memory_order_relaxed);
            if (!track.recordRing) {
                size_t ringBytes = static_cast<size_t>(sampleRate) * kRecordRingSeconds * sizeof(float);
                track.recordRing = jack_ringbuffer_create(ringBytes);
                if (track.recordRing) {
                    jack_ringbuffer_mlock(track.recordRing);
                }
            }
            if (!track.recordRingRight) {
                size_t ringBytes = static_cast<size_t>(sampleRate) * kRecordRingSeconds * sizeof(float);
                track.recordRingRight = jack_ringbuffer_create(ringBytes);
                if (track.recordRingRight) {
                    jack_ringbuffer_mlock(track.recordRingRight);
                }
            }
            if (physicalCount > 0) {
                reconnectTrackInput(audio, track);
            }
        }

        void cleanupTrack(DawTrack& track) {
            if (track.recordRing) {
                jack_ringbuffer_free(track.recordRing);
                track.recordRing = nullptr;
            }
            if (track.recordRingRight) {
                jack_ringbuffer_free(track.recordRingRight);
                track.recordRingRight = nullptr;
            }
        }

        void refreshTrackRouting(DawContext& daw, AudioContext& audio) {
            int physicalCount = static_cast<int>(audio.physicalInputPorts.size());
            for (int i = 0; i < getTrackCount(daw); ++i) {
                DawTrack& track = daw.tracks[i];
                track.inputIndex = i;
                if (track.physicalInputIndex < 0 || track.physicalInputIndex >= std::max(1, physicalCount + 1)) {
                    track.physicalInputIndex = 0;
                }
                if (track.stereoInputPair12 && physicalCount < 2) {
                    track.stereoInputPair12 = false;
                }
                track.useVirtualInput.store(physicalCount == 0, std::memory_order_relaxed);
                if (physicalCount > 0) {
                    reconnectTrackInput(audio, track);
                }
            }
        }

        int detectMirrorTrackCount(const DawContext& daw) {
            if (!daw.mirrorAvailable) return 0;
            std::error_code ec;
            int maxIndex = 0;
            for (const auto& entry : std::filesystem::directory_iterator(daw.mirrorPath, ec)) {
                if (ec || !entry.is_regular_file()) continue;
                const std::string name = entry.path().filename().string();
                if (name.rfind("track_", 0) != 0) continue;
                if (name.size() <= 10 || name.substr(name.size() - 4) != ".wav") continue;
                std::string num = name.substr(6, name.size() - 10);
                try {
                    int idx = std::stoi(num);
                    if (idx > maxIndex) maxIndex = idx;
                } catch (...) {
                }
            }
            return maxIndex;
        }

        void deleteStaleTrackFile(const DawContext& daw, int oneBasedIndex) {
            if (!daw.mirrorAvailable || oneBasedIndex <= 0) return;
            std::filesystem::path outPath = std::filesystem::path(daw.mirrorPath)
                / ("track_" + std::to_string(oneBasedIndex) + ".wav");
            std::error_code ec;
            std::filesystem::remove(outPath, ec);
        }

        void ensureTrackCount(BaseSystem& baseSystem, DawContext& daw, AudioContext& audio, int desired) {
            if (desired < 0) desired = 0;
            std::lock_guard<std::mutex> lock(daw.trackMutex);
            int current = getTrackCount(daw);
            if (desired > current) {
                daw.tracks.resize(static_cast<size_t>(desired));
                for (int i = current; i < desired; ++i) {
                    initTrack(daw.tracks[i], i, audio, daw.sampleRate);
                }
            } else if (desired < current) {
                int oldCount = current;
                for (int i = current - 1; i >= desired; --i) {
                    cleanupTrack(daw.tracks[static_cast<size_t>(i)]);
                }
                daw.tracks.erase(daw.tracks.begin() + desired, daw.tracks.end());
                deleteStaleTrackFile(daw, oldCount);
            }
            daw.trackCount = getTrackCount(daw);
            refreshTrackRouting(daw, audio);
            if (baseSystem.vst3) {
                Vst3SystemLogic::EnsureAudioTrackCount(*baseSystem.vst3, daw.trackCount);
            }
        }

        bool removeTrackAt(BaseSystem& baseSystem, DawContext& daw, AudioContext& audio, int trackIndex) {
            int current = getTrackCount(daw);
            if (trackIndex < 0 || trackIndex >= current) return false;
            std::lock_guard<std::mutex> lock(daw.trackMutex);
            int oldCount = current;
            if (baseSystem.vst3) {
                Vst3SystemLogic::RemoveAudioTrackChain(*baseSystem.vst3, trackIndex);
            }
            cleanupTrack(daw.tracks[static_cast<size_t>(trackIndex)]);
            daw.tracks.erase(daw.tracks.begin() + trackIndex);
            daw.trackCount = getTrackCount(daw);
            refreshTrackRouting(daw, audio);
            deleteStaleTrackFile(daw, oldCount);
            if (baseSystem.vst3) {
                Vst3SystemLogic::EnsureAudioTrackCount(*baseSystem.vst3, daw.trackCount);
            }
            removeLaneEntryForTrack(daw, 0, trackIndex);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            daw.uiCacheBuilt = false;
            return true;
        }

        bool moveTrack(BaseSystem& baseSystem, DawContext& daw, AudioContext&, int fromIndex, int toIndex) {
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

        int addTrackInternal(BaseSystem& baseSystem, DawContext& daw, AudioContext& audio) {
            std::lock_guard<std::mutex> lock(daw.trackMutex);
            int index = getTrackCount(daw);
            daw.tracks.emplace_back();
            initTrack(daw.tracks.back(), index, audio, daw.sampleRate);
            daw.trackCount = getTrackCount(daw);
            refreshTrackRouting(daw, audio);
            if (baseSystem.vst3) {
                Vst3SystemLogic::EnsureAudioTrackCount(*baseSystem.vst3, daw.trackCount);
            }
            return index;
        }

        bool addTrack(BaseSystem& baseSystem, DawContext& daw, AudioContext& audio) {
            int index = addTrackInternal(baseSystem, daw, audio);
            insertLaneEntry(daw, static_cast<int>(daw.laneOrder.size()), 0, index);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            daw.uiCacheBuilt = false;
            return true;
        }

        bool insertTrackAt(BaseSystem& baseSystem, DawContext& daw, AudioContext& audio, int trackIndex) {
            int laneIndex = trackIndex;
            int newIndex = addTrackInternal(baseSystem, daw, audio);
            insertLaneEntry(daw, laneIndex, 0, newIndex);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            daw.uiCacheBuilt = false;
            return true;
        }

        void initializeDaw(DawContext& daw, AudioContext& audio) {
            if (daw.initialized) return;
            refreshPhysicalInputs(audio);
            daw.sampleRate = audio.sampleRate > 0.0f ? audio.sampleRate : 44100.0f;
            DawIOSystemLogic::ResolveMirrorPath(daw);
            if (daw.exportFolderPath.empty()) {
                daw.exportFolderPath = daw.mirrorAvailable
                    ? daw.mirrorPath
                    : std::filesystem::current_path().string();
            }
            if (daw.trackCount <= 0 && daw.mirrorAvailable) {
                daw.trackCount = detectMirrorTrackCount(daw);
            }
            if (daw.trackCount < 0) daw.trackCount = 0;
            if (daw.tracks.empty() && daw.trackCount > 0) {
                daw.tracks.resize(static_cast<size_t>(daw.trackCount));
            }
            if (daw.trackCount != getTrackCount(daw)) {
                daw.trackCount = getTrackCount(daw);
            }
            for (int i = 0; i < getTrackCount(daw); ++i) {
                initTrack(daw.tracks[static_cast<size_t>(i)], i, audio, daw.sampleRate);
            }
            DawIOSystemLogic::LoadTracksIfAvailable(daw);
            DawIOSystemLogic::LoadMetronomeSample(daw);
            daw.timelineZeroSample = 0;
            if (daw.loopEndSamples == 0) {
                double bpm = daw.bpm.load(std::memory_order_relaxed);
                if (bpm <= 0.0) bpm = 120.0;
                double secondsPerBeat = 60.0 / bpm;
                uint64_t bars = 4;
                daw.loopStartSamples = static_cast<uint64_t>(std::max<int64_t>(0, daw.timelineZeroSample));
                daw.loopEndSamples = static_cast<uint64_t>(std::llround(secondsPerBeat * 4.0 * bars * daw.sampleRate));
                if (daw.loopEndSamples <= daw.loopStartSamples) {
                    daw.loopEndSamples = daw.loopStartSamples + 1;
                }
            }
            daw.playheadSample.store(static_cast<uint64_t>(std::max<int64_t>(0, daw.timelineZeroSample)),
                                     std::memory_order_relaxed);
            daw.transportPlaying.store(false, std::memory_order_relaxed);
            daw.transportRecording.store(false, std::memory_order_relaxed);
            daw.audioThreadIdle.store(true, std::memory_order_relaxed);
            daw.transportLatch = kTransportNone;
            daw.timelineSecondsPerScreen = 10.0;
            daw.timelineOffsetSamples = daw.timelineZeroSample;
            audio.daw = &daw;
            daw.initialized = true;
        }

        bool isTrackRowWorld(const std::string& name) {
            if (name == "TrackRowWorld") return true;
            return name.rfind("TrackRowWorld_", 0) == 0;
        }
    }

    void UpdateDawTracks(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float, GLFWwindow* win) {
        (void)prototypes;
        if (!baseSystem.daw || !baseSystem.audio || !baseSystem.ui) return;
        DawContext& daw = *baseSystem.daw;
        AudioContext& audio = *baseSystem.audio;
        UIContext& ui = *baseSystem.ui;

        if (!daw.initialized && audio.client) {
            initializeDaw(daw, audio);
        }
        if (!daw.initialized) return;

        daw.trackCount = getTrackCount(daw);
        int midiCount = getMidiTrackCount(baseSystem);
        int automationCount = getAutomationTrackCount(daw);
        ensureLaneOrder(daw, daw.trackCount, midiCount, automationCount);
        if (daw.selectedLaneIndex >= 0 && daw.selectedLaneIndex < static_cast<int>(daw.laneOrder.size())) {
            const auto& entry = daw.laneOrder[static_cast<size_t>(daw.selectedLaneIndex)];
            daw.selectedLaneType = entry.type;
            daw.selectedLaneTrack = entry.trackIndex;
        } else {
            daw.selectedLaneType = -1;
            daw.selectedLaneTrack = -1;
        }

        if (ui.active && ui.actionDelayFrames == 0 && !ui.pendingActionType.empty()) {
            if (ui.pendingActionType == "DawTrack") {
                const std::string key = ui.pendingActionKey;
                int trackCount = getTrackCount(daw);
                if (key == "arm") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        int current = daw.tracks[trackIndex].armMode.load(std::memory_order_relaxed);
                        int next = (current + 1) % 3;
                        daw.tracks[trackIndex].armMode.store(next, std::memory_order_relaxed);
                    }
                } else if (key == "solo") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        bool newSolo = !daw.tracks[trackIndex].solo.load(std::memory_order_relaxed);
                        daw.tracks[trackIndex].solo.store(newSolo, std::memory_order_relaxed);
                        if (newSolo) {
                            daw.tracks[trackIndex].mute.store(false, std::memory_order_relaxed);
                        }
                    }
                } else if (key == "mute") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        bool newMute = !daw.tracks[trackIndex].mute.load(std::memory_order_relaxed);
                        daw.tracks[trackIndex].mute.store(newMute, std::memory_order_relaxed);
                        if (newMute) {
                            daw.tracks[trackIndex].solo.store(false, std::memory_order_relaxed);
                        }
                    }
                } else if (key == "output") {
                    int trackIndex = -1;
                    int busIndex = -1;
                    if (parseTrackAndBus(ui.pendingActionValue, trackCount, trackIndex, busIndex)) {
                        daw.tracks[trackIndex].outputBus.store(busIndex, std::memory_order_relaxed);
                        daw.tracks[trackIndex].outputBusL.store(busIndex, std::memory_order_relaxed);
                        daw.tracks[trackIndex].outputBusR.store(busIndex, std::memory_order_relaxed);
                    } else {
                        trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                        if (trackIndex >= 0) {
                            int current = daw.tracks[trackIndex].outputBus.load(std::memory_order_relaxed);
                            int next = nextBusIndex(current);
                            daw.tracks[trackIndex].outputBus.store(next, std::memory_order_relaxed);
                            daw.tracks[trackIndex].outputBusL.store(next, std::memory_order_relaxed);
                            daw.tracks[trackIndex].outputBusR.store(next, std::memory_order_relaxed);
                        }
                    }
                } else if (key == "output_l") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        int current = daw.tracks[trackIndex].outputBusL.load(std::memory_order_relaxed);
                        int next = nextBusIndex(current);
                        daw.tracks[trackIndex].outputBusL.store(next, std::memory_order_relaxed);
                        if (daw.tracks[trackIndex].outputBusL.load(std::memory_order_relaxed)
                            == daw.tracks[trackIndex].outputBusR.load(std::memory_order_relaxed)) {
                            daw.tracks[trackIndex].outputBus.store(next, std::memory_order_relaxed);
                        }
                    }
                } else if (key == "output_r") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        int current = daw.tracks[trackIndex].outputBusR.load(std::memory_order_relaxed);
                        int next = nextBusIndex(current);
                        daw.tracks[trackIndex].outputBusR.store(next, std::memory_order_relaxed);
                        if (daw.tracks[trackIndex].outputBusL.load(std::memory_order_relaxed)
                            == daw.tracks[trackIndex].outputBusR.load(std::memory_order_relaxed)) {
                            daw.tracks[trackIndex].outputBus.store(next, std::memory_order_relaxed);
                        }
                    }
                } else if (key == "input") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        refreshPhysicalInputs(audio);
                        int physicalCount = static_cast<int>(audio.physicalInputPorts.size());
                        int stateCount = physicalCount + 1 + ((physicalCount >= 2) ? 1 : 0);
                        if (stateCount > 0) {
                            int stereoState = physicalCount + 1;
                            int currentState = daw.tracks[trackIndex].stereoInputPair12
                                ? stereoState
                                : std::clamp(daw.tracks[trackIndex].physicalInputIndex, 0, std::max(0, physicalCount));
                            int nextState = (currentState + 1) % stateCount;
                            if (physicalCount >= 2 && nextState == stereoState) {
                                daw.tracks[trackIndex].stereoInputPair12 = true;
                                daw.tracks[trackIndex].physicalInputIndex = 0;
                                daw.tracks[trackIndex].useVirtualInput.store(false, std::memory_order_relaxed);
                            } else {
                                daw.tracks[trackIndex].stereoInputPair12 = false;
                                daw.tracks[trackIndex].physicalInputIndex =
                                    std::clamp(nextState, 0, std::max(0, physicalCount));
                                daw.tracks[trackIndex].useVirtualInput.store(nextState >= physicalCount,
                                                                             std::memory_order_relaxed);
                            }
                            reconnectTrackInput(audio, daw.tracks[trackIndex]);
                        }
                    }
                } else if (key == "clear") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        daw.tracks[trackIndex].clearPending = true;
                    }
                } else if (key == "add") {
                    if (!daw.transportPlaying.load(std::memory_order_relaxed)
                        && !daw.transportRecording.load(std::memory_order_relaxed)) {
                        addTrack(baseSystem, daw, audio);
                    }
                } else if (key == "remove") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex < 0 && trackCount > 0) {
                        trackIndex = trackCount - 1;
                    }
                    if (trackIndex >= 0
                        && !daw.transportPlaying.load(std::memory_order_relaxed)
                        && !daw.transportRecording.load(std::memory_order_relaxed)) {
                        removeTrackAt(baseSystem, daw, audio, trackIndex);
                    }
                }
                ui.pendingActionType.clear();
                ui.pendingActionKey.clear();
                ui.pendingActionValue.clear();
            }
        }

        if (!daw.transportPlaying.load(std::memory_order_relaxed)) {
            for (int i = 0; i < getTrackCount(daw); ++i) {
                auto& track = daw.tracks[static_cast<size_t>(i)];
                if (!track.clearPending) continue;
                track.audio.clear();
                track.audioRight.clear();
                track.pendingRecord.clear();
                track.pendingRecordRight.clear();
                track.clips.clear();
                track.loopTakeClips.clear();
                track.waveformMin.clear();
                track.waveformMax.clear();
                track.waveformMinRight.clear();
                track.waveformMaxRight.clear();
                track.waveformColor.clear();
                track.waveformVersion += 1;
                track.activeLoopTakeIndex = -1;
                track.takeStackExpanded = false;
                track.nextTakeId = 1;
                track.loopTakeRangeStartSample = 0;
                track.loopTakeRangeLength = 0;
                track.recordLoopCapture = false;
                if (track.recordRing) {
                    jack_ringbuffer_reset(track.recordRing);
                }
                if (track.recordRingRight) {
                    jack_ringbuffer_reset(track.recordRingRight);
                }
                DawIOSystemLogic::WriteTrackAt(daw, i);
                track.clearPending = false;
            }
        }

        if (ui.active && win) {
            static bool deletePressedLast = false;
            bool deletePressed = glfwGetKey(win, GLFW_KEY_DELETE) == GLFW_PRESS
                || glfwGetKey(win, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
            if (deletePressed && !deletePressedLast) {
                if (daw.selectedLaneType == 1 && baseSystem.midi && daw.selectedLaneTrack >= 0) {
                    int idx = daw.selectedLaneTrack;
                    if (MidiTrackSystemLogic::RemoveTrackAt(baseSystem, idx)) {
                        daw.dragActive = false;
                        daw.dragPending = false;
                        daw.dragLaneIndex = -1;
                        daw.dragLaneType = -1;
                        daw.dragLaneTrack = -1;
                        daw.dragDropIndex = -1;
                    }
                } else if (daw.selectedLaneType == 2 && daw.selectedLaneTrack >= 0) {
                    int idx = daw.selectedLaneTrack;
                    if (AutomationTrackSystemLogic::RemoveTrackAt(baseSystem, idx)) {
                        daw.dragActive = false;
                        daw.dragPending = false;
                        daw.dragLaneIndex = -1;
                        daw.dragLaneType = -1;
                        daw.dragLaneTrack = -1;
                        daw.dragDropIndex = -1;
                    }
                } else if (daw.selectedLaneType == 0 && daw.selectedLaneTrack >= 0) {
                    int idx = daw.selectedLaneTrack;
                    if (RemoveTrackAt(baseSystem, idx)) {
                        daw.dragActive = false;
                        daw.dragPending = false;
                        daw.dragLaneIndex = -1;
                        daw.dragLaneType = -1;
                        daw.dragLaneTrack = -1;
                        daw.dragDropIndex = -1;
                    }
                }
            }
            deletePressedLast = deletePressed;
        }
    }

    void CleanupDawTracks(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow*) {
        if (!baseSystem.daw) return;
        DawContext& daw = *baseSystem.daw;
        for (auto& track : daw.tracks) {
            if (track.recordRing) {
                jack_ringbuffer_free(track.recordRing);
                track.recordRing = nullptr;
            }
            if (track.recordRingRight) {
                jack_ringbuffer_free(track.recordRingRight);
                track.recordRingRight = nullptr;
            }
        }
        if (baseSystem.audio) {
            baseSystem.audio->daw = nullptr;
        }
        daw.initialized = false;
    }

    bool InsertTrackAt(BaseSystem& baseSystem, int trackIndex) {
        if (!baseSystem.daw || !baseSystem.audio) return false;
        return insertTrackAt(baseSystem, *baseSystem.daw, *baseSystem.audio, trackIndex);
    }

    bool RemoveTrackAt(BaseSystem& baseSystem, int trackIndex) {
        if (!baseSystem.daw || !baseSystem.audio) return false;
        return removeTrackAt(baseSystem, *baseSystem.daw, *baseSystem.audio, trackIndex);
    }

    bool MoveTrack(BaseSystem& baseSystem, int fromIndex, int toIndex) {
        if (!baseSystem.daw || !baseSystem.audio) return false;
        return moveTrack(baseSystem, *baseSystem.daw, *baseSystem.audio, fromIndex, toIndex);
    }
}
