#pragma once

#include <algorithm>
#include <cmath>
#include <vector>
#include <jack/jack.h>
#include <jack/ringbuffer.h>

namespace MidiWaveformSystemLogic {
    void UpdateWaveformRange(MidiTrack& track, size_t startSample, size_t endSample);
}
namespace MidiIOSystemLogic {
    void WriteTracksIfNeeded(MidiContext& midi, const DawContext& daw);
}

namespace MidiTransportSystemLogic {

    namespace {
        void drainRecordRings(MidiContext& midi) {
            for (auto& track : midi.tracks) {
                if (!track.recordRing) continue;
                jack_ringbuffer_reset(track.recordRing);
                if (!track.pendingRecord.empty()) {
                    track.pendingRecord.clear();
                }
            }
        }

        void startRecording(MidiContext& midi, uint64_t playhead) {
            for (auto& track : midi.tracks) {
                int armMode = track.armMode.load(std::memory_order_relaxed);
                bool enable = (armMode > 0);
                track.recordEnabled.store(enable, std::memory_order_relaxed);
                if (!enable) {
                    track.recordingActive = false;
                    continue;
                }
                track.recordingActive = true;
                track.recordStartSample = playhead;
                track.recordStopSample = playhead;
                track.recordArmMode = armMode;
                track.pendingRecord.clear();
                track.pendingNotes.clear();
                track.activeRecordNote = -1;
                track.activeRecordNoteStart = 0;
                if (track.recordRing) {
                    jack_ringbuffer_reset(track.recordRing);
                }
            }
        }

        void stopRecording(MidiContext& midi, uint64_t stopSample) {
            for (auto& track : midi.tracks) {
                track.recordEnabled.store(false, std::memory_order_relaxed);
                if (track.recordingActive) {
                    track.recordingActive = false;
                    track.recordStopSample = stopSample;
                    if (track.activeRecordNote >= 0) {
                        uint64_t noteEnd = stopSample;
                        uint64_t noteStart = track.activeRecordNoteStart;
                        if (noteEnd > noteStart) {
                            MidiNote note;
                            note.pitch = track.activeRecordNote;
                            note.startSample = (noteStart > track.recordStartSample)
                                ? (noteStart - track.recordStartSample)
                                : 0;
                            note.length = noteEnd - noteStart;
                            note.velocity = 1.0f;
                            track.pendingNotes.push_back(note);
                        }
                        track.activeRecordNote = -1;
                    }
                }
            }
        }

        void mergePendingRecords(MidiContext& midi) {
            for (auto& track : midi.tracks) {
                uint64_t clipStart = track.recordStartSample;
                uint64_t clipEnd = track.recordStopSample;
                if (clipEnd <= clipStart) {
                    track.pendingNotes.clear();
                    track.pendingRecord.clear();
                    continue;
                }
                MidiClip clip{};
                clip.startSample = clipStart;
                clip.length = clipEnd - clipStart;
                if (!track.pendingNotes.empty()) {
                    clip.notes = std::move(track.pendingNotes);
                    for (auto& note : clip.notes) {
                        if (note.startSample >= clip.length) {
                            note.length = 0;
                        } else if (note.startSample + note.length > clip.length) {
                            note.length = clip.length - note.startSample;
                        }
                    }
                    clip.notes.erase(std::remove_if(clip.notes.begin(), clip.notes.end(),
                                                    [](const MidiNote& note) { return note.length == 0; }),
                                     clip.notes.end());
                }
                if (clip.length > 0) {
                    std::vector<MidiClip> updated;
                    uint64_t newStart = clip.startSample;
                    uint64_t newEnd = clip.startSample + clip.length;
                    updated.reserve(track.clips.size() + 1);
                    for (const auto& existing : track.clips) {
                        if (existing.length == 0) continue;
                        uint64_t exStart = existing.startSample;
                        uint64_t exEnd = existing.startSample + existing.length;
                        if (exEnd <= newStart || exStart >= newEnd) {
                            updated.push_back(existing);
                            continue;
                        }
                        if (newStart <= exStart && newEnd >= exEnd) {
                            continue;
                        }
                        if (newStart > exStart && newEnd < exEnd) {
                            MidiClip left = existing;
                            left.length = newStart - exStart;
                            MidiClip right = existing;
                            right.startSample = newEnd;
                            right.length = exEnd - newEnd;
                            left.notes.erase(std::remove_if(left.notes.begin(), left.notes.end(),
                                                           [&](const MidiNote& note) {
                                                               return note.startSample >= left.length;
                                                           }),
                                             left.notes.end());
                            right.notes.erase(std::remove_if(right.notes.begin(), right.notes.end(),
                                                            [&](const MidiNote& note) {
                                                                return note.startSample < right.startSample
                                                                    || note.startSample >= right.startSample + right.length;
                                                            }),
                                              right.notes.end());
                            for (auto& note : right.notes) {
                                note.startSample -= right.startSample;
                            }
                            if (left.length > 0) updated.push_back(left);
                            if (right.length > 0) updated.push_back(right);
                        } else if (newStart <= exStart) {
                            MidiClip right = existing;
                            right.startSample = newEnd;
                            right.length = exEnd - newEnd;
                            right.notes.erase(std::remove_if(right.notes.begin(), right.notes.end(),
                                                            [&](const MidiNote& note) {
                                                                return note.startSample < right.startSample
                                                                    || note.startSample >= right.startSample + right.length;
                                                            }),
                                              right.notes.end());
                            for (auto& note : right.notes) {
                                note.startSample -= right.startSample;
                            }
                            if (right.length > 0) updated.push_back(right);
                        } else {
                            MidiClip left = existing;
                            left.length = newStart - exStart;
                            left.notes.erase(std::remove_if(left.notes.begin(), left.notes.end(),
                                                           [&](const MidiNote& note) {
                                                               return note.startSample >= left.length;
                                                           }),
                                             left.notes.end());
                            if (left.length > 0) updated.push_back(left);
                        }
                    }
                    track.clips = std::move(updated);
                    track.clips.push_back(std::move(clip));
                    std::sort(track.clips.begin(), track.clips.end(), [](const MidiClip& a, const MidiClip& b) {
                        return a.startSample < b.startSample;
                    });
                }
                track.pendingRecord.clear();
            }
        }

