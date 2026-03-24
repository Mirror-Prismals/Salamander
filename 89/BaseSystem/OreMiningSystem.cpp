#pragma once

#include "Host/PlatformInput.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace AudioSystemLogic { bool TriggerGameplaySfx(BaseSystem& baseSystem, const std::string& cueName, float gain); }
namespace GemSystemLogic {
    void SpawnGemDropFromOre(BaseSystem& baseSystem,
                             std::vector<Entity>& prototypes,
                             int removedPrototypeID,
                             const glm::vec3& blockPos,
                             const glm::vec3& playerForward);
}
namespace VoxelMeshingSystemLogic { void RequestPriorityVoxelRemesh(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const glm::ivec3& worldCell); }
namespace StructureCaptureSystemLogic { void NotifyBlockChanged(BaseSystem& baseSystem, int worldIndex, const glm::vec3& position); }
namespace BlockSelectionSystemLogic { void RemoveBlockFromCache(BaseSystem& baseSystem, const std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position); }

namespace OreMiningSystemLogic {
    namespace {
        constexpr int kGridSize = 24;
        constexpr int kGridCellCount = kGridSize * kGridSize;
        // Keep overlays one mini-voxel out from the face so they read like a
        // thin decal layer, not a detached block in air.
        constexpr float kRenderFaceOffset = 1.0f / 24.0f;
        constexpr float kHoverFaceOffset = (1.0f / 24.0f) + 0.003f;

        struct ColorVertex {
            glm::vec3 pos;
            glm::vec3 color;
        };

        struct SparkleFrame {
            bool valid = false;
            int width = 0;
            int height = 0;
            std::vector<glm::vec4> pixels;
        };

        struct SparkleState {
            bool active = false;
            int worldIndex = -1;
            glm::ivec3 cell = glm::ivec3(0);
            glm::vec3 faceNormal = glm::vec3(0.0f, 0.0f, 1.0f);
            int oreKind = 0;
            float timeRemaining = 0.0f;
        };

        struct MiningSessionState {
            bool active = false;
            int worldIndex = -1;
            glm::ivec3 cell = glm::ivec3(0);
            glm::vec3 blockPos = glm::vec3(0.0f);
            glm::vec3 faceNormal = glm::vec3(0.0f, 0.0f, 1.0f);
            glm::vec3 playerForward = glm::vec3(0.0f, 0.0f, -1.0f);
            int oreKind = 0;
            int maxClicks = 19;
            int clicksUsed = 0;
            int oreTotalCells = 0;
            int oreRevealedCells = 0;
            std::array<uint8_t, kGridCellCount> oreMask{};
            std::array<uint8_t, kGridCellCount> revealed{};
        };

        struct OreMiningState {
            RenderHandle vao = 0;
            RenderHandle vbo = 0;
            uint32_t rngState = 0x6A09E667u;
            SparkleState sparkle;
            MiningSessionState mining;
            bool haveLastPlayerPos = false;
            glm::vec3 lastPlayerPos = glm::vec3(0.0f);
            float stepAccumulator = 0.0f;
            std::array<SparkleFrame, 4> sparkleFrames{};
            bool sparkleFramesLoaded = false;
            RenderHandle sparkleAtlasTexture = 0;
            glm::ivec2 sparkleAtlasSize = glm::ivec2(0);
            glm::ivec2 sparkleTileSize = glm::ivec2(0);
            int sparkleTilesPerRow = 0;
            int sparkleTilesPerCol = 0;
        };

        OreMiningState& state() {
            static OreMiningState s;
            return s;
        }

        int gridIndex(int x, int y) {
            return y * kGridSize + x;
        }

        bool readRegistryBool(const BaseSystem& baseSystem, const char* key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            std::string v = std::get<std::string>(it->second);
            std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
            if (v == "0" || v == "false" || v == "no" || v == "off") return false;
            return fallback;
        }

