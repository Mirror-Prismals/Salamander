#pragma once
#include "../Host.h"
#include "Host/PlatformInput.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace HostLogic { EntityInstance CreateInstance(BaseSystem& baseSystem, int prototypeID, glm::vec3 position, glm::vec3 color); }
namespace BlockSelectionSystemLogic {
    void RemoveBlockFromCache(BaseSystem& baseSystem, const std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position);
    bool HasBlockAt(BaseSystem& baseSystem, const std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position);
    void AddBlockToCache(BaseSystem& baseSystem, std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position, int prototypeID);
}
namespace StructureCaptureSystemLogic { void NotifyBlockChanged(BaseSystem& baseSystem, int worldIndex, const glm::vec3& position); }
namespace RayTracedAudioSystemLogic { void InvalidateSourceCache(BaseSystem& baseSystem); }
namespace ChucKSystemLogic { void StopNoiseShred(BaseSystem& baseSystem); }
namespace AudioSystemLogic { bool TriggerGameplaySfx(BaseSystem& baseSystem, const std::string& cueName, float gain); }
namespace VoxelMeshingSystemLogic { void RequestPriorityVoxelRemesh(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const glm::ivec3& worldCell); }
namespace TreeGenerationSystemLogic { void NotifyPineLogRemoved(const glm::ivec3& worldCell, int removedPrototypeID); }
namespace GemSystemLogic {
    void SpawnGemDropFromOre(BaseSystem& baseSystem, std::vector<Entity>& prototypes, int removedPrototypeID, const glm::vec3& blockPos, const glm::vec3& playerForward);
    bool TryPickupGemFromRay(BaseSystem& baseSystem, const glm::vec3& rayOrigin, const glm::vec3& rayDirection, float maxDistance, GemDropState* outDrop);
    void PlaceGemDrop(BaseSystem& baseSystem, GemDropState&& heldDrop, const glm::vec3& position);
}
namespace OreMiningSystemLogic {
    bool IsMiningActive(const BaseSystem& baseSystem);
    bool StartOreMiningFromBlock(BaseSystem& baseSystem,
                                 std::vector<Entity>& prototypes,
                                 int worldIndex,
                                 const glm::ivec3& cell,
                                 int targetPrototypeID,
                                 const glm::vec3& blockPos,
                                 const glm::vec3& playerForward);
}
namespace GroundCraftingSystemLogic { bool IsRitualActive(const BaseSystem& baseSystem); }
namespace GemChiselSystemLogic {
    bool IsChiselActive(const BaseSystem& baseSystem);
    bool StartGemChiselAtCell(BaseSystem& baseSystem, const glm::ivec3& cell, int worldIndex);
}
namespace BookSystemLogic { bool IsBookPrototypeName(const std::string& name); }

namespace BlockChargeSystemLogic {

    namespace {
        constexpr float CHARGE_TIME_PICKUP = 0.25f;
        constexpr float CHARGE_TIME_DESTROY = 0.25f;
        constexpr float POSITION_EPSILON = 0.05f;
        constexpr int HATCHET_MATERIAL_STONE = 0;
        constexpr int HATCHET_MATERIAL_RUBY = 1;
        constexpr int HATCHET_MATERIAL_AMETHYST = 2;
        constexpr int HATCHET_MATERIAL_FLOURITE = 3;
        constexpr int HATCHET_MATERIAL_SILVER = 4;
        constexpr int HATCHET_MATERIAL_COUNT = 5;
        constexpr int BLOCK_DAMAGE_SHADER_MAX = 64;
        constexpr int CHALK_TILE_CORNER = 186;
        constexpr int CHALK_TILE_CROSS = 187;
        constexpr int CHALK_TILE_DOT = 188;
        constexpr int CHALK_TILE_END = 189;
        constexpr int CHALK_TILE_STRAIGHT = 190;
        constexpr int CHALK_TILE_T = 191;
        constexpr int SURFACE_STONE_PILE_MIN = 1;
        constexpr int SURFACE_STONE_PILE_MAX = 8;

        struct ThrownHeldBlockRuntime {
            bool active = false;
            int worldIndex = -1;
            int prototypeID = -1;
            glm::vec3 color = glm::vec3(1.0f);
            uint32_t packedColor = 0u;
            bool hasSourceCell = false;
            glm::ivec3 sourceCell = glm::ivec3(0);
            glm::vec3 position = glm::vec3(0.0f);
            glm::vec3 velocity = glm::vec3(0.0f);
            int instanceID = -1;
            float age = 0.0f;
        };

        struct BlockDamageKey {
            int worldIndex = -1;
            glm::ivec3 cell = glm::ivec3(0);
            bool operator==(const BlockDamageKey& other) const {
                return worldIndex == other.worldIndex && cell == other.cell;
            }
        };

        struct BlockDamageKeyHash {
            std::size_t operator()(const BlockDamageKey& key) const noexcept {
                std::size_t h = std::hash<int>()(key.worldIndex);
                h ^= (std::hash<int>()(key.cell.x) + 0x9e3779b9u + (h << 6) + (h >> 2));
                h ^= (std::hash<int>()(key.cell.y) + 0x9e3779b9u + (h << 6) + (h >> 2));
                h ^= (std::hash<int>()(key.cell.z) + 0x9e3779b9u + (h << 6) + (h >> 2));
                return h;
            }
        };

        struct BlockDamageEntry {
            int prototypeID = -1;
            int hits = 0;
            int requiredHits = 8;
            double lastHitTime = 0.0;
        };