        bool hasRingData(const MidiContext& midi) {
            for (const auto& track : midi.tracks) {
                if (!track.recordRing) continue;
                if (jack_ringbuffer_read_space(track.recordRing) > 0) return true;
            }
            return false;
        }
    }

    void UpdateMidiTransport(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow* win) {
        if (!baseSystem.midi || !baseSystem.ui || !baseSystem.daw) return;
        MidiContext& midi = *baseSystem.midi;
        UIContext& ui = *baseSystem.ui;
        DawContext& daw = *baseSystem.daw;

        if (!midi.initialized) return;

        drainRecordRings(midi);

        if (win && ui.active && !ui.loadingActive) {
            int newNote = -1;
            if (glfwGetKey(win, GLFW_KEY_1) == GLFW_PRESS) newNote = 60;
            else if (glfwGetKey(win, GLFW_KEY_2) == GLFW_PRESS) newNote = 62;
            else if (glfwGetKey(win, GLFW_KEY_3) == GLFW_PRESS) newNote = 64;
            else if (glfwGetKey(win, GLFW_KEY_4) == GLFW_PRESS) newNote = 65;
            else if (glfwGetKey(win, GLFW_KEY_5) == GLFW_PRESS) newNote = 67;
            else if (glfwGetKey(win, GLFW_KEY_6) == GLFW_PRESS) newNote = 69;
            else if (glfwGetKey(win, GLFW_KEY_7) == GLFW_PRESS) newNote = 71;
            else if (glfwGetKey(win, GLFW_KEY_8) == GLFW_PRESS) newNote = 72;
            int prevNote = midi.activeNote.load(std::memory_order_relaxed);
            if (newNote != prevNote) {
                if (midi.recordingActive) {
                    uint64_t currentSample = daw.playheadSample.load(std::memory_order_relaxed);
                    for (auto& track : midi.tracks) {
                        if (!track.recordingActive) continue;
                        if (track.activeRecordNote >= 0) {
                            uint64_t noteEnd = currentSample;
                            uint64_t noteStart = track.activeRecordNoteStart;
                            if (noteEnd > noteStart) {
                                MidiNote note;
                                note.pitch = track.activeRecordNote;
                                note.startSample = (noteStart > track.recordStartSample)
                                    ? (noteStart - track.recordStartSample)
                                    : 0;
                                note.length = noteEnd - noteStart;
                                note.velocity = 1.0f;
                                track.pendingNotes.push_back(note);
                            }
                            track.activeRecordNote = -1;
                        }
                        if (newNote >= 0) {
                            track.activeRecordNote = newNote;
                            track.activeRecordNoteStart = currentSample;
                        }
                    }
                }
                midi.activeNote.store(newNote, std::memory_order_relaxed);
                midi.activeVelocity.store(newNote >= 0 ? 0.8f : 0.0f, std::memory_order_relaxed);
            }
        }

        bool recording = daw.transportRecording.load(std::memory_order_relaxed);
        if (recording && !midi.recordingActive) {
            startRecording(midi, daw.playheadSample.load(std::memory_order_relaxed));
            midi.recordingActive = true;
        } else if (!recording && midi.recordingActive) {
            stopRecording(midi, daw.playheadSample.load(std::memory_order_relaxed));
            midi.recordStopPending = true;
            midi.recordingActive = false;
        }

        if (midi.recordStopPending) {
            if (!daw.transportPlaying.load(std::memory_order_relaxed) &&
                daw.audioThreadIdle.load(std::memory_order_relaxed) && !hasRingData(midi)) {
                mergePendingRecords(midi);
                MidiIOSystemLogic::WriteTracksIfNeeded(midi, daw);
                midi.recordStopPending = false;
            }
        }
    }
}
