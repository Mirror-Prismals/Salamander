#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <glm/glm.hpp>

struct VoxelSectionKey {
    int lod = 0;
    glm::ivec3 coord{0};
    bool operator==(const VoxelSectionKey& other) const noexcept {
        return lod == other.lod && coord == other.coord;
    }
};

struct VoxelSectionKeyHash {
    std::size_t operator()(const VoxelSectionKey& key) const noexcept {
        std::size_t hl = std::hash<int>()(key.lod);
        std::size_t hx = std::hash<int>()(key.coord.x);
        std::size_t hy = std::hash<int>()(key.coord.y);
        std::size_t hz = std::hash<int>()(key.coord.z);
        return hl ^ (hx << 1) ^ (hy << 2) ^ (hz << 3);
    }
};

struct VoxelSection {
    int lod = 0;
    int size = 0;
    glm::ivec3 coord{0};
    std::vector<uint32_t> ids;
    std::vector<uint32_t> colors;
    int nonAirCount = 0;
    uint32_t editVersion = 0;
    bool dirty = false;
};

struct VoxelSectionBuffers {
    std::vector<uint32_t> ids;
    std::vector<uint32_t> colors;
};

struct VoxelWorldContext {
    int sectionSize = 128;
    int maxLod = 4;
    bool enabled = false;
    std::unordered_map<VoxelSectionKey, VoxelSection, VoxelSectionKeyHash> sections;
    std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> dirtySections;
    std::unordered_map<int, std::vector<VoxelSectionBuffers>> bufferPools;

    void reset();
    uint32_t getBlockWorld(const glm::ivec3& worldPos) const;
    uint32_t getColorWorld(const glm::ivec3& worldPos) const;
    void setBlockWorld(const glm::ivec3& worldPos, uint32_t id, uint32_t color);
    void setBlockLod(int lod, const glm::ivec3& lodCoord, uint32_t id, uint32_t color, bool markDirty = true);
    void releaseSection(const VoxelSectionKey& key);
};
