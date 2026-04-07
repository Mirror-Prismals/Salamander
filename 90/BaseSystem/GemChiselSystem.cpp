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
#include <unordered_map>
#include <vector>
#include "../stb_image.h"

namespace AudioSystemLogic { bool TriggerGameplaySfx(BaseSystem& baseSystem, const std::string& cueName, float gain); }
namespace OreMiningSystemLogic { bool IsMiningActive(const BaseSystem& baseSystem); }
namespace GroundCraftingSystemLogic { bool IsRitualActive(const BaseSystem& baseSystem); }

namespace GemChiselSystemLogic {
    namespace {
        constexpr float kMiniVoxelSize = 1.0f / 24.0f;

        struct ColorVertex {
            glm::vec3 pos;
            glm::vec3 color;
        };

        struct GemChiselState {
            RenderHandle vao = 0;
            RenderHandle vbo = 0;
        };

        struct BlueprintStencilMask {
            int width = 0;
            int height = 0;
            std::vector<uint8_t> protectedPixels;
        };

        struct BlueprintStencilCache {
            std::string atlasTexturePath;
            glm::ivec2 atlasSize = glm::ivec2(0);
            std::vector<unsigned char> atlasPixels;
            std::unordered_map<std::string, BlueprintStencilMask> masksByStencilKey;
        };

        GemChiselState& state() {
            static GemChiselState s;
            return s;
        }

        BlueprintStencilCache& stencilCache() {
            static BlueprintStencilCache s;
            return s;
        }

