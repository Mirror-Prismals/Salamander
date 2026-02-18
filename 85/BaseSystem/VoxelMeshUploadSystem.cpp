#pragma once

#include <GLFW/glfw3.h>
#include <chrono>
#include <string>
#include <vector>

namespace RenderInitSystemLogic {
    RenderBehavior BehaviorForPrototype(const Entity& proto);
    void DestroyVoxelGreedyRenderBuffers(VoxelGreedyRenderBuffers& buffers);
    void DestroyChunkRenderBuffers(ChunkRenderBuffers& buffers);
    int getRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback);
    bool shouldRenderVoxelSection(const BaseSystem& baseSystem,
                                  const VoxelSection& section,
                                  const glm::vec3& cameraPos);
}
namespace VoxelMeshInitSystemLogic {
    glm::vec3 UnpackColor(uint32_t packed);
}

namespace VoxelMeshUploadSystemLogic {
    namespace {
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

    void UpdateVoxelMeshUpload(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float, GLFWwindow*) {
        if (!baseSystem.renderer || !baseSystem.player) return;
        RendererContext& renderer = *baseSystem.renderer;
        glm::vec3 playerPos = baseSystem.player->cameraPosition;

        int voxelGreedyMaxLod = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelGreedyMaxLod", 1);
        bool useVoxelGreedy = baseSystem.voxelWorld && baseSystem.voxelWorld->enabled && baseSystem.voxelGreedy
            && renderer.faceShader && renderer.faceVAO && voxelGreedyMaxLod >= 0;
        bool useVoxelRendering = baseSystem.voxelWorld && baseSystem.voxelWorld->enabled && baseSystem.voxelRender
            && (!useVoxelGreedy || (baseSystem.voxelWorld && voxelGreedyMaxLod < baseSystem.voxelWorld->maxLod));

        if (useVoxelRendering) {
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            VoxelRenderContext& voxelRender = *baseSystem.voxelRender;
            int voxelGreedyMaxLodLocal = ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelGreedyMaxLod", 1);

            std::vector<VoxelSectionKey> staleSections;
            for (const auto& [key, _] : voxelRender.renderBuffers) {
                auto it = voxelWorld.sections.find(key);
                if (it == voxelWorld.sections.end() || it->second.nonAirCount <= 0) {
                    staleSections.push_back(key);
                }
            }
            for (const auto& key : staleSections) {
                ::RenderInitSystemLogic::DestroyChunkRenderBuffers(voxelRender.renderBuffers[key]);
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
                    ::RenderInitSystemLogic::DestroyVoxelGreedyRenderBuffers(bufIt->second);
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
                            ::RenderInitSystemLogic::DestroyVoxelGreedyRenderBuffers(bufIt->second);
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
        }
    }
}
