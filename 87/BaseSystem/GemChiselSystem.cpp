#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace AudioSystemLogic { bool TriggerGameplaySfx(BaseSystem& baseSystem, const std::string& cueName, float gain); }
namespace OreMiningSystemLogic { bool IsMiningActive(const BaseSystem& baseSystem); }
namespace GroundCraftingSystemLogic { bool IsRitualActive(const BaseSystem& baseSystem); }

namespace GemChiselSystemLogic {
    namespace {
        struct UiColorVertex {
            glm::vec2 pos;
            glm::vec3 color;
        };

        struct GemVertex {
            glm::vec3 pos;
            glm::vec3 color;
        };

        struct FaceDesc {
            glm::ivec3 neighbor;
            glm::vec3 normal;
            glm::vec3 axisU;
            glm::vec3 axisV;
        };

        struct GemRenderSetup {
            glm::vec3 modelCenter = glm::vec3(0.0f);
            glm::ivec3 minCell = glm::ivec3(0);
            glm::ivec3 maxCell = glm::ivec3(0);
            float voxelSize = 0.2f;
            float cameraDistance = 4.0f;
            glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 4.0f);
            glm::mat4 view = glm::mat4(1.0f);
            glm::mat4 proj = glm::mat4(1.0f);
            float windowW = 1920.0f;
            float windowH = 1080.0f;
        };

        struct GemChiselState {
            bool active = false;
            bool cursorCaptured = false;
            bool awaitingInitialRelease = false;
            bool rightDragging = false;
            bool lastLeftDown = false;
            bool lastRightDown = false;
            bool lastEscapeDown = false;
            double lastMouseX = 0.0;
            double lastMouseY = 0.0;
            glm::ivec3 targetCell = glm::ivec3(0);
            int dropIndex = -1;
            int hoveredVoxelIndex = -1;
            float yaw = 0.0f;
            float pitch = 20.0f;
            GLuint overlayVao = 0;
            GLuint overlayVbo = 0;
            GLuint meshVao = 0;
            GLuint meshVbo = 0;
        };

        GemChiselState& state() {
            static GemChiselState s;
            return s;
        }

