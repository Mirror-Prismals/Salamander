#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "stb_easy_font.h"

namespace DawLaneTimelineSystemLogic {
    struct LaneLayout;
    bool hasDawUiWorld(const LevelContext& level);
    LaneLayout ComputeLaneLayout(const BaseSystem& baseSystem, const DawContext& daw, GLFWwindow* win);
    std::vector<int> BuildAudioLaneIndex(const DawContext& daw, int audioTrackCount);
    double GridSecondsForZoom(double secondsPerScreen, double secondsPerBeat);
}

namespace DawLaneResourceSystemLogic {
    namespace {
        constexpr float kLaneAlpha = 0.85f;
        constexpr float kRulerSideInset = -15.0f;
        constexpr float kRulerHeight = 13.0f;
        constexpr float kRulerInset = 10.0f;
        constexpr float kRulerLowerOffset = 0.0f;
        constexpr float kRulerGap = 6.0f;
        constexpr float kTrackHandleSize = 60.0f;
        constexpr float kTrackHandleInset = 12.0f;
        constexpr float kClipHorizontalPad = 2.0f;
        constexpr float kClipVerticalInset = 0.0f;
        constexpr float kClipMinHeight = 2.0f;
        constexpr float kClipLipMinHeight = 6.0f;
        constexpr float kClipLipMaxHeight = 12.0f;
        constexpr size_t kWaveformBlockSize = 256;

        struct LaneRenderCache {
            std::vector<DawLaneTimelineSystemLogic::UiVertex> vertices;
            size_t staticVertexCount = 0;
            size_t totalVertexCount = 0;
            int cachedWidth = 0;
            int cachedHeight = 0;
            float cachedScrollY = 0.0f;
            float cachedStartY = 0.0f;
            float cachedLaneHeight = 60.0f;
            int cachedTrackCount = 0;
            int64_t cachedTimelineOffset = 0;
            double cachedSecondsPerScreen = 10.0;
            double cachedBpm = 120.0;
            std::vector<int> cachedLaneSignature;
            std::vector<uint64_t> waveVersions;
            std::vector<uint64_t> cachedClipSignature;
            int cachedPreviewSlot = -1;
            uint64_t cachedThemeRevision = 0;
        };

        LaneRenderCache g_cache;

        glm::vec2 pixelToNDC(const glm::vec2& pixel, double width, double height) {
            float ndcX = static_cast<float>((pixel.x / width) * 2.0 - 1.0);
            float ndcY = static_cast<float>(1.0 - (pixel.y / height) * 2.0);
            return {ndcX, ndcY};
        }

