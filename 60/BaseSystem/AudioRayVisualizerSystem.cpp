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
        if (!renderer.audioRayVoxelShader) {
            if (world.shaders.count("AUDIORAY_VOXEL_VERTEX_SHADER") && world.shaders.count("AUDIORAY_VOXEL_FRAGMENT_SHADER")) {
                renderer.audioRayVoxelShader = std::make_unique<Shader>(world.shaders["AUDIORAY_VOXEL_VERTEX_SHADER"].c_str(), world.shaders["AUDIORAY_VOXEL_FRAGMENT_SHADER"].c_str());
            } else if (!shaderWarned) {
                shaderWarned = true;
                std::cerr << "AudioRayVisualizer: voxel shader sources not found in procedures.glsl\n";
            }
        }

        if (renderer.audioRayVAO == 0) {
            glGenVertexArrays(1, &renderer.audioRayVAO);
            glGenBuffers(1, &renderer.audioRayVBO);
            glBindVertexArray(renderer.audioRayVAO);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.audioRayVBO);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
            glBindVertexArray(0);
        }

        if (renderer.audioRayVoxelVAO == 0) {
            glGenVertexArrays(1, &renderer.audioRayVoxelVAO);
            glGenBuffers(1, &renderer.audioRayVoxelInstanceVBO);
            glBindVertexArray(renderer.audioRayVoxelVAO);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.cubeVBO);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.audioRayVoxelInstanceVBO);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); // position
            glVertexAttribDivisor(2, 1);
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); // gain
            glVertexAttribDivisor(3, 1);
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(4 * sizeof(float))); // occluded flag
            glVertexAttribDivisor(4, 1);
            glBindVertexArray(0);
        }

        // Lines disabled; only voxels for heatmap.
        renderer.audioRayVertexCount = 0;

        // Build voxel instances (positions + gain/occluded).
        if (!renderer.audioRayVoxelShader || renderer.audioRayVoxelVAO == 0) return;
        const size_t maxVoxels = 1024;
        struct VData { glm::vec3 pos; float gain; float occluded; };
        std::vector<VData> inst;
        inst.reserve(std::min(rtAudio.debugVoxels.size(), maxVoxels));
        for (const auto& v : rtAudio.debugVoxels) {
            if (inst.size() >= maxVoxels) break;
            VData d;
            d.pos = v.pos;
            d.gain = glm::clamp(v.gain, 0.0f, 1.0f);
            d.occluded = v.occluded ? 1.0f : 0.0f;
            inst.push_back(d);
        }
        renderer.audioRayVoxelCount = static_cast<int>(inst.size());
        glBindBuffer(GL_ARRAY_BUFFER, renderer.audioRayVoxelInstanceVBO);
        glBufferData(GL_ARRAY_BUFFER, inst.size() * sizeof(VData), inst.data(), GL_DYNAMIC_DRAW);
    }
}
