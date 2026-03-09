#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <limits>
#include <vector>

#include "BaseSystem/Vst3Host.h"

namespace DawLaneTimelineSystemLogic {
    struct LaneLayout;
    bool hasDawUiWorld(const LevelContext& level);
    LaneLayout ComputeLaneLayout(const BaseSystem& baseSystem, const DawContext& daw, GLFWwindow* win);
    double GridSecondsForZoom(double secondsPerScreen, double secondsPerBeat);
}
namespace DawTimelineRebaseLogic { void ShiftTimelineRight(BaseSystem& baseSystem, uint64_t shiftSamples); }

namespace AutomationLaneSystemLogic {
    namespace {
        constexpr float kLaneAlpha = 0.85f;
        constexpr float kTrackHandleSize = 60.0f;
        constexpr float kTrackHandleInset = 12.0f;
        constexpr float kClipHorizontalPad = 2.0f;
        constexpr float kClipVerticalInset = 0.0f;
        constexpr float kClipMinHeight = 2.0f;
        constexpr float kClipLipMinHeight = 6.0f;
        constexpr float kClipLipMaxHeight = 12.0f;
        constexpr float kLineThickness = 2.0f;
        constexpr float kLineHitTolerance = 14.0f;
        constexpr float kPointRadius = 4.0f;
        constexpr float kPointHitRadius = 9.0f;
        constexpr double kDoubleClickSeconds = 0.45;

        struct UiVertex { glm::vec2 pos; glm::vec3 color; };

        static std::vector<UiVertex> g_vertices;
        static bool g_pointDragActive = false;
        static int g_pointDragTrack = -1;
        static int g_pointDragClip = -1;
        static int g_pointDragPoint = -1;
        static uint64_t g_pointDragMinOffset = 0;
        static uint64_t g_pointDragMaxOffset = 0;
        static bool g_rightMouseWasDown = false;
        static double g_lastLineClickTime = -1.0;
        static int g_lastLineClickTrack = -1;
        static int g_lastLineClickClip = -1;

        struct PointHit {
            bool valid = false;
            int track = -1;
            int clip = -1;
            int point = -1;
            float dist = FLT_MAX;
            int laneIndex = -1;
            float bodyTop = 0.0f;
            float bodyBottom = 0.0f;
        };

        struct LineHit {
            bool valid = false;
            int track = -1;
            int clip = -1;
            int laneIndex = -1;
            uint64_t localSample = 0;
            float y = 0.0f;
            float bodyTop = 0.0f;
            float bodyBottom = 0.0f;
        };

        glm::vec2 pixelToNDC(const glm::vec2& pixel, double width, double height) {
            float ndcX = static_cast<float>((pixel.x / width) * 2.0 - 1.0);
            float ndcY = static_cast<float>(1.0 - (pixel.y / height) * 2.0);
            return {ndcX, ndcY};
        }

        void pushQuad(std::vector<UiVertex>& verts,
                      const glm::vec2& a,
                      const glm::vec2& b,
                      const glm::vec2& c,
                      const glm::vec2& d,
                      const glm::vec3& color) {
            verts.push_back({a, color});
            verts.push_back({b, color});
            verts.push_back({c, color});
            verts.push_back({a, color});
            verts.push_back({c, color});
            verts.push_back({d, color});
        }

        void pushLine(std::vector<UiVertex>& verts,
                      const glm::vec2& p0,
                      const glm::vec2& p1,
                      float thickness,
                      const glm::vec3& color,
                      double width,
                      double height) {
            glm::vec2 d = p1 - p0;
            float len = std::sqrt(d.x * d.x + d.y * d.y);
            if (len <= 0.0001f) return;
            d /= len;
            glm::vec2 n(-d.y, d.x);
            glm::vec2 off = n * (thickness * 0.5f);
            pushQuad(verts,
                     pixelToNDC(p0 - off, width, height),
                     pixelToNDC(p0 + off, width, height),
                     pixelToNDC(p1 + off, width, height),
                     pixelToNDC(p1 - off, width, height),
                     color);
        }

        float clamp01(float v) {
            return std::clamp(v, 0.0f, 1.0f);
        }

        void sortAndClampPoints(AutomationClip& clip) {
            for (auto& point : clip.points) {
                if (point.offsetSample > clip.length) point.offsetSample = clip.length;
                point.value = clamp01(point.value);
            }
            std::sort(clip.points.begin(), clip.points.end(), [](const AutomationPoint& a, const AutomationPoint& b) {
                if (a.offsetSample == b.offsetSample) return a.value < b.value;
                return a.offsetSample < b.offsetSample;
            });
        }

        float valueFromY(float y, float bodyTop, float bodyBottom) {
            float h = std::max(1.0f, bodyBottom - bodyTop);
            float v = (bodyBottom - y) / h;
            return clamp01(v);
        }

        float yFromValue(float value, float bodyTop, float bodyBottom) {
            float h = std::max(1.0f, bodyBottom - bodyTop);
            return bodyBottom - clamp01(value) * h;
        }

        float evaluateClipValue(const AutomationClip& clip, uint64_t localSample) {
            if (clip.points.empty()) return 0.5f;
            if (clip.points.size() == 1) return clamp01(clip.points.front().value);
            if (localSample <= clip.points.front().offsetSample) {
                return clamp01(clip.points.front().value);
            }
            if (localSample >= clip.points.back().offsetSample) {
                return clamp01(clip.points.back().value);
            }
            for (size_t i = 0; i + 1 < clip.points.size(); ++i) {
                const AutomationPoint& a = clip.points[i];
                const AutomationPoint& b = clip.points[i + 1];
                if (localSample < a.offsetSample || localSample > b.offsetSample) continue;
                if (b.offsetSample <= a.offsetSample) return clamp01(a.value);
                float t = static_cast<float>(localSample - a.offsetSample)
                        / static_cast<float>(b.offsetSample - a.offsetSample);
                return clamp01(a.value + (b.value - a.value) * t);
            }
            return clamp01(clip.points.back().value);
        }

