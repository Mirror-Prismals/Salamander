#pragma once

#include "Host/PlatformInput.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace RenderInitSystemLogic {
    RenderBehavior BehaviorForPrototype(const Entity& proto);
    void DestroyVoxelGreedyRenderBuffers(VoxelGreedyRenderBuffers& buffers, IRenderBackend& renderBackend);
    void DestroyChunkRenderBuffers(ChunkRenderBuffers& buffers, IRenderBackend& renderBackend);
    int getRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback);
    bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback);
    bool shouldRenderVoxelSection(const BaseSystem& baseSystem,
                                  const VoxelSection& section,
                                  const glm::vec3& cameraPos);
}
namespace VoxelMeshInitSystemLogic {
    glm::vec3 UnpackColor(uint32_t packed);
}

namespace VoxelMeshUploadSystemLogic {
    namespace {
        bool webgpuGreedyLogEnabled() {
            static int cached = -1;
            if (cached < 0) {
                const char* env = std::getenv("SALAMANDER_WEBGPU_GREEDY_LOG");
                cached = (env && env[0] != '\0' && env[0] != '0') ? 1 : 0;
            }
            return cached == 1;
        }

        const std::vector<VertexAttribLayout>& FaceVertexLayout() {
            static const std::vector<VertexAttribLayout> kLayout = {
                {0u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(8 * sizeof(float)), 0u, 0u},
                {1u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(8 * sizeof(float)), static_cast<size_t>(3 * sizeof(float)), 0u},
                {2u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(8 * sizeof(float)), static_cast<size_t>(6 * sizeof(float)), 0u},
            };
            return kLayout;
        }