        bool readRegistryBool(const BaseSystem& baseSystem, const char* key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            std::string raw = std::get<std::string>(it->second);
            std::transform(raw.begin(), raw.end(), raw.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (raw == "1" || raw == "true" || raw == "yes" || raw == "on") return true;
            if (raw == "0" || raw == "false" || raw == "no" || raw == "off") return false;
            return fallback;
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

        std::string readRegistryString(const BaseSystem& baseSystem, const char* key, const char* fallback) {
            if (!baseSystem.registry) return std::string(fallback ? fallback : "");
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) {
                return std::string(fallback ? fallback : "");
            }
            return std::get<std::string>(it->second);
        }

        float fractf(float v) {
            return v - std::floor(v);
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

        int resolvePrimaryTileIndex(const FaceTextureSet& set) {
            if (set.all >= 0) return set.all;
            if (set.side >= 0) return set.side;
            if (set.top >= 0) return set.top;
            if (set.bottom >= 0) return set.bottom;
            return -1;
        }

        bool extractAtlasTilePixels(const std::vector<unsigned char>& atlasPixels,
                                    const glm::ivec2& atlasSize,
                                    int tileIndex,
                                    const glm::ivec2& tileSize,
                                    int tilesPerRow,
                                    int tilesPerCol,
                                    std::vector<unsigned char>& outPixels) {
            outPixels.clear();
            if (atlasPixels.empty()
                || atlasSize.x <= 0
                || atlasSize.y <= 0
                || tileIndex < 0
                || tileSize.x <= 0
                || tileSize.y <= 0
                || tilesPerRow <= 0
                || tilesPerCol <= 0) return false;

            const int tileX = (tileIndex % tilesPerRow) * tileSize.x;
            const int tileRowFromTop = (tileIndex / tilesPerRow);
            const int tileRowFromBottom = (tilesPerCol - 1 - tileRowFromTop);
            const int tileY = tileRowFromBottom * tileSize.y;
            if (tileX < 0
                || tileY < 0
                || tileX + tileSize.x > atlasSize.x
                || tileY + tileSize.y > atlasSize.y) return false;

            outPixels.resize(static_cast<size_t>(tileSize.x * tileSize.y * 4), 0u);
            for (int y = 0; y < tileSize.y; ++y) {
                const size_t src = static_cast<size_t>(((tileY + y) * atlasSize.x + tileX) * 4);
                const size_t dst = static_cast<size_t>((y * tileSize.x) * 4);
                std::copy_n(&atlasPixels[src], static_cast<size_t>(tileSize.x * 4), &outPixels[dst]);
            }
            return true;
        }

        bool loadAtlasPixelsForStencilCache(const BaseSystem& baseSystem,
                                            BlueprintStencilCache& cache) {
            const std::string defaultAtlasTexturePath = "Procedures/assets/atlas.png";
            const std::string configuredPath = readRegistryString(
                baseSystem,
                "AtlasTexturePath",
                defaultAtlasTexturePath.c_str()
            );
            if (!cache.atlasPixels.empty() && cache.atlasTexturePath == configuredPath) {
                return true;
            }

            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_set_flip_vertically_on_load(true);
            std::string atlasPath = configuredPath;
            unsigned char* pixels = stbi_load(atlasPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
            if (!pixels && atlasPath != defaultAtlasTexturePath) {
                atlasPath = defaultAtlasTexturePath;
                pixels = stbi_load(atlasPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
            }
            if (!pixels || width <= 0 || height <= 0) {
                if (pixels) stbi_image_free(pixels);
                cache.atlasPixels.clear();
                cache.masksByStencilKey.clear();
                cache.atlasTexturePath.clear();
                cache.atlasSize = glm::ivec2(0);
                return false;
            }

            cache.atlasPixels.assign(
                pixels,
                pixels + static_cast<size_t>(width * height * 4)
            );
            stbi_image_free(pixels);
            cache.atlasTexturePath = atlasPath;
            cache.atlasSize = glm::ivec2(width, height);
            cache.masksByStencilKey.clear();
            return true;
        }

        bool buildStencilMaskFromTile(const std::vector<unsigned char>& stencilTile,
                                      int width,
                                      int height,
                                      BlueprintStencilMask& outMask) {
            outMask = BlueprintStencilMask{};
            if (width <= 0 || height <= 0) return false;
            if (stencilTile.size() < static_cast<size_t>(width * height * 4)) return false;

            int minStencilAlpha = 255;
            int maxStencilAlpha = 0;
            for (size_t i = 0; i + 3 < stencilTile.size(); i += 4) {
                const int a = static_cast<int>(stencilTile[i + 3]);
                minStencilAlpha = std::min(minStencilAlpha, a);
                maxStencilAlpha = std::max(maxStencilAlpha, a);
            }
            const bool useStencilAlphaMask = (maxStencilAlpha - minStencilAlpha) > 16 && maxStencilAlpha > 0;
            auto readLumaAt = [&](int x, int y) -> int {
                const int sx = std::clamp(x, 0, std::max(0, width - 1));
                const int sy = std::clamp(y, 0, std::max(0, height - 1));
                const size_t idx = static_cast<size_t>((sy * width + sx) * 4);
                return (static_cast<int>(stencilTile[idx + 0])
                    + static_cast<int>(stencilTile[idx + 1])
                    + static_cast<int>(stencilTile[idx + 2])) / 3;
            };
            const int backgroundLuma = (
                readLumaAt(0, 0)
                + readLumaAt(width - 1, 0)
                + readLumaAt(0, height - 1)
                + readLumaAt(width - 1, height - 1)
            ) / 4;

            outMask.width = width;
            outMask.height = height;
            outMask.protectedPixels.resize(static_cast<size_t>(width * height), 0u);
            bool anyProtected = false;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const size_t idx = static_cast<size_t>((y * width + x) * 4);
                    int overlayAlpha = 0;
                    if (useStencilAlphaMask) {
                        overlayAlpha = static_cast<int>(stencilTile[idx + 3]);
                    } else {
                        const int stencilLuma = (static_cast<int>(stencilTile[idx + 0])
                            + static_cast<int>(stencilTile[idx + 1])
                            + static_cast<int>(stencilTile[idx + 2])) / 3;
                        const int lumaDelta = std::abs(stencilLuma - backgroundLuma);
                        overlayAlpha = std::clamp(lumaDelta * 2, 0, 255);
                    }
                    if (overlayAlpha < 20) overlayAlpha = 0;
                    const bool protectedPixel = overlayAlpha > 0;
                    outMask.protectedPixels[static_cast<size_t>(y * width + x)] = protectedPixel ? 1u : 0u;
                    anyProtected = anyProtected || protectedPixel;
                }
            }
            return anyProtected;
        }

        bool resolveBlueprintStencilKeyForPrototypeID(const WorldContext& world,
                                                      int prototypeID,
                                                      std::string& outStencilKey) {
            outStencilKey.clear();
            if (prototypeID < 0 || prototypeID >= static_cast<int>(world.prototypeTextureSets.size())) return false;
            const int prototypeTile = resolvePrimaryTileIndex(world.prototypeTextureSets[static_cast<size_t>(prototypeID)]);
            if (prototypeTile < 0) return false;
            const std::array<std::pair<const char*, const char*>, 5> kBlueprintStencilPairs = {{
                {"BlueprintAxehead", "24x24AxeheadStencil"},
                {"BlueprintHilt", "24x24HiltStencil"},
                {"BlueprintPickaxe", "24x24PickaxeStencil"},
                {"BlueprintScythe", "24x24ScytheStencil"},
                {"BlueprintSword", "24x24SwordStencil"},
            }};
            for (const auto& pair : kBlueprintStencilPairs) {
                const int blueprintTile = resolveMappedAtlasTileIndex(world, pair.first);
                if (blueprintTile >= 0 && blueprintTile == prototypeTile) {
                    outStencilKey = pair.second;
                    return true;
                }
            }
            return false;
        }

        int resolveActiveWorldIndex(const BaseSystem& baseSystem, int worldIndexHint) {
            if (!baseSystem.level || baseSystem.level->worlds.empty()) return -1;
            if (worldIndexHint >= 0 && worldIndexHint < static_cast<int>(baseSystem.level->worlds.size())) {
                return worldIndexHint;
            }
            int worldIndex = baseSystem.level->activeWorldIndex;
            if (worldIndex < 0 || worldIndex >= static_cast<int>(baseSystem.level->worlds.size())) {
                worldIndex = 0;
            }
            if (worldIndex < 0 || worldIndex >= static_cast<int>(baseSystem.level->worlds.size())) return -1;
            return worldIndex;
        }

        int resolvePrototypeAtCell(const BaseSystem& baseSystem,
                                   int worldIndex,
                                   const glm::ivec3& cell) {
            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                const uint32_t id = baseSystem.voxelWorld->getBlockWorld(cell);
                if (id > 0 && id < std::numeric_limits<uint32_t>::max()) {
                    return static_cast<int>(id);
                }
            }
            if (!baseSystem.level
                || worldIndex < 0
                || worldIndex >= static_cast<int>(baseSystem.level->worlds.size())) return -1;
            const Entity& world = baseSystem.level->worlds[static_cast<size_t>(worldIndex)];
            const glm::vec3 cellPos = glm::vec3(cell);
            for (const EntityInstance& inst : world.instances) {
                if (glm::distance(inst.position, cellPos) > 0.05f) continue;
                if (inst.prototypeID < 0) continue;
                return inst.prototypeID;
            }
            return -1;
        }

        bool tryGetBlueprintStencilMask(const BaseSystem& baseSystem,
                                        const WorldContext& world,
                                        const std::string& stencilKey,
                                        BlueprintStencilMask& outMask) {
            outMask = BlueprintStencilMask{};
            if (stencilKey.empty()) return false;
            BlueprintStencilCache& cache = stencilCache();
            if (!loadAtlasPixelsForStencilCache(baseSystem, cache)) return false;
            auto cached = cache.masksByStencilKey.find(stencilKey);
            if (cached != cache.masksByStencilKey.end()) {
                outMask = cached->second;
                return !outMask.protectedPixels.empty();
            }

            const int stencilTile = resolveMappedAtlasTileIndex(world, stencilKey.c_str());
            if (stencilTile < 0) return false;
            std::vector<unsigned char> stencilPixels;
            if (!extractAtlasTilePixels(cache.atlasPixels,
                                        cache.atlasSize,
                                        stencilTile,
                                        world.atlasTileSize,
                                        world.atlasTilesPerRow,
                                        world.atlasTilesPerCol,
                                        stencilPixels)) {
                return false;
            }

            BlueprintStencilMask builtMask;
            if (!buildStencilMaskFromTile(stencilPixels, world.atlasTileSize.x, world.atlasTileSize.y, builtMask)) {
                cache.masksByStencilKey[stencilKey] = builtMask;
                return false;
            }
            cache.masksByStencilKey[stencilKey] = builtMask;
            outMask = builtMask;
            return true;
        }

        bool isVoxelProtectedByStencil(const BlueprintStencilMask& mask,
                                       const glm::vec3& voxelWorldPosition) {
            if (mask.width <= 0 || mask.height <= 0 || mask.protectedPixels.empty()) return false;
            const float u = fractf(voxelWorldPosition.x + 0.5f);
            const float v = fractf(voxelWorldPosition.z + 0.5f);
            const int px = std::clamp(static_cast<int>(std::floor(u * static_cast<float>(mask.width))), 0, mask.width - 1);
            const int py = std::clamp(static_cast<int>(std::floor(v * static_cast<float>(mask.height))), 0, mask.height - 1);
            const size_t idx = static_cast<size_t>(py * mask.width + px);
            if (idx >= mask.protectedPixels.size()) return false;
            return mask.protectedPixels[idx] != 0u;
        }

        bool rayIntersectsAabb(const glm::vec3& origin,
                               const glm::vec3& direction,
                               const glm::vec3& minBounds,
                               const glm::vec3& maxBounds,
                               float& outDistance) {
            constexpr float kEps = 1e-6f;
            float tNear = -std::numeric_limits<float>::infinity();
            float tFar = std::numeric_limits<float>::infinity();

            for (int axis = 0; axis < 3; ++axis) {
                const float o = origin[axis];
                const float d = direction[axis];
                const float minB = minBounds[axis];
                const float maxB = maxBounds[axis];

                if (std::abs(d) < kEps) {
                    if (o < minB || o > maxB) return false;
                    continue;
                }

                float t1 = (minB - o) / d;
                float t2 = (maxB - o) / d;
                if (t1 > t2) std::swap(t1, t2);
                tNear = std::max(tNear, t1);
                tFar = std::min(tFar, t2);
                if (tNear > tFar) return false;
            }

            if (tFar < 0.0f) return false;
            outDistance = (tNear >= 0.0f) ? tNear : tFar;
            return outDistance >= 0.0f;
        }

        glm::vec3 cameraForward(const PlayerContext& player) {
            glm::vec3 forward;
            forward.x = std::cos(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
            forward.y = std::sin(glm::radians(player.cameraPitch));
            forward.z = std::sin(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
            if (glm::length(forward) < 0.0001f) return glm::vec3(0.0f, 0.0f, -1.0f);
            return glm::normalize(forward);
        }

        glm::vec3 cameraEyePosition(const BaseSystem& baseSystem, const PlayerContext& player) {
            if (baseSystem.gamemode == "survival") {
                return player.cameraPosition + glm::vec3(0.0f, 0.6f, 0.0f);
            }
            return player.cameraPosition;
        }

        glm::vec3 effectiveDropBasePosition(const GemDropState& drop) {
            if (drop.lockToCell) {
                return glm::vec3(
                    static_cast<float>(drop.lockedCell.x) + drop.lockedOffsetXZ.x,
                    static_cast<float>(drop.lockedCell.y),
                    static_cast<float>(drop.lockedCell.z) + drop.lockedOffsetXZ.y
                );
            }
            return drop.position;
        }

        glm::vec3 applyUpsideDown(const GemDropState& drop, const glm::vec3& v) {
            if (!drop.upsideDown) return v;
            return glm::vec3(v.x, -v.y, -v.z);
        }

        void computeVoxelScaleAndYOffset(const BaseSystem& baseSystem,
                                         const GemDropState& drop,
                                         float& outVoxelScale,
                                         float& outHalf,
                                         float& outAlignYOffset) {
            const float renderScale = glm::clamp(readRegistryFloat(baseSystem, "GemDropVisualScale", 1.0f), 0.1f, 100.0f);
            outVoxelScale = std::max(0.05f, renderScale);
            outHalf = (kMiniVoxelSize * 0.5f) * outVoxelScale;

            float minFaceY = std::numeric_limits<float>::max();
            for (const glm::ivec3& cell : drop.voxelCells) {
                const glm::vec3 flippedCenter = applyUpsideDown(drop, glm::vec3(cell) * (kMiniVoxelSize * outVoxelScale));
                const float centerY = flippedCenter.y;
                minFaceY = std::min(minFaceY, centerY - outHalf);
            }
            outAlignYOffset = (minFaceY < std::numeric_limits<float>::max() * 0.5f)
                ? ((-0.5f + drop.renderYOffset) - minFaceY)
                : 0.0f;
        }

        glm::vec3 voxelCenterWorld(const BaseSystem& baseSystem,
                                   const GemDropState& drop,
                                   const glm::ivec3& voxelCell) {
            float voxelScale = 1.0f;
            float half = 0.0f;
            float alignYOffset = 0.0f;
            computeVoxelScaleAndYOffset(baseSystem, drop, voxelScale, half, alignYOffset);
            (void)half;
            const glm::vec3 localCenter = applyUpsideDown(drop, glm::vec3(voxelCell) * (kMiniVoxelSize * voxelScale))
                + glm::vec3(0.0f, alignYOffset, 0.0f);
            return effectiveDropBasePosition(drop) + localCenter;
        }

        int pickVoxelIndexFromPlayerRay(const BaseSystem& baseSystem,
                                        const GemDropState& drop,
                                        float* outDistance) {
            if (!baseSystem.player || drop.voxelCells.empty()) return -1;

            const PlayerContext& player = *baseSystem.player;
            const glm::vec3 rayDir = cameraForward(player);
            const float rayStartOffset = glm::clamp(readRegistryFloat(baseSystem, "GemChiselRayStartOffset", 0.0f), 0.0f, 1.0f);
            const float minHitDistance = glm::clamp(readRegistryFloat(baseSystem, "GemChiselMinHitDistance", 0.04f), 0.0f, 0.5f);
            const glm::vec3 rayOrigin = cameraEyePosition(baseSystem, player) + rayDir * rayStartOffset;

            float voxelScale = 1.0f;
            float half = 0.0f;
            float alignYOffset = 0.0f;
            computeVoxelScaleAndYOffset(baseSystem, drop, voxelScale, half, alignYOffset);
            const float maxRange = glm::clamp(readRegistryFloat(baseSystem, "GemChiselRange", 3.0f), 0.5f, 12.0f);
            const float forgivingRadius = glm::clamp(readRegistryFloat(baseSystem, "GemChiselPickRadius", 0.22f), 0.02f, 1.0f);

            const glm::vec3 dropBase = effectiveDropBasePosition(drop);

            float bestScore = std::numeric_limits<float>::max();
            float bestAlong = std::numeric_limits<float>::max();
            int bestIndex = -1;
            for (int i = 0; i < static_cast<int>(drop.voxelCells.size()); ++i) {
                const glm::vec3 localCenter = applyUpsideDown(
                    drop,
                    glm::vec3(drop.voxelCells[static_cast<size_t>(i)]) * (kMiniVoxelSize * voxelScale))
                    + glm::vec3(0.0f, alignYOffset, 0.0f);
                const glm::vec3 center = dropBase + localCenter;
                float hitDistance = 0.0f;
                if (!rayIntersectsAabb(rayOrigin,
                                       rayDir,
                                       center - glm::vec3(half),
                                       center + glm::vec3(half),
                                       hitDistance)) {
                    continue;
                }
                if (hitDistance > maxRange) continue;

                const glm::vec3 toCenter = center - rayOrigin;
                const float along = glm::dot(toCenter, rayDir);
                if (along < -half || along > (maxRange + half)) continue;
                const glm::vec3 closestPoint = rayOrigin + rayDir * along;
                const float perpendicular = glm::length(center - closestPoint);
                const float denom = std::max(half, 1e-4f);
                float score = perpendicular / denom;
                score += glm::clamp(along / std::max(maxRange, 1e-4f), 0.0f, 2.0f) * 0.08f;
                if (hitDistance < minHitDistance) {
                    // Keep near-origin candidates instead of dropping them outright;
                    // this avoids snapping to low voxels when the camera is close.
                    score += (minHitDistance - hitDistance) * 0.35f;
                }
                if (rayDir.y > -0.15f) {
                    const float below = rayOrigin.y - center.y;
                    if (below > 0.0f) {
                        score += glm::clamp(below / std::max(kMiniVoxelSize * voxelScale, 1e-4f), 0.0f, 48.0f) * 0.02f;
                    }
                }

                if ((score < bestScore) || (std::abs(score - bestScore) < 1e-4f && along < bestAlong)) {
                    bestScore = score;
                    bestAlong = along;
                    bestIndex = i;
                }
            }

            if (bestIndex >= 0) {
                if (outDistance) *outDistance = std::max(0.0f, bestAlong);
                return bestIndex;
            }

            // Forgiving fallback: intersect against slightly-expanded voxel AABBs.
            // This tracks crosshair intent better than center-distance heuristics,
            // especially when the player is offset below/above the gem.
            float bestFallbackScore = std::numeric_limits<float>::max();
            float bestFallbackAlong = std::numeric_limits<float>::max();
            int bestFallbackIndex = -1;
            const float expandedHalf = half + forgivingRadius;
            for (int i = 0; i < static_cast<int>(drop.voxelCells.size()); ++i) {
                const glm::vec3 localCenter = applyUpsideDown(
                    drop,
                    glm::vec3(drop.voxelCells[static_cast<size_t>(i)]) * (kMiniVoxelSize * voxelScale))
                    + glm::vec3(0.0f, alignYOffset, 0.0f);
                const glm::vec3 center = dropBase + localCenter;
                float hitDistance = 0.0f;
                if (!rayIntersectsAabb(rayOrigin,
                                       rayDir,
                                       center - glm::vec3(expandedHalf),
                                       center + glm::vec3(expandedHalf),
                                       hitDistance)) {
                    continue;
                }
                if (hitDistance > maxRange) continue;

                const glm::vec3 toCenter = center - rayOrigin;
                const float along = glm::dot(toCenter, rayDir);
                if (along < -expandedHalf || along > (maxRange + expandedHalf)) continue;
                const glm::vec3 closestPoint = rayOrigin + rayDir * along;
                const float perpendicular = glm::length(center - closestPoint);
                float score = perpendicular / std::max(expandedHalf, 1e-4f);
                score += glm::clamp(along / std::max(maxRange, 1e-4f), 0.0f, 2.0f) * 0.10f;
                if (hitDistance < minHitDistance) {
                    score += (minHitDistance - hitDistance) * 0.45f;
                }
                if (rayDir.y > -0.15f) {
                    const float below = rayOrigin.y - center.y;
                    if (below > 0.0f) {
                        score += glm::clamp(below / std::max(kMiniVoxelSize * voxelScale, 1e-4f), 0.0f, 48.0f) * 0.03f;
                    }
                }

                if ((score < bestFallbackScore)
                    || (std::abs(score - bestFallbackScore) < 1e-4f && along < bestFallbackAlong)) {
                    bestFallbackScore = score;
                    bestFallbackAlong = along;
                    bestFallbackIndex = i;
                }
            }

            if (outDistance) {
                *outDistance = (bestFallbackIndex >= 0)
                    ? std::max(0.0f, bestFallbackAlong)
                    : std::numeric_limits<float>::max();
            }
            return bestFallbackIndex;
        }

        bool resolveRayChiselTarget(const BaseSystem& baseSystem,
                                    int* outDropIndex,
                                    int* outVoxelIndex,
                                    float* outDistance) {
            if (outDropIndex) *outDropIndex = -1;
            if (outVoxelIndex) *outVoxelIndex = -1;
            if (outDistance) *outDistance = std::numeric_limits<float>::max();
            if (!baseSystem.gems) return false;

            const GemContext& gems = *baseSystem.gems;
            if (gems.drops.empty()) return false;

            int bestDropIndex = -1;
            int bestVoxelIndex = -1;
            float bestDistance = std::numeric_limits<float>::max();
            for (int i = 0; i < static_cast<int>(gems.drops.size()); ++i) {
                const GemDropState& drop = gems.drops[static_cast<size_t>(i)];
                float hitDistance = std::numeric_limits<float>::max();
                const int voxelIndex = pickVoxelIndexFromPlayerRay(baseSystem, drop, &hitDistance);
                if (voxelIndex < 0) continue;
                if (!std::isfinite(hitDistance)) continue;
                if (hitDistance < bestDistance) {
                    bestDistance = hitDistance;
                    bestDropIndex = i;
                    bestVoxelIndex = voxelIndex;
                }
            }

            if (bestDropIndex < 0 || bestVoxelIndex < 0) return false;
            if (outDropIndex) *outDropIndex = bestDropIndex;
            if (outVoxelIndex) *outVoxelIndex = bestVoxelIndex;
            if (outDistance) *outDistance = bestDistance;
            return true;
        }

        void ensureRenderResources(RendererContext& renderer, WorldContext& world, GemChiselState& s, IRenderBackend& renderBackend) {
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

        void pushTri(std::vector<ColorVertex>& out,
                     const glm::vec3& a,
                     const glm::vec3& b,
                     const glm::vec3& c,
                     const glm::vec3& color) {
            out.push_back({a, color});
            out.push_back({b, color});
            out.push_back({c, color});
        }

        void pushVoxelShadowCube(std::vector<ColorVertex>& out,
                                 const glm::vec3& center,
                                 float half,
                                 const glm::vec3& color) {
            const glm::vec3 p000 = center + glm::vec3(-half, -half, -half);
            const glm::vec3 p100 = center + glm::vec3( half, -half, -half);
            const glm::vec3 p010 = center + glm::vec3(-half,  half, -half);
            const glm::vec3 p110 = center + glm::vec3( half,  half, -half);
            const glm::vec3 p001 = center + glm::vec3(-half, -half,  half);
            const glm::vec3 p101 = center + glm::vec3( half, -half,  half);
            const glm::vec3 p011 = center + glm::vec3(-half,  half,  half);
            const glm::vec3 p111 = center + glm::vec3( half,  half,  half);

            pushTri(out, p000, p100, p110, color);
            pushTri(out, p000, p110, p010, color);

            pushTri(out, p001, p101, p111, color);
            pushTri(out, p001, p111, p011, color);

            pushTri(out, p000, p001, p101, color);
            pushTri(out, p000, p101, p100, color);

            pushTri(out, p010, p011, p111, color);
            pushTri(out, p010, p111, p110, color);

            pushTri(out, p000, p001, p011, color);
            pushTri(out, p000, p011, p010, color);

            pushTri(out, p100, p101, p111, color);
            pushTri(out, p100, p111, p110, color);
        }
    } // namespace

    bool IsChiselActive(const BaseSystem& baseSystem) {
        (void)baseSystem;
        // Chiseling is now in-world and non-modal.
        return false;
    }

    bool StartGemChiselAtCell(BaseSystem& baseSystem, const glm::ivec3& cell, int worldIndex) {
        if (!readRegistryBool(baseSystem, "GemChiselSystem", true)) return false;
        if (!readRegistryBool(baseSystem, "GemChiselEnabled", true)) return false;
        if (!baseSystem.gems) return false;
        if (baseSystem.ui && baseSystem.ui->active) return false;
        if (OreMiningSystemLogic::IsMiningActive(baseSystem)) return false;
        if (GroundCraftingSystemLogic::IsRitualActive(baseSystem)) return false;

        int dropIndex = -1;
        int voxelIndex = -1;
        if (!resolveRayChiselTarget(baseSystem, &dropIndex, &voxelIndex, nullptr)) return false;
        if (dropIndex < 0 || dropIndex >= static_cast<int>(baseSystem.gems->drops.size())) return false;

        GemDropState& drop = baseSystem.gems->drops[static_cast<size_t>(dropIndex)];
        const bool stencilProtectEnabled = readRegistryBool(baseSystem, "GemChiselBlueprintStencilProtectEnabled", true);
        if (stencilProtectEnabled && baseSystem.world) {
            const glm::ivec3 supportCell = drop.lockToCell ? drop.lockedCell : cell;
            const int resolvedWorldIndex = resolveActiveWorldIndex(baseSystem, worldIndex);
            if (resolvedWorldIndex >= 0) {
                const int supportPrototypeID = resolvePrototypeAtCell(baseSystem, resolvedWorldIndex, supportCell);
                std::string stencilKey;
                if (supportPrototypeID >= 0
                    && resolveBlueprintStencilKeyForPrototypeID(*baseSystem.world, supportPrototypeID, stencilKey)) {
                    BlueprintStencilMask stencilMask;
                    if (tryGetBlueprintStencilMask(baseSystem, *baseSystem.world, stencilKey, stencilMask)) {
                        const glm::vec3 voxelPos = voxelCenterWorld(
                            baseSystem,
                            drop,
                            drop.voxelCells[static_cast<size_t>(voxelIndex)]
                        );
                        if (isVoxelProtectedByStencil(stencilMask, voxelPos)) {
                            // Consume the action so block-destroy logic does not run through.
                            return true;
                        }
                    }
                }
            }
        }
        if (drop.voxelCells.size() <= 1u) {
            return true;
        }

        drop.voxelCells[static_cast<size_t>(voxelIndex)] = drop.voxelCells.back();
        drop.voxelCells.pop_back();
        drop.velocity = glm::vec3(0.0f);
        drop.age = 0.0f;
        AudioSystemLogic::TriggerGameplaySfx(baseSystem, "break_stone.ck", 0.9f);
        return true;
    }

    void UpdateGemChisel(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)baseSystem;
        (void)prototypes;
        (void)dt;
        (void)win;
        // No modal update loop anymore; chiseling happens directly from block-charge interactions.
    }

    void RenderGemChisel(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)prototypes;
        (void)dt;
        (void)win;

        if (!readRegistryBool(baseSystem, "GemChiselSystem", true)
            || !readRegistryBool(baseSystem, "GemChiselEnabled", true)) {
            return;
        }
        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.player || !baseSystem.gems) return;

        PlayerContext& player = *baseSystem.player;

        int dropIndex = -1;
        int hoveredVoxel = -1;
        float hitDistance = std::numeric_limits<float>::max();
        if (!resolveRayChiselTarget(baseSystem, &dropIndex, &hoveredVoxel, &hitDistance)) return;
        if (dropIndex < 0 || dropIndex >= static_cast<int>(baseSystem.gems->drops.size())) return;
        if (hoveredVoxel < 0 || hoveredVoxel >= static_cast<int>(baseSystem.gems->drops[static_cast<size_t>(dropIndex)].voxelCells.size())) return;
        if (!std::isfinite(hitDistance)) return;

        GemDropState& drop = baseSystem.gems->drops[static_cast<size_t>(dropIndex)];

        float voxelScale = 1.0f;
        float half = 0.0f;
        float alignYOffset = 0.0f;
        computeVoxelScaleAndYOffset(baseSystem, drop, voxelScale, half, alignYOffset);
        (void)alignYOffset;

        const glm::vec3 center = voxelCenterWorld(baseSystem, drop, drop.voxelCells[static_cast<size_t>(hoveredVoxel)]);

        std::vector<ColorVertex> fillVerts;
        fillVerts.reserve(36u);
        const float shadowHalf = half + 0.0015f;
        pushVoxelShadowCube(fillVerts, center, shadowHalf, glm::vec3(0.06f, 0.06f, 0.06f));
        if (fillVerts.empty()) return;

        GemChiselState& s = state();
        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        if (!baseSystem.renderBackend) return;
        auto& renderBackend = *baseSystem.renderBackend;
        ensureRenderResources(renderer, world, s, renderBackend);
        if (!renderer.audioRayShader || s.vao == 0 || s.vbo == 0) return;

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
