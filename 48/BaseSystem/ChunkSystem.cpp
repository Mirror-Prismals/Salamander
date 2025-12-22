#pragma once

#include <cmath>

namespace ChunkSystemLogic {

    namespace {
        int floorDivInt(int value, int divisor) {
            if (divisor <= 0) return 0;
            if (value >= 0) return value / divisor;
            return -(((-value) + divisor - 1) / divisor);
        }

        glm::ivec3 chunkIndexFromPosition(const glm::vec3& position, const glm::ivec3& chunkSize) {
            int x = static_cast<int>(std::floor(position.x));
            int y = static_cast<int>(std::floor(position.y));
            int z = static_cast<int>(std::floor(position.z));
            return glm::ivec3(
                floorDivInt(x, chunkSize.x),
                floorDivInt(y, chunkSize.y),
                floorDivInt(z, chunkSize.z)
            );
        }

        bool isChunkablePrototype(const Entity& proto) {
            return proto.isChunkable;
        }

        void appendInstance(ChunkData& chunk, const EntityInstance& inst) {
            chunk.positions.push_back(inst.position);
            chunk.colors.push_back(inst.color);
            chunk.rotations.push_back(inst.rotation);
            chunk.prototypeIDs.push_back(inst.prototypeID);
        }

        bool isInsideChunk(const glm::vec3& position, const glm::vec3& minCorner, const glm::vec3& maxCorner) {
            return position.x >= minCorner.x && position.x < maxCorner.x &&
                   position.y >= minCorner.y && position.y < maxCorner.y &&
                   position.z >= minCorner.z && position.z < maxCorner.z;
        }
    }

    void MarkChunkDirty(BaseSystem& baseSystem, const glm::vec3& position) {
        if (!baseSystem.chunk) return;
        ChunkContext& chunkCtx = *baseSystem.chunk;
        glm::ivec3 idx = chunkIndexFromPosition(position, chunkCtx.chunkSize);
        chunkCtx.dirtyChunks.insert(idx);
    }

    void RebuildAllChunks(BaseSystem& baseSystem, std::vector<Entity>& prototypes) {
        if (!baseSystem.level || !baseSystem.chunk) return;
        ChunkContext& chunkCtx = *baseSystem.chunk;
        chunkCtx.chunks.clear();
        chunkCtx.dirtyChunks.clear();

        for (const auto& world : baseSystem.level->worlds) {
            for (const auto& inst : world.instances) {
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[inst.prototypeID];
                if (!isChunkablePrototype(proto)) continue;
                glm::ivec3 idx = chunkIndexFromPosition(inst.position, chunkCtx.chunkSize);
                ChunkData& chunk = chunkCtx.chunks[idx];
                appendInstance(chunk, inst);
            }
        }

        chunkCtx.initialized = true;
    }

    void RebuildChunk(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const glm::ivec3& chunkIndex) {
        if (!baseSystem.level || !baseSystem.chunk) return;
        ChunkContext& chunkCtx = *baseSystem.chunk;

        glm::vec3 minCorner = glm::vec3(chunkIndex * chunkCtx.chunkSize);
        glm::vec3 maxCorner = minCorner + glm::vec3(chunkCtx.chunkSize);

        ChunkData rebuilt;
        for (const auto& world : baseSystem.level->worlds) {
            for (const auto& inst : world.instances) {
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[inst.prototypeID];
                if (!isChunkablePrototype(proto)) continue;
                if (!isInsideChunk(inst.position, minCorner, maxCorner)) continue;
                appendInstance(rebuilt, inst);
            }
        }

        if (rebuilt.positions.empty()) {
            chunkCtx.chunks.erase(chunkIndex);
        } else {
            chunkCtx.chunks[chunkIndex] = std::move(rebuilt);
        }
    }

    void UpdateChunks(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt; (void)win;
        if (!baseSystem.level || !baseSystem.chunk) return;
        ChunkContext& chunkCtx = *baseSystem.chunk;
        if (!chunkCtx.initialized) {
            RebuildAllChunks(baseSystem, prototypes);
            return;
        }
        if (!chunkCtx.dirtyChunks.empty()) {
            for (const auto& idx : chunkCtx.dirtyChunks) {
                RebuildChunk(baseSystem, prototypes, idx);
            }
            chunkCtx.dirtyChunks.clear();
        }
    }
}
