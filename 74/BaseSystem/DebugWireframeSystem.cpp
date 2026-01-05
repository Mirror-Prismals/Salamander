#pragma once

#include <GLFW/glfw3.h>

namespace DebugWireframeSystemLogic {
    namespace {
        bool DebugWireframeEnabled(const BaseSystem& baseSystem) {
            if (!baseSystem.registry) return false;
            auto it = baseSystem.registry->find("DebugWireframeSystem");
            return it != baseSystem.registry->end()
                && std::holds_alternative<bool>(it->second)
                && std::get<bool>(it->second);
        }

        RenderBehavior BehaviorForPrototype(const Entity& proto) {
            if (proto.name == "Branch") return RenderBehavior::STATIC_BRANCH;
            if (proto.name == "Water") return RenderBehavior::ANIMATED_WATER;
            if (proto.name == "TransparentWave") return RenderBehavior::ANIMATED_TRANSPARENT_WAVE;
            if (proto.hasWireframe && proto.isAnimated) return RenderBehavior::ANIMATED_WIREFRAME;
            return RenderBehavior::STATIC_DEFAULT;
        }

        float ChunkCenterDistance(const ChunkKey& key,
                                  const glm::vec3& cameraPos,
                                  const glm::ivec3& chunkSize) {
            glm::vec3 minCorner = glm::vec3(key.chunkIndex * chunkSize);
            glm::vec3 center = minCorner + (glm::vec3(chunkSize) * 0.5f);
            return glm::length(center - cameraPos);
        }

        bool ShouldRenderChunk(const ChunkContext& chunkCtx,
                               const ChunkKey& key,
                               const glm::vec3& cameraPos,
                               float maxChunkDist) {
            if (chunkCtx.renderDistanceChunks <= 0) return true;
            return ChunkCenterDistance(key, cameraPos, chunkCtx.chunkSize) <= maxChunkDist;
        }
    }

    void UpdateDebugWireframe(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt; (void)win;
        if (!DebugWireframeEnabled(baseSystem)) return;
        if (!baseSystem.renderer || !baseSystem.player || !baseSystem.world || !baseSystem.level) return;

        RendererContext& renderer = *baseSystem.renderer;
        PlayerContext& player = *baseSystem.player;
        LevelContext& level = *baseSystem.level;

        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        // Replace the normal frame (and UI) with a pure wireframe pass.
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = player.viewMatrix;
        glm::mat4 projection = player.projectionMatrix;
        glm::vec3 cameraPos = player.cameraPosition;
        float time = static_cast<float>(glfwGetTime());

        bool useChunkRendering = baseSystem.chunk && baseSystem.chunk->initialized;
        float maxChunkDist = 0.0f;
        if (useChunkRendering) {
            ChunkContext& chunkCtx = *baseSystem.chunk;
            maxChunkDist = static_cast<float>(chunkCtx.renderDistanceChunks)
                * static_cast<float>(std::max(chunkCtx.chunkSize.x,
                    std::max(chunkCtx.chunkSize.y, chunkCtx.chunkSize.z)));
        }

        bool drewFaceChunks = false;
        if (baseSystem.face && baseSystem.face->initialized && renderer.faceShader && renderer.faceVAO) {
            FaceContext& faceCtx = *baseSystem.face;
            renderer.faceShader->use();
            renderer.faceShader->setMat4("view", view);
            renderer.faceShader->setMat4("projection", projection);
            renderer.faceShader->setMat4("model", glm::mat4(1.0f));
            renderer.faceShader->setVec3("cameraPos", cameraPos);
            renderer.faceShader->setInt("wireframeDebug", 1);

            glBindVertexArray(renderer.faceVAO);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.faceInstanceVBO);
            glEnable(GL_CULL_FACE);
            glFrontFace(GL_CCW);
            glCullFace(GL_BACK);

            std::array<std::vector<FaceInstanceRenderData>, 6> faceBatches;
            for (const auto& [chunkKey, faceChunk] : faceCtx.faces) {
                if (useChunkRendering && baseSystem.chunk) {
                    const ChunkContext& chunkCtx = *baseSystem.chunk;
                    if (!ShouldRenderChunk(chunkCtx, chunkKey, cameraPos, maxChunkDist)) continue;
                }
                for (size_t i = 0; i < faceChunk.positions.size(); ++i) {
                    int faceType = (i < faceChunk.faceTypes.size()) ? faceChunk.faceTypes[i] : -1;
                    if (faceType < 0 || faceType >= 6) continue;
                    glm::vec3 color = (i < faceChunk.colors.size()) ? faceChunk.colors[i] : glm::vec3(1.0f);
                    int tileIndex = (i < faceChunk.tileIndices.size()) ? faceChunk.tileIndices[i] : -1;
                    float alpha = (i < faceChunk.alphas.size()) ? faceChunk.alphas[i] : 1.0f;
                    glm::vec4 ao = (i < faceChunk.ao.size()) ? faceChunk.ao[i] : glm::vec4(1.0f);
                    faceBatches[faceType].push_back({faceChunk.positions[i], color, tileIndex, alpha, ao});
                }
            }

            for (int faceType = 0; faceType < 6; ++faceType) {
                auto& instances = faceBatches[faceType];
                if (instances.empty()) continue;
                renderer.faceShader->setInt("faceType", faceType);
                glBufferData(GL_ARRAY_BUFFER, instances.size() * sizeof(FaceInstanceRenderData), instances.data(), GL_DYNAMIC_DRAW);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, instances.size());
            }