        void pushQuad(std::vector<DawLaneTimelineSystemLogic::UiVertex>& verts,
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

        void pushText(std::vector<DawLaneTimelineSystemLogic::UiVertex>& verts,
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

        void ensureResources(RendererContext& renderer, WorldContext& world) {
            if (!renderer.uiColorShader) {
                renderer.uiColorShader = std::make_unique<Shader>(world.shaders["UI_COLOR_VERTEX_SHADER"].c_str(), world.shaders["UI_COLOR_FRAGMENT_SHADER"].c_str());
            }
            if (renderer.uiLaneVAO == 0) {
                glGenVertexArrays(1, &renderer.uiLaneVAO);
                glGenBuffers(1, &renderer.uiLaneVBO);
            }
        }

        void buildLanes(BaseSystem& baseSystem,
                        DawContext& daw,
                        WorldContext& world,
                        const DawLaneTimelineSystemLogic::LaneLayout& layout,
                        int previewSlot,
                        double bpm) {
            const int audioTrackCount = layout.audioTrackCount;
            const int laneCount = layout.laneCount;
            const float laneLeft = layout.laneLeft;
            const float laneRight = layout.laneRight;
            const float laneHalfH = layout.laneHalfH;
            const float rowSpan = layout.rowSpan;
            const float startY = layout.startY;
            const float topBound = layout.topBound;
            const float visualBottomBound = layout.visualBottomBound;
            const double secondsPerScreen = layout.secondsPerScreen;
            const bool previewingAudioLaneDrag = (previewSlot >= 0
                && daw.dragActive
                && daw.dragLaneType == 0
                && daw.dragLaneIndex >= 0);
            const int draggedLaneIndex = daw.dragLaneIndex;
            auto computeDisplayIndex = [&](int laneIndex) -> int {
                int displayIndex = laneIndex;
                if (previewSlot < 0) return displayIndex;
                if (previewingAudioLaneDrag) {
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

            g_cache.cachedWidth = static_cast<int>(layout.screenWidth);
            g_cache.cachedHeight = static_cast<int>(layout.screenHeight);
            g_cache.cachedScrollY = layout.scrollY;
            g_cache.cachedStartY = layout.startY;
            g_cache.cachedLaneHeight = layout.laneHeight;
            g_cache.cachedTrackCount = audioTrackCount;
            g_cache.cachedTimelineOffset = daw.timelineOffsetSamples;
            g_cache.cachedSecondsPerScreen = secondsPerScreen;
            g_cache.cachedBpm = bpm;
            g_cache.cachedThemeRevision = daw.themeRevision;
            g_cache.vertices.clear();
            g_cache.vertices.reserve(static_cast<size_t>(audioTrackCount) * 18);

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
            } else {
                auto it = world.colorLibrary.find("DarkGray");
                if (it != world.colorLibrary.end()) {
                    laneColor = glm::clamp(it->second + glm::vec3(0.02f), glm::vec3(0.0f), glm::vec3(1.0f));
                    laneHighlight = glm::clamp(it->second + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
                    laneShadow = glm::clamp(it->second - glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f));
                    waveformBaseColor = glm::clamp(it->second - glm::vec3(0.2f), glm::vec3(0.0f), glm::vec3(1.0f));
                }
            }

            if (daw.laneOrder.empty()) {
                glm::vec3 rulerFront = laneHighlight;
                glm::vec3 rulerTop = glm::clamp(laneHighlight + glm::vec3(0.05f), glm::vec3(0.0f), glm::vec3(1.0f));
                glm::vec3 rulerSide = glm::clamp(laneShadow - glm::vec3(0.02f), glm::vec3(0.0f), glm::vec3(1.0f));
                float rulerTopY = startY - laneHalfH - (kRulerHeight + kRulerInset) + kRulerLowerOffset;
                float rulerBottomY = rulerTopY + kRulerHeight;
                float bevelDepth = 6.0f;
                float rulerLeft = laneLeft + kRulerSideInset;
                float rulerRight = laneRight - kRulerSideInset;
                glm::vec2 rFrontA(rulerLeft, rulerTopY);
                glm::vec2 rFrontB(rulerRight, rulerTopY);
                glm::vec2 rFrontC(rulerRight, rulerBottomY);
                glm::vec2 rFrontD(rulerLeft, rulerBottomY);
                glm::vec2 rTopA = rFrontA;
                glm::vec2 rTopB = rFrontB;
                glm::vec2 rTopC(rFrontB.x - bevelDepth, rFrontB.y - bevelDepth);
                glm::vec2 rTopD(rFrontA.x - bevelDepth, rFrontA.y - bevelDepth);
                glm::vec2 rLeftA = rFrontA;
                glm::vec2 rLeftB = rFrontD;
                glm::vec2 rLeftC(rFrontD.x - bevelDepth, rFrontD.y - bevelDepth);
                glm::vec2 rLeftD(rFrontA.x - bevelDepth, rFrontA.y - bevelDepth);
                pushQuad(g_cache.vertices,
                         pixelToNDC(rFrontA, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(rFrontB, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(rFrontC, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(rFrontD, layout.screenWidth, layout.screenHeight),
                         rulerFront);
                pushQuad(g_cache.vertices,
                         pixelToNDC(rTopA, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(rTopB, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(rTopC, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(rTopD, layout.screenWidth, layout.screenHeight),
                         rulerTop);
                pushQuad(g_cache.vertices,
                         pixelToNDC(rLeftA, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(rLeftB, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(rLeftC, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(rLeftD, layout.screenWidth, layout.screenHeight),
                         rulerSide);
            }

            double secondsPerBeat = (bpm > 0.0) ? (60.0 / bpm) : 0.5;
            double gridSeconds = DawLaneTimelineSystemLogic::GridSecondsForZoom(secondsPerScreen, secondsPerBeat);
            if (gridSeconds > 0.0 && laneRight > laneLeft) {
                double offsetSec = static_cast<double>(daw.timelineOffsetSamples) / static_cast<double>(daw.sampleRate);
                double endSec = offsetSec + secondsPerScreen;
                double firstTick = std::floor(offsetSec / gridSeconds) * gridSeconds;
                glm::vec3 gridColor = glm::clamp(laneShadow + glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f));
                float lineHalf = 0.5f;
                for (double tick = firstTick; tick <= endSec + 0.0001; tick += gridSeconds) {
                    float t = static_cast<float>((tick - offsetSec) / secondsPerScreen);
                    if (t < -0.01f || t > 1.01f) continue;
                    float x = laneLeft + (laneRight - laneLeft) * t;
                    glm::vec3 lineColor = gridColor;
                    double barSeconds = secondsPerBeat * 4.0;
                    if (barSeconds > 0.0) {
                        double barStart = std::floor(tick / barSeconds) * barSeconds;
                        double beatStart = std::floor(tick / secondsPerBeat) * secondsPerBeat;
                        bool isBar = std::abs(tick - barStart) < 1e-6;
                        bool isBeat = std::abs(tick - beatStart) < 1e-6;
                        if (isBar) {
                            lineColor = gridColor;
                        } else if (isBeat) {
                            lineColor = glm::clamp(gridColor * 0.75f, glm::vec3(0.0f), glm::vec3(1.0f));
                        } else {
                            lineColor = glm::clamp(gridColor * 0.5f, glm::vec3(0.0f), glm::vec3(1.0f));
                        }
                    }
                    glm::vec2 a(x - lineHalf, topBound);
                    glm::vec2 b(x + lineHalf, topBound);
                    glm::vec2 c(x + lineHalf, visualBottomBound);
                    glm::vec2 d(x - lineHalf, visualBottomBound);
                    pushQuad(g_cache.vertices,
                             pixelToNDC(a, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(b, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(c, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(d, layout.screenWidth, layout.screenHeight),
                             lineColor);
                }
                glm::vec2 hTopA(laneLeft, topBound - lineHalf);
                glm::vec2 hTopB(laneRight, topBound - lineHalf);
                glm::vec2 hTopC(laneRight, topBound + lineHalf);
                glm::vec2 hTopD(laneLeft, topBound + lineHalf);
                pushQuad(g_cache.vertices,
                         pixelToNDC(hTopA, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(hTopB, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(hTopC, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(hTopD, layout.screenWidth, layout.screenHeight),
                         gridColor);
                glm::vec2 hBotA(laneLeft, visualBottomBound - lineHalf);
                glm::vec2 hBotB(laneRight, visualBottomBound - lineHalf);
                glm::vec2 hBotC(laneRight, visualBottomBound + lineHalf);
                glm::vec2 hBotD(laneLeft, visualBottomBound + lineHalf);
                pushQuad(g_cache.vertices,
                         pixelToNDC(hBotA, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(hBotB, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(hBotC, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(hBotD, layout.screenWidth, layout.screenHeight),
                         gridColor);
            }

            const auto audioLaneIndex = DawLaneTimelineSystemLogic::BuildAudioLaneIndex(daw, audioTrackCount);

            for (int i = 0; i < audioTrackCount; ++i) {
                int laneIndex = audioLaneIndex[static_cast<size_t>(i)];
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

                pushQuad(g_cache.vertices,
                         pixelToNDC(frontA, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(frontB, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(frontC, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(frontD, layout.screenWidth, layout.screenHeight),
                         laneColor);
                pushQuad(g_cache.vertices,
                         pixelToNDC(topA, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(topB, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(topC, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(topD, layout.screenWidth, layout.screenHeight),
                         laneHighlight);
                pushQuad(g_cache.vertices,
                         pixelToNDC(leftA, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(leftB, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(leftC, layout.screenWidth, layout.screenHeight),
                         pixelToNDC(leftD, layout.screenWidth, layout.screenHeight),
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
            for (int t = 0; t < audioTrackCount; ++t) {
                const auto& clips = daw.tracks[static_cast<size_t>(t)].clips;
                if (clips.empty()) continue;
                int laneIndex = audioLaneIndex[static_cast<size_t>(t)];
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
                    bool selectedClip = (t == daw.selectedClipTrack) && (static_cast<int>(ci) == daw.selectedClipIndex);
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
                    pushQuad(g_cache.vertices,
                             pixelToNDC(a, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(b, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(c, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(d, layout.screenWidth, layout.screenHeight),
                             bodyColor);
                    if (lipBottom > top + 0.5f) {
                        glm::vec2 la(x0, top);
                        glm::vec2 lb(x1, top);
                        glm::vec2 lc(x1, lipBottom);
                        glm::vec2 ld(x0, lipBottom);
                        pushQuad(g_cache.vertices,
                                 pixelToNDC(la, layout.screenWidth, layout.screenHeight),
                                 pixelToNDC(lb, layout.screenWidth, layout.screenHeight),
                                 pixelToNDC(lc, layout.screenWidth, layout.screenHeight),
                                 pixelToNDC(ld, layout.screenWidth, layout.screenHeight),
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
                                pushQuad(g_cache.vertices,
                                         pixelToNDC({x - lineHalf, markerTop}, layout.screenWidth, layout.screenHeight),
                                         pixelToNDC({x + lineHalf, markerTop}, layout.screenWidth, layout.screenHeight),
                                         pixelToNDC({x + lineHalf, markerBottom}, layout.screenWidth, layout.screenHeight),
                                         pixelToNDC({x - lineHalf, markerBottom}, layout.screenWidth, layout.screenHeight),
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
                                    pushText(g_cache.vertices,
                                             x + 2.0f,
                                             markerBottom + 2.0f,
                                             label,
                                             glm::vec3(0.04f, 0.04f, 0.04f),
                                             layout.screenWidth,
                                             layout.screenHeight);
                                    lastLabelX = x;
                                }
                                tick += beatStepSamples;
                                ++guard;
                            }
                        }
                    }
                }
            }

            float waveHeight = layout.laneHeight * 0.8f;
            float ampScale = waveHeight * 0.5f;
            int pixelWidth = static_cast<int>(laneRight - laneLeft);
            if (pixelWidth > 0) {
                for (int t = 0; t < audioTrackCount; ++t) {
                    const DawTrack& track = daw.tracks[t];
                    if (track.waveformMin.empty() || track.waveformMax.empty()) continue;
                    size_t blockCount = track.waveformMin.size();
                    if (blockCount == 0) continue;
                    bool trackHasStereoClip = false;
                    for (const auto& clip : track.clips) {
                        if (clip.audioId < 0 || clip.audioId >= static_cast<int>(daw.clipAudio.size())) continue;
                        const DawClipAudio& clipAudio = daw.clipAudio[clip.audioId];
                        if (clipAudio.channels > 1 && !clipAudio.right.empty()) {
                            trackHasStereoClip = true;
                            break;
                        }
                    }
                    const bool hasRightWave =
                        trackHasStereoClip
                        && (track.waveformMinRight.size() == blockCount)
                        && (track.waveformMaxRight.size() == blockCount);
                    int laneIndex = audioLaneIndex[static_cast<size_t>(t)];
                    if (laneIndex < 0) continue;
                    int displayIndex = computeDisplayIndex(laneIndex);
                    if (displayIndex < 0) continue;
                    float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                    float top = centerY - laneHalfH;
                    float bottom = centerY + laneHalfH;
                    if (hasRightWave) {
                        const float splitY = 0.5f * (top + bottom);
                        const float splitHalf = 0.5f;
                        pushQuad(g_cache.vertices,
                                 pixelToNDC({laneLeft, splitY - splitHalf}, layout.screenWidth, layout.screenHeight),
                                 pixelToNDC({laneRight, splitY - splitHalf}, layout.screenWidth, layout.screenHeight),
                                 pixelToNDC({laneRight, splitY + splitHalf}, layout.screenWidth, layout.screenHeight),
                                 pixelToNDC({laneLeft, splitY + splitHalf}, layout.screenWidth, layout.screenHeight),
                                 glm::vec3(0.03f, 0.03f, 0.03f));
                    }
                    for (int x = 0; x < pixelWidth; ++x) {
                        double samplePos = offsetSamples + (static_cast<double>(x) / static_cast<double>(pixelWidth)) * windowSamples;
                        if (samplePos < 0.0) continue;
                        size_t idx = static_cast<size_t>(samplePos) / kWaveformBlockSize;
                        if (idx >= blockCount) continue;
                        float minVal = track.waveformMin[idx];
                        float maxVal = track.waveformMax[idx];
                        float yTop = centerY - maxVal * ampScale;
                        float yBottom = centerY - minVal * ampScale;
                        yTop = std::max(yTop, top);
                        yBottom = std::min(yBottom, bottom);
                        float xPos = laneLeft + static_cast<float>(x);
                        float lineWidth = 1.0f;
                        glm::vec3 blockColor = waveformBaseColor;
                        if (idx < track.waveformColor.size()) {
                            blockColor = track.waveformColor[idx];
                        }
                        if (hasRightWave) {
                            const float splitY = 0.5f * (top + bottom);
                            const float regionGap = 1.0f;
                            const float topRegionTop = top;
                            const float topRegionBottom = splitY - regionGap;
                            const float bottomRegionTop = splitY + regionGap;
                            const float bottomRegionBottom = bottom;
                            const float topCenter = 0.5f * (topRegionTop + topRegionBottom);
                            const float bottomCenter = 0.5f * (bottomRegionTop + bottomRegionBottom);
                            const float topAmp = std::max(1.0f, (topRegionBottom - topRegionTop) * 0.45f);
                            const float bottomAmp = std::max(1.0f, (bottomRegionBottom - bottomRegionTop) * 0.45f);

                            float yTopL = topCenter - maxVal * topAmp;
                            float yBottomL = topCenter - minVal * topAmp;
                            yTopL = std::clamp(yTopL, topRegionTop, topRegionBottom);
                            yBottomL = std::clamp(yBottomL, topRegionTop, topRegionBottom);
                            pushQuad(g_cache.vertices,
                                     pixelToNDC({xPos - lineWidth, yTopL}, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC({xPos + lineWidth, yTopL}, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC({xPos + lineWidth, yBottomL}, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC({xPos - lineWidth, yBottomL}, layout.screenWidth, layout.screenHeight),
                                     blockColor);

                            float minValR = track.waveformMinRight[idx];
                            float maxValR = track.waveformMaxRight[idx];
                            float yTopR = bottomCenter - maxValR * bottomAmp;
                            float yBottomR = bottomCenter - minValR * bottomAmp;
                            yTopR = std::clamp(yTopR, bottomRegionTop, bottomRegionBottom);
                            yBottomR = std::clamp(yBottomR, bottomRegionTop, bottomRegionBottom);
                            glm::vec3 rightColor = glm::clamp(blockColor + glm::vec3(0.16f, 0.04f, 0.08f),
                                                              glm::vec3(0.0f), glm::vec3(1.0f));
                            pushQuad(g_cache.vertices,
                                     pixelToNDC({xPos - lineWidth, yTopR}, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC({xPos + lineWidth, yTopR}, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC({xPos + lineWidth, yBottomR}, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC({xPos - lineWidth, yBottomR}, layout.screenWidth, layout.screenHeight),
                                     rightColor);
                        } else {
                            glm::vec2 a(xPos - lineWidth, yTop);
                            glm::vec2 b(xPos + lineWidth, yTop);
                            glm::vec2 c(xPos + lineWidth, yBottom);
                            glm::vec2 d(xPos - lineWidth, yBottom);
                            pushQuad(g_cache.vertices,
                                     pixelToNDC(a, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC(b, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC(c, layout.screenWidth, layout.screenHeight),
                                     pixelToNDC(d, layout.screenWidth, layout.screenHeight),
                                     blockColor);
                        }
                    }
                }
            }

            {
                float handleSize = std::min(kTrackHandleSize, std::max(14.0f, layout.laneHeight));
                float handleHalf = handleSize * 0.5f;
                float handleCenterX = laneRight + kTrackHandleInset + handleHalf;
                float minCenterX = laneLeft + 4.0f + handleHalf;
                if (handleCenterX < minCenterX) handleCenterX = minCenterX;
                glm::vec3 handleFront = glm::clamp(laneColor + glm::vec3(0.03f), glm::vec3(0.0f), glm::vec3(1.0f));
                glm::vec3 handleTop = glm::clamp(laneHighlight + glm::vec3(0.02f), glm::vec3(0.0f), glm::vec3(1.0f));
                glm::vec3 handleSide = laneShadow;
                float handleDepth = std::min(6.0f, handleHalf * 0.5f);
                for (int t = 0; t < audioTrackCount; ++t) {
                    int laneIndex = audioLaneIndex[static_cast<size_t>(t)];
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
                    pushQuad(g_cache.vertices,
                             pixelToNDC(frontA, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(frontB, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(frontC, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(frontD, layout.screenWidth, layout.screenHeight),
                             handleFront);
                    pushQuad(g_cache.vertices,
                             pixelToNDC(topA, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(topB, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(topC, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(topD, layout.screenWidth, layout.screenHeight),
                             handleTop);
                    pushQuad(g_cache.vertices,
                             pixelToNDC(leftA, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(leftB, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(leftC, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(leftD, layout.screenWidth, layout.screenHeight),
                             handleSide);
                }
            }

            g_cache.staticVertexCount = g_cache.vertices.size();
            if (g_cache.waveVersions.size() != static_cast<size_t>(audioTrackCount)) {
                g_cache.waveVersions.assign(static_cast<size_t>(audioTrackCount), 0);
            }
            for (int t = 0; t < audioTrackCount; ++t) {
                g_cache.waveVersions[static_cast<size_t>(t)] = daw.tracks[t].waveformVersion;
            }
        }
    }

    using UiVertex = DawLaneTimelineSystemLogic::UiVertex;

    const std::vector<UiVertex>& GetLaneVertices() { return g_cache.vertices; }
    std::vector<UiVertex>& GetLaneVerticesMutable() { return g_cache.vertices; }
    size_t GetLaneStaticVertexCount() { return g_cache.staticVertexCount; }
    void SetLaneTotalVertexCount(size_t count) { g_cache.totalVertexCount = count; }
    size_t GetLaneTotalVertexCount() { return g_cache.totalVertexCount; }

    void UpdateDawLaneResources(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow* win) {
        if (!baseSystem.ui || !baseSystem.daw || !baseSystem.renderer || !baseSystem.world || !baseSystem.level || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
        if (baseSystem.midi && baseSystem.midi->pianoRollActive) return;
        if (!DawLaneTimelineSystemLogic::hasDawUiWorld(*baseSystem.level)) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        ensureResources(renderer, world);

        DawContext& daw = *baseSystem.daw;
        const auto layout = DawLaneTimelineSystemLogic::ComputeLaneLayout(baseSystem, daw, win);
        const int audioTrackCount = layout.audioTrackCount;
        const double secondsPerScreen = layout.secondsPerScreen;

        bool rebuildStatic = (static_cast<int>(layout.screenWidth) != g_cache.cachedWidth)
            || (static_cast<int>(layout.screenHeight) != g_cache.cachedHeight);
        if (std::abs(layout.scrollY - g_cache.cachedScrollY) > 0.01f) {
            rebuildStatic = true;
        }
        if (std::abs(layout.startY - g_cache.cachedStartY) > 0.01f) {
            rebuildStatic = true;
        }
        if (std::abs(layout.laneHeight - g_cache.cachedLaneHeight) > 0.01f) {
            rebuildStatic = true;
        }
        if (audioTrackCount != g_cache.cachedTrackCount) {
            rebuildStatic = true;
        }
        if (std::abs(secondsPerScreen - g_cache.cachedSecondsPerScreen) > 0.0001) {
            rebuildStatic = true;
        }
        double bpm = daw.bpm.load(std::memory_order_relaxed);
        if (bpm <= 0.0) bpm = 120.0;
        if (std::abs(bpm - g_cache.cachedBpm) > 0.001) {
            rebuildStatic = true;
        }
        if (daw.timelineOffsetSamples != g_cache.cachedTimelineOffset) {
            rebuildStatic = true;
        }
        if (daw.themeRevision != g_cache.cachedThemeRevision) {
            rebuildStatic = true;
        }
        {
            std::vector<uint64_t> clipSig;
            clipSig.reserve(static_cast<size_t>(audioTrackCount) * 4);
            for (int t = 0; t < audioTrackCount; ++t) {
                clipSig.push_back(static_cast<uint64_t>(t));
                const auto& clips = daw.tracks[static_cast<size_t>(t)].clips;
                clipSig.push_back(static_cast<uint64_t>(clips.size()));
                for (const auto& clip : clips) {
                    clipSig.push_back(static_cast<uint64_t>(clip.audioId));
                    clipSig.push_back(clip.startSample);
                    clipSig.push_back(clip.length);
                    clipSig.push_back(clip.sourceOffset);
                }
            }
            clipSig.push_back(static_cast<uint64_t>(daw.selectedClipTrack + 1));
            clipSig.push_back(static_cast<uint64_t>(daw.selectedClipIndex + 1));
            if (clipSig != g_cache.cachedClipSignature) {
                rebuildStatic = true;
                g_cache.cachedClipSignature = std::move(clipSig);
            }
        }
        if (g_cache.waveVersions.size() != static_cast<size_t>(audioTrackCount)) {
            g_cache.waveVersions.assign(static_cast<size_t>(audioTrackCount), 0);
            rebuildStatic = true;
        }
        for (int t = 0; t < audioTrackCount; ++t) {
            if (daw.tracks[t].waveformVersion != g_cache.waveVersions[static_cast<size_t>(t)]) {
                rebuildStatic = true;
                break;
            }
        }

        std::vector<int> laneSignature;
        laneSignature.reserve(daw.laneOrder.size() * 2);
        for (const auto& entry : daw.laneOrder) {
            laneSignature.push_back(entry.type);
            laneSignature.push_back(entry.trackIndex);
        }
        if (laneSignature != g_cache.cachedLaneSignature) {
            rebuildStatic = true;
            g_cache.cachedLaneSignature = laneSignature;
        }

        if (rebuildStatic) {
            buildLanes(baseSystem, daw, world, layout, -1, bpm);
        }

        int previewSlot = -1;
        if (daw.dragActive && daw.dragLaneType == 0) {
            previewSlot = daw.dragDropIndex;
        } else if (daw.externalDropActive && daw.externalDropType == 0) {
            previewSlot = daw.externalDropIndex;
        }
        if (previewSlot >= 0) {
            buildLanes(baseSystem, daw, world, layout, previewSlot, bpm);
        } else if (g_cache.cachedPreviewSlot >= 0 && !rebuildStatic) {
            // Drag preview just ended; rebuild static vertices immediately
            // so no preview-only lane offsets linger in cache.
            buildLanes(baseSystem, daw, world, layout, -1, bpm);
        }
        g_cache.cachedPreviewSlot = previewSlot;
    }
}
