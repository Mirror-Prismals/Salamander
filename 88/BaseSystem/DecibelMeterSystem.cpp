#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <vector>

#include "DawMixerLayout.h"

namespace DecibelMeterSystemLogic {
    struct UiVertex {
        glm::vec2 pos;
        glm::vec3 color;
    };

    struct Color {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
    };

    namespace {
        constexpr int kAuxMeterCount = 4;
        constexpr int kAuxMeterMainCk = 0;
        constexpr int kAuxMeterSoundtrack = 1;
        constexpr int kAuxMeterSpeakerBlock = 2;
        constexpr int kAuxMeterPlayerHead = 3;

        Color lerpColor(const Color& a, const Color& b, float t) {
            return {a.r + t * (b.r - a.r), a.g + t * (b.g - a.g), a.b + t * (b.b - a.b)};
        }

        Color darkenColor(const Color& c, float factor = 0.25f) {
            return {c.r * factor, c.g * factor, c.b * factor};
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
                      const glm::vec3& color,
                      double width,
                      double height) {
            verts.push_back({pixelToNDC(a, width, height), color});
            verts.push_back({pixelToNDC(b, width, height), color});
            verts.push_back({pixelToNDC(c, width, height), color});
            verts.push_back({pixelToNDC(a, width, height), color});
            verts.push_back({pixelToNDC(c, width, height), color});
            verts.push_back({pixelToNDC(d, width, height), color});
        }

        float dBToY(float dB, float meterTop, float meterHeight) {
            float t = (dB + 60.0f) / 60.0f;
            return meterTop + (1.0f - t) * meterHeight;
        }

        void ensureResources(RendererContext& renderer, WorldContext& world) {
            if (!renderer.uiColorShader) {
                renderer.uiColorShader = std::make_unique<Shader>(world.shaders["UI_COLOR_VERTEX_SHADER"].c_str(),
                                                                 world.shaders["UI_COLOR_FRAGMENT_SHADER"].c_str());
            }
            if (renderer.uiMeterVAO == 0) {
                glGenVertexArrays(1, &renderer.uiMeterVAO);
                glGenBuffers(1, &renderer.uiMeterVBO);
            }
        }

        void renderChannelMeter(std::vector<UiVertex>& verts,
                                float meterLeft,
                                float meterRight,
                                float meterTop,
                                float meterHeight,
                                float level,
                                bool invertPalette,
                                double screenWidth,
                                double screenHeight) {
            const float dBMin = -60.0f;
            const float dBMax = 0.0f;
            const int steps = 60;
            float stepDb = (dBMax - dBMin) / steps;

            Color blue = {0.0f, 0.0f, 1.0f};
            Color cyan = {0.0f, 1.0f, 1.0f};
            Color green = {0.0f, 1.0f, 0.0f};
            Color yellow = {1.0f, 1.0f, 0.0f};
            Color orange = {1.0f, 0.65f, 0.0f};
            Color red = {1.0f, 0.0f, 0.0f};

            if (level < 0.000001f) level = 0.000001f;
            float dB = 20.0f * std::log10(level);
            dB = std::clamp(dB, dBMin, dBMax);

            for (int i = 0; i < steps; ++i) {
                float segLow = dBMin + i * stepDb;
                float segHigh = segLow + stepDb;

                float yA = dBToY(segLow, meterTop, meterHeight);
                float yB = dBToY(segHigh, meterTop, meterHeight);
                float yTop = std::min(yA, yB);
                float yBottom = std::max(yA, yB);

                float t = ((segLow + segHigh) * 0.5f - dBMin) / (dBMax - dBMin);
                Color fullColor;
                if (t < 0.2f) {
                    fullColor = lerpColor(blue, cyan, t / 0.2f);
                } else if (t < 0.4f) {
                    fullColor = lerpColor(cyan, green, (t - 0.2f) / 0.2f);
                } else if (t < 0.6f) {
                    fullColor = lerpColor(green, yellow, (t - 0.4f) / 0.2f);
                } else if (t < 0.8f) {
                    fullColor = lerpColor(yellow, orange, (t - 0.6f) / 0.2f);
                } else {
                    fullColor = lerpColor(orange, red, (t - 0.8f) / 0.2f);
                }
                if (invertPalette) {
                    fullColor = {1.0f - fullColor.r, 1.0f - fullColor.g, 1.0f - fullColor.b};
                }
                Color darkColor = darkenColor(fullColor, 0.25f);
                glm::vec3 full(fullColor.r, fullColor.g, fullColor.b);
                glm::vec3 dark(darkColor.r, darkColor.g, darkColor.b);

                glm::vec2 a(meterLeft, yTop);
                glm::vec2 b(meterRight, yTop);
                glm::vec2 c(meterRight, yBottom);
                glm::vec2 d(meterLeft, yBottom);

                if (dB >= segHigh) {
                    pushQuad(verts, a, b, c, d, full, screenWidth, screenHeight);
                } else if (dB <= segLow) {
                    pushQuad(verts, a, b, c, d, dark, screenWidth, screenHeight);
                } else {
                    float yPartial = std::clamp(dBToY(dB, meterTop, meterHeight), yTop, yBottom);
                    glm::vec2 p1(meterLeft, yPartial);
                    glm::vec2 p2(meterRight, yPartial);
                    pushQuad(verts, a, b, p2, p1, dark, screenWidth, screenHeight);
                    pushQuad(verts, p1, p2, c, d, full, screenWidth, screenHeight);
                }
            }
        }
    }

