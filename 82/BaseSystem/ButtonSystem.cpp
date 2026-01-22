#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <unordered_map>

namespace ButtonSystemLogic {

    struct ButtonState {
        float pressAnim = 0.0f;
        bool toggled = false;
        bool trackingPress = false;
        int holdFrames = 0;
        glm::vec3 frontColor{0.3f};
        glm::vec3 topColor{0.3f};
        glm::vec3 sideColor{0.3f};
    };

    struct ButtonVertex {
        glm::vec2 pos;
        glm::vec3 color;
    };

    static std::unordered_map<int, ButtonState> g_buttonStates;

    enum class ButtonRenderPass {
        Side,
        TopBottom,
        All
    };

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
        constexpr float kButtonAlpha = 0.85f;
        constexpr bool kDebugRelayoutTrackButtons = false;

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

        const UiStateColors* findUiState(const std::vector<UiStateColors>& states, const std::string& name) {
            if (name.empty()) return nullptr;
            for (const auto& state : states) {
                if (state.name == name) return &state;
            }
            return nullptr;
        }

        bool isSidePanelButton(const EntityInstance& inst) {
            return inst.controlId.rfind("track_", 0) == 0 || inst.controlId.rfind("midi_track_", 0) == 0;
        }

        const char* fallbackTrackButtonName(const EntityInstance& inst) {
            if (inst.actionType != "DawTrack" && inst.actionType != "DawMidiTrack") return nullptr;
            if (inst.actionKey == "clear") return "TrackClearButton";
            if (inst.actionKey == "input") return "TrackInputButton";
            if (inst.actionKey == "arm") return "TrackArmButton";
            if (inst.actionKey == "solo") return "TrackSoloButton";
            if (inst.actionKey == "mute") return "TrackMuteButton";
            if (inst.actionKey == "output") return "TrackOutputButton";
            return nullptr;
        }

        const char* fallbackTrackButtonNameFromId(const EntityInstance& inst) {
            if (inst.controlRole != "button") return nullptr;
            if (inst.controlId.rfind("track_", 0) != 0 && inst.controlId.rfind("midi_track_", 0) != 0) return nullptr;
            if (inst.controlId.find("_clear") != std::string::npos) return "TrackClearButton";
            if (inst.controlId.find("_input") != std::string::npos) return "TrackInputButton";
            if (inst.controlId.find("_arm") != std::string::npos) return "TrackArmButton";
            if (inst.controlId.find("_solo") != std::string::npos) return "TrackSoloButton";
            if (inst.controlId.find("_mute") != std::string::npos) return "TrackMuteButton";
            if (inst.controlId.find("_output") != std::string::npos) return "TrackOutputButton";
            return nullptr;
        }

        bool shouldRenderButton(const EntityInstance& inst, ButtonRenderPass pass) {
            if (pass == ButtonRenderPass::All) return true;
            bool isSide = isSidePanelButton(inst);
            return (pass == ButtonRenderPass::Side) ? isSide : !isSide;
        }

