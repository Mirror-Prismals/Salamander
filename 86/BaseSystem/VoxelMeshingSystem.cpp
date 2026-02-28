#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
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
            bool cullPlantsBeyondLod0 = true;
            bool waterTopOnlyOutsideLod0 = true;
            const WorldContext* worldCtx = nullptr;
            std::vector<uint32_t> ids;
            std::vector<uint32_t> colors;
            std::vector<uint8_t> known;
        };

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
            std::thread worker;
            bool running = false;
            bool stop = false;
            const std::vector<Entity>* prototypes = nullptr;
        };

        static VoxelGreedyAsyncState g_voxelGreedyAsync;
        static size_t g_lastGreedyQueued = 0;
        static size_t g_lastGreedyApplied = 0;
        static size_t g_lastGreedyDropped = 0;

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
            return proto.name == "Leaf" || (proto.isAnimated && proto.hasWireframe && !proto.isSolid);
        }

        enum class PlantType : int { None = 0, GrassTall = 1, Flower = 2, GrassShort = 3 };

        PlantType plantTypeForPrototype(const Entity& proto) {
            if (proto.name == "GrassTuft") return PlantType::GrassTall;
            if (proto.name == "GrassTuftShort") return PlantType::GrassShort;
            if (proto.name == "Flower") return PlantType::Flower;
            return PlantType::None;
        }

        bool isPlantPrototype(const Entity& proto) {
            return plantTypeForPrototype(proto) != PlantType::None;
        }

        bool startsWith(const std::string& value, const char* prefix) {
            if (!prefix) return false;
            const size_t prefixLen = std::strlen(prefix);
            return value.size() >= prefixLen
                && value.compare(0, prefixLen, prefix) == 0;
        }

        bool endsWith(const std::string& value, const char* suffix) {
            if (!suffix) return false;
            const size_t suffixLen = std::strlen(suffix);
            return value.size() >= suffixLen
                && value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
        }

        bool isStonePebbleXName(const std::string& name) {
            return name == "StonePebbleTexX"
                || (startsWith(name, "StonePebble") && endsWith(name, "TexX"));
        }

        bool isStonePebbleZName(const std::string& name) {
            return name == "StonePebbleTexZ"
                || (startsWith(name, "StonePebble") && endsWith(name, "TexZ"));
        }

        enum class NarrowLogAxis : int { None = 0, X = 1, Y = 2, Z = 3 };
        enum class NarrowShape : int { Default = 0, Stick = 1, StonePebble = 2 };
        enum class NarrowMount : int { Floor = 0, WallPosX = 1, WallNegX = 2, WallPosZ = 3, WallNegZ = 4, Ceiling = 5 };

        NarrowLogAxis narrowLogAxis(const Entity& proto) {
            if (proto.name == "FirLog1Tex" || proto.name == "FirLog2Tex"
                || proto.name == "FirLog1TopTex" || proto.name == "FirLog2TopTex") {
                return NarrowLogAxis::Y;
            }
            if (proto.name == "FirLog1TexX" || proto.name == "FirLog2TexX"
                || proto.name == "FirLog1NubTexX" || proto.name == "FirLog2NubTexX"
                || proto.name == "StickTexX"
                || isStonePebbleXName(proto.name)
                || proto.name == "CeilingStoneTexX") {
                return NarrowLogAxis::X;
            }
            if (proto.name == "FirLog1TexZ" || proto.name == "FirLog2TexZ"
                || proto.name == "FirLog1NubTexZ" || proto.name == "FirLog2NubTexZ"
                || proto.name == "StickTexZ"
                || isStonePebbleZName(proto.name)
                || proto.name == "CeilingStoneTexZ") {
                return NarrowLogAxis::Z;
            }
            if (proto.name == "WallStoneTexPosX"
                || proto.name == "WallStoneTexNegX"
                || proto.name == "WallStoneTexPosZ"
                || proto.name == "WallStoneTexNegZ") {
                return NarrowLogAxis::Y;
            }
            return NarrowLogAxis::None;
        }

        bool isNarrowLogPrototype(const Entity& proto) {
            return narrowLogAxis(proto) != NarrowLogAxis::None;
        }

        NarrowShape narrowShapeForPrototype(const Entity& proto) {
            if (proto.name == "StickTexX" || proto.name == "StickTexZ") return NarrowShape::Stick;
            if (isStonePebbleXName(proto.name) || isStonePebbleZName(proto.name)
                || proto.name == "WallStoneTexPosX" || proto.name == "WallStoneTexNegX"
                || proto.name == "WallStoneTexPosZ" || proto.name == "WallStoneTexNegZ"
                || proto.name == "CeilingStoneTexX" || proto.name == "CeilingStoneTexZ") {
                return NarrowShape::StonePebble;
            }
            return NarrowShape::Default;
        }

        NarrowMount narrowMountForPrototype(const Entity& proto) {
            if (proto.name == "WallStoneTexPosX") return NarrowMount::WallPosX;
            if (proto.name == "WallStoneTexNegX") return NarrowMount::WallNegX;
            if (proto.name == "WallStoneTexPosZ") return NarrowMount::WallPosZ;
            if (proto.name == "WallStoneTexNegZ") return NarrowMount::WallNegZ;
            if (proto.name == "CeilingStoneTexX" || proto.name == "CeilingStoneTexZ") return NarrowMount::Ceiling;
            return NarrowMount::Floor;
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

        NarrowHalfExtents narrowHalfExtentsForShape(NarrowLogAxis axis, NarrowShape shape) {
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

        enum class SlopeDir : int { None = 0, PosX = 1, NegX = 2, PosZ = 3, NegZ = 4 };

        SlopeDir slopeDirForPrototype(const Entity& proto) {
            if (proto.name == "DebugSlopeTexPosX") return SlopeDir::PosX;
            if (proto.name == "DebugSlopeTexNegX") return SlopeDir::NegX;
            if (proto.name == "DebugSlopeTexPosZ") return SlopeDir::PosZ;
            if (proto.name == "DebugSlopeTexNegZ") return SlopeDir::NegZ;
            return SlopeDir::None;
        }

        bool isSlopePrototype(const Entity& proto) {
            return slopeDirForPrototype(proto) != SlopeDir::None;
        }

        constexpr float kSlopeCapAlphaA = -4.0f;
        constexpr float kSlopeCapAlphaB = -5.0f;
        constexpr float kSlopeTopAlphaPosX = -6.0f;
        constexpr float kSlopeTopAlphaNegX = -7.0f;
        constexpr float kSlopeTopAlphaPosZ = -8.0f;
        constexpr float kSlopeTopAlphaNegZ = -9.0f;

        float slopeTopAlpha(SlopeDir dir) {
            switch (dir) {
                case SlopeDir::PosX: return kSlopeTopAlphaPosX;
                case SlopeDir::NegX: return kSlopeTopAlphaNegX;
                case SlopeDir::PosZ: return kSlopeTopAlphaPosZ;
                case SlopeDir::NegZ: return kSlopeTopAlphaNegZ;
                default: return kSlopeTopAlphaPosX;
            }
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
                    outFaceA = 0; outAlphaA = kSlopeCapAlphaA;
                    outFaceB = 1; outAlphaB = kSlopeCapAlphaB;
                    break;
                case SlopeDir::NegZ:
                    outFaceA = 0; outAlphaA = kSlopeCapAlphaB;
                    outFaceB = 1; outAlphaB = kSlopeCapAlphaA;
                    break;
                default:
                    outFaceA = 4; outAlphaA = kSlopeCapAlphaA;
                    outFaceB = 5; outAlphaB = kSlopeCapAlphaB;
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
            static const float kAoLut[4] = {1.0f, 0.85f, 0.7f, 0.55f};
            return kAoLut[occlusion];
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

            struct CellInfo {
                bool filled = false;
                bool isLeaf = false;
                PlantType plantType = PlantType::None;
                SlopeDir slopeDir = SlopeDir::None;
                int protoID = -1;
                glm::vec3 color = glm::vec3(1.0f);
            };
            struct MaskCell {
                bool filled = false;
                bool isLeaf = false;
                PlantType plantType = PlantType::None;
                bool narrowLog = false;
                NarrowShape narrowShape = NarrowShape::Default;
                NarrowMount narrowMount = NarrowMount::Floor;
                NarrowLogAxis narrowAxis = NarrowLogAxis::None;
                int protoID = -1;
                int tileIndex = -1;
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

            std::vector<CellInfo> solidCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> waterCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> leafCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> plantCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> slopeCells(static_cast<size_t>(sizeX * sizeY * sizeZ));

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
                    outType = 1;
                }
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
                            waterCells[idx] = {true, false, PlantType::None, SlopeDir::None, static_cast<int>(id), color};
                        } else if (isSlopePrototype(proto)) {
                            slopeCells[idx] = {true, false, PlantType::None, slopeDirForPrototype(proto), static_cast<int>(id), color};
                        } else if (isLeafPrototype(proto)) {
                            leafCells[idx] = {true, true, PlantType::None, SlopeDir::None, static_cast<int>(id), color};
                        } else if (isPlantPrototype(proto)) {
                            plantCells[idx] = {true, false, plantTypeForPrototype(proto), SlopeDir::None, static_cast<int>(id), color};
                        } else {
                            solidCells[idx] = {true, false, PlantType::None, SlopeDir::None, static_cast<int>(id), color};
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
                        int sid = solidCells[idx].protoID;
                        if (sid >= 0 && sid < static_cast<int>(prototypes.size())
                            && isNarrowLogPrototype(prototypes[static_cast<size_t>(sid)])) {
                            return 4;
                        }
                        return 1;
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

            auto isOccluderAt = [&](const glm::ivec3& lodCoord) {
                glm::ivec3 local = lodCoord - minCoord;
                if (inBounds(local)) {
                    int idx = cellIndex(local);
                    return solidCells[idx].filled;
                }
                uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, section.lod, lodCoord);
                int type = 0;
                classifyProto(id, type);
                return isSolidOccluderType(type);
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
                    return bakedAoValue(s1, s2, c);
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
                    return solidCells[idx].filled || slopeCells[idx].filled;
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
                            alongSign = (vSign < 0) ? 1 : -1; // high at low V
                            perpSign = uSign;
                            break;
                        case SlopeDir::NegZ:
                            alongSign = (vSign > 0) ? 1 : -1; // high at high V
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
                    return bakedAoValue(s1, s2, c);
                };

                return glm::vec4(
                    cornerAoFromUv(-1, -1),
                    cornerAoFromUv(1, -1),
                    cornerAoFromUv(1, 1),
                    cornerAoFromUv(-1, 1)
                );
            };

            auto sameColor = [](const glm::vec3& a, const glm::vec3& b) {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            };
            auto sameAo = [](const glm::vec4& a, const glm::vec4& b) {
                return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
            };
            auto sameKey = [&](const MaskCell& a, const MaskCell& b) {
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
                                glm::ivec3 lodCoord = minCoord + local;
                                glm::ivec3 neighborCoord = lodCoord + VoxelMeshInitSystemLogic::FaceNormal(faceType);
                                int neighborType = neighborTypeAt(neighborCoord);
                                if (passType == 1) {
                                    // Unknown neighbor sections (streaming gaps / section boundaries) should not
                                    // produce water boundary planes; only draw against known non-water cells.
                                    if (neighborType == 0 && !neighborSectionKnownAt(neighborCoord)) {
                                        continue;
                                    }
                                    if (neighborType == 1 || neighborType == 2 || neighborType == 6) continue;
                                } else if (passType == 2) {
                                    if (neighborType != 0) continue;
                                } else if (passType == 3) {
                                    if (!(faceType == 0 || faceType == 1 || faceType == 4 || faceType == 5)) continue;
                                } else {
                                    bool neighborFull = isSolidOccluderType(neighborType);
                                    bool neighborNarrow = (neighborType == 4);
                                    bool faceAlongAxis = (currentAxis == NarrowLogAxis::Y && (faceType == 2 || faceType == 3))
                                        || (currentAxis == NarrowLogAxis::X && (faceType == 0 || faceType == 1))
                                        || (currentAxis == NarrowLogAxis::Z && (faceType == 4 || faceType == 5));
                                    if (!currentNarrow) {
                                        if (neighborFull) continue;
                                    } else {
                                        if (faceAlongAxis && (neighborFull || neighborNarrow)) continue;
                                    }
                                }
                                int tileIndex = (passType == 3)
                                    ? -1
                                    : ::RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), proto, faceType);
                                int idx = v * uLen + u;
                                mask[idx].filled = true;
                                mask[idx].isLeaf = cell.isLeaf;
                                mask[idx].plantType = cell.plantType;
                                mask[idx].narrowLog = currentNarrow;
                                mask[idx].narrowShape = narrowShapeForPrototype(proto);
                                mask[idx].narrowMount = narrowMountForPrototype(proto);
                                mask[idx].narrowAxis = currentAxis;
                                mask[idx].protoID = cell.protoID;
                                mask[idx].tileIndex = tileIndex;
                                mask[idx].alpha = (passType == 3)
                                    ? ((cell.plantType == PlantType::Flower) ? -3.0f
                                       : (cell.plantType == PlantType::GrassShort) ? -2.3f
                                       : -2.0f)
                                    : alpha;
                                mask[idx].color = cell.color;
                                mask[idx].ao = (passType == 0 && !disableAo) ? computeFaceAo(lodCoord, faceType) : glm::vec4(1.0f);
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
                                if (seed.plantType != PlantType::None) {
                                    axisOffset = 0.0f;
                                } else if (seed.narrowLog) {
                                    const NarrowHalfExtents ext = narrowHalfExtentsForShape(seed.narrowAxis, seed.narrowShape);
                                    float halfExtent = 0.5f;
                                    if (faceType == 0 || faceType == 1) halfExtent = ext.x;
                                    else if (faceType == 2 || faceType == 3) halfExtent = ext.y;
                                    else if (faceType == 4 || faceType == 5) halfExtent = ext.z;
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
                                    if (seed.plantType == PlantType::Flower) {
                                        plantWidth = 0.86f;
                                        plantHeight = 0.92f;
                                    } else if (seed.plantType == PlantType::GrassShort) {
                                        plantWidth = 1.00f;
                                        plantHeight = 1.00f;
                                    }
                                    scaleVec.x *= plantWidth;
                                    scaleVec.y *= plantHeight;
                                    uvScaleVec = glm::vec2(1.0f);
                                    center.y += (plantHeight - 1.0f) * 0.5f * static_cast<float>(scale);
                                } else if (seed.narrowLog) {
                                    const NarrowHalfExtents ext = narrowHalfExtentsForShape(seed.narrowAxis, seed.narrowShape);
                                    float uScale = 1.0f;
                                    float vScale = 1.0f;
                                    if (faceType == 0 || faceType == 1) {
                                        uScale = ext.z * 2.0f; // U axis is Z.
                                        vScale = ext.y * 2.0f; // V axis is Y.
                                    } else if (faceType == 2 || faceType == 3) {
                                        uScale = ext.x * 2.0f; // U axis is X.
                                        vScale = ext.z * 2.0f; // V axis is Z.
                                    } else if (faceType == 4 || faceType == 5) {
                                        uScale = ext.x * 2.0f; // U axis is X.
                                        vScale = ext.y * 2.0f; // V axis is Y.
                                    }
                                    scaleVec.x *= uScale;
                                    scaleVec.y *= vScale;
                                    uvScaleVec.x *= uScale;
                                    uvScaleVec.y *= vScale;
                                }
                                if (seed.narrowShape != NarrowShape::Default) {
                                    const NarrowHalfExtents ext = narrowHalfExtentsForShape(seed.narrowAxis, seed.narrowShape);
                                    if (seed.narrowMount == NarrowMount::WallPosX) {
                                        center.x += (0.5f - ext.x - 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowMount == NarrowMount::WallNegX) {
                                        center.x += (-0.5f + ext.x + 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowMount == NarrowMount::WallPosZ) {
                                        center.z += (0.5f - ext.z - 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowMount == NarrowMount::WallNegZ) {
                                        center.z += (-0.5f + ext.z + 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowMount == NarrowMount::Ceiling) {
                                        center.y += (0.5f - ext.y - 0.01f) * static_cast<float>(scale);
                                    } else {
                                        // Rest near the cell floor so narrow props appear laid on top of ground blocks.
                                        center.y += (-0.5f + ext.y + 0.01f) * static_cast<float>(scale);
                                    }
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
            buildPass(leafCells, -1.0f, 2, false);
            const bool cullPlantsBeyondLod0 = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "FoliageCullOutsideLod0", true);
            const bool includePlants = !(cullPlantsBeyondLod0 && section.lod > 0);
            if (includePlants) {
                buildPass(plantCells, -2.0f, 3, false);
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
                            glm::ivec3 lodCoord = minCoord + local;

                            auto tryEmit = [&](int faceType, float alpha) {
                                glm::ivec3 neighborCoord = lodCoord + VoxelMeshInitSystemLogic::FaceNormal(faceType);
                                int neighborType = neighborTypeAt(neighborCoord);
                                if (isSolidOccluderType(neighborType)) return;
                                int tileIndex = ::RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), proto, faceType);
                                glm::vec4 ao = disableAo ? glm::vec4(1.0f) : computeSlopeFaceAo(lodCoord, faceType, alpha, cell.slopeDir);
                                emitSlopeFace(local, faceType, tileIndex, alpha, cell.color, ao);
                            };

                            tryEmit(3, 1.0f);
                            tryEmit(slopeTallFace(cell.slopeDir), 1.0f);

                            int capFaceA = 4;
                            int capFaceB = 5;
                            float capAlphaA = kSlopeCapAlphaA;
                            float capAlphaB = kSlopeCapAlphaB;
                            slopeCapFacesAndAlpha(cell.slopeDir, capFaceA, capAlphaA, capFaceB, capAlphaB);
                            tryEmit(capFaceA, capAlphaA);
                            tryEmit(capFaceB, capAlphaB);
                            tryEmit(2, slopeTopAlpha(cell.slopeDir));
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

            struct CellInfo {
                bool filled = false;
                bool isLeaf = false;
                PlantType plantType = PlantType::None;
                SlopeDir slopeDir = SlopeDir::None;
                int protoID = -1;
                glm::vec3 color = glm::vec3(1.0f);
            };
            struct MaskCell {
                bool filled = false;
                bool isLeaf = false;
                PlantType plantType = PlantType::None;
                bool narrowLog = false;
                NarrowShape narrowShape = NarrowShape::Default;
                NarrowMount narrowMount = NarrowMount::Floor;
                NarrowLogAxis narrowAxis = NarrowLogAxis::None;
                int protoID = -1;
                int tileIndex = -1;
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

            std::vector<CellInfo> solidCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> waterCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> leafCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> plantCells(static_cast<size_t>(sizeX * sizeY * sizeZ));
            std::vector<CellInfo> slopeCells(static_cast<size_t>(sizeX * sizeY * sizeZ));

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
                    outType = 1;
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
                            waterCells[idx] = {true, false, PlantType::None, SlopeDir::None, static_cast<int>(id), color};
                        } else if (isSlopePrototype(proto)) {
                            slopeCells[idx] = {true, false, PlantType::None, slopeDirForPrototype(proto), static_cast<int>(id), color};
                        } else if (isLeafPrototype(proto)) {
                            leafCells[idx] = {true, true, PlantType::None, SlopeDir::None, static_cast<int>(id), color};
                        } else if (isPlantPrototype(proto)) {
                            plantCells[idx] = {true, false, plantTypeForPrototype(proto), SlopeDir::None, static_cast<int>(id), color};
                        } else {
                            solidCells[idx] = {true, false, PlantType::None, SlopeDir::None, static_cast<int>(id), color};
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

            auto computeFaceAo = [&](const glm::ivec3& lodCoord, int faceType) {
                glm::ivec3 uAxis = faceUAxisForAo(faceType);
                glm::ivec3 vAxis = faceVAxisForAo(faceType);
                auto cornerAo = [&](int uSign, int vSign) {
                    glm::ivec3 side1 = uAxis * uSign;
                    glm::ivec3 side2 = vAxis * vSign;
                    bool s1 = isOccluderAt(lodCoord + side1);
                    bool s2 = isOccluderAt(lodCoord + side2);
                    bool c = isOccluderAt(lodCoord + side1 + side2);
                    return bakedAoValue(s1, s2, c);
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
                            alongSign = (vSign < 0) ? 1 : -1;
                            perpSign = uSign;
                            break;
                        case SlopeDir::NegZ:
                            alongSign = (vSign > 0) ? 1 : -1;
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
                    return bakedAoValue(s1, s2, c);
                };

                return glm::vec4(
                    cornerAoFromUv(-1, -1),
                    cornerAoFromUv(1, -1),
                    cornerAoFromUv(1, 1),
                    cornerAoFromUv(-1, 1)
                );
            };

            auto sameColor = [](const glm::vec3& a, const glm::vec3& b) {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            };
            auto sameAo = [](const glm::vec4& a, const glm::vec4& b) {
                return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
            };
            auto sameKey = [&](const MaskCell& a, const MaskCell& b) {
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
                                glm::ivec3 lodCoord = minCoord + local;
                                glm::ivec3 neighborCoord = lodCoord + VoxelMeshInitSystemLogic::FaceNormal(faceType);
                                int neighborType = neighborTypeAt(neighborCoord);
                                if (passType == 1) {
                                    if (neighborType == 0 && !snapshotKnownAt(neighborCoord)) {
                                        continue;
                                    }
                                    if (neighborType == 1 || neighborType == 2 || neighborType == 6) continue;
                                } else if (passType == 2) {
                                    if (neighborType != 0) continue;
                                } else if (passType == 3) {
                                    if (!(faceType == 0 || faceType == 1 || faceType == 4 || faceType == 5)) continue;
                                } else {
                                    bool neighborFull = isSolidOccluderType(neighborType);
                                    bool neighborNarrow = (neighborType == 4);
                                    bool faceAlongAxis = (currentAxis == NarrowLogAxis::Y && (faceType == 2 || faceType == 3))
                                        || (currentAxis == NarrowLogAxis::X && (faceType == 0 || faceType == 1))
                                        || (currentAxis == NarrowLogAxis::Z && (faceType == 4 || faceType == 5));
                                    if (!currentNarrow) {
                                        if (neighborFull) continue;
                                    } else {
                                        if (faceAlongAxis && (neighborFull || neighborNarrow)) continue;
                                    }
                                }
                                int tileIndex = (passType == 3)
                                    ? -1
                                    : ::RenderInitSystemLogic::FaceTileIndexFor(snap.worldCtx, proto, faceType);
                                int idx = v * uLen + u;
                                mask[idx].filled = true;
                                mask[idx].isLeaf = cell.isLeaf;
                                mask[idx].plantType = cell.plantType;
                                mask[idx].narrowLog = currentNarrow;
                                mask[idx].narrowShape = narrowShapeForPrototype(proto);
                                mask[idx].narrowMount = narrowMountForPrototype(proto);
                                mask[idx].narrowAxis = currentAxis;
                                mask[idx].protoID = cell.protoID;
                                mask[idx].tileIndex = tileIndex;
                                mask[idx].alpha = (passType == 3)
                                    ? ((cell.plantType == PlantType::Flower) ? -3.0f
                                       : (cell.plantType == PlantType::GrassShort) ? -2.3f
                                       : -2.0f)
                                    : alpha;
                                mask[idx].color = cell.color;
                                mask[idx].ao = (passType == 0 && !disableAo) ? computeFaceAo(lodCoord, faceType) : glm::vec4(1.0f);
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
                                if (seed.plantType != PlantType::None) {
                                    axisOffset = 0.0f;
                                } else if (seed.narrowLog) {
                                    const NarrowHalfExtents ext = narrowHalfExtentsForShape(seed.narrowAxis, seed.narrowShape);
                                    float halfExtent = 0.5f;
                                    if (faceType == 0 || faceType == 1) halfExtent = ext.x;
                                    else if (faceType == 2 || faceType == 3) halfExtent = ext.y;
                                    else if (faceType == 4 || faceType == 5) halfExtent = ext.z;
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
                                    if (seed.plantType == PlantType::Flower) {
                                        plantWidth = 0.86f;
                                        plantHeight = 0.92f;
                                    } else if (seed.plantType == PlantType::GrassShort) {
                                        plantWidth = 1.00f;
                                        plantHeight = 1.00f;
                                    }
                                    scaleVec.x *= plantWidth;
                                    scaleVec.y *= plantHeight;
                                    uvScaleVec = glm::vec2(1.0f);
                                    center.y += (plantHeight - 1.0f) * 0.5f * static_cast<float>(scale);
                                } else if (seed.narrowLog) {
                                    const NarrowHalfExtents ext = narrowHalfExtentsForShape(seed.narrowAxis, seed.narrowShape);
                                    float uScale = 1.0f;
                                    float vScale = 1.0f;
                                    if (faceType == 0 || faceType == 1) {
                                        uScale = ext.z * 2.0f; // U axis is Z.
                                        vScale = ext.y * 2.0f; // V axis is Y.
                                    } else if (faceType == 2 || faceType == 3) {
                                        uScale = ext.x * 2.0f; // U axis is X.
                                        vScale = ext.z * 2.0f; // V axis is Z.
                                    } else if (faceType == 4 || faceType == 5) {
                                        uScale = ext.x * 2.0f; // U axis is X.
                                        vScale = ext.y * 2.0f; // V axis is Y.
                                    }
                                    scaleVec.x *= uScale;
                                    scaleVec.y *= vScale;
                                    uvScaleVec.x *= uScale;
                                    uvScaleVec.y *= vScale;
                                }
                                if (seed.narrowShape != NarrowShape::Default) {
                                    const NarrowHalfExtents ext = narrowHalfExtentsForShape(seed.narrowAxis, seed.narrowShape);
                                    if (seed.narrowMount == NarrowMount::WallPosX) {
                                        center.x += (0.5f - ext.x - 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowMount == NarrowMount::WallNegX) {
                                        center.x += (-0.5f + ext.x + 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowMount == NarrowMount::WallPosZ) {
                                        center.z += (0.5f - ext.z - 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowMount == NarrowMount::WallNegZ) {
                                        center.z += (-0.5f + ext.z + 0.01f) * static_cast<float>(scale);
                                    } else if (seed.narrowMount == NarrowMount::Ceiling) {
                                        center.y += (0.5f - ext.y - 0.01f) * static_cast<float>(scale);
                                    } else {
                                        // Keep narrow-prop geometry resting near the floor of its occupied voxel.
                                        center.y += (-0.5f + ext.y + 0.01f) * static_cast<float>(scale);
                                    }
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
            buildPass(leafCells, -1.0f, 2, false);
            const bool includePlants = !(snap.cullPlantsBeyondLod0 && snap.lod > 0);
            if (includePlants) {
                buildPass(plantCells, -2.0f, 3, false);
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
                            glm::ivec3 lodCoord = minCoord + local;

                            auto tryEmit = [&](int faceType, float alpha) {
                                glm::ivec3 neighborCoord = lodCoord + VoxelMeshInitSystemLogic::FaceNormal(faceType);
                                int neighborType = neighborTypeAt(neighborCoord);
                                if (isSolidOccluderType(neighborType)) return;
                                int tileIndex = ::RenderInitSystemLogic::FaceTileIndexFor(snap.worldCtx, proto, faceType);
                                glm::vec4 ao = disableAo ? glm::vec4(1.0f) : computeSlopeFaceAo(lodCoord, faceType, alpha, cell.slopeDir);
                                emitSlopeFace(local, faceType, tileIndex, alpha, cell.color, ao);
                            };

                            tryEmit(3, 1.0f);
                            tryEmit(slopeTallFace(cell.slopeDir), 1.0f);

                            int capFaceA = 4;
                            int capFaceB = 5;
                            float capAlphaA = kSlopeCapAlphaA;
                            float capAlphaB = kSlopeCapAlphaB;
                            slopeCapFacesAndAlpha(cell.slopeDir, capFaceA, capAlphaA, capFaceB, capAlphaB);
                            tryEmit(capFaceA, capAlphaA);
                            tryEmit(capFaceB, capAlphaB);
                            tryEmit(2, slopeTopAlpha(cell.slopeDir));
                        }
                    }
                }
            };
            buildSlopePass();
            return true;
        }

        void ensureGreedyAsyncStarted(const std::vector<Entity>& prototypes) {
            std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
            g_voxelGreedyAsync.prototypes = &prototypes;
            if (g_voxelGreedyAsync.running) return;
            g_voxelGreedyAsync.stop = false;
            g_voxelGreedyAsync.running = true;
            g_voxelGreedyAsync.worker = std::thread([]() {
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

        bool enqueueGreedySnapshot(const VoxelWorldContext& voxelWorld,
                                   const WorldContext* worldCtx,
                                   const VoxelSectionKey& sectionKey,
                                   int superChunkMinLod,
                                   int superChunkMaxLod,
                                   int superChunkSize,
                                   bool cullPlantsBeyondLod0,
                                   bool waterTopOnlyOutsideLod0,
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

            std::vector<uint32_t> ids(static_cast<size_t>(dimX * dimY * dimZ), 0);
            std::vector<uint32_t> colors(static_cast<size_t>(dimX * dimY * dimZ), 0);
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
            out.waterTopOnlyOutsideLod0 = waterTopOnlyOutsideLod0;
            out.worldCtx = worldCtx;
            out.ids = std::move(ids);
            out.colors = std::move(colors);
            out.known = std::move(known);
            return true;
        }
    }

    void StopGreedyAsync() {
        {
            std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
            if (!g_voxelGreedyAsync.running) return;
            g_voxelGreedyAsync.stop = true;
        }
        g_voxelGreedyAsync.cv.notify_all();
        if (g_voxelGreedyAsync.worker.joinable()) {
            g_voxelGreedyAsync.worker.join();
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
        const bool cullPlantsBeyondLod0 = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "FoliageCullOutsideLod0", true);
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
        const double nowSec = glfwGetTime();
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
            if (BuildVoxelGreedyMesh(baseSystem, prototypes, editedKey, editSyncFastNoAo)) {
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

        ensureGreedyAsyncStarted(prototypes);

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
                if (!enqueueGreedySnapshot(voxelWorld,
                                           baseSystem.world.get(),
                                           renderKey,
                                           superChunkMinLod,
                                           superChunkMaxLod,
                                           superChunkSize,
                                           cullPlantsBeyondLod0,
                                           waterTopOnlyOutsideLod0,
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

    void UpdateVoxelMeshing(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float, GLFWwindow*) {
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
            ensureGreedyAsyncStarted(prototypes);
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
        const bool cullPlantsBeyondLod0 = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "FoliageCullOutsideLod0", true);
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
                    if (enqueueGreedySnapshot(voxelWorld, baseSystem.world.get(), renderKey,
                                              superChunkMinLod, superChunkMaxLod, superChunkSize,
                                              cullPlantsBeyondLod0,
                                              waterTopOnlyOutsideLod0,
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
