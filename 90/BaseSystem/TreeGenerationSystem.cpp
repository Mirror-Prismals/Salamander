#pragma once
#include "Host/PlatformInput.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <array>
#include <chrono>

namespace HostLogic { const Entity* findPrototype(const std::string& name, const std::vector<Entity>& prototypes); }
namespace AudioSystemLogic { bool TriggerGameplaySfx(BaseSystem& baseSystem, const std::string& cueName, float gain); }
namespace ExpanseBiomeSystemLogic {
    bool SampleTerrain(const WorldContext& worldCtx, float x, float z, float& outHeight);
    int ResolveBiome(const WorldContext& worldCtx, float x, float z);
}
namespace RenderInitSystemLogic {
    int FaceTileIndexFor(const WorldContext* worldCtx, const Entity& proto, int faceType);
}
namespace TerrainSystemLogic {
    bool IsSectionTerrainReady(const VoxelSectionKey& key);
    bool IsColumnSurfaceReady(int lod, const glm::ivec2& columnCoord);
    void GetVoxelStreamingPerfStats(size_t& pending,
                                    size_t& desired,
                                    size_t& generated,
                                    size_t& jobs,
                                    int& stepped,
                                    int& built,
                                    int& consumed,
                                    int& skippedExisting,
                                    int& filteredOut,
                                    int& rescueSurfaceQueued,
                                    int& rescueMissingQueued,
                                    int& droppedByCap,
                                    int& reprioritized,
                                    float& generationMs);
}
namespace VoxelMeshingSystemLogic { void RequestPriorityVoxelRemesh(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const glm::ivec3& worldCell); }

namespace TreeGenerationSystemLogic {
    static std::unordered_map<VoxelColumnModel::ColumnKey, bool, VoxelColumnModel::ColumnKeyHash> g_treeColumnFoliageReady;
    static std::unordered_map<VoxelColumnModel::ColumnKey, uint64_t, VoxelColumnModel::ColumnKeyHash> g_treeColumnFoliageLastSeenFrame;

    namespace {
        struct PineSpec {
            int trunkHeight = 30;
            int canopyOffset = 26;
            int canopyLayers = 30;
            float canopyBottomRadius = 3.0f;
            float canopyTopRadius = 1.0f;
            int canopyLowerExtension = 2;      // extra canopy layers pushed lower (toward ground)
            float canopyLowerRadiusBoost = 1.0f; // wider ring on the lower extension
            int spawnModulo = 100;
            int trunkExclusionRadius = 3;
        };

        struct FoliageSpec {
            bool enabled = true;
            bool grassEnabled = true;
            bool grassCoverEnabled = true;
            bool pebblePatchEnabled = true;
            bool autumnLeafPatchEnabled = true;
            bool lilypadPatchEnabled = true;
            bool flowerEnabled = true;
            bool stickEnabled = true;
            bool waterFoliageEnabled = true;
            bool caveStoneEnabled = true;
            bool caveSlopeEnabled = true;
            bool caveWallStoneEnabled = true;
            bool caveCeilingStoneEnabled = true;
            bool temperateOnly = true;
            int grassSpawnModulo = 1;
            int grassCoverPercent = 35;
            int pebblePatchPercent = 18;
            int pebblePatchNearWaterRadius = 10;
            int pebblePatchNearWaterVerticalRange = 4;
            int autumnLeafPatchPercent = 18;
            int lilypadPatchPercent = 2;
            int flowerSpawnModulo = 41;
            int miniPineSpawnModulo = 180;
            int shortGrassPercent = 50;
            int grassTuftPercent = 45;
            int stickSpawnPercent = 18;
            int stickCanopySearchRadius = 1;
            int stickCanopySearchHeight = 22;
            int kelpSpawnPercent = 24;
            int seaUrchinSpawnPercent = 10;
            int sandDollarSpawnPercent = 22;
            int caveStoneSpawnPercent = 10;
            int caveStoneMinDepthFromSurface = 4;
            bool cavePotEnabled = true;
            int cavePotSpawnPercent = 25;
            int cavePotMinDepthFromSurface = 3;
            int cavePotPileMaxCount = 3;
            int caveSlopeSpawnPercent = 12;
            int caveSlopeMinDepthFromSurface = 4;
            int caveSlopeLargePercent = 35;
            int caveSlopeHugePercent = 18;
            int caveWallStoneSpawnPercent = 8;
            int caveWallStoneMinDepthFromSurface = 4;
            int caveCeilingStoneSpawnPercent = 8;
            int caveCeilingStoneMinDepthFromSurface = 4;
        };

        struct PineNubPlacement {
            int trunkOffsetY = 0;
            glm::ivec2 dir = glm::ivec2(1, 0); // x,z step on trunk side
        };

        static std::unordered_map<VoxelSectionKey, uint32_t, VoxelSectionKeyHash> g_treeAppliedVersion;
        static std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> g_treeBackfillVisited;
        static std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> g_treePendingDependencies;
        static std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> g_treePendingSections;
        static std::string g_treeLevelKey;
        static std::vector<std::pair<glm::ivec3, int>> g_pendingPineLogRemovals;
        static uint64_t g_treeFrameCounter = 0;
        static uint64_t g_treeFoliageSignature = 0;

        struct TreeFoliagePerfStats {
            size_t pendingSections = 0;
            size_t pendingDependencies = 0;
            size_t backfillVisited = 0;
            int selectedSections = 0;
            int processedSections = 0;
            int deferredByTimeBudget = 0;
            int backfillAppended = 0;
            bool backfillRan = false;
            float updateMs = 0.0f;
        };
        static TreeFoliagePerfStats g_treePerfStats;

        bool cellBelongsToSection(const glm::ivec3& worldCell, const glm::ivec3& sectionCoord, int sectionSize);

        int floorDivInt(int value, int divisor) {
            if (divisor <= 0) return 0;
            if (value >= 0) return value / divisor;
            return -(((-value) + divisor - 1) / divisor);
        }

        int sectionSizeForLod(const VoxelWorldContext& voxelWorld, int lod) {
            int size = voxelWorld.sectionSize >> lod;
            return size > 0 ? size : 1;
        }

        int sectionScaleForLod(int lod) {
            if (lod <= 0) return 1;
            return 1 << lod;
        }

        uint32_t getBlockAtLod(const VoxelWorldContext& voxelWorld,
                               int lod,
                               const glm::ivec3& lodCoord) {
            if (lod < 0) return 0u;
            const int size = sectionSizeForLod(voxelWorld, lod);
            const glm::ivec3 sectionCoord(
                floorDivInt(lodCoord.x, size),
                floorDivInt(lodCoord.y, size),
                floorDivInt(lodCoord.z, size)
            );
            const glm::ivec3 local = lodCoord - sectionCoord * size;
            VoxelSectionKey key{lod, sectionCoord};
            auto it = voxelWorld.sections.find(key);
            if (it == voxelWorld.sections.end()) return 0u;
            const VoxelSection& section = it->second;
            int idx = local.x + local.y * section.size + local.z * section.size * section.size;
            if (idx < 0 || idx >= static_cast<int>(section.ids.size())) return 0u;
            return section.ids[idx];
        }

        uint32_t getColorAtLod(const VoxelWorldContext& voxelWorld,
                               int lod,
                               const glm::ivec3& lodCoord) {
            if (lod < 0) return 0u;
            const int size = sectionSizeForLod(voxelWorld, lod);
            const glm::ivec3 sectionCoord(
                floorDivInt(lodCoord.x, size),
                floorDivInt(lodCoord.y, size),
                floorDivInt(lodCoord.z, size)
            );
            const glm::ivec3 local = lodCoord - sectionCoord * size;
            VoxelSectionKey key{lod, sectionCoord};
            auto it = voxelWorld.sections.find(key);
            if (it == voxelWorld.sections.end()) return 0u;
            const VoxelSection& section = it->second;
            int idx = local.x + local.y * section.size + local.z * section.size * section.size;
            if (idx < 0 || idx >= static_cast<int>(section.colors.size())) return 0u;
            return section.colors[idx];
        }

        bool isGroundDependencyPending(const VoxelWorldContext& voxelWorld,
                                       int lod,
                                       const glm::ivec3& lodCell,
                                       int sectionSize) {
            VoxelSectionKey groundKey{
                lod,
                glm::ivec3(
                    floorDivInt(lodCell.x, sectionSize),
                    floorDivInt(lodCell.y, sectionSize),
                    floorDivInt(lodCell.z, sectionSize)
                )
            };
            auto groundIt = voxelWorld.sections.find(groundKey);
            if (groundIt == voxelWorld.sections.end()) return true;
            // Non-empty, versioned sections are treated as stable even if this exact cell is air.
            // This avoids perpetual retries for valid holes/caves/player edits.
            if (groundIt->second.nonAirCount <= 0) return true;
            if (groundIt->second.editVersion == 0) return true;
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

        int encodeGrassCoverSnapshotTile(int tileIndex) {
            // 0 means "no snapshot"; store tile as +1 in the high byte.
            if (tileIndex < 0 || tileIndex > 254) return 0;
            return tileIndex + 1;
        }

        uint32_t withGrassCoverSnapshotTile(uint32_t packedColorRgb, int tileIndex) {
            const uint32_t rgb = packedColorRgb & 0x00ffffffu;
            const uint32_t encoded = static_cast<uint32_t>(encodeGrassCoverSnapshotTile(tileIndex) & 0xff) << 24;
            return rgb | encoded;
        }

        int decodeGrassCoverSnapshotTile(uint32_t packedColor) {
            const int encoded = static_cast<int>((packedColor >> 24) & 0xffu);
            if (encoded <= 0) return -1;
            // Water metadata now occupies high-byte values whose low nibble is marker [0..5]
            // and high nibble is wave class [0..4]. These are not grass snapshot tiles.
            const int marker = encoded & 0x0f;
            const int waveClass = (encoded >> 4) & 0x0f;
            if (marker <= 5 && waveClass <= 4) return -1;
            return encoded - 1;
        }

        constexpr uint8_t kWaterFoliageMarkerNone = 0u;
        constexpr uint8_t kWaterFoliageMarkerKelp = 1u;
        constexpr uint8_t kWaterFoliageMarkerSeaUrchinX = 2u;
        constexpr uint8_t kWaterFoliageMarkerSeaUrchinZ = 3u;
        constexpr uint8_t kWaterFoliageMarkerSandDollarX = 4u;
        constexpr uint8_t kWaterFoliageMarkerSandDollarZ = 5u;
        constexpr uint8_t kWaterWaveClassUnknown = 0u;
        constexpr uint8_t kWaterWaveClassRiver = 3u;
        constexpr uint8_t kWaterWaveClassOcean = 4u;

        uint8_t waterWaveClassFromPackedColor(uint32_t packedColor) {
            const uint8_t encoded = static_cast<uint8_t>((packedColor >> 24) & 0xffu);
            if (encoded <= kWaterFoliageMarkerSandDollarZ) {
                // Legacy marker-only format has no wave class.
                return kWaterWaveClassUnknown;
            }
            const uint8_t marker = static_cast<uint8_t>(encoded & 0x0fu);
            const uint8_t waveClass = static_cast<uint8_t>((encoded >> 4u) & 0x0fu);
            if (marker <= kWaterFoliageMarkerSandDollarZ && waveClass <= kWaterWaveClassOcean) {
                return waveClass;
            }
            return kWaterWaveClassUnknown;
        }

        uint32_t withWaterFoliageMarker(uint32_t packedColor, uint8_t marker) {
            const uint32_t rgb = packedColor & 0x00ffffffu;
            const uint8_t waveClass = waterWaveClassFromPackedColor(packedColor);
            const uint8_t encoded = static_cast<uint8_t>(((waveClass & 0x0fu) << 4u) | (marker & 0x0fu));
            return rgb | (static_cast<uint32_t>(encoded) << 24u);
        }

        uint8_t waterFoliageMarkerFromPackedColor(uint32_t packedColor) {
            const uint8_t encoded = static_cast<uint8_t>((packedColor >> 24) & 0xffu);
            if (encoded <= kWaterFoliageMarkerSandDollarZ) {
                // Legacy marker-only format.
                return encoded;
            }
            const uint8_t marker = static_cast<uint8_t>(encoded & 0x0fu);
            const uint8_t waveClass = static_cast<uint8_t>((encoded >> 4u) & 0x0fu);
            if (marker <= kWaterFoliageMarkerSandDollarZ && waveClass <= kWaterWaveClassOcean) {
                return marker;
            }
            return kWaterFoliageMarkerNone;
        }

        int getRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stoi(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<bool>(it->second)) return fallback;
            return std::get<bool>(it->second);
        }