            glDisable(GL_CULL_FACE);
            drewFaceChunks = true;
        }

        if (renderer.blockShader) {
            renderer.blockShader->use();
            renderer.blockShader->setMat4("view", view);
            renderer.blockShader->setMat4("projection", projection);
            renderer.blockShader->setVec3("cameraPos", cameraPos);
            renderer.blockShader->setFloat("time", time);
            renderer.blockShader->setVec3("lightDir", glm::vec3(0.0f, 1.0f, 0.0f));
            renderer.blockShader->setVec3("ambientLight", glm::vec3(0.4f));
            renderer.blockShader->setVec3("diffuseLight", glm::vec3(0.6f));
            renderer.blockShader->setMat4("model", glm::mat4(1.0f));
            renderer.blockShader->setInt("wireframeDebug", 1);

            if (!drewFaceChunks && useChunkRendering && baseSystem.chunk) {
                ChunkContext& chunkCtx = *baseSystem.chunk;
                for (const auto& [chunkKey, chunk] : chunkCtx.chunks) {
                    (void)chunk;
                    if (!ShouldRenderChunk(chunkCtx, chunkKey, cameraPos, maxChunkDist)) continue;
                    auto it = chunkCtx.renderBuffers.find(chunkKey);
                    if (it == chunkCtx.renderBuffers.end()) continue;
                    const ChunkRenderBuffers& buffers = it->second;
                    for (int i = 0; i < static_cast<int>(RenderBehavior::COUNT); ++i) {
                        int count = buffers.counts[i];
                        if (count <= 0) continue;
                        if (buffers.vaos[i] == 0) continue;
                        renderer.blockShader->setInt("behaviorType", i);
                        glBindVertexArray(buffers.vaos[i]);
                        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, count);
                    }
                }
            }

            std::vector<std::vector<InstanceData>> behaviorInstances(static_cast<int>(RenderBehavior::COUNT));
            std::vector<BranchInstanceData> branchInstances;
            for (const auto& worldProto : level.worlds) {
                for (const auto& instance : worldProto.instances) {
                    if (instance.prototypeID < 0 || instance.prototypeID >= static_cast<int>(prototypes.size())) continue;
                    const Entity& proto = prototypes[instance.prototypeID];
                    if (!proto.isBlock || !proto.isRenderable) continue;
                    if (useChunkRendering && proto.isChunkable) continue;
                    RenderBehavior behavior = BehaviorForPrototype(proto);
                    glm::vec3 lineColor = instance.color;
                    if (proto.useTexture) lineColor = glm::vec3(0.5f);
                    if (behavior == RenderBehavior::STATIC_BRANCH) {
                        branchInstances.push_back({instance.position, instance.rotation, lineColor});
                    } else {
                        behaviorInstances[static_cast<int>(behavior)].push_back({instance.position, lineColor});
                    }
                }
            }

            for (int i = 0; i < static_cast<int>(RenderBehavior::COUNT); ++i) {
                RenderBehavior behavior = static_cast<RenderBehavior>(i);
                if (behavior == RenderBehavior::STATIC_BRANCH) {
                    if (branchInstances.empty()) continue;
                    renderer.blockShader->setInt("behaviorType", i);
                    glBindVertexArray(renderer.behaviorVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, renderer.behaviorInstanceVBOs[i]);
                    glBufferData(GL_ARRAY_BUFFER, branchInstances.size() * sizeof(BranchInstanceData), branchInstances.data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, branchInstances.size());
                } else {
                    if (behaviorInstances[i].empty()) continue;
                    renderer.blockShader->setInt("behaviorType", i);
                    glBindVertexArray(renderer.behaviorVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, renderer.behaviorInstanceVBOs[i]);
                    glBufferData(GL_ARRAY_BUFFER, behaviorInstances[i].size() * sizeof(InstanceData), behaviorInstances[i].data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, behaviorInstances[i].size());
                }
            }
        }

        if (renderer.faceShader) {
            renderer.faceShader->use();
            renderer.faceShader->setInt("wireframeDebug", 0);
        }
        if (renderer.blockShader) {
            renderer.blockShader->use();
            renderer.blockShader->setInt("wireframeDebug", 0);
        }

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        // Keep debug text visible after the wireframe pass.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        FontSystemLogic::UpdateFonts(baseSystem, prototypes, dt, win);
    }
}
