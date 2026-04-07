#pragma once
#include "Host/PlatformInput.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <cmath>
#include <mutex>
#include <thread>
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace RenderInitSystemLogic {
    int getRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback);
    bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback);
    float getRegistryFloat(const BaseSystem& baseSystem, const std::string& key, float fallback);
    bool shouldRenderVoxelSection(const BaseSystem& baseSystem,
                                  const VoxelSection& section,
                                  const glm::vec3& cameraPos);
    int FaceTileIndexFor(const WorldContext* worldCtx, const Entity& proto, int faceType);
}
namespace VoxelMeshInitSystemLogic {
    glm::vec3 UnpackColor(uint32_t packed);
    int FloorDivInt(int value, int divisor);
    int SectionSizeForLod(const VoxelWorldContext& voxelWorld, int lod);
    uint32_t GetVoxelIdAtLod(const VoxelWorldContext& voxelWorld, int lod, const glm::ivec3& coord);
    uint32_t GetVoxelColorAtLod(const VoxelWorldContext& voxelWorld, int lod, const glm::ivec3& coord);
    glm::ivec3 LocalCellFromUV(int faceType, int slice, int u, int v);
    glm::ivec3 FaceNormal(int faceType);
}
namespace BlockChargeSystemLogic {
    void CollectDamagedVoxelCells(const BaseSystem& baseSystem, int worldIndex, std::vector<glm::ivec3>& outCells);
}

namespace VoxelMeshingSystemLogic {

    namespace {
        struct VoxelGreedySnapshot {
            VoxelSectionKey renderKey;
            int lod = 0;
            int sizeX = 0;
            int sizeY = 0;
            int sizeZ = 0;
            int dimX = 0;
            int dimY = 0;
            int dimZ = 0;
            glm::ivec3 minCoord{0};
            uint64_t versionKey = 0;
            uint32_t renderEditVersion = 0;
            bool cullPlantsBeyondLod0 = false;
            bool leafFanRenderInnerBlock = true;
            bool waterTopOnlyOutsideLod0 = true;
            bool disableAo = false;
            bool leafAoEnabled = true;
            float leafAoStrength = 1.0f;
            bool plantAoEnabled = false;
            float aoStrength = 1.0f;
            float plantAoStrength = 1.0f;
            bool lightingEnabled = true;
            bool lightingAffectWater = true;
            float lightingStrength = 1.0f;
            float lightingMinBrightness = 0.08f;
            float lightingGamma = 1.35f;
            int lightingDebugMode = 0;
            uint8_t lightingSkyFallbackLevel = static_cast<uint8_t>(15);
            const WorldContext* worldCtx = nullptr;
            std::vector<uint32_t> ids;
            std::vector<uint32_t> colors;
            std::vector<uint8_t> skyLights;
            std::vector<uint8_t> blockLights;
            std::vector<uint8_t> known;
            std::vector<glm::ivec3> damagedCells;
        };

        uint8_t resolveSkyFallbackLevel(const BaseSystem& baseSystem, const VoxelWorldContext& voxelWorld) {
            const int raw = ::RenderInitSystemLogic::getRegistryInt(
                baseSystem,
                "VoxelLightingCurrentSkyLevel",
                static_cast<int>(voxelWorld.defaultSkyLightLevel)
            );
            return static_cast<uint8_t>(std::clamp(raw, 0, 15));
        }

        struct VoxelGreedyResult {
            VoxelSectionKey renderKey;
            uint64_t versionKey = 0;
            uint32_t renderEditVersion = 0;
            bool empty = true;
            GreedyChunkData mesh;
        };

        struct VoxelGreedyAsyncState {
            std::mutex mutex;
            std::condition_variable cv;
            std::deque<VoxelGreedySnapshot> queue;
            std::deque<VoxelGreedyResult> results;
            std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> inFlight;
            std::vector<std::thread> workers;
            bool running = false;
            bool stop = false;
            const std::vector<Entity>* prototypes = nullptr;
        };

        static VoxelGreedyAsyncState g_voxelGreedyAsync;
        static size_t g_lastGreedyQueued = 0;
        static size_t g_lastGreedyApplied = 0;
        static size_t g_lastGreedyDropped = 0;

        struct IVec3Hash {
            std::size_t operator()(const glm::ivec3& v) const noexcept {
                std::size_t h = std::hash<int>()(v.x);
                h ^= (std::hash<int>()(v.y) + 0x9e3779b9u + (h << 6) + (h >> 2));
                h ^= (std::hash<int>()(v.z) + 0x9e3779b9u + (h << 6) + (h >> 2));
                return h;
            }
        };

        int resolveActiveWorldIndex(const BaseSystem& baseSystem) {
            if (!baseSystem.level) return -1;
            int worldIndex = baseSystem.level->activeWorldIndex;
            if (worldIndex < 0 || worldIndex >= static_cast<int>(baseSystem.level->worlds.size())) {
                worldIndex = 0;
            }
            if (worldIndex < 0 || worldIndex >= static_cast<int>(baseSystem.level->worlds.size())) {
                return -1;
            }
            return worldIndex;
        }

        std::unordered_set<glm::ivec3, IVec3Hash> collectDamagedVoxelCellSet(const BaseSystem& baseSystem,
                                                                              int lod) {
            std::unordered_set<glm::ivec3, IVec3Hash> out;
            if (lod != 0) return out;
            const bool crackNeighborFacesEnabled = ::RenderInitSystemLogic::getRegistryBool(
                baseSystem,
                "BlockBreakNeighborFacesEnabled",
                true
            );
            if (!crackNeighborFacesEnabled) return out;
            const int worldIndex = resolveActiveWorldIndex(baseSystem);
            if (worldIndex < 0) return out;
            std::vector<glm::ivec3> damagedCells;
            BlockChargeSystemLogic::CollectDamagedVoxelCells(baseSystem, worldIndex, damagedCells);
            if (damagedCells.empty()) return out;
            out.reserve(damagedCells.size());
            for (const glm::ivec3& cell : damagedCells) {
                out.insert(cell);
            }
            return out;
        }

        GreedyChunkData acquireGreedyChunk(VoxelGreedyContext& ctx) {
            GreedyChunkData out;
            if (!ctx.chunkPool.empty()) {
                out = std::move(ctx.chunkPool.back());
                ctx.chunkPool.pop_back();
                out.positions.clear();
                out.colors.clear();
                out.faceTypes.clear();
                out.tileIndices.clear();
                out.alphas.clear();
                out.ao.clear();
                out.scales.clear();
                out.uvScales.clear();
            }
            return out;
        }

        void releaseGreedyChunk(VoxelGreedyContext& ctx, GreedyChunkData&& chunk) {
            chunk.positions.clear();
            chunk.colors.clear();
            chunk.faceTypes.clear();
            chunk.tileIndices.clear();
            chunk.alphas.clear();
            chunk.ao.clear();
            chunk.scales.clear();
            chunk.uvScales.clear();
            ctx.chunkPool.push_back(std::move(chunk));
        }

        void releaseGreedyChunkIfPresent(VoxelGreedyContext& ctx, const VoxelSectionKey& key) {
            auto it = ctx.chunks.find(key);
            if (it == ctx.chunks.end()) return;
            releaseGreedyChunk(ctx, std::move(it->second));
            ctx.chunks.erase(it);
        }

        uint64_t mixVersionKey(uint64_t seed, uint64_t value) {
            return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
        }

        uint64_t computeGreedyVersionKey(const VoxelWorldContext& voxelWorld,
                                         int lod,
                                         const glm::ivec3& anchorCoord,
                                         int superChunkSize) {
            uint64_t key = 1469598103934665603ULL;
            bool found = false;
            for (int oz = 0; oz < superChunkSize; ++oz) {
                for (int ox = 0; ox < superChunkSize; ++ox) {
                    glm::ivec3 coord(anchorCoord.x + ox, anchorCoord.y, anchorCoord.z + oz);
                    VoxelSectionKey secKey{lod, coord};
                    auto it = voxelWorld.sections.find(secKey);
                    uint64_t coordHash = static_cast<uint64_t>(
                        (coord.x * 73856093) ^ (coord.y * 19349663) ^ (coord.z * 83492791)
                    );
                    key = mixVersionKey(key, coordHash);
                    if (it != voxelWorld.sections.end()) {
                        key = mixVersionKey(key, static_cast<uint64_t>(it->second.editVersion));
                        found = true;
                    } else {
                        key = mixVersionKey(key, 0);
                    }
                }
            }
            return found ? key : 0;
        }

        bool isLeafPrototype(const Entity& proto) {
            return proto.name == "Leaf"
                || proto.name.rfind("LeafJungle", 0) == 0
                || proto.name == "GrassTuftLeafFanOak"
                || proto.name == "GrassTuftLeafFanPine"
                || (proto.isAnimated && proto.hasWireframe && !proto.isSolid);
        }

        enum class PlantType : int { None = 0, GrassTall = 1, Flower = 2, GrassShort = 3, CavePot = 4 };

        bool isCavePotXName(const std::string& name) {
            return name == "StonePebbleCavePotTexX";
        }

        bool isCavePotZName(const std::string& name) {
            return name == "StonePebbleCavePotTexZ";
        }

        PlantType plantTypeForPrototype(const Entity& proto) {
            if (isCavePotXName(proto.name) || isCavePotZName(proto.name)) return PlantType::CavePot;
            if (proto.name.rfind("GrassTuftShort", 0) == 0) return PlantType::GrassShort;
            if (proto.name.rfind("GrassTuft", 0) == 0) return PlantType::GrassTall;
            if (proto.name.rfind("Flower", 0) == 0) return PlantType::Flower;
            return PlantType::None;
        }

        float alphaForPlantType(PlantType type) {
            switch (type) {
                case PlantType::Flower: return -3.0f;
                case PlantType::GrassShort: return -2.3f;
                case PlantType::CavePot: return -10.0f;
                case PlantType::GrassTall:
                case PlantType::None:
                default:
                    return -2.0f;
            }
        }

        bool isPlantPrototype(const Entity& proto) {
            return plantTypeForPrototype(proto) != PlantType::None;
        }

        bool isLeafFanPlantPrototype(const Entity& proto) {
            return proto.name == "GrassTuftLeafFanOak"
                || proto.name == "GrassTuftLeafFanPine";
        }

        bool startsWith(const std::string& value, const char* prefix) {
            if (!prefix) return false;
            const size_t prefixLen = std::strlen(prefix);
            return value.size() >= prefixLen
                && value.compare(0, prefixLen, prefix) == 0;
        }

