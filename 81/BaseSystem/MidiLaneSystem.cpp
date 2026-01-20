#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace MidiLaneSystemLogic {
    namespace {
        constexpr float kLaneAlpha = 0.85f;
        constexpr float kLaneLeftMargin = 40.0f;
        constexpr float kLaneRightMargin = 40.0f;
        constexpr float kLaneHeight = 60.0f;
        constexpr float kLaneGap = 12.0f;
        constexpr size_t kWaveformBlockSize = 256;

        struct UiVertex { glm::vec2 pos; glm::vec3 color; };
        static std::vector<UiVertex> g_laneVertices;
        static uint64_t g_waveVersion = 0;
        static int64_t g_cachedTimelineOffset = 0;
        static double g_cachedSecondsPerScreen = 10.0;
        static float g_cachedScrollY = 0.0f;
        static int g_cachedWidth = 0;
        static int g_cachedHeight = 0;

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
                renderer.uiColorShader = std::make_unique<Shader>(world.shaders["UI_COLOR_VERTEX_SHADER"].c_str(),
                                                                 world.shaders["UI_COLOR_FRAGMENT_SHADER"].c_str());
            }
            if (renderer.uiMidiLaneVAO == 0) {
                glGenVertexArrays(1, &renderer.uiMidiLaneVAO);
                glGenBuffers(1, &renderer.uiMidiLaneVBO);
            }
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
        DawContext& daw = *baseSystem.daw;
        const MidiTrack& track = midi.tracks[0];

        float laneLeft = kLaneLeftMargin;
        float laneRight = static_cast<float>(screenWidth) - kLaneRightMargin;
        if (laneRight < laneLeft + 200.0f) {
            laneRight = laneLeft + 200.0f;
        }
        float scrollY = 0.0f;
        if (baseSystem.uiStamp) {
            scrollY = baseSystem.uiStamp->scrollY;
        }
        float startY = 260.0f + scrollY + static_cast<float>(daw.trackCount) * (kLaneHeight + kLaneGap);
        float laneHalfH = kLaneHeight * 0.5f;
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
        }

        bool rebuildStatic = (windowWidth != g_cachedWidth) || (windowHeight != g_cachedHeight);
        if (std::abs(scrollY - g_cachedScrollY) > 0.01f) {
            rebuildStatic = true;
        }
        if (std::abs(secondsPerScreen - g_cachedSecondsPerScreen) > 0.0001) {
            rebuildStatic = true;
        }
        if (daw.timelineOffsetSamples != g_cachedTimelineOffset) {
            rebuildStatic = true;
        }
        if (track.waveformVersion != g_waveVersion) {
            rebuildStatic = true;
        }

        if (rebuildStatic) {
            g_cachedWidth = windowWidth;
            g_cachedHeight = windowHeight;
            g_cachedScrollY = scrollY;
            g_cachedTimelineOffset = daw.timelineOffsetSamples;
            g_cachedSecondsPerScreen = secondsPerScreen;
            g_waveVersion = track.waveformVersion;
            g_laneVertices.clear();
            g_laneVertices.reserve(60 * 6);

            float centerY = startY;
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

            float waveHeight = kLaneHeight * 0.8f;
            float ampScale = waveHeight * 0.5f;
            int pixelWidth = static_cast<int>(laneRight - laneLeft);
            if (pixelWidth > 0 && !track.waveformMin.empty()) {
                double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
                double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
                if (windowSamples < 0.0) windowSamples = 0.0;
                size_t blockCount = track.waveformMin.size();
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

        if (g_laneVertices.empty()) return;

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendColor(0.0f, 0.0f, 0.0f, kLaneAlpha);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        glBindVertexArray(renderer.uiMidiLaneVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.uiMidiLaneVBO);
        glBufferData(GL_ARRAY_BUFFER, g_laneVertices.size() * sizeof(UiVertex), g_laneVertices.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)(sizeof(glm::vec2)));

        renderer.uiColorShader->use();
        renderer.uiColorShader->setFloat("alpha", kLaneAlpha);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(g_laneVertices.size()));
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
    }
}
