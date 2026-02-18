#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace PianoRollResourceSystemLogic {
    struct PianoRollState;
    struct PianoRollLayout;
    struct PianoRollConfig;
    enum class ScaleType;
    PianoRollState& State();
    const PianoRollConfig& Config();
    void BuildKeyLayout(PianoRollState& state, double screenWidth, double screenHeight);
    double GetSnapSpacingSamples(const std::string& value, double beatSamples, double barSamples);
    const std::array<const char*, 12>& NoteNames();
    const std::array<const char*, 7>& ModeNames();
}

namespace PianoRollLayoutSystemLogic {
    namespace {
        void updateScaleButtonLabel(PianoRollResourceSystemLogic::PianoRollState& state) {
            if (state.scaleType == PianoRollResourceSystemLogic::ScaleType::None) {
                state.scaleButton.value = "none";
                return;
            }
            const auto& noteNames = PianoRollResourceSystemLogic::NoteNames();
            const auto& modeNames = PianoRollResourceSystemLogic::ModeNames();
            std::string name = noteNames[state.scaleRoot];
            name += modeNames[state.scaleMode % 7];
            if (state.scaleType == PianoRollResourceSystemLogic::ScaleType::Major) {
                name += " Major";
            } else if (state.scaleType == PianoRollResourceSystemLogic::ScaleType::HarmonicMinor) {
                name += " Harm";
            } else if (state.scaleType == PianoRollResourceSystemLogic::ScaleType::MelodicMinor) {
                name += " Mel";
            } else if (state.scaleType == PianoRollResourceSystemLogic::ScaleType::HungarianMinor) {
                name += " Hung";
            } else if (state.scaleType == PianoRollResourceSystemLogic::ScaleType::NeapolitanMajor) {
                name += " Neo";
            } else if (state.scaleType == PianoRollResourceSystemLogic::ScaleType::DoubleHarmonicMinor) {
                name += " Dbl";
            }
            state.scaleButton.value = name;
        }
    }

    void UpdatePianoRollLayout(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow* win) {
        PianoRollResourceSystemLogic::PianoRollState& state = PianoRollResourceSystemLogic::State();
        state.layoutReady = false;
        if (!state.active) return;
        if (!baseSystem.ui || !baseSystem.midi || !baseSystem.daw || !win) return;

        MidiContext& midi = *baseSystem.midi;
        DawContext& daw = *baseSystem.daw;

        int trackIndex = midi.pianoRollTrack;
        int clipIndex = midi.pianoRollClipIndex;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(midi.tracks.size())) return;
        if (clipIndex < 0 || clipIndex >= static_cast<int>(midi.tracks[trackIndex].clips.size())) return;
        MidiClip& clip = midi.tracks[trackIndex].clips[clipIndex];

        int windowWidth = 0, windowHeight = 0;
        glfwGetWindowSize(win, &windowWidth, &windowHeight);
        double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
        double screenHeight = windowHeight > 0 ? static_cast<double>(windowHeight) : 1080.0;

        const auto& cfg = PianoRollResourceSystemLogic::Config();
        PianoRollResourceSystemLogic::BuildKeyLayout(state, screenWidth, screenHeight);

        state.modeDrawButton.w = 44.0f;
        state.modeDrawButton.h = 36.0f;
        state.modeDrawButton.depth = 6.0f;
        state.modeDrawButton.x = 8.0f;
        state.modeDrawButton.y = 15.0f;
        state.modeDrawButton.value = "Draw";

        state.modePaintButton.w = 44.0f;
        state.modePaintButton.h = 36.0f;
        state.modePaintButton.depth = 6.0f;
        state.modePaintButton.x = state.modeDrawButton.x + state.modeDrawButton.w + 6.0f;
        state.modePaintButton.y = 15.0f;
        state.modePaintButton.value = "Paint";

        state.gridButton.w = 36.0f;
        state.gridButton.h = 36.0f;
        state.gridButton.depth = 6.0f;
        state.gridButton.x = state.modePaintButton.x + state.modePaintButton.w + 6.0f;
        state.gridButton.y = 15.0f;
        if (state.gridButton.value.empty()) state.gridButton.value = "beat";

        state.scaleButton.w = 48.0f;
        state.scaleButton.h = 36.0f;
        state.scaleButton.depth = 6.0f;
        state.scaleButton.x = state.gridButton.x + state.gridButton.w + 6.0f;
        state.scaleButton.y = 15.0f;
        if (state.scaleButton.value.empty()) state.scaleButton.value = "none";

        updateScaleButtonLabel(state);

        float viewTop = cfg.borderHeight;
        float viewBottom = static_cast<float>(screenHeight) - cfg.borderHeight;

        float contentTop = 1e9f;
        float contentBottom = -1e9f;
        for (const auto& key : state.whiteKeys) {
            contentTop = std::min(contentTop, key.y);
            contentBottom = std::max(contentBottom, key.y + key.h);
        }
        for (const auto& key : state.blackKeys) {
            contentTop = std::min(contentTop, key.y);
            contentBottom = std::max(contentBottom, key.y + key.h);
        }

        float gridContentBottom = contentBottom;
        float laneStep = std::max(6.0f, cfg.laneStep * static_cast<float>(std::clamp(state.verticalZoom, 0.5, 4.0)));
        float gridContentTop = gridContentBottom - cfg.totalRows * laneStep;
        float maxScrollY = viewTop - gridContentTop;
        float minScrollY = viewBottom - gridContentBottom;
        if (maxScrollY < minScrollY) {
            float centerScroll = 0.5f * (maxScrollY + minScrollY);
            maxScrollY = centerScroll;
            minScrollY = centerScroll;
        }

