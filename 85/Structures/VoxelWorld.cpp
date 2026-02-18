#pragma once

#include "Structures/VoxelWorld.h"
#include <array>
#include <algorithm>

namespace {
    int floorDivInt(int value, int divisor) {
        if (divisor <= 0) return 0;
        if (value >= 0) return value / divisor;
        return -(((-value) + divisor - 1) / divisor);
    }

    glm::ivec3 floorDivVec(const glm::ivec3& v, int divisor) {
        return glm::ivec3(
            floorDivInt(v.x, divisor),
            floorDivInt(v.y, divisor),
            floorDivInt(v.z, divisor)
        );
    }

    int sectionSizeForLod(int baseSize, int lod) {
        int size = baseSize >> lod;
        return size > 0 ? size : 1;
    }

    int voxelIndex(const glm::ivec3& local, int size) {
        return local.x + local.y * size + local.z * size * size;
    }

    struct MipSample {
        uint32_t id = 0;
        uint32_t color = 0;
    };

    MipSample pickMipVoxel(const std::array<uint32_t, 8>& ids,
                           const std::array<uint32_t, 8>& colors) {
        uint32_t best = 0;
        int bestCount = 0;
        std::array<uint32_t, 8> seen{};
        std::array<int, 8> counts{};
        int seenCount = 0;

        for (uint32_t v : ids) {
            if (v == 0) continue;
            int idx = -1;
            for (int i = 0; i < seenCount; ++i) {
                if (seen[i] == v) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0 && seenCount < static_cast<int>(seen.size())) {
                idx = seenCount++;
                seen[idx] = v;
                counts[idx] = 0;
            }
            if (idx >= 0) {
                counts[idx] += 1;
                if (counts[idx] > bestCount) {
                    bestCount = counts[idx];
                    best = v;
                }
            }
        }
        if (best == 0) return {};
        for (int i = 0; i < 8; ++i) {
            if (ids[i] == best) {
                return {best, colors[i]};
            }
        }
        return {best, 0};
    }
}

void VoxelWorldContext::reset() {
    sections.clear();
    dirtySections.clear();
}

namespace {
    VoxelSectionBuffers acquireBuffers(VoxelWorldContext& world, int size) {
        VoxelSectionBuffers buffers;
        auto it = world.bufferPools.find(size);
        if (it != world.bufferPools.end() && !it->second.empty()) {
            buffers = std::move(it->second.back());
            it->second.pop_back();
        }
        const size_t count = static_cast<size_t>(size * size * size);
        if (buffers.ids.size() != count) {
            buffers.ids.assign(count, 0);
        } else {
            std::fill(buffers.ids.begin(), buffers.ids.end(), 0);
        }
        if (buffers.colors.size() != count) {
            buffers.colors.assign(count, 0);
        } else {
            std::fill(buffers.colors.begin(), buffers.colors.end(), 0);
        }
        return buffers;
    }

    void releaseBuffers(VoxelWorldContext& world, int size, VoxelSectionBuffers&& buffers) {
        world.bufferPools[size].push_back(std::move(buffers));
    }
}

uint32_t VoxelWorldContext::getBlockWorld(const glm::ivec3& worldPos) const {
    int lod = 0;
    int size = sectionSizeForLod(sectionSize, lod);
    glm::ivec3 sectionCoord = floorDivVec(worldPos, size);
    glm::ivec3 local = worldPos - sectionCoord * size;
    VoxelSectionKey key{lod, sectionCoord};
    auto it = sections.find(key);
    if (it == sections.end()) return 0;
    const VoxelSection& section = it->second;
    int idx = voxelIndex(local, section.size);
    if (idx < 0 || idx >= static_cast<int>(section.ids.size())) return 0;
    return section.ids[idx];
}

uint32_t VoxelWorldContext::getColorWorld(const glm::ivec3& worldPos) const {
    int lod = 0;
    int size = sectionSizeForLod(sectionSize, lod);
    glm::ivec3 sectionCoord = floorDivVec(worldPos, size);
    glm::ivec3 local = worldPos - sectionCoord * size;
    VoxelSectionKey key{lod, sectionCoord};
    auto it = sections.find(key);
    if (it == sections.end()) return 0;
    const VoxelSection& section = it->second;
    int idx = voxelIndex(local, section.size);
    if (idx < 0 || idx >= static_cast<int>(section.colors.size())) return 0;
    return section.colors[idx];
}

