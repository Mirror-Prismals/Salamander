#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace DawStateSystemLogic {
    bool MoveTrack(BaseSystem& baseSystem, int fromIndex, int toIndex);
    void TrimClipsForNewClip(DawTrack& track, const DawClip& clip);
    void RebuildTrackCacheFromClips(DawContext& daw, DawTrack& track);
}

namespace DawLaneSystemLogic {
    namespace {
        constexpr float kLaneAlpha = 0.85f;
        constexpr float kLaneLeftMargin = 40.0f;
        constexpr float kLaneRightMargin = 40.0f;
        constexpr float kPlayheadHandleSize = 12.0f;
        constexpr float kPlayheadHandleYOffset = 14.0f;
        constexpr float kRulerHeight = 13.0f;
        constexpr float kRulerInset = 10.0f;
        constexpr float kRulerSideInset = -15.0f;
        constexpr float kRulerLowerOffset = 0.0f;
        constexpr float kRulerGap = 6.0f;
        constexpr float kLoopHandleWidth = 8.0f;

        struct UiVertex { glm::vec2 pos; glm::vec3 color; };
        static std::vector<UiVertex> g_laneVertices;
        static size_t g_staticVertexCount = 0;
        static size_t g_totalVertexCount = 0;
        static int g_cachedWidth = 0;
        static int g_cachedHeight = 0;
        static float g_cachedScrollY = 0.0f;
        static int g_cachedTrackCount = 0;
        static int64_t g_cachedTimelineOffset = 0;
        static double g_cachedSecondsPerScreen = 10.0;
        static double g_cachedBpm = 120.0;
        static std::vector<int> g_cachedLaneSignature;
        static std::vector<uint64_t> g_waveVersions;
        static std::vector<uint64_t> g_cachedClipSignature;
        constexpr size_t kWaveformBlockSize = 256;

        bool hasDawUiWorld(const LevelContext& level) {
            for (const auto& world : level.worlds) {
                if (world.name == "DAWScreenWorld") return true;
                if (world.name == "TrackRowWorld") return true;
                if (world.name.rfind("TrackRowWorld_", 0) == 0) return true;
            }
            return false;
        }

        glm::vec2 pixelToNDC(const glm::vec2& pixel, double width, double height) {
            float ndcX = static_cast<float>((pixel.x / width) * 2.0 - 1.0);
            float ndcY = static_cast<float>(1.0 - (pixel.y / height) * 2.0);
            return {ndcX, ndcY};
        }

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

        void ensureResources(RendererContext& renderer, WorldContext& world) {
            if (!renderer.uiColorShader) {
                renderer.uiColorShader = std::make_unique<Shader>(world.shaders["UI_COLOR_VERTEX_SHADER"].c_str(), world.shaders["UI_COLOR_FRAGMENT_SHADER"].c_str());
            }
            if (renderer.uiLaneVAO == 0) {
                glGenVertexArrays(1, &renderer.uiLaneVAO);
                glGenBuffers(1, &renderer.uiLaneVBO);
            }
        }

        uint64_t maxTimelineSamples(const DawContext& daw) {
            uint64_t maxSamples = daw.playheadSample.load(std::memory_order_relaxed);
            for (const auto& track : daw.tracks) {
                maxSamples = std::max<uint64_t>(maxSamples, static_cast<uint64_t>(track.audio.size()));
            }
            return maxSamples;
        }

        void clampTimelineOffset(DawContext& daw) {
            double secondsPerScreen = (daw.timelineSecondsPerScreen > 0.0) ? daw.timelineSecondsPerScreen : 10.0;
            int64_t windowSamples = static_cast<int64_t>(secondsPerScreen * daw.sampleRate);
            if (windowSamples < 0) windowSamples = 0;
            uint64_t maxSamples = maxTimelineSamples(daw);
            int64_t maxOffset = (maxSamples > static_cast<uint64_t>(windowSamples))
                ? static_cast<int64_t>(maxSamples - static_cast<uint64_t>(windowSamples))
                : 0;
            if (daw.timelineOffsetSamples < 0) daw.timelineOffsetSamples = 0;
            if (daw.timelineOffsetSamples > maxOffset) daw.timelineOffsetSamples = maxOffset;
        }

