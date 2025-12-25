#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <optional>
#include "json.hpp"

namespace ButtonSystemLogic { float GetButtonPressOffset(int instanceID); }
using json = nlohmann::json;

namespace GlyphSystemLogic {

    struct GlyphDef {
        int buttonIndex = -1;
        std::string type;
        std::string colorName;
        glm::vec3 color{1.0f};
    };

    static std::vector<GlyphDef> g_glyphs;
    static bool g_loaded = false;

    namespace {
        int findWorldIndexByName(BaseSystem& baseSystem, const std::string& name) {
            if (!baseSystem.level) return -1;
            for (size_t i = 0; i < baseSystem.level->worlds.size(); ++i) {
                if (baseSystem.level->worlds[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        void ensureLoaded(BaseSystem& baseSystem) {
            if (g_loaded) return;
            g_loaded = true;
            std::ifstream f("Procedures/glyphs.json");
            if (!f.is_open()) { std::cerr << "GlyphSystem: missing Procedures/glyphs.json\n"; return; }
            json data = json::parse(f);
            if (!data.contains("glyphs") || !data["glyphs"].is_array()) return;
            for (const auto& g : data["glyphs"]) {
                GlyphDef def;
                if (g.contains("buttonIndex")) def.buttonIndex = g["buttonIndex"].get<int>();
                if (g.contains("type")) def.type = g["type"].get<std::string>();
                if (g.contains("color")) {
                    def.colorName = g["color"].get<std::string>();
                    if (baseSystem.world && baseSystem.world->colorLibrary.count(def.colorName)) {
                        def.color = baseSystem.world->colorLibrary[def.colorName];
                    }
                }
                g_glyphs.push_back(def);
            }
        }

        GLuint createGlyphProgram() {
            static const char* vsSrc = R"(#version 330 core
            void main() {
                vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
                gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
            })";

            static const char* fsSrc = R"(#version 330 core
            out vec4 FragColor;
            uniform vec2 uResolution;
            uniform vec2 uCenter;
            uniform vec2 uButtonSize;
            uniform float uPressOffset;
            uniform int uType;
            uniform vec3 uColor;

            float tri(vec2 p, vec2 c, float s, float dir) {
                float h = s * 0.86602540378;
                vec2 A = c + vec2(-dir * h / 3.0,  s / 2.0);
                vec2 B = c + vec2(-dir * h / 3.0, -s / 2.0);
                vec2 C = c + vec2( dir * 2.0 * h / 3.0, 0.0);
                vec2 v0 = B - A, v1 = C - A, v2 = p - A;
                float d00 = dot(v0,v0), d01 = dot(v0,v1), d11 = dot(v1,v1);
                float d20 = dot(v2,v0), d21 = dot(v2,v1);
                float denom = d00 * d11 - d01 * d01;
                float v = (d11 * d20 - d01 * d21) / denom;
                float w = (d00 * d21 - d01 * d20) / denom;
                float u = 1.0 - v - w;
                return step(0.0, min(u, min(v, w)));
            }

            float squareMask(vec2 p, vec2 c, float s) {
                vec2 d = abs(p - c);
                return step(max(d.x, d.y), s * 0.5);
            }

            float circleMask(vec2 p, vec2 c, float r) {
                return step(length(p - c), r);
            }

            float bar(vec2 p, vec2 c, vec2 size) {
                vec2 d = abs(p - c);
                return step(max(d.x - size.x, d.y - size.y), 0.0);
            }

            void main() {
                vec2 p = gl_FragCoord.xy;
                // Button positions are authored in top-left origin space; convert to GL bottom-left.
                // Apply press offset with full left shift and half vertical shift.
                vec2 pressVec = vec2(-uPressOffset, uPressOffset * 0.5);
                vec2 center = vec2(uCenter.x, uResolution.y - uCenter.y) + pressVec;

                float base = min(uButtonSize.x, uButtonSize.y) * 0.8;
                float s = base * 0.85;

                float mask = 0.0;
                if (uType == 0) { // Stop
                    mask = squareMask(p, center, s);
                } else if (uType == 1) { // Play
                    mask = tri(p, center, s, 1.0);
                } else if (uType == 2) { // Record
                    mask = circleMask(p, center, s * 0.5);
                } else if (uType == 3) { // Back
                    float mTri = tri(p, center + vec2(s * 0.18, 0.0), s, -1.0);
                    float mBar = bar(p, center + vec2(-s * 0.42, 0.0), vec2(s * 0.027, s * 0.5));
                    mask = max(mTri, mBar);
                }

                if (mask < 0.5) discard;
                FragColor = vec4(uColor, 1.0);
            })";

            auto compileShader = [](GLenum type, const char* src) -> GLuint {
                GLuint s = glCreateShader(type);
                glShaderSource(s, 1, &src, nullptr);
                glCompileShader(s);
                GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
                if (!ok) {
                    char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log);
                    std::cerr << "GlyphSystem shader error: " << log << std::endl;
                }
                return s;
            };

            GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
            GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
            GLuint prog = glCreateProgram();
            glAttachShader(prog, vs);
            glAttachShader(prog, fs);
            glLinkProgram(prog);
            glDeleteShader(vs);
            glDeleteShader(fs);
            return prog;
        }
    }

