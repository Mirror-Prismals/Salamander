#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace DawLaneSystemLogic {
    namespace {
        struct UiVertex { glm::vec2 pos; glm::vec3 color; };
        static std::vector<UiVertex> g_laneVertices;
        static size_t g_staticVertexCount = 0;
        static size_t g_totalVertexCount = 0;
        static int g_cachedWidth = 0;
        static int g_cachedHeight = 0;
        static std::array<uint64_t, DawContext::kTrackCount> g_waveVersions{};

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
        if (!baseSystem.ui || !baseSystem.daw || !baseSystem.renderer || !baseSystem.world || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        ensureResources(renderer, world);

        int windowWidth = 0, windowHeight = 0;
        glfwGetWindowSize(win, &windowWidth, &windowHeight);
        double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
        double screenHeight = windowHeight > 0 ? static_cast<double>(windowHeight) : 1080.0;

        DawContext& daw = *baseSystem.daw;
        const float laneHeight = 60.0f;
        const float laneGap = 12.0f;
        const float laneHalfH = laneHeight * 0.5f;
        const float laneLeft = 320.0f;
        float laneRight = static_cast<float>(screenWidth) - 220.0f;
        if (laneRight < laneLeft + 200.0f) {
            laneRight = laneLeft + 200.0f;
        }
        const float startY = 260.0f;

        glm::vec3 laneColor(0.14f, 0.14f, 0.14f);
        glm::vec3 laneHighlight(0.2f, 0.2f, 0.2f);
        auto it = world.colorLibrary.find("DarkGray");
        if (it != world.colorLibrary.end()) {
            laneColor = glm::clamp(it->second + glm::vec3(0.02f), glm::vec3(0.0f), glm::vec3(1.0f));
            laneHighlight = glm::clamp(it->second + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
        }

        bool rebuildStatic = (windowWidth != g_cachedWidth) || (windowHeight != g_cachedHeight);
        for (int t = 0; t < DawContext::kTrackCount; ++t) {
            if (daw.tracks[t].waveformVersion != g_waveVersions[t]) {
                rebuildStatic = true;
                break;
            }
        }

        if (rebuildStatic) {
            g_cachedWidth = windowWidth;
            g_cachedHeight = windowHeight;
            g_laneVertices.clear();
            g_laneVertices.reserve(DawContext::kTrackCount * 18);

            for (int i = 0; i < DawContext::kTrackCount; ++i) {
                float centerY = startY + i * (laneHeight + laneGap);
                float top = centerY - laneHalfH;
                float bottom = centerY + laneHalfH;

                glm::vec2 a(laneLeft, top);
                glm::vec2 b(laneRight, top);
                glm::vec2 c(laneRight, bottom);
                glm::vec2 d(laneLeft, bottom);
                pushQuad(g_laneVertices,
                         pixelToNDC(a, screenWidth, screenHeight),
                         pixelToNDC(b, screenWidth, screenHeight),
                         pixelToNDC(c, screenWidth, screenHeight),
                         pixelToNDC(d, screenWidth, screenHeight),
                         laneColor);

                float highlightH = 4.0f;
                glm::vec2 ha(laneLeft, top);
                glm::vec2 hb(laneRight, top);
                glm::vec2 hc(laneRight, top + highlightH);
                glm::vec2 hd(laneLeft, top + highlightH);
                pushQuad(g_laneVertices,
                         pixelToNDC(ha, screenWidth, screenHeight),
                         pixelToNDC(hb, screenWidth, screenHeight),
                         pixelToNDC(hc, screenWidth, screenHeight),
                         pixelToNDC(hd, screenWidth, screenHeight),
                         laneHighlight);
            }

            const glm::vec3 waveformColor(0.05f, 0.05f, 0.05f);
            float waveHeight = laneHeight * 0.8f;
            float ampScale = waveHeight * 0.5f;
            int pixelWidth = static_cast<int>(laneRight - laneLeft);
            if (pixelWidth > 0) {
                for (int t = 0; t < DawContext::kTrackCount; ++t) {
                    const DawTrack& track = daw.tracks[t];
                    if (track.waveformMin.empty() || track.waveformMax.empty()) continue;
                    size_t blockCount = track.waveformMin.size();
                    if (blockCount == 0) continue;
                    float centerY = startY + t * (laneHeight + laneGap);
                    float top = centerY - laneHalfH;
                    float bottom = centerY + laneHalfH;
                    for (int x = 0; x < pixelWidth; ++x) {
                        size_t idx = (static_cast<size_t>(x) * blockCount) / static_cast<size_t>(pixelWidth);
                        float minVal = track.waveformMin[idx];
                        float maxVal = track.waveformMax[idx];
                        float yTop = centerY - maxVal * ampScale;
                        float yBottom = centerY - minVal * ampScale;
                        yTop = std::max(yTop, top);
                        yBottom = std::min(yBottom, bottom);
                        float xPos = laneLeft + static_cast<float>(x);
                        float lineWidth = 1.0f;
                        glm::vec2 a(xPos - lineWidth, yTop);
                        glm::vec2 b(xPos + lineWidth, yTop);
                        glm::vec2 c(xPos + lineWidth, yBottom);
                        glm::vec2 d(xPos - lineWidth, yBottom);
                        pushQuad(g_laneVertices,
                                 pixelToNDC(a, screenWidth, screenHeight),
                                 pixelToNDC(b, screenWidth, screenHeight),
                                 pixelToNDC(c, screenWidth, screenHeight),
                                 pixelToNDC(d, screenWidth, screenHeight),
                                 waveformColor);
                    }
                }
            }

            g_staticVertexCount = g_laneVertices.size();
            g_totalVertexCount = g_staticVertexCount + 6;
            g_laneVertices.resize(g_totalVertexCount);
            for (int t = 0; t < DawContext::kTrackCount; ++t) {
                g_waveVersions[t] = daw.tracks[t].waveformVersion;
            }
        }

        double secondsPerScreen = 10.0;
        double playheadSec = static_cast<double>(daw.playheadSample.load(std::memory_order_relaxed)) / static_cast<double>(daw.sampleRate);
        double phase = secondsPerScreen > 0.0 ? std::fmod(playheadSec / secondsPerScreen, 1.0) : 0.0;
        float playheadX = static_cast<float>(laneLeft + (laneRight - laneLeft) * phase);
        glm::vec3 playheadColor(0.9f, 0.9f, 0.9f);
        float lineWidth = 2.0f;
        glm::vec2 pa(playheadX - lineWidth, startY - laneHalfH);
        glm::vec2 pb(playheadX + lineWidth, startY - laneHalfH);
        glm::vec2 pc(playheadX + lineWidth, startY + (DawContext::kTrackCount - 1) * (laneHeight + laneGap) + laneHalfH);
        glm::vec2 pd(playheadX - lineWidth, startY + (DawContext::kTrackCount - 1) * (laneHeight + laneGap) + laneHalfH);
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
        if (g_totalVertexCount == 0 || !renderer.uiColorShader) return;

        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(renderer.uiLaneVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.uiLaneVBO);
        if (rebuildStatic || g_staticVertexCount == 0) {
            glBufferData(GL_ARRAY_BUFFER, g_totalVertexCount * sizeof(UiVertex), g_laneVertices.data(), GL_DYNAMIC_DRAW);
        } else {
            glBufferSubData(GL_ARRAY_BUFFER, g_staticVertexCount * sizeof(UiVertex), 6 * sizeof(UiVertex),
                            g_laneVertices.data() + g_staticVertexCount);
        }
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, pos));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, color));
        glEnableVertexAttribArray(1);
        renderer.uiColorShader->use();
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(g_totalVertexCount));
        glEnable(GL_DEPTH_TEST);
    }
}