        void computeClipRect(float centerY,
                             float laneHalfH,
                             float& top,
                             float& bottom,
                             float& lipBottom) {
            top = centerY - laneHalfH + kClipVerticalInset;
            bottom = centerY + laneHalfH - kClipVerticalInset;
            if (bottom < top + kClipMinHeight) {
                float mid = (top + bottom) * 0.5f;
                top = mid - (kClipMinHeight * 0.5f);
                bottom = mid + (kClipMinHeight * 0.5f);
            }
            float lipHeight = std::clamp((bottom - top) * 0.18f, kClipLipMinHeight, kClipLipMaxHeight);
            lipBottom = std::min(bottom, top + lipHeight);
        }

        bool isCommandDown(GLFWwindow* win) {
            if (!win) return false;
            return glfwGetKey(win, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS
                || glfwGetKey(win, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
        }

        uint64_t gridStepSamples(const DawContext& daw, double secondsPerScreen) {
            double bpm = daw.bpm.load(std::memory_order_relaxed);
            if (bpm <= 0.0) bpm = 120.0;
            double secondsPerBeat = 60.0 / bpm;
            double gridSeconds = DawLaneTimelineSystemLogic::GridSecondsForZoom(secondsPerScreen, secondsPerBeat);
            if (gridSeconds <= 0.0) return 1;
            double sampleRate = (daw.sampleRate > 0.0) ? daw.sampleRate : 44100.0;
            return std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(gridSeconds * sampleRate)));
        }

        uint64_t computeRebaseShiftSamples(const DawContext& daw, int64_t negativeSample) {
            if (negativeSample >= 0) return 0;
            double sampleRate = (daw.sampleRate > 0.0) ? daw.sampleRate : 44100.0;
            double bpm = daw.bpm.load(std::memory_order_relaxed);
            if (bpm <= 0.0) bpm = 120.0;
            uint64_t barSamples = std::max<uint64_t>(1,
                static_cast<uint64_t>(std::llround((60.0 / bpm) * 4.0 * sampleRate)));
            uint64_t need = static_cast<uint64_t>(-negativeSample) + barSamples * 2ull;
            uint64_t shift = ((need + barSamples - 1ull) / barSamples) * barSamples;
            if (shift == 0) shift = barSamples;
            return shift;
        }

        uint64_t sampleFromCursorX(BaseSystem& baseSystem,
                                   DawContext& daw,
                                   float laneLeft,
                                   float laneRight,
                                   double secondsPerScreen,
                                   double cursorX,
                                   bool snap) {
            double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
            double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
            if (windowSamples <= 0.0) windowSamples = 1.0;
            double t = (laneRight > laneLeft)
                ? (cursorX - laneLeft) / static_cast<double>(laneRight - laneLeft)
                : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            int64_t sample = static_cast<int64_t>(std::llround(offsetSamples + t * windowSamples));
            if (sample < 0) {
                uint64_t shiftSamples = computeRebaseShiftSamples(daw, sample);
                DawTimelineRebaseLogic::ShiftTimelineRight(baseSystem, shiftSamples);
                sample += static_cast<int64_t>(shiftSamples);
            }
            if (snap) {
                uint64_t step = gridStepSamples(daw, secondsPerScreen);
                if (step > 0) {
                    return (static_cast<uint64_t>(sample) / step) * step;
                }
            }
            return static_cast<uint64_t>(sample);
        }

        float sampleToX(uint64_t sample,
                        double offsetSamples,
                        double windowSamples,
                        float laneLeft,
                        float laneRight) {
            if (windowSamples <= 0.0) return laneLeft;
            float t = static_cast<float>((static_cast<double>(sample) - offsetSamples) / windowSamples);
            return laneLeft + (laneRight - laneLeft) * t;
        }

        int parseAutomationTrackFromLane(const DawContext& daw,
                                         int laneIndex,
                                         int audioTrackCount,
                                         int midiTrackCount,
                                         int automationTrackCount) {
            if (laneIndex < 0) return -1;
            if (!daw.laneOrder.empty()) {
                if (laneIndex >= static_cast<int>(daw.laneOrder.size())) return -1;
                const auto& entry = daw.laneOrder[static_cast<size_t>(laneIndex)];
                if (entry.type != 2) return -1;
                if (entry.trackIndex < 0 || entry.trackIndex >= automationTrackCount) return -1;
                return entry.trackIndex;
            }
            int idx = laneIndex - audioTrackCount - midiTrackCount;
            if (idx < 0 || idx >= automationTrackCount) return -1;
            return idx;
        }

        std::vector<int> buildAutomationLaneIndex(const DawContext& daw,
                                                  int automationTrackCount,
                                                  int audioTrackCount,
                                                  int midiTrackCount) {
            std::vector<int> out(static_cast<size_t>(std::max(0, automationTrackCount)), -1);
            if (automationTrackCount <= 0) return out;
            if (!daw.laneOrder.empty()) {
                for (size_t laneIdx = 0; laneIdx < daw.laneOrder.size(); ++laneIdx) {
                    const auto& entry = daw.laneOrder[laneIdx];
                    if (entry.type == 2
                        && entry.trackIndex >= 0
                        && entry.trackIndex < automationTrackCount) {
                        out[static_cast<size_t>(entry.trackIndex)] = static_cast<int>(laneIdx);
                    }
                }
                return out;
            }
            int base = audioTrackCount + midiTrackCount;
            for (int i = 0; i < automationTrackCount; ++i) {
                out[static_cast<size_t>(i)] = base + i;
            }
            return out;
        }