        bool readRegistryBool(const BaseSystem& baseSystem, const char* key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            std::string raw = std::get<std::string>(it->second);
            std::transform(raw.begin(), raw.end(), raw.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (raw == "1" || raw == "true" || raw == "yes" || raw == "on") return true;
            if (raw == "0" || raw == "false" || raw == "no" || raw == "off") return false;
            return fallback;
        }

        float readRegistryFloat(const BaseSystem& baseSystem, const char* key, float fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stof(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        void orePalette(int kind, glm::vec3& startColor, glm::vec3& endColor) {
            switch (kind) {
                case 0: // ruby
                    startColor = glm::vec3(0.62f, 0.04f, 0.07f);
                    endColor = glm::vec3(1.00f, 0.20f, 0.22f);
                    return;
                case 1: // amethyst
                    startColor = glm::vec3(0.36f, 0.12f, 0.62f);
                    endColor = glm::vec3(0.73f, 0.56f, 0.94f);
                    return;
                case 2: // flourite/fluorite
                    startColor = glm::vec3(0.10f, 0.28f, 0.78f);
                    endColor = glm::vec3(0.36f, 0.70f, 1.00f);
                    return;
                case 3: // silver
                default:
                    startColor = glm::vec3(0.78f, 0.78f, 0.82f);
                    endColor = glm::vec3(0.98f, 0.98f, 0.99f);
                    return;
            }
        }

        int findDropIndexAtCell(const BaseSystem& baseSystem, const glm::ivec3& cell) {
            if (!baseSystem.gems) return -1;
            const GemContext& gems = *baseSystem.gems;
            for (int i = 0; i < static_cast<int>(gems.drops.size()); ++i) {
                const GemDropState& drop = gems.drops[static_cast<size_t>(i)];
                const glm::ivec3 dropCell = glm::ivec3(glm::round(drop.position));
                if (dropCell == cell) return i;
            }
            return -1;
        }

        int resolveDropIndex(const BaseSystem& baseSystem, const GemChiselState& s) {
            if (!baseSystem.gems) return -1;
            const GemContext& gems = *baseSystem.gems;
            if (s.dropIndex >= 0 && s.dropIndex < static_cast<int>(gems.drops.size())) {
                return s.dropIndex;
            }
            return findDropIndexAtCell(baseSystem, s.targetCell);
        }

        int64_t packVoxelCoord(int x, int y, int z) {
            const int64_t ox = static_cast<int64_t>(x) + 1048576;
            const int64_t oy = static_cast<int64_t>(y) + 1048576;
            const int64_t oz = static_cast<int64_t>(z) + 1048576;
            return ((ox & 0x1fffffLL) << 42)
                | ((oy & 0x1fffffLL) << 21)
                | (oz & 0x1fffffLL);
        }

        bool rayIntersectsAabb(const glm::vec3& origin,
                               const glm::vec3& direction,
                               const glm::vec3& minBounds,
                               const glm::vec3& maxBounds,
                               float& outDistance) {
            constexpr float kEps = 1e-6f;
            float tNear = -std::numeric_limits<float>::infinity();
            float tFar = std::numeric_limits<float>::infinity();

            for (int axis = 0; axis < 3; ++axis) {
                const float o = origin[axis];
                const float d = direction[axis];
                const float minB = minBounds[axis];
                const float maxB = maxBounds[axis];

                if (std::abs(d) < kEps) {
                    if (o < minB || o > maxB) return false;
                    continue;
                }

                float t1 = (minB - o) / d;
                float t2 = (maxB - o) / d;
                if (t1 > t2) std::swap(t1, t2);
                tNear = std::max(tNear, t1);
                tFar = std::min(tFar, t2);
                if (tNear > tFar) return false;
            }

            if (tFar < 0.0f) return false;
            outDistance = (tNear >= 0.0f) ? tNear : tFar;
            return outDistance >= 0.0f;
        }

        glm::vec2 pixelToNdc(float x, float y, float width, float height) {
            const float ndcX = (x / width) * 2.0f - 1.0f;
            const float ndcY = 1.0f - (y / height) * 2.0f;
            return glm::vec2(ndcX, ndcY);
        }

        void pushOverlayQuad(std::vector<UiColorVertex>& out,
                             float x0,
                             float y0,
                             float x1,
                             float y1,
                             const glm::vec3& color,
                             float width,
                             float height) {
            const glm::vec2 a = pixelToNdc(x0, y0, width, height);
            const glm::vec2 b = pixelToNdc(x1, y0, width, height);
            const glm::vec2 c = pixelToNdc(x1, y1, width, height);
            const glm::vec2 d = pixelToNdc(x0, y1, width, height);
            out.push_back({a, color});
            out.push_back({b, color});
            out.push_back({c, color});
            out.push_back({a, color});
            out.push_back({c, color});
            out.push_back({d, color});
        }

        void pushTri(std::vector<GemVertex>& out,
                     const glm::vec3& a,
                     const glm::vec3& b,
                     const glm::vec3& c,
                     const glm::vec3& color) {
            out.push_back({a, color});
            out.push_back({b, color});
            out.push_back({c, color});
        }

        void pushLine(std::vector<GemVertex>& out,
                      const glm::vec3& a,
                      const glm::vec3& b,
                      const glm::vec3& color) {
            out.push_back({a, color});
            out.push_back({b, color});
        }

        void pushCubeOutline(std::vector<GemVertex>& lineVerts,
                             const glm::vec3& center,
                             float half,
                             const glm::vec3& color) {
            const glm::vec3 p000 = center + glm::vec3(-half, -half, -half);
            const glm::vec3 p100 = center + glm::vec3( half, -half, -half);
            const glm::vec3 p010 = center + glm::vec3(-half,  half, -half);
            const glm::vec3 p110 = center + glm::vec3( half,  half, -half);
            const glm::vec3 p001 = center + glm::vec3(-half, -half,  half);
            const glm::vec3 p101 = center + glm::vec3( half, -half,  half);
            const glm::vec3 p011 = center + glm::vec3(-half,  half,  half);
            const glm::vec3 p111 = center + glm::vec3( half,  half,  half);

            pushLine(lineVerts, p000, p100, color);
            pushLine(lineVerts, p100, p110, color);
            pushLine(lineVerts, p110, p010, color);
            pushLine(lineVerts, p010, p000, color);

            pushLine(lineVerts, p001, p101, color);
            pushLine(lineVerts, p101, p111, color);
            pushLine(lineVerts, p111, p011, color);
            pushLine(lineVerts, p011, p001, color);

            pushLine(lineVerts, p000, p001, color);
            pushLine(lineVerts, p100, p101, color);
            pushLine(lineVerts, p110, p111, color);
            pushLine(lineVerts, p010, p011, color);
        }

        void ensureRenderResources(RendererContext& renderer, WorldContext& world, GemChiselState& s) {
            if (!renderer.uiColorShader) {
                renderer.uiColorShader = std::make_unique<Shader>(
                    world.shaders["UI_COLOR_VERTEX_SHADER"].c_str(),
                    world.shaders["UI_COLOR_FRAGMENT_SHADER"].c_str());
            }
            if (!renderer.audioRayShader
                && world.shaders.count("AUDIORAY_VERTEX_SHADER")
                && world.shaders.count("AUDIORAY_FRAGMENT_SHADER")) {
                renderer.audioRayShader = std::make_unique<Shader>(
                    world.shaders["AUDIORAY_VERTEX_SHADER"].c_str(),
                    world.shaders["AUDIORAY_FRAGMENT_SHADER"].c_str());
            }

            if (s.overlayVao == 0) {
                glGenVertexArrays(1, &s.overlayVao);
            }
            if (s.overlayVbo == 0) {
                glGenBuffers(1, &s.overlayVbo);
            }
            if (s.meshVao == 0) {
                glGenVertexArrays(1, &s.meshVao);
            }
            if (s.meshVbo == 0) {
                glGenBuffers(1, &s.meshVbo);
            }

            glBindVertexArray(s.overlayVao);
            glBindBuffer(GL_ARRAY_BUFFER, s.overlayVbo);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiColorVertex), (void*)offsetof(UiColorVertex, pos));
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(UiColorVertex), (void*)offsetof(UiColorVertex, color));
            glEnableVertexAttribArray(1);

            glBindVertexArray(s.meshVao);
            glBindBuffer(GL_ARRAY_BUFFER, s.meshVbo);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GemVertex), (void*)offsetof(GemVertex, pos));
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GemVertex), (void*)offsetof(GemVertex, color));
            glEnableVertexAttribArray(1);
        }

        GemRenderSetup buildRenderSetup(const GemDropState& drop,
                                        const GemChiselState& s,
                                        GLFWwindow* win) {
            GemRenderSetup setup;

            int ww = 0;
            int wh = 0;
            if (win) glfwGetWindowSize(win, &ww, &wh);
            if (ww <= 0) ww = 1920;
            if (wh <= 0) wh = 1080;
            setup.windowW = static_cast<float>(ww);
            setup.windowH = static_cast<float>(wh);

            glm::ivec3 minCell(std::numeric_limits<int>::max());
            glm::ivec3 maxCell(std::numeric_limits<int>::min());
            for (const glm::ivec3& cell : drop.voxelCells) {
                minCell = glm::min(minCell, cell);
                maxCell = glm::max(maxCell, cell);
            }
            setup.minCell = minCell;
            setup.maxCell = maxCell;
            setup.modelCenter = (glm::vec3(minCell) + glm::vec3(maxCell)) * 0.5f;

            const glm::vec3 span = glm::vec3(maxCell - minCell + glm::ivec3(1));
            const float maxSpan = std::max(1.0f, std::max(span.x, std::max(span.y, span.z)));
            setup.voxelSize = glm::clamp(2.4f / maxSpan, 0.08f, 0.32f);

            const glm::vec3 spanScaled = span * setup.voxelSize;
            const float modelRadius = std::max(0.8f, 0.5f * glm::length(spanScaled));
            setup.cameraDistance = modelRadius * 2.6f + 1.1f;

            const float yawRad = glm::radians(s.yaw);
            const float pitchRad = glm::radians(s.pitch);
            glm::vec3 dir(0.0f);
            dir.x = std::cos(pitchRad) * std::cos(yawRad);
            dir.y = std::sin(pitchRad);
            dir.z = std::cos(pitchRad) * std::sin(yawRad);
            if (glm::length(dir) < 0.001f) dir = glm::vec3(0.0f, 0.0f, 1.0f);
            dir = glm::normalize(dir);

            const glm::vec3 target(0.0f);
            setup.cameraPos = target + dir * setup.cameraDistance;
            setup.view = glm::lookAt(setup.cameraPos, target, glm::vec3(0.0f, 1.0f, 0.0f));
            const float aspect = std::max(0.1f, setup.windowW / std::max(1.0f, setup.windowH));
            setup.proj = glm::perspective(glm::radians(44.0f), aspect, 0.05f, setup.cameraDistance + modelRadius * 5.0f + 10.0f);

            return setup;
        }

        bool screenRayFromMouse(const GemRenderSetup& setup,
                                GLFWwindow* win,
                                glm::vec3& outOrigin,
                                glm::vec3& outDir) {
            if (!win) return false;
            double mx = 0.0;
            double my = 0.0;
            glfwGetCursorPos(win, &mx, &my);
            if (mx < 0.0 || my < 0.0 || mx >= setup.windowW || my >= setup.windowH) return false;

            const float ndcX = static_cast<float>((mx / static_cast<double>(setup.windowW)) * 2.0 - 1.0);
            const float ndcY = static_cast<float>(1.0 - (my / static_cast<double>(setup.windowH)) * 2.0);

            const glm::mat4 invVP = glm::inverse(setup.proj * setup.view);
            glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
            glm::vec4 farH = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
            if (std::abs(nearH.w) < 1e-6f || std::abs(farH.w) < 1e-6f) return false;

            const glm::vec3 nearP = glm::vec3(nearH) / nearH.w;
            const glm::vec3 farP = glm::vec3(farH) / farH.w;
            glm::vec3 dir = farP - nearP;
            if (glm::length(dir) < 1e-6f) return false;
            dir = glm::normalize(dir);

            outOrigin = nearP;
            outDir = dir;
            return true;
        }

        int pickVoxelIndex(const GemDropState& drop,
                           const GemRenderSetup& setup,
                           GLFWwindow* win) {
            glm::vec3 rayOrigin(0.0f);
            glm::vec3 rayDir(0.0f);
            if (!screenRayFromMouse(setup, win, rayOrigin, rayDir)) return -1;

            const float half = setup.voxelSize * 0.5f;
            float bestDistance = std::numeric_limits<float>::max();
            int bestIndex = -1;
            for (int i = 0; i < static_cast<int>(drop.voxelCells.size()); ++i) {
                const glm::vec3 localCenter = (glm::vec3(drop.voxelCells[static_cast<size_t>(i)]) - setup.modelCenter) * setup.voxelSize;
                const glm::vec3 minB = localCenter - glm::vec3(half);
                const glm::vec3 maxB = localCenter + glm::vec3(half);
                float hitDistance = 0.0f;
                if (!rayIntersectsAabb(rayOrigin, rayDir, minB, maxB, hitDistance)) continue;
                if (hitDistance < bestDistance) {
                    bestDistance = hitDistance;
                    bestIndex = i;
                }
            }
            return bestIndex;
        }

        bool removeVoxelAtIndex(BaseSystem& baseSystem, GemDropState& drop, int voxelIndex) {
            if (voxelIndex < 0 || voxelIndex >= static_cast<int>(drop.voxelCells.size())) return false;
            if (drop.voxelCells.size() <= 1u) return false;
            drop.voxelCells[static_cast<size_t>(voxelIndex)] = drop.voxelCells.back();
            drop.voxelCells.pop_back();
            drop.velocity = glm::vec3(0.0f);
            drop.age = 0.0f;
            AudioSystemLogic::TriggerGameplaySfx(baseSystem, "break_stone.ck", 0.9f);
            return true;
        }

        void buildGemMeshVertices(const GemDropState& drop,
                                  const GemRenderSetup& setup,
                                  int hoveredVoxelIndex,
                                  std::vector<GemVertex>& fillVerts,
                                  std::vector<GemVertex>& lineVerts) {
            if (drop.voxelCells.empty()) return;

            glm::vec3 c0(1.0f);
            glm::vec3 c1(1.0f);
            orePalette(drop.kind, c0, c1);

            std::unordered_set<int64_t> occupied;
            occupied.reserve(drop.voxelCells.size() * 2u + 1u);
            for (const glm::ivec3& cell : drop.voxelCells) {
                occupied.insert(packVoxelCoord(cell.x, cell.y, cell.z));
            }

            const std::array<FaceDesc, 6> faces = {{
                {glm::ivec3( 1, 0, 0), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)},
                {glm::ivec3(-1, 0, 0), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f,-1.0f)},
                {glm::ivec3( 0, 1, 0), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f,-1.0f)},
                {glm::ivec3( 0,-1, 0), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)},
                {glm::ivec3( 0, 0, 1), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)},
                {glm::ivec3( 0, 0,-1), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(-1.0f,0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)}
            }};

            const float half = setup.voxelSize * 0.5f;
            const glm::vec3 lightDir = glm::normalize(glm::vec3(0.45f, 0.87f, 0.22f));
            const float spanY = static_cast<float>(std::max(1, setup.maxCell.y - setup.minCell.y));

            fillVerts.reserve(drop.voxelCells.size() * 36u);
            lineVerts.reserve(drop.voxelCells.size() * 24u);

            for (size_t i = 0; i < drop.voxelCells.size(); ++i) {
                const glm::ivec3 cell = drop.voxelCells[i];
                const glm::vec3 center = (glm::vec3(cell) - setup.modelCenter) * setup.voxelSize;
                const float t = glm::clamp(static_cast<float>(cell.y - setup.minCell.y) / spanY, 0.0f, 1.0f);
                const glm::vec3 baseColor = glm::mix(c0, c1, t);

                for (const FaceDesc& face : faces) {
                    const glm::ivec3 neighbor = cell + face.neighbor;
                    if (occupied.find(packVoxelCoord(neighbor.x, neighbor.y, neighbor.z)) != occupied.end()) continue;

                    const glm::vec3 n = face.normal * half;
                    const glm::vec3 u = face.axisU * half;
                    const glm::vec3 v = face.axisV * half;

                    const glm::vec3 p0 = center + n - u - v;
                    const glm::vec3 p1 = center + n + u - v;
                    const glm::vec3 p2 = center + n + u + v;
                    const glm::vec3 p3 = center + n - u + v;

                    const float ndl = std::max(0.0f, glm::dot(face.normal, lightDir));
                    const float shade = 0.38f + 0.62f * ndl;
                    const glm::vec3 faceColor = glm::clamp(baseColor * shade, glm::vec3(0.0f), glm::vec3(1.0f));
                    pushTri(fillVerts, p0, p1, p2, faceColor);
                    pushTri(fillVerts, p0, p2, p3, faceColor);

                    const glm::vec3 edgeColor = faceColor * 0.22f;
                    pushLine(lineVerts, p0, p1, edgeColor);
                    pushLine(lineVerts, p1, p2, edgeColor);
                    pushLine(lineVerts, p2, p3, edgeColor);
                    pushLine(lineVerts, p3, p0, edgeColor);
                }

                if (hoveredVoxelIndex >= 0 && hoveredVoxelIndex == static_cast<int>(i)) {
                    pushCubeOutline(lineVerts, center, half + 0.005f, glm::vec3(0.98f, 0.97f, 0.92f));
                }
            }
        }

        void updateCursorCapture(BaseSystem& baseSystem, GLFWwindow* win) {
            GemChiselState& s = state();
            if (!win) return;
            if (s.active && !s.cursorCaptured) {
                glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                s.cursorCaptured = true;
                if (baseSystem.player) baseSystem.player->firstMouse = true;
            } else if (!s.active && s.cursorCaptured && (!baseSystem.ui || !baseSystem.ui->active)) {
                glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                s.cursorCaptured = false;
                if (baseSystem.player) baseSystem.player->firstMouse = true;
            }
        }

        void closeSession() {
            GemChiselState& s = state();
            s.active = false;
            s.awaitingInitialRelease = false;
            s.rightDragging = false;
            s.lastLeftDown = false;
            s.lastRightDown = false;
            s.lastEscapeDown = false;
            s.hoveredVoxelIndex = -1;
            s.dropIndex = -1;
        }
    } // namespace

    bool IsChiselActive(const BaseSystem& baseSystem) {
        if (!readRegistryBool(baseSystem, "GemChiselSystem", true)) return false;
        if (!readRegistryBool(baseSystem, "GemChiselEnabled", true)) return false;
        return state().active;
    }

    bool StartGemChiselAtCell(BaseSystem& baseSystem, const glm::ivec3& cell, int worldIndex) {
        (void)worldIndex;
        if (!readRegistryBool(baseSystem, "GemChiselSystem", true)) return false;
        if (!readRegistryBool(baseSystem, "GemChiselEnabled", true)) return false;
        if (!baseSystem.gems) return false;
        if (baseSystem.ui && baseSystem.ui->active) return false;
        if (OreMiningSystemLogic::IsMiningActive(baseSystem)) return false;
        if (GroundCraftingSystemLogic::IsRitualActive(baseSystem)) return false;

        GemChiselState& s = state();
        if (s.active) return false;

        const int dropIndex = findDropIndexAtCell(baseSystem, cell);
        if (dropIndex < 0) return false;

        s.active = true;
        s.awaitingInitialRelease = true;
        s.rightDragging = false;
        s.lastLeftDown = false;
        s.lastRightDown = false;
        s.lastEscapeDown = false;
        s.hoveredVoxelIndex = -1;
        s.dropIndex = dropIndex;
        s.targetCell = cell;
        s.yaw = 0.0f;
        s.pitch = 20.0f;
        return true;
    }

    void UpdateGemChisel(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes;
        (void)dt;

        GemChiselState& s = state();
        if (!readRegistryBool(baseSystem, "GemChiselSystem", true)
            || !readRegistryBool(baseSystem, "GemChiselEnabled", true)) {
            if (s.active) {
                closeSession();
            }
            updateCursorCapture(baseSystem, win);
            return;
        }

        updateCursorCapture(baseSystem, win);
        if (!s.active || !win || !baseSystem.gems) return;

        const int resolvedIndex = resolveDropIndex(baseSystem, s);
        if (resolvedIndex < 0 || resolvedIndex >= static_cast<int>(baseSystem.gems->drops.size())) {
            closeSession();
            updateCursorCapture(baseSystem, win);
            return;
        }

        s.dropIndex = resolvedIndex;
        GemDropState& drop = baseSystem.gems->drops[static_cast<size_t>(s.dropIndex)];
        drop.position = glm::vec3(s.targetCell);
        drop.velocity = glm::vec3(0.0f);
        drop.age = 0.0f;

        const bool leftDown = (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
        const bool rightDown = (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
        const bool leftPressed = (!s.lastLeftDown && leftDown);
        const bool rightPressed = (!s.lastRightDown && rightDown);
        s.lastLeftDown = leftDown;
        s.lastRightDown = rightDown;

        const bool escapeDown = (glfwGetKey(win, GLFW_KEY_ENTER) == GLFW_PRESS)
            || (glfwGetKey(win, GLFW_KEY_KP_ENTER) == GLFW_PRESS);
        const bool escapePressed = (!s.lastEscapeDown && escapeDown);
        s.lastEscapeDown = escapeDown;
        if (escapePressed) {
            closeSession();
            updateCursorCapture(baseSystem, win);
            return;
        }

        double mx = 0.0;
        double my = 0.0;
        glfwGetCursorPos(win, &mx, &my);
        if (rightPressed) {
            s.rightDragging = true;
            s.lastMouseX = mx;
            s.lastMouseY = my;
        }

        if (!rightDown) {
            s.rightDragging = false;
        } else if (s.rightDragging) {
            const double dx = mx - s.lastMouseX;
            const double dy = my - s.lastMouseY;
            s.lastMouseX = mx;
            s.lastMouseY = my;
            const float sensitivity = glm::clamp(
                readRegistryFloat(baseSystem, "GemChiselRotateSensitivity", 0.010f),
                0.001f,
                0.08f);
            s.yaw += static_cast<float>(dx) * sensitivity * 60.0f;
            s.pitch = glm::clamp(s.pitch + static_cast<float>(dy) * sensitivity * 60.0f, -80.0f, 80.0f);
        }

        if (s.awaitingInitialRelease) {
            if (!leftDown && !rightDown) {
                s.awaitingInitialRelease = false;
            }
            return;
        }

        GemRenderSetup setup = buildRenderSetup(drop, s, win);
        s.hoveredVoxelIndex = pickVoxelIndex(drop, setup, win);

        if (!leftPressed || rightDown) return;
        if (removeVoxelAtIndex(baseSystem, drop, s.hoveredVoxelIndex)) {
            s.hoveredVoxelIndex = -1;
        }
    }

    void RenderGemChisel(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes;
        (void)dt;

        GemChiselState& s = state();
        if (!s.active || !win || !baseSystem.renderer || !baseSystem.world || !baseSystem.gems) return;

        const int resolvedIndex = resolveDropIndex(baseSystem, s);
        if (resolvedIndex < 0 || resolvedIndex >= static_cast<int>(baseSystem.gems->drops.size())) return;
        s.dropIndex = resolvedIndex;
        GemDropState& drop = baseSystem.gems->drops[static_cast<size_t>(s.dropIndex)];
        drop.position = glm::vec3(s.targetCell);
        drop.velocity = glm::vec3(0.0f);
        drop.age = 0.0f;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        ensureRenderResources(renderer, world, s);
        if (!renderer.uiColorShader || !renderer.audioRayShader || s.overlayVao == 0 || s.meshVao == 0) return;

        GemRenderSetup setup = buildRenderSetup(drop, s, win);

        std::vector<UiColorVertex> overlayVerts;
        overlayVerts.reserve(12u);
        pushOverlayQuad(overlayVerts,
                        0.0f,
                        0.0f,
                        setup.windowW,
                        setup.windowH,
                        glm::vec3(0.0f, 0.0f, 0.0f),
                        setup.windowW,
                        setup.windowH);

        // Subtle center pane to improve readability without forcing a fixed grid UI.
        const float paneW = std::floor(setup.windowW * 0.72f);
        const float paneH = std::floor(setup.windowH * 0.72f);
        const float paneL = std::floor((setup.windowW - paneW) * 0.5f);
        const float paneT = std::floor((setup.windowH - paneH) * 0.5f);
        pushOverlayQuad(overlayVerts,
                        paneL,
                        paneT,
                        paneL + paneW,
                        paneT + paneH,
                        glm::vec3(0.05f, 0.05f, 0.06f),
                        setup.windowW,
                        setup.windowH);

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBindVertexArray(s.overlayVao);
        glBindBuffer(GL_ARRAY_BUFFER, s.overlayVbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(overlayVerts.size() * sizeof(UiColorVertex)),
                     overlayVerts.data(),
                     GL_DYNAMIC_DRAW);

        renderer.uiColorShader->use();
        glBlendColor(0.0f, 0.0f, 0.0f, 0.58f);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(overlayVerts.size()));

        std::vector<GemVertex> fillVerts;
        std::vector<GemVertex> lineVerts;
        buildGemMeshVertices(drop, setup, s.hoveredVoxelIndex, fillVerts, lineVerts);
        if (fillVerts.empty() && lineVerts.empty()) {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            return;
        }

        const int fillCount = static_cast<int>(fillVerts.size());
        const int lineCount = static_cast<int>(lineVerts.size());
        std::vector<GemVertex> upload;
        upload.reserve(fillVerts.size() + lineVerts.size());
        upload.insert(upload.end(), fillVerts.begin(), fillVerts.end());
        upload.insert(upload.end(), lineVerts.begin(), lineVerts.end());

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        renderer.audioRayShader->use();
        renderer.audioRayShader->setMat4("view", setup.view);
        renderer.audioRayShader->setMat4("projection", setup.proj);

        glBindVertexArray(s.meshVao);
        glBindBuffer(GL_ARRAY_BUFFER, s.meshVbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(upload.size() * sizeof(GemVertex)),
                     upload.data(),
                     GL_DYNAMIC_DRAW);

        if (fillCount > 0) {
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(fillCount));
        }
        if (lineCount > 0) {
            glEnable(GL_BLEND);
            glLineWidth(1.0f);
            glDrawArrays(GL_LINES, fillCount, static_cast<GLsizei>(lineCount));
        }
    }
}
