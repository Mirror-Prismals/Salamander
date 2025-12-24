#pragma once

#include <unordered_set>

namespace FaceCullingSystemLogic {

    namespace {
        struct IVec3HashLocal {
            std::size_t operator()(const glm::ivec3& v) const noexcept {
                std::size_t hx = std::hash<int>()(v.x);
                std::size_t hy = std::hash<int>()(v.y);
                std::size_t hz = std::hash<int>()(v.z);
                return hx ^ (hy << 1) ^ (hz << 2);
            }
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

        void appendFace(FaceChunkData& out, const glm::vec3& position, const glm::vec3& color, float alpha, int faceType, int tileIndex) {
            out.positions.push_back(position);
            out.colors.push_back(color);
            out.faceTypes.push_back(faceType);
            out.tileIndices.push_back(tileIndex);
            out.alphas.push_back(alpha);
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

        faceCtx.faces.clear();
        faceCtx.dirtyChunks.clear();

        std::unordered_set<glm::ivec3, IVec3HashLocal> occupiedSolids;
        std::unordered_set<glm::ivec3, IVec3HashLocal> occupiedWater;
        occupiedSolids.reserve(chunkCtx.chunks.size() * 32);
        occupiedWater.reserve(chunkCtx.chunks.size() * 16);

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

                if (!shouldCullFace(glm::ivec3(1, 0, 0))) appendFace(out, pos + glm::vec3(0.5f, 0.0f, 0.0f), color, alpha, 0, faceTileIndexFor(proto, 0, baseSystem.world.get()));
                if (!shouldCullFace(glm::ivec3(-1, 0, 0))) appendFace(out, pos + glm::vec3(-0.5f, 0.0f, 0.0f), color, alpha, 1, faceTileIndexFor(proto, 1, baseSystem.world.get()));
                if (!shouldCullFace(glm::ivec3(0, 1, 0))) appendFace(out, pos + glm::vec3(0.0f, 0.5f, 0.0f), color, alpha, 2, faceTileIndexFor(proto, 2, baseSystem.world.get()));
                if (!shouldCullFace(glm::ivec3(0, -1, 0))) appendFace(out, pos + glm::vec3(0.0f, -0.5f, 0.0f), color, alpha, 3, faceTileIndexFor(proto, 3, baseSystem.world.get()));
                if (!shouldCullFace(glm::ivec3(0, 0, 1))) appendFace(out, pos + glm::vec3(0.0f, 0.0f, 0.5f), color, alpha, 4, faceTileIndexFor(proto, 4, baseSystem.world.get()));
                if (!shouldCullFace(glm::ivec3(0, 0, -1))) appendFace(out, pos + glm::vec3(0.0f, 0.0f, -0.5f), color, alpha, 5, faceTileIndexFor(proto, 5, baseSystem.world.get()));
            }
            if (!out.positions.empty()) {
                faceCtx.faces[chunkKey] = std::move(out);
            }
        }

        faceCtx.initialized = true;
    }

    void RebuildChunkFaces(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const ChunkKey& chunkKey) {
        if (!baseSystem.chunk || !baseSystem.face) return;
        const ChunkContext& chunkCtx = *baseSystem.chunk;
        FaceContext& faceCtx = *baseSystem.face;

        std::unordered_set<glm::ivec3, IVec3HashLocal> occupiedSolids;
        std::unordered_set<glm::ivec3, IVec3HashLocal> occupiedWater;
        for (const auto& [idx, chunk] : chunkCtx.chunks) {
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

            if (!shouldCullFace(glm::ivec3(1, 0, 0))) appendFace(out, pos + glm::vec3(0.5f, 0.0f, 0.0f), color, alpha, 0, faceTileIndexFor(proto, 0, baseSystem.world.get()));
            if (!shouldCullFace(glm::ivec3(-1, 0, 0))) appendFace(out, pos + glm::vec3(-0.5f, 0.0f, 0.0f), color, alpha, 1, faceTileIndexFor(proto, 1, baseSystem.world.get()));
            if (!shouldCullFace(glm::ivec3(0, 1, 0))) appendFace(out, pos + glm::vec3(0.0f, 0.5f, 0.0f), color, alpha, 2, faceTileIndexFor(proto, 2, baseSystem.world.get()));
            if (!shouldCullFace(glm::ivec3(0, -1, 0))) appendFace(out, pos + glm::vec3(0.0f, -0.5f, 0.0f), color, alpha, 3, faceTileIndexFor(proto, 3, baseSystem.world.get()));
            if (!shouldCullFace(glm::ivec3(0, 0, 1))) appendFace(out, pos + glm::vec3(0.0f, 0.0f, 0.5f), color, alpha, 4, faceTileIndexFor(proto, 4, baseSystem.world.get()));
            if (!shouldCullFace(glm::ivec3(0, 0, -1))) appendFace(out, pos + glm::vec3(0.0f, 0.0f, -0.5f), color, alpha, 5, faceTileIndexFor(proto, 5, baseSystem.world.get()));
        }

        if (out.positions.empty()) {
            faceCtx.faces.erase(chunkKey);
        } else {
            faceCtx.faces[chunkKey] = std::move(out);
        }
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
