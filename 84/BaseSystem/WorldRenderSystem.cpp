#pragma once

#include <array>
#include <cmath>
#include <ctime>
#include <iostream>
#include <vector>

namespace RenderInitSystemLogic {
    RenderBehavior BehaviorForPrototype(const Entity& proto);
    int getRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback);
    bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback);
    bool shouldRenderVoxelSection(const BaseSystem& baseSystem, const VoxelSection& section, const glm::vec3& cameraPos);
    bool shouldRenderVoxelSectionSized(const BaseSystem& baseSystem, int lod, const glm::ivec3& sectionCoord, int sectionSize, int sizeMultiplier, const glm::vec3& cameraPos);
    int FaceTileIndexFor(const WorldContext* worldCtx, const Entity& proto, int faceType);
}

namespace WorldRenderSystemLogic {

    void RenderWorld(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
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

        int voxelGreedyMaxLod = RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelGreedyMaxLod", 1);
        bool useVoxelGreedy = baseSystem.voxelWorld && baseSystem.voxelWorld->enabled && baseSystem.voxelGreedy
            && renderer.faceShader && renderer.faceVAO && voxelGreedyMaxLod >= 0;
        bool useVoxelRendering = baseSystem.voxelWorld && baseSystem.voxelWorld->enabled && baseSystem.voxelRender
            && (!useVoxelGreedy || (baseSystem.voxelWorld && voxelGreedyMaxLod < baseSystem.voxelWorld->maxLod));
        if (RenderInitSystemLogic::getRegistryBool(baseSystem, "DebugVoxelRender", false) && baseSystem.voxelWorld) {
            size_t sectionCount = baseSystem.voxelWorld->sections.size();
            size_t renderCount = baseSystem.voxelRender ? baseSystem.voxelRender->renderBuffers.size() : 0;
            size_t greedyCount = baseSystem.voxelGreedy ? baseSystem.voxelGreedy->renderBuffers.size() : 0;
            std::cout << "[DebugVoxelRender] sections=" << sectionCount
                      << " renderBuffers=" << renderCount
                      << " greedyBuffers=" << greedyCount
                      << " useVoxelRendering=" << (useVoxelRendering ? 1 : 0)
                      << " useVoxelGreedy=" << (useVoxelGreedy ? 1 : 0)
                      << std::endl;
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
                    if (!RenderInitSystemLogic::shouldRenderVoxelSection(baseSystem, section, playerPos)) continue;
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
            int superChunkMinLod = RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkMinLod", 3);
            int superChunkMaxLod = RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkMaxLod", 3);
            int superChunkSize = RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelSuperChunkSize", 1);
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
                if (!RenderInitSystemLogic::shouldRenderVoxelSectionSized(baseSystem,
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
                if (!RenderInitSystemLogic::shouldRenderVoxelSectionSized(baseSystem,
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
                        heldFace.tileIndex = RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), heldProto, faceType);
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
}