        int readRegistryInt(const BaseSystem& baseSystem, const char* key, int fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stoi(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        float readRegistryFloat(const BaseSystem& baseSystem, const char* key, float fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stof(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        uint32_t hashU32(uint32_t x) {
            x ^= x >> 16u;
            x *= 0x7feb352du;
            x ^= x >> 15u;
            x *= 0x846ca68bu;
            x ^= x >> 16u;
            return x;
        }

        uint32_t nextRandU32(uint32_t& stateValue) {
            if (stateValue == 0u) stateValue = 0xA341316Cu;
            stateValue ^= (stateValue << 13u);
            stateValue ^= (stateValue >> 17u);
            stateValue ^= (stateValue << 5u);
            return stateValue;
        }

        float nextRand01(uint32_t& stateValue) {
            return static_cast<float>(nextRandU32(stateValue) & 0x00ffffffu) / static_cast<float>(0x01000000u);
        }

        int nextRandRangeInt(uint32_t& stateValue, int minValue, int maxValueInclusive) {
            if (maxValueInclusive <= minValue) return minValue;
            const int span = maxValueInclusive - minValue + 1;
            int idx = static_cast<int>(nextRand01(stateValue) * static_cast<float>(span));
            if (idx >= span) idx = span - 1;
            return minValue + idx;
        }

        float nextRandRange(float minValue, float maxValue, uint32_t& stateValue) {
            if (maxValue <= minValue) return minValue;
            return minValue + (maxValue - minValue) * nextRand01(stateValue);
        }

        int oreKindForPrototypeName(const std::string& name) {
            if (name == "RubyOreTex") return 0;
            if (name == "AmethystOreTex") return 1;
            if (name == "FlouriteOreTex" || name == "FluoriteOreTex") return 2;
            if (name == "SilverOreTex") return 3;
            return -1;
        }

        int oreKindForPrototype(const std::vector<Entity>& prototypes, int prototypeID) {
            if (prototypeID < 0 || prototypeID >= static_cast<int>(prototypes.size())) return -1;
            return oreKindForPrototypeName(prototypes[static_cast<size_t>(prototypeID)].name);
        }

        bool isStonePrototypeName(const std::string& name) {
            if (name == "StoneBlockTex") return true;
            if (name.rfind("WallStoneTex", 0) == 0) return true;
            if (name.rfind("CeilingStoneTex", 0) == 0) return true;
            if (name.rfind("DebugSlopeTex", 0) == 0) return true;
            return false;
        }

        bool isStonePrototypeID(const std::vector<Entity>& prototypes, int prototypeID) {
            if (prototypeID < 0 || prototypeID >= static_cast<int>(prototypes.size())) return false;
            return isStonePrototypeName(prototypes[static_cast<size_t>(prototypeID)].name);
        }

        bool isSparkleSpawnStonePrototypeID(const std::vector<Entity>& prototypes, int prototypeID) {
            if (prototypeID < 0 || prototypeID >= static_cast<int>(prototypes.size())) return false;
            return prototypes[static_cast<size_t>(prototypeID)].name == "StoneBlockTex";
        }

        glm::vec3 axisNormalFromVector(const glm::vec3& n) {
            if (glm::length(n) < 0.0001f) return glm::vec3(0.0f, 0.0f, 1.0f);
            const glm::vec3 an = glm::abs(n);
            if (an.x >= an.y && an.x >= an.z) return glm::vec3((n.x >= 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f);
            if (an.y >= an.x && an.y >= an.z) return glm::vec3(0.0f, (n.y >= 0.0f) ? 1.0f : -1.0f, 0.0f);
            return glm::vec3(0.0f, 0.0f, (n.z >= 0.0f) ? 1.0f : -1.0f);
        }

        bool sameAxisNormal(const glm::vec3& a, const glm::vec3& b) {
            const glm::vec3 na = axisNormalFromVector(a);
            const glm::vec3 nb = axisNormalFromVector(b);
            return glm::dot(na, nb) > 0.999f;
        }

        void faceAxes(const glm::vec3& normal, glm::vec3& outU, glm::vec3& outV) {
            const glm::vec3 n = axisNormalFromVector(normal);
            if (n.x > 0.5f) {
                outU = glm::vec3(0.0f, 0.0f, 1.0f);
                outV = glm::vec3(0.0f, 1.0f, 0.0f);
                return;
            }
            if (n.x < -0.5f) {
                outU = glm::vec3(0.0f, 0.0f, -1.0f);
                outV = glm::vec3(0.0f, 1.0f, 0.0f);
                return;
            }
            if (n.y > 0.5f) {
                outU = glm::vec3(1.0f, 0.0f, 0.0f);
                outV = glm::vec3(0.0f, 0.0f, -1.0f);
                return;
            }
            if (n.y < -0.5f) {
                outU = glm::vec3(1.0f, 0.0f, 0.0f);
                outV = glm::vec3(0.0f, 0.0f, 1.0f);
                return;
            }
            if (n.z > 0.5f) {
                outU = glm::vec3(-1.0f, 0.0f, 0.0f);
                outV = glm::vec3(0.0f, 1.0f, 0.0f);
                return;
            }
            outU = glm::vec3(1.0f, 0.0f, 0.0f);
            outV = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        bool computeFaceCellFromHit(const glm::ivec3& cell,
                                    const glm::vec3& normal,
                                    const glm::vec3& hitPos,
                                    int& outX,
                                    int& outY) {
            glm::vec3 axisU(1.0f, 0.0f, 0.0f);
            glm::vec3 axisV(0.0f, 1.0f, 0.0f);
            faceAxes(normal, axisU, axisV);

            const glm::vec3 local = hitPos - glm::vec3(cell);
            float u = glm::dot(local, axisU) + 0.5f;
            float v = glm::dot(local, axisV) + 0.5f;
            u = glm::clamp(u, 0.0f, 0.999999f);
            v = glm::clamp(v, 0.0f, 0.999999f);
            outX = std::clamp(static_cast<int>(std::floor(u * static_cast<float>(kGridSize))), 0, kGridSize - 1);
            outY = std::clamp(static_cast<int>(std::floor(v * static_cast<float>(kGridSize))), 0, kGridSize - 1);
            return true;
        }

        void pushFaceCellQuad(std::vector<ColorVertex>& out,
                              const glm::ivec3& cell,
                              const glm::vec3& normal,
                              int gx,
                              int gy,
                              const glm::vec3& color,
                              float inset,
                              float offsetFromFace) {
            if (gx < 0 || gx >= kGridSize || gy < 0 || gy >= kGridSize) return;

            glm::vec3 axisU(1.0f, 0.0f, 0.0f);
            glm::vec3 axisV(0.0f, 1.0f, 0.0f);
            const glm::vec3 n = axisNormalFromVector(normal);
            faceAxes(n, axisU, axisV);

            const float step = 1.0f / static_cast<float>(kGridSize);
            float u0 = static_cast<float>(gx) * step + inset;
            float u1 = static_cast<float>(gx + 1) * step - inset;
            float v0 = static_cast<float>(gy) * step + inset;
            float v1 = static_cast<float>(gy + 1) * step - inset;
            if (u1 <= u0 || v1 <= v0) return;

            const glm::vec3 center = glm::vec3(cell) + n * (0.5f + offsetFromFace);
            const glm::vec3 p00 = center + axisU * (u0 - 0.5f) + axisV * (v0 - 0.5f);
            const glm::vec3 p10 = center + axisU * (u1 - 0.5f) + axisV * (v0 - 0.5f);
            const glm::vec3 p11 = center + axisU * (u1 - 0.5f) + axisV * (v1 - 0.5f);
            const glm::vec3 p01 = center + axisU * (u0 - 0.5f) + axisV * (v1 - 0.5f);

            out.push_back({p00, color});
            out.push_back({p10, color});
            out.push_back({p11, color});
            out.push_back({p00, color});
            out.push_back({p11, color});
            out.push_back({p01, color});
        }

        void clearMiningSession(MiningSessionState& m) {
            m.active = false;
            m.worldIndex = -1;
            m.cell = glm::ivec3(0);
            m.blockPos = glm::vec3(0.0f);
            m.faceNormal = glm::vec3(0.0f, 0.0f, 1.0f);
            m.playerForward = glm::vec3(0.0f, 0.0f, -1.0f);
            m.oreKind = 0;
            m.maxClicks = 19;
            m.clicksUsed = 0;
            m.oreTotalCells = 0;
            m.oreRevealedCells = 0;
            m.oreMask.fill(0u);
            m.revealed.fill(0u);
        }

        int revealCellIfHidden(MiningSessionState& m, int x, int y) {
            if (x < 0 || x >= kGridSize || y < 0 || y >= kGridSize) return 0;
            const int idx = gridIndex(x, y);
            if (m.revealed[static_cast<size_t>(idx)] != 0u) return 0;
            m.revealed[static_cast<size_t>(idx)] = 1u;
            if (m.oreMask[static_cast<size_t>(idx)] != 0u) {
                m.oreRevealedCells += 1;
            }
            return 1;
        }

        int revealCrossAtCell(MiningSessionState& m, int centerX, int centerY) {
            constexpr std::array<glm::ivec2, 5> kCrossOffsets = {
                glm::ivec2(0, 0),
                glm::ivec2(1, 0),
                glm::ivec2(-1, 0),
                glm::ivec2(0, 1),
                glm::ivec2(0, -1)
            };
            int revealedCount = 0;
            for (const glm::ivec2& offset : kCrossOffsets) {
                revealedCount += revealCellIfHidden(m, centerX + offset.x, centerY + offset.y);
            }
            return revealedCount;
        }

        void generateOreMask(uint32_t& rngState, MiningSessionState& m) {
            std::array<uint8_t, kGridCellCount> localMask{};
            localMask.fill(0u);

            float sizeT = nextRand01(rngState);
            int baseCount = 5;
            if (sizeT >= 0.78f) baseCount = 14;
            else if (sizeT >= 0.56f) baseCount = 11;
            else if (sizeT >= 0.34f) baseCount = 8;
            int targetCount = std::clamp(baseCount + nextRandRangeInt(rngState, -1, 1), 4, 16);

            std::vector<glm::ivec2> cells;
            cells.reserve(static_cast<size_t>(targetCount));
            cells.push_back(glm::ivec2(12, 12));
            localMask[gridIndex(12, 12)] = 1u;

            constexpr std::array<glm::ivec2, 4> kDirs = {
                glm::ivec2(1, 0), glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(0, -1)
            };

            int guard = 0;
            while (static_cast<int>(cells.size()) < targetCount && guard < 2048) {
                ++guard;
                const int pick = nextRandRangeInt(rngState, 0, static_cast<int>(cells.size()) - 1);
                const glm::ivec2 origin = cells[static_cast<size_t>(pick)];
                const glm::ivec2 dir = kDirs[static_cast<size_t>(nextRandRangeInt(rngState, 0, 3))];
                glm::ivec2 next = origin + dir;
                if (next.x < 2 || next.x >= (kGridSize - 2) || next.y < 2 || next.y >= (kGridSize - 2)) continue;
                const int idx = gridIndex(next.x, next.y);
                if (localMask[static_cast<size_t>(idx)] != 0u) continue;
                localMask[static_cast<size_t>(idx)] = 1u;
                cells.push_back(next);
            }

            if (cells.empty()) {
                localMask[gridIndex(12, 12)] = 1u;
                cells.push_back(glm::ivec2(12, 12));
            }

            std::array<uint8_t, kGridCellCount> rotated{};
            rotated.fill(0u);
            const int turns = nextRandRangeInt(rngState, 0, 3);
            for (int y = 0; y < kGridSize; ++y) {
                for (int x = 0; x < kGridSize; ++x) {
                    if (localMask[static_cast<size_t>(gridIndex(x, y))] == 0u) continue;
                    int rx = x;
                    int ry = y;
                    for (int i = 0; i < turns; ++i) {
                        const int nx = kGridSize - 1 - ry;
                        const int ny = rx;
                        rx = nx;
                        ry = ny;
                    }
                    rotated[static_cast<size_t>(gridIndex(rx, ry))] = 1u;
                }
            }

            int minX = kGridSize - 1;
            int maxX = 0;
            int minY = kGridSize - 1;
            int maxY = 0;
            for (int y = 0; y < kGridSize; ++y) {
                for (int x = 0; x < kGridSize; ++x) {
                    if (rotated[static_cast<size_t>(gridIndex(x, y))] == 0u) continue;
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }

            int shiftX = 0;
            int shiftY = 0;
            if (maxX >= minX && maxY >= minY) {
                const int minShiftX = -minX;
                const int maxShiftX = (kGridSize - 1) - maxX;
                const int minShiftY = -minY;
                const int maxShiftY = (kGridSize - 1) - maxY;
                shiftX = nextRandRangeInt(rngState, minShiftX, maxShiftX);
                shiftY = nextRandRangeInt(rngState, minShiftY, maxShiftY);
            }

            m.oreMask.fill(0u);
            m.oreTotalCells = 0;
            for (int y = 0; y < kGridSize; ++y) {
                for (int x = 0; x < kGridSize; ++x) {
                    if (rotated[static_cast<size_t>(gridIndex(x, y))] == 0u) continue;
                    int fx = x + shiftX;
                    int fy = y + shiftY;
                    if (fx < 0 || fx >= kGridSize || fy < 0 || fy >= kGridSize) continue;
                    const int outIdx = gridIndex(fx, fy);
                    m.oreMask[static_cast<size_t>(outIdx)] = 1u;
                    m.oreTotalCells += 1;
                }
            }

            if (m.oreTotalCells <= 0) {
                const int fallbackIdx = gridIndex(12, 12);
                m.oreMask[static_cast<size_t>(fallbackIdx)] = 1u;
                m.oreTotalCells = 1;
            }
        }

        bool removeBlockAtCell(BaseSystem& baseSystem,
                               std::vector<Entity>& prototypes,
                               int worldIndex,
                               const glm::ivec3& cell,
                               const glm::vec3& blockPos) {
            if (!baseSystem.level) return false;
            LevelContext& level = *baseSystem.level;

            int notifyWorldIndex = worldIndex;
            if (notifyWorldIndex < 0 || notifyWorldIndex >= static_cast<int>(level.worlds.size())) {
                notifyWorldIndex = level.activeWorldIndex;
            }

            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
                if (id != 0u) {
                    baseSystem.voxelWorld->setBlockWorld(cell, 0u, 0u);
                    VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, cell);
                    if (notifyWorldIndex >= 0 && notifyWorldIndex < static_cast<int>(level.worlds.size())) {
                        StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, notifyWorldIndex, glm::vec3(cell));
                        BlockSelectionSystemLogic::RemoveBlockFromCache(baseSystem, prototypes, notifyWorldIndex, glm::vec3(cell));
                    }
                    return true;
                }
            }

            if (notifyWorldIndex < 0 || notifyWorldIndex >= static_cast<int>(level.worlds.size())) return false;
            Entity& world = level.worlds[static_cast<size_t>(notifyWorldIndex)];
            constexpr float kPickEpsilon = 0.05f;
            for (size_t i = 0; i < world.instances.size(); ++i) {
                const EntityInstance& inst = world.instances[i];
                if (glm::distance(inst.position, blockPos) > kPickEpsilon) continue;
                world.instances[i] = world.instances.back();
                world.instances.pop_back();
                StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, notifyWorldIndex, blockPos);
                BlockSelectionSystemLogic::RemoveBlockFromCache(baseSystem, prototypes, notifyWorldIndex, blockPos);
                return true;
            }

            return false;
        }

        int findOrePrototypeIDForKind(const std::vector<Entity>& prototypes, int oreKind) {
            auto pickByName = [&](const char* nameA, const char* nameB = nullptr) -> int {
                for (const Entity& proto : prototypes) {
                    if (proto.name == nameA || (nameB && proto.name == nameB)) return proto.prototypeID;
                }
                return -1;
            };
            switch (oreKind) {
                case 0: return pickByName("RubyOreTex");
                case 1: return pickByName("AmethystOreTex");
                case 2: return pickByName("FlouriteOreTex", "FluoriteOreTex");
                case 3: return pickByName("SilverOreTex");
                default: return -1;
            }
        }

        int resolveMappedAtlasTileIndex(const WorldContext& world, const char* textureKey) {
            if (!textureKey) return -1;
            auto it = world.atlasMappings.find(textureKey);
            if (it == world.atlasMappings.end()) return -1;
            if (it->second.all >= 0) return it->second.all;
            if (it->second.side >= 0) return it->second.side;
            if (it->second.top >= 0) return it->second.top;
            if (it->second.bottom >= 0) return it->second.bottom;
            return -1;
        }

        bool loadSparkleFrameFromAtlasTile(const std::vector<unsigned char>& atlasPixels,
                                           const RendererContext& renderer,
                                           int tileIndex,
                                           SparkleFrame& outFrame) {
            if (atlasPixels.empty()
                || tileIndex < 0
                || renderer.atlasTextureSize.x <= 0
                || renderer.atlasTextureSize.y <= 0
                || renderer.atlasTileSize.x <= 0
                || renderer.atlasTileSize.y <= 0
                || renderer.atlasTilesPerRow <= 0
                || renderer.atlasTilesPerCol <= 0) return false;

            const int tileW = renderer.atlasTileSize.x;
            const int tileH = renderer.atlasTileSize.y;
            const int atlasW = renderer.atlasTextureSize.x;
            const int atlasH = renderer.atlasTextureSize.y;

            const int tileX = (tileIndex % renderer.atlasTilesPerRow) * tileW;
            const int tileRowFromTop = (tileIndex / renderer.atlasTilesPerRow);
            const int tileRowFromBottom = (renderer.atlasTilesPerCol - 1 - tileRowFromTop);
            const int tileY = tileRowFromBottom * tileH;
            if (tileX < 0 || tileY < 0 || tileX + tileW > atlasW || tileY + tileH > atlasH) return false;

            outFrame.valid = true;
            outFrame.width = tileW;
            outFrame.height = tileH;
            outFrame.pixels.assign(static_cast<size_t>(tileW * tileH), glm::vec4(0.0f));
            for (int y = 0; y < tileH; ++y) {
                for (int x = 0; x < tileW; ++x) {
                    const size_t idx = static_cast<size_t>(((tileY + y) * atlasW + (tileX + x)) * 4);
                    const glm::vec4 texel(
                        static_cast<float>(atlasPixels[idx + 0]) / 255.0f,
                        static_cast<float>(atlasPixels[idx + 1]) / 255.0f,
                        static_cast<float>(atlasPixels[idx + 2]) / 255.0f,
                        static_cast<float>(atlasPixels[idx + 3]) / 255.0f
                    );
                    outFrame.pixels[static_cast<size_t>(y * tileW + x)] = texel;
                }
            }
            return true;
        }

        void buildFallbackSparkleFrame(int frameIndex, SparkleFrame& outFrame) {
            outFrame.valid = true;
            outFrame.width = kGridSize;
            outFrame.height = kGridSize;
            outFrame.pixels.assign(static_cast<size_t>(kGridCellCount), glm::vec4(0.0f));
            const int cx = 5 + frameIndex * 5;
            const int cy = 7 + ((frameIndex * 7) % 10);
            for (int y = 0; y < kGridSize; ++y) {
                for (int x = 0; x < kGridSize; ++x) {
                    const int dx = std::abs(x - cx);
                    const int dy = std::abs(y - cy);
                    const bool star = (dx == 0 && dy <= 2) || (dy == 0 && dx <= 2) || (dx == 1 && dy == 1);
                    if (!star) continue;
                    glm::vec4 c(1.0f, 1.0f, 1.0f, 0.0f);
                    if (dx == 0 && dy == 0) c = glm::vec4(1.0f, 0.98f, 0.70f, 1.0f);
                    else c = glm::vec4(0.90f, 0.96f, 1.0f, 0.80f);
                    outFrame.pixels[static_cast<size_t>(y * kGridSize + x)] = c;
                }
            }
        }

        void ensureSparkleFramesLoaded(const BaseSystem& baseSystem) {
            OreMiningState& s = state();
            const RendererContext* renderer = baseSystem.renderer ? baseSystem.renderer.get() : nullptr;
            const WorldContext* world = baseSystem.world ? baseSystem.world.get() : nullptr;
            const bool atlasReady = renderer
                && world
                && renderer->atlasTexture != 0
                && renderer->atlasTextureSize.x > 0
                && renderer->atlasTextureSize.y > 0
                && renderer->atlasTileSize.x > 0
                && renderer->atlasTileSize.y > 0
                && renderer->atlasTilesPerRow > 0
                && renderer->atlasTilesPerCol > 0;

            const bool cacheMatches = atlasReady
                && s.sparkleFramesLoaded
                && s.sparkleAtlasTexture == renderer->atlasTexture
                && s.sparkleAtlasSize == renderer->atlasTextureSize
                && s.sparkleTileSize == renderer->atlasTileSize
                && s.sparkleTilesPerRow == renderer->atlasTilesPerRow
                && s.sparkleTilesPerCol == renderer->atlasTilesPerCol;
            if (cacheMatches) return;

            s.sparkleFramesLoaded = false;
            for (SparkleFrame& frame : s.sparkleFrames) frame = SparkleFrame{};

            const std::array<const char*, 4> textureKeys = {
                "24x24MiningGemSparkleRgbaV001",
                "24x24MiningGemSparkleRgbaV002",
                "24x24MiningGemSparkleRgbaV003",
                "24x24MiningGemSparkleRgbaV004"
            };

            bool loadedAny = false;
            if (atlasReady && baseSystem.renderBackend) {
                std::vector<unsigned char> atlasPixels;
                const bool readbackOk = baseSystem.renderBackend->readTexture2DRgba(
                    renderer->atlasTexture,
                    renderer->atlasTextureSize.x,
                    renderer->atlasTextureSize.y,
                    atlasPixels
                );
                if (readbackOk) {
                    for (int i = 0; i < static_cast<int>(textureKeys.size()); ++i) {
                        const int tileIndex = resolveMappedAtlasTileIndex(*world, textureKeys[static_cast<size_t>(i)]);
                        SparkleFrame frame;
                        if (loadSparkleFrameFromAtlasTile(atlasPixels, *renderer, tileIndex, frame)) {
                            s.sparkleFrames[static_cast<size_t>(i)] = std::move(frame);
                            loadedAny = true;
                        }
                    }
                }
            }

            for (int i = 0; i < static_cast<int>(s.sparkleFrames.size()); ++i) {
                SparkleFrame& frame = s.sparkleFrames[static_cast<size_t>(i)];
                if (!frame.valid) {
                    buildFallbackSparkleFrame(i, frame);
                }
            }

            if (atlasReady && loadedAny) {
                s.sparkleAtlasTexture = renderer->atlasTexture;
                s.sparkleAtlasSize = renderer->atlasTextureSize;
                s.sparkleTileSize = renderer->atlasTileSize;
                s.sparkleTilesPerRow = renderer->atlasTilesPerRow;
                s.sparkleTilesPerCol = renderer->atlasTilesPerCol;
            } else {
                s.sparkleAtlasTexture = 0;
                s.sparkleAtlasSize = glm::ivec2(0);
                s.sparkleTileSize = glm::ivec2(0);
                s.sparkleTilesPerRow = 0;
                s.sparkleTilesPerCol = 0;
            }

            s.sparkleFramesLoaded = true;
        }

        glm::vec4 sampleSparklePixel(const SparkleFrame& frame, int x, int y) {
            if (!frame.valid || frame.width <= 0 || frame.height <= 0 || frame.pixels.empty()) {
                return glm::vec4(0.0f);
            }
            int sx = x;
            int sy = y;
            if (frame.width != kGridSize || frame.height != kGridSize) {
                sx = std::clamp(static_cast<int>((static_cast<float>(x) / static_cast<float>(kGridSize)) * static_cast<float>(frame.width)), 0, frame.width - 1);
                sy = std::clamp(static_cast<int>((static_cast<float>(y) / static_cast<float>(kGridSize)) * static_cast<float>(frame.height)), 0, frame.height - 1);
            }
            sx = std::clamp(sx, 0, frame.width - 1);
            sy = std::clamp(sy, 0, frame.height - 1);
            return frame.pixels[static_cast<size_t>(sy * frame.width + sx)];
        }

        void orePalette(int kind, glm::vec3& startColor, glm::vec3& endColor) {
            switch (kind) {
                case 0:
                    startColor = glm::vec3(0.62f, 0.04f, 0.07f);
                    endColor = glm::vec3(1.00f, 0.20f, 0.22f);
                    return;
                case 1:
                    startColor = glm::vec3(0.36f, 0.12f, 0.62f);
                    endColor = glm::vec3(0.73f, 0.56f, 0.94f);
                    return;
                case 2:
                    startColor = glm::vec3(0.10f, 0.28f, 0.78f);
                    endColor = glm::vec3(0.36f, 0.70f, 1.00f);
                    return;
                case 3:
                default:
                    startColor = glm::vec3(0.78f, 0.78f, 0.82f);
                    endColor = glm::vec3(0.98f, 0.98f, 0.99f);
                    return;
            }
        }

        bool blockStillMineable(const BaseSystem& baseSystem,
                                const std::vector<Entity>& prototypes,
                                const glm::ivec3& cell) {
            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
                if (id == 0 || id >= prototypes.size()) return false;
                const int protoID = static_cast<int>(id);
                return isStonePrototypeID(prototypes, protoID) || (oreKindForPrototype(prototypes, protoID) >= 0);
            }
            return false;
        }

        bool chooseExposedFaceNormal(const BaseSystem& baseSystem,
                                     const glm::ivec3& cell,
                                     uint32_t& rng,
                                     glm::vec3& outNormal) {
            if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return false;
            const std::array<glm::vec3, 6> normals = {
                glm::vec3(1.0f, 0.0f, 0.0f),
                glm::vec3(-1.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f),
                glm::vec3(0.0f, -1.0f, 0.0f),
                glm::vec3(0.0f, 0.0f, 1.0f),
                glm::vec3(0.0f, 0.0f, -1.0f)
            };
            std::array<glm::vec3, 6> candidates{};
            int count = 0;
            for (const glm::vec3& n : normals) {
                const glm::ivec3 neighbor = cell + glm::ivec3(glm::round(n));
                if (baseSystem.voxelWorld->getBlockWorld(neighbor) != 0u) continue;
                candidates[static_cast<size_t>(count)] = n;
                ++count;
            }
            if (count <= 0) return false;
            const int idx = nextRandRangeInt(rng, 0, count - 1);
            outNormal = candidates[static_cast<size_t>(idx)];
            return true;
        }

        void maybeSpawnSparkle(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt) {
            OreMiningState& s = state();
            if (!baseSystem.player) return;
            if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return;
            if (s.sparkle.active || s.mining.active) return;

            PlayerContext& player = *baseSystem.player;
            const glm::vec3 playerPos = player.cameraPosition;
            const glm::vec2 playerXZ(playerPos.x, playerPos.z);

            if (!s.haveLastPlayerPos) {
                s.haveLastPlayerPos = true;
                s.lastPlayerPos = playerPos;
                return;
            }

            const glm::vec2 lastXZ(s.lastPlayerPos.x, s.lastPlayerPos.z);
            const float moved = glm::length(playerXZ - lastXZ);
            s.lastPlayerPos = playerPos;
            s.stepAccumulator += std::max(0.0f, moved);

            const float stepDistance = glm::clamp(readRegistryFloat(baseSystem, "OreSparkleStepDistance", 0.85f), 0.1f, 6.0f);
            const float spawnChance = glm::clamp(readRegistryFloat(baseSystem, "OreSparkleStepChance", 0.28f), 0.0f, 1.0f);
            const int minRadius = std::max(2, readRegistryInt(baseSystem, "OreSparkleMinRadius", 6));
            const int maxRadius = std::max(minRadius + 1, readRegistryInt(baseSystem, "OreSparkleMaxRadius", 24));
            const int verticalRange = std::max(1, readRegistryInt(baseSystem, "OreSparkleVerticalRange", 8));
            const int spawnAttempts = std::max(8, readRegistryInt(baseSystem, "OreSparkleSpawnAttempts", 96));

            while (s.stepAccumulator >= stepDistance) {
                s.stepAccumulator -= stepDistance;
                if (nextRand01(s.rngState) > spawnChance) continue;

                for (int attempt = 0; attempt < spawnAttempts; ++attempt) {
                    const float radius = nextRandRange(static_cast<float>(minRadius), static_cast<float>(maxRadius), s.rngState);
                    const float angle = nextRandRange(0.0f, 6.28318530718f, s.rngState);
                    const float ox = std::cos(angle) * radius;
                    const float oz = std::sin(angle) * radius;
                    const int oy = nextRandRangeInt(s.rngState, -verticalRange, verticalRange);
                    const glm::ivec3 cell = glm::ivec3(glm::round(playerPos + glm::vec3(ox, static_cast<float>(oy), oz)));

                    const uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
                    if (id == 0u || id >= prototypes.size()) continue;
                    if (!isSparkleSpawnStonePrototypeID(prototypes, static_cast<int>(id))) continue;

                    glm::vec3 faceNormal(0.0f);
                    if (!chooseExposedFaceNormal(baseSystem, cell, s.rngState, faceNormal)) continue;

                    SparkleState sparkle;
                    sparkle.active = true;
                    sparkle.worldIndex = (baseSystem.level && baseSystem.level->activeWorldIndex >= 0)
                        ? baseSystem.level->activeWorldIndex
                        : 0;
                    sparkle.cell = cell;
                    sparkle.faceNormal = faceNormal;
                    sparkle.oreKind = nextRandRangeInt(s.rngState, 0, 3);
                    sparkle.timeRemaining = glm::clamp(readRegistryFloat(baseSystem, "OreSparkleDuration", 10.0f), 1.0f, 60.0f);
                    s.sparkle = sparkle;
                    return;
                }
            }

            (void)dt;
        }

        void updateSparkleLifetime(float dt) {
            OreMiningState& s = state();
            if (!s.sparkle.active) return;
            s.sparkle.timeRemaining = std::max(0.0f, s.sparkle.timeRemaining - std::max(0.0f, dt));
            if (s.sparkle.timeRemaining <= 0.0f) {
                s.sparkle.active = false;
            }
        }

        bool isMiningTarget(const BaseSystem& baseSystem,
                            const SparkleState& sparkle,
                            int worldIndex,
                            const glm::ivec3& cell,
                            const glm::vec3& faceNormal) {
            if (!sparkle.active) return false;
            if (sparkle.worldIndex != worldIndex) return false;
            if (sparkle.cell != cell) return false;
            return sameAxisNormal(sparkle.faceNormal, faceNormal)
                || (baseSystem.player && glm::length(baseSystem.player->targetedBlockNormal) < 0.001f);
        }

        void ensureRenderResources(RendererContext& renderer, WorldContext& world, OreMiningState& s, IRenderBackend& renderBackend) {
            if (!renderer.audioRayShader
                && world.shaders.count("AUDIORAY_VERTEX_SHADER")
                && world.shaders.count("AUDIORAY_FRAGMENT_SHADER")) {
                renderer.audioRayShader = std::make_unique<Shader>(
                    world.shaders["AUDIORAY_VERTEX_SHADER"].c_str(),
                    world.shaders["AUDIORAY_FRAGMENT_SHADER"].c_str());
            }
            static const std::vector<VertexAttribLayout> kColorVertexLayout = {
                {0, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(ColorVertex)), offsetof(ColorVertex, pos), 0},
                {1, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(ColorVertex)), offsetof(ColorVertex, color), 0}
            };
            renderBackend.ensureVertexArray(s.vao);
            renderBackend.ensureArrayBuffer(s.vbo);
            renderBackend.configureVertexArray(s.vao, s.vbo, kColorVertexLayout, 0, {});
        }

