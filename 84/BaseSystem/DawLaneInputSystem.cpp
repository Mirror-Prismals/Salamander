#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace DawClipSystemLogic {
    void TrimClipsForNewClip(DawTrack& track, const DawClip& clip);
    void RebuildTrackCacheFromClips(DawContext& daw, DawTrack& track);
}
namespace DawTrackSystemLogic {
    bool MoveTrack(BaseSystem& baseSystem, int fromIndex, int toIndex);
}
namespace DawLaneTimelineSystemLogic {
    struct LaneLayout;
    bool hasDawUiWorld(const LevelContext& level);
    LaneLayout ComputeLaneLayout(const BaseSystem& baseSystem, const DawContext& daw, GLFWwindow* win);
    std::vector<int> BuildAudioLaneIndex(const DawContext& daw, int audioTrackCount);
    void ClampTimelineOffset(DawContext& daw);
    double GridSecondsForZoom(double secondsPerScreen, double secondsPerBeat);
    uint64_t MaxTimelineSamples(const DawContext& daw);
}

namespace DawLaneInputSystemLogic {
    namespace {
        constexpr float kLoopHandleWidth = 8.0f;

        bool cursorInLaneRect(const UIContext& ui, float laneLeft, float laneRight, float top, float bottom) {
            float x = static_cast<float>(ui.cursorX);
            float y = static_cast<float>(ui.cursorY);
            return x >= laneLeft && x <= laneRight && y >= top && y <= bottom;
        }

        bool cursorInRect(const UIContext& ui, float left, float right, float top, float bottom) {
            float x = static_cast<float>(ui.cursorX);
            float y = static_cast<float>(ui.cursorY);
            return x >= left && x <= right && y >= top && y <= bottom;
        }

        int laneIndexFromCursorY(float y, float startY, float laneHalfH, float rowSpan, int trackCount) {
            for (int i = 0; i < trackCount; ++i) {
                float centerY = startY + static_cast<float>(i) * rowSpan;
                if (y >= centerY - laneHalfH && y <= centerY + laneHalfH) {
                    return i;
                }
            }
            return -1;
        }

        int dropSlotFromCursorY(float y, float startY, float rowSpan, int trackCount) {
            float rel = (y - startY) / rowSpan;
            int slot = static_cast<int>(std::floor(rel + 0.5f));
            if (slot < 0) slot = 0;
            if (slot > trackCount) slot = trackCount;
            return slot;
        }
    }

