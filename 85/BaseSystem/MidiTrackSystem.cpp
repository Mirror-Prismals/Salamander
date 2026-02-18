#pragma once

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>
#include <jack/jack.h>
#include <jack/ringbuffer.h>

namespace MidiIOSystemLogic {
    void LoadTracksIfAvailable(MidiContext& midi, const DawContext& daw);
    void WriteDefaultTrackFile(MidiContext& midi, const DawContext& daw, const std::vector<float>& data);
}
namespace Vst3SystemLogic {
    void EnsureMidiTrackCount(Vst3Context& ctx, int trackCount);
    void RemoveMidiTrackChain(Vst3Context& ctx, int trackIndex);
}

namespace MidiTrackSystemLogic {

    namespace {
        constexpr int kRecordRingSeconds = 2;

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

        int getTrackCount(const MidiContext& midi) {
            return static_cast<int>(midi.tracks.size());
        }

        int nextBusIndex(int current) {
            if (current < -1 || current >= DawContext::kBusCount) current = DawContext::kBusCount - 1;
            if (current == DawContext::kBusCount - 1) return -1;
            return current + 1;
        }

        void initTrack(MidiTrack& track, int index, MidiContext& midi, AudioContext& audio) {
            (void)index;
            (void)audio;
            track.outputBus.store(2, std::memory_order_relaxed);
            track.outputBusL.store(track.outputBus.load(std::memory_order_relaxed), std::memory_order_relaxed);
            track.outputBusR.store(track.outputBus.load(std::memory_order_relaxed), std::memory_order_relaxed);
            track.physicalInputIndex = 0;
            if (!track.recordRing) {
                size_t ringBytes = static_cast<size_t>(midi.sampleRate) * kRecordRingSeconds * sizeof(float);
                track.recordRing = jack_ringbuffer_create(ringBytes);
                if (track.recordRing) {
                    jack_ringbuffer_mlock(track.recordRing);
                }
            }
        }

        void cleanupTrack(MidiTrack& track) {
            if (track.recordRing) {
                jack_ringbuffer_free(track.recordRing);
                track.recordRing = nullptr;
            }
        }

        void deleteStaleTrackFile(const DawContext& daw, int oneBasedIndex) {
            if (!daw.mirrorAvailable || oneBasedIndex <= 0) return;
            std::filesystem::path outPath = std::filesystem::path(daw.mirrorPath)
                / ("midi_track_" + std::to_string(oneBasedIndex) + ".wav");
            std::error_code ec;
            std::filesystem::remove(outPath, ec);
        }

        int detectMirrorTrackCount(const DawContext& daw) {
            if (!daw.mirrorAvailable) return 0;
            std::error_code ec;
            int maxIndex = 0;
            for (const auto& entry : std::filesystem::directory_iterator(daw.mirrorPath, ec)) {
                if (ec || !entry.is_regular_file()) continue;
                const std::string name = entry.path().filename().string();
                if (name.rfind("midi_track_", 0) != 0) continue;
                if (name.size() <= 13 || name.substr(name.size() - 4) != ".wav") continue;
                std::string num = name.substr(11, name.size() - 15);
                try {
                    int idx = std::stoi(num);
                    if (idx > maxIndex) maxIndex = idx;
                } catch (...) {
                }
            }
            return maxIndex;
        }