void VoxelWorldContext::setBlockWorld(const glm::ivec3& worldPos, uint32_t id, uint32_t color) {
    int lod = 0;
    int size = sectionSizeForLod(sectionSize, lod);
    glm::ivec3 sectionCoord = floorDivVec(worldPos, size);
    glm::ivec3 local = worldPos - sectionCoord * size;
    VoxelSectionKey key{lod, sectionCoord};

    auto it = sections.find(key);
    if (it == sections.end()) {
        if (id == 0) return;
        VoxelSection section;
        section.lod = lod;
        section.size = size;
        section.coord = sectionCoord;
        VoxelSectionBuffers buffers = acquireBuffers(*this, size);
        section.ids = std::move(buffers.ids);
        section.colors = std::move(buffers.colors);
        auto [insertedIt, _] = sections.emplace(key, std::move(section));
        it = insertedIt;
    }
    VoxelSection& section = it->second;

    int idx = voxelIndex(local, section.size);
    uint32_t oldId = section.ids[idx];
    uint32_t oldColor = section.colors[idx];
    if (oldId == id && oldColor == color) return;
    section.ids[idx] = id;
    section.colors[idx] = (id == 0) ? 0 : color;
    if (oldId == 0 && id != 0) section.nonAirCount += 1;
    if (oldId != 0 && id == 0) section.nonAirCount -= 1;
    section.editVersion += 1;
    section.dirty = true;
    dirtySections.insert(key);

    for (int parentLod = 1; parentLod <= maxLod; ++parentLod) {
        int parentSize = sectionSizeForLod(sectionSize, parentLod);
        glm::ivec3 parentCoord = floorDivVec(worldPos, 1 << parentLod);
        glm::ivec3 parentSectionCoord = floorDivVec(parentCoord, parentSize);
        glm::ivec3 parentLocal = parentCoord - parentSectionCoord * parentSize;
        VoxelSectionKey parentKey{parentLod, parentSectionCoord};

        std::array<uint32_t, 8> samples{};
        std::array<uint32_t, 8> sampleColors{};
        int s = 0;
        glm::ivec3 childBase = parentCoord * 2;
        for (int dz = 0; dz < 2; ++dz) {
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    glm::ivec3 childCoord = childBase + glm::ivec3(dx, dy, dz);
                    int childSize = sectionSizeForLod(sectionSize, parentLod - 1);
                    glm::ivec3 childSectionCoord = floorDivVec(childCoord, childSize);
                    glm::ivec3 childLocal = childCoord - childSectionCoord * childSize;
                    VoxelSectionKey childKey{parentLod - 1, childSectionCoord};
                    auto childIt = sections.find(childKey);
                    if (childIt == sections.end()) {
                        samples[s++] = 0;
                        sampleColors[s - 1] = 0;
                        continue;
                    }
                    const VoxelSection& childSection = childIt->second;
                    int childIdx = voxelIndex(childLocal, childSection.size);
                    if (childIdx < 0 || childIdx >= static_cast<int>(childSection.ids.size())) {
                        samples[s++] = 0;
                        sampleColors[s - 1] = 0;
                        continue;
                    }
                    samples[s] = childSection.ids[childIdx];
                    sampleColors[s] = childSection.colors[childIdx];
                    s += 1;
                }
            }
        }
        MipSample mip = pickMipVoxel(samples, sampleColors);
        uint32_t newId = mip.id;
        uint32_t newColor = mip.color;

        auto parentIt = sections.find(parentKey);
        if (parentIt == sections.end()) {
            if (newId == 0) break;
            VoxelSection parentSection;
            parentSection.lod = parentLod;
            parentSection.size = parentSize;
            parentSection.coord = parentSectionCoord;
            parentSection.ids.assign(static_cast<size_t>(parentSize * parentSize * parentSize), 0);
            parentSection.colors.assign(static_cast<size_t>(parentSize * parentSize * parentSize), 0);
            parentSection.nonAirCount = 0;
            auto [insertedIt, _] = sections.emplace(parentKey, std::move(parentSection));
            parentIt = insertedIt;
        }

        VoxelSection& parentSection = parentIt->second;
        int parentIdx = voxelIndex(parentLocal, parentSection.size);
        uint32_t parentOldId = parentSection.ids[parentIdx];
        if (parentOldId == newId) break;
        parentSection.ids[parentIdx] = newId;
        parentSection.colors[parentIdx] = (newId == 0) ? 0 : newColor;
        if (parentOldId == 0 && newId != 0) parentSection.nonAirCount += 1;
        if (parentOldId != 0 && newId == 0) parentSection.nonAirCount -= 1;
        parentSection.dirty = true;
        dirtySections.insert(parentKey);
    }
}

void VoxelWorldContext::setBlockLod(int lod, const glm::ivec3& lodCoord, uint32_t id, uint32_t color, bool markDirty) {
    if (lod < 0) return;
    int size = sectionSizeForLod(sectionSize, lod);
    glm::ivec3 sectionCoord = floorDivVec(lodCoord, size);
    glm::ivec3 local = lodCoord - sectionCoord * size;
    VoxelSectionKey key{lod, sectionCoord};

    auto it = sections.find(key);
    if (it == sections.end()) {
        if (id == 0) return;
        VoxelSection section;
        section.lod = lod;
        section.size = size;
        section.coord = sectionCoord;
        VoxelSectionBuffers buffers = acquireBuffers(*this, size);
        section.ids = std::move(buffers.ids);
        section.colors = std::move(buffers.colors);
        auto [insertedIt, _] = sections.emplace(key, std::move(section));
        it = insertedIt;
    }

    VoxelSection& section = it->second;
    int idx = voxelIndex(local, section.size);
    uint32_t oldId = section.ids[idx];
    uint32_t oldColor = section.colors[idx];
    if (oldId == id && oldColor == color) return;
    section.ids[idx] = id;
    section.colors[idx] = (id == 0) ? 0 : color;
    if (oldId == 0 && id != 0) section.nonAirCount += 1;
    if (oldId != 0 && id == 0) section.nonAirCount -= 1;
    if (markDirty) {
        section.editVersion += 1;
        section.dirty = true;
        dirtySections.insert(key);
    }

    if (section.nonAirCount <= 0) {
        releaseSection(key);
    }
}

void VoxelWorldContext::releaseSection(const VoxelSectionKey& key) {
    auto it = sections.find(key);
    if (it == sections.end()) return;
    VoxelSectionBuffers buffers;
    buffers.ids = std::move(it->second.ids);
    buffers.colors = std::move(it->second.colors);
    releaseBuffers(*this, it->second.size, std::move(buffers));
    sections.erase(it);
    dirtySections.erase(key);
}