        const std::vector<VertexAttribLayout>& FaceInstanceLayout() {
            static const std::vector<VertexAttribLayout> kLayout = {
                {3u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, position), 1u},
                {4u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, color), 1u},
                {5u, 1, VertexAttribType::Int,   false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, tileIndex), 1u},
                {6u, 1, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, alpha), 1u},
                {7u, 4, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, ao), 1u},
                {8u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, scale), 1u},
                {9u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, uvScale), 1u},
            };
            return kLayout;
        }

        const std::vector<VertexAttribLayout>& BranchInstanceLayout() {
            static const std::vector<VertexAttribLayout> kLayout = {
                {3u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(BranchInstanceData)), offsetof(BranchInstanceData, position), 1u},
                {4u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(BranchInstanceData)), offsetof(BranchInstanceData, rotation), 1u},
                {5u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(BranchInstanceData)), offsetof(BranchInstanceData, color), 1u},
            };
            return kLayout;
        }

        const std::vector<VertexAttribLayout>& BlockInstanceLayout() {
            static const std::vector<VertexAttribLayout> kLayout = {
                {3u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(InstanceData)), offsetof(InstanceData, position), 1u},
                {4u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(InstanceData)), offsetof(InstanceData, color), 1u},
                {5u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(InstanceData)), offsetof(InstanceData, color), 1u},
            };
            return kLayout;
        }

        void BuildVoxelGreedyRenderBuffers(IRenderBackend& renderBackend,
                                           const RendererContext& renderer,
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
                // Leaf cutouts use alpha sentinel < 0.0; draw them in opaque pass so depth resolves correctly.
                if (alpha < 0.0f) {
                    opaqueInstances[faceType].push_back(inst);
                } else if (alpha < 0.999f) {
                    alphaInstances[faceType].push_back(inst);
                } else {
                    opaqueInstances[faceType].push_back(inst);
                }
            }

            if (webgpuGreedyLogEnabled()) {
                static int logBudget = 256;
                if (logBudget > 0) {
                    std::cerr << "[VoxelGreedy][Buffers] chunkFaces=" << chunk.positions.size()
                              << " opaque(0..5)="
                              << opaqueInstances[0].size() << ","
                              << opaqueInstances[1].size() << ","
                              << opaqueInstances[2].size() << ","
                              << opaqueInstances[3].size() << ","
                              << opaqueInstances[4].size() << ","
                              << opaqueInstances[5].size()
                              << " alpha(0..5)="
                              << alphaInstances[0].size() << ","
                              << alphaInstances[1].size() << ","
                              << alphaInstances[2].size() << ","
                              << alphaInstances[3].size() << ","
                              << alphaInstances[4].size() << ","
                              << alphaInstances[5].size()
                              << std::endl;
                    --logBudget;
                }
            }

            for (int faceType = 0; faceType < 6; ++faceType) {
                const auto& opaque = opaqueInstances[faceType];
                buffers.opaqueCounts[faceType] = static_cast<int>(opaque.size());
                if (buffers.opaqueCounts[faceType] > 0) {
                    renderBackend.ensureVertexArray(buffers.opaqueVaos[faceType]);
                    renderBackend.ensureArrayBuffer(buffers.opaqueVBOs[faceType]);
                    renderBackend.uploadArrayBufferData(
                        buffers.opaqueVBOs[faceType],
                        opaque.data(),
                        opaque.size() * sizeof(FaceInstanceRenderData),
                        false
                    );
                    renderBackend.configureVertexArray(
                        buffers.opaqueVaos[faceType],
                        renderer.faceVBO,
                        FaceVertexLayout(),
                        buffers.opaqueVBOs[faceType],
                        FaceInstanceLayout()
                    );
                } else {
                    buffers.opaqueCounts[faceType] = 0;
                }

                const auto& alpha = alphaInstances[faceType];
                buffers.alphaCounts[faceType] = static_cast<int>(alpha.size());
                if (buffers.alphaCounts[faceType] > 0) {
                    renderBackend.ensureVertexArray(buffers.alphaVaos[faceType]);
                    renderBackend.ensureArrayBuffer(buffers.alphaVBOs[faceType]);
                    renderBackend.uploadArrayBufferData(
                        buffers.alphaVBOs[faceType],
                        alpha.data(),
                        alpha.size() * sizeof(FaceInstanceRenderData),
                        false
                    );
                    renderBackend.configureVertexArray(
                        buffers.alphaVaos[faceType],
                        renderer.faceVBO,
                        FaceVertexLayout(),
                        buffers.alphaVBOs[faceType],
                        FaceInstanceLayout()
                    );
                } else {
                    buffers.alphaCounts[faceType] = 0;
                }
            }
            renderBackend.unbindVertexArray();
        }

        void BuildVoxelRenderBuffers(BaseSystem& baseSystem,
                                     std::vector<Entity>& prototypes,
                                     const VoxelSectionKey& sectionKey,
                                     bool faceCullingInitialized,
                                     IRenderBackend& renderBackend) {
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
                        RenderBehavior behavior = ::RenderInitSystemLogic::BehaviorForPrototype(proto);
                        glm::vec3 color = VoxelMeshInitSystemLogic::UnpackColor(section.colors[idx]);
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

                renderBackend.ensureVertexArray(buffers.vaos[i]);
                renderBackend.ensureArrayBuffer(buffers.instanceVBOs[i]);
                if (isBranch) {
                    renderBackend.uploadArrayBufferData(
                        buffers.instanceVBOs[i],
                        branchData.data(),
                        branchData.size() * sizeof(BranchInstanceData),
                        false
                    );
                    renderBackend.configureVertexArray(
                        buffers.vaos[i],
                        renderer.cubeVBO,
                        FaceVertexLayout(),
                        buffers.instanceVBOs[i],
                        BranchInstanceLayout()
                    );
                } else {
                    renderBackend.uploadArrayBufferData(
                        buffers.instanceVBOs[i],
                        behaviorData[i].data(),
                        behaviorData[i].size() * sizeof(InstanceData),
                        false
                    );
                    renderBackend.configureVertexArray(
                        buffers.vaos[i],
                        renderer.cubeVBO,
                        FaceVertexLayout(),
                        buffers.instanceVBOs[i],
                        BlockInstanceLayout()
                    );
                }
            }

            renderBackend.unbindVertexArray();
            buffers.builtWithFaceCulling = faceCullingInitialized;
        }
    }

    void UpdateVoxelMeshUpload(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float, PlatformWindowHandle) {
        if (!baseSystem.renderer || !baseSystem.player || !baseSystem.renderBackend) return;
        RendererContext& renderer = *baseSystem.renderer;
        IRenderBackend& renderBackend = *baseSystem.renderBackend;
        glm::vec3 playerPos = baseSystem.player->cameraPosition;

        int voxelGreedyMaxLod = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelGreedyMaxLod", 1);
        const bool isWebGpuBackend = std::string(renderBackend.name()) == "WebGPU";
        const bool disableGreedyForWebGpu = isWebGpuBackend
            && ::RenderInitSystemLogic::getRegistryBool(baseSystem, "webgpuDisableVoxelGreedy", false);
        if (disableGreedyForWebGpu) {
            voxelGreedyMaxLod = -1;
        }
        bool useVoxelGreedy = baseSystem.voxelWorld && baseSystem.voxelWorld->enabled && baseSystem.voxelGreedy
            && renderer.faceShader && renderer.faceVAO && voxelGreedyMaxLod >= 0;
        bool useVoxelRendering = baseSystem.voxelWorld && baseSystem.voxelWorld->enabled && baseSystem.voxelRender
            && (!useVoxelGreedy || (baseSystem.voxelWorld && voxelGreedyMaxLod < baseSystem.voxelWorld->maxLod));
        const bool debugVoxelMeshingPerf = ::RenderInitSystemLogic::getRegistryBool(baseSystem, "DebugVoxelMeshingPerf", false);

        if (useVoxelRendering) {
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            VoxelRenderContext& voxelRender = *baseSystem.voxelRender;
            int voxelGreedyMaxLodLocal = voxelGreedyMaxLod;

            std::vector<VoxelSectionKey> staleSections;
            for (const auto& [key, _] : voxelRender.renderBuffers) {
                auto it = voxelWorld.sections.find(key);
                if (it == voxelWorld.sections.end() || it->second.nonAirCount <= 0) {
                    staleSections.push_back(key);
                }
            }
            for (const auto& key : staleSections) {
                ::RenderInitSystemLogic::DestroyChunkRenderBuffers(voxelRender.renderBuffers[key], renderBackend);
                voxelRender.renderBuffers.erase(key);
            }

            for (const auto& key : voxelWorld.dirtySections) {
                auto it = voxelWorld.sections.find(key);
                if (it == voxelWorld.sections.end()) continue;
                if (key.lod <= voxelGreedyMaxLodLocal) continue;
                if (!::RenderInitSystemLogic::shouldRenderVoxelSection(baseSystem, it->second, playerPos)) continue;
                voxelRender.renderBuffersDirty.insert(key);
            }

            if (!voxelRender.renderBuffersDirty.empty()) {
                auto start = std::chrono::steady_clock::now();
                size_t buildCount = 0;
                std::vector<VoxelSectionKey> builtKeys;
                builtKeys.reserve(voxelRender.renderBuffersDirty.size());
                for (const auto& key : voxelRender.renderBuffersDirty) {
                    BuildVoxelRenderBuffers(baseSystem, prototypes, key, false, renderBackend);
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
                if (debugVoxelMeshingPerf) {
                    std::cout << "RenderSystem: rebuilt " << buildCount
                              << " voxel section buffer(s) in "
                              << elapsedMs << " ms." << std::endl;
                }
            }
        }

        if (useVoxelGreedy) {
            VoxelGreedyContext& voxelGreedy = *baseSystem.voxelGreedy;
            std::vector<VoxelSectionKey> staleBuffers;
            for (const auto& [key, _] : voxelGreedy.renderBuffers) {
                if (voxelGreedy.chunks.find(key) == voxelGreedy.chunks.end()) {
                    staleBuffers.push_back(key);
                }
            }
            for (const auto& key : staleBuffers) {
                auto bufIt = voxelGreedy.renderBuffers.find(key);
                if (bufIt != voxelGreedy.renderBuffers.end()) {
                    ::RenderInitSystemLogic::DestroyVoxelGreedyRenderBuffers(bufIt->second, renderBackend);
                    voxelGreedy.renderBuffers.erase(bufIt);
                }
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
                            ::RenderInitSystemLogic::DestroyVoxelGreedyRenderBuffers(bufIt->second, renderBackend);
                            voxelGreedy.renderBuffers.erase(bufIt);
                        }
                        voxelGreedy.renderBuffersDirty.erase(key);
                        continue;
                    }
                    VoxelGreedyRenderBuffers& buffers = voxelGreedy.renderBuffers[key];
                    BuildVoxelGreedyRenderBuffers(renderBackend, renderer, chunkIt->second, buffers);
                    voxelGreedy.renderBuffersDirty.erase(key);
                }
            }
        }
    }
}