    void UpdateDecibelMeters(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)win;
        if (!baseSystem.decibelMeter || !baseSystem.daw || !baseSystem.ui) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
        DecibelMeterContext& meter = *baseSystem.decibelMeter;
        DawContext& daw = *baseSystem.daw;

        int audioTrackCount = static_cast<int>(daw.tracks.size());
        int midiTrackCount = baseSystem.midi ? baseSystem.midi->trackCount : 0;
        int baseTrackCount = audioTrackCount + midiTrackCount;
        int trackCount = baseTrackCount + kAuxMeterCount;
        if (meter.displayLevels.size() != static_cast<size_t>(trackCount)) {
            meter.displayLevels.assign(static_cast<size_t>(trackCount), 0.0f);
        }

        for (int i = 0; i < trackCount; ++i) {
            float target = 0.0f;
            if (i < audioTrackCount) {
                target = daw.tracks[i].meterLevel.load(std::memory_order_relaxed);
            } else if (baseSystem.midi) {
                int midiIndex = i - audioTrackCount;
                if (midiIndex >= 0 && midiIndex < baseSystem.midi->trackCount) {
                    target = baseSystem.midi->tracks[midiIndex].meterLevel.load(std::memory_order_relaxed);
                }
            }
            if (i >= baseTrackCount && baseSystem.audio) {
                int auxIndex = i - baseTrackCount;
                if (auxIndex == kAuxMeterMainCk) {
                    target = baseSystem.audio->chuckMainMeterLevel.load(std::memory_order_relaxed);
                } else if (auxIndex == kAuxMeterSoundtrack) {
                    target = baseSystem.audio->soundtrackMeterLevel.load(std::memory_order_relaxed);
                } else if (auxIndex == kAuxMeterSpeakerBlock) {
                    target = baseSystem.audio->speakerBlockMeterLevel.load(std::memory_order_relaxed);
                } else if (auxIndex == kAuxMeterPlayerHead) {
                    target = baseSystem.audio->playerHeadSpeakerMeterLevel.load(std::memory_order_relaxed);
                }
            }
            float current = meter.displayLevels[i];
            if (target > current) {
                current += (target - current) * meter.attackSpeed * dt;
            } else {
                current = std::max(target, current - meter.decayPerSecond * dt);
            }
            meter.displayLevels[i] = std::clamp(current, 0.0f, 1.0f);
        }

