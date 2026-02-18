#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <algorithm>
#include <array>
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
            const WorldContext* worldCtx = nullptr;
            std::vector<uint32_t> ids;
            std::vector<uint32_t> colors;
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

        bool BuildVoxelGreedyMesh(BaseSystem& baseSystem,
                                  std::vector<Entity>& prototypes,
                                  const VoxelSectionKey& sectionKey) {
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
                bool opaque = false;
                int protoID = -1;
                glm::vec3 color = glm::vec3(1.0f);
            };
            struct MaskCell {
                bool filled = false;
                int tileIndex = -1;
                glm::vec3 color = glm::vec3(1.0f);
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

            auto classifyProto = [&](uint32_t id, int& outType) {
                if (id == 0 || id >= prototypes.size()) { outType = 0; return; }
                const Entity& proto = prototypes[id];
                if (proto.isBlock && (proto.isOpaque || proto.isOccluder)) {
                    outType = 1;
                } else if (proto.isBlock && proto.name == "Water") {
                    outType = 2;
                } else {
                    outType = 3;
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
                        if (proto.isBlock && (proto.isOpaque || proto.isOccluder)) {
                            solidCells[idx] = {true, static_cast<int>(id), color};
                        } else if (proto.isBlock && proto.name == "Water") {
                            waterCells[idx] = {true, static_cast<int>(id), color};
                        }
                    }
                }
            }

            auto neighborTypeAt = [&](const glm::ivec3& lodCoord) {
                glm::ivec3 local = lodCoord - minCoord;
                int type = 0;
                if (inBounds(local)) {
                    int idx = cellIndex(local);
                    if (solidCells[idx].opaque) return 1;
                    if (waterCells[idx].opaque) return 2;
                    uint32_t id = section.ids[idx];
                    classifyProto(id, type);
                    return type;
                }
                uint32_t id = VoxelMeshInitSystemLogic::GetVoxelIdAtLod(voxelWorld, section.lod, lodCoord);
                classifyProto(id, type);
                return type;
            };

            auto sameColor = [](const glm::vec3& a, const glm::vec3& b) {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            };
            auto sameKey = [&](const MaskCell& a, const MaskCell& b) {
                return a.filled && b.filled && a.tileIndex == b.tileIndex && sameColor(a.color, b.color);
            };

            GreedyChunkData out = acquireGreedyChunk(voxelGreedy);
            auto buildPass = [&](const std::vector<CellInfo>& cellData, float alpha, bool isWaterPass) {
                for (int faceType = 0; faceType < 6; ++faceType) {
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
                                if (!cell.opaque) continue;
                                glm::ivec3 lodCoord = minCoord + local;
                                glm::ivec3 neighborCoord = lodCoord + VoxelMeshInitSystemLogic::FaceNormal(faceType);
                                int neighborType = neighborTypeAt(neighborCoord);
                                if (isWaterPass) {
                                    if (neighborType == 1 || neighborType == 2) continue;
                                } else {
                                    if (neighborType == 1) continue;
                                }
                                if (cell.protoID < 0 || cell.protoID >= static_cast<int>(prototypes.size())) continue;
                                const Entity& proto = prototypes[cell.protoID];
                                int tileIndex = ::RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), proto, faceType);
                                int idx = v * uLen + u;
                                mask[idx].filled = true;
                                mask[idx].tileIndex = tileIndex;
                                mask[idx].color = cell.color;
                            }
                        }

                        for (int v = 0; v < vLen; ++v) {
                            for (int u = 0; u < uLen; ++u) {
                                int idx = v * uLen + u;
                                if (!mask[idx].filled) continue;
                                MaskCell seed = mask[idx];
                                int width = 1;
                                while (u + width < uLen && sameKey(seed, mask[v * uLen + (u + width)])) {
                                    ++width;
                                }
                                int height = 1;
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

                                for (int dv = 0; dv < height; ++dv) {
                                    for (int du = 0; du < width; ++du) {
                                        mask[(v + dv) * uLen + (u + du)].filled = false;
                                    }
                                }

                                float centerU = static_cast<float>(u) + (static_cast<float>(width - 1) * 0.5f);
                                float centerV = static_cast<float>(v) + (static_cast<float>(height - 1) * 0.5f);
                                float axisOffset = (faceType % 2 == 0) ? 0.5f : -0.5f;
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
                                out.positions.push_back(center);
                                out.colors.push_back(seed.color);
                                out.faceTypes.push_back(faceType);
                                out.tileIndices.push_back(seed.tileIndex);
                                out.alphas.push_back(alpha);
                                out.ao.push_back(glm::vec4(1.0f));
                                out.scales.push_back(scaleVec);
                                out.uvScales.push_back(scaleVec);
                            }
                        }
                    }
                }
            };

            buildPass(solidCells, 1.0f, false);
            buildPass(waterCells, 0.6f, true);

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
                                              GreedyChunkData& out) {
            int sizeX = snap.sizeX;
            int sizeY = snap.sizeY;
            int sizeZ = snap.sizeZ;
            int scale = 1 << snap.lod;
            glm::ivec3 minCoord = snap.minCoord;

            struct CellInfo {
                bool opaque = false;
                int protoID = -1;
                glm::vec3 color = glm::vec3(1.0f);
            };
            struct MaskCell {
                bool filled = false;
                int tileIndex = -1;
                glm::vec3 color = glm::vec3(1.0f);
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

            auto classifyProto = [&](uint32_t id, int& outType) {
                if (id == 0 || id >= prototypes.size()) { outType = 0; return; }
                const Entity& proto = prototypes[id];
                if (proto.isBlock && (proto.isOpaque || proto.isOccluder)) {
                    outType = 1;
                } else if (proto.isBlock && proto.name == "Water") {
                    outType = 2;
                } else {
                    outType = 3;
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
                        if (proto.isBlock && (proto.isOpaque || proto.isOccluder)) {
                            solidCells[idx] = {true, static_cast<int>(id), color};
                        } else if (proto.isBlock && proto.name == "Water") {
                            waterCells[idx] = {true, static_cast<int>(id), color};
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

            auto sameColor = [](const glm::vec3& a, const glm::vec3& b) {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            };
            auto sameKey = [&](const MaskCell& a, const MaskCell& b) {
                return a.filled && b.filled && a.tileIndex == b.tileIndex && sameColor(a.color, b.color);
            };

            auto buildPass = [&](const std::vector<CellInfo>& cellData, float alpha, bool isWaterPass) {
                for (int faceType = 0; faceType < 6; ++faceType) {
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
                                if (!cell.opaque) continue;
                                glm::ivec3 lodCoord = minCoord + local;
                                glm::ivec3 neighborCoord = lodCoord + VoxelMeshInitSystemLogic::FaceNormal(faceType);
                                int neighborType = neighborTypeAt(neighborCoord);
                                if (isWaterPass) {
                                    if (neighborType == 1 || neighborType == 2) continue;
                                } else {
                                    if (neighborType == 1) continue;
                                }
                                if (cell.protoID < 0 || cell.protoID >= static_cast<int>(prototypes.size())) continue;
                                const Entity& proto = prototypes[cell.protoID];
                                int tileIndex = ::RenderInitSystemLogic::FaceTileIndexFor(snap.worldCtx, proto, faceType);
                                int idx = v * uLen + u;
                                mask[idx].filled = true;
                                mask[idx].tileIndex = tileIndex;
                                mask[idx].color = cell.color;
                            }
                        }

                        for (int v = 0; v < vLen; ++v) {
                            for (int u = 0; u < uLen; ++u) {
                                int idx = v * uLen + u;
                                if (!mask[idx].filled) continue;
                                MaskCell seed = mask[idx];
                                int width = 1;
                                while (u + width < uLen && sameKey(seed, mask[v * uLen + (u + width)])) {
                                    ++width;
                                }
                                int height = 1;
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

                                for (int dv = 0; dv < height; ++dv) {
                                    for (int du = 0; du < width; ++du) {
                                        mask[(v + dv) * uLen + (u + du)].filled = false;
                                    }
                                }

                                float centerU = static_cast<float>(u) + (static_cast<float>(width - 1) * 0.5f);
                                float centerV = static_cast<float>(v) + (static_cast<float>(height - 1) * 0.5f);
                                float axisOffset = (faceType % 2 == 0) ? 0.5f : -0.5f;
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
                                out.positions.push_back(center);
                                out.colors.push_back(seed.color);
                                out.faceTypes.push_back(faceType);
                                out.tileIndices.push_back(seed.tileIndex);
                                out.alphas.push_back(alpha);
                                out.ao.push_back(glm::vec4(1.0f));
                                out.scales.push_back(scaleVec);
                                out.uvScales.push_back(scaleVec);
                            }
                        }
                    }
                }
            };

            buildPass(solidCells, 1.0f, false);
            buildPass(waterCells, 0.6f, true);
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
                                   const VoxelSectionKey& renderKey,
                                   int superChunkMinLod,
                                   int superChunkMaxLod,
                                   int superChunkSize,
                                   VoxelGreedySnapshot& out) {
            int lod = renderKey.lod;
            int size = VoxelMeshInitSystemLogic::SectionSizeForLod(voxelWorld, lod);
            bool useSuperChunk = lod >= superChunkMinLod
                && lod <= superChunkMaxLod
                && superChunkSize > 1;
            int chunkSize = useSuperChunk ? superChunkSize : 1;
            glm::ivec3 anchorCoord = renderKey.coord;

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
            out.worldCtx = worldCtx;
            out.ids = std::move(ids);
            out.colors = std::move(colors);
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
        g_lastGreedyQueued = 0;
        g_lastGreedyApplied = 0;
        g_lastGreedyDropped = 0;

        auto logVoxelPerf = [&](const char* label,
                                const std::chrono::steady_clock::time_point& t0,
                                size_t count) {
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
        if (superChunkSize < 1) superChunkSize = 1;

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
            std::sort(buildList.begin(), buildList.end(), [](const VoxelSectionKey& a, const VoxelSectionKey& b) {
                if (a.lod != b.lod) return a.lod < b.lod;
                if (a.coord.x != b.coord.x) return a.coord.x < b.coord.x;
                if (a.coord.y != b.coord.y) return a.coord.y < b.coord.y;
                return a.coord.z < b.coord.z;
            });
            size_t buildLimit = buildList.size();
            if (useVoxelGreedyAsync) {
                const size_t queueLimit = 64;
                std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
                if (g_voxelGreedyAsync.queue.size() >= queueLimit) {
                    buildLimit = 0;
                }
            }
            auto start = std::chrono::steady_clock::now();
            size_t buildCount = 0;
            std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> retry;
            for (size_t i = 0; i < buildLimit; ++i) {
                const auto& key = buildList[i];
                if (useVoxelGreedyAsync) {
                    VoxelGreedySnapshot snap;
                    if (enqueueGreedySnapshot(voxelWorld, baseSystem.world.get(), key,
                                              superChunkMinLod, superChunkMaxLod, superChunkSize,
                                              snap)) {
                        {
                            std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
                            g_voxelGreedyAsync.queue.push_back(std::move(snap));
                            g_voxelGreedyAsync.inFlight.insert(key);
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
            for (size_t i = buildLimit; i < buildList.size(); ++i) {
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
