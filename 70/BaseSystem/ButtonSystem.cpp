#pragma once

#include <GLFW/glfw3.h>
#include <unordered_map>

namespace ButtonSystemLogic {

    struct ButtonState {
        float pressAnim = 0.0f;
        bool toggled = false;
        bool trackingPress = false;
        glm::vec3 frontColor{0.3f};
        glm::vec3 topColor{0.3f};
        glm::vec3 sideColor{0.3f};
    };

    struct ButtonVertex {
        glm::vec2 pos;
        glm::vec3 color;
    };

    static std::unordered_map<int, ButtonState> g_buttonStates;

    float GetButtonPressOffset(int instanceID) {
        auto it = g_buttonStates.find(instanceID);
        if (it == g_buttonStates.end()) return 0.0f;
        return it->second.pressAnim;
    }

    bool GetButtonToggled(int instanceID) {
        auto it = g_buttonStates.find(instanceID);
        if (it == g_buttonStates.end()) return false;
        return it->second.toggled;
    }

    void SetButtonToggled(int instanceID, bool toggled) {
        g_buttonStates[instanceID].toggled = toggled;
    }

    namespace {
        void ensureResources(RendererContext& renderer, WorldContext& world) {
            if (!renderer.uiColorShader) {
                renderer.uiColorShader = std::make_unique<Shader>(world.shaders["UI_COLOR_VERTEX_SHADER"].c_str(), world.shaders["UI_COLOR_FRAGMENT_SHADER"].c_str());
            }
            if (renderer.uiButtonVAO == 0) {
                glGenVertexArrays(1, &renderer.uiButtonVAO);
                glGenBuffers(1, &renderer.uiButtonVBO);
            }
        }

        void pushQuad(std::vector<ButtonVertex>& verts,
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
    
        glm::vec2 pixelToNDC(const glm::vec2& pixel, double width, double height) {
            float ndcX = static_cast<float>((pixel.x / width) * 2.0 - 1.0);
            float ndcY = static_cast<float>(1.0 - (pixel.y / height) * 2.0);
            return {ndcX, ndcY};
        }

        glm::vec3 resolveColor(const WorldContext* world, const std::string& name, const glm::vec3& fallback) {
            if (world && !name.empty()) {
                auto it = world->colorLibrary.find(name);
                if (it != world->colorLibrary.end()) return it->second;
            }
            return fallback;
        }

        void buildButtonGeometry(const glm::vec2& centerPx,
                                 const glm::vec2& halfSizePx,
                                 float depthPx,
                                 float pressAnim,
                                 const glm::vec3& frontColorIn,
                                 const glm::vec3& topColorIn,
                                 const glm::vec3& sideColorIn,
                                 double width,
                                 double height,
                                 std::vector<ButtonVertex>& verts) {
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

            auto clampColor = [](const glm::vec3& c) {
                return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
            };

            glm::vec3 frontColor = clampColor(frontColorIn);
            glm::vec3 topColor = clampColor(topColorIn);
            glm::vec3 leftColor = clampColor(sideColorIn);

            auto pushFace = [&](const glm::vec2& a, const glm::vec2& b, const glm::vec2& c, const glm::vec2& d, const glm::vec3& color) {
                pushQuad(verts, pixelToNDC(a, width, height), pixelToNDC(b, width, height), pixelToNDC(c, width, height), pixelToNDC(d, width, height), color);
            };

            pushFace(frontA, frontB, frontC, frontD, frontColor);
            pushFace(topA, topB, topC, topD, topColor);
            pushFace(leftA, leftB, leftC, leftD, leftColor);
        }

        void buildButtonCache(BaseSystem& baseSystem,
                              const std::vector<Entity>& prototypes,
                              UIContext& ui) {
            ui.buttonInstances.clear();
            if (!baseSystem.level) return;
            for (auto& world : baseSystem.level->worlds) {
                for (auto& inst : world.instances) {
                    if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                    const std::string& protoName = prototypes[inst.prototypeID].name;
                    if (protoName != "Button" && protoName != "ActionButton") continue;
                    ui.buttonInstances.push_back(&inst);
                }
            }
            ui.buttonCacheBuilt = true;
            ui.buttonCacheLevel = baseSystem.level.get();
        }
    }