        void appendSparkleOverlay(const BaseSystem& baseSystem,
                                  std::vector<ColorVertex>& out,
                                  const SparkleState& sparkle,
                                  float timeSeconds) {
            OreMiningState& s = state();
            ensureSparkleFramesLoaded(baseSystem);
            const int frame = static_cast<int>(std::floor(timeSeconds * 10.0f)) & 3;
            const SparkleFrame& sparkleFrame = s.sparkleFrames[static_cast<size_t>(frame)];

            for (int y = 0; y < kGridSize; ++y) {
                for (int x = 0; x < kGridSize; ++x) {
                    const glm::vec4 texel = sampleSparklePixel(sparkleFrame, x, y);
                    if (texel.a <= 0.03f) continue;
                    float pulse = 0.65f + 0.35f * std::sin(timeSeconds * 7.5f + static_cast<float>(x * 13 + y * 7));
                    glm::vec3 color = glm::clamp(glm::vec3(texel) * (0.45f + 0.55f * pulse), glm::vec3(0.0f), glm::vec3(1.0f));
                    pushFaceCellQuad(out, sparkle.cell, sparkle.faceNormal, x, y, color, 0.001f, kRenderFaceOffset);
                }
            }
        }

        void appendMiningOverlay(const BaseSystem& baseSystem,
                                 std::vector<ColorVertex>& out,
                                 const MiningSessionState& mining,
                                 float timeSeconds) {
            OreMiningState& s = state();
            ensureSparkleFramesLoaded(baseSystem);
            const int frame = static_cast<int>(std::floor(timeSeconds * 10.0f)) & 3;
            const SparkleFrame& sparkleFrame = s.sparkleFrames[static_cast<size_t>(frame)];
            glm::vec3 oreStart(1.0f);
            glm::vec3 oreEnd(1.0f);
            orePalette(mining.oreKind, oreStart, oreEnd);

            for (int y = 0; y < kGridSize; ++y) {
                for (int x = 0; x < kGridSize; ++x) {
                    const int idx = gridIndex(x, y);
                    if (mining.revealed[static_cast<size_t>(idx)] == 0u) continue;

                    glm::vec3 color(0.19f, 0.19f, 0.19f);
                    if (mining.oreMask[static_cast<size_t>(idx)] != 0u) {
                        const glm::vec4 texel = sampleSparklePixel(sparkleFrame, x, y);
                        const glm::vec3 sparkleColor = (texel.a > 0.03f)
                            ? glm::vec3(texel)
                            : glm::vec3(0.85f);
                        const float t = static_cast<float>(y) / static_cast<float>(kGridSize - 1);
                        const glm::vec3 oreTint = glm::mix(oreStart, oreEnd, t);
                        color = glm::clamp(sparkleColor * oreTint * 1.12f, glm::vec3(0.0f), glm::vec3(1.0f));
                    }

                    pushFaceCellQuad(out, mining.cell, mining.faceNormal, x, y, color, 0.0005f, kRenderFaceOffset);
                }
            }
        }