        std::unordered_map<BlockDamageKey, BlockDamageEntry, BlockDamageKeyHash>& blockDamageMap() {
            static std::unordered_map<BlockDamageKey, BlockDamageEntry, BlockDamageKeyHash> s_map;
            return s_map;
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

        int readRegistryInt(const BaseSystem& baseSystem, const char* key, int fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stoi(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        bool readRegistryBool(const BaseSystem& baseSystem, const char* key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            std::string v = std::get<std::string>(it->second);
            std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
            if (v == "0" || v == "false" || v == "no" || v == "off") return false;
            return fallback;
        }

        glm::vec3 cameraEyePosition(const BaseSystem& baseSystem, const PlayerContext& player) {
            if (baseSystem.gamemode == "survival") {
                return player.cameraPosition + glm::vec3(0.0f, 0.6f, 0.0f);
            }
            return player.cameraPosition;
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

        bool isPickupHandMode(BuildModeType mode) {
            return mode == BuildModeType::Pickup || mode == BuildModeType::PickupLeft;
        }

        void saveActiveHeldBlockForMode(PlayerContext& player, BuildModeType mode) {
            if (mode == BuildModeType::Pickup) {
                player.rightHandHoldingBlock = player.isHoldingBlock;
                player.rightHandHeldPrototypeID = player.heldPrototypeID;
                player.rightHandHeldBlockColor = player.heldBlockColor;
                player.rightHandHeldPackedColor = player.heldPackedColor;
                player.rightHandHeldHasSourceCell = player.heldHasSourceCell;
                player.rightHandHeldSourceCell = player.heldSourceCell;
            } else if (mode == BuildModeType::PickupLeft) {
                player.leftHandHoldingBlock = player.isHoldingBlock;
                player.leftHandHeldPrototypeID = player.heldPrototypeID;
                player.leftHandHeldBlockColor = player.heldBlockColor;
                player.leftHandHeldPackedColor = player.heldPackedColor;
                player.leftHandHeldHasSourceCell = player.heldHasSourceCell;
                player.leftHandHeldSourceCell = player.heldSourceCell;
            }
        }

        void loadActiveHeldBlockForMode(PlayerContext& player, BuildModeType mode) {
            if (mode == BuildModeType::Pickup) {
                player.isHoldingBlock = player.rightHandHoldingBlock;
                player.heldPrototypeID = player.rightHandHeldPrototypeID;
                player.heldBlockColor = player.rightHandHeldBlockColor;
                player.heldPackedColor = player.rightHandHeldPackedColor;
                player.heldHasSourceCell = player.rightHandHeldHasSourceCell;
                player.heldSourceCell = player.rightHandHeldSourceCell;
            } else if (mode == BuildModeType::PickupLeft) {
                player.isHoldingBlock = player.leftHandHoldingBlock;
                player.heldPrototypeID = player.leftHandHeldPrototypeID;
                player.heldBlockColor = player.leftHandHeldBlockColor;
                player.heldPackedColor = player.leftHandHeldPackedColor;
                player.heldHasSourceCell = player.leftHandHeldHasSourceCell;
                player.heldSourceCell = player.leftHandHeldSourceCell;
            }
        }

        struct ScopeExit {
            std::function<void()> fn;
            ~ScopeExit() {
                if (fn) fn();
            }
        };

        bool isRemovableGameplayBlock(const Entity& proto) {
            if (!proto.isBlock) return false;
            if (proto.name == "VoidPortalBlockTex") return false;
            // Terrain voxels are often chunkable but flagged immutable (e.g. ScaffoldBlock).
            // Allow those for gameplay pickup/destroy while keeping non-chunkable immutables protected.
            if (proto.isMutable) return true;
            return proto.isChunkable;
        }

        bool isComputerPrototypeID(const std::vector<Entity>& prototypes, int prototypeID) {
            return prototypeID >= 0
                && prototypeID < static_cast<int>(prototypes.size())
                && prototypes[static_cast<size_t>(prototypeID)].name == "Computer";
        }

        bool isWallStonePrototypeName(const std::string& name) {
            return name == "WallStoneTexPosX"
                || name == "WallStoneTexNegX"
                || name == "WallStoneTexPosZ"
                || name == "WallStoneTexNegZ";
        }

        int resolveLeafPrototypeID(const std::vector<Entity>& prototypes) {
            for (const auto& proto : prototypes) {
                if (proto.name == "Leaf") return proto.prototypeID;
            }
            return -1;
        }

        bool isStickPrototypeName(const std::string& name) {
            return name == "StickTexX" || name == "StickTexZ";
        }

        bool isChalkStickPrototypeName(const std::string& name) {
            return name == "StonePebbleChalkTexX"
                || name == "StonePebbleChalkTexZ";
        }

        bool isChalkDrawToolPrototypeName(const std::string& name) {
            return isChalkStickPrototypeName(name)
                || name == "ChalkBlockTex";
        }

        bool isChalkDustPrototypeName(const std::string& name) {
            return name == "GrassCoverChalkTexX"
                || name == "GrassCoverChalkTexZ";
        }

        bool isVerticalLogPrototypeName(const std::string& name) {
            return name == "FirLog1Tex"
                || name == "FirLog2Tex"
                || name == "SpruceLog1Tex"
                || name == "SpruceLog2Tex"
                || name == "SpruceLog3Tex"
                || name == "SpruceLog4Tex"
                || name == "OakLogTex";
        }

        bool isCavePotPrototypeName(const std::string& name) {
            return name == "StonePebbleCavePotTexX"
                || name == "StonePebbleCavePotTexZ";
        }

        bool isFlowerPrototypeName(const std::string& name) {
            return name == "Flower";
        }

        bool isNaturalSurfaceStonePrototypeName(const std::string& name) {
            return name == "StonePebbleTexX" || name == "StonePebbleTexZ";
        }

        bool isSurfaceStonePrototypeName(const std::string& name) {
            return isNaturalSurfaceStonePrototypeName(name)
                || name == "StonePebbleRubyTexX" || name == "StonePebbleRubyTexZ"
                || name == "StonePebbleAmethystTexX" || name == "StonePebbleAmethystTexZ"
                || name == "StonePebbleFlouriteTexX" || name == "StonePebbleFlouriteTexZ"
                || name == "StonePebbleSilverTexX" || name == "StonePebbleSilverTexZ";
        }

        bool isBlueprintPrototypeName(const std::string& name) {
            return name.rfind("GrassCoverBlueprint", 0) == 0;
        }

        int oreKindForPrototypeName(const std::string& name) {
            if (name == "RubyOreTex") return 0;
            if (name == "AmethystOreTex") return 1;
            if (name == "FlouriteOreTex" || name == "FluoriteOreTex") return 2;
            if (name == "SilverOreTex") return 3;
            return -1;
        }

        bool isOrePrototypeID(const std::vector<Entity>& prototypes, int prototypeID) {
            if (prototypeID < 0 || prototypeID >= static_cast<int>(prototypes.size())) return false;
            return oreKindForPrototypeName(prototypes[static_cast<size_t>(prototypeID)].name) >= 0;
        }

        int hatchetMaterialFromGemKind(int gemKind) {
            if (gemKind == 0) return HATCHET_MATERIAL_RUBY;
            if (gemKind == 1) return HATCHET_MATERIAL_AMETHYST;
            if (gemKind == 2) return HATCHET_MATERIAL_FLOURITE;
            if (gemKind == 3) return HATCHET_MATERIAL_SILVER;
            return HATCHET_MATERIAL_STONE;
        }

        int hatchetMaterialFromStonePrototypeName(const std::string& name, bool* outRecognized = nullptr) {
            if (outRecognized) *outRecognized = true;
            if (name == "StonePebbleTexX" || name == "StonePebbleTexZ") return HATCHET_MATERIAL_STONE;
            if (name == "StonePebbleRubyTexX" || name == "StonePebbleRubyTexZ") return HATCHET_MATERIAL_RUBY;
            if (name == "StonePebbleAmethystTexX" || name == "StonePebbleAmethystTexZ") return HATCHET_MATERIAL_AMETHYST;
            if (name == "StonePebbleFlouriteTexX" || name == "StonePebbleFlouriteTexZ") return HATCHET_MATERIAL_FLOURITE;
            if (name == "StonePebbleSilverTexX" || name == "StonePebbleSilverTexZ") return HATCHET_MATERIAL_SILVER;
            if (outRecognized) *outRecognized = false;
            return HATCHET_MATERIAL_STONE;
        }

        glm::vec3 hatchetMaterialColor(int material) {
            switch (material) {
                case HATCHET_MATERIAL_RUBY: return glm::vec3(0.86f, 0.18f, 0.20f);
                case HATCHET_MATERIAL_AMETHYST: return glm::vec3(0.64f, 0.48f, 0.88f);
                case HATCHET_MATERIAL_FLOURITE: return glm::vec3(0.38f, 0.67f, 0.96f);
                case HATCHET_MATERIAL_SILVER: return glm::vec3(0.92f, 0.93f, 0.95f);
                case HATCHET_MATERIAL_STONE:
                default: return glm::vec3(1.0f);
            }
        }

        int detectHatchetMaterialFromColor(const glm::vec3& color) {
            int bestMaterial = HATCHET_MATERIAL_STONE;
            float bestDist2 = std::numeric_limits<float>::max();
            for (int m = HATCHET_MATERIAL_RUBY; m < HATCHET_MATERIAL_COUNT; ++m) {
                const glm::vec3 target = hatchetMaterialColor(m);
                const glm::vec3 d = color - target;
                const float dist2 = glm::dot(d, d);
                if (dist2 < bestDist2) {
                    bestDist2 = dist2;
                    bestMaterial = m;
                }
            }
            // Keep natural grey stones as base material unless they are clearly gem-tinted.
            const float matchThreshold2 = 0.16f * 0.16f;
            if (bestDist2 <= matchThreshold2) return bestMaterial;
            return HATCHET_MATERIAL_STONE;
        }

        int sumHatchetInventory(const PlayerContext& player) {
            int total = 0;
            for (int i = 0; i < HATCHET_MATERIAL_COUNT; ++i) {
                total += std::max(0, player.hatchetInventoryByMaterial[static_cast<size_t>(i)]);
            }
            return total;
        }

        int firstAvailableHatchetMaterial(const PlayerContext& player) {
            for (int i = 0; i < HATCHET_MATERIAL_COUNT; ++i) {
                if (player.hatchetInventoryByMaterial[static_cast<size_t>(i)] > 0) return i;
            }
            return HATCHET_MATERIAL_STONE;
        }

        int resolveSurfaceStonePrototypeID(const std::vector<Entity>& prototypes, int preferredPrototypeID = -1) {
            if (preferredPrototypeID >= 0 && preferredPrototypeID < static_cast<int>(prototypes.size())) {
                if (isSurfaceStonePrototypeName(prototypes[static_cast<size_t>(preferredPrototypeID)].name)) {
                    return preferredPrototypeID;
                }
            }

            int fallbackZ = -1;
            for (const auto& proto : prototypes) {
                if (!proto.isBlock) continue;
                if (proto.name == "StonePebbleTexX") return proto.prototypeID;
                if (fallbackZ < 0 && proto.name == "StonePebbleTexZ") fallbackZ = proto.prototypeID;
            }
            return fallbackZ;
        }

        int resolveGemIngotPrototypeID(const std::vector<Entity>& prototypes, int material) {
            const char* preferredX = nullptr;
            const char* fallbackZ = nullptr;
            switch (material) {
                case HATCHET_MATERIAL_RUBY:
                    preferredX = "StonePebbleRubyTexX";
                    fallbackZ = "StonePebbleRubyTexZ";
                    break;
                case HATCHET_MATERIAL_AMETHYST:
                    preferredX = "StonePebbleAmethystTexX";
                    fallbackZ = "StonePebbleAmethystTexZ";
                    break;
                case HATCHET_MATERIAL_FLOURITE:
                    preferredX = "StonePebbleFlouriteTexX";
                    fallbackZ = "StonePebbleFlouriteTexZ";
                    break;
                case HATCHET_MATERIAL_SILVER:
                    preferredX = "StonePebbleSilverTexX";
                    fallbackZ = "StonePebbleSilverTexZ";
                    break;
                default:
                    return resolveSurfaceStonePrototypeID(prototypes);
            }

            int fallbackId = -1;
            for (const auto& proto : prototypes) {
                if (!proto.isBlock) continue;
                if (preferredX && proto.name == preferredX) return proto.prototypeID;
                if (fallbackId < 0 && fallbackZ && proto.name == fallbackZ) fallbackId = proto.prototypeID;
            }
            if (fallbackId >= 0) return fallbackId;
            return resolveSurfaceStonePrototypeID(prototypes);
        }

        int resolveMappedAtlasTileIndex(const WorldContext& world, const char* textureKey) {
            if (!textureKey) return -1;
            auto it = world.atlasMappings.find(textureKey);
            if (it == world.atlasMappings.end()) return -1;
            if (it->second.all >= 0) return it->second.all;
            if (it->second.side >= 0) return it->second.side;
            if (it->second.top >= 0) return it->second.top;
            if (it->second.bottom >= 0) return it->second.bottom;
            return -1;
        }

        bool extractAtlasTilePixels(const std::vector<unsigned char>& atlasPixels,
                                    const glm::ivec2& atlasSize,
                                    int tileIndex,
                                    const glm::ivec2& tileSize,
                                    int tilesPerRow,
                                    int tilesPerCol,
                                    std::vector<unsigned char>& outPixels) {
            outPixels.clear();
            if (atlasPixels.empty()
                || atlasSize.x <= 0
                || atlasSize.y <= 0
                || tileIndex < 0
                || tileSize.x <= 0
                || tileSize.y <= 0
                || tilesPerRow <= 0
                || tilesPerCol <= 0) {
                return false;
            }

            const int tileX = (tileIndex % tilesPerRow) * tileSize.x;
            const int tileRowFromTop = (tileIndex / tilesPerRow);
            const int tileRowFromBottom = (tilesPerCol - 1 - tileRowFromTop);
            const int tileY = tileRowFromBottom * tileSize.y;
            if (tileX < 0
                || tileY < 0
                || tileX + tileSize.x > atlasSize.x
                || tileY + tileSize.y > atlasSize.y) {
                return false;
            }

            outPixels.resize(static_cast<size_t>(tileSize.x * tileSize.y * 4), 0u);
            for (int y = 0; y < tileSize.y; ++y) {
                const size_t src = static_cast<size_t>(((tileY + y) * atlasSize.x + tileX) * 4);
                const size_t dst = static_cast<size_t>((y * tileSize.x) * 4);
                std::copy_n(&atlasPixels[src], static_cast<size_t>(tileSize.x * 4), &outPixels[dst]);
            }
            return true;
        }

        bool buildStencilHeadVoxelsFromTile(const std::vector<unsigned char>& stencilTile,
                                            int width,
                                            int height,
                                            std::vector<glm::ivec3>& outVoxels) {
            outVoxels.clear();
            if (width <= 0 || height <= 0) return false;
            if (stencilTile.size() < static_cast<size_t>(width * height * 4)) return false;

            int minStencilAlpha = 255;
            int maxStencilAlpha = 0;
            for (size_t i = 0; i + 3 < stencilTile.size(); i += 4) {
                const int a = static_cast<int>(stencilTile[i + 3]);
                minStencilAlpha = std::min(minStencilAlpha, a);
                maxStencilAlpha = std::max(maxStencilAlpha, a);
            }
            const bool useStencilAlphaMask = (maxStencilAlpha - minStencilAlpha) > 16 && maxStencilAlpha > 0;
            auto readLumaAt = [&](int x, int y) -> int {
                const int sx = std::clamp(x, 0, std::max(0, width - 1));
                const int sy = std::clamp(y, 0, std::max(0, height - 1));
                const size_t idx = static_cast<size_t>((sy * width + sx) * 4);
                return (static_cast<int>(stencilTile[idx + 0])
                    + static_cast<int>(stencilTile[idx + 1])
                    + static_cast<int>(stencilTile[idx + 2])) / 3;
            };
            const int backgroundLuma = (
                readLumaAt(0, 0)
                + readLumaAt(width - 1, 0)
                + readLumaAt(0, height - 1)
                + readLumaAt(width - 1, height - 1)
            ) / 4;

            const int halfW = width / 2;
            const int halfH = height / 2;
            outVoxels.reserve(static_cast<size_t>(width * height));
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const size_t idx = static_cast<size_t>((y * width + x) * 4);
                    int overlayAlpha = 0;
                    if (useStencilAlphaMask) {
                        overlayAlpha = static_cast<int>(stencilTile[idx + 3]);
                    } else {
                        const int stencilLuma = (static_cast<int>(stencilTile[idx + 0])
                            + static_cast<int>(stencilTile[idx + 1])
                            + static_cast<int>(stencilTile[idx + 2])) / 3;
                        const int lumaDelta = std::abs(stencilLuma - backgroundLuma);
                        overlayAlpha = std::clamp(lumaDelta * 2, 0, 255);
                    }
                    if (overlayAlpha < 20) continue;

                    const int localX = x - halfW;
                    const int localY = (height - 1 - y) - halfH;
                    outVoxels.emplace_back(localX, localY, 0);
                }
            }
            return !outVoxels.empty();
        }

        bool buildPickaxeHeadVoxelsFromStencil(BaseSystem& baseSystem,
                                               std::vector<glm::ivec3>& outVoxels) {
            outVoxels.clear();
            if (!baseSystem.world || !baseSystem.renderer || !baseSystem.renderBackend) return false;
            const WorldContext& world = *baseSystem.world;
            const RendererContext& renderer = *baseSystem.renderer;
            if (renderer.atlasTexture == 0
                || renderer.atlasTextureSize.x <= 0
                || renderer.atlasTextureSize.y <= 0
                || renderer.atlasTilesPerRow <= 0
                || renderer.atlasTilesPerCol <= 0
                || renderer.atlasTileSize.x <= 0
                || renderer.atlasTileSize.y <= 0) {
                return false;
            }

            const int stencilTile = resolveMappedAtlasTileIndex(world, "24x24PickaxeStencil");
            if (stencilTile < 0) return false;

            struct PickaxeHeadStencilCache {
                RenderHandle atlasTexture = 0;
                int atlasWidth = 0;
                int atlasHeight = 0;
                int tilesPerRow = 0;
                int tilesPerCol = 0;
                glm::ivec2 tileSize = glm::ivec2(0);
                int stencilTile = -1;
                std::vector<glm::ivec3> voxels;
            };
            static PickaxeHeadStencilCache s_cache;

            const bool cacheValid = !s_cache.voxels.empty()
                && s_cache.atlasTexture == renderer.atlasTexture
                && s_cache.atlasWidth == renderer.atlasTextureSize.x
                && s_cache.atlasHeight == renderer.atlasTextureSize.y
                && s_cache.tilesPerRow == renderer.atlasTilesPerRow
                && s_cache.tilesPerCol == renderer.atlasTilesPerCol
                && s_cache.tileSize == renderer.atlasTileSize
                && s_cache.stencilTile == stencilTile;
            if (cacheValid) {
                outVoxels = s_cache.voxels;
                return true;
            }

            std::vector<unsigned char> atlasPixels;
            if (!baseSystem.renderBackend->readTexture2DRgba(
                    renderer.atlasTexture,
                    renderer.atlasTextureSize.x,
                    renderer.atlasTextureSize.y,
                    atlasPixels)) {
                return false;
            }

            std::vector<unsigned char> stencilPixels;
            if (!extractAtlasTilePixels(
                    atlasPixels,
                    renderer.atlasTextureSize,
                    stencilTile,
                    renderer.atlasTileSize,
                    renderer.atlasTilesPerRow,
                    renderer.atlasTilesPerCol,
                    stencilPixels)) {
                return false;
            }

            std::vector<glm::ivec3> builtVoxels;
            if (!buildStencilHeadVoxelsFromTile(
                    stencilPixels,
                    renderer.atlasTileSize.x,
                    renderer.atlasTileSize.y,
                    builtVoxels)) {
                return false;
            }

            s_cache.atlasTexture = renderer.atlasTexture;
            s_cache.atlasWidth = renderer.atlasTextureSize.x;
            s_cache.atlasHeight = renderer.atlasTextureSize.y;
            s_cache.tilesPerRow = renderer.atlasTilesPerRow;
            s_cache.tilesPerCol = renderer.atlasTilesPerCol;
            s_cache.tileSize = renderer.atlasTileSize;
            s_cache.stencilTile = stencilTile;
            s_cache.voxels = builtVoxels;

            outVoxels = s_cache.voxels;
            return !outVoxels.empty();
        }

        int64_t packInt2(int x, int y) {
            return (static_cast<int64_t>(x) << 32)
                ^ static_cast<uint32_t>(y);
        }

        glm::ivec2 unpackInt2(int64_t packed) {
            return glm::ivec2(
                static_cast<int>(packed >> 32),
                static_cast<int>(static_cast<uint32_t>(packed & 0xffffffffu))
            );
        }

        bool isGemValidPickaxeHeadShape(BaseSystem& baseSystem,
                                        const GemDropState& drop,
                                        const std::vector<glm::ivec3>& stencilHeadVoxels) {
            if (drop.voxelCells.empty() || stencilHeadVoxels.empty()) return false;

            std::unordered_set<int64_t> stencil2D;
            stencil2D.reserve(stencilHeadVoxels.size() * 2u + 1u);
            glm::ivec2 sMin(std::numeric_limits<int>::max());
            glm::ivec2 sMax(std::numeric_limits<int>::min());
            for (const glm::ivec3& cell : stencilHeadVoxels) {
                const glm::ivec2 p(cell.x, cell.y);
                stencil2D.insert(packInt2(p.x, p.y));
                sMin = glm::min(sMin, p);
                sMax = glm::max(sMax, p);
            }
            if (stencil2D.empty()) return false;

            const std::array<std::pair<int, int>, 3> projections = {{
                {0, 1}, // x,y
                {0, 2}, // x,z
                {1, 2}  // y,z
            }};

            int bestOverlap = -1;
            int bestOutside = std::numeric_limits<int>::max();
            int bestGemCount = 0;
            for (const auto& axes : projections) {
                const int axisU = axes.first;
                const int axisV = axes.second;
                std::unordered_set<int64_t> gem2D;
                gem2D.reserve(drop.voxelCells.size() * 2u + 1u);
                glm::ivec2 gMin(std::numeric_limits<int>::max());
                glm::ivec2 gMax(std::numeric_limits<int>::min());

                for (const glm::ivec3& cell : drop.voxelCells) {
                    const int coords[3] = {cell.x, cell.y, cell.z};
                    const glm::ivec2 p(coords[axisU], coords[axisV]);
                    gem2D.insert(packInt2(p.x, p.y));
                    gMin = glm::min(gMin, p);
                    gMax = glm::max(gMax, p);
                }
                if (gem2D.empty()) continue;

                const int dxMin = std::max(-96, sMin.x - gMax.x - 2);
                const int dxMax = std::min(96, sMax.x - gMin.x + 2);
                const int dyMin = std::max(-96, sMin.y - gMax.y - 2);
                const int dyMax = std::min(96, sMax.y - gMin.y + 2);

                int localBestOverlap = -1;
                int localBestOutside = std::numeric_limits<int>::max();
                for (int dy = dyMin; dy <= dyMax; ++dy) {
                    for (int dx = dxMin; dx <= dxMax; ++dx) {
                        int overlap = 0;
                        for (int64_t packed : gem2D) {
                            const glm::ivec2 p = unpackInt2(packed);
                            if (stencil2D.find(packInt2(p.x + dx, p.y + dy)) != stencil2D.end()) {
                                ++overlap;
                            }
                        }
                        const int outside = static_cast<int>(gem2D.size()) - overlap;
                        if (overlap > localBestOverlap
                            || (overlap == localBestOverlap && outside < localBestOutside)) {
                            localBestOverlap = overlap;
                            localBestOutside = outside;
                        }
                    }
                }

                if (localBestOverlap > bestOverlap
                    || (localBestOverlap == bestOverlap && localBestOutside < bestOutside)) {
                    bestOverlap = localBestOverlap;
                    bestOutside = localBestOutside;
                    bestGemCount = static_cast<int>(gem2D.size());
                }
            }

            if (bestOverlap <= 0 || bestGemCount <= 0) return false;
            const int stencilCount = static_cast<int>(stencil2D.size());
            const float coverage = static_cast<float>(bestOverlap) / static_cast<float>(std::max(1, stencilCount));
            const float outsideRatio = static_cast<float>(bestOutside) / static_cast<float>(std::max(1, bestGemCount));
            const float minCoverage = glm::clamp(
                readRegistryFloat(baseSystem, "GemPickaxeHeadMinStencilCoverage", 0.60f),
                0.1f,
                1.0f
            );
            const float maxOutsideRatio = glm::clamp(
                readRegistryFloat(baseSystem, "GemPickaxeHeadMaxOutsideRatio", 0.25f),
                0.0f,
                1.0f
            );
            const int minAbsoluteOverlap = std::max(
                12,
                static_cast<int>(std::floor(static_cast<float>(stencilCount) * 0.30f))
            );

            return coverage >= minCoverage
                && outsideRatio <= maxOutsideRatio
                && bestOverlap >= minAbsoluteOverlap;
        }

        int detectFlowerPetalVariantFromColor(const glm::vec3& color) {
            static const std::array<glm::vec3, 6> kFlowerPalette = {
                glm::vec3(0.97f, 0.48f, 0.72f), // pink
                glm::vec3(0.94f, 0.72f, 0.27f), // marigold
                glm::vec3(0.99f, 0.95f, 0.85f), // cream
                glm::vec3(0.86f, 0.61f, 0.96f), // lilac
                glm::vec3(0.96f, 0.43f, 0.35f), // coral
                glm::vec3(0.95f, 0.83f, 0.28f)  // gold
            };
            int best = 0;
            float bestDist2 = std::numeric_limits<float>::max();
            for (int i = 0; i < static_cast<int>(kFlowerPalette.size()); ++i) {
                const glm::vec3 d = color - kFlowerPalette[static_cast<size_t>(i)];
                const float dist2 = glm::dot(d, d);
                if (dist2 < bestDist2) {
                    bestDist2 = dist2;
                    best = i;
                }
            }
            return best;
        }

        int resolveFlowerPetalsPrototypeID(const std::vector<Entity>& prototypes, int variant, bool preferX) {
            struct Pair { const char* x = nullptr; const char* z = nullptr; };
            static const std::array<Pair, 6> kPairs = {{
                {"StonePebblePetalsPinkTexX", "StonePebblePetalsPinkTexZ"},
                {"StonePebblePetalsMarigoldTexX", "StonePebblePetalsMarigoldTexZ"},
                {"StonePebblePetalsCreamTexX", "StonePebblePetalsCreamTexZ"},
                {"StonePebblePetalsLilacTexX", "StonePebblePetalsLilacTexZ"},
                {"StonePebblePetalsCoralTexX", "StonePebblePetalsCoralTexZ"},
                {"StonePebblePetalsGoldTexX", "StonePebblePetalsGoldTexZ"}
            }};
            if (variant < 0 || variant >= static_cast<int>(kPairs.size())) variant = 0;
            const Pair& pair = kPairs[static_cast<size_t>(variant)];
            const char* primary = preferX ? pair.x : pair.z;
            const char* secondary = preferX ? pair.z : pair.x;
            int fallback = -1;
            for (const auto& proto : prototypes) {
                if (!proto.isBlock) continue;
                if (primary && proto.name == primary) return proto.prototypeID;
                if (fallback < 0 && secondary && proto.name == secondary) fallback = proto.prototypeID;
            }
            return fallback;
        }

        glm::vec3 normalizeOrDefault(const glm::vec3& v, const glm::vec3& fallback) {
            if (glm::length(v) < 1e-4f) return fallback;
            return glm::normalize(v);
        }

        glm::vec3 projectDirectionOnSurface(const glm::vec3& direction,
                                            const glm::vec3& surfaceNormal) {
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

        bool isWallStonePrototypeID(const std::vector<Entity>& prototypes, int prototypeID) {
            if (prototypeID < 0 || prototypeID >= static_cast<int>(prototypes.size())) return false;
            return isWallStonePrototypeName(prototypes[static_cast<size_t>(prototypeID)].name);
        }

        bool isLeafPrototypeID(const std::vector<Entity>& prototypes, int prototypeID, int leafPrototypeID) {
            if (prototypeID < 0 || prototypeID >= static_cast<int>(prototypes.size())) return false;
            if (leafPrototypeID >= 0 && prototypeID == leafPrototypeID) return true;
            return prototypes[static_cast<size_t>(prototypeID)].name == "Leaf";
        }

        bool isBoulderingAnchorPrototypeID(const std::vector<Entity>& prototypes,
                                           int prototypeID,
                                           bool allowLeafAnchors,
                                           int leafPrototypeID) {
            if (isWallStonePrototypeID(prototypes, prototypeID)) return true;
            if (allowLeafAnchors && isLeafPrototypeID(prototypes, prototypeID, leafPrototypeID)) return true;
            return false;
        }

        int resolveTargetPrototypeID(const BaseSystem& baseSystem,
                                     const LevelContext& level,
                                     const std::vector<Entity>& prototypes,
                                     const PlayerContext& player,
                                     glm::ivec3* outCell = nullptr,
                                     bool* outFromVoxel = nullptr) {
            if (!player.hasBlockTarget) return -1;
            const glm::ivec3 targetCell = glm::ivec3(glm::round(player.targetedBlockPosition));
            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t id = baseSystem.voxelWorld->getBlockWorld(targetCell);
                if (id > 0 && id < prototypes.size()) {
                    if (outCell) *outCell = targetCell;
                    if (outFromVoxel) *outFromVoxel = true;
                    return static_cast<int>(id);
                }
            }

            int worldIndex = player.targetedWorldIndex;
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return -1;
            const Entity& world = level.worlds[static_cast<size_t>(worldIndex)];
            for (const auto& inst : world.instances) {
                if (glm::distance(inst.position, player.targetedBlockPosition) > POSITION_EPSILON) continue;
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                if (outCell) *outCell = glm::ivec3(glm::round(inst.position));
                if (outFromVoxel) *outFromVoxel = false;
                return inst.prototypeID;
            }
            return -1;
        }

        bool findBlueprintInstanceAtTarget(const LevelContext& level,
                                           const std::vector<Entity>& prototypes,
                                           const PlayerContext& player,
                                           glm::ivec3* outCell) {
            int worldIndex = player.targetedWorldIndex;
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return false;
            const Entity& world = level.worlds[static_cast<size_t>(worldIndex)];
            for (const auto& inst : world.instances) {
                if (glm::distance(inst.position, player.targetedBlockPosition) > POSITION_EPSILON) continue;
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[static_cast<size_t>(inst.prototypeID)];
                if (!isBlueprintPrototypeName(proto.name)) continue;
                if (outCell) *outCell = glm::ivec3(glm::round(inst.position));
                return true;
            }
            return false;
        }

        float snapToTwentyFourth(float v) {
            return std::round(v * 24.0f) / 24.0f;
        }

        bool computeGemPlacementWithinCell(const BaseSystem& baseSystem,
                                           const GemDropState& drop,
                                           const glm::ivec3& cell,
                                           const glm::vec3& hitPos,
                                           glm::vec3* outPos) {
            if (!outPos) return false;

            const float renderScale = glm::clamp(readRegistryFloat(baseSystem, "GemDropVisualScale", 1.0f), 0.1f, 100.0f);
            const float gemScale = std::max(0.05f, renderScale);
            constexpr float kMiniVoxelSize = 1.0f / 24.0f;
            const float half = (kMiniVoxelSize * 0.5f) * gemScale;

            float minX = -0.499f;
            float maxX = 0.499f;
            float minZ = -0.499f;
            float maxZ = 0.499f;
            if (!drop.voxelCells.empty()) {
                minX = std::numeric_limits<float>::max();
                maxX = -std::numeric_limits<float>::max();
                minZ = std::numeric_limits<float>::max();
                maxZ = -std::numeric_limits<float>::max();
                for (const glm::ivec3& voxel : drop.voxelCells) {
                    const float centerX = static_cast<float>(voxel.x) * (kMiniVoxelSize * gemScale);
                    const float centerZ = static_cast<float>(voxel.z) * (kMiniVoxelSize * gemScale);
                    minX = std::min(minX, centerX - half);
                    maxX = std::max(maxX, centerX + half);
                    minZ = std::min(minZ, centerZ - half);
                    maxZ = std::max(maxZ, centerZ + half);
                }
            }

            float minOffsetX = -0.5f - minX;
            float maxOffsetX = 0.5f - maxX;
            float minOffsetZ = -0.5f - minZ;
            float maxOffsetZ = 0.5f - maxZ;
            if (minOffsetX > maxOffsetX) {
                // If a raw gem footprint is larger than one block, still allow in-block
                // 1/24 placement control on blueprint surfaces.
                minOffsetX = -11.0f / 24.0f;
                maxOffsetX =  11.0f / 24.0f;
            }
            if (minOffsetZ > maxOffsetZ) {
                minOffsetZ = -11.0f / 24.0f;
                maxOffsetZ =  11.0f / 24.0f;
            }

            float localX = snapToTwentyFourth(hitPos.x - static_cast<float>(cell.x));
            float localZ = snapToTwentyFourth(hitPos.z - static_cast<float>(cell.z));
            localX = glm::clamp(localX, minOffsetX, maxOffsetX);
            localZ = glm::clamp(localZ, minOffsetZ, maxOffsetZ);
            localX = glm::clamp(snapToTwentyFourth(localX), minOffsetX, maxOffsetX);
            localZ = glm::clamp(snapToTwentyFourth(localZ), minOffsetZ, maxOffsetZ);

            *outPos = glm::vec3(
                static_cast<float>(cell.x) + localX,
                static_cast<float>(cell.y),
                static_cast<float>(cell.z) + localZ
            );
            return true;
        }

        bool hasBoulderingAnchorAtCell(const BaseSystem& baseSystem,
                                       const LevelContext& level,
                                       const std::vector<Entity>& prototypes,
                                       const glm::ivec3& cell,
                                       int worldIndexHint,
                                       bool allowLeafAnchors,
                                       int leafPrototypeID) {
            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
                if (id > 0
                    && id < prototypes.size()
                    && isBoulderingAnchorPrototypeID(
                        prototypes,
                        static_cast<int>(id),
                        allowLeafAnchors,
                        leafPrototypeID)) {
                    return true;
                }
            }
            if (worldIndexHint < 0 || worldIndexHint >= static_cast<int>(level.worlds.size())) return false;
            const Entity& world = level.worlds[static_cast<size_t>(worldIndexHint)];
            const glm::vec3 cellPos = glm::vec3(cell);
            for (const auto& inst : world.instances) {
                if (glm::distance(inst.position, cellPos) > POSITION_EPSILON) continue;
                if (isBoulderingAnchorPrototypeID(prototypes, inst.prototypeID, allowLeafAnchors, leafPrototypeID)) return true;
            }
            return false;
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

        int decodeSurfaceStonePileCount(uint32_t packedColor) {
            const int encoded = static_cast<int>((packedColor >> 24) & 0xffu);
            if (encoded <= 0) return SURFACE_STONE_PILE_MIN;
            return std::clamp(encoded, SURFACE_STONE_PILE_MIN, SURFACE_STONE_PILE_MAX);
        }

        uint32_t withSurfaceStonePileCount(uint32_t packedColorRgb, int pileCount) {
            const uint32_t rgb = packedColorRgb & 0x00ffffffu;
            const uint32_t encoded = static_cast<uint32_t>(
                std::clamp(pileCount, SURFACE_STONE_PILE_MIN, SURFACE_STONE_PILE_MAX)
            ) << 24u;
            return rgb | encoded;
        }

        int encodeChalkSnapshotMarker(int tileIndex, int quarterTurns) {
            if (tileIndex < CHALK_TILE_CORNER || tileIndex > CHALK_TILE_T) return 0;
            const int variant = tileIndex - CHALK_TILE_CORNER;
            const int turns = quarterTurns & 3;
            const int encoded = variant * 4 + turns + 1;
            return std::clamp(encoded, 0, 255);
        }

        uint32_t withSnapshotMarker(uint32_t packedColorRgb, int marker) {
            const uint32_t rgb = packedColorRgb & 0x00ffffffu;
            const uint32_t high = static_cast<uint32_t>(std::clamp(marker, 0, 255)) << 24;
            return rgb | high;
        }

        uint32_t packChalkDustSnapshotColor(int tileIndex, int quarterTurns) {
            return withSnapshotMarker(packColor(glm::vec3(1.0f)), encodeChalkSnapshotMarker(tileIndex, quarterTurns));
        }

        struct CellBlockInfo {
            bool present = false;
            bool fromVoxel = false;
            int prototypeID = -1;
            glm::vec3 color = glm::vec3(1.0f);
            size_t instanceIndex = 0;
        };

        bool queryBlockAtCell(const BaseSystem& baseSystem,
                              const LevelContext& level,
                              const std::vector<Entity>& prototypes,
                              int worldIndex,
                              const glm::ivec3& cell,
                              CellBlockInfo* outInfo) {
            if (outInfo) *outInfo = CellBlockInfo{};
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return false;

            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
                if (id > 0 && id < prototypes.size()) {
                    if (outInfo) {
                        outInfo->present = true;
                        outInfo->fromVoxel = true;
                        outInfo->prototypeID = static_cast<int>(id);
                        outInfo->color = unpackColor(baseSystem.voxelWorld->getColorWorld(cell));
                    }
                    return true;
                }
            }

            const Entity& world = level.worlds[static_cast<size_t>(worldIndex)];
            const glm::vec3 cellPos = glm::vec3(cell);
            for (size_t i = 0; i < world.instances.size(); ++i) {
                const EntityInstance& inst = world.instances[i];
                if (glm::distance(inst.position, cellPos) > POSITION_EPSILON) continue;
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                if (outInfo) {
                    outInfo->present = true;
                    outInfo->fromVoxel = false;
                    outInfo->prototypeID = inst.prototypeID;
                    outInfo->color = inst.color;
                    outInfo->instanceIndex = i;
                }
                return true;
            }
            return false;
        }

        bool replaceBlockAtCell(BaseSystem& baseSystem,
                                LevelContext& level,
                                std::vector<Entity>& prototypes,
                                int worldIndex,
                                const glm::ivec3& cell,
                                int expectedPrototypeID,
                                int newPrototypeID,
                                const glm::vec3& newColor,
                                uint32_t newPackedColor) {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return false;
            if (newPrototypeID < 0 || newPrototypeID >= static_cast<int>(prototypes.size())) return false;

            CellBlockInfo current;
            if (!queryBlockAtCell(baseSystem, level, prototypes, worldIndex, cell, &current) || !current.present) return false;
            if (expectedPrototypeID >= 0 && current.prototypeID != expectedPrototypeID) return false;

            if (current.fromVoxel && baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                baseSystem.voxelWorld->setBlockWorld(
                    cell,
                    static_cast<uint32_t>(newPrototypeID),
                    newPackedColor
                );
                VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, cell);
                StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, worldIndex, glm::vec3(cell));
                return true;
            }

