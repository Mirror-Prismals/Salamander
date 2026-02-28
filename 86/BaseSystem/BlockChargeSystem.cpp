#pragma once

#include <algorithm>
#include <array>
#include <cctype>
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

        bool isStickPrototypeName(const std::string& name) {
            return name == "StickTexX" || name == "StickTexZ";
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

        bool hasWallStoneAtCell(const BaseSystem& baseSystem,
                                const LevelContext& level,
                                const std::vector<Entity>& prototypes,
                                const glm::ivec3& cell,
                                int worldIndexHint) {
            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
                if (id > 0 && id < prototypes.size() && isWallStonePrototypeID(prototypes, static_cast<int>(id))) {
                    return true;
                }
            }
            if (worldIndexHint < 0 || worldIndexHint >= static_cast<int>(level.worlds.size())) return false;
            const Entity& world = level.worlds[static_cast<size_t>(worldIndexHint)];
            const glm::vec3 cellPos = glm::vec3(cell);
            for (const auto& inst : world.instances) {
                if (glm::distance(inst.position, cellPos) > POSITION_EPSILON) continue;
                if (isWallStonePrototypeID(prototypes, inst.prototypeID)) return true;
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
    }

    void UpdateBlockCharge(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.player || !baseSystem.level) return;
        PlayerContext& player = *baseSystem.player;
        LevelContext& level = *baseSystem.level;
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
        GemContext* gems = baseSystem.gems ? baseSystem.gems.get() : nullptr;
        auto clearHeldGem = [&]() {
            if (!gems) return;
            gems->blockModeHoldingGem = false;
            gems->heldDrop = GemDropState{};
        };

        auto clampHatchetMaterial = [&](int material) {
            return std::clamp(material, 0, HATCHET_MATERIAL_COUNT - 1);
        };
        auto refreshHatchetState = [&](PlayerContext& p) {
            for (int i = 0; i < HATCHET_MATERIAL_COUNT; ++i) {
                int& count = p.hatchetInventoryByMaterial[static_cast<size_t>(i)];
                if (count < 0) count = 0;
            }

            // Backward compatibility with prior single-count model.
            int migratedTotal = sumHatchetInventory(p);
            if (migratedTotal == 0 && !p.hatchetPlacedInWorld && p.hatchetInventoryCount > 0) {
                p.hatchetInventoryByMaterial[static_cast<size_t>(HATCHET_MATERIAL_STONE)] = p.hatchetInventoryCount;
                migratedTotal = sumHatchetInventory(p);
            }
            if (migratedTotal == 0 && !p.hatchetPlacedInWorld && p.hatchetHeld) {
                p.hatchetInventoryByMaterial[static_cast<size_t>(HATCHET_MATERIAL_STONE)] = 1;
                migratedTotal = 1;
            }

            p.hatchetSelectedMaterial = clampHatchetMaterial(p.hatchetSelectedMaterial);
            p.hatchetPlacedMaterial = clampHatchetMaterial(p.hatchetPlacedMaterial);

            // New invariant: only one hatchet can exist at a time.
            // If one is placed in world, inventory must be empty.
            if (p.hatchetPlacedInWorld && migratedTotal > 0) {
                for (int i = 0; i < HATCHET_MATERIAL_COUNT; ++i) {
                    p.hatchetInventoryByMaterial[static_cast<size_t>(i)] = 0;
                }
                migratedTotal = 0;
            }
            // If inventory has multiple materials from old state, keep one.
            if (!p.hatchetPlacedInWorld && migratedTotal > 1) {
                int keepMaterial = p.hatchetSelectedMaterial;
                if (p.hatchetInventoryByMaterial[static_cast<size_t>(keepMaterial)] <= 0) {
                    keepMaterial = firstAvailableHatchetMaterial(p);
                }
                for (int i = 0; i < HATCHET_MATERIAL_COUNT; ++i) {
                    p.hatchetInventoryByMaterial[static_cast<size_t>(i)] = (i == keepMaterial) ? 1 : 0;
                }
                migratedTotal = 1;
                p.hatchetSelectedMaterial = keepMaterial;
            }

            p.hatchetInventoryCount = migratedTotal;
            p.hatchetHeld = (migratedTotal > 0);
            if (migratedTotal > 0
                && p.hatchetInventoryByMaterial[static_cast<size_t>(p.hatchetSelectedMaterial)] <= 0) {
                p.hatchetSelectedMaterial = firstAvailableHatchetMaterial(p);
            }
        };
        refreshHatchetState(player);

        bool legacyDestroyMode = player.buildMode == BuildModeType::Destroy;
        bool interactionMode = player.buildMode == BuildModeType::Pickup || legacyDestroyMode;
        bool fishingMode = player.buildMode == BuildModeType::Fishing;
        bool boulderingMode = player.buildMode == BuildModeType::Bouldering;
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
                if (!hasWallStoneAtCell(baseSystem, level, prototypes, cell, worldIndex)) {
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
                if (!isWallStonePrototypeID(prototypes, targetPrototypeID)) return false;

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

            // Craft hatchet: place a surface stone onto a surface stick.
            if (isSurfaceStonePrototypeName(heldProto.name)) {
                bool recognizedStoneMaterial = false;
                int craftedMaterial = hatchetMaterialFromStonePrototypeName(heldProto.name, &recognizedStoneMaterial);
                if (!recognizedStoneMaterial) {
                    craftedMaterial = detectHatchetMaterialFromColor(playerCtx.heldBlockColor);
                }
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
                                &removedStick)) {
                            if (removedStick.fromVoxel) {
                                StructureCaptureSystemLogic::NotifyBlockChanged(
                                    baseSystem,
                                    playerCtx.targetedWorldIndex,
                                    glm::vec3(removedStick.voxelCell)
                                );
                            } else {
                                BlockSelectionSystemLogic::RemoveBlockFromCache(
                                    baseSystem,
                                    prototypes,
                                    playerCtx.targetedWorldIndex,
                                    playerCtx.targetedBlockPosition
                                );
                                StructureCaptureSystemLogic::NotifyBlockChanged(
                                    baseSystem,
                                    playerCtx.targetedWorldIndex,
                                    playerCtx.targetedBlockPosition
                                );
                            }

                            glm::vec3 placeNormal = normalizeOrDefault(playerCtx.targetedBlockNormal, glm::vec3(0.0f, 1.0f, 0.0f));
                            glm::vec3 placeForward = cameraForwardDirection(playerCtx);
                            // Crafting a hatchet replaces any existing hatchet state.
                            for (int i = 0; i < HATCHET_MATERIAL_COUNT; ++i) {
                                playerCtx.hatchetInventoryByMaterial[static_cast<size_t>(i)] = 0;
                            }
                            playerCtx.hatchetPlacedInWorld = true;
                            playerCtx.hatchetPlacedCell = targetCell;
                            playerCtx.hatchetPlacedWorldIndex = playerCtx.targetedWorldIndex;
                            playerCtx.hatchetPlacedNormal = placeNormal;
                            playerCtx.hatchetPlacedDirection = projectDirectionOnSurface(placeForward, placeNormal);
                            playerCtx.hatchetPlacedPosition = glm::vec3(targetCell) - placeNormal * 0.47f;
                            playerCtx.hatchetPlacedMaterial = craftedMaterial;
                            playerCtx.hatchetSelectedMaterial = craftedMaterial;
                            refreshHatchetState(playerCtx);
                            playerCtx.isHoldingBlock = false;
                            playerCtx.heldPrototypeID = -1;
                            triggerGameplaySfx(baseSystem, "place_block.ck", 0.02f);
                            return;
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
            if (!playerCtx.leftMousePressed) return;
            if (!playerCtx.hasBlockTarget || glm::length(playerCtx.targetedBlockNormal) < 0.1f) return;
            if (playerCtx.targetedWorldIndex < 0 || playerCtx.targetedWorldIndex >= static_cast<int>(level.worlds.size())) return;

            // Chipping station: placing a raw gem on a surface stone converts it into
            // a placeable "ingot" block (same stone-pebble shape) held in hand.
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
            (void)targetCell;
            (void)targetFromVoxel;
            if (targetPrototypeID >= 0 && targetPrototypeID < static_cast<int>(prototypes.size())) {
                const Entity& targetProto = prototypes[static_cast<size_t>(targetPrototypeID)];
                if (isNaturalSurfaceStonePrototypeName(targetProto.name)) {
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

            glm::vec3 placePos = playerCtx.targetedBlockPosition + playerCtx.targetedBlockNormal;
            if (BlockSelectionSystemLogic::HasBlockAt(baseSystem, prototypes, playerCtx.targetedWorldIndex, placePos)) return;

            GemSystemLogic::PlaceGemDrop(baseSystem, std::move(gems->heldDrop), placePos);
            gems->blockModeHoldingGem = false;
            gems->heldDrop = GemDropState{};
            triggerGameplaySfx(baseSystem, "place_block.ck", 0.02f);
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

        static bool bPressedLastFrame = false;
        const bool bPressed = (win && glfwGetKey(win, GLFW_KEY_B) == GLFW_PRESS);
        const bool bJustPressed = bPressed && !bPressedLastFrame;
        bPressedLastFrame = bPressed;
        if (bJustPressed && !player.isHoldingBlock && !(gems && gems->blockModeHoldingGem)) {
            if (player.hatchetPlacedInWorld) {
                const bool hasTarget = player.hasBlockTarget && player.targetedWorldIndex >= 0;
                const glm::ivec3 targetCell = glm::ivec3(glm::round(player.targetedBlockPosition));
                const bool worldMatches = (player.hatchetPlacedWorldIndex < 0)
                    || (player.targetedWorldIndex == player.hatchetPlacedWorldIndex);
                const bool cellMatches = (targetCell == player.hatchetPlacedCell);
                if (hasTarget && worldMatches && cellMatches) {
                    const int pickedMaterial = clampHatchetMaterial(player.hatchetPlacedMaterial);
                    player.hatchetPlacedInWorld = false;
                    player.hatchetPlacedWorldIndex = -1;
                    for (int i = 0; i < HATCHET_MATERIAL_COUNT; ++i) {
                        player.hatchetInventoryByMaterial[static_cast<size_t>(i)] = 0;
                    }
                    player.hatchetInventoryByMaterial[static_cast<size_t>(pickedMaterial)] = 1;
                    player.hatchetSelectedMaterial = pickedMaterial;
                    refreshHatchetState(player);
                    triggerGameplaySfx(baseSystem, "pickup_block.ck", 0.02f);
                    resetChargeState();
                    return;
                }
            } else if (player.hatchetInventoryCount > 0
                       && player.hasBlockTarget
                       && glm::length(player.targetedBlockNormal) > 0.1f
                       && player.targetedWorldIndex >= 0
                       && player.targetedWorldIndex < static_cast<int>(level.worlds.size())) {
                glm::vec3 placeNormal = normalizeOrDefault(player.targetedBlockNormal, glm::vec3(0.0f, 1.0f, 0.0f));
                glm::ivec3 placeCell = glm::ivec3(glm::round(player.targetedBlockPosition + placeNormal));
                int placeWorld = player.targetedWorldIndex;
                if (!BlockSelectionSystemLogic::HasBlockAt(baseSystem, prototypes, placeWorld, glm::vec3(placeCell))) {
                    int placeMaterial = clampHatchetMaterial(player.hatchetSelectedMaterial);
                    if (player.hatchetInventoryByMaterial[static_cast<size_t>(placeMaterial)] <= 0) {
                        placeMaterial = firstAvailableHatchetMaterial(player);
                    }
                    if (player.hatchetInventoryByMaterial[static_cast<size_t>(placeMaterial)] <= 0) {
                        refreshHatchetState(player);
                        return;
                    }
                    glm::vec3 placeForward = cameraForwardDirection(player);
                    player.hatchetPlacedInWorld = true;
                    player.hatchetPlacedCell = placeCell;
                    player.hatchetPlacedWorldIndex = placeWorld;
                    player.hatchetPlacedNormal = placeNormal;
                    player.hatchetPlacedDirection = projectDirectionOnSurface(placeForward, placeNormal);
                    player.hatchetPlacedPosition = glm::vec3(placeCell) - placeNormal * 0.47f;
                    player.hatchetPlacedMaterial = placeMaterial;
                    player.hatchetSelectedMaterial = placeMaterial;
                    player.hatchetInventoryByMaterial[static_cast<size_t>(placeMaterial)] =
                        std::max(0, player.hatchetInventoryByMaterial[static_cast<size_t>(placeMaterial)] - 1);
                    refreshHatchetState(player);
                    triggerGameplaySfx(baseSystem, "place_block.ck", 0.02f);
                    resetChargeState();
                    return;
                }
            }
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
        const bool destroyUnlocked = (player.hatchetInventoryCount > 0);
        if (legacyDestroyMode) {
            activeAction = BlockChargeAction::Destroy;
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

                // In pickup action, gem interaction takes priority so terrain blocks don't consume the click first.
                if (!destroyAction && gems && !player.isHoldingBlock && !gems->blockModeHoldingGem) {
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
                    RemovedBlockInfo removedBlock;
                    if (RemoveBlockAtPosition(baseSystem, level, prototypes, player.targetedBlockPosition, player.targetedWorldIndex, &removedBlock)) {
                        const glm::ivec3 removedCell = removedBlock.fromVoxel
                            ? removedBlock.voxelCell
                            : glm::ivec3(glm::round(player.targetedBlockPosition));
                        releaseLatchedAnchorAt(removedCell, player.targetedWorldIndex);
                        if (removedBlock.fromVoxel) {
                            StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, player.targetedWorldIndex, glm::vec3(removedBlock.voxelCell));
                        } else {
                            BlockSelectionSystemLogic::RemoveBlockFromCache(baseSystem, prototypes, player.targetedWorldIndex, player.targetedBlockPosition);
                            StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, player.targetedWorldIndex, player.targetedBlockPosition);
                        }
                        if (destroyAction
                            && removedBlock.prototypeID >= 0
                            && removedBlock.prototypeID < static_cast<int>(prototypes.size())) {
                            const glm::vec3 minedPos = removedBlock.fromVoxel
                                ? glm::vec3(removedBlock.voxelCell)
                                : player.targetedBlockPosition;
                            glm::vec3 spawnForward = -player.targetedBlockNormal;
                            if (glm::length(spawnForward) < 0.01f) spawnForward = glm::vec3(0.0f, 0.0f, -1.0f);
                            GemSystemLogic::SpawnGemDropFromOre(baseSystem, prototypes, removedBlock.prototypeID, minedPos, spawnForward);
                        }
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
}
