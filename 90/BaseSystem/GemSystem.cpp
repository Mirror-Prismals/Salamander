#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace GemSystemLogic {
    namespace {
        struct GemVertex {
            glm::vec3 position;
            glm::vec3 color;
        };

        constexpr float kTau = 6.28318530718f;
        constexpr float kMiniVoxelSize = 1.0f / 24.0f;

        struct GemAtlasCache {
            bool valid = false;
            RenderHandle atlasTexture = 0;
            int atlasWidth = 0;
            int atlasHeight = 0;
            int tilesPerRow = 0;
            int tilesPerCol = 0;
            glm::ivec2 tileSize = glm::ivec2(24, 24);
            std::vector<unsigned char> atlasPixels;
        };

        GemAtlasCache& gemAtlasCache() {
            static GemAtlasCache cache;
            return cache;
        }

        glm::vec3 fallbackGemColorForKind(int kind) {
            switch (kind) {
                case 0: return glm::vec3(0.86f, 0.18f, 0.20f); // ruby
                case 1: return glm::vec3(0.64f, 0.48f, 0.88f); // amethyst
                case 2: return glm::vec3(0.38f, 0.67f, 0.96f); // flourite
                case 3: return glm::vec3(0.92f, 0.93f, 0.95f); // silver
                default: return glm::vec3(1.0f);
            }
        }

        int cutGemAtlasTileForKind(int kind) {
            switch (kind) {
                case 0: return 68; // ruby
                case 1: return 48; // amethyst
                case 2: return 60; // flourite
                case 3: return 69; // silver
                default: return -1;
            }
        }

        bool ensureGemAtlasCacheLoaded(const BaseSystem& baseSystem) {
            GemAtlasCache& cache = gemAtlasCache();
            if (!baseSystem.renderer) {
                cache.valid = false;
                cache.atlasPixels.clear();
                return false;
            }

            const RendererContext& renderer = *baseSystem.renderer;
            if (renderer.atlasTexture == 0
                || renderer.atlasTextureSize.x <= 0
                || renderer.atlasTextureSize.y <= 0
                || renderer.atlasTilesPerRow <= 0
                || renderer.atlasTilesPerCol <= 0
                || renderer.atlasTileSize.x <= 0
                || renderer.atlasTileSize.y <= 0) {
                cache.valid = false;
                cache.atlasPixels.clear();
                return false;
            }

            const bool refresh = !cache.valid
                || cache.atlasTexture != renderer.atlasTexture
                || cache.atlasWidth != renderer.atlasTextureSize.x
                || cache.atlasHeight != renderer.atlasTextureSize.y
                || cache.tilesPerRow != renderer.atlasTilesPerRow
                || cache.tilesPerCol != renderer.atlasTilesPerCol
                || cache.tileSize != renderer.atlasTileSize;
            if (!refresh) return true;

            const size_t pixelCount = static_cast<size_t>(renderer.atlasTextureSize.x)
                * static_cast<size_t>(renderer.atlasTextureSize.y) * 4u;
            if (pixelCount == 0) {
                cache.valid = false;
                cache.atlasPixels.clear();
                return false;
            }

            if (!baseSystem.renderBackend) {
                cache.valid = false;
                cache.atlasPixels.clear();
                return false;
            }
            const bool readbackOk = baseSystem.renderBackend->readTexture2DRgba(
                renderer.atlasTexture,
                renderer.atlasTextureSize.x,
                renderer.atlasTextureSize.y,
                cache.atlasPixels
            );
            if (!readbackOk) {
                cache.valid = false;
                cache.atlasPixels.clear();
                return false;
            }

            cache.atlasTexture = renderer.atlasTexture;
            cache.atlasWidth = renderer.atlasTextureSize.x;
            cache.atlasHeight = renderer.atlasTextureSize.y;
            cache.tilesPerRow = renderer.atlasTilesPerRow;
            cache.tilesPerCol = renderer.atlasTilesPerCol;
            cache.tileSize = renderer.atlasTileSize;
            cache.valid = true;
            return true;
        }

        glm::vec3 sampleGemTextureColor(const GemAtlasCache& cache, int kind, float u, float v) {
            const glm::vec3 fallback = fallbackGemColorForKind(kind);
            if (!cache.valid || cache.atlasPixels.empty()) return fallback;

            const int tileIndex = cutGemAtlasTileForKind(kind);
            if (tileIndex < 0
                || cache.tilesPerRow <= 0
                || cache.tilesPerCol <= 0
                || cache.tileSize.x <= 0
                || cache.tileSize.y <= 0) {
                return fallback;
            }

            const int tileCount = cache.tilesPerRow * cache.tilesPerCol;
            if (tileIndex >= tileCount) return fallback;

            float uu = u - std::floor(u);
            float vv = v - std::floor(v);
            if (uu < 0.0f) uu += 1.0f;
            if (vv < 0.0f) vv += 1.0f;

            const int localX = std::clamp(static_cast<int>(std::floor(uu * static_cast<float>(cache.tileSize.x))), 0, cache.tileSize.x - 1);
            const int localY = std::clamp(static_cast<int>(std::floor(vv * static_cast<float>(cache.tileSize.y))), 0, cache.tileSize.y - 1);
            const int tileX = (tileIndex % cache.tilesPerRow) * cache.tileSize.x;
            const int tileRowTop = tileIndex / cache.tilesPerRow;
            const int tileRowBottom = cache.tilesPerCol - 1 - tileRowTop;
            const int tileY = tileRowBottom * cache.tileSize.y;
            const int px = tileX + localX;
            const int py = tileY + (cache.tileSize.y - 1 - localY);
            if (px < 0 || px >= cache.atlasWidth || py < 0 || py >= cache.atlasHeight) return fallback;

            const size_t idx = static_cast<size_t>((py * cache.atlasWidth + px) * 4);
            if (idx + 3 >= cache.atlasPixels.size()) return fallback;
            const float alpha = static_cast<float>(cache.atlasPixels[idx + 3]) / 255.0f;
            if (alpha <= 0.001f) return fallback;
            const float r = static_cast<float>(cache.atlasPixels[idx + 0]) / 255.0f;
            const float g = static_cast<float>(cache.atlasPixels[idx + 1]) / 255.0f;
            const float b = static_cast<float>(cache.atlasPixels[idx + 2]) / 255.0f;
            return glm::vec3(r, g, b);
        }

        bool readRegistryBool(const BaseSystem& baseSystem, const char* key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<bool>(it->second)) return fallback;
            return std::get<bool>(it->second);
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

        uint32_t nextRandU32(uint32_t& state) {
            if (state == 0) state = 0xA17C9E3Du;
            state ^= (state << 13u);
            state ^= (state >> 17u);
            state ^= (state << 5u);
            return state;
        }

        float nextRand01(uint32_t& state) {
            return static_cast<float>(nextRandU32(state) & 0x00ffffffu) / static_cast<float>(0x01000000u);
        }

        float nextRandRange(uint32_t& state, float minV, float maxV) {
            return minV + (maxV - minV) * nextRand01(state);
        }

        int nextRandRangeInt(uint32_t& state, int minV, int maxVInclusive) {
            if (maxVInclusive <= minV) return minV;
            float t = nextRand01(state);
            int span = maxVInclusive - minV + 1;
            int idx = std::min(span - 1, static_cast<int>(t * static_cast<float>(span)));
            return minV + idx;
        }

        glm::vec3 randomUnitVector(uint32_t& state) {
            float z = nextRandRange(state, -1.0f, 1.0f);
            float a = nextRandRange(state, 0.0f, kTau);
            float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            return glm::vec3(std::cos(a) * r, z, std::sin(a) * r);
        }

        glm::vec3 rotateY(const glm::vec3& v, float radians) {
            float c = std::cos(radians);
            float s = std::sin(radians);
            return glm::vec3(
                v.x * c - v.z * s,
                v.y,
                v.x * s + v.z * c
            );
        }

        int oreKindForPrototypeName(const std::string& name) {
            if (name == "RubyOreTex") return 0;
            if (name == "AmethystOreTex") return 1;
            if (name == "FlouriteOreTex" || name == "FluoriteOreTex") return 2;
            if (name == "SilverOreTex") return 3;
            return -1;
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

        const char* gemNameForKind(int kind) {
            switch (kind) {
                case 0: return "RUBY";
                case 1: return "AMETHYST";
                case 2: return "FLOURITE";
                case 3: return "SILVER";
                default: return "GEM";
            }
        }

        const char* gemSizeLabel(float scaleT) {
            if (scaleT >= 0.92f) return "GARGANTUAN";
            if (scaleT >= 0.72f) return "HUGE";
            if (scaleT >= 0.50f) return "LARGE";
            if (scaleT >= 0.28f) return "MEDIUM";
            return "SMALL";
        }

        float gemWeightForScale(int kind, float scaleT, float visualScale) {
            float minW = 0.20f;
            float maxW = 2.40f;
            switch (kind) {
                case 0: minW = 0.24f; maxW = 3.20f; break; // ruby
                case 1: minW = 0.30f; maxW = 3.95f; break; // amethyst
                case 2: minW = 0.27f; maxW = 3.65f; break; // flourite
                case 3: minW = 0.36f; maxW = 4.60f; break; // silver
                default: break;
            }
            const float t = glm::clamp(scaleT, 0.0f, 1.0f);
            float kg = glm::mix(minW, maxW, std::pow(t, 1.18f));
            kg *= glm::clamp(visualScale / 10.0f, 0.25f, 4.0f);
            return std::max(0.01f, kg);
        }

        void addEdgeUnique(std::vector<glm::ivec2>& edges, int a, int b) {
            if (a == b) return;
            if (a > b) std::swap(a, b);
            for (const glm::ivec2& edge : edges) {
                if (edge.x == a && edge.y == b) return;
            }
            edges.emplace_back(a, b);
        }

        bool buildConvexHullMesh(const std::vector<GemDropNode>& nodes,
                                 std::vector<glm::ivec3>& trianglesOut,
                                 std::vector<glm::ivec2>& edgesOut) {
            trianglesOut.clear();
            edgesOut.clear();
            const int n = static_cast<int>(nodes.size());
            if (n < 4) return false;

            std::vector<glm::vec3> points;
            points.reserve(nodes.size());
            glm::vec3 centroid(0.0f);
            for (const GemDropNode& node : nodes) {
                points.push_back(node.offset);
                centroid += node.offset;
            }
            centroid /= static_cast<float>(n);

            const float areaEps = 1e-5f;
            const float planeEps = 1e-4f;

            for (int i = 0; i < n; ++i) {
                for (int j = i + 1; j < n; ++j) {
                    for (int k = j + 1; k < n; ++k) {
                        glm::vec3 normal = glm::cross(points[j] - points[i], points[k] - points[i]);
                        float normalLen = glm::length(normal);
                        if (normalLen <= areaEps) continue;
                        normal /= normalLen;

                        bool hasPos = false;
                        bool hasNeg = false;
                        for (int m = 0; m < n; ++m) {
                            if (m == i || m == j || m == k) continue;
                            float side = glm::dot(normal, points[m] - points[i]);
                            if (side > planeEps) hasPos = true;
                            else if (side < -planeEps) hasNeg = true;
                            if (hasPos && hasNeg) break;
                        }
                        if (hasPos && hasNeg) continue;
                        if (!hasPos && !hasNeg) continue;

                        int a = i;
                        int b = j;
                        int c = k;
                        if (glm::dot(normal, centroid - points[i]) > 0.0f) {
                            std::swap(b, c);
                        }
                        trianglesOut.emplace_back(a, b, c);
                    }
                }
            }

            if (trianglesOut.size() < 4) {
                trianglesOut.clear();
                edgesOut.clear();
                return false;
            }

            for (const glm::ivec3& tri : trianglesOut) {
                addEdgeUnique(edgesOut, tri.x, tri.y);
                addEdgeUnique(edgesOut, tri.y, tri.z);
                addEdgeUnique(edgesOut, tri.x, tri.z);
            }
            return !edgesOut.empty();
        }

        int64_t packVoxelCoord(int x, int y, int z) {
            constexpr int64_t kBias = (1ll << 20);
            return ((static_cast<int64_t>(x) + kBias) << 42)
                | ((static_cast<int64_t>(y) + kBias) << 21)
                |  (static_cast<int64_t>(z) + kBias);
        }

        bool pointInsideConvexHull(const std::vector<GemDropNode>& nodes,
                                   const std::vector<glm::ivec3>& triangles,
                                   const glm::vec3& point,
                                   float eps) {
            if (nodes.empty() || triangles.empty()) return false;
            for (const glm::ivec3& tri : triangles) {
                if (tri.x < 0 || tri.y < 0 || tri.z < 0) continue;
                if (tri.x >= static_cast<int>(nodes.size())
                    || tri.y >= static_cast<int>(nodes.size())
                    || tri.z >= static_cast<int>(nodes.size())) {
                    continue;
                }
                const glm::vec3 a = nodes[tri.x].offset;
                const glm::vec3 b = nodes[tri.y].offset;
                const glm::vec3 c = nodes[tri.z].offset;
                glm::vec3 n = glm::cross(b - a, c - a);
                float nLen = glm::length(n);
                if (nLen < 1e-6f) continue;
                n /= nLen;
                if (glm::dot(n, point - a) > eps) return false;
            }
            return true;
        }

        void buildGemVoxelCellsFromMesh(const std::vector<GemDropNode>& nodes,
                                        const std::vector<glm::ivec3>& triangles,
                                        float voxelSize,
                                        int maxCells,
                                        std::vector<glm::ivec3>& outCells) {
            outCells.clear();
            if (nodes.empty()) return;

            if (triangles.empty()) {
                outCells.push_back(glm::ivec3(0));
                return;
            }

            glm::vec3 minP(std::numeric_limits<float>::max());
            glm::vec3 maxP(-std::numeric_limits<float>::max());
            glm::vec3 centroid(0.0f);
            for (const GemDropNode& node : nodes) {
                minP = glm::min(minP, node.offset);
                maxP = glm::max(maxP, node.offset);
                centroid += node.offset;
            }
            centroid /= static_cast<float>(nodes.size());

            const float halfV = voxelSize * 0.5f;
            const int minX = static_cast<int>(std::floor((minP.x - halfV) / voxelSize));
            const int minY = static_cast<int>(std::floor((minP.y - halfV) / voxelSize));
            const int minZ = static_cast<int>(std::floor((minP.z - halfV) / voxelSize));
            const int maxX = static_cast<int>(std::ceil((maxP.x + halfV) / voxelSize));
            const int maxY = static_cast<int>(std::ceil((maxP.y + halfV) / voxelSize));
            const int maxZ = static_cast<int>(std::ceil((maxP.z + halfV) / voxelSize));

            const float insideEps = voxelSize * 0.58f;
            const int cappedMax = std::max(8, maxCells);
            outCells.reserve(static_cast<size_t>(std::min(cappedMax, 2048)));
            bool reachedCap = false;
            for (int z = minZ; z <= maxZ && !reachedCap; ++z) {
                for (int y = minY; y <= maxY && !reachedCap; ++y) {
                    for (int x = minX; x <= maxX; ++x) {
                        glm::vec3 p(
                            static_cast<float>(x) * voxelSize,
                            static_cast<float>(y) * voxelSize,
                            static_cast<float>(z) * voxelSize
                        );
                        if (!pointInsideConvexHull(nodes, triangles, p, insideEps)) continue;
                        outCells.emplace_back(x, y, z);
                        if (static_cast<int>(outCells.size()) >= cappedMax) {
                            reachedCap = true;
                            break;
                        }
                    }
                }
            }

            if (outCells.empty()) {
                const glm::ivec3 fallbackCell = glm::ivec3(glm::round(centroid / voxelSize));
                outCells.push_back(fallbackCell);
            }
        }

        void buildGemVoxelCells(GemDropState& drop, int maxCells) {
            buildGemVoxelCellsFromMesh(drop.nodes, drop.triangles, kMiniVoxelSize, maxCells, drop.voxelCells);
        }

        bool isSolidVoxelCell(const BaseSystem& baseSystem,
                              const std::vector<Entity>& prototypes,
                              int x,
                              int y,
                              int z) {
            if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return false;
            glm::ivec3 cell(x, y, z);
            uint32_t protoID = baseSystem.voxelWorld->getBlockWorld(cell);
            if (protoID == 0 || protoID >= prototypes.size()) return false;
            const Entity& proto = prototypes[protoID];
            if (!proto.isBlock) return false;
            if (proto.name == "Water") return false;
            return true;
        }

        bool resolveSphereAgainstVoxelWorld(const BaseSystem& baseSystem,
                                            const std::vector<Entity>& prototypes,
                                            glm::vec3& center,
                                            glm::vec3& velocity,
                                            float radius,
                                            float bounce,
                                            float groundFriction,
                                            bool& groundedOut) {
            groundedOut = false;
            if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return false;
            bool collidedAny = false;
            const float r = std::max(0.02f, radius);
            const float r2 = r * r;
            const float extent = r + 0.5f;

            for (int iter = 0; iter < 4; ++iter) {
                bool collidedThisPass = false;
                const int minX = static_cast<int>(std::floor(center.x - extent));
                const int maxX = static_cast<int>(std::ceil(center.x + extent));
                const int minY = static_cast<int>(std::floor(center.y - extent));
                const int maxY = static_cast<int>(std::ceil(center.y + extent));
                const int minZ = static_cast<int>(std::floor(center.z - extent));
                const int maxZ = static_cast<int>(std::ceil(center.z + extent));

                for (int z = minZ; z <= maxZ; ++z) {
                    for (int y = minY; y <= maxY; ++y) {
                        for (int x = minX; x <= maxX; ++x) {
                            if (!isSolidVoxelCell(baseSystem, prototypes, x, y, z)) continue;

                            glm::vec3 boxMin(static_cast<float>(x) - 0.5f,
                                             static_cast<float>(y) - 0.5f,
                                             static_cast<float>(z) - 0.5f);
                            glm::vec3 boxMax(static_cast<float>(x) + 0.5f,
                                             static_cast<float>(y) + 0.5f,
                                             static_cast<float>(z) + 0.5f);
                            glm::vec3 closest = glm::clamp(center, boxMin, boxMax);
                            glm::vec3 delta = center - closest;
                            float dist2 = glm::dot(delta, delta);
                            if (dist2 >= r2) continue;

                            collidedThisPass = true;
                            collidedAny = true;
                            glm::vec3 normal(0.0f, 1.0f, 0.0f);
                            float penetration = 0.0f;

                            if (dist2 > 1e-8f) {
                                float dist = std::sqrt(dist2);
                                normal = delta / dist;
                                penetration = r - dist;
                            } else {
                                // Center is exactly on/in closest point; pick a stable axis away from block center.
                                glm::vec3 fromCell = center - glm::vec3(static_cast<float>(x),
                                                                        static_cast<float>(y),
                                                                        static_cast<float>(z));
                                glm::vec3 absC = glm::abs(fromCell);
                                if (absC.x >= absC.y && absC.x >= absC.z) {
                                    normal = glm::vec3((fromCell.x >= 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f);
                                } else if (absC.y >= absC.x && absC.y >= absC.z) {
                                    normal = glm::vec3(0.0f, (fromCell.y >= 0.0f) ? 1.0f : -1.0f, 0.0f);
                                } else {
                                    normal = glm::vec3(0.0f, 0.0f, (fromCell.z >= 0.0f) ? 1.0f : -1.0f);
                                }
                                penetration = r;
                            }

                            center += normal * (penetration + 0.0008f);
                            float vn = glm::dot(velocity, normal);
                            if (vn < 0.0f) {
                                velocity -= (1.0f + bounce) * vn * normal;
                            }
                            if (normal.y > 0.45f) {
                                groundedOut = true;
                                velocity.x *= groundFriction;
                                velocity.z *= groundFriction;
                            }
                        }
                    }
                }
                if (!collidedThisPass) break;
            }
            return collidedAny;
        }

        glm::vec3 snapGemCenterToBlockColumn(const glm::vec3& pos) {
            return glm::vec3(std::round(pos.x), pos.y, std::round(pos.z));
        }

        bool rayAabbHit(const glm::vec3& origin,
                        const glm::vec3& directionNorm,
                        const glm::vec3& minBounds,
                        const glm::vec3& maxBounds,
                        float& tOut) {
            float tNear = 0.0f;
            float tFar = std::numeric_limits<float>::max();
            constexpr float kEps = 1e-6f;

            for (int axis = 0; axis < 3; ++axis) {
                const float o = origin[axis];
                const float d = directionNorm[axis];
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
            tOut = (tNear >= 0.0f) ? tNear : tFar;
            return tOut >= 0.0f;
        }

        bool resolveGemBlockVerticalCollision(const BaseSystem& baseSystem,
                                              const std::vector<Entity>& prototypes,
                                              glm::vec3& center,
                                              glm::vec3& velocity,
                                              float halfExtent,
                                              bool& groundedOut) {
            groundedOut = false;
            if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return false;

            const float h = std::max(0.05f, halfExtent);
            const glm::vec3 minB = center - glm::vec3(h);
            const glm::vec3 maxB = center + glm::vec3(h);
            const int minX = static_cast<int>(std::floor(minB.x));
            const int maxX = static_cast<int>(std::floor(maxB.x));
            const int minY = static_cast<int>(std::floor(minB.y));
            const int maxY = static_cast<int>(std::floor(maxB.y));
            const int minZ = static_cast<int>(std::floor(minB.z));
            const int maxZ = static_cast<int>(std::floor(maxB.z));

            constexpr float kEps = 0.0008f;
            bool collided = false;
            float snapUpY = -std::numeric_limits<float>::max();
            float snapDownY = std::numeric_limits<float>::max();

            for (int z = minZ; z <= maxZ; ++z) {
                for (int y = minY; y <= maxY; ++y) {
                    for (int x = minX; x <= maxX; ++x) {
                        if (!isSolidVoxelCell(baseSystem, prototypes, x, y, z)) continue;

                        const glm::vec3 cellMin(
                            static_cast<float>(x) - 0.5f,
                            static_cast<float>(y) - 0.5f,
                            static_cast<float>(z) - 0.5f);
                        const glm::vec3 cellMax(
                            static_cast<float>(x) + 0.5f,
                            static_cast<float>(y) + 0.5f,
                            static_cast<float>(z) + 0.5f);
                        if (maxB.x <= cellMin.x || minB.x >= cellMax.x
                            || maxB.y <= cellMin.y || minB.y >= cellMax.y
                            || maxB.z <= cellMin.z || minB.z >= cellMax.z) {
                            continue;
                        }

                        collided = true;
                        if (velocity.y <= 0.0f) {
                            const float candidate = cellMax.y + h + kEps;
                            snapUpY = std::max(snapUpY, candidate);
                        } else {
                            const float candidate = cellMin.y - h - kEps;
                            snapDownY = std::min(snapDownY, candidate);
                        }
                    }
                }
            }

            if (!collided) return false;
            if (velocity.y <= 0.0f && snapUpY > -std::numeric_limits<float>::max() * 0.5f) {
                center.y = snapUpY;
                velocity.y = 0.0f;
                groundedOut = true;
                return true;
            }
            if (velocity.y > 0.0f && snapDownY < std::numeric_limits<float>::max() * 0.5f) {
                center.y = snapDownY;
                velocity.y = 0.0f;
                return true;
            }
            return false;
        }

        bool raySphereHit(const glm::vec3& origin,
                          const glm::vec3& directionNorm,
                          const glm::vec3& center,
                          float radius,
                          float& tOut) {
            glm::vec3 oc = origin - center;
            float b = glm::dot(oc, directionNorm);
            float c = glm::dot(oc, oc) - (radius * radius);
            float h = b * b - c;
            if (h < 0.0f) return false;
            h = std::sqrt(h);
            float t = -b - h;
            if (t < 0.0f) t = -b + h;
            if (t < 0.0f) return false;
            tOut = t;
            return true;
        }

        void pushTri(std::vector<GemVertex>& verts,
                     const glm::vec3& a,
                     const glm::vec3& b,
                     const glm::vec3& c,
                     const glm::vec3& color) {
            verts.push_back({a, color});
            verts.push_back({b, color});
            verts.push_back({c, color});
        }

        void pushLine(std::vector<GemVertex>& verts,
                      const glm::vec3& a,
                      const glm::vec3& b,
                      const glm::vec3& color) {
            verts.push_back({a, color});
            verts.push_back({b, color});
        }
    } // namespace

    void SpawnGemDropFromOre(BaseSystem& baseSystem,
                             std::vector<Entity>& prototypes,
                             int removedPrototypeID,
                             const glm::vec3& blockPos,
                             const glm::vec3& playerForward) {
        if (!baseSystem.gems) return;
        if (!readRegistryBool(baseSystem, "GemDropsEnabled", true)) return;
        if (removedPrototypeID < 0 || removedPrototypeID >= static_cast<int>(prototypes.size())) return;

        const Entity& removedProto = prototypes[removedPrototypeID];
        const int oreKind = oreKindForPrototypeName(removedProto.name);
        if (oreKind < 0) return;

        GemContext& gems = *baseSystem.gems;
        uint32_t& rng = gems.rngState;

        const int maxDrops = std::clamp(readRegistryInt(baseSystem, "GemDropMaxCount", 256), 4, 4096);
        const int nodeMin = std::clamp(readRegistryInt(baseSystem, "GemDropNodeMin", 9), 4, 32);
        const int nodeMax = std::clamp(readRegistryInt(baseSystem, "GemDropNodeMax", 16), nodeMin, 40);
        const float scaleMin = std::max(0.02f, readRegistryFloat(baseSystem, "GemDropScaleMin", 0.16f));
        const float scaleMax = std::max(scaleMin, readRegistryFloat(baseSystem, "GemDropScaleMax", 0.34f));
        const float launchUp = std::max(0.0f, readRegistryFloat(baseSystem, "GemDropLaunchUp", 1.4f));
        const float lifeMin = std::max(0.5f, readRegistryFloat(baseSystem, "GemDropLifetimeMin", 8.0f));
        const float lifeMax = std::max(lifeMin, readRegistryFloat(baseSystem, "GemDropLifetimeMax", 14.0f));
        const float visualScale = glm::clamp(readRegistryFloat(baseSystem, "GemDropVisualScale", 1.0f), 0.1f, 100.0f);
        const float collisionScale = glm::clamp(readRegistryFloat(baseSystem, "GemDropCollisionScale", visualScale), 0.1f, 100.0f);
        const int voxelMaxCells = std::clamp(readRegistryInt(baseSystem, "GemDropVoxelMaxCells", 4096), 64, 65536);

        (void)playerForward;

        GemDropState drop;
        drop.kind = oreKind;
        // Gems occupy block columns: keep X/Z snapped so interaction and collision feel block-like.
        drop.position = snapGemCenterToBlockColumn(blockPos + glm::vec3(0.0f, 0.02f, 0.0f));
        drop.velocity = glm::vec3(
            0.0f,
            launchUp * nextRandRange(rng, 0.55f, 1.05f),
            0.0f
        );
        drop.renderYOffset = 0.0f;
        drop.lockToCell = false;
        drop.lockedCell = glm::ivec3(glm::round(drop.position));
        drop.lockedOffsetXZ = glm::vec2(0.0f);
        drop.lifetime = nextRandRange(rng, lifeMin, lifeMax);
        drop.spin = 0.0f;
        drop.spinSpeed = 0.0f;
        drop.scale = nextRandRange(rng, scaleMin, scaleMax);

        glm::vec3 startColor(1.0f);
        glm::vec3 endColor(1.0f);
        orePalette(oreKind, startColor, endColor);

        const int nodeCount = nextRandRangeInt(rng, nodeMin, nodeMax);
        bool meshBuilt = false;
        constexpr int kGenerationAttempts = 6;
        const float minSpanRequired = drop.scale * 0.20f;
        for (int attempt = 0; attempt < kGenerationAttempts && !meshBuilt; ++attempt) {
            drop.nodes.clear();
            drop.edges.clear();
            drop.triangles.clear();
            drop.nodes.reserve(static_cast<size_t>(nodeCount));

            glm::vec3 axisScale(
                nextRandRange(rng, 0.78f, 1.26f),
                nextRandRange(rng, 0.78f, 1.32f),
                nextRandRange(rng, 0.78f, 1.26f)
            );
            for (int i = 0; i < nodeCount; ++i) {
                float t = (nodeCount > 1)
                    ? static_cast<float>(i) / static_cast<float>(nodeCount - 1)
                    : 0.5f;
                t = glm::clamp(t + nextRandRange(rng, -0.18f, 0.18f), 0.0f, 1.0f);
                glm::vec3 col = glm::mix(startColor, endColor, t);
                col.r = glm::clamp(col.r + nextRandRange(rng, -0.08f, 0.08f), 0.0f, 1.0f);
                col.g = glm::clamp(col.g + nextRandRange(rng, -0.08f, 0.08f), 0.0f, 1.0f);
                col.b = glm::clamp(col.b + nextRandRange(rng, -0.08f, 0.08f), 0.0f, 1.0f);

                glm::vec3 dir = randomUnitVector(rng);
                float radius = drop.scale * nextRandRange(rng, 0.48f, 1.16f);
                glm::vec3 offset = dir * radius;
                offset *= axisScale;
                offset += randomUnitVector(rng) * (drop.scale * nextRandRange(rng, 0.00f, 0.08f));

                GemDropNode node;
                node.offset = offset;
                node.color = col;
                node.edgeColor = glm::clamp(col * 1.15f + glm::vec3(0.03f), glm::vec3(0.0f), glm::vec3(1.0f));
                drop.nodes.push_back(node);
            }

            float minX = std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max();
            float minZ = std::numeric_limits<float>::max();
            float maxX = -std::numeric_limits<float>::max();
            float maxY = -std::numeric_limits<float>::max();
            float maxZ = -std::numeric_limits<float>::max();
            for (const GemDropNode& node : drop.nodes) {
                minX = std::min(minX, node.offset.x);
                minY = std::min(minY, node.offset.y);
                minZ = std::min(minZ, node.offset.z);
                maxX = std::max(maxX, node.offset.x);
                maxY = std::max(maxY, node.offset.y);
                maxZ = std::max(maxZ, node.offset.z);
            }
            const float spanX = maxX - minX;
            const float spanY = maxY - minY;
            const float spanZ = maxZ - minZ;
            const float minSpan = std::min(spanX, std::min(spanY, spanZ));
            if (minSpan < minSpanRequired) continue;

            if (buildConvexHullMesh(drop.nodes, drop.triangles, drop.edges)) {
                meshBuilt = true;
            }
        }

        if (!meshBuilt) {
            drop.nodes.clear();
            drop.edges.clear();
            drop.triangles.clear();
            drop.nodes.reserve(4);
            const glm::vec3 baseA = glm::vec3( 1.0f,  1.0f,  1.0f) * drop.scale;
            const glm::vec3 baseB = glm::vec3(-1.0f, -1.0f,  1.0f) * drop.scale;
            const glm::vec3 baseC = glm::vec3(-1.0f,  1.0f, -1.0f) * drop.scale;
            const glm::vec3 baseD = glm::vec3( 1.0f, -1.0f, -1.0f) * drop.scale;
            const std::array<glm::vec3, 4> tetra = {baseA, baseB, baseC, baseD};
            for (int i = 0; i < 4; ++i) {
                float t = static_cast<float>(i) / 3.0f;
                glm::vec3 col = glm::mix(startColor, endColor, t);
                GemDropNode node;
                node.offset = tetra[static_cast<size_t>(i)];
                node.color = col;
                node.edgeColor = glm::clamp(col * 1.15f + glm::vec3(0.03f), glm::vec3(0.0f), glm::vec3(1.0f));
                drop.nodes.push_back(node);
            }
            buildConvexHullMesh(drop.nodes, drop.triangles, drop.edges);
        }

        float maxOffsetLen = 0.0f;
        for (const GemDropNode& node : drop.nodes) {
            maxOffsetLen = std::max(maxOffsetLen, glm::length(node.offset));
        }
        drop.collisionRadius = std::max(0.06f, maxOffsetLen * collisionScale * 1.08f);
        buildGemVoxelCells(drop, voxelMaxCells);

        if (static_cast<int>(gems.drops.size()) >= maxDrops) return;
        gems.drops.push_back(std::move(drop));

        const bool showText = readRegistryBool(baseSystem, "GemDropTextEnabled", true);
        if (showText) {
            float scaleT = 0.0f;
            const float scaleDenom = scaleMax - scaleMin;
            if (scaleDenom > 1e-6f) {
                scaleT = (gems.drops.back().scale - scaleMin) / scaleDenom;
            }
            scaleT = glm::clamp(scaleT, 0.0f, 1.0f);
            const float weightKg = gemWeightForScale(oreKind, scaleT, visualScale);
            std::ostringstream ss;
            ss << gemNameForKind(oreKind)
               << " GEM | " << gemSizeLabel(scaleT)
               << " | " << std::fixed << std::setprecision(2) << weightKg << " kg";
            gems.lastDropText = ss.str();
            gems.lastDropTimer = glm::clamp(readRegistryFloat(baseSystem, "GemDropTextDuration", 2.4f), 0.25f, 8.0f);
        }
    }

    bool TryPickupGemFromRay(BaseSystem& baseSystem,
                             const glm::vec3& rayOrigin,
                             const glm::vec3& rayDirection,
                             float maxDistance,
                             GemDropState* outDrop) {
        if (!baseSystem.gems) return false;
        GemContext& gems = *baseSystem.gems;
        if (gems.drops.empty()) return false;

        glm::vec3 dir = rayDirection;
        float dirLen = glm::length(dir);
        if (dirLen < 0.0001f) return false;
        dir /= dirLen;

        const float maxT = std::max(0.01f, maxDistance);
        int hitIndex = -1;
        float hitT = maxT;
        for (size_t i = 0; i < gems.drops.size(); ++i) {
            const GemDropState& drop = gems.drops[i];
            float t = 0.0f;
            const glm::vec3 gemCenter = snapGemCenterToBlockColumn(drop.position);
            const glm::vec3 halfExtents(0.499f);
            const glm::vec3 minBounds = gemCenter - halfExtents;
            const glm::vec3 maxBounds = gemCenter + halfExtents;
            if (!rayAabbHit(rayOrigin, dir, minBounds, maxBounds, t)) continue;
            if (t > maxT) continue;
            if (hitIndex < 0 || t < hitT) {
                hitIndex = static_cast<int>(i);
                hitT = t;
            }
        }
        if (hitIndex < 0) return false;

        GemDropState moved = std::move(gems.drops[static_cast<size_t>(hitIndex)]);
        gems.drops[static_cast<size_t>(hitIndex)] = std::move(gems.drops.back());
        gems.drops.pop_back();

        moved.velocity = glm::vec3(0.0f);
        moved.age = 0.0f;
        moved.position = snapGemCenterToBlockColumn(moved.position);
        moved.renderYOffset = 0.0f;
        moved.lockToCell = false;
        moved.lockedCell = glm::ivec3(glm::round(moved.position));
        moved.lockedOffsetXZ = glm::vec2(0.0f);
        if (outDrop) {
            *outDrop = std::move(moved);
        }
        return true;
    }

    void PlaceGemDrop(BaseSystem& baseSystem, GemDropState&& heldDrop, const glm::vec3& position) {
        if (!baseSystem.gems) return;
        GemContext& gems = *baseSystem.gems;
        const int maxDrops = std::clamp(readRegistryInt(baseSystem, "GemDropMaxCount", 256), 4, 4096);
        const int voxelMaxCells = std::clamp(readRegistryInt(baseSystem, "GemDropVoxelMaxCells", 4096), 64, 65536);
        if (static_cast<int>(gems.drops.size()) >= maxDrops) return;

        const glm::vec3 snappedPos = glm::vec3(glm::round(position));
        for (const GemDropState& existing : gems.drops) {
            const glm::ivec3 existingCell = glm::ivec3(glm::round(existing.position));
            if (existingCell == glm::ivec3(glm::round(snappedPos))) {
                return;
            }
        }

        const glm::ivec3 snappedCell = glm::ivec3(glm::round(snappedPos));
        glm::vec2 localOffset(position.x - static_cast<float>(snappedCell.x),
                              position.z - static_cast<float>(snappedCell.z));
        localOffset.x = glm::clamp(localOffset.x, -0.499f, 0.499f);
        localOffset.y = glm::clamp(localOffset.y, -0.499f, 0.499f);
        heldDrop.lockToCell = true;
        heldDrop.lockedCell = snappedCell;
        heldDrop.lockedOffsetXZ = localOffset;
        heldDrop.position = glm::vec3(
            static_cast<float>(snappedCell.x) + localOffset.x,
            static_cast<float>(snappedCell.y),
            static_cast<float>(snappedCell.z) + localOffset.y
        );
        heldDrop.velocity = glm::vec3(0.0f);
        heldDrop.age = 0.0f;
        if (heldDrop.lifetime <= 0.01f) {
            heldDrop.lifetime = std::max(0.5f, readRegistryFloat(baseSystem, "GemDropLifetimeMax", 14.0f));
        }
        if (heldDrop.collisionRadius < 0.03f) {
            const float visualScale = glm::clamp(readRegistryFloat(baseSystem, "GemDropVisualScale", 1.0f), 0.1f, 100.0f);
            const float collisionScale = glm::clamp(readRegistryFloat(baseSystem, "GemDropCollisionScale", visualScale), 0.1f, 100.0f);
            float maxOffsetLen = 0.0f;
            for (const GemDropNode& node : heldDrop.nodes) {
                maxOffsetLen = std::max(maxOffsetLen, glm::length(node.offset));
            }
            heldDrop.collisionRadius = std::max(0.06f, maxOffsetLen * collisionScale * 1.08f);
        }
        if (heldDrop.voxelCells.empty()) {
            buildGemVoxelCells(heldDrop, voxelMaxCells);
        }

        gems.drops.push_back(std::move(heldDrop));
    }

    void UpdateGems(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)win;
        if (!baseSystem.gems) return;
        GemContext& gems = *baseSystem.gems;
        if (gems.lastDropTimer > 0.0f) {
            gems.lastDropTimer = std::max(0.0f, gems.lastDropTimer - std::max(0.0f, dt));
            if (gems.lastDropTimer <= 0.0f) {
                gems.lastDropText.clear();
            }
        }

        const bool enabled = readRegistryBool(baseSystem, "GemDropsEnabled", true);
        if (!enabled) {
            gems.drops.clear();
            gems.lastDropText.clear();
            gems.lastDropTimer = 0.0f;
            return;
        }
        if (gems.drops.empty() || dt <= 0.0f) return;

        const bool despawnEnabled = readRegistryBool(baseSystem, "GemDropDespawnEnabled", false);
        const float gravity = -std::abs(readRegistryFloat(baseSystem, "GemDropGravity", 14.5f));
        const float maxSpeed = std::max(0.25f, readRegistryFloat(baseSystem, "GemDropMaxSpeed", 11.0f));
        const float maxStepDistance = std::max(0.03f, readRegistryFloat(baseSystem, "GemDropMaxStepDistance", 0.20f));
        const float blockHalfExtent = 0.499f;

        size_t i = 0;
        while (i < gems.drops.size()) {
            GemDropState& drop = gems.drops[i];
            if (drop.lockToCell) {
                drop.velocity = glm::vec3(0.0f);
                drop.position = glm::vec3(
                    static_cast<float>(drop.lockedCell.x) + drop.lockedOffsetXZ.x,
                    static_cast<float>(drop.lockedCell.y),
                    static_cast<float>(drop.lockedCell.z) + drop.lockedOffsetXZ.y
                );
                ++i;
                continue;
            }
            drop.age += dt;
            if ((despawnEnabled && drop.age >= drop.lifetime) || drop.position.y < -300.0f) {
                gems.drops[i] = std::move(gems.drops.back());
                gems.drops.pop_back();
                continue;
            }

            // Full-block behavior: keep gems centered in a block column and move vertically under gravity.
            drop.position = snapGemCenterToBlockColumn(drop.position);
            drop.velocity.x = 0.0f;
            drop.velocity.z = 0.0f;
            drop.velocity.y += gravity * dt;
            drop.velocity.y = glm::clamp(drop.velocity.y, -maxSpeed, maxSpeed);

            const float frameTravel = std::abs(drop.velocity.y * dt);
            int substeps = static_cast<int>(std::ceil(frameTravel / maxStepDistance));
            substeps = std::clamp(substeps, 1, 8);
            const float stepDt = dt / static_cast<float>(substeps);

            for (int step = 0; step < substeps; ++step) {
                drop.position.y += drop.velocity.y * stepDt;
                if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                    bool grounded = false;
                    resolveGemBlockVerticalCollision(baseSystem,
                                                     prototypes,
                                                     drop.position,
                                                     drop.velocity,
                                                     blockHalfExtent,
                                                     grounded);
                    if (grounded) {
                        drop.position = snapGemCenterToBlockColumn(drop.position);
                        break;
                    }
                }
            }
            drop.position = snapGemCenterToBlockColumn(drop.position);
            ++i;
        }
    }

    void RenderGems(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)prototypes;
        (void)dt;
        (void)win;
        if (!baseSystem.renderer || !baseSystem.player || !baseSystem.world || !baseSystem.gems) return;
        if (!baseSystem.renderBackend) return;
        if (!readRegistryBool(baseSystem, "GemDropsEnabled", true)) return;

        RendererContext& renderer = *baseSystem.renderer;
        PlayerContext& player = *baseSystem.player;
        WorldContext& world = *baseSystem.world;
        GemContext& gems = *baseSystem.gems;
        auto& renderBackend = *baseSystem.renderBackend;
        const bool despawnEnabled = readRegistryBool(baseSystem, "GemDropDespawnEnabled", false);
        const float renderScale = glm::clamp(readRegistryFloat(baseSystem, "GemDropVisualScale", 1.0f), 0.1f, 100.0f);
        const float gemAlpha = glm::clamp(readRegistryFloat(baseSystem, "GemDropAlpha", 0.58f), 0.05f, 1.0f);
        const bool hasHeldGem = gems.blockModeHoldingGem && !gems.heldDrop.nodes.empty();
        if (gems.drops.empty() && !hasHeldGem) {
            renderer.gemVertexCount = 0;
            return;
        }

        static bool shaderWarned = false;
        if (!renderer.audioRayShader) {
            if (world.shaders.count("AUDIORAY_VERTEX_SHADER") && world.shaders.count("AUDIORAY_FRAGMENT_SHADER")) {
                renderer.audioRayShader = std::make_unique<Shader>(
                    world.shaders["AUDIORAY_VERTEX_SHADER"].c_str(),
                    world.shaders["AUDIORAY_FRAGMENT_SHADER"].c_str()
                );
            } else if (!shaderWarned) {
                shaderWarned = true;
                std::cerr << "GemSystem: audio-ray shader sources not found." << std::endl;
            }
        }
        if (!renderer.audioRayShader) {
            renderer.gemVertexCount = 0;
            return;
        }

        if (renderer.gemVAO == 0) {
            static const std::vector<VertexAttribLayout> kGemVertexLayout = {
                {0, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(GemVertex)), 0, 0},
                {1, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(GemVertex)), sizeof(glm::vec3), 0}
            };
            renderBackend.ensureVertexArray(renderer.gemVAO);
            renderBackend.ensureArrayBuffer(renderer.gemVBO);
            renderBackend.configureVertexArray(renderer.gemVAO, renderer.gemVBO, kGemVertexLayout, 0, {});
            renderBackend.unbindVertexArray();
        }

        std::vector<GemVertex> fillVerts;
        std::vector<GemVertex> lineVerts;
        fillVerts.reserve((gems.drops.size() + (hasHeldGem ? 1u : 0u)) * 96u);
        lineVerts.reserve((gems.drops.size() + (hasHeldGem ? 1u : 0u)) * 24u);
        ensureGemAtlasCacheLoaded(baseSystem);
        const GemAtlasCache& atlasCache = gemAtlasCache();
        const glm::vec3 lightDir = glm::normalize(glm::vec3(0.35f, 0.88f, 0.22f));
        glm::vec3 cameraForward;
        cameraForward.x = std::cos(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
        cameraForward.y = std::sin(glm::radians(player.cameraPitch));
        cameraForward.z = std::sin(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
        if (glm::length(cameraForward) < 0.0001f) cameraForward = glm::vec3(0.0f, 0.0f, -1.0f);
        cameraForward = glm::normalize(cameraForward);

        auto appendGemMesh = [&](const GemDropState& drop,
                                 const glm::vec3& centerPos,
                                 float fade,
                                 float scaleMul,
                                 float brightnessMul) {
            if (fade <= 0.001f) return;

            std::vector<glm::ivec3> tempVoxels;
            const std::vector<glm::ivec3>* voxels = &drop.voxelCells;
            if (voxels->empty()) {
                buildGemVoxelCellsFromMesh(drop.nodes, drop.triangles, kMiniVoxelSize, 4096, tempVoxels);
                voxels = &tempVoxels;
            }
            if (voxels->empty()) return;

            glm::ivec3 minCell(std::numeric_limits<int>::max());
            glm::ivec3 maxCell(std::numeric_limits<int>::min());
            for (const glm::ivec3& cell : *voxels) {
                minCell = glm::min(minCell, cell);
                maxCell = glm::max(maxCell, cell);
            }
            const float spanX = static_cast<float>(std::max(1, maxCell.x - minCell.x + 1));
            const float spanY = static_cast<float>(std::max(1, maxCell.y - minCell.y + 1));
            const float spanZ = static_cast<float>(std::max(1, maxCell.z - minCell.z + 1));

            float gemScale = std::max(0.05f, renderScale * scaleMul);
            const float half = (kMiniVoxelSize * 0.5f) * gemScale;
            const float yaw = 0.0f;
            auto applyUpsideDown = [&](const glm::vec3& v) -> glm::vec3 {
                if (!drop.upsideDown) return v;
                return glm::vec3(v.x, -v.y, -v.z);
            };
            float minFaceY = std::numeric_limits<float>::max();
            for (const glm::ivec3& cell : *voxels) {
                const glm::vec3 flippedCenter = applyUpsideDown(glm::vec3(cell) * (kMiniVoxelSize * gemScale));
                const float centerY = flippedCenter.y;
                minFaceY = std::min(minFaceY, centerY - half);
            }
            const float alignYOffset = (minFaceY < std::numeric_limits<float>::max() * 0.5f)
                ? ((-0.5f + drop.renderYOffset) - minFaceY)
                : 0.0f;

            struct FaceDesc {
                glm::ivec3 neighbor;
                glm::vec3 normal;
                glm::vec3 axisU;
                glm::vec3 axisV;
            };
            const std::array<FaceDesc, 6> faces = {{
                {glm::ivec3( 1, 0, 0), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)},
                {glm::ivec3(-1, 0, 0), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f,-1.0f)},
                {glm::ivec3( 0, 1, 0), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f,-1.0f)},
                {glm::ivec3( 0,-1, 0), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)},
                {glm::ivec3( 0, 0, 1), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)},
                {glm::ivec3( 0, 0,-1), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(-1.0f,0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)}
            }};

            std::unordered_set<int64_t> occupied;
            occupied.reserve(voxels->size() * 2u + 1u);
            for (const glm::ivec3& cell : *voxels) {
                occupied.insert(packVoxelCoord(cell.x, cell.y, cell.z));
            }

            const glm::vec3 wireColor(0.02f, 0.02f, 0.02f);
            for (const glm::ivec3& cell : *voxels) {
                const glm::vec3 localCenter = applyUpsideDown(glm::vec3(cell) * (kMiniVoxelSize * gemScale))
                    + glm::vec3(0.0f, alignYOffset, 0.0f);
                const glm::vec3 worldCenter = centerPos + rotateY(localCenter, yaw);

                for (const FaceDesc& face : faces) {
                    const glm::ivec3 neighbor = cell + face.neighbor;
                    if (occupied.find(packVoxelCoord(neighbor.x, neighbor.y, neighbor.z)) != occupied.end()) {
                        continue;
                    }

                    const glm::vec3 nrm = rotateY(applyUpsideDown(face.normal), yaw);
                    const glm::vec3 axisU = rotateY(applyUpsideDown(face.axisU), yaw);
                    const glm::vec3 axisV = rotateY(applyUpsideDown(face.axisV), yaw);
                    const glm::vec3 n = nrm * half;
                    const glm::vec3 u = axisU * half;
                    const glm::vec3 v = axisV * half;

                    const glm::vec3 p0 = worldCenter + n - u - v;
                    const glm::vec3 p1 = worldCenter + n + u - v;
                    const glm::vec3 p2 = worldCenter + n + u + v;
                    const glm::vec3 p3 = worldCenter + n - u + v;

                    float uvU = 0.5f;
                    float uvV = 0.5f;
                    if (face.neighbor.x != 0) {
                        uvU = (static_cast<float>(cell.z - minCell.z) + 0.5f) / spanZ;
                        uvV = (static_cast<float>(cell.y - minCell.y) + 0.5f) / spanY;
                        if (face.neighbor.x < 0) uvU = 1.0f - uvU;
                    } else if (face.neighbor.y != 0) {
                        uvU = (static_cast<float>(cell.x - minCell.x) + 0.5f) / spanX;
                        uvV = (static_cast<float>(cell.z - minCell.z) + 0.5f) / spanZ;
                        if (face.neighbor.y < 0) uvV = 1.0f - uvV;
                    } else {
                        uvU = (static_cast<float>(cell.x - minCell.x) + 0.5f) / spanX;
                        uvV = (static_cast<float>(cell.y - minCell.y) + 0.5f) / spanY;
                        if (face.neighbor.z < 0) uvU = 1.0f - uvU;
                    }
                    glm::vec3 texColor = sampleGemTextureColor(atlasCache, drop.kind, uvU, uvV);
                    texColor = glm::clamp(
                        texColor * (0.78f + 0.22f * fade) * std::max(0.0f, brightnessMul),
                        glm::vec3(0.0f),
                        glm::vec3(1.0f)
                    );

                    float ndl = std::max(0.0f, glm::dot(nrm, lightDir));
                    float shade = 0.42f + 0.58f * ndl;
                    const glm::vec3 faceColor = glm::clamp(texColor * shade, glm::vec3(0.0f), glm::vec3(1.0f));
                    pushTri(fillVerts, p0, p1, p2, faceColor);
                    pushTri(fillVerts, p0, p2, p3, faceColor);

                    pushLine(lineVerts, p0, p1, wireColor);
                    pushLine(lineVerts, p1, p2, wireColor);
                    pushLine(lineVerts, p2, p3, wireColor);
                    pushLine(lineVerts, p3, p0, wireColor);
                }
            }
        };

        for (const GemDropState& drop : gems.drops) {
            float lifeT = despawnEnabled
                ? glm::clamp(drop.age / std::max(0.001f, drop.lifetime), 0.0f, 1.0f)
                : 0.0f;
            float fade = despawnEnabled ? (1.0f - lifeT) : 1.0f;
            appendGemMesh(drop, drop.position, fade, 1.0f, 1.0f);
        }

        if (hasHeldGem) {
            if (gems.placementPreviewActive) {
                GemDropState previewDrop = gems.heldDrop;
                previewDrop.renderYOffset = gems.placementPreviewRenderYOffset;
                appendGemMesh(previewDrop, gems.placementPreviewPosition, 1.0f, 1.0f, 0.28f);
            } else {
                const float heldScale = glm::clamp(readRegistryFloat(baseSystem, "GemHeldScale", 0.95f), 0.2f, 2.0f);
                const float heldItemForward = glm::clamp(
                    readRegistryFloat(baseSystem, "HeldItemViewForward", 0.8f),
                    0.2f,
                    2.5f
                );
                const float heldItemVertical = glm::clamp(
                    readRegistryFloat(baseSystem, "HeldItemViewVertical", 0.08f),
                    -1.0f,
                    1.0f
                );
                glm::vec3 heldPos = player.cameraPosition
                    + cameraForward * heldItemForward
                    + glm::vec3(0.0f, heldItemVertical, 0.0f);
                appendGemMesh(gems.heldDrop, heldPos, 1.0f, heldScale, 1.0f);
            }
        }

        const int fillCount = static_cast<int>(fillVerts.size());
        const int lineCount = static_cast<int>(lineVerts.size());
        renderer.gemVertexCount = fillCount + lineCount;
        if (renderer.gemVertexCount <= 0) return;

        auto setDepthTestEnabled = [&](bool enabled) {
            renderBackend.setDepthTestEnabled(enabled);
        };
        auto setDepthWriteEnabled = [&](bool enabled) {
            renderBackend.setDepthWriteEnabled(enabled);
        };
        auto setBlendEnabled = [&](bool enabled) {
            renderBackend.setBlendEnabled(enabled);
        };
        auto setBlendEquationAdd = [&]() {
            renderBackend.setBlendEquationAdd();
        };
        auto setBlendModeConstantAlpha = [&](float alpha) {
            renderBackend.setBlendModeConstantAlpha(alpha);
        };
        auto setBlendModeAlpha = [&]() {
            renderBackend.setBlendModeAlpha();
        };

        setDepthWriteEnabled(true);
        setDepthTestEnabled(true);

        renderer.audioRayShader->use();
        renderer.audioRayShader->setMat4("view", player.viewMatrix);
        renderer.audioRayShader->setMat4("projection", player.projectionMatrix);
        renderBackend.bindVertexArray(renderer.gemVAO);
        if (fillCount > 0 || lineCount > 0) {
            std::vector<GemVertex> upload;
            upload.reserve(static_cast<size_t>(fillCount + lineCount));
            upload.insert(upload.end(), fillVerts.begin(), fillVerts.end());
            upload.insert(upload.end(), lineVerts.begin(), lineVerts.end());
            renderBackend.uploadArrayBufferData(
                renderer.gemVBO,
                upload.data(),
                upload.size() * sizeof(GemVertex),
                true
            );
            if (fillCount > 0) {
                // Gems render as a translucent material while preserving depth test
                // against world geometry.
                setBlendEnabled(true);
                setBlendEquationAdd();
                setBlendModeConstantAlpha(gemAlpha);
                setDepthWriteEnabled(false);
                renderBackend.drawArraysTriangles(0, fillCount);
                setDepthWriteEnabled(true);
                setBlendEnabled(false);
                setBlendModeAlpha();
            }
            if (lineCount > 0) {
                renderBackend.setLineWidth(1.0f);
                renderBackend.drawArraysLines(fillCount, lineCount);
            }
        }

        renderBackend.unbindVertexArray();
    }
}