    void UpdateDawLaneInput(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow* win) {
        if (!baseSystem.ui || !baseSystem.daw || !baseSystem.level || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
        if (baseSystem.midi && baseSystem.midi->pianoRollActive) return;
        if (!DawLaneTimelineSystemLogic::hasDawUiWorld(*baseSystem.level)) return;

        bool allowLaneInput = true;
        if (baseSystem.panel && baseSystem.panel->rightState > 0.01f) {
            PanelRect rightRect = (baseSystem.panel->rightRenderRect.w > 0.0f)
                ? baseSystem.panel->rightRenderRect
                : baseSystem.panel->rightRect;
            float cx = static_cast<float>(ui.cursorX);
            float cy = static_cast<float>(ui.cursorY);
            if (cx >= rightRect.x && cx <= rightRect.x + rightRect.w
                && cy >= rightRect.y && cy <= rightRect.y + rightRect.h) {
                allowLaneInput = false;
            }
        }

        DawContext& daw = *baseSystem.daw;
        const auto layout = DawLaneTimelineSystemLogic::ComputeLaneLayout(baseSystem, daw, win);
        const int audioTrackCount = layout.audioTrackCount;
        const int laneCount = layout.laneCount;
        const float laneLeft = layout.laneLeft;
        const float laneRight = layout.laneRight;
        const float laneHalfH = layout.laneHalfH;
        const float rowSpan = layout.rowSpan;
        const float startY = layout.startY;
        const float topBound = layout.topBound;
        const float laneBottomBound = layout.laneBottomBound;
        const float handleY = layout.handleY;
        const float handleHalf = layout.handleHalf;
        const float rulerTopY = layout.rulerTopY;
        const float rulerBottomY = layout.rulerBottomY;
        const float rulerLeft = layout.rulerLeft;
        const float rulerRight = layout.rulerRight;
        const float upperRulerTop = layout.upperRulerTop;
        const float upperRulerBottom = layout.upperRulerBottom;
        const double secondsPerScreen = layout.secondsPerScreen;

        bool playheadPressed = false;
        if (allowLaneInput && ui.uiLeftPressed) {
            double playheadSec = static_cast<double>(daw.playheadSample.load(std::memory_order_relaxed))
                / static_cast<double>(daw.sampleRate);
            double offsetSec = static_cast<double>(daw.timelineOffsetSamples) / static_cast<double>(daw.sampleRate);
            double tNorm = secondsPerScreen > 0.0 ? (playheadSec - offsetSec) / secondsPerScreen : 0.0;
            tNorm = std::clamp(tNorm, 0.0, 1.0);
            float playheadX = static_cast<float>(laneLeft + (laneRight - laneLeft) * tNorm);
            float left = playheadX - handleHalf;
            float right = playheadX + handleHalf;
            float top = handleY - handleHalf;
            float bottom = handleY + handleHalf;
            if (cursorInRect(ui, left, right, top, bottom)) {
                daw.playheadDragActive = true;
                daw.playheadDragOffsetX = playheadX - static_cast<float>(ui.cursorX);
                playheadPressed = true;
                ui.consumeClick = true;
            }
        }

        bool rulerPressed = false;
        if (allowLaneInput && ui.uiLeftPressed && !playheadPressed) {
            if (cursorInRect(ui, rulerLeft, rulerRight, rulerTopY, rulerBottomY)) {
                daw.rulerDragActive = true;
                daw.rulerDragStartY = ui.cursorY;
                daw.rulerDragStartSeconds = secondsPerScreen;
                rulerPressed = true;
                ui.consumeClick = true;
            }
        }

        const auto audioLaneIndex = DawLaneTimelineSystemLogic::BuildAudioLaneIndex(daw, audioTrackCount);

        if (!daw.clipDragActive && allowLaneInput && ui.uiLeftPressed && !playheadPressed && !rulerPressed) {
            double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
            double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
            if (windowSamples <= 0.0) windowSamples = 1.0;
            int hitTrack = -1;
            int hitClipIndex = -1;
            for (int t = 0; t < audioTrackCount; ++t) {
                const auto& clips = daw.tracks[static_cast<size_t>(t)].clips;
                if (clips.empty()) continue;
                int laneIndex = audioLaneIndex[static_cast<size_t>(t)];
                if (laneIndex < 0) continue;
                int displayIndex = laneIndex;
                if (daw.dragActive && daw.dragLaneType == 0 && daw.dragLaneIndex >= 0) {
                    int previewSlot = daw.dragDropIndex;
                    if (previewSlot >= 0 && laneIndex >= previewSlot) {
                        displayIndex += 1;
                    }
                } else if (daw.externalDropActive && daw.externalDropType == 0) {
                    int previewSlot = daw.externalDropIndex;
                    if (previewSlot >= 0 && laneIndex >= previewSlot) {
                        displayIndex += 1;
                    }
                }
                float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                float top = centerY - laneHalfH;
                float bottom = centerY + laneHalfH;
                if (ui.cursorY < top || ui.cursorY > bottom) continue;
                for (size_t ci = 0; ci < clips.size(); ++ci) {
                    const auto& clip = clips[ci];
                    if (clip.length == 0) continue;
                    double clipStart = static_cast<double>(clip.startSample);
                    double clipEnd = static_cast<double>(clip.startSample + clip.length);
                    if (clipEnd <= offsetSamples || clipStart >= offsetSamples + windowSamples) continue;
                    double visibleStart = std::max(clipStart, offsetSamples);
                    double visibleEnd = std::min(clipEnd, offsetSamples + windowSamples);
                    float t0 = static_cast<float>((visibleStart - offsetSamples) / windowSamples);
                    float t1 = static_cast<float>((visibleEnd - offsetSamples) / windowSamples);
                    float x0 = laneLeft + (laneRight - laneLeft) * t0;
                    float x1 = laneLeft + (laneRight - laneLeft) * t1;
                    if (ui.cursorX >= x0 && ui.cursorX <= x1) {
                        hitTrack = t;
                        hitClipIndex = static_cast<int>(ci);
                        break;
                    }
                }
                if (hitTrack >= 0) break;
            }
            if (hitTrack >= 0 && hitClipIndex >= 0) {
                double cursorT = (laneRight > laneLeft)
                    ? (static_cast<double>(ui.cursorX) - laneLeft) / (laneRight - laneLeft)
                    : 0.0;
                cursorT = std::clamp(cursorT, 0.0, 1.0);
                double cursorSample = offsetSamples + cursorT * windowSamples;
                const auto& clip = daw.tracks[static_cast<size_t>(hitTrack)].clips[static_cast<size_t>(hitClipIndex)];
                daw.clipDragActive = true;
                daw.clipDragTrack = hitTrack;
                daw.clipDragIndex = hitClipIndex;
                daw.clipDragOffsetSamples = static_cast<int64_t>(std::llround(cursorSample)) - static_cast<int64_t>(clip.startSample);
                daw.clipDragTargetTrack = hitTrack;
                daw.clipDragTargetStart = clip.startSample;
                daw.selectedClipTrack = hitTrack;
                daw.selectedClipIndex = hitClipIndex;
                if (!daw.laneOrder.empty()) {
                    for (size_t laneIdx = 0; laneIdx < daw.laneOrder.size(); ++laneIdx) {
                        const auto& entry = daw.laneOrder[laneIdx];
                        if (entry.type == 0 && entry.trackIndex == hitTrack) {
                            daw.selectedLaneIndex = static_cast<int>(laneIdx);
                            daw.selectedLaneType = entry.type;
                            daw.selectedLaneTrack = entry.trackIndex;
                            break;
                        }
                    }
                } else {
                    daw.selectedLaneIndex = hitTrack;
                    daw.selectedLaneType = 0;
                    daw.selectedLaneTrack = hitTrack;
                }
                ui.consumeClick = true;
            }
        }

        if (allowLaneInput && ui.uiLeftPressed && !playheadPressed && !rulerPressed) {
            if (cursorInRect(ui, rulerLeft, rulerRight, upperRulerTop, upperRulerBottom)) {
                double offsetSec = (daw.sampleRate > 0.0)
                    ? static_cast<double>(daw.timelineOffsetSamples) / static_cast<double>(daw.sampleRate)
                    : 0.0;
                double loopStartSec = (daw.sampleRate > 0.0)
                    ? static_cast<double>(daw.loopStartSamples) / static_cast<double>(daw.sampleRate)
                    : 0.0;
                double loopEndSec = (daw.sampleRate > 0.0)
                    ? static_cast<double>(daw.loopEndSamples) / static_cast<double>(daw.sampleRate)
                    : 0.0;
                float loopStartX = static_cast<float>(laneLeft + (laneRight - laneLeft) * ((loopStartSec - offsetSec) / secondsPerScreen));
                float loopEndX = static_cast<float>(laneLeft + (laneRight - laneLeft) * ((loopEndSec - offsetSec) / secondsPerScreen));
                if (loopEndX < loopStartX) std::swap(loopStartX, loopEndX);
                float loopLeft = std::clamp(loopStartX, rulerLeft, rulerRight);
                float loopRight = std::clamp(loopEndX, rulerLeft, rulerRight);
                float leftHandle = loopLeft - kLoopHandleWidth;
                float rightHandle = loopLeft + kLoopHandleWidth;
                float leftHandle2 = loopRight - kLoopHandleWidth;
                float rightHandle2 = loopRight + kLoopHandleWidth;
                if (cursorInRect(ui, leftHandle, rightHandle, upperRulerTop, upperRulerBottom)) {
                    daw.loopDragActive = true;
                    daw.loopDragMode = 1;
                    ui.consumeClick = true;
                } else if (cursorInRect(ui, leftHandle2, rightHandle2, upperRulerTop, upperRulerBottom)) {
                    daw.loopDragActive = true;
                    daw.loopDragMode = 2;
                    ui.consumeClick = true;
                } else if (cursorInRect(ui, loopLeft, loopRight, upperRulerTop, upperRulerBottom)) {
                    double sampleRate = (daw.sampleRate > 0.0) ? daw.sampleRate : 44100.0;
                    double cursorT = (laneRight > laneLeft)
                        ? (static_cast<double>(ui.cursorX) - laneLeft) / (laneRight - laneLeft)
                        : 0.0;
                    cursorT = std::clamp(cursorT, 0.0, 1.0);
                    double timeSec = offsetSec + cursorT * secondsPerScreen;
                    int64_t cursorSample = static_cast<int64_t>(std::llround(timeSec * sampleRate));
                    daw.loopDragActive = true;
                    daw.loopDragMode = 3;
                    daw.loopDragOffsetSamples = cursorSample - static_cast<int64_t>(daw.loopStartSamples);
                    daw.loopDragLengthSamples = (daw.loopEndSamples > daw.loopStartSamples)
                        ? (daw.loopEndSamples - daw.loopStartSamples)
                        : 0;
                    ui.consumeClick = true;
                }
            }
        }

        if (daw.clipDragActive) {
            if (!ui.uiLeftDown) {
                int srcTrack = daw.clipDragTrack;
                int srcIndex = daw.clipDragIndex;
                int dstTrack = daw.clipDragTargetTrack;
                if (srcTrack >= 0 && srcTrack < audioTrackCount
                    && dstTrack >= 0 && dstTrack < audioTrackCount) {
                    DawTrack& fromTrack = daw.tracks[static_cast<size_t>(srcTrack)];
                    if (srcIndex >= 0 && srcIndex < static_cast<int>(fromTrack.clips.size())) {
                        DawClip clip = fromTrack.clips[static_cast<size_t>(srcIndex)];
                        fromTrack.clips.erase(fromTrack.clips.begin() + srcIndex);
                        clip.startSample = daw.clipDragTargetStart;
                        DawTrack& toTrack = daw.tracks[static_cast<size_t>(dstTrack)];
                        DawClipSystemLogic::TrimClipsForNewClip(toTrack, clip);
                        toTrack.clips.push_back(clip);
                        std::sort(toTrack.clips.begin(), toTrack.clips.end(), [](const DawClip& a, const DawClip& b) {
                            if (a.startSample == b.startSample) return a.sourceOffset < b.sourceOffset;
                            return a.startSample < b.startSample;
                        });
                        DawClipSystemLogic::RebuildTrackCacheFromClips(daw, toTrack);
                        if (srcTrack != dstTrack) {
                            DawClipSystemLogic::RebuildTrackCacheFromClips(daw, fromTrack);
                        }
                    }
                }
                daw.clipDragActive = false;
                daw.clipDragTrack = -1;
                daw.clipDragIndex = -1;
                daw.clipDragTargetTrack = -1;
                daw.clipDragTargetStart = 0;
                daw.clipDragOffsetSamples = 0;
            } else {
                double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
                double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
                if (windowSamples <= 0.0) windowSamples = 1.0;
                double cursorT = (laneRight > laneLeft)
                    ? (static_cast<double>(ui.cursorX) - laneLeft) / (laneRight - laneLeft)
                    : 0.0;
                cursorT = std::clamp(cursorT, 0.0, 1.0);
                int64_t targetSample = static_cast<int64_t>(std::llround(offsetSamples + cursorT * windowSamples))
                    - daw.clipDragOffsetSamples;
                if (targetSample < 0) targetSample = 0;
                bool cmdDown = glfwGetKey(win, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS
                    || glfwGetKey(win, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
                if (!cmdDown) {
                    double bpm = daw.bpm.load(std::memory_order_relaxed);
                    if (bpm <= 0.0) bpm = 120.0;
                    double secondsPerBeat = 60.0 / bpm;
                    double gridSeconds = DawLaneTimelineSystemLogic::GridSecondsForZoom(secondsPerScreen, secondsPerBeat);
                    if (gridSeconds > 0.0) {
                        uint64_t gridStepSamples = std::max<uint64_t>(1,
                            static_cast<uint64_t>(std::llround(gridSeconds * daw.sampleRate)));
                        targetSample = static_cast<int64_t>((static_cast<uint64_t>(targetSample) / gridStepSamples) * gridStepSamples);
                    }
                }
                daw.clipDragTargetStart = static_cast<uint64_t>(targetSample);

                int dstTrack = daw.clipDragTrack;
                if (!daw.laneOrder.empty()) {
                    int laneIdx = laneIndexFromCursorY(static_cast<float>(ui.cursorY), startY, laneHalfH, rowSpan, laneCount);
                    if (laneIdx >= 0 && laneIdx < static_cast<int>(daw.laneOrder.size())) {
                        const auto& entry = daw.laneOrder[static_cast<size_t>(laneIdx)];
                        if (entry.type == 0) {
                            dstTrack = entry.trackIndex;
                        }
                    }
                } else {
                    int laneIdx = laneIndexFromCursorY(static_cast<float>(ui.cursorY), startY, laneHalfH, rowSpan, laneCount);
                    if (laneIdx >= 0) dstTrack = laneIdx;
                }
                daw.clipDragTargetTrack = dstTrack;
                ui.consumeClick = true;
            }
        }

        if (daw.loopDragActive) {
            if (!ui.uiLeftDown) {
                daw.loopDragActive = false;
                daw.loopDragMode = 0;
            } else {
                double bpm = daw.bpm.load(std::memory_order_relaxed);
                if (bpm <= 0.0) bpm = 120.0;
                double secondsPerBeat = (bpm > 0.0) ? (60.0 / bpm) : 0.5;
                double gridSeconds = DawLaneTimelineSystemLogic::GridSecondsForZoom(secondsPerScreen, secondsPerBeat);
                double cursorT = (laneRight > laneLeft)
                    ? (static_cast<double>(ui.cursorX) - laneLeft) / (laneRight - laneLeft)
                    : 0.0;
                cursorT = std::clamp(cursorT, 0.0, 1.0);
                double offsetSec = (daw.sampleRate > 0.0)
                    ? static_cast<double>(daw.timelineOffsetSamples) / static_cast<double>(daw.sampleRate)
                    : 0.0;
                double sampleRate = (daw.sampleRate > 0.0) ? daw.sampleRate : 44100.0;
                double timeSec = offsetSec + cursorT * secondsPerScreen;
                long long rawSample = std::llround(timeSec * sampleRate);
                if (rawSample < 0) rawSample = 0;
                uint64_t targetSample = static_cast<uint64_t>(rawSample);
                bool cmdDown = glfwGetKey(win, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS
                    || glfwGetKey(win, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
                uint64_t gridStepSamples = 1;
                if (!cmdDown && gridSeconds > 0.0) {
                    gridStepSamples = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(gridSeconds * sampleRate)));
                    if (gridStepSamples > 0) {
                        targetSample = static_cast<uint64_t>(std::llround(static_cast<double>(targetSample) / gridStepSamples)) * gridStepSamples;
                    }
                }
                uint64_t loopStart = daw.loopStartSamples;
                uint64_t loopEnd = daw.loopEndSamples;
                if (loopEnd <= loopStart) {
                    loopEnd = loopStart + gridStepSamples;
                }
                uint64_t minLen = std::max<uint64_t>(1, gridStepSamples);
                if (daw.loopDragMode == 1) {
                    if (targetSample + minLen > loopEnd) {
                        targetSample = loopEnd - minLen;
                    }
                    daw.loopStartSamples = targetSample;
                } else if (daw.loopDragMode == 2) {
                    if (targetSample < loopStart + minLen) {
                        targetSample = loopStart + minLen;
                    }
                    daw.loopEndSamples = targetSample;
                } else if (daw.loopDragMode == 3) {
                    int64_t proposedStart = static_cast<int64_t>(targetSample) - daw.loopDragOffsetSamples;
                    if (proposedStart < 0) proposedStart = 0;
                    uint64_t length = daw.loopDragLengthSamples;
                    if (length < minLen) length = minLen;
                    uint64_t newStart = static_cast<uint64_t>(proposedStart);
                    if (!cmdDown && gridStepSamples > 0) {
                        newStart = (newStart / gridStepSamples) * gridStepSamples;
                    }
                    daw.loopStartSamples = newStart;
                    daw.loopEndSamples = newStart + length;
                }
                uint64_t maxSamples = DawLaneTimelineSystemLogic::MaxTimelineSamples(daw);
                uint64_t windowSamples = static_cast<uint64_t>(std::max(0.0, secondsPerScreen * sampleRate));
                uint64_t maxAllowed = maxSamples + windowSamples;
                if (daw.loopStartSamples > maxAllowed) daw.loopStartSamples = maxAllowed;
                if (daw.loopEndSamples > maxAllowed) daw.loopEndSamples = maxAllowed;
            }
        }

        if (daw.rulerDragActive) {
            if (!ui.uiLeftDown) {
                daw.rulerDragActive = false;
            } else {
                double dy = ui.cursorY - daw.rulerDragStartY;
                double scale = std::exp(-dy * 0.01);
                double newSeconds = daw.rulerDragStartSeconds * scale;
                newSeconds = std::clamp(newSeconds, 2.0, 120.0);
                double cursorT = (laneRight > laneLeft)
                    ? (static_cast<double>(ui.cursorX) - laneLeft) / (laneRight - laneLeft)
                    : 0.0;
                cursorT = std::clamp(cursorT, 0.0, 1.0);
                double anchorTime = (static_cast<double>(daw.timelineOffsetSamples) / daw.sampleRate)
                    + cursorT * secondsPerScreen;
                double newOffsetSec = anchorTime - cursorT * newSeconds;
                daw.timelineSecondsPerScreen = newSeconds;
                daw.timelineOffsetSamples = static_cast<int64_t>(std::llround(newOffsetSec * daw.sampleRate));
                DawLaneTimelineSystemLogic::ClampTimelineOffset(daw);
            }
        }

        if (allowLaneInput && ui.mainScrollDelta != 0.0
            && cursorInRect(ui, rulerLeft, rulerRight, rulerTopY, rulerBottomY)) {
            double zoomFactor = (ui.mainScrollDelta > 0.0) ? 1.1 : (1.0 / 1.1);
            double newSeconds = secondsPerScreen * zoomFactor;
            newSeconds = std::clamp(newSeconds, 2.0, 120.0);
            double cursorT = (laneRight > laneLeft)
                ? (static_cast<double>(ui.cursorX) - laneLeft) / (laneRight - laneLeft)
                : 0.0;
            cursorT = std::clamp(cursorT, 0.0, 1.0);
            double anchorTime = (static_cast<double>(daw.timelineOffsetSamples) / daw.sampleRate)
                + cursorT * secondsPerScreen;
            double newOffsetSec = anchorTime - cursorT * newSeconds;
            daw.timelineSecondsPerScreen = newSeconds;
            daw.timelineOffsetSamples = static_cast<int64_t>(std::llround(newOffsetSec * daw.sampleRate));
            DawLaneTimelineSystemLogic::ClampTimelineOffset(daw);
            ui.mainScrollDelta = 0.0;
        }

        if (daw.playheadDragActive) {
            if (!ui.uiLeftDown) {
                daw.playheadDragActive = false;
            } else {
                float targetX = static_cast<float>(ui.cursorX) + daw.playheadDragOffsetX;
                float t = (laneRight > laneLeft) ? (targetX - laneLeft) / (laneRight - laneLeft) : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
                if (windowSamples < 0.0) windowSamples = 0.0;
                double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
                uint64_t newSample = static_cast<uint64_t>(std::max(0.0, offsetSamples + t * windowSamples));
                daw.playheadSample.store(newSample, std::memory_order_relaxed);
                ui.consumeClick = true;
            }
        }

        if (laneCount > 0 && allowLaneInput && ui.uiLeftPressed && !ui.consumeClick && !playheadPressed && !rulerPressed) {
            if (cursorInLaneRect(ui, laneLeft, laneRight, topBound, laneBottomBound)) {
                int laneIdx = laneIndexFromCursorY(static_cast<float>(ui.cursorY), startY, laneHalfH, rowSpan, laneCount);
                if (laneIdx >= 0) {
                    daw.selectedLaneIndex = laneIdx;
                    if (!daw.laneOrder.empty() && laneIdx < static_cast<int>(daw.laneOrder.size())) {
                        const auto& entry = daw.laneOrder[static_cast<size_t>(laneIdx)];
                        daw.selectedLaneType = entry.type;
                        daw.selectedLaneTrack = entry.trackIndex;
                        daw.dragLaneType = entry.type;
                        daw.dragLaneTrack = entry.trackIndex;
                    } else {
                        daw.selectedLaneType = 0;
                        daw.selectedLaneTrack = laneIdx;
                        daw.dragLaneType = 0;
                        daw.dragLaneTrack = laneIdx;
                    }
                    daw.dragLaneIndex = laneIdx;
                    daw.dragStartY = static_cast<float>(ui.cursorY);
                    daw.dragPending = true;
                    daw.dragActive = false;
                    ui.consumeClick = true;
                } else {
                    daw.selectedLaneIndex = -1;
                    daw.selectedLaneType = -1;
                    daw.selectedLaneTrack = -1;
                }
            } else {
                float cx = static_cast<float>(ui.cursorX);
                if (cx >= laneLeft && cx <= laneRight) {
                    daw.selectedLaneIndex = -1;
                    daw.selectedLaneType = -1;
                    daw.selectedLaneTrack = -1;
                }
            }
        }

        if (laneCount > 0 && allowLaneInput && daw.dragPending && ui.uiLeftDown) {
            float dy = std::abs(static_cast<float>(ui.cursorY) - daw.dragStartY);
            if (!daw.dragActive && dy > 4.0f) {
                daw.dragActive = true;
            }
        }

        if (laneCount > 0 && allowLaneInput && daw.dragActive) {
            if (cursorInLaneRect(ui, laneLeft, laneRight, topBound - layout.laneGap, laneBottomBound + layout.laneGap)) {
                daw.dragDropIndex = dropSlotFromCursorY(static_cast<float>(ui.cursorY), startY, rowSpan, laneCount);
            } else {
                daw.dragDropIndex = -1;
            }
        }

        if (!ui.uiLeftDown && (daw.dragPending || daw.dragActive)) {
            if (daw.dragActive && daw.dragDropIndex >= 0 && daw.dragLaneIndex >= 0) {
                int fromIndex = daw.dragLaneIndex;
                int dropSlot = daw.dragDropIndex;
                int toIndex = dropSlot;
                if (dropSlot > fromIndex) {
                    toIndex = dropSlot - 1;
                }
                if (toIndex != fromIndex) {
                    if (DawTrackSystemLogic::MoveTrack(baseSystem, fromIndex, toIndex)) {
                        daw.selectedLaneIndex = toIndex;
                    }
                }
            }
            daw.dragPending = false;
            daw.dragActive = false;
            daw.dragLaneIndex = -1;
            daw.dragLaneType = -1;
            daw.dragLaneTrack = -1;
            daw.dragDropIndex = -1;
        }
        if (!ui.uiLeftDown && !daw.dragPending && !daw.dragActive
            && daw.selectedLaneIndex < 0 && daw.selectedLaneType < 0 && daw.selectedLaneTrack < 0) {
            daw.dragDropIndex = -1;
        }
    }
}
