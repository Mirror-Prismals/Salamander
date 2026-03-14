#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <cctype>

namespace ButtonSystemLogic { float GetButtonPressOffset(int instanceID); }

namespace GlyphSystemLogic {

    struct GlyphInstance {
        EntityInstance* glyph = nullptr;
        EntityInstance* button = nullptr;
    };

    namespace {
        constexpr float kGlyphAlpha = 0.85f;

        glm::vec3 resolveColor(const WorldContext* world, const std::string& name, const glm::vec3& fallback) {
            if (world && !name.empty()) {
                auto it = world->colorLibrary.find(name);
                if (it != world->colorLibrary.end()) return it->second;
            }
            return fallback;
        }

        int resolveGlyphType(std::string type) {
            if (type.empty()) return -1;
            std::transform(type.begin(), type.end(), type.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (type == "stop") return 0;
            if (type == "play") return 1;
            if (type == "record") return 2;
            if (type == "back" || type == "rewind") return 3;
            return -1;
        }

        void ensureResources(RendererContext& renderer, WorldContext& world) {
            if (!renderer.glyphShader) {
                renderer.glyphShader = std::make_unique<Shader>(world.shaders["GLYPH_VERTEX_SHADER"].c_str(),
                                                               world.shaders["GLYPH_FRAGMENT_SHADER"].c_str());
            }
            if (renderer.glyphVAO == 0) {
                glGenVertexArrays(1, &renderer.glyphVAO);
            }
        }
    }

    void UpdateGlyphs(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt;
        if (!baseSystem.ui || !baseSystem.renderer || !baseSystem.level || !baseSystem.world || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active) return;
        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;

        std::vector<GlyphInstance> glyphs;
        std::unordered_map<std::string, EntityInstance*> buttonsById;

        for (auto& world : baseSystem.level->worlds) {
            for (auto& inst : world.instances) {
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const std::string& protoName = prototypes[inst.prototypeID].name;
                if (protoName == "Button") {
                    if (!inst.controlId.empty()) {
                        buttonsById[inst.controlId] = &inst;
                    }
                } else if (protoName == "Glyph") {
                    glyphs.push_back({&inst, nullptr});
                }
            }
        }

        if (!glyphs.empty()) {
            for (auto& entry : glyphs) {
                if (!entry.glyph || entry.glyph->controlId.empty()) continue;
                auto it = buttonsById.find(entry.glyph->controlId);
                if (it != buttonsById.end()) {
                    entry.button = it->second;
                }
            }
        } else {
            return;
        }

        ensureResources(renderer, world);
        if (!renderer.glyphShader) return;

        int fbw = 0, fbh = 0;
        int winW = 0, winH = 0;
        glfwGetFramebufferSize(win, &fbw, &fbh);
        glfwGetWindowSize(win, &winW, &winH);
        double screenWidth = fbw > 0 ? fbw : 1920.0;
        double screenHeight = fbh > 0 ? fbh : 1080.0;
        double scaleX = (winW > 0) ? (static_cast<double>(fbw) / static_cast<double>(winW)) : 1.0;
        double scaleY = (winH > 0) ? (static_cast<double>(fbh) / static_cast<double>(winH)) : 1.0;

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendColor(0.0f, 0.0f, 0.0f, kGlyphAlpha);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        renderer.glyphShader->use();
        renderer.glyphShader->setVec2("uResolution", glm::vec2(static_cast<float>(screenWidth),
                                                               static_cast<float>(screenHeight)));
        glBindVertexArray(renderer.glyphVAO);

        auto drawGlyph = [&](const glm::vec2& centerPx,
                             const glm::vec2& sizePx,
                             float pressOffset,
                             int typeId,
                             const glm::vec3& color) {
            renderer.glyphShader->setVec2("uCenter", centerPx);
            renderer.glyphShader->setVec2("uButtonSize", sizePx);
            renderer.glyphShader->setFloat("uPressOffset", pressOffset);
            renderer.glyphShader->setInt("uType", typeId);
            renderer.glyphShader->setVec3("uColor", color);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        };

        if (!glyphs.empty()) {
            for (const auto& g : glyphs) {
                if (!g.glyph) continue;
                std::string type = g.glyph->text;
                if (type.empty()) type = g.glyph->textKey;
                if (type.empty()) type = g.glyph->actionKey;
                int typeId = resolveGlyphType(type);
                if (typeId < 0) continue;

                glm::vec3 color = resolveColor(&world, g.glyph->colorName, g.glyph->color);
                float pressOffset = 0.0f;
                glm::vec2 sizePx(g.glyph->size.x * 2.0f * static_cast<float>(scaleX),
                                 g.glyph->size.y * 2.0f * static_cast<float>(scaleY));
                if ((sizePx.x <= 0.0f || sizePx.y <= 0.0f) && g.button) {
                    sizePx = glm::vec2(g.button->size.x * 2.0f * static_cast<float>(scaleX),
                                       g.button->size.y * 2.0f * static_cast<float>(scaleY));
                }
                if (g.button) {
                    float press = ButtonSystemLogic::GetButtonPressOffset(g.button->instanceID);
                    pressOffset = press * (g.button->size.z > 0.0f ? g.button->size.z * 0.5f : 5.0f) * static_cast<float>(scaleY);
                }

                glm::vec2 centerPx(static_cast<float>(g.glyph->position.x * scaleX),
                                   static_cast<float>(g.glyph->position.y * scaleY));
                drawGlyph(centerPx, sizePx, pressOffset, typeId, color);
            }
        }

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
    }
}