        bool isCursorOverOpenPanel(const BaseSystem& baseSystem, const UIContext& ui) {
            if (!baseSystem.panel) return false;
            const PanelContext& panel = *baseSystem.panel;
            const float x = static_cast<float>(ui.cursorX);
            const float y = static_cast<float>(ui.cursorY);
            auto pickRect = [](const PanelRect& renderRect, const PanelRect& fallbackRect) -> const PanelRect& {
                if (renderRect.w > 0.0f && renderRect.h > 0.0f) return renderRect;
                return fallbackRect;
            };
            auto overRect = [&](const PanelRect& rect) {
                if (rect.w <= 0.0f || rect.h <= 0.0f) return false;
                return x >= rect.x && x <= rect.x + rect.w
                    && y >= rect.y && y <= rect.y + rect.h;
            };
            if (panel.topState > 0.01f && overRect(pickRect(panel.topRenderRect, panel.topRect))) return true;
            if (panel.bottomState > 0.01f && overRect(pickRect(panel.bottomRenderRect, panel.bottomRect))) return true;
            if (panel.leftState > 0.01f && overRect(pickRect(panel.leftRenderRect, panel.leftRect))) return true;
            if (panel.rightState > 0.01f && overRect(pickRect(panel.rightRenderRect, panel.rightRect))) return true;
            return false;
        }

        Vst3Plugin* resolveTargetPlugin(BaseSystem& baseSystem, const AutomationTrack& track) {
            if (!baseSystem.vst3) return nullptr;
            Vst3Context& vst3 = *baseSystem.vst3;
            if (track.targetLaneType == 0) {
                int audioTrack = track.targetLaneTrack;
                if (audioTrack < 0 || audioTrack >= static_cast<int>(vst3.audioTracks.size())) return nullptr;
                auto& fx = vst3.audioTracks[static_cast<size_t>(audioTrack)].effects;
                if (fx.empty()) return nullptr;
                int slot = std::clamp(track.targetDeviceSlot, 0, static_cast<int>(fx.size()) - 1);
                return fx[static_cast<size_t>(slot)];
            }
            if (track.targetLaneType == 1) {
                int midiTrack = track.targetLaneTrack;
                if (midiTrack < 0 || midiTrack >= static_cast<int>(vst3.midiTracks.size())) return nullptr;
                std::vector<Vst3Plugin*> devices;
                if (midiTrack >= 0 && midiTrack < static_cast<int>(vst3.midiInstruments.size())) {
                    Vst3Plugin* inst = vst3.midiInstruments[static_cast<size_t>(midiTrack)];
                    if (inst) devices.push_back(inst);
                }
                auto& fx = vst3.midiTracks[static_cast<size_t>(midiTrack)].effects;
                for (Vst3Plugin* plugin : fx) {
                    if (plugin) devices.push_back(plugin);
                }
                if (devices.empty()) return nullptr;
                int slot = std::clamp(track.targetDeviceSlot, 0, static_cast<int>(devices.size()) - 1);
                return devices[static_cast<size_t>(slot)];
            }
            return nullptr;
        }

        void applyAutomationToTargets(BaseSystem& baseSystem, DawContext& daw) {
            if (!baseSystem.vst3) return;
            uint64_t playhead = daw.playheadSample.load(std::memory_order_relaxed);
            for (auto& track : daw.automationTracks) {
                if (track.targetParameterId < 0) continue;
                Vst3Plugin* plugin = resolveTargetPlugin(baseSystem, track);
                if (!plugin || !plugin->controller) continue;
                const AutomationClip* activeClip = nullptr;
                for (const auto& clip : track.clips) {
                    if (clip.length == 0) continue;
                    if (playhead < clip.startSample) continue;
                    if (playhead > clip.startSample + clip.length) continue;
                    activeClip = &clip;
                    break;
                }
                if (!activeClip) continue;
                uint64_t local = (playhead > activeClip->startSample)
                    ? (playhead - activeClip->startSample)
                    : 0;
                if (local > activeClip->length) local = activeClip->length;
                float value = evaluateClipValue(*activeClip, local);
                value = clamp01(value);
                plugin->controller->setParamNormalized(
                    static_cast<Steinberg::Vst::ParamID>(track.targetParameterId),
                    value);
            }
        }
    }

