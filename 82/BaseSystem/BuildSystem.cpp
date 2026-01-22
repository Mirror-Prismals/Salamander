#pragma once

#include <GLFW/glfw3.h>

namespace BlockSelectionSystemLogic {
    bool HasBlockAt(BaseSystem& baseSystem, const std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position);
    void AddBlockToCache(BaseSystem& baseSystem, std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position, int prototypeID);
}
namespace HostLogic { const Entity* findPrototype(const std::string& name, const std::vector<Entity>& prototypes); }
namespace BlockSelectionSystemLogic { void InvalidateWorldCache(int worldIndex); }
namespace StructureCaptureSystemLogic { void NotifyBlockChanged(BaseSystem& baseSystem, int worldIndex, const glm::vec3& position); }

namespace BuildSystemLogic {

    namespace {
        constexpr float kPickEpsilon = 0.05f;

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

        const auto& texturePalette = ensureTexturePalette(prototypes);

        bool hudRefreshed = false;

        if (player.middleMousePressed && player.hasBlockTarget && player.targetedWorldIndex >= 0) {
            EntityInstance picked;
            if (findBlockInstance(level, prototypes, player.targetedWorldIndex, player.targetedBlockPosition, picked)) {
                const Entity& pickedProto = prototypes[picked.prototypeID];
                if (pickedProto.useTexture) {
                    player.buildMode = BuildModeType::Texture;
                    int newIndex = 0;
                    for (size_t i = 0; i < texturePalette.size(); ++i) {
                        if (texturePalette[i] == picked.prototypeID) {
                            newIndex = static_cast<int>(i);
                            break;
                        }
                    }
                    player.buildTextureIndex = newIndex;
                } else {
                    player.buildMode = BuildModeType::Color;
                    player.buildColor = picked.color;
                }
                player.isHoldingBlock = false;
                player.heldPrototypeID = -1;
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
                Entity& world = baseSystem.level->worlds[player.targetedWorldIndex];
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
                        world.instances.push_back(HostLogic::CreateInstance(baseSystem, buildPrototypeID, placePos, buildColor));
                        BlockSelectionSystemLogic::AddBlockToCache(baseSystem, prototypes, player.targetedWorldIndex, placePos, buildPrototypeID);
                        StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, player.targetedWorldIndex, placePos);
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