    void UpdateButtons(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.ui || !baseSystem.level || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
        if (!baseSystem.renderer || !baseSystem.world) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        ensureResources(renderer, world);

        if (ui.consumeClick) {
            ui.uiLeftDown = false;
            ui.uiLeftPressed = false;
            ui.uiLeftReleased = false;
            ui.consumeClick = false;
        }

        int windowWidth = 0, windowHeight = 0;
        glfwGetWindowSize(win, &windowWidth, &windowHeight);
        double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
        double screenHeight = windowHeight > 0 ? static_cast<double>(windowHeight) : 1080.0;

        std::vector<ButtonVertex> vertices;
        vertices.reserve(64);

        if (!ui.buttonCacheBuilt || ui.buttonCacheLevel != baseSystem.level.get()) {
            buildButtonCache(baseSystem, prototypes, ui);
        }

        for (auto* instPtr : ui.buttonInstances) {
            if (!instPtr) continue;
            EntityInstance& inst = *instPtr;
            ButtonState& state = g_buttonStates[inst.instanceID];
                bool isMomentary = (inst.buttonMode == "momentary");
                bool isManaged = (inst.buttonMode == "managed");
                bool isStatic = (inst.buttonMode == "static");
                if (isMomentary && state.toggled) {
                    state.toggled = false;
                }
                if (isStatic) {
                    state.toggled = false;
                    state.trackingPress = false;
                }
                // Resolve per-face colors here so Host/BaseEntity stay generic.
                glm::vec3 baseFront = resolveColor(&world, inst.colorName, inst.color);
                glm::vec3 baseTop = resolveColor(&world, inst.topColorName, inst.topColor);
                glm::vec3 baseSide = resolveColor(&world, inst.sideColorName, inst.sideColor);

            // Derived fallbacks if top/side not specified.
            if (inst.topColorName.empty() && inst.topColor == glm::vec3(1.0f, 0.0f, 1.0f)) {
                baseTop = glm::clamp(baseFront + glm::vec3(0.1f), glm::vec3(0.0f), glm::vec3(1.0f));
            }
            if (inst.sideColorName.empty() && inst.sideColor == glm::vec3(1.0f, 0.0f, 1.0f)) {
                baseSide = glm::clamp(baseFront - glm::vec3(0.03f), glm::vec3(0.0f), glm::vec3(1.0f));
            }

            state.frontColor = baseFront;
            state.topColor = baseTop;
            state.sideColor = baseSide;

            glm::vec2 halfSizePx(inst.size.x, inst.size.y);
            glm::vec2 centerPx(inst.position.x, inst.position.y);
            glm::vec2 cursorPx(static_cast<float>(ui.cursorX), static_cast<float>(ui.cursorY));

            bool inside = (cursorPx.x >= centerPx.x - halfSizePx.x && cursorPx.x <= centerPx.x + halfSizePx.x &&
                           cursorPx.y >= centerPx.y - halfSizePx.y && cursorPx.y <= centerPx.y + halfSizePx.y);

                if (!isStatic) {
                    if (ui.uiLeftPressed && inside) {
                        state.trackingPress = true;
                    }
                    if (ui.uiLeftReleased) {
                        if (state.trackingPress && inside) {
                            if (!isMomentary && !isManaged) {
                                state.toggled = !state.toggled;
                            }
                            if (!inst.actionType.empty()) {
                                ui.pendingActionType = inst.actionType;
                                ui.pendingActionKey = inst.actionKey;
                                ui.pendingActionValue = inst.actionValue;
                                ui.actionDelayFrames = 1;
                            }
                        }
                        state.trackingPress = false;
                    }
                }

            bool activePress = state.trackingPress && ui.uiLeftDown;
            float target = (state.toggled || activePress) ? 0.5f : 0.0f;
            float speed = (0.5f / 0.15f);
            if (state.pressAnim < target) {
                state.pressAnim = std::min(target, state.pressAnim + speed * dt);
            } else if (state.pressAnim > target) {
                state.pressAnim = std::max(target, state.pressAnim - speed * dt);
            }

                float depthPx = inst.size.z > 0.0f ? inst.size.z : 10.0f;
                buildButtonGeometry(centerPx, halfSizePx, depthPx, state.pressAnim, state.frontColor, state.topColor, state.sideColor, screenWidth, screenHeight, vertices);
        }

        if (vertices.empty() || !renderer.uiColorShader) return;

        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(renderer.uiButtonVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.uiButtonVBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(ButtonVertex), vertices.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ButtonVertex), (void*)offsetof(ButtonVertex, pos));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ButtonVertex), (void*)offsetof(ButtonVertex, color));
        glEnableVertexAttribArray(1);

        renderer.uiColorShader->use();
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
        glEnable(GL_DEPTH_TEST);

        ui.uiLeftPressed = false;
        ui.uiLeftReleased = false;
    }
}
