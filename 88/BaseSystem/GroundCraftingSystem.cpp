#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace AudioSystemLogic { bool TriggerGameplaySfx(BaseSystem& baseSystem, const std::string& cueName, float gain); }
namespace OreMiningSystemLogic { bool IsMiningActive(const BaseSystem& baseSystem); }
namespace BlockSelectionSystemLogic {
    void RemoveBlockFromCache(BaseSystem& baseSystem, const std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position);
}
namespace StructureCaptureSystemLogic { void NotifyBlockChanged(BaseSystem& baseSystem, int worldIndex, const glm::vec3& position); }
namespace VoxelMeshingSystemLogic { void RequestPriorityVoxelRemesh(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const glm::ivec3& worldCell); }

namespace GroundCraftingSystemLogic {
    namespace {
        constexpr float kCellEpsilon = 0.05f;
        constexpr float kPi = 3.14159265359f;
        constexpr int kHatchetMaterialCount = 5;

        struct GroundCraftState {
            bool active = false;
            bool applied = false;
            bool lastVDown = false;
            float timer = 0.0f;
            float duration = 0.52f;
            float applyTime = 0.26f;
            int worldIndex = -1;
            glm::ivec3 centerCell = glm::ivec3(0);
            glm::ivec2 downDir = glm::ivec2(0, 1);
        };

        GroundCraftState& state() {
            static GroundCraftState s;
            return s;
        }

        struct CellBlock {
            bool present = false;
            bool fromVoxel = false;
            int prototypeID = -1;
            glm::vec3 color = glm::vec3(1.0f);
            glm::ivec3 cell = glm::ivec3(0);
        };

        struct RecipeMatch {
            bool valid = false;
            glm::ivec3 stoneCell = glm::ivec3(0);
            glm::ivec3 stickCell = glm::ivec3(0);
            int hatchetMaterial = 0;
        };

        bool readRegistryBool(const BaseSystem& baseSystem, const char* key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            const std::string& raw = std::get<std::string>(it->second);
            if (raw == "1" || raw == "true" || raw == "TRUE" || raw == "True") return true;
            if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "False") return false;
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

        bool isBlockPrototypeValid(const std::vector<Entity>& prototypes, int prototypeID) {
            return prototypeID >= 0
                && prototypeID < static_cast<int>(prototypes.size())
                && prototypes[static_cast<size_t>(prototypeID)].isBlock;
        }

        bool isPetalPrototypeName(const std::string& name) {
            return name.rfind("StonePebblePetals", 0) == 0;
        }

        bool isStickPrototypeName(const std::string& name) {
            return name == "StickTexX" || name == "StickTexZ";
        }

        int hatchetMaterialFromStonePrototypeName(const std::string& name) {
            if (name == "StonePebbleTexX" || name == "StonePebbleTexZ") return 0;
            if (name == "StonePebbleRubyTexX" || name == "StonePebbleRubyTexZ") return 1;
            if (name == "StonePebbleAmethystTexX" || name == "StonePebbleAmethystTexZ") return 2;
            if (name == "StonePebbleFlouriteTexX" || name == "StonePebbleFlouriteTexZ") return 3;
            if (name == "StonePebbleSilverTexX" || name == "StonePebbleSilverTexZ") return 4;
            return -1;
        }

        glm::vec3 unpackColor(uint32_t packed) {
            if (packed == 0) return glm::vec3(1.0f);
            float r = static_cast<float>((packed >> 16) & 0xff) / 255.0f;
            float g = static_cast<float>((packed >> 8) & 0xff) / 255.0f;
            float b = static_cast<float>(packed & 0xff) / 255.0f;
            return glm::vec3(r, g, b);
        }

        glm::vec3 normalizeOrDefault(const glm::vec3& v, const glm::vec3& fallback) {
            if (glm::length(v) < 1e-4f) return fallback;
            return glm::normalize(v);
        }

