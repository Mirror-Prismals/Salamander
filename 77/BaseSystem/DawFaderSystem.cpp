#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <vector>

#include "DawMixerLayout.h"

namespace DawFaderSystemLogic {
    struct UiVertex {
        glm::vec2 pos;
        glm::vec3 color;
    };

    struct PanelRectF {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    namespace {
        constexpr float kPressAnimSpeed = 0.5f / 0.15f;
        constexpr float kScrollSpeed = 40.0f;
        constexpr float kFaderAlpha = 0.85f;
        constexpr float kDeadZone = 0.02f;
        constexpr float kMinDb = -60.0f;
        constexpr float kMaxDb = 5.0f;

        float faderValueToGain(float value) {
            if (value <= kDeadZone) return 0.0f;
            float t = (value - kDeadZone) / (1.0f - kDeadZone);
            float db = kMinDb + t * (kMaxDb - kMinDb);
            return std::pow(10.0f, db / 20.0f);
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

        glm::vec3 resolveColor(const WorldContext* world, const std::string& name, const glm::vec3& fallback) {
            if (world && !name.empty()) {
                auto it = world->colorLibrary.find(name);
                if (it != world->colorLibrary.end()) return it->second;
            }
            return fallback;
        }

        void buildPanelGeometry(const PanelRectF& rect,
                                float depthPx,
                                const glm::vec3& frontColor,
                                const glm::vec3& topColor,
                                const glm::vec3& sideColor,
                                double width,
                                double height,
                                std::vector<UiVertex>& verts) {
            float bx = rect.x;
            float by = rect.y;
            float bw = rect.w;
            float bh = rect.h;

            glm::vec2 frontA = {bx, by};
            glm::vec2 frontB = {bx + bw, by};
            glm::vec2 frontC = {bx + bw, by + bh};
            glm::vec2 frontD = {bx, by + bh};

            glm::vec2 topA = frontA;
            glm::vec2 topB = frontB;
            glm::vec2 topC = {frontB.x - depthPx, frontB.y - depthPx};
            glm::vec2 topD = {frontA.x - depthPx, frontA.y - depthPx};

            glm::vec2 leftA = frontA;
            glm::vec2 leftB = frontD;
            glm::vec2 leftC = {frontD.x - depthPx, frontD.y - depthPx};
            glm::vec2 leftD = {frontA.x - depthPx, frontA.y - depthPx};

            pushQuad(verts, frontA, frontB, frontC, frontD, frontColor, width, height);
            pushQuad(verts, topA, topB, topC, topD, topColor, width, height);
            pushQuad(verts, leftA, leftB, leftC, leftD, sideColor, width, height);
        }

        void buildButtonGeometry(const glm::vec2& centerPx,
                                 const glm::vec2& halfSizePx,
                                 float depthPx,
                                 float pressAnim,
                                 const glm::vec3& frontColor,
                                 const glm::vec3& topColor,
                                 const glm::vec3& sideColor,
                                 double width,
                                 double height,
                                 std::vector<UiVertex>& verts) {
            float shiftLeft = 10.0f * pressAnim;
            float newDepth = depthPx * (1.0f - 0.5f * pressAnim);

            float bx = centerPx.x - halfSizePx.x - shiftLeft;
            float by = centerPx.y - halfSizePx.y;
            float bw = halfSizePx.x * 2.0f;
            float bh = halfSizePx.y * 2.0f;

            glm::vec2 frontA = {bx, by};
            glm::vec2 frontB = {bx + bw, by};
            glm::vec2 frontC = {bx + bw, by + bh};
            glm::vec2 frontD = {bx, by + bh};

            glm::vec2 topA = frontA;
            glm::vec2 topB = frontB;
            glm::vec2 topC = {frontB.x - newDepth, frontB.y - newDepth};
            glm::vec2 topD = {frontA.x - newDepth, frontA.y - newDepth};

            glm::vec2 leftA = frontA;
            glm::vec2 leftB = frontD;
            glm::vec2 leftC = {frontD.x - newDepth, frontD.y - newDepth};
            glm::vec2 leftD = {frontA.x - newDepth, frontA.y - newDepth};

            pushQuad(verts, frontA, frontB, frontC, frontD, frontColor, width, height);
            pushQuad(verts, topA, topB, topC, topD, topColor, width, height);
            pushQuad(verts, leftA, leftB, leftC, leftD, sideColor, width, height);
        }

        void ensureResources(RendererContext& renderer, WorldContext& world) {
            if (!renderer.uiColorShader) {
                renderer.uiColorShader = std::make_unique<Shader>(world.shaders["UI_COLOR_VERTEX_SHADER"].c_str(),
                                                                 world.shaders["UI_COLOR_FRAGMENT_SHADER"].c_str());
            }
            if (renderer.uiFaderVAO == 0) {
                glGenVertexArrays(1, &renderer.uiFaderVAO);
                glGenBuffers(1, &renderer.uiFaderVBO);
            }
        }

        void ensureFaderState(DawFaderContext& ctx, int trackCount) {
            if (ctx.values.size() != static_cast<size_t>(trackCount)) {
                ctx.values.assign(static_cast<size_t>(trackCount), 0.75f);
                ctx.pressAnim.assign(static_cast<size_t>(trackCount), 0.0f);
                ctx.activeIndex = -1;
                ctx.dragOffsetY = 0.0f;
            }
        }
    }

