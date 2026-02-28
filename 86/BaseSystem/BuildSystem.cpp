#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace BlockSelectionSystemLogic {
    bool HasBlockAt(BaseSystem& baseSystem, const std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position);
    void AddBlockToCache(BaseSystem& baseSystem, std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position, int prototypeID);
}
namespace HostLogic { const Entity* findPrototype(const std::string& name, const std::vector<Entity>& prototypes); }
namespace BlockSelectionSystemLogic { void InvalidateWorldCache(int worldIndex); }
namespace StructureCaptureSystemLogic { void NotifyBlockChanged(BaseSystem& baseSystem, int worldIndex, const glm::vec3& position); }
namespace VoxelMeshingSystemLogic { void RequestPriorityVoxelRemesh(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const glm::ivec3& worldCell); }

namespace BuildSystemLogic {

    namespace {
        constexpr float kPickEpsilon = 0.05f;

        bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            if (std::holds_alternative<std::string>(it->second)) {
                const std::string& raw = std::get<std::string>(it->second);
                if (raw == "true" || raw == "TRUE" || raw == "True" || raw == "1") return true;
                if (raw == "false" || raw == "FALSE" || raw == "False" || raw == "0") return false;
            }
            return fallback;
        }

        struct TexturePalette {
            std::vector<int> prototypeIDs;
            size_t sourceCount = 0;
            const Entity* sourcePtr = nullptr;
        };

        TexturePalette& getTexturePalette() {
            static TexturePalette palette;
            return palette;
        }

        void rebuildTexturePalette(TexturePalette& palette, const std::vector<Entity>& prototypes) {
            palette.prototypeIDs.clear();
            for (size_t i = 0; i < prototypes.size(); ++i) {
                const Entity& proto = prototypes[i];
                if (proto.isBlock && proto.useTexture) {
                    palette.prototypeIDs.push_back(static_cast<int>(i));
                }
            }
            palette.sourceCount = prototypes.size();
        }

        const std::vector<int>& ensureTexturePalette(const std::vector<Entity>& prototypes) {
            TexturePalette& palette = getTexturePalette();
            if (palette.sourceCount != prototypes.size() || palette.sourcePtr != prototypes.data()) {
                rebuildTexturePalette(palette, prototypes);
                palette.sourcePtr = prototypes.data();
            }
            return palette.prototypeIDs;
        }

        int resolvePreviewTileIndex(const WorldContext* world, int prototypeID) {
            if (!world) return -1;
            if (prototypeID < 0 || prototypeID >= static_cast<int>(world->prototypeTextureSets.size())) return -1;
            const FaceTextureSet& set = world->prototypeTextureSets[prototypeID];
            if (set.all >= 0) return set.all;
            if (set.side >= 0) return set.side;
            if (set.top >= 0) return set.top;
            if (set.bottom >= 0) return set.bottom;
            return -1;
        }

        uint32_t packColor(const glm::vec3& color) {
            auto clampByte = [](float v) {
                int iv = static_cast<int>(std::round(v * 255.0f));
                if (iv < 0) iv = 0;
                if (iv > 255) iv = 255;
                return static_cast<uint32_t>(iv);
            };
            uint32_t r = clampByte(color.r);
            uint32_t g = clampByte(color.g);
            uint32_t b = clampByte(color.b);
            return (r << 16) | (g << 8) | b;
        }

        glm::vec3 unpackColor(uint32_t packed) {
            if (packed == 0) return glm::vec3(1.0f);
            float r = static_cast<float>((packed >> 16) & 0xff) / 255.0f;
            float g = static_cast<float>((packed >> 8) & 0xff) / 255.0f;
            float b = static_cast<float>(packed & 0xff) / 255.0f;
            return glm::vec3(r, g, b);
        }

