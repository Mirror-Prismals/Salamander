#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <glm/glm.hpp>

namespace VoxelColumnModel {

struct ColumnKey {
    int lod = 0;
    glm::ivec2 coord{0};

    bool operator==(const ColumnKey& other) const noexcept {
        return lod == other.lod && coord == other.coord;
    }
};

struct ColumnKeyHash {
    std::size_t operator()(const ColumnKey& key) const noexcept {
        const std::size_t hl = std::hash<int>()(key.lod);
        const std::size_t hx = std::hash<int>()(key.coord.x);
        const std::size_t hz = std::hash<int>()(key.coord.y);
        return hl ^ (hx << 1) ^ (hz << 2);
    }
};

struct BandKey {
    ColumnKey column;
    int sy = 0;

    bool operator==(const BandKey& other) const noexcept {
        return sy == other.sy && column == other.column;
    }
};

struct BandKeyHash {
    std::size_t operator()(const BandKey& key) const noexcept {
        const std::size_t hc = ColumnKeyHash{}(key.column);
        const std::size_t hy = std::hash<int>()(key.sy);
        return hc ^ (hy << 1);
    }
};

struct BandStorage {
    int sy = 0;
    int sizeX = 0;
    int sizeY = 0;
    int sizeZ = 0;

    std::vector<uint32_t> ids;
    std::vector<uint32_t> colors;

    int nonAirCount = 0;
    uint32_t terrainVersion = 0;
    uint32_t foliageVersion = 0;
    bool terrainDirty = false;
    bool foliageDirty = false;
};

struct ColumnStorage {
    ColumnKey key;
    int sizeX = 0;
    int sizeZ = 0;
    int bandHeight = 16;

    std::unordered_map<int, BandStorage> bands;

    std::vector<int16_t> heightmap;
    std::vector<int16_t> waterTop;
    std::vector<uint8_t> biomeCache;

    bool surfaceReady = false;
    bool foliageReady = false;
};

struct ColumnTerrainJob {
    enum class Phase : uint8_t {
        Surface = 0,
        NearSubsurface = 1,
        Deep = 2,
        Done = 3
    };

    Phase phase = Phase::Surface;
    int nextBandSy = 0;
    bool wroteAny = false;
};

struct ColumnFoliageJob {
    bool queued = false;
    bool treesDone = false;
    bool grassDone = false;
};

struct ColumnWorldContext {
    int sectionSize = 32;
    int baseBandHeight = 16;
    int maxLod = 2;
    bool enabled = false;

    std::unordered_map<ColumnKey, ColumnStorage, ColumnKeyHash> columns;
    std::unordered_set<BandKey, BandKeyHash> dirtyBands;

    std::unordered_set<ColumnKey, ColumnKeyHash> desiredColumns;
    std::vector<ColumnKey> pendingColumns;
    std::unordered_set<ColumnKey, ColumnKeyHash> pendingColumnSet;

    std::unordered_map<ColumnKey, ColumnTerrainJob, ColumnKeyHash> terrainJobs;
    std::unordered_map<ColumnKey, ColumnFoliageJob, ColumnKeyHash> foliageJobs;

    void reset() {
        columns.clear();
        dirtyBands.clear();
        desiredColumns.clear();
        pendingColumns.clear();
        pendingColumnSet.clear();
        terrainJobs.clear();
        foliageJobs.clear();
    }
};

inline int FloorDivInt(int value, int divisor) {
    if (divisor <= 0) return 0;
    if (value >= 0) return value / divisor;
    return -(((-value) + divisor - 1) / divisor);
}

inline int ColumnSpanForLod(int baseSectionSize, int lod) {
    const int size = baseSectionSize >> lod;
    return size > 0 ? size : 1;
}

inline int BandHeightForLod(int baseBandHeight, int lod) {
    const int h = baseBandHeight >> lod;
    return h > 0 ? h : 1;
}

inline ColumnKey MakeColumnKeyFromLodCoord(int lod,
                                           const glm::ivec3& lodCoord,
                                           int baseSectionSize) {
    const int span = ColumnSpanForLod(baseSectionSize, lod);
    return ColumnKey{
        lod,
        glm::ivec2(
            FloorDivInt(lodCoord.x, span),
            FloorDivInt(lodCoord.z, span)
        )
    };
}

inline BandKey MakeBandKeyFromLodCoord(int lod,
                                       const glm::ivec3& lodCoord,
                                       int baseSectionSize,
                                       int baseBandHeight) {
    const ColumnKey column = MakeColumnKeyFromLodCoord(lod, lodCoord, baseSectionSize);
    const int bandH = BandHeightForLod(baseBandHeight, lod);
    return BandKey{column, FloorDivInt(lodCoord.y, bandH)};
}

inline int BandIndex(const glm::ivec3& local, int sizeX, int sizeY, int sizeZ) {
    if (local.x < 0 || local.y < 0 || local.z < 0) return -1;
    if (local.x >= sizeX || local.y >= sizeY || local.z >= sizeZ) return -1;
    return local.x + local.y * sizeX + local.z * sizeX * sizeY;
}

} // namespace VoxelColumnModel

