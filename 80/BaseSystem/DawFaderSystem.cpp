#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <vector>

#include "BaseSystem/Vst3Host.h"
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
        constexpr float kFxInsertSize = 72.0f;
        constexpr float kFxInsertGap = 18.0f;
        constexpr float kFxInsertDepth = 8.0f;
        constexpr float kFxRemoveSize = 28.0f;
        constexpr float kFxRemoveGap = 8.0f;

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

        struct NavButtonState {
            float pressAnim = 0.0f;
            bool tracking = false;
        };

        struct FxButton {
            Vst3Plugin* plugin = nullptr;
            PanelRectF rect;
            PanelRectF removeRect;
        };

        static std::array<NavButtonState, 4> g_navButtons{};
        static std::unordered_map<Vst3Plugin*, float> g_fxPressAnim;
        static Vst3Plugin* g_fxActivePress = nullptr;
        static Vst3Plugin* g_fxRemovePress = nullptr;

        void pushTri(std::vector<UiVertex>& verts,
                     const glm::vec2& a,
                     const glm::vec2& b,
                     const glm::vec2& c,
                     const glm::vec3& color,
                     double width,
                     double height) {
            verts.push_back({pixelToNDC(a, width, height), color});
            verts.push_back({pixelToNDC(b, width, height), color});
            verts.push_back({pixelToNDC(c, width, height), color});
        }

        bool hitRect(const PanelRectF& rect, const UIContext& ui) {
            float x = static_cast<float>(ui.cursorX);
            float y = static_cast<float>(ui.cursorY);
            return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
        }

        void updateNavButton(NavButtonState& state,
                             const PanelRectF& rect,
                             UIContext& ui,
                             float dt,
                             const std::function<void()>& onClick) {
            bool inside = hitRect(rect, ui);
            if (ui.uiLeftPressed && inside) {
                state.tracking = true;
            }
            if (ui.uiLeftReleased) {
                if (state.tracking && inside && onClick) {
                    onClick();
                    ui.consumeClick = true;
                }
                state.tracking = false;
            }
            float target = (state.tracking && ui.uiLeftDown) ? 0.5f : 0.0f;
            float current = state.pressAnim;
            if (current < target) {
                current = std::min(target, current + kPressAnimSpeed * dt);
            } else if (current > target) {
                current = std::max(target, current - kPressAnimSpeed * dt);
            }
            state.pressAnim = current;
        }

        void computeNavRects(const PanelRect& bottomRect,
                             PanelRectF& pagePrev,
                             PanelRectF& pageNext,
                             PanelRectF& trackPrev,
                             PanelRectF& trackNext) {
            float contentLeft = bottomRect.x + DawMixerLayout::kPaddingX;
            float contentTop = bottomRect.y + DawMixerLayout::kPaddingY + DawMixerLayout::kPanelTopInset;
            float half = DawMixerLayout::kNavButtonSize * 0.5f;
            float row1Y = contentTop + half;
            float row2Y = row1Y + DawMixerLayout::kNavButtonSize + DawMixerLayout::kNavRowGap;
            float x0 = contentLeft + half;
            float x1 = x0 + DawMixerLayout::kNavButtonSize + DawMixerLayout::kNavButtonGap;
            pagePrev = PanelRectF{x0 - half, row1Y - half, DawMixerLayout::kNavButtonSize, DawMixerLayout::kNavButtonSize};
            pageNext = PanelRectF{x1 - half, row1Y - half, DawMixerLayout::kNavButtonSize, DawMixerLayout::kNavButtonSize};
            trackPrev = PanelRectF{x0 - half, row2Y - half, DawMixerLayout::kNavButtonSize, DawMixerLayout::kNavButtonSize};
            trackNext = PanelRectF{x1 - half, row2Y - half, DawMixerLayout::kNavButtonSize, DawMixerLayout::kNavButtonSize};
        }

        std::vector<FxButton> buildFxButtons(const PanelRect& bottomRect,
                                             Vst3Context* vst3,
                                             int trackIndex,
                                             int audioTrackCount) {
            std::vector<FxButton> buttons;
            if (!vst3) return buttons;
            if (trackIndex < 0) return buttons;

            float contentLeft = bottomRect.x + DawMixerLayout::kPaddingX;
            float contentTop = bottomRect.y + DawMixerLayout::kPaddingY + DawMixerLayout::kPanelTopInset;
            float contentHeight = bottomRect.h - 2.0f * DawMixerLayout::kPaddingY - DawMixerLayout::kPanelTopInset;
            contentLeft += DawMixerLayout::navBlockWidth();
            float navHeight = DawMixerLayout::navBlockHeight();
            float availableHeight = contentHeight - navHeight;
            if (availableHeight <= 0.0f) return buttons;
            float centerY = contentTop + navHeight + availableHeight * 0.5f;

            float x = contentLeft + kFxInsertSize * 0.5f;
            if (trackIndex < audioTrackCount && trackIndex < static_cast<int>(vst3->audioTracks.size())) {
                const auto& chain = vst3->audioTracks[trackIndex].effects;
                for (auto* plugin : chain) {
                PanelRectF rect{x - kFxInsertSize * 0.5f, centerY - kFxInsertSize * 0.5f,
                                kFxInsertSize, kFxInsertSize};
                PanelRectF removeRect{rect.x + rect.w * 0.5f - kFxRemoveSize * 0.5f,
                                      rect.y - kFxRemoveGap - kFxRemoveSize,
                                      kFxRemoveSize, kFxRemoveSize};
                buttons.push_back({plugin, rect, removeRect});
                x += kFxInsertSize + kFxInsertGap;
            }
        } else {
            if (vst3->midiInstrument) {
                PanelRectF rect{x - kFxInsertSize * 0.5f, centerY - kFxInsertSize * 0.5f,
                                kFxInsertSize, kFxInsertSize};
                PanelRectF removeRect{rect.x + rect.w * 0.5f - kFxRemoveSize * 0.5f,
                                      rect.y - kFxRemoveGap - kFxRemoveSize,
                                      kFxRemoveSize, kFxRemoveSize};
                buttons.push_back({vst3->midiInstrument, rect, removeRect});
                x += kFxInsertSize + kFxInsertGap;
            }
            for (auto* plugin : vst3->midiFx.effects) {
                PanelRectF rect{x - kFxInsertSize * 0.5f, centerY - kFxInsertSize * 0.5f,
                                kFxInsertSize, kFxInsertSize};
                PanelRectF removeRect{rect.x + rect.w * 0.5f - kFxRemoveSize * 0.5f,
                                      rect.y - kFxRemoveGap - kFxRemoveSize,
                                      kFxRemoveSize, kFxRemoveSize};
                buttons.push_back({plugin, rect, removeRect});
                x += kFxInsertSize + kFxInsertGap;
            }
        }
        return buttons;
        }

        void updateFxChainUI(const PanelRect& bottomRect,
                             Vst3Context* vst3,
                             UIContext& ui,
                             float dt,
                             int trackIndex,
                             int audioTrackCount) {
            if (!vst3) return;
            auto buttons = buildFxButtons(bottomRect, vst3, trackIndex, audioTrackCount);
            if (buttons.empty()) {
                g_fxActivePress = nullptr;
                return;
            }

            if (ui.uiLeftPressed) {
                for (const auto& btn : buttons) {
                    if (!btn.plugin) continue;
                    if (hitRect(btn.removeRect, ui)) {
                        g_fxRemovePress = btn.plugin;
                        break;
                    }
                    if (hitRect(btn.rect, ui)) {
                        g_fxActivePress = btn.plugin;
                        break;
                    }
                }
            }
            if (ui.uiLeftReleased) {
                if (g_fxRemovePress) {
                    for (const auto& btn : buttons) {
                        if (btn.plugin != g_fxRemovePress) continue;
                        if (hitRect(btn.removeRect, ui)) {
                            if (Vst3SystemLogic::RemovePluginFromTrack(*vst3, btn.plugin, trackIndex, audioTrackCount)) {
                                g_fxPressAnim.erase(btn.plugin);
                                ui.consumeClick = true;
                            }
                            break;
                        }
                    }
                } else if (g_fxActivePress) {
                    for (const auto& btn : buttons) {
                        if (btn.plugin != g_fxActivePress) continue;
                        if (hitRect(btn.rect, ui)) {
                            if (btn.plugin->uiWindow) {
                                if (btn.plugin->uiVisible) {
                                    Vst3UI_HideWindow(btn.plugin->uiWindow);
                                    btn.plugin->uiVisible = false;
                                } else {
                                    Vst3SystemLogic::Vst3_OpenPluginUI(*btn.plugin);
                                }
                                ui.consumeClick = true;
                            } else {
                                Vst3SystemLogic::Vst3_OpenPluginUI(*btn.plugin);
                                if (btn.plugin->uiWindow) {
                                    ui.consumeClick = true;
                                }
                            }
                            break;
                        }
                    }
                }
                g_fxActivePress = nullptr;
                g_fxRemovePress = nullptr;
            }

            for (const auto& btn : buttons) {
                if (!btn.plugin) continue;
                float target = (btn.plugin == g_fxActivePress && ui.uiLeftDown) ? 0.5f : 0.0f;
                float current = g_fxPressAnim[btn.plugin];
                if (current < target) {
                    current = std::min(target, current + kPressAnimSpeed * dt);
                } else if (current > target) {
                    current = std::max(target, current - kPressAnimSpeed * dt);
                }
                g_fxPressAnim[btn.plugin] = current;
            }
        }

        void renderNavButtons(std::vector<UiVertex>& verts,
                              const PanelRectF& rect,
                              float pressAnim,
                              const glm::vec3& front,
                              const glm::vec3& top,
                              const glm::vec3& side,
                              const glm::vec3& glyphColor,
                              double width,
                              double height,
                              const char glyph) {
            glm::vec2 center(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f);
            glm::vec2 half(rect.w * 0.5f, rect.h * 0.5f);
            buildButtonGeometry(center, half, DawMixerLayout::kFaderHousingDepth, pressAnim,
                                front, top, side, width, height, verts);

            float shiftLeft = 10.0f * pressAnim;
            center.x -= shiftLeft;
            float size = rect.w * 0.28f;
            if (glyph == '<' || glyph == '>') {
                if (glyph == '<') {
                    glm::vec2 a(center.x - size, center.y);
                    glm::vec2 b(center.x + size, center.y - size);
                    glm::vec2 c(center.x + size, center.y + size);
                    pushTri(verts, a, b, c, glyphColor, width, height);
                } else {
                    glm::vec2 a(center.x + size, center.y);
                    glm::vec2 b(center.x - size, center.y - size);
                    glm::vec2 c(center.x - size, center.y + size);
                    pushTri(verts, a, b, c, glyphColor, width, height);
                }
            } else {
                float stroke = rect.w * 0.12f;
                float inset = rect.w * 0.22f;
                float topY = center.y - size;
                float bottomY = center.y + size;
                if (glyph == '{') {
                    PanelRectF v{center.x - size - inset, topY, stroke, bottomY - topY};
                    PanelRectF t{center.x - size - inset, topY, size, stroke};
                    PanelRectF b{center.x - size - inset, bottomY - stroke, size, stroke};
                    pushQuad(verts,
                             {t.x, t.y},
                             {t.x + t.w, t.y},
                             {t.x + t.w, t.y + t.h},
                             {t.x, t.y + t.h},
                             glyphColor,
                             width,
                             height);
                    pushQuad(verts,
                             {v.x, v.y},
                             {v.x + v.w, v.y},
                             {v.x + v.w, v.y + v.h},
                             {v.x, v.y + v.h},
                             glyphColor,
                             width,
                             height);
                    pushQuad(verts,
                             {b.x, b.y},
                             {b.x + b.w, b.y},
                             {b.x + b.w, b.y + b.h},
                             {b.x, b.y + b.h},
                             glyphColor,
                             width,
                             height);
                } else if (glyph == '}') {
                    PanelRectF v{center.x + size + inset - stroke, topY, stroke, bottomY - topY};
                    PanelRectF t{center.x + inset, topY, size, stroke};
                    PanelRectF b{center.x + inset, bottomY - stroke, size, stroke};
                    pushQuad(verts,
                             {t.x, t.y},
                             {t.x + t.w, t.y},
                             {t.x + t.w, t.y + t.h},
                             {t.x, t.y + t.h},
                             glyphColor,
                             width,
                             height);
                    pushQuad(verts,
                             {v.x, v.y},
                             {v.x + v.w, v.y},
                             {v.x + v.w, v.y + v.h},
                             {v.x, v.y + v.h},
                             glyphColor,
                             width,
                             height);
                    pushQuad(verts,
                             {b.x, b.y},
                             {b.x + b.w, b.y},
                             {b.x + b.w, b.y + b.h},
                             {b.x, b.y + b.h},
                             glyphColor,
                             width,
                             height);
                }
            }
        }

        void renderFxChain(std::vector<UiVertex>& verts,
                           const PanelRect& bottomRect,
                           Vst3Context* vst3,
                           int trackIndex,
                           int audioTrackCount,
                           const glm::vec3& baseFront,
                           const glm::vec3& baseTop,
                           const glm::vec3& baseSide,
                           const glm::vec3& activeFront,
                           double width,
                           double height) {
            if (!vst3) return;
            auto buttons = buildFxButtons(bottomRect, vst3, trackIndex, audioTrackCount);
            if (buttons.empty()) return;
            for (const auto& btn : buttons) {
                if (!btn.plugin) continue;
                float pressAnim = g_fxPressAnim[btn.plugin];
                glm::vec3 front = btn.plugin->uiVisible ? activeFront : baseFront;
                glm::vec2 center(btn.rect.x + btn.rect.w * 0.5f, btn.rect.y + btn.rect.h * 0.5f);
                glm::vec2 half(btn.rect.w * 0.5f, btn.rect.h * 0.5f);
                buildButtonGeometry(center, half, kFxInsertDepth, pressAnim,
                                    front, baseTop, baseSide, width, height, verts);
                if (btn.plugin->uiVisible) {
                    float inset = btn.rect.w * 0.08f;
                    PanelRectF insetRect{
                        btn.rect.x + inset,
                        btn.rect.y + inset,
                        btn.rect.w - 2.0f * inset,
                        btn.rect.h - 2.0f * inset
                    };
                    buildPanelGeometry(insetRect, kFxInsertDepth * 0.5f, front, baseTop, baseSide, width, height, verts);
                }

                glm::vec2 rCenter(btn.removeRect.x + btn.removeRect.w * 0.5f,
                                  btn.removeRect.y + btn.removeRect.h * 0.5f);
                glm::vec2 rHalf(btn.removeRect.w * 0.5f, btn.removeRect.h * 0.5f);
                buildButtonGeometry(rCenter, rHalf, DawMixerLayout::kFaderHousingDepth, 0.0f,
                                    baseFront, baseTop, baseSide, width, height, verts);
                auto pushDiag = [&](float x0, float y0, float x1, float y1, float thickness) {
                    glm::vec2 dir(x1 - x0, y1 - y0);
                    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                    if (len <= 0.001f) return;
                    dir.x /= len;
                    dir.y /= len;
                    glm::vec2 perp(-dir.y, dir.x);
                    perp *= (thickness * 0.5f);
                    glm::vec2 a(x0 + perp.x, y0 + perp.y);
                    glm::vec2 b(x0 - perp.x, y0 - perp.y);
                    glm::vec2 c(x1 - perp.x, y1 - perp.y);
                    glm::vec2 d(x1 + perp.x, y1 + perp.y);
                    pushQuad(verts, a, b, c, d, baseSide, width, height);
                };
                float inset = btn.removeRect.w * 0.24f;
                float stroke = btn.removeRect.w * 0.12f;
                float x0 = btn.removeRect.x + inset;
                float y0 = btn.removeRect.y + inset;
                float x1 = btn.removeRect.x + btn.removeRect.w - inset;
                float y1 = btn.removeRect.y + btn.removeRect.h - inset;
                pushDiag(x0, y0, x1, y1, stroke);
                pushDiag(x0, y1, x1, y0, stroke);
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

        int audioTrackCount = daw.trackCount > 0 ? std::min(daw.trackCount, DawContext::kTrackCount) : DawContext::kTrackCount;
        int midiTrackCount = baseSystem.midi ? baseSystem.midi->trackCount : 0;
        int trackCount = audioTrackCount + midiTrackCount;
        ensureFaderState(faders, trackCount);

        if (panel.bottomState <= 0.01f) {
            faders.activeIndex = -1;
            return;
        }

        PanelRect bottomRect = (panel.bottomRenderRect.w > 0.0f) ? panel.bottomRenderRect : panel.bottomRect;
        if (bottomRect.w <= 1.0f || bottomRect.h <= 1.0f) return;

        ui.bottomPanelPage = std::clamp(ui.bottomPanelPage, 0, 1);
        ui.bottomPanelTrack = std::clamp(ui.bottomPanelTrack, 0, std::max(0, trackCount - 1));

        PanelRectF pagePrev{}, pageNext{}, trackPrev{}, trackNext{};
        computeNavRects(bottomRect, pagePrev, pageNext, trackPrev, trackNext);
        updateNavButton(g_navButtons[0], pagePrev, ui, dt, [&]() {
            ui.bottomPanelPage = std::max(0, ui.bottomPanelPage - 1);
        });
        updateNavButton(g_navButtons[1], pageNext, ui, dt, [&]() {
            ui.bottomPanelPage = std::min(1, ui.bottomPanelPage + 1);
        });
        if (ui.bottomPanelPage == 1) {
            updateNavButton(g_navButtons[2], trackPrev, ui, dt, [&]() {
                ui.bottomPanelTrack = std::max(0, ui.bottomPanelTrack - 1);
            });
            updateNavButton(g_navButtons[3], trackNext, ui, dt, [&]() {
                ui.bottomPanelTrack = std::min(std::max(0, trackCount - 1), ui.bottomPanelTrack + 1);
            });
        } else {
            g_navButtons[2].tracking = false;
            g_navButtons[2].pressAnim = 0.0f;
            g_navButtons[3].tracking = false;
            g_navButtons[3].pressAnim = 0.0f;
        }

        if (ui.bottomPanelPage == 1) {
            Vst3Context* vst3 = baseSystem.audio ? baseSystem.audio->vst3 : nullptr;
            updateFxChainUI(bottomRect, vst3, ui, dt, ui.bottomPanelTrack, audioTrackCount);
            faders.activeIndex = -1;
            return;
        }

        for (int i = 0; i < trackCount; ++i) {
            float value = std::clamp(faders.values[i], 0.0f, 1.0f);
            float gain = faderValueToGain(value);
            if (i < audioTrackCount) {
                daw.tracks[i].gain.store(gain, std::memory_order_relaxed);
            } else if (baseSystem.midi) {
                int midiIndex = i - audioTrackCount;
                if (midiIndex >= 0 && midiIndex < baseSystem.midi->trackCount) {
                    baseSystem.midi->tracks[midiIndex].gain.store(gain, std::memory_order_relaxed);
                }
            }
        }

        float contentLeft = bottomRect.x + DawMixerLayout::kPaddingX;
        float contentTop = bottomRect.y + DawMixerLayout::kPaddingY + DawMixerLayout::kPanelTopInset;
        float contentWidth = bottomRect.w - 2.0f * DawMixerLayout::kPaddingX;
        float contentHeight = bottomRect.h - 2.0f * DawMixerLayout::kPaddingY - DawMixerLayout::kPanelTopInset;
        contentLeft += DawMixerLayout::navBlockWidth();
        contentWidth = std::max(0.0f, contentWidth - DawMixerLayout::navBlockWidth());
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
        contentLeft += DawMixerLayout::navBlockWidth();
        contentWidth = std::max(0.0f, contentWidth - DawMixerLayout::navBlockWidth());
        if (contentWidth <= 1.0f || contentHeight <= 1.0f) return;

        int audioTrackCount = baseSystem.daw->trackCount > 0
            ? std::min(baseSystem.daw->trackCount, DawContext::kTrackCount)
            : DawContext::kTrackCount;
        int midiTrackCount = baseSystem.midi ? baseSystem.midi->trackCount : 0;
        int trackCount = audioTrackCount + midiTrackCount;
        DawFaderContext& faders = *baseSystem.fader;
        ensureFaderState(faders, trackCount);

        glm::vec3 front = resolveColor(&world, "ButtonFront", glm::vec3(0.8f));
        glm::vec3 top = resolveColor(&world, "ButtonTopHighlight", glm::vec3(0.9f));
        glm::vec3 side = resolveColor(&world, "ButtonSideShadow", glm::vec3(0.6f));
        glm::vec3 trackColor = resolveColor(&world, "MiraLaneShadow", glm::vec3(0.6f));
        glm::vec3 glyphColor = resolveColor(&world, "ButtonGlyph", glm::vec3(0.15f));
        glm::vec3 fxActive = resolveColor(&world, "MiraLaneHighlight", glm::vec3(0.95f, 0.75f, 0.2f));

        std::vector<UiVertex> vertices;
        vertices.reserve(static_cast<size_t>(trackCount) * 60 + 240);

        PanelRectF pagePrev{}, pageNext{}, trackPrev{}, trackNext{};
        computeNavRects(bottomRect, pagePrev, pageNext, trackPrev, trackNext);
        renderNavButtons(vertices, pagePrev, g_navButtons[0].pressAnim, front, top, side, glyphColor, screenWidth, screenHeight, '<');
        renderNavButtons(vertices, pageNext, g_navButtons[1].pressAnim, front, top, side, glyphColor, screenWidth, screenHeight, '>');
        if (ui.bottomPanelPage == 1) {
            renderNavButtons(vertices, trackPrev, g_navButtons[2].pressAnim, front, top, side, glyphColor, screenWidth, screenHeight, '{');
            renderNavButtons(vertices, trackNext, g_navButtons[3].pressAnim, front, top, side, glyphColor, screenWidth, screenHeight, '}');
        }

        if (ui.bottomPanelPage == 1) {
            Vst3Context* vst3 = baseSystem.audio ? baseSystem.audio->vst3 : nullptr;
            renderFxChain(vertices, bottomRect, vst3, ui.bottomPanelTrack, audioTrackCount,
                          front, top, side, fxActive, screenWidth, screenHeight);
        } else {
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
