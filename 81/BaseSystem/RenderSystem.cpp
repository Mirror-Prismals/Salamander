#pragma once
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>

// Forward declare the function from the new system.
namespace SkyboxSystemLogic {
    void getCurrentSkyColors(float dayFraction, const std::vector<SkyColorKey>& skyKeys, glm::vec3& top, glm::vec3& bottom);
}

namespace RenderSystemLogic {

    namespace {
        RenderBehavior BehaviorForPrototype(const Entity& proto) {
            if (proto.name == "Branch") return RenderBehavior::STATIC_BRANCH;
            if (proto.name == "Water") return RenderBehavior::ANIMATED_WATER;
            if (proto.name == "TransparentWave") return RenderBehavior::ANIMATED_TRANSPARENT_WAVE;
            if (proto.hasWireframe && proto.isAnimated) return RenderBehavior::ANIMATED_WIREFRAME;
            return RenderBehavior::STATIC_DEFAULT;
        }

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

        void destroyVoxelGreedyRenderBuffers(VoxelGreedyRenderBuffers& buffers) {
            for (size_t i = 0; i < buffers.opaqueVaos.size(); ++i) {
                if (buffers.opaqueVaos[i]) {
                    glDeleteVertexArrays(1, &buffers.opaqueVaos[i]);
                    buffers.opaqueVaos[i] = 0;
                }
                if (buffers.opaqueVBOs[i]) {
                    glDeleteBuffers(1, &buffers.opaqueVBOs[i]);
                    buffers.opaqueVBOs[i] = 0;
                }
                buffers.opaqueCounts[i] = 0;
                if (buffers.alphaVaos[i]) {
                    glDeleteVertexArrays(1, &buffers.alphaVaos[i]);
                    buffers.alphaVaos[i] = 0;
                }
                if (buffers.alphaVBOs[i]) {
                    glDeleteBuffers(1, &buffers.alphaVBOs[i]);
                    buffers.alphaVBOs[i] = 0;
                }
                buffers.alphaCounts[i] = 0;
            }
        }

