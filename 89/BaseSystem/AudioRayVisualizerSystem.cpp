#pragma once

#include <cstddef>
#include <vector>
#include <glm/glm.hpp>

// Forward declarations
struct BaseSystem;
struct Entity;

namespace AudioRayVisualizerSystemLogic {

    void UpdateAudioRayVisualizer(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.rayTracedAudio || !baseSystem.renderBackend) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        RayTracedAudioContext& rtAudio = *baseSystem.rayTracedAudio;
        auto& renderBackend = *baseSystem.renderBackend;
        struct RayVertex { glm::vec3 pos; glm::vec3 color; };

        static bool shaderWarned = false;
        if (!renderer.audioRayShader) {
            if (world.shaders.count("AUDIORAY_VERTEX_SHADER") && world.shaders.count("AUDIORAY_FRAGMENT_SHADER")) {
                renderer.audioRayShader = std::make_unique<Shader>(world.shaders["AUDIORAY_VERTEX_SHADER"].c_str(), world.shaders["AUDIORAY_FRAGMENT_SHADER"].c_str());
            } else if (!shaderWarned) {
                shaderWarned = true;
                std::cerr << "AudioRayVisualizer: shader sources not found in procedures.glsl\n";
            }
        }
        if (!renderer.audioRayShader) return;

        if (renderer.audioRayVAO == 0) {
            static const std::vector<VertexAttribLayout> kRayVertexLayout = {
                {0, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(RayVertex)), offsetof(RayVertex, pos), 0},
                {1, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(RayVertex)), offsetof(RayVertex, color), 0}
            };
            renderBackend.ensureVertexArray(renderer.audioRayVAO);
            renderBackend.ensureArrayBuffer(renderer.audioRayVBO);
            renderBackend.configureVertexArray(renderer.audioRayVAO, renderer.audioRayVBO, kRayVertexLayout, 0, {});
            renderBackend.unbindVertexArray();
        }

        std::vector<RayVertex> verts;
        verts.reserve(rtAudio.debugSegments.size() * 2);
        for (const auto& seg : rtAudio.debugSegments) {
            verts.push_back({seg.from, seg.color});
            verts.push_back({seg.to, seg.color});
        }

        renderer.audioRayVertexCount = static_cast<int>(verts.size());
        renderer.audioRayVoxelCount = 0;
        const void* vertexData = verts.empty() ? nullptr : verts.data();
        renderBackend.uploadArrayBufferData(renderer.audioRayVBO, vertexData, verts.size() * sizeof(RayVertex), true);
    }
}
