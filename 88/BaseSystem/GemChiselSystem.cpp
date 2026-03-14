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
#include <vector>

namespace AudioSystemLogic { bool TriggerGameplaySfx(BaseSystem& baseSystem, const std::string& cueName, float gain); }
namespace OreMiningSystemLogic { bool IsMiningActive(const BaseSystem& baseSystem); }
namespace GroundCraftingSystemLogic { bool IsRitualActive(const BaseSystem& baseSystem); }

namespace GemChiselSystemLogic {
    namespace {
        constexpr float kMiniVoxelSize = 1.0f / 24.0f;

        struct ColorVertex {
            glm::vec3 pos;
            glm::vec3 color;
        };

        struct GemChiselState {
            GLuint vao = 0;
            GLuint vbo = 0;
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

        glm::vec3 cameraForward(const PlayerContext& player) {
            glm::vec3 forward;
            forward.x = std::cos(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
            forward.y = std::sin(glm::radians(player.cameraPitch));
            forward.z = std::sin(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
            if (glm::length(forward) < 0.0001f) return glm::vec3(0.0f, 0.0f, -1.0f);
            return glm::normalize(forward);
        }

        glm::vec3 cameraEyePosition(const BaseSystem& baseSystem, const PlayerContext& player) {
            if (baseSystem.gamemode == "survival") {
                return player.cameraPosition + glm::vec3(0.0f, 0.6f, 0.0f);
            }
            return player.cameraPosition;
        }

        glm::vec3 effectiveDropBasePosition(const GemDropState& drop) {
            if (drop.lockToCell) {
                return glm::vec3(
                    static_cast<float>(drop.lockedCell.x) + drop.lockedOffsetXZ.x,
                    static_cast<float>(drop.lockedCell.y),
                    static_cast<float>(drop.lockedCell.z) + drop.lockedOffsetXZ.y
                );
            }
            return drop.position;
        }

        glm::vec3 applyUpsideDown(const GemDropState& drop, const glm::vec3& v) {
            if (!drop.upsideDown) return v;
            return glm::vec3(v.x, -v.y, -v.z);
        }

        void computeVoxelScaleAndYOffset(const BaseSystem& baseSystem,
                                         const GemDropState& drop,
                                         float& outVoxelScale,
                                         float& outHalf,
                                         float& outAlignYOffset) {
            const float renderScale = glm::clamp(readRegistryFloat(baseSystem, "GemDropVisualScale", 1.0f), 0.1f, 100.0f);
            outVoxelScale = std::max(0.05f, renderScale);
            outHalf = (kMiniVoxelSize * 0.5f) * outVoxelScale;

            float minFaceY = std::numeric_limits<float>::max();
            for (const glm::ivec3& cell : drop.voxelCells) {
                const glm::vec3 flippedCenter = applyUpsideDown(drop, glm::vec3(cell) * (kMiniVoxelSize * outVoxelScale));
                const float centerY = flippedCenter.y;
                minFaceY = std::min(minFaceY, centerY - outHalf);
            }
            outAlignYOffset = (minFaceY < std::numeric_limits<float>::max() * 0.5f)
                ? ((-0.5f + drop.renderYOffset) - minFaceY)
                : 0.0f;
        }

        glm::vec3 voxelCenterWorld(const BaseSystem& baseSystem,
                                   const GemDropState& drop,
                                   const glm::ivec3& voxelCell) {
            float voxelScale = 1.0f;
            float half = 0.0f;
            float alignYOffset = 0.0f;
            computeVoxelScaleAndYOffset(baseSystem, drop, voxelScale, half, alignYOffset);
            (void)half;
            const glm::vec3 localCenter = applyUpsideDown(drop, glm::vec3(voxelCell) * (kMiniVoxelSize * voxelScale))
                + glm::vec3(0.0f, alignYOffset, 0.0f);
            return effectiveDropBasePosition(drop) + localCenter;
        }

        int pickVoxelIndexFromPlayerRay(const BaseSystem& baseSystem,
                                        const GemDropState& drop,
                                        float* outDistance) {
            if (!baseSystem.player || drop.voxelCells.empty()) return -1;

            const PlayerContext& player = *baseSystem.player;
            const glm::vec3 rayDir = cameraForward(player);
            const float rayStartOffset = glm::clamp(readRegistryFloat(baseSystem, "GemChiselRayStartOffset", 0.0f), 0.0f, 1.0f);
            const float minHitDistance = glm::clamp(readRegistryFloat(baseSystem, "GemChiselMinHitDistance", 0.04f), 0.0f, 0.5f);
            const glm::vec3 rayOrigin = cameraEyePosition(baseSystem, player) + rayDir * rayStartOffset;

            float voxelScale = 1.0f;
            float half = 0.0f;
            float alignYOffset = 0.0f;
            computeVoxelScaleAndYOffset(baseSystem, drop, voxelScale, half, alignYOffset);
            const float maxRange = glm::clamp(readRegistryFloat(baseSystem, "GemChiselRange", 3.0f), 0.5f, 12.0f);
            const float forgivingRadius = glm::clamp(readRegistryFloat(baseSystem, "GemChiselPickRadius", 0.22f), 0.02f, 1.0f);

            const glm::vec3 dropBase = effectiveDropBasePosition(drop);

            float bestScore = std::numeric_limits<float>::max();
            float bestAlong = std::numeric_limits<float>::max();
            int bestIndex = -1;
            for (int i = 0; i < static_cast<int>(drop.voxelCells.size()); ++i) {
                const glm::vec3 localCenter = applyUpsideDown(
                    drop,
                    glm::vec3(drop.voxelCells[static_cast<size_t>(i)]) * (kMiniVoxelSize * voxelScale))
                    + glm::vec3(0.0f, alignYOffset, 0.0f);
                const glm::vec3 center = dropBase + localCenter;
                float hitDistance = 0.0f;
                if (!rayIntersectsAabb(rayOrigin,
                                       rayDir,
                                       center - glm::vec3(half),
                                       center + glm::vec3(half),
                                       hitDistance)) {
                    continue;
                }
                if (hitDistance > maxRange) continue;

                const glm::vec3 toCenter = center - rayOrigin;
                const float along = glm::dot(toCenter, rayDir);
                if (along < -half || along > (maxRange + half)) continue;
                const glm::vec3 closestPoint = rayOrigin + rayDir * along;
                const float perpendicular = glm::length(center - closestPoint);
                const float denom = std::max(half, 1e-4f);
                float score = perpendicular / denom;
                score += glm::clamp(along / std::max(maxRange, 1e-4f), 0.0f, 2.0f) * 0.08f;
                if (hitDistance < minHitDistance) {
                    // Keep near-origin candidates instead of dropping them outright;
                    // this avoids snapping to low voxels when the camera is close.
                    score += (minHitDistance - hitDistance) * 0.35f;
                }
                if (rayDir.y > -0.15f) {
                    const float below = rayOrigin.y - center.y;
                    if (below > 0.0f) {
                        score += glm::clamp(below / std::max(kMiniVoxelSize * voxelScale, 1e-4f), 0.0f, 48.0f) * 0.02f;
                    }
                }

                if ((score < bestScore) || (std::abs(score - bestScore) < 1e-4f && along < bestAlong)) {
                    bestScore = score;
                    bestAlong = along;
                    bestIndex = i;
                }
            }

            if (bestIndex >= 0) {
                if (outDistance) *outDistance = std::max(0.0f, bestAlong);
                return bestIndex;
            }

            // Forgiving fallback: intersect against slightly-expanded voxel AABBs.
            // This tracks crosshair intent better than center-distance heuristics,
            // especially when the player is offset below/above the gem.
            float bestFallbackScore = std::numeric_limits<float>::max();
            float bestFallbackAlong = std::numeric_limits<float>::max();
            int bestFallbackIndex = -1;
            const float expandedHalf = half + forgivingRadius;
            for (int i = 0; i < static_cast<int>(drop.voxelCells.size()); ++i) {
                const glm::vec3 localCenter = applyUpsideDown(
                    drop,
                    glm::vec3(drop.voxelCells[static_cast<size_t>(i)]) * (kMiniVoxelSize * voxelScale))
                    + glm::vec3(0.0f, alignYOffset, 0.0f);
                const glm::vec3 center = dropBase + localCenter;
                float hitDistance = 0.0f;
                if (!rayIntersectsAabb(rayOrigin,
                                       rayDir,
                                       center - glm::vec3(expandedHalf),
                                       center + glm::vec3(expandedHalf),
                                       hitDistance)) {
                    continue;
                }
                if (hitDistance > maxRange) continue;

                const glm::vec3 toCenter = center - rayOrigin;
                const float along = glm::dot(toCenter, rayDir);
                if (along < -expandedHalf || along > (maxRange + expandedHalf)) continue;
                const glm::vec3 closestPoint = rayOrigin + rayDir * along;
                const float perpendicular = glm::length(center - closestPoint);
                float score = perpendicular / std::max(expandedHalf, 1e-4f);
                score += glm::clamp(along / std::max(maxRange, 1e-4f), 0.0f, 2.0f) * 0.10f;
                if (hitDistance < minHitDistance) {
                    score += (minHitDistance - hitDistance) * 0.45f;
                }
                if (rayDir.y > -0.15f) {
                    const float below = rayOrigin.y - center.y;
                    if (below > 0.0f) {
                        score += glm::clamp(below / std::max(kMiniVoxelSize * voxelScale, 1e-4f), 0.0f, 48.0f) * 0.03f;
                    }
                }

                if ((score < bestFallbackScore)
                    || (std::abs(score - bestFallbackScore) < 1e-4f && along < bestFallbackAlong)) {
                    bestFallbackScore = score;
                    bestFallbackAlong = along;
                    bestFallbackIndex = i;
                }
            }

            if (outDistance) {
                *outDistance = (bestFallbackIndex >= 0)
                    ? std::max(0.0f, bestFallbackAlong)
                    : std::numeric_limits<float>::max();
            }
            return bestFallbackIndex;
        }

        bool resolveRayChiselTarget(const BaseSystem& baseSystem,
                                    int* outDropIndex,
                                    int* outVoxelIndex,
                                    float* outDistance) {
            if (outDropIndex) *outDropIndex = -1;
            if (outVoxelIndex) *outVoxelIndex = -1;
            if (outDistance) *outDistance = std::numeric_limits<float>::max();
            if (!baseSystem.gems) return false;

            const GemContext& gems = *baseSystem.gems;
            if (gems.drops.empty()) return false;

            int bestDropIndex = -1;
            int bestVoxelIndex = -1;
            float bestDistance = std::numeric_limits<float>::max();
            for (int i = 0; i < static_cast<int>(gems.drops.size()); ++i) {
                const GemDropState& drop = gems.drops[static_cast<size_t>(i)];
                float hitDistance = std::numeric_limits<float>::max();
                const int voxelIndex = pickVoxelIndexFromPlayerRay(baseSystem, drop, &hitDistance);
                if (voxelIndex < 0) continue;
                if (!std::isfinite(hitDistance)) continue;
                if (hitDistance < bestDistance) {
                    bestDistance = hitDistance;
                    bestDropIndex = i;
                    bestVoxelIndex = voxelIndex;
                }
            }

            if (bestDropIndex < 0 || bestVoxelIndex < 0) return false;
            if (outDropIndex) *outDropIndex = bestDropIndex;
            if (outVoxelIndex) *outVoxelIndex = bestVoxelIndex;
            if (outDistance) *outDistance = bestDistance;
            return true;
        }

        void ensureRenderResources(RendererContext& renderer, WorldContext& world, GemChiselState& s) {
            if (!renderer.audioRayShader
                && world.shaders.count("AUDIORAY_VERTEX_SHADER")
                && world.shaders.count("AUDIORAY_FRAGMENT_SHADER")) {
                renderer.audioRayShader = std::make_unique<Shader>(
                    world.shaders["AUDIORAY_VERTEX_SHADER"].c_str(),
                    world.shaders["AUDIORAY_FRAGMENT_SHADER"].c_str());
            }

            if (s.vao == 0) glGenVertexArrays(1, &s.vao);
            if (s.vbo == 0) glGenBuffers(1, &s.vbo);

            glBindVertexArray(s.vao);
            glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ColorVertex), (void*)offsetof(ColorVertex, pos));
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ColorVertex), (void*)offsetof(ColorVertex, color));
            glEnableVertexAttribArray(1);
        }