        double sampleRate = (daw.sampleRate > 0.0f) ? static_cast<double>(daw.sampleRate) : 44100.0;
        double bpm = daw.bpm.load(std::memory_order_relaxed);
        if (bpm <= 0.0) bpm = 120.0;
        double secondsPerBeat = 60.0 / bpm;
        double beatSamples = secondsPerBeat * sampleRate;
        double barSamples = beatSamples * 4.0;

        float gridLeft = cfg.leftBorderWidth + cfg.keyWidth;
        float gridRight = static_cast<float>(screenWidth);
        float gridWidth = gridRight - gridLeft;
        if (state.timelineSecondsPerScreen <= 0.0) {
            state.timelineSecondsPerScreen = (daw.timelineSecondsPerScreen > 0.0) ? daw.timelineSecondsPerScreen : 10.0;
        }
        state.timelineSecondsPerScreen = std::clamp(state.timelineSecondsPerScreen, 0.5, 120.0);
        double samplesPerScreen = state.timelineSecondsPerScreen * sampleRate;
        if (samplesPerScreen <= 0.0) samplesPerScreen = 1.0;
        double pxPerSample = gridWidth / samplesPerScreen;

        double snapSamples = PianoRollResourceSystemLogic::GetSnapSpacingSamples(state.gridButton.value, beatSamples, barSamples);
        double defaultStepSamples = beatSamples * 0.25;
        double minNoteLenSamples = std::max(1.0, snapSamples > 0.0 ? snapSamples : defaultStepSamples);
        double minSnapLenSamples = std::max(1.0, snapSamples > 0.0 ? snapSamples : defaultStepSamples);

        double clipLenSamples = static_cast<double>(clip.length);
        if (clipLenSamples < 1.0) clipLenSamples = 1.0;
        double clipStartSample = static_cast<double>(clip.startSample);
        double clipEndSample = clipStartSample + clipLenSamples;
        double trackMaxEndSample = clipEndSample;
        for (const auto& laneClip : midi.tracks[trackIndex].clips) {
            if (laneClip.length == 0) continue;
            double laneClipEnd = static_cast<double>(laneClip.startSample + laneClip.length);
            if (laneClipEnd > trackMaxEndSample) {
                trackMaxEndSample = laneClipEnd;
            }
        }
        // Keep one extra screen to the right so users can inspect/edit past the lane content.
        double timelineTotalSamples = std::max(barSamples * 16.0, trackMaxEndSample + samplesPerScreen);
        if (timelineTotalSamples < samplesPerScreen) {
            timelineTotalSamples = samplesPerScreen;
        }

        float maxScrollX = 0.0f;
        float minScrollX = gridRight - (gridLeft + static_cast<float>(timelineTotalSamples * pxPerSample));
        if (maxScrollX < minScrollX) {
            float center = 0.5f * (maxScrollX + minScrollX);
            maxScrollX = center;
            minScrollX = center;
        }

        float closeSize = 32.0f;
        float closePad = 18.0f;
        float closeLeft = static_cast<float>(screenWidth) - closePad - closeSize;
        float closeTop = closePad;

        state.layout.ready = true;
        state.layout.trackIndex = trackIndex;
        state.layout.clipIndex = clipIndex;
        state.layout.screenWidth = screenWidth;
        state.layout.screenHeight = screenHeight;
        state.layout.closeSize = closeSize;
        state.layout.closePad = closePad;
        state.layout.closeLeft = closeLeft;
        state.layout.closeTop = closeTop;
        state.layout.viewTop = viewTop;
        state.layout.viewBottom = viewBottom;
        state.layout.gridLeft = gridLeft;
        state.layout.gridRight = gridRight;
        state.layout.gridWidth = gridWidth;
        state.layout.maxScrollY = maxScrollY;
        state.layout.minScrollY = minScrollY;
        state.layout.maxScrollX = maxScrollX;
        state.layout.minScrollX = minScrollX;
        state.layout.bpm = bpm;
        state.layout.secondsPerBeat = secondsPerBeat;
        state.layout.beatSamples = beatSamples;
        state.layout.barSamples = barSamples;
        state.layout.samplesPerScreen = samplesPerScreen;
        state.layout.timelineTotalSamples = timelineTotalSamples;
        state.layout.pxPerSample = pxPerSample;
        state.layout.snapSamples = snapSamples;
        state.layout.defaultStepSamples = defaultStepSamples;
        state.layout.minNoteLenSamples = minNoteLenSamples;
        state.layout.minSnapLenSamples = minSnapLenSamples;
        state.layout.clipStartSample = clipStartSample;
        state.layout.clipEndSample = clipEndSample;
        state.layout.gridOrigin = gridContentBottom;
        state.layout.gridStep = laneStep;

        // On open, start the piano-roll viewport at the clip's global timeline start.
        if (!state.timelineViewInitialized) {
            state.scrollOffsetX = static_cast<float>(-clipStartSample * pxPerSample);
            state.timelineViewInitialized = true;
        }
        state.scrollOffsetX = std::clamp(state.scrollOffsetX, state.layout.minScrollX, state.layout.maxScrollX);

        state.layoutReady = true;
    }
}
