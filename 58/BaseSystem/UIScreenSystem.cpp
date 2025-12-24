#pragma once

#include <GLFW/glfw3.h>

namespace UIScreenSystemLogic {

    namespace {
        constexpr float POSITION_EPSILON = 0.2f;

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
            UIContext& ui = *baseSystem.ui;
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

        void ensureUIResources(RendererContext& renderer, WorldContext& world) {
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
                glGenVertexArrays(1, &renderer.uiVAO);
                glGenBuffers(1, &renderer.uiVBO);
                glBindVertexArray(renderer.uiVAO);
                glBindBuffer(GL_ARRAY_BUFFER, renderer.uiVBO);
                glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
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

    void UpdateUIScreen(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player || !baseSystem.level || !baseSystem.ui) return;
        PlayerContext& player = *baseSystem.player;
        UIContext& ui = *baseSystem.ui;

        // Keep computer blocks dark gray by default
        glm::vec3 darkGray(0.1f);
        if (baseSystem.world) {
            auto it = baseSystem.world->colorLibrary.find("DarkGray");
            if (it != baseSystem.world->colorLibrary.end()) darkGray = it->second;
        }
        if (!ui.computerCacheBuilt) {
            buildComputerCache(baseSystem, prototypes, ui);
        }
        bool cacheValid = true;
        for (const auto& ref : ui.computerInstances) {
            EntityInstance* inst = findInstanceById(*baseSystem.level, ref.first, ref.second);
            if (!inst) { cacheValid = false; continue; }
            if (ui.active && ref.first == ui.activeWorldIndex && inst->instanceID == ui.activeInstanceID) continue;
            inst->color = darkGray;
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

        // Exit on rising edge of P
        static bool pPressedLast = false;
        bool pDown = win ? (glfwGetKey(win, GLFW_KEY_P) == GLFW_PRESS) : false;
        if (ui.active && pDown && !pPressedLast) {
            ui.active = false;
            ui.fullscreenActive = false;
            if (baseSystem.world) {
                auto it = baseSystem.world->colorLibrary.find("DarkGray");
                if (it != baseSystem.world->colorLibrary.end()) setComputerColor(baseSystem, it->second);
            }
        }
        pPressedLast = pDown;

        if (!ui.active) {
            // Ensure the computer is dark gray when idle
            if (baseSystem.world) {
                auto it = baseSystem.world->colorLibrary.find("DarkGray");
                if (it != baseSystem.world->colorLibrary.end()) {
                    setComputerColor(baseSystem, it->second);
                }
            }
            return;
        }
        if (!ui.fullscreenActive) return;

        if (!baseSystem.renderer || !baseSystem.world) return;
        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        ensureUIResources(renderer, world);

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
            glDisable(GL_DEPTH_TEST);
            renderer.uiShader->use();
            renderer.uiShader->setVec3("color", screenColor);
            glBindVertexArray(renderer.uiVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glEnable(GL_DEPTH_TEST);
        }
    }
}
