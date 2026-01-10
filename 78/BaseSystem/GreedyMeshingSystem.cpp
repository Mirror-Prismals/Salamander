#pragma once

#include <unordered_set>
#include <chrono>
#include <iostream>
#include <array>

namespace GreedyMeshingSystemLogic {

    namespace {
        struct IVec3HashLocal {
            std::size_t operator()(const glm::ivec3& v) const noexcept {
                uint64_t x = static_cast<uint32_t>(v.x);
                uint64_t y = static_cast<uint32_t>(v.y);
                uint64_t z = static_cast<uint32_t>(v.z);
                uint64_t h = (x * 73856093u) ^ (y * 19349663u) ^ (z * 83492791u);
                return static_cast<std::size_t>(h);
            }
        };

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

        static const std::array<glm::ivec3, 6> kFaceAxisX = {
            glm::ivec3(0, 0, 1),   // +X
            glm::ivec3(0, 0, -1),  // -X
            glm::ivec3(-1, 0, 0),  // +Y
            glm::ivec3(-1, 0, 0),  // -Y
            glm::ivec3(1, 0, 0),   // +Z
            glm::ivec3(-1, 0, 0)   // -Z
        };
        static const std::array<glm::ivec3, 6> kFaceAxisY = {
            glm::ivec3(0, 1, 0),   // +X
            glm::ivec3(0, 1, 0),   // -X
            glm::ivec3(0, 0, -1),  // +Y
            glm::ivec3(0, 0, 1),   // -Y
            glm::ivec3(0, 1, 0),   // +Z
            glm::ivec3(0, 1, 0)    // -Z
        };
        static const std::array<glm::ivec3, 6> kFaceNormals = {
            glm::ivec3(1, 0, 0),   // +X
            glm::ivec3(-1, 0, 0),  // -X
            glm::ivec3(0, 1, 0),   // +Y
            glm::ivec3(0, -1, 0),  // -Y
            glm::ivec3(0, 0, 1),   // +Z
            glm::ivec3(0, 0, -1)   // -Z
        };
        static const std::array<bool, 6> kAxisXNeg = {false, true, true, true, false, true};
        static const std::array<bool, 6> kAxisYNeg = {false, false, true, false, false, false};

        bool GreedyMeshingEnabled(const BaseSystem& baseSystem) {
            if (!baseSystem.registry) return false;
            auto it = baseSystem.registry->find("GreedyMeshingSystem");
            return it != baseSystem.registry->end()
                && std::holds_alternative<bool>(it->second)
                && std::get<bool>(it->second);
        }

        bool isOpaqueBlock(const Entity& proto) {
            return proto.isBlock && proto.isChunkable && proto.isOpaque;
        }

        bool isWaterBlock(const Entity& proto) {
            return proto.isBlock && proto.isChunkable && proto.name == "Water";
        }

        int faceTileIndexFor(const Entity& proto, int faceType, const WorldContext* worldCtx) {
            if (!worldCtx) return -1;
            if (!proto.useTexture) return -1;
            if (proto.prototypeID < 0 || proto.prototypeID >= static_cast<int>(worldCtx->prototypeTextureSets.size())) return -1;
            const FaceTextureSet& set = worldCtx->prototypeTextureSets[proto.prototypeID];
            auto resolve = [&](int specific) -> int {
                if (specific >= 0) return specific;
                if (set.all >= 0) return set.all;
                return -1;
            };
            int side = resolve(set.side);
            int top = resolve(set.top);
            int bottom = resolve(set.bottom);
            switch (faceType) {
                case 2: return top;
                case 3: return bottom;
                default: return side;
            }
        }

        float computeCornerAO(const glm::ivec3& cell,
                              const glm::ivec3& axisX,
                              const glm::ivec3& axisY,
                              int sx,
                              int sy,
                              bool includeWater,
                              const std::unordered_set<glm::ivec3, IVec3HashLocal>& occupiedSolids,
                              const std::unordered_set<glm::ivec3, IVec3HashLocal>& occupiedWater) {
            auto occludes = [&](const glm::ivec3& pos) {
                if (occupiedSolids.count(pos) > 0) return true;
                return includeWater && occupiedWater.count(pos) > 0;
            };
            glm::ivec3 side1 = cell + axisX * sx;
            glm::ivec3 side2 = cell + axisY * sy;
            glm::ivec3 corner = cell + axisX * sx + axisY * sy;
            int s1 = occludes(side1) ? 1 : 0;
            int s2 = occludes(side2) ? 1 : 0;
            int c = occludes(corner) ? 1 : 0;
            static const std::array<float, 4> kAOLevels = {1.0f, 0.94f, 0.88f, 0.82f};
            int occlusion = (s1 && s2) ? 3 : (s1 + s2 + c);
            return kAOLevels[occlusion];
        }