        void ensureTrackCount(BaseSystem& baseSystem, MidiContext& midi, AudioContext& audio, const DawContext& daw, int desired) {
            if (desired < 0) desired = 0;
            std::lock_guard<std::mutex> lock(midi.trackMutex);
            int current = getTrackCount(midi);
            if (desired > current) {
                midi.tracks.resize(static_cast<size_t>(desired));
                for (int i = current; i < desired; ++i) {
                    initTrack(midi.tracks[i], i, midi, audio);
                }
            } else if (desired < current) {
                int oldCount = current;
                for (int i = current - 1; i >= desired; --i) {
                    if (baseSystem.vst3) {
                        Vst3SystemLogic::RemoveMidiTrackChain(*baseSystem.vst3, i);
                    }
                    cleanupTrack(midi.tracks[static_cast<size_t>(i)]);
                }
                midi.tracks.erase(midi.tracks.begin() + desired, midi.tracks.end());
                deleteStaleTrackFile(daw, oldCount);
            }
            midi.trackCount = getTrackCount(midi);
            if (baseSystem.vst3) {
                Vst3SystemLogic::EnsureMidiTrackCount(*baseSystem.vst3, midi.trackCount);
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

        bool removeTrackAt(BaseSystem& baseSystem, MidiContext& midi, AudioContext& audio, DawContext& daw, int trackIndex) {
            int current = getTrackCount(midi);
            if (trackIndex < 0 || trackIndex >= current) return false;
            std::lock_guard<std::mutex> lock(midi.trackMutex);
            int oldCount = current;
            if (baseSystem.vst3) {
                Vst3SystemLogic::RemoveMidiTrackChain(*baseSystem.vst3, trackIndex);
            }
            cleanupTrack(midi.tracks[static_cast<size_t>(trackIndex)]);
            midi.tracks.erase(midi.tracks.begin() + trackIndex);
            midi.trackCount = getTrackCount(midi);
            if (baseSystem.vst3) {
                Vst3SystemLogic::EnsureMidiTrackCount(*baseSystem.vst3, midi.trackCount);
            }
            deleteStaleTrackFile(daw, oldCount);
            removeLaneEntryForTrack(daw, 1, trackIndex);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            midi.uiCacheBuilt = false;
            return true;
        }

        bool moveTrack(BaseSystem& baseSystem, MidiContext& midi, DawContext& daw, int fromIndex, int toIndex) {
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
            midi.uiCacheBuilt = false;
            return true;
        }

        bool addTrack(BaseSystem& baseSystem, MidiContext& midi, AudioContext& audio, DawContext& daw) {
            std::lock_guard<std::mutex> lock(midi.trackMutex);
            int index = getTrackCount(midi);
            midi.tracks.emplace_back();
            initTrack(midi.tracks.back(), index, midi, audio);
            midi.trackCount = getTrackCount(midi);
            if (baseSystem.vst3) {
                Vst3SystemLogic::EnsureMidiTrackCount(*baseSystem.vst3, midi.trackCount);
            }
            insertLaneEntry(daw, static_cast<int>(daw.laneOrder.size()), 1, index);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            midi.uiCacheBuilt = false;
            return true;
        }

        bool insertTrackAt(BaseSystem& baseSystem, MidiContext& midi, AudioContext& audio, DawContext& daw, int trackIndex) {
            std::lock_guard<std::mutex> lock(midi.trackMutex);
            int index = getTrackCount(midi);
            midi.tracks.emplace_back();
            initTrack(midi.tracks.back(), index, midi, audio);
            midi.trackCount = getTrackCount(midi);
            if (baseSystem.vst3) {
                Vst3SystemLogic::EnsureMidiTrackCount(*baseSystem.vst3, midi.trackCount);
            }
            insertLaneEntry(daw, trackIndex, 1, index);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            midi.uiCacheBuilt = false;
            return true;
        }

        void initializeMidi(BaseSystem& baseSystem, MidiContext& midi, AudioContext& audio, DawContext& daw) {
            midi.sampleRate = audio.sampleRate > 0.0f ? audio.sampleRate : 44100.0f;
            if (midi.trackCount <= 0 && daw.mirrorAvailable) {
                midi.trackCount = detectMirrorTrackCount(daw);
            }
            if (midi.trackCount < 0) midi.trackCount = 0;
            if (midi.tracks.empty() && midi.trackCount > 0) {
                midi.tracks.resize(static_cast<size_t>(midi.trackCount));
            }
            if (midi.trackCount != getTrackCount(midi)) {
                midi.trackCount = getTrackCount(midi);
            }
            for (int i = 0; i < getTrackCount(midi); ++i) {
                initTrack(midi.tracks[static_cast<size_t>(i)], i, midi, audio);
            }
            if (baseSystem.vst3) {
                Vst3SystemLogic::EnsureMidiTrackCount(*baseSystem.vst3, getTrackCount(midi));
            }
            MidiIOSystemLogic::LoadTracksIfAvailable(midi, daw);
            audio.midi = &midi;
            midi.initialized = true;
        }
    }

    void UpdateMidiTracks(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow* win) {
        if (!baseSystem.midi || !baseSystem.audio || !baseSystem.ui || !baseSystem.daw) return;
        MidiContext& midi = *baseSystem.midi;
        AudioContext& audio = *baseSystem.audio;
        UIContext& ui = *baseSystem.ui;
        DawContext& daw = *baseSystem.daw;

        if (!midi.initialized && audio.client) {
            initializeMidi(baseSystem, midi, audio, daw);
        }
        if (!midi.initialized) return;
        midi.trackCount = getTrackCount(midi);

        if (ui.active && ui.actionDelayFrames == 0 && !ui.pendingActionType.empty()) {
            if (ui.pendingActionType == "DawMidiTrack") {
                const std::string key = ui.pendingActionKey;
                int trackCount = getTrackCount(midi);
                if (key == "arm") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        int current = midi.tracks[trackIndex].armMode.load(std::memory_order_relaxed);
                        int next = (current + 1) % 3;
                        midi.tracks[trackIndex].armMode.store(next, std::memory_order_relaxed);
                    }
                } else if (key == "solo") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        bool newSolo = !midi.tracks[trackIndex].solo.load(std::memory_order_relaxed);
                        midi.tracks[trackIndex].solo.store(newSolo, std::memory_order_relaxed);
                        if (newSolo) {
                            midi.tracks[trackIndex].mute.store(false, std::memory_order_relaxed);
                        }
                    }
                } else if (key == "mute") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        bool newMute = !midi.tracks[trackIndex].mute.load(std::memory_order_relaxed);
                        midi.tracks[trackIndex].mute.store(newMute, std::memory_order_relaxed);
                        if (newMute) {
                            midi.tracks[trackIndex].solo.store(false, std::memory_order_relaxed);
                        }
                    }
                } else if (key == "output") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        int current = midi.tracks[trackIndex].outputBus.load(std::memory_order_relaxed);
                        int next = nextBusIndex(current);
                        midi.tracks[trackIndex].outputBus.store(next, std::memory_order_relaxed);
                        midi.tracks[trackIndex].outputBusL.store(next, std::memory_order_relaxed);
                        midi.tracks[trackIndex].outputBusR.store(next, std::memory_order_relaxed);
                    }
                } else if (key == "output_l") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        int current = midi.tracks[trackIndex].outputBusL.load(std::memory_order_relaxed);
                        int next = nextBusIndex(current);
                        midi.tracks[trackIndex].outputBusL.store(next, std::memory_order_relaxed);
                        if (midi.tracks[trackIndex].outputBusL.load(std::memory_order_relaxed)
                            == midi.tracks[trackIndex].outputBusR.load(std::memory_order_relaxed)) {
                            midi.tracks[trackIndex].outputBus.store(next, std::memory_order_relaxed);
                        }
                    }
                } else if (key == "output_r") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        int current = midi.tracks[trackIndex].outputBusR.load(std::memory_order_relaxed);
                        int next = nextBusIndex(current);
                        midi.tracks[trackIndex].outputBusR.store(next, std::memory_order_relaxed);
                        if (midi.tracks[trackIndex].outputBusL.load(std::memory_order_relaxed)
                            == midi.tracks[trackIndex].outputBusR.load(std::memory_order_relaxed)) {
                            midi.tracks[trackIndex].outputBus.store(next, std::memory_order_relaxed);
                        }
                    }
                } else if (key == "clear") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        midi.tracks[trackIndex].clearPending = true;
                    }
                } else if (key == "add") {
                    if (!daw.transportPlaying.load(std::memory_order_relaxed)
                        && !daw.transportRecording.load(std::memory_order_relaxed)) {
                        addTrack(baseSystem, midi, audio, daw);
                    }
                } else if (key == "remove") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex < 0 && trackCount > 0) {
                        trackIndex = trackCount - 1;
                    }
                    if (trackIndex >= 0
                        && !daw.transportPlaying.load(std::memory_order_relaxed)
                        && !daw.transportRecording.load(std::memory_order_relaxed)) {
                        removeTrackAt(baseSystem, midi, audio, daw, trackIndex);
                    }
                }
                ui.pendingActionType.clear();
                ui.pendingActionKey.clear();
                ui.pendingActionValue.clear();
            }
        }

        for (auto& track : midi.tracks) {
            if (!track.clearPending) continue;
            track.audio.clear();
            track.pendingRecord.clear();
            track.pendingNotes.clear();
            track.clips.clear();
            track.loopTakeClips.clear();
            track.waveformMin.clear();
            track.waveformMax.clear();
            track.waveformColor.clear();
            track.waveformVersion += 1;
            track.activeRecordNote = -1;
            track.activeRecordNoteStart = 0;
            track.activeLoopTakeIndex = -1;
            track.takeStackExpanded = false;
            track.nextTakeId = 1;
            track.loopTakeRangeStartSample = 0;
            track.loopTakeRangeLength = 0;
            track.recordLoopCapture = false;
            if (track.recordRing) {
                jack_ringbuffer_reset(track.recordRing);
            }
            MidiIOSystemLogic::WriteDefaultTrackFile(midi, daw, track.audio);
            track.clearPending = false;
        }
    }

    void CleanupMidiTracks(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow*) {
        if (!baseSystem.midi) return;
        MidiContext& midi = *baseSystem.midi;
        for (auto& track : midi.tracks) {
            if (track.recordRing) {
                jack_ringbuffer_free(track.recordRing);
                track.recordRing = nullptr;
            }
        }
        if (baseSystem.audio) {
            baseSystem.audio->midi = nullptr;
        }
        midi.initialized = false;
    }

    bool InsertTrackAt(BaseSystem& baseSystem, int trackIndex) {
        if (!baseSystem.midi || !baseSystem.audio || !baseSystem.daw) return false;
        return insertTrackAt(baseSystem, *baseSystem.midi, *baseSystem.audio, *baseSystem.daw, trackIndex);
    }

    bool RemoveTrackAt(BaseSystem& baseSystem, int trackIndex) {
        if (!baseSystem.midi || !baseSystem.audio || !baseSystem.daw) return false;
        return removeTrackAt(baseSystem, *baseSystem.midi, *baseSystem.audio, *baseSystem.daw, trackIndex);
    }

    bool MoveTrack(BaseSystem& baseSystem, int fromIndex, int toIndex) {
        if (!baseSystem.midi || !baseSystem.daw) return false;
        return moveTrack(baseSystem, *baseSystem.midi, *baseSystem.daw, fromIndex, toIndex);
    }
}