        bool findBlockInstance(const LevelContext& level,
                               const std::vector<Entity>& prototypes,
                               int worldIndex,
                               const glm::vec3& position,
                               EntityInstance& outInst) {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return false;
            const Entity& world = level.worlds[worldIndex];
            for (const auto& inst : world.instances) {
                if (glm::distance(inst.position, position) > kPickEpsilon) continue;
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[inst.prototypeID];
                if (!proto.isBlock) continue;
                outInst = inst;
                return true;
            }
            return false;
        }
    }

    bool BlockExistsAt(const Entity& world, const glm::vec3& position) {
        for (const auto& inst : world.instances) {
            if (glm::distance(inst.position, position) < 0.01f) return true;
        }
        return false;
    }

    void UpdateBuildMode(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player || !baseSystem.level || !baseSystem.hud) return;
        PlayerContext& player = *baseSystem.player;
        HUDContext& hud = *baseSystem.hud;
        LevelContext& level = *baseSystem.level;
        const bool legacyBuildModesEnabled = getRegistryBool(baseSystem, "LegacyBuildModesEnabled", false);
        if (baseSystem.ui && baseSystem.ui->active) {
            hud.buildModeActive = false;
            hud.showCharge = false;
            return;
        }
        if (!legacyBuildModesEnabled) {
            if (player.buildMode == BuildModeType::Color || player.buildMode == BuildModeType::Texture) {
                player.buildMode = BuildModeType::Pickup;
            }
            hud.buildModeActive = false;
            hud.showCharge = false;
            return;
        }

        const auto& texturePalette = ensureTexturePalette(prototypes);

        bool hudRefreshed = false;

        if (player.middleMousePressed && player.hasBlockTarget && player.targetedWorldIndex >= 0) {
            int pickedPrototypeID = -1;
            glm::vec3 pickedColor = glm::vec3(1.0f);

            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                glm::ivec3 targetCell = glm::ivec3(glm::round(player.targetedBlockPosition));
                uint32_t pickedVoxelID = baseSystem.voxelWorld->getBlockWorld(targetCell);
                if (pickedVoxelID != 0 && pickedVoxelID < prototypes.size() && prototypes[pickedVoxelID].isBlock) {
                    pickedPrototypeID = static_cast<int>(pickedVoxelID);
                    pickedColor = unpackColor(baseSystem.voxelWorld->getColorWorld(targetCell));
                }
            }

            if (pickedPrototypeID < 0) {
                EntityInstance picked;
                if (findBlockInstance(level, prototypes, player.targetedWorldIndex, player.targetedBlockPosition, picked)) {
                    pickedPrototypeID = picked.prototypeID;
                    pickedColor = picked.color;
                }
            }

            if (pickedPrototypeID >= 0 && pickedPrototypeID < static_cast<int>(prototypes.size())) {
                const Entity& pickedProto = prototypes[pickedPrototypeID];
                if (pickedProto.useTexture) {
                    player.buildMode = BuildModeType::Texture;
                    int newIndex = 0;
                    for (size_t i = 0; i < texturePalette.size(); ++i) {
                        if (texturePalette[i] == pickedPrototypeID) {
                            newIndex = static_cast<int>(i);
                            break;
                        }
                    }
                    player.buildTextureIndex = newIndex;
                } else {
                    player.buildMode = BuildModeType::Color;
                    player.buildColor = pickedColor;
                }
                player.isHoldingBlock = false;
                player.heldPrototypeID = -1;
                if (baseSystem.gems) {
                    baseSystem.gems->blockModeHoldingGem = false;
                    baseSystem.gems->heldDrop = GemDropState{};
                }
                hudRefreshed = true;
            }
        }

        bool inColorMode = player.buildMode == BuildModeType::Color;
        bool inTextureMode = player.buildMode == BuildModeType::Texture;
        if (!inColorMode && !inTextureMode) {
            hud.buildModeActive = false;
            hud.buildModeType = static_cast<int>(player.buildMode);
            hud.showCharge = false;
            return;
        }

        double scrollDelta = player.scrollYOffset;
        player.scrollYOffset = 0.0;
        if (inColorMode) {
            if (player.rightMousePressed) {
                player.buildChannel = (player.buildChannel + 1) % 3;
                hudRefreshed = true;
            }
            if (scrollDelta != 0.0) {
                float delta = static_cast<float>(scrollDelta) * 0.05f;
                player.buildColor[player.buildChannel] = glm::clamp(player.buildColor[player.buildChannel] + delta, 0.0f, 1.0f);
                hudRefreshed = true;
            }
        } else if (inTextureMode) {
            int paletteCount = static_cast<int>(texturePalette.size());
            if (paletteCount > 0 && scrollDelta != 0.0) {
                int steps = static_cast<int>(scrollDelta);
                if (steps == 0) steps = (scrollDelta > 0.0) ? 1 : -1;
                player.buildTextureIndex = (player.buildTextureIndex + steps) % paletteCount;
                if (player.buildTextureIndex < 0) player.buildTextureIndex += paletteCount;
                hudRefreshed = true;
            }
        }

        if (player.leftMousePressed && player.hasBlockTarget && glm::length(player.targetedBlockNormal) > 0.001f) {
            if (player.targetedWorldIndex >= 0 && player.targetedWorldIndex < static_cast<int>(baseSystem.level->worlds.size())) {
                int buildPrototypeID = -1;
                glm::vec3 buildColor = player.buildColor;
                if (inTextureMode) {
                    if (!texturePalette.empty()) {
                        int paletteCount = static_cast<int>(texturePalette.size());
                        if (player.buildTextureIndex < 0 || player.buildTextureIndex >= paletteCount) {
                            player.buildTextureIndex = 0;
                        }
                        buildPrototypeID = texturePalette[player.buildTextureIndex];
                        buildColor = glm::vec3(1.0f);
                    }
                } else {
                    const Entity* blockProto = HostLogic::findPrototype("Block", prototypes);
                    if (blockProto) {
                        buildPrototypeID = blockProto->prototypeID;
                    }
                }
                if (buildPrototypeID >= 0) {
                    glm::vec3 placePos = player.targetedBlockPosition + player.targetedBlockNormal;
                    if (!BlockSelectionSystemLogic::HasBlockAt(baseSystem, prototypes, player.targetedWorldIndex, placePos)) {
                        bool placedInVoxel = false;
                        if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled
                            && buildPrototypeID >= 0
                            && buildPrototypeID < static_cast<int>(prototypes.size())
                            && prototypes[buildPrototypeID].isChunkable) {
                            glm::ivec3 placeCell = glm::ivec3(glm::round(placePos));
                            baseSystem.voxelWorld->setBlockWorld(
                                placeCell,
                                static_cast<uint32_t>(buildPrototypeID),
                                packColor(buildColor)
                            );
                            VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, placeCell);
                            StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, player.targetedWorldIndex, glm::vec3(placeCell));
                            placedInVoxel = true;
                        }

                        if (!placedInVoxel) {
                            Entity& world = baseSystem.level->worlds[player.targetedWorldIndex];
                            world.instances.push_back(HostLogic::CreateInstance(baseSystem, buildPrototypeID, placePos, buildColor));
                            BlockSelectionSystemLogic::AddBlockToCache(baseSystem, prototypes, player.targetedWorldIndex, placePos, buildPrototypeID);
                            StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, player.targetedWorldIndex, placePos);
                        }
                    }
                }
            }
            hudRefreshed = true;
        }

        if (hudRefreshed) {
            hud.displayTimer = 2.0f;
        } else if (hud.displayTimer > 0.0f) {
            hud.displayTimer = std::max(0.0f, hud.displayTimer - dt);
        }

        hud.buildModeActive = true;
        hud.buildModeType = inTextureMode ? static_cast<int>(BuildModeType::Texture) : static_cast<int>(BuildModeType::Color);
        hud.buildPreviewColor = inTextureMode ? glm::vec3(1.0f) : player.buildColor;
        hud.buildChannel = inTextureMode ? 0 : player.buildChannel;
        if (inTextureMode && !texturePalette.empty()) {
            int paletteCount = static_cast<int>(texturePalette.size());
            if (player.buildTextureIndex < 0 || player.buildTextureIndex >= paletteCount) {
                player.buildTextureIndex = 0;
            }
            int protoID = texturePalette[player.buildTextureIndex];
            hud.buildPreviewTileIndex = resolvePreviewTileIndex(baseSystem.world.get(), protoID);
        } else {
            hud.buildPreviewTileIndex = -1;
        }
        hud.chargeValue = 1.0f;
        hud.chargeReady = true;
        hud.showCharge = hud.displayTimer > 0.0f;
    }
}
