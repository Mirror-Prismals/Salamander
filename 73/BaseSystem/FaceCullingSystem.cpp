#pragma once

#include <unordered_set>
#include <chrono>
#include <iostream>
#include <cstdint>
#include <array>

namespace FaceCullingSystemLogic {

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

        bool isSolidOccluder(const Entity& proto) {
            if (!proto.isChunkable) return false;
            if (!proto.isSolid) return false;
            if (proto.name == "TransparentWave") return false;
            if (proto.name == "Water") return false;
            return true;
        }

        bool isWaterChunkable(const Entity& proto) {
            if (!proto.isChunkable) return false;
            if (proto.name != "Water") return false;
            return true;
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
                              bool isSolid,
                              bool isWater,
                              const std::unordered_set<glm::ivec3, IVec3HashLocal>& occupiedSolids,
                              const std::unordered_set<glm::ivec3, IVec3HashLocal>& occupiedWater) {
            auto occludes = [&](const glm::ivec3& pos) {
                if (isSolid) return occupiedSolids.count(pos) > 0;
                if (isWater) return occupiedSolids.count(pos) > 0 || occupiedWater.count(pos) > 0;
                return false;
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
                                bool isSolid,
                                bool isWater,
                                const std::unordered_set<glm::ivec3, IVec3HashLocal>& occupiedSolids,
                                const std::unordered_set<glm::ivec3, IVec3HashLocal>& occupiedWater) {
            int idx = (faceType >= 0 && faceType < 6) ? faceType : 4;
            glm::ivec3 axisX = kFaceAxisX[idx];
            glm::ivec3 axisY = kFaceAxisY[idx];
            float aoBL = computeCornerAO(cell, axisX, axisY, -1, -1, isSolid, isWater, occupiedSolids, occupiedWater);
            float aoBR = computeCornerAO(cell, axisX, axisY, 1, -1, isSolid, isWater, occupiedSolids, occupiedWater);
            float aoTR = computeCornerAO(cell, axisX, axisY, 1, 1, isSolid, isWater, occupiedSolids, occupiedWater);
            float aoTL = computeCornerAO(cell, axisX, axisY, -1, 1, isSolid, isWater, occupiedSolids, occupiedWater);
            return glm::vec4(aoBL, aoBR, aoTR, aoTL);
        }

        void appendFace(FaceChunkData& out,
                        const glm::vec3& position,
                        const glm::vec3& color,
                        float alpha,
                        int faceType,
                        int tileIndex,
                        const glm::vec4& ao) {
            out.positions.push_back(position);
            out.colors.push_back(color);
            out.faceTypes.push_back(faceType);
            out.tileIndices.push_back(tileIndex);
            out.alphas.push_back(alpha);
            out.ao.push_back(ao);
        }
    }

    void MarkChunkDirty(BaseSystem& baseSystem, const ChunkKey& chunkKey) {
        if (!baseSystem.face) return;
        baseSystem.face->dirtyChunks.insert(chunkKey);
    }

