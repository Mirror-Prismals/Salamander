#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace DawLaneSystemLogic {
    namespace {
        constexpr float kLaneAlpha = 0.85f;
        constexpr float kLaneLeftMargin = 40.0f;
        constexpr float kLaneRightMargin = 40.0f;

        struct UiVertex { glm::vec2 pos; glm::vec3 color; };
        static std::vector<UiVertex> g_laneVertices;
        static size_t g_staticVertexCount = 0;
        static size_t g_totalVertexCount = 0;
        static int g_cachedWidth = 0;
        static int g_cachedHeight = 0;
        static float g_cachedScrollY = 0.0f;
        static int g_cachedTrackCount = DawContext::kTrackCount;
        static int64_t g_cachedTimelineOffset = 0;
        static double g_cachedSecondsPerScreen = 10.0;
        static std::array<uint64_t, DawContext::kTrackCount> g_waveVersions{};
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
    }

    void UpdateDawLanes(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt;
        if (!baseSystem.ui || !baseSystem.daw || !baseSystem.renderer || !baseSystem.world || !baseSystem.level || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
        if (!hasDawUiWorld(*baseSystem.level)) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        ensureResources(renderer, world);

        int windowWidth = 0, windowHeight = 0;
        glfwGetWindowSize(win, &windowWidth, &windowHeight);
        double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
        double screenHeight = windowHeight > 0 ? static_cast<double>(windowHeight) : 1080.0;

        DawContext& daw = *baseSystem.daw;
        int trackCount = daw.trackCount > 0
            ? std::min(daw.trackCount, DawContext::kTrackCount)
            : DawContext::kTrackCount;
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
        const float startY = 260.0f + scrollY;
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
        if (trackCount != g_cachedTrackCount) {
            rebuildStatic = true;
        }
        if (std::abs(secondsPerScreen - g_cachedSecondsPerScreen) > 0.0001) {
            rebuildStatic = true;
        }
        if (daw.timelineOffsetSamples != g_cachedTimelineOffset) {
            rebuildStatic = true;
        }
        for (int t = 0; t < trackCount; ++t) {
            if (daw.tracks[t].waveformVersion != g_waveVersions[t]) {
                rebuildStatic = true;
                break;
            }
        }

        if (rebuildStatic) {
            g_cachedWidth = windowWidth;
            g_cachedHeight = windowHeight;
            g_cachedScrollY = scrollY;
            g_cachedTrackCount = trackCount;
            g_cachedTimelineOffset = daw.timelineOffsetSamples;
            g_cachedSecondsPerScreen = secondsPerScreen;
            g_laneVertices.clear();
            g_laneVertices.reserve(static_cast<size_t>(trackCount) * 18);

            for (int i = 0; i < trackCount; ++i) {
                float centerY = startY + i * (laneHeight + laneGap);
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

            float waveHeight = laneHeight * 0.8f;
            float ampScale = waveHeight * 0.5f;
            int pixelWidth = static_cast<int>(laneRight - laneLeft);
            if (pixelWidth > 0) {
                double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
                double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
                if (windowSamples < 0.0) windowSamples = 0.0;
                for (int t = 0; t < trackCount; ++t) {
                    const DawTrack& track = daw.tracks[t];
                    if (track.waveformMin.empty() || track.waveformMax.empty()) continue;
                    size_t blockCount = track.waveformMin.size();
                    if (blockCount == 0) continue;
                    float centerY = startY + t * (laneHeight + laneGap);
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
            for (int t = 0; t < trackCount; ++t) {
                g_waveVersions[t] = daw.tracks[t].waveformVersion;
            }
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
        glm::vec2 pa(playheadX - lineWidth, startY - laneHalfH);
        glm::vec2 pb(playheadX + lineWidth, startY - laneHalfH);
        glm::vec2 pc(playheadX + lineWidth, startY + (trackCount - 1) * (laneHeight + laneGap) + laneHalfH);
        glm::vec2 pd(playheadX - lineWidth, startY + (trackCount - 1) * (laneHeight + laneGap) + laneHalfH);
        bool showPlayhead = daw.playheadSample.load(std::memory_order_relaxed) > 0;
        if (showPlayhead) {
            std::vector<UiVertex> playheadVerts;
            playheadVerts.reserve(6);
            pushQuad(playheadVerts,
                     pixelToNDC(pa, screenWidth, screenHeight),
                     pixelToNDC(pb, screenWidth, screenHeight),
                     pixelToNDC(pc, screenWidth, screenHeight),
                     pixelToNDC(pd, screenWidth, screenHeight),
                     playheadColor);
            if (playheadVerts.size() == 6 && g_laneVertices.size() >= g_staticVertexCount + 6) {
                std::copy(playheadVerts.begin(), playheadVerts.end(), g_laneVertices.begin() + g_staticVertexCount);
            }
        }
        g_totalVertexCount = g_staticVertexCount + (showPlayhead ? 6 : 0);
        if (g_totalVertexCount == 0 || !renderer.uiColorShader) return;

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendColor(0.0f, 0.0f, 0.0f, kLaneAlpha);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        glBindVertexArray(renderer.uiLaneVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.uiLaneVBO);
        if (rebuildStatic || g_staticVertexCount == 0) {
            glBufferData(GL_ARRAY_BUFFER, (g_staticVertexCount + 6) * sizeof(UiVertex), g_laneVertices.data(), GL_DYNAMIC_DRAW);
        } else if (showPlayhead) {
            glBufferSubData(GL_ARRAY_BUFFER, g_staticVertexCount * sizeof(UiVertex), 6 * sizeof(UiVertex),
                            g_laneVertices.data() + g_staticVertexCount);
        }
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