        glm::vec3 cameraForwardDirection(const PlayerContext& player) {
            glm::vec3 front(0.0f);
            front.x = std::cos(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
            front.y = std::sin(glm::radians(player.cameraPitch));
            front.z = std::sin(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
            if (glm::length(front) < 0.0001f) {
                return glm::vec3(0.0f, 0.0f, -1.0f);
            }
            return glm::normalize(front);
        }

        glm::vec3 projectDirectionOnSurface(const glm::vec3& direction, const glm::vec3& surfaceNormal) {
            glm::vec3 n = normalizeOrDefault(surfaceNormal, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::vec3 projected = direction - n * glm::dot(direction, n);
            if (glm::length(projected) < 1e-4f) {
                projected = glm::cross(n, glm::vec3(0.0f, 1.0f, 0.0f));
                if (glm::length(projected) < 1e-4f) {
                    projected = glm::cross(n, glm::vec3(1.0f, 0.0f, 0.0f));
                }
            }
            return normalizeOrDefault(projected, glm::vec3(1.0f, 0.0f, 0.0f));
        }

        bool queryCellBlock(BaseSystem& baseSystem,
                            const std::vector<Entity>& prototypes,
                            int worldIndex,
                            const glm::ivec3& cell,
                            CellBlock& outBlock) {
            outBlock = CellBlock{};
            outBlock.cell = cell;

            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t voxelID = baseSystem.voxelWorld->getBlockWorld(cell);
                if (voxelID != 0 && voxelID < prototypes.size() && isBlockPrototypeValid(prototypes, static_cast<int>(voxelID))) {
                    outBlock.present = true;
                    outBlock.fromVoxel = true;
                    outBlock.prototypeID = static_cast<int>(voxelID);
                    outBlock.color = unpackColor(baseSystem.voxelWorld->getColorWorld(cell));
                    return true;
                }
            }

            if (!baseSystem.level) return false;
            if (worldIndex < 0 || worldIndex >= static_cast<int>(baseSystem.level->worlds.size())) return false;
            const glm::vec3 position = glm::vec3(cell);
            const Entity& world = baseSystem.level->worlds[static_cast<size_t>(worldIndex)];
            for (const EntityInstance& inst : world.instances) {
                if (glm::distance(inst.position, position) > kCellEpsilon) continue;
                if (!isBlockPrototypeValid(prototypes, inst.prototypeID)) continue;
                outBlock.present = true;
                outBlock.fromVoxel = false;
                outBlock.prototypeID = inst.prototypeID;
                outBlock.color = inst.color;
                return true;
            }
            return false;
        }

        bool removeCellBlock(BaseSystem& baseSystem,
                             std::vector<Entity>& prototypes,
                             int worldIndex,
                             const glm::ivec3& cell,
                             int expectedPrototypeID) {
            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t voxelID = baseSystem.voxelWorld->getBlockWorld(cell);
                if (voxelID != 0
                    && voxelID < prototypes.size()
                    && (expectedPrototypeID < 0 || expectedPrototypeID == static_cast<int>(voxelID))) {
                    baseSystem.voxelWorld->setBlockWorld(cell, 0, 0);
                    VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, cell);
                    if (worldIndex >= 0) {
                        StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, worldIndex, glm::vec3(cell));
                        BlockSelectionSystemLogic::RemoveBlockFromCache(baseSystem, prototypes, worldIndex, glm::vec3(cell));
                    }
                    return true;
                }
            }

