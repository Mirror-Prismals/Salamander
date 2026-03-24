#pragma once

#include "Host/PlatformInput.h"

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

    }

    void UpdateDebugWireframe(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt; (void)win;
        if (!DebugWireframeEnabled(baseSystem)) return;
        if (!baseSystem.renderer || !baseSystem.player || !baseSystem.world || !baseSystem.level || !baseSystem.renderBackend) return;

        RendererContext& renderer = *baseSystem.renderer;
        PlayerContext& player = *baseSystem.player;
        LevelContext& level = *baseSystem.level;
        auto& renderBackend = *baseSystem.renderBackend;

        auto setDepthTestEnabled = [&](bool enabled) {
            renderBackend.setDepthTestEnabled(enabled);
        };
        auto setBlendEnabled = [&](bool enabled) {
            renderBackend.setBlendEnabled(enabled);
        };
        auto setBlendModeAlpha = [&]() {
            renderBackend.setBlendModeAlpha();
        };
        auto setWireframeEnabled = [&](bool enabled) {
            renderBackend.setWireframeEnabled(enabled);
        };

        setDepthTestEnabled(true);
        setBlendEnabled(false);
        setWireframeEnabled(true);

        // Replace the normal frame (and UI) with a pure wireframe pass.
        renderBackend.clearDefaultFramebuffer(0.0f, 0.0f, 0.0f, 1.0f, true);

        glm::mat4 view = player.viewMatrix;
        glm::mat4 projection = player.projectionMatrix;
        glm::vec3 cameraPos = player.cameraPosition;
        float time = static_cast<float>(PlatformInput::GetTimeSeconds());

        if (renderer.blockShader) {
            renderer.blockShader->use();
            renderer.blockShader->setMat4("view", view);
            renderer.blockShader->setMat4("projection", projection);
        renderer.blockShader->setVec3("cameraPos", cameraPos);
        renderer.blockShader->setFloat("time", time);
        renderer.blockShader->setFloat("instanceScale", 1.0f);
        renderer.blockShader->setVec3("lightDir", glm::vec3(0.0f, 1.0f, 0.0f));
            renderer.blockShader->setVec3("ambientLight", glm::vec3(0.4f));
            renderer.blockShader->setVec3("diffuseLight", glm::vec3(0.6f));
            renderer.blockShader->setMat4("model", glm::mat4(1.0f));
            renderer.blockShader->setInt("wireframeDebug", 1);

            std::vector<std::vector<InstanceData>> behaviorInstances(static_cast<int>(RenderBehavior::COUNT));
            std::vector<BranchInstanceData> branchInstances;
            for (const auto& worldProto : level.worlds) {
                for (const auto& instance : worldProto.instances) {
                    if (instance.prototypeID < 0 || instance.prototypeID >= static_cast<int>(prototypes.size())) continue;
                    const Entity& proto = prototypes[instance.prototypeID];
                    if (!proto.isBlock || !proto.isRenderable) continue;
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
                    renderBackend.bindVertexArray(renderer.behaviorVAOs[i]);
                    renderBackend.uploadArrayBufferData(
                        renderer.behaviorInstanceVBOs[i],
                        branchInstances.data(),
                        branchInstances.size() * sizeof(BranchInstanceData),
                        true
                    );
                    renderBackend.drawArraysTrianglesInstanced(0, 36, static_cast<int>(branchInstances.size()));
                } else {
                    if (behaviorInstances[i].empty()) continue;
                    renderer.blockShader->setInt("behaviorType", i);
                    renderBackend.bindVertexArray(renderer.behaviorVAOs[i]);
                    renderBackend.uploadArrayBufferData(
                        renderer.behaviorInstanceVBOs[i],
                        behaviorInstances[i].data(),
                        behaviorInstances[i].size() * sizeof(InstanceData),
                        true
                    );
                    renderBackend.drawArraysTrianglesInstanced(0, 36, static_cast<int>(behaviorInstances[i].size()));
                }
            }
        }

        if (renderer.faceShader) {
            renderer.faceShader->use();
            renderer.faceShader->setInt("wireframeDebug", 0);
            renderer.faceShader->setInt("leafBackfacesWhenInside", 0);
        }
        if (renderer.blockShader) {
            renderer.blockShader->use();
            renderer.blockShader->setInt("wireframeDebug", 0);
        }

        setWireframeEnabled(false);

        // Keep debug text visible after the wireframe pass.
        setBlendEnabled(true);
        setBlendModeAlpha();
        FontSystemLogic::UpdateFonts(baseSystem, prototypes, dt, win);
    }
}
