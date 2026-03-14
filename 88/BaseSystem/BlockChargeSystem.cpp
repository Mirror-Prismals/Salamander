#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>

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

        bool isRemovableGameplayBlock(const Entity& proto) {
            if (!proto.isBlock) return false;
            if (proto.name == "Water") return false;
            // Terrain voxels are often chunkable but flagged immutable (e.g. ScaffoldBlock).
            // Allow those for gameplay pickup/destroy while keeping non-chunkable immutables protected.
            if (proto.isMutable) return true;
            return proto.isChunkable;
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

        bool triggerGameplaySfx(BaseSystem& baseSystem, const char* fileName, float cooldownSeconds = 0.0f) {
            if (!fileName) return false;
            static std::unordered_map<std::string, double> s_lastTrigger;
            const std::string keyName(fileName);
            const double now = glfwGetTime();
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
            entry.lastHitTime = glfwGetTime();

            const int hitsNow = entry.hits;
            const bool shouldBreak = (hitsNow >= entry.requiredHits);
            if (shouldBreak) {
                damage.erase(key);
            }
            if (outHits) *outHits = hitsNow;
            return shouldBreak;
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
            const GLint enabledLoc = glGetUniformLocation(shader.ID, "blockDamageEnabled");
            if (enabledLoc < 0) return;

            const int count = enableMask ? static_cast<int>(entries.size()) : 0;
            glUniform1i(enabledLoc, (count > 0) ? 1 : 0);

            const GLint countLoc = glGetUniformLocation(shader.ID, "blockDamageCount");
            if (countLoc >= 0) {
                glUniform1i(countLoc, count);
            }

            const GLint gridLoc = glGetUniformLocation(shader.ID, "blockDamageGrid");
            if (gridLoc >= 0) {
                const float grid = glm::clamp(
                    readRegistryFloat(baseSystem, "BlockBreakMaskGrid", 24.0f),
                    4.0f,
                    96.0f
                );
                glUniform1f(gridLoc, grid);
            }

            if (count <= 0) return;

            const GLint cellsLoc = glGetUniformLocation(shader.ID, "blockDamageCells");
            const GLint progressLoc = glGetUniformLocation(shader.ID, "blockDamageProgress");
            if (cellsLoc < 0 || progressLoc < 0) return;

            std::vector<GLint> packedCells(static_cast<size_t>(count) * 3u, 0);
            std::vector<float> progress(static_cast<size_t>(count), 0.0f);
            for (int i = 0; i < count; ++i) {
                const BlockDamageMaskRenderEntry& e = entries[static_cast<size_t>(i)];
                packedCells[static_cast<size_t>(i) * 3u + 0u] = static_cast<GLint>(e.cell.x);
                packedCells[static_cast<size_t>(i) * 3u + 1u] = static_cast<GLint>(e.cell.y);
                packedCells[static_cast<size_t>(i) * 3u + 2u] = static_cast<GLint>(e.cell.z);
                progress[static_cast<size_t>(i)] = glm::clamp(e.progress, 0.0f, 1.0f);
            }

            glUniform3iv(cellsLoc, count, packedCells.data());
            glUniform1fv(progressLoc, count, progress.data());
        }

        struct RemovedBlockInfo {
            int prototypeID = -1;
            glm::vec3 color = glm::vec3(1.0f);
            bool fromVoxel = false;
            glm::ivec3 voxelCell = glm::ivec3(0);
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
                        if (removedInfo) {
                            removedInfo->prototypeID = static_cast<int>(id);
                            removedInfo->color = unpackColor(baseSystem.voxelWorld->getColorWorld(cell));
                            removedInfo->fromVoxel = true;
                            removedInfo->voxelCell = cell;
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
                    removedInfo->prototypeID = inst.prototypeID;
                    removedInfo->color = inst.color;
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

    void UpdateBlockCharge(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)win;
        if (!baseSystem.player || !baseSystem.level) return;
        PlayerContext& player = *baseSystem.player;
        LevelContext& level = *baseSystem.level;
        GemContext* gems = baseSystem.gems ? baseSystem.gems.get() : nullptr;
        if (gems) {
            gems->placementPreviewActive = false;
            gems->placementPreviewPosition = glm::vec3(0.0f);
            gems->placementPreviewRenderYOffset = 0.0f;
        }
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
        auto resetChargeState = [&]() {
            player.isChargingBlock = false;
            player.blockChargeReady = false;
            player.blockChargeValue = 0.0f;
            player.blockChargeAction = BlockChargeAction::None;
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

        bool legacyDestroyMode = player.buildMode == BuildModeType::Destroy;
        bool interactionMode = player.buildMode == BuildModeType::Pickup || legacyDestroyMode;
        bool fishingMode = player.buildMode == BuildModeType::Fishing;
        bool boulderingMode = player.buildMode == BuildModeType::Bouldering;
        const bool leafClimbEnabled = readRegistryBool(baseSystem, "LeafClimbEnabled", true);
        const bool leafBoulderingAnchorsEnabled = readRegistryBool(
            baseSystem,
            "LeafClimbBoulderingAnchorsEnabled",
            leafClimbEnabled);
        const int leafPrototypeID = leafBoulderingAnchorsEnabled ? resolveLeafPrototypeID(prototypes) : -1;
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

            BlockChargeAction activeAction = player.blockChargeAction;
            if (activeAction != BlockChargeAction::BoulderPrimary && activeAction != BlockChargeAction::BoulderSecondary) {
                activeAction = BlockChargeAction::None;
            }
            if (activeAction == BlockChargeAction::None) {
                bool wantsPrimaryCharge = player.rightMouseDown && !player.leftMouseDown;
                bool wantsSecondaryCharge = player.leftMouseDown && !player.rightMouseDown;
                if (player.rightMousePressed && !player.leftMouseDown) wantsPrimaryCharge = true;
                if (player.leftMousePressed && !player.rightMouseDown) wantsSecondaryCharge = true;
                if (wantsPrimaryCharge) activeAction = BlockChargeAction::BoulderPrimary;
                else if (wantsSecondaryCharge) activeAction = BlockChargeAction::BoulderSecondary;
            }

            bool wantsCharge = false;
            if (activeAction == BlockChargeAction::BoulderPrimary) wantsCharge = player.rightMouseDown;
            else if (activeAction == BlockChargeAction::BoulderSecondary) wantsCharge = player.leftMouseDown;

            if (wantsCharge) {
                if (!player.isChargingBlock) player.blockChargeValue = 0.0f;
                player.isChargingBlock = true;
                player.blockChargeAction = activeAction;
                player.blockChargeValue += dt / boulderChargeSeconds;
                if (player.blockChargeValue >= 1.0f) {
                    player.blockChargeValue = 1.0f;
                    player.blockChargeReady = true;
                }
            } else {
                resetChargeState();
            }

            const bool executePrimary = player.blockChargeAction == BlockChargeAction::BoulderPrimary
                && player.leftMousePressed
                && player.rightMouseDown;
            const bool executeSecondary = player.blockChargeAction == BlockChargeAction::BoulderSecondary
                && player.rightMousePressed
                && player.leftMouseDown;

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
                if (executePrimary) {
                    (void)tryLatchHand(true);
                } else {
                    (void)tryLatchHand(false);
                }
                resetChargeState();
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
            if (prototypes[playerCtx.heldPrototypeID].name == "Water") return;
            const Entity& heldProto = prototypes[playerCtx.heldPrototypeID];

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
                    if (isStickPrototypeName(targetProto.name)) {
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

            bool placedInVoxel = false;
            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled && heldProto.isChunkable) {
                glm::ivec3 placeCell = glm::ivec3(glm::round(placePos));
                baseSystem.voxelWorld->setBlockWorld(
                    placeCell,
                    static_cast<uint32_t>(playerCtx.heldPrototypeID),
                    packColor(playerCtx.heldBlockColor)
                );
                VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, placeCell);
                StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, playerCtx.targetedWorldIndex, glm::vec3(placeCell));
                placedInVoxel = true;
            }

            if (!placedInVoxel) {
                Entity& world = level.worlds[playerCtx.targetedWorldIndex];
                world.instances.push_back(HostLogic::CreateInstance(baseSystem, playerCtx.heldPrototypeID, placePos, playerCtx.heldBlockColor));
                BlockSelectionSystemLogic::AddBlockToCache(baseSystem, prototypes, playerCtx.targetedWorldIndex, placePos, playerCtx.heldPrototypeID);
                StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, playerCtx.targetedWorldIndex, placePos);
            }
            if (audioVisualizerProto && playerCtx.heldPrototypeID == audioVisualizerProto->prototypeID) {
                RayTracedAudioSystemLogic::InvalidateSourceCache(baseSystem);
            }
            triggerGameplaySfx(baseSystem, "place_block.ck", 0.02f);
            playerCtx.isHoldingBlock = false;
            playerCtx.heldPrototypeID = -1;
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
                if (!targetIsBlueprint
                    && isNaturalSurfaceStonePrototypeName(targetProto.name)
                    && playerCtx.leftMousePressed) {
                    const int ingotMaterial = hatchetMaterialFromGemKind(gems->heldDrop.kind);
                    const int ingotPrototypeID = resolveGemIngotPrototypeID(prototypes, ingotMaterial);
                    if (ingotPrototypeID >= 0) {
                        playerCtx.isHoldingBlock = true;
                        playerCtx.heldPrototypeID = ingotPrototypeID;
                        playerCtx.heldBlockColor = glm::vec3(1.0f);
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

        if (player.isHoldingBlock) {
            if (!legacyDestroyMode) {
                tryPlaceHeldBlock(player);
                resetChargeState();
                return;
            }
            player.isHoldingBlock = false;
            player.heldPrototypeID = -1;
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
        const bool swappedControls = (!legacyDestroyMode && player.buildMode == BuildModeType::Pickup && player.blockChargeControlsSwapped);
        const bool pickupChargeDown = swappedControls ? player.leftMouseDown : player.rightMouseDown;
        const bool pickupChargePressed = swappedControls ? player.leftMousePressed : player.rightMousePressed;
        const bool pickupExecutePressed = swappedControls ? player.rightMousePressed : player.leftMousePressed;
        const bool destroyChargeDown = swappedControls ? player.rightMouseDown : player.leftMouseDown;
        const bool destroyChargePressed = swappedControls ? player.rightMousePressed : player.leftMousePressed;
        const bool destroyExecutePressed = swappedControls ? player.leftMousePressed : player.rightMousePressed;

        BlockChargeAction activeAction = player.blockChargeAction;
        const bool destroyUnlocked = player.hatchetHeld;
        if (legacyDestroyMode) {
            activeAction = destroyUnlocked ? BlockChargeAction::Destroy : BlockChargeAction::None;
        } else if (activeAction == BlockChargeAction::Destroy && !destroyUnlocked) {
            activeAction = BlockChargeAction::None;
        } else if (activeAction == BlockChargeAction::None) {
            if (!player.isHoldingBlock && !(gems && gems->blockModeHoldingGem)) {
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
            if (!player.isChargingBlock) {
                player.blockChargeValue = 0.0f;
            }
            player.isChargingBlock = true;
            player.blockChargeAction = activeAction;
            const bool destroyAction = (activeAction == BlockChargeAction::Destroy);
            float chargeTime = destroyAction ? CHARGE_TIME_DESTROY : CHARGE_TIME_PICKUP;
            player.blockChargeValue += dt / chargeTime;
            if (player.blockChargeValue >= 1.0f) {
                player.blockChargeValue = 1.0f;
                player.blockChargeReady = true;
            }
        } else {
            resetChargeState();
        }

        bool executePickup = player.blockChargeAction == BlockChargeAction::Pickup
            && pickupExecutePressed
            && pickupChargeDown;
        bool executeDestroy = player.blockChargeAction == BlockChargeAction::Destroy
            && destroyExecutePressed
            && destroyChargeDown;

        if (executePickup || executeDestroy) {
            if (player.blockChargeReady) {
                bool actionPerformed = false;
                const bool destroyAction = executeDestroy;

                if (!destroyAction
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

                if (!actionPerformed
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
                        resetChargeState();
                        return;
                    }
                    if (destroyAction) {
                        const glm::ivec3 targetCell = glm::ivec3(glm::round(player.targetedBlockPosition));
                        if (GemChiselSystemLogic::StartGemChiselAtCell(baseSystem, targetCell, player.targetedWorldIndex)) {
                            actionPerformed = true;
                            resetChargeState();
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
                                resetChargeState();
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
                                    triggerGameplaySfx(baseSystem, "break_stone.ck", 0.02f);
                                    actionPerformed = true;
                                    resetChargeState();
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
                        clearBlockDamageAt(player.targetedWorldIndex, removedCell);
                        releaseLatchedAnchorAt(removedCell, player.targetedWorldIndex);
                        if (removedBlock.fromVoxel) {
                            StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, player.targetedWorldIndex, glm::vec3(removedBlock.voxelCell));
                        } else {
                            BlockSelectionSystemLogic::RemoveBlockFromCache(baseSystem, prototypes, player.targetedWorldIndex, player.targetedBlockPosition);
                            StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, player.targetedWorldIndex, player.targetedBlockPosition);
                        }
                        // Ore rewards are owned by the ore-mining minigame success path.
                        // Direct destroy should never spawn gem rewards.
                        if (!destroyAction) {
                            player.isHoldingBlock = true;
                            player.heldPrototypeID = removedBlock.prototypeID;
                            player.heldBlockColor = removedBlock.color;
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
            resetChargeState();
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

    void RenderBlockDamage(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt;
        (void)win;
        // Crack lines were replaced by in-shader erosion masks. Keep this hook to
        // prune stale damage state for systems that still call it.
        std::vector<BlockDamageMaskRenderEntry> entries;
        collectBlockDamageMaskEntries(baseSystem, prototypes, entries);
    }
}
