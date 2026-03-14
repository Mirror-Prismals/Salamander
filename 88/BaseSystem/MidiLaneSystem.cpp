#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "stb_easy_font.h"

namespace MidiTrackSystemLogic { bool MoveTrack(BaseSystem& baseSystem, int fromIndex, int toIndex); }
namespace DawLaneTimelineSystemLogic {
    struct LaneLayout;
    LaneLayout ComputeLaneLayout(const BaseSystem& baseSystem, const DawContext& daw, GLFWwindow* win);
    double GridSecondsForZoom(double secondsPerScreen, double secondsPerBeat);
}
namespace PianoRollResourceSystemLogic { void NoteColor(int noteIndex, float& r, float& g, float& b); }
namespace DawLaneInputSystemLogic { void ApplyLaneResizeCursor(GLFWwindow* win, bool active); }
namespace DawTimelineRebaseLogic { void ShiftTimelineRight(BaseSystem& baseSystem, uint64_t shiftSamples); }

namespace MidiLaneSystemLogic {
    namespace {
        constexpr float kLaneAlphaDefault = 0.85f;
        constexpr float kLaneLeftMargin = 40.0f;
        constexpr float kLaneRightMargin = 40.0f;
        constexpr float kLaneHeight = 60.0f;
        constexpr float kLaneGap = 12.0f;
        constexpr float kTrackHandleSize = 60.0f;
        constexpr float kTrackHandleInset = 12.0f;
        constexpr float kTrackHandleReserve = kTrackHandleInset + kTrackHandleSize;
        constexpr float kClipHorizontalPad = 2.0f;
        constexpr float kClipVerticalInset = 0.0f;
        constexpr float kClipMinHeight = 2.0f;
        constexpr float kClipLipMinHeight = 6.0f;
        constexpr float kClipLipMaxHeight = 12.0f;
        constexpr float kTakeRowGap = 4.0f;
        constexpr float kTakeRowSpacing = 2.0f;
        constexpr float kTakeRowMinHeight = 10.0f;
        constexpr float kTakeRowMaxHeight = 18.0f;
        constexpr float kTrimEdgeHitWidth = 8.0f;
        constexpr uint64_t kMinClipSamples = 1;
        constexpr size_t kWaveformBlockSize = 256;
        constexpr int kPreviewBasePitch = 24;
        constexpr int kPreviewRows = 84;

        struct UiVertex { glm::vec2 pos; glm::vec3 color; };
        static std::vector<UiVertex> g_laneVertices;
        static size_t g_staticVertexCount = 0;
        static std::vector<uint64_t> g_waveVersions;
        static int g_cachedTrackCount = 0;
        static int g_cachedAudioTrackCount = 0;
        static int64_t g_cachedTimelineOffset = 0;
        static double g_cachedSecondsPerScreen = 10.0;
        static float g_cachedScrollY = 0.0f;
        static int g_cachedWidth = 0;
        static int g_cachedHeight = 0;
        static std::vector<int> g_cachedLaneSignature;
        static std::vector<uint64_t> g_cachedClipSignature;
        static int g_cachedPreviewSlot = -1;
        static float g_cachedLaneHeight = kLaneHeight;
        static uint64_t g_cachedThemeRevision = 0;
        static double g_lastClipClickTime = -1.0;
        static int g_lastClipClickTrack = -1;
        static int g_lastClipClickIndex = -1;
        static bool g_clipDragActive = false;
        static int g_clipDragTrack = -1;
        static int g_clipDragIndex = -1;
        static int g_clipDragTargetTrack = -1;
        static uint64_t g_clipDragTargetStart = 0;
        static int64_t g_clipDragOffsetSamples = 0;
        static float g_clipDragStartX = 0.0f;
        static float g_clipDragStartY = 0.0f;
        static bool g_clipDragMoved = false;
        static bool g_clipTrimActive = false;
        static int g_clipTrimTrack = -1;
        static int g_clipTrimIndex = -1;
        static bool g_clipTrimLeftEdge = false;
        static uint64_t g_clipTrimOriginalStart = 0;
        static uint64_t g_clipTrimOriginalLength = 0;
        static uint64_t g_clipTrimTargetStart = 0;
        static uint64_t g_clipTrimTargetLength = 0;

        struct MidiTrimHit {
            bool valid = false;
            bool leftEdge = false;
            int track = -1;
            int clipIndex = -1;
            float edgeDistance = FLT_MAX;
            uint64_t clipStart = 0;
            uint64_t clipLength = 0;
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

