#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <glad/glad.h>

// Forward declarations
struct BaseSystem;
struct Entity;

namespace AudioRayVisualizerSystemLogic {

    void UpdateAudioRayVisualizer(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.rayTracedAudio) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        RayTracedAudioContext& rtAudio = *baseSystem.rayTracedAudio;

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
            glGenVertexArrays(1, &renderer.audioRayVAO);
            glGenBuffers(1, &renderer.audioRayVBO);
            glBindVertexArray(renderer.audioRayVAO);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.audioRayVBO);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
            glBindVertexArray(0);
        }

        struct RayVertex { glm::vec3 pos; glm::vec3 color; };
        std::vector<RayVertex> verts;
        verts.reserve(rtAudio.debugSegments.size() * 2);
        for (const auto& seg : rtAudio.debugSegments) {
            verts.push_back({seg.from, seg.color});
            verts.push_back({seg.to, seg.color});
        }

        renderer.audioRayVertexCount = static_cast<int>(verts.size());
        renderer.audioRayVoxelCount = 0;
        glBindBuffer(GL_ARRAY_BUFFER, renderer.audioRayVBO);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(RayVertex), verts.data(), GL_DYNAMIC_DRAW);
    }
}