        for (int i = 0; i < 4; ++i) {
            float target = daw.masterBusLevels[static_cast<size_t>(i)].load(std::memory_order_relaxed);
            float current = meter.masterDisplayLevels[static_cast<size_t>(i)];
            if (target > current) {
                current += (target - current) * meter.attackSpeed * dt;
            } else {
                current = std::max(target, current - meter.decayPerSecond * dt);
            }
            meter.masterDisplayLevels[static_cast<size_t>(i)] = std::clamp(current, 0.0f, 1.0f);
        }
    }

    void RenderDecibelMeters(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt;
        if (!baseSystem.decibelMeter || !baseSystem.daw || !baseSystem.ui || !baseSystem.panel) return;
        if (!baseSystem.renderer || !baseSystem.world || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
        if (ui.bottomPanelPage == 1) return;
        PanelContext& panel = *baseSystem.panel;
        if (panel.bottomState <= 0.01f) return;

        PanelRect bottomRect = (panel.bottomRenderRect.w > 0.0f) ? panel.bottomRenderRect : panel.bottomRect;
        if (bottomRect.w <= 1.0f || bottomRect.h <= 1.0f) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        ensureResources(renderer, world);
        if (!renderer.uiColorShader) return;

        int windowWidth = 0, windowHeight = 0;
        int fbWidth = 0, fbHeight = 0;
        glfwGetWindowSize(win, &windowWidth, &windowHeight);
        glfwGetFramebufferSize(win, &fbWidth, &fbHeight);
        double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
        double screenHeight = windowHeight > 0 ? static_cast<double>(windowHeight) : 1080.0;

        float contentLeft = bottomRect.x + DawMixerLayout::kPaddingX;
        float contentTop = bottomRect.y + DawMixerLayout::kPaddingY + DawMixerLayout::kPanelTopInset;
        float contentWidth = bottomRect.w - 2.0f * DawMixerLayout::kPaddingX;
        float contentHeight = bottomRect.h - 2.0f * DawMixerLayout::kPaddingY - DawMixerLayout::kPanelTopInset;
        contentLeft += DawMixerLayout::navBlockWidth();
        contentWidth = std::max(0.0f, contentWidth - DawMixerLayout::navBlockWidth());
        if (contentWidth <= 1.0f || contentHeight <= 1.0f) return;

        int audioTrackCount = static_cast<int>(baseSystem.daw->tracks.size());
        int midiTrackCount = baseSystem.midi ? baseSystem.midi->trackCount : 0;
        int baseTrackCount = audioTrackCount + midiTrackCount;
        int trackCount = baseTrackCount + kAuxMeterCount;
        constexpr int kMasterMeterCount = 4;

        float scrollX = baseSystem.fader ? baseSystem.fader->scrollX : 0.0f;

        std::vector<UiVertex> vertices;
        vertices.reserve(static_cast<size_t>(trackCount + kMasterMeterCount) * 60 * 2 * 6);

        for (int i = 0; i < trackCount; ++i) {
            float columnX = contentLeft + scrollX + static_cast<float>(i) * DawMixerLayout::columnSpacing();
            float meterLeft = columnX;
            float meterRight = meterLeft + DawMixerLayout::meterBlockWidth();
            float meterTop = contentTop;
            float meterHeight = contentHeight;

            float level = 0.0f;
            if (i < static_cast<int>(baseSystem.decibelMeter->displayLevels.size())) {
                level = baseSystem.decibelMeter->displayLevels[i];
            }
            int busL = -1;
            int busR = -1;
            if (i < audioTrackCount) {
                busL = baseSystem.daw->tracks[i].outputBusL.load(std::memory_order_relaxed);
                busR = baseSystem.daw->tracks[i].outputBusR.load(std::memory_order_relaxed);
            } else if (i < baseTrackCount && baseSystem.midi) {
                int midiIndex = i - audioTrackCount;
                if (midiIndex >= 0 && midiIndex < baseSystem.midi->trackCount) {
                    busL = baseSystem.midi->tracks[midiIndex].outputBusL.load(std::memory_order_relaxed);
                    busR = baseSystem.midi->tracks[midiIndex].outputBusR.load(std::memory_order_relaxed);
                }
            }
            float leftLevel = 0.0f;
            float rightLevel = 0.0f;
            auto addBusLevel = [&](int bus, float amount) {
                if (bus == 0) {
                    leftLevel += amount;
                } else if (bus == 3) {
                    rightLevel += amount;
                } else if (bus == 1 || bus == 2) {
                    leftLevel += amount;
                    rightLevel += amount;
                }
            };
            if (i < baseTrackCount) {
                if (busL >= 0 && busL <= 3) {
                    addBusLevel(busL, level);
                }
                if (busR >= 0 && busR <= 3) {
                    if (busR != busL) {
                        addBusLevel(busR, level);
                    } else if (busR == 1 || busR == 2) {
                        // Keep center-routed channels readable without over-brightening side-only routes.
                        addBusLevel(busR, level * 0.5f);
                    }
                }
            } else {
                leftLevel = level;
                rightLevel = level;
            }
            leftLevel = std::clamp(leftLevel, 0.0f, 1.0f);
            rightLevel = std::clamp(rightLevel, 0.0f, 1.0f);

            float channelWidth = DawMixerLayout::kMeterWidth;
            float gap = DawMixerLayout::kMeterGap;
            float leftChanLeft = meterLeft;
            float leftChanRight = leftChanLeft + channelWidth;
            float rightChanLeft = leftChanRight + gap;
            float rightChanRight = rightChanLeft + channelWidth;

            renderChannelMeter(vertices, leftChanLeft, leftChanRight, meterTop, meterHeight,
                               leftLevel, false, screenWidth, screenHeight);
            renderChannelMeter(vertices, rightChanLeft, rightChanRight, meterTop, meterHeight,
                               rightLevel, false, screenWidth, screenHeight);
        }

        float masterStartX = contentLeft + scrollX + static_cast<float>(trackCount) * DawMixerLayout::columnSpacing()
            + DawMixerLayout::kColumnGap;
        float masterSpacing = DawMixerLayout::meterBlockWidth() + DawMixerLayout::kColumnGap * 0.5f;
        for (int i = 0; i < kMasterMeterCount; ++i) {
            float meterLeft = masterStartX + static_cast<float>(i) * masterSpacing;
            float meterRight = meterLeft + DawMixerLayout::meterBlockWidth();
            float meterTop = contentTop;
            float meterHeight = contentHeight;

            float level = baseSystem.decibelMeter->masterDisplayLevels[static_cast<size_t>(i)];
            renderChannelMeter(vertices, meterLeft, meterRight, meterTop, meterHeight,
                               level, true, screenWidth, screenHeight);
        }

        if (vertices.empty()) return;

        glEnable(GL_SCISSOR_TEST);
        float scaleX = (windowWidth > 0) ? static_cast<float>(fbWidth) / static_cast<float>(windowWidth) : 1.0f;
        float scaleY = (windowHeight > 0) ? static_cast<float>(fbHeight) / static_cast<float>(windowHeight) : 1.0f;
        float bleed = DawMixerLayout::kMixerScissorBleed;
        float scissorXf = (bottomRect.x - bleed) * scaleX;
        float scissorYf = (static_cast<float>(screenHeight) - (bottomRect.y + bottomRect.h + bleed)) * scaleY;
        float scissorWf = (bottomRect.w + bleed) * scaleX;
        float scissorHf = (bottomRect.h + bleed) * scaleY;
        int scissorX = std::max(0, static_cast<int>(scissorXf));
        int scissorY = std::max(0, static_cast<int>(scissorYf));
        int scissorW = std::max(0, std::min(fbWidth, static_cast<int>(scissorWf)));
        int scissorH = std::max(0, std::min(fbHeight, static_cast<int>(scissorHf)));
        glScissor(scissorX, scissorY, scissorW, scissorH);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glBindVertexArray(renderer.uiMeterVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.uiMeterVBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(UiVertex), vertices.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, pos));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, color));
        glEnableVertexAttribArray(1);

        renderer.uiColorShader->use();
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
        glEnable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
    }
}