        void applyUiStateColors(const UiStateColors& stateDef,
                                const WorldContext& world,
                                glm::vec3& front,
                                glm::vec3& top,
                                glm::vec3& side,
                                bool& topExplicit,
                                bool& sideExplicit) {
            if (stateDef.hasFrontColor) {
                front = resolveColor(&world, stateDef.frontColorName, stateDef.frontColor);
            }
            if (stateDef.hasTopColor) {
                top = resolveColor(&world, stateDef.topColorName, stateDef.topColor);
                topExplicit = true;
            }
            if (stateDef.hasSideColor) {
                side = resolveColor(&world, stateDef.sideColorName, stateDef.sideColor);
                sideExplicit = true;
            }
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

        void updateButtonState(EntityInstance& inst, ButtonState& state, UIContext& ui, float dt) {
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
                            if (isManaged) {
                                state.holdFrames = std::max(state.holdFrames, 1);
                            }
                        }
                    }
                    state.trackingPress = false;
                }
            }

            bool activePress = state.trackingPress && ui.uiLeftDown;
            bool holdPress = (state.holdFrames > 0);
            float target = (state.toggled || activePress || holdPress) ? 0.5f : 0.0f;
            float speed = (0.5f / 0.15f);
            if (state.pressAnim < target) {
                state.pressAnim = std::min(target, state.pressAnim + speed * dt);
            } else if (state.pressAnim > target) {
                state.pressAnim = std::max(target, state.pressAnim - speed * dt);
            }
            if (state.holdFrames > 0) {
                state.holdFrames -= 1;
            }
        }

        void buildButtonCache(BaseSystem& baseSystem,
                              const std::vector<Entity>& prototypes,
                              UIContext& ui) {
            ui.buttonInstances.clear();
            if (!baseSystem.level) return;
            static bool g_debugPrinted = false;
            int trackButtonCount = 0;
            int midiButtonCount = 0;
            int dawTrackActionCount = 0;
            int rawTrackControlCount = 0;
            int rawTrackWorldCount = 0;
            for (auto& world : baseSystem.level->worlds) {
                if (world.name.rfind("TrackRowWorld", 0) == 0 || world.name.rfind("MidiTrackRowWorld", 0) == 0) {
                    rawTrackWorldCount += 1;
                }
                for (auto& inst : world.instances) {
                    if (inst.controlId.rfind("track_", 0) == 0 || inst.controlId.rfind("midi_track_", 0) == 0) {
                        rawTrackControlCount += 1;
                    }
                    if (inst.actionType == "DawTrack") {
                        dawTrackActionCount += 1;
                    }
                    if (inst.actionType == "DawTrack" || inst.actionType == "DawMidiTrack") {
                        if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())
                            || !prototypes[inst.prototypeID].isUIButton) {
                            if (const char* fallback = fallbackTrackButtonName(inst)) {
                                if (const Entity* proto = HostLogic::findPrototype(fallback, prototypes)) {
                                    inst.prototypeID = proto->prototypeID;
                                }
                            }
                        }
                    }
                    if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())
                        || !prototypes[inst.prototypeID].isUIButton) {
                        if (const char* fallback = fallbackTrackButtonNameFromId(inst)) {
                            if (const Entity* proto = HostLogic::findPrototype(fallback, prototypes)) {
                                inst.prototypeID = proto->prototypeID;
                            }
                        }
                    }
                    if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())
                        || !prototypes[inst.prototypeID].isUIButton) {
                        const Entity* proto = HostLogic::findPrototype(inst.name, prototypes);
                        if (!proto || !proto->isUIButton) continue;
                        inst.prototypeID = proto->prototypeID;
                    }
                    if (!prototypes[inst.prototypeID].isUIButton) continue;
                    ui.buttonInstances.push_back(&inst);
                    if (inst.controlId.rfind("track_", 0) == 0) trackButtonCount += 1;
                    if (inst.controlId.rfind("midi_track_", 0) == 0) midiButtonCount += 1;
                }
            }
            std::cerr << "[ButtonCache] buttons=" << ui.buttonInstances.size()
                      << " trackButtons=" << trackButtonCount
                      << " midiButtons=" << midiButtonCount
                      << " dawTrackActions=" << dawTrackActionCount
                      << " rawTrackWorlds=" << rawTrackWorldCount
                      << " rawTrackControls=" << rawTrackControlCount
                      << std::endl;
            if (!g_debugPrinted) {
                g_debugPrinted = true;
                int dumpCount = 0;
                for (const auto& world : baseSystem.level->worlds) {
                    if (world.name.rfind("TrackRowWorld", 0) != 0
                        && world.name.rfind("MidiTrackRowWorld", 0) != 0) {
                        continue;
                    }
                    std::cerr << "  [ButtonCache] world='" << world.name
                              << "' instances=" << world.instances.size() << std::endl;
                    for (const auto& inst : world.instances) {
                        if (inst.controlId.rfind("track_", 0) != 0
                            && inst.controlId.rfind("midi_track_", 0) != 0) {
                            continue;
                        }
                        std::cerr << "  [ButtonCache] inst controlId='" << inst.controlId
                                  << "' role='" << inst.controlRole
                                  << "' action='" << inst.actionType
                                  << "' key='" << inst.actionKey
                                  << "' proto=" << inst.prototypeID
                                  << std::endl;
                        if (++dumpCount >= 4) break;
                    }
                    if (dumpCount >= 4) break;
                }
            }
            ui.buttonCacheBuilt = true;
            ui.buttonCacheLevel = baseSystem.level.get();
        }

        void renderButtonsPass(BaseSystem& baseSystem,
                               std::vector<Entity>& prototypes,
                               UIContext& ui,
                               ButtonRenderPass pass,
                               GLFWwindow* win) {
            if (!baseSystem.renderer || !baseSystem.world || !baseSystem.level || !win) return;
            RendererContext& renderer = *baseSystem.renderer;
            WorldContext& world = *baseSystem.world;
            ensureResources(renderer, world);

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
                if (!shouldRenderButton(inst, pass)) continue;
                ButtonState& state = g_buttonStates[inst.instanceID];

                glm::vec3 baseFront = resolveColor(&world, inst.colorName, inst.color);
                glm::vec3 baseTop = resolveColor(&world, inst.topColorName, inst.topColor);
                glm::vec3 baseSide = resolveColor(&world, inst.sideColorName, inst.sideColor);
                bool topExplicit = !(inst.topColorName.empty() && inst.topColor == glm::vec3(1.0f, 0.0f, 1.0f));
                bool sideExplicit = !(inst.sideColorName.empty() && inst.sideColor == glm::vec3(1.0f, 0.0f, 1.0f));

                std::string stateKey = inst.uiState;
                if (stateKey.empty()) {
                    stateKey = state.toggled ? "active" : "idle";
                }
                const UiStateColors* stateDef = findUiState(inst.uiStates, stateKey);
                if (!stateDef && inst.prototypeID >= 0 && inst.prototypeID < static_cast<int>(prototypes.size())) {
                    stateDef = findUiState(prototypes[inst.prototypeID].uiStates, stateKey);
                }
                if (stateDef) {
                    applyUiStateColors(*stateDef, world, baseFront, baseTop, baseSide, topExplicit, sideExplicit);
                }

                if (!topExplicit) {
                    baseTop = glm::clamp(baseFront + glm::vec3(0.1f), glm::vec3(0.0f), glm::vec3(1.0f));
                }
                if (!sideExplicit) {
                    baseSide = glm::clamp(baseFront - glm::vec3(0.03f), glm::vec3(0.0f), glm::vec3(1.0f));
                }

                glm::vec2 halfSizePx(inst.size.x, inst.size.y);
                glm::vec2 centerPx(inst.position.x, inst.position.y);
                if (kDebugRelayoutTrackButtons && isSidePanelButton(inst)) {
                    float baseX = 200.0f;
                    if (inst.controlId.find("_clear") != std::string::npos) centerPx.x = baseX;
                    else if (inst.controlId.find("_input") != std::string::npos) centerPx.x = baseX + 60.0f;
                    else if (inst.controlId.find("_arm") != std::string::npos) centerPx.x = baseX + 120.0f;
                    else if (inst.controlId.find("_solo") != std::string::npos) centerPx.x = baseX + 180.0f;
                    else if (inst.controlId.find("_mute") != std::string::npos) centerPx.x = baseX + 240.0f;
                    else if (inst.controlId.find("_output") != std::string::npos) centerPx.x = baseX + 300.0f;
                }

                float depthPx = inst.size.z > 0.0f ? inst.size.z : 10.0f;
                buildButtonGeometry(centerPx, halfSizePx, depthPx, state.pressAnim, baseFront, baseTop, baseSide,
                                    screenWidth, screenHeight, vertices);
            }

            if (vertices.empty() || !renderer.uiColorShader) return;

            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendColor(0.0f, 0.0f, 0.0f, kButtonAlpha);
            glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
            glBindVertexArray(renderer.uiButtonVAO);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.uiButtonVBO);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(ButtonVertex), vertices.data(), GL_DYNAMIC_DRAW);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ButtonVertex), (void*)offsetof(ButtonVertex, pos));
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ButtonVertex), (void*)offsetof(ButtonVertex, color));
            glEnableVertexAttribArray(1);

            renderer.uiColorShader->use();
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_DEPTH_TEST);
        }
    }

    void UpdateButtonInput(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.ui || !baseSystem.level || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;

        if (ui.consumeClick) {
            ui.uiLeftDown = false;
            ui.uiLeftPressed = false;
            ui.uiLeftReleased = false;
            ui.consumeClick = false;
        }

        if (!ui.buttonCacheBuilt || ui.buttonCacheLevel != baseSystem.level.get()) {
            buildButtonCache(baseSystem, prototypes, ui);
        }

        for (auto* instPtr : ui.buttonInstances) {
            if (!instPtr) continue;
            EntityInstance& inst = *instPtr;
            ButtonState& state = g_buttonStates[inst.instanceID];
            updateButtonState(inst, state, ui, dt);
        }

        ui.uiLeftPressed = false;
        ui.uiLeftReleased = false;
    }

    void RenderButtonsSide(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt;
        if (!baseSystem.ui) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
        renderButtonsPass(baseSystem, prototypes, ui, ButtonRenderPass::Side, win);
    }

    void RenderButtonsTopBottom(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt;
        if (!baseSystem.ui) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
        renderButtonsPass(baseSystem, prototypes, ui, ButtonRenderPass::TopBottom, win);
    }

    void UpdateButtons(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        UpdateButtonInput(baseSystem, prototypes, dt, win);
        if (!baseSystem.ui || !baseSystem.ui->active) return;
        renderButtonsPass(baseSystem, prototypes, *baseSystem.ui, ButtonRenderPass::All, win);
    }
}
