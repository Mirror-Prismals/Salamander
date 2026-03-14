#pragma once

#include <algorithm>
#include <cmath>
#include <string>

namespace DawClipSystemLogic {
    void MergePendingRecords(DawContext& daw);
}
namespace DawIOSystemLogic {
    void WriteTracksIfNeeded(DawContext& daw);
    bool StartStemExport(BaseSystem& baseSystem);
    bool PromptAndSaveSession(BaseSystem& baseSystem);
    bool PromptAndLoadSession(BaseSystem& baseSystem);
    void TickStemExport(BaseSystem& baseSystem, float dt);
    void EnsureThemeState(BaseSystem& baseSystem);
}
namespace DawWaveformSystemLogic {
    void UpdateWaveformRange(DawTrack& track, size_t startSample, size_t endSample);
}
namespace DawUiSystemLogic {
    void ClampTimelineOffset(DawContext& daw);
}

namespace DawTransportSystemLogic {

    namespace {
        constexpr double kTimelineScrollSeconds = 5.0;

        enum TransportLatch {
            kTransportNone = 0,
            kTransportStop = 1,
            kTransportPlay = 2,
            kTransportRecord = 3
        };

        bool anyRecordEnabled(const DawContext& daw) {
            for (const auto& track : daw.tracks) {
                if (track.recordEnabled.load(std::memory_order_relaxed)) return true;
            }
            return false;
        }

        bool hasRingData(const DawContext& daw) {
            for (const auto& track : daw.tracks) {
                if (!track.recordRing) continue;
                if (jack_ringbuffer_read_space(track.recordRing) > 0) return true;
            }
            return false;
        }

        void drainRecordRings(DawContext& daw) {
            for (auto& track : daw.tracks) {
                if (!track.recordRing) continue;
                size_t bytes = jack_ringbuffer_read_space(track.recordRing);
                if (bytes == 0) continue;
                size_t frames = bytes / sizeof(float);
                if (frames == 0) continue;
                size_t oldSize = track.pendingRecord.size();
                track.pendingRecord.resize(oldSize + frames);
                jack_ringbuffer_read(track.recordRing,
                                     reinterpret_cast<char*>(track.pendingRecord.data() + oldSize),
                                     frames * sizeof(float));
                if (track.stereoInputPair12 && track.recordRingRight) {
                    size_t rightBytes = jack_ringbuffer_read_space(track.recordRingRight);
                    size_t rightFrames = rightBytes / sizeof(float);
                    size_t readRight = std::min(frames, rightFrames);
                    size_t oldRight = track.pendingRecordRight.size();
                    track.pendingRecordRight.resize(oldRight + frames, 0.0f);
                    if (readRight > 0) {
                        jack_ringbuffer_read(track.recordRingRight,
                                             reinterpret_cast<char*>(track.pendingRecordRight.data() + oldRight),
                                             readRight * sizeof(float));
                    }
                }
                size_t start = static_cast<size_t>(track.recordStartSample + oldSize);
                size_t end = static_cast<size_t>(track.recordStartSample + track.pendingRecord.size());
                DawWaveformSystemLogic::UpdateWaveformRange(track, start, end);
            }
        }

        void startRecording(DawContext& daw, uint64_t playhead) {
            bool loopCapture = daw.loopEnabled.load(std::memory_order_relaxed)
                && daw.loopEndSamples > daw.loopStartSamples;
            for (auto& track : daw.tracks) {
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
                track.recordLoopCapture = loopCapture;
                track.recordLoopStartSample = loopCapture ? daw.loopStartSamples : 0;
                track.recordLoopEndSample = loopCapture ? daw.loopEndSamples : 0;
                track.recordArmMode = armMode;
                track.pendingRecord.clear();
                track.pendingRecordRight.clear();
                if (track.recordRing) {
                    jack_ringbuffer_reset(track.recordRing);
                }
                if (track.recordRingRight) {
                    jack_ringbuffer_reset(track.recordRingRight);
                }
            }
        }

        void stopRecording(DawContext& daw) {
            for (auto& track : daw.tracks) {
                track.recordEnabled.store(false, std::memory_order_relaxed);
                if (track.recordingActive) {
                    track.recordingActive = false;
                }
            }
        }
    }