        bool getHoveredFaceCell(const BaseSystem& baseSystem,
                                const glm::ivec3& cell,
                                const glm::vec3& faceNormal,
                                int& outX,
                                int& outY) {
            if (!baseSystem.player) return false;
            const PlayerContext& player = *baseSystem.player;
            if (!player.hasBlockTarget) return false;
            if (glm::ivec3(glm::round(player.targetedBlockPosition)) != cell) return false;
            if (!sameAxisNormal(player.targetedBlockNormal, faceNormal)) return false;
            return computeFaceCellFromHit(cell, faceNormal, player.targetedBlockHitPosition, outX, outY);
        }
    } // namespace

    bool IsMiningActive(const BaseSystem& baseSystem) {
        (void)baseSystem;
        // Mining is now fully in-world and should not lock camera or keyboard input.
        return false;
    }

    bool StartOreMiningFromBlock(BaseSystem& baseSystem,
                                 std::vector<Entity>& prototypes,
                                 int worldIndex,
                                 const glm::ivec3& cell,
                                 int targetPrototypeID,
                                 const glm::vec3& blockPos,
                                 const glm::vec3& playerForward) {
        OreMiningState& s = state();
        if (!readRegistryBool(baseSystem, "OreMiningSystem", true)) return false;
        if (!readRegistryBool(baseSystem, "OreMiningEnabled", true)) return false;
        if (!baseSystem.player) return false;

        const glm::vec3 faceNormal = axisNormalFromVector(baseSystem.player->targetedBlockNormal);

        const bool targetIsOre = (oreKindForPrototype(prototypes, targetPrototypeID) >= 0);
        const bool targetIsStone = isStonePrototypeID(prototypes, targetPrototypeID);
        if (!targetIsOre && !targetIsStone) return false;

        const bool sessionMatches = s.mining.active
            && s.mining.worldIndex == worldIndex
            && s.mining.cell == cell
            && sameAxisNormal(s.mining.faceNormal, faceNormal);

        if (!sessionMatches) {
            int oreKind = -1;
            if (isMiningTarget(baseSystem, s.sparkle, worldIndex, cell, faceNormal)) {
                oreKind = s.sparkle.oreKind;
                s.sparkle.active = false;
            } else if (targetIsOre) {
                oreKind = oreKindForPrototype(prototypes, targetPrototypeID);
            }

            if (oreKind < 0) return false;

            clearMiningSession(s.mining);
            s.mining.active = true;
            s.mining.worldIndex = worldIndex;
            s.mining.cell = cell;
            s.mining.blockPos = blockPos;
            s.mining.faceNormal = faceNormal;
            s.mining.playerForward = (glm::length(playerForward) > 0.001f)
                ? glm::normalize(playerForward)
                : glm::vec3(0.0f, 0.0f, -1.0f);
            s.mining.oreKind = oreKind;

            const uint32_t seed = hashU32(
                static_cast<uint32_t>(cell.x * 73856093)
                ^ static_cast<uint32_t>(cell.y * 19349663)
                ^ static_cast<uint32_t>(cell.z * 83492791)
                ^ static_cast<uint32_t>((worldIndex + 19) * 2654435761u)
                ^ static_cast<uint32_t>(baseSystem.frameIndex));
            uint32_t localRng = (seed != 0u) ? seed : 0xD17F46A3u;
            const int baseMaxClicks = std::max(1, readRegistryInt(baseSystem, "OreMiningMaxClicks", 19));
            const int bonusMaxClicks = std::clamp(readRegistryInt(baseSystem, "OreMiningBonusClicksMax", 6), 0, 64);
            const int bonusClicks = (bonusMaxClicks > 0)
                ? nextRandRangeInt(localRng, 0, bonusMaxClicks)
                : 0;
            s.mining.maxClicks = baseMaxClicks + bonusClicks;
            s.mining.clicksUsed = 0;
            s.mining.oreRevealedCells = 0;
            s.mining.revealed.fill(0u);
            generateOreMask(localRng, s.mining);
            s.rngState = localRng;
        }

        if (!s.mining.active) return false;

        int cellX = 0;
        int cellY = 0;
        if (!computeFaceCellFromHit(cell, s.mining.faceNormal, baseSystem.player->targetedBlockHitPosition, cellX, cellY)) {
            return true;
        }

        const int idx = gridIndex(cellX, cellY);
        if (s.mining.revealed[static_cast<size_t>(idx)] == 0u) {
            revealCrossAtCell(s.mining, cellX, cellY);
            s.mining.clicksUsed += 1;
            AudioSystemLogic::TriggerGameplaySfx(baseSystem, "break_stone.ck", 1.0f);
        }

        if (s.mining.oreRevealedCells >= s.mining.oreTotalCells && s.mining.oreTotalCells > 0) {
            const bool removed = removeBlockAtCell(baseSystem, prototypes, worldIndex, s.mining.cell, s.mining.blockPos);
            if (removed) {
                const int rewardProtoID = findOrePrototypeIDForKind(prototypes, s.mining.oreKind);
                if (rewardProtoID >= 0) {
                    GemSystemLogic::SpawnGemDropFromOre(baseSystem,
                                                        prototypes,
                                                        rewardProtoID,
                                                        s.mining.blockPos,
                                                        s.mining.playerForward);
                }
            }
            clearMiningSession(s.mining);
            return true;
        }

        if (s.mining.clicksUsed >= s.mining.maxClicks) {
            AudioSystemLogic::TriggerGameplaySfx(baseSystem, "earthquake.ck", 1.0f);
            clearMiningSession(s.mining);
            return true;
        }

        return true;
    }

