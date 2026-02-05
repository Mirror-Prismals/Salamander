#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <vector>

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
        constexpr size_t kWaveformBlockSize = 256;

        struct LaneRenderCache {
            std::vector<DawLaneTimelineSystemLogic::UiVertex> vertices;
            size_t staticVertexCount = 0;
            size_t totalVertexCount = 0;
            int cachedWidth = 0;
            int cachedHeight = 0;
            float cachedScrollY = 0.0f;
            int cachedTrackCount = 0;
            int64_t cachedTimelineOffset = 0;
            double cachedSecondsPerScreen = 10.0;
            double cachedBpm = 120.0;
            std::vector<int> cachedLaneSignature;
            std::vector<uint64_t> waveVersions;
            std::vector<uint64_t> cachedClipSignature;
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

            g_cache.cachedWidth = static_cast<int>(layout.screenWidth);
            g_cache.cachedHeight = static_cast<int>(layout.screenHeight);
            g_cache.cachedScrollY = layout.scrollY;
            g_cache.cachedTrackCount = audioTrackCount;
            g_cache.cachedTimelineOffset = daw.timelineOffsetSamples;
            g_cache.cachedSecondsPerScreen = secondsPerScreen;
            g_cache.cachedBpm = bpm;
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

            const auto audioLaneIndex = DawLaneTimelineSystemLogic::BuildAudioLaneIndex(daw, audioTrackCount);

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
                    pushQuad(g_cache.vertices,
                             pixelToNDC(a, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(b, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(c, layout.screenWidth, layout.screenHeight),
                             pixelToNDC(d, layout.screenWidth, layout.screenHeight),
                             clipColor);
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
                        pushQuad(g_cache.vertices,
                                 pixelToNDC(a, layout.screenWidth, layout.screenHeight),
                                 pixelToNDC(b, layout.screenWidth, layout.screenHeight),
                                 pixelToNDC(c, layout.screenWidth, layout.screenHeight),
                                 pixelToNDC(d, layout.screenWidth, layout.screenHeight),
                                 blockColor);
                    }
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
        }
    }
}