            if (!baseSystem.level) return false;
            if (worldIndex < 0 || worldIndex >= static_cast<int>(baseSystem.level->worlds.size())) return false;
            Entity& world = baseSystem.level->worlds[static_cast<size_t>(worldIndex)];
            const glm::vec3 position = glm::vec3(cell);
            for (size_t i = 0; i < world.instances.size(); ++i) {
                const EntityInstance& inst = world.instances[i];
                if (glm::distance(inst.position, position) > kCellEpsilon) continue;
                if (!isBlockPrototypeValid(prototypes, inst.prototypeID)) continue;
                if (expectedPrototypeID >= 0 && inst.prototypeID != expectedPrototypeID) continue;
                world.instances[i] = world.instances.back();
                world.instances.pop_back();
                StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, worldIndex, position);
                BlockSelectionSystemLogic::RemoveBlockFromCache(baseSystem, prototypes, worldIndex, position);
                return true;
            }
            return false;
        }

        bool isValidPetalRing(BaseSystem& baseSystem,
                              const std::vector<Entity>& prototypes,
                              int worldIndex,
                              const glm::ivec3& centerCell) {
            for (int dz = -2; dz <= 2; ++dz) {
                for (int dx = -2; dx <= 2; ++dx) {
                    if (std::abs(dx) != 2 && std::abs(dz) != 2) continue;
                    const glm::ivec3 cell = centerCell + glm::ivec3(dx, 0, dz);
                    CellBlock block;
                    if (!queryCellBlock(baseSystem, prototypes, worldIndex, cell, block) || !block.present) return false;
                    if (!isBlockPrototypeValid(prototypes, block.prototypeID)) return false;
                    const std::string& name = prototypes[static_cast<size_t>(block.prototypeID)].name;
                    if (!isPetalPrototypeName(name)) return false;
                }
            }
            return true;
        }

        bool isPetalAtCell(BaseSystem& baseSystem,
                           const std::vector<Entity>& prototypes,
                           int worldIndex,
                           const glm::ivec3& cell) {
            CellBlock block;
            if (!queryCellBlock(baseSystem, prototypes, worldIndex, cell, block) || !block.present) return false;
            if (!isBlockPrototypeValid(prototypes, block.prototypeID)) return false;
            const std::string& name = prototypes[static_cast<size_t>(block.prototypeID)].name;
            return isPetalPrototypeName(name);
        }

        bool findRingDownDirection(BaseSystem& baseSystem,
                                   const std::vector<Entity>& prototypes,
                                   int worldIndex,
                                   const glm::ivec3& centerCell,
                                   glm::ivec2& outDownDir) {
            int markerCount = 0;
            glm::ivec2 markerDir(0, 1);
            constexpr int kMarkerDistance = 3;
            constexpr std::array<glm::ivec2, 4> kCardinalDirs = {
                glm::ivec2(0, -1),
                glm::ivec2(1, 0),
                glm::ivec2(0, 1),
                glm::ivec2(-1, 0)
            };
            for (const glm::ivec2& dir : kCardinalDirs) {
                const glm::ivec3 markerCell = centerCell + glm::ivec3(dir.x * kMarkerDistance, 0, dir.y * kMarkerDistance);
                if (!isPetalAtCell(baseSystem, prototypes, worldIndex, markerCell)) continue;
                markerDir = dir;
                ++markerCount;
            }

            if (markerCount != 1) return false;
            outDownDir = markerDir;
            return true;
        }

        bool findNearestPetalRingCenter(BaseSystem& baseSystem,
                                        const std::vector<Entity>& prototypes,
                                        int worldIndex,
                                        const glm::ivec3& anchorCell,
                                        const glm::vec3& nearPos,
                                        glm::ivec3& outCenter,
                                        glm::ivec2& outDownDir) {
            bool found = false;
            float bestDist2 = std::numeric_limits<float>::max();

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -6; dz <= 6; ++dz) {
                    for (int dx = -6; dx <= 6; ++dx) {
                        const glm::ivec3 center = anchorCell + glm::ivec3(dx, dy, dz);
                        if (!isValidPetalRing(baseSystem, prototypes, worldIndex, center)) continue;
                        glm::ivec2 downDir(0, 1);
                        if (!findRingDownDirection(baseSystem, prototypes, worldIndex, center, downDir)) continue;
                        const glm::vec3 centerPos = glm::vec3(center);
                        const glm::vec3 d = centerPos - nearPos;
                        const float dist2 = glm::dot(d, d);
                        if (!found || dist2 < bestDist2) {
                            found = true;
                            bestDist2 = dist2;
                            outCenter = center;
                            outDownDir = downDir;
                        }
                    }
                }
            }

            return found;
        }

        RecipeMatch findRecipeAtCenter(BaseSystem& baseSystem,
                                       const std::vector<Entity>& prototypes,
                                       int worldIndex,
                                       const glm::ivec3& centerCell,
                                       const glm::ivec2& downDir) {
            RecipeMatch match;
            const glm::ivec2 clampedDownDir(
                std::clamp(downDir.x, -1, 1),
                std::clamp(downDir.y, -1, 1));
            if (std::abs(clampedDownDir.x) + std::abs(clampedDownDir.y) != 1) {
                return match;
            }

            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const glm::ivec3 stoneCell = centerCell + glm::ivec3(dx, 0, dz);
                    const glm::ivec3 stickCell = stoneCell + glm::ivec3(clampedDownDir.x, 0, clampedDownDir.y);
                    if (std::abs(stickCell.x - centerCell.x) > 1 || std::abs(stickCell.z - centerCell.z) > 1) continue;

                    CellBlock stone;
                    CellBlock stick;
                    if (!queryCellBlock(baseSystem, prototypes, worldIndex, stoneCell, stone) || !stone.present) continue;
                    if (!queryCellBlock(baseSystem, prototypes, worldIndex, stickCell, stick) || !stick.present) continue;
                    if (!isBlockPrototypeValid(prototypes, stone.prototypeID) || !isBlockPrototypeValid(prototypes, stick.prototypeID)) continue;

                    const std::string& stoneName = prototypes[static_cast<size_t>(stone.prototypeID)].name;
                    const std::string& stickName = prototypes[static_cast<size_t>(stick.prototypeID)].name;
                    const int material = hatchetMaterialFromStonePrototypeName(stoneName);
                    if (material < 0) continue;
                    if (!isStickPrototypeName(stickName)) continue;

                    match.valid = true;
                    match.stoneCell = stoneCell;
                    match.stickCell = stickCell;
                    match.hatchetMaterial = material;
                    return match;
                }
            }
            return match;
        }

        bool craftHatchetFromRecipe(BaseSystem& baseSystem,
                                    std::vector<Entity>& prototypes,
                                    int worldIndex,
                                    const glm::ivec3& centerCell,
                                    const RecipeMatch& recipe) {
            if (!recipe.valid || !baseSystem.player) return false;

            CellBlock centerBlock;
            queryCellBlock(baseSystem, prototypes, worldIndex, centerCell, centerBlock);
            if (centerBlock.present
                && centerCell != recipe.stoneCell
                && centerCell != recipe.stickCell) {
                return false;
            }

            if (!removeCellBlock(baseSystem, prototypes, worldIndex, recipe.stoneCell, -1)) return false;
            if (!removeCellBlock(baseSystem, prototypes, worldIndex, recipe.stickCell, -1)) return false;

            PlayerContext& player = *baseSystem.player;
            for (int i = 0; i < kHatchetMaterialCount; ++i) {
                player.hatchetInventoryByMaterial[static_cast<size_t>(i)] = 0;
            }
            player.hatchetInventoryCount = 0;
            player.hatchetHeld = false;
            player.hatchetPlacedInWorld = true;
            player.hatchetPlacedCell = centerCell;
            player.hatchetPlacedWorldIndex = worldIndex;
            player.hatchetPlacedNormal = glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 forward = cameraForwardDirection(player);
            player.hatchetPlacedDirection = projectDirectionOnSurface(forward, player.hatchetPlacedNormal);
            player.hatchetPlacedPosition = glm::vec3(centerCell) - player.hatchetPlacedNormal * 0.47f;
            player.hatchetPlacedMaterial = std::clamp(recipe.hatchetMaterial, 0, kHatchetMaterialCount - 1);
            player.hatchetSelectedMaterial = player.hatchetPlacedMaterial;

            AudioSystemLogic::TriggerGameplaySfx(baseSystem, "place_block.ck", 1.0f);
            return true;
        }

        void ensureFullscreenResources(RendererContext& renderer, WorldContext& world) {
            if (!renderer.uiShader) {
                renderer.uiShader = std::make_unique<Shader>(
                    world.shaders["UI_VERTEX_SHADER"].c_str(),
                    world.shaders["UI_FRAGMENT_SHADER"].c_str());
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

        bool beginRitual(BaseSystem& baseSystem, std::vector<Entity>& prototypes) {
            if (!baseSystem.player || !baseSystem.level) return false;

            PlayerContext& player = *baseSystem.player;
            LevelContext& level = *baseSystem.level;
            if (level.worlds.empty()) return false;

            int worldIndex = player.targetedWorldIndex;
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) {
                worldIndex = level.activeWorldIndex;
            }
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) {
                worldIndex = 0;
            }
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return false;

            const glm::ivec3 anchorCell = player.hasBlockTarget
                ? glm::ivec3(glm::round(player.targetedBlockPosition))
                : glm::ivec3(glm::round(player.cameraPosition));

            glm::ivec3 centerCell(0);
            glm::ivec2 downDir(0, 1);
            if (!findNearestPetalRingCenter(
                    baseSystem,
                    prototypes,
                    worldIndex,
                    anchorCell,
                    player.cameraPosition,
                    centerCell,
                    downDir)) {
                return false;
            }

            const RecipeMatch recipe = findRecipeAtCenter(baseSystem, prototypes, worldIndex, centerCell, downDir);
            if (!recipe.valid) return false;

            GroundCraftState& s = state();
            s.active = true;
            s.applied = false;
            s.timer = 0.0f;
            s.duration = std::max(0.10f, readRegistryFloat(baseSystem, "GroundCraftingRitualDuration", 0.52f));
            s.applyTime = glm::clamp(readRegistryFloat(baseSystem, "GroundCraftingRitualApplyTime", 0.26f), 0.0f, s.duration);
            s.worldIndex = worldIndex;
            s.centerCell = centerCell;
            s.downDir = downDir;
            AudioSystemLogic::TriggerGameplaySfx(baseSystem, "place_block.ck", 1.0f);
            return true;
        }

        void applyRitual(BaseSystem& baseSystem, std::vector<Entity>& prototypes) {
            GroundCraftState& s = state();
            if (!s.active || s.applied) return;
            s.applied = true;

            if (!baseSystem.level || s.worldIndex < 0 || s.worldIndex >= static_cast<int>(baseSystem.level->worlds.size())) {
                return;
            }
            if (!isValidPetalRing(baseSystem, prototypes, s.worldIndex, s.centerCell)) {
                return;
            }
            glm::ivec2 downDir(0, 1);
            if (!findRingDownDirection(baseSystem, prototypes, s.worldIndex, s.centerCell, downDir)) {
                return;
            }
            s.downDir = downDir;

            const RecipeMatch recipe = findRecipeAtCenter(baseSystem, prototypes, s.worldIndex, s.centerCell, s.downDir);
            if (!recipe.valid) return;
            (void)craftHatchetFromRecipe(baseSystem, prototypes, s.worldIndex, s.centerCell, recipe);
        }
    } // namespace

    bool IsRitualActive(const BaseSystem& baseSystem) {
        if (!readRegistryBool(baseSystem, "GroundCraftingSystem", true)) return false;
        if (!readRegistryBool(baseSystem, "GroundCraftingEnabled", true)) return false;
        return state().active;
    }

    void UpdateGroundCrafting(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!readRegistryBool(baseSystem, "GroundCraftingSystem", true)
            || !readRegistryBool(baseSystem, "GroundCraftingEnabled", true)) {
            state().active = false;
            state().applied = false;
            return;
        }

        GroundCraftState& s = state();
        const bool vDown = (win && glfwGetKey(win, GLFW_KEY_V) == GLFW_PRESS);
        const bool vPressed = (!s.lastVDown && vDown);
        s.lastVDown = vDown;

        if (!s.active && vPressed) {
            (void)beginRitual(baseSystem, prototypes);
        }

        if (!s.active) return;

        s.timer += std::max(0.0f, dt);
        if (!s.applied && s.timer >= s.applyTime) {
            applyRitual(baseSystem, prototypes);
        }
        if (s.timer >= s.duration) {
            s.active = false;
            s.applied = false;
        }
    }

    void RenderGroundCrafting(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes;
        (void)dt;
        (void)win;

        GroundCraftState& s = state();
        if (!s.active || !baseSystem.renderer || !baseSystem.world) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        ensureFullscreenResources(renderer, world);
        if (!renderer.uiShader || renderer.uiVAO == 0) return;

        const float t = glm::clamp(s.timer / std::max(0.001f, s.duration), 0.0f, 1.0f);
        float alpha = std::sin(t * kPi);
        alpha = std::pow(glm::clamp(alpha, 0.0f, 1.0f), 0.72f);
        if (alpha <= 0.001f) return;

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendColor(0.0f, 0.0f, 0.0f, alpha);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);

        renderer.uiShader->use();
        renderer.uiShader->setVec3("color", glm::vec3(0.0f));
        glBindVertexArray(renderer.uiVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
    }
}