        void BuildVoxelGreedyRenderBuffers(const RendererContext& renderer,
                                           const GreedyChunkData& chunk,
                                           VoxelGreedyRenderBuffers& buffers) {
            std::array<std::vector<FaceInstanceRenderData>, 6> opaqueInstances;
            std::array<std::vector<FaceInstanceRenderData>, 6> alphaInstances;
            for (size_t i = 0; i < chunk.positions.size(); ++i) {
                int faceType = (i < chunk.faceTypes.size()) ? chunk.faceTypes[i] : -1;
                if (faceType < 0 || faceType >= 6) continue;
                glm::vec3 color = (i < chunk.colors.size()) ? chunk.colors[i] : glm::vec3(1.0f);
                int tileIndex = (i < chunk.tileIndices.size()) ? chunk.tileIndices[i] : -1;
                float alpha = (i < chunk.alphas.size()) ? chunk.alphas[i] : 1.0f;
                glm::vec4 ao = (i < chunk.ao.size()) ? chunk.ao[i] : glm::vec4(1.0f);
                glm::vec2 scale = (i < chunk.scales.size()) ? chunk.scales[i] : glm::vec2(1.0f);
                glm::vec2 uvScale = (i < chunk.uvScales.size()) ? chunk.uvScales[i] : scale;
                FaceInstanceRenderData inst{chunk.positions[i], color, tileIndex, alpha, ao, scale, uvScale};
                if (alpha < 0.999f) {
                    alphaInstances[faceType].push_back(inst);
                } else {
                    opaqueInstances[faceType].push_back(inst);
                }
            }

            for (int faceType = 0; faceType < 6; ++faceType) {
                const auto& opaque = opaqueInstances[faceType];
                buffers.opaqueCounts[faceType] = static_cast<int>(opaque.size());
                if (buffers.opaqueCounts[faceType] > 0) {
                    if (buffers.opaqueVaos[faceType] == 0) glGenVertexArrays(1, &buffers.opaqueVaos[faceType]);
                    if (buffers.opaqueVBOs[faceType] == 0) glGenBuffers(1, &buffers.opaqueVBOs[faceType]);

                    glBindVertexArray(buffers.opaqueVaos[faceType]);
                    glBindBuffer(GL_ARRAY_BUFFER, renderer.faceVBO);
                    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
                    glEnableVertexAttribArray(1);
                    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
                    glEnableVertexAttribArray(2);

                    glBindBuffer(GL_ARRAY_BUFFER, buffers.opaqueVBOs[faceType]);
                    glBufferData(GL_ARRAY_BUFFER, opaque.size() * sizeof(FaceInstanceRenderData), opaque.data(), GL_STATIC_DRAW);
                    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, position));
                    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, color));
                    glEnableVertexAttribArray(5); glVertexAttribIPointer(5, 1, GL_INT, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, tileIndex));
                    glEnableVertexAttribArray(6); glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, alpha));
                    glEnableVertexAttribArray(7); glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, ao));
                    glEnableVertexAttribArray(8); glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, scale));
                    glEnableVertexAttribArray(9); glVertexAttribPointer(9, 2, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, uvScale));
                    glVertexAttribDivisor(3, 1); glVertexAttribDivisor(4, 1); glVertexAttribDivisor(5, 1);
                    glVertexAttribDivisor(6, 1); glVertexAttribDivisor(7, 1); glVertexAttribDivisor(8, 1); glVertexAttribDivisor(9, 1);
                } else {
                    buffers.opaqueCounts[faceType] = 0;
                }

                const auto& alpha = alphaInstances[faceType];
                buffers.alphaCounts[faceType] = static_cast<int>(alpha.size());
                if (buffers.alphaCounts[faceType] > 0) {
                    if (buffers.alphaVaos[faceType] == 0) glGenVertexArrays(1, &buffers.alphaVaos[faceType]);
                    if (buffers.alphaVBOs[faceType] == 0) glGenBuffers(1, &buffers.alphaVBOs[faceType]);

                    glBindVertexArray(buffers.alphaVaos[faceType]);
                    glBindBuffer(GL_ARRAY_BUFFER, renderer.faceVBO);
                    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
                    glEnableVertexAttribArray(1);
                    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
                    glEnableVertexAttribArray(2);

                    glBindBuffer(GL_ARRAY_BUFFER, buffers.alphaVBOs[faceType]);
                    glBufferData(GL_ARRAY_BUFFER, alpha.size() * sizeof(FaceInstanceRenderData), alpha.data(), GL_STATIC_DRAW);
                    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, position));
                    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, color));
                    glEnableVertexAttribArray(5); glVertexAttribIPointer(5, 1, GL_INT, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, tileIndex));
                    glEnableVertexAttribArray(6); glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, alpha));
                    glEnableVertexAttribArray(7); glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, ao));
                    glEnableVertexAttribArray(8); glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, scale));
                    glEnableVertexAttribArray(9); glVertexAttribPointer(9, 2, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, uvScale));
                    glVertexAttribDivisor(3, 1); glVertexAttribDivisor(4, 1); glVertexAttribDivisor(5, 1);
                    glVertexAttribDivisor(6, 1); glVertexAttribDivisor(7, 1); glVertexAttribDivisor(8, 1); glVertexAttribDivisor(9, 1);
                } else {
                    buffers.alphaCounts[faceType] = 0;
                }
            }
            glBindVertexArray(0);
        }

        int FaceTileIndexFor(const WorldContext* worldCtx, const Entity& proto, int faceType) {
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

        void DestroyChunkRenderBuffers(ChunkRenderBuffers& buffers) {
            for (GLuint vao : buffers.vaos) {
                if (vao) glDeleteVertexArrays(1, &vao);
            }
            for (GLuint vbo : buffers.instanceVBOs) {
                if (vbo) glDeleteBuffers(1, &vbo);
            }
            buffers.vaos.fill(0);
            buffers.instanceVBOs.fill(0);
            buffers.counts.fill(0);
            buffers.builtWithFaceCulling = false;
        }

        glm::vec3 unpackColor(uint32_t packed) {
            float r = static_cast<float>((packed >> 16) & 0xff) / 255.0f;
            float g = static_cast<float>((packed >> 8) & 0xff) / 255.0f;
            float b = static_cast<float>(packed & 0xff) / 255.0f;
            return glm::vec3(r, g, b);
        }

        int floorDivInt(int value, int divisor) {
            if (divisor <= 0) return 0;
            if (value >= 0) return value / divisor;
            return -(((-value) + divisor - 1) / divisor);
        }

        int sectionSizeForLod(const VoxelWorldContext& voxelWorld, int lod) {
            int size = voxelWorld.sectionSize >> lod;
            return size > 0 ? size : 1;
        }

        int getRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback);
        bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback);

        uint32_t getVoxelIdAtLod(const VoxelWorldContext& voxelWorld,
                                 int lod,
                                 const glm::ivec3& coord) {
            int size = sectionSizeForLod(voxelWorld, lod);
            glm::ivec3 sectionCoord(
                floorDivInt(coord.x, size),
                floorDivInt(coord.y, size),
                floorDivInt(coord.z, size)
            );
            glm::ivec3 local = coord - sectionCoord * size;
            VoxelSectionKey key{lod, sectionCoord};
            auto it = voxelWorld.sections.find(key);
            if (it == voxelWorld.sections.end()) return 0;
            const VoxelSection& section = it->second;
            int idx = local.x + local.y * section.size + local.z * section.size * section.size;
            if (idx < 0 || idx >= static_cast<int>(section.ids.size())) return 0;
            return section.ids[idx];
        }

        uint32_t getVoxelColorAtLod(const VoxelWorldContext& voxelWorld,
                                    int lod,
                                    const glm::ivec3& coord) {
            int size = sectionSizeForLod(voxelWorld, lod);
            glm::ivec3 sectionCoord(
                floorDivInt(coord.x, size),
                floorDivInt(coord.y, size),
                floorDivInt(coord.z, size)
            );
            glm::ivec3 local = coord - sectionCoord * size;
            VoxelSectionKey key{lod, sectionCoord};
            auto it = voxelWorld.sections.find(key);
            if (it == voxelWorld.sections.end()) return 0;
            const VoxelSection& section = it->second;
            int idx = local.x + local.y * section.size + local.z * section.size * section.size;
            if (idx < 0 || idx >= static_cast<int>(section.colors.size())) return 0;
            return section.colors[idx];
        }

        glm::ivec3 localCellFromUV(int faceType, int slice, int u, int v) {
            switch (faceType) {
                case 0:
                case 1:
                    return glm::ivec3(slice, v, u);
                case 2:
                case 3:
                    return glm::ivec3(u, slice, v);
                case 4:
                case 5:
                    return glm::ivec3(u, v, slice);
                default:
                    return glm::ivec3(0);
            }
        }

        glm::ivec3 faceNormal(int faceType) {
            switch (faceType) {
                case 0: return glm::ivec3(1, 0, 0);
                case 1: return glm::ivec3(-1, 0, 0);
                case 2: return glm::ivec3(0, 1, 0);
                case 3: return glm::ivec3(0, -1, 0);
                case 4: return glm::ivec3(0, 0, 1);
                case 5: return glm::ivec3(0, 0, -1);
                default: return glm::ivec3(0, 0, 1);
            }
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
            int superChunkMinLod = getRegistryInt(baseSystem, "voxelSuperChunkMinLod", 3);
            int superChunkMaxLod = getRegistryInt(baseSystem, "voxelSuperChunkMaxLod", 3);
            int superChunkSize = getRegistryInt(baseSystem, "voxelSuperChunkSize", 1);
            if (superChunkSize < 1) superChunkSize = 1;
            bool useSuperChunk = section.lod >= superChunkMinLod
                && section.lod <= superChunkMaxLod
                && superChunkSize > 1;
            glm::ivec3 anchorCoord = section.coord;
            if (useSuperChunk) {
                anchorCoord.x = floorDivInt(section.coord.x, superChunkSize) * superChunkSize;
                anchorCoord.z = floorDivInt(section.coord.z, superChunkSize) * superChunkSize;
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
                        uint32_t id = getVoxelIdAtLod(voxelWorld, section.lod, lodCoord);
                        if (id == 0 || id >= prototypes.size()) continue;
                        const Entity& proto = prototypes[id];
                        int idx = cellIndex(glm::ivec3(x, y, z));
                        uint32_t packedColor = getVoxelColorAtLod(voxelWorld, section.lod, lodCoord);
                        glm::vec3 color = unpackColor(packedColor);
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
                uint32_t id = getVoxelIdAtLod(voxelWorld, section.lod, lodCoord);
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
                                glm::ivec3 local = localCellFromUV(faceType, slice, u, v);
                                if (!inBounds(local)) continue;
                                const CellInfo& cell = cellData[cellIndex(local)];
                                if (!cell.opaque) continue;
                                glm::ivec3 lodCoord = minCoord + local;
                                glm::ivec3 neighborCoord = lodCoord + faceNormal(faceType);
                                int neighborType = neighborTypeAt(neighborCoord);
                                if (isWaterPass) {
                                    if (neighborType == 1 || neighborType == 2) continue;
                                } else {
                                    if (neighborType == 1) continue;
                                }
                                if (cell.protoID < 0 || cell.protoID >= static_cast<int>(prototypes.size())) continue;
                                const Entity& proto = prototypes[cell.protoID];
                                int tileIndex = FaceTileIndexFor(baseSystem.world.get(), proto, faceType);
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
                        glm::vec3 color = unpackColor(packedColor);
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
                                glm::ivec3 local = localCellFromUV(faceType, slice, u, v);
                                if (local.x < 0 || local.y < 0 || local.z < 0
                                    || local.x >= sizeX || local.y >= sizeY || local.z >= sizeZ) continue;
                                const CellInfo& cell = cellData[cellIndex(local)];
                                if (!cell.opaque) continue;
                                glm::ivec3 lodCoord = minCoord + local;
                                glm::ivec3 neighborCoord = lodCoord + faceNormal(faceType);
                                int neighborType = neighborTypeAt(neighborCoord);
                                if (isWaterPass) {
                                    if (neighborType == 1 || neighborType == 2) continue;
                                } else {
                                    if (neighborType == 1) continue;
                                }
                                if (cell.protoID < 0 || cell.protoID >= static_cast<int>(prototypes.size())) continue;
                                const Entity& proto = prototypes[cell.protoID];
                                int tileIndex = FaceTileIndexFor(snap.worldCtx, proto, faceType);
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

        void stopGreedyAsync() {
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

        bool enqueueGreedySnapshot(const VoxelWorldContext& voxelWorld,
                                   const WorldContext* worldCtx,
                                   const VoxelSectionKey& renderKey,
                                   int superChunkMinLod,
                                   int superChunkMaxLod,
                                   int superChunkSize,
                                   VoxelGreedySnapshot& out) {
            int lod = renderKey.lod;
            int size = sectionSizeForLod(voxelWorld, lod);
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

        bool shouldRenderVoxelSection(const BaseSystem& baseSystem,
                                      const VoxelSection& section,
                                      const glm::vec3& cameraPos) {
            int radius = getRegistryInt(baseSystem, "voxelLod" + std::to_string(section.lod) + "Radius", 0);
            if (radius <= 0) return false;
            int prevRadius = (section.lod > 0)
                ? getRegistryInt(baseSystem, "voxelLod" + std::to_string(section.lod - 1) + "Radius", 0)
                : 0;
            int scale = 1 << section.lod;
            glm::vec2 minB(section.coord.x * section.size * scale,
                           section.coord.z * section.size * scale);
            glm::vec2 maxB = minB + glm::vec2(section.size * scale);
            glm::vec2 camXZ(cameraPos.x, cameraPos.z);
            float dx = 0.0f;
            if (camXZ.x < minB.x) dx = minB.x - camXZ.x;
            else if (camXZ.x > maxB.x) dx = camXZ.x - maxB.x;
            float dz = 0.0f;
            if (camXZ.y < minB.y) dz = minB.y - camXZ.y;
            else if (camXZ.y > maxB.y) dz = camXZ.y - maxB.y;
            float dist = std::sqrt(dx * dx + dz * dz);
            if (dist > static_cast<float>(radius)) return false;
            if (prevRadius > 0) {
                float dxMax = std::max(std::abs(camXZ.x - minB.x), std::abs(camXZ.x - maxB.x));
                float dzMax = std::max(std::abs(camXZ.y - minB.y), std::abs(camXZ.y - maxB.y));
                float maxDist = std::sqrt(dxMax * dxMax + dzMax * dzMax);
                if (maxDist <= static_cast<float>(prevRadius)) return false;
            }
            return true;
        }

        bool shouldRenderVoxelSectionSized(const BaseSystem& baseSystem,
                                           int lod,
                                           const glm::ivec3& sectionCoord,
                                           int sectionSize,
                                           int sizeMultiplier,
                                           const glm::vec3& cameraPos) {
            int radius = getRegistryInt(baseSystem, "voxelLod" + std::to_string(lod) + "Radius", 0);
            if (radius <= 0) return false;
            int prevRadius = (lod > 0)
                ? getRegistryInt(baseSystem, "voxelLod" + std::to_string(lod - 1) + "Radius", 0)
                : 0;
            int scale = 1 << lod;
            int size = sectionSize * sizeMultiplier * scale;
            glm::vec2 minB(sectionCoord.x * sectionSize * scale,
                           sectionCoord.z * sectionSize * scale);
            glm::vec2 maxB = minB + glm::vec2(size);
            glm::vec2 camXZ(cameraPos.x, cameraPos.z);
            float dx = 0.0f;
            if (camXZ.x < minB.x) dx = minB.x - camXZ.x;
            else if (camXZ.x > maxB.x) dx = camXZ.x - maxB.x;
            float dz = 0.0f;
            if (camXZ.y < minB.y) dz = minB.y - camXZ.y;
            else if (camXZ.y > maxB.y) dz = camXZ.y - maxB.y;
            float dist = std::sqrt(dx * dx + dz * dz);
            if (dist > static_cast<float>(radius)) return false;
            if (prevRadius > 0) {
                float dxMax = std::max(std::abs(camXZ.x - minB.x), std::abs(camXZ.x - maxB.x));
                float dzMax = std::max(std::abs(camXZ.y - minB.y), std::abs(camXZ.y - maxB.y));
                float maxDist = std::sqrt(dxMax * dxMax + dzMax * dzMax);
                if (maxDist <= static_cast<float>(prevRadius)) return false;
            }
            return true;
        }

        void BuildVoxelRenderBuffers(BaseSystem& baseSystem,
                                     std::vector<Entity>& prototypes,
                                     const VoxelSectionKey& sectionKey,
                                     bool faceCullingInitialized) {
            if (!baseSystem.voxelWorld || !baseSystem.voxelRender || !baseSystem.renderer) return;
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            VoxelRenderContext& voxelRender = *baseSystem.voxelRender;
            RendererContext& renderer = *baseSystem.renderer;
            auto secIt = voxelWorld.sections.find(sectionKey);
            if (secIt == voxelWorld.sections.end()) return;
            const VoxelSection& section = secIt->second;
            if (section.nonAirCount <= 0) return;
            ChunkRenderBuffers& buffers = voxelRender.renderBuffers[sectionKey];
            const int behaviorCount = static_cast<int>(RenderBehavior::COUNT);
            std::array<std::vector<InstanceData>, static_cast<int>(RenderBehavior::COUNT)> behaviorData;
            std::vector<BranchInstanceData> branchData;
            buffers.counts.fill(0);

            int scale = 1 << section.lod;
            glm::ivec3 base = section.coord * section.size;
            for (int z = 0; z < section.size; ++z) {
                for (int y = 0; y < section.size; ++y) {
                    for (int x = 0; x < section.size; ++x) {
                        int idx = x + y * section.size + z * section.size * section.size;
                        if (idx < 0 || idx >= static_cast<int>(section.ids.size())) continue;
                        uint32_t id = section.ids[idx];
                        if (id == 0 || id >= prototypes.size()) continue;
                        const Entity& proto = prototypes[id];
                        if (!proto.isRenderable || !proto.isBlock) continue;
                        RenderBehavior behavior = BehaviorForPrototype(proto);
                        glm::vec3 color = unpackColor(section.colors[idx]);
                        glm::vec3 position = glm::vec3((base + glm::ivec3(x, y, z)) * scale);
                        if (behavior == RenderBehavior::STATIC_BRANCH) {
                            BranchInstanceData inst;
                            inst.position = position;
                            inst.rotation = 0.0f;
                            inst.color = color;
                            branchData.push_back(inst);
                        } else {
                            InstanceData inst;
                            inst.position = position;
                            inst.color = color;
                            behaviorData[static_cast<int>(behavior)].push_back(inst);
                        }
                    }
                }
            }

            for (int i = 0; i < behaviorCount; ++i) {
                RenderBehavior behavior = static_cast<RenderBehavior>(i);
                bool isBranch = behavior == RenderBehavior::STATIC_BRANCH;
                int count = isBranch ? static_cast<int>(branchData.size()) : static_cast<int>(behaviorData[i].size());
                buffers.counts[i] = count;
                if (count == 0) continue;

                if (buffers.vaos[i] == 0) glGenVertexArrays(1, &buffers.vaos[i]);
                if (buffers.instanceVBOs[i] == 0) glGenBuffers(1, &buffers.instanceVBOs[i]);

                glBindVertexArray(buffers.vaos[i]);
                glBindBuffer(GL_ARRAY_BUFFER, renderer.cubeVBO);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);

                glBindBuffer(GL_ARRAY_BUFFER, buffers.instanceVBOs[i]);
                if (isBranch) {
                    glBufferData(GL_ARRAY_BUFFER, branchData.size() * sizeof(BranchInstanceData), branchData.data(), GL_STATIC_DRAW);
                    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, position));
                    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, rotation));
                    glEnableVertexAttribArray(5); glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, color));
                    glVertexAttribDivisor(3, 1); glVertexAttribDivisor(4, 1); glVertexAttribDivisor(5, 1);
                } else {
                    glBufferData(GL_ARRAY_BUFFER, behaviorData[i].size() * sizeof(InstanceData), behaviorData[i].data(), GL_STATIC_DRAW);
                    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, position));
                    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, color));
                    glVertexAttribDivisor(3, 1); glVertexAttribDivisor(4, 1);
                }
            }

            glBindVertexArray(0);
            buffers.builtWithFaceCulling = faceCullingInitialized;
        }
    }
    
    void InitializeRenderer(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.renderer || !baseSystem.world) { std::cerr << "ERROR: RenderSystem cannot init without RendererContext or WorldContext." << std::endl; return; }
        WorldContext& world = *baseSystem.world;
        RendererContext& renderer = *baseSystem.renderer;
        renderer.blockShader = std::make_unique<Shader>(world.shaders["BLOCK_VERTEX_SHADER"].c_str(), world.shaders["BLOCK_FRAGMENT_SHADER"].c_str());
        renderer.faceShader = std::make_unique<Shader>(world.shaders["FACE_VERTEX_SHADER"].c_str(), world.shaders["FACE_FRAGMENT_SHADER"].c_str());
        renderer.skyboxShader = std::make_unique<Shader>(world.shaders["SKYBOX_VERTEX_SHADER"].c_str(), world.shaders["SKYBOX_FRAGMENT_SHADER"].c_str());
        renderer.sunMoonShader = std::make_unique<Shader>(world.shaders["SUNMOON_VERTEX_SHADER"].c_str(), world.shaders["SUNMOON_FRAGMENT_SHADER"].c_str());
        renderer.starShader = std::make_unique<Shader>(world.shaders["STAR_VERTEX_SHADER"].c_str(), world.shaders["STAR_FRAGMENT_SHADER"].c_str());
        renderer.godrayRadialShader = std::make_unique<Shader>(world.shaders["GODRAY_VERTEX_SHADER"].c_str(), world.shaders["GODRAY_RADIAL_FRAGMENT_SHADER"].c_str());
        renderer.godrayCompositeShader = std::make_unique<Shader>(world.shaders["GODRAY_VERTEX_SHADER"].c_str(), world.shaders["GODRAY_COMPOSITE_FRAGMENT_SHADER"].c_str());
        int behaviorCount = static_cast<int>(RenderBehavior::COUNT);
        renderer.behaviorVAOs.resize(behaviorCount);
        renderer.behaviorInstanceVBOs.resize(behaviorCount);
        glGenVertexArrays(behaviorCount, renderer.behaviorVAOs.data());
        glGenBuffers(behaviorCount, renderer.behaviorInstanceVBOs.data());
        glGenBuffers(1, &renderer.cubeVBO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, world.cubeVertices.size() * sizeof(float), world.cubeVertices.data(), GL_STATIC_DRAW);
        for (int i = 0; i < behaviorCount; ++i) {
            glBindVertexArray(renderer.behaviorVAOs[i]);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.cubeVBO);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.behaviorInstanceVBOs[i]);
            if (static_cast<RenderBehavior>(i) == RenderBehavior::STATIC_BRANCH) {
                glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, position));
                glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, rotation));
                glEnableVertexAttribArray(5); glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, color));
                glVertexAttribDivisor(3, 1); glVertexAttribDivisor(4, 1); glVertexAttribDivisor(5, 1);
            } else {
                glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, position));
                glEnableVertexAttribArray(4); glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, color));
                glVertexAttribDivisor(3, 1); glVertexAttribDivisor(4, 1);
            }
        }
        float skyboxQuadVertices[]={-1,1,-1,-1,1,-1,-1,1,1,-1,1,1};
        glGenVertexArrays(1,&renderer.skyboxVAO);glGenBuffers(1,&renderer.skyboxVBO);
        glBindVertexArray(renderer.skyboxVAO);glBindBuffer(GL_ARRAY_BUFFER,renderer.skyboxVBO);
        glBufferData(GL_ARRAY_BUFFER,sizeof(skyboxQuadVertices),skyboxQuadVertices,GL_STATIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
        glGenVertexArrays(1,&renderer.sunMoonVAO);glGenBuffers(1,&renderer.sunMoonVBO);
        glBindVertexArray(renderer.sunMoonVAO);glBindBuffer(GL_ARRAY_BUFFER,renderer.sunMoonVBO);
        glBufferData(GL_ARRAY_BUFFER,sizeof(skyboxQuadVertices),skyboxQuadVertices,GL_STATIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
        glGenVertexArrays(1,&renderer.starVAO);glGenBuffers(1,&renderer.starVBO);

        float faceVerts[] = {
            -0.5f, -0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
             0.5f, -0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f,
             0.5f,  0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
            -0.5f, -0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
             0.5f,  0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
            -0.5f,  0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f
        };
        glGenVertexArrays(1, &renderer.faceVAO);
        glGenBuffers(1, &renderer.faceVBO);
        glGenBuffers(1, &renderer.faceInstanceVBO);
        glBindVertexArray(renderer.faceVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.faceVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(faceVerts), faceVerts, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.faceInstanceVBO);
        glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, position));
        glEnableVertexAttribArray(4); glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, color));
        glEnableVertexAttribArray(5); glVertexAttribIPointer(5, 1, GL_INT, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, tileIndex));
        glVertexAttribDivisor(3, 1); glVertexAttribDivisor(4, 1); glVertexAttribDivisor(5, 1);
        glEnableVertexAttribArray(6); glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, alpha));
        glVertexAttribDivisor(6, 1);
        glEnableVertexAttribArray(7); glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, ao));
        glVertexAttribDivisor(7, 1);
        glEnableVertexAttribArray(8); glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, scale));
        glVertexAttribDivisor(8, 1);
        glEnableVertexAttribArray(9); glVertexAttribPointer(9, 2, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, uvScale));
        glVertexAttribDivisor(9, 1);

        // Godray quad
        float quadVerts[] = { -1,-1,  1,-1,  -1,1,  -1,1,  1,-1,  1,1 };
        glGenVertexArrays(1, &renderer.godrayQuadVAO);
        glGenBuffers(1, &renderer.godrayQuadVBO);
        glBindVertexArray(renderer.godrayQuadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.godrayQuadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        // Godray FBOs
        renderer.godrayWidth = baseSystem.app ? baseSystem.app->windowWidth / renderer.godrayDownsample : 960;
        renderer.godrayHeight = baseSystem.app ? baseSystem.app->windowHeight / renderer.godrayDownsample : 540;

        auto setupFBO = [](GLuint& fbo, GLuint& tex, int w, int h){
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        };

        setupFBO(renderer.godrayOcclusionFBO, renderer.godrayOcclusionTex, renderer.godrayWidth, renderer.godrayHeight);
        setupFBO(renderer.godrayBlurFBO, renderer.godrayBlurTex, renderer.godrayWidth, renderer.godrayHeight);

        renderer.selectionShader = std::make_unique<Shader>(world.shaders["SELECTION_VERTEX_SHADER"].c_str(), world.shaders["SELECTION_FRAGMENT_SHADER"].c_str());
        std::vector<float> selectionVertices;
        selectionVertices.reserve((12 + 12) * 2 * 6);
        auto pushVertex = [&](const glm::vec3& pos, const glm::vec3& normal){
            selectionVertices.push_back(pos.x);
            selectionVertices.push_back(pos.y);
            selectionVertices.push_back(pos.z);
            selectionVertices.push_back(normal.x);
            selectionVertices.push_back(normal.y);
            selectionVertices.push_back(normal.z);
        };
        auto addLine = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& normal){
            pushVertex(a, normal);
            pushVertex(b, normal);
        };
        auto addFace = [&](const glm::vec3& normal, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d){
            addLine(a, c, normal);
            addLine(b, d, normal);
            addLine(a, b, normal);
            addLine(b, c, normal);
            addLine(c, d, normal);
            addLine(d, a, normal);
        };
        addFace(glm::vec3(0,0,1),
                glm::vec3(-0.5f,-0.5f,0.5f),
                glm::vec3(0.5f,-0.5f,0.5f),
                glm::vec3(0.5f,0.5f,0.5f),
                glm::vec3(-0.5f,0.5f,0.5f));
        addFace(glm::vec3(0,0,-1),
                glm::vec3(-0.5f,-0.5f,-0.5f),
                glm::vec3(0.5f,-0.5f,-0.5f),
                glm::vec3(0.5f,0.5f,-0.5f),
                glm::vec3(-0.5f,0.5f,-0.5f));
        addFace(glm::vec3(1,0,0),
                glm::vec3(0.5f,-0.5f,-0.5f),
                glm::vec3(0.5f,-0.5f,0.5f),
                glm::vec3(0.5f,0.5f,0.5f),
                glm::vec3(0.5f,0.5f,-0.5f));
        addFace(glm::vec3(-1,0,0),
                glm::vec3(-0.5f,-0.5f,-0.5f),
                glm::vec3(-0.5f,-0.5f,0.5f),
                glm::vec3(-0.5f,0.5f,0.5f),
                glm::vec3(-0.5f,0.5f,-0.5f));
        addFace(glm::vec3(0,1,0),
                glm::vec3(-0.5f,0.5f,-0.5f),
                glm::vec3(0.5f,0.5f,-0.5f),
                glm::vec3(0.5f,0.5f,0.5f),
                glm::vec3(-0.5f,0.5f,0.5f));
        addFace(glm::vec3(0,-1,0),
                glm::vec3(-0.5f,-0.5f,-0.5f),
                glm::vec3(0.5f,-0.5f,-0.5f),
                glm::vec3(0.5f,-0.5f,0.5f),
                glm::vec3(-0.5f,-0.5f,0.5f));

        renderer.selectionVertexCount = static_cast<int>(selectionVertices.size() / 6);
        glGenVertexArrays(1, &renderer.selectionVAO);
        glGenBuffers(1, &renderer.selectionVBO);
        glBindVertexArray(renderer.selectionVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.selectionVBO);
        glBufferData(GL_ARRAY_BUFFER, selectionVertices.size() * sizeof(float), selectionVertices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        renderer.hudShader = std::make_unique<Shader>(world.shaders["HUD_VERTEX_SHADER"].c_str(), world.shaders["HUD_FRAGMENT_SHADER"].c_str());
        renderer.crosshairShader = std::make_unique<Shader>(world.shaders["CROSSHAIR_VERTEX_SHADER"].c_str(), world.shaders["CROSSHAIR_FRAGMENT_SHADER"].c_str());
        float hudVertices[] = {
            0.035f, -0.05f, 0.0f, 0.0f,
            0.08f,  -0.05f, 1.0f, 0.0f,
            0.08f,   0.05f, 1.0f, 1.0f,
            0.035f, -0.05f, 0.0f, 0.0f,
            0.08f,   0.05f, 1.0f, 1.0f,
            0.035f,  0.05f, 0.0f, 1.0f
        };
        glGenVertexArrays(1, &renderer.hudVAO);
        glGenBuffers(1, &renderer.hudVBO);
        glBindVertexArray(renderer.hudVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.hudVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(hudVertices), hudVertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        float chLen = 0.02f;
        float chLenH = 0.016f;
        float crosshairVertices[] = {
            0.0f, -chLen, 1.0f, 1.0f, 1.0f,
            0.0f,  chLen, 1.0f, 1.0f, 1.0f,
            -chLenH, 0.0f, 1.0f, 1.0f, 1.0f,
             chLenH, 0.0f, 1.0f, 1.0f, 1.0f
        };
        glGenVertexArrays(1, &renderer.crosshairVAO);
        glGenBuffers(1, &renderer.crosshairVBO);
        glBindVertexArray(renderer.crosshairVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.crosshairVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(crosshairVertices), crosshairVertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        renderer.crosshairVertexCount = 4;
    }

    void RenderScene(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.player || !baseSystem.level) return;
        PlayerContext& player = *baseSystem.player;
        WorldContext& world = *baseSystem.world;
        RendererContext& renderer = *baseSystem.renderer;
        LevelContext& level = *baseSystem.level;

        float time = static_cast<float>(glfwGetTime());
        glm::mat4 view = player.viewMatrix;
        glm::mat4 projection = player.projectionMatrix;
        glm::vec3 playerPos = player.cameraPosition;
        glm::vec3 cameraForward;
        cameraForward.x = cos(glm::radians(player.cameraYaw)) * cos(glm::radians(player.cameraPitch));
        cameraForward.y = sin(glm::radians(player.cameraPitch));
        cameraForward.z = sin(glm::radians(player.cameraYaw)) * cos(glm::radians(player.cameraPitch));
        cameraForward = glm::normalize(cameraForward);
        
        time_t ct; std::time(&ct); tm lt;
        #ifdef _WIN32
        localtime_s(&lt, &ct);
        #else
        localtime_r(&ct, &lt);
        #endif
        float dayFraction = (lt.tm_hour*3600+lt.tm_min*60+lt.tm_sec)/86400.0f;
        std::vector<glm::vec3> starPositions;
        std::vector<std::vector<InstanceData>> behaviorInstances(static_cast<int>(RenderBehavior::COUNT));
        std::vector<BranchInstanceData> branchInstances;
        std::array<std::vector<FaceInstanceRenderData>, 6> faceInstances;

        size_t voxelRenderDirtyCount = 0;

        for (size_t worldIndex = 0; worldIndex < level.worlds.size(); ++worldIndex) {
            const auto& worldProto = level.worlds[worldIndex];
            for (const auto& instance : worldProto.instances) {
                if (instance.prototypeID < 0 || instance.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[instance.prototypeID];
                if (proto.isStar) {
                    starPositions.push_back(instance.position);
                }
                if (proto.name == "Face_PosX") { faceInstances[0].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.name == "Face_NegX") { faceInstances[1].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.name == "Face_PosY") { faceInstances[2].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.name == "Face_NegY") { faceInstances[3].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.name == "Face_PosZ") { faceInstances[4].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.name == "Face_NegZ") { faceInstances[5].push_back({instance.position, instance.color, -1, 1.0f, glm::vec4(1.0f), glm::vec2(1.0f), glm::vec2(1.0f)}); continue; }
                if (proto.isRenderable && proto.isBlock) {
                    RenderBehavior behavior = RenderBehavior::STATIC_DEFAULT;
                    if (proto.name == "Branch") behavior = RenderBehavior::STATIC_BRANCH;
                    else if (proto.name == "Water") behavior = RenderBehavior::ANIMATED_WATER;
                    else if (proto.name == "TransparentWave") behavior = RenderBehavior::ANIMATED_TRANSPARENT_WAVE;
                    else if (proto.hasWireframe && proto.isAnimated) behavior = RenderBehavior::ANIMATED_WIREFRAME;
                    if (behavior == RenderBehavior::STATIC_BRANCH) branchInstances.push_back({instance.position, instance.rotation, instance.color});
                    else behaviorInstances[static_cast<int>(behavior)].push_back({instance.position, instance.color});
                }
            }
        }

        int voxelGreedyMaxLod = getRegistryInt(baseSystem, "voxelGreedyMaxLod", 1);
        bool useVoxelGreedy = baseSystem.voxelWorld && baseSystem.voxelWorld->enabled && baseSystem.voxelGreedy
            && renderer.faceShader && renderer.faceVAO && voxelGreedyMaxLod >= 0;
        bool useVoxelRendering = baseSystem.voxelWorld && baseSystem.voxelWorld->enabled && baseSystem.voxelRender
            && (!useVoxelGreedy || (baseSystem.voxelWorld && voxelGreedyMaxLod < baseSystem.voxelWorld->maxLod));
        if (useVoxelRendering) {
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            VoxelRenderContext& voxelRender = *baseSystem.voxelRender;
            int voxelGreedyMaxLodLocal = getRegistryInt(baseSystem, "voxelGreedyMaxLod", 1);
            size_t voxelRenderDirtyCount = 0;

            std::vector<VoxelSectionKey> staleSections;
            for (const auto& [key, _] : voxelRender.renderBuffers) {
                auto it = voxelWorld.sections.find(key);
                if (it == voxelWorld.sections.end() || it->second.nonAirCount <= 0) {
                    staleSections.push_back(key);
                }
            }
            for (const auto& key : staleSections) {
                DestroyChunkRenderBuffers(voxelRender.renderBuffers[key]);
                voxelRender.renderBuffers.erase(key);
            }

            for (const auto& key : voxelWorld.dirtySections) {
                auto it = voxelWorld.sections.find(key);
                if (it == voxelWorld.sections.end()) continue;
                if (key.lod <= voxelGreedyMaxLodLocal) continue;
                if (!shouldRenderVoxelSection(baseSystem, it->second, playerPos)) continue;
                voxelRender.renderBuffersDirty.insert(key);
            }
            voxelRenderDirtyCount = voxelRender.renderBuffersDirty.size();

            if (!voxelRender.renderBuffersDirty.empty()) {
                auto start = std::chrono::steady_clock::now();
                size_t buildCount = 0;
                std::vector<VoxelSectionKey> builtKeys;
                builtKeys.reserve(voxelRender.renderBuffersDirty.size());
                for (const auto& key : voxelRender.renderBuffersDirty) {
                    BuildVoxelRenderBuffers(baseSystem, prototypes, key, false);
                    builtKeys.push_back(key);
                    ++buildCount;
                }
                voxelRender.renderBuffersDirty.clear();
                for (const auto& key : builtKeys) {
                    voxelWorld.dirtySections.erase(key);
                }
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start
                ).count();
                std::cout << "RenderSystem: rebuilt " << buildCount
                          << " voxel section buffer(s) in "
                          << elapsedMs << " ms." << std::endl;
            }
        }

        if (useVoxelGreedy) {
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            VoxelGreedyContext& voxelGreedy = *baseSystem.voxelGreedy;
            bool useVoxelGreedyAsync = getRegistryBool(baseSystem, "voxelGreedyAsync", true);
            size_t greedyQueuedCount = 0;
            size_t greedyAppliedCount = 0;
            size_t greedyDroppedCount = 0;

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
                stopGreedyAsync();
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
                auto bufIt = voxelGreedy.renderBuffers.find(key);
                if (bufIt != voxelGreedy.renderBuffers.end()) {
                    destroyVoxelGreedyRenderBuffers(bufIt->second);
                    voxelGreedy.renderBuffers.erase(bufIt);
                }
            }
            std::vector<VoxelSectionKey> staleBuffers;
            for (const auto& [key, _] : voxelGreedy.renderBuffers) {
                if (voxelGreedy.chunks.find(key) == voxelGreedy.chunks.end()) {
                    staleBuffers.push_back(key);
                }
            }
            for (const auto& key : staleBuffers) {
                auto bufIt = voxelGreedy.renderBuffers.find(key);
                if (bufIt != voxelGreedy.renderBuffers.end()) {
                    destroyVoxelGreedyRenderBuffers(bufIt->second);
                    voxelGreedy.renderBuffers.erase(bufIt);
                }
            }

            int superChunkMinLod = getRegistryInt(baseSystem, "voxelSuperChunkMinLod", 3);
            int superChunkMaxLod = getRegistryInt(baseSystem, "voxelSuperChunkMaxLod", 3);
            int superChunkSize = getRegistryInt(baseSystem, "voxelSuperChunkSize", 1);
            if (superChunkSize < 1) superChunkSize = 1;

            auto clearDirtyForKey = [&](const VoxelSectionKey& key) {
                bool useSuperChunk = key.lod >= superChunkMinLod
                    && key.lod <= superChunkMaxLod
                    && superChunkSize > 1;
                if (useSuperChunk) {
                    glm::ivec3 anchorCoord(
                        floorDivInt(key.coord.x, superChunkSize) * superChunkSize,
                        key.coord.y,
                        floorDivInt(key.coord.z, superChunkSize) * superChunkSize
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
                            greedyDroppedCount += 1;
                            continue;
                        }
                        if (secIt->second.editVersion != result.renderEditVersion) {
                            releaseGreedyChunk(voxelGreedy, std::move(result.mesh));
                            greedyDroppedCount += 1;
                            continue;
                        }
                    } else if (currentVersion != result.versionKey) {
                        releaseGreedyChunk(voxelGreedy, std::move(result.mesh));
                        greedyDroppedCount += 1;
                        continue;
                    }
                    if (result.empty) {
                        releaseGreedyChunkIfPresent(voxelGreedy, result.renderKey);
                        auto bufIt = voxelGreedy.renderBuffers.find(result.renderKey);
                        if (bufIt != voxelGreedy.renderBuffers.end()) {
                            destroyVoxelGreedyRenderBuffers(bufIt->second);
                            voxelGreedy.renderBuffers.erase(bufIt);
                        }
                    } else {
                        releaseGreedyChunkIfPresent(voxelGreedy, result.renderKey);
                        voxelGreedy.chunks[result.renderKey] = std::move(result.mesh);
                        voxelGreedy.renderBuffersDirty.insert(result.renderKey);
                    }
                    clearDirtyForKey(result.renderKey);
                    greedyAppliedCount += 1;
                }
            }

            for (const auto& key : voxelWorld.dirtySections) {
                if (key.lod > voxelGreedyMaxLod) continue;
                auto it = voxelWorld.sections.find(key);
                if (it == voxelWorld.sections.end()) continue;
                if (!shouldRenderVoxelSection(baseSystem, it->second, playerPos)) continue;
                voxelGreedy.dirtySections.insert(key);
            }

            if (!voxelGreedy.dirtySections.empty()) {
                auto start = std::chrono::steady_clock::now();
                size_t buildCount = 0;
                std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> toBuild;
                toBuild.reserve(voxelGreedy.dirtySections.size());
                for (const auto& key : voxelGreedy.dirtySections) {
                    int lod = key.lod;
                    auto secIt = voxelWorld.sections.find(key);
                    if (secIt != voxelWorld.sections.end()) lod = secIt->second.lod;
                    if (lod > voxelGreedyMaxLod) {
                        releaseGreedyChunkIfPresent(voxelGreedy, key);
                        auto bufIt = voxelGreedy.renderBuffers.find(key);
                        if (bufIt != voxelGreedy.renderBuffers.end()) {
                            destroyVoxelGreedyRenderBuffers(bufIt->second);
                            voxelGreedy.renderBuffers.erase(bufIt);
                        }
                        continue;
                    }
                    if (lod >= superChunkMinLod && lod <= superChunkMaxLod && superChunkSize > 1) {
                        glm::ivec3 anchorCoord(
                            floorDivInt(key.coord.x, superChunkSize) * superChunkSize,
                            key.coord.y,
                            floorDivInt(key.coord.z, superChunkSize) * superChunkSize
                        );
                        toBuild.insert({lod, anchorCoord});
                    } else {
                        toBuild.insert(key);
                    }
                }
                std::vector<VoxelSectionKey> buildList;
                buildList.reserve(toBuild.size());
                for (const auto& key : toBuild) {
                    buildList.push_back(key);
                }
                auto sectionCenter = [&](const VoxelSectionKey& key) {
                    int size = sectionSizeForLod(voxelWorld, key.lod);
                    int scale = 1 << key.lod;
                    glm::vec3 origin = glm::vec3(key.coord * size * scale);
                    return origin + glm::vec3(size * scale * 0.5f);
                };
                std::sort(buildList.begin(), buildList.end(),
                          [&](const VoxelSectionKey& a, const VoxelSectionKey& b) {
                              glm::vec3 ca = sectionCenter(a);
                              glm::vec3 cb = sectionCenter(b);
                              glm::vec3 daVec = ca - playerPos;
                              glm::vec3 dbVec = cb - playerPos;
                              float da = glm::dot(daVec, daVec);
                              float db = glm::dot(dbVec, dbVec);
                              return da < db;
                          });

                int greedyBudget = getRegistryInt(baseSystem, "voxelGreedyMeshesPerFrame", 0);
                int buildLimit = greedyBudget > 0
                    ? std::min(static_cast<int>(buildList.size()), greedyBudget)
                    : static_cast<int>(buildList.size());

                std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> retry;
                retry.reserve(toBuild.size());
                if (useVoxelGreedyAsync) {
                    for (int i = 0; i < buildLimit; ++i) {
                        const auto& key = buildList[i];
                        bool canEnqueue = false;
                        {
                            std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
                            if (g_voxelGreedyAsync.inFlight.count(key) == 0) {
                                g_voxelGreedyAsync.inFlight.insert(key);
                                canEnqueue = true;
                            }
                        }
                        if (!canEnqueue) {
                            retry.insert(key);
                            continue;
                        }
                        VoxelGreedySnapshot snap;
                        if (!enqueueGreedySnapshot(voxelWorld, baseSystem.world.get(), key,
                                                   superChunkMinLod, superChunkMaxLod, superChunkSize, snap)) {
                            {
                                std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
                                g_voxelGreedyAsync.inFlight.erase(key);
                            }
                            retry.insert(key);
                            continue;
                        }
                        {
                            std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
                            g_voxelGreedyAsync.queue.push_back(std::move(snap));
                        }
                        g_voxelGreedyAsync.cv.notify_one();
                        ++buildCount;
                    }
                } else {
                    for (int i = 0; i < buildLimit; ++i) {
                        const auto& key = buildList[i];
                        if (!BuildVoxelGreedyMesh(baseSystem, prototypes, key)) {
                            retry.insert(key);
                            continue;
                        }
                        clearDirtyForKey(key);
                        voxelGreedy.renderBuffersDirty.insert(key);
                        ++buildCount;
                    }
                }
                greedyQueuedCount = buildCount;
                for (size_t i = static_cast<size_t>(buildLimit); i < buildList.size(); ++i) {
                    retry.insert(buildList[i]);
                }
                voxelGreedy.dirtySections.clear();
                for (const auto& key : retry) {
                    voxelGreedy.dirtySections.insert(key);
                }
                logVoxelPerf(useVoxelGreedyAsync ? "enqueued voxel greedy mesh(es)" : "rebuilt voxel greedy mesh(es)", start, buildCount);
            }

            if (!voxelGreedy.renderBuffersDirty.empty()) {
                std::vector<VoxelSectionKey> toBuild;
                toBuild.reserve(voxelGreedy.renderBuffersDirty.size());
                for (const auto& key : voxelGreedy.renderBuffersDirty) {
                    toBuild.push_back(key);
                }
                for (const auto& key : toBuild) {
                    auto chunkIt = voxelGreedy.chunks.find(key);
                    if (chunkIt == voxelGreedy.chunks.end()) {
                        auto bufIt = voxelGreedy.renderBuffers.find(key);
                        if (bufIt != voxelGreedy.renderBuffers.end()) {
                            destroyVoxelGreedyRenderBuffers(bufIt->second);
                            voxelGreedy.renderBuffers.erase(bufIt);
                        }
                        voxelGreedy.renderBuffersDirty.erase(key);
                        continue;
                    }
                    VoxelGreedyRenderBuffers& buffers = voxelGreedy.renderBuffers[key];
                    BuildVoxelGreedyRenderBuffers(renderer, chunkIt->second, buffers);
                    voxelGreedy.renderBuffersDirty.erase(key);
                }
            }

            static auto lastPerfLog = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (now - lastPerfLog >= std::chrono::seconds(1)) {
                std::vector<size_t> lodSections(static_cast<size_t>(voxelWorld.maxLod + 1), 0);
                std::vector<size_t> lodMeshes(static_cast<size_t>(voxelWorld.maxLod + 1), 0);
                for (const auto& [key, _] : voxelWorld.sections) {
                    if (key.lod >= 0 && key.lod <= voxelWorld.maxLod) {
                        lodSections[static_cast<size_t>(key.lod)] += 1;
                    }
                }
                for (const auto& [key, _] : voxelGreedy.chunks) {
                    if (key.lod >= 0 && key.lod <= voxelWorld.maxLod) {
                        lodMeshes[static_cast<size_t>(key.lod)] += 1;
                    }
                }
                size_t inFlight = 0;
                size_t queued = 0;
                if (useVoxelGreedyAsync) {
                    std::lock_guard<std::mutex> lock(g_voxelGreedyAsync.mutex);
                    inFlight = g_voxelGreedyAsync.inFlight.size();
                    queued = g_voxelGreedyAsync.queue.size();
                }
                std::cout << "[VoxelPerf] dirty=" << voxelWorld.dirtySections.size()
                          << " greedyDirty=" << voxelGreedy.dirtySections.size()
                          << " greedyQueued=" << greedyQueuedCount
                          << " greedyApplied=" << greedyAppliedCount
                          << " greedyDropped=" << greedyDroppedCount
                          << " voxelRenderDirty=" << voxelRenderDirtyCount
                          << " sections=" << voxelWorld.sections.size()
                          << " greedyMeshes=" << voxelGreedy.chunks.size()
                          << " greedyInFlight=" << inFlight
                          << " greedyQueue=" << queued
                          << " lodSections=";
                for (size_t i = 0; i < lodSections.size(); ++i) {
                    std::cout << (i == 0 ? "" : ",") << lodSections[i];
                }
                std::cout << " lodMeshes=";
                for (size_t i = 0; i < lodMeshes.size(); ++i) {
                    std::cout << (i == 0 ? "" : ",") << lodMeshes[i];
                }
                std::cout << std::endl;
                lastPerfLog = now;
            }
        }
        
        glm::vec3 lightDir;
        SkyboxSystemLogic::RenderSkyAndCelestials(baseSystem, prototypes, starPositions, time, dayFraction, view, projection, playerPos, lightDir);
        bool auroraEnabled = true;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("AuroraSystem");
            if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second)) {
                auroraEnabled = std::get<bool>(it->second);
            }
        }
        if (auroraEnabled) {
            AuroraSystemLogic::RenderAuroras(baseSystem, time, view, projection);
        }
        CloudSystemLogic::RenderClouds(baseSystem, lightDir, time);

        renderer.blockShader->use();
        renderer.blockShader->setMat4("view", view);
        renderer.blockShader->setMat4("projection", projection);
        renderer.blockShader->setVec3("cameraPos", playerPos);
        renderer.blockShader->setFloat("time", time);
        renderer.blockShader->setFloat("instanceScale", 1.0f);
        renderer.blockShader->setVec3("lightDir",lightDir);
        renderer.blockShader->setVec3("ambientLight",glm::vec3(0.4f));
        renderer.blockShader->setVec3("diffuseLight",glm::vec3(0.6f));
        renderer.blockShader->setMat4("model", glm::mat4(1.0f));
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        for (int i = 0; i < static_cast<int>(RenderBehavior::COUNT); ++i) {
            RenderBehavior currentBehavior = static_cast<RenderBehavior>(i);
            bool translucent = (currentBehavior == RenderBehavior::ANIMATED_WATER || currentBehavior == RenderBehavior::ANIMATED_TRANSPARENT_WAVE);
            if (translucent) {
                // Let translucent passes read depth but avoid writing it so surfaces beneath stay visible.
                glDepthMask(GL_FALSE);
            }
            if (useVoxelRendering) {
                VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
                VoxelRenderContext& voxelRender = *baseSystem.voxelRender;
                for (const auto& [sectionKey, buffers] : voxelRender.renderBuffers) {
                    auto secIt = voxelWorld.sections.find(sectionKey);
                    if (secIt == voxelWorld.sections.end()) continue;
                    const VoxelSection& section = secIt->second;
                    if (!shouldRenderVoxelSection(baseSystem, section, playerPos)) continue;
                    int count = buffers.counts[i];
                    if (count <= 0) continue;
                    renderer.blockShader->setFloat("instanceScale", static_cast<float>(1 << section.lod));
                    renderer.blockShader->setInt("behaviorType", i);
                    if (buffers.vaos[i] == 0) continue;
                    glBindVertexArray(buffers.vaos[i]);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, count);
                }
                renderer.blockShader->setFloat("instanceScale", 1.0f);
            }
            if (currentBehavior == RenderBehavior::STATIC_BRANCH) {
                if (!branchInstances.empty()) {
                    renderer.blockShader->setInt("behaviorType", i);
                    glBindVertexArray(renderer.behaviorVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, renderer.behaviorInstanceVBOs[i]);
                    glBufferData(GL_ARRAY_BUFFER, branchInstances.size() * sizeof(BranchInstanceData), branchInstances.data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, branchInstances.size());
                }
            } else {
                if (!behaviorInstances[i].empty()) {
                    renderer.blockShader->setInt("behaviorType", i);
                    glBindVertexArray(renderer.behaviorVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, renderer.behaviorInstanceVBOs[i]);
                    glBufferData(GL_ARRAY_BUFFER, behaviorInstances[i].size() * sizeof(InstanceData), behaviorInstances[i].data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, behaviorInstances[i].size());
                }
            }
            if (translucent) {
                glDepthMask(GL_TRUE);
            }
        }

        auto bindFaceTextureUniforms = [&](Shader& shader){
            shader.setInt("atlasEnabled", (renderer.atlasTexture != 0 && renderer.atlasTilesPerRow > 0 && renderer.atlasTilesPerCol > 0) ? 1 : 0);
            shader.setVec2("atlasTileSize", glm::vec2(renderer.atlasTileSize));
            shader.setVec2("atlasTextureSize", glm::vec2(renderer.atlasTextureSize));
            shader.setInt("tilesPerRow", renderer.atlasTilesPerRow);
            shader.setInt("tilesPerCol", renderer.atlasTilesPerCol);
            shader.setInt("atlasTexture", 0);
            if (renderer.atlasTexture != 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, renderer.atlasTexture);
            }
        };

        auto drawFaceBatches = [&](const std::array<std::vector<FaceInstanceRenderData>, 6>& batches, bool depthWrite){
            if (!renderer.faceShader || !renderer.faceVAO) return;
            if (!depthWrite) glDepthMask(GL_FALSE);
            glEnable(GL_CULL_FACE);
            glFrontFace(GL_CCW);
            glCullFace(GL_BACK);

            renderer.faceShader->use();
            renderer.faceShader->setMat4("view", view);
            renderer.faceShader->setMat4("projection", projection);
            renderer.faceShader->setMat4("model", glm::mat4(1.0f));
            renderer.faceShader->setVec3("cameraPos", playerPos);
            renderer.faceShader->setVec3("lightDir", lightDir);
            renderer.faceShader->setVec3("ambientLight", glm::vec3(0.4f));
            renderer.faceShader->setVec3("diffuseLight", glm::vec3(0.6f));
            renderer.faceShader->setInt("faceType", 0);
            bindFaceTextureUniforms(*renderer.faceShader);
            glBindVertexArray(renderer.faceVAO);
            for (int faceType = 0; faceType < 6; ++faceType) {
                const auto& instances = batches[faceType];
                if (instances.empty()) continue;
                renderer.faceShader->setInt("faceType", faceType);
                glBindBuffer(GL_ARRAY_BUFFER, renderer.faceInstanceVBO);
                glBufferData(GL_ARRAY_BUFFER, instances.size() * sizeof(FaceInstanceRenderData), instances.data(), GL_DYNAMIC_DRAW);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, instances.size());
            }

            glDisable(GL_CULL_FACE);
            if (!depthWrite) glDepthMask(GL_TRUE);
        };

        if (renderer.faceShader && renderer.faceVAO) {
            std::array<std::vector<FaceInstanceRenderData>, 6> faceInstancesOpaque;
            std::array<std::vector<FaceInstanceRenderData>, 6> faceInstancesAlpha;
            for (int f = 0; f < 6; ++f) {
                for (const auto& inst : faceInstances[f]) {
                    if (inst.alpha < 0.999f) faceInstancesAlpha[f].push_back(inst);
                    else faceInstancesOpaque[f].push_back(inst);
                }
            }
            drawFaceBatches(faceInstancesOpaque, true);
            drawFaceBatches(faceInstancesAlpha, false);
        }

        if (useVoxelGreedy && renderer.faceShader && renderer.faceVAO) {
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            VoxelGreedyContext& voxelGreedy = *baseSystem.voxelGreedy;
            int superChunkMinLod = getRegistryInt(baseSystem, "voxelSuperChunkMinLod", 3);
            int superChunkMaxLod = getRegistryInt(baseSystem, "voxelSuperChunkMaxLod", 3);
            int superChunkSize = getRegistryInt(baseSystem, "voxelSuperChunkSize", 1);
            if (superChunkSize < 1) superChunkSize = 1;

            renderer.faceShader->use();
            renderer.faceShader->setMat4("view", view);
            renderer.faceShader->setMat4("projection", projection);
            renderer.faceShader->setMat4("model", glm::mat4(1.0f));
            renderer.faceShader->setVec3("cameraPos", playerPos);
            renderer.faceShader->setVec3("lightDir", lightDir);
            renderer.faceShader->setVec3("ambientLight", glm::vec3(0.4f));
            renderer.faceShader->setVec3("diffuseLight", glm::vec3(0.6f));
            renderer.faceShader->setInt("faceType", 0);
            bindFaceTextureUniforms(*renderer.faceShader);

            glEnable(GL_CULL_FACE);
            glFrontFace(GL_CCW);
            glCullFace(GL_BACK);

            for (const auto& [sectionKey, buffers] : voxelGreedy.renderBuffers) {
                auto secIt = voxelWorld.sections.find(sectionKey);
                if (secIt == voxelWorld.sections.end()) continue;
                if (secIt->second.lod > voxelGreedyMaxLod) continue;
                int mult = (sectionKey.lod >= superChunkMinLod
                            && sectionKey.lod <= superChunkMaxLod
                            && superChunkSize > 1) ? superChunkSize : 1;
                if (!shouldRenderVoxelSectionSized(baseSystem,
                                                   sectionKey.lod,
                                                   sectionKey.coord,
                                                   secIt->second.size,
                                                   mult,
                                                   playerPos)) {
                    continue;
                }
                for (int faceType = 0; faceType < 6; ++faceType) {
                    int count = buffers.opaqueCounts[faceType];
                    if (count > 0 && buffers.opaqueVaos[faceType] != 0) {
                        renderer.faceShader->setInt("faceType", faceType);
                        glBindVertexArray(buffers.opaqueVaos[faceType]);
                        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
                    }
                }
            }

            glDepthMask(GL_FALSE);
            for (const auto& [sectionKey, buffers] : voxelGreedy.renderBuffers) {
                auto secIt = voxelWorld.sections.find(sectionKey);
                if (secIt == voxelWorld.sections.end()) continue;
                if (secIt->second.lod > voxelGreedyMaxLod) continue;
                int mult = (sectionKey.lod >= superChunkMinLod
                            && sectionKey.lod <= superChunkMaxLod
                            && superChunkSize > 1) ? superChunkSize : 1;
                if (!shouldRenderVoxelSectionSized(baseSystem,
                                                   sectionKey.lod,
                                                   sectionKey.coord,
                                                   secIt->second.size,
                                                   mult,
                                                   playerPos)) {
                    continue;
                }
                for (int faceType = 0; faceType < 6; ++faceType) {
                    int count = buffers.alphaCounts[faceType];
                    if (count > 0 && buffers.alphaVaos[faceType] != 0) {
                        renderer.faceShader->setInt("faceType", faceType);
                        glBindVertexArray(buffers.alphaVaos[faceType]);
                        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
                    }
                }
            }
            glDepthMask(GL_TRUE);
            glDisable(GL_CULL_FACE);
        }

        if (player.isHoldingBlock && player.heldPrototypeID >= 0) {
            glm::vec3 heldPos = player.cameraPosition + cameraForward * 0.8f + glm::vec3(0.0f, -0.2f, 0.0f);
            bool drewTextured = false;
            if (player.heldPrototypeID < static_cast<int>(prototypes.size())) {
                const Entity& heldProto = prototypes[player.heldPrototypeID];
                if (heldProto.useTexture && renderer.faceShader && renderer.faceVAO) {
                    static const std::array<glm::vec3, 6> kFaceOffsets = {
                        glm::vec3(0.5f, 0.0f, 0.0f),  glm::vec3(-0.5f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 0.5f, 0.0f),  glm::vec3(0.0f, -0.5f, 0.0f),
                        glm::vec3(0.0f, 0.0f, 0.5f),  glm::vec3(0.0f, 0.0f, -0.5f)
                    };
                    renderer.faceShader->use();
                    renderer.faceShader->setMat4("view", view);
                    renderer.faceShader->setMat4("projection", projection);
                    renderer.faceShader->setMat4("model", glm::mat4(1.0f));
                    renderer.faceShader->setVec3("cameraPos", playerPos);
                    renderer.faceShader->setVec3("lightDir", lightDir);
                    renderer.faceShader->setVec3("ambientLight", glm::vec3(0.4f));
                    renderer.faceShader->setVec3("diffuseLight", glm::vec3(0.6f));
                    renderer.faceShader->setInt("faceType", 0);
                    renderer.faceShader->setInt("wireframeDebug", 0);
                    bindFaceTextureUniforms(*renderer.faceShader);
                    glEnable(GL_CULL_FACE);
                    glFrontFace(GL_CCW);
                    glCullFace(GL_BACK);
                    glBindVertexArray(renderer.faceVAO);
                    glBindBuffer(GL_ARRAY_BUFFER, renderer.faceInstanceVBO);
                    for (int faceType = 0; faceType < 6; ++faceType) {
                        FaceInstanceRenderData heldFace;
                        heldFace.position = heldPos + kFaceOffsets[faceType];
                        heldFace.color = player.heldBlockColor;
                        heldFace.tileIndex = FaceTileIndexFor(baseSystem.world.get(), heldProto, faceType);
                        heldFace.alpha = 1.0f;
                        heldFace.ao = glm::vec4(1.0f);
                        heldFace.scale = glm::vec2(1.0f);
                        heldFace.uvScale = glm::vec2(1.0f);
                        renderer.faceShader->setInt("faceType", faceType);
                        glBufferData(GL_ARRAY_BUFFER, sizeof(FaceInstanceRenderData), &heldFace, GL_DYNAMIC_DRAW);
                        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 1);
                    }
                    glDisable(GL_CULL_FACE);
                    drewTextured = true;
                }
            }
            if (!drewTextured) {
                InstanceData heldInstance;
                heldInstance.position = heldPos;
                heldInstance.color = player.heldBlockColor;
                int behaviorIndex = static_cast<int>(RenderBehavior::STATIC_DEFAULT);
                renderer.blockShader->use();
                renderer.blockShader->setMat4("view", view);
                renderer.blockShader->setMat4("projection", projection);
                renderer.blockShader->setVec3("cameraPos", playerPos);
                renderer.blockShader->setFloat("time", time);
                renderer.blockShader->setFloat("instanceScale", 1.0f);
                renderer.blockShader->setVec3("lightDir", lightDir);
                renderer.blockShader->setVec3("ambientLight", glm::vec3(0.4f));
                renderer.blockShader->setVec3("diffuseLight", glm::vec3(0.6f));
                renderer.blockShader->setMat4("model", glm::mat4(1.0f));
                renderer.blockShader->setInt("behaviorType", behaviorIndex);
                glBindVertexArray(renderer.behaviorVAOs[behaviorIndex]);
                glBindBuffer(GL_ARRAY_BUFFER, renderer.behaviorInstanceVBOs[behaviorIndex]);
                glBufferData(GL_ARRAY_BUFFER, sizeof(InstanceData), &heldInstance, GL_DYNAMIC_DRAW);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 36, 1);
            }
        }

        if (player.hasBlockTarget && renderer.selectionShader && renderer.selectionVAO && renderer.selectionVertexCount > 0) {
            renderer.selectionShader->use();
            glm::mat4 selectionModel = glm::translate(glm::mat4(1.0f), player.targetedBlockPosition);
            selectionModel = glm::scale(selectionModel, glm::vec3(1.02f));
            renderer.selectionShader->setMat4("model", selectionModel);
            renderer.selectionShader->setMat4("view", view);
            renderer.selectionShader->setMat4("projection", projection);
            renderer.selectionShader->setVec3("cameraPos", playerPos);
            renderer.selectionShader->setFloat("time", time);
            glBindVertexArray(renderer.selectionVAO);
            glDrawArrays(GL_LINES, 0, renderer.selectionVertexCount);
        }

        if (renderer.audioRayShader && renderer.audioRayVAO && renderer.audioRayVertexCount > 0) {
            glEnable(GL_BLEND);
            renderer.audioRayShader->use();
            renderer.audioRayShader->setMat4("view", view);
            renderer.audioRayShader->setMat4("projection", projection);
            glBindVertexArray(renderer.audioRayVAO);
            glLineWidth(1.6f);
            glDrawArrays(GL_LINES, 0, renderer.audioRayVertexCount);
            glLineWidth(1.0f);
        }

        bool crosshairEnabled = true;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("CrosshairEnabled");
            if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second)) {
                crosshairEnabled = std::get<bool>(it->second);
            }
        }
        if (crosshairEnabled && renderer.crosshairShader && renderer.crosshairVAO && renderer.crosshairVertexCount > 0) {
            glDisable(GL_DEPTH_TEST);
            renderer.crosshairShader->use();
            glBindVertexArray(renderer.crosshairVAO);
            glLineWidth(1.0f);
            glDrawArrays(GL_LINES, 0, renderer.crosshairVertexCount);
            glLineWidth(1.0f);
            glEnable(GL_DEPTH_TEST);
        }

        if (baseSystem.hud && renderer.hudShader && renderer.hudVAO) {
            HUDContext& hud = *baseSystem.hud;
            if (hud.showCharge) {
                glDisable(GL_DEPTH_TEST);
                renderer.hudShader->use();
                renderer.hudShader->setFloat("fillAmount", glm::clamp(hud.chargeValue, 0.0f, 1.0f));
                renderer.hudShader->setInt("ready", hud.chargeReady ? 1 : 0);
                renderer.hudShader->setInt("buildModeType", hud.buildModeType);
                renderer.hudShader->setVec3("previewColor", hud.buildPreviewColor);
                renderer.hudShader->setInt("channelIndex", hud.buildChannel);
                renderer.hudShader->setInt("previewTileIndex", hud.buildPreviewTileIndex);
                bindFaceTextureUniforms(*renderer.hudShader);
                glBindVertexArray(renderer.hudVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glEnable(GL_DEPTH_TEST);
            }
        }
    }

    void CleanupRenderer(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.renderer) return;
        stopGreedyAsync();
        RendererContext& renderer = *baseSystem.renderer;
        if (baseSystem.voxelGreedy) {
            for (auto& [_, buffers] : baseSystem.voxelGreedy->renderBuffers) {
                destroyVoxelGreedyRenderBuffers(buffers);
            }
            baseSystem.voxelGreedy->renderBuffers.clear();
            baseSystem.voxelGreedy->renderBuffersDirty.clear();
        }
        int behaviorCount = static_cast<int>(RenderBehavior::COUNT);
        glDeleteVertexArrays(behaviorCount, renderer.behaviorVAOs.data());
        glDeleteBuffers(behaviorCount, renderer.behaviorInstanceVBOs.data());
        glDeleteVertexArrays(1, &renderer.skyboxVAO);
        glDeleteVertexArrays(1, &renderer.sunMoonVAO);
        glDeleteVertexArrays(1, &renderer.starVAO);
        glDeleteBuffers(1, &renderer.cubeVBO);
        glDeleteBuffers(1, &renderer.skyboxVBO);
        glDeleteBuffers(1, &renderer.sunMoonVBO);
        glDeleteBuffers(1, &renderer.starVBO);
        if (renderer.selectionVAO) glDeleteVertexArrays(1, &renderer.selectionVAO);
        if (renderer.selectionVBO) glDeleteBuffers(1, &renderer.selectionVBO);
        if (renderer.hudVAO) glDeleteVertexArrays(1, &renderer.hudVAO);
        if (renderer.hudVBO) glDeleteBuffers(1, &renderer.hudVBO);
        if (renderer.crosshairVAO) glDeleteVertexArrays(1, &renderer.crosshairVAO);
        if (renderer.crosshairVBO) glDeleteBuffers(1, &renderer.crosshairVBO);
        if (renderer.uiVAO) glDeleteVertexArrays(1, &renderer.uiVAO);
        if (renderer.uiVBO) glDeleteBuffers(1, &renderer.uiVBO);
        if (renderer.uiButtonVAO) glDeleteVertexArrays(1, &renderer.uiButtonVAO);
        if (renderer.uiButtonVBO) glDeleteBuffers(1, &renderer.uiButtonVBO);
        if (renderer.fontVAO) glDeleteVertexArrays(1, &renderer.fontVAO);
        if (renderer.fontVBO) glDeleteBuffers(1, &renderer.fontVBO);
        if (renderer.audioRayVAO) glDeleteVertexArrays(1, &renderer.audioRayVAO);
        if (renderer.audioRayVBO) glDeleteBuffers(1, &renderer.audioRayVBO);
        if (renderer.audioRayVoxelVAO) glDeleteVertexArrays(1, &renderer.audioRayVoxelVAO);
        if (renderer.audioRayVoxelInstanceVBO) glDeleteBuffers(1, &renderer.audioRayVoxelInstanceVBO);
        if (renderer.faceVAO) glDeleteVertexArrays(1, &renderer.faceVAO);
        if (renderer.faceVBO) glDeleteBuffers(1, &renderer.faceVBO);
        if (renderer.faceInstanceVBO) glDeleteBuffers(1, &renderer.faceInstanceVBO);
        if (renderer.atlasTexture) glDeleteTextures(1, &renderer.atlasTexture);
    }
}