        double gridSecondsForZoom(double secondsPerScreen, double secondsPerBeat) {
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
    }

    void UpdateDawLanes(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt;
        if (!baseSystem.ui || !baseSystem.daw || !baseSystem.renderer || !baseSystem.world || !baseSystem.level || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
        if (baseSystem.midi && baseSystem.midi->pianoRollActive) return;
        if (!hasDawUiWorld(*baseSystem.level)) return;
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

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        ensureResources(renderer, world);

        int windowWidth = 0, windowHeight = 0;
        glfwGetWindowSize(win, &windowWidth, &windowHeight);
        double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
        double screenHeight = windowHeight > 0 ? static_cast<double>(windowHeight) : 1080.0;

        DawContext& daw = *baseSystem.daw;
        int audioTrackCount = static_cast<int>(daw.tracks.size());
        int laneCount = static_cast<int>(daw.laneOrder.size());
        if (laneCount == 0) {
            laneCount = audioTrackCount;
        }
        const float laneHeight = 60.0f;
        const float laneGap = 12.0f;
        const float laneHalfH = laneHeight * 0.5f;
        float laneLeft = kLaneLeftMargin;
        float laneRight = static_cast<float>(screenWidth) - kLaneRightMargin;
        if (laneRight < laneLeft + 200.0f) {
            laneRight = laneLeft + 200.0f;
        }
        float scrollY = 0.0f;
        if (baseSystem.uiStamp) {
            scrollY = baseSystem.uiStamp->scrollY;
        }
        const float startY = 100.0f + scrollY;
        double secondsPerScreen = (daw.timelineSecondsPerScreen > 0.0) ? daw.timelineSecondsPerScreen : 10.0;

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

        bool rebuildStatic = (windowWidth != g_cachedWidth) || (windowHeight != g_cachedHeight);
        if (std::abs(scrollY - g_cachedScrollY) > 0.01f) {
            rebuildStatic = true;
        }
        if (audioTrackCount != g_cachedTrackCount) {
            rebuildStatic = true;
        }
        if (std::abs(secondsPerScreen - g_cachedSecondsPerScreen) > 0.0001) {
            rebuildStatic = true;
        }
        double bpm = daw.bpm.load(std::memory_order_relaxed);
        if (bpm <= 0.0) bpm = 120.0;
        if (std::abs(bpm - g_cachedBpm) > 0.001) {
            rebuildStatic = true;
        }
        if (daw.timelineOffsetSamples != g_cachedTimelineOffset) {
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
            if (clipSig != g_cachedClipSignature) {
                rebuildStatic = true;
                g_cachedClipSignature = std::move(clipSig);
            }
        }
        if (g_waveVersions.size() != static_cast<size_t>(audioTrackCount)) {
            g_waveVersions.assign(static_cast<size_t>(audioTrackCount), 0);
            rebuildStatic = true;
        }
        for (int t = 0; t < audioTrackCount; ++t) {
            if (daw.tracks[t].waveformVersion != g_waveVersions[static_cast<size_t>(t)]) {
                rebuildStatic = true;
                break;
            }
        }

        std::vector<int> audioLaneIndex(static_cast<size_t>(audioTrackCount), -1);
        if (!daw.laneOrder.empty()) {
            for (size_t laneIdx = 0; laneIdx < daw.laneOrder.size(); ++laneIdx) {
                const auto& entry = daw.laneOrder[laneIdx];
                if (entry.type == 0 && entry.trackIndex >= 0 && entry.trackIndex < audioTrackCount) {
                    audioLaneIndex[static_cast<size_t>(entry.trackIndex)] = static_cast<int>(laneIdx);
                }
            }
        } else {
            glm::vec3 rulerFront = laneHighlight;
            glm::vec3 rulerTop = glm::clamp(laneHighlight + glm::vec3(0.05f), glm::vec3(0.0f), glm::vec3(1.0f));
            glm::vec3 rulerSide = glm::clamp(laneShadow - glm::vec3(0.02f), glm::vec3(0.0f), glm::vec3(1.0f));
            float rulerTopY = startY - laneHalfH - (kRulerHeight + kRulerInset);
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
            pushQuad(g_laneVertices,
                     pixelToNDC(rFrontA, screenWidth, screenHeight),
                     pixelToNDC(rFrontB, screenWidth, screenHeight),
                     pixelToNDC(rFrontC, screenWidth, screenHeight),
                     pixelToNDC(rFrontD, screenWidth, screenHeight),
                     rulerFront);
            pushQuad(g_laneVertices,
                     pixelToNDC(rTopA, screenWidth, screenHeight),
                     pixelToNDC(rTopB, screenWidth, screenHeight),
                     pixelToNDC(rTopC, screenWidth, screenHeight),
                     pixelToNDC(rTopD, screenWidth, screenHeight),
                     rulerTop);
            pushQuad(g_laneVertices,
                     pixelToNDC(rLeftA, screenWidth, screenHeight),
                     pixelToNDC(rLeftB, screenWidth, screenHeight),
                     pixelToNDC(rLeftC, screenWidth, screenHeight),
                     pixelToNDC(rLeftD, screenWidth, screenHeight),
                     rulerSide);

            for (int i = 0; i < audioTrackCount; ++i) {
                audioLaneIndex[static_cast<size_t>(i)] = i;
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

        float rowSpan = laneHeight + laneGap;
        float topBound = startY - laneHalfH;
        float laneBottomBound = (laneCount > 0)
            ? (startY + (laneCount - 1) * rowSpan + laneHalfH)
            : (topBound + 1.0f);
        if (laneBottomBound < topBound + 1.0f) {
            laneBottomBound = topBound + 1.0f;
        }
        float visualBottomBound = std::max(laneBottomBound, static_cast<float>(screenHeight) - 40.0f);
        float handleY = topBound - kPlayheadHandleYOffset + kRulerLowerOffset;
        float handleHalf = kPlayheadHandleSize * 0.5f;
        float rulerTopY = startY - laneHalfH - (kRulerHeight + kRulerInset) + kRulerLowerOffset;
        float rulerBottomY = rulerTopY + kRulerHeight;
        if (rulerTopY < 0.0f) {
            float shift = -rulerTopY;
            rulerTopY += shift;
            rulerBottomY += shift;
        }
        float rulerLeft = laneLeft + kRulerSideInset;
        float rulerRight = laneRight - kRulerSideInset;
        if (rulerRight < rulerLeft + 1.0f) {
            rulerRight = rulerLeft + 1.0f;
        }
        float upperRulerBottom = rulerTopY - kRulerGap;
        float upperRulerTop = upperRulerBottom - kRulerHeight;
        auto buildLanes = [&](int previewSlot) {
            g_cachedWidth = windowWidth;
            g_cachedHeight = windowHeight;
            g_cachedScrollY = scrollY;
            g_cachedTrackCount = audioTrackCount;
            g_cachedTimelineOffset = daw.timelineOffsetSamples;
            g_cachedSecondsPerScreen = secondsPerScreen;
            g_cachedBpm = bpm;
            g_laneVertices.clear();
            g_laneVertices.reserve(static_cast<size_t>(audioTrackCount) * 18);

            for (int i = 0; i < audioTrackCount; ++i) {
                int laneIndex = audioLaneIndex[static_cast<size_t>(i)];
                if (laneIndex < 0) continue;
                int displayIndex = laneIndex;
                if (previewSlot >= 0 && laneIndex >= previewSlot) {
                    displayIndex += 1;
                }
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

            float topBound = startY - laneHalfH;
            float laneBottomBound = (laneCount > 0)
                ? (startY + (laneCount - 1) * rowSpan + laneHalfH)
                : (topBound + 1.0f);
            if (laneBottomBound < topBound + 1.0f) {
                laneBottomBound = topBound + 1.0f;
            }
            float visualBottomBound = std::max(laneBottomBound, static_cast<float>(screenHeight) - 40.0f);
            double secondsPerBeat = (bpm > 0.0) ? (60.0 / bpm) : 0.5;
            double gridSeconds = gridSecondsForZoom(secondsPerScreen, secondsPerBeat);
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
                    pushQuad(g_laneVertices,
                             pixelToNDC(a, screenWidth, screenHeight),
                             pixelToNDC(b, screenWidth, screenHeight),
                             pixelToNDC(c, screenWidth, screenHeight),
                             pixelToNDC(d, screenWidth, screenHeight),
                             lineColor);
                }
                glm::vec2 hTopA(laneLeft, topBound - lineHalf);
                glm::vec2 hTopB(laneRight, topBound - lineHalf);
                glm::vec2 hTopC(laneRight, topBound + lineHalf);
                glm::vec2 hTopD(laneLeft, topBound + lineHalf);
                pushQuad(g_laneVertices,
                         pixelToNDC(hTopA, screenWidth, screenHeight),
                         pixelToNDC(hTopB, screenWidth, screenHeight),
                         pixelToNDC(hTopC, screenWidth, screenHeight),
                         pixelToNDC(hTopD, screenWidth, screenHeight),
                         gridColor);
                glm::vec2 hBotA(laneLeft, visualBottomBound - lineHalf);
                glm::vec2 hBotB(laneRight, visualBottomBound - lineHalf);
                glm::vec2 hBotC(laneRight, visualBottomBound + lineHalf);
                glm::vec2 hBotD(laneLeft, visualBottomBound + lineHalf);
                pushQuad(g_laneVertices,
                         pixelToNDC(hBotA, screenWidth, screenHeight),
                         pixelToNDC(hBotB, screenWidth, screenHeight),
                         pixelToNDC(hBotC, screenWidth, screenHeight),
                         pixelToNDC(hBotD, screenWidth, screenHeight),
                         gridColor);
            }

            double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
            double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
            if (windowSamples < 0.0) windowSamples = 0.0;
            float clipInset = 6.0f;
            glm::vec3 clipColor = glm::clamp(laneHighlight + glm::vec3(0.12f), glm::vec3(0.0f), glm::vec3(1.0f));
            for (int t = 0; t < audioTrackCount; ++t) {
                const auto& clips = daw.tracks[static_cast<size_t>(t)].clips;
                if (clips.empty()) continue;
                int laneIndex = audioLaneIndex[static_cast<size_t>(t)];
                if (laneIndex < 0) continue;
                int displayIndex = laneIndex;
                if (previewSlot >= 0 && laneIndex >= previewSlot) {
                    displayIndex += 1;
                }
                float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                float top = centerY - laneHalfH + clipInset;
                float bottom = centerY + laneHalfH - clipInset;
                for (const auto& clip : clips) {
                    if (clip.length == 0) continue;
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
                    float x0 = laneLeft + (laneRight - laneLeft) * t0;
                    float x1 = laneLeft + (laneRight - laneLeft) * t1;
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
                             clipColor);
                }
            }

            float waveHeight = laneHeight * 0.8f;
            float ampScale = waveHeight * 0.5f;
            int pixelWidth = static_cast<int>(laneRight - laneLeft);
            if (pixelWidth > 0) {
                for (int t = 0; t < audioTrackCount; ++t) {
                    const DawTrack& track = daw.tracks[t];
                    if (track.waveformMin.empty() || track.waveformMax.empty()) continue;
                    size_t blockCount = track.waveformMin.size();
                    if (blockCount == 0) continue;
                    int laneIndex = audioLaneIndex[static_cast<size_t>(t)];
                    if (laneIndex < 0) continue;
                    int displayIndex = laneIndex;
                    if (previewSlot >= 0 && laneIndex >= previewSlot) {
                        displayIndex += 1;
                    }
                    float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                    float top = centerY - laneHalfH;
                    float bottom = centerY + laneHalfH;
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
                        glm::vec2 a(xPos - lineWidth, yTop);
                        glm::vec2 b(xPos + lineWidth, yTop);
                        glm::vec2 c(xPos + lineWidth, yBottom);
                        glm::vec2 d(xPos - lineWidth, yBottom);
                        pushQuad(g_laneVertices,
                                 pixelToNDC(a, screenWidth, screenHeight),
                                 pixelToNDC(b, screenWidth, screenHeight),
                                 pixelToNDC(c, screenWidth, screenHeight),
                                 pixelToNDC(d, screenWidth, screenHeight),
                                 blockColor);
                    }
                }
            }

            g_staticVertexCount = g_laneVertices.size();
            g_laneVertices.resize(g_staticVertexCount + 6);
            for (int t = 0; t < audioTrackCount; ++t) {
                g_waveVersions[static_cast<size_t>(t)] = daw.tracks[t].waveformVersion;
            }
        };

        if (rebuildStatic) {
            buildLanes(-1);
        }

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
                        DawStateSystemLogic::TrimClipsForNewClip(toTrack, clip);
                        toTrack.clips.push_back(clip);
                        std::sort(toTrack.clips.begin(), toTrack.clips.end(), [](const DawClip& a, const DawClip& b) {
                            if (a.startSample == b.startSample) return a.sourceOffset < b.sourceOffset;
                            return a.startSample < b.startSample;
                        });
                        DawStateSystemLogic::RebuildTrackCacheFromClips(daw, toTrack);
                        if (srcTrack != dstTrack) {
                            DawStateSystemLogic::RebuildTrackCacheFromClips(daw, fromTrack);
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
                    double gridSeconds = gridSecondsForZoom(secondsPerScreen, secondsPerBeat);
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
                double secondsPerBeat = (bpm > 0.0) ? (60.0 / bpm) : 0.5;
                double gridSeconds = gridSecondsForZoom(secondsPerScreen, secondsPerBeat);
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
                uint64_t maxSamples = maxTimelineSamples(daw);
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
                clampTimelineOffset(daw);
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
            clampTimelineOffset(daw);
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
            if (cursorInLaneRect(ui, laneLeft, laneRight, topBound - laneGap, laneBottomBound + laneGap)) {
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
                    if (DawStateSystemLogic::MoveTrack(baseSystem, fromIndex, toIndex)) {
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

        int previewSlot = -1;
        if (laneCount > 0 && daw.dragActive && daw.dragLaneType == 0) {
            previewSlot = daw.dragDropIndex;
        } else if (daw.externalDropActive && daw.externalDropType == 0) {
            previewSlot = daw.externalDropIndex;
        }
        if (previewSlot >= 0) {
            buildLanes(previewSlot);
        }

        double playheadSec = static_cast<double>(daw.playheadSample.load(std::memory_order_relaxed)) / static_cast<double>(daw.sampleRate);
        double offsetSec = static_cast<double>(daw.timelineOffsetSamples) / static_cast<double>(daw.sampleRate);
        double tNorm = secondsPerScreen > 0.0 ? (playheadSec - offsetSec) / secondsPerScreen : 0.0;
        bool clamped = false;
        if (tNorm < 0.0) { tNorm = 0.0; clamped = true; }
        if (tNorm > 1.0) { tNorm = 1.0; clamped = true; }
        float playheadX = static_cast<float>(laneLeft + (laneRight - laneLeft) * tNorm);
        glm::vec3 playheadColor(0.2f, 0.2f, 0.2f);
        auto itText = world.colorLibrary.find("MiraText");
        if (itText != world.colorLibrary.end()) {
            playheadColor = itText->second;
        }
        float lineWidth = 0.5f;
        glm::vec2 pa(playheadX - lineWidth, topBound);
        glm::vec2 pb(playheadX + lineWidth, topBound);
        glm::vec2 pc(playheadX + lineWidth, visualBottomBound);
        glm::vec2 pd(playheadX - lineWidth, visualBottomBound);
        bool showPlayhead = true;
        g_laneVertices.resize(g_staticVertexCount);
        {
            glm::vec3 rulerFront = laneHighlight;
            glm::vec3 rulerTop = glm::clamp(laneHighlight + glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f));
            glm::vec3 rulerSide = glm::clamp(laneShadow - glm::vec3(0.02f), glm::vec3(0.0f), glm::vec3(1.0f));
            auto itFront = world.colorLibrary.find("ButtonFront");
            if (itFront != world.colorLibrary.end()) {
                rulerFront = itFront->second;
            }
            auto itTop = world.colorLibrary.find("ButtonTopHighlight");
            if (itTop != world.colorLibrary.end()) {
                rulerTop = itTop->second;
            }
            auto itSide = world.colorLibrary.find("ButtonSideShadow");
            if (itSide != world.colorLibrary.end()) {
                rulerSide = itSide->second;
            }
            float rulerTopY = startY - laneHalfH - (kRulerHeight + kRulerInset) + kRulerLowerOffset;
            float rulerBottomY = rulerTopY + kRulerHeight;
            if (rulerTopY < 0.0f) {
                float shift = -rulerTopY;
                rulerTopY += shift;
                rulerBottomY += shift;
            }
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
            pushQuad(g_laneVertices,
                     pixelToNDC(rFrontA, screenWidth, screenHeight),
                     pixelToNDC(rFrontB, screenWidth, screenHeight),
                     pixelToNDC(rFrontC, screenWidth, screenHeight),
                     pixelToNDC(rFrontD, screenWidth, screenHeight),
                     rulerFront);
            pushQuad(g_laneVertices,
                     pixelToNDC(rTopA, screenWidth, screenHeight),
                     pixelToNDC(rTopB, screenWidth, screenHeight),
                     pixelToNDC(rTopC, screenWidth, screenHeight),
                     pixelToNDC(rTopD, screenWidth, screenHeight),
                     rulerTop);
            pushQuad(g_laneVertices,
                     pixelToNDC(rLeftA, screenWidth, screenHeight),
                     pixelToNDC(rLeftB, screenWidth, screenHeight),
                     pixelToNDC(rLeftC, screenWidth, screenHeight),
                     pixelToNDC(rLeftD, screenWidth, screenHeight),
                     rulerSide);

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
            float upperBottom = rulerTopY - kRulerGap;
            float upperTop = upperBottom - kRulerHeight;
            float loopLeft = std::clamp(loopStartX, rulerLeft, rulerRight);
            float loopRight = std::clamp(loopEndX, rulerLeft, rulerRight);
            if (loopRight < loopLeft + 1.0f) loopRight = loopLeft + 1.0f;
            glm::vec2 uFrontA(loopLeft, upperTop);
            glm::vec2 uFrontB(loopRight, upperTop);
            glm::vec2 uFrontC(loopRight, upperBottom);
            glm::vec2 uFrontD(loopLeft, upperBottom);
            glm::vec2 uTopA = uFrontA;
            glm::vec2 uTopB = uFrontB;
            glm::vec2 uTopC(uFrontB.x - bevelDepth, uFrontB.y - bevelDepth);
            glm::vec2 uTopD(uFrontA.x - bevelDepth, uFrontA.y - bevelDepth);
            glm::vec2 uLeftA = uFrontA;
            glm::vec2 uLeftB = uFrontD;
            glm::vec2 uLeftC(uFrontD.x - bevelDepth, uFrontD.y - bevelDepth);
            glm::vec2 uLeftD(uFrontA.x - bevelDepth, uFrontA.y - bevelDepth);
            pushQuad(g_laneVertices,
                     pixelToNDC(uFrontA, screenWidth, screenHeight),
                     pixelToNDC(uFrontB, screenWidth, screenHeight),
                     pixelToNDC(uFrontC, screenWidth, screenHeight),
                     pixelToNDC(uFrontD, screenWidth, screenHeight),
                     rulerFront);
            pushQuad(g_laneVertices,
                     pixelToNDC(uTopA, screenWidth, screenHeight),
                     pixelToNDC(uTopB, screenWidth, screenHeight),
                     pixelToNDC(uTopC, screenWidth, screenHeight),
                     pixelToNDC(uTopD, screenWidth, screenHeight),
                     rulerTop);
            pushQuad(g_laneVertices,
                     pixelToNDC(uLeftA, screenWidth, screenHeight),
                     pixelToNDC(uLeftB, screenWidth, screenHeight),
                     pixelToNDC(uLeftC, screenWidth, screenHeight),
                     pixelToNDC(uLeftD, screenWidth, screenHeight),
                     rulerSide);
        }
        if (daw.clipDragActive && daw.clipDragTrack >= 0 && daw.clipDragIndex >= 0
            && daw.clipDragTrack < audioTrackCount) {
            DawTrack& track = daw.tracks[static_cast<size_t>(daw.clipDragTrack)];
            if (daw.clipDragIndex < static_cast<int>(track.clips.size())) {
                const DawClip& clip = track.clips[static_cast<size_t>(daw.clipDragIndex)];
                int targetTrack = daw.clipDragTargetTrack;
                if (targetTrack >= 0 && targetTrack < audioTrackCount) {
                    int laneIndex = audioLaneIndex[static_cast<size_t>(targetTrack)];
                    if (laneIndex >= 0) {
                        int displayIndex = laneIndex;
                        if (previewSlot >= 0 && laneIndex >= previewSlot) {
                            displayIndex += 1;
                        }
                        float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
                        float top = centerY - laneHalfH + 6.0f;
                        float bottom = centerY + laneHalfH - 6.0f;
                        double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
                        double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
                        if (windowSamples <= 0.0) windowSamples = 1.0;
                        double clipStart = static_cast<double>(daw.clipDragTargetStart);
                        double clipEnd = static_cast<double>(daw.clipDragTargetStart + clip.length);
                        if (clipEnd > offsetSamples && clipStart < offsetSamples + windowSamples) {
                            double visibleStart = std::max(clipStart, offsetSamples);
                            double visibleEnd = std::min(clipEnd, offsetSamples + windowSamples);
                            float t0 = static_cast<float>((visibleStart - offsetSamples) / windowSamples);
                            float t1 = static_cast<float>((visibleEnd - offsetSamples) / windowSamples);
                            float x0 = laneLeft + (laneRight - laneLeft) * t0;
                            float x1 = laneLeft + (laneRight - laneLeft) * t1;
                            glm::vec3 ghostColor = glm::clamp(playheadColor + glm::vec3(0.12f), glm::vec3(0.0f), glm::vec3(1.0f));
                            glm::vec2 a(x0, top);
                            glm::vec2 b(x1, top);
                            glm::vec2 c(x1, bottom);
                            glm::vec2 d(x0, bottom);
                            pushQuad(g_laneVertices,
                                     pixelToNDC(a, screenWidth, screenHeight),
                                     pixelToNDC(b, screenWidth, screenHeight),
                                     pixelToNDC(c, screenWidth, screenHeight),
                                     pixelToNDC(d, screenWidth, screenHeight),
                                     ghostColor);
                        }
                    }
                }
            }
        }
        {
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
            float lineHalf = 0.5f;
            glm::vec3 loopColor = playheadColor;
            if (daw.loopEnabled.load(std::memory_order_relaxed)) {
                glm::vec2 la(loopStartX - lineHalf, topBound);
                glm::vec2 lb(loopStartX + lineHalf, topBound);
                glm::vec2 lc(loopStartX + lineHalf, visualBottomBound);
                glm::vec2 ld(loopStartX - lineHalf, visualBottomBound);
                pushQuad(g_laneVertices,
                         pixelToNDC(la, screenWidth, screenHeight),
                         pixelToNDC(lb, screenWidth, screenHeight),
                         pixelToNDC(lc, screenWidth, screenHeight),
                         pixelToNDC(ld, screenWidth, screenHeight),
                         loopColor);
                glm::vec2 ra(loopEndX - lineHalf, topBound);
                glm::vec2 rb(loopEndX + lineHalf, topBound);
                glm::vec2 rc(loopEndX + lineHalf, visualBottomBound);
                glm::vec2 rd(loopEndX - lineHalf, visualBottomBound);
                pushQuad(g_laneVertices,
                         pixelToNDC(ra, screenWidth, screenHeight),
                         pixelToNDC(rb, screenWidth, screenHeight),
                         pixelToNDC(rc, screenWidth, screenHeight),
                         pixelToNDC(rd, screenWidth, screenHeight),
                         loopColor);
            }
        }
        if (showPlayhead) {
            pushQuad(g_laneVertices,
                     pixelToNDC(pa, screenWidth, screenHeight),
                     pixelToNDC(pb, screenWidth, screenHeight),
                     pixelToNDC(pc, screenWidth, screenHeight),
                     pixelToNDC(pd, screenWidth, screenHeight),
                     playheadColor);
            glm::vec2 tA(playheadX, handleY + handleHalf);
            glm::vec2 tB(playheadX - handleHalf, handleY - handleHalf);
            glm::vec2 tC(playheadX + handleHalf, handleY - handleHalf);
            g_laneVertices.push_back({pixelToNDC(tA, screenWidth, screenHeight), playheadColor});
            g_laneVertices.push_back({pixelToNDC(tB, screenWidth, screenHeight), playheadColor});
            g_laneVertices.push_back({pixelToNDC(tC, screenWidth, screenHeight), playheadColor});
        }

        glm::vec3 selectedColor(0.45f, 0.72f, 1.0f);
        auto itSelected = world.colorLibrary.find("MiraLaneSelected");
        if (itSelected != world.colorLibrary.end()) {
            selectedColor = itSelected->second;
        }
        if (daw.selectedLaneType == 0 && daw.selectedLaneIndex >= 0 && daw.selectedLaneIndex < laneCount) {
            int displayIndex = daw.selectedLaneIndex;
            if (previewSlot >= 0 && daw.selectedLaneIndex >= previewSlot) {
                displayIndex += 1;
            }
            float centerY = startY + static_cast<float>(displayIndex) * rowSpan;
            float top = centerY - laneHalfH;
            float bottom = centerY + laneHalfH;
            glm::vec3 selectedHighlight = glm::clamp(selectedColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
            glm::vec3 selectedShadow = glm::clamp(selectedColor - glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f));
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

        int insertSlot = daw.dragActive ? daw.dragDropIndex
            : (daw.externalDropActive && daw.externalDropType == 0 ? daw.externalDropIndex : -1);
        if ((daw.dragActive && daw.dragLaneType == 0) || (daw.externalDropActive && daw.externalDropType == 0)) {
            float ghostCenterY = daw.dragActive
                ? static_cast<float>(ui.cursorY)
                : (startY + static_cast<float>(daw.externalDropIndex) * rowSpan);
            float ghostTop = ghostCenterY - laneHalfH;
            float ghostBottom = ghostCenterY + laneHalfH;
            glm::vec3 ghostColor = glm::clamp(selectedColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
            pushQuad(g_laneVertices,
                     pixelToNDC({laneLeft, ghostTop}, screenWidth, screenHeight),
                     pixelToNDC({laneRight, ghostTop}, screenWidth, screenHeight),
                     pixelToNDC({laneRight, ghostBottom}, screenWidth, screenHeight),
                     pixelToNDC({laneLeft, ghostBottom}, screenWidth, screenHeight),
                     ghostColor);
            if (insertSlot >= 0) {
                float insertY = startY + (static_cast<float>(insertSlot) - 0.5f) * rowSpan;
                float lineHalf = 2.0f;
                pushQuad(g_laneVertices,
                         pixelToNDC({laneLeft, insertY - lineHalf}, screenWidth, screenHeight),
                         pixelToNDC({laneRight, insertY - lineHalf}, screenWidth, screenHeight),
                         pixelToNDC({laneRight, insertY + lineHalf}, screenWidth, screenHeight),
                         pixelToNDC({laneLeft, insertY + lineHalf}, screenWidth, screenHeight),
                         selectedColor);
            }
        }

        g_totalVertexCount = g_laneVertices.size();
        if (g_totalVertexCount == 0 || !renderer.uiColorShader) return;

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendColor(0.0f, 0.0f, 0.0f, kLaneAlpha);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        glBindVertexArray(renderer.uiLaneVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.uiLaneVBO);
        glBufferData(GL_ARRAY_BUFFER, g_totalVertexCount * sizeof(UiVertex), g_laneVertices.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, pos));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, color));
        glEnableVertexAttribArray(1);
        renderer.uiColorShader->use();
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(g_totalVertexCount));
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
    }
}