    void UpdateGlyphs(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.ui || !baseSystem.renderer || !baseSystem.level || !baseSystem.world || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active) return;

        ensureLoaded(baseSystem);
        if (g_glyphs.empty()) return;

        int buttonWorldIndex = findWorldIndexByName(baseSystem, "UIButtonWorld");
        if (buttonWorldIndex < 0 || buttonWorldIndex >= static_cast<int>(baseSystem.level->worlds.size())) return;
        Entity& buttonWorld = baseSystem.level->worlds[buttonWorldIndex];

        // Map button indices to instances (order in world.instances).
        std::vector<EntityInstance*> buttons;
        for (auto& inst : buttonWorld.instances) {
            if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
            if (prototypes[inst.prototypeID].name != "Button") continue;
            buttons.push_back(&inst);
        }

        static GLuint program = createGlyphProgram();
        static GLuint vao = 0;
        if (vao == 0) glGenVertexArrays(1, &vao);

        GLint uResLoc = glGetUniformLocation(program, "uResolution");
        GLint uCenterLoc = glGetUniformLocation(program, "uCenter");
        GLint uButtonSizeLoc = glGetUniformLocation(program, "uButtonSize");
        GLint uPressLoc = glGetUniformLocation(program, "uPressOffset");
        GLint uTypeLoc = glGetUniformLocation(program, "uType");
        GLint uColorLoc = glGetUniformLocation(program, "uColor");

        int fbw = 0, fbh = 0;
        int winW = 0, winH = 0;
        glfwGetFramebufferSize(win, &fbw, &fbh);
        glfwGetWindowSize(win, &winW, &winH);
        double screenWidth = fbw > 0 ? fbw : 1920.0;
        double screenHeight = fbh > 0 ? fbh : 1080.0;
        double scaleX = (winW > 0) ? (static_cast<double>(fbw) / static_cast<double>(winW)) : 1.0;
        double scaleY = (winH > 0) ? (static_cast<double>(fbh) / static_cast<double>(winH)) : 1.0;

        glDisable(GL_DEPTH_TEST);
        glUseProgram(program);
        glUniform2f(uResLoc, static_cast<float>(screenWidth), static_cast<float>(screenHeight));
        glBindVertexArray(vao);

        for (const auto& g : g_glyphs) {
            if (g.buttonIndex < 0 || g.buttonIndex >= static_cast<int>(buttons.size())) continue;
            EntityInstance* btn = buttons[g.buttonIndex];
            float press = ButtonSystemLogic::GetButtonPressOffset(btn->instanceID);
            float pressOffset = press * (btn->size.z > 0.0f ? btn->size.z * 0.5f : 5.0f) * static_cast<float>(scaleY);

            float cx = static_cast<float>(btn->position.x * scaleX);
            float cy = static_cast<float>(btn->position.y * scaleY);
            glUniform2f(uCenterLoc, cx, cy);
            glUniform2f(uButtonSizeLoc, btn->size.x * 2.0f * static_cast<float>(scaleX), btn->size.y * 2.0f * static_cast<float>(scaleY));
            glUniform1f(uPressLoc, pressOffset);

            int typeId = 0;
            if (g.type == "Stop") typeId = 0;
            else if (g.type == "Play") typeId = 1;
            else if (g.type == "Record") typeId = 2;
            else if (g.type == "Back") typeId = 3;
            glUniform1i(uTypeLoc, typeId);
            glUniform3f(uColorLoc, g.color.r, g.color.g, g.color.b);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        glEnable(GL_DEPTH_TEST);
    }
}