    void UpdateOreMining(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)win;
        OreMiningState& s = state();

        if (!readRegistryBool(baseSystem, "OreMiningSystem", true)
            || !readRegistryBool(baseSystem, "OreMiningEnabled", true)) {
            s.sparkle.active = false;
            clearMiningSession(s.mining);
            return;
        }

        updateSparkleLifetime(dt);
        maybeSpawnSparkle(baseSystem, prototypes, dt);

        if (s.sparkle.active && !blockStillMineable(baseSystem, prototypes, s.sparkle.cell)) {
            s.sparkle.active = false;
        }

        if (s.mining.active && !blockStillMineable(baseSystem, prototypes, s.mining.cell)) {
            clearMiningSession(s.mining);
        }
    }

    void RenderOreMining(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)prototypes;
        (void)dt;
        (void)win;

        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.player || !baseSystem.renderBackend) return;
        if (!readRegistryBool(baseSystem, "OreMiningSystem", true)
            || !readRegistryBool(baseSystem, "OreMiningEnabled", true)) {
            return;
        }

        OreMiningState& s = state();
        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        PlayerContext& player = *baseSystem.player;
        auto& renderBackend = *baseSystem.renderBackend;

        ensureRenderResources(renderer, world, s, renderBackend);
        if (!renderer.audioRayShader || s.vao == 0 || s.vbo == 0) return;

        std::vector<ColorVertex> fillVerts;
        fillVerts.reserve(7000u);

        const float t = static_cast<float>(PlatformInput::GetTimeSeconds());
        if (s.sparkle.active) {
            appendSparkleOverlay(baseSystem, fillVerts, s.sparkle, t);
        }
        if (s.mining.active) {
            appendMiningOverlay(baseSystem, fillVerts, s.mining, t);
        }

        int hoverX = 0;
        int hoverY = 0;
        if (s.mining.active
            && getHoveredFaceCell(baseSystem, s.mining.cell, s.mining.faceNormal, hoverX, hoverY)) {
            pushFaceCellQuad(fillVerts,
                             s.mining.cell,
                             s.mining.faceNormal,
                             hoverX,
                             hoverY,
                             glm::vec3(0.06f, 0.06f, 0.06f),
                             0.001f,
                             kHoverFaceOffset);
        } else if (s.sparkle.active
            && getHoveredFaceCell(baseSystem, s.sparkle.cell, s.sparkle.faceNormal, hoverX, hoverY)) {
            pushFaceCellQuad(fillVerts,
                             s.sparkle.cell,
                             s.sparkle.faceNormal,
                             hoverX,
                             hoverY,
                             glm::vec3(0.06f, 0.06f, 0.06f),
                             0.001f,
                             kHoverFaceOffset);
        }

        if (fillVerts.empty()) return;

        auto setDepthTestEnabled = [&](bool enabled) {
            renderBackend.setDepthTestEnabled(enabled);
        };
        auto setBlendEnabled = [&](bool enabled) {
            renderBackend.setBlendEnabled(enabled);
        };
        auto setBlendModeAlpha = [&]() {
            renderBackend.setBlendModeAlpha();
        };

        setDepthTestEnabled(true);
        setBlendEnabled(true);
        setBlendModeAlpha();

        renderer.audioRayShader->use();
        renderer.audioRayShader->setMat4("view", player.viewMatrix);
        renderer.audioRayShader->setMat4("projection", player.projectionMatrix);

        renderBackend.bindVertexArray(s.vao);
        renderBackend.uploadArrayBufferData(s.vbo, fillVerts.data(), fillVerts.size() * sizeof(ColorVertex), true);
        renderBackend.drawArraysTriangles(0, static_cast<int>(fillVerts.size()));
    }
}