        void pushText(std::vector<UiVertex>& verts,
                      float x,
                      float y,
                      const char* text,
                      const glm::vec3& color,
                      double width,
                      double height) {
            if (!text || text[0] == '\0') return;
            char buffer[99999];
            int numQuads = stb_easy_font_print(x, y, const_cast<char*>(text), nullptr, buffer, sizeof(buffer));
            float* vertsRaw = reinterpret_cast<float*>(buffer);
            for (int i = 0; i < numQuads; ++i) {
                const int base = i * 16;
                glm::vec2 v0{vertsRaw[base + 0], vertsRaw[base + 1]};
                glm::vec2 v1{vertsRaw[base + 4], vertsRaw[base + 5]};
                glm::vec2 v2{vertsRaw[base + 8], vertsRaw[base + 9]};
                glm::vec2 v3{vertsRaw[base + 12], vertsRaw[base + 13]};
                pushQuad(verts,
                         pixelToNDC(v0, width, height),
                         pixelToNDC(v1, width, height),
                         pixelToNDC(v2, width, height),
                         pixelToNDC(v3, width, height),
                         color);
            }
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

        float takeRowHeight(float laneHalfH) {
            float laneHeight = laneHalfH * 2.0f;
            return std::clamp(laneHeight * 0.26f, kTakeRowMinHeight, kTakeRowMaxHeight);
        }

        int laneIndexFromCursorY(float y, float startY, float laneHalfH, float rowSpan, int laneCount) {
            for (int i = 0; i < laneCount; ++i) {
                float centerY = startY + static_cast<float>(i) * rowSpan;
                if (y >= centerY - laneHalfH && y <= centerY + laneHalfH) {
                    return i;
                }
            }
            return -1;
        }

        int laneIndexFromCursorYClamped(float y, float startY, float rowSpan, int laneCount) {
            if (laneCount <= 0) return -1;
            int idx = static_cast<int>(std::floor((y - startY) / rowSpan + 0.5f));
            return std::clamp(idx, 0, laneCount - 1);
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

        uint64_t computeRebaseShiftSamples(const DawContext& daw, int64_t negativeSample);

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

        void applyTrimToMidiClip(MidiClip& clip, uint64_t newStart, uint64_t newLength) {
            uint64_t oldStart = clip.startSample;
            uint64_t newEnd = newStart + newLength;
            std::vector<MidiNote> trimmed;
            trimmed.reserve(clip.notes.size());
            for (const auto& note : clip.notes) {
                if (note.length == 0) continue;
                uint64_t noteStart = oldStart + note.startSample;
                uint64_t noteEnd = noteStart + note.length;
                if (noteEnd <= newStart || noteStart >= newEnd) continue;
                uint64_t clippedStart = std::max(noteStart, newStart);
                uint64_t clippedEnd = std::min(noteEnd, newEnd);
                if (clippedEnd <= clippedStart) continue;
                MidiNote out = note;
                out.startSample = clippedStart - newStart;
                out.length = clippedEnd - clippedStart;
                trimmed.push_back(out);
            }
            clip.startSample = newStart;
            clip.length = newLength;
            clip.notes = std::move(trimmed);
        }

        MidiTrimHit findMidiTrimHit(const MidiContext& midi,
                                    const DawContext& daw,
                                    const UIContext& ui,
                                    const std::vector<int>& midiLaneIndex,
                                    int midiTrackCount,
                                    float laneLeft,
                                    float laneRight,
                                    float laneHalfH,
                                    float rowSpan,
                                    float startY,
                                    double secondsPerScreen) {
            MidiTrimHit hit;
            if (midiTrackCount <= 0 || laneRight <= laneLeft) return hit;
            double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
            double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
            if (windowSamples <= 0.0) windowSamples = 1.0;

            for (int t = 0; t < midiTrackCount; ++t) {
                const auto& clips = midi.tracks[static_cast<size_t>(t)].clips;
                if (clips.empty()) continue;
                int laneIndex = midiLaneIndex[static_cast<size_t>(t)];
                if (laneIndex < 0) continue;
                float centerY = startY + static_cast<float>(laneIndex) * rowSpan;
                float top = 0.0f;
                float bottom = 0.0f;
                float lipBottom = 0.0f;
                computeClipRect(centerY, laneHalfH, top, bottom, lipBottom);
                if (ui.cursorY < top || ui.cursorY > lipBottom) continue;

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
                    float x0 = laneLeft + (laneRight - laneLeft) * t0 - kClipHorizontalPad;
                    float x1 = laneLeft + (laneRight - laneLeft) * t1 + kClipHorizontalPad;
                    x0 = std::max(x0, laneLeft);
                    x1 = std::min(x1, laneRight);
                    if (x1 <= x0) continue;
                    if (ui.cursorX < x0 - kTrimEdgeHitWidth || ui.cursorX > x1 + kTrimEdgeHitWidth) continue;

                    float distLeft = std::fabs(static_cast<float>(ui.cursorX) - x0);
                    float distRight = std::fabs(static_cast<float>(ui.cursorX) - x1);
                    bool nearLeft = distLeft <= kTrimEdgeHitWidth;
                    bool nearRight = distRight <= kTrimEdgeHitWidth;
                    if (!nearLeft && !nearRight) continue;

                    bool pickLeft = false;
                    float edgeDist = 0.0f;
                    if (nearLeft && nearRight) {
                        pickLeft = distLeft <= distRight;
                        edgeDist = pickLeft ? distLeft : distRight;
                    } else if (nearLeft) {
                        pickLeft = true;
                        edgeDist = distLeft;
                    } else {
                        edgeDist = distRight;
                    }

                    if (!hit.valid || edgeDist < hit.edgeDistance) {
                        hit.valid = true;
                        hit.leftEdge = pickLeft;
                        hit.track = t;
                        hit.clipIndex = static_cast<int>(ci);
                        hit.edgeDistance = edgeDist;
                        hit.clipStart = clip.startSample;
                        hit.clipLength = clip.length;
                    }
                }
            }
            return hit;
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
                    left.notes.erase(std::remove_if(left.notes.begin(), left.notes.end(),
                                                    [&](const MidiNote& note) {
                                                        return note.startSample >= left.length;
                                                    }),
                                     left.notes.end());
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

        void sortMidiClipsByStart(std::vector<MidiClip>& clips) {
            std::sort(clips.begin(), clips.end(), [](const MidiClip& a, const MidiClip& b) {
                if (a.startSample == b.startSample) return a.length < b.length;
                return a.startSample < b.startSample;
            });
        }

        void ensureResources(RendererContext& renderer, WorldContext& world) {
            if (!renderer.uiColorShader) {
                renderer.uiColorShader = std::make_unique<Shader>(world.shaders["UI_COLOR_VERTEX_SHADER"].c_str(),
                                                                 world.shaders["UI_COLOR_FRAGMENT_SHADER"].c_str());
            }
            if (renderer.uiMidiLaneVAO == 0) {
                glGenVertexArrays(1, &renderer.uiMidiLaneVAO);
                glGenBuffers(1, &renderer.uiMidiLaneVBO);
            }
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
    }

    void UpdateMidiLane(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt;
        if (!baseSystem.ui || !baseSystem.midi || !baseSystem.daw || !baseSystem.renderer || !baseSystem.world || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        ensureResources(renderer, world);
        if (!renderer.uiColorShader) return;

        int windowWidth = 0, windowHeight = 0;
        glfwGetWindowSize(win, &windowWidth, &windowHeight);
        double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
        double screenHeight = windowHeight > 0 ? static_cast<double>(windowHeight) : 1080.0;

        MidiContext& midi = *baseSystem.midi;
        if (midi.pianoRollActive) return;
        DawContext& daw = *baseSystem.daw;
        bool allowLaneInput = !isCursorOverOpenPanel(baseSystem, ui);
        int midiTrackCount = static_cast<int>(midi.tracks.size());
        if (midiTrackCount <= 0) return;
        if (midi.selectedClipTrack < 0 || midi.selectedClipTrack >= midiTrackCount) {
            midi.selectedClipTrack = -1;
            midi.selectedClipIndex = -1;
        } else {
            const auto& selectedTrackClips = midi.tracks[static_cast<size_t>(midi.selectedClipTrack)].clips;
            if (midi.selectedClipIndex < 0 || midi.selectedClipIndex >= static_cast<int>(selectedTrackClips.size())) {
                midi.selectedClipTrack = -1;
                midi.selectedClipIndex = -1;
            }
        }

        const auto laneLayout = DawLaneTimelineSystemLogic::ComputeLaneLayout(baseSystem, daw, win);
        float laneLeft = laneLayout.laneLeft;
        float laneRight = laneLayout.laneRight;
        float scrollY = laneLayout.startY;
        int audioTrackCount = static_cast<int>(daw.tracks.size());
        int laneCount = laneLayout.laneCount;
        float startY = laneLayout.startY;
        float rowSpan = laneLayout.rowSpan;
        if (laneCount == 0) {
            startY += static_cast<float>(audioTrackCount) * rowSpan;
            laneCount = audioTrackCount + midiTrackCount;
        }
        float laneHalfH = laneLayout.laneHalfH;
        double secondsPerScreen = laneLayout.secondsPerScreen;

        glm::vec3 laneColor(0.14f, 0.14f, 0.14f);
        glm::vec3 laneHighlight(0.2f, 0.2f, 0.2f);
        glm::vec3 laneShadow(0.08f, 0.08f, 0.08f);
        glm::vec3 waveformBaseColor(0.05f, 0.05f, 0.05f);
        auto itBase = world.colorLibrary.find("MiraLaneBase");
        if (itBase != world.colorLibrary.end()) {
            laneColor = itBase->second;
            auto itHighlight = world.colorLibrary.find("MiraLaneHighlight");
            if (itHighlight != world.colorLibrary.end()) {
                laneHighlight = itHighlight->second;
            } else {
                laneHighlight = glm::clamp(laneColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
            }
            auto itShadow = world.colorLibrary.find("MiraLaneShadow");
            if (itShadow != world.colorLibrary.end()) {
                laneShadow = itShadow->second;
            } else {
                laneShadow = glm::clamp(laneColor - glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
            }
            auto itWave = world.colorLibrary.find("MiraWaveform");
            if (itWave != world.colorLibrary.end()) {
                waveformBaseColor = itWave->second;
            } else {
                waveformBaseColor = glm::clamp(laneShadow - glm::vec3(0.2f), glm::vec3(0.0f), glm::vec3(1.0f));
            }
        }

        bool rebuildStatic = (windowWidth != g_cachedWidth) || (windowHeight != g_cachedHeight);
        if (std::abs(scrollY - g_cachedScrollY) > 0.01f) {
            rebuildStatic = true;
        }
        if (midiTrackCount != g_cachedTrackCount) {
            rebuildStatic = true;
        }
        if (audioTrackCount != g_cachedAudioTrackCount) {
            rebuildStatic = true;
        }
        if (std::abs(secondsPerScreen - g_cachedSecondsPerScreen) > 0.0001) {
            rebuildStatic = true;
        }
        if (daw.timelineOffsetSamples != g_cachedTimelineOffset) {
            rebuildStatic = true;
        }
        if (daw.themeRevision != g_cachedThemeRevision) {
            rebuildStatic = true;
        }
        if (std::abs(laneLayout.laneHeight - g_cachedLaneHeight) > 0.01f) {
            rebuildStatic = true;
        }
        {
            std::vector<uint64_t> clipSig;
            clipSig.reserve(static_cast<size_t>(midiTrackCount) * 4);
            for (int t = 0; t < midiTrackCount; ++t) {
                clipSig.push_back(static_cast<uint64_t>(t));
                const auto& clips = midi.tracks[static_cast<size_t>(t)].clips;
                clipSig.push_back(static_cast<uint64_t>(clips.size()));
                for (const auto& clip : clips) {
                    clipSig.push_back(clip.startSample);
                    clipSig.push_back(clip.length);
                    clipSig.push_back(static_cast<uint64_t>(clip.notes.size()));
                    for (const auto& note : clip.notes) {
                        clipSig.push_back(static_cast<uint64_t>(note.pitch + 256));
                        clipSig.push_back(note.startSample);
                        clipSig.push_back(note.length);
                    }
                }
            }
            clipSig.push_back(static_cast<uint64_t>(midi.selectedClipTrack + 1));
            clipSig.push_back(static_cast<uint64_t>(midi.selectedClipIndex + 1));
            if (clipSig != g_cachedClipSignature) {
                rebuildStatic = true;
                g_cachedClipSignature = std::move(clipSig);
            }
        }
        if (g_waveVersions.size() != static_cast<size_t>(midiTrackCount)) {
            g_waveVersions.assign(static_cast<size_t>(midiTrackCount), 0);
            rebuildStatic = true;
        }
        for (int t = 0; t < midiTrackCount; ++t) {
            if (midi.tracks[static_cast<size_t>(t)].waveformVersion != g_waveVersions[static_cast<size_t>(t)]) {
                rebuildStatic = true;
                break;
            }
        }

        std::vector<int> midiLaneIndex(static_cast<size_t>(midiTrackCount), -1);
        if (!daw.laneOrder.empty()) {
            for (size_t laneIdx = 0; laneIdx < daw.laneOrder.size(); ++laneIdx) {
                const auto& entry = daw.laneOrder[laneIdx];
                if (entry.type == 1 && entry.trackIndex >= 0 && entry.trackIndex < midiTrackCount) {
                    midiLaneIndex[static_cast<size_t>(entry.trackIndex)] = static_cast<int>(laneIdx);
                }
            }
        } else {
            for (int i = 0; i < midiTrackCount; ++i) {
                midiLaneIndex[static_cast<size_t>(i)] = audioTrackCount + i;
            }
        }

        std::vector<int> laneSignature;
        laneSignature.reserve(daw.laneOrder.size() * 2);
        for (const auto& entry : daw.laneOrder) {
            laneSignature.push_back(entry.type);
            laneSignature.push_back(entry.trackIndex);
        }
        if (laneSignature != g_cachedLaneSignature) {
            rebuildStatic = true;
            g_cachedLaneSignature = laneSignature;
        }

        MidiTrimHit trimHover;
        bool trimCursorWanted = false;
        if (allowLaneInput && !g_clipDragActive && !g_clipTrimActive) {
            trimHover = findMidiTrimHit(midi,
                                        daw,
                                        ui,
                                        midiLaneIndex,
                                        midiTrackCount,
                                        laneLeft,
                                        laneRight,
                                        laneHalfH,
                                        rowSpan,
                                        startY,
                                        secondsPerScreen);
            trimCursorWanted = trimHover.valid;
        }

        if (!g_clipDragActive && !g_clipTrimActive
            && allowLaneInput && ui.uiLeftPressed && !ui.consumeClick
            && trimHover.valid) {
            g_clipTrimActive = true;
            g_clipTrimTrack = trimHover.track;
            g_clipTrimIndex = trimHover.clipIndex;
            g_clipTrimLeftEdge = trimHover.leftEdge;
            g_clipTrimOriginalStart = trimHover.clipStart;
            g_clipTrimOriginalLength = trimHover.clipLength;
            g_clipTrimTargetStart = trimHover.clipStart;
            g_clipTrimTargetLength = trimHover.clipLength;
            midi.selectedTrackIndex = trimHover.track;
            midi.selectedClipTrack = trimHover.track;
            midi.selectedClipIndex = trimHover.clipIndex;
            daw.selectedClipTrack = -1;
            daw.selectedClipIndex = -1;
            ui.consumeClick = true;
            trimCursorWanted = true;
        }

        if (!g_clipDragActive && !g_clipTrimActive && allowLaneInput && ui.uiLeftPressed && !ui.consumeClick) {
            double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
            double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
            if (windowSamples <= 0.0) windowSamples = 1.0;
            int hitTrack = -1;
            int hitClip = -1;
            uint64_t hitCursorSample = 0;
            for (int t = 0; t < midiTrackCount; ++t) {
                const auto& clips = midi.tracks[static_cast<size_t>(t)].clips;
                if (clips.empty()) continue;
                int laneIndex = midiLaneIndex[static_cast<size_t>(t)];
                if (laneIndex < 0) continue;
                float centerY = startY + static_cast<float>(laneIndex) * rowSpan;
                float top = 0.0f;
                float bottom = 0.0f;
                float lipBottom = 0.0f;
                computeClipRect(centerY, laneHalfH, top, bottom, lipBottom);
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
                    float x0 = laneLeft + (laneRight - laneLeft) * t0 - kClipHorizontalPad;
                    float x1 = laneLeft + (laneRight - laneLeft) * t1 + kClipHorizontalPad;
                    x0 = std::max(x0, laneLeft);
                    x1 = std::min(x1, laneRight);
                    if (x1 <= x0) continue;
                    if (ui.cursorX >= x0 && ui.cursorX <= x1
                        && ui.cursorY >= top && ui.cursorY <= lipBottom) {
                        double cursorT = (laneRight > laneLeft)
                            ? (static_cast<double>(ui.cursorX) - laneLeft) / (laneRight - laneLeft)
                            : 0.0;
                        cursorT = std::clamp(cursorT, 0.0, 1.0);
                        hitCursorSample = static_cast<uint64_t>(std::llround(offsetSamples + cursorT * windowSamples));
                        hitTrack = t;
                        hitClip = static_cast<int>(ci);
                        break;
                    }
                }
                if (hitTrack >= 0) break;
            }
            if (hitTrack >= 0 && hitClip >= 0) {
                const auto& track = midi.tracks[static_cast<size_t>(hitTrack)];
                if (hitClip >= 0 && hitClip < static_cast<int>(track.clips.size())) {
                    const MidiClip& clip = track.clips[static_cast<size_t>(hitClip)];
                    g_clipDragActive = true;
                    g_clipDragTrack = hitTrack;
                    g_clipDragIndex = hitClip;
                    g_clipDragTargetTrack = hitTrack;
                    g_clipDragTargetStart = clip.startSample;
                    g_clipDragOffsetSamples = static_cast<int64_t>(hitCursorSample) - static_cast<int64_t>(clip.startSample);
                    g_clipDragStartX = static_cast<float>(ui.cursorX);
                    g_clipDragStartY = static_cast<float>(ui.cursorY);
                    g_clipDragMoved = false;
                    midi.selectedTrackIndex = hitTrack;
                    midi.selectedClipTrack = hitTrack;
                    midi.selectedClipIndex = hitClip;
                    daw.selectedClipTrack = -1;
                    daw.selectedClipIndex = -1;
                    int hitLane = (hitTrack >= 0 && hitTrack < static_cast<int>(midiLaneIndex.size()))
                        ? midiLaneIndex[static_cast<size_t>(hitTrack)]
                        : hitTrack;
                    if (hitLane < 0) hitLane = 0;
                    daw.timelineSelectionStartSample = clip.startSample;
                    daw.timelineSelectionEndSample = clip.startSample + clip.length;
                    daw.timelineSelectionStartLane = hitLane;
                    daw.timelineSelectionEndLane = hitLane;
                    daw.timelineSelectionAnchorSample = clip.startSample;
                    daw.timelineSelectionAnchorLane = hitLane;
                    daw.timelineSelectionActive = (clip.length > 0);
                    daw.timelineSelectionDragActive = false;
                    daw.timelineSelectionFromPlayhead = false;
                    daw.selectedLaneIndex = -1;
                    daw.selectedLaneType = -1;
                    daw.selectedLaneTrack = -1;
                    ui.consumeClick = true;
                }
            }
        }

        if (g_clipDragActive) {
            if (g_clipDragTrack < 0 || g_clipDragTrack >= midiTrackCount
                || g_clipDragIndex < 0
                || g_clipDragIndex >= static_cast<int>(midi.tracks[static_cast<size_t>(g_clipDragTrack)].clips.size())) {
                g_clipDragActive = false;
                g_clipDragTrack = -1;
                g_clipDragIndex = -1;
                g_clipDragTargetTrack = -1;
                g_clipDragTargetStart = 0;
                g_clipDragOffsetSamples = 0;
                g_clipDragMoved = false;
            } else if (!ui.uiLeftDown) {
                if (!g_clipDragMoved) {
                    double now = glfwGetTime();
                    if (g_lastClipClickTrack == g_clipDragTrack && g_lastClipClickIndex == g_clipDragIndex
                        && now - g_lastClipClickTime < 0.35) {
                        midi.pianoRollActive = true;
                        midi.pianoRollTrack = g_clipDragTrack;
                        midi.pianoRollClipIndex = g_clipDragIndex;
                    }
                    g_lastClipClickTime = now;
                    g_lastClipClickTrack = g_clipDragTrack;
                    g_lastClipClickIndex = g_clipDragIndex;
                } else {
                    int srcTrack = g_clipDragTrack;
                    int srcIndex = g_clipDragIndex;
                    int dstTrack = g_clipDragTargetTrack;
                    if (dstTrack < 0 || dstTrack >= midiTrackCount) dstTrack = srcTrack;
                    MidiTrack& fromTrack = midi.tracks[static_cast<size_t>(srcTrack)];
                    if (srcIndex >= 0 && srcIndex < static_cast<int>(fromTrack.clips.size())) {
                        MidiClip moved = fromTrack.clips[static_cast<size_t>(srcIndex)];
                        fromTrack.clips.erase(fromTrack.clips.begin() + srcIndex);
                        moved.startSample = g_clipDragTargetStart;
                        uint64_t movedStart = moved.startSample;
                        uint64_t movedLength = moved.length;
                        size_t movedNoteCount = moved.notes.size();
                        int movedTakeId = moved.takeId;
                        MidiTrack& toTrack = midi.tracks[static_cast<size_t>(dstTrack)];
                        trimMidiClipsForNewClip(toTrack, moved);
                        toTrack.clips.push_back(std::move(moved));
                        sortMidiClipsByStart(toTrack.clips);
                        if (srcTrack != dstTrack) {
                            sortMidiClipsByStart(fromTrack.clips);
                        }
                        midi.selectedTrackIndex = dstTrack;
                        int selectedIndex = -1;
                        for (size_t i = 0; i < toTrack.clips.size(); ++i) {
                            const MidiClip& candidate = toTrack.clips[i];
                            if (candidate.startSample != movedStart) continue;
                            if (candidate.length != movedLength) continue;
                            if (candidate.notes.size() != movedNoteCount) continue;
                            if (candidate.takeId != movedTakeId) continue;
                            selectedIndex = static_cast<int>(i);
                            break;
                        }
                        if (selectedIndex < 0) {
                            for (size_t i = 0; i < toTrack.clips.size(); ++i) {
                                const MidiClip& candidate = toTrack.clips[i];
                                if (candidate.startSample == movedStart) {
                                    selectedIndex = static_cast<int>(i);
                                    break;
                                }
                            }
                        }
                        midi.selectedClipTrack = dstTrack;
                        midi.selectedClipIndex = selectedIndex;
                        const MidiClip* selectedClip = nullptr;
                        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(toTrack.clips.size())) {
                            selectedClip = &toTrack.clips[static_cast<size_t>(selectedIndex)];
                        } else {
                            selectedClip = &toTrack.clips.back();
                        }
                        int selectedLane = (dstTrack >= 0 && dstTrack < static_cast<int>(midiLaneIndex.size()))
                            ? midiLaneIndex[static_cast<size_t>(dstTrack)]
                            : dstTrack;
                        if (selectedLane < 0) selectedLane = 0;
                        daw.timelineSelectionStartSample = selectedClip->startSample;
                        daw.timelineSelectionEndSample = selectedClip->startSample + selectedClip->length;
                        daw.timelineSelectionStartLane = selectedLane;
                        daw.timelineSelectionEndLane = selectedLane;
                        daw.timelineSelectionAnchorSample = selectedClip->startSample;
                        daw.timelineSelectionAnchorLane = selectedLane;
                        daw.timelineSelectionActive = (selectedClip->length > 0);
                        daw.timelineSelectionDragActive = false;
                        daw.timelineSelectionFromPlayhead = false;
                        daw.selectedClipTrack = -1;
                        daw.selectedClipIndex = -1;
                    }
                }

                g_clipDragActive = false;
                g_clipDragTrack = -1;
                g_clipDragIndex = -1;
                g_clipDragTargetTrack = -1;
                g_clipDragTargetStart = 0;
                g_clipDragOffsetSamples = 0;
                g_clipDragMoved = false;
                ui.consumeClick = true;
            } else {
                const MidiClip& draggingClip = midi.tracks[static_cast<size_t>(g_clipDragTrack)].clips[static_cast<size_t>(g_clipDragIndex)];
                double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
                double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
                if (windowSamples <= 0.0) windowSamples = 1.0;
                double cursorT = (laneRight > laneLeft)
                    ? (static_cast<double>(ui.cursorX) - laneLeft) / (laneRight - laneLeft)
                    : 0.0;
                cursorT = std::clamp(cursorT, 0.0, 1.0);
                int64_t targetSample = static_cast<int64_t>(std::llround(offsetSamples + cursorT * windowSamples))
                    - g_clipDragOffsetSamples;
                if (targetSample < 0) {
                    uint64_t shiftSamples = computeRebaseShiftSamples(daw, targetSample);
                    DawTimelineRebaseLogic::ShiftTimelineRight(baseSystem, shiftSamples);
                    targetSample += static_cast<int64_t>(shiftSamples);
                }

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

                int targetTrack = g_clipDragTrack;
                if (!daw.laneOrder.empty()) {
                    std::vector<int> laneToMidi(static_cast<size_t>(laneCount), -1);
                    for (size_t laneIdx = 0; laneIdx < daw.laneOrder.size() && laneIdx < laneToMidi.size(); ++laneIdx) {
                        const auto& entry = daw.laneOrder[laneIdx];
                        if (entry.type == 1 && entry.trackIndex >= 0 && entry.trackIndex < midiTrackCount) {
                            laneToMidi[laneIdx] = entry.trackIndex;
                        }
                    }
                    int laneIdx = laneIndexFromCursorY(static_cast<float>(ui.cursorY), startY, laneHalfH, rowSpan, laneCount);
                    if (laneIdx >= 0 && laneIdx < static_cast<int>(laneToMidi.size()) && laneToMidi[static_cast<size_t>(laneIdx)] >= 0) {
                        targetTrack = laneToMidi[static_cast<size_t>(laneIdx)];
                    }
                } else {
                    int laneIdx = laneIndexFromCursorY(static_cast<float>(ui.cursorY), startY, laneHalfH, rowSpan, midiTrackCount);
                    if (laneIdx >= 0 && laneIdx < midiTrackCount) {
                        targetTrack = laneIdx;
                    }
                }
                if (targetTrack < 0 || targetTrack >= midiTrackCount) {
                    targetTrack = g_clipDragTrack;
                }

                g_clipDragTargetTrack = targetTrack;
                g_clipDragTargetStart = static_cast<uint64_t>(targetSample);

                if (!g_clipDragMoved) {
                    float dx = std::fabs(static_cast<float>(ui.cursorX) - g_clipDragStartX);
                    float dy = std::fabs(static_cast<float>(ui.cursorY) - g_clipDragStartY);
                    if (dx > 3.0f || dy > 3.0f
                        || g_clipDragTargetTrack != g_clipDragTrack
                        || g_clipDragTargetStart != draggingClip.startSample) {
                        g_clipDragMoved = true;
                    }
                }
                ui.consumeClick = true;
            }
        }

        if (g_clipTrimActive) {
            trimCursorWanted = true;
            bool validTrimClip = g_clipTrimTrack >= 0
                && g_clipTrimTrack < midiTrackCount
                && g_clipTrimIndex >= 0
                && g_clipTrimIndex < static_cast<int>(midi.tracks[static_cast<size_t>(g_clipTrimTrack)].clips.size());
            if (!validTrimClip) {
                g_clipTrimActive = false;
                g_clipTrimTrack = -1;
                g_clipTrimIndex = -1;
            } else if (!ui.uiLeftDown) {
                MidiTrack& track = midi.tracks[static_cast<size_t>(g_clipTrimTrack)];
                MidiClip& clip = track.clips[static_cast<size_t>(g_clipTrimIndex)];
                applyTrimToMidiClip(clip, g_clipTrimTargetStart, g_clipTrimTargetLength);
                g_clipTrimActive = false;
                g_clipTrimTrack = -1;
                g_clipTrimIndex = -1;
                ui.consumeClick = true;
            } else {
                MidiTrack& track = midi.tracks[static_cast<size_t>(g_clipTrimTrack)];
                uint64_t origStart = g_clipTrimOriginalStart;
                uint64_t origLength = g_clipTrimOriginalLength;
                uint64_t origEnd = origStart + origLength;
                uint64_t prevEnd = 0;
                uint64_t nextStart = UINT64_MAX;
                for (size_t i = 0; i < track.clips.size(); ++i) {
                    if (static_cast<int>(i) == g_clipTrimIndex) continue;
                    const MidiClip& other = track.clips[i];
                    if (other.length == 0) continue;
                    uint64_t otherStart = other.startSample;
                    uint64_t otherEnd = other.startSample + other.length;
                    if (otherEnd <= origStart) {
                        prevEnd = std::max(prevEnd, otherEnd);
                    } else if (otherStart >= origEnd) {
                        nextStart = std::min(nextStart, otherStart);
                    }
                }

                bool cmdDown = isCommandDown(win);
                uint64_t cursorSample = sampleFromCursorX(baseSystem,
                                                          daw,
                                                          laneLeft,
                                                          laneRight,
                                                          secondsPerScreen,
                                                          ui.cursorX,
                                                          !cmdDown);
                if (g_clipTrimLeftEdge) {
                    uint64_t minStart = prevEnd;
                    uint64_t maxStart = (origLength > kMinClipSamples)
                        ? (origStart + origLength - kMinClipSamples)
                        : origStart;
                    if (maxStart < minStart) maxStart = minStart;
                    uint64_t newStart = std::clamp(cursorSample, minStart, maxStart);
                    g_clipTrimTargetStart = newStart;
                    g_clipTrimTargetLength = std::max<uint64_t>(kMinClipSamples, origEnd - newStart);
                } else {
                    uint64_t minEnd = origStart + kMinClipSamples;
                    uint64_t maxEnd = (nextStart == UINT64_MAX) ? UINT64_MAX : nextStart;
                    if (maxEnd < minEnd) maxEnd = minEnd;
                    uint64_t newEnd = std::clamp(cursorSample, minEnd, maxEnd);
                    g_clipTrimTargetStart = origStart;
                    g_clipTrimTargetLength = std::max<uint64_t>(kMinClipSamples, newEnd - origStart);
                }
                ui.consumeClick = true;
            }
        }

        auto buildLanes = [&](int previewSlot) {
            g_cachedWidth = windowWidth;
            g_cachedHeight = windowHeight;
            g_cachedScrollY = scrollY;
            g_cachedTrackCount = midiTrackCount;
            g_cachedAudioTrackCount = audioTrackCount;
            g_cachedTimelineOffset = daw.timelineOffsetSamples;
            g_cachedSecondsPerScreen = secondsPerScreen;
            g_cachedLaneHeight = laneLayout.laneHeight;
            g_cachedThemeRevision = daw.themeRevision;
            g_laneVertices.clear();
            g_laneVertices.reserve(static_cast<size_t>(midiTrackCount) * 60 * 6);
            const bool previewingMidiLaneDrag = (previewSlot >= 0
                && daw.dragActive
                && daw.dragLaneType == 1
                && daw.dragLaneIndex >= 0);
            const int draggedLaneIndex = daw.dragLaneIndex;
            auto computeDisplayIndex = [&](int laneIndex) -> int {
                int displayIndex = laneIndex;
                if (previewSlot < 0) return displayIndex;
                if (previewingMidiLaneDrag) {
                    if (laneIndex == draggedLaneIndex) return -1;
                    if (laneIndex > draggedLaneIndex) {
                        displayIndex -= 1;
                    }
                }
                if (displayIndex >= previewSlot) {
                    displayIndex += 1;
                }
                return displayIndex;
            };

            for (int t = 0; t < midiTrackCount; ++t) {
                int laneIndex = midiLaneIndex[static_cast<size_t>(t)];
                if (laneIndex < 0) continue;
                int displayIndex = computeDisplayIndex(laneIndex);
                if (displayIndex < 0) continue;
                float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                float top = centerY - laneHalfH;
                float bottom = centerY + laneHalfH;

                float bevelDepth = 6.0f;
                glm::vec2 frontA(laneLeft, top);
                glm::vec2 frontB(laneRight, top);
                glm::vec2 frontC(laneRight, bottom);
                glm::vec2 frontD(laneLeft, bottom);

                glm::vec2 topA = frontA;
                glm::vec2 topB = frontB;
                glm::vec2 topC(frontB.x - bevelDepth, frontB.y - bevelDepth);
                glm::vec2 topD(frontA.x - bevelDepth, frontA.y - bevelDepth);

                glm::vec2 leftA = frontA;
                glm::vec2 leftB = frontD;
                glm::vec2 leftC(frontD.x - bevelDepth, frontD.y - bevelDepth);
                glm::vec2 leftD(frontA.x - bevelDepth, frontA.y - bevelDepth);

                pushQuad(g_laneVertices,
                         pixelToNDC(frontA, screenWidth, screenHeight),
                         pixelToNDC(frontB, screenWidth, screenHeight),
                         pixelToNDC(frontC, screenWidth, screenHeight),
                         pixelToNDC(frontD, screenWidth, screenHeight),
                         laneColor);
                pushQuad(g_laneVertices,
                         pixelToNDC(topA, screenWidth, screenHeight),
                         pixelToNDC(topB, screenWidth, screenHeight),
                         pixelToNDC(topC, screenWidth, screenHeight),
                         pixelToNDC(topD, screenWidth, screenHeight),
                         laneHighlight);
                pushQuad(g_laneVertices,
                         pixelToNDC(leftA, screenWidth, screenHeight),
                         pixelToNDC(leftB, screenWidth, screenHeight),
                         pixelToNDC(leftC, screenWidth, screenHeight),
                         pixelToNDC(leftD, screenWidth, screenHeight),
                         laneShadow);
            }

            double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
            double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
            if (windowSamples < 0.0) windowSamples = 0.0;
            glm::vec3 clipColor = glm::clamp(laneHighlight + glm::vec3(0.12f), glm::vec3(0.0f), glm::vec3(1.0f));
            glm::vec3 clipLipColor = glm::clamp(clipColor - glm::vec3(0.09f), glm::vec3(0.0f), glm::vec3(1.0f));
            glm::vec3 selectedClipColor = glm::clamp(clipColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
            auto itSelectedClip = world.colorLibrary.find("MiraLaneSelected");
            if (itSelectedClip != world.colorLibrary.end()) {
                selectedClipColor = itSelectedClip->second;
            }
            glm::vec3 selectedClipLipColor = glm::clamp(selectedClipColor - glm::vec3(0.09f), glm::vec3(0.0f), glm::vec3(1.0f));
            double bpmNow = daw.bpm.load(std::memory_order_relaxed);
            if (bpmNow <= 0.0) bpmNow = 120.0;
            uint64_t beatStepSamples = std::max<uint64_t>(1,
                static_cast<uint64_t>(std::llround((60.0 / bpmNow) * static_cast<double>(daw.sampleRate))));
            for (int t = 0; t < midiTrackCount; ++t) {
                const auto& clips = midi.tracks[static_cast<size_t>(t)].clips;
                if (clips.empty()) continue;
                int laneIndex = midiLaneIndex[static_cast<size_t>(t)];
                if (laneIndex < 0) continue;
                int displayIndex = computeDisplayIndex(laneIndex);
                if (displayIndex < 0) continue;
                float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                float top = 0.0f;
                float bottom = 0.0f;
                float lipBottom = 0.0f;
                computeClipRect(centerY, laneHalfH, top, bottom, lipBottom);
                for (size_t ci = 0; ci < clips.size(); ++ci) {
                    const auto& clip = clips[ci];
                    if (clip.length == 0) continue;
                    bool selectedClip = (t == midi.selectedClipTrack) && (static_cast<int>(ci) == midi.selectedClipIndex);
                    const glm::vec3& bodyColor = selectedClip ? selectedClipColor : clipColor;
                    const glm::vec3& lipColor = selectedClip ? selectedClipLipColor : clipLipColor;
                    double clipStart = static_cast<double>(clip.startSample);
                    double clipEnd = static_cast<double>(clip.startSample + clip.length);
                    if (clipEnd <= offsetSamples || clipStart >= offsetSamples + windowSamples) continue;
                    double visibleStart = std::max(clipStart, offsetSamples);
                    double visibleEnd = std::min(clipEnd, offsetSamples + windowSamples);
                    float t0 = (windowSamples > 0.0)
                        ? static_cast<float>((visibleStart - offsetSamples) / windowSamples)
                        : 0.0f;
                    float t1 = (windowSamples > 0.0)
                        ? static_cast<float>((visibleEnd - offsetSamples) / windowSamples)
                        : 0.0f;
                    float x0 = laneLeft + (laneRight - laneLeft) * t0 - kClipHorizontalPad;
                    float x1 = laneLeft + (laneRight - laneLeft) * t1 + kClipHorizontalPad;
                    x0 = std::max(x0, laneLeft);
                    x1 = std::min(x1, laneRight);
                    if (x1 < laneLeft || x0 > laneRight) continue;
                    glm::vec2 a(x0, top);
                    glm::vec2 b(x1, top);
                    glm::vec2 c(x1, bottom);
                    glm::vec2 d(x0, bottom);
                    pushQuad(g_laneVertices,
                             pixelToNDC(a, screenWidth, screenHeight),
                             pixelToNDC(b, screenWidth, screenHeight),
                             pixelToNDC(c, screenWidth, screenHeight),
                             pixelToNDC(d, screenWidth, screenHeight),
                             bodyColor);
                    if (lipBottom > top + 0.5f) {
                        glm::vec2 la(x0, top);
                        glm::vec2 lb(x1, top);
                        glm::vec2 lc(x1, lipBottom);
                        glm::vec2 ld(x0, lipBottom);
                        pushQuad(g_laneVertices,
                                 pixelToNDC(la, screenWidth, screenHeight),
                                 pixelToNDC(lb, screenWidth, screenHeight),
                                 pixelToNDC(lc, screenWidth, screenHeight),
                                 pixelToNDC(ld, screenWidth, screenHeight),
                                 lipColor);
                    }
                    if (beatStepSamples > 0 && x1 > x0 + 2.0f) {
                        uint64_t visibleStartSample = static_cast<uint64_t>(std::max(visibleStart, 0.0));
                        uint64_t visibleEndSample = static_cast<uint64_t>(std::max(visibleEnd, 0.0));
                        if (visibleEndSample > visibleStartSample) {
                            const uint64_t clipStartSample = clip.startSample;
                            uint64_t tick = clipStartSample;
                            if (visibleStartSample > clipStartSample) {
                                uint64_t delta = visibleStartSample - clipStartSample;
                                uint64_t steps = (delta + beatStepSamples - 1) / beatStepSamples;
                                tick = clipStartSample + steps * beatStepSamples;
                            }
                            int guard = 0;
                            float markerTop = lipBottom + 1.0f;
                            float maxTickHeight = std::max(3.0f, bottom - markerTop - 2.0f);
                            float beatTickHeight = std::clamp(maxTickHeight * 0.30f, 3.0f, 8.0f);
                            float barTickHeight = std::clamp(maxTickHeight * 0.52f, 5.0f, 13.0f);
                            float phraseTickHeight = std::clamp(maxTickHeight * 0.68f, 7.0f, 16.0f);
                            float lastLabelX = -10000.0f;
                            while (tick < visibleEndSample && guard < 512) {
                                float tt = static_cast<float>((static_cast<double>(tick) - offsetSamples) / windowSamples);
                                float x = laneLeft + (laneRight - laneLeft) * tt;
                                x = std::clamp(x, x0 + 1.0f, x1 - 1.0f);
                                uint64_t localBeat = (tick >= clipStartSample)
                                    ? ((tick - clipStartSample) / beatStepSamples)
                                    : 0;
                                bool isBar = ((localBeat % 4ull) == 0ull);
                                bool isPhrase = ((localBeat % 16ull) == 0ull);
                                float lineHalf = isBar ? 0.9f : 0.45f;
                                glm::vec3 lineColor = isBar ? glm::vec3(0.02f, 0.02f, 0.02f)
                                                            : glm::vec3(0.06f, 0.06f, 0.06f);
                                float markerBottom = markerTop + (isPhrase ? phraseTickHeight : (isBar ? barTickHeight : beatTickHeight));
                                markerBottom = std::min(markerBottom, bottom - 1.0f);
                                pushQuad(g_laneVertices,
                                         pixelToNDC({x - lineHalf, markerTop}, screenWidth, screenHeight),
                                         pixelToNDC({x + lineHalf, markerTop}, screenWidth, screenHeight),
                                         pixelToNDC({x + lineHalf, markerBottom}, screenWidth, screenHeight),
                                         pixelToNDC({x - lineHalf, markerBottom}, screenWidth, screenHeight),
                                         lineColor);
                                if ((x - lastLabelX) >= 46.0f
                                    && (x + 30.0f) <= (x1 - 2.0f)
                                    && (markerBottom + 10.0f) <= (bottom - 2.0f)) {
                                    uint64_t barIndex = (localBeat / 4ull) + 1ull;
                                    uint64_t beatInBar = (localBeat % 4ull) + 1ull;
                                    char label[24];
                                    std::snprintf(label, sizeof(label), "%llu.%llu.1",
                                                  static_cast<unsigned long long>(barIndex),
                                                  static_cast<unsigned long long>(beatInBar));
                                    pushText(g_laneVertices,
                                             x + 2.0f,
                                             markerBottom + 2.0f,
                                             label,
                                             glm::vec3(0.04f, 0.04f, 0.04f),
                                             screenWidth,
                                             screenHeight);
                                    lastLabelX = x;
                                }
                                tick += beatStepSamples;
                                ++guard;
                            }
                        }
                    }
                }
            }

            float waveHeight = laneLayout.laneHeight * 0.8f;
            float ampScale = waveHeight * 0.5f;
            int pixelWidth = static_cast<int>(laneRight - laneLeft);
            if (pixelWidth > 0) {
                for (int t = 0; t < midiTrackCount; ++t) {
                    const MidiTrack& track = midi.tracks[static_cast<size_t>(t)];
                    if (track.waveformMin.empty()) continue;
                    size_t blockCount = track.waveformMin.size();
                    if (blockCount == 0) continue;
                    int laneIndex = midiLaneIndex[static_cast<size_t>(t)];
                    if (laneIndex < 0) continue;
                    int displayIndex = computeDisplayIndex(laneIndex);
                    if (displayIndex < 0) continue;
                    float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                    float top = centerY - laneHalfH;
                    float bottom = centerY + laneHalfH;
                    for (int px = 0; px < pixelWidth; ++px) {
                        double t0 = static_cast<double>(px) / static_cast<double>(pixelWidth);
                        double t1 = static_cast<double>(px + 1) / static_cast<double>(pixelWidth);
                        double s0 = offsetSamples + t0 * windowSamples;
                        double s1 = offsetSamples + t1 * windowSamples;
                        size_t block0 = static_cast<size_t>(s0 / static_cast<double>(kWaveformBlockSize));
                        size_t block1 = static_cast<size_t>(s1 / static_cast<double>(kWaveformBlockSize));
                        if (block0 >= blockCount) continue;
                        block1 = std::min(block1, blockCount - 1);

                        float minVal = 0.0f;
                        float maxVal = 0.0f;
                        glm::vec3 color = waveformBaseColor;
                        bool init = false;
                        for (size_t b = block0; b <= block1; ++b) {
                            float minB = track.waveformMin[b];
                            float maxB = track.waveformMax[b];
                            if (!init) {
                                minVal = minB;
                                maxVal = maxB;
                                color = track.waveformColor[b];
                                init = true;
                            } else {
                                minVal = std::min(minVal, minB);
                                maxVal = std::max(maxVal, maxB);
                                color = (color + track.waveformColor[b]) * 0.5f;
                            }
                        }
                        if (!init) continue;
                        float x = laneLeft + static_cast<float>(px);
                        float yCenter = centerY;
                        float yMin = yCenter + minVal * ampScale;
                        float yMax = yCenter + maxVal * ampScale;
                        if (yMax < yMin) std::swap(yMax, yMin);
                        yMin = std::clamp(yMin, top, bottom);
                        yMax = std::clamp(yMax, top, bottom);
                        glm::vec2 a(x, yMin);
                        glm::vec2 b(x + 1.0f, yMin);
                        glm::vec2 c(x + 1.0f, yMax);
                        glm::vec2 d(x, yMax);
                        pushQuad(g_laneVertices,
                                 pixelToNDC(a, screenWidth, screenHeight),
                                 pixelToNDC(b, screenWidth, screenHeight),
                                 pixelToNDC(c, screenWidth, screenHeight),
                                 pixelToNDC(d, screenWidth, screenHeight),
                                 color);
                    }
                }
            }

            for (int t = 0; t < midiTrackCount; ++t) {
                const auto& clips = midi.tracks[static_cast<size_t>(t)].clips;
                if (clips.empty()) continue;
                int laneIndex = midiLaneIndex[static_cast<size_t>(t)];
                if (laneIndex < 0) continue;
                int displayIndex = computeDisplayIndex(laneIndex);
                if (displayIndex < 0) continue;
                float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                float top = 0.0f;
                float bottom = 0.0f;
                float lipBottom = 0.0f;
                computeClipRect(centerY, laneHalfH, top, bottom, lipBottom);
                float noteTop = std::min(bottom, lipBottom + 1.0f);
                float noteBottom = std::max(noteTop + 1.0f, bottom - 1.0f);
                float noteHeight = noteBottom - noteTop;
                if (noteHeight <= 0.5f) continue;

                for (const auto& clip : clips) {
                    if (clip.length == 0 || clip.notes.empty()) continue;
                    double clipStart = static_cast<double>(clip.startSample);
                    double clipEnd = static_cast<double>(clip.startSample + clip.length);
                    if (clipEnd <= offsetSamples || clipStart >= offsetSamples + windowSamples) continue;
                    double clipVisibleStart = std::max(clipStart, offsetSamples);
                    double clipVisibleEnd = std::min(clipEnd, offsetSamples + windowSamples);
                    float clipT0 = static_cast<float>((clipVisibleStart - offsetSamples) / windowSamples);
                    float clipT1 = static_cast<float>((clipVisibleEnd - offsetSamples) / windowSamples);
                    float clipX0 = laneLeft + (laneRight - laneLeft) * clipT0;
                    float clipX1 = laneLeft + (laneRight - laneLeft) * clipT1;
                    clipX0 = std::max(clipX0, laneLeft);
                    clipX1 = std::min(clipX1, laneRight);
                    if (clipX1 <= clipX0) continue;

                    for (const auto& note : clip.notes) {
                        if (note.length == 0) continue;
                        double noteStart = static_cast<double>(clip.startSample + note.startSample);
                        double noteEnd = noteStart + static_cast<double>(note.length);
                        if (noteEnd <= offsetSamples || noteStart >= offsetSamples + windowSamples) continue;
                        double noteVisibleStart = std::max(noteStart, offsetSamples);
                        double noteVisibleEnd = std::min(noteEnd, offsetSamples + windowSamples);
                        float noteT0 = static_cast<float>((noteVisibleStart - offsetSamples) / windowSamples);
                        float noteT1 = static_cast<float>((noteVisibleEnd - offsetSamples) / windowSamples);
                        float x0 = laneLeft + (laneRight - laneLeft) * noteT0;
                        float x1 = laneLeft + (laneRight - laneLeft) * noteT1;
                        x0 = std::clamp(x0, clipX0 + 1.0f, clipX1 - 1.0f);
                        x1 = std::clamp(x1, clipX0 + 1.0f, clipX1 - 1.0f);
                        if (x1 <= x0 + 0.5f) continue;

                        int row = std::clamp(note.pitch - kPreviewBasePitch, 0, kPreviewRows - 1);
                        float rowT0 = static_cast<float>(row) / static_cast<float>(kPreviewRows);
                        float rowT1 = static_cast<float>(row + 1) / static_cast<float>(kPreviewRows);
                        float y0 = noteBottom - rowT1 * noteHeight;
                        float y1 = noteBottom - rowT0 * noteHeight;
                        if (y1 < y0 + 1.0f) y1 = y0 + 1.0f;
                        if (y0 < noteTop) y0 = noteTop;
                        if (y1 > noteBottom) y1 = noteBottom;
                        if (y1 <= y0) continue;

                        int noteIndex = note.pitch % 12;
                        if (noteIndex < 0) noteIndex += 12;
                        float nr = 0.75f, ng = 0.85f, nb = 0.9f;
                        PianoRollResourceSystemLogic::NoteColor(noteIndex, nr, ng, nb);
                        glm::vec3 noteColor(nr, ng, nb);
                        pushQuad(g_laneVertices,
                                 pixelToNDC({x0, y0}, screenWidth, screenHeight),
                                 pixelToNDC({x1, y0}, screenWidth, screenHeight),
                                 pixelToNDC({x1, y1}, screenWidth, screenHeight),
                                 pixelToNDC({x0, y1}, screenWidth, screenHeight),
                                 noteColor);
                    }
                }
            }

            {
                float handleSize = std::min(kTrackHandleSize, std::max(14.0f, laneLayout.laneHeight));
                float handleHalf = handleSize * 0.5f;
                float handleCenterX = laneRight + kTrackHandleInset + handleHalf;
                float minCenterX = laneLeft + 4.0f + handleHalf;
                if (handleCenterX < minCenterX) handleCenterX = minCenterX;
                glm::vec3 handleFront = glm::clamp(laneColor + glm::vec3(0.03f), glm::vec3(0.0f), glm::vec3(1.0f));
                glm::vec3 handleTop = glm::clamp(laneHighlight + glm::vec3(0.02f), glm::vec3(0.0f), glm::vec3(1.0f));
                glm::vec3 handleSide = laneShadow;
                float handleDepth = std::min(6.0f, handleHalf * 0.5f);
                for (int t = 0; t < midiTrackCount; ++t) {
                    int laneIndex = midiLaneIndex[static_cast<size_t>(t)];
                    if (laneIndex < 0) continue;
                    int displayIndex = computeDisplayIndex(laneIndex);
                    if (displayIndex < 0) continue;
                    float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                    float top = centerY - handleHalf;
                    float bottom = centerY + handleHalf;
                    glm::vec2 frontA(handleCenterX - handleHalf, top);
                    glm::vec2 frontB(handleCenterX + handleHalf, top);
                    glm::vec2 frontC(handleCenterX + handleHalf, bottom);
                    glm::vec2 frontD(handleCenterX - handleHalf, bottom);
                    glm::vec2 topA = frontA;
                    glm::vec2 topB = frontB;
                    glm::vec2 topC(frontB.x - handleDepth, frontB.y - handleDepth);
                    glm::vec2 topD(frontA.x - handleDepth, frontA.y - handleDepth);
                    glm::vec2 leftA = frontA;
                    glm::vec2 leftB = frontD;
                    glm::vec2 leftC(frontD.x - handleDepth, frontD.y - handleDepth);
                    glm::vec2 leftD(frontA.x - handleDepth, frontA.y - handleDepth);
                    pushQuad(g_laneVertices,
                             pixelToNDC(frontA, screenWidth, screenHeight),
                             pixelToNDC(frontB, screenWidth, screenHeight),
                             pixelToNDC(frontC, screenWidth, screenHeight),
                             pixelToNDC(frontD, screenWidth, screenHeight),
                             handleFront);
                    pushQuad(g_laneVertices,
                             pixelToNDC(topA, screenWidth, screenHeight),
                             pixelToNDC(topB, screenWidth, screenHeight),
                             pixelToNDC(topC, screenWidth, screenHeight),
                             pixelToNDC(topD, screenWidth, screenHeight),
                             handleTop);
                    pushQuad(g_laneVertices,
                             pixelToNDC(leftA, screenWidth, screenHeight),
                             pixelToNDC(leftB, screenWidth, screenHeight),
                             pixelToNDC(leftC, screenWidth, screenHeight),
                             pixelToNDC(leftD, screenWidth, screenHeight),
                             handleSide);
                }
            }

            g_staticVertexCount = g_laneVertices.size();
            for (int t = 0; t < midiTrackCount; ++t) {
                g_waveVersions[static_cast<size_t>(t)] = midi.tracks[static_cast<size_t>(t)].waveformVersion;
            }
        };

        if (rebuildStatic) {
            buildLanes(-1);
        }

        int previewSlot = -1;
        if (daw.dragActive && daw.dragLaneType == 1) {
            previewSlot = daw.dragDropIndex;
        } else if (daw.externalDropActive && daw.externalDropType == 1) {
            previewSlot = daw.externalDropIndex;
        }
        const bool previewingMidiLaneDrag = (previewSlot >= 0
            && daw.dragActive
            && daw.dragLaneType == 1
            && daw.dragLaneIndex >= 0);
        const int draggedLaneIndex = daw.dragLaneIndex;
        auto computeDisplayIndex = [&](int laneIndex) -> int {
            int displayIndex = laneIndex;
            if (previewSlot < 0) return displayIndex;
            if (previewingMidiLaneDrag) {
                if (laneIndex == draggedLaneIndex) return -1;
                if (laneIndex > draggedLaneIndex) {
                    displayIndex -= 1;
                }
            }
            if (displayIndex >= previewSlot) {
                displayIndex += 1;
            }
            return displayIndex;
        };
        if (previewSlot >= 0) {
            buildLanes(previewSlot);
        } else if (g_cachedPreviewSlot >= 0 && !rebuildStatic) {
            // Preview ended; refresh cached static lane vertices immediately.
            buildLanes(-1);
        }
        g_cachedPreviewSlot = previewSlot;

        if (g_laneVertices.empty()) return;

        g_laneVertices.resize(g_staticVertexCount);
        glm::vec3 selectedColor(0.45f, 0.72f, 1.0f);
        auto itSelected = world.colorLibrary.find("MiraLaneSelected");
        if (itSelected != world.colorLibrary.end()) {
            selectedColor = itSelected->second;
        }
        if ((daw.timelineSelectionActive || daw.timelineSelectionDragActive) && laneRight > laneLeft) {
            uint64_t selStart = std::min(daw.timelineSelectionStartSample, daw.timelineSelectionEndSample);
            uint64_t selEnd = std::max(daw.timelineSelectionStartSample, daw.timelineSelectionEndSample);
            int selLaneMin = std::min(daw.timelineSelectionStartLane, daw.timelineSelectionEndLane);
            int selLaneMax = std::max(daw.timelineSelectionStartLane, daw.timelineSelectionEndLane);
            int midiLaneMin = INT_MAX;
            int midiLaneMax = -1;
            if (!daw.laneOrder.empty()) {
                selLaneMin = std::max(0, selLaneMin);
                selLaneMax = std::min(selLaneMax, static_cast<int>(daw.laneOrder.size()) - 1);
                for (int lane = selLaneMin; lane <= selLaneMax; ++lane) {
                    const auto& entry = daw.laneOrder[static_cast<size_t>(lane)];
                    if (entry.type != 1) continue;
                    midiLaneMin = std::min(midiLaneMin, lane);
                    midiLaneMax = std::max(midiLaneMax, lane);
                }
            } else {
                int firstMidiLane = audioTrackCount;
                int lastMidiLane = firstMidiLane + midiTrackCount - 1;
                midiLaneMin = std::max(selLaneMin, firstMidiLane);
                midiLaneMax = std::min(selLaneMax, lastMidiLane);
            }
            if (selEnd > selStart && midiLaneMin <= midiLaneMax) {
                double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
                double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
                if (windowSamples <= 0.0) windowSamples = 1.0;
                if (static_cast<double>(selEnd) > offsetSamples
                    && static_cast<double>(selStart) < offsetSamples + windowSamples) {
                    float t0 = static_cast<float>((static_cast<double>(selStart) - offsetSamples) / windowSamples);
                    float t1 = static_cast<float>((static_cast<double>(selEnd) - offsetSamples) / windowSamples);
                    float x0 = laneLeft + (laneRight - laneLeft) * t0;
                    float x1 = laneLeft + (laneRight - laneLeft) * t1;
                    x0 = std::clamp(x0, laneLeft, laneRight);
                    x1 = std::clamp(x1, laneLeft, laneRight);
                    if (x1 < x0) std::swap(x0, x1);
                    float topMin = 0.0f;
                    float bottomMin = 0.0f;
                    float lipBottomMin = 0.0f;
                    float topMax = 0.0f;
                    float bottomMax = 0.0f;
                    float lipBottomMax = 0.0f;
                    computeClipRect(startY + static_cast<float>(midiLaneMin) * rowSpan, laneHalfH, topMin, bottomMin, lipBottomMin);
                    computeClipRect(startY + static_cast<float>(midiLaneMax) * rowSpan, laneHalfH, topMax, bottomMax, lipBottomMax);
                    float y0 = lipBottomMin;
                    float y1 = bottomMax;
                    glm::vec3 fillColor = glm::clamp(selectedColor * 0.35f, glm::vec3(0.0f), glm::vec3(1.0f));
                    glm::vec3 edgeColor = glm::clamp(selectedColor + glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f));
                    pushQuad(g_laneVertices,
                             pixelToNDC({x0, y0}, screenWidth, screenHeight),
                             pixelToNDC({x1, y0}, screenWidth, screenHeight),
                             pixelToNDC({x1, y1}, screenWidth, screenHeight),
                             pixelToNDC({x0, y1}, screenWidth, screenHeight),
                             fillColor);
                    float edge = 1.0f;
                    pushQuad(g_laneVertices,
                             pixelToNDC({x0 - edge, y0}, screenWidth, screenHeight),
                             pixelToNDC({x0 + edge, y0}, screenWidth, screenHeight),
                             pixelToNDC({x0 + edge, y1}, screenWidth, screenHeight),
                             pixelToNDC({x0 - edge, y1}, screenWidth, screenHeight),
                             edgeColor);
                    pushQuad(g_laneVertices,
                             pixelToNDC({x1 - edge, y0}, screenWidth, screenHeight),
                             pixelToNDC({x1 + edge, y0}, screenWidth, screenHeight),
                             pixelToNDC({x1 + edge, y1}, screenWidth, screenHeight),
                             pixelToNDC({x1 - edge, y1}, screenWidth, screenHeight),
                             edgeColor);
                }
            }
        }
        {
            float rowHeight = takeRowHeight(laneHalfH);
            double offsetSamples3 = static_cast<double>(daw.timelineOffsetSamples);
            double windowSamples3 = secondsPerScreen * static_cast<double>(daw.sampleRate);
            if (windowSamples3 <= 0.0) windowSamples3 = 1.0;
            glm::vec3 rowBg(0.07f, 0.07f, 0.07f);
            glm::vec3 rowClip(0.17f, 0.17f, 0.17f);
            glm::vec3 rowComp = glm::clamp(selectedColor * 0.72f, glm::vec3(0.0f), glm::vec3(1.0f));
            glm::vec3 rowPreview = glm::clamp(selectedColor + glm::vec3(0.04f), glm::vec3(0.0f), glm::vec3(1.0f));
            for (int t = 0; t < midiTrackCount; ++t) {
                if (t < 0 || t >= static_cast<int>(midi.tracks.size())) continue;
                const MidiTrack& track = midi.tracks[static_cast<size_t>(t)];
                if (!track.takeStackExpanded || track.loopTakeClips.empty()) continue;
                int laneIndex = (t >= 0 && t < static_cast<int>(midiLaneIndex.size()))
                    ? midiLaneIndex[static_cast<size_t>(t)]
                    : -1;
                if (laneIndex < 0) continue;
                int displayIndex = computeDisplayIndex(laneIndex);
                if (displayIndex < 0) continue;
                float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                float top = 0.0f;
                float bottom = 0.0f;
                float lipBottom = 0.0f;
                computeClipRect(centerY, laneHalfH, top, bottom, lipBottom);
                (void)top;
                (void)lipBottom;
                float rowStartY = bottom + kTakeRowGap;
                for (size_t i = 0; i < track.loopTakeClips.size(); ++i) {
                    const MidiClip& take = track.loopTakeClips[i];
                    if (take.length == 0) continue;
                    float y0 = rowStartY + static_cast<float>(i) * (rowHeight + kTakeRowSpacing);
                    float y1 = y0 + rowHeight;
                    if (y1 < laneLayout.topBound || y0 > laneLayout.visualBottomBound) continue;
                    pushQuad(g_laneVertices,
                             pixelToNDC({laneLeft, y0}, screenWidth, screenHeight),
                             pixelToNDC({laneRight, y0}, screenWidth, screenHeight),
                             pixelToNDC({laneRight, y1}, screenWidth, screenHeight),
                             pixelToNDC({laneLeft, y1}, screenWidth, screenHeight),
                             rowBg);
                    double takeStart = static_cast<double>(take.startSample);
                    double takeEnd = static_cast<double>(take.startSample + take.length);
                    if (takeEnd > offsetSamples3 && takeStart < offsetSamples3 + windowSamples3) {
                        double visStart = std::max(takeStart, offsetSamples3);
                        double visEnd = std::min(takeEnd, offsetSamples3 + windowSamples3);
                        float t0 = static_cast<float>((visStart - offsetSamples3) / windowSamples3);
                        float t1 = static_cast<float>((visEnd - offsetSamples3) / windowSamples3);
                        float x0 = laneLeft + (laneRight - laneLeft) * t0;
                        float x1 = laneLeft + (laneRight - laneLeft) * t1;
                        x0 = std::clamp(x0, laneLeft, laneRight);
                        x1 = std::clamp(x1, laneLeft, laneRight);
                        if (x1 > x0) {
                            pushQuad(g_laneVertices,
                                     pixelToNDC({x0, y0 + 1.0f}, screenWidth, screenHeight),
                                     pixelToNDC({x1, y0 + 1.0f}, screenWidth, screenHeight),
                                     pixelToNDC({x1, y1 - 1.0f}, screenWidth, screenHeight),
                                     pixelToNDC({x0, y1 - 1.0f}, screenWidth, screenHeight),
                                     rowClip);
                        }
                    }
                    if (take.takeId >= 0) {
                        for (const auto& clip : track.clips) {
                            if (clip.length == 0) continue;
                            if (clip.takeId != take.takeId) continue;
                            uint64_t overlapStart = std::max(clip.startSample, take.startSample);
                            uint64_t overlapEnd = std::min(clip.startSample + clip.length, take.startSample + take.length);
                            if (overlapEnd <= overlapStart) continue;
                            double visStart = std::max<double>(static_cast<double>(overlapStart), offsetSamples3);
                            double visEnd = std::min<double>(static_cast<double>(overlapEnd), offsetSamples3 + windowSamples3);
                            if (visEnd <= visStart) continue;
                            float t0 = static_cast<float>((visStart - offsetSamples3) / windowSamples3);
                            float t1 = static_cast<float>((visEnd - offsetSamples3) / windowSamples3);
                            float x0 = laneLeft + (laneRight - laneLeft) * t0;
                            float x1 = laneLeft + (laneRight - laneLeft) * t1;
                            x0 = std::clamp(x0, laneLeft, laneRight);
                            x1 = std::clamp(x1, laneLeft, laneRight);
                            if (x1 <= x0) continue;
                            pushQuad(g_laneVertices,
                                     pixelToNDC({x0, y0 + 2.0f}, screenWidth, screenHeight),
                                     pixelToNDC({x1, y0 + 2.0f}, screenWidth, screenHeight),
                                     pixelToNDC({x1, y1 - 2.0f}, screenWidth, screenHeight),
                                     pixelToNDC({x0, y1 - 2.0f}, screenWidth, screenHeight),
                                     rowComp);
                        }
                    }
                    if (daw.takeCompDragActive
                        && daw.takeCompLaneType == 1
                        && daw.takeCompTrack == t
                        && daw.takeCompTakeIndex == static_cast<int>(i)) {
                        uint64_t selStart = std::min(daw.takeCompStartSample, daw.takeCompEndSample);
                        uint64_t selEnd = std::max(daw.takeCompStartSample, daw.takeCompEndSample);
                        double visStart = std::max<double>(static_cast<double>(selStart), offsetSamples3);
                        double visEnd = std::min<double>(static_cast<double>(selEnd), offsetSamples3 + windowSamples3);
                        if (visEnd > visStart) {
                            float t0 = static_cast<float>((visStart - offsetSamples3) / windowSamples3);
                            float t1 = static_cast<float>((visEnd - offsetSamples3) / windowSamples3);
                            float x0 = laneLeft + (laneRight - laneLeft) * t0;
                            float x1 = laneLeft + (laneRight - laneLeft) * t1;
                            x0 = std::clamp(x0, laneLeft, laneRight);
                            x1 = std::clamp(x1, laneLeft, laneRight);
                            if (x1 > x0) {
                                pushQuad(g_laneVertices,
                                         pixelToNDC({x0, y0}, screenWidth, screenHeight),
                                         pixelToNDC({x1, y0}, screenWidth, screenHeight),
                                         pixelToNDC({x1, y1}, screenWidth, screenHeight),
                                         pixelToNDC({x0, y1}, screenWidth, screenHeight),
                                         rowPreview);
                            }
                        }
                    }
                }
            }
        }
        if (g_clipDragActive && g_clipDragTrack >= 0 && g_clipDragTrack < midiTrackCount
            && g_clipDragIndex >= 0
            && g_clipDragIndex < static_cast<int>(midi.tracks[static_cast<size_t>(g_clipDragTrack)].clips.size())) {
            const MidiClip& clip = midi.tracks[static_cast<size_t>(g_clipDragTrack)].clips[static_cast<size_t>(g_clipDragIndex)];
            int targetTrack = g_clipDragTargetTrack;
            if (targetTrack >= 0 && targetTrack < midiTrackCount) {
                int laneIndex = midiLaneIndex[static_cast<size_t>(targetTrack)];
                if (laneIndex >= 0) {
                    int displayIndex = computeDisplayIndex(laneIndex);
                    if (displayIndex >= 0) {
                        float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                        float top = 0.0f;
                        float bottom = 0.0f;
                        float lipBottom = 0.0f;
                        computeClipRect(centerY, laneHalfH, top, bottom, lipBottom);
                        double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
                        double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
                        if (windowSamples <= 0.0) windowSamples = 1.0;
                        double clipStart = static_cast<double>(g_clipDragTargetStart);
                        double clipEnd = static_cast<double>(g_clipDragTargetStart + clip.length);
                        if (clipEnd > offsetSamples && clipStart < offsetSamples + windowSamples) {
                            double visibleStart = std::max(clipStart, offsetSamples);
                            double visibleEnd = std::min(clipEnd, offsetSamples + windowSamples);
                            float t0 = static_cast<float>((visibleStart - offsetSamples) / windowSamples);
                            float t1 = static_cast<float>((visibleEnd - offsetSamples) / windowSamples);
                            float x0 = laneLeft + (laneRight - laneLeft) * t0 - kClipHorizontalPad;
                            float x1 = laneLeft + (laneRight - laneLeft) * t1 + kClipHorizontalPad;
                            x0 = std::max(x0, laneLeft);
                            x1 = std::min(x1, laneRight);
                            glm::vec3 ghostColor = glm::clamp(selectedColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
                            glm::vec3 ghostLip = glm::clamp(ghostColor - glm::vec3(0.09f), glm::vec3(0.0f), glm::vec3(1.0f));
                            pushQuad(g_laneVertices,
                                     pixelToNDC({x0, top}, screenWidth, screenHeight),
                                     pixelToNDC({x1, top}, screenWidth, screenHeight),
                                     pixelToNDC({x1, bottom}, screenWidth, screenHeight),
                                     pixelToNDC({x0, bottom}, screenWidth, screenHeight),
                                     ghostColor);
                            if (lipBottom > top + 0.5f) {
                                pushQuad(g_laneVertices,
                                         pixelToNDC({x0, top}, screenWidth, screenHeight),
                                         pixelToNDC({x1, top}, screenWidth, screenHeight),
                                         pixelToNDC({x1, lipBottom}, screenWidth, screenHeight),
                                         pixelToNDC({x0, lipBottom}, screenWidth, screenHeight),
                                         ghostLip);
                            }
                        }
                    }
                }
            }
        }
        if (g_clipTrimActive && g_clipTrimTrack >= 0 && g_clipTrimTrack < midiTrackCount
            && g_clipTrimIndex >= 0
            && g_clipTrimIndex < static_cast<int>(midi.tracks[static_cast<size_t>(g_clipTrimTrack)].clips.size())) {
            int laneIndex = midiLaneIndex[static_cast<size_t>(g_clipTrimTrack)];
            if (laneIndex >= 0) {
                int displayIndex = computeDisplayIndex(laneIndex);
                if (displayIndex >= 0) {
                    float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                    float top = 0.0f;
                    float bottom = 0.0f;
                    float lipBottom = 0.0f;
                    computeClipRect(centerY, laneHalfH, top, bottom, lipBottom);
                    double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
                    double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
                    if (windowSamples <= 0.0) windowSamples = 1.0;
                    double clipStart = static_cast<double>(g_clipTrimTargetStart);
                    double clipEnd = static_cast<double>(g_clipTrimTargetStart + g_clipTrimTargetLength);
                    if (clipEnd > offsetSamples && clipStart < offsetSamples + windowSamples) {
                        double visibleStart = std::max(clipStart, offsetSamples);
                        double visibleEnd = std::min(clipEnd, offsetSamples + windowSamples);
                        float t0 = static_cast<float>((visibleStart - offsetSamples) / windowSamples);
                        float t1 = static_cast<float>((visibleEnd - offsetSamples) / windowSamples);
                        float x0 = laneLeft + (laneRight - laneLeft) * t0 - kClipHorizontalPad;
                        float x1 = laneLeft + (laneRight - laneLeft) * t1 + kClipHorizontalPad;
                        x0 = std::max(x0, laneLeft);
                        x1 = std::min(x1, laneRight);
                        glm::vec3 ghostColor = glm::clamp(selectedColor + glm::vec3(0.05f), glm::vec3(0.0f), glm::vec3(1.0f));
                        glm::vec3 ghostLip = glm::clamp(ghostColor - glm::vec3(0.09f), glm::vec3(0.0f), glm::vec3(1.0f));
                        pushQuad(g_laneVertices,
                                 pixelToNDC({x0, top}, screenWidth, screenHeight),
                                 pixelToNDC({x1, top}, screenWidth, screenHeight),
                                 pixelToNDC({x1, bottom}, screenWidth, screenHeight),
                                 pixelToNDC({x0, bottom}, screenWidth, screenHeight),
                                 ghostColor);
                        if (lipBottom > top + 0.5f) {
                            pushQuad(g_laneVertices,
                                     pixelToNDC({x0, top}, screenWidth, screenHeight),
                                     pixelToNDC({x1, top}, screenWidth, screenHeight),
                                     pixelToNDC({x1, lipBottom}, screenWidth, screenHeight),
                                     pixelToNDC({x0, lipBottom}, screenWidth, screenHeight),
                                     ghostLip);
                        }
                    }
                }
            }
        }
        if (daw.selectedLaneType == 1 && daw.selectedLaneIndex >= 0) {
            int displayIndex = computeDisplayIndex(daw.selectedLaneIndex);
            if (displayIndex < 0) {
                displayIndex = -1;
            }
            if (displayIndex >= 0) {
            float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
            float handleSize = std::min(kTrackHandleSize, std::max(14.0f, laneLayout.laneHeight));
            float handleHalf = handleSize * 0.5f;
            float centerX = laneRight + kTrackHandleInset + handleHalf;
            float minCenterX = laneLeft + 4.0f + handleHalf;
            if (centerX < minCenterX) centerX = minCenterX;
            float top = centerY - handleHalf;
            float bottom = centerY + handleHalf;
            glm::vec3 selectedHighlight = glm::clamp(selectedColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
            glm::vec3 selectedShadow = glm::clamp(selectedColor - glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f));
            float bevelDepth = std::min(6.0f, handleHalf * 0.5f);
            glm::vec2 frontA(centerX - handleHalf, top);
            glm::vec2 frontB(centerX + handleHalf, top);
            glm::vec2 frontC(centerX + handleHalf, bottom);
            glm::vec2 frontD(centerX - handleHalf, bottom);
            glm::vec2 topA = frontA;
            glm::vec2 topB = frontB;
            glm::vec2 topC(frontB.x - bevelDepth, frontB.y - bevelDepth);
            glm::vec2 topD(frontA.x - bevelDepth, frontA.y - bevelDepth);
            glm::vec2 leftA = frontA;
            glm::vec2 leftB = frontD;
            glm::vec2 leftC(frontD.x - bevelDepth, frontD.y - bevelDepth);
            glm::vec2 leftD(frontA.x - bevelDepth, frontA.y - bevelDepth);
            pushQuad(g_laneVertices,
                     pixelToNDC(frontA, screenWidth, screenHeight),
                     pixelToNDC(frontB, screenWidth, screenHeight),
                     pixelToNDC(frontC, screenWidth, screenHeight),
                     pixelToNDC(frontD, screenWidth, screenHeight),
                     selectedColor);
            pushQuad(g_laneVertices,
                     pixelToNDC(topA, screenWidth, screenHeight),
                     pixelToNDC(topB, screenWidth, screenHeight),
                     pixelToNDC(topC, screenWidth, screenHeight),
                     pixelToNDC(topD, screenWidth, screenHeight),
                     selectedHighlight);
            pushQuad(g_laneVertices,
                     pixelToNDC(leftA, screenWidth, screenHeight),
                     pixelToNDC(leftB, screenWidth, screenHeight),
                     pixelToNDC(leftC, screenWidth, screenHeight),
                     pixelToNDC(leftD, screenWidth, screenHeight),
                     selectedShadow);
            }
        }
        if ((daw.dragActive && daw.dragLaneType == 1) || (daw.externalDropActive && daw.externalDropType == 1)) {
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
            pushQuad(g_laneVertices,
                     pixelToNDC({centerX - handleHalf, ghostTop}, screenWidth, screenHeight),
                     pixelToNDC({centerX + handleHalf, ghostTop}, screenWidth, screenHeight),
                     pixelToNDC({centerX + handleHalf, ghostBottom}, screenWidth, screenHeight),
                     pixelToNDC({centerX - handleHalf, ghostBottom}, screenWidth, screenHeight),
                     ghostColor);
            pushQuad(g_laneVertices,
                     pixelToNDC({centerX - handleHalf, ghostTop}, screenWidth, screenHeight),
                     pixelToNDC({centerX + handleHalf, ghostTop}, screenWidth, screenHeight),
                     pixelToNDC({centerX + handleHalf - bevelDepth, ghostTop - bevelDepth}, screenWidth, screenHeight),
                     pixelToNDC({centerX - handleHalf - bevelDepth, ghostTop - bevelDepth}, screenWidth, screenHeight),
                     glm::clamp(ghostColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f)));
            pushQuad(g_laneVertices,
                     pixelToNDC({centerX - handleHalf, ghostTop}, screenWidth, screenHeight),
                     pixelToNDC({centerX - handleHalf, ghostBottom}, screenWidth, screenHeight),
                     pixelToNDC({centerX - handleHalf - bevelDepth, ghostBottom - bevelDepth}, screenWidth, screenHeight),
                     pixelToNDC({centerX - handleHalf - bevelDepth, ghostTop - bevelDepth}, screenWidth, screenHeight),
                     glm::clamp(ghostColor - glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f)));
            if (previewSlot >= 0) {
                float insertY = startY + (static_cast<float>(previewSlot) - 0.5f) * rowSpan;
                float lineHalf = 2.0f;
                pushQuad(g_laneVertices,
                         pixelToNDC({laneLeft, insertY - lineHalf}, screenWidth, screenHeight),
                         pixelToNDC({laneRight, insertY - lineHalf}, screenWidth, screenHeight),
                         pixelToNDC({laneRight, insertY + lineHalf}, screenWidth, screenHeight),
                         pixelToNDC({laneLeft, insertY + lineHalf}, screenWidth, screenHeight),
                         selectedColor);
            }
        }

        DawLaneInputSystemLogic::ApplyLaneResizeCursor(win, trimCursorWanted || g_clipTrimActive);

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        float laneAlpha = kLaneAlphaDefault;
        if (baseSystem.daw) {
            laneAlpha = std::clamp(baseSystem.daw->activeThemeBackground.a, 0.0f, 1.0f);
        }
        glBlendColor(0.0f, 0.0f, 0.0f, laneAlpha);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        glBindVertexArray(renderer.uiMidiLaneVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.uiMidiLaneVBO);
        glBufferData(GL_ARRAY_BUFFER, g_laneVertices.size() * sizeof(UiVertex), g_laneVertices.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)(sizeof(glm::vec2)));

        renderer.uiColorShader->use();
        renderer.uiColorShader->setFloat("alpha", laneAlpha);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(g_laneVertices.size()));
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
    }

    void OnTimelineRebased(uint64_t shiftSamples) {
        if (shiftSamples == 0) return;
        auto addWithSaturation = [&](uint64_t& value) {
            if (shiftSamples > (std::numeric_limits<uint64_t>::max() - value)) {
                value = std::numeric_limits<uint64_t>::max();
            } else {
                value += shiftSamples;
            }
        };
        addWithSaturation(g_clipDragTargetStart);
        addWithSaturation(g_clipTrimOriginalStart);
        addWithSaturation(g_clipTrimTargetStart);
    }
}
