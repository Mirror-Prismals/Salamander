#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
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
        constexpr float kPickEpsilon = 0.05f;

        struct UiColorVertex {
            glm::vec2 pos;
            glm::vec3 color;
        };

        struct OreMiningState {
            bool active = false;
            bool cursorCaptured = false;
            bool awaitingInitialRelease = false;
            bool lastLeftDown = false;
            bool lastEscapeDown = false;
            int worldIndex = -1;
            glm::ivec3 cell = glm::ivec3(0);
            glm::vec3 blockPos = glm::vec3(0.0f);
            glm::vec3 playerForward = glm::vec3(0.0f, 0.0f, -1.0f);
            int orePrototypeID = -1;
            int oreKind = -1;
            int maxClicks = 19;
            int clicksUsed = 0;
            int oreTotalCells = 0;
            int oreRevealedCells = 0;
            std::array<uint8_t, kGridCellCount> oreMask{};
            std::array<uint8_t, kGridCellCount> revealed{};
            std::array<glm::vec3, kGridCellCount> hiddenColors{};
            std::array<glm::vec3, kGridCellCount> revealBaseColors{};
            std::array<glm::vec3, kGridCellCount> revealOreColors{};
            GLuint vao = 0;
            GLuint vbo = 0;
            bool atlasValid = false;
            GLuint atlasTexture = 0;
            int atlasWidth = 0;
            int atlasHeight = 0;
            int tilesPerRow = 0;
            int tilesPerCol = 0;
            glm::ivec2 tileSize = glm::ivec2(24, 24);
            std::vector<unsigned char> atlasPixels;
            uint32_t rngState = 0xD17F46A3u;
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

        uint32_t hashU32(uint32_t x) {
            x ^= x >> 16u;
            x *= 0x7feb352du;
            x ^= x >> 15u;
            x *= 0x846ca68bu;
            x ^= x >> 16u;
            return x;
        }

        uint32_t nextRandU32(uint32_t& stateValue) {
            if (stateValue == 0) stateValue = 0xA341316Cu;
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
            const float t = nextRand01(stateValue);
            int idx = static_cast<int>(t * static_cast<float>(span));
            if (idx >= span) idx = span - 1;
            return minValue + idx;
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

        void orePalette(int kind, glm::vec3& startColor, glm::vec3& endColor) {
            switch (kind) {
                case 0: // ruby
                    startColor = glm::vec3(0.62f, 0.04f, 0.07f);
                    endColor = glm::vec3(1.00f, 0.20f, 0.22f);
                    return;
                case 1: // amethyst
                    startColor = glm::vec3(0.36f, 0.12f, 0.62f);
                    endColor = glm::vec3(0.73f, 0.56f, 0.94f);
                    return;
                case 2: // flourite/fluorite
                    startColor = glm::vec3(0.10f, 0.28f, 0.78f);
                    endColor = glm::vec3(0.36f, 0.70f, 1.00f);
                    return;
                case 3: // silver
                default:
                    startColor = glm::vec3(0.78f, 0.78f, 0.82f);
                    endColor = glm::vec3(0.98f, 0.98f, 0.99f);
                    return;
            }
        }

        int tileIndexFromSet(const FaceTextureSet& set) {
            if (set.all >= 0) return set.all;
            if (set.side >= 0) return set.side;
            if (set.top >= 0) return set.top;
            if (set.bottom >= 0) return set.bottom;
            return -1;
        }

        int findAtlasTileForKey(const WorldContext* world, const char* key) {
            if (!world || !key) return -1;
            auto it = world->atlasMappings.find(key);
            if (it == world->atlasMappings.end()) return -1;
            return tileIndexFromSet(it->second);
        }

        bool ensureAtlasCache(BaseSystem& baseSystem) {
            OreMiningState& s = state();
            if (!baseSystem.renderer) return false;
            RendererContext& renderer = *baseSystem.renderer;
            if (renderer.atlasTexture == 0
                || renderer.atlasTextureSize.x <= 0
                || renderer.atlasTextureSize.y <= 0
                || renderer.atlasTilesPerRow <= 0
                || renderer.atlasTilesPerCol <= 0
                || renderer.atlasTileSize.x <= 0
                || renderer.atlasTileSize.y <= 0) {
                s.atlasValid = false;
                s.atlasPixels.clear();
                return false;
            }

            bool refresh = !s.atlasValid
                || s.atlasTexture != renderer.atlasTexture
                || s.atlasWidth != renderer.atlasTextureSize.x
                || s.atlasHeight != renderer.atlasTextureSize.y
                || s.tilesPerRow != renderer.atlasTilesPerRow
                || s.tilesPerCol != renderer.atlasTilesPerCol
                || s.tileSize != renderer.atlasTileSize;

            if (!refresh) return true;

            const size_t pixelCount = static_cast<size_t>(renderer.atlasTextureSize.x)
                * static_cast<size_t>(renderer.atlasTextureSize.y) * 4u;
            if (pixelCount == 0) {
                s.atlasValid = false;
                s.atlasPixels.clear();
                return false;
            }

            s.atlasPixels.assign(pixelCount, 0u);
            glBindTexture(GL_TEXTURE_2D, renderer.atlasTexture);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, s.atlasPixels.data());

            s.atlasTexture = renderer.atlasTexture;
            s.atlasWidth = renderer.atlasTextureSize.x;
            s.atlasHeight = renderer.atlasTextureSize.y;
            s.tilesPerRow = renderer.atlasTilesPerRow;
            s.tilesPerCol = renderer.atlasTilesPerCol;
            s.tileSize = renderer.atlasTileSize;
            s.atlasValid = true;
            return true;
        }

        glm::vec3 sampleTileColor(const OreMiningState& s,
                                  int tileIndex,
                                  int localX,
                                  int localY,
                                  const glm::vec3& fallback) {
            if (!s.atlasValid || s.atlasPixels.empty()) return fallback;
            if (tileIndex < 0 || s.tilesPerRow <= 0 || s.tilesPerCol <= 0 || s.tileSize.x <= 0 || s.tileSize.y <= 0) {
                return fallback;
            }
            const int tileCount = s.tilesPerRow * s.tilesPerCol;
            if (tileIndex < 0 || tileIndex >= tileCount) return fallback;

            const int tileW = s.tileSize.x;
            const int tileH = s.tileSize.y;
            const int tileX = (tileIndex % s.tilesPerRow) * tileW;
            const int tileRowTop = tileIndex / s.tilesPerRow;
            const int tileRowBottom = (s.tilesPerCol - 1 - tileRowTop);
            const int tileY = tileRowBottom * tileH;

            int px = std::clamp(localX, 0, tileW - 1);
            int py = std::clamp(tileH - 1 - localY, 0, tileH - 1);
            int tx = tileX + px;
            int ty = tileY + py;
            if (tx < 0 || tx >= s.atlasWidth || ty < 0 || ty >= s.atlasHeight) return fallback;

            const size_t idx = static_cast<size_t>((ty * s.atlasWidth + tx) * 4);
            if (idx + 2 >= s.atlasPixels.size()) return fallback;
            const float r = static_cast<float>(s.atlasPixels[idx + 0]) / 255.0f;
            const float g = static_cast<float>(s.atlasPixels[idx + 1]) / 255.0f;
            const float b = static_cast<float>(s.atlasPixels[idx + 2]) / 255.0f;
            return glm::vec3(r, g, b);
        }

        glm::vec2 pixelToNdc(float x, float y, float width, float height) {
            const float ndcX = (x / width) * 2.0f - 1.0f;
            const float ndcY = 1.0f - (y / height) * 2.0f;
            return glm::vec2(ndcX, ndcY);
        }

        void pushQuad(std::vector<UiColorVertex>& out,
                      float x0,
                      float y0,
                      float x1,
                      float y1,
                      const glm::vec3& color,
                      float width,
                      float height) {
            const glm::vec2 a = pixelToNdc(x0, y0, width, height);
            const glm::vec2 b = pixelToNdc(x1, y0, width, height);
            const glm::vec2 c = pixelToNdc(x1, y1, width, height);
            const glm::vec2 d = pixelToNdc(x0, y1, width, height);
            out.push_back({a, color});
            out.push_back({b, color});
            out.push_back({c, color});
            out.push_back({a, color});
            out.push_back({c, color});
            out.push_back({d, color});
        }

        void pushLine(std::vector<UiColorVertex>& out,
                      float x0,
                      float y0,
                      float x1,
                      float y1,
                      const glm::vec3& color,
                      float width,
                      float height) {
            out.push_back({pixelToNdc(x0, y0, width, height), color});
            out.push_back({pixelToNdc(x1, y1, width, height), color});
        }

        void clearSessionGrid(OreMiningState& s) {
            s.oreMask.fill(0u);
            s.revealed.fill(0u);
            s.hiddenColors.fill(glm::vec3(0.30f, 0.28f, 0.26f));
            s.revealBaseColors.fill(glm::vec3(0.38f, 0.35f, 0.32f));
            s.revealOreColors.fill(glm::vec3(0.82f, 0.22f, 0.20f));
            s.oreTotalCells = 0;
            s.oreRevealedCells = 0;
            s.clicksUsed = 0;
        }

        int revealCellIfHidden(OreMiningState& s, int x, int y) {
            if (x < 0 || x >= kGridSize || y < 0 || y >= kGridSize) return 0;
            const int idx = gridIndex(x, y);
            if (s.revealed[static_cast<size_t>(idx)] != 0u) return 0;
            s.revealed[static_cast<size_t>(idx)] = 1u;
            if (s.oreMask[static_cast<size_t>(idx)] != 0u) {
                s.oreRevealedCells += 1;
            }
            return 1;
        }

        int revealCrossAtCell(OreMiningState& s, int centerX, int centerY) {
            constexpr std::array<glm::ivec2, 5> kCrossOffsets = {
                glm::ivec2(0, 0),
                glm::ivec2(1, 0),
                glm::ivec2(-1, 0),
                glm::ivec2(0, 1),
                glm::ivec2(0, -1)
            };
            int revealedCount = 0;
            for (const glm::ivec2& offset : kCrossOffsets) {
                revealedCount += revealCellIfHidden(s, centerX + offset.x, centerY + offset.y);
            }
            return revealedCount;
        }

        void generateOreMask(OreMiningState& s) {
            std::array<uint8_t, kGridCellCount> localMask{};
            localMask.fill(0u);

            float sizeT = nextRand01(s.rngState);
            int baseCount = 5;
            if (sizeT >= 0.78f) baseCount = 14;
            else if (sizeT >= 0.56f) baseCount = 11;
            else if (sizeT >= 0.34f) baseCount = 8;
            int targetCount = std::clamp(baseCount + nextRandRangeInt(s.rngState, -1, 1), 4, 16);

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
                const int pick = nextRandRangeInt(s.rngState, 0, static_cast<int>(cells.size()) - 1);
                const glm::ivec2 origin = cells[static_cast<size_t>(pick)];
                const glm::ivec2 dir = kDirs[static_cast<size_t>(nextRandRangeInt(s.rngState, 0, 3))];
                glm::ivec2 next = origin + dir;
                if (next.x < 2 || next.x >= (kGridSize - 2) || next.y < 2 || next.y >= (kGridSize - 2)) {
                    continue;
                }
                const int idx = gridIndex(next.x, next.y);
                if (localMask[static_cast<size_t>(idx)] != 0u) continue;
                localMask[static_cast<size_t>(idx)] = 1u;
                cells.push_back(next);
            }

            if (cells.empty()) {
                localMask[gridIndex(12, 12)] = 1u;
                cells.push_back(glm::ivec2(12, 12));
            }

            int minX = kGridSize - 1;
            int maxX = 0;
            int minY = kGridSize - 1;
            int maxY = 0;
            for (const glm::ivec2& c : cells) {
                minX = std::min(minX, c.x);
                maxX = std::max(maxX, c.x);
                minY = std::min(minY, c.y);
                maxY = std::max(maxY, c.y);
            }

            std::array<uint8_t, kGridCellCount> rotated{};
            rotated.fill(0u);
            const int turns = nextRandRangeInt(s.rngState, 0, 3);
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

            minX = kGridSize - 1;
            maxX = 0;
            minY = kGridSize - 1;
            maxY = 0;
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
                shiftX = nextRandRangeInt(s.rngState, minShiftX, maxShiftX);
                shiftY = nextRandRangeInt(s.rngState, minShiftY, maxShiftY);
            }

            s.oreMask.fill(0u);
            s.oreTotalCells = 0;
            for (int y = 0; y < kGridSize; ++y) {
                for (int x = 0; x < kGridSize; ++x) {
                    if (rotated[static_cast<size_t>(gridIndex(x, y))] == 0u) continue;
                    int fx = x + shiftX;
                    int fy = y + shiftY;
                    if (fx < 0 || fx >= kGridSize || fy < 0 || fy >= kGridSize) continue;
                    const int outIdx = gridIndex(fx, fy);
                    s.oreMask[static_cast<size_t>(outIdx)] = 1u;
                    s.oreTotalCells += 1;
                }
            }

            if (s.oreTotalCells <= 0) {
                const int fallbackIdx = gridIndex(12, 12);
                s.oreMask[static_cast<size_t>(fallbackIdx)] = 1u;
                s.oreTotalCells = 1;
            }
        }

        void bakeGridColors(BaseSystem& baseSystem) {
            OreMiningState& s = state();
            const WorldContext* world = baseSystem.world ? baseSystem.world.get() : nullptr;
            int stoneTile = findAtlasTileForKey(world, "StoneExternal");
            if (stoneTile < 0) stoneTile = findAtlasTileForKey(world, "Stone");
            if (stoneTile < 0) stoneTile = 3;
            int cobbleTile = findAtlasTileForKey(world, "Cobblestone");
            if (cobbleTile < 0) cobbleTile = 6;
            glm::vec3 oreStart(0.84f, 0.22f, 0.20f);
            glm::vec3 oreEnd(0.98f, 0.42f, 0.32f);
            orePalette(s.oreKind, oreStart, oreEnd);

            int oreMinX = kGridSize - 1;
            int oreMaxX = 0;
            int oreMinY = kGridSize - 1;
            int oreMaxY = 0;
            for (int y = 0; y < kGridSize; ++y) {
                for (int x = 0; x < kGridSize; ++x) {
                    if (s.oreMask[static_cast<size_t>(gridIndex(x, y))] == 0u) continue;
                    oreMinX = std::min(oreMinX, x);
                    oreMaxX = std::max(oreMaxX, x);
                    oreMinY = std::min(oreMinY, y);
                    oreMaxY = std::max(oreMaxY, y);
                }
            }
            const int oreSpanX = std::max(1, oreMaxX - oreMinX + 1);
            const int oreSpanY = std::max(1, oreMaxY - oreMinY + 1);

            const bool atlasReady = ensureAtlasCache(baseSystem);
            for (int y = 0; y < kGridSize; ++y) {
                for (int x = 0; x < kGridSize; ++x) {
                    const int idx = gridIndex(x, y);
                    const float shadeJitter = 0.90f + 0.16f * nextRand01(s.rngState);
                    glm::vec3 hidden = glm::vec3(0.32f, 0.30f, 0.28f);
                    glm::vec3 base = glm::vec3(0.40f, 0.37f, 0.34f);
                    glm::vec3 ore = glm::vec3(0.84f, 0.22f, 0.20f);
                    if (atlasReady) {
                        hidden = sampleTileColor(s, stoneTile, x, y, hidden);
                        base = sampleTileColor(s, cobbleTile, x, y, base);
                    }
                    const float tx = glm::clamp(static_cast<float>(x - oreMinX) / static_cast<float>(oreSpanX), 0.0f, 1.0f);
                    const float ty = glm::clamp(static_cast<float>(y - oreMinY) / static_cast<float>(oreSpanY), 0.0f, 1.0f);
                    float t = glm::clamp((tx * 0.62f) + (ty * 0.38f), 0.0f, 1.0f);
                    t = glm::clamp(t + (nextRand01(s.rngState) - 0.5f) * 0.12f, 0.0f, 1.0f);
                    ore = glm::mix(oreStart, oreEnd, t);
                    s.hiddenColors[static_cast<size_t>(idx)] = glm::clamp(hidden * shadeJitter, glm::vec3(0.0f), glm::vec3(1.0f));
                    s.revealBaseColors[static_cast<size_t>(idx)] = glm::clamp(base * (0.92f + 0.12f * nextRand01(s.rngState)), glm::vec3(0.0f), glm::vec3(1.0f));
                    s.revealOreColors[static_cast<size_t>(idx)] = glm::clamp(ore * (0.88f + 0.24f * nextRand01(s.rngState)), glm::vec3(0.0f), glm::vec3(1.0f));
                }
            }
        }

        bool removeOreBlockAndMaybeReward(BaseSystem& baseSystem,
                                          std::vector<Entity>& prototypes,
                                          OreMiningState& s,
                                          bool grantReward) {
            if (!baseSystem.level) return false;
            LevelContext& level = *baseSystem.level;

            int removedPrototypeID = -1;
            bool removed = false;
            int notifyWorldIndex = s.worldIndex;
            if (notifyWorldIndex < 0 || notifyWorldIndex >= static_cast<int>(level.worlds.size())) {
                notifyWorldIndex = level.activeWorldIndex;
            }

            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t id = baseSystem.voxelWorld->getBlockWorld(s.cell);
                if (id != 0 && id < prototypes.size() && oreKindForPrototype(prototypes, static_cast<int>(id)) >= 0) {
                    removedPrototypeID = static_cast<int>(id);
                    baseSystem.voxelWorld->setBlockWorld(s.cell, 0, 0);
                    VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, s.cell);
                    if (notifyWorldIndex >= 0 && notifyWorldIndex < static_cast<int>(level.worlds.size())) {
                        StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, notifyWorldIndex, glm::vec3(s.cell));
                        BlockSelectionSystemLogic::RemoveBlockFromCache(baseSystem, prototypes, notifyWorldIndex, glm::vec3(s.cell));
                    }
                    removed = true;
                }
            }

            if (!removed && notifyWorldIndex >= 0 && notifyWorldIndex < static_cast<int>(level.worlds.size())) {
                Entity& world = level.worlds[static_cast<size_t>(notifyWorldIndex)];
                for (size_t i = 0; i < world.instances.size(); ++i) {
                    const EntityInstance& inst = world.instances[i];
                    if (glm::distance(inst.position, s.blockPos) > kPickEpsilon) continue;
                    if (oreKindForPrototype(prototypes, inst.prototypeID) < 0) continue;
                    removedPrototypeID = inst.prototypeID;
                    world.instances[i] = world.instances.back();
                    world.instances.pop_back();
                    StructureCaptureSystemLogic::NotifyBlockChanged(baseSystem, notifyWorldIndex, s.blockPos);
                    BlockSelectionSystemLogic::RemoveBlockFromCache(baseSystem, prototypes, notifyWorldIndex, s.blockPos);
                    removed = true;
                    break;
                }
            }

            if (!removed || removedPrototypeID < 0) return false;

            if (grantReward) {
                AudioSystemLogic::TriggerGameplaySfx(baseSystem, "break_stone.ck", 1.0f);
                GemSystemLogic::SpawnGemDropFromOre(baseSystem,
                                                    prototypes,
                                                    removedPrototypeID,
                                                    s.blockPos,
                                                    s.playerForward);
            }
            return true;
        }

        void endSession(BaseSystem& baseSystem, GLFWwindow* win, bool success, std::vector<Entity>& prototypes) {
            OreMiningState& s = state();
            if (!s.active) return;

            const bool grantReward = success && (s.oreRevealedCells >= s.oreTotalCells) && (s.oreTotalCells > 0);
            removeOreBlockAndMaybeReward(baseSystem, prototypes, s, grantReward);
            if (!success) {
                AudioSystemLogic::TriggerGameplaySfx(baseSystem, "earthquake.ck", 1.0f);
            }

            s.active = false;
            s.awaitingInitialRelease = false;
            s.lastEscapeDown = false;
            s.lastLeftDown = false;

            if (s.cursorCaptured && win && (!baseSystem.ui || !baseSystem.ui->active)) {
                glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                s.cursorCaptured = false;
                if (baseSystem.player) baseSystem.player->firstMouse = true;
            }
        }

        void updateCursorCapture(BaseSystem& baseSystem, GLFWwindow* win) {
            OreMiningState& s = state();
            if (!win) return;
            if (s.active && !s.cursorCaptured) {
                glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                s.cursorCaptured = true;
                if (baseSystem.player) baseSystem.player->firstMouse = true;
            } else if (!s.active && s.cursorCaptured && (!baseSystem.ui || !baseSystem.ui->active)) {
                glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                s.cursorCaptured = false;
                if (baseSystem.player) baseSystem.player->firstMouse = true;
            }
        }

        void ensureRenderResources(RendererContext& renderer, WorldContext& world, OreMiningState& s) {
            if (!renderer.uiColorShader) {
                renderer.uiColorShader = std::make_unique<Shader>(
                    world.shaders["UI_COLOR_VERTEX_SHADER"].c_str(),
                    world.shaders["UI_COLOR_FRAGMENT_SHADER"].c_str());
            }
            if (s.vao == 0) {
                glGenVertexArrays(1, &s.vao);
            }
            if (s.vbo == 0) {
                glGenBuffers(1, &s.vbo);
            }
        }

        void computeBoardLayout(GLFWwindow* win,
                                float& outLeft,
                                float& outTop,
                                float& outCellSize,
                                float& outBoardSize,
                                float& outWindowW,
                                float& outWindowH) {
            int ww = 0;
            int wh = 0;
            if (win) glfwGetWindowSize(win, &ww, &wh);
            if (ww <= 0) ww = 1920;
            if (wh <= 0) wh = 1080;
            outWindowW = static_cast<float>(ww);
            outWindowH = static_cast<float>(wh);
            float boardSize = std::floor(std::min(outWindowW, outWindowH) * 0.72f);
            float cellSize = std::floor(boardSize / static_cast<float>(kGridSize));
            if (cellSize < 8.0f) cellSize = 8.0f;
            boardSize = cellSize * static_cast<float>(kGridSize);
            outCellSize = cellSize;
            outBoardSize = boardSize;
            outLeft = std::floor((outWindowW - boardSize) * 0.5f);
            outTop = std::floor((outWindowH - boardSize) * 0.5f);
        }
    } // namespace

    bool IsMiningActive(const BaseSystem& baseSystem) {
        return state().active;
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
        if (baseSystem.ui && baseSystem.ui->active) return false;
        if (s.active) return false;

        const int oreKind = oreKindForPrototype(prototypes, targetPrototypeID);
        if (oreKind < 0) return false;

        const uint32_t seed = hashU32(
            static_cast<uint32_t>(cell.x * 73856093)
            ^ static_cast<uint32_t>(cell.y * 19349663)
            ^ static_cast<uint32_t>(cell.z * 83492791)
            ^ static_cast<uint32_t>((worldIndex + 19) * 2654435761u)
            ^ static_cast<uint32_t>(baseSystem.frameIndex));

        s.active = true;
        s.awaitingInitialRelease = true;
        s.lastLeftDown = false;
        s.lastEscapeDown = false;
        s.worldIndex = worldIndex;
        s.cell = cell;
        s.blockPos = blockPos;
        s.orePrototypeID = targetPrototypeID;
        s.oreKind = oreKind;
        s.playerForward = (glm::length(playerForward) > 0.001f)
            ? glm::normalize(playerForward)
            : glm::vec3(0.0f, 0.0f, -1.0f);
        s.rngState = seed != 0 ? seed : 0xD17F46A3u;
        const int baseMaxClicks = std::max(1, readRegistryInt(baseSystem, "OreMiningMaxClicks", 19));
        const int bonusMaxClicks = std::clamp(readRegistryInt(baseSystem, "OreMiningBonusClicksMax", 6), 0, 64);
        const int bonusClicks = (bonusMaxClicks > 0)
            ? nextRandRangeInt(s.rngState, 0, bonusMaxClicks)
            : 0;
        s.maxClicks = baseMaxClicks + bonusClicks;

        clearSessionGrid(s);
        generateOreMask(s);
        bakeGridColors(baseSystem);

        return true;
    }

    void UpdateOreMining(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt;
        OreMiningState& s = state();

        if (!readRegistryBool(baseSystem, "OreMiningEnabled", true)) {
            if (s.active) {
                s.active = false;
            }
            updateCursorCapture(baseSystem, win);
            return;
        }

        updateCursorCapture(baseSystem, win);
        if (!s.active || !win) return;

        const bool leftDown = (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
        const bool leftPressed = (!s.lastLeftDown && leftDown);
        s.lastLeftDown = leftDown;

        const bool escapeDown = (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS);
        const bool escapePressed = (!s.lastEscapeDown && escapeDown);
        s.lastEscapeDown = escapeDown;
        if (escapePressed) {
            endSession(baseSystem, win, false, prototypes);
            return;
        }

        if (s.awaitingInitialRelease) {
            if (!leftDown) {
                s.awaitingInitialRelease = false;
            }
            return;
        }

        if (!leftPressed) return;

        float boardLeft = 0.0f;
        float boardTop = 0.0f;
        float cellSize = 0.0f;
        float boardSize = 0.0f;
        float windowW = 0.0f;
        float windowH = 0.0f;
        computeBoardLayout(win, boardLeft, boardTop, cellSize, boardSize, windowW, windowH);

        double mx = 0.0;
        double my = 0.0;
        glfwGetCursorPos(win, &mx, &my);
        if (mx < boardLeft || my < boardTop || mx >= (boardLeft + boardSize) || my >= (boardTop + boardSize)) {
            return;
        }

        const int cellX = std::clamp(static_cast<int>((mx - static_cast<double>(boardLeft)) / static_cast<double>(cellSize)), 0, kGridSize - 1);
        const int cellY = std::clamp(static_cast<int>((my - static_cast<double>(boardTop)) / static_cast<double>(cellSize)), 0, kGridSize - 1);
        const int idx = gridIndex(cellX, cellY);
        if (s.revealed[static_cast<size_t>(idx)] != 0u) {
            return;
        }

        revealCrossAtCell(s, cellX, cellY);
        s.clicksUsed += 1;
        AudioSystemLogic::TriggerGameplaySfx(baseSystem, "break_stone.ck", 1.0f);

        if (s.oreRevealedCells >= s.oreTotalCells) {
            endSession(baseSystem, win, true, prototypes);
            return;
        }

        if (s.clicksUsed >= s.maxClicks) {
            endSession(baseSystem, win, false, prototypes);
            return;
        }
    }

    void RenderOreMining(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes;
        (void)dt;
        OreMiningState& s = state();
        if (!s.active || !win || !baseSystem.renderer || !baseSystem.world) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        ensureRenderResources(renderer, world, s);
        if (!renderer.uiColorShader || s.vao == 0 || s.vbo == 0) return;

        float boardLeft = 0.0f;
        float boardTop = 0.0f;
        float cellSize = 0.0f;
        float boardSize = 0.0f;
        float windowW = 0.0f;
        float windowH = 0.0f;
        computeBoardLayout(win, boardLeft, boardTop, cellSize, boardSize, windowW, windowH);

        std::vector<UiColorVertex> fillVerts;
        std::vector<UiColorVertex> lineVerts;
        fillVerts.reserve(static_cast<size_t>(kGridCellCount) * 6u + 18u);
        lineVerts.reserve(static_cast<size_t>(kGridSize + 1) * 4u + 8u);

        pushQuad(fillVerts,
                 0.0f,
                 0.0f,
                 windowW,
                 windowH,
                 glm::vec3(0.0f, 0.0f, 0.0f),
                 windowW,
                 windowH);

        const float border = std::max(2.0f, std::floor(cellSize * 0.15f));
        pushQuad(fillVerts,
                 boardLeft - border,
                 boardTop - border,
                 boardLeft + boardSize + border,
                 boardTop + boardSize + border,
                 glm::vec3(0.08f, 0.07f, 0.06f),
                 windowW,
                 windowH);

        for (int y = 0; y < kGridSize; ++y) {
            for (int x = 0; x < kGridSize; ++x) {
                const int idx = gridIndex(x, y);
                glm::vec3 color = s.hiddenColors[static_cast<size_t>(idx)];
                if (s.revealed[static_cast<size_t>(idx)] != 0u) {
                    color = (s.oreMask[static_cast<size_t>(idx)] != 0u)
                        ? s.revealOreColors[static_cast<size_t>(idx)]
                        : s.revealBaseColors[static_cast<size_t>(idx)];
                }

                const float x0 = boardLeft + static_cast<float>(x) * cellSize;
                const float y0 = boardTop + static_cast<float>(y) * cellSize;
                const float x1 = x0 + cellSize;
                const float y1 = y0 + cellSize;
                pushQuad(fillVerts, x0, y0, x1, y1, color, windowW, windowH);
            }
        }

        const glm::vec3 gridColor(0.05f, 0.05f, 0.05f);
        for (int i = 0; i <= kGridSize; ++i) {
            const float x = boardLeft + static_cast<float>(i) * cellSize;
            const float y = boardTop + static_cast<float>(i) * cellSize;
            pushLine(lineVerts, x, boardTop, x, boardTop + boardSize, gridColor, windowW, windowH);
            pushLine(lineVerts, boardLeft, y, boardLeft + boardSize, y, gridColor, windowW, windowH);
        }

        const float hitsRatio = (s.maxClicks > 0)
            ? glm::clamp(static_cast<float>(s.clicksUsed) / static_cast<float>(s.maxClicks), 0.0f, 1.0f)
            : 1.0f;
        const float barPad = std::max(6.0f, std::floor(cellSize * 0.30f));
        const float barH = std::max(10.0f, std::floor(cellSize * 0.70f));
        const float barW = boardSize;
        const float barX = boardLeft;
        const float barY = boardTop - barH - barPad;

        pushQuad(fillVerts, barX, barY, barX + barW, barY + barH, glm::vec3(0.13f, 0.11f, 0.10f), windowW, windowH);
        const float fillW = barW * (1.0f - hitsRatio);
        const glm::vec3 fillColor = glm::mix(glm::vec3(0.88f, 0.20f, 0.18f), glm::vec3(0.24f, 0.82f, 0.30f), 1.0f - hitsRatio);
        pushQuad(fillVerts, barX, barY, barX + fillW, barY + barH, fillColor, windowW, windowH);

        const int fillCount = static_cast<int>(fillVerts.size());
        const int lineCount = static_cast<int>(lineVerts.size());
        if (fillCount <= 0) return;

        std::vector<UiColorVertex> upload;
        upload.reserve(fillVerts.size() + lineVerts.size());
        upload.insert(upload.end(), fillVerts.begin(), fillVerts.end());
        upload.insert(upload.end(), lineVerts.begin(), lineVerts.end());

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);

        glBindVertexArray(s.vao);
        glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(upload.size() * sizeof(UiColorVertex)),
                     upload.data(),
                     GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiColorVertex), (void*)offsetof(UiColorVertex, pos));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(UiColorVertex), (void*)offsetof(UiColorVertex, color));
        glEnableVertexAttribArray(1);

        renderer.uiColorShader->use();

        glBlendColor(0.0f, 0.0f, 0.0f, 0.58f);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES, 6, fillCount - 6);
        if (lineCount > 0) {
            glLineWidth(1.0f);
            glDrawArrays(GL_LINES, fillCount, lineCount);
        }

        glEnable(GL_DEPTH_TEST);
    }
}
