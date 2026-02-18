#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
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
        uint64_t addWithSaturation(uint64_t a, uint64_t b) {
            if (b > (std::numeric_limits<uint64_t>::max() - a)) {
                return std::numeric_limits<uint64_t>::max();
            }
            return a + b;
        }

        struct LoopRecordSegment {
            uint64_t linearStart = 0;
            uint64_t linearEnd = 0;
            uint64_t logicalStart = 0;
            bool loopTake = false;
        };

        void drainRecordRings(MidiContext& midi) {
            for (auto& track : midi.tracks) {
                if (!track.recordRing) continue;
                jack_ringbuffer_reset(track.recordRing);
                if (!track.pendingRecord.empty()) {
                    track.pendingRecord.clear();
                }
            }
        }

        void sortMidiClipsByStart(std::vector<MidiClip>& clips) {
            std::sort(clips.begin(), clips.end(), [](const MidiClip& a, const MidiClip& b) {
                if (a.startSample == b.startSample) return a.length < b.length;
                return a.startSample < b.startSample;
            });
        }

        int issueTakeId(MidiTrack& track) {
            if (track.nextTakeId <= 0) track.nextTakeId = 1;
            int id = track.nextTakeId;
            if (track.nextTakeId < std::numeric_limits<int>::max()) {
                track.nextTakeId += 1;
            }
            return id;
        }

        void pushTakeClip(MidiTrack& track, MidiClip clip) {
            if (clip.length == 0) return;
            if (clip.takeId < 0) {
                clip.takeId = issueTakeId(track);
            }
            track.loopTakeClips.push_back(clip);
            track.activeLoopTakeIndex = static_cast<int>(track.loopTakeClips.size()) - 1;
        }

        void trimMidiClipsForNewClip(MidiTrack& track, const MidiClip& clip) {
            if (clip.length == 0) return;
            uint64_t newStart = clip.startSample;
            uint64_t newEnd = clip.startSample + clip.length;
            std::vector<MidiClip> updated;
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
                    if (left.length > 0) updated.push_back(std::move(left));
                    if (right.length > 0) updated.push_back(std::move(right));
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
                    if (right.length > 0) updated.push_back(std::move(right));
                } else {
                    MidiClip left = existing;
                    left.length = newStart - exStart;
                    left.notes.erase(std::remove_if(left.notes.begin(), left.notes.end(),
                                                    [&](const MidiNote& note) {
                                                        return note.startSample >= left.length;
                                                    }),
                                     left.notes.end());
                    if (left.length > 0) updated.push_back(std::move(left));
                }
            }
            track.clips = std::move(updated);
        }

        void captureOverwrittenAsTakes(MidiTrack& track, const MidiClip& incoming) {
            if (incoming.length == 0) return;
            const uint64_t incomingStart = incoming.startSample;
            const uint64_t incomingEnd = incoming.startSample + incoming.length;
            for (const auto& existing : track.clips) {
                if (existing.length == 0) continue;
                const uint64_t exStart = existing.startSample;
                const uint64_t exEnd = existing.startSample + existing.length;
                if (exEnd <= incomingStart || exStart >= incomingEnd) continue;
                const uint64_t overlapStart = std::max(exStart, incomingStart);
                const uint64_t overlapEnd = std::min(exEnd, incomingEnd);
                if (overlapEnd <= overlapStart) continue;
                MidiClip take = existing;
                take.startSample = overlapStart;
                take.length = overlapEnd - overlapStart;
                std::vector<MidiNote> clipped;
                clipped.reserve(existing.notes.size());
                for (const auto& note : existing.notes) {
                    if (note.length == 0) continue;
                    const uint64_t noteAbsStart = exStart + note.startSample;
                    const uint64_t noteAbsEnd = noteAbsStart + note.length;
                    if (noteAbsEnd <= overlapStart || noteAbsStart >= overlapEnd) continue;
                    const uint64_t clippedStart = std::max(noteAbsStart, overlapStart);
                    const uint64_t clippedEnd = std::min(noteAbsEnd, overlapEnd);
                    if (clippedEnd <= clippedStart) continue;
                    MidiNote out = note;
                    out.startSample = clippedStart - overlapStart;
                    out.length = clippedEnd - clippedStart;
                    clipped.push_back(out);
                }
                take.notes = std::move(clipped);
                pushTakeClip(track, take);
            }
        }

        void insertActiveClip(MidiTrack& track, const MidiClip& clip, bool captureOverwritten) {
            if (clip.length == 0) return;
            if (captureOverwritten) {
                captureOverwrittenAsTakes(track, clip);
            }
            trimMidiClipsForNewClip(track, clip);
            track.clips.push_back(clip);
            sortMidiClipsByStart(track.clips);
        }

        uint64_t updateRecordLinearClock(MidiTrack& track, uint64_t playheadSample) {
            if (track.recordLoopCapture && track.recordLoopEndSample > track.recordLoopStartSample) {
                if (playheadSample < track.recordLastPlayheadSample) {
                    uint64_t loopLength = track.recordLoopEndSample - track.recordLoopStartSample;
                    track.recordPassOffsetSamples = addWithSaturation(track.recordPassOffsetSamples, loopLength);
                }
            }
            track.recordLastPlayheadSample = playheadSample;
            return addWithSaturation(track.recordPassOffsetSamples, playheadSample);
        }

        void buildLoopRecordSegments(uint64_t linearStart,
                                     uint64_t linearStop,
                                     uint64_t loopStart,
                                     uint64_t loopLength,
                                     std::vector<LoopRecordSegment>& outSegments) {
            outSegments.clear();
            if (linearStop <= linearStart || loopLength == 0) return;
            uint64_t cursor = linearStart;
            outSegments.reserve(static_cast<size_t>((linearStop - linearStart) / loopLength) + 2);
            while (cursor < linearStop) {
                LoopRecordSegment seg{};
                seg.linearStart = cursor;
                if (cursor < loopStart) {
                    uint64_t toLoopStart = loopStart - cursor;
                    seg.linearEnd = std::min<uint64_t>(linearStop, cursor + toLoopStart);
                    seg.logicalStart = cursor;
                    seg.loopTake = false;
                } else {
                    uint64_t loopPos = (cursor - loopStart) % loopLength;
                    uint64_t toBoundary = loopLength - loopPos;
                    seg.linearEnd = std::min<uint64_t>(linearStop, cursor + toBoundary);
                    seg.logicalStart = loopStart + loopPos;
                    seg.loopTake = true;
                }
                if (seg.linearEnd <= seg.linearStart) break;
                outSegments.push_back(seg);
                cursor = seg.linearEnd;
            }
        }

        MidiClip buildClipFromSegmentAndNotes(const LoopRecordSegment& seg,
                                              const std::vector<MidiNote>& recordedNotes) {
            MidiClip clip{};
            if (seg.linearEnd <= seg.linearStart) return clip;
            clip.startSample = seg.logicalStart;
            clip.length = seg.linearEnd - seg.linearStart;
            clip.takeId = -1;
            if (recordedNotes.empty()) return clip;
            clip.notes.reserve(recordedNotes.size());
            for (const auto& note : recordedNotes) {
                if (note.length == 0) continue;
                uint64_t noteStart = note.startSample;
                uint64_t noteEnd = addWithSaturation(note.startSample, note.length);
                if (noteEnd <= seg.linearStart || noteStart >= seg.linearEnd) continue;
                uint64_t clippedStart = std::max(noteStart, seg.linearStart);
                uint64_t clippedEnd = std::min(noteEnd, seg.linearEnd);
                if (clippedEnd <= clippedStart) continue;
                MidiNote out = note;
                out.startSample = clippedStart - seg.linearStart;
                out.length = clippedEnd - clippedStart;
                clip.notes.push_back(out);
            }
            std::sort(clip.notes.begin(), clip.notes.end(), [](const MidiNote& a, const MidiNote& b) {
                if (a.startSample == b.startSample) return a.pitch < b.pitch;
                return a.startSample < b.startSample;
            });
            return clip;
        }

        void startRecording(MidiContext& midi, const DawContext& daw, uint64_t playhead) {
            bool loopCapture = daw.loopEnabled.load(std::memory_order_relaxed)
                && daw.loopEndSamples > daw.loopStartSamples;
            for (auto& track : midi.tracks) {
                int armMode = track.armMode.load(std::memory_order_relaxed);
                bool enable = (armMode > 0);
                track.recordEnabled.store(enable, std::memory_order_relaxed);
                if (!enable) {
                    track.recordingActive = false;
                    track.recordLoopCapture = false;
                    continue;
                }
                track.recordingActive = true;
                track.recordStartSample = playhead;
                track.recordStopSample = playhead;
                track.recordLinearStartSample = playhead;
                track.recordLinearStopSample = playhead;
                track.recordPassOffsetSamples = 0;
                track.recordLastPlayheadSample = playhead;
                track.recordLoopCapture = loopCapture;
                track.recordLoopStartSample = loopCapture ? daw.loopStartSamples : 0;
                track.recordLoopEndSample = loopCapture ? daw.loopEndSamples : 0;
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
                if (!track.recordingActive) continue;

                track.recordingActive = false;
                track.recordStopSample = stopSample;
                uint64_t stopLinear = updateRecordLinearClock(track, stopSample);
                track.recordLinearStopSample = stopLinear;

                if (track.activeRecordNote >= 0) {
                    uint64_t noteEnd = stopLinear;
                    uint64_t noteStart = track.activeRecordNoteStart;
                    if (noteEnd > noteStart) {
                        MidiNote note;
                        note.pitch = track.activeRecordNote;
                        note.startSample = noteStart;
                        note.length = noteEnd - noteStart;
                        note.velocity = 1.0f;
                        track.pendingNotes.push_back(note);
                    }
                    track.activeRecordNote = -1;
                }
            }
        }

        void mergePendingRecords(MidiContext& midi) {
            for (auto& track : midi.tracks) {
                uint64_t linearStart = track.recordLinearStartSample;
                uint64_t linearStop = track.recordLinearStopSample;
                if (linearStop <= linearStart) {
                    track.pendingNotes.clear();
                    track.pendingRecord.clear();
                    track.recordLoopCapture = false;
                    continue;
                }

                std::vector<MidiNote> recordedNotes = std::move(track.pendingNotes);
                track.pendingNotes.clear();
                track.pendingRecord.clear();

                std::vector<MidiClip> regularClips;
                std::vector<MidiClip> loopTakes;

                const bool loopCapture = track.recordLoopCapture
                    && track.recordLoopEndSample > track.recordLoopStartSample;

                if (loopCapture) {
                    uint64_t loopStart = track.recordLoopStartSample;
                    uint64_t loopLength = track.recordLoopEndSample - track.recordLoopStartSample;
                    std::vector<LoopRecordSegment> segments;
                    buildLoopRecordSegments(linearStart, linearStop, loopStart, loopLength, segments);
                    for (const auto& seg : segments) {
                        MidiClip clip = buildClipFromSegmentAndNotes(seg, recordedNotes);
                        if (clip.length == 0) continue;
                        if (seg.loopTake) {
                            loopTakes.push_back(std::move(clip));
                        } else {
                            regularClips.push_back(std::move(clip));
                        }
                    }
                } else {
                    LoopRecordSegment seg{};
                    seg.linearStart = linearStart;
                    seg.linearEnd = linearStop;
                    seg.logicalStart = track.recordStartSample;
                    seg.loopTake = false;
                    MidiClip clip = buildClipFromSegmentAndNotes(seg, recordedNotes);
                    if (clip.length > 0) {
                        regularClips.push_back(std::move(clip));
                    }
                }

                for (const auto& clip : regularClips) {
                    MidiClip take = clip;
                    if (take.takeId < 0) take.takeId = issueTakeId(track);
                    pushTakeClip(track, take);
                    insertActiveClip(track, take, true);
                }

                if (!loopTakes.empty()) {
                    const uint64_t loopRangeStart = track.recordLoopStartSample;
                    const uint64_t loopRangeLength = track.recordLoopEndSample - track.recordLoopStartSample;
                    track.loopTakeRangeStartSample = loopRangeStart;
                    track.loopTakeRangeLength = loopRangeLength;
                    for (auto take : loopTakes) {
                        if (take.takeId < 0) take.takeId = issueTakeId(track);
                        pushTakeClip(track, take);
                    }
                    if (track.activeLoopTakeIndex >= 0
                        && track.activeLoopTakeIndex < static_cast<int>(track.loopTakeClips.size())) {
                        const MidiClip& activeTake = track.loopTakeClips[static_cast<size_t>(track.activeLoopTakeIndex)];
                        insertActiveClip(track, activeTake, true);
                    }
                }

                track.recordLoopCapture = false;
            }
        }

        bool hasRingData(const MidiContext& midi) {
            for (const auto& track : midi.tracks) {
                if (!track.recordRing) continue;
                if (jack_ringbuffer_read_space(track.recordRing) > 0) return true;
            }
            return false;
        }

        bool cycleTrackLoopTake(MidiContext& midi, int trackIndex, int direction) {
            if (direction == 0) return false;
            if (trackIndex < 0 || trackIndex >= static_cast<int>(midi.tracks.size())) return false;
            MidiTrack& track = midi.tracks[static_cast<size_t>(trackIndex)];
            const int takeCount = static_cast<int>(track.loopTakeClips.size());
            if (takeCount < 2) return false;
            int active = track.activeLoopTakeIndex;
            if (active < 0 || active >= takeCount) active = takeCount - 1;
            active += (direction > 0) ? 1 : -1;
            if (active < 0) active = takeCount - 1;
            if (active >= takeCount) active = 0;
            track.activeLoopTakeIndex = active;
            const MidiClip& nextTake = track.loopTakeClips[static_cast<size_t>(active)];
            insertActiveClip(track, nextTake, false);
            return true;
        }
    }

    void UpdateMidiTransport(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow* win) {
        if (!baseSystem.midi || !baseSystem.ui || !baseSystem.daw) return;
        MidiContext& midi = *baseSystem.midi;
        UIContext& ui = *baseSystem.ui;
        DawContext& daw = *baseSystem.daw;

        if (!midi.initialized) return;

        drainRecordRings(midi);

        if (midi.recordingActive) {
            uint64_t playheadSample = daw.playheadSample.load(std::memory_order_relaxed);
            for (auto& track : midi.tracks) {
                if (!track.recordingActive) continue;
                updateRecordLinearClock(track, playheadSample);
            }
        }

        if (win && ui.active && !ui.loadingActive) {
            int currentNote = midi.activeNote.load(std::memory_order_relaxed);
            float currentVelocity = midi.activeVelocity.load(std::memory_order_relaxed);
            if (currentVelocity <= 0.0f) {
                currentNote = -1;
            }
            static int s_previousLiveNote = -1;
            if (currentNote != s_previousLiveNote) {
                if (midi.recordingActive) {
                    uint64_t currentSample = daw.playheadSample.load(std::memory_order_relaxed);
                    for (auto& track : midi.tracks) {
                        if (!track.recordingActive) continue;
                        uint64_t currentLinear = addWithSaturation(track.recordPassOffsetSamples, currentSample);
                        if (track.activeRecordNote >= 0) {
                            uint64_t noteEnd = currentLinear;
                            uint64_t noteStart = track.activeRecordNoteStart;
                            if (noteEnd > noteStart) {
                                MidiNote note;
                                note.pitch = track.activeRecordNote;
                                note.startSample = noteStart;
                                note.length = noteEnd - noteStart;
                                note.velocity = 1.0f;
                                track.pendingNotes.push_back(note);
                            }
                            track.activeRecordNote = -1;
                        }
                        if (currentNote >= 0) {
                            track.activeRecordNote = currentNote;
                            track.activeRecordNoteStart = currentLinear;
                        }
                    }
                }
                s_previousLiveNote = currentNote;
            }
        }

        bool recording = daw.transportRecording.load(std::memory_order_relaxed);
        if (recording && !midi.recordingActive) {
            startRecording(midi, daw, daw.playheadSample.load(std::memory_order_relaxed));
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

    bool CycleTrackLoopTake(MidiContext& midi, int trackIndex, int direction) {
        return cycleTrackLoopTake(midi, trackIndex, direction);
    }
}