    void RebuildAllFaces(BaseSystem& baseSystem, std::vector<Entity>& prototypes) {
        if (!baseSystem.chunk || !baseSystem.face) return;
        const ChunkContext& chunkCtx = *baseSystem.chunk;
        FaceContext& faceCtx = *baseSystem.face;

        auto start = std::chrono::steady_clock::now();
        size_t totalBlocks = 0;
        size_t totalFaces = 0;

        faceCtx.faces.clear();
        faceCtx.dirtyChunks.clear();

        std::unordered_set<glm::ivec3, IVec3HashLocal> occupiedSolids;
        std::unordered_set<glm::ivec3, IVec3HashLocal> occupiedWater;
        occupiedSolids.max_load_factor(0.7f);
        occupiedWater.max_load_factor(0.7f);
        size_t totalPositions = 0;
        for (const auto& [chunkKey, chunk] : chunkCtx.chunks) {
            (void)chunkKey;
            totalPositions += chunk.positions.size();
        }
        occupiedSolids.reserve(totalPositions);
        occupiedWater.reserve(totalPositions / 2);

        for (const auto& [chunkKey, chunk] : chunkCtx.chunks) {
            (void)chunkKey;
            for (size_t i = 0; i < chunk.positions.size(); ++i) {
                int protoID = (i < chunk.prototypeIDs.size()) ? chunk.prototypeIDs[i] : -1;
                if (protoID < 0 || protoID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[protoID];
                if (isSolidOccluder(proto)) {
                    glm::ivec3 cell = glm::ivec3(glm::round(chunk.positions[i]));
                    occupiedSolids.insert(cell);
                } else if (isWaterChunkable(proto)) {
                    glm::ivec3 cell = glm::ivec3(glm::round(chunk.positions[i]));
                    occupiedWater.insert(cell);
                } else {
                    continue;
                }
            }
        }

        for (const auto& [chunkKey, chunk] : chunkCtx.chunks) {
            FaceChunkData out;
            for (size_t i = 0; i < chunk.positions.size(); ++i) {
                int protoID = (i < chunk.prototypeIDs.size()) ? chunk.prototypeIDs[i] : -1;
                if (protoID < 0 || protoID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[protoID];
                bool isSolid = isSolidOccluder(proto);
                bool isWater = isWaterChunkable(proto);
                if (!isSolid && !isWater) continue;

                ++totalBlocks;
                glm::vec3 pos = chunk.positions[i];
                glm::vec3 color = (i < chunk.colors.size()) ? chunk.colors[i] : glm::vec3(1.0f);
                glm::ivec3 cell = glm::ivec3(glm::round(pos));
                float alpha = proto.name == "Water" ? 0.6f : 1.0f;

                auto neighborSolid = [&](const glm::ivec3& offset){ return occupiedSolids.count(cell + offset) > 0; };
                auto neighborWater = [&](const glm::ivec3& offset){ return occupiedWater.count(cell + offset) > 0; };

                auto shouldCullFace = [&](const glm::ivec3& offset){
                    if (isSolid) {
                        // Solids are culled only by other solids. Water should not hide them.
                        return neighborSolid(offset);
                    } else { // water
                        // For water, skip faces against solids to avoid coplanar z-fight; allow faces against air or other water.
                        if (neighborSolid(offset)) return true;
                        return neighborWater(offset);
                    }
                };

                if (!shouldCullFace(glm::ivec3(1, 0, 0))) {
                    appendFace(out, pos + glm::vec3(0.5f, 0.0f, 0.0f), color, alpha, 0, faceTileIndexFor(proto, 0, baseSystem.world.get()),
                               computeFaceAO(0, cell, isSolid, isWater, occupiedSolids, occupiedWater));
                }
                if (!shouldCullFace(glm::ivec3(-1, 0, 0))) {
                    appendFace(out, pos + glm::vec3(-0.5f, 0.0f, 0.0f), color, alpha, 1, faceTileIndexFor(proto, 1, baseSystem.world.get()),
                               computeFaceAO(1, cell, isSolid, isWater, occupiedSolids, occupiedWater));
                }
                if (!shouldCullFace(glm::ivec3(0, 1, 0))) {
                    appendFace(out, pos + glm::vec3(0.0f, 0.5f, 0.0f), color, alpha, 2, faceTileIndexFor(proto, 2, baseSystem.world.get()),
                               computeFaceAO(2, cell, isSolid, isWater, occupiedSolids, occupiedWater));
                }
                if (!shouldCullFace(glm::ivec3(0, -1, 0))) {
                    appendFace(out, pos + glm::vec3(0.0f, -0.5f, 0.0f), color, alpha, 3, faceTileIndexFor(proto, 3, baseSystem.world.get()),
                               computeFaceAO(3, cell, isSolid, isWater, occupiedSolids, occupiedWater));
                }
                if (!shouldCullFace(glm::ivec3(0, 0, 1))) {
                    appendFace(out, pos + glm::vec3(0.0f, 0.0f, 0.5f), color, alpha, 4, faceTileIndexFor(proto, 4, baseSystem.world.get()),
                               computeFaceAO(4, cell, isSolid, isWater, occupiedSolids, occupiedWater));
                }
                if (!shouldCullFace(glm::ivec3(0, 0, -1))) {
                    appendFace(out, pos + glm::vec3(0.0f, 0.0f, -0.5f), color, alpha, 5, faceTileIndexFor(proto, 5, baseSystem.world.get()),
                               computeFaceAO(5, cell, isSolid, isWater, occupiedSolids, occupiedWater));
                }
            }
            size_t chunkFaceCount = out.positions.size();
            if (!out.positions.empty()) {
                faceCtx.faces[chunkKey] = std::move(out);
            }
            totalFaces += chunkFaceCount;
        }

        faceCtx.initialized = true;
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        ).count();
        std::cout << "FaceCullingSystem: rebuilt all faces for "
                  << chunkCtx.chunks.size() << " chunk(s), "
                  << totalBlocks << " block(s), "
                  << totalFaces << " face(s) in "
                  << elapsedMs << " ms." << std::endl;
    }

    void RebuildChunkFaces(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const ChunkKey& chunkKey) {
        if (!baseSystem.chunk || !baseSystem.face) return;
        const ChunkContext& chunkCtx = *baseSystem.chunk;
        FaceContext& faceCtx = *baseSystem.face;

        auto start = std::chrono::steady_clock::now();
        size_t totalBlocks = 0;

        std::unordered_set<glm::ivec3, IVec3HashLocal> occupiedSolids;
        std::unordered_set<glm::ivec3, IVec3HashLocal> occupiedWater;
        occupiedSolids.max_load_factor(0.7f);
        occupiedWater.max_load_factor(0.7f);
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
        occupiedSolids.reserve(occupancyEstimate);
        occupiedWater.reserve(occupancyEstimate / 2);
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
                        if (isSolidOccluder(proto)) {
                            occupiedSolids.insert(glm::ivec3(glm::round(chunk.positions[i])));
                        } else if (isWaterChunkable(proto)) {
                            occupiedWater.insert(glm::ivec3(glm::round(chunk.positions[i])));
                        }
                    }
                }
            }
        }

        auto it = chunkCtx.chunks.find(chunkKey);
        if (it == chunkCtx.chunks.end()) {
            faceCtx.faces.erase(chunkKey);
            return;
        }

        const ChunkData& chunk = it->second;
        FaceChunkData out;
        for (size_t i = 0; i < chunk.positions.size(); ++i) {
            int protoID = (i < chunk.prototypeIDs.size()) ? chunk.prototypeIDs[i] : -1;
            if (protoID < 0 || protoID >= static_cast<int>(prototypes.size())) continue;
            const Entity& proto = prototypes[protoID];
            bool isSolid = isSolidOccluder(proto);
            bool isWater = isWaterChunkable(proto);
            if (!isSolid && !isWater) continue;

            ++totalBlocks;
            glm::vec3 pos = chunk.positions[i];
            glm::vec3 color = (i < chunk.colors.size()) ? chunk.colors[i] : glm::vec3(1.0f);
            glm::ivec3 cell = glm::ivec3(glm::round(pos));
            float alpha = proto.name == "Water" ? 0.6f : 1.0f;

            auto neighborSolid = [&](const glm::ivec3& offset){ return occupiedSolids.count(cell + offset) > 0; };
            auto neighborWater = [&](const glm::ivec3& offset){ return occupiedWater.count(cell + offset) > 0; };

            auto shouldCullFace = [&](const glm::ivec3& offset){
                if (isSolid) {
                    return neighborSolid(offset);
                } else {
                    if (neighborSolid(offset)) return true; // avoid coplanar faces with solids
                    return neighborWater(offset);
                }
            };

            if (!shouldCullFace(glm::ivec3(1, 0, 0))) {
                appendFace(out, pos + glm::vec3(0.5f, 0.0f, 0.0f), color, alpha, 0, faceTileIndexFor(proto, 0, baseSystem.world.get()),
                           computeFaceAO(0, cell, isSolid, isWater, occupiedSolids, occupiedWater));
            }
            if (!shouldCullFace(glm::ivec3(-1, 0, 0))) {
                appendFace(out, pos + glm::vec3(-0.5f, 0.0f, 0.0f), color, alpha, 1, faceTileIndexFor(proto, 1, baseSystem.world.get()),
                           computeFaceAO(1, cell, isSolid, isWater, occupiedSolids, occupiedWater));
            }
            if (!shouldCullFace(glm::ivec3(0, 1, 0))) {
                appendFace(out, pos + glm::vec3(0.0f, 0.5f, 0.0f), color, alpha, 2, faceTileIndexFor(proto, 2, baseSystem.world.get()),
                           computeFaceAO(2, cell, isSolid, isWater, occupiedSolids, occupiedWater));
            }
            if (!shouldCullFace(glm::ivec3(0, -1, 0))) {
                appendFace(out, pos + glm::vec3(0.0f, -0.5f, 0.0f), color, alpha, 3, faceTileIndexFor(proto, 3, baseSystem.world.get()),
                           computeFaceAO(3, cell, isSolid, isWater, occupiedSolids, occupiedWater));
            }
            if (!shouldCullFace(glm::ivec3(0, 0, 1))) {
                appendFace(out, pos + glm::vec3(0.0f, 0.0f, 0.5f), color, alpha, 4, faceTileIndexFor(proto, 4, baseSystem.world.get()),
                           computeFaceAO(4, cell, isSolid, isWater, occupiedSolids, occupiedWater));
            }
            if (!shouldCullFace(glm::ivec3(0, 0, -1))) {
                appendFace(out, pos + glm::vec3(0.0f, 0.0f, -0.5f), color, alpha, 5, faceTileIndexFor(proto, 5, baseSystem.world.get()),
                           computeFaceAO(5, cell, isSolid, isWater, occupiedSolids, occupiedWater));
            }
        }

        if (out.positions.empty()) {
            faceCtx.faces.erase(chunkKey);
        } else {
            faceCtx.faces[chunkKey] = std::move(out);
        }
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        ).count();
        std::cout << "FaceCullingSystem: rebuilt faces for chunk (world "
                  << chunkKey.worldIndex << ", "
                  << chunkKey.chunkIndex.x << ", "
                  << chunkKey.chunkIndex.y << ", "
                  << chunkKey.chunkIndex.z << "), "
                  << totalBlocks << " block(s), "
                  << out.positions.size() << " face(s) in "
                  << elapsedMs << " ms." << std::endl;
    }

    void UpdateFaces(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt; (void)win;
        if (!baseSystem.chunk || !baseSystem.face) return;
        if (!baseSystem.chunk->initialized) return;
        FaceContext& faceCtx = *baseSystem.face;

        if (!faceCtx.initialized) {
            RebuildAllFaces(baseSystem, prototypes);
            return;
        }

        if (!faceCtx.dirtyChunks.empty()) {
            for (const auto& key : faceCtx.dirtyChunks) {
                RebuildChunkFaces(baseSystem, prototypes, key);
            }
            faceCtx.dirtyChunks.clear();
        }
    }
}