    void UpdateDawFaders(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes;
        if (!baseSystem.ui || !baseSystem.panel || !baseSystem.daw || !baseSystem.fader || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
        PanelContext& panel = *baseSystem.panel;
        DawContext& daw = *baseSystem.daw;
        DawFaderContext& faders = *baseSystem.fader;

        int trackCount = daw.trackCount > 0 ? std::min(daw.trackCount, DawContext::kTrackCount) : DawContext::kTrackCount;
        ensureFaderState(faders, trackCount);

        for (int i = 0; i < trackCount; ++i) {
            float value = std::clamp(faders.values[i], 0.0f, 1.0f);
            daw.tracks[i].gain.store(faderValueToGain(value), std::memory_order_relaxed);
        }

        if (panel.bottomState <= 0.01f) {
            faders.activeIndex = -1;
            return;
        }

        PanelRect bottomRect = (panel.bottomRenderRect.w > 0.0f) ? panel.bottomRenderRect : panel.bottomRect;
        if (bottomRect.w <= 1.0f || bottomRect.h <= 1.0f) return;

        float contentLeft = bottomRect.x + DawMixerLayout::kPaddingX;
        float contentTop = bottomRect.y + DawMixerLayout::kPaddingY + DawMixerLayout::kPanelTopInset;
        float contentWidth = bottomRect.w - 2.0f * DawMixerLayout::kPaddingX;
        float contentHeight = bottomRect.h - 2.0f * DawMixerLayout::kPaddingY - DawMixerLayout::kPanelTopInset;
        if (contentWidth <= 1.0f || contentHeight <= 1.0f) return;

        float columnSpacing = DawMixerLayout::columnSpacing();
        float contentSpan = columnSpacing * static_cast<float>(trackCount);
        float extraSpace = contentWidth - contentSpan;
        float minScroll = (extraSpace >= 0.0f) ? -extraSpace : extraSpace;
        float maxScroll = (extraSpace >= 0.0f) ? extraSpace : 0.0f;
        if (ui.bottomPanelScrollDelta != 0.0) {
            faders.scrollX += static_cast<float>(ui.bottomPanelScrollDelta) * kScrollSpeed;
            faders.scrollX = std::clamp(faders.scrollX, minScroll, maxScroll);
        }

        float trackTop = contentTop + DawMixerLayout::kFaderTrackInset;
        float trackBottom = contentTop + contentHeight - DawMixerLayout::kFaderTrackInset;
        float knobHalf = DawMixerLayout::kKnobHalfSize;
        float usableHeight = std::max(1.0f, (trackBottom - trackTop) - 2.0f * knobHalf);

        if (ui.uiLeftPressed) {
            for (int i = 0; i < trackCount; ++i) {
                float columnX = contentLeft + faders.scrollX + static_cast<float>(i) * columnSpacing;
                float knobCenterX = columnX + DawMixerLayout::meterBlockWidth()
                                    + DawMixerLayout::kMeterToFaderGap
                                    + DawMixerLayout::faderHousingWidth() * 0.5f;
                float value = std::clamp(faders.values[i], 0.0f, 1.0f);
                float knobCenterY = trackTop + knobHalf + (1.0f - value) * usableHeight;

                float left = knobCenterX - knobHalf;
                float right = knobCenterX + knobHalf;
                float top = knobCenterY - knobHalf;
                float bottom = knobCenterY + knobHalf;
                if (ui.cursorX >= left && ui.cursorX <= right && ui.cursorY >= top && ui.cursorY <= bottom) {
                    faders.activeIndex = i;
                    faders.dragOffsetY = static_cast<float>(knobCenterY - ui.cursorY);
                    ui.consumeClick = true;
                    break;
                }
            }
        }

        if (!ui.uiLeftDown) {
            faders.activeIndex = -1;
        }

        if (faders.activeIndex >= 0 && faders.activeIndex < trackCount && ui.uiLeftDown) {
            float knobCenterY = static_cast<float>(ui.cursorY) + faders.dragOffsetY;
            knobCenterY = std::clamp(knobCenterY, trackTop + knobHalf, trackBottom - knobHalf);
            float t = (knobCenterY - (trackTop + knobHalf)) / usableHeight;
            faders.values[faders.activeIndex] = std::clamp(1.0f - t, 0.0f, 1.0f);
        }

        for (int i = 0; i < trackCount; ++i) {
            float target = (i == faders.activeIndex && ui.uiLeftDown) ? 0.5f : 0.0f;
            float current = faders.pressAnim[i];
            if (current < target) {
                current = std::min(target, current + kPressAnimSpeed * dt);
            } else if (current > target) {
                current = std::max(target, current - kPressAnimSpeed * dt);
            }
            faders.pressAnim[i] = current;
        }
    }

    void RenderDawFaders(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt;
        if (!baseSystem.ui || !baseSystem.panel || !baseSystem.daw || !baseSystem.fader) return;
        if (!baseSystem.renderer || !baseSystem.world || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
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
        if (contentWidth <= 1.0f || contentHeight <= 1.0f) return;

        int trackCount = baseSystem.daw->trackCount > 0
            ? std::min(baseSystem.daw->trackCount, DawContext::kTrackCount)
            : DawContext::kTrackCount;
        DawFaderContext& faders = *baseSystem.fader;
        ensureFaderState(faders, trackCount);

        glm::vec3 front = resolveColor(&world, "ButtonFront", glm::vec3(0.8f));
        glm::vec3 top = resolveColor(&world, "ButtonTopHighlight", glm::vec3(0.9f));
        glm::vec3 side = resolveColor(&world, "ButtonSideShadow", glm::vec3(0.6f));
        glm::vec3 trackColor = resolveColor(&world, "MiraLaneShadow", glm::vec3(0.6f));

        std::vector<UiVertex> vertices;
        vertices.reserve(static_cast<size_t>(trackCount) * 60);

        float columnSpacing = DawMixerLayout::columnSpacing();
        float trackTop = contentTop + DawMixerLayout::kFaderTrackInset;
        float trackBottom = contentTop + contentHeight - DawMixerLayout::kFaderTrackInset;
        float knobHalf = DawMixerLayout::kKnobHalfSize;
        float usableHeight = std::max(1.0f, (trackBottom - trackTop) - 2.0f * knobHalf);

        for (int i = 0; i < trackCount; ++i) {
            float columnX = contentLeft + faders.scrollX + static_cast<float>(i) * columnSpacing;
            float housingLeft = columnX + DawMixerLayout::meterBlockWidth() + DawMixerLayout::kMeterToFaderGap;
            float housingWidth = DawMixerLayout::faderHousingWidth();
            PanelRectF housingRect{housingLeft, contentTop, housingWidth, contentHeight};
            buildPanelGeometry(housingRect, DawMixerLayout::kFaderHousingDepth, front, top, side, screenWidth, screenHeight, vertices);

            float trackCenterX = housingLeft + housingWidth * 0.5f;
            float trackHalfW = DawMixerLayout::kFaderTrackWidth * 0.5f;
            glm::vec2 trackA(trackCenterX - trackHalfW, trackTop);
            glm::vec2 trackB(trackCenterX + trackHalfW, trackTop);
            glm::vec2 trackC(trackCenterX + trackHalfW, trackBottom);
            glm::vec2 trackD(trackCenterX - trackHalfW, trackBottom);
            pushQuad(vertices, trackA, trackB, trackC, trackD, trackColor, screenWidth, screenHeight);

            float value = std::clamp(faders.values[i], 0.0f, 1.0f);
            float knobCenterY = trackTop + knobHalf + (1.0f - value) * usableHeight;
            glm::vec2 knobCenter(trackCenterX, knobCenterY);
            glm::vec2 knobHalfSize(knobHalf, knobHalf);
            buildButtonGeometry(knobCenter, knobHalfSize, DawMixerLayout::kFaderHousingDepth, faders.pressAnim[i],
                                front, top, side, screenWidth, screenHeight, vertices);
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
        glEnable(GL_BLEND);
        glBlendColor(0.0f, 0.0f, 0.0f, kFaderAlpha);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        glBindVertexArray(renderer.uiFaderVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.uiFaderVBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(UiVertex), vertices.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, pos));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, color));
        glEnableVertexAttribArray(1);

        renderer.uiColorShader->use();
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
    }
}