    void UpdateAutomationLane(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow* win) {
        if (!baseSystem.ui || !baseSystem.daw || !baseSystem.renderer || !baseSystem.world || !baseSystem.level || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
        if (baseSystem.midi && baseSystem.midi->pianoRollActive) return;
        if (!DawLaneTimelineSystemLogic::hasDawUiWorld(*baseSystem.level)) return;

        DawContext& daw = *baseSystem.daw;
        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;

        if (!renderer.uiColorShader) {
            renderer.uiColorShader = std::make_unique<Shader>(world.shaders["UI_COLOR_VERTEX_SHADER"].c_str(),
                                                              world.shaders["UI_COLOR_FRAGMENT_SHADER"].c_str());
        }
        if (renderer.uiMidiLaneVAO == 0) {
            glGenVertexArrays(1, &renderer.uiMidiLaneVAO);
        }
        if (renderer.uiMidiLaneVBO == 0) {
            glGenBuffers(1, &renderer.uiMidiLaneVBO);
        }

        const auto laneLayout = DawLaneTimelineSystemLogic::ComputeLaneLayout(baseSystem, daw, win);
        const int audioTrackCount = laneLayout.audioTrackCount;
        const int midiTrackCount = baseSystem.midi ? static_cast<int>(baseSystem.midi->tracks.size()) : 0;
        const int automationTrackCount = static_cast<int>(daw.automationTracks.size());
        const int laneCount = laneLayout.laneCount;
        if (automationTrackCount <= 0 || laneCount <= 0) {
            g_pointDragActive = false;
            g_rightMouseWasDown = (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
            return;
        }

        const float laneLeft = laneLayout.laneLeft;
        const float laneRight = laneLayout.laneRight;
        const float laneHalfH = laneLayout.laneHalfH;
        const float rowSpan = laneLayout.rowSpan;
        const float startY = laneLayout.startY;
        const float topBound = laneLayout.topBound;
        const float visualBottomBound = laneLayout.visualBottomBound;
        const double secondsPerScreen = laneLayout.secondsPerScreen;
        const double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
        double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
        if (windowSamples <= 0.0) windowSamples = 1.0;

        const auto automationLaneIndex = buildAutomationLaneIndex(daw, automationTrackCount, audioTrackCount, midiTrackCount);

        int previewSlot = -1;
        if (daw.dragActive && daw.dragLaneType == 2) {
            previewSlot = daw.dragDropIndex;
        } else if (daw.externalDropActive && daw.externalDropType == 2) {
            previewSlot = daw.externalDropIndex;
        }
        const bool previewingDrag = (previewSlot >= 0
            && daw.dragActive
            && daw.dragLaneType == 2
            && daw.dragLaneIndex >= 0);
        const int draggedLaneIndex = daw.dragLaneIndex;
        auto computeDisplayIndex = [&](int laneIndex) -> int {
            int displayIndex = laneIndex;
            if (previewSlot < 0) return displayIndex;
            if (previewingDrag) {
                if (laneIndex == draggedLaneIndex) return -1;
                if (laneIndex > draggedLaneIndex) displayIndex -= 1;
            }
            if (displayIndex >= previewSlot) displayIndex += 1;
            return displayIndex;
        };

        bool allowLaneInput = !isCursorOverOpenPanel(baseSystem, ui);
        const bool rightDown = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        const bool rightPressed = rightDown && !g_rightMouseWasDown;

        PointHit pointHit;
        LineHit lineHit;
        if (allowLaneInput) {
            for (int trackIndex = 0; trackIndex < automationTrackCount; ++trackIndex) {
                int laneIndex = automationLaneIndex[static_cast<size_t>(trackIndex)];
                if (laneIndex < 0) continue;
                int displayIndex = computeDisplayIndex(laneIndex);
                if (displayIndex < 0) continue;
                float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                float top = 0.0f;
                float bottom = 0.0f;
                float lipBottom = 0.0f;
                computeClipRect(centerY, laneHalfH, top, bottom, lipBottom);

                AutomationTrack& track = daw.automationTracks[static_cast<size_t>(trackIndex)];
                for (int clipIndex = 0; clipIndex < static_cast<int>(track.clips.size()); ++clipIndex) {
                    AutomationClip& clip = track.clips[static_cast<size_t>(clipIndex)];
                    if (clip.length == 0) continue;
                    sortAndClampPoints(clip);
                    double clipStart = static_cast<double>(clip.startSample);
                    double clipEnd = static_cast<double>(clip.startSample + clip.length);
                    if (clipEnd <= offsetSamples || clipStart >= offsetSamples + windowSamples) continue;
                    double visStart = std::max(clipStart, offsetSamples);
                    double visEnd = std::min(clipEnd, offsetSamples + windowSamples);
                    float t0 = static_cast<float>((visStart - offsetSamples) / windowSamples);
                    float t1 = static_cast<float>((visEnd - offsetSamples) / windowSamples);
                    float x0 = laneLeft + (laneRight - laneLeft) * t0 - kClipHorizontalPad;
                    float x1 = laneLeft + (laneRight - laneLeft) * t1 + kClipHorizontalPad;
                    x0 = std::max(x0, laneLeft);
                    x1 = std::min(x1, laneRight);
                    if (x1 <= x0) continue;

                    float bodyTop = std::min(bottom, lipBottom + 1.0f);
                    float bodyBottom = std::max(bodyTop + 1.0f, bottom - 1.0f);
                    if (ui.cursorX < x0 || ui.cursorX > x1 || ui.cursorY < bodyTop || ui.cursorY > bodyBottom) {
                        continue;
                    }

                    uint64_t cursorAbs = sampleFromCursorX(baseSystem,
                                                           daw,
                                                           laneLeft,
                                                           laneRight,
                                                           secondsPerScreen,
                                                           ui.cursorX,
                                                           false);
                    uint64_t localSample = (cursorAbs > clip.startSample)
                        ? (cursorAbs - clip.startSample)
                        : 0;
                    if (localSample > clip.length) localSample = clip.length;

                    for (int pi = 0; pi < static_cast<int>(clip.points.size()); ++pi) {
                        const AutomationPoint& pt = clip.points[static_cast<size_t>(pi)];
                        uint64_t absPoint = clip.startSample + pt.offsetSample;
                        float px = sampleToX(absPoint, offsetSamples, windowSamples, laneLeft, laneRight);
                        float py = yFromValue(pt.value, bodyTop, bodyBottom);
                        float dx = static_cast<float>(ui.cursorX) - px;
                        float dy = static_cast<float>(ui.cursorY) - py;
                        float dist = std::sqrt(dx * dx + dy * dy);
                        if (dist <= kPointHitRadius && dist < pointHit.dist) {
                            pointHit.valid = true;
                            pointHit.track = trackIndex;
                            pointHit.clip = clipIndex;
                            pointHit.point = pi;
                            pointHit.dist = dist;
                            pointHit.laneIndex = laneIndex;
                            pointHit.bodyTop = bodyTop;
                            pointHit.bodyBottom = bodyBottom;
                        }
                    }

                    float lineY = yFromValue(evaluateClipValue(clip, localSample), bodyTop, bodyBottom);
                    float lineDist = std::fabs(static_cast<float>(ui.cursorY) - lineY);
                    if (lineDist <= kLineHitTolerance) {
                        lineHit.valid = true;
                        lineHit.track = trackIndex;
                        lineHit.clip = clipIndex;
                        lineHit.laneIndex = laneIndex;
                        lineHit.localSample = localSample;
                        lineHit.y = lineY;
                        lineHit.bodyTop = bodyTop;
                        lineHit.bodyBottom = bodyBottom;
                    }
                }
            }
        }

        if (allowLaneInput && rightPressed && pointHit.valid) {
            AutomationTrack& track = daw.automationTracks[static_cast<size_t>(pointHit.track)];
            AutomationClip& clip = track.clips[static_cast<size_t>(pointHit.clip)];
            if (pointHit.point >= 0 && pointHit.point < static_cast<int>(clip.points.size())) {
                clip.points.erase(clip.points.begin() + pointHit.point);
                daw.selectedAutomationClipTrack = pointHit.track;
                daw.selectedAutomationClipIndex = pointHit.clip;
                daw.selectedClipTrack = -1;
                daw.selectedClipIndex = -1;
                if (baseSystem.midi) {
                    baseSystem.midi->selectedClipTrack = -1;
                    baseSystem.midi->selectedClipIndex = -1;
                }
                daw.timelineSelectionDragActive = false;
                daw.timelineSelectionActive = false;
                ui.consumeClick = true;
            }
        }

        if (allowLaneInput && ui.uiLeftPressed) {
            if (pointHit.valid) {
                AutomationTrack& track = daw.automationTracks[static_cast<size_t>(pointHit.track)];
                AutomationClip& clip = track.clips[static_cast<size_t>(pointHit.clip)];
                sortAndClampPoints(clip);
                if (pointHit.point >= 0 && pointHit.point < static_cast<int>(clip.points.size())) {
                    // Automation editing takes precedence over timeline selection drag for this click.
                    daw.timelineSelectionDragActive = false;
                    daw.timelineSelectionActive = false;
                    g_pointDragActive = true;
                    g_pointDragTrack = pointHit.track;
                    g_pointDragClip = pointHit.clip;
                    g_pointDragPoint = pointHit.point;
                    uint64_t minOffset = 0;
                    uint64_t maxOffset = clip.length;
                    if (pointHit.point > 0) {
                        minOffset = clip.points[static_cast<size_t>(pointHit.point - 1)].offsetSample;
                    }
                    if (pointHit.point + 1 < static_cast<int>(clip.points.size())) {
                        maxOffset = clip.points[static_cast<size_t>(pointHit.point + 1)].offsetSample;
                    }
                    g_pointDragMinOffset = minOffset;
                    g_pointDragMaxOffset = maxOffset;
                    daw.selectedAutomationClipTrack = pointHit.track;
                    daw.selectedAutomationClipIndex = pointHit.clip;
                    daw.selectedClipTrack = -1;
                    daw.selectedClipIndex = -1;
                    if (baseSystem.midi) {
                        baseSystem.midi->selectedClipTrack = -1;
                        baseSystem.midi->selectedClipIndex = -1;
                    }
                    daw.timelineSelectionDragActive = false;
                    daw.timelineSelectionActive = false;
                    ui.consumeClick = true;
                }
            } else if (lineHit.valid) {
                // Prevent timeline body click-drag logic from stealing this automation interaction.
                daw.timelineSelectionDragActive = false;
                daw.timelineSelectionActive = false;
                const double now = glfwGetTime();
                bool isDoubleClick = (lineHit.track == g_lastLineClickTrack
                    && lineHit.clip == g_lastLineClickClip
                    && g_lastLineClickTime > 0.0
                    && (now - g_lastLineClickTime) <= kDoubleClickSeconds);
                g_lastLineClickTrack = lineHit.track;
                g_lastLineClickClip = lineHit.clip;
                g_lastLineClickTime = now;

                if (isDoubleClick) {
                    AutomationTrack& track = daw.automationTracks[static_cast<size_t>(lineHit.track)];
                    AutomationClip& clip = track.clips[static_cast<size_t>(lineHit.clip)];
                    bool cmdDown = isCommandDown(win);
                    uint64_t cursorAbs = sampleFromCursorX(baseSystem,
                                                           daw,
                                                           laneLeft,
                                                           laneRight,
                                                           secondsPerScreen,
                                                           ui.cursorX,
                                                           !cmdDown);
                    uint64_t localSample = (cursorAbs > clip.startSample)
                        ? (cursorAbs - clip.startSample)
                        : 0;
                    if (localSample > clip.length) localSample = clip.length;
                    float value = valueFromY(static_cast<float>(ui.cursorY), lineHit.bodyTop, lineHit.bodyBottom);
                    clip.points.push_back({localSample, value});
                    sortAndClampPoints(clip);

                    int newPoint = -1;
                    for (int i = 0; i < static_cast<int>(clip.points.size()); ++i) {
                        const AutomationPoint& pt = clip.points[static_cast<size_t>(i)];
                        if (pt.offsetSample == localSample
                            && std::fabs(pt.value - value) <= 0.0001f) {
                            newPoint = i;
                            break;
                        }
                    }
                    if (newPoint < 0) {
                        newPoint = static_cast<int>(clip.points.size()) - 1;
                    }

                    g_pointDragActive = true;
                    g_pointDragTrack = lineHit.track;
                    g_pointDragClip = lineHit.clip;
                    g_pointDragPoint = newPoint;
                    uint64_t minOffset = 0;
                    uint64_t maxOffset = clip.length;
                    if (newPoint > 0) {
                        minOffset = clip.points[static_cast<size_t>(newPoint - 1)].offsetSample;
                    }
                    if (newPoint + 1 < static_cast<int>(clip.points.size())) {
                        maxOffset = clip.points[static_cast<size_t>(newPoint + 1)].offsetSample;
                    }
                    g_pointDragMinOffset = minOffset;
                    g_pointDragMaxOffset = maxOffset;

                    daw.selectedAutomationClipTrack = lineHit.track;
                    daw.selectedAutomationClipIndex = lineHit.clip;
                    daw.selectedClipTrack = -1;
                    daw.selectedClipIndex = -1;
                    if (baseSystem.midi) {
                        baseSystem.midi->selectedClipTrack = -1;
                        baseSystem.midi->selectedClipIndex = -1;
                    }
                    daw.timelineSelectionDragActive = false;
                    daw.timelineSelectionActive = false;
                    ui.consumeClick = true;
                }
            }
        }

        if (g_pointDragActive) {
            if (!ui.uiLeftDown) {
                g_pointDragActive = false;
                g_pointDragTrack = -1;
                g_pointDragClip = -1;
                g_pointDragPoint = -1;
            } else {
                if (g_pointDragTrack < 0
                    || g_pointDragTrack >= automationTrackCount
                    || g_pointDragClip < 0
                    || g_pointDragClip >= static_cast<int>(daw.automationTracks[static_cast<size_t>(g_pointDragTrack)].clips.size())) {
                    g_pointDragActive = false;
                    g_pointDragTrack = -1;
                    g_pointDragClip = -1;
                    g_pointDragPoint = -1;
                } else {
                    AutomationTrack& track = daw.automationTracks[static_cast<size_t>(g_pointDragTrack)];
                    AutomationClip& clip = track.clips[static_cast<size_t>(g_pointDragClip)];
                    sortAndClampPoints(clip);
                    if (g_pointDragPoint >= 0 && g_pointDragPoint < static_cast<int>(clip.points.size())) {
                        int laneIndex = automationLaneIndex[static_cast<size_t>(g_pointDragTrack)];
                        int displayIndex = computeDisplayIndex(laneIndex);
                        float centerY = startY + static_cast<float>(std::max(displayIndex, 0)) * rowSpan;
                        float top = 0.0f;
                        float bottom = 0.0f;
                        float lipBottom = 0.0f;
                        computeClipRect(centerY, laneHalfH, top, bottom, lipBottom);
                        float bodyTop = std::min(bottom, lipBottom + 1.0f);
                        float bodyBottom = std::max(bodyTop + 1.0f, bottom - 1.0f);
                        bool cmdDown = isCommandDown(win);
                        uint64_t cursorAbs = sampleFromCursorX(baseSystem,
                                                               daw,
                                                               laneLeft,
                                                               laneRight,
                                                               secondsPerScreen,
                                                               ui.cursorX,
                                                               !cmdDown);
                        uint64_t localSample = (cursorAbs > clip.startSample)
                            ? (cursorAbs - clip.startSample)
                            : 0;
                        if (localSample > clip.length) localSample = clip.length;
                        localSample = std::clamp(localSample, g_pointDragMinOffset, g_pointDragMaxOffset);
                        float value = valueFromY(static_cast<float>(ui.cursorY), bodyTop, bodyBottom);
                        clip.points[static_cast<size_t>(g_pointDragPoint)].offsetSample = localSample;
                        clip.points[static_cast<size_t>(g_pointDragPoint)].value = value;
                        daw.selectedAutomationClipTrack = g_pointDragTrack;
                        daw.selectedAutomationClipIndex = g_pointDragClip;
                        daw.selectedClipTrack = -1;
                        daw.selectedClipIndex = -1;
                        if (baseSystem.midi) {
                            baseSystem.midi->selectedClipTrack = -1;
                            baseSystem.midi->selectedClipIndex = -1;
                        }
                        daw.timelineSelectionDragActive = false;
                        daw.timelineSelectionActive = false;
                        ui.consumeClick = true;
                    }
                }
            }
        }

        applyAutomationToTargets(baseSystem, daw);

        g_vertices.clear();
        glm::vec3 clipColor(0.42f, 0.20f, 0.20f);
        glm::vec3 lipColor(0.28f, 0.11f, 0.11f);
        glm::vec3 lineColor(0.90f, 0.22f, 0.22f);
        glm::vec3 pointColor(1.0f, 0.85f, 0.85f);
        glm::vec3 selectedColor(0.45f, 0.72f, 1.0f);
        auto itSel = world.colorLibrary.find("MiraLaneSelected");
        if (itSel != world.colorLibrary.end()) {
            selectedColor = itSel->second;
        }

        for (int trackIndex = 0; trackIndex < automationTrackCount; ++trackIndex) {
            int laneIndex = automationLaneIndex[static_cast<size_t>(trackIndex)];
            if (laneIndex < 0) continue;
            int displayIndex = computeDisplayIndex(laneIndex);
            if (displayIndex < 0) continue;
            float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
            float top = 0.0f;
            float bottom = 0.0f;
            float lipBottom = 0.0f;
            computeClipRect(centerY, laneHalfH, top, bottom, lipBottom);
            float bodyTop = std::min(bottom, lipBottom + 1.0f);
            float bodyBottom = std::max(bodyTop + 1.0f, bottom - 1.0f);

            AutomationTrack& track = daw.automationTracks[static_cast<size_t>(trackIndex)];
            for (int clipIndex = 0; clipIndex < static_cast<int>(track.clips.size()); ++clipIndex) {
                AutomationClip& clip = track.clips[static_cast<size_t>(clipIndex)];
                if (clip.length == 0) continue;
                sortAndClampPoints(clip);
                double clipStart = static_cast<double>(clip.startSample);
                double clipEnd = static_cast<double>(clip.startSample + clip.length);
                if (clipEnd <= offsetSamples || clipStart >= offsetSamples + windowSamples) continue;

                double visStart = std::max(clipStart, offsetSamples);
                double visEnd = std::min(clipEnd, offsetSamples + windowSamples);
                float t0 = static_cast<float>((visStart - offsetSamples) / windowSamples);
                float t1 = static_cast<float>((visEnd - offsetSamples) / windowSamples);
                float x0 = laneLeft + (laneRight - laneLeft) * t0 - kClipHorizontalPad;
                float x1 = laneLeft + (laneRight - laneLeft) * t1 + kClipHorizontalPad;
                x0 = std::max(x0, laneLeft);
                x1 = std::min(x1, laneRight);
                if (x1 <= x0) continue;

                glm::vec3 localClip = clipColor;
                glm::vec3 localLip = lipColor;
                if (daw.selectedAutomationClipTrack == trackIndex
                    && daw.selectedAutomationClipIndex == clipIndex) {
                    localClip = glm::clamp(selectedColor * 0.65f, glm::vec3(0.0f), glm::vec3(1.0f));
                    localLip = glm::clamp(localClip - glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
                }

                pushQuad(g_vertices,
                         pixelToNDC({x0, top}, laneLayout.screenWidth, laneLayout.screenHeight),
                         pixelToNDC({x1, top}, laneLayout.screenWidth, laneLayout.screenHeight),
                         pixelToNDC({x1, bottom}, laneLayout.screenWidth, laneLayout.screenHeight),
                         pixelToNDC({x0, bottom}, laneLayout.screenWidth, laneLayout.screenHeight),
                         localClip);
                if (lipBottom > top + 0.5f) {
                    pushQuad(g_vertices,
                             pixelToNDC({x0, top}, laneLayout.screenWidth, laneLayout.screenHeight),
                             pixelToNDC({x1, top}, laneLayout.screenWidth, laneLayout.screenHeight),
                             pixelToNDC({x1, lipBottom}, laneLayout.screenWidth, laneLayout.screenHeight),
                             pixelToNDC({x0, lipBottom}, laneLayout.screenWidth, laneLayout.screenHeight),
                             localLip);
                }

                std::vector<std::pair<uint64_t, float>> nodes;
                if (clip.points.empty()) {
                    nodes.push_back({0, 0.5f});
                    nodes.push_back({clip.length, 0.5f});
                } else {
                    nodes.reserve(clip.points.size() + 2);
                    nodes.push_back({0, clamp01(clip.points.front().value)});
                    for (const auto& point : clip.points) {
                        nodes.push_back({point.offsetSample, clamp01(point.value)});
                    }
                    nodes.push_back({clip.length, clamp01(clip.points.back().value)});
                }

                for (size_t i = 0; i + 1 < nodes.size(); ++i) {
                    uint64_t segA = nodes[i].first;
                    uint64_t segB = nodes[i + 1].first;
                    float valA = nodes[i].second;
                    float valB = nodes[i + 1].second;
                    if (segB < segA) std::swap(segA, segB);
                    uint64_t absA = clip.startSample + segA;
                    uint64_t absB = clip.startSample + segB;
                    if (absB < static_cast<uint64_t>(offsetSamples)
                        || absA > static_cast<uint64_t>(offsetSamples + windowSamples)) {
                        continue;
                    }

                    uint64_t clipAbsStart = std::max<uint64_t>(absA, static_cast<uint64_t>(offsetSamples));
                    uint64_t clipAbsEnd = std::min<uint64_t>(absB, static_cast<uint64_t>(offsetSamples + windowSamples));
                    if (clipAbsEnd < clipAbsStart) continue;

                    auto valueAtAbs = [&](uint64_t absSample) {
                        if (absB <= absA) return valA;
                        float t = static_cast<float>(absSample - absA) / static_cast<float>(absB - absA);
                        return clamp01(valA + (valB - valA) * t);
                    };

                    float xA = sampleToX(clipAbsStart, offsetSamples, windowSamples, laneLeft, laneRight);
                    float xB = sampleToX(clipAbsEnd, offsetSamples, windowSamples, laneLeft, laneRight);
                    float yA = yFromValue(valueAtAbs(clipAbsStart), bodyTop, bodyBottom);
                    float yB = yFromValue(valueAtAbs(clipAbsEnd), bodyTop, bodyBottom);
                    pushLine(g_vertices,
                             {xA, yA},
                             {xB, yB},
                             kLineThickness,
                             lineColor,
                             laneLayout.screenWidth,
                             laneLayout.screenHeight);
                }

                for (int pi = 0; pi < static_cast<int>(clip.points.size()); ++pi) {
                    const AutomationPoint& point = clip.points[static_cast<size_t>(pi)];
                    uint64_t absSample = clip.startSample + point.offsetSample;
                    float x = sampleToX(absSample, offsetSamples, windowSamples, laneLeft, laneRight);
                    if (x < laneLeft - 8.0f || x > laneRight + 8.0f) continue;
                    float y = yFromValue(point.value, bodyTop, bodyBottom);
                    float radius = kPointRadius;
                    if (g_pointDragActive
                        && g_pointDragTrack == trackIndex
                        && g_pointDragClip == clipIndex
                        && g_pointDragPoint == pi) {
                        radius = kPointRadius + 1.5f;
                    }
                    pushQuad(g_vertices,
                             pixelToNDC({x - radius, y - radius}, laneLayout.screenWidth, laneLayout.screenHeight),
                             pixelToNDC({x + radius, y - radius}, laneLayout.screenWidth, laneLayout.screenHeight),
                             pixelToNDC({x + radius, y + radius}, laneLayout.screenWidth, laneLayout.screenHeight),
                             pixelToNDC({x - radius, y + radius}, laneLayout.screenWidth, laneLayout.screenHeight),
                             pointColor);
                }
            }
        }

        if (daw.selectedLaneType == 2 && daw.selectedLaneIndex >= 0 && daw.selectedLaneIndex < laneCount) {
            int displayIndex = computeDisplayIndex(daw.selectedLaneIndex);
            if (displayIndex >= 0) {
                float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                float handleSize = std::min(kTrackHandleSize, std::max(14.0f, laneLayout.laneHeight));
                float handleHalf = handleSize * 0.5f;
                float centerX = laneRight + kTrackHandleInset + handleHalf;
                float minCenterX = laneLeft + 4.0f + handleHalf;
                if (centerX < minCenterX) centerX = minCenterX;
                float top = centerY - handleHalf;
                float bottom = centerY + handleHalf;
                float bevelDepth = std::min(6.0f, handleHalf * 0.5f);
                glm::vec3 selectedHighlight = glm::clamp(selectedColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
                glm::vec3 selectedShadow = glm::clamp(selectedColor - glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f));
                pushQuad(g_vertices,
                         pixelToNDC({centerX - handleHalf, top}, laneLayout.screenWidth, laneLayout.screenHeight),
                         pixelToNDC({centerX + handleHalf, top}, laneLayout.screenWidth, laneLayout.screenHeight),
                         pixelToNDC({centerX + handleHalf, bottom}, laneLayout.screenWidth, laneLayout.screenHeight),
                         pixelToNDC({centerX - handleHalf, bottom}, laneLayout.screenWidth, laneLayout.screenHeight),
                         selectedColor);
                pushQuad(g_vertices,
                         pixelToNDC({centerX - handleHalf, top}, laneLayout.screenWidth, laneLayout.screenHeight),
                         pixelToNDC({centerX + handleHalf, top}, laneLayout.screenWidth, laneLayout.screenHeight),
                         pixelToNDC({centerX + handleHalf - bevelDepth, top - bevelDepth}, laneLayout.screenWidth, laneLayout.screenHeight),
                         pixelToNDC({centerX - handleHalf - bevelDepth, top - bevelDepth}, laneLayout.screenWidth, laneLayout.screenHeight),
                         selectedHighlight);
                pushQuad(g_vertices,
                         pixelToNDC({centerX - handleHalf, top}, laneLayout.screenWidth, laneLayout.screenHeight),
                         pixelToNDC({centerX - handleHalf, bottom}, laneLayout.screenWidth, laneLayout.screenHeight),
                         pixelToNDC({centerX - handleHalf - bevelDepth, bottom - bevelDepth}, laneLayout.screenWidth, laneLayout.screenHeight),
                         pixelToNDC({centerX - handleHalf - bevelDepth, top - bevelDepth}, laneLayout.screenWidth, laneLayout.screenHeight),
                         selectedShadow);
            }
        }

        if ((daw.dragActive && daw.dragLaneType == 2) || (daw.externalDropActive && daw.externalDropType == 2)) {
            float ghostCenterY = daw.dragActive
                ? static_cast<float>(ui.cursorY)
                : (startY + static_cast<float>(daw.externalDropIndex) * rowSpan);
            float handleSize = std::min(kTrackHandleSize, std::max(14.0f, laneLayout.laneHeight));
            float handleHalf = handleSize * 0.5f;
            float centerX = laneRight + kTrackHandleInset + handleHalf;
            float minCenterX = laneLeft + 4.0f + handleHalf;
            if (centerX < minCenterX) centerX = minCenterX;
            float ghostTop = ghostCenterY - handleHalf;
            float ghostBottom = ghostCenterY + handleHalf;
            glm::vec3 ghostColor = glm::clamp(selectedColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
            float bevelDepth = std::min(6.0f, handleHalf * 0.5f);
            pushQuad(g_vertices,
                     pixelToNDC({centerX - handleHalf, ghostTop}, laneLayout.screenWidth, laneLayout.screenHeight),
                     pixelToNDC({centerX + handleHalf, ghostTop}, laneLayout.screenWidth, laneLayout.screenHeight),
                     pixelToNDC({centerX + handleHalf, ghostBottom}, laneLayout.screenWidth, laneLayout.screenHeight),
                     pixelToNDC({centerX - handleHalf, ghostBottom}, laneLayout.screenWidth, laneLayout.screenHeight),
                     ghostColor);
            pushQuad(g_vertices,
                     pixelToNDC({centerX - handleHalf, ghostTop}, laneLayout.screenWidth, laneLayout.screenHeight),
                     pixelToNDC({centerX + handleHalf, ghostTop}, laneLayout.screenWidth, laneLayout.screenHeight),
                     pixelToNDC({centerX + handleHalf - bevelDepth, ghostTop - bevelDepth}, laneLayout.screenWidth, laneLayout.screenHeight),
                     pixelToNDC({centerX - handleHalf - bevelDepth, ghostTop - bevelDepth}, laneLayout.screenWidth, laneLayout.screenHeight),
                     glm::clamp(ghostColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f)));
            pushQuad(g_vertices,
                     pixelToNDC({centerX - handleHalf, ghostTop}, laneLayout.screenWidth, laneLayout.screenHeight),
                     pixelToNDC({centerX - handleHalf, ghostBottom}, laneLayout.screenWidth, laneLayout.screenHeight),
                     pixelToNDC({centerX - handleHalf - bevelDepth, ghostBottom - bevelDepth}, laneLayout.screenWidth, laneLayout.screenHeight),
                     pixelToNDC({centerX - handleHalf - bevelDepth, ghostTop - bevelDepth}, laneLayout.screenWidth, laneLayout.screenHeight),
                     glm::clamp(ghostColor - glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f)));
            if (previewSlot >= 0) {
                float insertY = startY + (static_cast<float>(previewSlot) - 0.5f) * rowSpan;
                float lineHalf = 2.0f;
                pushQuad(g_vertices,
                         pixelToNDC({laneLeft, insertY - lineHalf}, laneLayout.screenWidth, laneLayout.screenHeight),
                         pixelToNDC({laneRight, insertY - lineHalf}, laneLayout.screenWidth, laneLayout.screenHeight),
                         pixelToNDC({laneRight, insertY + lineHalf}, laneLayout.screenWidth, laneLayout.screenHeight),
                         pixelToNDC({laneLeft, insertY + lineHalf}, laneLayout.screenWidth, laneLayout.screenHeight),
                         selectedColor);
            }
        }

        if (!g_vertices.empty()) {
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendColor(0.0f, 0.0f, 0.0f, kLaneAlpha);
            glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
            glBindVertexArray(renderer.uiMidiLaneVAO);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.uiMidiLaneVBO);
            glBufferData(GL_ARRAY_BUFFER,
                         g_vertices.size() * sizeof(UiVertex),
                         g_vertices.data(),
                         GL_DYNAMIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)(sizeof(glm::vec2)));

            renderer.uiColorShader->use();
            renderer.uiColorShader->setFloat("alpha", kLaneAlpha);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(g_vertices.size()));
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_DEPTH_TEST);
        }

        g_rightMouseWasDown = rightDown;
    }

    void OnTimelineRebased(uint64_t shiftSamples) {
        (void)shiftSamples;
    }
}
