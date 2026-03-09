#pragma once

#include <GLFW/glfw3.h>

namespace BootSequenceSystemLogic {
    namespace {
        constexpr float kBootLoadingSeconds = 10.0f;
        constexpr float kSwitchLoadingSeconds = 6.0f;

        struct UiVertex { glm::vec2 pos; glm::vec3 color; };

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

        void ensureFullscreenResources(RendererContext& renderer, WorldContext& world) {
        // Only need solid-color shader for the bar; skip fullscreen shader to avoid grey overlay.
    }

        void ensureBarResources(RendererContext& renderer, WorldContext& world) {
            if (!renderer.uiColorShader) {
                renderer.uiColorShader = std::make_unique<Shader>(world.shaders["UI_COLOR_VERTEX_SHADER"].c_str(), world.shaders["UI_COLOR_FRAGMENT_SHADER"].c_str());
            }
            if (renderer.uiButtonVAO == 0) {
                glGenVertexArrays(1, &renderer.uiButtonVAO);
                glGenBuffers(1, &renderer.uiButtonVBO);
            }
        }

        glm::vec3 resolveColor(const WorldContext* world, const std::string& name, const glm::vec3& fallback) {
            if (world && !name.empty()) {
                auto it = world->colorLibrary.find(name);
                if (it != world->colorLibrary.end()) return it->second;
            }
            return fallback;
        }

        EntityInstance* findLoadingBarInstance(BaseSystem& baseSystem, const std::vector<Entity>& prototypes) {
            if (!baseSystem.level) return nullptr;
            for (auto& world : baseSystem.level->worlds) {
                for (auto& inst : world.instances) {
                    if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                    if (prototypes[inst.prototypeID].name == "LoadingBar") {
                        return &inst;
                    }
                }
            }
            return nullptr;
        }
    }

    void UpdateBootSequence(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.ui || !baseSystem.registry) return;
        UIContext& ui = *baseSystem.ui;

        std::string currentLevel;
        auto regIt = baseSystem.registry->find("level");
        if (regIt != baseSystem.registry->end() && std::holds_alternative<std::string>(regIt->second)) {
            currentLevel = std::get<std::string>(regIt->second);
        }
        bool isMenuLevel = (currentLevel == "menu");

        bool bootLoaded = false;
        auto bootIt = baseSystem.registry->find("boot_loaded");
        if (bootIt != baseSystem.registry->end() && std::holds_alternative<bool>(bootIt->second)) {
            bootLoaded = std::get<bool>(bootIt->second);
        }

        // Start boot loading only when menu level is active.
        if (isMenuLevel && !ui.bootLoadingStarted && !bootLoaded) {
            ui.bootLoadingStarted = true;
            ui.loadingActive = true;
            ui.loadingTimer = 0.0f;
            ui.loadingDuration = kBootLoadingSeconds;
            ui.fullscreenColor = glm::vec3(0.0f);
        }

        // Start switch loading when requested by RegistryEditor.
        if (ui.levelSwitchPending && !ui.loadingActive) {
            ui.loadingActive = true;
            ui.loadingTimer = 0.0f;
            ui.loadingDuration = kSwitchLoadingSeconds;
            ui.fullscreenColor = glm::vec3(0.0f);
        }

        // UI active when in menu or loading overlay is up.
        if (isMenuLevel || ui.loadingActive) {
            ui.active = true;
        } else if (!ui.loadingActive && ui.active && ui.bootLoadingStarted && !isMenuLevel) {
            // Ensure non-menu starts release UI once boot path is done.
            ui.active = false;
            ui.cursorReleased = false;
        }
        // Never trigger UIScreen fullscreen during loading; keep sky visible.
        if (ui.loadingActive) {
            ui.fullscreenActive = false;
        }

        // Drive timer and trigger reload after delay when switching.
        if (ui.loadingActive) {
            ui.loadingTimer += dt;
            if (ui.loadingTimer >= ui.loadingDuration) {
                if (ui.levelSwitchPending && baseSystem.reloadRequested && baseSystem.reloadTarget) {
                    *baseSystem.reloadRequested = true;
                    *baseSystem.reloadTarget = ui.levelSwitchTarget;
                    ui.levelSwitchPending = false;
                } else if (isMenuLevel && !bootLoaded) {
                    (*baseSystem.registry)["boot_loaded"] = true;
                }
                ui.loadingActive = false;
                ui.loadingTimer = 0.0f;
            }
        }

