#pragma once

#include "Host/PlatformInput.h"

namespace UIScreenSystemLogic {

    namespace {
        constexpr float POSITION_EPSILON = 0.2f;
        constexpr float kScreenAlphaDefault = 0.85f;

        bool positionsMatch(const glm::vec3& a, const glm::vec3& b) {
            return glm::length(a - b) < POSITION_EPSILON;
        }

        EntityInstance* findInstanceById(LevelContext& level, int worldIndex, int instanceId) {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return nullptr;
            Entity& world = level.worlds[worldIndex];
            for (auto& inst : world.instances) {
                if (inst.instanceID == instanceId) return &inst;
            }
            return nullptr;
        }

        void buildComputerCache(BaseSystem& baseSystem, std::vector<Entity>& prototypes, UIContext& ui) {
            if (!baseSystem.level) return;
            ui.computerInstances.clear();
            for (size_t wi = 0; wi < baseSystem.level->worlds.size(); ++wi) {
                const auto& world = baseSystem.level->worlds[wi];
                for (const auto& inst : world.instances) {
                    if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                    if (prototypes[inst.prototypeID].name != "Computer") continue;
                    ui.computerInstances.emplace_back(static_cast<int>(wi), inst.instanceID);
                }
            }
            ui.computerCacheBuilt = true;
        }

        EntityInstance* findTargetedComputer(BaseSystem& baseSystem,
                                             std::vector<Entity>& prototypes,
                                             int worldIndex,
                                             const glm::vec3& targetCenter) {
            if (!baseSystem.level || !baseSystem.ui) return nullptr;
            if (worldIndex < 0 || worldIndex >= static_cast<int>(baseSystem.level->worlds.size())) return nullptr;
            UIContext& ui = *baseSystem.ui;

            // Always prefer a direct world scan at the targeted cell so freshly placed
            // computers are interactable immediately even if cache invalidation lags.
            Entity& world = baseSystem.level->worlds[static_cast<size_t>(worldIndex)];
            for (auto& inst : world.instances) {
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                if (prototypes[static_cast<size_t>(inst.prototypeID)].name != "Computer") continue;
                if (!positionsMatch(inst.position, targetCenter)) continue;
                return &inst;
            }

            if (!ui.computerCacheBuilt) {
                buildComputerCache(baseSystem, prototypes, ui);
            }
            for (const auto& ref : ui.computerInstances) {
                if (ref.first != worldIndex) continue;
                EntityInstance* inst = findInstanceById(*baseSystem.level, ref.first, ref.second);
                if (!inst) continue;
                if (!positionsMatch(inst->position, targetCenter)) continue;
                return inst;
            }
            return nullptr;
        }

        void ensureUIResources(RendererContext& renderer, WorldContext& world, IRenderBackend& renderBackend) {
            if (!renderer.uiShader) {
                renderer.uiShader = std::make_unique<Shader>(world.shaders["UI_VERTEX_SHADER"].c_str(), world.shaders["UI_FRAGMENT_SHADER"].c_str());
            }
            if (renderer.uiVAO == 0) {
                float quad[] = {
                    -1.0f, -1.0f,
                     1.0f, -1.0f,
                     1.0f,  1.0f,
                    -1.0f, -1.0f,
                     1.0f,  1.0f,
                    -1.0f,  1.0f
                };
                static const std::vector<VertexAttribLayout> kQuadPos2Layout = {
                    {0, 2, VertexAttribType::Float, false, static_cast<unsigned int>(2u * sizeof(float)), 0, 0}
                };
                renderBackend.ensureVertexArray(renderer.uiVAO);
                renderBackend.ensureArrayBuffer(renderer.uiVBO);
                renderBackend.uploadArrayBufferData(renderer.uiVBO, quad, sizeof(quad), false);
                renderBackend.configureVertexArray(renderer.uiVAO, renderer.uiVBO, kQuadPos2Layout, 0, {});
            }
        }

