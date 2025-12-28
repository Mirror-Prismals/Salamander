#pragma once

#include <cmath>
#include <chrono>
#include <iostream>

namespace FaceCullingSystemLogic { void MarkChunkDirty(BaseSystem&, const ChunkKey&); }

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

        void appendInstance(ChunkData& chunk, const EntityInstance& inst, int worldIndex) {
            chunk.positions.push_back(inst.position);
            chunk.colors.push_back(inst.color);
            chunk.rotations.push_back(inst.rotation);
            chunk.prototypeIDs.push_back(inst.prototypeID);
            chunk.worldIndices.push_back(worldIndex);
        }

        bool isInsideChunk(const glm::vec3& position, const glm::vec3& minCorner, const glm::vec3& maxCorner) {
            return position.x >= minCorner.x && position.x < maxCorner.x &&
                   position.y >= minCorner.y && position.y < maxCorner.y &&
                   position.z >= minCorner.z && position.z < maxCorner.z;
        }

        ChunkKey chunkKeyFromPosition(int worldIndex, const glm::vec3& position, const glm::ivec3& chunkSize) {
            return ChunkKey{worldIndex, chunkIndexFromPosition(position, chunkSize)};
        }

        float chunkCenterDistance(const ChunkKey& key, const glm::vec3& cameraPos, const glm::ivec3& chunkSize) {
            glm::vec3 minCorner = glm::vec3(key.chunkIndex * chunkSize);
            glm::vec3 center = minCorner + (glm::vec3(chunkSize) * 0.5f);
            return glm::length(center - cameraPos);
        }
    }

    void MarkChunkDirty(BaseSystem& baseSystem, int worldIndex, const glm::vec3& position) {
        if (!baseSystem.chunk || worldIndex < 0) return;
        ChunkContext& chunkCtx = *baseSystem.chunk;
        ChunkKey key = chunkKeyFromPosition(worldIndex, position, chunkCtx.chunkSize);
        chunkCtx.dirtyChunks.insert(key);
        chunkCtx.renderBuffersDirty.insert(key);
        chunkCtx.chunkIndexDirty = true;
    }

    void RebuildChunk(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const ChunkKey& key) {
        if (!baseSystem.level || !baseSystem.chunk) return;
        ChunkContext& chunkCtx = *baseSystem.chunk;
        if (key.worldIndex < 0 || key.worldIndex >= static_cast<int>(baseSystem.level->worlds.size())) return;

        glm::vec3 minCorner = glm::vec3(key.chunkIndex * chunkCtx.chunkSize);
        glm::vec3 maxCorner = minCorner + glm::vec3(chunkCtx.chunkSize);

        ChunkData rebuilt;
        const auto& world = baseSystem.level->worlds[key.worldIndex];
        auto lutIt = chunkCtx.chunkInstanceLUT.find(key);
        if (lutIt != chunkCtx.chunkInstanceLUT.end()) {
            for (size_t instIdx : lutIt->second) {
                if (instIdx >= world.instances.size()) continue;
                const auto& inst = world.instances[instIdx];
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[inst.prototypeID];
                if (!isChunkablePrototype(proto)) continue;
                if (!isInsideChunk(inst.position, minCorner, maxCorner)) continue;
                appendInstance(rebuilt, inst, static_cast<int>(key.worldIndex));
            }
        }

        if (rebuilt.positions.empty()) {
            chunkCtx.chunks.erase(key);
        } else {
            chunkCtx.chunks[key] = std::move(rebuilt);
        }
        chunkCtx.renderBuffersDirty.insert(key);
        if (baseSystem.face) {
            FaceCullingSystemLogic::MarkChunkDirty(baseSystem, key);
        }
    }

    void UpdateChunks(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt; (void)win;
        if (!baseSystem.level || !baseSystem.chunk || !baseSystem.player) return;
        ChunkContext& chunkCtx = *baseSystem.chunk;
        if (!chunkCtx.initialized) {
            chunkCtx.initialized = true;
        }

        if (chunkCtx.renderDistanceChunks <= 0) {
            chunkCtx.renderDistanceChunks = 6;
        }
        if (chunkCtx.unloadDistanceChunks <= chunkCtx.renderDistanceChunks) {
            chunkCtx.unloadDistanceChunks = chunkCtx.renderDistanceChunks + 1;
        }

        glm::vec3 cameraPos = baseSystem.player->cameraPosition;
        int renderDistChunks = chunkCtx.renderDistanceChunks;
        int unloadDistChunks = chunkCtx.unloadDistanceChunks;
        float maxDim = static_cast<float>(std::max(chunkCtx.chunkSize.x, std::max(chunkCtx.chunkSize.y, chunkCtx.chunkSize.z)));
        float renderRadius = static_cast<float>(renderDistChunks) * maxDim;
        float unloadRadius = static_cast<float>(unloadDistChunks) * maxDim;

        if (chunkCtx.chunkIndexDirty) {
            auto start = std::chrono::steady_clock::now();
            size_t totalInstances = 0;
            size_t chunkableInstances = 0;
            chunkCtx.chunkInstanceLUT.clear();
            chunkCtx.worldHasChunkable.assign(baseSystem.level->worlds.size(), false);
            chunkCtx.worldHasNonChunkable.assign(baseSystem.level->worlds.size(), false);
            for (int worldIndex = 0; worldIndex < static_cast<int>(baseSystem.level->worlds.size()); ++worldIndex) {
                const auto& world = baseSystem.level->worlds[worldIndex];
                for (size_t i = 0; i < world.instances.size(); ++i) {
                    ++totalInstances;
                    const auto& inst = world.instances[i];
                    if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                    const Entity& proto = prototypes[inst.prototypeID];
                    if (proto.isChunkable) {
                        chunkCtx.worldHasChunkable[worldIndex] = true;
                    } else {
                        chunkCtx.worldHasNonChunkable[worldIndex] = true;
                    }
                    if (!isChunkablePrototype(proto)) continue;
                    ++chunkableInstances;
                    ChunkKey key = chunkKeyFromPosition(worldIndex, inst.position, chunkCtx.chunkSize);
                    chunkCtx.chunkInstanceLUT[key].push_back(i);
                }
            }
            chunkCtx.chunkIndexDirty = false;
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start
            ).count();
            std::cout << "ChunkSystem: rebuilt chunk index for "
                      << baseSystem.level->worlds.size() << " worlds, "
                      << totalInstances << " instances (" << chunkableInstances
                      << " chunkable) in " << elapsedMs << " ms." << std::endl;
        }

        std::unordered_set<ChunkKey, ChunkKeyHash> desiredChunks;
        desiredChunks.reserve(baseSystem.level->worlds.size() * 1024);

        size_t rebuiltChunks = 0;
        for (int worldIndex = 0; worldIndex < static_cast<int>(baseSystem.level->worlds.size()); ++worldIndex) {
            if (worldIndex < static_cast<int>(chunkCtx.worldHasChunkable.size()) &&
                !chunkCtx.worldHasChunkable[worldIndex]) {
                continue;
            }
            glm::ivec3 cameraChunk = chunkIndexFromPosition(cameraPos, chunkCtx.chunkSize);
            for (int dx = -renderDistChunks; dx <= renderDistChunks; ++dx) {
                for (int dy = -renderDistChunks; dy <= renderDistChunks; ++dy) {
                    for (int dz = -renderDistChunks; dz <= renderDistChunks; ++dz) {
                        glm::ivec3 idx = cameraChunk + glm::ivec3(dx, dy, dz);
                        ChunkKey key{worldIndex, idx};
                        float dist = chunkCenterDistance(key, cameraPos, chunkCtx.chunkSize);
                        if (dist > renderRadius) continue;
                        if (chunkCtx.chunkInstanceLUT.find(key) == chunkCtx.chunkInstanceLUT.end() &&
                            chunkCtx.chunks.find(key) == chunkCtx.chunks.end()) {
                            continue; // No instances here and nothing loaded; skip.
                        }
                        desiredChunks.insert(key);
                        bool needsBuild = chunkCtx.chunks.find(key) == chunkCtx.chunks.end() || chunkCtx.dirtyChunks.count(key) > 0;
                        if (needsBuild) {
                            RebuildChunk(baseSystem, prototypes, key);
                            ++rebuiltChunks;
                        }
                    }
                }
            }
        }

        chunkCtx.dirtyChunks.clear();
        if (rebuiltChunks > 0) {
            std::cout << "ChunkSystem: rebuilt " << rebuiltChunks
                      << " chunk(s) this frame." << std::endl;
        }

        std::vector<ChunkKey> toRemove;
        toRemove.reserve(chunkCtx.chunks.size());
        for (const auto& [key, _] : chunkCtx.chunks) {
            // Cull chunks too far or belonging to worlds that no longer exist, or not desired this frame.
            if (key.worldIndex < 0 || key.worldIndex >= static_cast<int>(baseSystem.level->worlds.size())) {
                toRemove.push_back(key);
                continue;
            }
            if (desiredChunks.count(key) == 0) {
                toRemove.push_back(key);
            }
        }

        for (const auto& key : toRemove) {
            chunkCtx.chunks.erase(key);
            chunkCtx.dirtyChunks.erase(key);
            if (baseSystem.face) {
                baseSystem.face->faces.erase(key);
                baseSystem.face->dirtyChunks.erase(key);
            }
            // Leave render buffers; RenderSystem will clean up stale buffers.
        }

    }
}