        if (!ui.loadingActive) return;
        if (!baseSystem.renderer || !baseSystem.world || !win) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        ensureFullscreenResources(renderer, world);
        ensureBarResources(renderer, world);

        glDisable(GL_DEPTH_TEST);

        int windowWidth = 0, windowHeight = 0;
        glfwGetWindowSize(win, &windowWidth, &windowHeight);
        double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
        double screenHeight = windowHeight > 0 ? static_cast<double>(windowHeight) : 1080.0;
        float uiScaleX = 1.0f;
        float uiScaleY = 1.0f;
        if (baseSystem.app) {
            if (baseSystem.app->windowWidth > 0) {
                uiScaleX = static_cast<float>(screenWidth / static_cast<double>(baseSystem.app->windowWidth));
            }
            if (baseSystem.app->windowHeight > 0) {
                uiScaleY = static_cast<float>(screenHeight / static_cast<double>(baseSystem.app->windowHeight));
            }
        }

        // Draw loading bar regardless of level content to guarantee visibility.
        float progress = ui.loadingDuration > 0.0f ? std::min(1.0f, ui.loadingTimer / ui.loadingDuration) : 1.0f;
        float barHalfW = 200.0f;
        float barHalfH = 10.0f;
        float centerX = static_cast<float>(screenWidth * 0.5);
        float centerY = static_cast<float>(screenHeight * 0.85);
        float left = centerX - barHalfW;
        float top = centerY - barHalfH;
        float fullW = barHalfW * 2.0f;
        float fullH = barHalfH * 2.0f;
        std::vector<UiVertex> vertices;
        vertices.reserve(12);
        glm::vec3 fillColor(1.0f, 1.0f, 1.0f);
        glm::vec3 backColor(0.2f, 0.2f, 0.2f);

        if (EntityInstance* barInst = findLoadingBarInstance(baseSystem, prototypes)) {
            centerX = barInst->position.x * uiScaleX;
            centerY = barInst->position.y * uiScaleY;
            if (barInst->size.x > 0.0f) barHalfW = barInst->size.x * uiScaleX;
            if (barInst->size.y > 0.0f) barHalfH = barInst->size.y * uiScaleY;
            fillColor = resolveColor(&world, barInst->colorName, barInst->color);
            bool hasBack = !(barInst->topColorName.empty() && barInst->topColor == glm::vec3(1.0f, 0.0f, 1.0f));
            if (hasBack) {
                backColor = resolveColor(&world, barInst->topColorName, barInst->topColor);
            }
            left = centerX - barHalfW;
            top = centerY - barHalfH;
            fullW = barHalfW * 2.0f;
            fullH = barHalfH * 2.0f;
        }

        glm::vec2 bgA(left, top);
        glm::vec2 bgB(left + fullW, top);
        glm::vec2 bgC(left + fullW, top + fullH);
        glm::vec2 bgD(left, top + fullH);
        pushQuad(vertices,
                 pixelToNDC(bgA, screenWidth, screenHeight),
                 pixelToNDC(bgB, screenWidth, screenHeight),
                 pixelToNDC(bgC, screenWidth, screenHeight),
                 pixelToNDC(bgD, screenWidth, screenHeight),
                 backColor);

        float fillW = fullW * progress;
        glm::vec2 fillA(left, top);
        glm::vec2 fillB(left + fillW, top);
        glm::vec2 fillC(left + fillW, top + fullH);
        glm::vec2 fillD(left, top + fullH);
        pushQuad(vertices,
                 pixelToNDC(fillA, screenWidth, screenHeight),
                 pixelToNDC(fillB, screenWidth, screenHeight),
                 pixelToNDC(fillC, screenWidth, screenHeight),
                 pixelToNDC(fillD, screenWidth, screenHeight),
                 fillColor);

        if (!vertices.empty() && renderer.uiColorShader) {
            glBindVertexArray(renderer.uiButtonVAO);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.uiButtonVBO);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(UiVertex), vertices.data(), GL_DYNAMIC_DRAW);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, pos));
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, color));
            glEnableVertexAttribArray(1);
            renderer.uiColorShader->use();
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
        }

        glEnable(GL_DEPTH_TEST);
    }
}
