#pragma once

#include <GLFW/glfw3.h>
#include <chrono>
#include <iostream>
#include <vector>

namespace RenderInitSystemLogic {
    int getRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback);
    bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback);
}
namespace VoxelMeshingSystemLogic {
    size_t GetGreedyInFlightCount();
    size_t GetGreedyQueueCount();
    void GetGreedyStats(size_t& queued, size_t& applied, size_t& dropped);
}

namespace VoxelMeshDebugSystemLogic {
    void UpdateVoxelMeshDebug(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow*) {
        if (!baseSystem.voxelWorld || !baseSystem.voxelGreedy) return;
        VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
        VoxelGreedyContext& voxelGreedy = *baseSystem.voxelGreedy;

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
            size_t queued = 0, applied = 0, dropped = 0;
            VoxelMeshingSystemLogic::GetGreedyStats(queued, applied, dropped);
            size_t inFlight = VoxelMeshingSystemLogic::GetGreedyInFlightCount();
            size_t queueSize = VoxelMeshingSystemLogic::GetGreedyQueueCount();
            size_t voxelRenderDirtyCount = baseSystem.voxelRender ? baseSystem.voxelRender->renderBuffersDirty.size() : 0;
            std::cout << "[VoxelPerf] dirty=" << voxelWorld.dirtySections.size()
                      << " greedyDirty=" << voxelGreedy.dirtySections.size()
                      << " greedyQueued=" << queued
                      << " greedyApplied=" << applied
                      << " greedyDropped=" << dropped
                      << " voxelRenderDirty=" << voxelRenderDirtyCount
                      << " sections=" << voxelWorld.sections.size()
                      << " greedyMeshes=" << voxelGreedy.chunks.size()
                      << " greedyInFlight=" << inFlight
                      << " greedyQueue=" << queueSize
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

        if (::RenderInitSystemLogic::getRegistryBool(baseSystem, "DebugVoxelMesh", false)) {
            static auto lastDebugLog = std::chrono::steady_clock::now();
            auto nowDbg = std::chrono::steady_clock::now();
            if (nowDbg - lastDebugLog >= std::chrono::seconds(1)) {
                size_t worldDirty = baseSystem.voxelWorld ? baseSystem.voxelWorld->dirtySections.size() : 0;
                size_t greedyDirty = baseSystem.voxelGreedy ? baseSystem.voxelGreedy->dirtySections.size() : 0;
                size_t greedyBufDirty = baseSystem.voxelGreedy ? baseSystem.voxelGreedy->renderBuffersDirty.size() : 0;
                size_t inFlight = VoxelMeshingSystemLogic::GetGreedyInFlightCount();
                size_t queued = VoxelMeshingSystemLogic::GetGreedyQueueCount();
                std::cout << "[DebugVoxelMesh] worldDirty=" << worldDirty
                          << " greedyDirty=" << greedyDirty
                          << " greedyBufDirty=" << greedyBufDirty
                          << " inFlight=" << inFlight
                          << " queued=" << queued
                          << std::endl;
                lastDebugLog = nowDbg;
            }
        }
    }
}
