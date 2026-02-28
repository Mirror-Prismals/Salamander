#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <vector>

namespace VoxelMeshInitSystemLogic {
    glm::vec3 UnpackColor(uint32_t packed) {
        float r = static_cast<float>((packed >> 16) & 0xff) / 255.0f;
        float g = static_cast<float>((packed >> 8) & 0xff) / 255.0f;
        float b = static_cast<float>(packed & 0xff) / 255.0f;
        return glm::vec3(r, g, b);
    }

    int FloorDivInt(int value, int divisor) {
        if (divisor <= 0) return 0;
        if (value >= 0) return value / divisor;
        return -(((-value) + divisor - 1) / divisor);
    }

    int SectionSizeForLod(const VoxelWorldContext& voxelWorld, int lod) {
        int size = voxelWorld.sectionSize >> lod;
        return size > 0 ? size : 1;
    }

    uint32_t GetVoxelIdAtLod(const VoxelWorldContext& voxelWorld,
                             int lod,
                             const glm::ivec3& coord) {
        int size = SectionSizeForLod(voxelWorld, lod);
        glm::ivec3 sectionCoord(
            FloorDivInt(coord.x, size),
            FloorDivInt(coord.y, size),
            FloorDivInt(coord.z, size)
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

    uint32_t GetVoxelColorAtLod(const VoxelWorldContext& voxelWorld,
                                int lod,
                                const glm::ivec3& coord) {
        int size = SectionSizeForLod(voxelWorld, lod);
        glm::ivec3 sectionCoord(
            FloorDivInt(coord.x, size),
            FloorDivInt(coord.y, size),
            FloorDivInt(coord.z, size)
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

    glm::ivec3 LocalCellFromUV(int faceType, int slice, int u, int v) {
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

    glm::ivec3 FaceNormal(int faceType) {
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

    void UpdateVoxelMeshInit(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow*) {
        if (!baseSystem.voxelWorld) return;
    }
}