        int findPrototypeIDByName(const std::vector<Entity>& prototypes, const char* name) {
            if (!name) return -1;
            for (size_t i = 0; i < prototypes.size(); ++i) {
                if (prototypes[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        bool endsWith(const std::string& value, const char* suffix) {
            if (!suffix) return false;
            const size_t suffixLen = std::strlen(suffix);
            return value.size() >= suffixLen
                && value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
        }

        bool isStonePebbleXName(const std::string& name) {
            if (isCavePotXName(name)) return false;
            return name == "StonePebbleTexX"
                || (startsWith(name, "StonePebble") && endsWith(name, "TexX"));
        }

        bool isStonePebbleZName(const std::string& name) {
            if (isCavePotZName(name)) return false;
            return name == "StonePebbleTexZ"
                || (startsWith(name, "StonePebble") && endsWith(name, "TexZ"));
        }

        bool isSurfaceStonePebbleName(const std::string& name) {
            return name == "StonePebbleTexX" || name == "StonePebbleTexZ"
                || name == "StonePebbleRubyTexX" || name == "StonePebbleRubyTexZ"
                || name == "StonePebbleAmethystTexX" || name == "StonePebbleAmethystTexZ"
                || name == "StonePebbleFlouriteTexX" || name == "StonePebbleFlouriteTexZ"
                || name == "StonePebbleSilverTexX" || name == "StonePebbleSilverTexZ";
        }

        bool isGrassCoverXName(const std::string& name) {
            return name == "GrassCoverTexX"
                || (startsWith(name, "GrassCover") && endsWith(name, "TexX"));
        }

        bool isGrassCoverZName(const std::string& name) {
            return name == "GrassCoverTexZ"
                || (startsWith(name, "GrassCover") && endsWith(name, "TexZ"));
        }

        bool isChalkDustXName(const std::string& name) {
            return name == "GrassCoverChalkTexX";
        }

        bool isChalkDustZName(const std::string& name) {
            return name == "GrassCoverChalkTexZ";
        }

        bool isChalkDustPrototype(const Entity& proto) {
            return isChalkDustXName(proto.name) || isChalkDustZName(proto.name);
        }

        bool isBlueprintMatXName(const std::string& name) {
            return startsWith(name, "GrassCoverBlueprint") && endsWith(name, "TexX");
        }

        bool isBlueprintMatZName(const std::string& name) {
            return startsWith(name, "GrassCoverBlueprint") && endsWith(name, "TexZ");
        }

        bool isPetalPileName(const std::string& name) {
            if (startsWith(name, "StonePebblePetalsBook")) return false;
            return startsWith(name, "StonePebblePetals")
                || startsWith(name, "StonePebblePatch")
                || startsWith(name, "StonePebbleLeaf")
                || startsWith(name, "StonePebbleLilypad")
                || startsWith(name, "StonePebbleSandDollar");
        }

        bool isBookPrototypeName(const std::string& name) {
            return startsWith(name, "StonePebblePetalsBook");
        }

        bool isVoidPortalPrototypeName(const std::string& name) {
            return name == "VoidPortalBlockTex";
        }

        int positiveMod(int value, int modulus) {
            if (modulus <= 0) return 0;
            int result = value % modulus;
            if (result < 0) result += modulus;
            return result;
        }

        int voidPortalTileIndexForFace(const glm::ivec3& lodCoord, int faceType) {
            int u = lodCoord.x;
            int v = lodCoord.z;
            switch (faceType) {
                case 0:
                case 1:
                    u = lodCoord.z;
                    v = lodCoord.y;
                    break;
                case 4:
                case 5:
                    u = lodCoord.x;
                    v = lodCoord.y;
                    break;
                case 2:
                case 3:
                default:
                    u = lodCoord.x;
                    v = lodCoord.z;
                    break;
            }
            const int col = positiveMod(u, 3);
            const int row = positiveMod(v, 3);
            return 441 + row * 3 + col;
        }

        bool isLilypadName(const std::string& name) {
            return startsWith(name, "StonePebbleLilypad")
                || startsWith(name, "GrassCoverBigLilypad");
        }

        bool isSandDollarName(const std::string& name) {
            return startsWith(name, "StonePebbleSandDollar");
        }

        bool isLavaName(const std::string& name) {
            return name == "LavaBlockTex"
                || startsWith(name, "DepthLavaTile")
                || name == "Lava";
        }

        bool isBigLilypadName(const std::string& name) {
            return startsWith(name, "GrassCoverBigLilypad");
        }

        bool isDepthCrystalClusterName(const std::string& name) {
            return name == "DepthCrystalClusterTex"
                || name == "DepthCrystalClusterBlueTex"
                || name == "DepthCrystalClusterBlueBigTex"
                || name == "DepthCrystalClusterMagentaBigTex";
        }

        bool isCactusArmName(const std::string& name) {
            return name == "Cactus1TexX"
                || name == "Cactus2TexX"
                || name == "Cactus1TexZ"
                || name == "Cactus2TexZ";
        }

        bool isCactusJunctionName(const std::string& name) {
            return name == "Cactus1TexJunctionX"
                || name == "Cactus2TexJunctionX"
                || name == "Cactus1TexJunctionZ"
                || name == "Cactus2TexJunctionZ";
        }

        bool isCeilingStoneName(const std::string& name) {
            return name.rfind("CeilingStoneTex", 0) == 0;
        }

        bool isCeilingStoneXName(const std::string& name) {
            return name == "CeilingStoneTexX"
                || name == "CeilingStoneTexPosX"
                || name == "CeilingStoneTexNegX";
        }

        bool isCeilingStoneZName(const std::string& name) {
            return name == "CeilingStoneTexZ"
                || name == "CeilingStoneTexPosZ"
                || name == "CeilingStoneTexNegZ";
        }

        bool isWallDecalName(const std::string& name) {
            return name == "WallStoneTexPosX"
                || name == "WallStoneTexNegX"
                || name == "WallStoneTexPosZ"
                || name == "WallStoneTexNegZ"
                || name == "DepthMossWallTexPosX"
                || name == "DepthMossWallTexNegX"
                || name == "DepthMossWallTexPosZ"
                || name == "DepthMossWallTexNegZ"
                || name == "WallBranchLongTexPosX"
                || name == "WallBranchLongTexNegX"
                || name == "WallBranchLongTexPosZ"
                || name == "WallBranchLongTexNegZ"
                || name == "WallBranchLongTipTexPosX"
                || name == "WallBranchLongTipTexNegX"
                || name == "WallBranchLongTipTexPosZ"
                || name == "WallBranchLongTipTexNegZ";
        }

        enum class NarrowLogAxis : int { None = 0, X = 1, Y = 2, Z = 3 };
        enum class NarrowShape : int { Default = 0, Stick = 1, StonePebble = 2, PetalPile = 3, GrassCover = 4, BlueprintMat = 5, ChalkDust = 6, WallDecal = 7, CactusArm = 8, CactusJunction = 9, Book = 10, Lantern = 11, BigLilypad = 12, DepthCrystal = 13 };
        enum class NarrowMount : int { Floor = 0, WallPosX = 1, WallNegX = 2, WallPosZ = 3, WallNegZ = 4, Ceiling = 5 };

        NarrowLogAxis narrowLogAxis(const Entity& proto) {
            if (proto.name == "LanternBlockTex") {
                return NarrowLogAxis::Y;
            }
            if (isDepthCrystalClusterName(proto.name)) {
                return NarrowLogAxis::Y;
            }
            if (proto.name == "Cactus1Tex" || proto.name == "Cactus2Tex") {
                return NarrowLogAxis::Y;
            }
            if (proto.name == "Cactus1TexX" || proto.name == "Cactus2TexX"
                || proto.name == "Cactus1TexJunctionX" || proto.name == "Cactus2TexJunctionX"
                || proto.name == "StickTexX"
                || proto.name == "StickWinterTexX"
                || isGrassCoverXName(proto.name)
                || isStonePebbleXName(proto.name)
                || isCeilingStoneXName(proto.name)) {
                return NarrowLogAxis::X;
            }
            if (proto.name == "Cactus1TexZ" || proto.name == "Cactus2TexZ"
                || proto.name == "Cactus1TexJunctionZ" || proto.name == "Cactus2TexJunctionZ"
                || proto.name == "StickTexZ"
                || proto.name == "StickWinterTexZ"
                || isGrassCoverZName(proto.name)
                || isStonePebbleZName(proto.name)
                || isCeilingStoneZName(proto.name)) {
                return NarrowLogAxis::Z;
            }
            if (isWallDecalName(proto.name)) {
                return NarrowLogAxis::Y;
            }
            return NarrowLogAxis::None;
        }

        bool isNarrowLogPrototype(const Entity& proto) {
            return narrowLogAxis(proto) != NarrowLogAxis::None;
        }

        NarrowShape narrowShapeForPrototype(const Entity& proto) {
            if (proto.name == "LanternBlockTex") return NarrowShape::Lantern;
            if (isDepthCrystalClusterName(proto.name)) return NarrowShape::DepthCrystal;
            if (isBookPrototypeName(proto.name)) return NarrowShape::Book;
            if (isBigLilypadName(proto.name)) return NarrowShape::BigLilypad;
            if (isPetalPileName(proto.name)) return NarrowShape::PetalPile;
            if (isCactusJunctionName(proto.name)) return NarrowShape::CactusJunction;
            if (isCactusArmName(proto.name)) return NarrowShape::CactusArm;
            if (proto.name == "StickTexX" || proto.name == "StickTexZ"
                || proto.name == "StickWinterTexX" || proto.name == "StickWinterTexZ") {
                return NarrowShape::Stick;
            }
            if (isBlueprintMatXName(proto.name) || isBlueprintMatZName(proto.name)) return NarrowShape::BlueprintMat;
            if (isChalkDustXName(proto.name) || isChalkDustZName(proto.name)) return NarrowShape::ChalkDust;
            if (isGrassCoverXName(proto.name) || isGrassCoverZName(proto.name)) return NarrowShape::GrassCover;
            if (isWallDecalName(proto.name)) {
                return NarrowShape::WallDecal;
            }
            if (isStonePebbleXName(proto.name) || isStonePebbleZName(proto.name)
                || isCeilingStoneName(proto.name)) {
                return NarrowShape::StonePebble;
            }
            return NarrowShape::Default;
        }

        NarrowMount narrowMountForPrototype(const Entity& proto) {
            if (proto.name == "WallStoneTexPosX") return NarrowMount::WallPosX;
            if (proto.name == "WallStoneTexNegX") return NarrowMount::WallNegX;
            if (proto.name == "WallStoneTexPosZ") return NarrowMount::WallPosZ;
            if (proto.name == "WallStoneTexNegZ") return NarrowMount::WallNegZ;
            if (proto.name == "DepthMossWallTexPosX") return NarrowMount::WallPosX;
            if (proto.name == "DepthMossWallTexNegX") return NarrowMount::WallNegX;
            if (proto.name == "DepthMossWallTexPosZ") return NarrowMount::WallPosZ;
            if (proto.name == "DepthMossWallTexNegZ") return NarrowMount::WallNegZ;
            if (proto.name == "WallBranchLongTexPosX" || proto.name == "WallBranchLongTipTexPosX") return NarrowMount::WallPosX;
            if (proto.name == "WallBranchLongTexNegX" || proto.name == "WallBranchLongTipTexNegX") return NarrowMount::WallNegX;
            if (proto.name == "WallBranchLongTexPosZ" || proto.name == "WallBranchLongTipTexPosZ") return NarrowMount::WallPosZ;
            if (proto.name == "WallBranchLongTexNegZ" || proto.name == "WallBranchLongTipTexNegZ") return NarrowMount::WallNegZ;
            if (isCeilingStoneName(proto.name)) return NarrowMount::Ceiling;
            return NarrowMount::Floor;
        }

        bool wallDecalFaceEnabled(NarrowMount mount, int faceType) {
            // Emit a single wall-facing quad for decal mounts to avoid doubled side rendering.
            if (mount == NarrowMount::WallPosX) return faceType == 1;
            if (mount == NarrowMount::WallNegX) return faceType == 0;
            if (mount == NarrowMount::WallPosZ) return faceType == 5;
            if (mount == NarrowMount::WallNegZ) return faceType == 4;
            return true;
        }

        bool chalkDustFaceEnabled(int faceType) {
            // Chalk dust is a floor decal; only render the upward-facing quad.
            return faceType == 2;
        }

        bool bigLilypadFaceEnabled(int faceType) {
            // Big lilypads are authored as flat 2x2 surface covers; only draw top face.
            return faceType == 2;
        }

        bool depthCrystalFaceEnabled(int faceType) {
            // Depth crystal clusters are single non-cross cards with one orientation (+Z).
            return faceType == 4;
        }

        struct NarrowHalfExtents {
            float x;
            float y;
            float z;
        };

        NarrowHalfExtents narrowHalfExtentsForAxis(NarrowLogAxis axis) {
            // Base pine profile is 12x24x14 in 24-unit block space.
            constexpr float kHalf12 = 12.0f / 48.0f;
            constexpr float kHalf14 = 14.0f / 48.0f;
            constexpr float kHalf24 = 24.0f / 48.0f;
            switch (axis) {
                case NarrowLogAxis::X: return {kHalf24, kHalf12, kHalf14};
                case NarrowLogAxis::Y: return {kHalf12, kHalf24, kHalf14};
                case NarrowLogAxis::Z: return {kHalf12, kHalf14, kHalf24};
                default: return {kHalf24, kHalf24, kHalf24};
            }
        }

        NarrowHalfExtents narrowHalfExtentsForShape(NarrowLogAxis axis, NarrowShape shape, NarrowMount mount) {
            if (shape == NarrowShape::Default) return narrowHalfExtentsForAxis(axis);
            if (shape == NarrowShape::Stick) {
                // Thin on-ground stick profile: 12x1x1 in 24-unit block space.
                constexpr float kHalf1 = 1.0f / 48.0f;
                constexpr float kHalf12 = 12.0f / 48.0f;
                switch (axis) {
                    case NarrowLogAxis::X: return {kHalf12, kHalf1, kHalf1};
                    case NarrowLogAxis::Y: return {kHalf1, kHalf12, kHalf1};
                    case NarrowLogAxis::Z: return {kHalf1, kHalf1, kHalf12};
                    default: return {kHalf12, kHalf1, kHalf1};
                }
            }
            if (shape == NarrowShape::PetalPile || shape == NarrowShape::BigLilypad) {
                // Flat petal pile profile: 24x1x24 in 24-unit block space.
                constexpr float kHalf1 = 1.0f / 48.0f;
                constexpr float kHalf24 = 24.0f / 48.0f;
                (void)axis;
                return {kHalf24, kHalf1, kHalf24};
            }
            if (shape == NarrowShape::Book) {
                // Closed book footprint: 16x2x12 in 24-unit block space.
                constexpr float kHalf2 = 2.0f / 48.0f;
                constexpr float kHalf6 = 6.0f / 48.0f;
                constexpr float kHalf8 = 8.0f / 48.0f;
                switch (axis) {
                    case NarrowLogAxis::X: return {kHalf8, kHalf2, kHalf6};
                    case NarrowLogAxis::Z: return {kHalf6, kHalf2, kHalf8};
                    case NarrowLogAxis::Y:
                    default: return {kHalf8, kHalf2, kHalf6};
                }
            }
            if (shape == NarrowShape::Lantern) {
                // Lantern body profile: 9x24x9 in 24-unit block space.
                constexpr float kHalf9 = 9.0f / 48.0f;
                constexpr float kHalf24 = 24.0f / 48.0f;
                switch (axis) {
                    case NarrowLogAxis::X: return {kHalf24, kHalf9, kHalf9};
                    case NarrowLogAxis::Z: return {kHalf9, kHalf9, kHalf24};
                    case NarrowLogAxis::Y:
                    default: return {kHalf9, kHalf24, kHalf9};
                }
            }
            if (shape == NarrowShape::DepthCrystal) {
                // Thin upright card; custom layer offsets handle visual placement.
                constexpr float kHalf1 = 1.0f / 48.0f;
                constexpr float kHalf24 = 24.0f / 48.0f;
                (void)axis;
                return {kHalf24, kHalf24, kHalf1};
            }
            if (shape == NarrowShape::GrassCover || shape == NarrowShape::BlueprintMat || shape == NarrowShape::ChalkDust) {
                // Flat floor-mat profile: 24x1x24 in 24-unit block space.
                constexpr float kHalf1 = 1.0f / 48.0f;
                constexpr float kHalf24 = 24.0f / 48.0f;
                (void)axis;
                return {kHalf24, kHalf1, kHalf24};
            }
            if (shape == NarrowShape::WallDecal) {
                // Thin, wall-mounted decal profile for bouldering holds.
                constexpr float kHalf1 = 1.0f / 48.0f;
                constexpr float kHalf24 = 24.0f / 48.0f;
                if (mount == NarrowMount::WallPosX || mount == NarrowMount::WallNegX) {
                    return {kHalf1, kHalf24, kHalf24};
                }
                if (mount == NarrowMount::WallPosZ || mount == NarrowMount::WallNegZ) {
                    return {kHalf24, kHalf24, kHalf1};
                }
                if (mount == NarrowMount::Ceiling) {
                    return {kHalf24, kHalf1, kHalf24};
                }
                return {kHalf24, kHalf1, kHalf24};
            }
            if (shape == NarrowShape::CactusArm) {
                // Slightly shorter and taller than a side log so arm blocks read as a curved elbow.
                constexpr float kHalf12 = 12.0f / 48.0f;
                constexpr float kHalf16 = 16.0f / 48.0f;
                constexpr float kHalf24 = 24.0f / 48.0f;
                switch (axis) {
                    case NarrowLogAxis::X: return {kHalf24, kHalf16, kHalf12};
                    case NarrowLogAxis::Z: return {kHalf12, kHalf16, kHalf24};
                    case NarrowLogAxis::Y: return {kHalf12, kHalf16, kHalf12};
                    default: return {kHalf24, kHalf16, kHalf12};
                }
            }
            if (shape == NarrowShape::CactusJunction) {
                // Full-height trunk junction that widens toward arm axis to visually connect branches.
                constexpr float kHalf12 = 12.0f / 48.0f;
                constexpr float kHalf24 = 24.0f / 48.0f;
                switch (axis) {
                    case NarrowLogAxis::X: return {kHalf24, kHalf24, kHalf12};
                    case NarrowLogAxis::Z: return {kHalf12, kHalf24, kHalf24};
                    case NarrowLogAxis::Y: return {kHalf12, kHalf24, kHalf12};
                    default: return {kHalf12, kHalf24, kHalf12};
                }
            }

            // Cave stone profile: half the stick length and double the thickness (6x2x2).
            constexpr float kHalf2 = 2.0f / 48.0f;
            constexpr float kHalf6 = 6.0f / 48.0f;
            switch (axis) {
                case NarrowLogAxis::X: return {kHalf6, kHalf2, kHalf2};
                case NarrowLogAxis::Y: return {kHalf2, kHalf6, kHalf2};
                case NarrowLogAxis::Z: return {kHalf2, kHalf2, kHalf6};
                default: return {kHalf6, kHalf2, kHalf2};
            }
        }

        uint32_t hashCell3D(int x, int y, int z) {
            uint32_t h = static_cast<uint32_t>(x) * 73856093u;
            h ^= static_cast<uint32_t>(y) * 19349663u;
            h ^= static_cast<uint32_t>(z) * 83492791u;
            h ^= (h >> 13);
            h *= 1274126177u;
            h ^= (h >> 16);
            return h;
        }

        constexpr int kSurfaceStonePileMin = 1;
        constexpr int kSurfaceStonePileMax = 8;

        struct StonePebblePilePieces {
            int count = 0;
            std::array<glm::vec2, kSurfaceStonePileMax> offsets{};
            std::array<NarrowHalfExtents, kSurfaceStonePileMax> halfExtents{};
        };

        StonePebblePilePieces stonePebblePilePiecesForCell(const glm::ivec3& lodCoord, int requestedCount) {
            StonePebblePilePieces out;
            out.count = std::clamp(requestedCount, kSurfaceStonePileMin, kSurfaceStonePileMax);
            int placed = 0;
            constexpr float kPlacementPad = 1.0f / 96.0f;
            for (int i = 0; i < out.count; ++i) {
                const uint32_t sizeHash = hashCell3D(
                    lodCoord.x + i * 83,
                    lodCoord.y - i * 47,
                    lodCoord.z + i * 59
                );
                NarrowHalfExtents ext;
                ext.x = (2.0f + static_cast<float>(sizeHash & 3u)) / 48.0f;
                ext.z = (2.0f + static_cast<float>((sizeHash >> 2u) & 3u)) / 48.0f;
                ext.y = (1.0f + static_cast<float>((sizeHash >> 4u) % 3u)) / 48.0f;
                if ((sizeHash >> 6u) & 1u) {
                    std::swap(ext.x, ext.z);
                }
                if (out.count == 1) {
                    ext.x *= 1.35f;
                    ext.z *= 1.35f;
                    ext.y *= 1.20f;
                }

                bool placedThis = false;
                for (int attempt = 0; attempt < 12; ++attempt) {
                    const uint32_t h = hashCell3D(
                        lodCoord.x + i * 37 + attempt * 11,
                        lodCoord.y + i * 19 - attempt * 7,
                        lodCoord.z - i * 53 + attempt * 13
                    );
                    float ox = (static_cast<float>((h >> 8u) & 0xffu) / 255.0f - 0.5f) * 0.72f;
                    float oz = (static_cast<float>((h >> 16u) & 0xffu) / 255.0f - 0.5f) * 0.72f;
                    ox = std::clamp(ox, -0.5f + ext.x + kPlacementPad, 0.5f - ext.x - kPlacementPad);
                    oz = std::clamp(oz, -0.5f + ext.z + kPlacementPad, 0.5f - ext.z - kPlacementPad);

                    bool overlaps = false;
                    for (int j = 0; j < placed; ++j) {
                        const glm::vec2 prevOffset = out.offsets[static_cast<size_t>(j)];
                        const NarrowHalfExtents prevExt = out.halfExtents[static_cast<size_t>(j)];
                        if (std::abs(ox - prevOffset.x) < (ext.x + prevExt.x + kPlacementPad)
                            && std::abs(oz - prevOffset.y) < (ext.z + prevExt.z + kPlacementPad)) {
                            overlaps = true;
                            break;
                        }
                    }
                    if (overlaps) continue;

                    out.offsets[static_cast<size_t>(placed)] = glm::vec2(ox, oz);
                    out.halfExtents[static_cast<size_t>(placed)] = ext;
                    ++placed;
                    placedThis = true;
                    break;
                }

                if (!placedThis) {
                    const uint32_t h = hashCell3D(
                        lodCoord.x - i * 71,
                        lodCoord.y + i * 43,
                        lodCoord.z + i * 29
                    );
                    const float angle = (static_cast<float>(h & 1023u) / 1023.0f) * 6.2831853f + static_cast<float>(i) * 0.71f;
                    const float radius = 0.09f + 0.03f * static_cast<float>(i % 4);
                    float ox = std::cos(angle) * radius;
                    float oz = std::sin(angle) * radius;
                    ox = std::clamp(ox, -0.5f + ext.x + kPlacementPad, 0.5f - ext.x - kPlacementPad);
                    oz = std::clamp(oz, -0.5f + ext.z + kPlacementPad, 0.5f - ext.z - kPlacementPad);
                    out.offsets[static_cast<size_t>(placed)] = glm::vec2(ox, oz);
                    out.halfExtents[static_cast<size_t>(placed)] = ext;
                    ++placed;
                }
            }

            out.count = std::max(placed, 1);
            return out;
        }

        int wallDecalQuarterTurnsForCell(const glm::ivec3& lodCoord) {
            const uint32_t h = hashCell3D(lodCoord.x + 271, lodCoord.y - 593, lodCoord.z + 887);
            return static_cast<int>(h & 3u);
        }

        bool isDesertSandPrototype(const Entity& proto) {
            return proto.name.rfind("SandBlockDesertTex", 0) == 0;
        }

        int desertSandQuarterTurnsForCell(const glm::ivec3& lodCoord) {
            const uint32_t h = hashCell3D(lodCoord.x + 401, lodCoord.y - 829, lodCoord.z + 1663);
            return static_cast<int>(h & 3u);
        }

        glm::vec3 wallDecalPanOffsetForCell(const glm::ivec3& lodCoord, NarrowMount mount) {
            // Per-cell deterministic variation:
            // direction in {up, down, right, left}, magnitude in [1, 3] / 24 block units.
            const uint32_t h = hashCell3D(lodCoord.x - 431, lodCoord.y + 719, lodCoord.z + 157);
            const int direction = static_cast<int>(h & 3u);
            const int steps = 1 + static_cast<int>((h >> 6u) % 3u);
            const float amount = static_cast<float>(steps) / 24.0f;

            if (mount == NarrowMount::WallPosX || mount == NarrowMount::WallNegX) {
                if (direction == 0) return glm::vec3(0.0f, amount, 0.0f);
                if (direction == 1) return glm::vec3(0.0f, -amount, 0.0f);
                if (direction == 2) return glm::vec3(0.0f, 0.0f, amount);
                return glm::vec3(0.0f, 0.0f, -amount);
            }
            if (mount == NarrowMount::WallPosZ || mount == NarrowMount::WallNegZ) {
                if (direction == 0) return glm::vec3(0.0f, amount, 0.0f);
                if (direction == 1) return glm::vec3(0.0f, -amount, 0.0f);
                if (direction == 2) return glm::vec3(amount, 0.0f, 0.0f);
                return glm::vec3(-amount, 0.0f, 0.0f);
            }
            return glm::vec3(0.0f);
        }

        struct GrassCoverDots {
            int count = 0;
            std::array<glm::vec2, 48> offsets{};
        };

        GrassCoverDots grassCoverDotsForCell(const glm::ivec3& lodCoord) {
            GrassCoverDots out;
            constexpr int kMinDots = 36;
            constexpr int kMaxDots = 48;
            constexpr int kGridCellsPerAxis = 24;
            constexpr float kGridUnit = 1.0f / 24.0f;
            const uint32_t seed = hashCell3D(lodCoord.x + 913, lodCoord.y + 37, lodCoord.z - 211);
            out.count = kMinDots + static_cast<int>(seed % static_cast<uint32_t>(kMaxDots - kMinDots + 1));
            for (int i = 0; i < out.count; ++i) {
                const uint32_t h = hashCell3D(
                    lodCoord.x + i * 31,
                    lodCoord.y - i * 17,
                    lodCoord.z + i * 13
                );
                // Align mini-cube centers to the center of each 1/24 grid cell, not the grid lines.
                const int oxSlot = static_cast<int>(h % static_cast<uint32_t>(kGridCellsPerAxis));
                const int ozSlot = static_cast<int>((h >> 8u) % static_cast<uint32_t>(kGridCellsPerAxis));
                const float ox = (static_cast<float>(oxSlot) + 0.5f) * kGridUnit - 0.5f;
                const float oz = (static_cast<float>(ozSlot) + 0.5f) * kGridUnit - 0.5f;
                out.offsets[static_cast<size_t>(i)] = glm::vec2(
                    ox,
                    oz
                );
            }
            return out;
        }

        float floorMountYOffset(NarrowShape shape, const NarrowHalfExtents& ext) {
            if (shape == NarrowShape::PetalPile) {
                // Keep petals visibly above the supporting surface: 1/24 block lift.
                return -0.5f + ext.y + (1.0f / 24.0f);
            }
            if (shape == NarrowShape::Book) {
                // Keep book just above floor to avoid z-fighting with terrain.
                return -0.5f + ext.y + 0.012f;
            }
            return -0.5f + ext.y + 0.01f;
        }

        enum class SlopeDir : int {
            None = 0,
            PosX = 1,
            NegX = 2,
            PosZ = 3,
            NegZ = 4,
            PosXPosZ = 5,
            PosXNegZ = 6,
            NegXPosZ = 7,
            NegXNegZ = 8
        };

        SlopeDir slopeDirForPrototype(const Entity& proto) {
            if (proto.name == "DebugSlopeTexPosX") return SlopeDir::PosX;
            if (proto.name == "DebugSlopeTexNegX") return SlopeDir::NegX;
            if (proto.name == "DebugSlopeTexPosZ") return SlopeDir::PosZ;
            if (proto.name == "DebugSlopeTexNegZ") return SlopeDir::NegZ;
            if (proto.name == "WaterSlopePosX") return SlopeDir::PosX;
            if (proto.name == "WaterSlopeNegX") return SlopeDir::NegX;
            if (proto.name == "WaterSlopePosZ") return SlopeDir::PosZ;
            if (proto.name == "WaterSlopeNegZ") return SlopeDir::NegZ;
            if (proto.name == "WaterSlopeCornerPosXPosZ") return SlopeDir::PosXPosZ;
            if (proto.name == "WaterSlopeCornerPosXNegZ") return SlopeDir::PosXNegZ;
            if (proto.name == "WaterSlopeCornerNegXPosZ") return SlopeDir::NegXPosZ;
            if (proto.name == "WaterSlopeCornerNegXNegZ") return SlopeDir::NegXNegZ;
            return SlopeDir::None;
        }

        bool isSlopePrototype(const Entity& proto) {
            return slopeDirForPrototype(proto) != SlopeDir::None;
        }

        bool isWaterSlopePrototype(const Entity& proto) {
            return proto.name == "WaterSlopePosX"
                || proto.name == "WaterSlopeNegX"
                || proto.name == "WaterSlopePosZ"
                || proto.name == "WaterSlopeNegZ"
                || proto.name == "WaterSlopeCornerPosXPosZ"
                || proto.name == "WaterSlopeCornerPosXNegZ"
                || proto.name == "WaterSlopeCornerNegXPosZ"
                || proto.name == "WaterSlopeCornerNegXNegZ";
        }

        constexpr float kSlopeCapAlphaA = -4.0f;
        constexpr float kSlopeCapAlphaB = -5.0f;
        constexpr float kSlopeTopAlphaPosX = -6.0f;
        constexpr float kSlopeTopAlphaNegX = -7.0f;
        constexpr float kSlopeTopAlphaPosZ = -8.0f;
        constexpr float kSlopeTopAlphaNegZ = -9.0f;
        constexpr float kWaterSlopeSurfaceAlpha = 0.6f;
        constexpr float kWaterSlopeCapAlphaA = -24.0f;
        constexpr float kWaterSlopeCapAlphaB = -25.0f;
        constexpr float kWaterSlopeTopAlphaPosX = -26.0f;
        constexpr float kWaterSlopeTopAlphaNegX = -27.0f;
        constexpr float kWaterSlopeTopAlphaPosZ = -28.0f;
        constexpr float kWaterSlopeTopAlphaNegZ = -29.0f;
        constexpr float kWaterSlopeTopAlphaPosXPosZ = -30.0f;
        constexpr float kWaterSlopeTopAlphaPosXNegZ = -31.0f;
        constexpr float kWaterSlopeTopAlphaNegXPosZ = -32.0f;
        constexpr float kWaterSlopeTopAlphaNegXNegZ = -33.0f;
        constexpr float kBookPageAlpha = -40.0f;
        constexpr float kGrassCoverAlpha = -14.0f;
        constexpr float kChalkDustAlpha = -15.0f;
        constexpr float kWallDecalAlpha = -16.0f;
        constexpr float kLavaSurfaceAlpha = 0.6f;
        constexpr int kBookPageTileIndex = 56;
        constexpr float kUnderwaterCausticsAoFlag = 2.0f;
        constexpr float kWaterShorelineAoFlag = 4.0f;
        constexpr float kWaterWaveClassAoEncodeStride = 8.0f;
        constexpr float kWaterfallFoamAoFlag = 64.0f;
        constexpr int kChalkTileEncodeStride = 2048;
        constexpr int kChalkDustTileCorner = 186;
        constexpr int kChalkDustTileCross = 187;
        constexpr int kChalkDustTileDot = 188;
        constexpr int kChalkDustTileEnd = 189;
        constexpr int kChalkDustTileStraight = 190;
        constexpr int kChalkDustTileT = 191;
        struct DepthCrystalLayerSpec {
            int tileIndex = -1;
            float bottomPaddingPx = 0.0f;
            float offsetXPx = 0.0f;
            float offsetYPx = 0.0f;
            float offsetZPx = 0.0f;
            bool useBottomPadding = true;
        };
        struct DepthCrystalLayerSet {
            const DepthCrystalLayerSpec* layers = nullptr;
            int count = 0;
        };
        // Bottom alpha padding measured from atlas_v8 tiles (24x24):
        // 462 => 8px, 466 => 3px, 467 => 10px.
        constexpr std::array<DepthCrystalLayerSpec, 3> kDepthCrystalLayers = {{
            {462, 8.0f},
            {466, 3.0f},
            {467, 10.0f}
        }};
        // Blue cluster:
        // 463 => 8px, 464 => 2px.
        constexpr std::array<DepthCrystalLayerSpec, 2> kDepthCrystalBlueLayers = {{
            {463, 8.0f},
            {464, 2.0f}
        }};
        // Big blue cluster: 2x2 crystal tiles (489-492) + small accents (493-496).
        // Big crystal group gets a left offset of 11px as requested.
        constexpr std::array<DepthCrystalLayerSpec, 8> kDepthCrystalBlueBigLayers = {{
            // Topper pair sits one block above the base pair, but inherits the base inset.
            {489, 0.0f, -11.0f, 20.0f, -4.0f, false}, // top-left
            // Right-half tiles should sit one tile-width to the right of the shared anchor.
            {490, 0.0f, 13.0f, 20.0f, -4.0f, false}, // top-right
            {491, 4.0f, -11.0f}, // bottom-left
            {492, 4.0f, 13.0f}, // bottom-right
            {493, 9.0f, 0.0f},
            {494, 5.0f, 0.0f},
            {495, 9.0f, 0.0f},
            {496, 3.0f, 0.0f}
        }};
        // Big magenta cluster:
        // topper: 497 (above/right, matching base inset)
        // base: 498 (left), 499 (right)
        // accents: 500-503
        constexpr std::array<DepthCrystalLayerSpec, 7> kDepthCrystalMagentaBigLayers = {{
            {497, 0.0f, 13.0f, 21.0f, -3.0f, false}, // topper over right base (499)
            {498, 3.0f, -11.0f}, // base-left
            {499, 3.0f, 13.0f}, // base-right
            {500, 11.0f, 0.0f},
            {501, 11.0f, 0.0f},
            {502, 2.0f, 0.0f},
            {503, 6.0f, 0.0f}
        }};

        DepthCrystalLayerSet depthCrystalLayersForPrototype(const Entity& proto) {
            if (proto.name == "DepthCrystalClusterMagentaBigTex") {
                return DepthCrystalLayerSet{ kDepthCrystalMagentaBigLayers.data(), static_cast<int>(kDepthCrystalMagentaBigLayers.size()) };
            }
            if (proto.name == "DepthCrystalClusterBlueBigTex") {
                return DepthCrystalLayerSet{ kDepthCrystalBlueBigLayers.data(), static_cast<int>(kDepthCrystalBlueBigLayers.size()) };
            }
            if (proto.name == "DepthCrystalClusterBlueTex") {
                return DepthCrystalLayerSet{ kDepthCrystalBlueLayers.data(), static_cast<int>(kDepthCrystalBlueLayers.size()) };
            }
            return DepthCrystalLayerSet{ kDepthCrystalLayers.data(), static_cast<int>(kDepthCrystalLayers.size()) };
        }

        int decodeGrassCoverSnapshotTile(uint32_t packedColor) {
            const int encoded = static_cast<int>((packedColor >> 24) & 0xffu);
            if (encoded <= 0) return -1;
            // Water metadata uses [waveClass(4 bits) | marker(4 bits)] in this byte.
            const int marker = encoded & 0x0f;
            const int waveClass = (encoded >> 4) & 0x0f;
            if (marker <= 5 && waveClass <= 4) return -1;
            return encoded - 1;
        }

        int decodeSurfaceStonePileCount(uint32_t packedColor) {
            const int encoded = static_cast<int>((packedColor >> 24) & 0xffu);
            if (encoded <= 0) return kSurfaceStonePileMin;
            return std::clamp(encoded, kSurfaceStonePileMin, kSurfaceStonePileMax);
        }

        constexpr uint8_t kWaterFoliageMarkerNone = 0u;
        constexpr uint8_t kWaterFoliageMarkerKelp = 1u;
        constexpr uint8_t kWaterFoliageMarkerSeaUrchinX = 2u;
        constexpr uint8_t kWaterFoliageMarkerSeaUrchinZ = 3u;
        constexpr uint8_t kWaterFoliageMarkerSandDollarX = 4u;
        constexpr uint8_t kWaterFoliageMarkerSandDollarZ = 5u;
        constexpr uint8_t kWaterWaveClassUnknown = 0u;
        constexpr uint8_t kWaterWaveClassPond = 1u;
        constexpr uint8_t kWaterWaveClassLake = 2u;
        constexpr uint8_t kWaterWaveClassRiver = 3u;
        constexpr uint8_t kWaterWaveClassOcean = 4u;

        uint8_t decodeWaterFoliageMarker(uint32_t packedColor) {
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

        uint8_t decodeWaterWaveClass(uint32_t packedColor) {
            const uint8_t encoded = static_cast<uint8_t>((packedColor >> 24) & 0xffu);
            if (encoded <= kWaterFoliageMarkerSandDollarZ) {
                // Legacy marker-only format carries no wave class.
                return kWaterWaveClassUnknown;
            }
            const uint8_t marker = static_cast<uint8_t>(encoded & 0x0fu);
            const uint8_t waveClass = static_cast<uint8_t>((encoded >> 4u) & 0x0fu);
            if (marker <= kWaterFoliageMarkerSandDollarZ && waveClass <= kWaterWaveClassOcean) {
                return waveClass;
            }
            return kWaterWaveClassUnknown;
        }

        int chooseChalkDustTileFromNeighbors(bool north, bool east, bool south, bool west, int& outQuarterTurns) {
            const int count = static_cast<int>(north) + static_cast<int>(east)
                + static_cast<int>(south) + static_cast<int>(west);
            outQuarterTurns = 0;
            if (count <= 0) return kChalkDustTileDot;
            if (count == 4) return kChalkDustTileCross;

            if (count == 1) {
                if (west) outQuarterTurns = 0;
                else if (north) outQuarterTurns = 1;
                else if (east) outQuarterTurns = 2;
                else outQuarterTurns = 3;
                return kChalkDustTileEnd;
            }

            if (count == 2) {
                if (west && east) {
                    outQuarterTurns = 0;
                    return kChalkDustTileStraight;
                }
                if (north && south) {
                    outQuarterTurns = 1;
                    return kChalkDustTileStraight;
                }
                // Corner base orientation (turn=0) is south+east (bottom+right).
                if (south && east) outQuarterTurns = 0;
                else if (west && south) outQuarterTurns = 1;
                else if (west && north) outQuarterTurns = 2;
                else outQuarterTurns = 3; // north+east
                outQuarterTurns = (outQuarterTurns + 2) & 3;
                return kChalkDustTileCorner;
            }

            if (!north) outQuarterTurns = 0;
            else if (!east) outQuarterTurns = 1;
            else if (!south) outQuarterTurns = 2;
            else outQuarterTurns = 3;
            outQuarterTurns = (outQuarterTurns + 2) & 3;
            return kChalkDustTileT;
        }

        float slopeTopAlpha(SlopeDir dir) {
            switch (dir) {
                case SlopeDir::PosX: return kSlopeTopAlphaPosX;
                case SlopeDir::NegX: return kSlopeTopAlphaNegX;
                case SlopeDir::PosZ: return kSlopeTopAlphaPosZ;
                case SlopeDir::NegZ: return kSlopeTopAlphaNegZ;
                default: return kSlopeTopAlphaPosX;
            }
        }

        float waterSlopeTopAlpha(SlopeDir dir) {
            switch (dir) {
                case SlopeDir::PosX: return kWaterSlopeTopAlphaPosX;
                case SlopeDir::NegX: return kWaterSlopeTopAlphaNegX;
                case SlopeDir::PosZ: return kWaterSlopeTopAlphaPosZ;
                case SlopeDir::NegZ: return kWaterSlopeTopAlphaNegZ;
                case SlopeDir::PosXPosZ: return kWaterSlopeTopAlphaPosXPosZ;
                case SlopeDir::PosXNegZ: return kWaterSlopeTopAlphaPosXNegZ;
                case SlopeDir::NegXPosZ: return kWaterSlopeTopAlphaNegXPosZ;
                case SlopeDir::NegXNegZ: return kWaterSlopeTopAlphaNegXNegZ;
                default: return kWaterSlopeTopAlphaPosX;
            }
        }

        bool isCornerSlopeDir(SlopeDir dir) {
            return dir == SlopeDir::PosXPosZ
                || dir == SlopeDir::PosXNegZ
                || dir == SlopeDir::NegXPosZ
                || dir == SlopeDir::NegXNegZ;
        }

        int slopeTallFace(SlopeDir dir) {
            switch (dir) {
                case SlopeDir::PosX: return 0;
                case SlopeDir::NegX: return 1;
                case SlopeDir::PosZ: return 4;
                case SlopeDir::NegZ: return 5;
                default: return 0;
            }
        }

        void slopeCapFacesAndAlpha(SlopeDir dir, int& outFaceA, float& outAlphaA, int& outFaceB, float& outAlphaB) {
            switch (dir) {
                case SlopeDir::PosX:
                    outFaceA = 4; outAlphaA = kSlopeCapAlphaA;
                    outFaceB = 5; outAlphaB = kSlopeCapAlphaB;
                    break;
                case SlopeDir::NegX:
                    outFaceA = 4; outAlphaA = kSlopeCapAlphaB;
                    outFaceB = 5; outAlphaB = kSlopeCapAlphaA;
                    break;
                case SlopeDir::PosZ:
                    outFaceA = 0; outAlphaA = kSlopeCapAlphaB;
                    outFaceB = 1; outAlphaB = kSlopeCapAlphaA;
                    break;
                case SlopeDir::NegZ:
                    outFaceA = 0; outAlphaA = kSlopeCapAlphaA;
                    outFaceB = 1; outAlphaB = kSlopeCapAlphaB;
                    break;
                default:
                    outFaceA = 4; outAlphaA = kSlopeCapAlphaA;
                    outFaceB = 5; outAlphaB = kSlopeCapAlphaB;
                    break;
            }
        }

        void waterSlopeCapFacesAndAlpha(SlopeDir dir, int& outFaceA, float& outAlphaA, int& outFaceB, float& outAlphaB) {
            switch (dir) {
                case SlopeDir::PosX:
                    outFaceA = 4; outAlphaA = kWaterSlopeCapAlphaA;
                    outFaceB = 5; outAlphaB = kWaterSlopeCapAlphaB;
                    break;
                case SlopeDir::NegX:
                    outFaceA = 4; outAlphaA = kWaterSlopeCapAlphaB;
                    outFaceB = 5; outAlphaB = kWaterSlopeCapAlphaA;
                    break;
                case SlopeDir::PosZ:
                    outFaceA = 0; outAlphaA = kWaterSlopeCapAlphaB;
                    outFaceB = 1; outAlphaB = kWaterSlopeCapAlphaA;
                    break;
                case SlopeDir::NegZ:
                    outFaceA = 0; outAlphaA = kWaterSlopeCapAlphaA;
                    outFaceB = 1; outAlphaB = kWaterSlopeCapAlphaB;
                    break;
                default:
                    outFaceA = 4; outAlphaA = kWaterSlopeCapAlphaA;
                    outFaceB = 5; outAlphaB = kWaterSlopeCapAlphaB;
                    break;
            }
        }

        bool isSolidOccluderType(int type) {
            // Slopes are not full cubes; do not let them cull neighboring faces.
            return type == 1;
        }

        glm::ivec3 faceUAxisForAo(int faceType) {
            switch (faceType) {
                case 0: return glm::ivec3(0, 0, 1);   // +X
                case 1: return glm::ivec3(0, 0, -1);  // -X
                case 2: return glm::ivec3(-1, 0, 0);  // +Y
                case 3: return glm::ivec3(-1, 0, 0);  // -Y
                case 4: return glm::ivec3(1, 0, 0);   // +Z
                case 5: return glm::ivec3(-1, 0, 0);  // -Z
                default: return glm::ivec3(1, 0, 0);
            }
        }

        glm::ivec3 faceVAxisForAo(int faceType) {
            switch (faceType) {
                case 0: return glm::ivec3(0, 1, 0);   // +X
                case 1: return glm::ivec3(0, 1, 0);   // -X
                case 2: return glm::ivec3(0, 0, -1);  // +Y
                case 3: return glm::ivec3(0, 0, 1);   // -Y
                case 4: return glm::ivec3(0, 1, 0);   // +Z
                case 5: return glm::ivec3(0, 1, 0);   // -Z
                default: return glm::ivec3(0, 1, 0);
            }
        }

        float bakedAoValue(bool side1, bool side2, bool corner) {
            int occlusion = (side1 && side2) ? 3 : (static_cast<int>(side1) + static_cast<int>(side2) + static_cast<int>(corner));
            // Slightly softer AO to avoid over-darkening adjacent block faces.
            static const float kAoLut[4] = {1.0f, 0.90f, 0.78f, 0.66f};
            return kAoLut[occlusion];
        }

        float applyAoStrength(float aoValue, float strength) {
            // 0.0 = no AO (all 1.0), 1.0 = full AO.
            return 1.0f + (aoValue - 1.0f) * strength;
        }

        glm::ivec3 slopeHighDirection(SlopeDir dir) {
            switch (dir) {
                case SlopeDir::PosX: return glm::ivec3(1, 0, 0);
                case SlopeDir::NegX: return glm::ivec3(-1, 0, 0);
                case SlopeDir::PosZ: return glm::ivec3(0, 0, 1);
                case SlopeDir::NegZ: return glm::ivec3(0, 0, -1);
                default: return glm::ivec3(0, 0, 0);
            }
        }

        glm::vec4 aoMirrorU(const glm::vec4& ao) {
            // (u,v): 00,10,11,01 -> mirror U -> 10,00,01,11
            return glm::vec4(ao.y, ao.x, ao.w, ao.z);
        }

        glm::vec4 aoMirrorV(const glm::vec4& ao) {
            // (u,v): 00,10,11,01 -> mirror V -> 01,11,10,00
            return glm::vec4(ao.w, ao.z, ao.y, ao.x);
        }

        glm::vec4 aoSwapUV(const glm::vec4& ao) {
            // (u,v): 00,10,11,01 -> swap UV -> 00,01,11,10
            return glm::vec4(ao.x, ao.w, ao.z, ao.y);
        }

        glm::vec4 remapSlopeTopAoForDir(const glm::vec4& ao, SlopeDir dir) {
            switch (dir) {
                case SlopeDir::PosX: return ao;
                case SlopeDir::NegX: return aoMirrorU(ao);
                case SlopeDir::PosZ: return aoSwapUV(ao);
                case SlopeDir::NegZ: return aoMirrorV(aoSwapUV(ao));
                default: return ao;
            }
        }

        bool BuildVoxelGreedyMesh(BaseSystem& baseSystem,
                                  std::vector<Entity>& prototypes,
                                  const VoxelSectionKey& sectionKey,
                                  bool disableAo = false) {
            if (!baseSystem.voxelWorld || !baseSystem.voxelGreedy) return true;
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            VoxelGreedyContext& voxelGreedy = *baseSystem.voxelGreedy;
            auto secIt = voxelWorld.sections.find(sectionKey);
            if (secIt == voxelWorld.sections.end()) {
                releaseGreedyChunkIfPresent(voxelGreedy, sectionKey);
                return true;
            }
            const VoxelSection& section = secIt->second;
            int superChunkMinLod = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkMinLod", 3);
            int superChunkMaxLod = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkMaxLod", 3);
            int superChunkSize = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkSize", 1);
            if (superChunkSize < 1) superChunkSize = 1;
            bool useSuperChunk = section.lod >= superChunkMinLod
                && section.lod <= superChunkMaxLod
                && superChunkSize > 1;
            glm::ivec3 anchorCoord = section.coord;
            if (useSuperChunk) {
                anchorCoord.x = VoxelMeshInitSystemLogic::FloorDivInt(section.coord.x, superChunkSize) * superChunkSize;
                anchorCoord.z = VoxelMeshInitSystemLogic::FloorDivInt(section.coord.z, superChunkSize) * superChunkSize;
            }
            VoxelSectionKey renderKey{section.lod, anchorCoord};
            if (!(sectionKey == renderKey)) {
                releaseGreedyChunkIfPresent(voxelGreedy, sectionKey);
            }
            if (useSuperChunk) {
                for (int oz = 0; oz < superChunkSize; ++oz) {
                    for (int ox = 0; ox < superChunkSize; ++ox) {
                        VoxelSectionKey key{section.lod, glm::ivec3(anchorCoord.x + ox,
                                                                   anchorCoord.y,
                                                                   anchorCoord.z + oz)};
                        if (voxelWorld.sections.find(key) == voxelWorld.sections.end()) {
                            return false;
                        }
                    }
                }
            }
            if (section.nonAirCount <= 0) {
                releaseGreedyChunkIfPresent(voxelGreedy, renderKey);
                return true;
            }

            int sizeX = section.size * (useSuperChunk ? superChunkSize : 1);
            int sizeY = section.size;
            int sizeZ = section.size * (useSuperChunk ? superChunkSize : 1);
            int scale = 1 << section.lod;
            glm::ivec3 minCoord = anchorCoord * section.size;
            const float aoStrength = std::clamp(
                ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "voxelAoStrength", 1.0f),
                0.0f,
                1.0f
            );
            const bool leafAoEnabled = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "LeafAoEnabled", true);
            const float leafAoStrength = std::clamp(
                ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "LeafAoStrength", 1.0f),
                0.0f,
                1.0f
            );
            const bool plantAoEnabled = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "PlantAoEnabled", false);
            const float plantAoStrength = std::clamp(
                ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "PlantAoStrength", 1.0f),
                0.0f,
                1.0f
            );
            const bool aoDisabled = disableAo
                || ::RenderInitSystemLogic::getRegistryBool(baseSystem, "voxelDisableAo", false)
                || aoStrength <= 0.0001f;
            const bool voxelLightingEnabled = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "VoxelLightingEnabled", true);
            const bool voxelLightingAffectWater = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "VoxelLightingAffectWater", true);
            const float voxelLightingStrength = std::clamp(
                ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "VoxelLightingStrength", 1.0f),
                0.0f,
                1.0f
            );
            const float voxelLightingMinBrightness = std::clamp(
                ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "VoxelLightingMinBrightness", 0.08f),
                0.0f,
                1.0f
            );
            const float voxelLightingGamma = std::clamp(
                ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "VoxelLightingGamma", 1.35f),
                0.25f,
                4.0f
            );
            const int voxelLightingDebugMode = std::clamp(
                ::RenderInitSystemLogic::getRegistryInt(baseSystem, "VoxelLightingDebugMode", 0),
                0,
                4
            );
            const uint8_t voxelLightingSkyFallbackLevel = resolveSkyFallbackLevel(baseSystem, voxelWorld);

            struct CellInfo {
                bool filled = false;
                bool isLeaf = false;
                PlantType plantType = PlantType::None;
                SlopeDir slopeDir = SlopeDir::None;
                int protoID = -1;
                glm::vec3 color = glm::vec3(1.0f);
                uint32_t packedColor = 0;
            };
            struct MaskCell {
                bool filled = false;
                bool isLeaf = false;
                PlantType plantType = PlantType::None;
                bool narrowLog = false;
                NarrowShape narrowShape = NarrowShape::Default;
                NarrowMount narrowMount = NarrowMount::Floor;
                NarrowLogAxis narrowAxis = NarrowLogAxis::None;
                glm::ivec3 cellCoord = glm::ivec3(0);
                int protoID = -1;
                int tileIndex = -1;
                uint32_t packedColor = 0;
                float alpha = 1.0f;
                glm::vec3 color = glm::vec3(1.0f);
                glm::vec4 ao = glm::vec4(1.0f);
            };

            auto cellIndex = [&](const glm::ivec3& local) {
                return (local.x * sizeY + local.y) * sizeZ + local.z;
            };
            auto inBounds = [&](const glm::ivec3& local) {
                return local.x >= 0 && local.y >= 0 && local.z >= 0
                    && local.x < sizeX && local.y < sizeY && local.z < sizeZ;
            };
            auto lightFactorFromLevel = [&](uint8_t level) {
                const float normalized = std::clamp(static_cast<float>(level) / 15.0f, 0.0f, 1.0f);
                const float curve = std::pow(normalized, voxelLightingGamma);
                const float factor = voxelLightingMinBrightness + (1.0f - voxelLightingMinBrightness) * curve;
                return 1.0f + (factor - 1.0f) * voxelLightingStrength;
            };
            auto lightLevelsAt = [&](const glm::ivec3& lodCoord) -> std::array<uint8_t, 2> {
                if (!voxelLightingEnabled || section.lod != 0) return {voxelLightingSkyFallbackLevel, static_cast<uint8_t>(0)};
                const int sectionSize = VoxelMeshInitSystemLogic::SectionSizeForLod(voxelWorld, section.lod);
                glm::ivec3 sectionCoord(
                    VoxelMeshInitSystemLogic::FloorDivInt(lodCoord.x, sectionSize),
                    VoxelMeshInitSystemLogic::FloorDivInt(lodCoord.y, sectionSize),
                    VoxelMeshInitSystemLogic::FloorDivInt(lodCoord.z, sectionSize)
                );
                VoxelSectionKey lightKey{section.lod, sectionCoord};
                auto it = voxelWorld.sections.find(lightKey);
                if (it == voxelWorld.sections.end()) return {voxelLightingSkyFallbackLevel, static_cast<uint8_t>(0)};
                const VoxelSection& lightSection = it->second;
                glm::ivec3 local = lodCoord - sectionCoord * sectionSize;
                int idx = local.x + local.y * lightSection.size + local.z * lightSection.size * lightSection.size;
                if (idx < 0) return {voxelLightingSkyFallbackLevel, static_cast<uint8_t>(0)};
                uint8_t sky = voxelLightingSkyFallbackLevel;
                uint8_t block = static_cast<uint8_t>(0);
                if (idx < static_cast<int>(lightSection.skyLight.size())) {
                    sky = lightSection.skyLight[static_cast<size_t>(idx)];
                }
                if (idx < static_cast<int>(lightSection.blockLight.size())) {
                    block = lightSection.blockLight[static_cast<size_t>(idx)];
                }
                return {sky, block};
            };
            auto applyLightingDebugColor = [&](const glm::vec3& fallbackColor,
                                               uint8_t skyLevel,
                                               uint8_t blockLevel) {
                if (voxelLightingDebugMode <= 0 || section.lod != 0) return fallbackColor;
                const float skyN = std::clamp(static_cast<float>(skyLevel) / 15.0f, 0.0f, 1.0f);
                const float blockN = std::clamp(static_cast<float>(blockLevel) / 15.0f, 0.0f, 1.0f);
                switch (voxelLightingDebugMode) {
                    case 1: {
                        const float v = std::max(skyN, blockN);
                        return glm::vec3(v);
                    }
                    case 2:
                        return glm::vec3(0.10f * skyN, 0.35f * skyN, 1.0f * skyN);
                    case 3:
                        return glm::vec3(1.0f * blockN, 0.45f * blockN, 0.08f * blockN);
                    case 4:
                        return glm::vec3(blockN, 0.08f * (skyN + blockN), skyN);
                    default:
                        return fallbackColor;
                }
            };

            std::vector<CellInfo> solidCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> waterCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> leafCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> plantCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> waterloggedFoliageCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> slopeCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            const bool leafFanRenderInnerBlock = ::RenderInitSystemLogic::getRegistryBool(
                baseSystem,
                "LeafFanRenderInnerBlock",
                true
            );
            const int kelpPrototypeID = findPrototypeIDByName(prototypes, "GrassTuftKelp");
            const int kelpTileIndex = (kelpPrototypeID >= 0)
                ? ::RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), prototypes[static_cast<size_t>(kelpPrototypeID)], 0)
                : -1;
            const int seaUrchinPrototypeIDX = findPrototypeIDByName(prototypes, "StonePebbleSeaUrchinTexX");
            const int seaUrchinPrototypeIDZ = findPrototypeIDByName(prototypes, "StonePebbleSeaUrchinTexZ");
            const int sandDollarPrototypeIDX = findPrototypeIDByName(prototypes, "StonePebbleSandDollarTexX");
            const int sandDollarPrototypeIDZ = findPrototypeIDByName(prototypes, "StonePebbleSandDollarTexZ");
            const int pineLeafPrototypeID = findPrototypeIDByName(prototypes, "Leaf");
            const int oakLeafPrototypeID = findPrototypeIDByName(prototypes, "LeafJungleV001");

            auto classifyProto = [&](uint32_t id, int& outType) {
                if (id == 0 || id >= prototypes.size()) { outType = 0; return; }
                const Entity& proto = prototypes[id];
                if (!proto.isBlock) {
                    outType = 0;
                } else if (proto.name == "Water") {
                    outType = 2;
                } else if (isSlopePrototype(proto)) {
                    outType = 6;
                } else if (isLeafPrototype(proto)) {
                    outType = 3;
                } else if (isPlantPrototype(proto)) {
                    outType = 5;
                } else if (isNarrowLogPrototype(proto)) {
                    outType = 4;
                } else {
                    // Type 1 is reserved for full-cube occluders used by face culling/AO.
                    // Non-solid or non-opaque blocks should still render, but they must not
                    // cull neighboring faces (otherwise semi-alpha texels reveal sky gaps).
                    outType = (proto.isSolid && proto.isOpaque) ? 1 : 7;
                }
            };
            auto solidProtoNeighborType = [&](int protoID) {
                if (protoID < 0 || protoID >= static_cast<int>(prototypes.size())) return 0;
                const Entity& solidProto = prototypes[static_cast<size_t>(protoID)];
                if (isNarrowLogPrototype(solidProto)) return 4;
                return (solidProto.isSolid && solidProto.isOpaque) ? 1 : 7;
            };

            for (int z = 0; z < sizeZ; ++z) {
                for (int y = 0; y < sizeY; ++y) {
                    for (int x = 0; x < sizeX; ++x) {
                        glm::ivec3 lodCoord = minCoord + glm::ivec3(x, y, z);
                        uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, section.lod, lodCoord);
                        if (id == 0 || id >= prototypes.size()) continue;
                        const Entity& proto = prototypes[id];
                        int idx = cellIndex(glm::ivec3(x, y, z));
                        uint32_t packedColor = VoxelMeshInitSystemLogic::GetVoxelColorAtLod(voxelWorld, section.lod, lodCoord);
                        glm::vec3 color = VoxelMeshInitSystemLogic::UnpackColor(packedColor);
                        if (!proto.isBlock) continue;
                        if (proto.name == "Water") {
                            waterCells[idx] = {true, false, PlantType::None, SlopeDir::None, static_cast<int>(id), color, packedColor};
                            uint8_t marker = decodeWaterFoliageMarker(packedColor);
                            if (marker == kWaterFoliageMarkerNone
                                && kelpPrototypeID >= 0
                                && kelpTileIndex >= 0
                                && decodeGrassCoverSnapshotTile(packedColor) == kelpTileIndex) {
                                marker = kWaterFoliageMarkerKelp; // legacy migration support
                            }

                            int overlayProtoID = -1;
                            PlantType overlayPlantType = PlantType::None;
                            if (marker == kWaterFoliageMarkerKelp && kelpPrototypeID >= 0) {
                                overlayProtoID = kelpPrototypeID;
                                overlayPlantType = PlantType::GrassTall;
                            } else if (marker == kWaterFoliageMarkerSeaUrchinX && seaUrchinPrototypeIDX >= 0) {
                                overlayProtoID = seaUrchinPrototypeIDX;
                                overlayPlantType = PlantType::Flower;
                            } else if (marker == kWaterFoliageMarkerSeaUrchinZ && seaUrchinPrototypeIDZ >= 0) {
                                overlayProtoID = seaUrchinPrototypeIDZ;
                                overlayPlantType = PlantType::Flower;
                            } else if (marker == kWaterFoliageMarkerSandDollarX && sandDollarPrototypeIDX >= 0) {
                                overlayProtoID = sandDollarPrototypeIDX;
                                overlayPlantType = PlantType::None;
                            } else if (marker == kWaterFoliageMarkerSandDollarZ && sandDollarPrototypeIDZ >= 0) {
                                overlayProtoID = sandDollarPrototypeIDZ;
                                overlayPlantType = PlantType::None;
                            }

                            if (overlayProtoID >= 0) {
                                waterloggedFoliageCells[idx] = {
                                    true,
                                    false,
                                    overlayPlantType,
                                    SlopeDir::None,
                                    overlayProtoID,
                                    glm::vec3(1.0f),
                                    packedColor
                                };
                            }
                        } else if (isSlopePrototype(proto)) {
                            slopeCells[idx] = {true, false, PlantType::None, slopeDirForPrototype(proto), static_cast<int>(id), color, packedColor};
                        } else {
                            const bool fanLeaf = isLeafFanPlantPrototype(proto);
                            const bool renderAsLeaf = isLeafPrototype(proto) && (!fanLeaf || leafFanRenderInnerBlock);
                            const bool renderAsPlant = isPlantPrototype(proto);
                            if (renderAsLeaf) {
                                leafCells[idx] = {true, true, PlantType::None, SlopeDir::None, static_cast<int>(id), color, packedColor};
                            }
                            if (renderAsPlant) {
                                plantCells[idx] = {true, false, plantTypeForPrototype(proto), SlopeDir::None, static_cast<int>(id), color, packedColor};
                            }
                            if (!renderAsLeaf && !renderAsPlant) {
                                solidCells[idx] = {true, false, PlantType::None, SlopeDir::None, static_cast<int>(id), color, packedColor};
                            }
                        }
                    }
                }
            }

            auto neighborTypeAt = [&](const glm::ivec3& lodCoord) {
                glm::ivec3 local = lodCoord - minCoord;
                int type = 0;
                if (inBounds(local)) {
                    int idx = cellIndex(local);
                    if (solidCells[idx].filled) {
                        return solidProtoNeighborType(solidCells[idx].protoID);
                    }
                    if (slopeCells[idx].filled) return 6;
                    if (waterCells[idx].filled) return 2;
                    if (leafCells[idx].filled) return 3;
                    if (plantCells[idx].filled) return 5;
                    uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, section.lod, lodCoord);
                    classifyProto(id, type);
                    return type;
                }
                uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, section.lod, lodCoord);
                classifyProto(id, type);
                return type;
            };
            auto oppositeFaceType = [](int faceType) {
                switch (faceType) {
                    case 0: return 1;
                    case 1: return 0;
                    case 2: return 3;
                    case 3: return 2;
                    case 4: return 5;
                    case 5: return 4;
                    default: return faceType;
                }
            };
            auto slopeDirAt = [&](const glm::ivec3& lodCoord) {
                glm::ivec3 local = lodCoord - minCoord;
                if (inBounds(local)) {
                    const int idx = cellIndex(local);
                    if (slopeCells[idx].filled) return slopeCells[idx].slopeDir;
                }
                const uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, section.lod, lodCoord);
                if (id == 0 || id >= prototypes.size()) return SlopeDir::None;
                return slopeDirForPrototype(prototypes[static_cast<size_t>(id)]);
            };
            auto slopeOccludesSharedFace = [&](SlopeDir dir, int sharedFaceTypeFromCurrent) {
                if (dir == SlopeDir::None) return false;
                const int slopeFaceType = oppositeFaceType(sharedFaceTypeFromCurrent);
                if (isCornerSlopeDir(dir)) return true;
                if (slopeFaceType == 2 || slopeFaceType == 3) return true;
                if (slopeFaceType == slopeTallFace(dir)) return true;
                int capFaceA = 4;
                int capFaceB = 5;
                float capAlphaA = 0.0f;
                float capAlphaB = 0.0f;
                waterSlopeCapFacesAndAlpha(dir, capFaceA, capAlphaA, capFaceB, capAlphaB);
                (void)capAlphaA;
                (void)capAlphaB;
                return slopeFaceType == capFaceA || slopeFaceType == capFaceB;
            };
            auto waterSlopeNeighborOccludesFace = [&](const glm::ivec3& slopeCoord, int sharedFaceTypeFromCurrent) {
                const SlopeDir dir = slopeDirAt(slopeCoord);
                return slopeOccludesSharedFace(dir, sharedFaceTypeFromCurrent);
            };

            auto isOccluderAt = [&](const glm::ivec3& lodCoord) {
                glm::ivec3 local = lodCoord - minCoord;
                if (inBounds(local)) {
                    int idx = cellIndex(local);
                    if (!solidCells[idx].filled) return false;
                    return solidProtoNeighborType(solidCells[idx].protoID) == 1;
                }
                uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, section.lod, lodCoord);
                int type = 0;
                classifyProto(id, type);
                return isSolidOccluderType(type);
            };
            auto isChalkDustAt = [&](const glm::ivec3& lodCoord) {
                const uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, section.lod, lodCoord);
                if (id == 0 || id >= prototypes.size()) return false;
                return isChalkDustPrototype(prototypes[static_cast<size_t>(id)]);
            };
            auto isExposedChalkDustAt = [&](const glm::ivec3& lodCoord) {
                if (!isChalkDustAt(lodCoord)) return false;
                const int aboveType = neighborTypeAt(lodCoord + glm::ivec3(0, 1, 0));
                return !isSolidOccluderType(aboveType);
            };

            auto computeFaceAo = [&](const glm::ivec3& lodCoord, int faceType) {
                glm::ivec3 uAxis = faceUAxisForAo(faceType);
                glm::ivec3 vAxis = faceVAxisForAo(faceType);
                auto cornerAo = [&](int uSign, int vSign) {
                    glm::ivec3 side1 = uAxis * uSign;
                    glm::ivec3 side2 = vAxis * vSign;
                    bool s1 = isOccluderAt(lodCoord + side1);
                    bool s2 = isOccluderAt(lodCoord + side2);
                    bool c = isOccluderAt(lodCoord + side1 + side2);
                    return applyAoStrength(bakedAoValue(s1, s2, c), aoStrength);
                };
                return glm::vec4(
                    cornerAo(-1, -1), // uv (0,0)
                    cornerAo(1, -1),  // uv (1,0)
                    cornerAo(1, 1),   // uv (1,1)
                    cornerAo(-1, 1)   // uv (0,1)
                );
            };
            auto isSlopeAoOccluderAt = [&](const glm::ivec3& lodCoord) {
                glm::ivec3 local = lodCoord - minCoord;
                if (inBounds(local)) {
                    int idx = cellIndex(local);
                    const bool solidOccluder = solidCells[idx].filled
                        && solidProtoNeighborType(solidCells[idx].protoID) == 1;
                    return solidOccluder || slopeCells[idx].filled;
                }
                uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, section.lod, lodCoord);
                int type = 0;
                classifyProto(id, type);
                return type == 1 || type == 6;
            };
            auto computeSlopeFaceAo = [&](const glm::ivec3& lodCoord, int faceType, float alpha, SlopeDir dir) {
                (void)alpha;
                if (faceType != 2 || dir == SlopeDir::None) {
                    return computeFaceAo(lodCoord, faceType);
                }

                const glm::ivec3 high = slopeHighDirection(dir);
                if (high == glm::ivec3(0)) {
                    return computeFaceAo(lodCoord, faceType);
                }
                const glm::ivec3 low = -high;
                const glm::ivec3 perp = (dir == SlopeDir::PosX || dir == SlopeDir::NegX)
                    ? glm::ivec3(0, 0, 1)
                    : glm::ivec3(1, 0, 0);

                auto cornerAoFromUv = [&](int uSign, int vSign) {
                    int alongSign = -1;
                    int perpSign = 1;
                    switch (dir) {
                        case SlopeDir::PosX:
                            alongSign = (uSign < 0) ? 1 : -1; // high at low U
                            perpSign = vSign;
                            break;
                        case SlopeDir::NegX:
                            alongSign = (uSign > 0) ? 1 : -1; // high at high U
                            perpSign = vSign;
                            break;
                        case SlopeDir::PosZ:
                            alongSign = (vSign > 0) ? 1 : -1; // high at high V
                            perpSign = uSign;
                            break;
                        case SlopeDir::NegZ:
                            alongSign = (vSign < 0) ? 1 : -1; // high at low V
                            perpSign = uSign;
                            break;
                        default:
                            break;
                    }

                    // Key mapping: low side AO samples one cell down to connect to stepped ramps.
                    const glm::ivec3 alongOffset = (alongSign > 0) ? high : (low + glm::ivec3(0, -1, 0));
                    const glm::ivec3 sideOffset = perp * perpSign;
                    const bool s1 = isSlopeAoOccluderAt(lodCoord + alongOffset);
                    const bool s2 = isSlopeAoOccluderAt(lodCoord + sideOffset);
                    const bool c = isSlopeAoOccluderAt(lodCoord + alongOffset + sideOffset);
                    return applyAoStrength(bakedAoValue(s1, s2, c), aoStrength);
                };

                return glm::vec4(
                    cornerAoFromUv(-1, -1),
                    cornerAoFromUv(1, -1),
                    cornerAoFromUv(1, 1),
                    cornerAoFromUv(-1, 1)
                );
            };
            auto estimateWaveClassFromNeighborhood = [&](const glm::ivec3& center) -> uint8_t {
                auto spanAlong = [&](const glm::ivec3& axisStep) {
                    constexpr int kMaxProbe = 40;
                    int span = 1;
                    for (int s = 1; s <= kMaxProbe; ++s) {
                        if (neighborTypeAt(center + axisStep * s) != 2) break;
                        span += 1;
                    }
                    for (int s = 1; s <= kMaxProbe; ++s) {
                        if (neighborTypeAt(center - axisStep * s) != 2) break;
                        span += 1;
                    }
                    return span;
                };
                const int spanX = spanAlong(glm::ivec3(1, 0, 0));
                const int spanZ = spanAlong(glm::ivec3(0, 0, 1));
                const int minSpan = std::min(spanX, spanZ);
                const int maxSpan = std::max(spanX, spanZ);
                if (maxSpan <= 12 && minSpan <= 8) return kWaterWaveClassPond;
                if (minSpan <= 10 && maxSpan >= 20) return kWaterWaveClassRiver;
                if (minSpan >= 38 && maxSpan >= 38) return kWaterWaveClassOcean;
                return kWaterWaveClassLake;
            };
            auto resolveLilypadWaveClass = [&](const glm::ivec3& center) -> uint8_t {
                static const std::array<glm::ivec3, 5> kWaterProbes = {
                    glm::ivec3(0, -1, 0),
                    glm::ivec3(0, 0, 0),
                    glm::ivec3(0, 1, 0),
                    glm::ivec3(1, -1, 0),
                    glm::ivec3(0, -1, 1)
                };
                for (const glm::ivec3& step : kWaterProbes) {
                    const glm::ivec3 probe = center + step;
                    const glm::ivec3 local = probe - minCoord;
                    if (inBounds(local)) {
                        const CellInfo& waterCell = waterCells[cellIndex(local)];
                        if (!waterCell.filled) continue;
                        const uint8_t waveClass = decodeWaterWaveClass(waterCell.packedColor);
                        if (waveClass >= kWaterWaveClassPond && waveClass <= kWaterWaveClassOcean) {
                            return waveClass;
                        }
                    }
                }
                return estimateWaveClassFromNeighborhood(center + glm::ivec3(0, -1, 0));
            };

            auto sameColor = [](const glm::vec3& a, const glm::vec3& b) {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            };
            auto sameAo = [](const glm::vec4& a, const glm::vec4& b) {
                return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
            };
            auto sameKey = [&](const MaskCell& a, const MaskCell& b) {
                // Emit grass-cover/blueprint mats per-cell; do not greedy-merge across neighbors.
                if (a.narrowShape == NarrowShape::GrassCover
                    || b.narrowShape == NarrowShape::GrassCover
                    || a.narrowShape == NarrowShape::BlueprintMat
                    || b.narrowShape == NarrowShape::BlueprintMat
                    || a.narrowShape == NarrowShape::ChalkDust
                    || b.narrowShape == NarrowShape::ChalkDust
                    || a.narrowShape == NarrowShape::Book
                    || b.narrowShape == NarrowShape::Book
                    || a.narrowShape == NarrowShape::Lantern
                    || b.narrowShape == NarrowShape::Lantern
                    || a.narrowShape == NarrowShape::DepthCrystal
                    || b.narrowShape == NarrowShape::DepthCrystal
                    || a.narrowShape == NarrowShape::StonePebble
                    || b.narrowShape == NarrowShape::StonePebble
                    || a.narrowShape == NarrowShape::WallDecal
                    || b.narrowShape == NarrowShape::WallDecal) return false;
                return a.filled && b.filled && a.isLeaf == b.isLeaf
                    && a.plantType == b.plantType
                    && a.narrowLog == b.narrowLog
                    && a.narrowShape == b.narrowShape
                    && a.narrowMount == b.narrowMount
                    && a.narrowAxis == b.narrowAxis
                    && a.protoID == b.protoID
                    && a.alpha == b.alpha
                    && a.tileIndex == b.tileIndex && sameColor(a.color, b.color) && sameAo(a.ao, b.ao);
            };
            const bool waterTopOnlyOutsideLod0 = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "WaterTopOnlyOutsideLod0", true);
            const auto damagedCells = collectDamagedVoxelCellSet(baseSystem, section.lod);
            auto isDamagedCell = [&](const glm::ivec3& worldCell) {
                return damagedCells.find(worldCell) != damagedCells.end();
            };
            auto neighborSectionKnownAt = [&](const glm::ivec3& lodCoord) {
                glm::ivec3 local = lodCoord - minCoord;
                if (inBounds(local)) return true;
                glm::ivec3 sectionCoord(
                    VoxelMeshInitSystemLogic::FloorDivInt(lodCoord.x, section.size),
                    VoxelMeshInitSystemLogic::FloorDivInt(lodCoord.y, section.size),
                    VoxelMeshInitSystemLogic::FloorDivInt(lodCoord.z, section.size)
                );
                VoxelSectionKey key{section.lod, sectionCoord};
                return voxelWorld.sections.find(key) != voxelWorld.sections.end();
            };

            GreedyChunkData out = acquireGreedyChunk(voxelGreedy);
            auto buildPass = [&](const std::vector<CellInfo>& cellData, float alpha, int passType, bool allowGreedyMerge) {
                for (int faceType = 0; faceType < 6; ++faceType) {
                    if (passType == 1 && waterTopOnlyOutsideLod0 && section.lod > 0 && faceType != 2) {
                        // Avoid persistent water "chunk walls" across LOD/section transitions.
                        continue;
                    }
                    int sliceLen = 0;
                    int uLen = 0;
                    int vLen = 0;
                    switch (faceType) {
                        case 0:
                        case 1:
                            sliceLen = sizeX; uLen = sizeZ; vLen = sizeY; break;
                        case 2:
                        case 3:
                            sliceLen = sizeY; uLen = sizeX; vLen = sizeZ; break;
                        case 4:
                        case 5:
                            sliceLen = sizeZ; uLen = sizeX; vLen = sizeY; break;
                        default:
                            break;
                    }
                    if (sliceLen <= 0 || uLen <= 0 || vLen <= 0) continue;

                    std::vector<MaskCell> mask(static_cast<size_t>(uLen * vLen));
                    for (int slice = 0; slice < sliceLen; ++slice) {
                        for (auto& cell : mask) {
                            cell.filled = false;
                        }
                        for (int v = 0; v < vLen; ++v) {
                            for (int u = 0; u < uLen; ++u) {
                                glm::ivec3 local = VoxelMeshInitSystemLogic::LocalCellFromUV(faceType, slice, u, v);
                                if (!inBounds(local)) continue;
                                const CellInfo& cell = cellData[cellIndex(local)];
                                if (!cell.filled) continue;
                                if (cell.protoID < 0 || cell.protoID >= static_cast<int>(prototypes.size())) continue;
                                const Entity& proto = prototypes[cell.protoID];
                                NarrowLogAxis currentAxis = narrowLogAxis(proto);
                                bool currentNarrow = currentAxis != NarrowLogAxis::None;
                                NarrowShape currentShape = narrowShapeForPrototype(proto);
                                NarrowMount currentMount = narrowMountForPrototype(proto);
                                if (currentShape == NarrowShape::WallDecal
                                    && !wallDecalFaceEnabled(currentMount, faceType)) {
                                    continue;
                                }
                                if (currentShape == NarrowShape::ChalkDust
                                    && !chalkDustFaceEnabled(faceType)) {
                                    continue;
                                }
                                if (currentShape == NarrowShape::BigLilypad
                                    && !bigLilypadFaceEnabled(faceType)) {
                                    continue;
                                }
                                if (currentShape == NarrowShape::DepthCrystal
                                    && !depthCrystalFaceEnabled(faceType)) {
                                    continue;
                                }
                                glm::ivec3 lodCoord = minCoord + local;
                                glm::ivec3 neighborCoord = lodCoord + VoxelMeshInitSystemLogic::FaceNormal(faceType);
                                int neighborType = neighborTypeAt(neighborCoord);
                                if (passType == 1) {
                                    // Unknown neighbor sections (streaming gaps / section boundaries) should not
                                    // produce water boundary planes; only draw against known non-water cells.
                                    if (neighborType == 0 && !neighborSectionKnownAt(neighborCoord)) {
                                        continue;
                                    }
                                    if (neighborType == 1 || neighborType == 2) continue;
                                    if (neighborType == 6
                                        && waterSlopeNeighborOccludesFace(neighborCoord, faceType)) {
                                        continue;
                                    }
                                } else if (passType == 2) {
                                    // Leaves should only be hidden by full solids or other leaf voxels.
                                    // Narrow surface props (sticks/pebbles), plants, water, and slopes are non-full.
                                    if (neighborType == 1 || neighborType == 3) continue;
                                } else if (passType == 3) {
                                    if (isSandDollarName(proto.name)) {
                                        if (faceType != 2) continue;
                                    } else if (!(faceType == 0 || faceType == 1 || faceType == 4 || faceType == 5)) {
                                        continue;
                                    }
                                } else {
                                    bool neighborFull = isSolidOccluderType(neighborType);
                                    bool neighborNarrow = (neighborType == 4);
                                    const bool currentDamaged = isDamagedCell(lodCoord);
                                    const bool neighborDamaged = isDamagedCell(neighborCoord);
                                    const bool emitCrackBackingFace = neighborFull && !currentDamaged && neighborDamaged;
                                    bool faceAlongAxis = (currentAxis == NarrowLogAxis::Y && (faceType == 2 || faceType == 3))
                                        || (currentAxis == NarrowLogAxis::X && (faceType == 0 || faceType == 1))
                                        || (currentAxis == NarrowLogAxis::Z && (faceType == 4 || faceType == 5));
                                    if (!currentNarrow) {
                                        if (neighborFull && !emitCrackBackingFace) continue;
                                    } else {
                                        if (faceAlongAxis && (neighborFull || neighborNarrow)) continue;
                                    }
                                }
                                int tileIndex = ::RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), proto, faceType);
                                if (isVoidPortalPrototypeName(proto.name)) {
                                    tileIndex = voidPortalTileIndexForFace(lodCoord, faceType);
                                }
                                if (passType == 2 && isLeafFanPlantPrototype(proto)) {
                                    int baseLeafPrototypeID = -1;
                                    if (proto.name == "GrassTuftLeafFanPine") {
                                        baseLeafPrototypeID = pineLeafPrototypeID;
                                    } else if (proto.name == "GrassTuftLeafFanOak") {
                                        baseLeafPrototypeID = oakLeafPrototypeID;
                                    }
                                    if (baseLeafPrototypeID >= 0
                                        && baseLeafPrototypeID < static_cast<int>(prototypes.size())) {
                                        tileIndex = ::RenderInitSystemLogic::FaceTileIndexFor(
                                            baseSystem.world.get(),
                                            prototypes[static_cast<size_t>(baseLeafPrototypeID)],
                                            faceType
                                        );
                                    }
                                }
                                if (currentShape == NarrowShape::GrassCover && currentMount == NarrowMount::Floor) {
                                    const int snapshotTile = decodeGrassCoverSnapshotTile(cell.packedColor);
                                    if (snapshotTile >= 0) {
                                        tileIndex = snapshotTile;
                                    } else {
                                        const glm::ivec3 supportCoord = lodCoord + glm::ivec3(0, -1, 0);
                                        const uint32_t supportId = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(
                                            voxelWorld,
                                            section.lod,
                                            supportCoord
                                        );
                                        if (supportId > 0 && supportId < static_cast<uint32_t>(prototypes.size())) {
                                            const Entity& supportProto = prototypes[static_cast<size_t>(supportId)];
                                            const int supportTopTile = ::RenderInitSystemLogic::FaceTileIndexFor(
                                                baseSystem.world.get(),
                                                supportProto,
                                                2
                                            );
                                            if (supportTopTile >= 0) {
                                                tileIndex = supportTopTile;
                                            }
                                        }
                                    }
                                }
                                if (currentShape == NarrowShape::Default
                                    && tileIndex >= 0
                                    && isDesertSandPrototype(proto)) {
                                    tileIndex += desertSandQuarterTurnsForCell(lodCoord) * kChalkTileEncodeStride;
                                }
                                if (currentShape == NarrowShape::ChalkDust && currentMount == NarrowMount::Floor) {
                                    int chalkQuarterTurns = 0;
                                    const bool north = isExposedChalkDustAt(lodCoord + glm::ivec3(0, 0, -1));
                                    const bool east = isExposedChalkDustAt(lodCoord + glm::ivec3(1, 0, 0));
                                    const bool south = isExposedChalkDustAt(lodCoord + glm::ivec3(0, 0, 1));
                                    const bool west = isExposedChalkDustAt(lodCoord + glm::ivec3(-1, 0, 0));
                                    const int chalkTile = chooseChalkDustTileFromNeighbors(north, east, south, west, chalkQuarterTurns);
                                    tileIndex = chalkTile + (chalkQuarterTurns * kChalkTileEncodeStride);
                                }
                                if (currentShape == NarrowShape::WallDecal) {
                                    tileIndex += wallDecalQuarterTurnsForCell(lodCoord) * kChalkTileEncodeStride;
                                }
                                if (currentShape == NarrowShape::Book) {
                                    // Use paper texture on side faces; keep blueprint cover on top/bottom.
                                    if (faceType == 0 || faceType == 1 || faceType == 4 || faceType == 5) {
                                        tileIndex = kBookPageTileIndex;
                                    }
                                }
                                if (passType == 3 && cell.plantType == PlantType::Flower && tileIndex < 0) {
                                    // Legacy flowers with no atlas tile stay procedural in shader space.
                                    tileIndex = -1;
                                }
                                int idx = v * uLen + u;
                                mask[idx].filled = true;
                                mask[idx].isLeaf = cell.isLeaf;
                                mask[idx].plantType = cell.plantType;
                                mask[idx].narrowLog = currentNarrow;
                                mask[idx].narrowShape = currentShape;
                                mask[idx].narrowMount = currentMount;
                                mask[idx].narrowAxis = currentAxis;
                                mask[idx].cellCoord = lodCoord;
                                mask[idx].protoID = cell.protoID;
                                mask[idx].tileIndex = tileIndex;
                                mask[idx].packedColor = cell.packedColor;
                                float encodedAlpha = (passType == 3)
                                    ? alphaForPlantType(cell.plantType)
                                    : alpha;
                                if (passType != 3 && currentShape == NarrowShape::WallDecal) {
                                    encodedAlpha = kWallDecalAlpha;
                                } else if (passType != 3 && currentShape == NarrowShape::GrassCover) {
                                    encodedAlpha = kGrassCoverAlpha;
                                } else if (passType != 3 && currentShape == NarrowShape::ChalkDust) {
                                    encodedAlpha = kChalkDustAlpha;
                                } else if (passType != 3
                                           && currentShape == NarrowShape::Book
                                           && (faceType == 0 || faceType == 1 || faceType == 4 || faceType == 5)) {
                                    encodedAlpha = kBookPageAlpha;
                                }
                                if (passType == 3 && isSandDollarName(proto.name)) {
                                    encodedAlpha = kGrassCoverAlpha;
                                }
                                if (passType != 3 && isLavaName(proto.name)) {
                                    encodedAlpha = kLavaSurfaceAlpha;
                                }
                                mask[idx].alpha = encodedAlpha;
                                const bool applyVoxelLighting = voxelLightingEnabled
                                    && section.lod == 0
                                    && (passType == 0
                                        || passType == 2
                                        || passType == 3
                                        || (passType == 1 && voxelLightingAffectWater));
                                const std::array<uint8_t, 2> lightLevels = lightLevelsAt(neighborCoord);
                                const bool texturedFace = tileIndex >= 0;
                                const glm::vec3 baseFaceColor = texturedFace ? glm::vec3(1.0f) : cell.color;
                                if (applyVoxelLighting) {
                                    const float lightFactor = lightFactorFromLevel(std::max(lightLevels[0], lightLevels[1]));
                                    mask[idx].color = applyLightingDebugColor(baseFaceColor * lightFactor, lightLevels[0], lightLevels[1]);
                                } else {
                                    mask[idx].color = applyLightingDebugColor(baseFaceColor, lightLevels[0], lightLevels[1]);
                                }
                                const bool applyAo = !aoDisabled
                                    && (passType == 0
                                        || (passType == 2 && leafAoEnabled)
                                        || (passType == 3 && plantAoEnabled));
                                glm::vec4 faceAo = applyAo ? computeFaceAo(lodCoord, faceType) : glm::vec4(1.0f);
                                if (passType == 2) {
                                    faceAo = glm::vec4(1.0f) + (faceAo - glm::vec4(1.0f)) * leafAoStrength;
                                } else if (passType == 3) {
                                    faceAo = glm::vec4(1.0f) + (faceAo - glm::vec4(1.0f)) * plantAoStrength;
                                }
                                if (passType == 0
                                    && (currentShape == NarrowShape::PetalPile
                                        || currentShape == NarrowShape::BigLilypad)
                                    && isLilypadName(proto.name)) {
                                    const uint8_t waveClass = resolveLilypadWaveClass(lodCoord);
                                    if (waveClass >= kWaterWaveClassPond && waveClass <= kWaterWaveClassOcean) {
                                        faceAo += glm::vec4(kWaterWaveClassAoEncodeStride * static_cast<float>(waveClass));
                                    }
                                }
                                if (faceType == 2) {
                                    if (passType == 0) {
                                        const int aboveType = neighborTypeAt(lodCoord + glm::ivec3(0, 1, 0));
                                        if (aboveType == 2) {
                                            faceAo += glm::vec4(kUnderwaterCausticsAoFlag);
                                        }
                                    } else if (passType == 1) {
                                        uint8_t waveClass = decodeWaterWaveClass(cell.packedColor);
                                        if (waveClass == kWaterWaveClassUnknown) {
                                            waveClass = estimateWaveClassFromNeighborhood(lodCoord);
                                        }
                                        if (waveClass >= kWaterWaveClassPond && waveClass <= kWaterWaveClassOcean) {
                                            faceAo += glm::vec4(kWaterWaveClassAoEncodeStride * static_cast<float>(waveClass));
                                        }
                                        bool shoreline = false;
                                        const std::array<glm::ivec3, 4> sideOffsets{
                                            glm::ivec3(1, 0, 0),
                                            glm::ivec3(-1, 0, 0),
                                            glm::ivec3(0, 0, 1),
                                            glm::ivec3(0, 0, -1)
                                        };
                                        for (const glm::ivec3& side : sideOffsets) {
                                            const int sideType = neighborTypeAt(lodCoord + side);
                                            if (isSolidOccluderType(sideType)) {
                                                shoreline = true;
                                                break;
                                            }
                                        }
                                        if (shoreline) {
                                            faceAo += glm::vec4(kWaterShorelineAoFlag);
                                        }
                                    }
                                } else if (passType == 1
                                    && (faceType == 0 || faceType == 1 || faceType == 4 || faceType == 5)) {
                                    const int aboveType = neighborTypeAt(lodCoord + glm::ivec3(0, 1, 0));
                                    const int belowType = neighborTypeAt(lodCoord + glm::ivec3(0, -1, 0));
                                    const bool aboveWaterLike = (aboveType == 2 || aboveType == 6);
                                    const bool belowWaterLike = (belowType == 2 || belowType == 6);
                                    if (aboveWaterLike && belowWaterLike) {
                                        faceAo += glm::vec4(kWaterfallFoamAoFlag);
                                    }
                                }
                                mask[idx].ao = faceAo;
                            }
                        }

                        for (int v = 0; v < vLen; ++v) {
                            for (int u = 0; u < uLen; ++u) {
                                int idx = v * uLen + u;
                                if (!mask[idx].filled) continue;
                                MaskCell seed = mask[idx];
                                int width = 1;
                                int height = 1;
                                if (allowGreedyMerge) {
                                    while (u + width < uLen && sameKey(seed, mask[v * uLen + (u + width)])) {
                                        ++width;
                                    }
                                    bool done = false;
                                    while (v + height < vLen && !done) {
                                        for (int k = 0; k < width; ++k) {
                                            if (!sameKey(seed, mask[(v + height) * uLen + (u + k)])) {
                                                done = true;
                                                break;
                                            }
                                        }
                                        if (!done) ++height;
                                    }
                                }

                                for (int dv = 0; dv < height; ++dv) {
                                    for (int du = 0; du < width; ++du) {
                                        mask[(v + dv) * uLen + (u + du)].filled = false;
                                    }
                                }

                                float centerU = static_cast<float>(u) + (static_cast<float>(width - 1) * 0.5f);
                                float centerV = static_cast<float>(v) + (static_cast<float>(height - 1) * 0.5f);
                                float axisOffset = (faceType % 2 == 0) ? 0.5f : -0.5f;
                                NarrowHalfExtents narrowExt = narrowHalfExtentsForShape(seed.narrowAxis, seed.narrowShape, seed.narrowMount);
                                if (seed.narrowShape == NarrowShape::GrassCover) {
                                    constexpr float kDotHalf = 1.0f / 48.0f;
                                    narrowExt = {kDotHalf, kDotHalf, kDotHalf};
                                }
                                if (seed.plantType != PlantType::None) {
                                    axisOffset = 0.0f;
                                } else if (seed.narrowLog) {
                                    float halfExtent = 0.5f;
                                    if (faceType == 0 || faceType == 1) halfExtent = narrowExt.x;
                                    else if (faceType == 2 || faceType == 3) halfExtent = narrowExt.y;
                                    else if (faceType == 4 || faceType == 5) halfExtent = narrowExt.z;
                                    axisOffset = (faceType % 2 == 0) ? halfExtent : -halfExtent;
                                }
                                float axisCoord = static_cast<float>(slice) + axisOffset;
                                glm::vec3 center;
                                switch (faceType) {
                                    case 0:
                                    case 1:
                                        center = glm::vec3(minCoord.x + axisCoord,
                                                           minCoord.y + centerV,
                                                           minCoord.z + centerU);
                                        break;
                                    case 2:
                                    case 3:
                                        center = glm::vec3(minCoord.x + centerU,
                                                           minCoord.y + axisCoord,
                                                           minCoord.z + centerV);
                                        break;
                                    case 4:
                                    case 5:
                                        center = glm::vec3(minCoord.x + centerU,
                                                           minCoord.y + centerV,
                                                           minCoord.z + axisCoord);
                                        break;
                                    default:
                                        center = glm::vec3(minCoord);
                                        break;
                                }
                                center *= static_cast<float>(scale);

                                glm::vec2 scaleVec(static_cast<float>(width * scale), static_cast<float>(height * scale));
                                glm::vec2 uvScaleVec = scaleVec;
                                if (seed.plantType != PlantType::None) {
                                    float plantWidth = 1.00f;
                                    float plantHeight = 1.00f;
                                    bool keepPlantBottomAnchored = true;
                                    const bool isLeafFanPlant =
                                        seed.protoID >= 0
                                        && seed.protoID < static_cast<int>(prototypes.size())
                                        && isLeafFanPlantPrototype(prototypes[static_cast<size_t>(seed.protoID)]);
                                    if (isLeafFanPlant) {
                                        // Fan leaves are authored as a 3x3 block sheet where the center tile
                                        // should map to a full voxel-sized card. Scale the card to 3x and
                                        // keep it centered so outer tiles protrude equally from all sides.
                                        plantWidth = 3.00f;
                                        plantHeight = 3.00f;
                                        keepPlantBottomAnchored = false;
                                    } else if (seed.plantType == PlantType::Flower) {
                                        // Textured flowers use full grass-card profile; legacy procedural flowers stay compact.
                                        if (seed.tileIndex < 0) {
                                            plantWidth = 0.86f;
                                            plantHeight = 0.92f;
                                        }
                                    } else if (seed.plantType == PlantType::GrassShort) {
                                        plantWidth = 1.00f;
                                        plantHeight = 1.00f;
                                    }
                                    scaleVec.x *= plantWidth;
                                    scaleVec.y *= plantHeight;
                                    uvScaleVec = glm::vec2(1.0f);
                                    if (keepPlantBottomAnchored) {
                                        center.y += (plantHeight - 1.0f) * 0.5f * static_cast<float>(scale);
                                    }
                                } else if (seed.narrowLog) {
                                    float uScale = 1.0f;
                                    float vScale = 1.0f;
                                    if (faceType == 0 || faceType == 1) {
                                        uScale = narrowExt.z * 2.0f; // U axis is Z.
                                        vScale = narrowExt.y * 2.0f; // V axis is Y.
                                    } else if (faceType == 2 || faceType == 3) {
                                        uScale = narrowExt.x * 2.0f; // U axis is X.
                                        vScale = narrowExt.z * 2.0f; // V axis is Z.
                                    } else if (faceType == 4 || faceType == 5) {
                                        uScale = narrowExt.x * 2.0f; // U axis is X.
                                        vScale = narrowExt.y * 2.0f; // V axis is Y.
                                    }
                                    scaleVec.x *= uScale;
                                    scaleVec.y *= vScale;
                                    uvScaleVec.x *= uScale;
                                    uvScaleVec.y *= vScale;
                                }
                                if (seed.narrowShape != NarrowShape::Default) {
                                    if (seed.narrowMount == NarrowMount::WallPosX) {
                                        center.x += (0.5f - narrowExt.x - 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowMount == NarrowMount::WallNegX) {
                                        center.x += (-0.5f + narrowExt.x + 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowMount == NarrowMount::WallPosZ) {
                                        center.z += (0.5f - narrowExt.z - 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowMount == NarrowMount::WallNegZ) {
                                        center.z += (-0.5f + narrowExt.z + 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowMount == NarrowMount::Ceiling) {
                                        center.y += (0.5f - narrowExt.y - 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowShape == NarrowShape::CactusArm) {
                                        // Lift arm geometry toward the top of its voxel to create a curved saguaro silhouette.
                                        center.y += (6.0f / 24.0f) * static_cast<float>(scale);
                                    } else if (seed.narrowShape == NarrowShape::CactusJunction) {
                                        // Junction blocks stay centered to keep trunk/arm seams closed.
                                    } else if (seed.narrowShape == NarrowShape::Lantern) {
                                        // Lanterns are full-height slim columns and should stay centered.
                                    } else {
                                        // Rest near the cell floor so narrow props appear laid on top of ground blocks.
                                        center.y += floorMountYOffset(seed.narrowShape, narrowExt) * static_cast<float>(scale);
                                    }
                                }
                                if (seed.narrowShape == NarrowShape::WallDecal) {
                                    center += wallDecalPanOffsetForCell(seed.cellCoord, seed.narrowMount) * static_cast<float>(scale);
                                }
                                if (seed.narrowShape == NarrowShape::DepthCrystal && seed.narrowMount == NarrowMount::Floor) {
                                    if (seed.protoID < 0 || seed.protoID >= static_cast<int>(prototypes.size())) {
                                        continue;
                                    }
                                    const DepthCrystalLayerSet layerSet =
                                        depthCrystalLayersForPrototype(prototypes[static_cast<size_t>(seed.protoID)]);
                                    for (int layerIdx = 0; layerIdx < layerSet.count; ++layerIdx) {
                                        const DepthCrystalLayerSpec& layer = layerSet.layers[layerIdx];
                                        glm::vec3 layerCenter = center;
                                        const float padPx = layer.useBottomPadding ? layer.bottomPaddingPx : 0.0f;
                                        const float padOffset = (padPx / 24.0f) * static_cast<float>(scale);
                                        layerCenter.y -= padOffset;
                                        layerCenter.z -= padOffset;
                                        layerCenter.x += (layer.offsetXPx / 24.0f) * static_cast<float>(scale);
                                        layerCenter.y += (layer.offsetYPx / 24.0f) * static_cast<float>(scale);
                                        layerCenter.z += (layer.offsetZPx / 24.0f) * static_cast<float>(scale);
                                        out.positions.push_back(layerCenter);
                                        out.colors.push_back(seed.color);
                                        out.faceTypes.push_back(faceType);
                                        out.tileIndices.push_back(layer.tileIndex);
                                        out.alphas.push_back(seed.alpha);
                                        out.ao.push_back(seed.ao);
                                        out.scales.push_back(scaleVec);
                                        out.uvScales.push_back(uvScaleVec);
                                    }
                                    continue;
                                }
                                if (seed.narrowShape == NarrowShape::StonePebble
                                    && seed.narrowMount == NarrowMount::Floor
                                    && seed.protoID >= 0
                                    && seed.protoID < static_cast<int>(prototypes.size())
                                    && isSurfaceStonePebbleName(prototypes[static_cast<size_t>(seed.protoID)].name)) {
                                    const int pileCount = decodeSurfaceStonePileCount(seed.packedColor);
                                    const StonePebblePilePieces pile = stonePebblePilePiecesForCell(seed.cellCoord, pileCount);
                                    for (int piece = 0; piece < pile.count; ++piece) {
                                        const glm::vec2 pieceOffset = pile.offsets[static_cast<size_t>(piece)];
                                        const NarrowHalfExtents pieceExt = pile.halfExtents[static_cast<size_t>(piece)];
                                        glm::vec3 pieceCenter = glm::vec3(seed.cellCoord) * static_cast<float>(scale);
                                        pieceCenter.x += pieceOffset.x * static_cast<float>(scale);
                                        pieceCenter.z += pieceOffset.y * static_cast<float>(scale);
                                        pieceCenter.y += (-0.5f + pieceExt.y + 0.01f) * static_cast<float>(scale);

                                        glm::vec3 faceCenter = pieceCenter;
                                        glm::vec2 faceScale(1.0f);
                                        if (faceType == 0 || faceType == 1) {
                                            faceCenter.x += ((faceType == 0) ? pieceExt.x : -pieceExt.x) * static_cast<float>(scale);
                                            faceScale = glm::vec2(
                                                pieceExt.z * 2.0f * static_cast<float>(scale),
                                                pieceExt.y * 2.0f * static_cast<float>(scale)
                                            );
                                        } else if (faceType == 2 || faceType == 3) {
                                            faceCenter.y += ((faceType == 2) ? pieceExt.y : -pieceExt.y) * static_cast<float>(scale);
                                            faceScale = glm::vec2(
                                                pieceExt.x * 2.0f * static_cast<float>(scale),
                                                pieceExt.z * 2.0f * static_cast<float>(scale)
                                            );
                                        } else {
                                            faceCenter.z += ((faceType == 4) ? pieceExt.z : -pieceExt.z) * static_cast<float>(scale);
                                            faceScale = glm::vec2(
                                                pieceExt.x * 2.0f * static_cast<float>(scale),
                                                pieceExt.y * 2.0f * static_cast<float>(scale)
                                            );
                                        }

                                        out.positions.push_back(faceCenter);
                                        out.colors.push_back(seed.color);
                                        out.faceTypes.push_back(faceType);
                                        out.tileIndices.push_back(seed.tileIndex);
                                        out.alphas.push_back(seed.alpha);
                                        out.ao.push_back(seed.ao);
                                        out.scales.push_back(faceScale);
                                        out.uvScales.push_back(faceScale);
                                    }
                                    continue;
                                }
                                if (seed.narrowShape == NarrowShape::GrassCover && seed.narrowMount == NarrowMount::Floor) {
                                    const GrassCoverDots dots = grassCoverDotsForCell(seed.cellCoord);
                                    for (int dot = 0; dot < dots.count; ++dot) {
                                        glm::vec3 dotCenter = center;
                                        const glm::vec2 offset = dots.offsets[static_cast<size_t>(dot)];
                                        dotCenter.x += offset.x * static_cast<float>(scale);
                                        dotCenter.z += offset.y * static_cast<float>(scale);
                                        out.positions.push_back(dotCenter);
                                        out.colors.push_back(seed.color);
                                        out.faceTypes.push_back(faceType);
                                        out.tileIndices.push_back(seed.tileIndex);
                                        out.alphas.push_back(seed.alpha);
                                        out.ao.push_back(seed.ao);
                                        out.scales.push_back(scaleVec);
                                        out.uvScales.push_back(uvScaleVec);
                                    }
                                    continue;
                                }
                                out.positions.push_back(center);
                                out.colors.push_back(seed.color);
                                out.faceTypes.push_back(faceType);
                                out.tileIndices.push_back(seed.tileIndex);
                                out.alphas.push_back(seed.alpha);
                                out.ao.push_back(seed.ao);
                                out.scales.push_back(scaleVec);
                                out.uvScales.push_back(uvScaleVec);
                            }
                        }
                    }
                }
            };

            buildPass(solidCells, 1.0f, 0, true);
            buildPass(waterCells, 0.6f, 1, true);
            const bool cullPlantsBeyondLod0 = ::RenderInitSystemLogic::getRegistryBool(
                baseSystem,
                "FoliageCullOutsideLod0",
                true
            );
            const bool includeFoliage = !(cullPlantsBeyondLod0 && section.lod > 0);
            if (includeFoliage) {
                buildPass(leafCells, -1.0f, 2, false);
                buildPass(plantCells, -2.0f, 3, false);
                buildPass(waterloggedFoliageCells, -2.0f, 3, false);
            }

            auto emitSlopeFace = [&](const glm::ivec3& local,
                                     int faceType,
                                     int tileIndex,
                                     float alpha,
                                     const glm::vec3& color,
                                     const glm::vec4& ao) {
                float axisOffset = (faceType % 2 == 0) ? 0.5f : -0.5f;
                glm::vec3 center;
                switch (faceType) {
                    case 0:
                    case 1:
                        center = glm::vec3(minCoord.x + local.x + axisOffset,
                                           minCoord.y + local.y,
                                           minCoord.z + local.z);
                        break;
                    case 2:
                    case 3:
                        center = glm::vec3(minCoord.x + local.x,
                                           minCoord.y + local.y + axisOffset,
                                           minCoord.z + local.z);
                        break;
                    case 4:
                    case 5:
                        center = glm::vec3(minCoord.x + local.x,
                                           minCoord.y + local.y,
                                           minCoord.z + local.z + axisOffset);
                        break;
                    default:
                        center = glm::vec3(minCoord);
                        break;
                }
                center *= static_cast<float>(scale);
                out.positions.push_back(center);
                out.colors.push_back(color);
                out.faceTypes.push_back(faceType);
                out.tileIndices.push_back(tileIndex);
                out.alphas.push_back(alpha);
                out.ao.push_back(ao);
                out.scales.push_back(glm::vec2(static_cast<float>(scale)));
                out.uvScales.push_back(glm::vec2(static_cast<float>(scale)));
            };

            auto buildSlopePass = [&]() {
                for (int z = 0; z < sizeZ; ++z) {
                    for (int y = 0; y < sizeY; ++y) {
                        for (int x = 0; x < sizeX; ++x) {
                            glm::ivec3 local(x, y, z);
                            const CellInfo& cell = slopeCells[cellIndex(local)];
                            if (!cell.filled || cell.slopeDir == SlopeDir::None) continue;
                            if (cell.protoID < 0 || cell.protoID >= static_cast<int>(prototypes.size())) continue;
                            const Entity& proto = prototypes[cell.protoID];
                            const bool isWaterSlope = isWaterSlopePrototype(proto);
                            glm::ivec3 lodCoord = minCoord + local;

                            auto tryEmit = [&](int faceType, float alpha) {
                                glm::ivec3 neighborCoord = lodCoord + VoxelMeshInitSystemLogic::FaceNormal(faceType);
                                int neighborType = neighborTypeAt(neighborCoord);
                                if (isSolidOccluderType(neighborType)) return;
                                int tileIndex = ::RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), proto, faceType);
                                glm::vec4 ao = aoDisabled ? glm::vec4(1.0f) : computeSlopeFaceAo(lodCoord, faceType, alpha, cell.slopeDir);
                                if (faceType == 2) {
                                    const int aboveType = neighborTypeAt(lodCoord + glm::ivec3(0, 1, 0));
                                    if (aboveType == 2) {
                                        ao += glm::vec4(kUnderwaterCausticsAoFlag);
                                    }
                                }
                                if (isWaterSlope) {
                                    uint8_t waveClass = decodeWaterWaveClass(cell.packedColor);
                                    if (waveClass == kWaterWaveClassUnknown) {
                                        waveClass = estimateWaveClassFromNeighborhood(lodCoord);
                                    }
                                    if (waveClass >= kWaterWaveClassPond && waveClass <= kWaterWaveClassOcean) {
                                        ao += glm::vec4(kWaterWaveClassAoEncodeStride * static_cast<float>(waveClass));
                                    }
                                }
                                const bool texturedFace = tileIndex >= 0;
                                glm::vec3 litColor = texturedFace ? glm::vec3(1.0f) : cell.color;
                                const std::array<uint8_t, 2> lightLevels = lightLevelsAt(neighborCoord);
                                if (voxelLightingEnabled && section.lod == 0) {
                                    const float lightFactor = lightFactorFromLevel(std::max(lightLevels[0], lightLevels[1]));
                                    litColor *= lightFactor;
                                }
                                litColor = applyLightingDebugColor(litColor, lightLevels[0], lightLevels[1]);
                                emitSlopeFace(local, faceType, tileIndex, alpha, litColor, ao);
                            };

                            if (isWaterSlope && isCornerSlopeDir(cell.slopeDir)) {
                                tryEmit(3, kWaterSlopeSurfaceAlpha);
                                tryEmit(0, kWaterSlopeSurfaceAlpha);
                                tryEmit(1, kWaterSlopeSurfaceAlpha);
                                tryEmit(4, kWaterSlopeSurfaceAlpha);
                                tryEmit(5, kWaterSlopeSurfaceAlpha);
                                tryEmit(2, waterSlopeTopAlpha(cell.slopeDir));
                                continue;
                            }

                            tryEmit(3, isWaterSlope ? kWaterSlopeSurfaceAlpha : 1.0f);
                            tryEmit(slopeTallFace(cell.slopeDir), isWaterSlope ? kWaterSlopeSurfaceAlpha : 1.0f);

                            int capFaceA = 4;
                            int capFaceB = 5;
                            float capAlphaA = kSlopeCapAlphaA;
                            float capAlphaB = kSlopeCapAlphaB;
                            if (isWaterSlope) {
                                waterSlopeCapFacesAndAlpha(cell.slopeDir, capFaceA, capAlphaA, capFaceB, capAlphaB);
                            } else {
                                slopeCapFacesAndAlpha(cell.slopeDir, capFaceA, capAlphaA, capFaceB, capAlphaB);
                            }
                            tryEmit(capFaceA, capAlphaA);
                            tryEmit(capFaceB, capAlphaB);
                            tryEmit(2, isWaterSlope ? waterSlopeTopAlpha(cell.slopeDir) : slopeTopAlpha(cell.slopeDir));
                        }
                    }
                }
            };
            buildSlopePass();

            if (out.positions.empty()) {
                releaseGreedyChunk(voxelGreedy, std::move(out));
                releaseGreedyChunkIfPresent(voxelGreedy, renderKey);
            } else {
                releaseGreedyChunkIfPresent(voxelGreedy, renderKey);
                voxelGreedy.chunks[renderKey] = std::move(out);
            }
            return true;
        }

        bool BuildVoxelGreedyMeshFromSnapshot(const VoxelGreedySnapshot& snap,
                                              const std::vector<Entity>& prototypes,
                                              GreedyChunkData& out,
                                              bool disableAo = false) {
            int sizeX = snap.sizeX;
            int sizeY = snap.sizeY;
            int sizeZ = snap.sizeZ;
            int scale = 1 << snap.lod;
            glm::ivec3 minCoord = snap.minCoord;
            const float aoStrength = std::clamp(snap.aoStrength, 0.0f, 1.0f);
            const bool aoDisabled = disableAo || snap.disableAo || aoStrength <= 0.0001f;
            const bool leafAoEnabled = snap.leafAoEnabled;
            const float leafAoStrength = std::clamp(snap.leafAoStrength, 0.0f, 1.0f);
            const bool plantAoEnabled = snap.plantAoEnabled;
            const float plantAoStrength = std::clamp(snap.plantAoStrength, 0.0f, 1.0f);
            const bool voxelLightingEnabled = snap.lightingEnabled;
            const bool voxelLightingAffectWater = snap.lightingAffectWater;
            const float voxelLightingStrength = std::clamp(snap.lightingStrength, 0.0f, 1.0f);
            const float voxelLightingMinBrightness = std::clamp(snap.lightingMinBrightness, 0.0f, 1.0f);
            const float voxelLightingGamma = std::clamp(snap.lightingGamma, 0.25f, 4.0f);
            const int voxelLightingDebugMode = std::clamp(snap.lightingDebugMode, 0, 4);
            const uint8_t voxelLightingSkyFallbackLevel = static_cast<uint8_t>(std::clamp(static_cast<int>(snap.lightingSkyFallbackLevel), 0, 15));

            struct CellInfo {
                bool filled = false;
                bool isLeaf = false;
                PlantType plantType = PlantType::None;
                SlopeDir slopeDir = SlopeDir::None;
                int protoID = -1;
                glm::vec3 color = glm::vec3(1.0f);
                uint32_t packedColor = 0;
            };
            struct MaskCell {
                bool filled = false;
                bool isLeaf = false;
                PlantType plantType = PlantType::None;
                bool narrowLog = false;
                NarrowShape narrowShape = NarrowShape::Default;
                NarrowMount narrowMount = NarrowMount::Floor;
                NarrowLogAxis narrowAxis = NarrowLogAxis::None;
                glm::ivec3 cellCoord = glm::ivec3(0);
                int protoID = -1;
                int tileIndex = -1;
                uint32_t packedColor = 0;
                float alpha = 1.0f;
                glm::vec3 color = glm::vec3(1.0f);
                glm::vec4 ao = glm::vec4(1.0f);
            };

            auto cellIndex = [&](const glm::ivec3& local) {
                return (local.x * sizeY + local.y) * sizeZ + local.z;
            };
            auto snapIndex = [&](int x, int y, int z) {
                return (x * snap.dimY + y) * snap.dimZ + z;
            };
            auto snapInBounds = [&](const glm::ivec3& local) {
                return local.x >= 0 && local.y >= 0 && local.z >= 0
                    && local.x < snap.dimX && local.y < snap.dimY && local.z < snap.dimZ;
            };
            auto lightFactorFromLevel = [&](uint8_t level) {
                const float normalized = std::clamp(static_cast<float>(level) / 15.0f, 0.0f, 1.0f);
                const float curve = std::pow(normalized, voxelLightingGamma);
                const float factor = voxelLightingMinBrightness + (1.0f - voxelLightingMinBrightness) * curve;
                return 1.0f + (factor - 1.0f) * voxelLightingStrength;
            };
            auto lightLevelsAt = [&](const glm::ivec3& lodCoord) -> std::array<uint8_t, 2> {
                if (!voxelLightingEnabled || snap.lod != 0) return {voxelLightingSkyFallbackLevel, static_cast<uint8_t>(0)};
                glm::ivec3 local = lodCoord - minCoord;
                glm::ivec3 snapLocal = local + glm::ivec3(1);
                if (!snapInBounds(snapLocal)) return {voxelLightingSkyFallbackLevel, static_cast<uint8_t>(0)};
                int idx = snapIndex(snapLocal.x, snapLocal.y, snapLocal.z);
                if (idx < 0) return {voxelLightingSkyFallbackLevel, static_cast<uint8_t>(0)};
                uint8_t sky = voxelLightingSkyFallbackLevel;
                uint8_t block = static_cast<uint8_t>(0);
                if (idx < static_cast<int>(snap.skyLights.size())) {
                    sky = snap.skyLights[static_cast<size_t>(idx)];
                }
                if (idx < static_cast<int>(snap.blockLights.size())) {
                    block = snap.blockLights[static_cast<size_t>(idx)];
                }
                return {sky, block};
            };
            auto applyLightingDebugColor = [&](const glm::vec3& fallbackColor,
                                               uint8_t skyLevel,
                                               uint8_t blockLevel) {
                if (voxelLightingDebugMode <= 0 || snap.lod != 0) return fallbackColor;
                const float skyN = std::clamp(static_cast<float>(skyLevel) / 15.0f, 0.0f, 1.0f);
                const float blockN = std::clamp(static_cast<float>(blockLevel) / 15.0f, 0.0f, 1.0f);
                switch (voxelLightingDebugMode) {
                    case 1: {
                        const float v = std::max(skyN, blockN);
                        return glm::vec3(v);
                    }
                    case 2:
                        return glm::vec3(0.10f * skyN, 0.35f * skyN, 1.0f * skyN);
                    case 3:
                        return glm::vec3(1.0f * blockN, 0.45f * blockN, 0.08f * blockN);
                    case 4:
                        return glm::vec3(blockN, 0.08f * (skyN + blockN), skyN);
                    default:
                        return fallbackColor;
                }
            };

            std::vector<CellInfo> solidCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> waterCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> leafCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> plantCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> waterloggedFoliageCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> slopeCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            const bool leafFanRenderInnerBlock = snap.leafFanRenderInnerBlock;
            const int kelpPrototypeID = findPrototypeIDByName(prototypes, "GrassTuftKelp");
            const int kelpTileIndex = (kelpPrototypeID >= 0)
                ? ::RenderInitSystemLogic::FaceTileIndexFor(snap.worldCtx, prototypes[static_cast<size_t>(kelpPrototypeID)], 0)
                : -1;
            const int seaUrchinPrototypeIDX = findPrototypeIDByName(prototypes, "StonePebbleSeaUrchinTexX");
            const int seaUrchinPrototypeIDZ = findPrototypeIDByName(prototypes, "StonePebbleSeaUrchinTexZ");
            const int sandDollarPrototypeIDX = findPrototypeIDByName(prototypes, "StonePebbleSandDollarTexX");
            const int sandDollarPrototypeIDZ = findPrototypeIDByName(prototypes, "StonePebbleSandDollarTexZ");
            const int pineLeafPrototypeID = findPrototypeIDByName(prototypes, "Leaf");
            const int oakLeafPrototypeID = findPrototypeIDByName(prototypes, "LeafJungleV001");

            auto classifyProto = [&](uint32_t id, int& outType) {
                if (id == 0 || id >= prototypes.size()) { outType = 0; return; }
                const Entity& proto = prototypes[id];
                if (!proto.isBlock) {
                    outType = 0;
                } else if (proto.name == "Water") {
                    outType = 2;
                } else if (isSlopePrototype(proto)) {
                    outType = 6;
                } else if (isLeafPrototype(proto)) {
                    outType = 3;
                } else if (isPlantPrototype(proto)) {
                    outType = 5;
                } else if (isNarrowLogPrototype(proto)) {
                    outType = 4;
                } else {
                    // Keep type 1 for full-cube occluders only.
                    outType = (proto.isSolid && proto.isOpaque) ? 1 : 7;
                }
            };

            for (int z = 0; z < sizeZ; ++z) {
                for (int y = 0; y < sizeY; ++y) {
                    for (int x = 0; x < sizeX; ++x) {
                        int sIdx = snapIndex(x + 1, y + 1, z + 1);
                        uint32_t id = snap.ids[sIdx];
                        if (id == 0 || id >= prototypes.size()) continue;
                        const Entity& proto = prototypes[id];
                        int idx = cellIndex(glm::ivec3(x, y, z));
                        uint32_t packedColor = snap.colors[sIdx];
                        glm::vec3 color = VoxelMeshInitSystemLogic::UnpackColor(packedColor);
                        if (!proto.isBlock) continue;
                        if (proto.name == "Water") {
                            waterCells[idx] = {true, false, PlantType::None, SlopeDir::None, static_cast<int>(id), color, packedColor};
                            uint8_t marker = decodeWaterFoliageMarker(packedColor);
                            if (marker == kWaterFoliageMarkerNone
                                && kelpPrototypeID >= 0
                                && kelpTileIndex >= 0
                                && decodeGrassCoverSnapshotTile(packedColor) == kelpTileIndex) {
                                marker = kWaterFoliageMarkerKelp; // legacy migration support
                            }

                            int overlayProtoID = -1;
                            PlantType overlayPlantType = PlantType::None;
                            if (marker == kWaterFoliageMarkerKelp && kelpPrototypeID >= 0) {
                                overlayProtoID = kelpPrototypeID;
                                overlayPlantType = PlantType::GrassTall;
                            } else if (marker == kWaterFoliageMarkerSeaUrchinX && seaUrchinPrototypeIDX >= 0) {
                                overlayProtoID = seaUrchinPrototypeIDX;
                                overlayPlantType = PlantType::Flower;
                            } else if (marker == kWaterFoliageMarkerSeaUrchinZ && seaUrchinPrototypeIDZ >= 0) {
                                overlayProtoID = seaUrchinPrototypeIDZ;
                                overlayPlantType = PlantType::Flower;
                            } else if (marker == kWaterFoliageMarkerSandDollarX && sandDollarPrototypeIDX >= 0) {
                                overlayProtoID = sandDollarPrototypeIDX;
                                overlayPlantType = PlantType::None;
                            } else if (marker == kWaterFoliageMarkerSandDollarZ && sandDollarPrototypeIDZ >= 0) {
                                overlayProtoID = sandDollarPrototypeIDZ;
                                overlayPlantType = PlantType::None;
                            }

                            if (overlayProtoID >= 0) {
                                waterloggedFoliageCells[idx] = {
                                    true,
                                    false,
                                    overlayPlantType,
                                    SlopeDir::None,
                                    overlayProtoID,
                                    glm::vec3(1.0f),
                                    packedColor
                                };
                            }
                        } else if (isSlopePrototype(proto)) {
                            slopeCells[idx] = {true, false, PlantType::None, slopeDirForPrototype(proto), static_cast<int>(id), color, packedColor};
                        } else {
                            const bool fanLeaf = isLeafFanPlantPrototype(proto);
                            const bool renderAsLeaf = isLeafPrototype(proto) && (!fanLeaf || leafFanRenderInnerBlock);
                            const bool renderAsPlant = isPlantPrototype(proto);
                            if (renderAsLeaf) {
                                leafCells[idx] = {true, true, PlantType::None, SlopeDir::None, static_cast<int>(id), color, packedColor};
                            }
                            if (renderAsPlant) {
                                plantCells[idx] = {true, false, plantTypeForPrototype(proto), SlopeDir::None, static_cast<int>(id), color, packedColor};
                            }
                            if (!renderAsLeaf && !renderAsPlant) {
                                solidCells[idx] = {true, false, PlantType::None, SlopeDir::None, static_cast<int>(id), color, packedColor};
                            }
                        }
                    }
                }
            }

            auto neighborTypeAt = [&](const glm::ivec3& lodCoord) {
                glm::ivec3 local = lodCoord - minCoord;
                glm::ivec3 snapLocal = local + glm::ivec3(1);
                int type = 0;
                if (snapInBounds(snapLocal)) {
                    int idx = snapIndex(snapLocal.x, snapLocal.y, snapLocal.z);
                    uint32_t id = snap.ids[idx];
                    classifyProto(id, type);
                    return type;
                }
                return 0;
            };
            auto oppositeFaceType = [](int faceType) {
                switch (faceType) {
                    case 0: return 1;
                    case 1: return 0;
                    case 2: return 3;
                    case 3: return 2;
                    case 4: return 5;
                    case 5: return 4;
                    default: return faceType;
                }
            };
            auto slopeDirAt = [&](const glm::ivec3& lodCoord) {
                glm::ivec3 local = lodCoord - minCoord;
                glm::ivec3 snapLocal = local + glm::ivec3(1);
                if (!snapInBounds(snapLocal)) return SlopeDir::None;
                const int idx = snapIndex(snapLocal.x, snapLocal.y, snapLocal.z);
                if (idx < 0) return SlopeDir::None;
                const uint32_t id = snap.ids[static_cast<size_t>(idx)];
                if (id == 0 || id >= prototypes.size()) return SlopeDir::None;
                return slopeDirForPrototype(prototypes[static_cast<size_t>(id)]);
            };
            auto slopeOccludesSharedFace = [&](SlopeDir dir, int sharedFaceTypeFromCurrent) {
                if (dir == SlopeDir::None) return false;
                const int slopeFaceType = oppositeFaceType(sharedFaceTypeFromCurrent);
                if (isCornerSlopeDir(dir)) return true;
                if (slopeFaceType == 2 || slopeFaceType == 3) return true;
                if (slopeFaceType == slopeTallFace(dir)) return true;
                int capFaceA = 4;
                int capFaceB = 5;
                float capAlphaA = 0.0f;
                float capAlphaB = 0.0f;
                waterSlopeCapFacesAndAlpha(dir, capFaceA, capAlphaA, capFaceB, capAlphaB);
                (void)capAlphaA;
                (void)capAlphaB;
                return slopeFaceType == capFaceA || slopeFaceType == capFaceB;
            };
            auto waterSlopeNeighborOccludesFace = [&](const glm::ivec3& slopeCoord, int sharedFaceTypeFromCurrent) {
                const SlopeDir dir = slopeDirAt(slopeCoord);
                return slopeOccludesSharedFace(dir, sharedFaceTypeFromCurrent);
            };

            auto isOccluderAt = [&](const glm::ivec3& lodCoord) {
                glm::ivec3 local = lodCoord - minCoord;
                glm::ivec3 snapLocal = local + glm::ivec3(1);
                if (!snapInBounds(snapLocal)) return false;
                int idx = snapIndex(snapLocal.x, snapLocal.y, snapLocal.z);
                uint32_t id = snap.ids[idx];
                int type = 0;
                classifyProto(id, type);
                return isSolidOccluderType(type);
            };
            auto isChalkDustAt = [&](const glm::ivec3& lodCoord) {
                glm::ivec3 local = lodCoord - minCoord;
                glm::ivec3 snapLocal = local + glm::ivec3(1);
                if (!snapInBounds(snapLocal)) return false;
                const int idx = snapIndex(snapLocal.x, snapLocal.y, snapLocal.z);
                const uint32_t id = snap.ids[static_cast<size_t>(idx)];
                if (id == 0 || id >= prototypes.size()) return false;
                return isChalkDustPrototype(prototypes[static_cast<size_t>(id)]);
            };
            auto isExposedChalkDustAt = [&](const glm::ivec3& lodCoord) {
                if (!isChalkDustAt(lodCoord)) return false;
                const int aboveType = neighborTypeAt(lodCoord + glm::ivec3(0, 1, 0));
                return !isSolidOccluderType(aboveType);
            };

            auto computeFaceAo = [&](const glm::ivec3& lodCoord, int faceType) {
                glm::ivec3 uAxis = faceUAxisForAo(faceType);
                glm::ivec3 vAxis = faceVAxisForAo(faceType);
                auto cornerAo = [&](int uSign, int vSign) {
                    glm::ivec3 side1 = uAxis * uSign;
                    glm::ivec3 side2 = vAxis * vSign;
                    bool s1 = isOccluderAt(lodCoord + side1);
                    bool s2 = isOccluderAt(lodCoord + side2);
                    bool c = isOccluderAt(lodCoord + side1 + side2);
                    return applyAoStrength(bakedAoValue(s1, s2, c), aoStrength);
                };
                return glm::vec4(
                    cornerAo(-1, -1), // uv (0,0)
                    cornerAo(1, -1),  // uv (1,0)
                    cornerAo(1, 1),   // uv (1,1)
                    cornerAo(-1, 1)   // uv (0,1)
                );
            };
            auto isSlopeAoOccluderAt = [&](const glm::ivec3& lodCoord) {
                glm::ivec3 local = lodCoord - minCoord;
                glm::ivec3 snapLocal = local + glm::ivec3(1);
                if (!snapInBounds(snapLocal)) return false;
                int idx = snapIndex(snapLocal.x, snapLocal.y, snapLocal.z);
                uint32_t id = snap.ids[idx];
                int type = 0;
                classifyProto(id, type);
                return type == 1 || type == 6;
            };
            auto computeSlopeFaceAo = [&](const glm::ivec3& lodCoord, int faceType, float alpha, SlopeDir dir) {
                (void)alpha;
                if (faceType != 2 || dir == SlopeDir::None) {
                    return computeFaceAo(lodCoord, faceType);
                }

                const glm::ivec3 high = slopeHighDirection(dir);
                if (high == glm::ivec3(0)) {
                    return computeFaceAo(lodCoord, faceType);
                }
                const glm::ivec3 low = -high;
                const glm::ivec3 perp = (dir == SlopeDir::PosX || dir == SlopeDir::NegX)
                    ? glm::ivec3(0, 0, 1)
                    : glm::ivec3(1, 0, 0);

                auto cornerAoFromUv = [&](int uSign, int vSign) {
                    int alongSign = -1;
                    int perpSign = 1;
                    switch (dir) {
                        case SlopeDir::PosX:
                            alongSign = (uSign < 0) ? 1 : -1;
                            perpSign = vSign;
                            break;
                        case SlopeDir::NegX:
                            alongSign = (uSign > 0) ? 1 : -1;
                            perpSign = vSign;
                            break;
                        case SlopeDir::PosZ:
                            alongSign = (vSign > 0) ? 1 : -1;
                            perpSign = uSign;
                            break;
                        case SlopeDir::NegZ:
                            alongSign = (vSign < 0) ? 1 : -1;
                            perpSign = uSign;
                            break;
                        default:
                            break;
                    }

                    const glm::ivec3 alongOffset = (alongSign > 0) ? high : (low + glm::ivec3(0, -1, 0));
                    const glm::ivec3 sideOffset = perp * perpSign;
                    const bool s1 = isSlopeAoOccluderAt(lodCoord + alongOffset);
                    const bool s2 = isSlopeAoOccluderAt(lodCoord + sideOffset);
                    const bool c = isSlopeAoOccluderAt(lodCoord + alongOffset + sideOffset);
                    return applyAoStrength(bakedAoValue(s1, s2, c), aoStrength);
                };

                return glm::vec4(
                    cornerAoFromUv(-1, -1),
                    cornerAoFromUv(1, -1),
                    cornerAoFromUv(1, 1),
                    cornerAoFromUv(-1, 1)
                );
            };
            auto estimateWaveClassFromNeighborhood = [&](const glm::ivec3& center) -> uint8_t {
                auto spanAlong = [&](const glm::ivec3& axisStep) {
                    constexpr int kMaxProbe = 40;
                    int span = 1;
                    for (int s = 1; s <= kMaxProbe; ++s) {
                        if (neighborTypeAt(center + axisStep * s) != 2) break;
                        span += 1;
                    }
                    for (int s = 1; s <= kMaxProbe; ++s) {
                        if (neighborTypeAt(center - axisStep * s) != 2) break;
                        span += 1;
                    }
                    return span;
                };
                const int spanX = spanAlong(glm::ivec3(1, 0, 0));
                const int spanZ = spanAlong(glm::ivec3(0, 0, 1));
                const int minSpan = std::min(spanX, spanZ);
                const int maxSpan = std::max(spanX, spanZ);
                if (maxSpan <= 12 && minSpan <= 8) return kWaterWaveClassPond;
                if (minSpan <= 10 && maxSpan >= 20) return kWaterWaveClassRiver;
                if (minSpan >= 38 && maxSpan >= 38) return kWaterWaveClassOcean;
                return kWaterWaveClassLake;
            };
            auto resolveLilypadWaveClass = [&](const glm::ivec3& center) -> uint8_t {
                static const std::array<glm::ivec3, 5> kWaterProbes = {
                    glm::ivec3(0, -1, 0),
                    glm::ivec3(0, 0, 0),
                    glm::ivec3(0, 1, 0),
                    glm::ivec3(1, -1, 0),
                    glm::ivec3(0, -1, 1)
                };
                for (const glm::ivec3& step : kWaterProbes) {
                    const glm::ivec3 probe = center + step;
                    const glm::ivec3 local = probe - minCoord;
                    if (local.x < 0 || local.y < 0 || local.z < 0
                        || local.x >= sizeX || local.y >= sizeY || local.z >= sizeZ) {
                        continue;
                    }
                    const CellInfo& waterCell = waterCells[cellIndex(local)];
                    if (!waterCell.filled) continue;
                    const uint8_t waveClass = decodeWaterWaveClass(waterCell.packedColor);
                    if (waveClass >= kWaterWaveClassPond && waveClass <= kWaterWaveClassOcean) {
                        return waveClass;
                    }
                }
                return estimateWaveClassFromNeighborhood(center + glm::ivec3(0, -1, 0));
            };

            auto sameColor = [](const glm::vec3& a, const glm::vec3& b) {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            };
            auto sameAo = [](const glm::vec4& a, const glm::vec4& b) {
                return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
            };
            auto sameKey = [&](const MaskCell& a, const MaskCell& b) {
                // Emit grass-cover/blueprint mats per-cell; do not greedy-merge across neighbors.
                if (a.narrowShape == NarrowShape::GrassCover
                    || b.narrowShape == NarrowShape::GrassCover
                    || a.narrowShape == NarrowShape::BlueprintMat
                    || b.narrowShape == NarrowShape::BlueprintMat
                    || a.narrowShape == NarrowShape::ChalkDust
                    || b.narrowShape == NarrowShape::ChalkDust
                    || a.narrowShape == NarrowShape::Book
                    || b.narrowShape == NarrowShape::Book
                    || a.narrowShape == NarrowShape::Lantern
                    || b.narrowShape == NarrowShape::Lantern
                    || a.narrowShape == NarrowShape::DepthCrystal
                    || b.narrowShape == NarrowShape::DepthCrystal
                    || a.narrowShape == NarrowShape::StonePebble
                    || b.narrowShape == NarrowShape::StonePebble
                    || a.narrowShape == NarrowShape::WallDecal
                    || b.narrowShape == NarrowShape::WallDecal) return false;
                return a.filled && b.filled && a.isLeaf == b.isLeaf
                    && a.plantType == b.plantType
                    && a.narrowLog == b.narrowLog
                    && a.narrowShape == b.narrowShape
                    && a.narrowMount == b.narrowMount
                    && a.narrowAxis == b.narrowAxis
                    && a.protoID == b.protoID
                    && a.alpha == b.alpha
                    && a.tileIndex == b.tileIndex && sameColor(a.color, b.color) && sameAo(a.ao, b.ao);
            };
            const bool waterTopOnlyOutsideLod0 = snap.waterTopOnlyOutsideLod0;
            std::unordered_set<glm::ivec3, IVec3Hash> damagedCells;
            if (snap.lod == 0 && !snap.damagedCells.empty()) {
                damagedCells.reserve(snap.damagedCells.size());
                for (const glm::ivec3& cell : snap.damagedCells) {
                    damagedCells.insert(cell);
                }
            }
            auto isDamagedCell = [&](const glm::ivec3& worldCell) {
                return damagedCells.find(worldCell) != damagedCells.end();
            };
            auto snapshotKnownAt = [&](const glm::ivec3& lodCoord) {
                glm::ivec3 local = lodCoord - minCoord;
                glm::ivec3 snapLocal = local + glm::ivec3(1);
                if (!snapInBounds(snapLocal)) return false;
                int idx = snapIndex(snapLocal.x, snapLocal.y, snapLocal.z);
                if (idx < 0 || idx >= static_cast<int>(snap.known.size())) return false;
                return snap.known[static_cast<size_t>(idx)] != 0;
            };

            auto buildPass = [&](const std::vector<CellInfo>& cellData, float alpha, int passType, bool allowGreedyMerge) {
                for (int faceType = 0; faceType < 6; ++faceType) {
                    if (passType == 1 && waterTopOnlyOutsideLod0 && snap.lod > 0 && faceType != 2) {
                        // Match runtime path: keep far water as a surface sheet.
                        continue;
                    }
                    int sliceLen = 0;
                    int uLen = 0;
                    int vLen = 0;
                    switch (faceType) {
                        case 0:
                        case 1:
                            sliceLen = sizeX; uLen = sizeZ; vLen = sizeY; break;
                        case 2:
                        case 3:
                            sliceLen = sizeY; uLen = sizeX; vLen = sizeZ; break;
                        case 4:
                        case 5:
                            sliceLen = sizeZ; uLen = sizeX; vLen = sizeY; break;
                        default:
                            break;
                    }
                    if (sliceLen <= 0 || uLen <= 0 || vLen <= 0) continue;

                    std::vector<MaskCell> mask(static_cast<size_t>(uLen * vLen));
                    for (int slice = 0; slice < sliceLen; ++slice) {
                        for (auto& cell : mask) {
                            cell.filled = false;
                        }
                        for (int v = 0; v < vLen; ++v) {
                            for (int u = 0; u < uLen; ++u) {
                                glm::ivec3 local = VoxelMeshInitSystemLogic::LocalCellFromUV(faceType, slice, u, v);
                                if (local.x < 0 || local.y < 0 || local.z < 0
                                    || local.x >= sizeX || local.y >= sizeY || local.z >= sizeZ) continue;
                                const CellInfo& cell = cellData[cellIndex(local)];
                                if (!cell.filled) continue;
                                if (cell.protoID < 0 || cell.protoID >= static_cast<int>(prototypes.size())) continue;
                                const Entity& proto = prototypes[cell.protoID];
                                NarrowLogAxis currentAxis = narrowLogAxis(proto);
                                bool currentNarrow = currentAxis != NarrowLogAxis::None;
                                NarrowShape currentShape = narrowShapeForPrototype(proto);
                                NarrowMount currentMount = narrowMountForPrototype(proto);
                                if (currentShape == NarrowShape::WallDecal
                                    && !wallDecalFaceEnabled(currentMount, faceType)) {
                                    continue;
                                }
                                if (currentShape == NarrowShape::ChalkDust
                                    && !chalkDustFaceEnabled(faceType)) {
                                    continue;
                                }
                                if (currentShape == NarrowShape::BigLilypad
                                    && !bigLilypadFaceEnabled(faceType)) {
                                    continue;
                                }
                                if (currentShape == NarrowShape::DepthCrystal
                                    && !depthCrystalFaceEnabled(faceType)) {
                                    continue;
                                }
                                glm::ivec3 lodCoord = minCoord + local;
                                glm::ivec3 neighborCoord = lodCoord + VoxelMeshInitSystemLogic::FaceNormal(faceType);
                                int neighborType = neighborTypeAt(neighborCoord);
                                if (passType == 1) {
                                    if (neighborType == 0 && !snapshotKnownAt(neighborCoord)) {
                                        continue;
                                    }
                                    if (neighborType == 1 || neighborType == 2) continue;
                                    if (neighborType == 6
                                        && waterSlopeNeighborOccludesFace(neighborCoord, faceType)) {
                                        continue;
                                    }
                                } else if (passType == 2) {
                                    // Keep runtime/snapshot meshing behavior aligned.
                                    if (neighborType == 1 || neighborType == 3) continue;
                                } else if (passType == 3) {
                                    if (isSandDollarName(proto.name)) {
                                        if (faceType != 2) continue;
                                    } else if (!(faceType == 0 || faceType == 1 || faceType == 4 || faceType == 5)) {
                                        continue;
                                    }
                                } else {
                                    bool neighborFull = isSolidOccluderType(neighborType);
                                    bool neighborNarrow = (neighborType == 4);
                                    const bool currentDamaged = isDamagedCell(lodCoord);
                                    const bool neighborDamaged = isDamagedCell(neighborCoord);
                                    const bool emitCrackBackingFace = neighborFull && !currentDamaged && neighborDamaged;
                                    bool faceAlongAxis = (currentAxis == NarrowLogAxis::Y && (faceType == 2 || faceType == 3))
                                        || (currentAxis == NarrowLogAxis::X && (faceType == 0 || faceType == 1))
                                        || (currentAxis == NarrowLogAxis::Z && (faceType == 4 || faceType == 5));
                                    if (!currentNarrow) {
                                        if (neighborFull && !emitCrackBackingFace) continue;
                                    } else {
                                        if (faceAlongAxis && (neighborFull || neighborNarrow)) continue;
                                    }
                                }
                                int tileIndex = ::RenderInitSystemLogic::FaceTileIndexFor(snap.worldCtx, proto, faceType);
                                if (isVoidPortalPrototypeName(proto.name)) {
                                    tileIndex = voidPortalTileIndexForFace(lodCoord, faceType);
                                }
                                if (passType == 2 && isLeafFanPlantPrototype(proto)) {
                                    int baseLeafPrototypeID = -1;
                                    if (proto.name == "GrassTuftLeafFanPine") {
                                        baseLeafPrototypeID = pineLeafPrototypeID;
                                    } else if (proto.name == "GrassTuftLeafFanOak") {
                                        baseLeafPrototypeID = oakLeafPrototypeID;
                                    }
                                    if (baseLeafPrototypeID >= 0
                                        && baseLeafPrototypeID < static_cast<int>(prototypes.size())) {
                                        tileIndex = ::RenderInitSystemLogic::FaceTileIndexFor(
                                            snap.worldCtx,
                                            prototypes[static_cast<size_t>(baseLeafPrototypeID)],
                                            faceType
                                        );
                                    }
                                }
                                if (currentShape == NarrowShape::GrassCover && currentMount == NarrowMount::Floor) {
                                    const int snapshotTile = decodeGrassCoverSnapshotTile(cell.packedColor);
                                    if (snapshotTile >= 0) {
                                        tileIndex = snapshotTile;
                                    } else {
                                        glm::ivec3 snapLocal = local + glm::ivec3(1);
                                        glm::ivec3 supportSnapLocal = snapLocal + glm::ivec3(0, -1, 0);
                                        if (snapInBounds(supportSnapLocal)) {
                                            int supportIdx = snapIndex(supportSnapLocal.x, supportSnapLocal.y, supportSnapLocal.z);
                                            uint32_t supportId = snap.ids[static_cast<size_t>(supportIdx)];
                                            if (supportId > 0 && supportId < static_cast<uint32_t>(prototypes.size())) {
                                                const Entity& supportProto = prototypes[static_cast<size_t>(supportId)];
                                                const int supportTopTile = ::RenderInitSystemLogic::FaceTileIndexFor(
                                                    snap.worldCtx,
                                                    supportProto,
                                                    2
                                                );
                                                if (supportTopTile >= 0) {
                                                    tileIndex = supportTopTile;
                                                }
                                            }
                                        }
                                    }
                                }
                                if (currentShape == NarrowShape::Default
                                    && tileIndex >= 0
                                    && isDesertSandPrototype(proto)) {
                                    tileIndex += desertSandQuarterTurnsForCell(lodCoord) * kChalkTileEncodeStride;
                                }
                                if (currentShape == NarrowShape::ChalkDust && currentMount == NarrowMount::Floor) {
                                    int chalkQuarterTurns = 0;
                                    const bool north = isExposedChalkDustAt(lodCoord + glm::ivec3(0, 0, -1));
                                    const bool east = isExposedChalkDustAt(lodCoord + glm::ivec3(1, 0, 0));
                                    const bool south = isExposedChalkDustAt(lodCoord + glm::ivec3(0, 0, 1));
                                    const bool west = isExposedChalkDustAt(lodCoord + glm::ivec3(-1, 0, 0));
                                    const int chalkTile = chooseChalkDustTileFromNeighbors(north, east, south, west, chalkQuarterTurns);
                                    tileIndex = chalkTile + (chalkQuarterTurns * kChalkTileEncodeStride);
                                }
                                if (currentShape == NarrowShape::WallDecal) {
                                    tileIndex += wallDecalQuarterTurnsForCell(lodCoord) * kChalkTileEncodeStride;
                                }
                                if (currentShape == NarrowShape::Book) {
                                    // Use paper texture on side faces; keep blueprint cover on top/bottom.
                                    if (faceType == 0 || faceType == 1 || faceType == 4 || faceType == 5) {
                                        tileIndex = kBookPageTileIndex;
                                    }
                                }
                                if (passType == 3 && cell.plantType == PlantType::Flower && tileIndex < 0) {
                                    // Legacy flowers with no atlas tile stay procedural in shader space.
                                    tileIndex = -1;
                                }
                                int idx = v * uLen + u;
                                mask[idx].filled = true;
                                mask[idx].isLeaf = cell.isLeaf;
                                mask[idx].plantType = cell.plantType;
                                mask[idx].narrowLog = currentNarrow;
                                mask[idx].narrowShape = currentShape;
                                mask[idx].narrowMount = currentMount;
                                mask[idx].narrowAxis = currentAxis;
                                mask[idx].cellCoord = lodCoord;
                                mask[idx].protoID = cell.protoID;
                                mask[idx].tileIndex = tileIndex;
                                mask[idx].packedColor = cell.packedColor;
                                float encodedAlpha = (passType == 3)
                                    ? alphaForPlantType(cell.plantType)
                                    : alpha;
                                if (passType != 3 && currentShape == NarrowShape::WallDecal) {
                                    encodedAlpha = kWallDecalAlpha;
                                } else if (passType != 3 && currentShape == NarrowShape::GrassCover) {
                                    encodedAlpha = kGrassCoverAlpha;
                                } else if (passType != 3 && currentShape == NarrowShape::ChalkDust) {
                                    encodedAlpha = kChalkDustAlpha;
                                } else if (passType != 3
                                           && currentShape == NarrowShape::Book
                                           && (faceType == 0 || faceType == 1 || faceType == 4 || faceType == 5)) {
                                    encodedAlpha = kBookPageAlpha;
                                }
                                if (passType == 3 && isSandDollarName(proto.name)) {
                                    encodedAlpha = kGrassCoverAlpha;
                                }
                                if (passType != 3 && isLavaName(proto.name)) {
                                    encodedAlpha = kLavaSurfaceAlpha;
                                }
                                mask[idx].alpha = encodedAlpha;
                                const bool applyVoxelLighting = voxelLightingEnabled
                                    && snap.lod == 0
                                    && (passType == 0
                                        || passType == 2
                                        || passType == 3
                                        || (passType == 1 && voxelLightingAffectWater));
                                const std::array<uint8_t, 2> lightLevels = lightLevelsAt(neighborCoord);
                                const bool texturedFace = tileIndex >= 0;
                                const glm::vec3 baseFaceColor = texturedFace ? glm::vec3(1.0f) : cell.color;
                                if (applyVoxelLighting) {
                                    const float lightFactor = lightFactorFromLevel(std::max(lightLevels[0], lightLevels[1]));
                                    mask[idx].color = applyLightingDebugColor(baseFaceColor * lightFactor, lightLevels[0], lightLevels[1]);
                                } else {
                                    mask[idx].color = applyLightingDebugColor(baseFaceColor, lightLevels[0], lightLevels[1]);
                                }
                                const bool applyAo = !aoDisabled
                                    && (passType == 0
                                        || (passType == 2 && leafAoEnabled)
                                        || (passType == 3 && plantAoEnabled));
                                glm::vec4 faceAo = applyAo ? computeFaceAo(lodCoord, faceType) : glm::vec4(1.0f);
                                if (passType == 2) {
                                    faceAo = glm::vec4(1.0f) + (faceAo - glm::vec4(1.0f)) * leafAoStrength;
                                } else if (passType == 3) {
                                    faceAo = glm::vec4(1.0f) + (faceAo - glm::vec4(1.0f)) * plantAoStrength;
                                }
                                if (passType == 0
                                    && (currentShape == NarrowShape::PetalPile
                                        || currentShape == NarrowShape::BigLilypad)
                                    && isLilypadName(proto.name)) {
                                    const uint8_t waveClass = resolveLilypadWaveClass(lodCoord);
                                    if (waveClass >= kWaterWaveClassPond && waveClass <= kWaterWaveClassOcean) {
                                        faceAo += glm::vec4(kWaterWaveClassAoEncodeStride * static_cast<float>(waveClass));
                                    }
                                }
                                if (faceType == 2) {
                                    if (passType == 0) {
                                        const int aboveType = neighborTypeAt(lodCoord + glm::ivec3(0, 1, 0));
                                        if (aboveType == 2) {
                                            faceAo += glm::vec4(kUnderwaterCausticsAoFlag);
                                        }
                                    } else if (passType == 1) {
                                        uint8_t waveClass = decodeWaterWaveClass(cell.packedColor);
                                        if (waveClass == kWaterWaveClassUnknown) {
                                            waveClass = estimateWaveClassFromNeighborhood(lodCoord);
                                        }
                                        if (waveClass >= kWaterWaveClassPond && waveClass <= kWaterWaveClassOcean) {
                                            faceAo += glm::vec4(kWaterWaveClassAoEncodeStride * static_cast<float>(waveClass));
                                        }
                                        bool shoreline = false;
                                        const std::array<glm::ivec3, 4> sideOffsets{
                                            glm::ivec3(1, 0, 0),
                                            glm::ivec3(-1, 0, 0),
                                            glm::ivec3(0, 0, 1),
                                            glm::ivec3(0, 0, -1)
                                        };
                                        for (const glm::ivec3& side : sideOffsets) {
                                            const int sideType = neighborTypeAt(lodCoord + side);
                                            if (isSolidOccluderType(sideType)) {
                                                shoreline = true;
                                                break;
                                            }
                                        }
                                        if (shoreline) {
                                            faceAo += glm::vec4(kWaterShorelineAoFlag);
                                        }
                                    }
                                } else if (passType == 1
                                    && (faceType == 0 || faceType == 1 || faceType == 4 || faceType == 5)) {
                                    const int aboveType = neighborTypeAt(lodCoord + glm::ivec3(0, 1, 0));
                                    const int belowType = neighborTypeAt(lodCoord + glm::ivec3(0, -1, 0));
                                    const bool aboveWaterLike = (aboveType == 2 || aboveType == 6);
                                    const bool belowWaterLike = (belowType == 2 || belowType == 6);
                                    if (aboveWaterLike && belowWaterLike) {
                                        faceAo += glm::vec4(kWaterfallFoamAoFlag);
                                    }
                                }
                                mask[idx].ao = faceAo;
                            }
                        }

                        for (int v = 0; v < vLen; ++v) {
                            for (int u = 0; u < uLen; ++u) {
                                int idx = v * uLen + u;
                                if (!mask[idx].filled) continue;
                                MaskCell seed = mask[idx];
                                int width = 1;
                                int height = 1;
                                if (allowGreedyMerge) {
                                    while (u + width < uLen && sameKey(seed, mask[v * uLen + (u + width)])) {
                                        ++width;
                                    }
                                    bool done = false;
                                    while (v + height < vLen && !done) {
                                        for (int k = 0; k < width; ++k) {
                                            if (!sameKey(seed, mask[(v + height) * uLen + (u + k)])) {
                                                done = true;
                                                break;
                                            }
                                        }
                                        if (!done) ++height;
                                    }
                                }

                                for (int dv = 0; dv < height; ++dv) {
                                    for (int du = 0; du < width; ++du) {
                                        mask[(v + dv) * uLen + (u + du)].filled = false;
                                    }
                                }

                                float centerU = static_cast<float>(u) + (static_cast<float>(width - 1) * 0.5f);
                                float centerV = static_cast<float>(v) + (static_cast<float>(height - 1) * 0.5f);
                                float axisOffset = (faceType % 2 == 0) ? 0.5f : -0.5f;
                                NarrowHalfExtents narrowExt = narrowHalfExtentsForShape(seed.narrowAxis, seed.narrowShape, seed.narrowMount);
                                if (seed.narrowShape == NarrowShape::GrassCover) {
                                    constexpr float kDotHalf = 1.0f / 48.0f;
                                    narrowExt = {kDotHalf, kDotHalf, kDotHalf};
                                }
                                if (seed.plantType != PlantType::None) {
                                    axisOffset = 0.0f;
                                } else if (seed.narrowLog) {
                                    float halfExtent = 0.5f;
                                    if (faceType == 0 || faceType == 1) halfExtent = narrowExt.x;
                                    else if (faceType == 2 || faceType == 3) halfExtent = narrowExt.y;
                                    else if (faceType == 4 || faceType == 5) halfExtent = narrowExt.z;
                                    axisOffset = (faceType % 2 == 0) ? halfExtent : -halfExtent;
                                }
                                float axisCoord = static_cast<float>(slice) + axisOffset;
                                glm::vec3 center;
                                switch (faceType) {
                                    case 0:
                                    case 1:
                                        center = glm::vec3(minCoord.x + axisCoord,
                                                           minCoord.y + centerV,
                                                           minCoord.z + centerU);
                                        break;
                                    case 2:
                                    case 3:
                                        center = glm::vec3(minCoord.x + centerU,
                                                           minCoord.y + axisCoord,
                                                           minCoord.z + centerV);
                                        break;
                                    case 4:
                                    case 5:
                                        center = glm::vec3(minCoord.x + centerU,
                                                           minCoord.y + centerV,
                                                           minCoord.z + axisCoord);
                                        break;
                                    default:
                                        center = glm::vec3(minCoord);
                                        break;
                                }
                                center *= static_cast<float>(scale);

                                glm::vec2 scaleVec(static_cast<float>(width * scale), static_cast<float>(height * scale));
                                glm::vec2 uvScaleVec = scaleVec;
                                if (seed.plantType != PlantType::None) {
                                    float plantWidth = 1.00f;
                                    float plantHeight = 1.00f;
                                    bool keepPlantBottomAnchored = true;
                                    const bool isLeafFanPlant =
                                        seed.protoID >= 0
                                        && seed.protoID < static_cast<int>(prototypes.size())
                                        && isLeafFanPlantPrototype(prototypes[static_cast<size_t>(seed.protoID)]);
                                    if (isLeafFanPlant) {
                                        // Keep snapshot meshing behavior identical to runtime meshing.
                                        plantWidth = 3.00f;
                                        plantHeight = 3.00f;
                                        keepPlantBottomAnchored = false;
                                    } else if (seed.plantType == PlantType::Flower) {
                                        // Textured flowers use full grass-card profile; legacy procedural flowers stay compact.
                                        if (seed.tileIndex < 0) {
                                            plantWidth = 0.86f;
                                            plantHeight = 0.92f;
                                        }
                                    } else if (seed.plantType == PlantType::GrassShort) {
                                        plantWidth = 1.00f;
                                        plantHeight = 1.00f;
                                    }
                                    scaleVec.x *= plantWidth;
                                    scaleVec.y *= plantHeight;
                                    uvScaleVec = glm::vec2(1.0f);
                                    if (keepPlantBottomAnchored) {
                                        center.y += (plantHeight - 1.0f) * 0.5f * static_cast<float>(scale);
                                    }
                                } else if (seed.narrowLog) {
                                    float uScale = 1.0f;
                                    float vScale = 1.0f;
                                    if (faceType == 0 || faceType == 1) {
                                        uScale = narrowExt.z * 2.0f; // U axis is Z.
                                        vScale = narrowExt.y * 2.0f; // V axis is Y.
                                    } else if (faceType == 2 || faceType == 3) {
                                        uScale = narrowExt.x * 2.0f; // U axis is X.
                                        vScale = narrowExt.z * 2.0f; // V axis is Z.
                                    } else if (faceType == 4 || faceType == 5) {
                                        uScale = narrowExt.x * 2.0f; // U axis is X.
                                        vScale = narrowExt.y * 2.0f; // V axis is Y.
                                    }
                                    scaleVec.x *= uScale;
                                    scaleVec.y *= vScale;
                                    uvScaleVec.x *= uScale;
                                    uvScaleVec.y *= vScale;
                                }
                                if (seed.narrowShape != NarrowShape::Default) {
                                    if (seed.narrowMount == NarrowMount::WallPosX) {
                                        center.x += (0.5f - narrowExt.x - 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowMount == NarrowMount::WallNegX) {
                                        center.x += (-0.5f + narrowExt.x + 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowMount == NarrowMount::WallPosZ) {
                                        center.z += (0.5f - narrowExt.z - 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowMount == NarrowMount::WallNegZ) {
                                        center.z += (-0.5f + narrowExt.z + 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowMount == NarrowMount::Ceiling) {
                                        center.y += (0.5f - narrowExt.y - 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowShape == NarrowShape::CactusArm) {
                                        // Keep runtime and snapshot meshing in sync for cactus arm curvature.
                                        center.y += (6.0f / 24.0f) * static_cast<float>(scale);
                                    } else if (seed.narrowShape == NarrowShape::CactusJunction) {
                                        // Match runtime path: keep junctions centered.
                                    } else if (seed.narrowShape == NarrowShape::Lantern) {
                                        // Lanterns are full-height slim columns and should stay centered.
                                    } else {
                                        // Keep narrow-prop geometry resting near the floor of its occupied voxel.
                                        center.y += floorMountYOffset(seed.narrowShape, narrowExt) * static_cast<float>(scale);
                                    }
                                }
                                if (seed.narrowShape == NarrowShape::WallDecal) {
                                    center += wallDecalPanOffsetForCell(seed.cellCoord, seed.narrowMount) * static_cast<float>(scale);
                                }
                                if (seed.narrowShape == NarrowShape::DepthCrystal && seed.narrowMount == NarrowMount::Floor) {
                                    if (seed.protoID < 0 || seed.protoID >= static_cast<int>(prototypes.size())) {
                                        continue;
                                    }
                                    const DepthCrystalLayerSet layerSet =
                                        depthCrystalLayersForPrototype(prototypes[static_cast<size_t>(seed.protoID)]);
                                    for (int layerIdx = 0; layerIdx < layerSet.count; ++layerIdx) {
                                        const DepthCrystalLayerSpec& layer = layerSet.layers[layerIdx];
                                        glm::vec3 layerCenter = center;
                                        const float padPx = layer.useBottomPadding ? layer.bottomPaddingPx : 0.0f;
                                        const float padOffset = (padPx / 24.0f) * static_cast<float>(scale);
                                        layerCenter.y -= padOffset;
                                        layerCenter.z -= padOffset;
                                        layerCenter.x += (layer.offsetXPx / 24.0f) * static_cast<float>(scale);
                                        layerCenter.y += (layer.offsetYPx / 24.0f) * static_cast<float>(scale);
                                        layerCenter.z += (layer.offsetZPx / 24.0f) * static_cast<float>(scale);
                                        out.positions.push_back(layerCenter);
                                        out.colors.push_back(seed.color);
                                        out.faceTypes.push_back(faceType);
                                        out.tileIndices.push_back(layer.tileIndex);
                                        out.alphas.push_back(seed.alpha);
                                        out.ao.push_back(seed.ao);
                                        out.scales.push_back(scaleVec);
                                        out.uvScales.push_back(uvScaleVec);
                                    }
                                    continue;
                                }
                                if (seed.narrowShape == NarrowShape::StonePebble
                                    && seed.narrowMount == NarrowMount::Floor
                                    && seed.protoID >= 0
                                    && seed.protoID < static_cast<int>(prototypes.size())
                                    && isSurfaceStonePebbleName(prototypes[static_cast<size_t>(seed.protoID)].name)) {
                                    const int pileCount = decodeSurfaceStonePileCount(seed.packedColor);
                                    const StonePebblePilePieces pile = stonePebblePilePiecesForCell(seed.cellCoord, pileCount);
                                    for (int piece = 0; piece < pile.count; ++piece) {
                                        const glm::vec2 pieceOffset = pile.offsets[static_cast<size_t>(piece)];
                                        const NarrowHalfExtents pieceExt = pile.halfExtents[static_cast<size_t>(piece)];
                                        glm::vec3 pieceCenter = glm::vec3(seed.cellCoord) * static_cast<float>(scale);
                                        pieceCenter.x += pieceOffset.x * static_cast<float>(scale);
                                        pieceCenter.z += pieceOffset.y * static_cast<float>(scale);
                                        pieceCenter.y += (-0.5f + pieceExt.y + 0.01f) * static_cast<float>(scale);

                                        glm::vec3 faceCenter = pieceCenter;
                                        glm::vec2 faceScale(1.0f);
                                        if (faceType == 0 || faceType == 1) {
                                            faceCenter.x += ((faceType == 0) ? pieceExt.x : -pieceExt.x) * static_cast<float>(scale);
                                            faceScale = glm::vec2(
                                                pieceExt.z * 2.0f * static_cast<float>(scale),
                                                pieceExt.y * 2.0f * static_cast<float>(scale)
                                            );
                                        } else if (faceType == 2 || faceType == 3) {
                                            faceCenter.y += ((faceType == 2) ? pieceExt.y : -pieceExt.y) * static_cast<float>(scale);
                                            faceScale = glm::vec2(
                                                pieceExt.x * 2.0f * static_cast<float>(scale),
                                                pieceExt.z * 2.0f * static_cast<float>(scale)
                                            );
                                        } else {
                                            faceCenter.z += ((faceType == 4) ? pieceExt.z : -pieceExt.z) * static_cast<float>(scale);
                                            faceScale = glm::vec2(
                                                pieceExt.x * 2.0f * static_cast<float>(scale),
                                                pieceExt.y * 2.0f * static_cast<float>(scale)
                                            );
                                        }

                                        out.positions.push_back(faceCenter);
                                        out.colors.push_back(seed.color);
                                        out.faceTypes.push_back(faceType);
                                        out.tileIndices.push_back(seed.tileIndex);
                                        out.alphas.push_back(seed.alpha);
                                        out.ao.push_back(seed.ao);
                                        out.scales.push_back(faceScale);
                                        out.uvScales.push_back(faceScale);
                                    }
                                    continue;
                                }
                                if (seed.narrowShape == NarrowShape::GrassCover && seed.narrowMount == NarrowMount::Floor) {
                                    const GrassCoverDots dots = grassCoverDotsForCell(seed.cellCoord);
                                    for (int dot = 0; dot < dots.count; ++dot) {
                                        glm::vec3 dotCenter = center;
                                        const glm::vec2 offset = dots.offsets[static_cast<size_t>(dot)];
                                        dotCenter.x += offset.x * static_cast<float>(scale);
                                        dotCenter.z += offset.y * static_cast<float>(scale);
                                        out.positions.push_back(dotCenter);
                                        out.colors.push_back(seed.color);
                                        out.faceTypes.push_back(faceType);
                                        out.tileIndices.push_back(seed.tileIndex);
                                        out.alphas.push_back(seed.alpha);
                                        out.ao.push_back(seed.ao);
                                        out.scales.push_back(scaleVec);
                                        out.uvScales.push_back(uvScaleVec);
                                    }
                                    continue;
                                }
                                out.positions.push_back(center);
                                out.colors.push_back(seed.color);
                                out.faceTypes.push_back(faceType);
                                out.tileIndices.push_back(seed.tileIndex);
                                out.alphas.push_back(seed.alpha);
                                out.ao.push_back(seed.ao);
                                out.scales.push_back(scaleVec);
                                out.uvScales.push_back(uvScaleVec);
                            }
                        }
                    }
                }
            };

            buildPass(solidCells, 1.0f, 0, true);
            buildPass(waterCells, 0.6f, 1, true);
            const bool includeFoliage = !(snap.cullPlantsBeyondLod0 && snap.lod > 0);
            if (includeFoliage) {
                buildPass(leafCells, -1.0f, 2, false);
                buildPass(plantCells, -2.0f, 3, false);
                buildPass(waterloggedFoliageCells, -2.0f, 3, false);
            }

            auto emitSlopeFace = [&](const glm::ivec3& local,
                                     int faceType,
                                     int tileIndex,
                                     float alpha,
                                     const glm::vec3& color,
                                     const glm::vec4& ao) {
                float axisOffset = (faceType % 2 == 0) ? 0.5f : -0.5f;
                glm::vec3 center;
                switch (faceType) {
                    case 0:
                    case 1:
                        center = glm::vec3(minCoord.x + local.x + axisOffset,
                                           minCoord.y + local.y,
                                           minCoord.z + local.z);
                        break;
                    case 2:
                    case 3:
                        center = glm::vec3(minCoord.x + local.x,
                                           minCoord.y + local.y + axisOffset,
                                           minCoord.z + local.z);
                        break;
                    case 4:
                    case 5:
                        center = glm::vec3(minCoord.x + local.x,
                                           minCoord.y + local.y,
                                           minCoord.z + local.z + axisOffset);
                        break;
                    default:
                        center = glm::vec3(minCoord);
                        break;
                }
                center *= static_cast<float>(scale);
                out.positions.push_back(center);
                out.colors.push_back(color);
                out.faceTypes.push_back(faceType);
                out.tileIndices.push_back(tileIndex);
                out.alphas.push_back(alpha);
                out.ao.push_back(ao);
                out.scales.push_back(glm::vec2(static_cast<float>(scale)));
                out.uvScales.push_back(glm::vec2(static_cast<float>(scale)));
            };

            auto buildSlopePass = [&]() {
                for (int z = 0; z < sizeZ; ++z) {
                    for (int y = 0; y < sizeY; ++y) {
                        for (int x = 0; x < sizeX; ++x) {
                            glm::ivec3 local(x, y, z);
                            const CellInfo& cell = slopeCells[cellIndex(local)];
                            if (!cell.filled || cell.slopeDir == SlopeDir::None) continue;
                            if (cell.protoID < 0 || cell.protoID >= static_cast<int>(prototypes.size())) continue;
                            const Entity& proto = prototypes[cell.protoID];
                            const bool isWaterSlope = isWaterSlopePrototype(proto);
                            glm::ivec3 lodCoord = minCoord + local;

                            auto tryEmit = [&](int faceType, float alpha) {
                                glm::ivec3 neighborCoord = lodCoord + VoxelMeshInitSystemLogic::FaceNormal(faceType);
                                int neighborType = neighborTypeAt(neighborCoord);
                                if (isSolidOccluderType(neighborType)) return;
                                int tileIndex = ::RenderInitSystemLogic::FaceTileIndexFor(snap.worldCtx, proto, faceType);
                                glm::vec4 ao = aoDisabled ? glm::vec4(1.0f) : computeSlopeFaceAo(lodCoord, faceType, alpha, cell.slopeDir);
                                if (faceType == 2) {
                                    const int aboveType = neighborTypeAt(lodCoord + glm::ivec3(0, 1, 0));
                                    if (aboveType == 2) {
                                        ao += glm::vec4(kUnderwaterCausticsAoFlag);
                                    }
                                }
                                if (isWaterSlope) {
                                    uint8_t waveClass = decodeWaterWaveClass(cell.packedColor);
                                    if (waveClass == kWaterWaveClassUnknown) {
                                        waveClass = estimateWaveClassFromNeighborhood(lodCoord);
                                    }
                                    if (waveClass >= kWaterWaveClassPond && waveClass <= kWaterWaveClassOcean) {
                                        ao += glm::vec4(kWaterWaveClassAoEncodeStride * static_cast<float>(waveClass));
                                    }
                                }
                                const bool texturedFace = tileIndex >= 0;
                                glm::vec3 litColor = texturedFace ? glm::vec3(1.0f) : cell.color;
                                const std::array<uint8_t, 2> lightLevels = lightLevelsAt(neighborCoord);
                                if (voxelLightingEnabled && snap.lod == 0) {
                                    const float lightFactor = lightFactorFromLevel(std::max(lightLevels[0], lightLevels[1]));
                                    litColor *= lightFactor;
                                }
                                litColor = applyLightingDebugColor(litColor, lightLevels[0], lightLevels[1]);
                                emitSlopeFace(local, faceType, tileIndex, alpha, litColor, ao);
                            };

                            if (isWaterSlope && isCornerSlopeDir(cell.slopeDir)) {
                                tryEmit(3, kWaterSlopeSurfaceAlpha);
                                tryEmit(0, kWaterSlopeSurfaceAlpha);
                                tryEmit(1, kWaterSlopeSurfaceAlpha);
                                tryEmit(4, kWaterSlopeSurfaceAlpha);
                                tryEmit(5, kWaterSlopeSurfaceAlpha);
                                tryEmit(2, waterSlopeTopAlpha(cell.slopeDir));
                                continue;
                            }

                            tryEmit(3, isWaterSlope ? kWaterSlopeSurfaceAlpha : 1.0f);
                            tryEmit(slopeTallFace(cell.slopeDir), isWaterSlope ? kWaterSlopeSurfaceAlpha : 1.0f);

                            int capFaceA = 4;
                            int capFaceB = 5;
                            float capAlphaA = kSlopeCapAlphaA;
                            float capAlphaB = kSlopeCapAlphaB;
                            if (isWaterSlope) {
                                waterSlopeCapFacesAndAlpha(cell.slopeDir, capFaceA, capAlphaA, capFaceB, capAlphaB);
                            } else {
                                slopeCapFacesAndAlpha(cell.slopeDir, capFaceA, capAlphaA, capFaceB, capAlphaB);
                            }
                            tryEmit(capFaceA, capAlphaA);
                            tryEmit(capFaceB, capAlphaB);
                            tryEmit(2, isWaterSlope ? waterSlopeTopAlpha(cell.slopeDir) : slopeTopAlpha(cell.slopeDir));
                        }
                    }
                }
            };
            buildSlopePass();
            return true;
        }

        size_t resolveGreedyWorkerCount(const BaseSystem& baseSystem) {
            const int configured = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelGreedyWorkerThreads", 0);
            if (configured > 0) {
                return static_cast<size_t>(std::clamp(configured, 1, 16));
            }
            const unsigned int hardwareThreads = std::thread::hardware_concurrency();
            size_t fallbackWorkers = hardwareThreads > 1u ? static_cast<size_t>(hardwareThreads - 1u) : static_cast<size_t>(1u);
            fallbackWorkers = std::clamp<size_t>(fallbackWorkers, 1, 8);
            return fallbackWorkers;
        }

        void ensureGreedyAsyncStarted(const std::vector<Entity>& prototypes, size_t workerCount) {
            workerCount = std::max<size_t>(1, workerCount);
            std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
            g_voxelGreedyAsync.prototypes = &prototypes;
            if (g_voxelGreedyAsync.running) return;
            g_voxelGreedyAsync.stop = false;
            g_voxelGreedyAsync.running = true;
            g_voxelGreedyAsync.workers.reserve(workerCount);
            for (size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
                g_voxelGreedyAsync.workers.emplace_back([]() {
                    while (true) {
                        VoxelGreedySnapshot snap;
                        const std::vector<Entity>* protos = nullptr;
                        {
                            std::unique_lock<std::mutex> lock(g_voxelGreedyAsync.mutex);
                            g_voxelGreedyAsync.cv.wait(lock, []() {
                                return g_voxelGreedyAsync.stop || !g_voxelGreedyAsync.queue.empty();
                            });
                            if (g_voxelGreedyAsync.stop && g_voxelGreedyAsync.queue.empty()) {
                                return;
                            }
                            snap = std::move(g_voxelGreedyAsync.queue.front());
                            g_voxelGreedyAsync.queue.pop_front();
                            protos = g_voxelGreedyAsync.prototypes;
                        }

                        VoxelGreedyResult result;
                        result.renderKey = snap.renderKey;
                        result.versionKey = snap.versionKey;
                        result.renderEditVersion = snap.renderEditVersion;
                        if (protos) {
                            GreedyChunkData mesh;
                            BuildVoxelGreedyMeshFromSnapshot(snap, *protos, mesh);
                            result.empty = mesh.positions.empty();
                            if (!result.empty) {
                                result.mesh = std::move(mesh);
                            }
                        }
                        {
                            std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
                            g_voxelGreedyAsync.results.push_back(std::move(result));
                            g_voxelGreedyAsync.inFlight.erase(snap.renderKey);
                        }
                    }
                });
            }
        }

        bool enqueueGreedySnapshot(const BaseSystem& baseSystem,
                                   const VoxelWorldContext& voxelWorld,
                                   const WorldContext* worldCtx,
                                   const VoxelSectionKey& sectionKey,
                                   int superChunkMinLod,
                                   int superChunkMaxLod,
                                   int superChunkSize,
                                   bool cullPlantsBeyondLod0,
                                   bool leafFanRenderInnerBlock,
                                   bool waterTopOnlyOutsideLod0,
                                   bool disableAo,
                                   bool leafAoEnabled,
                                   float leafAoStrength,
                                   float aoStrength,
                                   bool plantAoEnabled,
                                   float plantAoStrength,
                                   bool lightingEnabled,
                                   bool lightingAffectWater,
                                   float lightingStrength,
                                   float lightingMinBrightness,
                                   float lightingGamma,
                                   int lightingDebugMode,
                                   int lightingSkyFallbackLevel,
                                   VoxelGreedySnapshot& out) {
            int lod = sectionKey.lod;
            int size = VoxelMeshInitSystemLogic::SectionSizeForLod(voxelWorld, lod);
            bool useSuperChunk = lod >= superChunkMinLod
                && lod <= superChunkMaxLod
                && superChunkSize > 1;
            int chunkSize = useSuperChunk ? superChunkSize : 1;
            glm::ivec3 anchorCoord = sectionKey.coord;
            if (useSuperChunk) {
                anchorCoord.x = VoxelMeshInitSystemLogic::FloorDivInt(anchorCoord.x, superChunkSize) * superChunkSize;
                anchorCoord.z = VoxelMeshInitSystemLogic::FloorDivInt(anchorCoord.z, superChunkSize) * superChunkSize;
            }
            VoxelSectionKey renderKey{lod, anchorCoord};

            uint64_t versionKey = computeGreedyVersionKey(voxelWorld, lod, anchorCoord, chunkSize);
            if (versionKey == 0) return false;

            int sizeX = size * chunkSize;
            int sizeY = size;
            int sizeZ = size * chunkSize;
            int dimX = sizeX + 2;
            int dimY = sizeY + 2;
            int dimZ = sizeZ + 2;
            glm::ivec3 minCoord = anchorCoord * size;
            glm::ivec3 origin = minCoord - glm::ivec3(1, 1, 1);
            const uint8_t clampedSkyFallback = static_cast<uint8_t>(std::clamp(lightingSkyFallbackLevel, 0, 15));

            std::vector<uint32_t> ids(static_cast<size_t>(dimX * dimY * dimZ), 0);
            std::vector<uint32_t> colors(static_cast<size_t>(dimX * dimY * dimZ), 0);
            std::vector<uint8_t> skyLights(static_cast<size_t>(dimX * dimY * dimZ), clampedSkyFallback);
            std::vector<uint8_t> blockLights(static_cast<size_t>(dimX * dimY * dimZ), static_cast<uint8_t>(0));
            std::vector<uint8_t> known(static_cast<size_t>(dimX * dimY * dimZ), 0);

            auto inBounds = [&](int x, int y, int z) {
                return x >= 0 && y >= 0 && z >= 0 && x < dimX && y < dimY && z < dimZ;
            };
            auto dstIndex = [&](int x, int y, int z) {
                return (x * dimY + y) * dimZ + z;
            };

            bool anyFound = false;
            for (int sz = -1; sz <= chunkSize; ++sz) {
                for (int sx = -1; sx <= chunkSize; ++sx) {
                    for (int sy = -1; sy <= 1; ++sy) {
                        glm::ivec3 coord(anchorCoord.x + sx, anchorCoord.y + sy, anchorCoord.z + sz);
                        VoxelSectionKey key{lod, coord};
                        auto it = voxelWorld.sections.find(key);
                        if (it == voxelWorld.sections.end()) continue;
                        const VoxelSection& src = it->second;
                        anyFound = true;
                        glm::ivec3 base = coord * src.size;
                        glm::ivec3 offset = base - origin;
                        for (int z = 0; z < src.size; ++z) {
                            for (int y = 0; y < src.size; ++y) {
                                for (int x = 0; x < src.size; ++x) {
                                    int dx = offset.x + x;
                                    int dy = offset.y + y;
                                    int dz = offset.z + z;
                                    if (!inBounds(dx, dy, dz)) continue;
                                    int srcIdx = x + y * src.size + z * src.size * src.size;
                                    int dstIdx = dstIndex(dx, dy, dz);
                                    ids[dstIdx] = src.ids[srcIdx];
                                    colors[dstIdx] = src.colors[srcIdx];
                                    if (srcIdx >= 0 && srcIdx < static_cast<int>(src.skyLight.size())) {
                                        skyLights[dstIdx] = src.skyLight[static_cast<size_t>(srcIdx)];
                                    }
                                    if (srcIdx >= 0 && srcIdx < static_cast<int>(src.blockLight.size())) {
                                        blockLights[dstIdx] = src.blockLight[static_cast<size_t>(srcIdx)];
                                    }
                                    known[dstIdx] = 1;
                                }
                            }
                        }
                    }
                }
            }
            if (!anyFound) return false;

            out.renderKey = renderKey;
            out.lod = lod;
            out.sizeX = sizeX;
            out.sizeY = sizeY;
            out.sizeZ = sizeZ;
            out.dimX = dimX;
            out.dimY = dimY;
            out.dimZ = dimZ;
            out.minCoord = minCoord;
            out.versionKey = versionKey;
            auto secIt = voxelWorld.sections.find(renderKey);
            out.renderEditVersion = (secIt != voxelWorld.sections.end()) ? secIt->second.editVersion : 0;
            out.cullPlantsBeyondLod0 = cullPlantsBeyondLod0;
            out.leafFanRenderInnerBlock = leafFanRenderInnerBlock;
            out.waterTopOnlyOutsideLod0 = waterTopOnlyOutsideLod0;
            out.disableAo = disableAo;
            out.leafAoEnabled = leafAoEnabled;
            out.leafAoStrength = std::clamp(leafAoStrength, 0.0f, 1.0f);
            out.aoStrength = std::clamp(aoStrength, 0.0f, 1.0f);
            out.plantAoEnabled = plantAoEnabled;
            out.plantAoStrength = std::clamp(plantAoStrength, 0.0f, 1.0f);
            out.lightingEnabled = lightingEnabled;
            out.lightingAffectWater = lightingAffectWater;
            out.lightingStrength = std::clamp(lightingStrength, 0.0f, 1.0f);
            out.lightingMinBrightness = std::clamp(lightingMinBrightness, 0.0f, 1.0f);
            out.lightingGamma = std::clamp(lightingGamma, 0.25f, 4.0f);
            out.lightingDebugMode = std::clamp(lightingDebugMode, 0, 4);
            out.lightingSkyFallbackLevel = clampedSkyFallback;
            out.worldCtx = worldCtx;
            out.ids = std::move(ids);
            out.colors = std::move(colors);
            out.skyLights = std::move(skyLights);
            out.blockLights = std::move(blockLights);
            out.known = std::move(known);
            std::vector<glm::ivec3> damagedCells;
            const bool crackNeighborFacesEnabled = ::RenderInitSystemLogic::getRegistryBool(
                baseSystem,
                "BlockBreakNeighborFacesEnabled",
                true
            );
            if (crackNeighborFacesEnabled && lod == 0) {
                const int worldIndex = resolveActiveWorldIndex(baseSystem);
                if (worldIndex >= 0) {
                    BlockChargeSystemLogic::CollectDamagedVoxelCells(baseSystem, worldIndex, damagedCells);
                }
            }
            out.damagedCells = std::move(damagedCells);
            return true;
        }
    }

    void StopGreedyAsync() {
        std::vector<std::thread> workers;
        {
            std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
            if (!g_voxelGreedyAsync.running) return;
            g_voxelGreedyAsync.stop = true;
            workers.swap(g_voxelGreedyAsync.workers);
        }
        g_voxelGreedyAsync.cv.notify_all();
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
        g_voxelGreedyAsync.queue.clear();
        g_voxelGreedyAsync.results.clear();
        g_voxelGreedyAsync.inFlight.clear();
        g_voxelGreedyAsync.running = false;
        g_voxelGreedyAsync.stop = false;
    }

    size_t GetGreedyInFlightCount() {
        std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
        return g_voxelGreedyAsync.inFlight.size();
    }

    size_t GetGreedyQueueCount() {
        std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
        return g_voxelGreedyAsync.queue.size();
    }

    void RequestPriorityVoxelRemesh(BaseSystem& baseSystem,
                                    std::vector<Entity>& prototypes,
                                    const glm::ivec3& worldCell) {
        if (!baseSystem.voxelWorld || !baseSystem.voxelGreedy || !baseSystem.voxelWorld->enabled || !baseSystem.world) return;
        VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
        VoxelGreedyContext& voxelGreedy = *baseSystem.voxelGreedy;

        int voxelGreedyMaxLod = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelGreedyMaxLod", 1);
        if (voxelGreedyMaxLod < 0) return;

        int sectionSize = VoxelMeshInitSystemLogic::SectionSizeForLod(voxelWorld, 0);
        glm::ivec3 baseCoord(
            VoxelMeshInitSystemLogic::FloorDivInt(worldCell.x, sectionSize),
            VoxelMeshInitSystemLogic::FloorDivInt(worldCell.y, sectionSize),
            VoxelMeshInitSystemLogic::FloorDivInt(worldCell.z, sectionSize)
        );
        glm::ivec3 local = worldCell - baseCoord * sectionSize;

        std::vector<VoxelSectionKey> keys;
        keys.reserve(7);
        auto addKey = [&](const glm::ivec3& coord) {
            VoxelSectionKey key{0, coord};
            for (const auto& existing : keys) {
                if (existing == key) return;
            }
            keys.push_back(key);
        };

        addKey(baseCoord);
        if (local.x == 0) addKey(baseCoord + glm::ivec3(-1, 0, 0));
        if (local.x == sectionSize - 1) addKey(baseCoord + glm::ivec3(1, 0, 0));
        if (local.y == 0) addKey(baseCoord + glm::ivec3(0, -1, 0));
        if (local.y == sectionSize - 1) addKey(baseCoord + glm::ivec3(0, 1, 0));
        if (local.z == 0) addKey(baseCoord + glm::ivec3(0, 0, -1));
        if (local.z == sectionSize - 1) addKey(baseCoord + glm::ivec3(0, 0, 1));

        for (const auto& key : keys) {
            voxelWorld.dirtySections.insert(key);
            voxelGreedy.dirtySections.insert(key);
        }

        int superChunkMinLod = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkMinLod", 3);
        int superChunkMaxLod = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkMaxLod", 3);
        int superChunkSize = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkSize", 1);
        const bool cullPlantsBeyondLod0 = ::RenderInitSystemLogic::getRegistryBool(
            baseSystem,
            "FoliageCullOutsideLod0",
            true
        );
        const bool leafFanRenderInnerBlock = ::RenderInitSystemLogic::getRegistryBool(
            baseSystem,
            "LeafFanRenderInnerBlock",
            true
        );
        const bool waterTopOnlyOutsideLod0 = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "WaterTopOnlyOutsideLod0", true);
        if (superChunkSize < 1) superChunkSize = 1;
        auto toRenderKey = [&](const VoxelSectionKey& key) {
            bool useSuperChunk = key.lod >= superChunkMinLod
                && key.lod <= superChunkMaxLod
                && superChunkSize > 1;
            glm::ivec3 anchor = key.coord;
            if (useSuperChunk) {
                anchor.x = VoxelMeshInitSystemLogic::FloorDivInt(anchor.x, superChunkSize) * superChunkSize;
                anchor.z = VoxelMeshInitSystemLogic::FloorDivInt(anchor.z, superChunkSize) * superChunkSize;
            }
            return VoxelSectionKey{key.lod, anchor};
        };
        std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> priorityRenderKeys;
        priorityRenderKeys.reserve(keys.size());
        for (const auto& key : keys) {
            if (key.lod > voxelGreedyMaxLod) continue;
            priorityRenderKeys.insert(toRenderKey(key));
        }

        bool useVoxelGreedyAsync = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "voxelGreedyAsync", true);
        const bool immediateEditMeshing = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "voxelEditImmediateMeshing", false);
        const bool editSyncFallback = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "voxelEditSyncFallback", true);
        const bool editSyncFastNoAo = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "voxelEditSyncFastNoAo", false);
        const bool voxelDisableAo = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "voxelDisableAo", false);
        const bool leafAoEnabled = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "LeafAoEnabled", true);
        const float leafAoStrength = std::clamp(
            ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "LeafAoStrength", 1.0f),
            0.0f,
            1.0f
        );
        const bool plantAoEnabled = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "PlantAoEnabled", false);
        const float voxelAoStrength = std::clamp(
            ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "voxelAoStrength", 1.0f),
            0.0f,
            1.0f
        );
        const float plantAoStrength = std::clamp(
            ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "PlantAoStrength", 1.0f),
            0.0f,
            1.0f
        );
        const bool voxelLightingEnabled = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "VoxelLightingEnabled", true);
        const bool voxelLightingAffectWater = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "VoxelLightingAffectWater", true);
        const float voxelLightingStrength = std::clamp(
            ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "VoxelLightingStrength", 1.0f),
            0.0f,
            1.0f
        );
        const float voxelLightingMinBrightness = std::clamp(
            ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "VoxelLightingMinBrightness", 0.08f),
            0.0f,
            1.0f
        );
        const float voxelLightingGamma = std::clamp(
            ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "VoxelLightingGamma", 1.35f),
            0.25f,
            4.0f
        );
        const int voxelLightingDebugMode = std::clamp(
            ::RenderInitSystemLogic::getRegistryInt(baseSystem, "VoxelLightingDebugMode", 0),
            0,
            4
        );
        const int voxelLightingSkyFallbackLevel = static_cast<int>(resolveSkyFallbackLevel(baseSystem, voxelWorld));
        const int editSyncMinIntervalMs = std::max(0, ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelEditSyncMinIntervalMs", 60));
        const int editSyncMaxBacklog = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelEditSyncMaxBacklog", 24);
        const bool editPriorityFlushQueued = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "voxelEditPriorityFlushQueued", true);
        const size_t queueLimit = static_cast<size_t>(std::max(8, ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelGreedyQueueLimit", 24)));
        size_t asyncBacklog = 0;
        if (useVoxelGreedyAsync) {
            std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
            asyncBacklog = g_voxelGreedyAsync.queue.size() + g_voxelGreedyAsync.inFlight.size();
        }
        const bool backlogLimited = (editSyncMaxBacklog >= 0) && (asyncBacklog > static_cast<size_t>(editSyncMaxBacklog));
        static double s_lastSyncEditMeshingTime = -1.0;
        const double nowSec = PlatformInput::GetTimeSeconds();
        const bool intervalLimited =
            (editSyncMinIntervalMs > 0)
            && (s_lastSyncEditMeshingTime >= 0.0)
            && ((nowSec - s_lastSyncEditMeshingTime) * 1000.0 < static_cast<double>(editSyncMinIntervalMs));
        // If we're using the fast no-AO sync edit path, prefer visual responsiveness for the edited chunk
        // even when async backlog is large.
        const bool allowSyncOnBacklog = editSyncFastNoAo;
        const bool syncAllowedByBacklog = !backlogLimited || allowSyncOnBacklog;
        bool syncedEditedKey = false;
        VoxelSectionKey editedKey{0, baseCoord};
        VoxelSectionKey editedRenderKey = toRenderKey(editedKey);
        if (editSyncFallback
            && !immediateEditMeshing
            && useVoxelGreedyAsync
            && voxelGreedyMaxLod >= 0
            && syncAllowedByBacklog
            && !intervalLimited) {
            // Hybrid edit path: synchronously rebuild only the directly edited LOD0 chunk so
            // break/place visuals always update immediately without forcing full neighbor sync rebuilds.
            if (BuildVoxelGreedyMesh(baseSystem, prototypes, editedKey, editSyncFastNoAo || voxelDisableAo)) {
                voxelGreedy.renderBuffersDirty.insert(editedRenderKey);
                voxelWorld.dirtySections.erase(editedKey);
                voxelGreedy.dirtySections.erase(editedKey);
                syncedEditedKey = true;
                s_lastSyncEditMeshingTime = nowSec;
            }
        }
        if (immediateEditMeshing || !useVoxelGreedyAsync) {
            // Optional low-latency path (can spike on large sections): remesh synchronously now.
            bool rebuiltAny = false;
            for (const auto& key : keys) {
                if (key.lod > voxelGreedyMaxLod) continue;
                if (BuildVoxelGreedyMesh(baseSystem, prototypes, key)) {
                    voxelGreedy.renderBuffersDirty.insert(toRenderKey(key));
                    voxelWorld.dirtySections.erase(key);
                    voxelGreedy.dirtySections.erase(key);
                    rebuiltAny = true;
                }
            }
            if (rebuiltAny) {
                return;
            }
        }

        ensureGreedyAsyncStarted(prototypes, resolveGreedyWorkerCount(baseSystem));

        bool queuedAny = false;
        {
            std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
            auto isPriorityKey = [&](const VoxelSectionKey& candidate) {
                return priorityRenderKeys.count(candidate) > 0;
            };
            if (editPriorityFlushQueued) {
                for (auto it = g_voxelGreedyAsync.queue.begin(); it != g_voxelGreedyAsync.queue.end();) {
                    if (isPriorityKey(it->renderKey)) {
                        ++it;
                        continue;
                    }
                    g_voxelGreedyAsync.inFlight.erase(it->renderKey);
                    it = g_voxelGreedyAsync.queue.erase(it);
                }
            }
            if (syncedEditedKey && !editSyncFastNoAo) {
                for (auto it = g_voxelGreedyAsync.queue.begin(); it != g_voxelGreedyAsync.queue.end();) {
                    if (it->renderKey == editedRenderKey) {
                        it = g_voxelGreedyAsync.queue.erase(it);
                    } else {
                        ++it;
                    }
                }
                g_voxelGreedyAsync.inFlight.erase(editedRenderKey);
            }
            for (const auto& key : keys) {
                if (key.lod > voxelGreedyMaxLod) continue;
                const VoxelSectionKey renderKey = toRenderKey(key);
                if (syncedEditedKey && !editSyncFastNoAo && renderKey == editedRenderKey) {
                    // Already rebuilt with full AO in sync path; avoid redundant async rebuild.
                    continue;
                }
                for (auto it = g_voxelGreedyAsync.queue.begin();
                     it != g_voxelGreedyAsync.queue.end();
                     ++it) {
                    if (it->renderKey == renderKey) {
                        g_voxelGreedyAsync.queue.erase(it);
                        g_voxelGreedyAsync.inFlight.erase(renderKey);
                        break;
                    }
                }
                VoxelGreedySnapshot snap;
                if (!enqueueGreedySnapshot(baseSystem,
                                           voxelWorld,
                                           baseSystem.world.get(),
                                           renderKey,
                                           superChunkMinLod,
                                           superChunkMaxLod,
                                           superChunkSize,
                                           cullPlantsBeyondLod0,
                                           leafFanRenderInnerBlock,
                                           waterTopOnlyOutsideLod0,
                                           voxelDisableAo,
                                           leafAoEnabled,
                                           leafAoStrength,
                                           voxelAoStrength,
                                           plantAoEnabled,
                                           plantAoStrength,
                                           voxelLightingEnabled,
                                           voxelLightingAffectWater,
                                           voxelLightingStrength,
                                           voxelLightingMinBrightness,
                                           voxelLightingGamma,
                                           voxelLightingDebugMode,
                                           voxelLightingSkyFallbackLevel,
                                           snap)) {
                    continue;
                }
                // Guarantee room for edit-priority work by evicting oldest queued background jobs.
                while (g_voxelGreedyAsync.queue.size() >= queueLimit && !g_voxelGreedyAsync.queue.empty()) {
                    const VoxelSectionKey evictedKey = g_voxelGreedyAsync.queue.back().renderKey;
                    g_voxelGreedyAsync.queue.pop_back();
                    g_voxelGreedyAsync.inFlight.erase(evictedKey);
                }
                g_voxelGreedyAsync.queue.push_front(std::move(snap));
                g_voxelGreedyAsync.inFlight.insert(renderKey);
                g_lastGreedyQueued += 1;
                queuedAny = true;
            }
        }
        if (queuedAny) {
            g_voxelGreedyAsync.cv.notify_all();
        }
    }

    void GetGreedyStats(size_t& queued, size_t& applied, size_t& dropped) {
        queued = g_lastGreedyQueued;
        applied = g_lastGreedyApplied;
        dropped = g_lastGreedyDropped;
    }

    void UpdateVoxelMeshing(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float, PlatformWindowHandle) {
        if (!baseSystem.renderer || !baseSystem.player) return;
        RendererContext& renderer = *baseSystem.renderer;
        glm::vec3 playerPos = baseSystem.player->cameraPosition;

        int voxelGreedyMaxLod = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelGreedyMaxLod", 1);
        bool useVoxelGreedy = baseSystem.voxelWorld && baseSystem.voxelWorld->enabled && baseSystem.voxelGreedy
            && renderer.faceShader && renderer.faceVAO && voxelGreedyMaxLod >= 0;

        if (!useVoxelGreedy) {
            StopGreedyAsync();
            return;
        }

        VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
        VoxelGreedyContext& voxelGreedy = *baseSystem.voxelGreedy;
        bool useVoxelGreedyAsync = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "voxelGreedyAsync", true);
        const bool voxelDisableAo = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "voxelDisableAo", false);
        const bool leafAoEnabled = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "LeafAoEnabled", true);
        const float leafAoStrength = std::clamp(
            ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "LeafAoStrength", 1.0f),
            0.0f,
            1.0f
        );
        const bool plantAoEnabled = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "PlantAoEnabled", false);
        const float voxelAoStrength = std::clamp(
            ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "voxelAoStrength", 1.0f),
            0.0f,
            1.0f
        );
        const float plantAoStrength = std::clamp(
            ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "PlantAoStrength", 1.0f),
            0.0f,
            1.0f
        );
        const bool voxelLightingEnabled = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "VoxelLightingEnabled", true);
        const bool voxelLightingAffectWater = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "VoxelLightingAffectWater", true);
        const float voxelLightingStrength = std::clamp(
            ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "VoxelLightingStrength", 1.0f),
            0.0f,
            1.0f
        );
        const float voxelLightingMinBrightness = std::clamp(
            ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "VoxelLightingMinBrightness", 0.08f),
            0.0f,
            1.0f
        );
        const float voxelLightingGamma = std::clamp(
            ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "VoxelLightingGamma", 1.35f),
            0.25f,
            4.0f
        );
        const int voxelLightingDebugMode = std::clamp(
            ::RenderInitSystemLogic::getRegistryInt(baseSystem, "VoxelLightingDebugMode", 0),
            0,
            4
        );
        const int voxelLightingSkyFallbackLevel = static_cast<int>(resolveSkyFallbackLevel(baseSystem, voxelWorld));
        const bool debugVoxelMeshingPerf = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "DebugVoxelMeshingPerf", false);
        g_lastGreedyQueued = 0;
        g_lastGreedyApplied = 0;
        g_lastGreedyDropped = 0;

        auto logVoxelPerf = [&](const char* label,
                                const std::chrono::steady_clock::time_point& t0,
                                size_t count) {
            if (!debugVoxelMeshingPerf) return;
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0
            ).count();
            std::cout << "RenderSystem: " << label << " " << count << " in "
                      << elapsedMs << " ms." << std::endl;
        };

        if (useVoxelGreedyAsync) {
            ensureGreedyAsyncStarted(prototypes, resolveGreedyWorkerCount(baseSystem));
        } else {
            StopGreedyAsync();
        }

        std::vector<VoxelSectionKey> staleSections;
        for (const auto& [key, _] : voxelGreedy.chunks) {
            auto it = voxelWorld.sections.find(key);
            if (it == voxelWorld.sections.end() || it->second.nonAirCount <= 0) {
                staleSections.push_back(key);
            }
        }
        for (const auto& key : staleSections) {
            releaseGreedyChunkIfPresent(voxelGreedy, key);
        }

        int superChunkMinLod = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkMinLod", 3);
        int superChunkMaxLod = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkMaxLod", 3);
        int superChunkSize = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkSize", 1);
        const bool cullPlantsBeyondLod0 = ::RenderInitSystemLogic::getRegistryBool(
            baseSystem,
            "FoliageCullOutsideLod0",
            true
        );
        const bool leafFanRenderInnerBlock = ::RenderInitSystemLogic::getRegistryBool(
            baseSystem,
            "LeafFanRenderInnerBlock",
            true
        );
        const bool waterTopOnlyOutsideLod0 = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "WaterTopOnlyOutsideLod0", true);
        if (superChunkSize < 1) superChunkSize = 1;
        auto toRenderKey = [&](const VoxelSectionKey& key) {
            bool useSuperChunk = key.lod >= superChunkMinLod
                && key.lod <= superChunkMaxLod
                && superChunkSize > 1;
            glm::ivec3 anchor = key.coord;
            if (useSuperChunk) {
                anchor.x = VoxelMeshInitSystemLogic::FloorDivInt(anchor.x, superChunkSize) * superChunkSize;
                anchor.z = VoxelMeshInitSystemLogic::FloorDivInt(anchor.z, superChunkSize) * superChunkSize;
            }
            return VoxelSectionKey{key.lod, anchor};
        };

        auto clearDirtyForKey = [&](const VoxelSectionKey& key) {
            bool useSuperChunk = key.lod >= superChunkMinLod
                && key.lod <= superChunkMaxLod
                && superChunkSize > 1;
            if (useSuperChunk) {
                glm::ivec3 anchorCoord(
                    VoxelMeshInitSystemLogic::FloorDivInt(key.coord.x, superChunkSize) * superChunkSize,
                    key.coord.y,
                    VoxelMeshInitSystemLogic::FloorDivInt(key.coord.z, superChunkSize) * superChunkSize
                );
                for (int oz = 0; oz < superChunkSize; ++oz) {
                    for (int ox = 0; ox < superChunkSize; ++ox) {
                        VoxelSectionKey subKey{key.lod, glm::ivec3(anchorCoord.x + ox,
                                                                  key.coord.y,
                                                                  anchorCoord.z + oz)};
                        voxelWorld.dirtySections.erase(subKey);
                        voxelGreedy.dirtySections.erase(subKey);
                    }
                }
            } else {
                voxelWorld.dirtySections.erase(key);
                voxelGreedy.dirtySections.erase(key);
            }
        };

        if (useVoxelGreedyAsync) {
            std::deque<VoxelGreedyResult> results;
            {
                std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
                results.swap(g_voxelGreedyAsync.results);
            }
            for (auto& result : results) {
                int lod = result.renderKey.lod;
                int chunkSize = (lod >= superChunkMinLod && lod <= superChunkMaxLod && superChunkSize > 1)
                    ? superChunkSize
                    : 1;
                uint64_t currentVersion = computeGreedyVersionKey(voxelWorld, lod, result.renderKey.coord, chunkSize);
                if (currentVersion == 0) {
                    auto secIt = voxelWorld.sections.find(result.renderKey);
                    if (secIt == voxelWorld.sections.end()) {
                        releaseGreedyChunk(voxelGreedy, std::move(result.mesh));
                        g_lastGreedyDropped += 1;
                        continue;
                    }
                    if (secIt->second.editVersion != result.renderEditVersion) {
                        releaseGreedyChunk(voxelGreedy, std::move(result.mesh));
                        g_lastGreedyDropped += 1;
                        continue;
                    }
                } else if (currentVersion != result.versionKey) {
                    releaseGreedyChunk(voxelGreedy, std::move(result.mesh));
                    g_lastGreedyDropped += 1;
                    continue;
                }
                if (result.empty) {
                    releaseGreedyChunkIfPresent(voxelGreedy, result.renderKey);
                } else {
                    releaseGreedyChunkIfPresent(voxelGreedy, result.renderKey);
                    voxelGreedy.chunks[result.renderKey] = std::move(result.mesh);
                    voxelGreedy.renderBuffersDirty.insert(result.renderKey);
                    g_lastGreedyApplied += 1;
                }
                clearDirtyForKey(result.renderKey);
            }
        }

        for (const auto& key : voxelWorld.dirtySections) {
            if (key.lod > voxelGreedyMaxLod) continue;
            auto it = voxelWorld.sections.find(key);
            if (it == voxelWorld.sections.end()) continue;
            if (!::RenderInitSystemLogic::shouldRenderVoxelSection(baseSystem, it->second, playerPos)) continue;
            voxelGreedy.dirtySections.insert(key);
        }

        if (!voxelGreedy.dirtySections.empty()) {
            std::vector<VoxelSectionKey> buildList;
            buildList.reserve(voxelGreedy.dirtySections.size());
            for (const auto& key : voxelGreedy.dirtySections) {
                auto it = voxelWorld.sections.find(key);
                if (it == voxelWorld.sections.end()) continue;
                if (!::RenderInitSystemLogic::shouldRenderVoxelSection(baseSystem, it->second, playerPos)) continue;
                buildList.push_back(key);
            }
            std::sort(buildList.begin(), buildList.end(),
                      [&](const VoxelSectionKey& a, const VoxelSectionKey& b) {
                if (a.lod != b.lod) return a.lod < b.lod;
                auto sectionCenter = [&](const VoxelSectionKey& key) {
                    int size = VoxelMeshInitSystemLogic::SectionSizeForLod(voxelWorld, key.lod);
                    int scale = 1 << key.lod;
                    float worldSize = static_cast<float>(size * scale);
                    return glm::vec3(
                        (static_cast<float>(key.coord.x) + 0.5f) * worldSize,
                        (static_cast<float>(key.coord.y) + 0.5f) * worldSize,
                        (static_cast<float>(key.coord.z) + 0.5f) * worldSize
                    );
                };
                glm::vec3 ca = sectionCenter(a);
                glm::vec3 cb = sectionCenter(b);
                float adx = ca.x - playerPos.x;
                float adz = ca.z - playerPos.z;
                float bdx = cb.x - playerPos.x;
                float bdz = cb.z - playerPos.z;
                float ad2 = adx * adx + adz * adz;
                float bd2 = bdx * bdx + bdz * bdz;
                if (ad2 != bd2) return ad2 < bd2;
                float ady = std::abs(ca.y - playerPos.y);
                float bdy = std::abs(cb.y - playerPos.y);
                if (ady != bdy) return ady < bdy;
                if (a.coord.x != b.coord.x) return a.coord.x < b.coord.x;
                if (a.coord.y != b.coord.y) return a.coord.y < b.coord.y;
                return a.coord.z < b.coord.z;
            });
            const int enqueueBudgetInt = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelGreedyMeshesPerFrame", 4);
            const size_t queueLimit = static_cast<size_t>(std::max(8, ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelGreedyQueueLimit", 24)));
            const float meshingTimeBudgetMs = std::max(
                0.0f,
                ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "voxelGreedyMeshingMaxMsPerFrame", 0.0f)
            );
            const int minEnqueueBeforeTimeCap = std::max(
                0,
                ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelGreedyMeshingMinEnqueuePerFrame", 1)
            );
            size_t enqueueBudget = buildList.size();
            if (enqueueBudgetInt > 0) {
                enqueueBudget = std::min(enqueueBudget, static_cast<size_t>(enqueueBudgetInt));
            }
            const size_t scanLimit = std::min(
                buildList.size(),
                enqueueBudget > 0 ? (enqueueBudget * 32 + 64) : static_cast<size_t>(0)
            );
            auto start = std::chrono::steady_clock::now();
            size_t buildCount = 0;
            std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> retry;
            size_t scanned = 0;
            for (; scanned < scanLimit && buildCount < enqueueBudget; ++scanned) {
                if (meshingTimeBudgetMs > 0.0f && static_cast<int>(buildCount) >= minEnqueueBeforeTimeCap) {
                    float elapsedMs = std::chrono::duration<float, std::milli>(
                        std::chrono::steady_clock::now() - start
                    ).count();
                    if (elapsedMs >= meshingTimeBudgetMs) {
                        break;
                    }
                }
                const auto& key = buildList[scanned];
                const VoxelSectionKey renderKey = toRenderKey(key);
                if (useVoxelGreedyAsync) {
                    bool queueFull = false;
                    bool alreadyQueuedOrInFlight = false;
                    {
                        std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
                        queueFull = g_voxelGreedyAsync.queue.size() >= queueLimit;
                        alreadyQueuedOrInFlight = (g_voxelGreedyAsync.inFlight.count(renderKey) > 0);
                    }
                    if (queueFull) {
                        // Queue is saturated; don't build expensive snapshots that cannot enqueue.
                        retry.insert(key);
                        continue;
                    }
                    if (alreadyQueuedOrInFlight) {
                        // Keep dirty so it can retry after the in-flight/queued build completes.
                        retry.insert(key);
                        continue;
                    }

                    VoxelGreedySnapshot snap;
                    if (enqueueGreedySnapshot(baseSystem, voxelWorld, baseSystem.world.get(), renderKey,
                                              superChunkMinLod, superChunkMaxLod, superChunkSize,
                                              cullPlantsBeyondLod0,
                                              leafFanRenderInnerBlock,
                                              waterTopOnlyOutsideLod0,
                                              voxelDisableAo,
                                              leafAoEnabled,
                                              leafAoStrength,
                                              voxelAoStrength,
                                              plantAoEnabled,
                                              plantAoStrength,
                                              voxelLightingEnabled,
                                              voxelLightingAffectWater,
                                              voxelLightingStrength,
                                              voxelLightingMinBrightness,
                                              voxelLightingGamma,
                                              voxelLightingDebugMode,
                                              voxelLightingSkyFallbackLevel,
                                              snap)) {
                        {
                            std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
                            // For background streaming, do not evict queued work when full.
                            // Eviction causes churn/starvation patterns (checkerboard holes).
                            if (g_voxelGreedyAsync.queue.size() >= queueLimit) {
                                retry.insert(key);
                                continue;
                            }
                            g_voxelGreedyAsync.queue.push_back(std::move(snap));
                            g_voxelGreedyAsync.inFlight.insert(renderKey);
                        }
                        g_voxelGreedyAsync.cv.notify_one();
                        g_lastGreedyQueued += 1;
                        buildCount += 1;
                    } else {
                        retry.insert(key);
                    }
                } else {
                    if (!BuildVoxelGreedyMesh(baseSystem, prototypes, key)) {
                        retry.insert(key);
                    } else {
                        g_lastGreedyApplied += 1;
                        buildCount += 1;
                    }
                }
            }
            for (size_t i = scanned; i < buildList.size(); ++i) {
                retry.insert(buildList[i]);
            }
            voxelGreedy.dirtySections.clear();
            for (const auto& key : retry) {
                voxelGreedy.dirtySections.insert(key);
            }
            logVoxelPerf(useVoxelGreedyAsync ? "enqueued voxel greedy mesh(es)" : "rebuilt voxel greedy mesh(es)", start, buildCount);
        }
    }
}