        float getRegistryFloat(const BaseSystem& baseSystem, const std::string& key, float fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stof(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        std::string getRegistryString(const BaseSystem& baseSystem,
                                      const std::string& key,
                                      const std::string& fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            return std::get<std::string>(it->second);
        }

        bool triggerGameplaySfx(BaseSystem& baseSystem, const char* fileName, float cooldownSeconds = 0.0f) {
            if (!fileName) return false;
            static std::unordered_map<std::string, double> s_lastTrigger;
            const double now = PlatformInput::GetTimeSeconds();
            const std::string keyName(fileName);
            auto it = s_lastTrigger.find(keyName);
            if (it != s_lastTrigger.end() && (now - it->second) < static_cast<double>(cooldownSeconds)) {
                return false;
            }

            if (AudioSystemLogic::TriggerGameplaySfx(baseSystem, keyName, 1.0f)) {
                s_lastTrigger[keyName] = now;
                return true;
            }

            if (!getRegistryBool(baseSystem, "GameplaySfxFallbackToChuck", false)) return false;
            if (!baseSystem.audio || !baseSystem.audio->chuck) return false;
            const std::string scriptPath = std::string("Procedures/chuck/gameplay/") + fileName;
            std::vector<t_CKUINT> ids;
            bool ok = baseSystem.audio->chuck->compileFile(scriptPath, "", 1, FALSE, &ids);
            if (ok && !ids.empty()) {
                s_lastTrigger[keyName] = now;
                return true;
            }
            return false;
        }

        uint32_t hash2D(int x, int z) {
            uint32_t ux = static_cast<uint32_t>(x) * 73856093u;
            uint32_t uz = static_cast<uint32_t>(z) * 19349663u;
            uint32_t h = ux ^ uz;
            h ^= (h >> 13);
            h *= 1274126177u;
            h ^= (h >> 16);
            return h;
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

        struct IVec3Hash {
            std::size_t operator()(const glm::ivec3& v) const noexcept {
                std::size_t hx = std::hash<int>()(v.x);
                std::size_t hy = std::hash<int>()(v.y);
                std::size_t hz = std::hash<int>()(v.z);
                return hx ^ (hy << 1) ^ (hz << 2);
            }
        };

        enum class FallDirection : int { PosX = 0, NegX = 1, PosZ = 2, NegZ = 3 };

        bool isPineVerticalLogName(const std::string& name) {
            return name == "FirLog1Tex" || name == "FirLog2Tex"
                || name == "FirLog1TopTex" || name == "FirLog2TopTex";
        }

        bool isPineAnyLogName(const std::string& name) {
            return isPineVerticalLogName(name)
                || name == "FirLog1TexX" || name == "FirLog2TexX"
                || name == "FirLog1TexZ" || name == "FirLog2TexZ"
                || name == "FirLog1NubTexX" || name == "FirLog2NubTexX"
                || name == "FirLog1NubTexZ" || name == "FirLog2NubTexZ";
        }

        bool isPineVerticalLogPrototypeID(const std::vector<Entity>& prototypes, int prototypeID) {
            return prototypeID >= 0
                && prototypeID < static_cast<int>(prototypes.size())
                && isPineVerticalLogName(prototypes[prototypeID].name);
        }

        bool isPineAnyLogPrototypeID(const std::vector<Entity>& prototypes, uint32_t prototypeID) {
            int id = static_cast<int>(prototypeID);
            return id >= 0
                && id < static_cast<int>(prototypes.size())
                && isPineAnyLogName(prototypes[id].name);
        }

        bool isLeafPrototypeName(const std::string& name) {
            return name == "Leaf"
                || name.rfind("LeafJungle", 0) == 0
                || name == "GrassTuftLeafFanOak"
                || name == "GrassTuftLeafFanPine";
        }

        bool isJungleLeafPrototypeName(const std::string& name) {
            return name.rfind("LeafJungle", 0) == 0;
        }

        bool isLeafPrototypeID(const std::vector<Entity>& prototypes, uint32_t prototypeID) {
            int id = static_cast<int>(prototypeID);
            return id >= 0
                && id < static_cast<int>(prototypes.size())
                && isLeafPrototypeName(prototypes[id].name);
        }

        bool isWithinJungleVolcano(const ExpanseConfig& cfg,
                                   int biomeID,
                                   int worldX,
                                   int worldZ) {
            if (!cfg.loaded
                || !cfg.jungleVolcanoEnabled
                || !cfg.secondaryBiomeEnabled
                || cfg.islandRadius <= 0.0f) {
                return false;
            }
            if (biomeID != 3) return false; // jungle quadrant only
            const float centerFactorX = std::clamp(cfg.jungleVolcanoCenterFactorX, 0.0f, 1.0f);
            const float centerFactorZ = std::clamp(cfg.jungleVolcanoCenterFactorZ, 0.0f, 1.0f);
            const float volcanoCenterX = cfg.islandCenterX + cfg.islandRadius * centerFactorX;
            const float volcanoCenterZ = cfg.islandCenterZ + cfg.islandRadius * centerFactorZ;
            const float volcanoRadius = std::max(8.0f, cfg.jungleVolcanoOuterRadius);
            const float dx = static_cast<float>(worldX) - volcanoCenterX;
            const float dz = static_cast<float>(worldZ) - volcanoCenterZ;
            return (dx * dx + dz * dz) <= (volcanoRadius * volcanoRadius);
        }

        bool startsWith(const std::string& value, const char* prefix) {
            if (!prefix) return false;
            const size_t prefixLen = std::strlen(prefix);
            return value.size() >= prefixLen
                && value.compare(0, prefixLen, prefix) == 0;
        }

        bool isGrassFoliagePrototypeName(const std::string& name) {
            return startsWith(name, "GrassTuft");
        }

        bool isFlowerFoliagePrototypeName(const std::string& name) {
            return startsWith(name, "Flower");
        }

        bool isFoliageGroundPrototypeID(const std::vector<Entity>& prototypes,
                                        uint32_t prototypeID,
                                        int waterPrototypeID) {
            int id = static_cast<int>(prototypeID);
            if (id <= 0 || id >= static_cast<int>(prototypes.size())) return false;
            if (id == waterPrototypeID) return false;
            const Entity& proto = prototypes[static_cast<size_t>(id)];
            if (!proto.isBlock || !proto.isSolid) return false;
            if (isLeafPrototypeName(proto.name)
                || isGrassFoliagePrototypeName(proto.name)
                || isFlowerFoliagePrototypeName(proto.name)
                || proto.name == "StickTexX"
                || proto.name == "StickTexZ"
                || proto.name == "StonePebbleTexX"
                || proto.name == "StonePebbleTexZ"
                || proto.name == "WallStoneTexPosX"
                || proto.name == "WallStoneTexNegX"
                || proto.name == "WallStoneTexPosZ"
                || proto.name == "WallStoneTexNegZ"
                || proto.name.rfind("CeilingStoneTex", 0) == 0) return false;
            return true;
        }

        uint32_t grassColorForCell(int worldX, int worldZ) {
            const uint32_t h = hash2D(worldX - 911, worldZ + 613);
            const float tint = static_cast<float>(h & 0xffu) / 255.0f;
            glm::vec3 c(0.18f, 0.67f, 0.25f);
            c += glm::vec3(0.07f, 0.09f, 0.05f) * (tint - 0.5f);
            c = glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
            return packColor(c);
        }

        uint32_t flowerColorForCell(int worldX, int worldZ) {
            static const std::array<glm::vec3, 6> kFlowerPalette = {
                glm::vec3(0.97f, 0.48f, 0.72f), // pink
                glm::vec3(0.94f, 0.72f, 0.27f), // marigold
                glm::vec3(0.99f, 0.95f, 0.85f), // white
                glm::vec3(0.86f, 0.61f, 0.96f), // lilac
                glm::vec3(0.96f, 0.43f, 0.35f), // coral
                glm::vec3(0.95f, 0.83f, 0.28f)  // yellow
            };
            const uint32_t h = hash2D(worldX + 257, worldZ - 419);
            return packColor(kFlowerPalette[h % kFlowerPalette.size()]);
        }

        constexpr int kBlueFlowerRareChance = 20;
        constexpr int kYoungClematisExtraRarityMultiplier = 6;
        constexpr int kMiniPineRarityMultiplier = 20;
        constexpr int kJungleLeafVariantCount = 16;
        constexpr int kRareFlowerPoolMax = 24;

        int chooseRareFlowerPrototypeIDForSeed(const std::array<int, kRareFlowerPoolMax>& prototypeIDs,
                                               int prototypeCount,
                                               uint32_t seed) {
            if (prototypeCount <= 0) return -1;
            const int clampedCount = std::max(1, std::min(static_cast<int>(prototypeIDs.size()), prototypeCount));
            return prototypeIDs[static_cast<size_t>(seed % static_cast<uint32_t>(clampedCount))];
        }

        int chooseJungleLeafPrototypeIDForCell(const std::array<int, kJungleLeafVariantCount>& prototypeIDs,
                                               int prototypeCount,
                                               const glm::ivec3& worldCell) {
            if (prototypeCount <= 0) return -1;
            const int clampedCount = std::max(1, std::min(static_cast<int>(prototypeIDs.size()), prototypeCount));
            const uint32_t seed = hash3D(worldCell.x + 1901, worldCell.y - 733, worldCell.z + 421);
            return prototypeIDs[static_cast<size_t>(seed % static_cast<uint32_t>(clampedCount))];
        }

        uint32_t stickColorForCell(int worldX, int worldZ) {
            const uint32_t h = hash2D(worldX + 777, worldZ - 1337);
            const float tint = static_cast<float>(h & 0xffu) / 255.0f;
            glm::vec3 c(0.29f, 0.21f, 0.13f);
            c += glm::vec3(0.045f, 0.030f, 0.020f) * (tint - 0.5f);
            c = glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
            return packColor(c);
        }

        bool isGrassSurfacePrototypeName(const std::string& name) {
            return name == "ScaffoldBlock"
                || startsWith(name, "GrassBlock");
        }

        bool isMeadowGrassSurfacePrototypeName(const std::string& name) {
            return name == "GrassBlockMeadowTex";
        }

        bool isForestFloorSurfacePrototypeName(const std::string& name) {
            return name == "GrassBlockTemperateTex";
        }

        bool isJungleGrassSurfacePrototypeName(const std::string& name) {
            return name == "GrassBlockJungleTex";
        }

        bool isBareWinterSurfacePrototypeName(const std::string& name) {
            return name == "GrassBlockBareWinterTex";
        }

        bool isGrassGrowthBlockedSurfacePrototypeName(const std::string& name) {
            return name == "ChalkBlockTex"
                || name == "ClayBlockTex"
                || name == "GraniteBlockTex";
        }

        bool isDesertGroundPrototypeName(const std::string& name) {
            return name == "SandBlockTex"
                || name == "SandBlockDesertTex"
                || name == "SandBlockBeachTex"
                || name == "SandBlockSeabedTex"
                || name == "ScaffoldBlock";
        }

        bool isStoneSurfacePrototypeName(const std::string& name) {
            return name == "StoneBlockTex" || name == "ScaffoldBlock";
        }

        bool isWallStonePrototypeName(const std::string& name) {
            return name == "WallStoneTexPosX"
                || name == "WallStoneTexNegX"
                || name == "WallStoneTexPosZ"
                || name == "WallStoneTexNegZ";
        }

        enum class CaveSlopeDir : int { None = 0, PosX = 1, NegX = 2, PosZ = 3, NegZ = 4 };

        CaveSlopeDir caveSlopeDirFromName(const std::string& name) {
            if (name == "DebugSlopeTexPosX") return CaveSlopeDir::PosX;
            if (name == "DebugSlopeTexNegX") return CaveSlopeDir::NegX;
            if (name == "DebugSlopeTexPosZ") return CaveSlopeDir::PosZ;
            if (name == "DebugSlopeTexNegZ") return CaveSlopeDir::NegZ;
            return CaveSlopeDir::None;
        }

        CaveSlopeDir caveSlopeDirFromExposedAirSide(const glm::ivec3& airOffset) {
            // Exposed air marks the low side; slope high side points opposite.
            if (airOffset.x > 0) return CaveSlopeDir::NegX;
            if (airOffset.x < 0) return CaveSlopeDir::PosX;
            // Z axes are flipped relative to the current slope prototype orientation.
            if (airOffset.z > 0) return CaveSlopeDir::PosZ;
            if (airOffset.z < 0) return CaveSlopeDir::NegZ;
            return CaveSlopeDir::None;
        }

        glm::ivec3 caveSlopeLowDirection(CaveSlopeDir dir) {
            switch (dir) {
                case CaveSlopeDir::PosX: return glm::ivec3(-1, 0, 0);
                case CaveSlopeDir::NegX: return glm::ivec3(1, 0, 0);
                case CaveSlopeDir::PosZ: return glm::ivec3(0, 0, -1);
                case CaveSlopeDir::NegZ: return glm::ivec3(0, 0, 1);
                default: return glm::ivec3(0, 0, 0);
            }
        }

        glm::ivec3 caveSlopePerpDirection(CaveSlopeDir dir) {
            switch (dir) {
                case CaveSlopeDir::PosX:
                case CaveSlopeDir::NegX:
                    return glm::ivec3(0, 0, 1);
                case CaveSlopeDir::PosZ:
                case CaveSlopeDir::NegZ:
                    return glm::ivec3(1, 0, 0);
                default:
                    return glm::ivec3(0, 0, 0);
            }
        }

        int caveSlopePrototypeForDir(CaveSlopeDir dir,
                                     int slopeProtoPosX,
                                     int slopeProtoNegX,
                                     int slopeProtoPosZ,
                                     int slopeProtoNegZ) {
            switch (dir) {
                case CaveSlopeDir::PosX: return slopeProtoPosX;
                case CaveSlopeDir::NegX: return slopeProtoNegX;
                case CaveSlopeDir::PosZ: return slopeProtoPosZ;
                case CaveSlopeDir::NegZ: return slopeProtoNegZ;
                default: return -1;
            }
        }

        uint32_t caveStoneColorForCell(int worldX, int worldY, int worldZ) {
            const uint32_t h = hash3D(worldX - 1021, worldY + 271, worldZ + 733);
            const float tint = 0.92f + 0.08f * (static_cast<float>(h & 0xffu) / 255.0f);
            const glm::vec3 c = glm::clamp(glm::vec3(tint), glm::vec3(0.0f), glm::vec3(1.0f));
            return packColor(c);
        }

        bool hasLeafCanopyNear(const VoxelWorldContext& voxelWorld,
                               int sectionLod,
                               int leafPrototypeID,
                               int lodX,
                               int groundLodY,
                               int lodZ,
                               int radius,
                               int searchHeight) {
            if (!voxelWorld.enabled || leafPrototypeID < 0) return false;
            const int clampedRadius = std::max(0, std::min(4, radius));
            const int clampedHeight = std::max(2, std::min(96, searchHeight));
            const int minY = groundLodY + 2;
            const int maxY = groundLodY + clampedHeight;
            for (int dz = -clampedRadius; dz <= clampedRadius; ++dz) {
                for (int dx = -clampedRadius; dx <= clampedRadius; ++dx) {
                    for (int y = minY; y <= maxY; ++y) {
                        const glm::ivec3 probe(lodX + dx, y, lodZ + dz);
                        if (getBlockAtLod(voxelWorld, sectionLod, probe) == static_cast<uint32_t>(leafPrototypeID)) {
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        bool findLeafHangingCellNear(const std::vector<Entity>& prototypes,
                                     const VoxelWorldContext& voxelWorld,
                                     int sectionLod,
                                     const glm::ivec3& sectionCoord,
                                     int sectionSize,
                                     int lodX,
                                     int groundLodY,
                                     int lodZ,
                                     int radius,
                                     int searchHeight,
                                     glm::ivec3& outCell) {
            if (!voxelWorld.enabled) return false;
            const int clampedRadius = std::max(0, std::min(4, radius));
            const int clampedHeight = std::max(2, std::min(96, searchHeight));
            const int minY = groundLodY + 2;
            const int maxY = groundLodY + clampedHeight;

            bool found = false;
            int bestY = std::numeric_limits<int>::max();
            int bestDist2 = std::numeric_limits<int>::max();
            glm::ivec3 bestCell(0);

            for (int dz = -clampedRadius; dz <= clampedRadius; ++dz) {
                for (int dx = -clampedRadius; dx <= clampedRadius; ++dx) {
                    const int dist2 = dx * dx + dz * dz;
                    for (int y = minY; y <= maxY; ++y) {
                        const glm::ivec3 leafCell(lodX + dx, y, lodZ + dz);
                        const uint32_t leafID = getBlockAtLod(voxelWorld, sectionLod, leafCell);
                        if (leafID == 0u || leafID >= prototypes.size()) continue;
                        if (!isLeafPrototypeName(prototypes[static_cast<size_t>(leafID)].name)) continue;

                        const glm::ivec3 hangingCell = leafCell + glm::ivec3(0, -1, 0);
                        if (!cellBelongsToSection(hangingCell, sectionCoord, sectionSize)) continue;
                        if (getBlockAtLod(voxelWorld, sectionLod, hangingCell) != 0u) continue;

                        if (!found || hangingCell.y < bestY || (hangingCell.y == bestY && dist2 < bestDist2)) {
                            found = true;
                            bestY = hangingCell.y;
                            bestDist2 = dist2;
                            bestCell = hangingCell;
                        }
                    }
                }
            }

            if (!found) return false;
            outCell = bestCell;
            return true;
        }

        bool shouldSpawnPine(int worldX, int worldZ, const PineSpec& spec) {
            return (hash2D(worldX, worldZ) % static_cast<uint32_t>(spec.spawnModulo)) == 0u;
        }

        PineSpec pineSpecForCell(const PineSpec& baseSpec,
                                 int worldX,
                                 int worldZ,
                                 int minTrunkHeight,
                                 int maxTrunkHeight) {
            PineSpec out = baseSpec;

            int minH = std::max(6, minTrunkHeight);
            int maxH = std::max(minH, maxTrunkHeight);
            if (maxH < minH) std::swap(minH, maxH);

            const uint32_t h = hash2D(worldX + 9091, worldZ - 7919);
            const int span = maxH - minH + 1;
            out.trunkHeight = minH + static_cast<int>(h % static_cast<uint32_t>(span));

            // Preserve the original canopy base/top relationship relative to trunk height.
            const int baseCanopyBase = baseSpec.trunkHeight - baseSpec.canopyOffset;
            const int baseTopOverhang = baseSpec.canopyLayers - baseSpec.canopyOffset - 1;
            const int canopyBase = std::max(2, baseCanopyBase);
            const int canopyTop = std::max(canopyBase, out.trunkHeight + baseTopOverhang);
            out.canopyOffset = std::max(1, out.trunkHeight - canopyBase);
            out.canopyLayers = std::max(1, canopyTop - canopyBase + 1);

            // Short trees: reduce peak canopy thickness by 1 block.
            if (out.trunkHeight < 20) {
                out.canopyBottomRadius = std::max(out.canopyTopRadius, out.canopyBottomRadius - 1.0f);
            }
            return out;
        }

        PineSpec scaledPineSpecForLod(const PineSpec& source, int sectionScale) {
            if (sectionScale <= 1) return source;
            PineSpec out = source;
            auto scaleIntCeil = [&](int v, int minValue) {
                if (v <= 0) return minValue;
                return std::max(minValue, (v + sectionScale - 1) / sectionScale);
            };
            out.trunkHeight = scaleIntCeil(source.trunkHeight, 2);
            out.canopyOffset = scaleIntCeil(source.canopyOffset, 1);
            out.canopyLayers = scaleIntCeil(source.canopyLayers, 1);
            out.canopyLowerExtension = scaleIntCeil(source.canopyLowerExtension, 0);
            out.trunkExclusionRadius = scaleIntCeil(source.trunkExclusionRadius, 1);
            out.canopyBottomRadius = std::max(1.0f / static_cast<float>(sectionScale), source.canopyBottomRadius / static_cast<float>(sectionScale));
            out.canopyTopRadius = std::max(1.0f / static_cast<float>(sectionScale), source.canopyTopRadius / static_cast<float>(sectionScale));
            out.canopyLowerRadiusBoost = source.canopyLowerRadiusBoost / static_cast<float>(sectionScale);
            if (out.canopyOffset >= out.trunkHeight) {
                out.canopyOffset = std::max(1, out.trunkHeight - 1);
            }
            return out;
        }

        float pineCanopyLayerRadius(const PineSpec& spec, int layer) {
            if (layer < 0) {
                const float depthT = (spec.canopyLowerExtension > 0)
                    ? static_cast<float>(-layer) / static_cast<float>(spec.canopyLowerExtension)
                    : 0.0f;
                return spec.canopyBottomRadius + spec.canopyLowerRadiusBoost * depthT;
            }
            const float t = (spec.canopyLayers > 1)
                ? static_cast<float>(layer) / static_cast<float>(spec.canopyLayers - 1)
                : 0.0f;
            return spec.canopyBottomRadius + t * (spec.canopyTopRadius - spec.canopyBottomRadius);
        }

        int pineNubMaxTrunkOffsetY(const PineSpec& spec) {
            const int trunkSafeTop = std::max(3, spec.trunkHeight - 2);
            const int canopyBase = spec.trunkHeight - spec.canopyOffset;
            constexpr float kMinCanopyCoverRadius = 2.0f;
            int canopyCoveredTop = std::max(3, canopyBase);
            bool foundCoveredLayer = false;
            for (int layer = -spec.canopyLowerExtension; layer < spec.canopyLayers; ++layer) {
                const float radius = pineCanopyLayerRadius(spec, layer);
                if (radius + 1e-4f < kMinCanopyCoverRadius) continue;
                const int y = canopyBase + layer;
                if (y < 3) continue;
                canopyCoveredTop = std::max(canopyCoveredTop, y);
                foundCoveredLayer = true;
            }
            if (!foundCoveredLayer) {
                canopyCoveredTop = std::max(3, canopyBase + std::max(0, spec.canopyLayers / 2));
            }
            return std::max(3, std::min(trunkSafeTop, canopyCoveredTop));
        }

        void writeGroundFoliageToSection(const std::vector<Entity>& prototypes,
                                         const WorldContext& worldCtx,
                                         VoxelWorldContext& voxelWorld,
                                         int sectionLod,
                                         const glm::ivec3& sectionCoord,
                                         int sectionSize,
                                         int sectionScale,
                                         int grassPrototypeID,
                                         int shortGrassPrototypeID,
                                         int grassPrototypeBiome1ID,
                                         int shortGrassPrototypeBiome1ID,
                                         int grassPrototypeBiome3ID,
                                         const std::array<int, 4>& grassPrototypeBiome4IDs,
                                         int grassPrototypeBiome4Count,
                                         int grassCoverPrototypeIDX,
                                         int grassCoverPrototypeIDZ,
                                         int grassCoverPrototypeMeadowIDX,
                                         int grassCoverPrototypeMeadowIDZ,
                                         int grassCoverPrototypeForestIDX,
                                         int grassCoverPrototypeForestIDZ,
                                        int grassCoverPrototypeBiome3IDX,
                                        int grassCoverPrototypeBiome3IDZ,
                                         int grassCoverPrototypeBiome4IDX,
                                         int grassCoverPrototypeBiome4IDZ,
                                         int grassCoverPrototypeBiome2IDX,
                                         int grassCoverPrototypeBiome2IDZ,
                                         int pebblePatchPrototypeX,
                                         int pebblePatchPrototypeZ,
                                         int autumnLeafPatchPrototypeX,
                                         int autumnLeafPatchPrototypeZ,
                                         int deadLeafPatchPrototypeX,
                                         int deadLeafPatchPrototypeZ,
                                         int lilypadPatchPrototypeX,
                                         int lilypadPatchPrototypeZ,
                                         int bareBranchPrototypeX,
                                         int bareBranchPrototypeZ,
                                         int miniPineBottomPrototypeID,
                                         int miniPineTopPrototypeID,
                                         int miniPineTripleBottomPrototypeID,
                                         int miniPineTripleMiddlePrototypeID,
                                         int miniPineTripleTopPrototypeID,
                                         int flaxDoubleBottomPrototypeID,
                                         int flaxDoubleTopPrototypeID,
                                         int hempDoubleBottomPrototypeID,
                                         int hempDoubleTopPrototypeID,
                                        const std::array<int, 16>& blueFlowerPrototypeIDs,
                                        int blueFlowerPrototypeCount,
                                         int blueRareFlowerPrototypeID,
                                         int flaxRareFlowerPrototypeID,
                                         int hempRareFlowerPrototypeID,
                                         int fernRareFlowerPrototypeID,
                                         int flowerPrototypeID,
                                         int succulentPrototypeID,
                                         int succulentPrototypeVariantID,
                                         int jungleOrangeUnderLeafPrototypeID,
                                         int stickPrototypeIDX,
                                         int stickPrototypeIDZ,
                                         int winterStickPrototypeIDX,
                                         int winterStickPrototypeIDZ,
                                         int leafPrototypeID,
                                         int waterPrototypeID,
                                         const FoliageSpec& spec,
                                         bool& unresolvedDependencies,
                                         bool& modified) {
            if (!spec.enabled) return;
            const bool hasAnyGrassPrototype =
                (grassPrototypeID >= 0 || shortGrassPrototypeID >= 0
                    || grassPrototypeBiome1ID >= 0 || shortGrassPrototypeBiome1ID >= 0
                    || grassPrototypeBiome3ID >= 0
                    || grassPrototypeBiome4Count > 0);
            const bool hasAnyGrassCoverPrototype =
                (grassCoverPrototypeIDX >= 0 || grassCoverPrototypeIDZ >= 0
                    || grassCoverPrototypeMeadowIDX >= 0 || grassCoverPrototypeMeadowIDZ >= 0
                    || grassCoverPrototypeForestIDX >= 0 || grassCoverPrototypeForestIDZ >= 0
                    || grassCoverPrototypeBiome3IDX >= 0 || grassCoverPrototypeBiome3IDZ >= 0
                    || grassCoverPrototypeBiome4IDX >= 0 || grassCoverPrototypeBiome4IDZ >= 0
                    || grassCoverPrototypeBiome2IDX >= 0 || grassCoverPrototypeBiome2IDZ >= 0);
            const bool hasAnyStickPrototype =
                (stickPrototypeIDX >= 0 || stickPrototypeIDZ >= 0
                    || winterStickPrototypeIDX >= 0 || winterStickPrototypeIDZ >= 0);
            const bool hasAnyGrassOrCoverPrototype = hasAnyGrassPrototype || hasAnyGrassCoverPrototype;
            std::array<int, kRareFlowerPoolMax> rareFlowerPrototypeIDs{};
            int rareFlowerPrototypeCount = 0;
            for (int i = 0;
                 i < blueFlowerPrototypeCount && rareFlowerPrototypeCount < static_cast<int>(rareFlowerPrototypeIDs.size());
                 ++i) {
                const int id = blueFlowerPrototypeIDs[static_cast<size_t>(i)];
                if (id < 0) continue;
                rareFlowerPrototypeIDs[static_cast<size_t>(rareFlowerPrototypeCount)] = id;
                rareFlowerPrototypeCount += 1;
            }
            auto addRareFlowerToPool = [&](int id) {
                if (id < 0 || rareFlowerPrototypeCount >= static_cast<int>(rareFlowerPrototypeIDs.size())) return;
                rareFlowerPrototypeIDs[static_cast<size_t>(rareFlowerPrototypeCount)] = id;
                rareFlowerPrototypeCount += 1;
            };
            addRareFlowerToPool(blueRareFlowerPrototypeID);
            addRareFlowerToPool(flaxRareFlowerPrototypeID);
            addRareFlowerToPool(hempRareFlowerPrototypeID);
            addRareFlowerToPool(fernRareFlowerPrototypeID);
            const bool hasAnyRareFlower = rareFlowerPrototypeCount > 0;
            const bool hasAnySucculentPrototype =
                (succulentPrototypeID >= 0)
                || (succulentPrototypeVariantID >= 0);
            const bool hasAnyFlowerPrototype =
                hasAnyRareFlower
                || (flowerPrototypeID >= 0)
                || hasAnySucculentPrototype
                || (jungleOrangeUnderLeafPrototypeID >= 0);
            const bool hasMiniPine = miniPineBottomPrototypeID >= 0 && miniPineTopPrototypeID >= 0;
            const bool hasMiniPineTriple =
                miniPineTripleBottomPrototypeID >= 0
                && miniPineTripleMiddlePrototypeID >= 0
                && miniPineTripleTopPrototypeID >= 0;
            const bool hasFlaxDouble = flaxDoubleBottomPrototypeID >= 0 && flaxDoubleTopPrototypeID >= 0;
            const bool hasHempDouble = hempDoubleBottomPrototypeID >= 0 && hempDoubleTopPrototypeID >= 0;
            const int miniPineModulo = std::max(1, spec.miniPineSpawnModulo);
            const int miniPineTripleModulo = std::max(
                1,
                miniPineModulo * std::max(1, kMiniPineRarityMultiplier / 10)
            );
            const int tallDoublePlantModulo = std::max(1, spec.flowerSpawnModulo * kBlueFlowerRareChance);
            const int rareFlowerSpawnModulo = std::max(1, spec.flowerSpawnModulo * kBlueFlowerRareChance);
            const bool hasLilypadPrototype = (lilypadPatchPrototypeX >= 0 || lilypadPatchPrototypeZ >= 0);
            if ((!spec.grassEnabled || !hasAnyGrassOrCoverPrototype)
                && (!spec.flowerEnabled || !hasAnyFlowerPrototype)
                && (!spec.lilypadPatchEnabled || !hasLilypadPrototype)) return;

            const int minX = sectionCoord.x * sectionSize;
            const int minZ = sectionCoord.z * sectionSize;
            const int minY = sectionCoord.y * sectionSize;
            const int maxX = minX + sectionSize - 1;
            const int maxY = minY + sectionSize - 1;
            const int maxZ = minZ + sectionSize - 1;
            const int pebbleNearWaterRadiusLod = std::max(
                1,
                (std::max(1, spec.pebblePatchNearWaterRadius) + sectionScale - 1) / sectionScale
            );
            const int pebbleNearWaterVerticalRangeLod = std::max(
                0,
                (std::max(0, spec.pebblePatchNearWaterVerticalRange) + sectionScale - 1) / sectionScale
            );

            auto isNearSurfaceWater = [&](const glm::ivec3& centerCell) {
                if (waterPrototypeID < 0) return false;
                const int radiusSq = pebbleNearWaterRadiusLod * pebbleNearWaterRadiusLod;
                for (int dz = -pebbleNearWaterRadiusLod; dz <= pebbleNearWaterRadiusLod; ++dz) {
                    for (int dx = -pebbleNearWaterRadiusLod; dx <= pebbleNearWaterRadiusLod; ++dx) {
                        if ((dx * dx + dz * dz) > radiusSq) continue;
                        for (int dy = -pebbleNearWaterVerticalRangeLod; dy <= pebbleNearWaterVerticalRangeLod; ++dy) {
                            const glm::ivec3 probe(centerCell.x + dx, centerCell.y + dy, centerCell.z + dz);
                            if (getBlockAtLod(voxelWorld, sectionLod, probe) != static_cast<uint32_t>(waterPrototypeID)) {
                                continue;
                            }
                            if (getBlockAtLod(voxelWorld, sectionLod, probe + glm::ivec3(0, 1, 0)) == 0u) {
                                return true;
                            }
                        }
                    }
                }
                return false;
            };

            for (int lodZ = minZ; lodZ <= maxZ; ++lodZ) {
                for (int lodX = minX; lodX <= maxX; ++lodX) {
                    const int worldX = lodX * sectionScale;
                    const int worldZ = lodZ * sectionScale;
                    const bool islandQuadrants =
                        (worldCtx.expanse.islandRadius > 0.0f) && worldCtx.expanse.secondaryBiomeEnabled;
                    const uint32_t seed = hash2D(worldX, worldZ);
                    const int biomeID = ExpanseBiomeSystemLogic::ResolveBiome(
                        worldCtx,
                        static_cast<float>(worldX),
                        static_cast<float>(worldZ)
                    );
                    if (isWithinJungleVolcano(worldCtx.expanse, biomeID, worldX, worldZ)) {
                        continue;
                    }

                    if (spec.lilypadPatchEnabled && hasLilypadPrototype && waterPrototypeID >= 0) {
                        glm::ivec3 lilypadCell(0);
                        bool foundLilypadCell = false;
                        // Find the highest exposed water cell in this section column.
                        for (int lodY = maxY - 1; lodY >= minY; --lodY) {
                            const glm::ivec3 waterCell(lodX, lodY, lodZ);
                            if (getBlockAtLod(voxelWorld, sectionLod, waterCell) != static_cast<uint32_t>(waterPrototypeID)) {
                                continue;
                            }
                            const glm::ivec3 candidateCell(lodX, lodY + 1, lodZ);
                            if (!cellBelongsToSection(candidateCell, sectionCoord, sectionSize)) {
                                continue;
                            }
                            if (getBlockAtLod(voxelWorld, sectionLod, candidateCell) != 0u) {
                                continue;
                            }
                            lilypadCell = candidateCell;
                            foundLilypadCell = true;
                            break;
                        }
                        if (foundLilypadCell) {
                            const int lilypadWorldY = lilypadCell.y * sectionScale;
                            if (lilypadWorldY <= -99) {
                                // Depth rivers use dedicated 2x2 lilypad generation in terrain pass.
                                continue;
                            }
                            const int lilyChance = std::max(0, std::min(100, spec.lilypadPatchPercent));
                            if (lilyChance > 0) {
                                // Lilypads spawn in coarse "veins" (patchy lanes) instead of uniform per-column scatter.
                                // This keeps water surfaces from being covered too evenly.
                                constexpr int kLilypadVeinCellSizeBlocks = 12;
                                constexpr int kLilypadVeinPercentA = 16;
                                constexpr int kLilypadVeinPercentB = 9;
                                constexpr int kLilypadVeinChanceMultiplier = 3;
                                auto floorDiv = [](int v, int d) -> int {
                                    if (d <= 0) return 0;
                                    if (v >= 0) return v / d;
                                    return -(((-v) + d - 1) / d);
                                };
                                const int veinCellX = floorDiv(worldX, kLilypadVeinCellSizeBlocks);
                                const int veinCellZ = floorDiv(worldZ, kLilypadVeinCellSizeBlocks);
                                const uint32_t veinSeedA = hash2D(veinCellX + 9013, veinCellZ + 1733);
                                const uint32_t veinSeedB = hash2D(veinCellX + 4103, veinCellZ + 1297);
                                const bool inLilypadVein =
                                    (static_cast<int>(veinSeedA % 100u) < kLilypadVeinPercentA)
                                    || (static_cast<int>(veinSeedB % 100u) < kLilypadVeinPercentB);
                                if (!inLilypadVein) {
                                    continue;
                                }

                                const uint32_t lilySeed = hash3D(
                                    worldX + 229,
                                    lilypadCell.y * sectionScale + 311,
                                    worldZ + 419
                                );
                                const int lilyVeinChance = std::min(100, lilyChance * kLilypadVeinChanceMultiplier);
                                if (static_cast<int>(lilySeed % 100u) < lilyVeinChance) {
                                    int patchID = ((lilySeed >> 13u) & 1u) == 0u
                                        ? lilypadPatchPrototypeX
                                        : lilypadPatchPrototypeZ;
                                    if (patchID < 0) {
                                        patchID = (lilypadPatchPrototypeX >= 0) ? lilypadPatchPrototypeX : lilypadPatchPrototypeZ;
                                    }
                                    if (patchID >= 0) {
                                        voxelWorld.setBlockLod(
                                            sectionLod,
                                            lilypadCell,
                                            static_cast<uint32_t>(patchID),
                                            packColor(glm::vec3(1.0f)),
                                            false
                                        );
                                        modified = true;
                                    }
                                }
                            }
                        }
                    }

                    bool spawnFlower = false;
                    if (spec.flowerEnabled && flowerPrototypeID >= 0 && spec.flowerSpawnModulo > 0) {
                        spawnFlower = (seed % static_cast<uint32_t>(spec.flowerSpawnModulo)) == 0u;
                    }

                    bool spawnGrass = false;
                    if (spec.grassEnabled && hasAnyGrassOrCoverPrototype && spec.grassSpawnModulo > 0) {
                        spawnGrass = ((seed >> 1u) % static_cast<uint32_t>(spec.grassSpawnModulo)) == 0u;
                    }

                    if (spec.temperateOnly) {
                        const bool flowerBiomeAllowed = (biomeID == 0 || biomeID == 1);
                        if (!flowerBiomeAllowed) {
                            spawnFlower = false;
                        }
                    }

                    auto rareRollMatchesIndex = [&](int rareIndex) -> bool {
                        if (rareIndex < 0 || rareIndex >= rareFlowerPrototypeCount) return false;
                        const int protoID = rareFlowerPrototypeIDs[static_cast<size_t>(rareIndex)];
                        if (protoID < 0) return false;
                        if (protoID == blueRareFlowerPrototypeID) {
                            if (biomeID == 2 || biomeID == 3) return false; // desert / jungle
                        }
                        const int salt = 97 * (rareIndex + 1);
                        const uint32_t rareSeed = hash2D(worldX + salt, worldZ - salt * 3);
                        int spawnModulo = rareFlowerSpawnModulo;
                        if (protoID == blueRareFlowerPrototypeID) {
                            spawnModulo = std::max(1, rareFlowerSpawnModulo * kYoungClematisExtraRarityMultiplier);
                        }
                        return (rareSeed % static_cast<uint32_t>(spawnModulo)) == 0u;
                    };

                    bool spawnAnyRareFlower = false;
                    if (spec.flowerEnabled && hasAnyRareFlower) {
                        for (int i = 0; i < rareFlowerPrototypeCount; ++i) {
                            if (rareRollMatchesIndex(i)) {
                                spawnAnyRareFlower = true;
                                break;
                            }
                        }
                    }

                    const bool spawnMiniPine =
                        hasMiniPine
                        && biomeID == 0
                        && ((seed % static_cast<uint32_t>(miniPineModulo)) == 0u);
                    const bool spawnMiniPineTriple =
                        hasMiniPineTriple
                        && biomeID == 0
                        && ((hash2D(worldX + 613, worldZ - 947) % static_cast<uint32_t>(miniPineTripleModulo)) == 0u);
                    const bool spawnFlaxDouble =
                        hasFlaxDouble
                        && ((hash2D(worldX - 1423, worldZ + 2719) % static_cast<uint32_t>(tallDoublePlantModulo)) == 0u);
                    const bool spawnHempDouble =
                        hasHempDouble
                        && ((hash2D(worldX + 3307, worldZ - 811) % static_cast<uint32_t>(tallDoublePlantModulo)) == 0u);
                    const bool spawnSucculent =
                        spec.flowerEnabled
                        && hasAnySucculentPrototype
                        && biomeID == 2
                        && ((hash2D(worldX - 2089, worldZ + 1103) % static_cast<uint32_t>(rareFlowerSpawnModulo)) == 0u);
                    constexpr int kJungleOrangeUnderLeafPercent = 12;
                    const bool spawnJungleOrangeUnderLeaf =
                        spec.flowerEnabled
                        && jungleOrangeUnderLeafPrototypeID >= 0
                        && biomeID == 3
                        && (static_cast<int>(
                            hash2D(worldX + 557, worldZ - 983) % 100u
                        ) < kJungleOrangeUnderLeafPercent);
                    if (!spawnFlower
                        && !spawnGrass
                        && !spawnMiniPine
                        && !spawnMiniPineTriple
                        && !spawnFlaxDouble
                        && !spawnHempDouble
                        && !spawnSucculent
                        && !spawnJungleOrangeUnderLeaf
                        && !spawnAnyRareFlower) continue;

                    float terrainHeight = 0.0f;
                    const bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(
                        worldCtx,
                        static_cast<float>(worldX),
                        static_cast<float>(worldZ),
                        terrainHeight
                    );

                    const int groundWorldY = static_cast<int>(std::floor(terrainHeight));
                    const int groundLodY = floorDivInt(groundWorldY, sectionScale);
                    const glm::ivec3 placeCell(lodX, groundLodY + 1, lodZ);
                    if (!cellBelongsToSection(placeCell, sectionCoord, sectionSize)) continue;

                    const glm::ivec3 groundCell(lodX, groundLodY, lodZ);
                    const uint32_t groundID = getBlockAtLod(voxelWorld, sectionLod, groundCell);
                    if (groundID == 0u) {
                        if (isGroundDependencyPending(voxelWorld, sectionLod, groundCell, sectionSize)) {
                            unresolvedDependencies = true;
                        }
                        continue;
                    }

                    if (!isLand) continue;

                    if (!isFoliageGroundPrototypeID(prototypes, groundID, waterPrototypeID)) continue;

                    const uint32_t placeCellID = getBlockAtLod(voxelWorld, sectionLod, placeCell);

                    if (spawnJungleOrangeUnderLeaf) {
                        const int canopyRadius = std::max(0, spec.stickCanopySearchRadius / sectionScale);
                        const int canopyHeight = std::max(2, spec.stickCanopySearchHeight / sectionScale);
                        glm::ivec3 hangingCell(0);
                        if (findLeafHangingCellNear(
                                prototypes,
                                voxelWorld,
                                sectionLod,
                                sectionCoord,
                                sectionSize,
                                lodX,
                                groundLodY,
                                lodZ,
                                canopyRadius,
                                canopyHeight,
                                hangingCell)) {
                            voxelWorld.setBlockLod(
                                sectionLod,
                                hangingCell,
                                static_cast<uint32_t>(jungleOrangeUnderLeafPrototypeID),
                                packColor(glm::vec3(1.0f)),
                                false
                            );
                            modified = true;
                            continue;
                        }
                    }

                    if (placeCellID != 0u) continue;

                    if (spawnMiniPineTriple) {
                        const glm::ivec3 middleCell = placeCell + glm::ivec3(0, 1, 0);
                        const glm::ivec3 topCell = placeCell + glm::ivec3(0, 2, 0);
                        if (!cellBelongsToSection(middleCell, sectionCoord, sectionSize)) continue;
                        if (!cellBelongsToSection(topCell, sectionCoord, sectionSize)) continue;
                        if (getBlockAtLod(voxelWorld, sectionLod, middleCell) != 0u) continue;
                        if (getBlockAtLod(voxelWorld, sectionLod, topCell) != 0u) continue;
                        voxelWorld.setBlockLod(
                            sectionLod,
                            placeCell,
                            static_cast<uint32_t>(miniPineTripleBottomPrototypeID),
                            packColor(glm::vec3(1.0f)),
                            false
                        );
                        voxelWorld.setBlockLod(
                            sectionLod,
                            middleCell,
                            static_cast<uint32_t>(miniPineTripleMiddlePrototypeID),
                            packColor(glm::vec3(1.0f)),
                            false
                        );
                        voxelWorld.setBlockLod(
                            sectionLod,
                            topCell,
                            static_cast<uint32_t>(miniPineTripleTopPrototypeID),
                            packColor(glm::vec3(1.0f)),
                            false
                        );
                        modified = true;
                        continue;
                    }

                    if (spawnMiniPine) {
                        glm::ivec3 topCell = placeCell + glm::ivec3(0, 1, 0);
                        if (!cellBelongsToSection(topCell, sectionCoord, sectionSize)) continue;
                        if (getBlockAtLod(voxelWorld, sectionLod, topCell) != 0u) continue;
                        voxelWorld.setBlockLod(
                            sectionLod,
                            placeCell,
                            static_cast<uint32_t>(miniPineBottomPrototypeID),
                            packColor(glm::vec3(1.0f)),
                            false
                        );
                        voxelWorld.setBlockLod(
                            sectionLod,
                            topCell,
                            static_cast<uint32_t>(miniPineTopPrototypeID),
                            packColor(glm::vec3(1.0f)),
                            false
                        );
                        modified = true;
                        continue;
                    }

                    if (spawnFlaxDouble || spawnHempDouble) {
                        int bottomID = -1;
                        int topID = -1;
                        if (spawnFlaxDouble && spawnHempDouble) {
                            const bool chooseFlax = ((seed >> 7u) & 1u) == 0u;
                            bottomID = chooseFlax ? flaxDoubleBottomPrototypeID : hempDoubleBottomPrototypeID;
                            topID = chooseFlax ? flaxDoubleTopPrototypeID : hempDoubleTopPrototypeID;
                        } else if (spawnFlaxDouble) {
                            bottomID = flaxDoubleBottomPrototypeID;
                            topID = flaxDoubleTopPrototypeID;
                        } else {
                            bottomID = hempDoubleBottomPrototypeID;
                            topID = hempDoubleTopPrototypeID;
                        }
                        if (bottomID >= 0 && topID >= 0) {
                            glm::ivec3 topCell = placeCell + glm::ivec3(0, 1, 0);
                            if (!cellBelongsToSection(topCell, sectionCoord, sectionSize)) continue;
                            if (getBlockAtLod(voxelWorld, sectionLod, topCell) != 0u) continue;
                            voxelWorld.setBlockLod(
                                sectionLod,
                                placeCell,
                                static_cast<uint32_t>(bottomID),
                                packColor(glm::vec3(1.0f)),
                                false
                            );
                            voxelWorld.setBlockLod(
                                sectionLod,
                                topCell,
                                static_cast<uint32_t>(topID),
                                packColor(glm::vec3(1.0f)),
                                false
                            );
                            modified = true;
                            continue;
                        }
                    }

                    if (spawnSucculent) {
                        int succulentID = succulentPrototypeID;
                        if (succulentPrototypeID >= 0 && succulentPrototypeVariantID >= 0) {
                            const uint32_t succulentSeed = hash2D(worldX + 2903, worldZ - 1901);
                            succulentID = ((succulentSeed >> 9u) & 1u) == 0u
                                ? succulentPrototypeID
                                : succulentPrototypeVariantID;
                        } else if (succulentID < 0) {
                            succulentID = succulentPrototypeVariantID;
                        }
                        if (succulentID >= 0) {
                            voxelWorld.setBlockLod(
                                sectionLod,
                                placeCell,
                                static_cast<uint32_t>(succulentID),
                                packColor(glm::vec3(1.0f)),
                                false
                            );
                            modified = true;
                            continue;
                        }
                    }

                    if (!spawnFlower && spawnAnyRareFlower) {
                        std::array<int, kRareFlowerPoolMax> triggeredRareFlowerIDs{};
                        int triggeredRareFlowerCount = 0;
                        for (int i = 0; i < rareFlowerPrototypeCount; ++i) {
                            if (!rareRollMatchesIndex(i)) continue;
                            const int protoID = rareFlowerPrototypeIDs[static_cast<size_t>(i)];
                            if (protoID < 0) continue;
                            triggeredRareFlowerIDs[static_cast<size_t>(triggeredRareFlowerCount)] = protoID;
                            triggeredRareFlowerCount += 1;
                            if (triggeredRareFlowerCount >= static_cast<int>(triggeredRareFlowerIDs.size())) break;
                        }
                        const int rareFlowerID = chooseRareFlowerPrototypeIDForSeed(
                            triggeredRareFlowerIDs,
                            triggeredRareFlowerCount,
                            seed >> 4u
                        );
                        if (rareFlowerID >= 0) {
                            voxelWorld.setBlockLod(
                                sectionLod,
                                placeCell,
                                static_cast<uint32_t>(rareFlowerID),
                                packColor(glm::vec3(1.0f)),
                                false
                            );
                            modified = true;
                            continue;
                        }
                    }

                    if (spawnFlower) {
                        voxelWorld.setBlockLod(
                            sectionLod,
                            placeCell,
                            static_cast<uint32_t>(flowerPrototypeID),
                            flowerColorForCell(worldX, worldZ),
                            false
                        );
                        modified = true;
                    } else if (spawnGrass) {
                        const std::string& groundName = prototypes[static_cast<size_t>(groundID)].name;
                        const bool isWinterBareBiome = (biomeID == 4);
                        bool spawnStick = false;
                        uint32_t stickSeed = 0u;
                        if (spec.stickEnabled && hasAnyStickPrototype && isGrassSurfacePrototypeName(groundName)) {
                            const int stickChance = std::max(0, std::min(100, spec.stickSpawnPercent));
                            if (stickChance > 0) {
                                stickSeed = hash3D(worldX, groundWorldY, worldZ);
                                if (static_cast<int>(stickSeed % 100u) < stickChance) {
                                    if (isWinterBareBiome) {
                                        spawnStick = true;
                                    } else {
                                        const int canopyRadius = std::max(0, spec.stickCanopySearchRadius / sectionScale);
                                        const int canopyHeight = std::max(2, spec.stickCanopySearchHeight / sectionScale);
                                        spawnStick = hasLeafCanopyNear(
                                            voxelWorld,
                                            sectionLod,
                                            leafPrototypeID,
                                            lodX,
                                            groundLodY,
                                            lodZ,
                                            canopyRadius,
                                            canopyHeight
                                        );
                                    }
                                }
                            }
                        }
                        if (spawnStick) {
                            int biomeStickIDX = stickPrototypeIDX;
                            int biomeStickIDZ = stickPrototypeIDZ;
                            if (isWinterBareBiome) {
                                if (winterStickPrototypeIDX >= 0) biomeStickIDX = winterStickPrototypeIDX;
                                if (winterStickPrototypeIDZ >= 0) biomeStickIDZ = winterStickPrototypeIDZ;
                            }
                            const bool useBareBranch =
                                isWinterBareBiome
                                && (bareBranchPrototypeX >= 0 || bareBranchPrototypeZ >= 0)
                                && (static_cast<int>((stickSeed >> 8u) % 100u) < 35);
                            int stickID = -1;
                            if (useBareBranch) {
                                stickID = ((seed >> 9u) & 1u) == 0u ? bareBranchPrototypeX : bareBranchPrototypeZ;
                                if (stickID < 0) {
                                    stickID = (bareBranchPrototypeX >= 0) ? bareBranchPrototypeX : bareBranchPrototypeZ;
                                }
                            } else {
                                stickID = ((seed >> 9u) & 1u) == 0u ? biomeStickIDX : biomeStickIDZ;
                                if (stickID < 0) stickID = (biomeStickIDX >= 0) ? biomeStickIDX : biomeStickIDZ;
                            }
                            if (stickID >= 0) {
                                voxelWorld.setBlockLod(sectionLod, placeCell, static_cast<uint32_t>(stickID), stickColorForCell(worldX, worldZ), false);
                                modified = true;
                            }
                        } else {
                            int biomeCoverIDX = grassCoverPrototypeIDX;
                            int biomeCoverIDZ = grassCoverPrototypeIDZ;
                            if (biomeID == 1) {
                                if (grassCoverPrototypeMeadowIDX >= 0) biomeCoverIDX = grassCoverPrototypeMeadowIDX;
                                if (grassCoverPrototypeMeadowIDZ >= 0) biomeCoverIDZ = grassCoverPrototypeMeadowIDZ;
                            } else if (islandQuadrants && biomeID == 3) {
                                if (grassCoverPrototypeBiome3IDX >= 0) biomeCoverIDX = grassCoverPrototypeBiome3IDX;
                                if (grassCoverPrototypeBiome3IDZ >= 0) biomeCoverIDZ = grassCoverPrototypeBiome3IDZ;
                            } else if (biomeID == 4) {
                                if (grassCoverPrototypeBiome4IDX >= 0) biomeCoverIDX = grassCoverPrototypeBiome4IDX;
                                if (grassCoverPrototypeBiome4IDZ >= 0) biomeCoverIDZ = grassCoverPrototypeBiome4IDZ;
                            } else if (biomeID == 2) {
                                if (grassCoverPrototypeBiome2IDX >= 0) biomeCoverIDX = grassCoverPrototypeBiome2IDX;
                                if (grassCoverPrototypeBiome2IDZ >= 0) biomeCoverIDZ = grassCoverPrototypeBiome2IDZ;
                            }

                            bool placedGrassCover = false;
                            const bool coverGroundAllowed =
                                isGrassSurfacePrototypeName(groundName)
                                || (biomeID == 2 && isDesertGroundPrototypeName(groundName));
                            if (spec.grassCoverEnabled
                                && biomeID != 2
                                && (biomeCoverIDX >= 0 || biomeCoverIDZ >= 0)) {
                                // Prefer exact texture match to the supporting top block before biome fallback.
                                if (isForestFloorSurfacePrototypeName(groundName)) {
                                    if (grassCoverPrototypeForestIDX >= 0) biomeCoverIDX = grassCoverPrototypeForestIDX;
                                    if (grassCoverPrototypeForestIDZ >= 0) biomeCoverIDZ = grassCoverPrototypeForestIDZ;
                                } else if (isMeadowGrassSurfacePrototypeName(groundName)) {
                                    if (grassCoverPrototypeMeadowIDX >= 0) biomeCoverIDX = grassCoverPrototypeMeadowIDX;
                                    if (grassCoverPrototypeMeadowIDZ >= 0) biomeCoverIDZ = grassCoverPrototypeMeadowIDZ;
                                } else if (isJungleGrassSurfacePrototypeName(groundName)) {
                                    if (grassCoverPrototypeBiome3IDX >= 0) biomeCoverIDX = grassCoverPrototypeBiome3IDX;
                                    if (grassCoverPrototypeBiome3IDZ >= 0) biomeCoverIDZ = grassCoverPrototypeBiome3IDZ;
                                } else if (isBareWinterSurfacePrototypeName(groundName)) {
                                    if (grassCoverPrototypeBiome4IDX >= 0) biomeCoverIDX = grassCoverPrototypeBiome4IDX;
                                    if (grassCoverPrototypeBiome4IDZ >= 0) biomeCoverIDZ = grassCoverPrototypeBiome4IDZ;
                                } else if (isDesertGroundPrototypeName(groundName)) {
                                    if (grassCoverPrototypeBiome2IDX >= 0) biomeCoverIDX = grassCoverPrototypeBiome2IDX;
                                    if (grassCoverPrototypeBiome2IDZ >= 0) biomeCoverIDZ = grassCoverPrototypeBiome2IDZ;
                                }
                                if (coverGroundAllowed) {
                                    const int coverChance = std::max(0, std::min(100, spec.grassCoverPercent));
                                    if (coverChance > 0) {
                                        const uint32_t coverSeed = hash3D(worldX + 913, groundWorldY + 37, worldZ - 211);
                                        if (static_cast<int>(coverSeed % 100u) < coverChance) {
                                            int coverID = ((seed >> 13u) & 1u) == 0u ? biomeCoverIDX : biomeCoverIDZ;
                                            if (coverID < 0) coverID = (biomeCoverIDX >= 0) ? biomeCoverIDX : biomeCoverIDZ;
                                            if (coverID >= 0) {
                                                const int supportTopTile = ::RenderInitSystemLogic::FaceTileIndexFor(
                                                    &worldCtx,
                                                    prototypes[static_cast<size_t>(groundID)],
                                                    2
                                                );
                                                voxelWorld.setBlockLod(
                                                    sectionLod,
                                                    placeCell,
                                                    static_cast<uint32_t>(coverID),
                                                    withGrassCoverSnapshotTile(grassColorForCell(worldX, worldZ), supportTopTile),
                                                    false
                                                );
                                                modified = true;
                                                placedGrassCover = true;
                                            }
                                        }
                                    }
                                }
                            }
                            if (placedGrassCover) continue;

                            bool placedPebblePatch = false;
                            if (spec.pebblePatchEnabled
                                && biomeID != 2
                                && coverGroundAllowed
                                && (pebblePatchPrototypeX >= 0 || pebblePatchPrototypeZ >= 0)) {
                                const int pebbleChance = std::max(0, std::min(100, spec.pebblePatchPercent));
                                if (pebbleChance > 0) {
                                    const uint32_t pebbleSeed = hash3D(worldX + 1579, groundWorldY + 223, worldZ - 673);
                                    if (static_cast<int>(pebbleSeed % 100u) < pebbleChance) {
                                        if (!isNearSurfaceWater(placeCell)) continue;
                                        int patchID = ((pebbleSeed >> 13u) & 1u) == 0u ? pebblePatchPrototypeX : pebblePatchPrototypeZ;
                                        if (patchID < 0) patchID = (pebblePatchPrototypeX >= 0) ? pebblePatchPrototypeX : pebblePatchPrototypeZ;
                                        if (patchID >= 0) {
                                            voxelWorld.setBlockLod(
                                                sectionLod,
                                                placeCell,
                                                static_cast<uint32_t>(patchID),
                                                packColor(glm::vec3(1.0f)),
                                                false
                                            );
                                            modified = true;
                                            placedPebblePatch = true;
                                        }
                                    }
                                }
                            }
                            if (placedPebblePatch) continue;

                            bool placedAutumnLeafPatch = false;
                            int leafPatchPrototypeX = -1;
                            int leafPatchPrototypeZ = -1;
                            if (biomeID == 0) {
                                leafPatchPrototypeX = autumnLeafPatchPrototypeX;
                                leafPatchPrototypeZ = autumnLeafPatchPrototypeZ;
                            } else if (biomeID == 4) {
                                leafPatchPrototypeX = deadLeafPatchPrototypeX;
                                leafPatchPrototypeZ = deadLeafPatchPrototypeZ;
                            }
                            if (spec.autumnLeafPatchEnabled
                                && coverGroundAllowed
                                && (leafPatchPrototypeX >= 0 || leafPatchPrototypeZ >= 0)) {
                                const int leafChance = std::max(0, std::min(100, spec.autumnLeafPatchPercent));
                                if (leafChance > 0) {
                                    const uint32_t leafSeed = hash3D(worldX + 229, groundWorldY + 311, worldZ + 419);
                                    if (static_cast<int>(leafSeed % 100u) < leafChance) {
                                        int patchID = ((leafSeed >> 13u) & 1u) == 0u ? leafPatchPrototypeX : leafPatchPrototypeZ;
                                        if (patchID < 0) patchID = (leafPatchPrototypeX >= 0) ? leafPatchPrototypeX : leafPatchPrototypeZ;
                                        if (patchID >= 0) {
                                            voxelWorld.setBlockLod(
                                                sectionLod,
                                                placeCell,
                                                static_cast<uint32_t>(patchID),
                                                packColor(glm::vec3(1.0f)),
                                                false
                                            );
                                            modified = true;
                                            placedAutumnLeafPatch = true;
                                        }
                                    }
                                }
                            }
                            if (placedAutumnLeafPatch) continue;

                            if (isGrassGrowthBlockedSurfacePrototypeName(groundName)) {
                                continue;
                            }

                            const int clampedGrassTuftPercent = std::max(0, std::min(100, spec.grassTuftPercent));
                            if (clampedGrassTuftPercent <= 0) {
                                continue;
                            }
                            if (clampedGrassTuftPercent < 100) {
                                const uint32_t grassTuftSeed = hash3D(worldX + 1847, groundWorldY + 97, worldZ - 563);
                                if (static_cast<int>(grassTuftSeed % 100u) >= clampedGrassTuftPercent) {
                                    continue;
                                }
                            }

                            int biomeTallGrassID = grassPrototypeID;
                            int biomeShortGrassID = shortGrassPrototypeID;
                            if (biomeID == 1) {
                                if (grassPrototypeBiome1ID >= 0) biomeTallGrassID = grassPrototypeBiome1ID;
                                if (shortGrassPrototypeBiome1ID >= 0) biomeShortGrassID = shortGrassPrototypeBiome1ID;
                            } else if (islandQuadrants && biomeID == 3) {
                                if (grassPrototypeBiome3ID >= 0) biomeTallGrassID = grassPrototypeBiome3ID;
                                biomeShortGrassID = -1; // no short grass in jungle
                            } else if (biomeID == 4) {
                                biomeShortGrassID = -1;
                                if (grassPrototypeBiome4Count > 0) {
                                    const int grassIndex = static_cast<int>((seed >> 14u) % static_cast<uint32_t>(grassPrototypeBiome4Count));
                                    biomeTallGrassID = grassPrototypeBiome4IDs[static_cast<size_t>(grassIndex)];
                                } else if (grassPrototypeBiome3ID >= 0) {
                                    biomeTallGrassID = grassPrototypeBiome3ID;
                                }
                            } else if (biomeID == 2) {
                                biomeTallGrassID = -1;
                                biomeShortGrassID = -1;
                            }

                            int grassID = biomeTallGrassID;
                            if (biomeShortGrassID >= 0) {
                                const int clampedShortPercent = std::max(0, std::min(100, spec.shortGrassPercent));
                                if (grassID < 0 || static_cast<int>((seed >> 11u) % 100u) < clampedShortPercent) {
                                    grassID = biomeShortGrassID;
                                }
                            }
                            if (grassID < 0) continue;
                            voxelWorld.setBlockLod(sectionLod, placeCell, static_cast<uint32_t>(grassID), grassColorForCell(worldX, worldZ), false);
                            modified = true;
                        }
                    }
                }
            }
        }

        void writeWaterFoliageToSection(const std::vector<Entity>& prototypes,
                                        VoxelWorldContext& voxelWorld,
                                        int sectionLod,
                                        const glm::ivec3& sectionCoord,
                                        int sectionSize,
                                        int sectionScale,
                                        int kelpPrototypeID,
                                        int kelpTileIndex,
                                        int seaUrchinPrototypeIDX,
                                        int seaUrchinPrototypeIDZ,
                                        int sandDollarPrototypeIDX,
                                        int sandDollarPrototypeIDZ,
                                        int waterPrototypeID,
                                        const FoliageSpec& spec,
                                        bool& modified) {
            if (!spec.enabled || !spec.waterFoliageEnabled) return;
            if (waterPrototypeID < 0) return;
            const bool hasKelp = kelpPrototypeID >= 0 && kelpTileIndex >= 0;
            const bool hasSeaUrchinX = seaUrchinPrototypeIDX >= 0;
            const bool hasSeaUrchinZ = seaUrchinPrototypeIDZ >= 0;
            const bool hasSeaUrchin = hasSeaUrchinX || hasSeaUrchinZ;
            const bool hasSandDollarX = sandDollarPrototypeIDX >= 0;
            const bool hasSandDollarZ = sandDollarPrototypeIDZ >= 0;
            const bool hasSandDollar = hasSandDollarX || hasSandDollarZ;
            if (!hasKelp && !hasSeaUrchin && !hasSandDollar) return;

            const int kelpChance = std::max(0, std::min(100, spec.kelpSpawnPercent));
            const int seaUrchinChance = std::max(0, std::min(100, spec.seaUrchinSpawnPercent));
            const int sandDollarChance = std::max(0, std::min(100, spec.sandDollarSpawnPercent));
            const int seaUrchinSpawnModulo = std::max(
                1,
                spec.flowerSpawnModulo * kBlueFlowerRareChance * kYoungClematisExtraRarityMultiplier
            );
            if (kelpChance <= 0 && seaUrchinChance <= 0 && sandDollarChance <= 0) return;
            constexpr int kDepthFoliageTopY = -99;

            const int minX = sectionCoord.x * sectionSize;
            const int minY = sectionCoord.y * sectionSize;
            const int minZ = sectionCoord.z * sectionSize;
            const int maxX = minX + sectionSize - 1;
            const int maxY = minY + sectionSize - 1;
            const int maxZ = minZ + sectionSize - 1;
            auto inSection = [&](const glm::ivec3& cell) {
                return cell.x >= minX && cell.x <= maxX
                    && cell.y >= minY && cell.y <= maxY
                    && cell.z >= minZ && cell.z <= maxZ;
            };
            auto isWaterCellInSection = [&](const glm::ivec3& cell) {
                return inSection(cell)
                    && getBlockAtLod(voxelWorld, sectionLod, cell) == static_cast<uint32_t>(waterPrototypeID);
            };
            auto clearEmbeddedTile = [&](uint32_t packedColor) {
                // Strip legacy high-byte payloads while preserving supported water wave class metadata.
                const uint8_t waveClass = waterWaveClassFromPackedColor(packedColor);
                const uint8_t encoded = static_cast<uint8_t>((waveClass & 0x0fu) << 4u);
                const uint32_t rgb = packedColor & 0x00ffffffu;
                return rgb | (static_cast<uint32_t>(encoded) << 24u);
            };
            auto nearbyWaterColor = [&](const glm::ivec3& cell) {
                static const std::array<glm::ivec3, 6> kNeighborSteps = {
                    glm::ivec3(0, -1, 0),
                    glm::ivec3(0, 1, 0),
                    glm::ivec3(1, 0, 0),
                    glm::ivec3(-1, 0, 0),
                    glm::ivec3(0, 0, 1),
                    glm::ivec3(0, 0, -1)
                };
                for (const glm::ivec3& step : kNeighborSteps) {
                    const glm::ivec3 probe = cell + step;
                    if (!inSection(probe)) continue;
                    if (getBlockAtLod(voxelWorld, sectionLod, probe) != static_cast<uint32_t>(waterPrototypeID)) continue;
                    const uint32_t probeColor = clearEmbeddedTile(getColorAtLod(voxelWorld, sectionLod, probe));
                    if (probeColor != 0u) return probeColor;
                }
                const uint32_t selfColor = clearEmbeddedTile(getColorAtLod(voxelWorld, sectionLod, cell));
                if (selfColor != 0u) return selfColor;
                return withWaterFoliageMarker(packColor(glm::vec3(0.05f, 0.2f, 0.5f)), kWaterFoliageMarkerNone);
            };
            auto isWaterFoliageSupportedCellInSection = [&](const glm::ivec3& cell) {
                if (!isWaterCellInSection(cell)) return false;
                if (!isWaterCellInSection(cell + glm::ivec3(0, 1, 0))) return false;
                const glm::ivec3 below = cell + glm::ivec3(0, -1, 0);
                if (!inSection(below)) return false;
                const uint32_t belowId = getBlockAtLod(voxelWorld, sectionLod, below);
                return isFoliageGroundPrototypeID(prototypes, belowId, waterPrototypeID);
            };

            // Migrate legacy aquatic-foliage blocks and clear invalid/legacy water markers.
            for (int lodY = minY; lodY <= maxY; ++lodY) {
                for (int lodZ = minZ; lodZ <= maxZ; ++lodZ) {
                    for (int lodX = minX; lodX <= maxX; ++lodX) {
                        const glm::ivec3 cell(lodX, lodY, lodZ);
                        const uint32_t blockID = getBlockAtLod(voxelWorld, sectionLod, cell);
                        if (blockID == static_cast<uint32_t>(waterPrototypeID)) {
                            const uint32_t packedColor = getColorAtLod(voxelWorld, sectionLod, cell);
                            uint8_t marker = waterFoliageMarkerFromPackedColor(packedColor);
                            const int legacyEmbeddedTile = decodeGrassCoverSnapshotTile(packedColor);
                            if (marker == kWaterFoliageMarkerNone
                                && hasKelp
                                && legacyEmbeddedTile == kelpTileIndex) {
                                marker = kWaterFoliageMarkerKelp;
                            }
                            if (marker != kWaterFoliageMarkerNone) {
                                const glm::ivec3 belowCell = cell + glm::ivec3(0, -1, 0);
                                const bool validGround = inSection(belowCell)
                                    && isFoliageGroundPrototypeID(
                                        prototypes,
                                        getBlockAtLod(voxelWorld, sectionLod, belowCell),
                                        waterPrototypeID
                                    );
                                const bool markerSupported =
                                    (marker == kWaterFoliageMarkerKelp && hasKelp)
                                    || (marker == kWaterFoliageMarkerSeaUrchinX && hasSeaUrchinX)
                                    || (marker == kWaterFoliageMarkerSeaUrchinZ && hasSeaUrchinZ)
                                    || (marker == kWaterFoliageMarkerSandDollarX && hasSandDollarX)
                                    || (marker == kWaterFoliageMarkerSandDollarZ && hasSandDollarZ);
                                const bool keepMarker = markerSupported
                                    && validGround
                                    && isWaterFoliageSupportedCellInSection(cell);
                                if (keepMarker) {
                                    if (waterFoliageMarkerFromPackedColor(packedColor) != marker) {
                                        voxelWorld.setBlockLod(
                                            sectionLod,
                                            cell,
                                            static_cast<uint32_t>(waterPrototypeID),
                                            withWaterFoliageMarker(clearEmbeddedTile(packedColor), marker),
                                            false
                                        );
                                        modified = true;
                                    }
                                    continue;
                                }
                                voxelWorld.setBlockLod(
                                    sectionLod,
                                    cell,
                                    static_cast<uint32_t>(waterPrototypeID),
                                    clearEmbeddedTile(packedColor),
                                    false
                                );
                                modified = true;
                            }
                            continue;
                        }

                        if ((hasKelp && blockID == static_cast<uint32_t>(kelpPrototypeID))
                            || (hasSeaUrchinX && blockID == static_cast<uint32_t>(seaUrchinPrototypeIDX))
                            || (hasSeaUrchinZ && blockID == static_cast<uint32_t>(seaUrchinPrototypeIDZ))
                            || (hasSandDollarX && blockID == static_cast<uint32_t>(sandDollarPrototypeIDX))
                            || (hasSandDollarZ && blockID == static_cast<uint32_t>(sandDollarPrototypeIDZ))) {
                            voxelWorld.setBlockLod(
                                sectionLod,
                                cell,
                                static_cast<uint32_t>(waterPrototypeID),
                                nearbyWaterColor(cell),
                                false
                            );
                            modified = true;
                        }
                    }
                }
            }

            for (int lodZ = minZ + 1; lodZ <= maxZ - 1; ++lodZ) {
                for (int lodX = minX + 1; lodX <= maxX - 1; ++lodX) {
                    int baseGroundLodY = std::numeric_limits<int>::min();
                    for (int lodY = maxY - 1; lodY >= minY; --lodY) {
                        const glm::ivec3 groundCell(lodX, lodY, lodZ);
                        const uint32_t groundID = getBlockAtLod(voxelWorld, sectionLod, groundCell);
                        if (groundID == 0u) continue;
                        if (!isFoliageGroundPrototypeID(prototypes, groundID, waterPrototypeID)) continue;

                        const glm::ivec3 placeCell(lodX, lodY + 1, lodZ);
                        if (!inSection(placeCell)) continue;
                        if (!isWaterFoliageSupportedCellInSection(placeCell)) continue;
                        baseGroundLodY = lodY;
                        break;
                    }
                    if (baseGroundLodY == std::numeric_limits<int>::min()) continue;

                    const int worldX = lodX * sectionScale;
                    const int worldY = (baseGroundLodY + 1) * sectionScale;
                    const int worldZ = lodZ * sectionScale;
                    const uint32_t seed = hash3D(worldX + 173, worldY + 991, worldZ - 557);

                    bool placed = false;
                    const bool canPlaceUrchin = hasSeaUrchin && seaUrchinChance > 0
                        && ((seed % static_cast<uint32_t>(seaUrchinSpawnModulo)) == 0u);
                    const bool canPlaceSandDollar = hasSandDollar
                        && sandDollarChance > 0
                        && (worldY <= kDepthFoliageTopY)
                        && (static_cast<int>((seed >> 20u) % 100u) < sandDollarChance);
                    const bool canPlaceKelp = hasKelp && kelpChance > 0
                        && static_cast<int>((seed >> 8u) % 100u) < kelpChance;
                    const glm::ivec3 kelpCell(lodX, baseGroundLodY + 1, lodZ);

                    if (canPlaceSandDollar
                        && (!canPlaceKelp || ((seed >> 16u) & 1u) == 0u)
                        && (!canPlaceUrchin || ((seed >> 17u) & 1u) == 0u)) {
                        uint8_t marker = (((seed >> 5u) & 1u) == 0u)
                            ? kWaterFoliageMarkerSandDollarX
                            : kWaterFoliageMarkerSandDollarZ;
                        if (marker == kWaterFoliageMarkerSandDollarX && !hasSandDollarX) {
                            marker = hasSandDollarZ ? kWaterFoliageMarkerSandDollarZ : kWaterFoliageMarkerNone;
                        } else if (marker == kWaterFoliageMarkerSandDollarZ && !hasSandDollarZ) {
                            marker = hasSandDollarX ? kWaterFoliageMarkerSandDollarX : kWaterFoliageMarkerNone;
                        }
                        if (marker != kWaterFoliageMarkerNone) {
                            const uint32_t baseWaterColor = nearbyWaterColor(kelpCell);
                            voxelWorld.setBlockLod(
                                sectionLod,
                                kelpCell,
                                static_cast<uint32_t>(waterPrototypeID),
                                withWaterFoliageMarker(baseWaterColor, marker),
                                false
                            );
                            modified = true;
                            placed = true;
                        }
                    } else if (canPlaceUrchin && (!canPlaceKelp || ((seed >> 16u) & 1u) == 0u)) {
                        uint8_t marker = (((seed >> 4u) & 1u) == 0u)
                            ? kWaterFoliageMarkerSeaUrchinX
                            : kWaterFoliageMarkerSeaUrchinZ;
                        if (marker == kWaterFoliageMarkerSeaUrchinX && !hasSeaUrchinX) {
                            marker = hasSeaUrchinZ ? kWaterFoliageMarkerSeaUrchinZ : kWaterFoliageMarkerNone;
                        } else if (marker == kWaterFoliageMarkerSeaUrchinZ && !hasSeaUrchinZ) {
                            marker = hasSeaUrchinX ? kWaterFoliageMarkerSeaUrchinX : kWaterFoliageMarkerNone;
                        }
                        if (marker != kWaterFoliageMarkerNone) {
                            const uint32_t baseWaterColor = nearbyWaterColor(kelpCell);
                            voxelWorld.setBlockLod(
                                sectionLod,
                                kelpCell,
                                static_cast<uint32_t>(waterPrototypeID),
                                withWaterFoliageMarker(baseWaterColor, marker),
                                false
                            );
                            modified = true;
                            placed = true;
                        }
                    }

                    if (!placed && canPlaceKelp) {
                        if (inSection(kelpCell) && isWaterFoliageSupportedCellInSection(kelpCell)) {
                            const uint32_t baseWaterColor = nearbyWaterColor(kelpCell);
                            voxelWorld.setBlockLod(
                                sectionLod,
                                kelpCell,
                                static_cast<uint32_t>(waterPrototypeID),
                                withWaterFoliageMarker(baseWaterColor, kWaterFoliageMarkerKelp),
                                false
                            );
                            modified = true;
                        }
                    } else if (!placed && isWaterCellInSection(kelpCell)) {
                        const uint32_t packedColor = getColorAtLod(voxelWorld, sectionLod, kelpCell);
                        const uint8_t marker = waterFoliageMarkerFromPackedColor(packedColor);
                        const int legacyEmbeddedTile = decodeGrassCoverSnapshotTile(packedColor);
                        if (marker != kWaterFoliageMarkerNone
                            || (hasKelp && legacyEmbeddedTile == kelpTileIndex)) {
                            voxelWorld.setBlockLod(
                                sectionLod,
                                kelpCell,
                                static_cast<uint32_t>(waterPrototypeID),
                                clearEmbeddedTile(packedColor),
                                false
                            );
                            modified = true;
                        }
                    }
                }
            }
        }

        void writeDesertCactusToSection(const std::vector<Entity>& prototypes,
                                        const WorldContext& worldCtx,
                                        VoxelWorldContext& voxelWorld,
                                        int sectionLod,
                                        const glm::ivec3& sectionCoord,
                                        int sectionSize,
                                        int sectionScale,
                                        int cactusPrototypeAID,
                                        int cactusPrototypeBID,
                                        int cactusPrototypeAXID,
                                        int cactusPrototypeAZID,
                                        int cactusPrototypeBXID,
                                        int cactusPrototypeBZID,
                                        int cactusPrototypeAJunctionXID,
                                        int cactusPrototypeAJunctionZID,
                                        int cactusPrototypeBJunctionXID,
                                        int cactusPrototypeBJunctionZID,
                                        int waterPrototypeID,
                                        int spawnModulo,
                                        bool& unresolvedDependencies,
                                        bool& modified) {
            if (spawnModulo <= 0) return;
            if (cactusPrototypeAID < 0 && cactusPrototypeBID < 0) return;
            constexpr int kCactusTrunkHeight = 5;
            const uint32_t cactusColor = packColor(glm::vec3(0.30f, 0.68f, 0.26f));
            const std::array<glm::ivec2, 4> kArmDirs = {
                glm::ivec2(1, 0),
                glm::ivec2(-1, 0),
                glm::ivec2(0, 1),
                glm::ivec2(0, -1)
            };

            const int minX = sectionCoord.x * sectionSize;
            const int minZ = sectionCoord.z * sectionSize;
            const int maxX = minX + sectionSize - 1;
            const int maxZ = minZ + sectionSize - 1;

            for (int lodZ = minZ; lodZ <= maxZ; ++lodZ) {
                for (int lodX = minX; lodX <= maxX; ++lodX) {
                    const int worldX = lodX * sectionScale;
                    const int worldZ = lodZ * sectionScale;
                    if (ExpanseBiomeSystemLogic::ResolveBiome(
                            worldCtx,
                            static_cast<float>(worldX),
                            static_cast<float>(worldZ)) != 2) {
                        continue;
                    }

                    const uint32_t seed = hash2D(worldX + 907, worldZ - 127);
                    if ((seed % static_cast<uint32_t>(spawnModulo)) != 0u) continue;

                    float terrainHeight = 0.0f;
                    const bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(
                        worldCtx,
                        static_cast<float>(worldX),
                        static_cast<float>(worldZ),
                        terrainHeight
                    );
                    if (!isLand) continue;

                    const int groundWorldY = static_cast<int>(std::floor(terrainHeight));
                    const int groundLodY = floorDivInt(groundWorldY, sectionScale);
                    const glm::ivec3 placeCell(lodX, groundLodY + 1, lodZ);
                    if (!cellBelongsToSection(placeCell, sectionCoord, sectionSize)) continue;
                    if (getBlockAtLod(voxelWorld, sectionLod, placeCell) != 0u) continue;

                    const glm::ivec3 groundCell(lodX, groundLodY, lodZ);
                    const uint32_t groundID = getBlockAtLod(voxelWorld, sectionLod, groundCell);
                    if (groundID == 0u) {
                        if (isGroundDependencyPending(voxelWorld, sectionLod, groundCell, sectionSize)) {
                            unresolvedDependencies = true;
                        }
                        continue;
                    }
                    if (!isFoliageGroundPrototypeID(prototypes, groundID, waterPrototypeID)) continue;
                    if (groundID >= prototypes.size()) continue;
                    if (!isDesertGroundPrototypeName(prototypes[static_cast<size_t>(groundID)].name)) continue;

                    const bool useFamilyB = (cactusPrototypeBID >= 0)
                        && (cactusPrototypeAID < 0 || (((seed >> 8u) & 1u) == 1u));
                    int cactusTrunkID = useFamilyB ? cactusPrototypeBID : cactusPrototypeAID;
                    if (cactusTrunkID < 0) {
                        cactusTrunkID = (cactusPrototypeAID >= 0) ? cactusPrototypeAID : cactusPrototypeBID;
                    }
                    if (cactusTrunkID < 0) continue;

                    int cactusArmXID = useFamilyB ? cactusPrototypeBXID : cactusPrototypeAXID;
                    int cactusArmZID = useFamilyB ? cactusPrototypeBZID : cactusPrototypeAZID;
                    int cactusJunctionXID = useFamilyB ? cactusPrototypeBJunctionXID : cactusPrototypeAJunctionXID;
                    int cactusJunctionZID = useFamilyB ? cactusPrototypeBJunctionZID : cactusPrototypeAJunctionZID;
                    if (cactusArmXID < 0) cactusArmXID = cactusTrunkID;
                    if (cactusArmZID < 0) cactusArmZID = cactusTrunkID;
                    if (cactusJunctionXID < 0) cactusJunctionXID = cactusTrunkID;
                    if (cactusJunctionZID < 0) cactusJunctionZID = cactusTrunkID;

                    bool canPlaceTrunk = true;
                    for (int i = 1; i <= kCactusTrunkHeight; ++i) {
                        const glm::ivec3 trunkCell(lodX, groundLodY + i, lodZ);
                        if (!cellBelongsToSection(trunkCell, sectionCoord, sectionSize)
                            || getBlockAtLod(voxelWorld, sectionLod, trunkCell) != 0u) {
                            canPlaceTrunk = false;
                            break;
                        }
                    }
                    if (!canPlaceTrunk) continue;

                    for (int i = 1; i <= kCactusTrunkHeight; ++i) {
                        const glm::ivec3 trunkCell(lodX, groundLodY + i, lodZ);
                        voxelWorld.setBlockLod(
                            sectionLod,
                            trunkCell,
                            static_cast<uint32_t>(cactusTrunkID),
                            cactusColor,
                            false
                        );
                    }
                    modified = true;

                    auto placeArm = [&](int directionIndex, int trunkOffsetY) {
                        if (trunkOffsetY <= 0 || trunkOffsetY >= kCactusTrunkHeight) return;
                        const glm::ivec2 dir = kArmDirs[static_cast<size_t>(directionIndex & 3)];
                        const glm::ivec3 trunkCell(lodX, groundLodY + trunkOffsetY, lodZ);
                        const glm::ivec3 armCell(lodX + dir.x, groundLodY + trunkOffsetY, lodZ + dir.y);
                        if (!cellBelongsToSection(armCell, sectionCoord, sectionSize)) return;
                        if (getBlockAtLod(voxelWorld, sectionLod, armCell) != 0u) return;

                        const int junctionPrototypeID = (dir.x != 0) ? cactusJunctionXID : cactusJunctionZID;
                        voxelWorld.setBlockLod(
                            sectionLod,
                            trunkCell,
                            static_cast<uint32_t>(junctionPrototypeID),
                            cactusColor,
                            false
                        );
                        modified = true;

                        const int armPrototypeID = (dir.x != 0) ? cactusArmXID : cactusArmZID;
                        voxelWorld.setBlockLod(
                            sectionLod,
                            armCell,
                            static_cast<uint32_t>(armPrototypeID),
                            cactusColor,
                            false
                        );
                        modified = true;

                        const glm::ivec3 tipCell(armCell.x, armCell.y + 1, armCell.z);
                        if (!cellBelongsToSection(tipCell, sectionCoord, sectionSize)) return;
                        if (getBlockAtLod(voxelWorld, sectionLod, tipCell) != 0u) return;
                        voxelWorld.setBlockLod(
                            sectionLod,
                            tipCell,
                            static_cast<uint32_t>(cactusTrunkID),
                            cactusColor,
                            false
                        );
                        modified = true;
                    };

                    const int firstArmDir = static_cast<int>((seed >> 12u) & 3u);
                    const int firstArmOffsetY = 2 + static_cast<int>((seed >> 14u) & 1u);
                    placeArm(firstArmDir, firstArmOffsetY);

                    const bool hasSecondArm = (((seed >> 16u) & 1u) == 1u);
                    if (hasSecondArm) {
                        int secondArmOffsetY = 3 + static_cast<int>((seed >> 18u) & 1u);
                        if (secondArmOffsetY == firstArmOffsetY) {
                            secondArmOffsetY = std::min(kCactusTrunkHeight - 1, firstArmOffsetY + 1);
                        }
                        const int secondArmDir = (firstArmDir + 2) & 3;
                        placeArm(secondArmDir, secondArmOffsetY);
                    }
                }
            }
        }

        void writeCaveSlopeToSection(const std::vector<Entity>& prototypes,
                                     const WorldContext& worldCtx,
                                     VoxelWorldContext& voxelWorld,
                                     int sectionLod,
                                     const glm::ivec3& sectionCoord,
                                     int sectionSize,
                                     int sectionScale,
                                     int slopeProtoPosX,
                                     int slopeProtoNegX,
                                     int slopeProtoPosZ,
                                     int slopeProtoNegZ,
                                     const FoliageSpec& spec,
                                     bool& modified) {
            if (!spec.enabled || !spec.caveSlopeEnabled) return;
            const bool hasAnySlopePrototype =
                (slopeProtoPosX >= 0)
                || (slopeProtoNegX >= 0)
                || (slopeProtoPosZ >= 0)
                || (slopeProtoNegZ >= 0);
            if (!hasAnySlopePrototype) return;

            const int slopeChance = std::max(0, std::min(100, spec.caveSlopeSpawnPercent));
            if (slopeChance <= 0) return;
            const int minDepthFromSurface = std::max(1, std::min(256, spec.caveSlopeMinDepthFromSurface));
            const int minDepthFromSurfaceLod = std::max(
                1,
                static_cast<int>(std::ceil(static_cast<float>(minDepthFromSurface) / static_cast<float>(sectionScale)))
            );

            const int minX = sectionCoord.x * sectionSize;
            const int minY = sectionCoord.y * sectionSize;
            const int minZ = sectionCoord.z * sectionSize;
            const int maxX = minX + sectionSize - 1;
            const int maxY = minY + sectionSize - 1;
            const int maxZ = minZ + sectionSize - 1;
            auto inSection = [&](const glm::ivec3& cell) {
                return cell.x >= minX && cell.x <= maxX
                    && cell.y >= minY && cell.y <= maxY
                    && cell.z >= minZ && cell.z <= maxZ;
            };

            auto isStoneSurfaceCell = [&](const glm::ivec3& cell) {
                const uint32_t id = getBlockAtLod(voxelWorld, sectionLod, cell);
                if (id == 0u || id >= prototypes.size()) return false;
                const Entity& proto = prototypes[id];
                if (!proto.isBlock) return false;
                return proto.isSolid && isStoneSurfacePrototypeName(proto.name);
            };
            auto isWallStonePrototypeID = [&](uint32_t id) {
                if (id == 0u || id >= prototypes.size()) return false;
                return isWallStonePrototypeName(prototypes[id].name);
            };
            auto isSolidSupportCell = [&](const glm::ivec3& cell) {
                const uint32_t id = getBlockAtLod(voxelWorld, sectionLod, cell);
                if (id == 0u || id >= prototypes.size()) return false;
                const Entity& proto = prototypes[id];
                if (!proto.isBlock || !proto.isSolid) return false;
                return proto.name != "Water";
            };

            const int kNoSurface = std::numeric_limits<int>::min();
            std::vector<int> surfaceHeights(static_cast<size_t>(sectionSize * sectionSize), kNoSurface);
            auto surfaceIdx = [&](int lodX, int lodZ) {
                return (lodZ - minZ) * sectionSize + (lodX - minX);
            };

            for (int lodZ = minZ; lodZ <= maxZ; ++lodZ) {
                for (int lodX = minX; lodX <= maxX; ++lodX) {
                    const int worldX = lodX * sectionScale;
                    const int worldZ = lodZ * sectionScale;
                    float terrainHeight = 0.0f;
                    if (!ExpanseBiomeSystemLogic::SampleTerrain(
                            worldCtx,
                            static_cast<float>(worldX),
                            static_cast<float>(worldZ),
                            terrainHeight)) {
                        continue;
                    }
                    surfaceHeights[static_cast<size_t>(surfaceIdx(lodX, lodZ))] =
                        floorDivInt(static_cast<int>(std::floor(terrainHeight)), sectionScale);
                }
            }
            const std::array<glm::ivec3, 4> sideOffsets = {
                glm::ivec3(1, 0, 0),
                glm::ivec3(-1, 0, 0),
                glm::ivec3(0, 0, 1),
                glm::ivec3(0, 0, -1)
            };

            for (int lodZ = minZ; lodZ <= maxZ; ++lodZ) {
                for (int lodX = minX; lodX <= maxX; ++lodX) {
                    const int worldX = lodX * sectionScale;
                    const int worldZ = lodZ * sectionScale;
                    const int surfaceY = surfaceHeights[static_cast<size_t>(surfaceIdx(lodX, lodZ))];
                    if (surfaceY == kNoSurface) continue;
                    const int maxPlaceY = std::min(maxY, surfaceY - minDepthFromSurfaceLod);
                    if (maxPlaceY < minY) continue;

                    for (int lodY = minY; lodY <= maxPlaceY; ++lodY) {
                        const int worldY = lodY * sectionScale;
                        const glm::ivec3 cell(lodX, lodY, lodZ);
                        if (!isStoneSurfaceCell(cell)) continue;
                        // Simple rule: block must have free air above.
                        if (getBlockAtLod(voxelWorld, sectionLod, cell + glm::ivec3(0, 1, 0)) != 0u) continue;

                        const uint32_t seed = hash3D(worldX, worldY, worldZ);
                        if (static_cast<int>(seed % 100u) >= slopeChance) continue;

                        glm::ivec3 exposedAir(0);
                        int sideAirCount = 0;
                        for (const glm::ivec3& side : sideOffsets) {
                            const glm::ivec3 sideCell = cell + side;
                            // Keep writes local to this section: do not classify neighbors
                            // outside the current section as open candidates.
                            if (!inSection(sideCell)) continue;
                            const uint32_t sideID = getBlockAtLod(voxelWorld, sectionLod, sideCell);
                            if (sideID == 0u || isWallStonePrototypeID(sideID)) {
                                exposedAir = side;
                                sideAirCount += 1;
                            }
                        }
                        // Keep slope facing stable: only place when exactly one side is open.
                        if (sideAirCount != 1) continue;

                        const CaveSlopeDir slopeDir = caveSlopeDirFromExposedAirSide(exposedAir);
                        if (slopeDir == CaveSlopeDir::None) continue;
                        const int slopeID = caveSlopePrototypeForDir(slopeDir, slopeProtoPosX, slopeProtoNegX, slopeProtoPosZ, slopeProtoNegZ);
                        if (slopeID < 0) continue;
                        const uint32_t solidStoneID = getBlockAtLod(voxelWorld, sectionLod, cell);
                        if (solidStoneID == 0u || solidStoneID >= prototypes.size()) continue;

                        // If the open side is currently a wall-stone hold, clear it first.
                        bool clearedOpenWallStone = false;
                        glm::ivec3 clearedOpenWallStoneCell(0);
                        uint32_t clearedOpenWallStoneID = 0u;
                        uint32_t clearedOpenWallStoneColor = 0u;
                        const glm::ivec3 sideCell = cell + exposedAir;
                        if (inSection(sideCell)) {
                            const uint32_t sideID = getBlockAtLod(voxelWorld, sectionLod, sideCell);
                            if (isWallStonePrototypeID(sideID)) {
                                clearedOpenWallStone = true;
                                clearedOpenWallStoneCell = sideCell;
                                clearedOpenWallStoneID = sideID;
                                clearedOpenWallStoneColor = getColorAtLod(voxelWorld, sectionLod, sideCell);
                                voxelWorld.setBlockLod(sectionLod, sideCell, 0u, 0u, false);
                            }
                        }

                        const glm::ivec3 lowDir = caveSlopeLowDirection(slopeDir);
                        const glm::ivec3 perpDir = caveSlopePerpDirection(slopeDir);
                        auto canReplaceWithSlope = [&](const glm::ivec3& target) {
                            if (!inSection(target)) return false;
                            const uint32_t id = getBlockAtLod(voxelWorld, sectionLod, target);
                            if (id == 0u) return true;
                            if (id >= prototypes.size()) return false;
                            const Entity& proto = prototypes[id];
                            if (!proto.isBlock) return false;
                            if (proto.name == "Water") return false;
                            return true;
                        };
                        struct PriorCellState {
                            glm::ivec3 pos{0};
                            uint32_t id = 0u;
                            uint32_t color = 0u;
                        };
                        std::vector<PriorCellState> placedSlopePriorStates;
                        std::unordered_set<glm::ivec3, IVec3Hash> placedSlopeCells;
                        std::vector<glm::ivec3> centerStepCells;
                        centerStepCells.reserve(4);
                        glm::ivec3 lowestCenterCell = cell;
                        bool hasLowestCenterCell = false;
                        auto placeSlopeCell = [&](const glm::ivec3& target) -> bool {
                            if (!canReplaceWithSlope(target)) return false;
                            if (placedSlopeCells.insert(target).second) {
                                placedSlopePriorStates.push_back(
                                    PriorCellState{
                                        target,
                                        getBlockAtLod(voxelWorld, sectionLod, target),
                                        getColorAtLod(voxelWorld, sectionLod, target)
                                    }
                                );
                            }
                            voxelWorld.setBlockLod(
                                sectionLod,
                                target,
                                static_cast<uint32_t>(slopeID),
                                caveStoneColorForCell(
                                    target.x * sectionScale,
                                    target.y * sectionScale,
                                    target.z * sectionScale
                                ),
                                false
                            );
                            modified = true;
                            return true;
                        };

                        // Build up to 4 chained steps (origin + 3 down/forward checks).
                        for (int chainStep = 0; chainStep <= 3; ++chainStep) {
                            const glm::ivec3 stepCell =
                                cell + (lowDir * chainStep) + glm::ivec3(0, -chainStep, 0);
                            if (!inSection(stepCell)) break;
                            // Chained continuation still requires the next center cell to be air.
                            if (chainStep > 0 && getBlockAtLod(voxelWorld, sectionLod, stepCell) != 0u) break;
                            if (!placeSlopeCell(stepCell)) break;
                            centerStepCells.push_back(stepCell);
                            lowestCenterCell = stepCell;
                            hasLowestCenterCell = true;

                            // Widen each step: clear side blocks and place parallel slopes.
                            for (int lateralSign = -1; lateralSign <= 1; lateralSign += 2) {
                                const glm::ivec3 sideStep = stepCell + perpDir * lateralSign;
                                if (!inSection(sideStep)) continue;
                                placeSlopeCell(sideStep);
                            }
                        }

                        // Support validation is applied only once after generating the full ramp:
                        // check only under the lowest center step.
                        bool rampAccepted = true;
                        if (hasLowestCenterCell) {
                            const glm::ivec3 supportCell = lowestCenterCell + glm::ivec3(0, -1, 0);
                            if (!isSolidSupportCell(supportCell)) {
                                for (const auto& prior : placedSlopePriorStates) {
                                    voxelWorld.setBlockLod(sectionLod, prior.pos, prior.id, prior.color, false);
                                }
                                if (clearedOpenWallStone) {
                                    voxelWorld.setBlockLod(
                                        sectionLod,
                                        clearedOpenWallStoneCell,
                                        clearedOpenWallStoneID,
                                        clearedOpenWallStoneColor,
                                        false
                                    );
                                }
                                modified = true;
                                rampAccepted = false;
                            }
                        }
                        if (!rampAccepted) continue;

                        // For 4-step ramps, add solid supports under the two middle rows (2 x 3 cells).
                        // Keep ramp cells intact; supports are placed one block below.
                        for (int midRow = 1; midRow <= 2; ++midRow) {
                            if (midRow >= static_cast<int>(centerStepCells.size())) break;
                            const glm::ivec3 midCenter = centerStepCells[static_cast<size_t>(midRow)];
                            for (int lateral = -1; lateral <= 1; ++lateral) {
                                const glm::ivec3 target = midCenter + perpDir * lateral + glm::ivec3(0, -1, 0);
                                if (!inSection(target)) continue;
                                const uint32_t existingID = getBlockAtLod(voxelWorld, sectionLod, target);
                                if (existingID != 0u && existingID < prototypes.size()) {
                                    const Entity& existingProto = prototypes[existingID];
                                    if (existingProto.name == "Water") continue;
                                }
                                voxelWorld.setBlockLod(
                                    sectionLod,
                                    target,
                                    solidStoneID,
                                    caveStoneColorForCell(
                                        target.x * sectionScale,
                                        target.y * sectionScale,
                                        target.z * sectionScale
                                    ),
                                    false
                                );
                                modified = true;

                                // The higher/back support row gets one extra block under it.
                                if (midRow == 1) {
                                    const glm::ivec3 extraUnder = target + glm::ivec3(0, -1, 0);
                                    if (!inSection(extraUnder)) continue;
                                    const uint32_t extraID = getBlockAtLod(voxelWorld, sectionLod, extraUnder);
                                    if (extraID != 0u && extraID < prototypes.size()) {
                                        const Entity& extraProto = prototypes[extraID];
                                        if (extraProto.name == "Water") continue;
                                    }
                                    voxelWorld.setBlockLod(
                                        sectionLod,
                                        extraUnder,
                                        solidStoneID,
                                        caveStoneColorForCell(
                                            extraUnder.x * sectionScale,
                                            extraUnder.y * sectionScale,
                                            extraUnder.z * sectionScale
                                        ),
                                        false
                                    );
                                    modified = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        void writeCaveStoneToSection(const std::vector<Entity>& prototypes,
                                     const WorldContext& worldCtx,
                                     VoxelWorldContext& voxelWorld,
                                     int sectionLod,
                                     const glm::ivec3& sectionCoord,
                                     int sectionSize,
                                     int sectionScale,
                                     int stonePrototypeIDX,
                                     int stonePrototypeIDZ,
                                     int waterPrototypeID,
                                     const FoliageSpec& spec,
                                     bool& modified) {
            if (!spec.enabled || !spec.caveStoneEnabled) return;
            const bool hasAnyStonePrototype = (stonePrototypeIDX >= 0 || stonePrototypeIDZ >= 0);
            if (!hasAnyStonePrototype) return;
            const int stoneChance = std::max(0, std::min(100, spec.caveStoneSpawnPercent));
            if (stoneChance <= 0) return;

            const int minX = sectionCoord.x * sectionSize;
            const int minY = sectionCoord.y * sectionSize;
            const int minZ = sectionCoord.z * sectionSize;
            const int maxX = minX + sectionSize - 1;
            const int maxY = minY + sectionSize - 1;
            const int maxZ = minZ + sectionSize - 1;
            const int stoneNearWaterRadiusLod = std::max(
                1,
                (std::max(1, spec.pebblePatchNearWaterRadius) + sectionScale - 1) / sectionScale
            );
            const int stoneNearWaterVerticalRangeLod = std::max(
                0,
                (std::max(0, spec.pebblePatchNearWaterVerticalRange) + sectionScale - 1) / sectionScale
            );
            auto isNearSurfaceWater = [&](const glm::ivec3& centerCell) {
                if (waterPrototypeID < 0) return false;
                const int radiusSq = stoneNearWaterRadiusLod * stoneNearWaterRadiusLod;
                for (int dz = -stoneNearWaterRadiusLod; dz <= stoneNearWaterRadiusLod; ++dz) {
                    for (int dx = -stoneNearWaterRadiusLod; dx <= stoneNearWaterRadiusLod; ++dx) {
                        if ((dx * dx + dz * dz) > radiusSq) continue;
                        for (int dy = -stoneNearWaterVerticalRangeLod; dy <= stoneNearWaterVerticalRangeLod; ++dy) {
                            const glm::ivec3 probe(centerCell.x + dx, centerCell.y + dy, centerCell.z + dz);
                            if (getBlockAtLod(voxelWorld, sectionLod, probe) != static_cast<uint32_t>(waterPrototypeID)) {
                                continue;
                            }
                            if (getBlockAtLod(voxelWorld, sectionLod, probe + glm::ivec3(0, 1, 0)) == 0u) {
                                return true;
                            }
                        }
                    }
                }
                return false;
            };

            const int kNoSurface = std::numeric_limits<int>::min();
            std::vector<int> surfaceHeights(static_cast<size_t>(sectionSize * sectionSize), kNoSurface);
            auto surfaceIdx = [&](int lodX, int lodZ) {
                return (lodZ - minZ) * sectionSize + (lodX - minX);
            };

            for (int lodZ = minZ; lodZ <= maxZ; ++lodZ) {
                for (int lodX = minX; lodX <= maxX; ++lodX) {
                    const int worldX = lodX * sectionScale;
                    const int worldZ = lodZ * sectionScale;
                    float terrainHeight = 0.0f;
                    if (!ExpanseBiomeSystemLogic::SampleTerrain(
                            worldCtx,
                            static_cast<float>(worldX),
                            static_cast<float>(worldZ),
                            terrainHeight)) {
                        continue;
                    }
                    surfaceHeights[static_cast<size_t>(surfaceIdx(lodX, lodZ))] =
                        floorDivInt(static_cast<int>(std::floor(terrainHeight)), sectionScale);
                }
            }

            for (int lodZ = minZ; lodZ <= maxZ; ++lodZ) {
                for (int lodX = minX; lodX <= maxX; ++lodX) {
                    const int worldX = lodX * sectionScale;
                    const int worldZ = lodZ * sectionScale;
                    const int biomeID = ExpanseBiomeSystemLogic::ResolveBiome(
                        worldCtx,
                        static_cast<float>(worldX),
                        static_cast<float>(worldZ)
                    );
                    if (biomeID == 2) continue;
                    const int surfaceY = surfaceHeights[static_cast<size_t>(surfaceIdx(lodX, lodZ))];
                    if (surfaceY == kNoSurface) continue;
                    const int lodY = surfaceY + 1;
                    if (lodY < minY || lodY > maxY) continue;
                    const int worldY = lodY * sectionScale;

                    const uint32_t seed = hash3D(worldX, worldY, worldZ);
                    if (static_cast<int>(seed % 100u) >= stoneChance) continue;

                    const glm::ivec3 placeCell(lodX, lodY, lodZ);
                    if (!isNearSurfaceWater(placeCell)) continue;
                    const uint32_t existingID = getBlockAtLod(voxelWorld, sectionLod, placeCell);
                    if (existingID != 0u) {
                        bool replaceGrass = false;
                        if (existingID < prototypes.size()) {
                            const std::string& existingName = prototypes[existingID].name;
                            replaceGrass = isGrassFoliagePrototypeName(existingName);
                        }
                        if (!replaceGrass) continue;
                    }

                    const glm::ivec3 aboveCell(lodX, lodY + 1, lodZ);
                    if (getBlockAtLod(voxelWorld, sectionLod, aboveCell) != 0u) continue;

                    const glm::ivec3 groundCell(lodX, lodY - 1, lodZ);
                    const uint32_t groundID = getBlockAtLod(voxelWorld, sectionLod, groundCell);
                    if (!isFoliageGroundPrototypeID(prototypes, groundID, waterPrototypeID)) continue;

                    int stoneID = (((seed >> 10u) & 1u) == 0u) ? stonePrototypeIDX : stonePrototypeIDZ;
                    if (stoneID < 0) stoneID = (stonePrototypeIDX >= 0) ? stonePrototypeIDX : stonePrototypeIDZ;
                    if (stoneID < 0) continue;

                    voxelWorld.setBlockLod(
                        sectionLod,
                        placeCell,
                        static_cast<uint32_t>(stoneID),
                        caveStoneColorForCell(worldX, worldY, worldZ),
                        false
                    );
                    modified = true;
                }
            }
        }

        void writeCavePotsToSection(const std::vector<Entity>& prototypes,
                                    const WorldContext& worldCtx,
                                    VoxelWorldContext& voxelWorld,
                                    int sectionLod,
                                    const glm::ivec3& sectionCoord,
                                    int sectionSize,
                                    int sectionScale,
                                    int potPrototypeIDX,
                                    int potPrototypeIDZ,
                                    const FoliageSpec& spec,
                                    bool& modified) {
            if (!spec.enabled || !spec.cavePotEnabled) return;
            const bool hasAnyPotPrototype = (potPrototypeIDX >= 0 || potPrototypeIDZ >= 0);
            if (!hasAnyPotPrototype) return;
            const int potChance = std::max(0, std::min(100, spec.cavePotSpawnPercent));
            if (potChance <= 0) return;
            const int minDepthFromSurface = std::max(1, std::min(256, spec.cavePotMinDepthFromSurface));
            const int minDepthFromSurfaceLod = std::max(
                1,
                static_cast<int>(std::ceil(static_cast<float>(minDepthFromSurface) / static_cast<float>(sectionScale)))
            );
            const int pileMaxCount = std::max(1, std::min(3, spec.cavePotPileMaxCount));

            const int minX = sectionCoord.x * sectionSize;
            const int minY = sectionCoord.y * sectionSize;
            const int minZ = sectionCoord.z * sectionSize;
            const int maxX = minX + sectionSize - 1;
            const int maxY = minY + sectionSize - 1;
            const int maxZ = minZ + sectionSize - 1;
            auto inSection = [&](const glm::ivec3& cell) {
                return cell.x >= minX && cell.x <= maxX
                    && cell.y >= minY && cell.y <= maxY
                    && cell.z >= minZ && cell.z <= maxZ;
            };

            const int kNoSurface = std::numeric_limits<int>::min();
            std::vector<int> surfaceHeights(static_cast<size_t>(sectionSize * sectionSize), kNoSurface);
            auto surfaceIdx = [&](int lodX, int lodZ) {
                return (lodZ - minZ) * sectionSize + (lodX - minX);
            };

            for (int lodZ = minZ; lodZ <= maxZ; ++lodZ) {
                for (int lodX = minX; lodX <= maxX; ++lodX) {
                    const int worldX = lodX * sectionScale;
                    const int worldZ = lodZ * sectionScale;
                    float terrainHeight = 0.0f;
                    if (!ExpanseBiomeSystemLogic::SampleTerrain(
                            worldCtx,
                            static_cast<float>(worldX),
                            static_cast<float>(worldZ),
                            terrainHeight)) {
                        continue;
                    }
                    surfaceHeights[static_cast<size_t>(surfaceIdx(lodX, lodZ))] =
                        floorDivInt(static_cast<int>(std::floor(terrainHeight)), sectionScale);
                }
            }

            std::unordered_set<glm::ivec3, IVec3Hash> placedCells;
            auto canPlacePotAt = [&](const glm::ivec3& cell) -> bool {
                if (!inSection(cell)) return false;
                const int worldY = cell.y * sectionScale;
                if (worldY >= 60) return false;
                if (placedCells.count(cell) > 0) return false;
                if (getBlockAtLod(voxelWorld, sectionLod, cell) != 0u) return false;
                if (getBlockAtLod(voxelWorld, sectionLod, cell + glm::ivec3(0, 1, 0)) != 0u) return false;

                const glm::ivec3 supportCell = cell + glm::ivec3(0, -1, 0);
                const uint32_t supportID = getBlockAtLod(voxelWorld, sectionLod, supportCell);
                if (supportID == 0u || supportID >= prototypes.size()) return false;
                const Entity& supportProto = prototypes[supportID];
                if (!supportProto.isBlock || !supportProto.isSolid) return false;
                if (supportProto.name == "Water") return false;
                if (caveSlopeDirFromName(supportProto.name) != CaveSlopeDir::None) return false;
                if (supportProto.name.rfind("WaterSlope", 0) == 0) return false;

                int openSides = 0;
                const std::array<glm::ivec3, 4> sideOffsets = {
                    glm::ivec3(1, 0, 0),
                    glm::ivec3(-1, 0, 0),
                    glm::ivec3(0, 0, 1),
                    glm::ivec3(0, 0, -1)
                };
                for (const glm::ivec3& side : sideOffsets) {
                    const glm::ivec3 sideCell = cell + side;
                    if (!inSection(sideCell)) continue;
                    if (getBlockAtLod(voxelWorld, sectionLod, sideCell) == 0u) {
                        openSides += 1;
                    }
                }
                return openSides > 0;
            };

            const std::array<glm::ivec3, 9> pileOffsets = {
                glm::ivec3(0, 0, 0),
                glm::ivec3(1, 0, 0),
                glm::ivec3(-1, 0, 0),
                glm::ivec3(0, 0, 1),
                glm::ivec3(0, 0, -1),
                glm::ivec3(1, 0, 1),
                glm::ivec3(-1, 0, 1),
                glm::ivec3(1, 0, -1),
                glm::ivec3(-1, 0, -1)
            };

            std::vector<glm::ivec3> candidates;
            auto gatherCandidates = [&](bool enforceMinDepth) {
                candidates.clear();
                candidates.reserve(static_cast<size_t>(sectionSize * sectionSize));
                for (int lodZ = minZ; lodZ <= maxZ; ++lodZ) {
                    for (int lodX = minX; lodX <= maxX; ++lodX) {
                        const int surfaceY = surfaceHeights[static_cast<size_t>(surfaceIdx(lodX, lodZ))];
                        if (surfaceY == kNoSurface) continue;
                        int maxPlaceY = maxY;
                        if (enforceMinDepth) {
                            maxPlaceY = std::min(maxY, surfaceY - minDepthFromSurfaceLod);
                        }
                        if (maxPlaceY < minY) continue;
                        for (int lodY = minY; lodY <= maxPlaceY; ++lodY) {
                            const glm::ivec3 cell(lodX, lodY, lodZ);
                            if (canPlacePotAt(cell)) {
                                candidates.push_back(cell);
                            }
                        }
                    }
                }
            };
            gatherCandidates(true);
            // Fallback: if strict depth filtering finds nothing, allow shallower cave pockets.
            if (candidates.empty()) {
                gatherCandidates(false);
            }
            if (candidates.empty()) return;

            const uint32_t sectionSeed = hash3D(
                sectionCoord.x * 911 + sectionLod * 37,
                sectionCoord.y * 613 - sectionLod * 53,
                sectionCoord.z * 353 + 19
            );
            // Rare-by-section roll so cave pots do not explode in dense cave volumes.
            if (static_cast<int>(sectionSeed % 100u) >= potChance) return;

            const size_t startIndex = static_cast<size_t>((sectionSeed >> 8u) % static_cast<uint32_t>(candidates.size()));
            for (size_t attempt = 0; attempt < candidates.size(); ++attempt) {
                const glm::ivec3 baseCell = candidates[(startIndex + attempt) % candidates.size()];
                if (!canPlacePotAt(baseCell)) continue;

                const int baseWorldX = baseCell.x * sectionScale;
                const int baseWorldY = baseCell.y * sectionScale;
                const int baseWorldZ = baseCell.z * sectionScale;
                const uint32_t seed = hash3D(baseWorldX + 121, baseWorldY - 877, baseWorldZ + 43);
                const int pileCount = 1 + static_cast<int>((seed >> 8u) % static_cast<uint32_t>(pileMaxCount));
                const int offsetStart = static_cast<int>((seed >> 14u) % static_cast<uint32_t>(pileOffsets.size()));

                int placed = 0;
                for (size_t i = 0; i < pileOffsets.size() && placed < pileCount; ++i) {
                    const size_t offsetIndex = (static_cast<size_t>(offsetStart) + i) % pileOffsets.size();
                    const glm::ivec3 target = baseCell + pileOffsets[offsetIndex];
                    if (!canPlacePotAt(target)) continue;

                    const int targetWorldX = target.x * sectionScale;
                    const int targetWorldY = target.y * sectionScale;
                    const int targetWorldZ = target.z * sectionScale;
                    const uint32_t pickSeed = hash3D(targetWorldX + 9, targetWorldY + 17, targetWorldZ + 23);
                    int potID = (((pickSeed >> 9u) & 1u) == 0u) ? potPrototypeIDX : potPrototypeIDZ;
                    if (potID < 0) potID = (potPrototypeIDX >= 0) ? potPrototypeIDX : potPrototypeIDZ;
                    if (potID < 0) continue;

                    voxelWorld.setBlockLod(
                        sectionLod,
                        target,
                        static_cast<uint32_t>(potID),
                        packColor(glm::vec3(1.0f)),
                        false
                    );
                    placedCells.insert(target);
                    modified = true;
                    placed += 1;
                }

                // At most one pile per section by design to keep cave pots very rare.
                if (placed > 0) break;
            }
        }

        void writeCaveWallStoneToSection(const std::vector<Entity>& prototypes,
                                         const WorldContext& worldCtx,
                                         VoxelWorldContext& voxelWorld,
                                         int sectionLod,
                                         const glm::ivec3& sectionCoord,
                                         int sectionSize,
                                         int sectionScale,
                                         int wallStonePrototypePosX,
                                         int wallStonePrototypeNegX,
                                         int wallStonePrototypePosZ,
                                         int wallStonePrototypeNegZ,
                                         const FoliageSpec& spec,
                                         bool& modified) {
            if (!spec.enabled || !spec.caveWallStoneEnabled) return;
            const bool hasAnyWallStonePrototype =
                (wallStonePrototypePosX >= 0)
                || (wallStonePrototypeNegX >= 0)
                || (wallStonePrototypePosZ >= 0)
                || (wallStonePrototypeNegZ >= 0);
            if (!hasAnyWallStonePrototype) return;
            const int stoneChance = std::max(0, std::min(100, spec.caveWallStoneSpawnPercent));
            if (stoneChance <= 0) return;
            const int minDepthFromSurface = std::max(1, std::min(256, spec.caveWallStoneMinDepthFromSurface));
            const int minDepthFromSurfaceLod = std::max(
                1,
                static_cast<int>(std::ceil(static_cast<float>(minDepthFromSurface) / static_cast<float>(sectionScale)))
            );

            const int minX = sectionCoord.x * sectionSize;
            const int minY = sectionCoord.y * sectionSize;
            const int minZ = sectionCoord.z * sectionSize;
            const int maxX = minX + sectionSize - 1;
            const int maxY = minY + sectionSize - 1;
            const int maxZ = minZ + sectionSize - 1;

            const int kNoSurface = std::numeric_limits<int>::min();
            std::vector<int> surfaceHeights(static_cast<size_t>(sectionSize * sectionSize), kNoSurface);
            auto surfaceIdx = [&](int lodX, int lodZ) {
                return (lodZ - minZ) * sectionSize + (lodX - minX);
            };

            for (int lodZ = minZ; lodZ <= maxZ; ++lodZ) {
                for (int lodX = minX; lodX <= maxX; ++lodX) {
                    const int worldX = lodX * sectionScale;
                    const int worldZ = lodZ * sectionScale;
                    float terrainHeight = 0.0f;
                    if (!ExpanseBiomeSystemLogic::SampleTerrain(
                            worldCtx,
                            static_cast<float>(worldX),
                            static_cast<float>(worldZ),
                            terrainHeight)) {
                        continue;
                    }
                    surfaceHeights[static_cast<size_t>(surfaceIdx(lodX, lodZ))] =
                        floorDivInt(static_cast<int>(std::floor(terrainHeight)), sectionScale);
                }
            }

            const uint32_t sectionSeed = hash3D(
                sectionCoord.x * 911 + sectionLod * 37,
                sectionCoord.y * 613 - sectionLod * 53,
                sectionCoord.z * 353 + 97
            );
            const int sectionVeinChance = std::max(1, std::min(100, stoneChance * 2));
            if (static_cast<int>(sectionSeed % 100u) >= sectionVeinChance) return;

            const int seedChance = std::max(1, stoneChance / 8);
            const int growChance = std::max(20, std::min(90, stoneChance * 5));
            const int growRounds = 2 + static_cast<int>((sectionSeed >> 8u) % 3u);
            const int maxPlacements = std::max(4, std::min(64, stoneChance * 2));
            int placedCount = 0;
            std::unordered_set<glm::ivec3, IVec3Hash> placedCells;

            auto hasPlacedNeighbor = [&](const glm::ivec3& cell) {
                static const std::array<glm::ivec3, 6> kNeighbors = {
                    glm::ivec3(1, 0, 0),
                    glm::ivec3(-1, 0, 0),
                    glm::ivec3(0, 1, 0),
                    glm::ivec3(0, -1, 0),
                    glm::ivec3(0, 0, 1),
                    glm::ivec3(0, 0, -1)
                };
                for (const glm::ivec3& n : kNeighbors) {
                    if (placedCells.count(cell + n) > 0) return true;
                }
                return false;
            };

            auto tryPlaceWallStone = [&](const glm::ivec3& placeCell, uint32_t seed, bool requireNeighbor) {
                if (placedCount >= maxPlacements) return false;
                if (getBlockAtLod(voxelWorld, sectionLod, placeCell) != 0u) return false;
                if (placedCells.count(placeCell) > 0) return false;
                if (requireNeighbor && !hasPlacedNeighbor(placeCell)) return false;

                std::array<int, 4> candidates = {-1, -1, -1, -1};
                int candidateCount = 0;
                auto trySupport = [&](const glm::ivec3& offset, int wallStonePrototypeID) {
                    if (wallStonePrototypeID < 0) return;
                    const glm::ivec3 supportCell = placeCell + offset;
                    const uint32_t supportID = getBlockAtLod(voxelWorld, sectionLod, supportCell);
                    if (supportID == 0u || supportID >= prototypes.size()) return;
                    const Entity& supportProto = prototypes[supportID];
                    if (!supportProto.isBlock || !supportProto.isSolid) return;
                    if (!isStoneSurfacePrototypeName(supportProto.name)) return;
                    candidates[static_cast<size_t>(candidateCount)] = wallStonePrototypeID;
                    candidateCount += 1;
                };

                // Support comes from the neighboring wall block; selected prototype encodes mount side.
                trySupport(glm::ivec3(1, 0, 0), wallStonePrototypePosX);
                trySupport(glm::ivec3(-1, 0, 0), wallStonePrototypeNegX);
                trySupport(glm::ivec3(0, 0, 1), wallStonePrototypePosZ);
                trySupport(glm::ivec3(0, 0, -1), wallStonePrototypeNegZ);
                if (candidateCount <= 0) return false;

                int pick = static_cast<int>((seed >> 9u) % static_cast<uint32_t>(candidateCount));
                pick = std::max(0, std::min(candidateCount - 1, pick));
                const int stoneID = candidates[static_cast<size_t>(pick)];
                if (stoneID < 0) return false;

                const int worldX = placeCell.x * sectionScale;
                const int worldY = placeCell.y * sectionScale;
                const int worldZ = placeCell.z * sectionScale;
                voxelWorld.setBlockLod(
                    sectionLod,
                    placeCell,
                    static_cast<uint32_t>(stoneID),
                    caveStoneColorForCell(worldX, worldY, worldZ),
                    false
                );
                placedCells.insert(placeCell);
                placedCount += 1;
                modified = true;
                return true;
            };

            for (int lodZ = minZ; lodZ <= maxZ && placedCount < maxPlacements; ++lodZ) {
                for (int lodX = minX; lodX <= maxX && placedCount < maxPlacements; ++lodX) {
                    const int surfaceY = surfaceHeights[static_cast<size_t>(surfaceIdx(lodX, lodZ))];
                    if (surfaceY == kNoSurface) continue;
                    const int maxPlaceY = std::min(maxY, surfaceY - minDepthFromSurfaceLod);
                    if (maxPlaceY < minY) continue;
                    for (int lodY = minY; lodY <= maxPlaceY && placedCount < maxPlacements; ++lodY) {
                        const int worldX = lodX * sectionScale;
                        const int worldY = lodY * sectionScale;
                        const int worldZ = lodZ * sectionScale;
                        const uint32_t seed = hash3D(worldX + 13, worldY - 47, worldZ + 71);
                        if (static_cast<int>(seed % 100u) >= seedChance) continue;
                        (void)tryPlaceWallStone(glm::ivec3(lodX, lodY, lodZ), seed, false);
                    }
                }
            }

            for (int round = 0; round < growRounds && placedCount < maxPlacements; ++round) {
                bool grew = false;
                for (int lodZ = minZ; lodZ <= maxZ && placedCount < maxPlacements; ++lodZ) {
                    for (int lodX = minX; lodX <= maxX && placedCount < maxPlacements; ++lodX) {
                        const int surfaceY = surfaceHeights[static_cast<size_t>(surfaceIdx(lodX, lodZ))];
                        if (surfaceY == kNoSurface) continue;
                        const int maxPlaceY = std::min(maxY, surfaceY - minDepthFromSurfaceLod);
                        if (maxPlaceY < minY) continue;
                        for (int lodY = minY; lodY <= maxPlaceY && placedCount < maxPlacements; ++lodY) {
                            const int worldX = lodX * sectionScale;
                            const int worldY = lodY * sectionScale;
                            const int worldZ = lodZ * sectionScale;
                            const uint32_t seed = hash3D(
                                worldX + 101 * (round + 1),
                                worldY - 53 * (round + 1),
                                worldZ + 211 * (round + 1)
                            );
                            if (static_cast<int>(seed % 100u) >= growChance) continue;
                            if (tryPlaceWallStone(glm::ivec3(lodX, lodY, lodZ), seed, true)) {
                                grew = true;
                            }
                        }
                    }
                }
                if (!grew) break;
            }
        }

        void writeCaveCeilingStoneToSection(const std::vector<Entity>& prototypes,
                                            const WorldContext& worldCtx,
                                            VoxelWorldContext& voxelWorld,
                                            int sectionLod,
                                            const glm::ivec3& sectionCoord,
                                            int sectionSize,
                                            int sectionScale,
                                            int ceilingStonePrototypePosX,
                                            int ceilingStonePrototypeNegX,
                                            int ceilingStonePrototypePosZ,
                                            int ceilingStonePrototypeNegZ,
                                            const FoliageSpec& spec,
                                            bool& modified) {
            if (!spec.enabled || !spec.caveCeilingStoneEnabled) return;
            const bool hasAnyCeilingStonePrototype =
                (ceilingStonePrototypePosX >= 0)
                || (ceilingStonePrototypeNegX >= 0)
                || (ceilingStonePrototypePosZ >= 0)
                || (ceilingStonePrototypeNegZ >= 0);
            if (!hasAnyCeilingStonePrototype) return;
            const int stoneChance = std::max(0, std::min(100, spec.caveCeilingStoneSpawnPercent));
            if (stoneChance <= 0) return;
            const int minDepthFromSurface = std::max(1, std::min(256, spec.caveCeilingStoneMinDepthFromSurface));
            const int minDepthFromSurfaceLod = std::max(
                1,
                static_cast<int>(std::ceil(static_cast<float>(minDepthFromSurface) / static_cast<float>(sectionScale)))
            );

            const int minX = sectionCoord.x * sectionSize;
            const int minY = sectionCoord.y * sectionSize;
            const int minZ = sectionCoord.z * sectionSize;
            const int maxX = minX + sectionSize - 1;
            const int maxY = minY + sectionSize - 1;
            const int maxZ = minZ + sectionSize - 1;

            const int kNoSurface = std::numeric_limits<int>::min();
            std::vector<int> surfaceHeights(static_cast<size_t>(sectionSize * sectionSize), kNoSurface);
            auto surfaceIdx = [&](int lodX, int lodZ) {
                return (lodZ - minZ) * sectionSize + (lodX - minX);
            };

            for (int lodZ = minZ; lodZ <= maxZ; ++lodZ) {
                for (int lodX = minX; lodX <= maxX; ++lodX) {
                    const int worldX = lodX * sectionScale;
                    const int worldZ = lodZ * sectionScale;
                    float terrainHeight = 0.0f;
                    if (!ExpanseBiomeSystemLogic::SampleTerrain(
                            worldCtx,
                            static_cast<float>(worldX),
                            static_cast<float>(worldZ),
                            terrainHeight)) {
                        continue;
                    }
                    surfaceHeights[static_cast<size_t>(surfaceIdx(lodX, lodZ))] =
                        floorDivInt(static_cast<int>(std::floor(terrainHeight)), sectionScale);
                }
            }

            const uint32_t sectionSeed = hash3D(
                sectionCoord.x * 911 + sectionLod * 37,
                sectionCoord.y * 613 - sectionLod * 53,
                sectionCoord.z * 353 + 131
            );
            const int sectionVeinChance = std::max(1, std::min(100, stoneChance * 2));
            if (static_cast<int>(sectionSeed % 100u) >= sectionVeinChance) return;

            const int seedChance = std::max(1, stoneChance / 8);
            const int growChance = std::max(20, std::min(90, stoneChance * 5));
            const int growRounds = 2 + static_cast<int>((sectionSeed >> 8u) % 3u);
            const int maxPlacements = std::max(4, std::min(64, stoneChance * 2));
            int placedCount = 0;
            std::unordered_set<glm::ivec3, IVec3Hash> placedCells;

            auto hasPlacedNeighbor = [&](const glm::ivec3& cell) {
                static const std::array<glm::ivec3, 6> kNeighbors = {
                    glm::ivec3(1, 0, 0),
                    glm::ivec3(-1, 0, 0),
                    glm::ivec3(0, 1, 0),
                    glm::ivec3(0, -1, 0),
                    glm::ivec3(0, 0, 1),
                    glm::ivec3(0, 0, -1)
                };
                for (const glm::ivec3& n : kNeighbors) {
                    if (placedCells.count(cell + n) > 0) return true;
                }
                return false;
            };

            auto tryPlaceCeilingStone = [&](const glm::ivec3& placeCell, uint32_t seed, bool requireNeighbor) {
                if (placedCount >= maxPlacements) return false;
                if (getBlockAtLod(voxelWorld, sectionLod, placeCell) != 0u) return false;
                if (placedCells.count(placeCell) > 0) return false;
                if (requireNeighbor && !hasPlacedNeighbor(placeCell)) return false;

                const glm::ivec3 belowCell = placeCell + glm::ivec3(0, -1, 0);
                if (getBlockAtLod(voxelWorld, sectionLod, belowCell) != 0u) return false;

                const glm::ivec3 supportCell = placeCell + glm::ivec3(0, 1, 0);
                const uint32_t supportID = getBlockAtLod(voxelWorld, sectionLod, supportCell);
                if (supportID == 0u || supportID >= prototypes.size()) return false;
                const Entity& supportProto = prototypes[supportID];
                if (!supportProto.isBlock || !supportProto.isSolid) return false;
                if (!isStoneSurfacePrototypeName(supportProto.name)) return false;

                std::array<int, 4> candidates = {
                    ceilingStonePrototypePosX,
                    ceilingStonePrototypeNegX,
                    ceilingStonePrototypePosZ,
                    ceilingStonePrototypeNegZ
                };
                int candidateCount = 0;
                for (int i = 0; i < 4; ++i) {
                    if (candidates[static_cast<size_t>(i)] >= 0) {
                        candidates[static_cast<size_t>(candidateCount)] = candidates[static_cast<size_t>(i)];
                        candidateCount += 1;
                    }
                }
                if (candidateCount <= 0) return false;

                int pick = static_cast<int>((seed >> 10u) % static_cast<uint32_t>(candidateCount));
                pick = std::max(0, std::min(candidateCount - 1, pick));
                const int stoneID = candidates[static_cast<size_t>(pick)];
                if (stoneID < 0) return false;

                const int worldX = placeCell.x * sectionScale;
                const int worldY = placeCell.y * sectionScale;
                const int worldZ = placeCell.z * sectionScale;
                voxelWorld.setBlockLod(
                    sectionLod,
                    placeCell,
                    static_cast<uint32_t>(stoneID),
                    caveStoneColorForCell(worldX, worldY, worldZ),
                    false
                );
                placedCells.insert(placeCell);
                placedCount += 1;
                modified = true;
                return true;
            };

            for (int lodZ = minZ; lodZ <= maxZ && placedCount < maxPlacements; ++lodZ) {
                for (int lodX = minX; lodX <= maxX && placedCount < maxPlacements; ++lodX) {
                    const int surfaceY = surfaceHeights[static_cast<size_t>(surfaceIdx(lodX, lodZ))];
                    if (surfaceY == kNoSurface) continue;
                    const int maxPlaceY = std::min(maxY, surfaceY - minDepthFromSurfaceLod);
                    if (maxPlaceY < minY) continue;
                    for (int lodY = minY; lodY <= maxPlaceY && placedCount < maxPlacements; ++lodY) {
                        const int worldX = lodX * sectionScale;
                        const int worldY = lodY * sectionScale;
                        const int worldZ = lodZ * sectionScale;
                        const uint32_t seed = hash3D(worldX + 17, worldY - 61, worldZ + 89);
                        if (static_cast<int>(seed % 100u) >= seedChance) continue;
                        (void)tryPlaceCeilingStone(glm::ivec3(lodX, lodY, lodZ), seed, false);
                    }
                }
            }

            for (int round = 0; round < growRounds && placedCount < maxPlacements; ++round) {
                bool grew = false;
                for (int lodZ = minZ; lodZ <= maxZ && placedCount < maxPlacements; ++lodZ) {
                    for (int lodX = minX; lodX <= maxX && placedCount < maxPlacements; ++lodX) {
                        const int surfaceY = surfaceHeights[static_cast<size_t>(surfaceIdx(lodX, lodZ))];
                        if (surfaceY == kNoSurface) continue;
                        const int maxPlaceY = std::min(maxY, surfaceY - minDepthFromSurfaceLod);
                        if (maxPlaceY < minY) continue;
                        for (int lodY = minY; lodY <= maxPlaceY && placedCount < maxPlacements; ++lodY) {
                            const int worldX = lodX * sectionScale;
                            const int worldY = lodY * sectionScale;
                            const int worldZ = lodZ * sectionScale;
                            const uint32_t seed = hash3D(
                                worldX + 131 * (round + 1),
                                worldY - 73 * (round + 1),
                                worldZ + 227 * (round + 1)
                            );
                            if (static_cast<int>(seed % 100u) >= growChance) continue;
                            if (tryPlaceCeilingStone(glm::ivec3(lodX, lodY, lodZ), seed, true)) {
                                grew = true;
                            }
                        }
                    }
                }
                if (!grew) break;
            }
        }

        void collectPineNubPlacements(int worldX,
                                      int worldZ,
                                      const PineSpec& spec,
                                      std::vector<PineNubPlacement>& outNubs) {
            outNubs.clear();
            const uint32_t baseSeed = hash2D(worldX + 104729, worldZ - 130363);
            const int nubCount = static_cast<int>((baseSeed >> 2u) & 0x3u); // 0..3
            if (nubCount <= 0) return;

            std::array<int, 3> bands = {0, 1, 2}; // low, mid, high
            for (int i = 0; i < 3; ++i) {
                uint32_t shuf = hash3D(worldX + 37 * i, worldZ - 53 * i, 911 + 71 * i);
                int j = i + static_cast<int>(shuf % static_cast<uint32_t>(3 - i));
                std::swap(bands[i], bands[j]);
            }

            const int safeTop = pineNubMaxTrunkOffsetY(spec);
            const int minOffset = std::min(std::max(3, spec.trunkHeight / 6), safeTop);
            const int span = safeTop - minOffset + 1;
            if (span <= 0) return;
            std::array<std::pair<int, int>, 3> bandRanges;
            for (int band = 0; band < 3; ++band) {
                int hMin = minOffset + (span * band) / 3;
                int hMax = minOffset + (span * (band + 1)) / 3 - 1;
                if (band == 2) hMax = safeTop;
                if (hMax < hMin) hMax = hMin;
                bandRanges[static_cast<size_t>(band)] = std::make_pair(hMin, std::min(hMax, safeTop));
            }

            const std::array<glm::ivec2, 4> dirs = {
                glm::ivec2(1, 0), glm::ivec2(-1, 0),
                glm::ivec2(0, 1), glm::ivec2(0, -1)
            };
            std::array<bool, 4> dirUsed = {false, false, false, false};
            outNubs.reserve(static_cast<size_t>(nubCount));

            for (int i = 0; i < nubCount; ++i) {
                int band = bands[static_cast<size_t>(i)];
                int hMin = bandRanges[static_cast<size_t>(band)].first;
                int hMax = bandRanges[static_cast<size_t>(band)].second;
                if (hMax < hMin) std::swap(hMin, hMax);
                const uint32_t hHash = hash3D(worldX + 17 * (i + 1), worldZ - 29 * (i + 1), 733 + band * 97);
                const int trunkOffsetY = hMin + static_cast<int>(hHash % static_cast<uint32_t>(hMax - hMin + 1));

                int dirIndex = static_cast<int>(hash3D(worldX - 31 * (i + 1), worldZ + 47 * (i + 1), 1181 + band * 53) % 4u);
                for (int attempt = 0; attempt < 4; ++attempt) {
                    int candidate = (dirIndex + attempt) % 4;
                    if (!dirUsed[static_cast<size_t>(candidate)]) {
                        dirIndex = candidate;
                        break;
                    }
                }
                dirUsed[static_cast<size_t>(dirIndex)] = true;
                outNubs.push_back({trunkOffsetY, dirs[static_cast<size_t>(dirIndex)]});
            }
        }

        const std::vector<glm::ivec3>& pineCanopyOffsets(const PineSpec& spec) {
            static std::unordered_map<std::uint64_t, std::vector<glm::ivec3>> offsetsCache;
            auto makeSignature = [&](const PineSpec& s) -> std::uint64_t {
                std::uint64_t sig = 1469598103934665603ull;
                auto mix = [&](std::uint64_t v) {
                    sig ^= v;
                    sig *= 1099511628211ull;
                };
                auto mixInt = [&](int v) { mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(v))); };
                auto mixFloat = [&](float v) {
                    std::uint32_t bits = 0;
                    std::memcpy(&bits, &v, sizeof(bits));
                    mix(static_cast<std::uint64_t>(bits));
                };
                mixInt(s.trunkHeight);
                mixInt(s.canopyOffset);
                mixInt(s.canopyLayers);
                mixFloat(s.canopyBottomRadius);
                mixFloat(s.canopyTopRadius);
                mixInt(s.canopyLowerExtension);
                mixFloat(s.canopyLowerRadiusBoost);
                return sig;
            };
            const std::uint64_t sig = makeSignature(spec);
            auto cached = offsetsCache.find(sig);
            if (cached != offsetsCache.end()) return cached->second;

            std::vector<glm::ivec3> offsets;

            const int canopyBase = spec.trunkHeight - spec.canopyOffset;
            for (int layer = -spec.canopyLowerExtension; layer < spec.canopyLayers; ++layer) {
                float radius = spec.canopyBottomRadius;
                if (layer < 0) {
                    float depthT = (spec.canopyLowerExtension > 0)
                        ? static_cast<float>(-layer) / static_cast<float>(spec.canopyLowerExtension)
                        : 0.0f;
                    radius += spec.canopyLowerRadiusBoost * depthT;
                } else {
                    float t = (spec.canopyLayers > 1)
                        ? static_cast<float>(layer) / static_cast<float>(spec.canopyLayers - 1)
                        : 0.0f;
                    radius = spec.canopyBottomRadius + t * (spec.canopyTopRadius - spec.canopyBottomRadius);
                }
                int r = static_cast<int>(std::ceil(radius));
                int y = canopyBase + layer;
                for (int dz = -r; dz <= r; ++dz) {
                    for (int dx = -r; dx <= r; ++dx) {
                        float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
                        if (dist <= radius) {
                            offsets.push_back(glm::ivec3(dx, y, dz));
                        }
                    }
                }
            }
            auto inserted = offsetsCache.emplace(sig, std::move(offsets));
            return inserted.first->second;
        }

        bool cellBelongsToSection(const glm::ivec3& worldCell, const glm::ivec3& sectionCoord, int sectionSize) {
            return floorDivInt(worldCell.x, sectionSize) == sectionCoord.x
                && floorDivInt(worldCell.y, sectionSize) == sectionCoord.y
                && floorDivInt(worldCell.z, sectionSize) == sectionCoord.z;
        }

        bool isTrunkPrototype(uint32_t id, int trunkPrototypeIDA, int trunkPrototypeIDB) {
            if (trunkPrototypeIDA >= 0 && static_cast<int>(id) == trunkPrototypeIDA) return true;
            if (trunkPrototypeIDB >= 0 && static_cast<int>(id) == trunkPrototypeIDB) return true;
            return false;
        }

        bool hasNearbyConflictingTrunk(const VoxelWorldContext& voxelWorld,
                                       int sectionLod,
                                       int trunkPrototypeIDA,
                                       int trunkPrototypeIDB,
                                       int baseX,
                                       int baseY,
                                       int baseZ,
                                       int radius) {
            for (int dz = -radius; dz <= radius; ++dz) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx == 0 && dz == 0) continue; // same tree trunk column
                    for (int dy = 0; dy < 6; ++dy) {
                        uint32_t id = getBlockAtLod(voxelWorld, sectionLod, glm::ivec3(baseX + dx, baseY + dy, baseZ + dz));
                        if (isTrunkPrototype(id, trunkPrototypeIDA, trunkPrototypeIDB)) return true;
                    }
                }
            }
            return false;
        }

        bool trunkColumnCanExist(const VoxelWorldContext& voxelWorld,
                                 int sectionLod,
                                 int trunkPrototypeIDA,
                                 int trunkPrototypeIDB,
                                 int worldX,
                                 int groundY,
                                 int worldZ,
                                 int trunkHeight) {
            // Require valid terrain support under the trunk.
            if (getBlockAtLod(voxelWorld, sectionLod, glm::ivec3(worldX, groundY, worldZ)) == 0u) return false;
            for (int i = 1; i <= trunkHeight; ++i) {
                glm::ivec3 pos(worldX, groundY + i, worldZ);
                uint32_t id = getBlockAtLod(voxelWorld, sectionLod, pos);
                if (id == 0) continue;
                if (isTrunkPrototype(id, trunkPrototypeIDA, trunkPrototypeIDB)) continue;
                return false;
            }
            return true;
        }

        int horizontalLogPrototypeFor(const std::vector<Entity>& prototypes,
                                      int sourcePrototypeID,
                                      FallDirection direction);
        int horizontalNubLogPrototypeFor(const std::vector<Entity>& prototypes,
                                         int sourcePrototypeID,
                                         FallDirection direction);
        int topLogPrototypeFor(const std::vector<Entity>& prototypes,
                               int sourcePrototypeID);
        bool leafCellTouchesAir(const VoxelWorldContext& voxelWorld,
                                int sectionLod,
                                const glm::ivec3& cell) {
            static const std::array<glm::ivec3, 6> kNeighborDirs = {
                glm::ivec3(1, 0, 0),
                glm::ivec3(-1, 0, 0),
                glm::ivec3(0, 1, 0),
                glm::ivec3(0, -1, 0),
                glm::ivec3(0, 0, 1),
                glm::ivec3(0, 0, -1)
            };
            for (const glm::ivec3& dir : kNeighborDirs) {
                if (getBlockAtLod(voxelWorld, sectionLod, cell + dir) == 0u) {
                    return true;
                }
            }
            return false;
        }

        void applyLeafFanShell(VoxelWorldContext& voxelWorld,
                               int sectionLod,
                               int sectionSize,
                               int leafFanPrototypeID,
                               uint32_t leafColor,
                               const std::vector<glm::ivec3>& placedLeafCells,
                               std::unordered_set<glm::ivec3, IVec3Hash>& outTouchedSections,
                               bool& modified) {
            if (leafFanPrototypeID < 0 || placedLeafCells.empty()) return;
            for (const glm::ivec3& cell : placedLeafCells) {
                if (!leafCellTouchesAir(voxelWorld, sectionLod, cell)) continue;
                if (getBlockAtLod(voxelWorld, sectionLod, cell) == 0u) continue;
                voxelWorld.setBlockLod(
                    sectionLod,
                    cell,
                    static_cast<uint32_t>(leafFanPrototypeID),
                    leafColor,
                    false
                );
                outTouchedSections.insert(glm::ivec3(
                    floorDivInt(cell.x, sectionSize),
                    floorDivInt(cell.y, sectionSize),
                    floorDivInt(cell.z, sectionSize)
                ));
                modified = true;
            }
        }

        void convertExposedLeafShellInSection(VoxelWorldContext& voxelWorld,
                                              const std::vector<Entity>& prototypes,
                                              int sectionLod,
                                              const glm::ivec3& sectionCoord,
                                              int sectionSize,
                                              int pineLeafPrototypeID,
                                              int pineLeafFanPrototypeID,
                                              int oakLeafFanPrototypeID,
                                              uint32_t leafColor,
                                              std::unordered_set<glm::ivec3, IVec3Hash>& outTouchedSections,
                                              bool& modified) {
            if (!voxelWorld.enabled || sectionSize <= 0) return;
            if (pineLeafFanPrototypeID < 0 && oakLeafFanPrototypeID < 0) return;

            const int minX = sectionCoord.x * sectionSize;
            const int minY = sectionCoord.y * sectionSize;
            const int minZ = sectionCoord.z * sectionSize;
            const int maxX = minX + sectionSize - 1;
            const int maxY = minY + sectionSize - 1;
            const int maxZ = minZ + sectionSize - 1;

            for (int y = minY; y <= maxY; ++y) {
                for (int z = minZ; z <= maxZ; ++z) {
                    for (int x = minX; x <= maxX; ++x) {
                        const glm::ivec3 cell(x, y, z);
                        const uint32_t id = getBlockAtLod(voxelWorld, sectionLod, cell);
                        if (id == 0u || id >= prototypes.size()) continue;

                        int targetFanPrototypeID = -1;
                        if (pineLeafFanPrototypeID >= 0
                            && pineLeafPrototypeID >= 0
                            && static_cast<int>(id) == pineLeafPrototypeID) {
                            targetFanPrototypeID = pineLeafFanPrototypeID;
                        } else if (oakLeafFanPrototypeID >= 0
                                   && isJungleLeafPrototypeName(prototypes[static_cast<size_t>(id)].name)) {
                            targetFanPrototypeID = oakLeafFanPrototypeID;
                        } else {
                            continue;
                        }

                        if (!leafCellTouchesAir(voxelWorld, sectionLod, cell)) continue;
                        voxelWorld.setBlockLod(
                            sectionLod,
                            cell,
                            static_cast<uint32_t>(targetFanPrototypeID),
                            leafColor,
                            false
                        );
                        outTouchedSections.insert(sectionCoord);
                        modified = true;
                    }
                }
            }
        }

        void writeTreeToWorld(VoxelWorldContext& voxelWorld,
                              const std::vector<Entity>& prototypes,
                              int sectionLod,
                              int sectionSize,
                              int trunkPrototypeID,
                              int topPrototypeID,
                              int nubPrototypeID,
                              int leafPrototypeID,
                              int leafFanPrototypeID,
                              uint32_t trunkColor,
                              uint32_t leafColor,
                              int worldX,
                              int groundY,
                              int worldZ,
                              const PineSpec& spec,
                              std::unordered_set<glm::ivec3, IVec3Hash>& outTouchedSections,
                              bool& modified) {
            auto setIfEmpty = [&](const glm::ivec3& cell, uint32_t id, uint32_t color) -> bool {
                if (getBlockAtLod(voxelWorld, sectionLod, cell) != 0u) return false;
                voxelWorld.setBlockLod(sectionLod, cell, id, color, false);
                outTouchedSections.insert(glm::ivec3(
                    floorDivInt(cell.x, sectionSize),
                    floorDivInt(cell.y, sectionSize),
                    floorDivInt(cell.z, sectionSize)
                ));
                modified = true;
                return true;
            };

            for (int i = 1; i <= spec.trunkHeight; ++i) {
                const uint32_t placeID = (i == spec.trunkHeight && topPrototypeID >= 0)
                    ? static_cast<uint32_t>(topPrototypeID)
                    : static_cast<uint32_t>(trunkPrototypeID);
                setIfEmpty(glm::ivec3(worldX, groundY + i, worldZ),
                           placeID,
                           trunkColor);
            }

            std::vector<PineNubPlacement> nubs;
            collectPineNubPlacements(worldX, worldZ, spec, nubs);
            for (const auto& nub : nubs) {
                const int trunkY = groundY + nub.trunkOffsetY;
                if (trunkY <= groundY + 1 || trunkY >= groundY + spec.trunkHeight) continue;
                glm::ivec3 nubCell(worldX + nub.dir.x, trunkY, worldZ + nub.dir.y);
                FallDirection axisDir = (nub.dir.x != 0) ? FallDirection::PosX : FallDirection::PosZ;
                int horizontalID = horizontalNubLogPrototypeFor(prototypes, nubPrototypeID, axisDir);
                if (horizontalID < 0) horizontalID = trunkPrototypeID;
                setIfEmpty(nubCell, static_cast<uint32_t>(horizontalID), trunkColor);
            }

            const auto& canopy = pineCanopyOffsets(spec);
            std::vector<glm::ivec3> placedLeafCells;
            placedLeafCells.reserve(canopy.size());
            for (const auto& off : canopy) {
                glm::ivec3 cell(worldX + off.x, groundY + off.y, worldZ + off.z);
                // Keep canopy above trunk base to preserve pine silhouette.
                if (cell.y <= groundY + 1) continue;
                if (setIfEmpty(cell, static_cast<uint32_t>(leafPrototypeID), leafColor)) {
                    placedLeafCells.push_back(cell);
                }
            }
            applyLeafFanShell(
                voxelWorld,
                sectionLod,
                sectionSize,
                leafFanPrototypeID,
                leafColor,
                placedLeafCells,
                outTouchedSections,
                modified
            );
        }

        void writeBareTreeToWorld(VoxelWorldContext& voxelWorld,
                                  int sectionLod,
                                  int sectionSize,
                                  int trunkPrototypeID,
                                  int branchPrototypeIDX,
                                  int branchPrototypeIDZ,
                                  int wallBranchLongPosXID,
                                  int wallBranchLongNegXID,
                                  int wallBranchLongPosZID,
                                  int wallBranchLongNegZID,
                                  int wallBranchLongTipPosXID,
                                  int wallBranchLongTipNegXID,
                                  int wallBranchLongTipPosZID,
                                  int wallBranchLongTipNegZID,
                                  uint32_t trunkColor,
                                  uint32_t branchColor,
                                  int worldX,
                                  int groundY,
                                  int worldZ,
                                  int trunkHeight,
                                  uint32_t seed,
                                  std::unordered_set<glm::ivec3, IVec3Hash>& outTouchedSections,
                                  bool& modified) {
            if (trunkPrototypeID < 0 || trunkHeight < 2) return;
            auto setIfEmpty = [&](const glm::ivec3& cell, uint32_t id, uint32_t color) -> bool {
                if (getBlockAtLod(voxelWorld, sectionLod, cell) != 0u) return false;
                voxelWorld.setBlockLod(sectionLod, cell, id, color, false);
                outTouchedSections.insert(glm::ivec3(
                    floorDivInt(cell.x, sectionSize),
                    floorDivInt(cell.y, sectionSize),
                    floorDivInt(cell.z, sectionSize)
                ));
                modified = true;
                return true;
            };

            for (int i = 1; i <= trunkHeight; ++i) {
                setIfEmpty(
                    glm::ivec3(worldX, groundY + i, worldZ),
                    static_cast<uint32_t>(trunkPrototypeID),
                    trunkColor
                );
            }

            const std::array<glm::ivec2, 4> dirs = {
                glm::ivec2(1, 0),
                glm::ivec2(-1, 0),
                glm::ivec2(0, 1),
                glm::ivec2(0, -1)
            };
            auto longBranchIDForDir = [&](const glm::ivec2& dir,
                                          int posXID,
                                          int negXID,
                                          int posZID,
                                          int negZID) {
                if (dir.x > 0) return negXID;
                if (dir.x < 0) return posXID;
                if (dir.y > 0) return negZID;
                if (dir.y < 0) return posZID;
                return -1;
            };
            const int branchHeightMin = std::max(2, trunkHeight / 3);
            const int branchHeightMax = std::max(branchHeightMin, trunkHeight - 2);
            const int branchCount = 1 + static_cast<int>((seed >> 3u) & 1u); // 1..2
            std::array<bool, 4> usedDirs = {false, false, false, false};
            for (int i = 0; i < branchCount; ++i) {
                const uint32_t branchSeed = hash3D(worldX + 911 * (i + 1), groundY + 67 * (i + 1), worldZ - 503 * (i + 1));
                const int branchHeight = branchHeightMin + static_cast<int>(
                    branchSeed % static_cast<uint32_t>(branchHeightMax - branchHeightMin + 1)
                );
                int dirIdx = static_cast<int>((branchSeed >> 9u) % dirs.size());
                for (int k = 0; k < 4; ++k) {
                    const int candidate = (dirIdx + k) % 4;
                    if (!usedDirs[static_cast<size_t>(candidate)]) {
                        dirIdx = candidate;
                        break;
                    }
                }
                usedDirs[static_cast<size_t>(dirIdx)] = true;
                const glm::ivec2 dir = dirs[static_cast<size_t>(dirIdx)];
                const glm::ivec3 shortBranchCell(worldX + dir.x, groundY + branchHeight, worldZ + dir.y);
                int shortBranchID = (dir.x != 0) ? branchPrototypeIDX : branchPrototypeIDZ;
                if (shortBranchID < 0) shortBranchID = (branchPrototypeIDX >= 0) ? branchPrototypeIDX : branchPrototypeIDZ;
                if (shortBranchID >= 0) {
                    setIfEmpty(shortBranchCell, static_cast<uint32_t>(shortBranchID), branchColor);
                }

                // Two-piece long branch set: piece 1 then piece 2 shifted +1 in the branch direction and +1 up.
                if (((branchSeed >> 17u) % 100u) < 60u) {
                    const int longBaseID = longBranchIDForDir(
                        dir,
                        wallBranchLongPosXID,
                        wallBranchLongNegXID,
                        wallBranchLongPosZID,
                        wallBranchLongNegZID
                    );
                    const int longTipID = longBranchIDForDir(
                        dir,
                        wallBranchLongTipPosXID,
                        wallBranchLongTipNegXID,
                        wallBranchLongTipPosZID,
                        wallBranchLongTipNegZID
                    );
                    if (longBaseID >= 0 && longTipID >= 0) {
                        const glm::ivec3 longBaseCell(worldX + dir.x, groundY + branchHeight + 1, worldZ + dir.y);
                        const glm::ivec3 longTipCell(worldX + dir.x * 2, groundY + branchHeight + 2, worldZ + dir.y * 2);
                        if (setIfEmpty(longBaseCell, static_cast<uint32_t>(longBaseID), branchColor)) {
                            setIfEmpty(longTipCell, static_cast<uint32_t>(longTipID), branchColor);
                        }
                    }
                }
            }
        }

        void writeJungleTreeToWorld(VoxelWorldContext& voxelWorld,
                                    int sectionLod,
                                    int sectionSize,
                                    int trunkPrototypeID,
                                    int defaultLeafPrototypeID,
                                    int leafFanPrototypeID,
                                    const std::array<int, kJungleLeafVariantCount>& jungleLeafPrototypeIDs,
                                    int jungleLeafPrototypeCount,
                                    uint32_t trunkColor,
                                    uint32_t leafColor,
                                    int worldX,
                                    int groundY,
                                    int worldZ,
                                    int trunkHeight,
                                    int canopyRadius,
                                    std::unordered_set<glm::ivec3, IVec3Hash>& outTouchedSections,
                                    bool& modified) {
            auto setIfEmpty = [&](const glm::ivec3& cell, uint32_t id, uint32_t color) -> bool {
                if (getBlockAtLod(voxelWorld, sectionLod, cell) != 0u) return false;
                voxelWorld.setBlockLod(sectionLod, cell, id, color, false);
                outTouchedSections.insert(glm::ivec3(
                    floorDivInt(cell.x, sectionSize),
                    floorDivInt(cell.y, sectionSize),
                    floorDivInt(cell.z, sectionSize)
                ));
                modified = true;
                return true;
            };

            for (int i = 1; i <= trunkHeight; ++i) {
                setIfEmpty(glm::ivec3(worldX, groundY + i, worldZ),
                           static_cast<uint32_t>(trunkPrototypeID),
                           trunkColor);
            }

            const int centerY = groundY + trunkHeight + 2;
            const int r = std::max(2, canopyRadius);
            std::vector<glm::ivec3> placedLeafCells;
            placedLeafCells.reserve(static_cast<size_t>((2 * r + 1) * (2 * r + 1) * (2 * r + 1)));
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    for (int dz = -r; dz <= r; ++dz) {
                        const float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy + dz * dz));
                        if (dist > static_cast<float>(r)) continue;
                        const glm::ivec3 leafCell(worldX + dx, centerY + dy, worldZ + dz);
                        if (leafCell.y <= groundY + 1) continue;
                        int leafPrototypeID = chooseJungleLeafPrototypeIDForCell(
                            jungleLeafPrototypeIDs,
                            jungleLeafPrototypeCount,
                            leafCell
                        );
                        if (leafPrototypeID < 0) leafPrototypeID = defaultLeafPrototypeID;
                        if (leafPrototypeID < 0) continue;
                        if (setIfEmpty(leafCell, static_cast<uint32_t>(leafPrototypeID), leafColor)) {
                            placedLeafCells.push_back(leafCell);
                        }
                    }
                }
            }
            applyLeafFanShell(
                voxelWorld,
                sectionLod,
                sectionSize,
                leafFanPrototypeID,
                leafColor,
                placedLeafCells,
                outTouchedSections,
                modified
            );
        }

        int findPrototypeIDByName(const std::vector<Entity>& prototypes, const std::string& name) {
            for (const auto& proto : prototypes) {
                if (proto.name == name) return proto.prototypeID;
            }
            return -1;
        }

        int horizontalLogPrototypeFor(const std::vector<Entity>& prototypes,
                                      int sourcePrototypeID,
                                      FallDirection direction) {
            if (sourcePrototypeID < 0 || sourcePrototypeID >= static_cast<int>(prototypes.size())) {
                return sourcePrototypeID;
            }
            const std::string& srcName = prototypes[sourcePrototypeID].name;
            bool familyTwo = srcName.find("2") != std::string::npos;
            const bool axisX = (direction == FallDirection::PosX || direction == FallDirection::NegX);
            const std::string targetName = axisX
                ? (familyTwo ? "FirLog2TexX" : "FirLog1TexX")
                : (familyTwo ? "FirLog2TexZ" : "FirLog1TexZ");
            int targetID = findPrototypeIDByName(prototypes, targetName);
            return (targetID >= 0) ? targetID : sourcePrototypeID;
        }

        int horizontalNubLogPrototypeFor(const std::vector<Entity>& prototypes,
                                         int sourcePrototypeID,
                                         FallDirection direction) {
            if (sourcePrototypeID < 0 || sourcePrototypeID >= static_cast<int>(prototypes.size())) {
                return sourcePrototypeID;
            }
            const std::string& srcName = prototypes[sourcePrototypeID].name;
            const bool familyTwo = srcName.find("2") != std::string::npos;
            const bool axisX = (direction == FallDirection::PosX || direction == FallDirection::NegX);
            const std::string targetName = axisX
                ? (familyTwo ? "FirLog2NubTexX" : "FirLog1NubTexX")
                : (familyTwo ? "FirLog2NubTexZ" : "FirLog1NubTexZ");
            int targetID = findPrototypeIDByName(prototypes, targetName);
            if (targetID >= 0) return targetID;
            return horizontalLogPrototypeFor(prototypes, sourcePrototypeID, direction);
        }

        int topLogPrototypeFor(const std::vector<Entity>& prototypes,
                               int sourcePrototypeID) {
            if (sourcePrototypeID < 0 || sourcePrototypeID >= static_cast<int>(prototypes.size())) {
                return sourcePrototypeID;
            }
            const std::string& srcName = prototypes[sourcePrototypeID].name;
            const bool familyTwo = srcName.find("2") != std::string::npos;
            const std::string targetName = familyTwo ? "FirLog2TopTex" : "FirLog1TopTex";
            int targetID = findPrototypeIDByName(prototypes, targetName);
            return (targetID >= 0) ? targetID : sourcePrototypeID;
        }

        FallDirection chooseFallDirection(const glm::ivec3& pivot, uint32_t salt) {
            const uint32_t h = hash3D(pivot.x + static_cast<int>(salt), pivot.y, pivot.z - static_cast<int>(salt));
            switch (h % 4u) {
                case 0: return FallDirection::PosX;
                case 1: return FallDirection::NegX;
                case 2: return FallDirection::PosZ;
                default: return FallDirection::NegZ;
            }
        }

        glm::ivec3 rotateAroundPivot90(const glm::ivec3& pos,
                                       const glm::ivec3& pivot,
                                       FallDirection direction) {
            const glm::ivec3 local = pos - pivot;
            switch (direction) {
                case FallDirection::PosX: // +Y -> +X
                    return glm::ivec3(pivot.x + local.y, pivot.y - local.x, pivot.z + local.z);
                case FallDirection::NegX: // +Y -> -X
                    return glm::ivec3(pivot.x - local.y, pivot.y + local.x, pivot.z + local.z);
                case FallDirection::PosZ: // +Y -> +Z
                    return glm::ivec3(pivot.x + local.x, pivot.y - local.z, pivot.z + local.y);
                case FallDirection::NegZ: // +Y -> -Z
                    return glm::ivec3(pivot.x + local.x, pivot.y + local.z, pivot.z - local.y);
                default:
                    return pos;
            }
        }

        void collectConnectedLogs(const VoxelWorldContext& voxelWorld,
                                  const std::vector<Entity>& prototypes,
                                  const glm::ivec3& seedCell,
                                  std::unordered_set<glm::ivec3, IVec3Hash>& outLogs) {
            static const std::array<glm::ivec3, 6> kNeighbors = {
                glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0),
                glm::ivec3(0, 1, 0), glm::ivec3(0, -1, 0),
                glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1)
            };
            outLogs.clear();
            if (!isPineAnyLogPrototypeID(prototypes, voxelWorld.getBlockWorld(seedCell))) return;
            std::queue<glm::ivec3> q;
            q.push(seedCell);
            outLogs.insert(seedCell);
            while (!q.empty()) {
                glm::ivec3 cell = q.front();
                q.pop();
                for (const auto& step : kNeighbors) {
                    glm::ivec3 next = cell + step;
                    if (outLogs.count(next) > 0) continue;
                    if (!isPineAnyLogPrototypeID(prototypes, voxelWorld.getBlockWorld(next))) continue;
                    outLogs.insert(next);
                    q.push(next);
                }
            }
        }

        void collectConnectedLeaves(const VoxelWorldContext& voxelWorld,
                                    const std::vector<Entity>& prototypes,
                                    const std::unordered_set<glm::ivec3, IVec3Hash>& logs,
                                    std::unordered_set<glm::ivec3, IVec3Hash>& outLeaves) {
            static const std::array<glm::ivec3, 6> kLeafSteps = {
                glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0),
                glm::ivec3(0, 1, 0), glm::ivec3(0, -1, 0),
                glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1)
            };
            outLeaves.clear();
            if (logs.empty()) return;

            glm::ivec3 minC(1 << 30);
            glm::ivec3 maxC(-(1 << 30));
            for (const auto& cell : logs) {
                minC = glm::min(minC, cell);
                maxC = glm::max(maxC, cell);
            }
            minC -= glm::ivec3(6, 6, 6);
            maxC += glm::ivec3(6, 6, 6);
            auto inBounds = [&](const glm::ivec3& p) {
                return p.x >= minC.x && p.y >= minC.y && p.z >= minC.z
                    && p.x <= maxC.x && p.y <= maxC.y && p.z <= maxC.z;
            };

            std::queue<glm::ivec3> q;
            for (const auto& log : logs) {
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            glm::ivec3 near = log + glm::ivec3(dx, dy, dz);
                            if (!inBounds(near)) continue;
                            if (outLeaves.count(near) > 0) continue;
                            if (!isLeafPrototypeID(prototypes, voxelWorld.getBlockWorld(near))) continue;
                            outLeaves.insert(near);
                            q.push(near);
                        }
                    }
                }
            }