        glm::vec4 computeFaceAO(int faceType,
                                const glm::ivec3& cell,
                                bool includeWater,
                                const std::unordered_set<glm::ivec3, IVec3HashLocal>& occupiedSolids,
                                const std::unordered_set<glm::ivec3, IVec3HashLocal>& occupiedWater) {
            int idx = (faceType >= 0 && faceType < 6) ? faceType : 4;
            glm::ivec3 axisX = kFaceAxisX[idx];
            glm::ivec3 axisY = kFaceAxisY[idx];
            float aoBL = computeCornerAO(cell, axisX, axisY, -1, -1, includeWater, occupiedSolids, occupiedWater);
            float aoBR = computeCornerAO(cell, axisX, axisY, 1, -1, includeWater, occupiedSolids, occupiedWater);
            float aoTR = computeCornerAO(cell, axisX, axisY, 1, 1, includeWater, occupiedSolids, occupiedWater);
            float aoTL = computeCornerAO(cell, axisX, axisY, -1, 1, includeWater, occupiedSolids, occupiedWater);
            return glm::vec4(aoBL, aoBR, aoTR, aoTL);
        }

        int cellIndex(const glm::ivec3& local, const glm::ivec3& size) {
            return (local.x * size.y + local.y) * size.z + local.z;
        }

        bool inBounds(const glm::ivec3& local, const glm::ivec3& size) {
            return local.x >= 0 && local.x < size.x &&
                   local.y >= 0 && local.y < size.y &&
                   local.z >= 0 && local.z < size.z;
        }

        glm::ivec3 localCellFromUV(int faceType, int slice, int u, int v) {
            switch (faceType) {
                case 0:
                case 1:
                    return glm::ivec3(slice, v, u); // u=Z, v=Y
                case 2:
                case 3:
                    return glm::ivec3(u, slice, v); // u=X, v=Z
                case 4:
                case 5:
                    return glm::ivec3(u, v, slice); // u=X, v=Y
                default:
                    return glm::ivec3(0);
            }
        }

        bool sameColor(const glm::vec3& a, const glm::vec3& b) {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }

        bool sameKey(const MaskCell& a, const MaskCell& b) {
            return a.filled && b.filled && a.tileIndex == b.tileIndex && sameColor(a.color, b.color);
        }

        void appendGreedyFace(GreedyChunkData& out,
                              const glm::vec3& position,
                              const glm::vec3& color,
                              float alpha,
                              int faceType,
                              int tileIndex,
                              const glm::vec4& ao,
                              const glm::vec2& scale,
                              const glm::vec2& uvScale) {
            out.positions.push_back(position);
            out.colors.push_back(color);
            out.faceTypes.push_back(faceType);
            out.tileIndices.push_back(tileIndex);
            out.alphas.push_back(alpha);
            out.ao.push_back(ao);
            out.scales.push_back(scale);
            out.uvScales.push_back(uvScale);
        }
    }

    void MarkChunkDirty(BaseSystem& baseSystem, const ChunkKey& chunkKey) {
        if (!baseSystem.greedy) return;
        baseSystem.greedy->dirtyChunks.insert(chunkKey);
    }