    void ResetTransportState(DawContext& daw) {
        uint64_t zeroSample = (daw.timelineZeroSample >= 0)
            ? static_cast<uint64_t>(daw.timelineZeroSample)
            : 0ull;
        daw.playheadSample.store(zeroSample, std::memory_order_relaxed);
        daw.transportPlaying.store(false, std::memory_order_relaxed);
        daw.transportRecording.store(false, std::memory_order_relaxed);
        daw.audioThreadIdle.store(true, std::memory_order_relaxed);
        daw.transportLatch = kTransportNone;
        daw.timelineSecondsPerScreen = 10.0;
        daw.timelineOffsetSamples = daw.timelineZeroSample;
    }

    void UpdateDawTransport(BaseSystem& baseSystem, std::vector<Entity>&, float dt, GLFWwindow*) {
        if (!baseSystem.daw || !baseSystem.ui) return;
        DawContext& daw = *baseSystem.daw;
        UIContext& ui = *baseSystem.ui;

        if (!daw.initialized) return;

        DawIOSystemLogic::EnsureThemeState(baseSystem);

        DawIOSystemLogic::TickStemExport(baseSystem, dt);
        drainRecordRings(daw);

        if (ui.active && ui.actionDelayFrames == 0 && !ui.pendingActionType.empty()) {
            if (ui.pendingActionType == "DawTransport") {
                const std::string action = ui.pendingActionKey;
                if (action == "play") {
                    if (!daw.transportRecording.load(std::memory_order_relaxed)) {
                        daw.transportPlaying.store(true, std::memory_order_relaxed);
                        daw.transportRecording.store(false, std::memory_order_relaxed);
                        daw.transportLatch = kTransportPlay;
                    }
                } else if (action == "stop") {
                    if (daw.transportLatch != kTransportStop) {
                        if (daw.transportRecording.load(std::memory_order_relaxed)) {
                            daw.recordStopPending = true;
                            daw.transportRecording.store(false, std::memory_order_relaxed);
                            stopRecording(daw);
                        }
                        daw.transportPlaying.store(false, std::memory_order_relaxed);
                        daw.transportLatch = kTransportStop;
                    }
                } else if (action == "record") {
                    if (!daw.transportRecording.load(std::memory_order_relaxed)) {
                        daw.recordStopPending = false;
                        daw.transportRecording.store(true, std::memory_order_relaxed);
                        daw.transportPlaying.store(true, std::memory_order_relaxed);
                        startRecording(daw, daw.playheadSample.load(std::memory_order_relaxed));
                        daw.transportLatch = kTransportRecord;
                    }
                } else if (action == "rewind") {
                    if (daw.transportRecording.load(std::memory_order_relaxed)) {
                        daw.recordStopPending = true;
                        daw.transportRecording.store(false, std::memory_order_relaxed);
                        stopRecording(daw);
                    }
                    daw.transportPlaying.store(false, std::memory_order_relaxed);
                    uint64_t zeroSample = (daw.timelineZeroSample >= 0)
                        ? static_cast<uint64_t>(daw.timelineZeroSample)
                        : 0ull;
                    daw.playheadSample.store(zeroSample, std::memory_order_relaxed);
                    daw.transportLatch = kTransportNone;
                }
                ui.pendingActionType.clear();
                ui.pendingActionKey.clear();
                ui.pendingActionValue.clear();
            } else if (ui.pendingActionType == "DawTimeline") {
                const std::string key = ui.pendingActionKey;
                if (key == "scroll") {
                    double deltaSec = 0.0;
                    if (!ui.pendingActionValue.empty()) {
                        try {
                            deltaSec = std::stod(ui.pendingActionValue);
                        } catch (...) {
                            deltaSec = 0.0;
                        }
                    }
                    if (std::abs(deltaSec) < 0.0001) {
                        deltaSec = kTimelineScrollSeconds;
                    }
                    int64_t deltaSamples = static_cast<int64_t>(deltaSec * daw.sampleRate);
                    daw.timelineOffsetSamples += deltaSamples;
                    DawUiSystemLogic::ClampTimelineOffset(daw);
                }
                ui.pendingActionType.clear();
                ui.pendingActionKey.clear();
                ui.pendingActionValue.clear();
            } else if (ui.pendingActionType == "DawTempo") {
                const std::string key = ui.pendingActionKey;
                double bpm = daw.bpm.load(std::memory_order_relaxed);
                if (key == "bpm_up") {
                    bpm += 1.0;
                } else if (key == "bpm_down") {
                    bpm -= 1.0;
                } else if (key == "metronome") {
                    bool enabled = daw.metronomeEnabled.load(std::memory_order_relaxed);
                    daw.metronomeEnabled.store(!enabled, std::memory_order_relaxed);
                }
                bpm = std::clamp(bpm, 40.0, 240.0);
                daw.bpm.store(bpm, std::memory_order_relaxed);
                ui.pendingActionType.clear();
                ui.pendingActionKey.clear();
                ui.pendingActionValue.clear();
            } else if (ui.pendingActionType == "DawLoop") {
                if (ui.pendingActionKey == "toggle") {
                    bool enabled = daw.loopEnabled.load(std::memory_order_relaxed);
                    daw.loopEnabled.store(!enabled, std::memory_order_relaxed);
                }
                ui.pendingActionType.clear();
                ui.pendingActionKey.clear();
                ui.pendingActionValue.clear();
            } else if (ui.pendingActionType == "DawExport") {
                if (ui.pendingActionKey == "toggle") {
                    if (!daw.exportInProgress.load(std::memory_order_relaxed)) {
                        daw.exportMenuOpen = !daw.exportMenuOpen;
                        if (!daw.exportMenuOpen) {
                            daw.exportSelectedStem = -1;
                        } else {
                            daw.settingsMenuOpen = false;
                            daw.themeCreateMode = false;
                            daw.themeEditField = -1;
                        }
                    }
                } else if (ui.pendingActionKey == "start") {
                    daw.exportMenuOpen = true;
                    daw.settingsMenuOpen = false;
                    daw.themeCreateMode = false;
                    daw.themeEditField = -1;
                    DawIOSystemLogic::StartStemExport(baseSystem);
                }
                ui.pendingActionType.clear();
                ui.pendingActionKey.clear();
                ui.pendingActionValue.clear();
            } else if (ui.pendingActionType == "DawSession") {
                if (ui.pendingActionKey == "save") {
                    DawIOSystemLogic::PromptAndSaveSession(baseSystem);
                } else if (ui.pendingActionKey == "load") {
                    DawIOSystemLogic::PromptAndLoadSession(baseSystem);
                }
                ui.pendingActionType.clear();
                ui.pendingActionKey.clear();
                ui.pendingActionValue.clear();
            } else if (ui.pendingActionType == "DawSettings") {
                if (ui.pendingActionKey == "toggle") {
                    daw.settingsMenuOpen = !daw.settingsMenuOpen;
                    if (!daw.settingsMenuOpen) {
                        daw.themeEditField = -1;
                        daw.themeCreateMode = false;
                    } else if (!daw.exportInProgress.load(std::memory_order_relaxed)) {
                        daw.exportMenuOpen = false;
                        daw.exportSelectedStem = -1;
                    }
                }
                ui.pendingActionType.clear();
                ui.pendingActionKey.clear();
                ui.pendingActionValue.clear();
            }
        }

        if (daw.recordStopPending) {
            if (!daw.transportPlaying.load(std::memory_order_relaxed) &&
                daw.audioThreadIdle.load(std::memory_order_relaxed) && !hasRingData(daw)) {
                DawClipSystemLogic::MergePendingRecords(daw);
                DawIOSystemLogic::WriteTracksIfNeeded(daw);
                daw.recordStopPending = false;
            }
        }

        if (baseSystem.rayTracedAudio) {
            bool micRequested = false;
            if (daw.transportRecording.load(std::memory_order_relaxed)) {
                for (const auto& track : daw.tracks) {
                    if (!track.recordEnabled.load(std::memory_order_relaxed)) continue;
                    if (track.useVirtualInput.load(std::memory_order_relaxed)) {
                        micRequested = true;
                        break;
                    }
                }
            }
            baseSystem.rayTracedAudio->micCaptureActive = micRequested;
        }
    }
}