        int findWorldIndexByName(BaseSystem& baseSystem, const std::string& name) {
            if (!baseSystem.level) return -1;
            for (size_t i = 0; i < baseSystem.level->worlds.size(); ++i) {
                if (baseSystem.level->worlds[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        void setComputerColor(BaseSystem& baseSystem, const glm::vec3& color) {
            if (!baseSystem.level || !baseSystem.world || !baseSystem.ui) return;
            UIContext& ui = *baseSystem.ui;
            if (ui.activeWorldIndex < 0) return;
            EntityInstance* inst = findInstanceById(*baseSystem.level, ui.activeWorldIndex, ui.activeInstanceID);
            if (inst) inst->color = color;
        }
    }

    void UpdateUIScreen(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.player || !baseSystem.level || !baseSystem.ui) return;
        PlayerContext& player = *baseSystem.player;
        UIContext& ui = *baseSystem.ui;

        // Keep computer textures untinted by default so atlas artwork is visible.
        glm::vec3 computerTint(1.0f);
        if (baseSystem.world) {
            auto it = baseSystem.world->colorLibrary.find("White");
            if (it != baseSystem.world->colorLibrary.end()) computerTint = it->second;
        }
        if (!ui.computerCacheBuilt) {
            buildComputerCache(baseSystem, prototypes, ui);
        }
        bool cacheValid = true;
        for (const auto& ref : ui.computerInstances) {
            EntityInstance* inst = findInstanceById(*baseSystem.level, ref.first, ref.second);
            if (!inst) { cacheValid = false; continue; }
            if (ui.active && ref.first == ui.activeWorldIndex && inst->instanceID == ui.activeInstanceID) continue;
            inst->color = computerTint;
        }
        if (!cacheValid) {
            ui.computerCacheBuilt = false;
        }

        // Activate when clicking the computer block
        if (!ui.active && player.leftMousePressed && player.hasBlockTarget && player.targetedWorldIndex >= 0) {
            EntityInstance* computerInst = findTargetedComputer(baseSystem, prototypes, player.targetedWorldIndex, player.targetedBlockPosition);
            if (computerInst) {
                ui.active = true;
                ui.fullscreenActive = true;
                ui.activeWorldIndex = player.targetedWorldIndex;
                ui.activeInstanceID = computerInst->instanceID;
                ui.consumeClick = true;
                ui.uiLeftDown = ui.uiLeftPressed = ui.uiLeftReleased = false;
                if (baseSystem.world) {
                    auto it = baseSystem.world->colorLibrary.find("White");
                    if (it != baseSystem.world->colorLibrary.end()) {
                        computerInst->color = it->second;
                    }
                }
            }
        }

        // Exit on rising edge of Return
        static bool pPressedLast = false;
        bool pDown = PlatformInput::IsKeyDown(win, PlatformInput::Key::Enter);
        if (ui.active && pDown && !pPressedLast) {
            ui.active = false;
            ui.fullscreenActive = false;
            if (baseSystem.world) {
                auto it = baseSystem.world->colorLibrary.find("White");
                if (it != baseSystem.world->colorLibrary.end()) setComputerColor(baseSystem, it->second);
            }
        }
        pPressedLast = pDown;

        if (!ui.active) {
            // Ensure the computer remains untinted when idle.
            if (baseSystem.world) {
                auto it = baseSystem.world->colorLibrary.find("White");
                if (it != baseSystem.world->colorLibrary.end()) {
                    setComputerColor(baseSystem, it->second);
                }
            }
            return;
        }
        if (!ui.fullscreenActive) return;

        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.renderBackend) return;
        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        auto& renderBackend = *baseSystem.renderBackend;
        ensureUIResources(renderer, world, renderBackend);

        glm::vec3 screenColor(0.1f);
        int screenWorldIndex = findWorldIndexByName(baseSystem, "DAWScreenWorld");
        if (screenWorldIndex >= 0 && screenWorldIndex < static_cast<int>(baseSystem.level->worlds.size())) {
            Entity& screenWorld = baseSystem.level->worlds[screenWorldIndex];
            for (const auto& inst : screenWorld.instances) {
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                if (prototypes[inst.prototypeID].name != "Screen") continue;
                screenColor = inst.color;
                break;
            }
        } else {
            auto it = world.colorLibrary.find("DarkGray");
            if (it != world.colorLibrary.end()) screenColor = it->second;
        }

        if (renderer.uiShader) {
            auto setDepthTestEnabled = [&](bool enabled) {
                renderBackend.setDepthTestEnabled(enabled);
            };
            auto setBlendEnabled = [&](bool enabled) {
                renderBackend.setBlendEnabled(enabled);
            };
            auto setBlendModeConstantAlpha = [&](float alpha) {
                renderBackend.setBlendModeConstantAlpha(alpha);
            };
            auto setBlendModeAlpha = [&]() {
                renderBackend.setBlendModeAlpha();
            };

            setDepthTestEnabled(false);
            setBlendEnabled(true);
            float screenAlpha = kScreenAlphaDefault;
            if (baseSystem.daw) {
                screenAlpha = std::clamp(baseSystem.daw->activeThemeBackground.a, 0.0f, 1.0f);
            }
            setBlendModeConstantAlpha(screenAlpha);
            renderer.uiShader->use();
            renderer.uiShader->setVec3("color", screenColor);
            renderBackend.bindVertexArray(renderer.uiVAO);
            renderBackend.drawArraysTriangles(0, 6);
            setBlendModeAlpha();
            setDepthTestEnabled(true);
        }
    }
}
