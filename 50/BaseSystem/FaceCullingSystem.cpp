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

        bool isOpaqueChunkable(const Entity& proto) {
            if (!proto.isChunkable) return false;
            if (!proto.isSolid) return false;
            if (proto.name == "Water" || proto.name == "TransparentWave") return false;
            return true;
        }

        void appendFace(FaceChunkData& out, const glm::vec3& position, const glm::vec3& color, int faceType) {
            out.positions.push_back(position);
            out.colors.push_back(color);
            out.faceTypes.push_back(faceType);
        }
    }

    void MarkChunkDirty(BaseSystem& baseSystem, const glm::ivec3& chunkIndex) {
        if (!baseSystem.face) return;
        baseSystem.face->dirtyChunks.insert(chunkIndex);
    }

    void RebuildAllFaces(BaseSystem& baseSystem, std::vector<Entity>& prototypes) {
        if (!baseSystem.chunk || !baseSystem.face) return;
        const ChunkContext& chunkCtx = *baseSystem.chunk;
        FaceContext& faceCtx = *baseSystem.face;

        faceCtx.faces.clear();
        faceCtx.dirtyChunks.clear();

        std::unordered_set<glm::ivec3, IVec3HashLocal> occupied;
        occupied.reserve(chunkCtx.chunks.size() * 32);

        for (const auto& [chunkIndex, chunk] : chunkCtx.chunks) {
            (void)chunkIndex;
            for (size_t i = 0; i < chunk.positions.size(); ++i) {
                int protoID = (i < chunk.prototypeIDs.size()) ? chunk.prototypeIDs[i] : -1;
                if (protoID < 0 || protoID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[protoID];
                if (!isOpaqueChunkable(proto)) continue;
                glm::ivec3 cell = glm::ivec3(glm::round(chunk.positions[i]));
                occupied.insert(cell);
            }
        }

        for (const auto& [chunkIndex, chunk] : chunkCtx.chunks) {
            FaceChunkData out;
            for (size_t i = 0; i < chunk.positions.size(); ++i) {
                int protoID = (i < chunk.prototypeIDs.size()) ? chunk.prototypeIDs[i] : -1;
                if (protoID < 0 || protoID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[protoID];
                if (!isOpaqueChunkable(proto)) continue;

                glm::vec3 pos = chunk.positions[i];
                glm::vec3 color = (i < chunk.colors.size()) ? chunk.colors[i] : glm::vec3(1.0f);
                glm::ivec3 cell = glm::ivec3(glm::round(pos));

                if (!occupied.count(cell + glm::ivec3(1, 0, 0))) appendFace(out, pos + glm::vec3(0.5f, 0.0f, 0.0f), color, 0);
                if (!occupied.count(cell + glm::ivec3(-1, 0, 0))) appendFace(out, pos + glm::vec3(-0.5f, 0.0f, 0.0f), color, 1);
                if (!occupied.count(cell + glm::ivec3(0, 1, 0))) appendFace(out, pos + glm::vec3(0.0f, 0.5f, 0.0f), color, 2);
                if (!occupied.count(cell + glm::ivec3(0, -1, 0))) appendFace(out, pos + glm::vec3(0.0f, -0.5f, 0.0f), color, 3);
                if (!occupied.count(cell + glm::ivec3(0, 0, 1))) appendFace(out, pos + glm::vec3(0.0f, 0.0f, 0.5f), color, 4);
                if (!occupied.count(cell + glm::ivec3(0, 0, -1))) appendFace(out, pos + glm::vec3(0.0f, 0.0f, -0.5f), color, 5);
            }
            if (!out.positions.empty()) {
                faceCtx.faces[chunkIndex] = std::move(out);
            }
        }

        faceCtx.initialized = true;
    }

    void RebuildChunkFaces(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const glm::ivec3& chunkIndex) {
        if (!baseSystem.chunk || !baseSystem.face) return;
        const ChunkContext& chunkCtx = *baseSystem.chunk;
        FaceContext& faceCtx = *baseSystem.face;

        std::unordered_set<glm::ivec3, IVec3HashLocal> occupied;
        for (const auto& [idx, chunk] : chunkCtx.chunks) {
            (void)idx;
            for (size_t i = 0; i < chunk.positions.size(); ++i) {
                int protoID = (i < chunk.prototypeIDs.size()) ? chunk.prototypeIDs[i] : -1;
                if (protoID < 0 || protoID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[protoID];
                if (!isOpaqueChunkable(proto)) continue;
                glm::ivec3 cell = glm::ivec3(glm::round(chunk.positions[i]));
                occupied.insert(cell);
            }
        }

        auto it = chunkCtx.chunks.find(chunkIndex);
        if (it == chunkCtx.chunks.end()) {
            faceCtx.faces.erase(chunkIndex);
            return;
        }

        const ChunkData& chunk = it->second;
        FaceChunkData out;
        for (size_t i = 0; i < chunk.positions.size(); ++i) {
            int protoID = (i < chunk.prototypeIDs.size()) ? chunk.prototypeIDs[i] : -1;
            if (protoID < 0 || protoID >= static_cast<int>(prototypes.size())) continue;
            const Entity& proto = prototypes[protoID];
            if (!isOpaqueChunkable(proto)) continue;

            glm::vec3 pos = chunk.positions[i];
            glm::vec3 color = (i < chunk.colors.size()) ? chunk.colors[i] : glm::vec3(1.0f);
            glm::ivec3 cell = glm::ivec3(glm::round(pos));

            if (!occupied.count(cell + glm::ivec3(1, 0, 0))) appendFace(out, pos + glm::vec3(0.5f, 0.0f, 0.0f), color, 0);
            if (!occupied.count(cell + glm::ivec3(-1, 0, 0))) appendFace(out, pos + glm::vec3(-0.5f, 0.0f, 0.0f), color, 1);
            if (!occupied.count(cell + glm::ivec3(0, 1, 0))) appendFace(out, pos + glm::vec3(0.0f, 0.5f, 0.0f), color, 2);
            if (!occupied.count(cell + glm::ivec3(0, -1, 0))) appendFace(out, pos + glm::vec3(0.0f, -0.5f, 0.0f), color, 3);
            if (!occupied.count(cell + glm::ivec3(0, 0, 1))) appendFace(out, pos + glm::vec3(0.0f, 0.0f, 0.5f), color, 4);
            if (!occupied.count(cell + glm::ivec3(0, 0, -1))) appendFace(out, pos + glm::vec3(0.0f, 0.0f, -0.5f), color, 5);
        }

        if (out.positions.empty()) {
            faceCtx.faces.erase(chunkIndex);
        } else {
            faceCtx.faces[chunkIndex] = std::move(out);
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
            for (const auto& idx : faceCtx.dirtyChunks) {
                RebuildChunkFaces(baseSystem, prototypes, idx);
            }
            faceCtx.dirtyChunks.clear();
        }
    }
}