    void RebuildChunkGreedy(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const ChunkKey& chunkKey) {
        if (!baseSystem.chunk || !baseSystem.greedy) return;
        const ChunkContext& chunkCtx = *baseSystem.chunk;
        GreedyContext& greedyCtx = *baseSystem.greedy;
        auto it = chunkCtx.chunks.find(chunkKey);
        if (it == chunkCtx.chunks.end()) {
            greedyCtx.chunks.erase(chunkKey);
            return;
        }

        auto start = std::chrono::steady_clock::now();
        const glm::ivec3 chunkSize = chunkCtx.chunkSize;
        const glm::ivec3 minCorner = chunkKey.chunkIndex * chunkSize;

        size_t occupancyEstimate = 0;
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    ChunkKey neighborKey{chunkKey.worldIndex, chunkKey.chunkIndex + glm::ivec3(dx, dy, dz)};
                    auto neighborIt = chunkCtx.chunks.find(neighborKey);
                    if (neighborIt == chunkCtx.chunks.end()) continue;
                    occupancyEstimate += neighborIt->second.positions.size();
                }
            }
        }

        std::unordered_set<glm::ivec3, IVec3HashLocal> occupiedSolids;
        std::unordered_set<glm::ivec3, IVec3HashLocal> occupiedWater;
        occupiedSolids.max_load_factor(0.7f);
        occupiedWater.max_load_factor(0.7f);
        occupiedSolids.reserve(occupancyEstimate);
        occupiedWater.reserve(occupancyEstimate / 2);

        std::vector<CellInfo> solidCells(chunkSize.x * chunkSize.y * chunkSize.z);
        std::vector<CellInfo> waterCells(chunkSize.x * chunkSize.y * chunkSize.z);

        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    ChunkKey neighborKey{chunkKey.worldIndex, chunkKey.chunkIndex + glm::ivec3(dx, dy, dz)};
                    auto neighborIt = chunkCtx.chunks.find(neighborKey);
                    if (neighborIt == chunkCtx.chunks.end()) continue;
                    const ChunkData& chunk = neighborIt->second;
                    for (size_t i = 0; i < chunk.positions.size(); ++i) {
                        int protoID = (i < chunk.prototypeIDs.size()) ? chunk.prototypeIDs[i] : -1;
                        if (protoID < 0 || protoID >= static_cast<int>(prototypes.size())) continue;
                        const Entity& proto = prototypes[protoID];
                        glm::ivec3 cell = glm::ivec3(glm::round(chunk.positions[i]));
                        if (isOpaqueBlock(proto)) {
                            occupiedSolids.insert(cell);
                            if (neighborKey == chunkKey) {
                                glm::ivec3 local = cell - minCorner;
                                if (!inBounds(local, chunkSize)) continue;
                                int idx = cellIndex(local, chunkSize);
                                solidCells[idx].opaque = true;
                                solidCells[idx].protoID = protoID;
                                solidCells[idx].color = (i < chunk.colors.size()) ? chunk.colors[i] : glm::vec3(1.0f);
                            }
                        } else if (isWaterBlock(proto)) {
                            occupiedWater.insert(cell);
                            if (neighborKey == chunkKey) {
                                glm::ivec3 local = cell - minCorner;
                                if (!inBounds(local, chunkSize)) continue;
                                int idx = cellIndex(local, chunkSize);
                                waterCells[idx].opaque = true;
                                waterCells[idx].protoID = protoID;
                                waterCells[idx].color = (i < chunk.colors.size()) ? chunk.colors[i] : glm::vec3(1.0f);
                            }
                        }
                    }
                }
            }
        }

        GreedyChunkData out;
        size_t quadCount = 0;
        auto buildPass = [&](const std::vector<CellInfo>& cellData, bool includeWaterAO, float alpha, bool isWaterPass) {
            for (int faceType = 0; faceType < 6; ++faceType) {
                int sliceLen = 0;
                int uLen = 0;
                int vLen = 0;
                switch (faceType) {
                    case 0:
                    case 1:
                        sliceLen = chunkSize.x; uLen = chunkSize.z; vLen = chunkSize.y; break;
                    case 2:
                    case 3:
                        sliceLen = chunkSize.y; uLen = chunkSize.x; vLen = chunkSize.z; break;
                    case 4:
                    case 5:
                        sliceLen = chunkSize.z; uLen = chunkSize.x; vLen = chunkSize.y; break;
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
                            glm::ivec3 local = localCellFromUV(faceType, slice, u, v);
                            if (!inBounds(local, chunkSize)) continue;
                            const CellInfo& cell = cellData[cellIndex(local, chunkSize)];
                            if (!cell.opaque) continue;
                            glm::ivec3 worldCell = local + minCorner;
                            bool neighborSolid = occupiedSolids.count(worldCell + kFaceNormals[faceType]) > 0;
                            bool neighborWater = occupiedWater.count(worldCell + kFaceNormals[faceType]) > 0;
                            if (isWaterPass) {
                                if (neighborSolid || neighborWater) continue;
                            } else {
                                if (neighborSolid) continue;
                            }
                            const Entity& proto = prototypes[cell.protoID];
                            int tileIndex = faceTileIndexFor(proto, faceType, baseSystem.world.get());
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
                                    center = glm::vec3(minCorner.x + axisCoord,
                                                       minCorner.y + centerV,
                                                       minCorner.z + centerU);
                                    break;
                                case 2:
                                case 3:
                                    center = glm::vec3(minCorner.x + centerU,
                                                       minCorner.y + axisCoord,
                                                       minCorner.z + centerV);
                                    break;
                                case 4:
                                case 5:
                                    center = glm::vec3(minCorner.x + centerU,
                                                       minCorner.y + centerV,
                                                       minCorner.z + axisCoord);
                                    break;
                                default:
                                    center = glm::vec3(minCorner);
                                    break;
                            }

                            int uMin = u;
                            int uMax = u + width - 1;
                            int vMin = v;
                            int vMax = v + height - 1;

                            int uBL = kAxisXNeg[faceType] ? uMax : uMin;
                            int vBL = kAxisYNeg[faceType] ? vMax : vMin;
                            int uBR = kAxisXNeg[faceType] ? uMin : uMax;
                            int vBR = kAxisYNeg[faceType] ? vMax : vMin;
                            int uTR = kAxisXNeg[faceType] ? uMin : uMax;
                            int vTR = kAxisYNeg[faceType] ? vMin : vMax;
                            int uTL = kAxisXNeg[faceType] ? uMax : uMin;
                            int vTL = kAxisYNeg[faceType] ? vMin : vMax;

                            glm::ivec3 localBL = localCellFromUV(faceType, slice, uBL, vBL);
                            glm::ivec3 localBR = localCellFromUV(faceType, slice, uBR, vBR);
                            glm::ivec3 localTR = localCellFromUV(faceType, slice, uTR, vTR);
                            glm::ivec3 localTL = localCellFromUV(faceType, slice, uTL, vTL);
                            glm::vec4 aoBL = computeFaceAO(faceType, localBL + minCorner, includeWaterAO, occupiedSolids, occupiedWater);
                            glm::vec4 aoBR = computeFaceAO(faceType, localBR + minCorner, includeWaterAO, occupiedSolids, occupiedWater);
                            glm::vec4 aoTR = computeFaceAO(faceType, localTR + minCorner, includeWaterAO, occupiedSolids, occupiedWater);
                            glm::vec4 aoTL = computeFaceAO(faceType, localTL + minCorner, includeWaterAO, occupiedSolids, occupiedWater);
                            glm::vec4 ao(aoBL.x, aoBR.y, aoTR.z, aoTL.w);

                            glm::vec2 scale(static_cast<float>(width), static_cast<float>(height));
                            appendGreedyFace(out, center, seed.color, alpha, faceType, seed.tileIndex, ao, scale, scale);
                            ++quadCount;
                        }
                    }
                }
            }
        };

        buildPass(solidCells, false, 1.0f, false);
        buildPass(waterCells, true, 0.6f, true);

        if (out.positions.empty()) {
            greedyCtx.chunks.erase(chunkKey);
        } else {
            greedyCtx.chunks[chunkKey] = std::move(out);
        }

        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        ).count();
        std::cout << "GreedyMeshingSystem: rebuilt chunk (world "
                  << chunkKey.worldIndex << ", "
                  << chunkKey.chunkIndex.x << ", "
                  << chunkKey.chunkIndex.y << ", "
                  << chunkKey.chunkIndex.z << ") with "
                  << quadCount << " quad(s) in "
                  << elapsedMs << " ms." << std::endl;
    }

    void RebuildAllGreedy(BaseSystem& baseSystem, std::vector<Entity>& prototypes) {
        if (!baseSystem.chunk || !baseSystem.greedy) return;
        const ChunkContext& chunkCtx = *baseSystem.chunk;
        GreedyContext& greedyCtx = *baseSystem.greedy;

        auto start = std::chrono::steady_clock::now();
        size_t totalQuads = 0;

        greedyCtx.chunks.clear();
        greedyCtx.dirtyChunks.clear();

        for (const auto& [chunkKey, _] : chunkCtx.chunks) {
            RebuildChunkGreedy(baseSystem, prototypes, chunkKey);
            auto it = greedyCtx.chunks.find(chunkKey);
            if (it != greedyCtx.chunks.end()) {
                totalQuads += it->second.positions.size();
            }
        }

        greedyCtx.initialized = true;
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        ).count();
        std::cout << "GreedyMeshingSystem: rebuilt all greedy meshes for "
                  << chunkCtx.chunks.size() << " chunk(s), "
                  << totalQuads << " quad(s) in "
                  << elapsedMs << " ms." << std::endl;
    }

    void UpdateGreedyMeshes(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt; (void)win;
        if (!baseSystem.chunk || !baseSystem.greedy) return;
        if (!GreedyMeshingEnabled(baseSystem)) return;
        if (!baseSystem.chunk->initialized) return;

        GreedyContext& greedyCtx = *baseSystem.greedy;
        if (!greedyCtx.initialized) {
            RebuildAllGreedy(baseSystem, prototypes);
            return;
        }

        if (!greedyCtx.dirtyChunks.empty()) {
            for (const auto& key : greedyCtx.dirtyChunks) {
                RebuildChunkGreedy(baseSystem, prototypes, key);
            }
            greedyCtx.dirtyChunks.clear();
        }

        const ChunkContext& chunkCtx = *baseSystem.chunk;
        std::vector<ChunkKey> stale;
        for (const auto& [key, _] : greedyCtx.chunks) {
            if (chunkCtx.chunks.find(key) == chunkCtx.chunks.end()) {
                stale.push_back(key);
            }
        }
        for (const auto& key : stale) {
            greedyCtx.chunks.erase(key);
            greedyCtx.dirtyChunks.erase(key);
        }
    }
}
