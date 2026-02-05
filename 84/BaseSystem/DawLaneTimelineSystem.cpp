#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace DawLaneTimelineSystemLogic {
    namespace {
        constexpr float kLaneLeftMargin = 40.0f;
        constexpr float kLaneRightMargin = 40.0f;
        constexpr float kPlayheadHandleSize = 12.0f;
        constexpr float kPlayheadHandleYOffset = 14.0f;
        constexpr float kRulerHeight = 13.0f;
        constexpr float kRulerInset = 10.0f;
        constexpr float kRulerSideInset = -15.0f;
        constexpr float kRulerLowerOffset = 0.0f;
        constexpr float kRulerGap = 6.0f;
    }

    struct UiVertex { glm::vec2 pos; glm::vec3 color; };

    struct LaneLayout {
        double screenWidth = 1920.0;
        double screenHeight = 1080.0;
        float laneLeft = kLaneLeftMargin;
        float laneRight = 1920.0f - kLaneRightMargin;
        float laneHeight = 60.0f;
        float laneGap = 12.0f;
        float laneHalfH = 30.0f;
        float rowSpan = 72.0f;
        float scrollY = 0.0f;
        float startY = 100.0f;
        float topBound = 0.0f;
        float laneBottomBound = 0.0f;
        float visualBottomBound = 0.0f;
        float handleY = 0.0f;
        float handleHalf = kPlayheadHandleSize * 0.5f;
        float rulerTopY = 0.0f;
        float rulerBottomY = 0.0f;
        float upperRulerTop = 0.0f;
        float upperRulerBottom = 0.0f;
        float rulerLeft = 0.0f;
        float rulerRight = 0.0f;
        int audioTrackCount = 0;
        int laneCount = 0;
        double secondsPerScreen = 10.0;
    };

    bool hasDawUiWorld(const LevelContext& level) {
        for (const auto& world : level.worlds) {
            if (world.name == "DAWScreenWorld") return true;
            if (world.name == "TrackRowWorld") return true;
            if (world.name.rfind("TrackRowWorld_", 0) == 0) return true;
        }
        return false;
    }

    LaneLayout ComputeLaneLayout(const BaseSystem& baseSystem, const DawContext& daw, GLFWwindow* win) {
        LaneLayout layout;
        if (win) {
            int windowWidth = 0, windowHeight = 0;
            glfwGetWindowSize(win, &windowWidth, &windowHeight);
            if (windowWidth > 0) layout.screenWidth = static_cast<double>(windowWidth);
            if (windowHeight > 0) layout.screenHeight = static_cast<double>(windowHeight);
        }
        layout.audioTrackCount = static_cast<int>(daw.tracks.size());
        layout.laneCount = static_cast<int>(daw.laneOrder.size());
        if (layout.laneCount == 0) {
            layout.laneCount = layout.audioTrackCount;
        }
        layout.laneHeight = 60.0f;
        layout.laneGap = 12.0f;
        layout.laneHalfH = layout.laneHeight * 0.5f;
        layout.rowSpan = layout.laneHeight + layout.laneGap;
        layout.laneLeft = kLaneLeftMargin;
        layout.laneRight = static_cast<float>(layout.screenWidth) - kLaneRightMargin;
        if (layout.laneRight < layout.laneLeft + 200.0f) {
            layout.laneRight = layout.laneLeft + 200.0f;
        }
        if (baseSystem.uiStamp) {
            layout.scrollY = baseSystem.uiStamp->scrollY;
        }
        layout.startY = 100.0f + layout.scrollY;
        layout.secondsPerScreen = (daw.timelineSecondsPerScreen > 0.0) ? daw.timelineSecondsPerScreen : 10.0;
        layout.topBound = layout.startY - layout.laneHalfH;
        layout.laneBottomBound = (layout.laneCount > 0)
            ? (layout.startY + (layout.laneCount - 1) * layout.rowSpan + layout.laneHalfH)
            : (layout.topBound + 1.0f);
        if (layout.laneBottomBound < layout.topBound + 1.0f) {
            layout.laneBottomBound = layout.topBound + 1.0f;
        }
        layout.visualBottomBound = std::max(layout.laneBottomBound, static_cast<float>(layout.screenHeight) - 40.0f);
        layout.handleY = layout.topBound - kPlayheadHandleYOffset + kRulerLowerOffset;
        layout.handleHalf = kPlayheadHandleSize * 0.5f;
        layout.rulerTopY = layout.startY - layout.laneHalfH - (kRulerHeight + kRulerInset) + kRulerLowerOffset;
        layout.rulerBottomY = layout.rulerTopY + kRulerHeight;
        if (layout.rulerTopY < 0.0f) {
            float shift = -layout.rulerTopY;
            layout.rulerTopY += shift;
            layout.rulerBottomY += shift;
        }
        layout.rulerLeft = layout.laneLeft + kRulerSideInset;
        layout.rulerRight = layout.laneRight - kRulerSideInset;
        if (layout.rulerRight < layout.rulerLeft + 1.0f) {
            layout.rulerRight = layout.rulerLeft + 1.0f;
        }
        layout.upperRulerBottom = layout.rulerTopY - kRulerGap;
        layout.upperRulerTop = layout.upperRulerBottom - kRulerHeight;
        return layout;
    }

    std::vector<int> BuildAudioLaneIndex(const DawContext& daw, int audioTrackCount) {
        std::vector<int> audioLaneIndex(static_cast<size_t>(audioTrackCount), -1);
        if (!daw.laneOrder.empty()) {
            for (size_t laneIdx = 0; laneIdx < daw.laneOrder.size(); ++laneIdx) {
                const auto& entry = daw.laneOrder[laneIdx];
                if (entry.type == 0 && entry.trackIndex >= 0 && entry.trackIndex < audioTrackCount) {
                    audioLaneIndex[static_cast<size_t>(entry.trackIndex)] = static_cast<int>(laneIdx);
                }
            }
        } else {
            for (int i = 0; i < audioTrackCount; ++i) {
                audioLaneIndex[static_cast<size_t>(i)] = i;
            }
        }
        return audioLaneIndex;
    }

    uint64_t MaxTimelineSamples(const DawContext& daw) {
        uint64_t maxSamples = daw.playheadSample.load(std::memory_order_relaxed);
        for (const auto& track : daw.tracks) {
            maxSamples = std::max<uint64_t>(maxSamples, static_cast<uint64_t>(track.audio.size()));
        }
        return maxSamples;
    }

    void ClampTimelineOffset(DawContext& daw) {
        double secondsPerScreen = (daw.timelineSecondsPerScreen > 0.0) ? daw.timelineSecondsPerScreen : 10.0;
        int64_t windowSamples = static_cast<int64_t>(secondsPerScreen * daw.sampleRate);
        if (windowSamples < 0) windowSamples = 0;
        uint64_t maxSamples = MaxTimelineSamples(daw);
        int64_t maxOffset = (maxSamples > static_cast<uint64_t>(windowSamples))
            ? static_cast<int64_t>(maxSamples - static_cast<uint64_t>(windowSamples))
            : 0;
        if (daw.timelineOffsetSamples < 0) daw.timelineOffsetSamples = 0;
        if (daw.timelineOffsetSamples > maxOffset) daw.timelineOffsetSamples = maxOffset;
    }

    double GridSecondsForZoom(double secondsPerScreen, double secondsPerBeat) {
        if (secondsPerBeat <= 0.0) return 0.5;
        if (secondsPerScreen > 64.0) {
            return secondsPerBeat * 4.0; // bars
        }
        if (secondsPerScreen > 32.0) {
            return secondsPerBeat * 2.0; // half-bar
        }
        if (secondsPerScreen > 16.0) {
            return secondsPerBeat; // beats
        }
        if (secondsPerScreen > 8.0) {
            return secondsPerBeat * 0.5; // half-beat
        }
        if (secondsPerScreen > 4.0) {
            return secondsPerBeat * 0.25; // quarter-beat
        }
        return secondsPerBeat * 0.125; // eighth-beat
    }

    void UpdateDawLaneTimeline(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow* win) {
        if (!baseSystem.daw || !win) return;
        DawContext& daw = *baseSystem.daw;
        if (daw.timelineSecondsPerScreen <= 0.0) {
            daw.timelineSecondsPerScreen = 10.0;
        }
        ClampTimelineOffset(daw);
    }
}