            while (!q.empty()) {
                glm::ivec3 cell = q.front();
                q.pop();
                for (const auto& step : kLeafSteps) {
                    glm::ivec3 next = cell + step;
                    if (!inBounds(next)) continue;
                    if (outLeaves.count(next) > 0) continue;
                    if (!isLeafPrototypeID(prototypes, voxelWorld.getBlockWorld(next))) continue;
                    outLeaves.insert(next);
                    q.push(next);
                }
            }
        }

        void processSingleTreeFall(BaseSystem& baseSystem,
                                   std::vector<Entity>& prototypes,
                                   const glm::ivec3& removedCell) {
            if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return;
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;

            glm::ivec3 seed = removedCell + glm::ivec3(0, 1, 0);
            if (!isPineAnyLogPrototypeID(prototypes, voxelWorld.getBlockWorld(seed))) return;

            std::unordered_set<glm::ivec3, IVec3Hash> logs;
            collectConnectedLogs(voxelWorld, prototypes, seed, logs);
            if (logs.empty()) return;

            std::unordered_set<glm::ivec3, IVec3Hash> leaves;
            collectConnectedLeaves(voxelWorld, prototypes, logs, leaves);

            struct CellData {
                glm::ivec3 pos;
                uint32_t id = 0;
                uint32_t color = 0;
                bool isLog = false;
            };
            std::vector<CellData> cells;
            cells.reserve(logs.size() + leaves.size());
            std::unordered_set<glm::ivec3, IVec3Hash> cleared;
            cleared.reserve(logs.size() + leaves.size());

            for (const auto& cell : logs) {
                uint32_t id = voxelWorld.getBlockWorld(cell);
                if (!isPineAnyLogPrototypeID(prototypes, id)) continue;
                cells.push_back({cell, id, voxelWorld.getColorWorld(cell), true});
                cleared.insert(cell);
            }
            for (const auto& cell : leaves) {
                uint32_t id = voxelWorld.getBlockWorld(cell);
                if (!isLeafPrototypeID(prototypes, id)) continue;
                cells.push_back({cell, id, voxelWorld.getColorWorld(cell), false});
                cleared.insert(cell);
            }
            if (cells.empty()) return;

            for (const auto& cell : cleared) {
                voxelWorld.setBlockWorld(cell, 0, 0);
            }

            const FallDirection direction = chooseFallDirection(removedCell, static_cast<uint32_t>(baseSystem.frameIndex & 0xffffffffu));

            struct Placement {
                glm::ivec3 pos;
                uint32_t id = 0;
                uint32_t color = 0;
                bool isLog = false;
            };
            std::vector<Placement> placements;
            placements.reserve(cells.size());
            std::unordered_set<glm::ivec3, IVec3Hash> occupied;
            occupied.reserve(cells.size());

            for (const auto& cell : cells) {
                glm::ivec3 rotated = rotateAroundPivot90(cell.pos, removedCell, direction);
                uint32_t placeId = cell.id;
                if (cell.isLog) {
                    int horizontalID = horizontalLogPrototypeFor(prototypes, static_cast<int>(cell.id), direction);
                    if (horizontalID >= 0) placeId = static_cast<uint32_t>(horizontalID);
                }
                placements.push_back({rotated, placeId, cell.color, cell.isLog});
                occupied.insert(rotated);
            }

            auto canDropOne = [&]() {
                for (const auto& p : placements) {
                    glm::ivec3 below = p.pos + glm::ivec3(0, -1, 0);
                    if (below.y < -512) return false;
                    if (occupied.count(below) > 0) continue;
                    if (voxelWorld.getBlockWorld(below) != 0) return false;
                }
                return true;
            };

            while (canDropOne()) {
                occupied.clear();
                for (auto& p : placements) {
                    p.pos.y -= 1;
                    occupied.insert(p.pos);
                }
            }

            std::vector<glm::ivec3> changedCells;
            changedCells.reserve(cleared.size() + placements.size());
            int placedLogCount = 0;
            for (const auto& cell : cleared) changedCells.push_back(cell);
            for (const auto& p : placements) {
                if (voxelWorld.getBlockWorld(p.pos) != 0) continue;
                voxelWorld.setBlockWorld(p.pos, p.id, p.color);
                changedCells.push_back(p.pos);
                if (p.isLog) placedLogCount += 1;
            }
            if (placedLogCount > 0) {
                triggerGameplaySfx(baseSystem, "tree_fall.ck", 0.12f);
            }

            if (!changedCells.empty()) {
                std::unordered_set<glm::ivec3, IVec3Hash> touchedSections;
                touchedSections.reserve(changedCells.size());
                const int sectionSize = voxelWorld.sectionSize > 0 ? voxelWorld.sectionSize : 64;
                for (const auto& cell : changedCells) {
                    glm::ivec3 sec(
                        floorDivInt(cell.x, sectionSize),
                        floorDivInt(cell.y, sectionSize),
                        floorDivInt(cell.z, sectionSize)
                    );
                    touchedSections.insert(sec);
                }
                for (const auto& sec : touchedSections) {
                    glm::ivec3 requestCell = sec * sectionSize;
                    VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, requestCell);
                }
            }
        }
    }

    void GetTreeFoliagePerfStats(size_t& pendingSections,
                                 size_t& pendingDependencies,
                                 size_t& backfillVisited,
                                 int& selectedSections,
                                 int& processedSections,
                                 int& deferredByTimeBudget,
                                 int& backfillAppended,
                                 bool& backfillRan,
                                 float& updateMs) {
        pendingSections = g_treePerfStats.pendingSections;
        pendingDependencies = g_treePerfStats.pendingDependencies;
        backfillVisited = g_treePerfStats.backfillVisited;
        selectedSections = g_treePerfStats.selectedSections;
        processedSections = g_treePerfStats.processedSections;
        deferredByTimeBudget = g_treePerfStats.deferredByTimeBudget;
        backfillAppended = g_treePerfStats.backfillAppended;
        backfillRan = g_treePerfStats.backfillRan;
        updateMs = g_treePerfStats.updateMs;
    }

    void NotifyPineLogRemoved(const glm::ivec3& worldCell, int removedPrototypeID) {
        g_pendingPineLogRemovals.emplace_back(worldCell, removedPrototypeID);
    }

    bool IsColumnFoliageReady(int lod, const glm::ivec2& columnCoord) {
        auto it = g_treeColumnFoliageReady.find(VoxelColumnModel::ColumnKey{lod, columnCoord});
        if (it == g_treeColumnFoliageReady.end()) return false;
        return it->second;
    }

    void UpdateExpanseTrees(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt;
        (void)win;
        if (!baseSystem.world || !baseSystem.voxelWorld || !baseSystem.registry) return;
        if (!baseSystem.voxelWorld->enabled) return;

        WorldContext& worldCtx = *baseSystem.world;
        VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
        if (!worldCtx.expanse.loaded) return;
        if (baseSystem.player
            && baseSystem.player->prismalMapMode == 2
            && getRegistryBool(baseSystem, "PrismalMapPauseWorldStreaming", true)) {
            return;
        }
        g_treeFrameCounter += 1;
        auto updateStart = std::chrono::steady_clock::now();
        const bool columnMode = getRegistryString(baseSystem, "voxelStorageMode", "cubic") == "column";
        const int columnFoliageMaxLod = std::max(0, getRegistryInt(baseSystem, "voxelColumnFoliageMaxLod", 0));
        const int columnSurfaceDepthBands = std::max(1, getRegistryInt(baseSystem, "voxelColumnSurfaceDepthBands", 4));
        const int columnSurfaceUpBands = std::max(0, getRegistryInt(baseSystem, "voxelColumnSurfaceUpBands", 2));
        std::unordered_map<VoxelSectionKey, int, VoxelSectionKeyHash> columnSurfaceSectionYCache;
        columnSurfaceSectionYCache.reserve(512);

        auto isFoliageEligibleSection = [&](const VoxelSectionKey& key) -> bool {
            auto sectionIt = voxelWorld.sections.find(key);
            if (sectionIt == voxelWorld.sections.end()) return false;
            if (!columnMode) return true;
            if (key.lod > columnFoliageMaxLod) return false;
            if (!TerrainSystemLogic::IsColumnSurfaceReady(key.lod, glm::ivec2(key.coord.x, key.coord.z))) {
                return false;
            }

            const VoxelSectionKey columnCacheKey{
                key.lod,
                glm::ivec3(key.coord.x, 0, key.coord.z)
            };
            int surfaceSectionY = 0;
            auto cacheIt = columnSurfaceSectionYCache.find(columnCacheKey);
            if (cacheIt != columnSurfaceSectionYCache.end()) {
                surfaceSectionY = cacheIt->second;
            } else {
                const int sectionScale = sectionScaleForLod(key.lod);
                const int sectionSize = sectionSizeForLod(voxelWorld, key.lod);
                const float sampleWX = (static_cast<float>(key.coord.x) + 0.5f)
                    * static_cast<float>(sectionSize * sectionScale);
                const float sampleWZ = (static_cast<float>(key.coord.z) + 0.5f)
                    * static_cast<float>(sectionSize * sectionScale);
                float terrainHeight = 0.0f;
                const bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(
                    worldCtx,
                    sampleWX,
                    sampleWZ,
                    terrainHeight
                );
                const int targetY = isLand
                    ? static_cast<int>(std::floor(terrainHeight))
                    : static_cast<int>(std::floor(worldCtx.expanse.waterSurface));
                surfaceSectionY = floorDivInt(targetY, sectionSize * sectionScale);
                columnSurfaceSectionYCache.emplace(columnCacheKey, surfaceSectionY);
            }

            const bool nearSurfaceBand =
                key.coord.y >= (surfaceSectionY - columnSurfaceDepthBands)
                && key.coord.y <= (surfaceSectionY + columnSurfaceUpBands);
            if (nearSurfaceBand) return true;

            // Keep deep underwater foliage passes (kelp/urchin/sand dollar markers)
            // active in unified-depth sections even when they are far below surface bands.
            const bool waterFoliageEnabled = getRegistryBool(baseSystem, "WaterFoliageGenerationEnabled", true);
            const bool unifiedDepthsEnabled = getRegistryBool(baseSystem, "UnifiedDepthsEnabled", true);
            if (!waterFoliageEnabled || !unifiedDepthsEnabled) return false;

            const int sectionScale = sectionScaleForLod(key.lod);
            const int sectionSize = sectionSizeForLod(voxelWorld, key.lod);
            const int worldSectionMinY = key.coord.y * sectionSize * sectionScale;
            const int worldSectionMaxY = worldSectionMinY + (sectionSize * sectionScale - 1);
            const int unifiedDepthsTopY = getRegistryInt(baseSystem, "expanseDepthSplitY", -98) - 1;
            const int unifiedDepthsMinY = std::min(
                unifiedDepthsTopY,
                getRegistryInt(baseSystem, "UnifiedDepthsMinY", -200)
            );
            return worldSectionMaxY >= unifiedDepthsMinY
                && worldSectionMinY <= unifiedDepthsTopY;
        };

        auto rebuildColumnFoliageReady = [&]() {
            if (!columnMode) {
                g_treeColumnFoliageReady.clear();
                g_treeColumnFoliageLastSeenFrame.clear();
                return;
            }

            const int holdFrames = std::max(0, getRegistryInt(baseSystem, "voxelColumnFoliageReadyHoldFrames", 120));
            std::unordered_set<VoxelColumnModel::ColumnKey, VoxelColumnModel::ColumnKeyHash> seenColumns;
            seenColumns.reserve(voxelWorld.sections.size());

            for (const auto& [key, section] : voxelWorld.sections) {
                if (key.lod > columnFoliageMaxLod) continue;
                if (!isFoliageEligibleSection(key)) continue;
                VoxelColumnModel::ColumnKey columnKey{key.lod, glm::ivec2(key.coord.x, key.coord.z)};
                seenColumns.insert(columnKey);
                g_treeColumnFoliageLastSeenFrame[columnKey] = g_treeFrameCounter;

                const bool pending = (g_treePendingSections.count(key) > 0)
                    || (g_treePendingDependencies.count(key) > 0);
                const auto appliedIt = g_treeAppliedVersion.find(key);
                const bool appliedCurrent = (appliedIt != g_treeAppliedVersion.end())
                    && (appliedIt->second == section.editVersion);
                const bool readyNow = !pending && appliedCurrent;

                auto readyIt = g_treeColumnFoliageReady.find(columnKey);
                if (readyIt == g_treeColumnFoliageReady.end()) {
                    g_treeColumnFoliageReady.emplace(columnKey, readyNow);
                } else if (readyNow) {
                    // Latch true while column remains in memory to prevent render pop/flicker.
                    readyIt->second = true;
                }
            }

            const uint64_t holdFramesU64 = static_cast<uint64_t>(holdFrames);
            for (auto it = g_treeColumnFoliageReady.begin(); it != g_treeColumnFoliageReady.end(); ) {
                if (seenColumns.count(it->first) > 0) {
                    ++it;
                    continue;
                }
                if (holdFrames <= 0) {
                    g_treeColumnFoliageLastSeenFrame.erase(it->first);
                    it = g_treeColumnFoliageReady.erase(it);
                    continue;
                }
                auto seenIt = g_treeColumnFoliageLastSeenFrame.find(it->first);
                if (seenIt == g_treeColumnFoliageLastSeenFrame.end()
                    || g_treeFrameCounter <= seenIt->second
                    || (g_treeFrameCounter - seenIt->second) <= holdFramesU64) {
                    ++it;
                    continue;
                }
                g_treeColumnFoliageLastSeenFrame.erase(seenIt);
                it = g_treeColumnFoliageReady.erase(it);
            }
        };

        std::string levelKey;
        auto levelIt = baseSystem.registry->find("level");
        if (levelIt != baseSystem.registry->end() && std::holds_alternative<std::string>(levelIt->second)) {
            levelKey = std::get<std::string>(levelIt->second);
        }
        if (g_treeLevelKey != levelKey) {
            g_treeLevelKey = levelKey;
            g_treeAppliedVersion.clear();
            g_treeBackfillVisited.clear();
            g_treePendingDependencies.clear();
            g_treePendingSections.clear();
            g_treeColumnFoliageReady.clear();
            g_treeColumnFoliageLastSeenFrame.clear();
            g_pendingPineLogRemovals.clear();
            g_treeFoliageSignature = 0;
        }
        if (levelKey == "the_depths") {
            return;
        }
        // Streaming unload/reload can recreate a section with the same key and reset editVersion.
        // Prune cache entries for currently unloaded keys so first-pass foliage/tree generation
        // cannot be incorrectly skipped when those sections stream back in.
        for (auto it = g_treeAppliedVersion.begin(); it != g_treeAppliedVersion.end(); ) {
            if (!isFoliageEligibleSection(it->first)) {
                it = g_treeAppliedVersion.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = g_treeBackfillVisited.begin(); it != g_treeBackfillVisited.end(); ) {
            if (!isFoliageEligibleSection(*it)) {
                it = g_treeBackfillVisited.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = g_treePendingDependencies.begin(); it != g_treePendingDependencies.end(); ) {
            if (!isFoliageEligibleSection(*it)) {
                it = g_treePendingDependencies.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = g_treePendingSections.begin(); it != g_treePendingSections.end(); ) {
            if (!isFoliageEligibleSection(*it)) {
                it = g_treePendingSections.erase(it);
            } else {
                ++it;
            }
        }

        const Entity* trunkProtoA = HostLogic::findPrototype("FirLog1Tex", prototypes);
        const Entity* trunkProtoB = HostLogic::findPrototype("FirLog2Tex", prototypes);
        if (!trunkProtoA) trunkProtoA = HostLogic::findPrototype("Branch", prototypes);
        if (!trunkProtoA) trunkProtoA = HostLogic::findPrototype("Block", prototypes);
        if (!trunkProtoB) trunkProtoB = trunkProtoA;
        const Entity* leafProto = HostLogic::findPrototype("Leaf", prototypes);
        const Entity* leafFanOakProto = HostLogic::findPrototype("GrassTuftLeafFanOak", prototypes);
        const Entity* leafFanPineProto = HostLogic::findPrototype("GrassTuftLeafFanPine", prototypes);
        const Entity* grassProto = HostLogic::findPrototype("GrassTuft", prototypes);
        const Entity* shortGrassProto = HostLogic::findPrototype("GrassTuftShort", prototypes);
        const Entity* grassProtoBiome1 = HostLogic::findPrototype("GrassTuftMeadow", prototypes);
        const Entity* shortGrassProtoBiome1 = HostLogic::findPrototype("GrassTuftShortMeadow", prototypes);
        const Entity* grassProtoBiome3 = HostLogic::findPrototype("GrassTuftJungle", prototypes);
        std::array<int, 4> grassPrototypeBiome4IDs{};
        int grassPrototypeBiome4Count = 0;
        {
            const std::array<const char*, 4> kBareGrassNames = {
                "GrassTuftBareV001",
                "GrassTuftBareV002",
                "GrassTuftBareV003",
                "GrassTuftBareV004"
            };
            for (const char* name : kBareGrassNames) {
                if (const Entity* proto = HostLogic::findPrototype(name, prototypes)) {
                    grassPrototypeBiome4IDs[static_cast<size_t>(grassPrototypeBiome4Count)] = proto->prototypeID;
                    grassPrototypeBiome4Count += 1;
                }
            }
        }
        const Entity* flowerProto = HostLogic::findPrototype("Flower", prototypes);
        const Entity* blueRareFlowerProto = HostLogic::findPrototype("FlowerBlueRare", prototypes);
        const Entity* flaxRareFlowerProto = HostLogic::findPrototype("FlowerFlaxRare", prototypes);
        const Entity* hempRareFlowerProto = HostLogic::findPrototype("FlowerHempRare", prototypes);
        const Entity* fernRareFlowerProto = HostLogic::findPrototype("FlowerFernRare", prototypes);
        const Entity* succulentProto = HostLogic::findPrototype("FlowerSucculentV001", prototypes);
        const Entity* succulentProtoV2 = HostLogic::findPrototype("FlowerSucculentV002", prototypes);
        const Entity* jungleOrangeUnderLeafProto = HostLogic::findPrototype("FlowerOrangeUnderLeafV001", prototypes);
        std::array<int, 16> blueFlowerPrototypeIDs{};
        int blueFlowerPrototypeCount = 0;
        {
            const std::array<const char*, 13> kBlueFlowerNames = {
                "FlowerBlueV001",
                "FlowerBlueV002",
                "FlowerBlueV003",
                "FlowerBlueV004",
                "FlowerBlueV005",
                "FlowerBlueV006",
                "FlowerBlueV007",
                "FlowerBlueV008",
                "FlowerLittleBluestemV001",
                "FlowerLittleBluestemV002",
                "FlowerLittleBluestemV003",
                "FlowerLittleBluestemV004",
                "FlowerLittleBluestemV005"
            };
            for (const char* name : kBlueFlowerNames) {
                if (const Entity* proto = HostLogic::findPrototype(name, prototypes)) {
                    blueFlowerPrototypeIDs[static_cast<size_t>(blueFlowerPrototypeCount)] = proto->prototypeID;
                    blueFlowerPrototypeCount += 1;
                }
            }
        }
        std::array<int, kJungleLeafVariantCount> jungleLeafPrototypeIDs{};
        int jungleLeafPrototypeCount = 0;
        {
            const std::array<const char*, kJungleLeafVariantCount> kJungleLeafNames = {
                "LeafJungleV001",
                "LeafJungleV002",
                "LeafJungleV003",
                "LeafJungleV004",
                "LeafJungleV005",
                "LeafJungleV006",
                "LeafJungleV007",
                "LeafJungleV008",
                "LeafJungleV009",
                "LeafJungleV010",
                "LeafJungleV011",
                "LeafJungleV012",
                "LeafJungleV013",
                "LeafJungleV014",
                "LeafJungleV015",
                "LeafJungleV016"
            };
            for (const char* name : kJungleLeafNames) {
                if (const Entity* proto = HostLogic::findPrototype(name, prototypes)) {
                    jungleLeafPrototypeIDs[static_cast<size_t>(jungleLeafPrototypeCount)] = proto->prototypeID;
                    jungleLeafPrototypeCount += 1;
                }
            }
        }
        const Entity* stickProtoX = HostLogic::findPrototype("StickTexX", prototypes);
        const Entity* stickProtoZ = HostLogic::findPrototype("StickTexZ", prototypes);
        const Entity* stickWinterProtoX = HostLogic::findPrototype("StickWinterTexX", prototypes);
        const Entity* stickWinterProtoZ = HostLogic::findPrototype("StickWinterTexZ", prototypes);
        const Entity* grassCoverProtoX = HostLogic::findPrototype("GrassCoverTexX", prototypes);
        const Entity* grassCoverProtoZ = HostLogic::findPrototype("GrassCoverTexZ", prototypes);
        const Entity* grassCoverMeadowProtoX = HostLogic::findPrototype("GrassCoverMeadowTexX", prototypes);
        const Entity* grassCoverMeadowProtoZ = HostLogic::findPrototype("GrassCoverMeadowTexZ", prototypes);
        const Entity* grassCoverForestProtoX = HostLogic::findPrototype("GrassCoverForestTexX", prototypes);
        const Entity* grassCoverForestProtoZ = HostLogic::findPrototype("GrassCoverForestTexZ", prototypes);
        const Entity* grassCoverJungleProtoX = HostLogic::findPrototype("GrassCoverJungleTexX", prototypes);
        const Entity* grassCoverJungleProtoZ = HostLogic::findPrototype("GrassCoverJungleTexZ", prototypes);
        const Entity* grassCoverBareProtoX = HostLogic::findPrototype("GrassCoverBareTexX", prototypes);
        const Entity* grassCoverBareProtoZ = HostLogic::findPrototype("GrassCoverBareTexZ", prototypes);
        const Entity* grassCoverDesertProtoX = HostLogic::findPrototype("GrassCoverDesertTexX", prototypes);
        const Entity* grassCoverDesertProtoZ = HostLogic::findPrototype("GrassCoverDesertTexZ", prototypes);
        const Entity* miniPineBottomProto = HostLogic::findPrototype("GrassTuftMiniPineBottom", prototypes);
        const Entity* miniPineTopProto = HostLogic::findPrototype("GrassTuftMiniPineTop", prototypes);
        const Entity* miniPineTripleBottomProto = HostLogic::findPrototype("GrassTuftMiniPineTripleBottom", prototypes);
        const Entity* miniPineTripleMiddleProto = HostLogic::findPrototype("GrassTuftMiniPineTripleMiddle", prototypes);
        const Entity* miniPineTripleTopProto = HostLogic::findPrototype("GrassTuftMiniPineTripleTop", prototypes);
        const Entity* flaxDoubleBottomProto = HostLogic::findPrototype("GrassTuftFlaxDoubleBottom", prototypes);
        const Entity* flaxDoubleTopProto = HostLogic::findPrototype("GrassTuftFlaxDoubleTop", prototypes);
        const Entity* hempDoubleBottomProto = HostLogic::findPrototype("GrassTuftHempDoubleBottom", prototypes);
        const Entity* hempDoubleTopProto = HostLogic::findPrototype("GrassTuftHempDoubleTop", prototypes);
        const Entity* pebblePatchProtoX = HostLogic::findPrototype("StonePebblePatchTexX", prototypes);
        const Entity* pebblePatchProtoZ = HostLogic::findPrototype("StonePebblePatchTexZ", prototypes);
        const Entity* autumnLeafPatchProtoX = HostLogic::findPrototype("StonePebbleLeafTexX", prototypes);
        const Entity* autumnLeafPatchProtoZ = HostLogic::findPrototype("StonePebbleLeafTexZ", prototypes);
        const Entity* deadLeafPatchProtoX = HostLogic::findPrototype("StonePebbleDeadLeafTexX", prototypes);
        const Entity* deadLeafPatchProtoZ = HostLogic::findPrototype("StonePebbleDeadLeafTexZ", prototypes);
        const Entity* bareBranchProtoX = HostLogic::findPrototype("StonePebbleBareBranchTexX", prototypes);
        const Entity* bareBranchProtoZ = HostLogic::findPrototype("StonePebbleBareBranchTexZ", prototypes);
        const Entity* lilypadPatchProtoX = HostLogic::findPrototype("StonePebbleLilypadTexX", prototypes);
        const Entity* lilypadPatchProtoZ = HostLogic::findPrototype("StonePebbleLilypadTexZ", prototypes);
        const Entity* kelpProto = HostLogic::findPrototype("GrassTuftKelp", prototypes);
        const int kelpTileIndex = kelpProto
            ? RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), *kelpProto, 0)
            : -1;
        const Entity* seaUrchinProtoX = HostLogic::findPrototype("StonePebbleSeaUrchinTexX", prototypes);
        const Entity* seaUrchinProtoZ = HostLogic::findPrototype("StonePebbleSeaUrchinTexZ", prototypes);
        const Entity* sandDollarProtoX = HostLogic::findPrototype("StonePebbleSandDollarTexX", prototypes);
        const Entity* sandDollarProtoZ = HostLogic::findPrototype("StonePebbleSandDollarTexZ", prototypes);
        const Entity* jungleTrunkProto = HostLogic::findPrototype("OakLogTex", prototypes);
        const Entity* bareTrunkProto = HostLogic::findPrototype("BareLogTex", prototypes);
        const Entity* wallBranchLongProtoPosX = HostLogic::findPrototype("WallBranchLongTexPosX", prototypes);
        const Entity* wallBranchLongProtoNegX = HostLogic::findPrototype("WallBranchLongTexNegX", prototypes);
        const Entity* wallBranchLongProtoPosZ = HostLogic::findPrototype("WallBranchLongTexPosZ", prototypes);
        const Entity* wallBranchLongProtoNegZ = HostLogic::findPrototype("WallBranchLongTexNegZ", prototypes);
        const Entity* wallBranchLongTipProtoPosX = HostLogic::findPrototype("WallBranchLongTipTexPosX", prototypes);
        const Entity* wallBranchLongTipProtoNegX = HostLogic::findPrototype("WallBranchLongTipTexNegX", prototypes);
        const Entity* wallBranchLongTipProtoPosZ = HostLogic::findPrototype("WallBranchLongTipTexPosZ", prototypes);
        const Entity* wallBranchLongTipProtoNegZ = HostLogic::findPrototype("WallBranchLongTipTexNegZ", prototypes);
        const Entity* cactusProtoA = HostLogic::findPrototype("Cactus1Tex", prototypes);
        const Entity* cactusProtoB = HostLogic::findPrototype("Cactus2Tex", prototypes);
        const Entity* cactusProtoAX = HostLogic::findPrototype("Cactus1TexX", prototypes);
        const Entity* cactusProtoAZ = HostLogic::findPrototype("Cactus1TexZ", prototypes);
        const Entity* cactusProtoBX = HostLogic::findPrototype("Cactus2TexX", prototypes);
        const Entity* cactusProtoBZ = HostLogic::findPrototype("Cactus2TexZ", prototypes);
        const Entity* cactusProtoAJunctionX = HostLogic::findPrototype("Cactus1TexJunctionX", prototypes);
        const Entity* cactusProtoAJunctionZ = HostLogic::findPrototype("Cactus1TexJunctionZ", prototypes);
        const Entity* cactusProtoBJunctionX = HostLogic::findPrototype("Cactus2TexJunctionX", prototypes);
        const Entity* cactusProtoBJunctionZ = HostLogic::findPrototype("Cactus2TexJunctionZ", prototypes);
        const Entity* stonePebbleProtoX = HostLogic::findPrototype("StonePebbleTexX", prototypes);
        const Entity* stonePebbleProtoZ = HostLogic::findPrototype("StonePebbleTexZ", prototypes);
        const Entity* cavePotProtoX = HostLogic::findPrototype("StonePebbleCavePotTexX", prototypes);
        const Entity* cavePotProtoZ = HostLogic::findPrototype("StonePebbleCavePotTexZ", prototypes);
        const Entity* wallStoneProtoPosX = HostLogic::findPrototype("WallStoneTexPosX", prototypes);
        const Entity* wallStoneProtoNegX = HostLogic::findPrototype("WallStoneTexNegX", prototypes);
        const Entity* wallStoneProtoPosZ = HostLogic::findPrototype("WallStoneTexPosZ", prototypes);
        const Entity* wallStoneProtoNegZ = HostLogic::findPrototype("WallStoneTexNegZ", prototypes);
        const Entity* ceilingStoneProtoPosX = HostLogic::findPrototype("CeilingStoneTexPosX", prototypes);
        const Entity* ceilingStoneProtoNegX = HostLogic::findPrototype("CeilingStoneTexNegX", prototypes);
        const Entity* ceilingStoneProtoPosZ = HostLogic::findPrototype("CeilingStoneTexPosZ", prototypes);
        const Entity* ceilingStoneProtoNegZ = HostLogic::findPrototype("CeilingStoneTexNegZ", prototypes);
        const Entity* ceilingStoneProtoX = HostLogic::findPrototype("CeilingStoneTexX", prototypes);
        const Entity* ceilingStoneProtoZ = HostLogic::findPrototype("CeilingStoneTexZ", prototypes);
        const Entity* slopeProtoPosX = HostLogic::findPrototype("DebugSlopeTexPosX", prototypes);
        const Entity* slopeProtoNegX = HostLogic::findPrototype("DebugSlopeTexNegX", prototypes);
        const Entity* slopeProtoPosZ = HostLogic::findPrototype("DebugSlopeTexPosZ", prototypes);
        const Entity* slopeProtoNegZ = HostLogic::findPrototype("DebugSlopeTexNegZ", prototypes);
        const Entity* waterProto = HostLogic::findPrototype("Water", prototypes);
        if (!trunkProtoA || !trunkProtoB || !leafProto) return;
        if (!grassProtoBiome1) grassProtoBiome1 = grassProto;
        if (!shortGrassProtoBiome1) shortGrassProtoBiome1 = shortGrassProto;
        if (!grassProtoBiome3) grassProtoBiome3 = grassProto;
        if (grassPrototypeBiome4Count <= 0 && grassProtoBiome3) {
            grassPrototypeBiome4IDs[0] = grassProtoBiome3->prototypeID;
            grassPrototypeBiome4Count = 1;
        }
        if (!grassCoverMeadowProtoX) grassCoverMeadowProtoX = grassCoverProtoX;
        if (!grassCoverMeadowProtoZ) grassCoverMeadowProtoZ = grassCoverProtoZ;
        if (!grassCoverForestProtoX) grassCoverForestProtoX = grassCoverProtoX;
        if (!grassCoverForestProtoZ) grassCoverForestProtoZ = grassCoverProtoZ;
        if (!grassCoverJungleProtoX) grassCoverJungleProtoX = grassCoverProtoX;
        if (!grassCoverJungleProtoZ) grassCoverJungleProtoZ = grassCoverProtoZ;
        if (!grassCoverBareProtoX) grassCoverBareProtoX = grassCoverForestProtoX;
        if (!grassCoverBareProtoZ) grassCoverBareProtoZ = grassCoverForestProtoZ;
        if (!grassCoverDesertProtoX) grassCoverDesertProtoX = grassCoverProtoX;
        if (!grassCoverDesertProtoZ) grassCoverDesertProtoZ = grassCoverProtoZ;
        if (!deadLeafPatchProtoX) deadLeafPatchProtoX = autumnLeafPatchProtoX;
        if (!deadLeafPatchProtoZ) deadLeafPatchProtoZ = autumnLeafPatchProtoZ;
        if (!bareBranchProtoX) bareBranchProtoX = stickWinterProtoX ? stickWinterProtoX : stickProtoX;
        if (!bareBranchProtoZ) bareBranchProtoZ = stickWinterProtoZ ? stickWinterProtoZ : stickProtoZ;

        // Prototype colors from temp/prismals_game.cpp:
        // trunk = blockColors[2], pine leaf = blockColors[3]
        const uint32_t trunkColor = packColor(glm::vec3(0.29f, 0.21f, 0.13f));
        const uint32_t leafColor = packColor(glm::vec3(0.07f, 0.46f, 0.34f));
        const PineSpec baseSpec;
        const int pineMinTrunkHeight = std::max(6, getRegistryInt(baseSystem, "PineTrunkHeightMin", 15));
        const int pineMaxTrunkHeight = std::max(
            pineMinTrunkHeight,
            getRegistryInt(baseSystem, "PineTrunkHeightMax", baseSpec.trunkHeight)
        );
        const int jungleTreeSpawnModulo = std::max(1, getRegistryInt(baseSystem, "JungleTreeSpawnModulo", 1000));
        const int jungleTreeTrunkMin = std::max(4, getRegistryInt(baseSystem, "JungleTreeTrunkHeightMin", 6));
        const int jungleTreeTrunkMax = std::max(jungleTreeTrunkMin, getRegistryInt(baseSystem, "JungleTreeTrunkHeightMax", 9));
        const int jungleTreeCanopyRadius = std::max(2, std::min(8, getRegistryInt(baseSystem, "JungleTreeCanopyRadius", 4)));
        const int bareTreeSpawnModulo = std::max(1, getRegistryInt(baseSystem, "BareTreeSpawnModulo", 380));
        const int bareTreeTrunkMin = std::max(4, getRegistryInt(baseSystem, "BareTreeTrunkHeightMin", 7));
        const int bareTreeTrunkMax = std::max(bareTreeTrunkMin, getRegistryInt(baseSystem, "BareTreeTrunkHeightMax", 12));
        const int desertCactusSpawnModulo = std::max(1, getRegistryInt(baseSystem, "DesertCactusSpawnModulo", 128));
        const bool desertCactusEnabled = getRegistryBool(baseSystem, "DesertCactusGenerationEnabled", true);
        FoliageSpec foliageSpec;
        foliageSpec.enabled = getRegistryBool(baseSystem, "FoliageGenerationEnabled", true);
        foliageSpec.grassEnabled = getRegistryBool(baseSystem, "GrassGenerationEnabled", true);
        foliageSpec.grassCoverEnabled = getRegistryBool(baseSystem, "GrassCoverGenerationEnabled", true);
        foliageSpec.pebblePatchEnabled = getRegistryBool(baseSystem, "PebblePatchGenerationEnabled", true);
        foliageSpec.autumnLeafPatchEnabled = getRegistryBool(baseSystem, "AutumnLeafPatchGenerationEnabled", true);
        foliageSpec.lilypadPatchEnabled = getRegistryBool(baseSystem, "LilypadPatchGenerationEnabled", true);
        foliageSpec.flowerEnabled = getRegistryBool(baseSystem, "FlowerGenerationEnabled", true);
        foliageSpec.stickEnabled = getRegistryBool(baseSystem, "StickGenerationEnabled", true);
        foliageSpec.waterFoliageEnabled = getRegistryBool(baseSystem, "WaterFoliageGenerationEnabled", true);
        foliageSpec.caveStoneEnabled = getRegistryBool(baseSystem, "CaveStoneGenerationEnabled", true);
        foliageSpec.caveSlopeEnabled = getRegistryBool(baseSystem, "CaveSlopeGenerationEnabled", true);
        foliageSpec.caveWallStoneEnabled = getRegistryBool(baseSystem, "CaveWallStoneGenerationEnabled", true);
        foliageSpec.caveCeilingStoneEnabled = getRegistryBool(baseSystem, "CaveCeilingStoneGenerationEnabled", true);
        foliageSpec.temperateOnly = getRegistryBool(baseSystem, "FoliageTemperateOnly", true);
        foliageSpec.grassSpawnModulo = std::max(1, getRegistryInt(baseSystem, "GrassSpawnModulo", foliageSpec.grassSpawnModulo));
        foliageSpec.grassCoverPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "GrassCoverPercent", foliageSpec.grassCoverPercent)));
        foliageSpec.pebblePatchPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "PebblePatchPercent", foliageSpec.pebblePatchPercent)));
        foliageSpec.pebblePatchNearWaterRadius = std::max(1, std::min(64, getRegistryInt(baseSystem, "PebblePatchNearWaterRadius", foliageSpec.pebblePatchNearWaterRadius)));
        foliageSpec.pebblePatchNearWaterVerticalRange = std::max(0, std::min(32, getRegistryInt(baseSystem, "PebblePatchNearWaterVerticalRange", foliageSpec.pebblePatchNearWaterVerticalRange)));
        foliageSpec.autumnLeafPatchPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "AutumnLeafPatchPercent", foliageSpec.autumnLeafPatchPercent)));
        foliageSpec.lilypadPatchPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "LilypadPatchPercent", foliageSpec.lilypadPatchPercent)));
        foliageSpec.flowerSpawnModulo = std::max(1, getRegistryInt(baseSystem, "FlowerSpawnModulo", foliageSpec.flowerSpawnModulo));
        foliageSpec.miniPineSpawnModulo = std::max(1, getRegistryInt(baseSystem, "MiniPineSpawnModulo", foliageSpec.miniPineSpawnModulo));
        foliageSpec.shortGrassPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "ShortGrassPercent", foliageSpec.shortGrassPercent)));
        foliageSpec.grassTuftPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "GrassTuftPercent", foliageSpec.grassTuftPercent)));
        foliageSpec.stickSpawnPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "StickSpawnPercent", foliageSpec.stickSpawnPercent)));
        foliageSpec.stickCanopySearchRadius = std::max(0, std::min(4, getRegistryInt(baseSystem, "StickCanopySearchRadius", foliageSpec.stickCanopySearchRadius)));
        foliageSpec.stickCanopySearchHeight = std::max(2, std::min(96, getRegistryInt(baseSystem, "StickCanopySearchHeight", foliageSpec.stickCanopySearchHeight)));
        foliageSpec.kelpSpawnPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "KelpSpawnPercent", foliageSpec.kelpSpawnPercent)));
        foliageSpec.seaUrchinSpawnPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "SeaUrchinSpawnPercent", foliageSpec.seaUrchinSpawnPercent)));
        foliageSpec.sandDollarSpawnPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "SandDollarSpawnPercent", foliageSpec.sandDollarSpawnPercent)));
        foliageSpec.caveStoneSpawnPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "CaveStoneSpawnPercent", foliageSpec.caveStoneSpawnPercent)));
        foliageSpec.caveStoneMinDepthFromSurface = std::max(1, std::min(256, getRegistryInt(baseSystem, "CaveStoneMinDepthFromSurface", foliageSpec.caveStoneMinDepthFromSurface)));
        foliageSpec.cavePotEnabled = getRegistryBool(baseSystem, "CavePotGenerationEnabled", true);
        foliageSpec.cavePotSpawnPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "CavePotSpawnPercent", foliageSpec.cavePotSpawnPercent)));
        foliageSpec.cavePotMinDepthFromSurface = std::max(1, std::min(256, getRegistryInt(baseSystem, "CavePotMinDepthFromSurface", foliageSpec.cavePotMinDepthFromSurface)));
        foliageSpec.cavePotPileMaxCount = std::max(1, std::min(3, getRegistryInt(baseSystem, "CavePotPileMaxCount", foliageSpec.cavePotPileMaxCount)));
        foliageSpec.caveSlopeSpawnPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "CaveSlopeSpawnPercent", foliageSpec.caveSlopeSpawnPercent)));
        foliageSpec.caveSlopeMinDepthFromSurface = std::max(1, std::min(256, getRegistryInt(baseSystem, "CaveSlopeMinDepthFromSurface", foliageSpec.caveSlopeMinDepthFromSurface)));
        foliageSpec.caveSlopeLargePercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "CaveSlopeLargePercent", foliageSpec.caveSlopeLargePercent)));
        foliageSpec.caveSlopeHugePercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "CaveSlopeHugePercent", foliageSpec.caveSlopeHugePercent)));
        foliageSpec.caveWallStoneSpawnPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "CaveWallStoneSpawnPercent", foliageSpec.caveWallStoneSpawnPercent)));
        foliageSpec.caveWallStoneMinDepthFromSurface = std::max(1, std::min(256, getRegistryInt(baseSystem, "CaveWallStoneMinDepthFromSurface", foliageSpec.caveWallStoneMinDepthFromSurface)));
        foliageSpec.caveCeilingStoneSpawnPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "CaveCeilingStoneSpawnPercent", foliageSpec.caveCeilingStoneSpawnPercent)));
        foliageSpec.caveCeilingStoneMinDepthFromSurface = std::max(1, std::min(256, getRegistryInt(baseSystem, "CaveCeilingStoneMinDepthFromSurface", foliageSpec.caveCeilingStoneMinDepthFromSurface)));
        if (!grassProto
            && !shortGrassProto
            && !grassProtoBiome1
            && !shortGrassProtoBiome1
            && !grassProtoBiome3
            && grassPrototypeBiome4Count <= 0) {
            foliageSpec.grassEnabled = false;
        }
        if (!grassCoverProtoX && !grassCoverProtoZ
            && !grassCoverMeadowProtoX && !grassCoverMeadowProtoZ
            && !grassCoverForestProtoX && !grassCoverForestProtoZ
            && !grassCoverJungleProtoX && !grassCoverJungleProtoZ
            && !grassCoverBareProtoX && !grassCoverBareProtoZ
            && !grassCoverDesertProtoX && !grassCoverDesertProtoZ) {
            foliageSpec.grassCoverEnabled = false;
            foliageSpec.pebblePatchEnabled = false;
        }
        if (!pebblePatchProtoX && !pebblePatchProtoZ) {
            foliageSpec.pebblePatchEnabled = false;
        }
        if (!autumnLeafPatchProtoX && !autumnLeafPatchProtoZ
            && !deadLeafPatchProtoX && !deadLeafPatchProtoZ) {
            foliageSpec.autumnLeafPatchEnabled = false;
        }
        if (!lilypadPatchProtoX && !lilypadPatchProtoZ) {
            foliageSpec.lilypadPatchEnabled = false;
        }
        if (!flowerProto
            && blueFlowerPrototypeCount <= 0
            && !succulentProto
            && !succulentProtoV2
            && !jungleOrangeUnderLeafProto) {
            foliageSpec.flowerEnabled = false;
        }
        if (!stickProtoX && !stickProtoZ && !stickWinterProtoX && !stickWinterProtoZ) foliageSpec.stickEnabled = false;
        if (!kelpProto && !seaUrchinProtoX && !seaUrchinProtoZ && !sandDollarProtoX && !sandDollarProtoZ) foliageSpec.waterFoliageEnabled = false;
        if (!stonePebbleProtoX && !stonePebbleProtoZ) foliageSpec.caveStoneEnabled = false;
        if (!cavePotProtoX && !cavePotProtoZ) foliageSpec.cavePotEnabled = false;
        if (!slopeProtoPosX && !slopeProtoNegX && !slopeProtoPosZ && !slopeProtoNegZ) foliageSpec.caveSlopeEnabled = false;
        if (!wallStoneProtoPosX && !wallStoneProtoNegX && !wallStoneProtoPosZ && !wallStoneProtoNegZ) foliageSpec.caveWallStoneEnabled = false;
        if (!ceilingStoneProtoPosX
            && !ceilingStoneProtoNegX
            && !ceilingStoneProtoPosZ
            && !ceilingStoneProtoNegZ
            && !ceilingStoneProtoX
            && !ceilingStoneProtoZ) {
            foliageSpec.caveCeilingStoneEnabled = false;
        }
        if (!foliageSpec.grassEnabled
            && !foliageSpec.grassCoverEnabled
            && !foliageSpec.flowerEnabled
            && !foliageSpec.waterFoliageEnabled
            && !foliageSpec.caveStoneEnabled
            && !foliageSpec.cavePotEnabled
            && !foliageSpec.caveSlopeEnabled
            && !foliageSpec.caveWallStoneEnabled
            && !foliageSpec.caveCeilingStoneEnabled) {
            foliageSpec.enabled = false;
        }
        const int waterPrototypeID = waterProto ? waterProto->prototypeID : -1;

        // Re-run first-pass foliage when generation rules/prototypes change.
        uint64_t foliageSignature = 1469598103934665603ull;
        auto mixSig = [&](uint64_t v) {
            foliageSignature ^= v;
            foliageSignature *= 1099511628211ull;
        };
        mixSig(0xCAFEA16u); // include winter bare biome foliage/tree prototype revisions
        mixSig(static_cast<uint64_t>(foliageSpec.cavePotEnabled ? 1u : 0u));
        mixSig(static_cast<uint64_t>(foliageSpec.cavePotSpawnPercent));
        mixSig(static_cast<uint64_t>(foliageSpec.cavePotMinDepthFromSurface));
        mixSig(static_cast<uint64_t>(foliageSpec.cavePotPileMaxCount));
        mixSig(static_cast<uint64_t>(cavePotProtoX ? std::max(0, cavePotProtoX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(cavePotProtoZ ? std::max(0, cavePotProtoZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(foliageSpec.caveWallStoneEnabled ? 1u : 0u));
        mixSig(static_cast<uint64_t>(foliageSpec.caveWallStoneSpawnPercent));
        mixSig(static_cast<uint64_t>(foliageSpec.caveWallStoneMinDepthFromSurface));
        mixSig(static_cast<uint64_t>(wallStoneProtoPosX ? std::max(0, wallStoneProtoPosX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallStoneProtoNegX ? std::max(0, wallStoneProtoNegX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallStoneProtoPosZ ? std::max(0, wallStoneProtoPosZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallStoneProtoNegZ ? std::max(0, wallStoneProtoNegZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(foliageSpec.caveCeilingStoneEnabled ? 1u : 0u));
        mixSig(static_cast<uint64_t>(foliageSpec.caveCeilingStoneSpawnPercent));
        mixSig(static_cast<uint64_t>(foliageSpec.caveCeilingStoneMinDepthFromSurface));
        mixSig(static_cast<uint64_t>(ceilingStoneProtoPosX ? std::max(0, ceilingStoneProtoPosX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(ceilingStoneProtoNegX ? std::max(0, ceilingStoneProtoNegX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(ceilingStoneProtoPosZ ? std::max(0, ceilingStoneProtoPosZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(ceilingStoneProtoNegZ ? std::max(0, ceilingStoneProtoNegZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(ceilingStoneProtoX ? std::max(0, ceilingStoneProtoX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(ceilingStoneProtoZ ? std::max(0, ceilingStoneProtoZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(foliageSpec.waterFoliageEnabled ? 1u : 0u));
        mixSig(static_cast<uint64_t>(foliageSpec.kelpSpawnPercent));
        mixSig(static_cast<uint64_t>(foliageSpec.seaUrchinSpawnPercent));
        mixSig(static_cast<uint64_t>(foliageSpec.sandDollarSpawnPercent));
        mixSig(static_cast<uint64_t>(kelpProto ? std::max(0, kelpProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(seaUrchinProtoX ? std::max(0, seaUrchinProtoX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(seaUrchinProtoZ ? std::max(0, seaUrchinProtoZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(sandDollarProtoX ? std::max(0, sandDollarProtoX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(sandDollarProtoZ ? std::max(0, sandDollarProtoZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(miniPineBottomProto ? std::max(0, miniPineBottomProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(miniPineTopProto ? std::max(0, miniPineTopProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(miniPineTripleBottomProto ? std::max(0, miniPineTripleBottomProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(miniPineTripleMiddleProto ? std::max(0, miniPineTripleMiddleProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(miniPineTripleTopProto ? std::max(0, miniPineTripleTopProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(jungleOrangeUnderLeafProto ? std::max(0, jungleOrangeUnderLeafProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(grassPrototypeBiome4Count));
        for (int i = 0; i < grassPrototypeBiome4Count; ++i) {
            mixSig(static_cast<uint64_t>(std::max(0, grassPrototypeBiome4IDs[static_cast<size_t>(i)])));
        }
        mixSig(static_cast<uint64_t>(grassCoverBareProtoX ? std::max(0, grassCoverBareProtoX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(grassCoverBareProtoZ ? std::max(0, grassCoverBareProtoZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(deadLeafPatchProtoX ? std::max(0, deadLeafPatchProtoX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(deadLeafPatchProtoZ ? std::max(0, deadLeafPatchProtoZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(bareBranchProtoX ? std::max(0, bareBranchProtoX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(bareBranchProtoZ ? std::max(0, bareBranchProtoZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(stickWinterProtoX ? std::max(0, stickWinterProtoX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(stickWinterProtoZ ? std::max(0, stickWinterProtoZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(bareTrunkProto ? std::max(0, bareTrunkProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallBranchLongProtoPosX ? std::max(0, wallBranchLongProtoPosX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallBranchLongProtoNegX ? std::max(0, wallBranchLongProtoNegX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallBranchLongProtoPosZ ? std::max(0, wallBranchLongProtoPosZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallBranchLongProtoNegZ ? std::max(0, wallBranchLongProtoNegZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallBranchLongTipProtoPosX ? std::max(0, wallBranchLongTipProtoPosX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallBranchLongTipProtoNegX ? std::max(0, wallBranchLongTipProtoNegX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallBranchLongTipProtoPosZ ? std::max(0, wallBranchLongTipProtoPosZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallBranchLongTipProtoNegZ ? std::max(0, wallBranchLongTipProtoNegZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(flaxDoubleBottomProto ? std::max(0, flaxDoubleBottomProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(flaxDoubleTopProto ? std::max(0, flaxDoubleTopProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(hempDoubleBottomProto ? std::max(0, hempDoubleBottomProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(hempDoubleTopProto ? std::max(0, hempDoubleTopProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(leafFanOakProto ? std::max(0, leafFanOakProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(leafFanPineProto ? std::max(0, leafFanPineProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(succulentProto ? std::max(0, succulentProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(succulentProtoV2 ? std::max(0, succulentProtoV2->prototypeID) : 0));
        if (g_treeFoliageSignature != foliageSignature) {
            g_treeFoliageSignature = foliageSignature;
            g_treeAppliedVersion.clear();
            g_treeBackfillVisited.clear();
            g_treePendingDependencies.clear();
            g_treePendingSections.clear();
            g_treeColumnFoliageReady.clear();
            g_treeColumnFoliageLastSeenFrame.clear();
        }

        if (!g_pendingPineLogRemovals.empty()) {
            std::vector<std::pair<glm::ivec3, int>> pending;
            pending.swap(g_pendingPineLogRemovals);
            std::vector<std::pair<glm::ivec3, int>> deferred;
            deferred.reserve(pending.size());
            std::unordered_set<glm::ivec3, IVec3Hash> seenCells;
            int processed = 0;
            constexpr int kMaxFallsPerFrame = 2;
            for (const auto& event : pending) {
                if (!isPineVerticalLogPrototypeID(prototypes, event.second)) continue;
                if (seenCells.count(event.first) > 0) continue;
                seenCells.insert(event.first);
                if (processed >= kMaxFallsPerFrame) {
                    deferred.push_back(event);
                    continue;
                }
                processSingleTreeFall(baseSystem, prototypes, event.first);
                processed += 1;
            }
            if (!deferred.empty()) {
                g_pendingPineLogRemovals.insert(g_pendingPineLogRemovals.end(), deferred.begin(), deferred.end());
            }
        }

        for (const auto& key : voxelWorld.dirtySections) {
            if (isFoliageEligibleSection(key)) {
                g_treePendingSections.insert(key);
            } else {
                g_treePendingSections.erase(key);
                g_treePendingDependencies.erase(key);
                g_treeBackfillVisited.erase(key);
            }
        }

        std::vector<VoxelSectionKey> dirtySections;
        dirtySections.reserve(g_treePendingSections.size() + voxelWorld.dirtySections.size());
        std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> selected;
        for (auto it = g_treePendingSections.begin(); it != g_treePendingSections.end(); ) {
            const VoxelSectionKey key = *it;
            if (!isFoliageEligibleSection(key)) {
                g_treePendingDependencies.erase(key);
                g_treeBackfillVisited.erase(key);
                it = g_treePendingSections.erase(it);
                continue;
            }
            if (!TerrainSystemLogic::IsSectionTerrainReady(key)) {
                ++it;
                continue;
            }
            dirtySections.push_back(key);
            selected.insert(key);
            ++it;
        }

        const bool backfillLoaded = getRegistryBool(baseSystem, "TreeFoliageBackfillAllLoaded", true);
        int backfillBudget = std::max(1, getRegistryInt(baseSystem, "TreeFoliageBackfillSectionsPerFrame", 12));
        const int backfillIntervalFrames = std::max(1, getRegistryInt(baseSystem, "TreeFoliageBackfillIntervalFrames", 4));
        const float backfillMaxDistance = std::max(0.0f, getRegistryFloat(baseSystem, "TreeFoliageBackfillMaxDistance", 0.0f));
        const float backfillMaxDistanceSq = backfillMaxDistance > 0.0f ? backfillMaxDistance * backfillMaxDistance : 0.0f;
        size_t terrainPending = 0, terrainDesired = 0, terrainGenerated = 0, terrainJobs = 0;
        int terrainStepped = 0, terrainBuilt = 0, terrainConsumed = 0, terrainSkipped = 0, terrainFiltered = 0;
        int terrainRescueSurface = 0, terrainRescueMissing = 0, terrainCapDrop = 0, terrainReprio = 0;
        float terrainGenMs = 0.0f;
        TerrainSystemLogic::GetVoxelStreamingPerfStats(
            terrainPending,
            terrainDesired,
            terrainGenerated,
            terrainJobs,
            terrainStepped,
            terrainBuilt,
            terrainConsumed,
            terrainSkipped,
            terrainFiltered,
            terrainRescueSurface,
            terrainRescueMissing,
            terrainCapDrop,
            terrainReprio,
            terrainGenMs
        );
        (void)terrainDesired;
        (void)terrainGenerated;
        (void)terrainStepped;
        (void)terrainBuilt;
        (void)terrainConsumed;
        (void)terrainSkipped;
        (void)terrainFiltered;
        (void)terrainRescueSurface;
        (void)terrainRescueMissing;
        (void)terrainCapDrop;
        (void)terrainReprio;
        (void)terrainGenMs;
        const bool throttleByTerrainBacklog = getRegistryBool(baseSystem, "TreeFoliageThrottleByTerrainBacklog", true);
        const int terrainPendingThreshold = std::max(0, getRegistryInt(baseSystem, "TreeFoliageTerrainPendingThreshold", 4096));
        const int terrainJobsThreshold = std::max(0, getRegistryInt(baseSystem, "TreeFoliageTerrainJobsThreshold", 24));
        const bool terrainBacklogged = throttleByTerrainBacklog
            && ((terrainPendingThreshold > 0 && static_cast<int>(terrainPending) >= terrainPendingThreshold)
                || (terrainJobsThreshold > 0 && static_cast<int>(terrainJobs) >= terrainJobsThreshold));
        const bool skipCaveDecorWhenBacklogged = terrainBacklogged
            && getRegistryBool(baseSystem, "TreeFoliageSkipCaveDecorWhenBacklogged", true);
        const int caveDecorMaxLod = std::max(0, getRegistryInt(baseSystem, "TreeFoliageCaveDecorMaxLod", 0));
        const int throttledBackfillBudget = std::max(1, getRegistryInt(baseSystem, "TreeFoliageBackfillSectionsPerFrameBacklogged", 1));
        if (terrainBacklogged) {
            backfillBudget = std::min(backfillBudget, throttledBackfillBudget);
        }
        bool runBackfillThisFrame = backfillLoaded
            && baseSystem.player
            && ((g_treeFrameCounter % static_cast<uint64_t>(backfillIntervalFrames)) == 0u
                || !g_treePendingDependencies.empty());
        if (terrainBacklogged && getRegistryBool(baseSystem, "TreeFoliageSkipBackfillWhenBacklogged", true)
            && g_treePendingDependencies.empty()) {
            runBackfillThisFrame = false;
        }
        int backfillAppended = 0;
        bool backfillRan = false;
        int cameraSurfaceWorldY = 0;
        if (baseSystem.player) {
            float cameraSurface = 0.0f;
            const bool cameraOnLand = ExpanseBiomeSystemLogic::SampleTerrain(
                worldCtx,
                baseSystem.player->cameraPosition.x,
                baseSystem.player->cameraPosition.z,
                cameraSurface
            );
            cameraSurfaceWorldY = cameraOnLand
                ? (static_cast<int>(std::floor(cameraSurface)) + 1)
                : (static_cast<int>(std::floor(worldCtx.expanse.waterSurface)) + 1);
        }
        if (runBackfillThisFrame) {
            backfillRan = true;
            struct BackfillCandidate {
                VoxelSectionKey key;
                float dist2 = 0.0f;
                int yDistToSurface = 0;
                bool pendingDependencies = false;
            };
            std::vector<BackfillCandidate> candidates;
            candidates.reserve(voxelWorld.sections.size());
            const glm::vec3 camPos = baseSystem.player->cameraPosition;
            for (const auto& [key, section] : voxelWorld.sections) {
                if (selected.count(key) > 0) continue;
                if (!isFoliageEligibleSection(key)) continue;
                if (!TerrainSystemLogic::IsSectionTerrainReady(key)) continue;
                const bool waitingDependencies = g_treePendingDependencies.count(key) > 0;
                if (!waitingDependencies && g_treeBackfillVisited.count(key) > 0) continue;
                const int sectionScale = sectionScaleForLod(key.lod);
                const float worldSectionSpan = static_cast<float>(section.size * sectionScale);
                float centerX = (static_cast<float>(key.coord.x) + 0.5f) * worldSectionSpan;
                float centerZ = (static_cast<float>(key.coord.z) + 0.5f) * worldSectionSpan;
                float dx = centerX - camPos.x;
                float dz = centerZ - camPos.z;
                float dist2 = dx * dx + dz * dz;
                if (backfillMaxDistanceSq > 0.0f && dist2 > backfillMaxDistanceSq) continue;
                const int cameraSurfaceLodY = floorDivInt(cameraSurfaceWorldY, sectionScale);
                const int cameraSurfaceSectionY = floorDivInt(cameraSurfaceLodY, section.size);
                candidates.push_back({
                    key,
                    dist2,
                    std::abs(key.coord.y - cameraSurfaceSectionY),
                    waitingDependencies
                });
            }
            std::sort(candidates.begin(), candidates.end(), [](const BackfillCandidate& a, const BackfillCandidate& b) {
                if (a.pendingDependencies != b.pendingDependencies) return a.pendingDependencies > b.pendingDependencies;
                if (a.key.lod != b.key.lod) return a.key.lod < b.key.lod;
                if (a.dist2 != b.dist2) return a.dist2 < b.dist2;
                if (a.yDistToSurface != b.yDistToSurface) return a.yDistToSurface < b.yDistToSurface;
                if (a.key.coord.x != b.key.coord.x) return a.key.coord.x < b.key.coord.x;
                if (a.key.coord.y != b.key.coord.y) return a.key.coord.y < b.key.coord.y;
                return a.key.coord.z < b.key.coord.z;
            });
            int appended = 0;
            for (const auto& candidate : candidates) {
                if (appended >= backfillBudget) break;
                dirtySections.push_back(candidate.key);
                selected.insert(candidate.key);
                appended += 1;
            }
            backfillAppended = appended;
        }
        if (dirtySections.empty()) {
            rebuildColumnFoliageReady();
            g_treePerfStats.pendingSections = g_treePendingSections.size();
            g_treePerfStats.pendingDependencies = g_treePendingDependencies.size();
            g_treePerfStats.backfillVisited = g_treeBackfillVisited.size();
            g_treePerfStats.selectedSections = 0;
            g_treePerfStats.processedSections = 0;
            g_treePerfStats.deferredByTimeBudget = 0;
            g_treePerfStats.backfillAppended = backfillAppended;
            g_treePerfStats.backfillRan = backfillRan;
            g_treePerfStats.updateMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - updateStart
            ).count();
            return;
        }
        {
            int sectionBudget = std::max(1, getRegistryInt(baseSystem, "TreeFoliageSectionsPerFrame", 4));
            if (terrainBacklogged) {
                const int throttledBudget = std::max(1, getRegistryInt(baseSystem, "TreeFoliageSectionsPerFrameBacklogged", 1));
                sectionBudget = std::min(sectionBudget, throttledBudget);
            }
            if (static_cast<int>(dirtySections.size()) > sectionBudget && baseSystem.player) {
                const glm::vec3 camPos = baseSystem.player->cameraPosition;
                auto dist2ToCam = [&](const VoxelSectionKey& k) {
                    auto it = voxelWorld.sections.find(k);
                    if (it == voxelWorld.sections.end()) return std::numeric_limits<float>::max();
                    const int sectionScale = sectionScaleForLod(k.lod);
                    const float worldSectionSpan = static_cast<float>(it->second.size * sectionScale);
                    const float cx = (static_cast<float>(k.coord.x) + 0.5f) * worldSectionSpan;
                    const float cz = (static_cast<float>(k.coord.z) + 0.5f) * worldSectionSpan;
                    const float dx = cx - camPos.x;
                    const float dz = cz - camPos.z;
                    return dx * dx + dz * dz;
                };
                auto yDistToSurface = [&](const VoxelSectionKey& k) {
                    auto it = voxelWorld.sections.find(k);
                    if (it == voxelWorld.sections.end()) return std::numeric_limits<int>::max();
                    const int sectionScale = sectionScaleForLod(k.lod);
                    const int cameraSurfaceLodY = floorDivInt(cameraSurfaceWorldY, sectionScale);
                    const int cameraSurfaceSectionY = floorDivInt(cameraSurfaceLodY, it->second.size);
                    return std::abs(k.coord.y - cameraSurfaceSectionY);
                };
                std::nth_element(
                    dirtySections.begin(),
                    dirtySections.begin() + sectionBudget,
                    dirtySections.end(),
                    [&](const VoxelSectionKey& a, const VoxelSectionKey& b) {
                        if (a.lod != b.lod) return a.lod < b.lod;
                        const float ad2 = dist2ToCam(a);
                        const float bd2 = dist2ToCam(b);
                        if (ad2 != bd2) return ad2 < bd2;
                        const int ay = yDistToSurface(a);
                        const int by = yDistToSurface(b);
                        if (ay != by) return ay < by;
                        if (a.coord.x != b.coord.x) return a.coord.x < b.coord.x;
                        if (a.coord.y != b.coord.y) return a.coord.y < b.coord.y;
                        return a.coord.z < b.coord.z;
                    }
                );
                dirtySections.resize(static_cast<size_t>(sectionBudget));
            } else if (static_cast<int>(dirtySections.size()) > sectionBudget) {
                dirtySections.resize(static_cast<size_t>(sectionBudget));
            }
        }

        const float foliageTimeBudgetMs = std::max(0.0f, getRegistryFloat(baseSystem, "TreeFoliageMaxMsPerFrame", 0.0f));
        const int foliageMinSectionsBeforeTimeCap = std::max(0, getRegistryInt(baseSystem, "TreeFoliageMinSectionsPerFrame", 1));
        int processedSections = 0;
        int deferredByTimeBudget = 0;
        for (size_t keyIndex = 0; keyIndex < dirtySections.size(); ++keyIndex) {
            if (foliageTimeBudgetMs > 0.0f && processedSections >= foliageMinSectionsBeforeTimeCap) {
                float elapsedMs = std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - updateStart
                ).count();
                if (elapsedMs >= foliageTimeBudgetMs) {
                    for (size_t deferredIndex = keyIndex; deferredIndex < dirtySections.size(); ++deferredIndex) {
                        g_treePendingSections.insert(dirtySections[deferredIndex]);
                        deferredByTimeBudget += 1;
                    }
                    break;
                }
            }
            const auto& key = dirtySections[keyIndex];
            if (!isFoliageEligibleSection(key)) {
                g_treePendingSections.erase(key);
                g_treePendingDependencies.erase(key);
                g_treeBackfillVisited.erase(key);
                continue;
            }
            processedSections += 1;
            if (!TerrainSystemLogic::IsSectionTerrainReady(key)) {
                g_treePendingSections.insert(key);
                continue;
            }
            const bool queuedFromDirty = g_treePendingSections.count(key) > 0;
            const bool forceBackfill = backfillLoaded && !queuedFromDirty;
            auto sectionIt = voxelWorld.sections.find(key);
            if (sectionIt == voxelWorld.sections.end()) continue;
            const int sectionLod = sectionIt->second.lod;
            const int sectionScale = sectionScaleForLod(sectionLod);

            auto appliedIt = g_treeAppliedVersion.find(key);
            const bool waitingDependencies = g_treePendingDependencies.count(key) > 0;
            // Pending sections should retry when they are explicitly dirtied by dependency updates.
            // Do not force full retries just because backfill selected them.
            const bool retryPendingNow = waitingDependencies && queuedFromDirty;
            if (!forceBackfill
                && !retryPendingNow
                && appliedIt != g_treeAppliedVersion.end()
                && appliedIt->second == sectionIt->second.editVersion) {
                if (queuedFromDirty) {
                    g_treePendingSections.erase(key);
                }
                continue;
            }

            // Once a section has already been processed by this system, treat any newer
            // editVersion as user/gameplay edits and do not auto-regrow trees/foliage into it.
            // First-time sections are still allowed even if editVersion > 1, because
            // neighboring tree writes can bump version before the section's first pass.
            if (!forceBackfill
                && !retryPendingNow
                && appliedIt != g_treeAppliedVersion.end()
                && sectionIt->second.editVersion > appliedIt->second) {
                g_treeAppliedVersion[key] = sectionIt->second.editVersion;
                if (queuedFromDirty) {
                    g_treePendingSections.erase(key);
                }
                continue;
            }

            bool modified = false;
            bool unresolvedDependencies = false;
            std::unordered_set<glm::ivec3, IVec3Hash> touchedSections;
            int sectionSize = sectionIt->second.size;
            glm::ivec3 sectionCoord = key.coord;
            const int canopyPad = std::max(
                1,
                static_cast<int>(std::ceil(
                    (baseSpec.canopyBottomRadius + baseSpec.canopyLowerRadiusBoost)
                    / static_cast<float>(sectionScale)
                ))
            );
            int minX = sectionCoord.x * sectionSize;
            int minZ = sectionCoord.z * sectionSize;
            int maxX = minX + sectionSize - 1;
            int maxZ = minZ + sectionSize - 1;

            for (int lodZ = minZ - canopyPad; lodZ <= maxZ + canopyPad; ++lodZ) {
                for (int lodX = minX - canopyPad; lodX <= maxX + canopyPad; ++lodX) {
                    const int worldX = lodX * sectionScale;
                    const int worldZ = lodZ * sectionScale;
                    const int biomeID = ExpanseBiomeSystemLogic::ResolveBiome(
                        worldCtx,
                        static_cast<float>(worldX),
                        static_cast<float>(worldZ)
                    );
                    const bool islandQuadrants =
                        (worldCtx.expanse.islandRadius > 0.0f) && worldCtx.expanse.secondaryBiomeEnabled;
                    if (isWithinJungleVolcano(worldCtx.expanse, biomeID, worldX, worldZ)) continue;
                    if (biomeID == 1 || biomeID == 2) continue; // meadow/desert: no trees

                    const bool wantsPineTree = (biomeID == 0) && shouldSpawnPine(worldX, worldZ, baseSpec);
                    const bool wantsJungleTree =
                        islandQuadrants && (biomeID == 3)
                        && ((hash2D(worldX + 173, worldZ - 911) % static_cast<uint32_t>(jungleTreeSpawnModulo)) == 0u);
                    const bool wantsBareTree =
                        (biomeID == 4)
                        && ((hash2D(worldX - 433, worldZ + 1259) % static_cast<uint32_t>(bareTreeSpawnModulo)) == 0u);
                    if (!wantsPineTree && !wantsJungleTree && !wantsBareTree) continue;

                    float terrainHeight = 0.0f;
                    bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(
                        worldCtx,
                        static_cast<float>(worldX),
                        static_cast<float>(worldZ),
                        terrainHeight
                    );
                    if (!isLand) continue;
                    int groundY = floorDivInt(static_cast<int>(std::floor(terrainHeight)), sectionScale);
                    VoxelSectionKey trunkSectionKey{
                        sectionLod,
                        glm::ivec3(
                            floorDivInt(lodX, sectionSize),
                            floorDivInt(groundY + 1, sectionSize),
                            floorDivInt(lodZ, sectionSize)
                        )
                    };
                    // Root section ownership: only generate a given tree once, from the section
                    // containing the trunk base. This prevents cross-section duplicates while
                    // still allowing the tree to write into higher sections.
                    if (trunkSectionKey.coord != sectionCoord) continue;
                    if (voxelWorld.sections.find(trunkSectionKey) == voxelWorld.sections.end()) continue;
                    const glm::ivec3 groundCell(lodX, groundY, lodZ);
                    if (getBlockAtLod(voxelWorld, sectionLod, groundCell) == 0u) {
                        if (isGroundDependencyPending(voxelWorld, sectionLod, groundCell, sectionSize)) {
                            unresolvedDependencies = true;
                        }
                        continue;
                    }

                    if (wantsPineTree) {
                        PineSpec treeSpec = pineSpecForCell(
                            baseSpec,
                            worldX,
                            worldZ,
                            pineMinTrunkHeight,
                            pineMaxTrunkHeight
                        );
                        treeSpec = scaledPineSpecForLod(treeSpec, sectionScale);

                        const int trunkPrototypeID = ((hash2D(worldX, worldZ) >> 8u) & 1u) == 0u
                            ? trunkProtoA->prototypeID
                            : trunkProtoB->prototypeID;
                        const int nubPrototypeID = (trunkPrototypeID == trunkProtoA->prototypeID)
                            ? trunkProtoB->prototypeID
                            : trunkProtoA->prototypeID;
                        const int topPrototypeID = topLogPrototypeFor(prototypes, nubPrototypeID);

                        if (!trunkColumnCanExist(voxelWorld,
                                                 sectionLod,
                                                 trunkProtoA->prototypeID,
                                                 trunkProtoB->prototypeID,
                                                 lodX,
                                                 groundY,
                                                 lodZ,
                                                 treeSpec.trunkHeight)) {
                            continue;
                        }

                        if (hasNearbyConflictingTrunk(voxelWorld,
                                                      sectionLod,
                                                      trunkProtoA->prototypeID,
                                                      trunkProtoB->prototypeID,
                                                      lodX,
                                                      groundY + 1,
                                                      lodZ,
                                                      treeSpec.trunkExclusionRadius)) {
                            continue;
                        }

                        writeTreeToWorld(voxelWorld,
                                         prototypes,
                                         sectionLod,
                                         sectionSize,
                                         trunkPrototypeID,
                                         topPrototypeID,
                                         nubPrototypeID,
                                         leafProto->prototypeID,
                                         leafFanPineProto ? leafFanPineProto->prototypeID : -1,
                                         trunkColor,
                                         leafColor,
                                         lodX,
                                         groundY,
                                         lodZ,
                                         treeSpec,
                                         touchedSections,
                                         modified);
                    } else if (wantsBareTree) {
                        const int bareTrunkID = bareTrunkProto ? bareTrunkProto->prototypeID : trunkProtoA->prototypeID;
                        const uint32_t bareSeed = hash2D(worldX + 6097, worldZ - 2953);
                        const int bareHeightSpan = bareTreeTrunkMax - bareTreeTrunkMin + 1;
                        const int bareTrunkHeight = bareTreeTrunkMin + static_cast<int>(
                            bareSeed % static_cast<uint32_t>(bareHeightSpan)
                        );

                        if (!trunkColumnCanExist(voxelWorld,
                                                 sectionLod,
                                                 bareTrunkID,
                                                 bareTrunkID,
                                                 lodX,
                                                 groundY,
                                                 lodZ,
                                                 bareTrunkHeight)) {
                            continue;
                        }

                        if (hasNearbyConflictingTrunk(voxelWorld,
                                                      sectionLod,
                                                      bareTrunkID,
                                                      bareTrunkID,
                                                      lodX,
                                                      groundY + 1,
                                                      lodZ,
                                                      2)) {
                            continue;
                        }

                        const uint32_t bareTrunkColor = packColor(glm::vec3(0.35f, 0.29f, 0.23f));
                        const uint32_t bareBranchColor = packColor(glm::vec3(0.42f, 0.35f, 0.27f));
                        writeBareTreeToWorld(
                            voxelWorld,
                            sectionLod,
                            sectionSize,
                            bareTrunkID,
                            bareBranchProtoX ? bareBranchProtoX->prototypeID : -1,
                            bareBranchProtoZ ? bareBranchProtoZ->prototypeID : -1,
                            wallBranchLongProtoPosX ? wallBranchLongProtoPosX->prototypeID : -1,
                            wallBranchLongProtoNegX ? wallBranchLongProtoNegX->prototypeID : -1,
                            wallBranchLongProtoPosZ ? wallBranchLongProtoPosZ->prototypeID : -1,
                            wallBranchLongProtoNegZ ? wallBranchLongProtoNegZ->prototypeID : -1,
                            wallBranchLongTipProtoPosX ? wallBranchLongTipProtoPosX->prototypeID : -1,
                            wallBranchLongTipProtoNegX ? wallBranchLongTipProtoNegX->prototypeID : -1,
                            wallBranchLongTipProtoPosZ ? wallBranchLongTipProtoPosZ->prototypeID : -1,
                            wallBranchLongTipProtoNegZ ? wallBranchLongTipProtoNegZ->prototypeID : -1,
                            bareTrunkColor,
                            bareBranchColor,
                            lodX,
                            groundY,
                            lodZ,
                            bareTrunkHeight,
                            bareSeed,
                            touchedSections,
                            modified
                        );
                    } else {
                        const int jungleTrunkID = jungleTrunkProto ? jungleTrunkProto->prototypeID : trunkProtoA->prototypeID;
                        const uint32_t jungleSeed = hash2D(worldX + 4041, worldZ - 7927);
                        const int jungleHeightSpan = jungleTreeTrunkMax - jungleTreeTrunkMin + 1;
                        const int jungleTrunkHeight = jungleTreeTrunkMin + static_cast<int>(jungleSeed % static_cast<uint32_t>(jungleHeightSpan));

                        if (!trunkColumnCanExist(voxelWorld,
                                                 sectionLod,
                                                 jungleTrunkID,
                                                 jungleTrunkID,
                                                 lodX,
                                                 groundY,
                                                 lodZ,
                                                 jungleTrunkHeight)) {
                            continue;
                        }

                        if (hasNearbyConflictingTrunk(voxelWorld,
                                                      sectionLod,
                                                      jungleTrunkID,
                                                      jungleTrunkID,
                                                      lodX,
                                                      groundY + 1,
                                                      lodZ,
                                                      2)) {
                            continue;
                        }

                        const uint32_t jungleTrunkColor = packColor(glm::vec3(0.36f, 0.24f, 0.15f));
                        writeJungleTreeToWorld(
                            voxelWorld,
                            sectionLod,
                            sectionSize,
                            jungleTrunkID,
                            leafProto->prototypeID,
                            leafFanOakProto ? leafFanOakProto->prototypeID : -1,
                            jungleLeafPrototypeIDs,
                            jungleLeafPrototypeCount,
                            jungleTrunkColor,
                            leafColor,
                            lodX,
                            groundY,
                            lodZ,
                            jungleTrunkHeight,
                            jungleTreeCanopyRadius,
                            touchedSections,
                            modified
                        );
                    }
                }
            }

            convertExposedLeafShellInSection(
                voxelWorld,
                prototypes,
                sectionLod,
                sectionCoord,
                sectionSize,
                leafProto ? leafProto->prototypeID : -1,
                leafFanPineProto ? leafFanPineProto->prototypeID : -1,
                leafFanOakProto ? leafFanOakProto->prototypeID : -1,
                leafColor,
                touchedSections,
                modified
            );

            writeGroundFoliageToSection(
                prototypes,
                worldCtx,
                voxelWorld,
                sectionLod,
                sectionCoord,
                sectionSize,
                sectionScale,
                grassProto ? grassProto->prototypeID : -1,
                shortGrassProto ? shortGrassProto->prototypeID : -1,
                grassProtoBiome1 ? grassProtoBiome1->prototypeID : -1,
                shortGrassProtoBiome1 ? shortGrassProtoBiome1->prototypeID : -1,
                grassProtoBiome3 ? grassProtoBiome3->prototypeID : -1,
                grassPrototypeBiome4IDs,
                grassPrototypeBiome4Count,
                grassCoverProtoX ? grassCoverProtoX->prototypeID : -1,
                grassCoverProtoZ ? grassCoverProtoZ->prototypeID : -1,
                grassCoverMeadowProtoX ? grassCoverMeadowProtoX->prototypeID : -1,
                grassCoverMeadowProtoZ ? grassCoverMeadowProtoZ->prototypeID : -1,
                grassCoverForestProtoX ? grassCoverForestProtoX->prototypeID : -1,
                grassCoverForestProtoZ ? grassCoverForestProtoZ->prototypeID : -1,
                grassCoverJungleProtoX ? grassCoverJungleProtoX->prototypeID : -1,
                grassCoverJungleProtoZ ? grassCoverJungleProtoZ->prototypeID : -1,
                grassCoverBareProtoX ? grassCoverBareProtoX->prototypeID : -1,
                grassCoverBareProtoZ ? grassCoverBareProtoZ->prototypeID : -1,
                grassCoverDesertProtoX ? grassCoverDesertProtoX->prototypeID : -1,
                grassCoverDesertProtoZ ? grassCoverDesertProtoZ->prototypeID : -1,
                pebblePatchProtoX ? pebblePatchProtoX->prototypeID : -1,
                pebblePatchProtoZ ? pebblePatchProtoZ->prototypeID : -1,
                autumnLeafPatchProtoX ? autumnLeafPatchProtoX->prototypeID : -1,
                autumnLeafPatchProtoZ ? autumnLeafPatchProtoZ->prototypeID : -1,
                deadLeafPatchProtoX ? deadLeafPatchProtoX->prototypeID : -1,
                deadLeafPatchProtoZ ? deadLeafPatchProtoZ->prototypeID : -1,
                lilypadPatchProtoX ? lilypadPatchProtoX->prototypeID : -1,
                lilypadPatchProtoZ ? lilypadPatchProtoZ->prototypeID : -1,
                bareBranchProtoX ? bareBranchProtoX->prototypeID : -1,
                bareBranchProtoZ ? bareBranchProtoZ->prototypeID : -1,
                miniPineBottomProto ? miniPineBottomProto->prototypeID : -1,
                miniPineTopProto ? miniPineTopProto->prototypeID : -1,
                miniPineTripleBottomProto ? miniPineTripleBottomProto->prototypeID : -1,
                miniPineTripleMiddleProto ? miniPineTripleMiddleProto->prototypeID : -1,
                miniPineTripleTopProto ? miniPineTripleTopProto->prototypeID : -1,
                flaxDoubleBottomProto ? flaxDoubleBottomProto->prototypeID : -1,
                flaxDoubleTopProto ? flaxDoubleTopProto->prototypeID : -1,
                hempDoubleBottomProto ? hempDoubleBottomProto->prototypeID : -1,
                hempDoubleTopProto ? hempDoubleTopProto->prototypeID : -1,
                blueFlowerPrototypeIDs,
                blueFlowerPrototypeCount,
                blueRareFlowerProto ? blueRareFlowerProto->prototypeID : -1,
                flaxRareFlowerProto ? flaxRareFlowerProto->prototypeID : -1,
                hempRareFlowerProto ? hempRareFlowerProto->prototypeID : -1,
                fernRareFlowerProto ? fernRareFlowerProto->prototypeID : -1,
                flowerProto ? flowerProto->prototypeID : -1,
                succulentProto ? succulentProto->prototypeID : -1,
                succulentProtoV2 ? succulentProtoV2->prototypeID : -1,
                jungleOrangeUnderLeafProto ? jungleOrangeUnderLeafProto->prototypeID : -1,
                stickProtoX ? stickProtoX->prototypeID : -1,
                stickProtoZ ? stickProtoZ->prototypeID : -1,
                stickWinterProtoX ? stickWinterProtoX->prototypeID : -1,
                stickWinterProtoZ ? stickWinterProtoZ->prototypeID : -1,
                leafProto ? leafProto->prototypeID : -1,
                waterPrototypeID,
                foliageSpec,
                unresolvedDependencies,
                modified
            );
            writeWaterFoliageToSection(
                prototypes,
                voxelWorld,
                sectionLod,
                sectionCoord,
                sectionSize,
                sectionScale,
                kelpProto ? kelpProto->prototypeID : -1,
                kelpTileIndex,
                seaUrchinProtoX ? seaUrchinProtoX->prototypeID : -1,
                seaUrchinProtoZ ? seaUrchinProtoZ->prototypeID : -1,
                sandDollarProtoX ? sandDollarProtoX->prototypeID : -1,
                sandDollarProtoZ ? sandDollarProtoZ->prototypeID : -1,
                waterPrototypeID,
                foliageSpec,
                modified
            );
            if (desertCactusEnabled) {
                writeDesertCactusToSection(
                    prototypes,
                    worldCtx,
                    voxelWorld,
                    sectionLod,
                    sectionCoord,
                    sectionSize,
                    sectionScale,
                    cactusProtoA ? cactusProtoA->prototypeID : -1,
                    cactusProtoB ? cactusProtoB->prototypeID : -1,
                    cactusProtoAX ? cactusProtoAX->prototypeID : -1,
                    cactusProtoAZ ? cactusProtoAZ->prototypeID : -1,
                    cactusProtoBX ? cactusProtoBX->prototypeID : -1,
                    cactusProtoBZ ? cactusProtoBZ->prototypeID : -1,
                    cactusProtoAJunctionX ? cactusProtoAJunctionX->prototypeID : -1,
                    cactusProtoAJunctionZ ? cactusProtoAJunctionZ->prototypeID : -1,
                    cactusProtoBJunctionX ? cactusProtoBJunctionX->prototypeID : -1,
                    cactusProtoBJunctionZ ? cactusProtoBJunctionZ->prototypeID : -1,
                    waterPrototypeID,
                    desertCactusSpawnModulo,
                    unresolvedDependencies,
                    modified
                );
            }
            const bool allowCaveDecorForSection =
                (sectionLod <= caveDecorMaxLod) && !skipCaveDecorWhenBacklogged;
            if (allowCaveDecorForSection) {
                writeCaveSlopeToSection(
                    prototypes,
                    worldCtx,
                    voxelWorld,
                    sectionLod,
                    sectionCoord,
                    sectionSize,
                    sectionScale,
                    slopeProtoPosX ? slopeProtoPosX->prototypeID : -1,
                    slopeProtoNegX ? slopeProtoNegX->prototypeID : -1,
                    slopeProtoPosZ ? slopeProtoPosZ->prototypeID : -1,
                    slopeProtoNegZ ? slopeProtoNegZ->prototypeID : -1,
                    foliageSpec,
                    modified
                );
                writeCaveStoneToSection(
                    prototypes,
                    worldCtx,
                    voxelWorld,
                    sectionLod,
                    sectionCoord,
                    sectionSize,
                    sectionScale,
                    stonePebbleProtoX ? stonePebbleProtoX->prototypeID : -1,
                    stonePebbleProtoZ ? stonePebbleProtoZ->prototypeID : -1,
                    waterPrototypeID,
                    foliageSpec,
                    modified
                );
                writeCavePotsToSection(
                    prototypes,
                    worldCtx,
                    voxelWorld,
                    sectionLod,
                    sectionCoord,
                    sectionSize,
                    sectionScale,
                    cavePotProtoX ? cavePotProtoX->prototypeID : -1,
                    cavePotProtoZ ? cavePotProtoZ->prototypeID : -1,
                    foliageSpec,
                    modified
                );
                writeCaveWallStoneToSection(
                    prototypes,
                    worldCtx,
                    voxelWorld,
                    sectionLod,
                    sectionCoord,
                    sectionSize,
                    sectionScale,
                    wallStoneProtoPosX ? wallStoneProtoPosX->prototypeID : -1,
                    wallStoneProtoNegX ? wallStoneProtoNegX->prototypeID : -1,
                    wallStoneProtoPosZ ? wallStoneProtoPosZ->prototypeID : -1,
                    wallStoneProtoNegZ ? wallStoneProtoNegZ->prototypeID : -1,
                    foliageSpec,
                    modified
                );
                writeCaveCeilingStoneToSection(
                    prototypes,
                    worldCtx,
                    voxelWorld,
                    sectionLod,
                    sectionCoord,
                    sectionSize,
                    sectionScale,
                    ceilingStoneProtoPosX ? ceilingStoneProtoPosX->prototypeID
                                          : (ceilingStoneProtoX ? ceilingStoneProtoX->prototypeID : -1),
                    ceilingStoneProtoNegX ? ceilingStoneProtoNegX->prototypeID
                                          : (ceilingStoneProtoX ? ceilingStoneProtoX->prototypeID : -1),
                    ceilingStoneProtoPosZ ? ceilingStoneProtoPosZ->prototypeID
                                          : (ceilingStoneProtoZ ? ceilingStoneProtoZ->prototypeID : -1),
                    ceilingStoneProtoNegZ ? ceilingStoneProtoNegZ->prototypeID
                                          : (ceilingStoneProtoZ ? ceilingStoneProtoZ->prototypeID : -1),
                    foliageSpec,
                    modified
                );
            }

            if (modified) touchedSections.insert(sectionCoord);
            if (modified) {
                for (const auto& touched : touchedSections) {
                    VoxelSectionKey touchedKey{sectionLod, touched};
                    auto touchedIt = voxelWorld.sections.find(touchedKey);
                    if (touchedIt == voxelWorld.sections.end()) continue;
                    touchedIt->second.editVersion += 1;
                    touchedIt->second.dirty = true;
                    voxelWorld.dirtySections.insert(touchedKey);
                }
                if (getRegistryBool(baseSystem, "TreeFoliagePriorityRemesh", true)) {
                    const glm::ivec3 requestCell = (sectionCoord * sectionSize + glm::ivec3(sectionSize / 2)) * sectionScale;
                    VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, requestCell);
                }
            }
            const bool canFinalizeSection = !unresolvedDependencies;
            auto finalSectionIt = voxelWorld.sections.find(key);
            if (canFinalizeSection && finalSectionIt != voxelWorld.sections.end()) {
                g_treeAppliedVersion[key] = finalSectionIt->second.editVersion;
            }
            if (canFinalizeSection) {
                g_treePendingDependencies.erase(key);
                g_treePendingSections.erase(key);
            } else {
                g_treePendingDependencies.insert(key);
                g_treePendingSections.insert(key);
            }
            if (forceBackfill && canFinalizeSection) {
                g_treeBackfillVisited.insert(key);
            }
        }

        g_treePerfStats.pendingSections = g_treePendingSections.size();
        g_treePerfStats.pendingDependencies = g_treePendingDependencies.size();
        g_treePerfStats.backfillVisited = g_treeBackfillVisited.size();
        g_treePerfStats.selectedSections = static_cast<int>(dirtySections.size());
        g_treePerfStats.processedSections = processedSections;
        g_treePerfStats.deferredByTimeBudget = deferredByTimeBudget;
        g_treePerfStats.backfillAppended = backfillAppended;
        g_treePerfStats.backfillRan = backfillRan;
        g_treePerfStats.updateMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - updateStart
        ).count();
        rebuildColumnFoliageReady();
    }
}