            Entity& world = level.worlds[static_cast<size_t>(worldIndex)];
            if (current.instanceIndex >= world.instances.size()) return false;
            const glm::vec3 cellPos = glm::vec3(cell);
            BlockSelectionSystemLogic::RemoveBlockFromCache(baseSystem, prototypes, worldIndex, cellPos);
            world.instances[current.instanceIndex].prototypeID = newPrototypeID;
            world.instances[current.instanceIndex].color = newColor;
            BlockSelectionSystemLogic::AddBlockToCache(baseSystem, prototypes, worldIndex, cellPos, newPrototypeID);
            StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, worldIndex, cellPos);
            return true;
        }

        bool isChalkDustPrototypeID(const std::vector<Entity>& prototypes, int prototypeID) {
            if (prototypeID < 0 || prototypeID >= static_cast<int>(prototypes.size())) return false;
            return isChalkDustPrototypeName(prototypes[static_cast<size_t>(prototypeID)].name);
        }

        void pruneLegacyChalkDustInstancesAtCell(BaseSystem& baseSystem,
                                                 LevelContext& level,
                                                 const std::vector<Entity>& prototypes,
                                                 int worldIndex,
                                                 const glm::ivec3& cell) {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return;
            Entity& world = level.worlds[static_cast<size_t>(worldIndex)];
            const glm::vec3 cellPos = glm::vec3(cell);
            for (size_t i = 0; i < world.instances.size();) {
                const EntityInstance& inst = world.instances[i];
                if (glm::distance(inst.position, cellPos) > POSITION_EPSILON) {
                    ++i;
                    continue;
                }
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) {
                    ++i;
                    continue;
                }
                if (!isChalkDustPrototypeID(prototypes, inst.prototypeID)) {
                    ++i;
                    continue;
                }
                BlockSelectionSystemLogic::RemoveBlockFromCache(baseSystem, prototypes, worldIndex, inst.position);
                world.instances[i] = world.instances.back();
                world.instances.pop_back();
            }
        }

        int resolveChalkDustPrototypeID(const std::vector<Entity>& prototypes, bool preferX) {
            int fallback = -1;
            const char* primary = preferX ? "GrassCoverChalkTexX" : "GrassCoverChalkTexZ";
            const char* secondary = preferX ? "GrassCoverChalkTexZ" : "GrassCoverChalkTexX";
            for (const auto& proto : prototypes) {
                if (!proto.isBlock) continue;
                if (proto.name == primary) return proto.prototypeID;
                if (fallback < 0 && proto.name == secondary) fallback = proto.prototypeID;
            }
            return fallback;
        }

        int resolveWorkbenchPrototypeID(const std::vector<Entity>& prototypes, const glm::vec3& facingDir) {
            int posX = -1;
            int negX = -1;
            int posZ = -1;
            int negZ = -1;
            for (const auto& proto : prototypes) {
                if (!proto.isBlock) continue;
                if (proto.name == "WorkbenchTexPosX") posX = proto.prototypeID;
                else if (proto.name == "WorkbenchTexNegX") negX = proto.prototypeID;
                else if (proto.name == "WorkbenchTexPosZ") posZ = proto.prototypeID;
                else if (proto.name == "WorkbenchTexNegZ") negZ = proto.prototypeID;
            }

            const glm::vec3 dir = normalizeOrDefault(facingDir, glm::vec3(0.0f, 0.0f, 1.0f));
            if (std::abs(dir.x) >= std::abs(dir.z)) {
                if (dir.x >= 0.0f && posX >= 0) return posX;
                if (dir.x < 0.0f && negX >= 0) return negX;
            } else {
                if (dir.z >= 0.0f && posZ >= 0) return posZ;
                if (dir.z < 0.0f && negZ >= 0) return negZ;
            }
            if (posX >= 0) return posX;
            if (negX >= 0) return negX;
            if (posZ >= 0) return posZ;
            return negZ;
        }

        bool isValidChalkCraftRing(const BaseSystem& baseSystem,
                                   const LevelContext& level,
                                   const std::vector<Entity>& prototypes,
                                   int worldIndex,
                                   const glm::ivec3& centerCell) {
            for (int dz = -2; dz <= 2; ++dz) {
                for (int dx = -2; dx <= 2; ++dx) {
                    if (std::abs(dx) != 2 && std::abs(dz) != 2) continue;
                    const glm::ivec3 cell = centerCell + glm::ivec3(dx, 0, dz);
                    CellBlockInfo info;
                    if (!queryBlockAtCell(baseSystem, level, prototypes, worldIndex, cell, &info)) return false;
                    if (!info.present || !isChalkDustPrototypeID(prototypes, info.prototypeID)) return false;
                }
            }
            return true;
        }

        int chooseChalkDustTile(bool north, bool east, bool south, bool west, int& outQuarterTurns) {
            const int count = static_cast<int>(north) + static_cast<int>(east) + static_cast<int>(south) + static_cast<int>(west);
            outQuarterTurns = 0;
            if (count <= 0) return CHALK_TILE_DOT;
            if (count == 4) return CHALK_TILE_CROSS;

            if (count == 1) {
                if (west) outQuarterTurns = 0;
                else if (north) outQuarterTurns = 1;
                else if (east) outQuarterTurns = 2;
                else outQuarterTurns = 3;
                return CHALK_TILE_END;
            }

            if (count == 2) {
                if (west && east) {
                    outQuarterTurns = 0;
                    return CHALK_TILE_STRAIGHT;
                }
                if (north && south) {
                    outQuarterTurns = 1;
                    return CHALK_TILE_STRAIGHT;
                }
                // Corner base orientation (turn=0) is south+east (bottom+right).
                if (south && east) outQuarterTurns = 0;
                else if (west && south) outQuarterTurns = 1;
                else if (west && north) outQuarterTurns = 2;
                else outQuarterTurns = 3; // north+east
                outQuarterTurns = (outQuarterTurns + 2) & 3;
                return CHALK_TILE_CORNER;
            }

            if (!north) outQuarterTurns = 0;
            else if (!east) outQuarterTurns = 1;
            else if (!south) outQuarterTurns = 2;
            else outQuarterTurns = 3;
            outQuarterTurns = (outQuarterTurns + 2) & 3;
            return CHALK_TILE_T;
        }

        bool isChalkDustAtCell(const BaseSystem& baseSystem,
                               const LevelContext& level,
                               const std::vector<Entity>& prototypes,
                               int worldIndex,
                               const glm::ivec3& cell) {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return false;
            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
                return id > 0
                    && id < prototypes.size()
                    && isChalkDustPrototypeID(prototypes, static_cast<int>(id));
            }
            CellBlockInfo info;
            if (!queryBlockAtCell(baseSystem, level, prototypes, worldIndex, cell, &info)) return false;
            return info.present && isChalkDustPrototypeID(prototypes, info.prototypeID);
        }

        void refreshChalkDustCell(BaseSystem& baseSystem,
                                  LevelContext& level,
                                  std::vector<Entity>& prototypes,
                                  int worldIndex,
                                  const glm::ivec3& cell) {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return;
            // Keep voxel-mode chalk authoritative by pruning legacy per-instance chalk at touched cells.
            pruneLegacyChalkDustInstancesAtCell(baseSystem, level, prototypes, worldIndex, cell);
            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
                if (!(id > 0 && id < prototypes.size())) return;
                if (!isChalkDustPrototypeID(prototypes, static_cast<int>(id))) return;

                const bool north = isChalkDustAtCell(baseSystem, level, prototypes, worldIndex, cell + glm::ivec3(0, 0, -1));
                const bool east  = isChalkDustAtCell(baseSystem, level, prototypes, worldIndex, cell + glm::ivec3(1, 0, 0));
                const bool south = isChalkDustAtCell(baseSystem, level, prototypes, worldIndex, cell + glm::ivec3(0, 0, 1));
                const bool west  = isChalkDustAtCell(baseSystem, level, prototypes, worldIndex, cell + glm::ivec3(-1, 0, 0));

                int quarterTurns = 0;
                const int tileIndex = chooseChalkDustTile(north, east, south, west, quarterTurns);
                const uint32_t packed = packChalkDustSnapshotColor(tileIndex, quarterTurns);
                baseSystem.voxelWorld->setBlockWorld(cell, id, packed);
                VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, cell);
                StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, worldIndex, glm::vec3(cell));
                return;
            }

            CellBlockInfo info;
            if (!queryBlockAtCell(baseSystem, level, prototypes, worldIndex, cell, &info)) return;
            if (!info.present || !isChalkDustPrototypeID(prototypes, info.prototypeID)) return;

            const bool north = isChalkDustAtCell(baseSystem, level, prototypes, worldIndex, cell + glm::ivec3(0, 0, -1));
            const bool east  = isChalkDustAtCell(baseSystem, level, prototypes, worldIndex, cell + glm::ivec3(1, 0, 0));
            const bool south = isChalkDustAtCell(baseSystem, level, prototypes, worldIndex, cell + glm::ivec3(0, 0, 1));
            const bool west  = isChalkDustAtCell(baseSystem, level, prototypes, worldIndex, cell + glm::ivec3(-1, 0, 0));

            int quarterTurns = 0;
            const int tileIndex = chooseChalkDustTile(north, east, south, west, quarterTurns);
            const uint32_t packed = packChalkDustSnapshotColor(tileIndex, quarterTurns);

            if (info.fromVoxel && baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                baseSystem.voxelWorld->setBlockWorld(
                    cell,
                    static_cast<uint32_t>(info.prototypeID),
                    packed
                );
                VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, cell);
                StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, worldIndex, glm::vec3(cell));
                return;
            }

            Entity& world = level.worlds[static_cast<size_t>(worldIndex)];
            if (info.instanceIndex >= world.instances.size()) return;
            world.instances[info.instanceIndex].color = glm::vec3(1.0f);
            StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, worldIndex, glm::vec3(cell));
        }

        void refreshChalkDustNeighborhood(BaseSystem& baseSystem,
                                          LevelContext& level,
                                          std::vector<Entity>& prototypes,
                                          int worldIndex,
                                          const glm::ivec3& centerCell) {
            refreshChalkDustCell(baseSystem, level, prototypes, worldIndex, centerCell);
            refreshChalkDustCell(baseSystem, level, prototypes, worldIndex, centerCell + glm::ivec3(0, 0, -1));
            refreshChalkDustCell(baseSystem, level, prototypes, worldIndex, centerCell + glm::ivec3(1, 0, 0));
            refreshChalkDustCell(baseSystem, level, prototypes, worldIndex, centerCell + glm::ivec3(0, 0, 1));
            refreshChalkDustCell(baseSystem, level, prototypes, worldIndex, centerCell + glm::ivec3(-1, 0, 0));
        }

        bool triggerGameplaySfx(BaseSystem& baseSystem, const char* fileName, float cooldownSeconds = 0.0f) {
            if (!fileName) return false;
            static std::unordered_map<std::string, double> s_lastTrigger;
            const std::string keyName(fileName);
            const double now = PlatformInput::GetTimeSeconds();
            auto it = s_lastTrigger.find(keyName);
            if (it != s_lastTrigger.end() && (now - it->second) < static_cast<double>(cooldownSeconds)) {
                return false;
            }

            // Primary path: preloaded/game-thread-safe one-shot audio from AudioSystem.
            if (AudioSystemLogic::TriggerGameplaySfx(baseSystem, keyName, 1.0f)) {
                s_lastTrigger[keyName] = now;
                return true;
            }

            // Optional fallback for debugging/legacy behavior.
            if (!readRegistryBool(baseSystem, "GameplaySfxFallbackToChuck", false)) {
                return false;
            }
            if (!baseSystem.audio || !baseSystem.audio->chuck) return false;
            const std::string scriptPath = std::string("Procedures/chuck/gameplay/") + fileName;
            std::vector<t_CKUINT> ids;
            bool ok = baseSystem.audio->chuck->compileFile(scriptPath, "", 1, FALSE, &ids);
            if (!ok || ids.empty()) return false;
            s_lastTrigger[keyName] = now;
            return true;
        }

        uint32_t hash3D(int x, int y, int z) {
            uint32_t ux = static_cast<uint32_t>(x) * 73856093u;
            uint32_t uy = static_cast<uint32_t>(y) * 19349663u;
            uint32_t uz = static_cast<uint32_t>(z) * 83492791u;
            uint32_t h = ux ^ uy ^ uz;
            h ^= (h >> 13);
            h *= 1274126177u;
            h ^= (h >> 16);
            return h;
        }

        void clearBlockDamageAt(int worldIndex, const glm::ivec3& cell) {
            blockDamageMap().erase(BlockDamageKey{worldIndex, cell});
        }

        bool queryRemovableBlockAtCell(const BaseSystem& baseSystem,
                                       const std::vector<Entity>& prototypes,
                                       int worldIndex,
                                       const glm::ivec3& cell,
                                       int* outPrototypeID) {
            if (outPrototypeID) *outPrototypeID = -1;

            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
                if (id > 0 && id < prototypes.size()) {
                    const int protoID = static_cast<int>(id);
                    if (isRemovableGameplayBlock(prototypes[static_cast<size_t>(protoID)])) {
                        if (outPrototypeID) *outPrototypeID = protoID;
                        return true;
                    }
                }
            }

            if (!baseSystem.level) return false;
            if (worldIndex < 0 || worldIndex >= static_cast<int>(baseSystem.level->worlds.size())) return false;
            const glm::vec3 cellPos = glm::vec3(cell);
            const Entity& world = baseSystem.level->worlds[static_cast<size_t>(worldIndex)];
            for (const EntityInstance& inst : world.instances) {
                if (glm::distance(inst.position, cellPos) > POSITION_EPSILON) continue;
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                if (!isRemovableGameplayBlock(prototypes[static_cast<size_t>(inst.prototypeID)])) continue;
                if (outPrototypeID) *outPrototypeID = inst.prototypeID;
                return true;
            }
            return false;
        }

        bool applyBlockDamageHit(int worldIndex,
                                 const glm::ivec3& cell,
                                 int prototypeID,
                                 int requiredHits,
                                 int* outHits) {
            const int clampedRequired = std::max(1, requiredHits);
            BlockDamageKey key;
            key.worldIndex = worldIndex;
            key.cell = cell;

            auto& damage = blockDamageMap();
            BlockDamageEntry& entry = damage[key];
            if (entry.prototypeID != prototypeID || entry.requiredHits != clampedRequired) {
                entry.hits = 0;
            }
            entry.prototypeID = prototypeID;
            entry.requiredHits = clampedRequired;
            entry.hits = std::min(entry.requiredHits, entry.hits + 1);
            entry.lastHitTime = PlatformInput::GetTimeSeconds();

            const int hitsNow = entry.hits;
            const bool shouldBreak = (hitsNow >= entry.requiredHits);
            if (shouldBreak) {
                damage.erase(key);
            }
            if (outHits) *outHits = hitsNow;
            return shouldBreak;
        }

        void decayBlockDamageOverTime(BaseSystem& baseSystem,
                                      std::vector<Entity>& prototypes) {
            auto& damage = blockDamageMap();
            if (damage.empty()) return;

            const bool repairEnabled = readRegistryBool(baseSystem, "BlockBreakRepairEnabled", true);
            if (!repairEnabled) return;
            const double repairTickSeconds = static_cast<double>(std::max(
                0.05f,
                readRegistryFloat(baseSystem, "BlockBreakRepairTickSeconds", 5.0f)
            ));
            const double now = PlatformInput::GetTimeSeconds();

            for (auto it = damage.begin(); it != damage.end();) {
                int presentPrototypeID = -1;
                if (!queryRemovableBlockAtCell(
                        baseSystem,
                        prototypes,
                        it->first.worldIndex,
                        it->first.cell,
                        &presentPrototypeID)) {
                    it = damage.erase(it);
                    continue;
                }
                if (it->second.prototypeID >= 0 && presentPrototypeID != it->second.prototypeID) {
                    it = damage.erase(it);
                    continue;
                }
                if (it->second.hits <= 0 || it->second.requiredHits <= 0) {
                    it = damage.erase(it);
                    continue;
                }

                const double elapsed = now - it->second.lastHitTime;
                if (elapsed < repairTickSeconds) {
                    ++it;
                    continue;
                }

                const int healTicks = static_cast<int>(std::floor(elapsed / repairTickSeconds));
                if (healTicks <= 0) {
                    ++it;
                    continue;
                }

                const int beforeHits = it->second.hits;
                it->second.hits = std::max(0, it->second.hits - healTicks);
                it->second.lastHitTime += static_cast<double>(healTicks) * repairTickSeconds;

                const bool changed = (it->second.hits != beforeHits);
                const glm::ivec3 changedCell = it->first.cell;
                const bool removeNow = (it->second.hits <= 0);
                if (removeNow) {
                    it = damage.erase(it);
                } else {
                    ++it;
                }

                if (changed && baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                    VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, changedCell);
                }
            }
        }

        struct BlockDamageMaskRenderEntry {
            glm::ivec3 cell = glm::ivec3(0);
            float progress = 0.0f;
        };

        void collectBlockDamageMaskEntries(BaseSystem& baseSystem,
                                           std::vector<Entity>& prototypes,
                                           std::vector<BlockDamageMaskRenderEntry>& outEntries) {
            outEntries.clear();

            auto& damage = blockDamageMap();
            if (damage.empty()) return;

            struct Candidate {
                float distance2 = 0.0f;
                BlockDamageMaskRenderEntry entry;
            };
            std::vector<Candidate> candidates;
            candidates.reserve(damage.size());

            const bool hasPlayer = (baseSystem.player != nullptr);
            const glm::vec3 cameraPos = hasPlayer ? baseSystem.player->cameraPosition : glm::vec3(0.0f);

            for (auto it = damage.begin(); it != damage.end();) {
                int presentPrototypeID = -1;
                if (!queryRemovableBlockAtCell(
                        baseSystem,
                        prototypes,
                        it->first.worldIndex,
                        it->first.cell,
                        &presentPrototypeID)) {
                    it = damage.erase(it);
                    continue;
                }
                if (it->second.prototypeID >= 0 && presentPrototypeID != it->second.prototypeID) {
                    it = damage.erase(it);
                    continue;
                }
                if (it->second.hits <= 0 || it->second.requiredHits <= 0) {
                    it = damage.erase(it);
                    continue;
                }

                Candidate c;
                c.entry.cell = it->first.cell;
                c.entry.progress = glm::clamp(
                    static_cast<float>(it->second.hits) / static_cast<float>(std::max(1, it->second.requiredHits)),
                    0.0f,
                    1.0f
                );
                if (hasPlayer) {
                    const glm::vec3 d = glm::vec3(c.entry.cell) - cameraPos;
                    c.distance2 = glm::dot(d, d);
                }
                candidates.push_back(c);
                ++it;
            }

            if (candidates.empty()) return;

            std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
                return a.distance2 < b.distance2;
            });

            const int maxEntries = std::clamp(
                readRegistryInt(baseSystem, "BlockBreakMaskMaxBlocks", BLOCK_DAMAGE_SHADER_MAX),
                1,
                BLOCK_DAMAGE_SHADER_MAX
            );
            const int emitCount = std::min(static_cast<int>(candidates.size()), maxEntries);
            outEntries.reserve(static_cast<size_t>(emitCount));
            for (int i = 0; i < emitCount; ++i) {
                outEntries.push_back(candidates[static_cast<size_t>(i)].entry);
            }
        }

        void uploadBlockDamageUniforms(const BaseSystem& baseSystem,
                                       const std::vector<BlockDamageMaskRenderEntry>& entries,
                                       const Shader& shader,
                                       bool enableMask) {
            const int enabledLoc = shader.findUniform("blockDamageEnabled");
            if (enabledLoc < 0) return;

            const int count = enableMask ? static_cast<int>(entries.size()) : 0;
            shader.setIntUniform(enabledLoc, (count > 0) ? 1 : 0);

            const int countLoc = shader.findUniform("blockDamageCount");
            if (countLoc >= 0) {
                shader.setIntUniform(countLoc, count);
            }

            const int gridLoc = shader.findUniform("blockDamageGrid");
            if (gridLoc >= 0) {
                const float grid = glm::clamp(
                    readRegistryFloat(baseSystem, "BlockBreakMaskGrid", 24.0f),
                    4.0f,
                    96.0f
                );
                shader.setFloatUniform(gridLoc, grid);
            }

            if (count <= 0) return;

            const int cellsLoc = shader.findUniform("blockDamageCells");
            const int progressLoc = shader.findUniform("blockDamageProgress");
            if (cellsLoc < 0 || progressLoc < 0) return;

            std::vector<int> packedCells(static_cast<size_t>(count) * 3u, 0);
            std::vector<float> progress(static_cast<size_t>(count), 0.0f);
            for (int i = 0; i < count; ++i) {
                const BlockDamageMaskRenderEntry& e = entries[static_cast<size_t>(i)];
                packedCells[static_cast<size_t>(i) * 3u + 0u] = e.cell.x;
                packedCells[static_cast<size_t>(i) * 3u + 1u] = e.cell.y;
                packedCells[static_cast<size_t>(i) * 3u + 2u] = e.cell.z;
                progress[static_cast<size_t>(i)] = glm::clamp(e.progress, 0.0f, 1.0f);
            }

            shader.setInt3ArrayUniform(cellsLoc, count, packedCells.data());
            shader.setFloatArrayUniform(progressLoc, count, progress.data());
        }

        struct RemovedBlockInfo {
            int prototypeID = -1;
            glm::vec3 color = glm::vec3(1.0f);
            uint32_t packedColor = 0u;
            bool fromVoxel = false;
            glm::ivec3 voxelCell = glm::ivec3(0);
            bool hasSourceCell = false;
            glm::ivec3 sourceCell = glm::ivec3(0);
        };

        bool RemoveBlockAtPosition(BaseSystem& baseSystem,
                                   LevelContext& level,
                                   std::vector<Entity>& prototypes,
                                   const glm::vec3& position,
                                   int worldIndex,
                                   RemovedBlockInfo* removedInfo) {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return false;
            Entity& world = level.worlds[worldIndex];
            glm::ivec3 cell = glm::ivec3(glm::round(position));
            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
                if (id != 0 && id < prototypes.size()) {
                    const Entity& proto = prototypes[id];
                    if (isRemovableGameplayBlock(proto)) {
                        const uint32_t packedColor = baseSystem.voxelWorld->getColorWorld(cell);
                        if (isSurfaceStonePrototypeName(proto.name)) {
                            const int pileCount = decodeSurfaceStonePileCount(packedColor);
                            if (pileCount > SURFACE_STONE_PILE_MIN) {
                                if (removedInfo) {
                                    removedInfo->prototypeID = static_cast<int>(id);
                                    removedInfo->color = unpackColor(packedColor);
                                    removedInfo->packedColor = withSurfaceStonePileCount(packedColor, SURFACE_STONE_PILE_MIN);
                                    removedInfo->fromVoxel = true;
                                    removedInfo->voxelCell = cell;
                                    removedInfo->hasSourceCell = true;
                                    removedInfo->sourceCell = cell;
                                }
                                baseSystem.voxelWorld->setBlockWorld(
                                    cell,
                                    id,
                                    withSurfaceStonePileCount(packedColor, pileCount - 1)
                                );
                                VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, cell);
                                return true;
                            }
                        }
                        if (removedInfo) {
                            uint32_t heldPackedColor = packedColor;
                            if (isSurfaceStonePrototypeName(proto.name)) {
                                heldPackedColor = withSurfaceStonePileCount(packedColor, SURFACE_STONE_PILE_MIN);
                            }
                            removedInfo->prototypeID = static_cast<int>(id);
                            removedInfo->color = unpackColor(packedColor);
                            removedInfo->packedColor = heldPackedColor;
                            removedInfo->fromVoxel = true;
                            removedInfo->voxelCell = cell;
                            removedInfo->hasSourceCell = true;
                            removedInfo->sourceCell = cell;
                        }
                        baseSystem.voxelWorld->setBlockWorld(cell, 0, 0);
                        TreeGenerationSystemLogic::NotifyPineLogRemoved(cell, static_cast<int>(id));
                        VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, cell);

                        const bool pruneLegacyInstances = readRegistryBool(baseSystem, "voxelEditPruneLegacyInstances", false);
                        if (pruneLegacyInstances) {
                            // Optional maintenance pass for legacy worlds that still contain duplicate
                            // chunkable instances alongside voxel data. Disabled by default because
                            // scanning large instance arrays can spike edit latency.
                            for (size_t i = 0; i < world.instances.size();) {
                                const EntityInstance& inst = world.instances[i];
                                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) {
                                    ++i;
                                    continue;
                                }
                                const Entity& instProto = prototypes[inst.prototypeID];
                                if (!isRemovableGameplayBlock(instProto)) {
                                    ++i;
                                    continue;
                                }
                                glm::ivec3 instCell = glm::ivec3(glm::round(inst.position));
                                if (instCell != cell) {
                                    ++i;
                                    continue;
                                }
                                world.instances[i] = world.instances.back();
                                world.instances.pop_back();
                            }
                        }
                        return true;
                    }
                }
            }

            for (size_t i = 0; i < world.instances.size(); ++i) {
                const EntityInstance& inst = world.instances[i];
                if (glm::distance(inst.position, position) > POSITION_EPSILON) continue;
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[inst.prototypeID];
                if (!isRemovableGameplayBlock(proto)) continue;
                if (removedInfo) {
                    uint32_t packed = packColor(inst.color);
                    if (isSurfaceStonePrototypeName(proto.name)) {
                        packed = withSurfaceStonePileCount(packed, SURFACE_STONE_PILE_MIN);
                    }
                    removedInfo->prototypeID = inst.prototypeID;
                    removedInfo->color = inst.color;
                    removedInfo->packedColor = packed;
                    removedInfo->hasSourceCell = true;
                    removedInfo->sourceCell = glm::ivec3(glm::round(inst.position));
                }
                world.instances[i] = world.instances.back();
                world.instances.pop_back();
                return true;
            }
            return false;
        }

        bool SpawnCavePotLoot(BaseSystem& baseSystem,
                              LevelContext& level,
                              std::vector<Entity>& prototypes,
                              int worldIndex,
                              const RemovedBlockInfo& removedBlock,
                              const PlayerContext& player) {
            if (removedBlock.prototypeID < 0 || removedBlock.prototypeID >= static_cast<int>(prototypes.size())) return false;
            if (!isCavePotPrototypeName(prototypes[static_cast<size_t>(removedBlock.prototypeID)].name)) return false;
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return false;

            struct BlueprintVariantPrototypes {
                int x = -1;
                int z = -1;
            };
            std::array<BlueprintVariantPrototypes, 5> blueprintVariants;
            for (const auto& proto : prototypes) {
                if (!proto.isBlock) continue;
                if (proto.name == "GrassCoverBlueprintAxeheadTexX") blueprintVariants[0].x = proto.prototypeID;
                else if (proto.name == "GrassCoverBlueprintAxeheadTexZ") blueprintVariants[0].z = proto.prototypeID;
                else if (proto.name == "GrassCoverBlueprintHiltTexX") blueprintVariants[1].x = proto.prototypeID;
                else if (proto.name == "GrassCoverBlueprintHiltTexZ") blueprintVariants[1].z = proto.prototypeID;
                else if (proto.name == "GrassCoverBlueprintPickaxeTexX") blueprintVariants[2].x = proto.prototypeID;
                else if (proto.name == "GrassCoverBlueprintPickaxeTexZ") blueprintVariants[2].z = proto.prototypeID;
                else if (proto.name == "GrassCoverBlueprintScytheTexX") blueprintVariants[3].x = proto.prototypeID;
                else if (proto.name == "GrassCoverBlueprintScytheTexZ") blueprintVariants[3].z = proto.prototypeID;
                else if (proto.name == "GrassCoverBlueprintSwordTexX") blueprintVariants[4].x = proto.prototypeID;
                else if (proto.name == "GrassCoverBlueprintSwordTexZ") blueprintVariants[4].z = proto.prototypeID;
            }
            std::vector<int> availableBlueprintVariants;
            for (int i = 0; i < static_cast<int>(blueprintVariants.size()); ++i) {
                if (blueprintVariants[static_cast<size_t>(i)].x >= 0
                    || blueprintVariants[static_cast<size_t>(i)].z >= 0) {
                    availableBlueprintVariants.push_back(i);
                }
            }
            if (availableBlueprintVariants.empty()) return false;

            const glm::ivec3 lootCell = removedBlock.fromVoxel
                ? removedBlock.voxelCell
                : glm::ivec3(glm::round(player.targetedBlockPosition));
            const uint32_t seed = hash3D(
                lootCell.x + static_cast<int>(baseSystem.frameIndex & 0x7fffffffu),
                lootCell.y + worldIndex * 31,
                lootCell.z - 17
            );

            int rewardPrototype = -1;
            const int variantIdx = availableBlueprintVariants[static_cast<size_t>(
                (seed >> 13u) % static_cast<uint32_t>(availableBlueprintVariants.size())
            )];
            const BlueprintVariantPrototypes& variant = blueprintVariants[static_cast<size_t>(variantIdx)];
            rewardPrototype = (((seed >> 18u) & 1u) == 0u) ? variant.x : variant.z;
            if (rewardPrototype < 0) rewardPrototype = (variant.x >= 0) ? variant.x : variant.z;
            const glm::vec3 rewardColor(1.0f);
            if (rewardPrototype < 0) return false;

            if (removedBlock.fromVoxel && baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                if (baseSystem.voxelWorld->getBlockWorld(lootCell) == 0u) {
                    baseSystem.voxelWorld->setBlockWorld(
                        lootCell,
                        static_cast<uint32_t>(rewardPrototype),
                        packColor(rewardColor)
                    );
                    VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, lootCell);
                    StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, worldIndex, glm::vec3(lootCell));
                }
            } else {
                const glm::vec3 rewardPos = glm::vec3(lootCell);
                if (!BlockSelectionSystemLogic::HasBlockAt(baseSystem, prototypes, worldIndex, rewardPos)) {
                    Entity& world = level.worlds[static_cast<size_t>(worldIndex)];
                    world.instances.push_back(
                        HostLogic::CreateInstance(baseSystem, rewardPrototype, rewardPos, rewardColor)
                    );
                    BlockSelectionSystemLogic::AddBlockToCache(
                        baseSystem,
                        prototypes,
                        worldIndex,
                        rewardPos,
                        rewardPrototype
                    );
                    StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, worldIndex, rewardPos);
                }
            }
            return true;
        }
    }

    void UpdateBlockCharge(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.player || !baseSystem.level) return;
        PlayerContext& player = *baseSystem.player;
        LevelContext& level = *baseSystem.level;
        decayBlockDamageOverTime(baseSystem, prototypes);
        const BuildModeType heldModeAtEntry = player.buildMode;
        if (isPickupHandMode(heldModeAtEntry)) {
            loadActiveHeldBlockForMode(player, heldModeAtEntry);
        }
        ScopeExit heldStateSync{[&]() {
            if (isPickupHandMode(heldModeAtEntry)) {
                saveActiveHeldBlockForMode(player, heldModeAtEntry);
            }
        }};
        GemContext* gems = baseSystem.gems ? baseSystem.gems.get() : nullptr;
        if (gems) {
            gems->placementPreviewActive = false;
            gems->placementPreviewPosition = glm::vec3(0.0f);
            gems->placementPreviewRenderYOffset = 0.0f;
        }
        constexpr bool kHatchetsEnabled = false;
        if (kHatchetsEnabled) {
            player.hatchetSelectedMaterial = glm::clamp(player.hatchetSelectedMaterial, 0, HATCHET_MATERIAL_COUNT - 1);
            player.hatchetPlacedMaterial = glm::clamp(player.hatchetPlacedMaterial, 0, HATCHET_MATERIAL_COUNT - 1);
            player.hatchetInventoryCount = sumHatchetInventory(player);
            if (player.hatchetInventoryCount <= 0) {
                player.hatchetHeld = false;
            }
            if (player.hatchetHeld
                && player.hatchetInventoryByMaterial[static_cast<size_t>(player.hatchetSelectedMaterial)] <= 0) {
                player.hatchetSelectedMaterial = firstAvailableHatchetMaterial(player);
            }
        } else {
            player.hatchetHeld = false;
            player.hatchetInventoryCount = 0;
            player.hatchetSelectedMaterial = HATCHET_MATERIAL_STONE;
            player.hatchetPlacedInWorld = false;
            player.hatchetPlacedCell = glm::ivec3(0);
            player.hatchetPlacedWorldIndex = -1;
            player.hatchetPlacedMaterial = HATCHET_MATERIAL_STONE;
            player.hatchetPlacedPosition = glm::vec3(0.0f);
            player.hatchetPlacedNormal = glm::vec3(0.0f, 1.0f, 0.0f);
            player.hatchetPlacedDirection = glm::vec3(1.0f, 0.0f, 0.0f);
            for (int i = 0; i < HATCHET_MATERIAL_COUNT; ++i) {
                player.hatchetInventoryByMaterial[static_cast<size_t>(i)] = 0;
            }
        }
        auto resetChargeState = [&]() {
            player.isChargingBlock = false;
            player.blockChargeReady = false;
            player.blockChargeValue = 0.0f;
            player.blockChargeAction = BlockChargeAction::None;
            player.blockChargeDecayTimer = 0.0f;
            player.blockChargeExecuteGraceTimer = 0.0f;
        };
        const float chargeDecaySeconds = std::max(
            0.01f,
            readRegistryFloat(baseSystem, "BlockChargeDecaySeconds", 1.0f));
        const float chargeExecuteGraceSeconds = std::max(
            0.0f,
            readRegistryFloat(baseSystem, "BlockChargeExecuteGraceSeconds", 1.0f));
        auto releaseChargeToTail = [&]() {
            if (!player.isChargingBlock) return;
            player.isChargingBlock = false;
            player.blockChargeDecayTimer = std::max(player.blockChargeDecayTimer, chargeDecaySeconds);
            player.blockChargeExecuteGraceTimer = std::max(player.blockChargeExecuteGraceTimer, chargeExecuteGraceSeconds);
        };
        auto updateChargeTail = [&]() {
            if (player.isChargingBlock) return;
            if (player.blockChargeAction == BlockChargeAction::None) return;
            if (player.blockChargeDecayTimer > 0.0f) {
                player.blockChargeDecayTimer = std::max(0.0f, player.blockChargeDecayTimer - dt);
                player.blockChargeValue = std::max(0.0f, player.blockChargeValue - (dt / chargeDecaySeconds));
            } else {
                player.blockChargeValue = 0.0f;
            }
            if (player.blockChargeExecuteGraceTimer > 0.0f) {
                player.blockChargeExecuteGraceTimer = std::max(0.0f, player.blockChargeExecuteGraceTimer - dt);
            }
            if (player.blockChargeExecuteGraceTimer <= 0.0f) {
                player.blockChargeReady = false;
            }
            if (player.blockChargeValue <= 0.001f && player.blockChargeExecuteGraceTimer <= 0.0f) {
                resetChargeState();
            }
        };
        auto releaseChargeUseToTail = [&]() {
            releaseChargeToTail();
            if (player.blockChargeAction != BlockChargeAction::None) {
                player.blockChargeDecayTimer = std::max(player.blockChargeDecayTimer, chargeDecaySeconds);
            }
            player.blockChargeReady = false;
            player.blockChargeExecuteGraceTimer = 0.0f;
        };
        auto triggerChargeFireInvertTail = [&]() {
            if (!baseSystem.colorEmotion) return;
            ColorEmotionContext& emotion = *baseSystem.colorEmotion;
            const float invertTailSeconds = std::max(
                0.05f,
                readRegistryFloat(baseSystem, "ColorEmotionChargeFireTailSeconds", 1.0f)
            );
            emotion.chargeFireInvertDuration = invertTailSeconds;
            emotion.chargeFireInvertTimer = invertTailSeconds;
            emotion.chargeFireInvertTail = 1.0f;
        };
        if (baseSystem.ui && baseSystem.ui->active) {
            resetChargeState();
            return;
        }
        if (OreMiningSystemLogic::IsMiningActive(baseSystem)) {
            resetChargeState();
            return;
        }
        if (GroundCraftingSystemLogic::IsRitualActive(baseSystem)) {
            resetChargeState();
            return;
        }
        if (GemChiselSystemLogic::IsChiselActive(baseSystem)) {
            resetChargeState();
            return;
        }
        auto clearHeldGem = [&]() {
            if (!gems) return;
            gems->blockModeHoldingGem = false;
            gems->heldDrop = GemDropState{};
            gems->placementPreviewActive = false;
            gems->placementPreviewPosition = glm::vec3(0.0f);
            gems->placementPreviewRenderYOffset = 0.0f;
        };
        auto clearPlacedHatchet = [&]() {
            player.hatchetPlacedInWorld = false;
            player.hatchetPlacedCell = glm::ivec3(0);
            player.hatchetPlacedWorldIndex = -1;
            player.hatchetPlacedMaterial = HATCHET_MATERIAL_STONE;
            player.hatchetPlacedPosition = glm::vec3(0.0f);
            player.hatchetPlacedNormal = glm::vec3(0.0f, 1.0f, 0.0f);
            player.hatchetPlacedDirection = glm::vec3(1.0f, 0.0f, 0.0f);
        };
        auto setHeldHatchetMaterial = [&](int material) {
            const int m = glm::clamp(material, 0, HATCHET_MATERIAL_COUNT - 1);
            for (int i = 0; i < HATCHET_MATERIAL_COUNT; ++i) {
                player.hatchetInventoryByMaterial[static_cast<size_t>(i)] = 0;
            }
            player.hatchetInventoryByMaterial[static_cast<size_t>(m)] = 1;
            player.hatchetInventoryCount = 1;
            player.hatchetSelectedMaterial = m;
            player.hatchetHeld = true;
        };

        const bool pickupHandMode = isPickupHandMode(player.buildMode);
        bool legacyDestroyMode = player.buildMode == BuildModeType::Destroy;
        bool interactionMode = pickupHandMode || legacyDestroyMode;
        bool fishingMode = player.buildMode == BuildModeType::Fishing;
        bool boulderingMode = player.buildMode == BuildModeType::Bouldering;
        const bool leafClimbEnabled = readRegistryBool(baseSystem, "LeafClimbEnabled", true);
        const bool leafBoulderingAnchorsEnabled = readRegistryBool(
            baseSystem,
            "LeafClimbBoulderingAnchorsEnabled",
            leafClimbEnabled);
        const int leafPrototypeID = leafBoulderingAnchorsEnabled ? resolveLeafPrototypeID(prototypes) : -1;
        const bool holdingChalkTool = player.isHoldingBlock
            && player.heldPrototypeID >= 0
            && player.heldPrototypeID < static_cast<int>(prototypes.size())
            && isChalkStickPrototypeName(prototypes[static_cast<size_t>(player.heldPrototypeID)].name);
        static bool bKeyDownLastFrame = false;
        const bool bKeyDown = PlatformInput::IsKeyDown(win, PlatformInput::Key::B);
        const bool bKeyJustPressed = bKeyDown && !bKeyDownLastFrame;
        bKeyDownLastFrame = bKeyDown;
        static bool throwWDownLastFrame = false;
        static bool throwRequireWReleaseForNextStart = false;
        static double throwLastWPressTime = -1000.0;
        static double throwLastLmbPressTime = -1000.0;
        static ThrownHeldBlockRuntime thrownHeldBlock;
        auto normalizeThrowWorldIndex = [&](int worldIndex) -> int {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) {
                worldIndex = level.activeWorldIndex;
            }
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) {
                worldIndex = 0;
            }
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) {
                return -1;
            }
            return worldIndex;
        };
        auto findWorldInstanceByID = [&](int worldIndex, int instanceID) -> EntityInstance* {
            if (instanceID <= 0) return nullptr;
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return nullptr;
            Entity& world = level.worlds[static_cast<size_t>(worldIndex)];
            for (EntityInstance& inst : world.instances) {
                if (inst.instanceID == instanceID) return &inst;
            }
            return nullptr;
        };
        auto clearThrownHeldVisual = [&]() {
            if (thrownHeldBlock.instanceID <= 0) return;
            const int worldIndex = normalizeThrowWorldIndex(thrownHeldBlock.worldIndex);
            if (worldIndex >= 0) {
                Entity& world = level.worlds[static_cast<size_t>(worldIndex)];
                for (size_t i = 0; i < world.instances.size(); ++i) {
                    if (world.instances[i].instanceID != thrownHeldBlock.instanceID) continue;
                    world.instances[i] = world.instances.back();
                    world.instances.pop_back();
                    break;
                }
            }
            thrownHeldBlock.instanceID = -1;
        };
        auto clearThrownHeldState = [&]() {
            clearThrownHeldVisual();
            thrownHeldBlock = ThrownHeldBlockRuntime{};
        };
        auto isSolidThrowCell = [&](int worldIndex, const glm::ivec3& cell, int ignoreInstanceID) -> bool {
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return false;
            auto prototypeIsSolid = [&](int prototypeID) -> bool {
                if (prototypeID < 0 || prototypeID >= static_cast<int>(prototypes.size())) return false;
                const Entity& proto = prototypes[static_cast<size_t>(prototypeID)];
                if (!proto.isBlock) return false;
                if (proto.name == "Water") return false;
                return true;
            };

            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t voxelId = baseSystem.voxelWorld->getBlockWorld(cell);
                if (voxelId > 0 && voxelId < prototypes.size() && prototypeIsSolid(static_cast<int>(voxelId))) {
                    return true;
                }
            }

            const Entity& world = level.worlds[static_cast<size_t>(worldIndex)];
            for (const EntityInstance& inst : world.instances) {
                if (inst.instanceID == ignoreInstanceID) continue;
                if (glm::ivec3(glm::round(inst.position)) != cell) continue;
                if (prototypeIsSolid(inst.prototypeID)) return true;
            }
            return false;
        };
        auto shatterThrownCavePotAtCell = [&](const glm::ivec3& cell) {
            if (!thrownHeldBlock.active) return;
            if (thrownHeldBlock.prototypeID < 0 || thrownHeldBlock.prototypeID >= static_cast<int>(prototypes.size())) return;
            const Entity& proto = prototypes[static_cast<size_t>(thrownHeldBlock.prototypeID)];
            if (!isCavePotPrototypeName(proto.name)) return;
            const int worldIndex = normalizeThrowWorldIndex(thrownHeldBlock.worldIndex);
            if (worldIndex < 0) return;

            RemovedBlockInfo removedBlock;
            removedBlock.prototypeID = thrownHeldBlock.prototypeID;
            removedBlock.color = thrownHeldBlock.color;
            removedBlock.packedColor = (thrownHeldBlock.packedColor & 0x00ffffffu) == 0u
                ? packColor(thrownHeldBlock.color)
                : thrownHeldBlock.packedColor;
            removedBlock.fromVoxel = true;
            removedBlock.voxelCell = cell;
            removedBlock.hasSourceCell = true;
            removedBlock.sourceCell = cell;
            (void)SpawnCavePotLoot(
                baseSystem,
                level,
                prototypes,
                worldIndex,
                removedBlock,
                player
            );
            triggerGameplaySfx(baseSystem, "break_stone.ck", 0.02f);
        };
        auto placeThrownHeldBlockAtCell = [&](const glm::ivec3& cell) -> bool {
            if (!thrownHeldBlock.active) return false;
            if (thrownHeldBlock.prototypeID < 0 || thrownHeldBlock.prototypeID >= static_cast<int>(prototypes.size())) return false;
            const int worldIndex = normalizeThrowWorldIndex(thrownHeldBlock.worldIndex);
            if (worldIndex < 0) return false;
            if (isSolidThrowCell(worldIndex, cell, thrownHeldBlock.instanceID)) return false;

            const Entity& proto = prototypes[static_cast<size_t>(thrownHeldBlock.prototypeID)];
            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled && proto.isChunkable) {
                uint32_t packedColor = thrownHeldBlock.packedColor;
                if ((packedColor & 0x00ffffffu) == 0u) {
                    packedColor = packColor(thrownHeldBlock.color);
                }
                if (isSurfaceStonePrototypeName(proto.name)) {
                    packedColor = withSurfaceStonePileCount(packedColor, SURFACE_STONE_PILE_MIN);
                }
                baseSystem.voxelWorld->setBlockWorld(
                    cell,
                    static_cast<uint32_t>(thrownHeldBlock.prototypeID),
                    packedColor
                );
                VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, cell);
                StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, worldIndex, glm::vec3(cell));
            } else {
                const glm::vec3 placePos = glm::vec3(cell);
                if (BlockSelectionSystemLogic::HasBlockAt(baseSystem, prototypes, worldIndex, placePos)) return false;
                Entity& world = level.worlds[static_cast<size_t>(worldIndex)];
                world.instances.push_back(HostLogic::CreateInstance(
                    baseSystem,
                    thrownHeldBlock.prototypeID,
                    placePos,
                    thrownHeldBlock.color
                ));
                BlockSelectionSystemLogic::AddBlockToCache(
                    baseSystem,
                    prototypes,
                    worldIndex,
                    placePos,
                    thrownHeldBlock.prototypeID
                );
                StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, worldIndex, placePos);
            }
            if (isChalkDustPrototypeID(prototypes, thrownHeldBlock.prototypeID)) {
                refreshChalkDustNeighborhood(baseSystem, level, prototypes, worldIndex, cell);
            }
            if (proto.name == "AudioVisualizer") {
                RayTracedAudioSystemLogic::InvalidateSourceCache(baseSystem);
            }
            triggerGameplaySfx(baseSystem, "place_block.ck", 0.02f);
            return true;
        };
        auto ensureThrownHeldVisual = [&]() {
            if (!thrownHeldBlock.active) return;
            thrownHeldBlock.worldIndex = normalizeThrowWorldIndex(thrownHeldBlock.worldIndex);
            if (thrownHeldBlock.worldIndex < 0) {
                clearThrownHeldState();
                return;
            }
            EntityInstance* visual = findWorldInstanceByID(thrownHeldBlock.worldIndex, thrownHeldBlock.instanceID);
            if (!visual) {
                EntityInstance inst = HostLogic::CreateInstance(
                    baseSystem,
                    thrownHeldBlock.prototypeID,
                    thrownHeldBlock.position,
                    thrownHeldBlock.color
                );
                if (inst.instanceID > 0) {
                    inst.name = "__ThrownHeldBlockVisual";
                    thrownHeldBlock.instanceID = inst.instanceID;
                    level.worlds[static_cast<size_t>(thrownHeldBlock.worldIndex)].instances.push_back(inst);
                    visual = findWorldInstanceByID(thrownHeldBlock.worldIndex, thrownHeldBlock.instanceID);
                } else {
                    thrownHeldBlock.instanceID = -1;
                }
            }
            if (visual) {
                visual->position = thrownHeldBlock.position;
                visual->color = thrownHeldBlock.color;
            }
        };
        auto updateThrownHeldBlock = [&]() {
            if (!thrownHeldBlock.active) return;
            if (thrownHeldBlock.prototypeID < 0 || thrownHeldBlock.prototypeID >= static_cast<int>(prototypes.size())) {
                clearThrownHeldState();
                return;
            }
            thrownHeldBlock.worldIndex = normalizeThrowWorldIndex(thrownHeldBlock.worldIndex);
            if (thrownHeldBlock.worldIndex < 0) {
                clearThrownHeldState();
                return;
            }
            ensureThrownHeldVisual();
            if (dt <= 0.0f) return;

            const float gravity = -std::abs(readRegistryFloat(baseSystem, "HeldThrowGravity", 18.0f));
            const float maxSpeed = std::max(1.0f, readRegistryFloat(baseSystem, "HeldThrowMaxSpeed", 24.0f));
            const float maxStepDistance = std::max(0.04f, readRegistryFloat(baseSystem, "HeldThrowMaxStepDistance", 0.20f));
            const float maxLifetime = std::max(0.5f, readRegistryFloat(baseSystem, "HeldThrowLifetimeSeconds", 10.0f));

            thrownHeldBlock.age += dt;
            if (thrownHeldBlock.age >= maxLifetime) {
                const glm::ivec3 expiryCell = glm::ivec3(glm::round(thrownHeldBlock.position));
                const bool thrownIsCavePot =
                    isCavePotPrototypeName(prototypes[static_cast<size_t>(thrownHeldBlock.prototypeID)].name);
                if (thrownIsCavePot) {
                    shatterThrownCavePotAtCell(expiryCell);
                } else {
                    (void)placeThrownHeldBlockAtCell(expiryCell);
                }
                clearThrownHeldState();
                return;
            }

            const float frameDistance = glm::length(thrownHeldBlock.velocity) * dt;
            int substeps = static_cast<int>(std::ceil(frameDistance / maxStepDistance));
            substeps = std::clamp(substeps, 1, 32);
            const float stepDt = dt / static_cast<float>(substeps);
            bool collided = false;
            glm::ivec3 collisionCell = glm::ivec3(0);
            glm::ivec3 lastFreeCell = glm::ivec3(glm::round(thrownHeldBlock.position));
            glm::vec3 collisionDir = thrownHeldBlock.velocity;

            for (int step = 0; step < substeps; ++step) {
                thrownHeldBlock.velocity.y += gravity * stepDt;
                const float speed = glm::length(thrownHeldBlock.velocity);
                if (speed > maxSpeed) {
                    thrownHeldBlock.velocity *= (maxSpeed / speed);
                }
                const glm::vec3 stepDir = thrownHeldBlock.velocity * stepDt;
                const glm::vec3 nextPos = thrownHeldBlock.position + stepDir;
                const glm::ivec3 nextCell = glm::ivec3(glm::round(nextPos));
                if (isSolidThrowCell(thrownHeldBlock.worldIndex, nextCell, thrownHeldBlock.instanceID)) {
                    collided = true;
                    collisionCell = nextCell;
                    collisionDir = stepDir;
                    break;
                }
                thrownHeldBlock.position = nextPos;
                lastFreeCell = nextCell;
            }

            ensureThrownHeldVisual();

            if (!collided) return;

            glm::ivec3 travelAxis(0);
            const glm::vec3 dir = normalizeOrDefault(collisionDir, glm::vec3(0.0f, -1.0f, 0.0f));
            const glm::vec3 absDir = glm::abs(dir);
            if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
                travelAxis.x = dir.x >= 0.0f ? 1 : -1;
            } else if (absDir.y >= absDir.x && absDir.y >= absDir.z) {
                travelAxis.y = dir.y >= 0.0f ? 1 : -1;
            } else {
                travelAxis.z = dir.z >= 0.0f ? 1 : -1;
            }

            const std::array<glm::ivec3, 9> candidateCells = {
                lastFreeCell,
                collisionCell - travelAxis,
                lastFreeCell + glm::ivec3(0, 1, 0),
                collisionCell + glm::ivec3(0, 1, 0),
                collisionCell + glm::ivec3(1, 0, 0),
                collisionCell + glm::ivec3(-1, 0, 0),
                collisionCell + glm::ivec3(0, 0, 1),
                collisionCell + glm::ivec3(0, 0, -1),
                glm::ivec3(glm::round(thrownHeldBlock.position))
            };

            const bool thrownIsCavePot =
                isCavePotPrototypeName(prototypes[static_cast<size_t>(thrownHeldBlock.prototypeID)].name);
            if (thrownIsCavePot) {
                glm::ivec3 shatterCell = lastFreeCell;
                for (const glm::ivec3& candidate : candidateCells) {
                    if (!isSolidThrowCell(thrownHeldBlock.worldIndex, candidate, thrownHeldBlock.instanceID)) {
                        shatterCell = candidate;
                        break;
                    }
                }
                shatterThrownCavePotAtCell(shatterCell);
                clearThrownHeldState();
                return;
            }

            bool placed = false;
            for (const glm::ivec3& candidate : candidateCells) {
                if (placeThrownHeldBlockAtCell(candidate)) {
                    placed = true;
                    break;
                }
            }

            if (!placed && !player.isHoldingBlock) {
                player.isHoldingBlock = true;
                player.heldPrototypeID = thrownHeldBlock.prototypeID;
                player.heldBlockColor = thrownHeldBlock.color;
                player.heldPackedColor = thrownHeldBlock.packedColor;
                player.heldHasSourceCell = thrownHeldBlock.hasSourceCell;
                player.heldSourceCell = thrownHeldBlock.sourceCell;
                triggerGameplaySfx(baseSystem, "pickup_block.ck", 0.02f);
            }
            clearThrownHeldState();
        };
        updateThrownHeldBlock();
        const bool holdingInspectableBook = player.isHoldingBlock
            && player.heldPrototypeID >= 0
            && player.heldPrototypeID < static_cast<int>(prototypes.size())
            && BookSystemLogic::IsBookPrototypeName(prototypes[static_cast<size_t>(player.heldPrototypeID)].name);
        if (holdingInspectableBook && player.bookInspectActive) {
            resetChargeState();
            return;
        }
        auto worldMatches = [](int lhs, int rhs) {
            return lhs < 0 || rhs < 0 || lhs == rhs;
        };
        auto isLatchedAnchorCell = [&](const glm::ivec3& cell, int worldIndex) {
            if (player.boulderPrimaryLatched
                && player.boulderPrimaryCell == cell
                && worldMatches(worldIndex, player.boulderPrimaryWorldIndex)) {
                return true;
            }
            if (player.boulderSecondaryLatched
                && player.boulderSecondaryCell == cell
                && worldMatches(worldIndex, player.boulderSecondaryWorldIndex)) {
                return true;
            }
            return false;
        };
        auto targetIsLatchedAnchorSuppressed = [&]() {
            if (player.buildMode == BuildModeType::Bouldering) return false;
            if (!(player.boulderPrimaryLatched || player.boulderSecondaryLatched)) return false;
            if (!player.hasBlockTarget) return false;
            const glm::ivec3 targetCell = glm::ivec3(glm::round(player.targetedBlockPosition));
            return isLatchedAnchorCell(targetCell, player.targetedWorldIndex);
        };
        auto releaseLatchedAnchorAt = [&](const glm::ivec3& cell, int worldIndex) {
            bool released = false;
            if (player.boulderPrimaryLatched
                && player.boulderPrimaryCell == cell
                && worldMatches(worldIndex, player.boulderPrimaryWorldIndex)) {
                player.boulderPrimaryLatched = false;
                player.boulderPrimaryRestLength = 0.0f;
                player.boulderPrimaryWorldIndex = -1;
                player.boulderPrimaryNormal = glm::vec3(0.0f, 0.0f, 1.0f);
                released = true;
            }
            if (player.boulderSecondaryLatched
                && player.boulderSecondaryCell == cell
                && worldMatches(worldIndex, player.boulderSecondaryWorldIndex)) {
                player.boulderSecondaryLatched = false;
                player.boulderSecondaryRestLength = 0.0f;
                player.boulderSecondaryWorldIndex = -1;
                player.boulderSecondaryNormal = glm::vec3(0.0f, 0.0f, 1.0f);
                released = true;
            }
            if (released) {
                player.boulderLaunchVelocity = glm::vec3(0.0f);
            }
            return released;
        };
        if (fishingMode) {
            // Fishing mode owns the charge meter semantics; avoid stomping state here.
            return;
        }
        if (boulderingMode) {
            auto validateLatch = [&](bool& latched, const glm::ivec3& cell, int worldIndex, float& restLength) {
                if (!latched) return;
                if (!hasBoulderingAnchorAtCell(
                        baseSystem,
                        level,
                        prototypes,
                        cell,
                        worldIndex,
                        leafBoulderingAnchorsEnabled,
                        leafPrototypeID)) {
                    latched = false;
                    restLength = 0.0f;
                }
            };
            validateLatch(player.boulderPrimaryLatched, player.boulderPrimaryCell, player.boulderPrimaryWorldIndex, player.boulderPrimaryRestLength);
            validateLatch(player.boulderSecondaryLatched, player.boulderSecondaryCell, player.boulderSecondaryWorldIndex, player.boulderSecondaryRestLength);

            const float boulderChargeSeconds = std::max(0.05f, readRegistryFloat(baseSystem, "BoulderingChargeSeconds", CHARGE_TIME_PICKUP));
            const float boulderLatchMaxDistance = std::max(0.25f, readRegistryFloat(baseSystem, "BoulderingLatchMaxDistance", 2.5f));
            const float boulderRestMin = std::max(0.05f, readRegistryFloat(baseSystem, "BoulderingRestLengthMin", 0.22f));
            const float boulderRestMax = std::max(boulderRestMin, readRegistryFloat(baseSystem, "BoulderingRestLengthMax", 1.6f));
            const float boulderRestTarget = glm::clamp(readRegistryFloat(baseSystem, "BoulderingLatchRestLength", 0.26f), boulderRestMin, boulderRestMax);
            const float boulderSnapBlend = glm::clamp(readRegistryFloat(baseSystem, "BoulderingLatchSnapBlend", 0.62f), 0.0f, 1.0f);
            const bool boulderSwappedControls = player.blockChargeControlsSwapped;
            const bool primaryChargeDown = boulderSwappedControls ? player.leftMouseDown : player.rightMouseDown;
            const bool primaryChargePressed = boulderSwappedControls ? player.leftMousePressed : player.rightMousePressed;
            const bool primaryExecutePressed = boulderSwappedControls ? player.rightMousePressed : player.leftMousePressed;
            const bool secondaryChargeDown = boulderSwappedControls ? player.rightMouseDown : player.leftMouseDown;
            const bool secondaryChargePressed = boulderSwappedControls ? player.rightMousePressed : player.leftMousePressed;
            const bool secondaryExecutePressed = boulderSwappedControls ? player.leftMousePressed : player.rightMousePressed;

            BlockChargeAction activeAction = player.blockChargeAction;
            if (activeAction != BlockChargeAction::BoulderPrimary && activeAction != BlockChargeAction::BoulderSecondary) {
                activeAction = BlockChargeAction::None;
            }
            if (activeAction == BlockChargeAction::None) {
                bool wantsPrimaryCharge = primaryChargeDown && !secondaryChargeDown;
                bool wantsSecondaryCharge = secondaryChargeDown && !primaryChargeDown;
                if (primaryChargePressed && !secondaryChargeDown) wantsPrimaryCharge = true;
                if (secondaryChargePressed && !primaryChargeDown) wantsSecondaryCharge = true;
                if (wantsPrimaryCharge) activeAction = BlockChargeAction::BoulderPrimary;
                else if (wantsSecondaryCharge) activeAction = BlockChargeAction::BoulderSecondary;
            }

            bool wantsCharge = false;
            if (activeAction == BlockChargeAction::BoulderPrimary) wantsCharge = primaryChargeDown;
            else if (activeAction == BlockChargeAction::BoulderSecondary) wantsCharge = secondaryChargeDown;

            if (wantsCharge) {
                if (!player.isChargingBlock || player.blockChargeAction != activeAction) {
                    player.blockChargeValue = 0.0f;
                    player.blockChargeReady = false;
                }
                player.isChargingBlock = true;
                player.blockChargeAction = activeAction;
                player.blockChargeDecayTimer = chargeDecaySeconds;
                player.blockChargeExecuteGraceTimer = 0.0f;
                player.blockChargeValue += dt / boulderChargeSeconds;
                if (player.blockChargeValue >= 1.0f) {
                    player.blockChargeValue = 1.0f;
                    player.blockChargeReady = true;
                }
            } else {
                releaseChargeToTail();
                updateChargeTail();
            }

            const bool hasGraceWindow = player.blockChargeExecuteGraceTimer > 0.0f;
            const bool executePrimary = player.blockChargeAction == BlockChargeAction::BoulderPrimary
                && primaryExecutePressed
                && (primaryChargeDown || hasGraceWindow);
            const bool executeSecondary = player.blockChargeAction == BlockChargeAction::BoulderSecondary
                && secondaryExecutePressed
                && (secondaryChargeDown || hasGraceWindow);

            auto tryLatchHand = [&](bool primaryHand) -> bool {
                if (!player.hasBlockTarget) return false;
                if (glm::length(player.targetedBlockNormal) < 0.1f) return false;
                const glm::vec3 eye = cameraEyePosition(baseSystem, player);
                if (glm::distance(eye, player.targetedBlockPosition) > boulderLatchMaxDistance) return false;

                glm::ivec3 targetCell(0);
                bool fromVoxel = false;
                const int targetPrototypeID = resolveTargetPrototypeID(baseSystem, level, prototypes, player, &targetCell, &fromVoxel);
                (void)fromVoxel;
                if (!isBoulderingAnchorPrototypeID(
                        prototypes,
                        targetPrototypeID,
                        leafBoulderingAnchorsEnabled,
                        leafPrototypeID)) {
                    return false;
                }

                glm::vec3 normal = player.targetedBlockNormal;
                if (glm::length(normal) < 0.01f) normal = glm::vec3(0.0f, 0.0f, 1.0f);
                normal = glm::normalize(normal);
                const glm::vec3 anchorPos = player.targetedBlockPosition + normal * 0.45f;
                const float restLength = boulderRestTarget;
                const glm::vec3 snapTarget = anchorPos + normal * restLength;
                player.cameraPosition = glm::mix(player.cameraPosition, snapTarget, boulderSnapBlend);
                // Keep latch at wall-stone eye level for consistent lateral climbing controls.
                player.cameraPosition.y = player.targetedBlockPosition.y;
                player.verticalVelocity = 0.0f;
                player.boulderLaunchVelocity = glm::vec3(0.0f);
                if (primaryHand) {
                    player.boulderPrimaryLatched = true;
                    player.boulderPrimaryAnchor = anchorPos;
                    player.boulderPrimaryNormal = normal;
                    player.boulderPrimaryCell = targetCell;
                    player.boulderPrimaryRestLength = restLength;
                    player.boulderPrimaryWorldIndex = player.targetedWorldIndex;
                } else {
                    player.boulderSecondaryLatched = true;
                    player.boulderSecondaryAnchor = anchorPos;
                    player.boulderSecondaryNormal = normal;
                    player.boulderSecondaryCell = targetCell;
                    player.boulderSecondaryRestLength = restLength;
                    player.boulderSecondaryWorldIndex = player.targetedWorldIndex;
                }
                player.onGround = false;
                triggerGameplaySfx(baseSystem, "pickup_block.ck", 0.02f);
                return true;
            };

            if ((executePrimary || executeSecondary) && player.blockChargeReady) {
                triggerChargeFireInvertTail();
                if (executePrimary) {
                    (void)tryLatchHand(true);
                } else {
                    (void)tryLatchHand(false);
                }
                releaseChargeUseToTail();
            }
            return;
        }
        if (!interactionMode) {
            resetChargeState();
            return;
        }
        const Entity* audioVisualizerProto = nullptr;
        for (const auto& proto : prototypes) {
            if (proto.name == "AudioVisualizer") {
                audioVisualizerProto = &proto;
                break;
            }
        }

        auto tryPlaceHeldBlock = [&](PlayerContext& playerCtx) {
            if (!playerCtx.leftMousePressed) return;
            if (!playerCtx.hasBlockTarget || glm::length(playerCtx.targetedBlockNormal) < 0.1f) return;
            if (playerCtx.targetedWorldIndex < 0 || playerCtx.targetedWorldIndex >= static_cast<int>(level.worlds.size())) return;
            if (playerCtx.heldPrototypeID < 0 || playerCtx.heldPrototypeID >= static_cast<int>(prototypes.size())) return;
            const Entity& heldProto = prototypes[playerCtx.heldPrototypeID];
            const bool heldIsChalkDust = isChalkDustPrototypeID(prototypes, playerCtx.heldPrototypeID);
            const bool heldIsComputer = (heldProto.name == "Computer");

            if (isSurfaceStonePrototypeName(heldProto.name)) {
                glm::ivec3 targetCell(0);
                bool targetFromVoxel = false;
                const int targetPrototypeID = resolveTargetPrototypeID(
                    baseSystem,
                    level,
                    prototypes,
                    playerCtx,
                    &targetCell,
                    &targetFromVoxel
                );
                (void)targetFromVoxel;
                if (targetPrototypeID >= 0 && targetPrototypeID < static_cast<int>(prototypes.size())) {
                    const Entity& targetProto = prototypes[static_cast<size_t>(targetPrototypeID)];
                    if (baseSystem.voxelWorld
                        && baseSystem.voxelWorld->enabled
                        && targetCell == glm::ivec3(glm::round(playerCtx.targetedBlockPosition))
                        && isSurfaceStonePrototypeName(targetProto.name)
                        && targetProto.prototypeID == playerCtx.heldPrototypeID) {
                        const uint32_t packed = baseSystem.voxelWorld->getColorWorld(targetCell);
                        const int pileCount = decodeSurfaceStonePileCount(packed);
                        if (pileCount < SURFACE_STONE_PILE_MAX) {
                            uint32_t basePacked = packed;
                            if ((basePacked & 0x00ffffffu) == 0u) {
                                basePacked = packColor(playerCtx.heldBlockColor);
                            }
                            baseSystem.voxelWorld->setBlockWorld(
                                targetCell,
                                static_cast<uint32_t>(targetPrototypeID),
                                withSurfaceStonePileCount(basePacked, pileCount + 1)
                            );
                            VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, targetCell);
                            StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, playerCtx.targetedWorldIndex, glm::vec3(targetCell));
                            triggerGameplaySfx(baseSystem, "place_block.ck", 0.02f);
                            playerCtx.isHoldingBlock = false;
                            playerCtx.heldPrototypeID = -1;
                            playerCtx.heldPackedColor = 0u;
                            playerCtx.heldHasSourceCell = false;
                            playerCtx.heldSourceCell = glm::ivec3(0);
                            return;
                        }
                        return;
                    }
                    if (kHatchetsEnabled && isStickPrototypeName(targetProto.name)) {
                        RemovedBlockInfo removedStick;
                        if (RemoveBlockAtPosition(
                                baseSystem,
                                level,
                                prototypes,
                                playerCtx.targetedBlockPosition,
                                playerCtx.targetedWorldIndex,
                                &removedStick)
                            && removedStick.prototypeID >= 0
                            && removedStick.prototypeID < static_cast<int>(prototypes.size())
                            && isStickPrototypeName(prototypes[static_cast<size_t>(removedStick.prototypeID)].name)) {
                            bool materialRecognized = false;
                            int material = hatchetMaterialFromStonePrototypeName(heldProto.name, &materialRecognized);
                            if (!materialRecognized) {
                                material = detectHatchetMaterialFromColor(playerCtx.heldBlockColor);
                            }
                            material = glm::clamp(material, 0, HATCHET_MATERIAL_COUNT - 1);

                            const glm::vec3 surfaceNormal = normalizeOrDefault(playerCtx.targetedBlockNormal, glm::vec3(0.0f, 1.0f, 0.0f));
                            const glm::vec3 forward = cameraForwardDirection(playerCtx);
                            playerCtx.hatchetPlacedInWorld = true;
                            playerCtx.hatchetPlacedCell = targetCell;
                            playerCtx.hatchetPlacedWorldIndex = playerCtx.targetedWorldIndex;
                            playerCtx.hatchetPlacedNormal = surfaceNormal;
                            playerCtx.hatchetPlacedDirection = projectDirectionOnSurface(forward, surfaceNormal);
                            playerCtx.hatchetPlacedPosition = glm::vec3(targetCell) - surfaceNormal * 0.47f;
                            playerCtx.hatchetPlacedMaterial = material;
                            playerCtx.hatchetSelectedMaterial = material;
                            playerCtx.hatchetHeld = false;
                            for (int i = 0; i < HATCHET_MATERIAL_COUNT; ++i) {
                                playerCtx.hatchetInventoryByMaterial[static_cast<size_t>(i)] = 0;
                            }
                            playerCtx.hatchetInventoryCount = 0;
                            playerCtx.isHoldingBlock = false;
                            playerCtx.heldPrototypeID = -1;
                            playerCtx.heldPackedColor = 0u;
                            playerCtx.heldHasSourceCell = false;
                            playerCtx.heldSourceCell = glm::ivec3(0);
                            triggerGameplaySfx(baseSystem, "place_block.ck", 0.02f);
                            return;
                        }
                    }
                    if (isFlowerPrototypeName(targetProto.name)) {
                        RemovedBlockInfo removedFlower;
                        if (RemoveBlockAtPosition(
                                baseSystem,
                                level,
                                prototypes,
                                playerCtx.targetedBlockPosition,
                                playerCtx.targetedWorldIndex,
                                &removedFlower)) {
                            const int variant = detectFlowerPetalVariantFromColor(removedFlower.color);
                            glm::ivec3 petalsCell = targetCell;
                            if (removedFlower.fromVoxel) {
                                petalsCell = removedFlower.voxelCell;
                            }
                            const bool preferX = (((petalsCell.x ^ petalsCell.y ^ petalsCell.z) & 1) == 0);
                            const int petalsPrototypeID = resolveFlowerPetalsPrototypeID(prototypes, variant, preferX);
                            if (petalsPrototypeID >= 0) {
                                if (removedFlower.fromVoxel && baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                                    baseSystem.voxelWorld->setBlockWorld(
                                        petalsCell,
                                        static_cast<uint32_t>(petalsPrototypeID),
                                        packColor(glm::vec3(1.0f))
                                    );
                                    VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, petalsCell);
                                    StructureCaptureSystemLogic::NotifyBlockChanged(
                                        baseSystem,
                                        playerCtx.targetedWorldIndex,
                                        glm::vec3(petalsCell)
                                    );
                                } else {
                                    BlockSelectionSystemLogic::RemoveBlockFromCache(
                                        baseSystem,
                                        prototypes,
                                        playerCtx.targetedWorldIndex,
                                        playerCtx.targetedBlockPosition
                                    );
                                    const glm::vec3 petalsPos = glm::vec3(petalsCell);
                                    Entity& world = level.worlds[static_cast<size_t>(playerCtx.targetedWorldIndex)];
                                    world.instances.push_back(HostLogic::CreateInstance(
                                        baseSystem,
                                        petalsPrototypeID,
                                        petalsPos,
                                        glm::vec3(1.0f)
                                    ));
                                    BlockSelectionSystemLogic::AddBlockToCache(
                                        baseSystem,
                                        prototypes,
                                        playerCtx.targetedWorldIndex,
                                        petalsPos,
                                        petalsPrototypeID
                                    );
                                    StructureCaptureSystemLogic::NotifyBlockChanged(
                                        baseSystem,
                                        playerCtx.targetedWorldIndex,
                                        petalsPos
                                    );
                                }
                                triggerGameplaySfx(baseSystem, "place_block.ck", 0.02f);
                                return;
                            }
                        }
                    }
                }
            }

            glm::vec3 placePos = playerCtx.targetedBlockPosition + playerCtx.targetedBlockNormal;
            if (BlockSelectionSystemLogic::HasBlockAt(baseSystem, prototypes, playerCtx.targetedWorldIndex, placePos)) return;
            glm::ivec3 placedCell = glm::ivec3(glm::round(placePos));

            bool placedInVoxel = false;
            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled && heldProto.isChunkable) {
                glm::ivec3 placeCell = glm::ivec3(glm::round(placePos));
                uint32_t packedColor = packColor(playerCtx.heldBlockColor);
                if (isSurfaceStonePrototypeName(heldProto.name)) {
                    packedColor = withSurfaceStonePileCount(packedColor, SURFACE_STONE_PILE_MIN);
                }
                baseSystem.voxelWorld->setBlockWorld(
                    placeCell,
                    static_cast<uint32_t>(playerCtx.heldPrototypeID),
                    packedColor
                );
                VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, placeCell);
                StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, playerCtx.targetedWorldIndex, glm::vec3(placeCell));
                placedCell = placeCell;
                placedInVoxel = true;
            }

            if (!placedInVoxel) {
                Entity& world = level.worlds[playerCtx.targetedWorldIndex];
                world.instances.push_back(HostLogic::CreateInstance(baseSystem, playerCtx.heldPrototypeID, placePos, playerCtx.heldBlockColor));
                BlockSelectionSystemLogic::AddBlockToCache(baseSystem, prototypes, playerCtx.targetedWorldIndex, placePos, playerCtx.heldPrototypeID);
                StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, playerCtx.targetedWorldIndex, placePos);
            }
            if (heldIsChalkDust) {
                refreshChalkDustNeighborhood(baseSystem, level, prototypes, playerCtx.targetedWorldIndex, placedCell);
            }
            if (heldIsComputer && baseSystem.ui) {
                baseSystem.ui->computerCacheBuilt = false;
            }
            if (audioVisualizerProto && playerCtx.heldPrototypeID == audioVisualizerProto->prototypeID) {
                RayTracedAudioSystemLogic::InvalidateSourceCache(baseSystem);
            }
            triggerGameplaySfx(baseSystem, "place_block.ck", 0.02f);
            playerCtx.isHoldingBlock = false;
            playerCtx.heldPrototypeID = -1;
            playerCtx.heldPackedColor = 0u;
            playerCtx.heldHasSourceCell = false;
            playerCtx.heldSourceCell = glm::ivec3(0);
        };

        auto tryPlaceHeldGem = [&](PlayerContext& playerCtx) {
            if (!gems || !gems->blockModeHoldingGem) return;
            if (!playerCtx.hasBlockTarget || glm::length(playerCtx.targetedBlockNormal) < 0.1f) return;
            if (playerCtx.targetedWorldIndex < 0 || playerCtx.targetedWorldIndex >= static_cast<int>(level.worlds.size())) return;

            glm::ivec3 targetCell = glm::ivec3(glm::round(playerCtx.targetedBlockPosition));
            bool targetFromVoxel = false;
            const int targetPrototypeID = resolveTargetPrototypeID(
                baseSystem,
                level,
                prototypes,
                playerCtx,
                &targetCell,
                &targetFromVoxel
            );
            (void)targetFromVoxel;
            glm::ivec3 blueprintCell(0);
            bool targetIsBlueprint = false;
            if (targetPrototypeID >= 0 && targetPrototypeID < static_cast<int>(prototypes.size())) {
                const Entity& targetProto = prototypes[static_cast<size_t>(targetPrototypeID)];
                if (isBlueprintPrototypeName(targetProto.name)) {
                    targetIsBlueprint = true;
                }
            }
            if (!targetIsBlueprint && findBlueprintInstanceAtTarget(level, prototypes, playerCtx, &blueprintCell)) {
                targetIsBlueprint = true;
                targetCell = blueprintCell;
            }

            // Chipping station: placing a raw gem on a natural surface stone converts it into
            // a placeable "ingot" block (same stone-pebble shape) held in hand.
            if (targetPrototypeID >= 0 && targetPrototypeID < static_cast<int>(prototypes.size())) {
                const Entity& targetProto = prototypes[static_cast<size_t>(targetPrototypeID)];
                if (isStickPrototypeName(targetProto.name) && playerCtx.leftMousePressed) {
                    std::vector<glm::ivec3> stencilHeadVoxels;
                    if (!buildPickaxeHeadVoxelsFromStencil(baseSystem, stencilHeadVoxels)
                        || stencilHeadVoxels.empty()) {
                        return;
                    }
                    if (!isGemValidPickaxeHeadShape(baseSystem, gems->heldDrop, stencilHeadVoxels)) {
                        return;
                    }

                    RemovedBlockInfo removedStick;
                    if (RemoveBlockAtPosition(
                            baseSystem,
                            level,
                            prototypes,
                            playerCtx.targetedBlockPosition,
                            playerCtx.targetedWorldIndex,
                            &removedStick)
                        && removedStick.prototypeID >= 0
                        && removedStick.prototypeID < static_cast<int>(prototypes.size())
                        && isStickPrototypeName(prototypes[static_cast<size_t>(removedStick.prototypeID)].name)) {
                        playerCtx.pickaxeHeld = true;
                        playerCtx.pickaxeGemKind = glm::clamp(gems->heldDrop.kind, 0, 3);
                        playerCtx.pickaxeHeadVoxels = std::move(stencilHeadVoxels);
                        gems->blockModeHoldingGem = false;
                        gems->heldDrop = GemDropState{};
                        gems->placementPreviewActive = false;
                        gems->placementPreviewPosition = glm::vec3(0.0f);
                        gems->placementPreviewRenderYOffset = 0.0f;
                        triggerGameplaySfx(baseSystem, "pickup_block.ck", 0.02f);
                        return;
                    }
                }
                if (!targetIsBlueprint
                    && isNaturalSurfaceStonePrototypeName(targetProto.name)
                    && playerCtx.leftMousePressed) {
                    const int ingotMaterial = hatchetMaterialFromGemKind(gems->heldDrop.kind);
                    const int ingotPrototypeID = resolveGemIngotPrototypeID(prototypes, ingotMaterial);
                    if (ingotPrototypeID >= 0) {
                        playerCtx.isHoldingBlock = true;
                        playerCtx.heldPrototypeID = ingotPrototypeID;
                        playerCtx.heldBlockColor = glm::vec3(1.0f);
                        playerCtx.heldPackedColor = 0u;
                        playerCtx.heldHasSourceCell = false;
                        playerCtx.heldSourceCell = glm::ivec3(0);
                        gems->blockModeHoldingGem = false;
                        gems->heldDrop = GemDropState{};
                        triggerGameplaySfx(baseSystem, "pickup_block.ck", 0.02f);
                        return;
                    }
                }
            }

            if (targetIsBlueprint) {
                glm::vec3 previewPos = glm::vec3(targetCell);
                if (!computeGemPlacementWithinCell(
                        baseSystem,
                        gems->heldDrop,
                        targetCell,
                        playerCtx.targetedBlockHitPosition,
                        &previewPos)) {
                    return;
                }

                const float previewYOffset = 2.0f / 24.0f;
                gems->placementPreviewActive = true;
                gems->placementPreviewPosition = previewPos;
                gems->placementPreviewRenderYOffset = previewYOffset;

                if (!playerCtx.leftMousePressed) return;

                gems->heldDrop.renderYOffset = previewYOffset;
                GemSystemLogic::PlaceGemDrop(baseSystem, std::move(gems->heldDrop), previewPos);
                gems->blockModeHoldingGem = false;
                gems->heldDrop = GemDropState{};
                gems->placementPreviewActive = false;
                triggerGameplaySfx(baseSystem, "place_block.ck", 0.02f);
                return;
            }

            if (!playerCtx.leftMousePressed) return;
            glm::vec3 placePos = playerCtx.targetedBlockPosition + playerCtx.targetedBlockNormal;
            if (BlockSelectionSystemLogic::HasBlockAt(baseSystem, prototypes, playerCtx.targetedWorldIndex, placePos)) return;

            gems->heldDrop.renderYOffset = 0.0f;
            GemSystemLogic::PlaceGemDrop(baseSystem, std::move(gems->heldDrop), placePos);
            gems->blockModeHoldingGem = false;
            gems->heldDrop = GemDropState{};
            triggerGameplaySfx(baseSystem, "place_block.ck", 0.02f);
        };
        auto tryPlaceHeldHatchet = [&](PlayerContext& playerCtx) -> bool {
            if (!playerCtx.hatchetHeld) return false;
            if (!playerCtx.hasBlockTarget || glm::length(playerCtx.targetedBlockNormal) < 0.1f) return false;
            if (playerCtx.targetedWorldIndex < 0 || playerCtx.targetedWorldIndex >= static_cast<int>(level.worlds.size())) return false;
            if (targetIsLatchedAnchorSuppressed()) return false;

            const glm::vec3 surfaceNormal = normalizeOrDefault(playerCtx.targetedBlockNormal, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec3 forward = cameraForwardDirection(playerCtx);
            playerCtx.hatchetPlacedInWorld = true;
            playerCtx.hatchetPlacedCell = glm::ivec3(glm::round(playerCtx.targetedBlockPosition));
            playerCtx.hatchetPlacedWorldIndex = playerCtx.targetedWorldIndex;
            playerCtx.hatchetPlacedNormal = surfaceNormal;
            playerCtx.hatchetPlacedDirection = projectDirectionOnSurface(forward, surfaceNormal);
            playerCtx.hatchetPlacedPosition = playerCtx.targetedBlockPosition - surfaceNormal * 0.47f;
            playerCtx.hatchetPlacedMaterial = glm::clamp(playerCtx.hatchetSelectedMaterial, 0, HATCHET_MATERIAL_COUNT - 1);
            playerCtx.hatchetHeld = false;
            for (int i = 0; i < HATCHET_MATERIAL_COUNT; ++i) {
                playerCtx.hatchetInventoryByMaterial[static_cast<size_t>(i)] = 0;
            }
            playerCtx.hatchetInventoryCount = 0;
            triggerGameplaySfx(baseSystem, "place_block.ck", 0.02f);
            return true;
        };

        auto tryDrawChalkDustUsingHeldChalk = [&](PlayerContext& playerCtx) -> bool {
            if (!playerCtx.isHoldingBlock) return false;
            if (playerCtx.heldPrototypeID < 0 || playerCtx.heldPrototypeID >= static_cast<int>(prototypes.size())) return false;
            const Entity& heldProto = prototypes[static_cast<size_t>(playerCtx.heldPrototypeID)];
            if (!isChalkDrawToolPrototypeName(heldProto.name)) return false;
            if (!playerCtx.hasBlockTarget) return false;
            if (glm::length(playerCtx.targetedBlockNormal) < 0.1f) return false;
            const glm::vec3 n = normalizeOrDefault(playerCtx.targetedBlockNormal, glm::vec3(0.0f, 1.0f, 0.0f));
            if (n.y < 0.7f) return false;

            int worldIndex = playerCtx.targetedWorldIndex;
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) {
                worldIndex = level.activeWorldIndex;
            }
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) {
                worldIndex = 0;
            }
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return false;
            const glm::ivec3 placeCell = glm::ivec3(glm::round(playerCtx.targetedBlockPosition + playerCtx.targetedBlockNormal));
            CellBlockInfo existing;
            const bool hasExisting = queryBlockAtCell(baseSystem, level, prototypes, worldIndex, placeCell, &existing);
            if (hasExisting && existing.present && !isChalkDustPrototypeID(prototypes, existing.prototypeID)) {
                return false;
            }

            int dustPrototypeID = existing.prototypeID;
            if (!hasExisting || !existing.present || !isChalkDustPrototypeID(prototypes, dustPrototypeID)) {
                const bool preferX = (((placeCell.x ^ placeCell.y ^ placeCell.z) & 1) == 0);
                dustPrototypeID = resolveChalkDustPrototypeID(prototypes, preferX);
                if (dustPrototypeID < 0) return false;
                const uint32_t packed = packChalkDustSnapshotColor(CHALK_TILE_DOT, 0);
                if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                    baseSystem.voxelWorld->setBlockWorld(
                        placeCell,
                        static_cast<uint32_t>(dustPrototypeID),
                        packed
                    );
                    VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, placeCell);
                    StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, worldIndex, glm::vec3(placeCell));
                } else {
                    const glm::vec3 placePos = glm::vec3(placeCell);
                    if (BlockSelectionSystemLogic::HasBlockAt(baseSystem, prototypes, worldIndex, placePos)) return false;
                    Entity& world = level.worlds[static_cast<size_t>(worldIndex)];
                    world.instances.push_back(HostLogic::CreateInstance(
                        baseSystem,
                        dustPrototypeID,
                        placePos,
                        glm::vec3(1.0f)
                    ));
                    BlockSelectionSystemLogic::AddBlockToCache(baseSystem, prototypes, worldIndex, placePos, dustPrototypeID);
                    StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, worldIndex, placePos);
                }
            }

            refreshChalkDustNeighborhood(baseSystem, level, prototypes, worldIndex, placeCell);
            triggerGameplaySfx(baseSystem, "place_block.ck", 0.02f);
            return true;
        };

        if (interactionMode
            && bKeyJustPressed
            && player.isHoldingBlock
            && player.heldPrototypeID >= 0
            && player.heldPrototypeID < static_cast<int>(prototypes.size())
            && isChalkDrawToolPrototypeName(prototypes[static_cast<size_t>(player.heldPrototypeID)].name)) {
            if (tryDrawChalkDustUsingHeldChalk(player)) {
                resetChargeState();
                return;
            }
        }

        auto tryCraftWorkbenchFromHammerHit = [&](PlayerContext& playerCtx) -> bool {
            if (!playerCtx.hasBlockTarget) return false;
            int worldIndex = playerCtx.targetedWorldIndex;
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) {
                worldIndex = level.activeWorldIndex;
            }
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) {
                worldIndex = 0;
            }
            if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) return false;
            const glm::ivec3 targetCell = glm::ivec3(glm::round(playerCtx.targetedBlockPosition));

            CellBlockInfo targetInfo;
            if (!queryBlockAtCell(baseSystem, level, prototypes, worldIndex, targetCell, &targetInfo) || !targetInfo.present) return false;
            if (targetInfo.prototypeID < 0 || targetInfo.prototypeID >= static_cast<int>(prototypes.size())) return false;
            const Entity& targetProto = prototypes[static_cast<size_t>(targetInfo.prototypeID)];
            if (!isVerticalLogPrototypeName(targetProto.name)) return false;
            if (!isValidChalkCraftRing(baseSystem, level, prototypes, worldIndex, targetCell)) return false;

            const glm::vec3 forward = cameraForwardDirection(playerCtx);
            const glm::vec3 facingDir = -glm::vec3(forward.x, 0.0f, forward.z);
            const int workbenchID = resolveWorkbenchPrototypeID(prototypes, facingDir);
            if (workbenchID < 0) return false;

            if (!replaceBlockAtCell(
                    baseSystem,
                    level,
                    prototypes,
                    worldIndex,
                    targetCell,
                    targetInfo.prototypeID,
                    workbenchID,
                    glm::vec3(1.0f),
                    packColor(glm::vec3(1.0f)))) {
                return false;
            }
            clearBlockDamageAt(worldIndex, targetCell);
            triggerGameplaySfx(baseSystem, "place_block.ck", 0.02f);
            return true;
        };

        const bool throwModifierDown = PlatformInput::IsKeyDown(win, PlatformInput::Key::W);
        const bool throwWJustPressed = throwModifierDown && !throwWDownLastFrame;
        throwWDownLastFrame = throwModifierDown;
        if (!throwModifierDown) {
            throwRequireWReleaseForNextStart = false;
        }
        const double throwNow = PlatformInput::GetTimeSeconds();
        if (throwWJustPressed) throwLastWPressTime = throwNow;
        if (player.leftMousePressed) throwLastLmbPressTime = throwNow;
        const float throwChordWindowSeconds = std::max(
            0.01f,
            readRegistryFloat(baseSystem, "HeldThrowChordWindowSeconds", 0.14f)
        );

        if (player.isHoldingBlock && !holdingChalkTool) {
            const bool throwTailActive =
                player.blockChargeAction == BlockChargeAction::Throw
                && (player.isChargingBlock
                    || player.blockChargeExecuteGraceTimer > 0.0f
                    || player.blockChargeValue > 0.001f);
            const bool throwStartChord =
                !throwTailActive
                && !throwRequireWReleaseForNextStart
                && throwModifierDown
                && player.leftMouseDown
                && (throwWJustPressed || player.leftMousePressed)
                && (std::abs(throwLastWPressTime - throwLastLmbPressTime) <= static_cast<double>(throwChordWindowSeconds));
            const bool throwChargeGesture = throwStartChord || throwTailActive;
            if (!legacyDestroyMode && throwChargeGesture) {
                if (throwStartChord) {
                    // After one valid start, W must be released before the next start.
                    throwRequireWReleaseForNextStart = true;
                }
                const bool throwChargeDown = player.leftMouseDown
                    && (player.blockChargeAction == BlockChargeAction::Throw || throwStartChord);
                const bool throwExecutePressed = player.rightMousePressed;

                if (throwChargeDown) {
                    if (!player.isChargingBlock || player.blockChargeAction != BlockChargeAction::Throw) {
                        player.blockChargeValue = 0.0f;
                        player.blockChargeReady = false;
                    }
                    player.isChargingBlock = true;
                    player.blockChargeAction = BlockChargeAction::Throw;
                    player.blockChargeDecayTimer = chargeDecaySeconds;
                    player.blockChargeExecuteGraceTimer = 0.0f;
                    const float throwChargeSeconds = std::max(
                        0.05f,
                        readRegistryFloat(baseSystem, "HeldThrowChargeSeconds", CHARGE_TIME_PICKUP)
                    );
                    player.blockChargeValue += dt / throwChargeSeconds;
                    if (player.blockChargeValue >= 1.0f) {
                        player.blockChargeValue = 1.0f;
                        player.blockChargeReady = true;
                    }
                } else if (player.blockChargeAction == BlockChargeAction::Throw) {
                    releaseChargeToTail();
                    updateChargeTail();
                }

                const bool hasGraceWindow = player.blockChargeExecuteGraceTimer > 0.0f;
                const bool executeThrow = player.blockChargeAction == BlockChargeAction::Throw
                    && throwExecutePressed
                    && (throwChargeDown || hasGraceWindow);
                if (executeThrow) {
                    if (player.blockChargeReady && !thrownHeldBlock.active) {
                        const int throwWorldIndex = normalizeThrowWorldIndex(player.targetedWorldIndex);
                        if (throwWorldIndex >= 0) {
                            glm::vec3 forward = cameraForwardDirection(player);
                            if (glm::length(forward) < 0.01f) forward = glm::vec3(0.0f, 0.0f, -1.0f);
                            forward = glm::normalize(forward);

                            const float launchSpeed = std::max(0.5f, readRegistryFloat(baseSystem, "HeldThrowLaunchSpeed", 10.5f));
                            const float launchUp = readRegistryFloat(baseSystem, "HeldThrowLaunchUp", 1.2f);
                            const float spawnForwardOffset = std::max(0.2f, readRegistryFloat(baseSystem, "HeldThrowSpawnForwardOffset", 0.75f));
                            const float spawnLift = readRegistryFloat(baseSystem, "HeldThrowSpawnLift", 0.12f);

                            glm::vec3 spawnPos = cameraEyePosition(baseSystem, player)
                                + forward * spawnForwardOffset
                                + glm::vec3(0.0f, spawnLift, 0.0f);
                            for (int i = 0; i < 6; ++i) {
                                const glm::ivec3 spawnCell = glm::ivec3(glm::round(spawnPos));
                                if (!isSolidThrowCell(throwWorldIndex, spawnCell, -1)) break;
                                spawnPos += forward * 0.35f;
                            }

                            clearThrownHeldState();
                            thrownHeldBlock.active = true;
                            thrownHeldBlock.worldIndex = throwWorldIndex;
                            thrownHeldBlock.prototypeID = player.heldPrototypeID;
                            thrownHeldBlock.color = player.heldBlockColor;
                            thrownHeldBlock.packedColor = player.heldPackedColor;
                            thrownHeldBlock.hasSourceCell = player.heldHasSourceCell;
                            thrownHeldBlock.sourceCell = player.heldSourceCell;
                            thrownHeldBlock.position = spawnPos;
                            thrownHeldBlock.velocity = forward * launchSpeed + glm::vec3(0.0f, launchUp, 0.0f);
                            thrownHeldBlock.instanceID = -1;
                            thrownHeldBlock.age = 0.0f;
                            ensureThrownHeldVisual();
                            triggerChargeFireInvertTail();
                            triggerGameplaySfx(baseSystem, "pickup_block.ck", 0.02f);

                            player.isHoldingBlock = false;
                            player.heldPrototypeID = -1;
                            player.heldPackedColor = 0u;
                            player.heldHasSourceCell = false;
                            player.heldSourceCell = glm::ivec3(0);
                        }
                        releaseChargeUseToTail();
                    } else {
                        resetChargeState();
                    }
                    return;
                }

                if (player.blockChargeAction == BlockChargeAction::Throw || throwChargeDown) {
                    return;
                }
            }

            if (!legacyDestroyMode) {
                tryPlaceHeldBlock(player);
                resetChargeState();
                return;
            }
            player.isHoldingBlock = false;
            player.heldPrototypeID = -1;
            player.heldPackedColor = 0u;
            player.heldHasSourceCell = false;
            player.heldSourceCell = glm::ivec3(0);
        }
        if (gems && gems->blockModeHoldingGem) {
            if (!legacyDestroyMode) {
                tryPlaceHeldGem(player);
                resetChargeState();
                return;
            }
            clearHeldGem();
        }

        // Combined interaction mode (toggle with E while in Pickup mode):
        // Default:
        //   Pickup: hold RMB to charge, then press LMB to execute.
        //   Destroy: hold LMB to charge, then press RMB to execute.
        // Swapped:
        //   Pickup: hold LMB to charge, then press RMB to execute.
        //   Destroy: hold RMB to charge, then press LMB to execute.
        const bool swappedControls = (!legacyDestroyMode && pickupHandMode && player.blockChargeControlsSwapped);
        const bool pickupChargeDown = swappedControls ? player.leftMouseDown : player.rightMouseDown;
        const bool pickupChargePressed = swappedControls ? player.leftMousePressed : player.rightMousePressed;
        const bool pickupExecutePressed = swappedControls ? player.rightMousePressed : player.leftMousePressed;
        const bool destroyChargeDown = swappedControls ? player.rightMouseDown : player.leftMouseDown;
        const bool destroyChargePressed = swappedControls ? player.rightMousePressed : player.leftMousePressed;
        const bool destroyExecutePressed = swappedControls ? player.leftMousePressed : player.rightMousePressed;

        BlockChargeAction activeAction = player.blockChargeAction;
        const bool destroyChargeRequiresHatchet = readRegistryBool(baseSystem, "DestroyChargeRequiresHatchet", false);
        const bool destroyUnlocked = !destroyChargeRequiresHatchet || !kHatchetsEnabled || player.hatchetHeld || holdingChalkTool;
        if (legacyDestroyMode) {
            activeAction = destroyUnlocked ? BlockChargeAction::Destroy : BlockChargeAction::None;
        } else if (activeAction == BlockChargeAction::Destroy && !destroyUnlocked) {
            activeAction = BlockChargeAction::None;
        } else if (activeAction == BlockChargeAction::None) {
            if ((!player.isHoldingBlock || holdingChalkTool) && !(gems && gems->blockModeHoldingGem)) {
                bool wantsDestroyCharge = destroyUnlocked && destroyChargeDown && !pickupChargeDown;
                bool wantsPickupCharge = pickupChargeDown && !destroyChargeDown;
                if (destroyUnlocked && destroyChargePressed && !pickupChargeDown) wantsDestroyCharge = true;
                if (pickupChargePressed && !destroyChargeDown) wantsPickupCharge = true;
                if (wantsDestroyCharge) activeAction = BlockChargeAction::Destroy;
                else if (wantsPickupCharge) activeAction = BlockChargeAction::Pickup;
            }
        }

        bool wantsCharge = false;
        if (activeAction == BlockChargeAction::Pickup) {
            wantsCharge = pickupChargeDown;
        } else if (activeAction == BlockChargeAction::Destroy) {
            wantsCharge = destroyChargeDown;
        }

        if (wantsCharge) {
            if (!player.isChargingBlock || player.blockChargeAction != activeAction) {
                player.blockChargeValue = 0.0f;
                player.blockChargeReady = false;
            }
            player.isChargingBlock = true;
            player.blockChargeAction = activeAction;
            player.blockChargeDecayTimer = chargeDecaySeconds;
            player.blockChargeExecuteGraceTimer = 0.0f;
            const bool destroyAction = (activeAction == BlockChargeAction::Destroy);
            float chargeTime = destroyAction ? CHARGE_TIME_DESTROY : CHARGE_TIME_PICKUP;
            player.blockChargeValue += dt / chargeTime;
            if (player.blockChargeValue >= 1.0f) {
                player.blockChargeValue = 1.0f;
                player.blockChargeReady = true;
            }
        } else {
            releaseChargeToTail();
            updateChargeTail();
        }

        const bool hasGraceWindow = player.blockChargeExecuteGraceTimer > 0.0f;
        bool executePickup = player.blockChargeAction == BlockChargeAction::Pickup
            && pickupExecutePressed
            && (pickupChargeDown || hasGraceWindow);
        bool executeDestroy = player.blockChargeAction == BlockChargeAction::Destroy
            && destroyExecutePressed
            && (destroyChargeDown || hasGraceWindow);

        if (executePickup || executeDestroy) {
            const bool firedCharge = player.blockChargeReady;
            if (firedCharge) {
                triggerChargeFireInvertTail();
                bool actionPerformed = false;
                const bool destroyAction = executeDestroy;

                if (kHatchetsEnabled
                    && !destroyAction
                    && !player.hatchetHeld
                    && player.hatchetPlacedInWorld
                    && player.hasBlockTarget) {
                    const glm::ivec3 targetCell = glm::ivec3(glm::round(player.targetedBlockPosition));
                    if (targetCell == player.hatchetPlacedCell
                        && worldMatches(player.targetedWorldIndex, player.hatchetPlacedWorldIndex)) {
                        setHeldHatchetMaterial(player.hatchetPlacedMaterial);
                        clearPlacedHatchet();
                        triggerGameplaySfx(baseSystem, "pickup_block.ck", 0.02f);
                        actionPerformed = true;
                    }
                }

                if (kHatchetsEnabled
                    && !actionPerformed
                    && !destroyAction
                    && player.hatchetHeld
                    && !player.isHoldingBlock
                    && !(gems && gems->blockModeHoldingGem)) {
                    if (tryPlaceHeldHatchet(player)) {
                        actionPerformed = true;
                    }
                }

                // In pickup action, gem interaction takes priority so terrain blocks don't consume the click first.
                if (!actionPerformed
                    && !destroyAction
                    && gems
                    && !player.isHoldingBlock
                    && !gems->blockModeHoldingGem) {
                    GemDropState pickedGem;
                    const glm::vec3 rayOrigin = cameraEyePosition(baseSystem, player);
                    const glm::vec3 rayDirection = cameraForwardDirection(player);
                    const float rayDistance = std::max(0.25f, readRegistryFloat(baseSystem, "GemPickupRayDistance", 5.0f));
                    if (GemSystemLogic::TryPickupGemFromRay(baseSystem, rayOrigin, rayDirection, rayDistance, &pickedGem)) {
                        gems->heldDrop = std::move(pickedGem);
                        gems->blockModeHoldingGem = true;
                        triggerGameplaySfx(baseSystem, "pickup_block.ck", 0.02f);
                        actionPerformed = true;
                    }
                }

                if (!actionPerformed && player.hasBlockTarget) {
                    if (targetIsLatchedAnchorSuppressed()) {
                        releaseChargeUseToTail();
                        return;
                    }
                    if (destroyAction && holdingChalkTool) {
                        (void)tryDrawChalkDustUsingHeldChalk(player);
                        releaseChargeUseToTail();
                        return;
                    }
                    if (destroyAction && !holdingChalkTool) {
                        if (tryCraftWorkbenchFromHammerHit(player)) {
                            actionPerformed = true;
                            releaseChargeUseToTail();
                            return;
                        }
                    }
                    if (destroyAction) {
                        const glm::ivec3 targetCell = glm::ivec3(glm::round(player.targetedBlockPosition));
                        if (GemChiselSystemLogic::StartGemChiselAtCell(baseSystem, targetCell, player.targetedWorldIndex)) {
                            actionPerformed = true;
                            releaseChargeUseToTail();
                            return;
                        }
                    }
                    if (destroyAction) {
                        glm::ivec3 targetCell = glm::ivec3(glm::round(player.targetedBlockPosition));
                        bool targetFromVoxel = false;
                        const int targetPrototypeID = resolveTargetPrototypeID(
                            baseSystem,
                            level,
                            prototypes,
                            player,
                            &targetCell,
                            &targetFromVoxel);
                        if (targetPrototypeID >= 0) {
                            glm::vec3 spawnForward = -player.targetedBlockNormal;
                            if (glm::length(spawnForward) < 0.01f) {
                                spawnForward = glm::vec3(0.0f, 0.0f, -1.0f);
                            }
                            const glm::vec3 targetPos = targetFromVoxel
                                ? glm::vec3(targetCell)
                                : player.targetedBlockPosition;
                            if (OreMiningSystemLogic::StartOreMiningFromBlock(
                                    baseSystem,
                                    prototypes,
                                    player.targetedWorldIndex,
                                    targetCell,
                                    targetPrototypeID,
                                    targetPos,
                                    spawnForward)) {
                                actionPerformed = true;
                                releaseChargeUseToTail();
                                return;
                            }
                        }
                    }
                    if (destroyAction) {
                        glm::ivec3 damageCell = glm::ivec3(glm::round(player.targetedBlockPosition));
                        bool damageFromVoxel = false;
                        const int damagePrototypeID = resolveTargetPrototypeID(
                            baseSystem,
                            level,
                            prototypes,
                            player,
                            &damageCell,
                            &damageFromVoxel
                        );
                        (void)damageFromVoxel;
                        if (damagePrototypeID >= 0
                            && damagePrototypeID < static_cast<int>(prototypes.size())
                            && isRemovableGameplayBlock(prototypes[static_cast<size_t>(damagePrototypeID)])) {
                            CellBlockInfo damageInfo;
                            queryBlockAtCell(baseSystem, level, prototypes, player.targetedWorldIndex, damageCell, &damageInfo);
                            const glm::vec3 damageColor = damageInfo.present ? damageInfo.color : glm::vec3(1.0f);
                            MiniVoxelParticleSystemLogic::SpawnFromBlock(
                                baseSystem,
                                prototypes,
                                player.targetedWorldIndex,
                                damageCell,
                                damagePrototypeID,
                                damageColor,
                                player.targetedBlockHitPosition,
                                player.targetedBlockNormal
                            );
                            const int requiredHits = std::max(1, readRegistryInt(baseSystem, "BlockBreakHitsBase", 8));
                            if (requiredHits > 1) {
                                const bool shouldBreakNow = applyBlockDamageHit(
                                    player.targetedWorldIndex,
                                    damageCell,
                                    damagePrototypeID,
                                    requiredHits,
                                    nullptr
                                );
                                if (!shouldBreakNow) {
                                    if (damageFromVoxel) {
                                        VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, damageCell);
                                    }
                                    triggerGameplaySfx(baseSystem, "break_stone.ck", 0.02f);
                                    actionPerformed = true;
                                    releaseChargeUseToTail();
                                    return;
                                }
                            } else {
                                clearBlockDamageAt(player.targetedWorldIndex, damageCell);
                            }
                        }
                    }
                    RemovedBlockInfo removedBlock;
                    if (RemoveBlockAtPosition(baseSystem, level, prototypes, player.targetedBlockPosition, player.targetedWorldIndex, &removedBlock)) {
                        const glm::ivec3 removedCell = removedBlock.fromVoxel
                            ? removedBlock.voxelCell
                            : glm::ivec3(glm::round(player.targetedBlockPosition));
                        const bool removedWasChalkDust = removedBlock.prototypeID >= 0
                            && isChalkDustPrototypeID(prototypes, removedBlock.prototypeID);
                        const bool removedWasComputer = isComputerPrototypeID(prototypes, removedBlock.prototypeID);
                        clearBlockDamageAt(player.targetedWorldIndex, removedCell);
                        releaseLatchedAnchorAt(removedCell, player.targetedWorldIndex);
                        if (removedBlock.fromVoxel) {
                            StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, player.targetedWorldIndex, glm::vec3(removedBlock.voxelCell));
                        } else {
                            BlockSelectionSystemLogic::RemoveBlockFromCache(baseSystem, prototypes, player.targetedWorldIndex, player.targetedBlockPosition);
                            StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, player.targetedWorldIndex, player.targetedBlockPosition);
                        }
                        if (removedWasChalkDust) {
                            refreshChalkDustNeighborhood(baseSystem, level, prototypes, player.targetedWorldIndex, removedCell);
                        }
                        if (removedWasComputer && baseSystem.ui) {
                            baseSystem.ui->computerCacheBuilt = false;
                        }
                        // Ore rewards are owned by the ore-mining minigame success path.
                        // Direct destroy should never spawn gem rewards.
                        if (!destroyAction) {
                            player.isHoldingBlock = true;
                            player.heldPrototypeID = removedBlock.prototypeID;
                            player.heldBlockColor = removedBlock.color;
                            player.heldPackedColor = removedBlock.packedColor;
                            player.heldHasSourceCell = removedBlock.hasSourceCell;
                            player.heldSourceCell = removedBlock.sourceCell;
                        }
                        if (audioVisualizerProto && removedBlock.prototypeID == audioVisualizerProto->prototypeID) {
                            RayTracedAudioSystemLogic::InvalidateSourceCache(baseSystem);
                            ChucKSystemLogic::StopNoiseShred(baseSystem);
                        }
                        if (destroyAction) {
                            (void)SpawnCavePotLoot(
                                baseSystem,
                                level,
                                prototypes,
                                player.targetedWorldIndex,
                                removedBlock,
                                player
                            );
                            triggerGameplaySfx(baseSystem, "break_stone.ck", 0.02f);
                        } else {
                            triggerGameplaySfx(baseSystem, "pickup_block.ck", 0.02f);
                        }
                        actionPerformed = true;
                    }
                }
            }
            if (firedCharge) {
                releaseChargeUseToTail();
            } else {
                resetChargeState();
            }
        }

        if (!player.isHoldingBlock) {
            tryPlaceHeldBlock(player);
        }
    }

    void ApplyBlockDamageMaskUniforms(BaseSystem& baseSystem,
                                      std::vector<Entity>& prototypes,
                                      const Shader& shader,
                                      bool enableMask) {
        std::vector<BlockDamageMaskRenderEntry> entries;
        collectBlockDamageMaskEntries(baseSystem, prototypes, entries);
        const bool cracksEnabled = readRegistryBool(baseSystem, "BlockBreakCrackEnabled", true);
        uploadBlockDamageUniforms(baseSystem, entries, shader, enableMask && cracksEnabled);
    }

    void CollectDamagedVoxelCells(const BaseSystem&,
                                  int worldIndex,
                                  std::vector<glm::ivec3>& outCells) {
        outCells.clear();
        if (worldIndex < 0) return;
        const auto& damage = blockDamageMap();
        if (damage.empty()) return;
        outCells.reserve(damage.size());
        for (const auto& pair : damage) {
            const BlockDamageKey& key = pair.first;
            const BlockDamageEntry& entry = pair.second;
            if (key.worldIndex != worldIndex) continue;
            if (entry.hits <= 0 || entry.requiredHits <= 0) continue;
            outCells.push_back(key.cell);
        }
    }

    void RenderBlockDamage(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt;
        (void)win;
        // Crack lines were replaced by in-shader erosion masks. Keep this hook to
        // prune stale damage state for systems that still call it.
        std::vector<BlockDamageMaskRenderEntry> entries;
        collectBlockDamageMaskEntries(baseSystem, prototypes, entries);
    }
}
