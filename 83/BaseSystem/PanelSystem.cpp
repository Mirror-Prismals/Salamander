#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace PanelSystemLogic {
    namespace {
        constexpr float kLeftFraction = 0.2f;
        constexpr float kRightFraction = 0.2f;
        constexpr float kTopFraction = 0.3f;
        constexpr float kBottomFraction = 0.3f;
        constexpr float kLaneInset = 40.0f;
        constexpr float kPanelFaceInset = 18.0f;
        constexpr float kTimelineLeftOffset = 90.0f;
        constexpr float kTimelineRightOffset = 40.0f;
        constexpr float kTransportYOffset = 10.0f;
        constexpr float kTimelineYOffset = -100.0f;
        constexpr float kPanelAlpha = 0.8f;
        constexpr float kWiggleAmp = 0.1f;

        struct PanelVertex {
            glm::vec2 pos;
            glm::vec3 color;
        };

        PanelRect computeTopRect(float state, float width, float height) {
            float h = height * kTopFraction;
            float openY = kPanelFaceInset;
            float closedY = -h;
            float y = closedY + (openY - closedY) * state;
            return {kPanelFaceInset, y, width - kPanelFaceInset, h};
        }

        PanelRect computeBottomRect(float state, float width, float height) {
            float h = height * kBottomFraction;
            float closedY = height;
            float openY = height - h;
            float y = closedY + (openY - closedY) * state;
            return {kPanelFaceInset, y, width - kPanelFaceInset, h};
        }

        PanelRect computeTopRect(float state,
                                 float width,
                                 float height,
                                 float leftBound,
                                 float rightBound) {
            float h = height * kTopFraction;
            float openY = kPanelFaceInset;
            float closedY = -h;
            float y = closedY + (openY - closedY) * state;
            float w = std::max(1.0f, rightBound - leftBound);
            return {leftBound, y, w, h};
        }

        PanelRect computeBottomRect(float state,
                                    float width,
                                    float height,
                                    float leftBound,
                                    float rightBound) {
            float h = height * kBottomFraction;
            float closedY = height;
            float openY = height - h;
            float y = closedY + (openY - closedY) * state;
            float w = std::max(1.0f, rightBound - leftBound);
            return {leftBound, y, w, h};
        }

        void computeFixedPanelBounds(float width, float& outLeft, float& outRight) {
            float leftW = width * kLeftFraction;
            float rightW = width * kRightFraction;
            outLeft = kPanelFaceInset + leftW;
            outRight = width - rightW;
            if (outRight < outLeft + 1.0f) {
                outRight = outLeft + 1.0f;
            }
        }

        PanelRect computeLeftRect(float state, float width, float height) {
            float w = width * kLeftFraction;
            float openX = kPanelFaceInset;
            float closedX = -w;
            float x = closedX + (openX - closedX) * state;
            return {x, kPanelFaceInset, w, height - kPanelFaceInset};
        }

        PanelRect computeRightRect(float state, float width, float height) {
            float w = width * kRightFraction;
            float closedX = width;
            float openX = width - w;
            float x = closedX + (openX - closedX) * state;
            return {x, kPanelFaceInset, w, height - kPanelFaceInset};
        }

        float computeWiggle(float t, float threshold, float amplitude) {
            if (t >= threshold) return amplitude;
            float half = threshold * 0.5f;
            return (t <= half) ? amplitude * (t / half) : amplitude * ((threshold - t) / half);
        }

        glm::vec2 pixelToNDC(const glm::vec2& pixel, double width, double height) {
            float ndcX = static_cast<float>((pixel.x / width) * 2.0 - 1.0);
            float ndcY = static_cast<float>(1.0 - (pixel.y / height) * 2.0);
            return {ndcX, ndcY};
        }

        void pushQuad(std::vector<PanelVertex>& verts,
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

        void buildPanelGeometry(const PanelRect& rect,
                                float depthPx,
                                const glm::vec3& frontColorIn,
                                const glm::vec3& topColorIn,
                                const glm::vec3& sideColorIn,
                                double width,
                                double height,
                                std::vector<PanelVertex>& verts) {
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

            auto clampColor = [](const glm::vec3& c) {
                return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
            };

            glm::vec3 frontColor = clampColor(frontColorIn);
            glm::vec3 topColor = clampColor(topColorIn);
            glm::vec3 leftColor = clampColor(sideColorIn);

            pushQuad(verts, frontA, frontB, frontC, frontD, frontColor, width, height);
            pushQuad(verts, topA, topB, topC, topD, topColor, width, height);
            pushQuad(verts, leftA, leftB, leftC, leftD, leftColor, width, height);
        }

        void ensureResources(RendererContext& renderer, WorldContext& world) {
            if (!renderer.uiColorShader) {
                renderer.uiColorShader = std::make_unique<Shader>(world.shaders["UI_COLOR_VERTEX_SHADER"].c_str(),
                                                                 world.shaders["UI_COLOR_FRAGMENT_SHADER"].c_str());
            }
            if (renderer.uiPanelVAO == 0) {
                glGenVertexArrays(1, &renderer.uiPanelVAO);
                glGenBuffers(1, &renderer.uiPanelVBO);
            }
        }

        int findWorldIndex(const LevelContext& level, const std::string& name) {
            for (size_t i = 0; i < level.worlds.size(); ++i) {
                if (level.worlds[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        EntityInstance* getInstance(LevelContext& level, int worldIndex, int instIndex) {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return nullptr;
            auto& insts = level.worlds[worldIndex].instances;
            if (instIndex < 0 || instIndex >= static_cast<int>(insts.size())) return nullptr;
            return &insts[instIndex];
        }

        void buildPanelCache(BaseSystem& baseSystem, PanelContext& panel) {
            panel.cacheBuilt = false;
            panel.panelWorldIndex = -1;
            panel.screenWorldIndex = -1;
            panel.transportWorldIndex = -1;
            panel.panelTopIndex = -1;
            panel.panelBottomIndex = -1;
            panel.panelLeftIndex = -1;
            panel.panelRightIndex = -1;
            panel.timelineLeftIndices.clear();
            panel.timelineRightIndices.clear();
            panel.timelineLeftBasePositions.clear();
            panel.timelineRightBasePositions.clear();
            panel.transportBasePositions.clear();
            panel.transportMinX = 0.0f;

            if (!baseSystem.level) return;
            LevelContext& level = *baseSystem.level;

            panel.panelWorldIndex = findWorldIndex(level, "DAWPanelWorld");
            panel.screenWorldIndex = findWorldIndex(level, "DAWScreenWorld");
            panel.transportWorldIndex = findWorldIndex(level, "UIButtonWorld");

            if (panel.panelWorldIndex >= 0 && panel.panelWorldIndex < static_cast<int>(level.worlds.size())) {
                const auto& insts = level.worlds[panel.panelWorldIndex].instances;
                for (size_t i = 0; i < insts.size(); ++i) {
                    const std::string& id = insts[i].controlId;
                    if (id == "panel_top") panel.panelTopIndex = static_cast<int>(i);
                    else if (id == "panel_bottom") panel.panelBottomIndex = static_cast<int>(i);
                    else if (id == "panel_left") panel.panelLeftIndex = static_cast<int>(i);
                    else if (id == "panel_right") panel.panelRightIndex = static_cast<int>(i);
                }
            }

            if (panel.screenWorldIndex >= 0 && panel.screenWorldIndex < static_cast<int>(level.worlds.size())) {
                const auto& insts = level.worlds[panel.screenWorldIndex].instances;
                for (size_t i = 0; i < insts.size(); ++i) {
                    const std::string& id = insts[i].controlId;
                    if (id == "daw_timeline_left") {
                        panel.timelineLeftIndices.push_back(static_cast<int>(i));
                        panel.timelineLeftBasePositions.push_back(insts[i].position - glm::vec3(0.0f, panel.timelineOffsetY, 0.0f));
                    } else if (id == "daw_timeline_right") {
                        panel.timelineRightIndices.push_back(static_cast<int>(i));
                        panel.timelineRightBasePositions.push_back(insts[i].position - glm::vec3(0.0f, panel.timelineOffsetY, 0.0f));
                    }
                }
            }

            if (panel.transportWorldIndex >= 0 && panel.transportWorldIndex < static_cast<int>(level.worlds.size())) {
                const auto& insts = level.worlds[panel.transportWorldIndex].instances;
                panel.transportBasePositions.reserve(insts.size());
                float minX = std::numeric_limits<float>::max();
                for (const auto& inst : insts) {
                    panel.transportBasePositions.push_back(inst.position - glm::vec3(0.0f, panel.transportOffsetY, 0.0f));
                    if (inst.position.x < minX) minX = inst.position.x;
                }
                if (minX != std::numeric_limits<float>::max()) {
                    panel.transportMinX = minX;
                }
            }

            panel.cacheBuilt = true;
            panel.cachedLevel = &level;
            panel.cachedWorldCount = level.worlds.size();
        }

        void updatePanelInstance(LevelContext& level, int worldIndex, int instIndex, const PanelRect& rect) {
            EntityInstance* inst = getInstance(level, worldIndex, instIndex);
            if (!inst) return;
            inst->position.x = rect.x + rect.w * 0.5f;
            inst->position.y = rect.y + rect.h * 0.5f;
            inst->size.x = rect.w * 0.5f;
            inst->size.y = rect.h * 0.5f;
        }

        void updateTimelineButtons(LevelContext& level, PanelContext& panel, float laneLeft, float topOffset) {
            if (panel.screenWorldIndex < 0 || panel.screenWorldIndex >= static_cast<int>(level.worlds.size())) return;
            auto& insts = level.worlds[panel.screenWorldIndex].instances;
            float anchorX = laneLeft;
            if (!panel.transportBasePositions.empty()) {
                anchorX = panel.transportMinX;
            }
            float leftX = anchorX - (kTimelineLeftOffset + 100.0f);
            float rightX = anchorX - (kTimelineRightOffset + 100.0f);
            float appliedOffset = topOffset + kTimelineYOffset;

            if (panel.timelineLeftIndices.size() != panel.timelineLeftBasePositions.size()
                || panel.timelineRightIndices.size() != panel.timelineRightBasePositions.size()) {
                panel.cacheBuilt = false;
                return;
            }

            for (size_t i = 0; i < panel.timelineLeftIndices.size(); ++i) {
                int idx = panel.timelineLeftIndices[i];
                if (idx >= 0 && idx < static_cast<int>(insts.size())) {
                    insts[idx].position = panel.timelineLeftBasePositions[i] + glm::vec3(0.0f, appliedOffset, 0.0f);
                    insts[idx].position.x = leftX;
                }
            }
            for (size_t i = 0; i < panel.timelineRightIndices.size(); ++i) {
                int idx = panel.timelineRightIndices[i];
                if (idx >= 0 && idx < static_cast<int>(insts.size())) {
                    insts[idx].position = panel.timelineRightBasePositions[i] + glm::vec3(0.0f, appliedOffset, 0.0f);
                    insts[idx].position.x = rightX;
                }
            }
            panel.timelineOffsetY = appliedOffset;
        }

        void updateTransportOffsets(LevelContext& level, PanelContext& panel, float topOffset) {
            if (panel.transportWorldIndex < 0 || panel.transportWorldIndex >= static_cast<int>(level.worlds.size())) return;
            auto& insts = level.worlds[panel.transportWorldIndex].instances;
            if (insts.size() != panel.transportBasePositions.size()) {
                panel.cacheBuilt = false;
                return;
            }
            float appliedOffset = topOffset + kTransportYOffset;
            for (size_t i = 0; i < insts.size(); ++i) {
                insts[i].position = panel.transportBasePositions[i] + glm::vec3(0.0f, appliedOffset, 0.0f);
            }
            panel.transportOffsetY = appliedOffset;
        }
    }

    void UpdatePanels(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes;
        if (!baseSystem.panel || !win) return;
        PanelContext& panel = *baseSystem.panel;

        int windowWidth = 0, windowHeight = 0;
        glfwGetWindowSize(win, &windowWidth, &windowHeight);
        float screenWidth = windowWidth > 0 ? static_cast<float>(windowWidth) : 1920.0f;
        float screenHeight = windowHeight > 0 ? static_cast<float>(windowHeight) : 1080.0f;

        bool uiActive = baseSystem.ui && baseSystem.ui->active;
        if (!uiActive) {
            panel.uiWasActive = false;
            panel.cacheBuilt = false;
            panel.upHoldActive = panel.downHoldActive = panel.leftHoldActive = panel.rightHoldActive = false;
            panel.upHoldTimer = panel.downHoldTimer = panel.leftHoldTimer = panel.rightHoldTimer = 0.0f;
            panel.upCommitted = panel.downCommitted = panel.leftCommitted = panel.rightCommitted = false;
            panel.mainRect = {0.0f, 0.0f, screenWidth, screenHeight};
            if (baseSystem.ui) {
                baseSystem.ui->mainScrollDelta = 0.0;
                baseSystem.ui->panelScrollDelta = 0.0;
                baseSystem.ui->bottomPanelScrollDelta = 0.0;
            }
            return;
        }

        if (!panel.uiWasActive) {
            panel.topOpen = panel.bottomOpen = panel.leftOpen = panel.rightOpen = false;
            panel.topState = panel.bottomState = panel.leftState = panel.rightState = 0.0f;
            panel.topTarget = panel.bottomTarget = panel.leftTarget = panel.rightTarget = 0.0f;
            panel.cacheBuilt = false;
        }
        panel.uiWasActive = true;

        if (baseSystem.level) {
            if (!panel.cacheBuilt || panel.cachedLevel != baseSystem.level.get()
                || panel.cachedWorldCount != baseSystem.level->worlds.size()) {
                buildPanelCache(baseSystem, panel);
            }
        }

        {
            bool upDown = glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS;
            bool downDown = glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS;
            bool leftDown = glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS;
            bool rightDown = glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS;

            if (upDown && !panel.upHoldActive) {
                panel.upHoldActive = true;
                panel.upHoldTimer = 0.0f;
                panel.upCommitted = false;
            } else if (!upDown && panel.upHoldActive) {
                panel.upHoldActive = false;
                panel.upHoldTimer = 0.0f;
                panel.upCommitted = false;
            }
            if (downDown && !panel.downHoldActive) {
                panel.downHoldActive = true;
                panel.downHoldTimer = 0.0f;
                panel.downCommitted = false;
            } else if (!downDown && panel.downHoldActive) {
                panel.downHoldActive = false;
                panel.downHoldTimer = 0.0f;
                panel.downCommitted = false;
            }
            if (leftDown && !panel.leftHoldActive) {
                panel.leftHoldActive = true;
                panel.leftHoldTimer = 0.0f;
                panel.leftCommitted = false;
            } else if (!leftDown && panel.leftHoldActive) {
                panel.leftHoldActive = false;
                panel.leftHoldTimer = 0.0f;
                panel.leftCommitted = false;
            }
            if (rightDown && !panel.rightHoldActive) {
                panel.rightHoldActive = true;
                panel.rightHoldTimer = 0.0f;
                panel.rightCommitted = false;
            } else if (!rightDown && panel.rightHoldActive) {
                panel.rightHoldActive = false;
                panel.rightHoldTimer = 0.0f;
                panel.rightCommitted = false;
            }

            if (panel.upHoldActive) {
                panel.upHoldTimer += dt;
                if (panel.upHoldTimer >= panel.holdThreshold && !panel.upCommitted) {
                    panel.topOpen = !panel.topOpen;
                    panel.upCommitted = true;
                }
            }
            if (panel.downHoldActive) {
                panel.downHoldTimer += dt;
                if (panel.downHoldTimer >= panel.holdThreshold && !panel.downCommitted) {
                    panel.bottomOpen = !panel.bottomOpen;
                    panel.downCommitted = true;
                }
            }
            if (panel.leftHoldActive) {
                panel.leftHoldTimer += dt;
                if (panel.leftHoldTimer >= panel.holdThreshold && !panel.leftCommitted) {
                    panel.leftOpen = !panel.leftOpen;
                    panel.leftCommitted = true;
                }
            }
            if (panel.rightHoldActive) {
                panel.rightHoldTimer += dt;
                if (panel.rightHoldTimer >= panel.holdThreshold && !panel.rightCommitted) {
                    panel.rightOpen = !panel.rightOpen;
                    panel.rightCommitted = true;
                }
            }
        }

        panel.topTarget = panel.topOpen ? 1.0f : 0.0f;
        panel.bottomTarget = panel.bottomOpen ? 1.0f : 0.0f;
        panel.leftTarget = panel.leftOpen ? 1.0f : 0.0f;
        panel.rightTarget = panel.rightOpen ? 1.0f : 0.0f;

        panel.topState += (panel.topTarget - panel.topState) * dt * panel.stateSpeed;
        panel.bottomState += (panel.bottomTarget - panel.bottomState) * dt * panel.stateSpeed;
        panel.leftState += (panel.leftTarget - panel.leftState) * dt * panel.stateSpeed;
        panel.rightState += (panel.rightTarget - panel.rightState) * dt * panel.stateSpeed;

        panel.topState = std::clamp(panel.topState, 0.0f, 1.0f);
        panel.bottomState = std::clamp(panel.bottomState, 0.0f, 1.0f);
        panel.leftState = std::clamp(panel.leftState, 0.0f, 1.0f);
        panel.rightState = std::clamp(panel.rightState, 0.0f, 1.0f);

        panel.leftRect = computeLeftRect(panel.leftState, screenWidth, screenHeight);
        panel.rightRect = computeRightRect(panel.rightState, screenWidth, screenHeight);
        float mainLeft = std::max(0.0f, panel.leftRect.x + panel.leftRect.w);
        float mainRight = std::min(screenWidth, panel.rightRect.x);
        if (mainRight < mainLeft + 1.0f) mainRight = mainLeft + 1.0f;
        panel.mainRect = {mainLeft, 0.0f, mainRight - mainLeft, screenHeight};
        float fixedLeft = 0.0f;
        float fixedRight = screenWidth;
        computeFixedPanelBounds(screenWidth, fixedLeft, fixedRight);
        panel.topRect = computeTopRect(panel.topState, screenWidth, screenHeight, fixedLeft, fixedRight);
        panel.bottomRect = computeBottomRect(panel.bottomState, screenWidth, screenHeight, fixedLeft, fixedRight);

        float effectiveTop = panel.topState;
        float effectiveBottom = panel.bottomState;
        float effectiveLeft = panel.leftState;
        float effectiveRight = panel.rightState;
        if (panel.upHoldActive && !panel.upCommitted && panel.upHoldTimer < panel.holdThreshold) {
            float wiggle = computeWiggle(panel.upHoldTimer, panel.holdThreshold, kWiggleAmp);
            effectiveTop = panel.topState * (1.0f - wiggle);
        }
        if (panel.downHoldActive && !panel.downCommitted && panel.downHoldTimer < panel.holdThreshold) {
            float wiggle = computeWiggle(panel.downHoldTimer, panel.holdThreshold, kWiggleAmp);
            effectiveBottom = panel.bottomState * (1.0f - wiggle);
        }
        if (panel.leftHoldActive && !panel.leftCommitted && panel.leftHoldTimer < panel.holdThreshold) {
            float wiggle = computeWiggle(panel.leftHoldTimer, panel.holdThreshold, kWiggleAmp);
            effectiveLeft = panel.leftState * (1.0f - wiggle);
        }
        if (panel.rightHoldActive && !panel.rightCommitted && panel.rightHoldTimer < panel.holdThreshold) {
            float wiggle = computeWiggle(panel.rightHoldTimer, panel.holdThreshold, kWiggleAmp);
            effectiveRight = panel.rightState * (1.0f - wiggle);
        }

        panel.leftRenderRect = computeLeftRect(effectiveLeft, screenWidth, screenHeight);
        panel.rightRenderRect = computeRightRect(effectiveRight, screenWidth, screenHeight);
        float renderFixedLeft = 0.0f;
        float renderFixedRight = screenWidth;
        computeFixedPanelBounds(screenWidth, renderFixedLeft, renderFixedRight);
        panel.topRenderRect = computeTopRect(effectiveTop, screenWidth, screenHeight, renderFixedLeft, renderFixedRight);
        panel.bottomRenderRect = computeBottomRect(effectiveBottom, screenWidth, screenHeight, renderFixedLeft, renderFixedRight);

        if (baseSystem.ui) {
            UIContext& ui = *baseSystem.ui;
            ui.mainScrollDelta = 0.0;
            ui.panelScrollDelta = 0.0;
            ui.bottomPanelScrollDelta = 0.0;
            if (baseSystem.player) {
                double scroll = baseSystem.player->scrollYOffset;
                if (scroll != 0.0) {
                    double mx = 0.0, my = 0.0;
                    glfwGetCursorPos(win, &mx, &my);
                    PanelRect bottomRect = (panel.bottomRenderRect.w > 0.0f) ? panel.bottomRenderRect : panel.bottomRect;
                    PanelRect rightRect = (panel.rightRenderRect.w > 0.0f) ? panel.rightRenderRect : panel.rightRect;
                    bool overBottom = (mx >= bottomRect.x && mx <= bottomRect.x + bottomRect.w &&
                                       my >= bottomRect.y && my <= bottomRect.y + bottomRect.h);
                    bool overRight = (mx >= rightRect.x && mx <= rightRect.x + rightRect.w &&
                                      my >= rightRect.y && my <= rightRect.y + rightRect.h);
                    if (overBottom && panel.bottomState > 0.01f) {
                        ui.bottomPanelScrollDelta = scroll;
                    } else if (overRight && panel.rightState > 0.01f) {
                        ui.panelScrollDelta = scroll;
                    } else {
                        ui.mainScrollDelta = scroll;
                    }
                }
            }
        }

        if (baseSystem.level && panel.cacheBuilt) {
            LevelContext& level = *baseSystem.level;
            updatePanelInstance(level, panel.panelWorldIndex, panel.panelTopIndex, panel.topRect);
            updatePanelInstance(level, panel.panelWorldIndex, panel.panelBottomIndex, panel.bottomRect);
            updatePanelInstance(level, panel.panelWorldIndex, panel.panelLeftIndex, panel.leftRect);
            updatePanelInstance(level, panel.panelWorldIndex, panel.panelRightIndex, panel.rightRect);

            updateTransportOffsets(level, panel, panel.topRenderRect.y);

            float laneLeft = panel.mainRect.x + kLaneInset;
            updateTimelineButtons(level, panel, laneLeft, panel.topRenderRect.y);
        }
    }

    namespace {
        void renderPanels(BaseSystem& baseSystem, bool includeSide, bool includeTopBottom, GLFWwindow* win) {
            if (!baseSystem.panel || !baseSystem.renderer || !baseSystem.world || !baseSystem.level || !win) return;
            if (!baseSystem.ui || !baseSystem.ui->active) return;

            PanelContext& panel = *baseSystem.panel;
            if (!panel.cacheBuilt) return;
            RendererContext& renderer = *baseSystem.renderer;
            WorldContext& world = *baseSystem.world;
            LevelContext& level = *baseSystem.level;
            ensureResources(renderer, world);

            int windowWidth = 0, windowHeight = 0;
            glfwGetWindowSize(win, &windowWidth, &windowHeight);
            double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
            double screenHeight = windowHeight > 0 ? static_cast<double>(windowHeight) : 1080.0;

            float effectiveTop = panel.topState;
            float effectiveBottom = panel.bottomState;
            float effectiveLeft = panel.leftState;
            float effectiveRight = panel.rightState;
            if (panel.upHoldActive && !panel.upCommitted && panel.upHoldTimer < panel.holdThreshold) {
                float wiggle = computeWiggle(panel.upHoldTimer, panel.holdThreshold, kWiggleAmp);
                effectiveTop = panel.topState * (1.0f - wiggle);
            }
            if (panel.downHoldActive && !panel.downCommitted && panel.downHoldTimer < panel.holdThreshold) {
                float wiggle = computeWiggle(panel.downHoldTimer, panel.holdThreshold, kWiggleAmp);
                effectiveBottom = panel.bottomState * (1.0f - wiggle);
            }
            if (panel.leftHoldActive && !panel.leftCommitted && panel.leftHoldTimer < panel.holdThreshold) {
                float wiggle = computeWiggle(panel.leftHoldTimer, panel.holdThreshold, kWiggleAmp);
                effectiveLeft = panel.leftState * (1.0f - wiggle);
            }
            if (panel.rightHoldActive && !panel.rightCommitted && panel.rightHoldTimer < panel.holdThreshold) {
                float wiggle = computeWiggle(panel.rightHoldTimer, panel.holdThreshold, kWiggleAmp);
                effectiveRight = panel.rightState * (1.0f - wiggle);
            }

            PanelRect leftRect = computeLeftRect(effectiveLeft, static_cast<float>(screenWidth), static_cast<float>(screenHeight));
            PanelRect rightRect = computeRightRect(effectiveRight, static_cast<float>(screenWidth), static_cast<float>(screenHeight));
            float fixedLeft = 0.0f;
            float fixedRight = static_cast<float>(screenWidth);
            computeFixedPanelBounds(static_cast<float>(screenWidth), fixedLeft, fixedRight);
            PanelRect topRect = computeTopRect(effectiveTop, static_cast<float>(screenWidth),
                                               static_cast<float>(screenHeight), fixedLeft, fixedRight);
            PanelRect bottomRect = computeBottomRect(effectiveBottom, static_cast<float>(screenWidth),
                                                     static_cast<float>(screenHeight), fixedLeft, fixedRight);

            std::vector<PanelVertex> vertices;
            vertices.reserve(72);

            auto appendPanel = [&](const PanelRect& rect, int instIndex) {
                EntityInstance* inst = getInstance(level, panel.panelWorldIndex, instIndex);
                glm::vec3 front(0.2f);
                glm::vec3 top(0.25f);
                glm::vec3 side(0.15f);
                float depth = 12.0f;
                if (inst) {
                    front = resolveColor(&world, inst->colorName, inst->color);
                    top = resolveColor(&world, inst->topColorName, inst->topColor);
                    side = resolveColor(&world, inst->sideColorName, inst->sideColor);
                    if (inst->size.z > 0.0f) depth = inst->size.z;
                    bool topExplicit = !(inst->topColorName.empty() && inst->topColor == glm::vec3(1.0f, 0.0f, 1.0f));
                    bool sideExplicit = !(inst->sideColorName.empty() && inst->sideColor == glm::vec3(1.0f, 0.0f, 1.0f));
                    if (!topExplicit) top = glm::clamp(front + glm::vec3(0.1f), glm::vec3(0.0f), glm::vec3(1.0f));
                    if (!sideExplicit) side = glm::clamp(front - glm::vec3(0.03f), glm::vec3(0.0f), glm::vec3(1.0f));
                }
                buildPanelGeometry(rect, depth, front, top, side, screenWidth, screenHeight, vertices);
            };

            if (includeSide) {
                appendPanel(leftRect, panel.panelLeftIndex);
                appendPanel(rightRect, panel.panelRightIndex);
            }
            if (includeTopBottom) {
                appendPanel(topRect, panel.panelTopIndex);
                appendPanel(bottomRect, panel.panelBottomIndex);
            }

            if (vertices.empty() || !renderer.uiColorShader) return;

            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendColor(0.0f, 0.0f, 0.0f, kPanelAlpha);
            glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
            glBindVertexArray(renderer.uiPanelVAO);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.uiPanelVBO);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(PanelVertex), vertices.data(), GL_DYNAMIC_DRAW);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(PanelVertex), (void*)offsetof(PanelVertex, pos));
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PanelVertex), (void*)offsetof(PanelVertex, color));
            glEnableVertexAttribArray(1);

            renderer.uiColorShader->use();
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_DEPTH_TEST);
        }
    }

    void RenderSidePanels(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt;
        renderPanels(baseSystem, true, false, win);
    }

    void RenderTopBottomPanels(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt;
        renderPanels(baseSystem, false, true, win);
    }

    void RenderPanels(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt;
        renderPanels(baseSystem, true, true, win);
    }
}