        void pushTri(std::vector<ColorVertex>& out,
                     const glm::vec3& a,
                     const glm::vec3& b,
                     const glm::vec3& c,
                     const glm::vec3& color) {
            out.push_back({a, color});
            out.push_back({b, color});
            out.push_back({c, color});
        }

        void pushVoxelShadowCube(std::vector<ColorVertex>& out,
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

            pushTri(out, p000, p100, p110, color);
            pushTri(out, p000, p110, p010, color);

            pushTri(out, p001, p101, p111, color);
            pushTri(out, p001, p111, p011, color);

            pushTri(out, p000, p001, p101, color);
            pushTri(out, p000, p101, p100, color);

            pushTri(out, p010, p011, p111, color);
            pushTri(out, p010, p111, p110, color);

            pushTri(out, p000, p001, p011, color);
            pushTri(out, p000, p011, p010, color);

            pushTri(out, p100, p101, p111, color);
            pushTri(out, p100, p111, p110, color);
        }
    } // namespace

    bool IsChiselActive(const BaseSystem& baseSystem) {
        (void)baseSystem;
        // Chiseling is now in-world and non-modal.
        return false;
    }

    bool StartGemChiselAtCell(BaseSystem& baseSystem, const glm::ivec3& cell, int worldIndex) {
        (void)cell;
        (void)worldIndex;
        if (!readRegistryBool(baseSystem, "GemChiselSystem", true)) return false;
        if (!readRegistryBool(baseSystem, "GemChiselEnabled", true)) return false;
        if (!baseSystem.gems) return false;
        if (baseSystem.ui && baseSystem.ui->active) return false;
        if (OreMiningSystemLogic::IsMiningActive(baseSystem)) return false;
        if (GroundCraftingSystemLogic::IsRitualActive(baseSystem)) return false;

        int dropIndex = -1;
        int voxelIndex = -1;
        if (!resolveRayChiselTarget(baseSystem, &dropIndex, &voxelIndex, nullptr)) return false;
        if (dropIndex < 0 || dropIndex >= static_cast<int>(baseSystem.gems->drops.size())) return false;

        GemDropState& drop = baseSystem.gems->drops[static_cast<size_t>(dropIndex)];
        if (drop.voxelCells.size() <= 1u) {
            return true;
        }

        drop.voxelCells[static_cast<size_t>(voxelIndex)] = drop.voxelCells.back();
        drop.voxelCells.pop_back();
        drop.velocity = glm::vec3(0.0f);
        drop.age = 0.0f;
        AudioSystemLogic::TriggerGameplaySfx(baseSystem, "break_stone.ck", 0.9f);
        return true;
    }

    void UpdateGemChisel(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)baseSystem;
        (void)prototypes;
        (void)dt;
        (void)win;
        // No modal update loop anymore; chiseling happens directly from block-charge interactions.
    }

    void RenderGemChisel(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes;
        (void)dt;
        (void)win;

        if (!readRegistryBool(baseSystem, "GemChiselSystem", true)
            || !readRegistryBool(baseSystem, "GemChiselEnabled", true)) {
            return;
        }
        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.player || !baseSystem.gems) return;

        PlayerContext& player = *baseSystem.player;

        int dropIndex = -1;
        int hoveredVoxel = -1;
        float hitDistance = std::numeric_limits<float>::max();
        if (!resolveRayChiselTarget(baseSystem, &dropIndex, &hoveredVoxel, &hitDistance)) return;
        if (dropIndex < 0 || dropIndex >= static_cast<int>(baseSystem.gems->drops.size())) return;
        if (hoveredVoxel < 0 || hoveredVoxel >= static_cast<int>(baseSystem.gems->drops[static_cast<size_t>(dropIndex)].voxelCells.size())) return;
        if (!std::isfinite(hitDistance)) return;

        GemDropState& drop = baseSystem.gems->drops[static_cast<size_t>(dropIndex)];

        float voxelScale = 1.0f;
        float half = 0.0f;
        float alignYOffset = 0.0f;
        computeVoxelScaleAndYOffset(baseSystem, drop, voxelScale, half, alignYOffset);
        (void)alignYOffset;

        const glm::vec3 center = voxelCenterWorld(baseSystem, drop, drop.voxelCells[static_cast<size_t>(hoveredVoxel)]);

        std::vector<ColorVertex> fillVerts;
        fillVerts.reserve(36u);
        const float shadowHalf = half + 0.0015f;
        pushVoxelShadowCube(fillVerts, center, shadowHalf, glm::vec3(0.06f, 0.06f, 0.06f));
        if (fillVerts.empty()) return;

        GemChiselState& s = state();
        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        ensureRenderResources(renderer, world, s);
        if (!renderer.audioRayShader || s.vao == 0 || s.vbo == 0) return;

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        renderer.audioRayShader->use();
        renderer.audioRayShader->setMat4("view", player.viewMatrix);
        renderer.audioRayShader->setMat4("projection", player.projectionMatrix);

        glBindVertexArray(s.vao);
        glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(fillVerts.size() * sizeof(ColorVertex)),
                     fillVerts.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(fillVerts.size()));
    }
}
