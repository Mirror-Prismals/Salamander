#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>

namespace HostLogic { const Entity* findPrototype(const std::string& name, const std::vector<Entity>& prototypes); EntityInstance CreateInstance(BaseSystem& baseSystem, int prototypeID, glm::vec3 position, glm::vec3 color); }
namespace ExpanseBiomeSystemLogic { bool SampleTerrain(const WorldContext& worldCtx, float x, float z, float& outHeight); }
namespace ChunkSystemLogic { void MarkChunkDirty(BaseSystem& baseSystem, int worldIndex, const glm::vec3& position); }
namespace BlockSelectionSystemLogic { void InvalidateWorldCache(int worldIndex); }

namespace TreeGenerationSystemLogic {

    namespace {
        int floorDivInt(int value, int divisor) {
            if (divisor <= 0) return 0;
            if (value >= 0) return value / divisor;
            return -(((-value) + divisor - 1) / divisor);
        }

        int chunkIndexFromCoord(float position, int chunkSize) {
            int cell = static_cast<int>(std::floor(position));
            return floorDivInt(cell, chunkSize);
        }

        struct TreeChunkKey {
            int x = 0;
            int z = 0;
            bool operator==(const TreeChunkKey& other) const noexcept {
                return x == other.x && z == other.z;
            }
        };

        struct TreeChunkKeyHash {
            std::size_t operator()(const TreeChunkKey& k) const noexcept {
                std::size_t hx = std::hash<int>()(k.x);
                std::size_t hz = std::hash<int>()(k.z);
                return hx ^ (hz << 1);
            }
        };

        struct TreeChunkData {
            std::vector<EntityInstance> instances;
            int minY = 0;
            int maxY = 0;
        };

        struct TreeState {
            std::unordered_map<TreeChunkKey, TreeChunkData, TreeChunkKeyHash> chunks;
            std::string levelKey;
        };

        static TreeState g_treeState;

        int findWorldIndexByName(const LevelContext& level, const std::string& name) {
            for (size_t i = 0; i < level.worlds.size(); ++i) {
                if (level.worlds[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        int hashPair(int x, int z, int mulX, int mulZ) {
            int h = (x * mulX) ^ (z * mulZ);
            return std::abs(h);
        }

        glm::vec3 resolveColor(const WorldContext& world, const std::string& name, const glm::vec3& fallback) {
            auto it = world.colorLibrary.find(name);
            if (it != world.colorLibrary.end()) return it->second;
            return fallback;
        }

        bool treeCollision(const std::vector<glm::vec3>& trunkArray, const glm::vec3& base) {
            for (const auto& p : trunkArray) {
                if (glm::distance(p, base) < 3.0f) return true;
            }
            return false;
        }

        std::vector<glm::vec3> generatePineCanopy(int groundHeight, int effectiveTrunkHeight, int trunkThickness, float worldX, float worldZ) {
            std::vector<glm::vec3> leafPositions;
            int canopyOffset = 70;
            int canopyLayers = 80;
            int canopyBase = groundHeight + effectiveTrunkHeight - canopyOffset;
            float bottomRadius = 8.0f;
            float topRadius = 2.0f;
            float centerOffset = (trunkThickness - 1) / 2.0f;

            for (int layer = 0; layer < canopyLayers; ++layer) {
                float currentRadius = bottomRadius - layer * ((bottomRadius - topRadius) / (canopyLayers - 1));
                int yPos = canopyBase + layer;
                int range = static_cast<int>(std::ceil(currentRadius));
                for (int dx = -range; dx <= range; ++dx) {
                    for (int dz = -range; dz <= range; ++dz) {
                        float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
                        if (dist <= currentRadius) {
                            leafPositions.push_back(glm::vec3(worldX + centerOffset + dx, yPos, worldZ + centerOffset + dz));
                        }
                    }
                }
            }
            return leafPositions;
        }

        std::vector<glm::vec3> generateFirCanopy(int groundHeight, int trunkHeight, int trunkThickness, float worldX, float worldZ) {
            std::vector<glm::vec3> leafPositions;
            int centerY = groundHeight + trunkHeight;
            float radius = 7.0f;
            for (int dy = -static_cast<int>(radius); dy <= static_cast<int>(radius); ++dy) {
                for (int dx = -static_cast<int>(radius); dx <= static_cast<int>(radius); ++dx) {
                    for (int dz = -static_cast<int>(radius); dz <= static_cast<int>(radius); ++dz) {
                        float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy + dz * dz));
                        if (dist < radius) {
                            leafPositions.push_back(glm::vec3(worldX + trunkThickness / 2.0f + dx, centerY + dy, worldZ + trunkThickness / 2.0f + dz));
                        }
                    }
                }
            }
            return leafPositions;
        }

        std::vector<glm::vec3> generateOakCanopy(int groundHeight, int trunkHeight, int trunkThickness, float worldX, float worldZ) {
            std::vector<glm::vec3> leaves;
            int centerY = groundHeight + trunkHeight + 2;
            float radius = 4.0f;
            float centerOffset = trunkThickness / 2.0f;
            for (int dy = -static_cast<int>(radius); dy <= static_cast<int>(radius); ++dy) {
                for (int dx = -static_cast<int>(radius); dx <= static_cast<int>(radius); ++dx) {
                    for (int dz = -static_cast<int>(radius); dz <= static_cast<int>(radius); ++dz) {
                        float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy + dz * dz));
                        if (dist < radius) {
                            leaves.push_back(glm::vec3(worldX + centerOffset + dx, centerY + dy, worldZ + centerOffset + dz));
                        }
                    }
                }
            }
            return leaves;
        }

        TreeChunkData GenerateTreeColumn(BaseSystem& baseSystem,
                                         std::vector<Entity>& prototypes,
                                         WorldContext& worldCtx,
                                         const ExpanseConfig& cfg,
                                         int chunkX,
                                         int chunkZ,
                                         const glm::ivec3& chunkSize) {
            TreeChunkData out;
            out.minY = std::numeric_limits<int>::max();
            out.maxY = std::numeric_limits<int>::min();

            const Entity* blockProto = HostLogic::findPrototype("Block", prototypes);
            const Entity* leafProto = HostLogic::findPrototype("Leaf", prototypes);
            const Entity* branchProto = HostLogic::findPrototype("Branch", prototypes);
            if (!blockProto || !leafProto || !branchProto) {
                std::cerr << "TreeGenerationSystem: missing Block/Leaf/Branch prototypes." << std::endl;
                out.minY = cfg.minY;
                out.maxY = cfg.minY;
                return out;
            }

            glm::vec3 woodColor = resolveColor(worldCtx, cfg.colorWood, glm::vec3(0.4f, 0.2f, 0.1f));
            glm::vec3 leafColor = resolveColor(worldCtx, cfg.colorLeaf, glm::vec3(0.1f, 0.7f, 0.2f));

            std::vector<glm::vec3> pineTrunks;
            std::vector<glm::vec3> oakTrunks;
            std::vector<glm::vec3> ancientTrunks;

            auto addInstance = [&](int protoID, const glm::vec3& pos, const glm::vec3& color, float rotation) {
                EntityInstance inst = HostLogic::CreateInstance(baseSystem, protoID, pos, color);
                inst.rotation = rotation;
                out.instances.push_back(inst);
                int y = static_cast<int>(std::floor(pos.y));
                out.minY = std::min(out.minY, y);
                out.maxY = std::max(out.maxY, y);
            };

            auto addBlock = [&](int protoID, const glm::vec3& pos, const glm::vec3& color) {
                addInstance(protoID, pos, color, 0.0f);
            };

            for (int localX = 0; localX < chunkSize.x; ++localX) {
                for (int localZ = 0; localZ < chunkSize.z; ++localZ) {
                    float worldX = static_cast<float>(chunkX * chunkSize.x + localX);
                    float worldZ = static_cast<float>(chunkZ * chunkSize.z + localZ);
                    float height = 0.0f;
                    bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(worldCtx, worldX, worldZ, height);

                    if (!isLand) {
                        if (localX > 3 && localX < chunkSize.x - 3 && localZ > 3 && localZ < chunkSize.z - 3 &&
                            (localX % 7 == 3) && (localZ % 7 == 3)) {
                            bool canPlaceLily = true;
                            for (int dx = -3; dx <= 3; ++dx) {
                                for (int dz = -3; dz <= 3; ++dz) {
                                    float neighborHeight = 0.0f;
                                    if (ExpanseBiomeSystemLogic::SampleTerrain(worldCtx, worldX + dx, worldZ + dz, neighborHeight)) {
                                        canPlaceLily = false;
                                        break;
                                    }
                                }
                                if (!canPlaceLily) break;
                            }
                            if (canPlaceLily) {
                                int hashVal = hashPair(static_cast<int>(worldX), static_cast<int>(worldZ), 91321, 7817);
                                if (hashVal % 100 < 1) {
                                    for (int dx = -6; dx < 6; ++dx) {
                                        for (int dz = -6; dz < 6; ++dz) {
                                            if ((dx <= -5 || dx >= 4) && (dz <= -5 || dz >= 4)) continue;
                                            addBlock(leafProto->prototypeID, glm::vec3(worldX + dx, cfg.waterSurface, worldZ + dz), leafColor);
                                        }
                                    }
                                }
                            }
                        }
                        continue;
                    }

                    if (height <= 2.0f) continue;
                    int groundHeight = static_cast<int>(std::floor(height));
                    int intWorldX = static_cast<int>(worldX);
                    int intWorldZ = static_cast<int>(worldZ);

                    int trunkHeight = 80;
                    int trunkThickness = 4;
                    int extraBottom = 15;
                    int extraHeight = 90;

                    if (worldZ <= -40.0f) {
                        int hashValPine = hashPair(intWorldX, intWorldZ, 73856093, 19349663);
                        glm::vec3 pineBase(worldX, groundHeight + 1, worldZ);
                        if (hashValPine % 2000 < 1 && !treeCollision(pineTrunks, pineBase)) {
                            for (int i = 1; i <= trunkHeight; ++i) {
                                for (int tx = 0; tx < trunkThickness; ++tx) {
                                    for (int tz = 0; tz < trunkThickness; ++tz) {
                                        glm::vec3 pos(worldX + tx, groundHeight + i, worldZ + tz);
                                        pineTrunks.push_back(pos);
                                        addBlock(blockProto->prototypeID, pos, woodColor);
                                    }
                                }
                            }
                            for (int i = 0; i < extraBottom; ++i) {
                                for (int tx = 0; tx < trunkThickness; ++tx) {
                                    for (int tz = 0; tz < trunkThickness; ++tz) {
                                        glm::vec3 pos(worldX + tx, groundHeight - i, worldZ + tz);
                                        pineTrunks.push_back(pos);
                                        addBlock(blockProto->prototypeID, pos, woodColor);
                                    }
                                }
                            }
                            for (int i = trunkHeight + 1; i <= trunkHeight + extraHeight; ++i) {
                                for (int tx = 0; tx < trunkThickness; ++tx) {
                                    for (int tz = 0; tz < trunkThickness; ++tz) {
                                        glm::vec3 pos(worldX + tx, groundHeight + i, worldZ + tz);
                                        pineTrunks.push_back(pos);
                                        addBlock(blockProto->prototypeID, pos, woodColor);
                                    }
                                }
                            }
                            std::vector<glm::vec3> canopy = generatePineCanopy(groundHeight, trunkHeight, trunkThickness, worldX, worldZ);
                            for (const auto& leaf : canopy) {
                                addBlock(leafProto->prototypeID, leaf, leafColor);
                            }
                        }
                    } else if (worldX < 20.0f || worldZ >= 40.0f) {
                        if (worldZ < 40.0f) {
                            int hashValPine = hashPair(intWorldX, intWorldZ, 73856093, 19349663);
                            glm::vec3 pineBase(worldX, groundHeight + 1, worldZ);
                            if (hashValPine % 2000 < 1 && !treeCollision(pineTrunks, pineBase)) {
                                for (int i = 1; i <= trunkHeight; ++i) {
                                    for (int tx = 0; tx < trunkThickness; ++tx) {
                                        for (int tz = 0; tz < trunkThickness; ++tz) {
                                            glm::vec3 pos(worldX + tx, groundHeight + i, worldZ + tz);
                                            if (i > trunkHeight - 1) {
                                                addBlock(leafProto->prototypeID, pos, leafColor);
                                            } else {
                                                pineTrunks.push_back(pos);
                                                addBlock(blockProto->prototypeID, pos, woodColor);
                                            }
                                        }
                                    }
                                }
                                for (int i = 0; i < extraBottom; ++i) {
                                    for (int tx = 0; tx < trunkThickness; ++tx) {
                                        for (int tz = 0; tz < trunkThickness; ++tz) {
                                            glm::vec3 pos(worldX + tx, groundHeight - i, worldZ + tz);
                                            pineTrunks.push_back(pos);
                                            addBlock(blockProto->prototypeID, pos, woodColor);
                                        }
                                    }
                                }
                                std::vector<glm::vec3> canopy = generatePineCanopy(groundHeight, trunkHeight, trunkThickness, worldX, worldZ);
                                for (const auto& leaf : canopy) {
                                    addBlock(leafProto->prototypeID, leaf, leafColor);
                                }
                            }
                        }

                        int hashValFir = hashPair(intWorldX, intWorldZ, 83492791, 19349663);
                        glm::vec3 firBase(worldX, groundHeight + 1, worldZ);
                        if (hashValFir % 2000 < 1 && !treeCollision(pineTrunks, firBase)) {
                            int trunkHeightFir = 40;
                            int trunkThicknessFir = 3;
                            for (int i = 1; i <= trunkHeightFir; ++i) {
                                for (int tx = 0; tx < trunkThicknessFir; ++tx) {
                                    for (int tz = 0; tz < trunkThicknessFir; ++tz) {
                                        glm::vec3 pos(worldX + tx, groundHeight + i, worldZ + tz);
                                        pineTrunks.push_back(pos);
                                        addBlock(blockProto->prototypeID, pos, woodColor);
                                    }
                                }
                            }
                            std::vector<glm::vec3> firCanopy = generateFirCanopy(groundHeight, trunkHeightFir, trunkThicknessFir, worldX, worldZ);
                            for (const auto& leaf : firCanopy) {
                                addBlock(leafProto->prototypeID, leaf, leafColor);
                            }
                        }

                        int hashValOak = hashPair(intWorldX, intWorldZ, 92821, 123457);
                        glm::vec3 oakBase(worldX, groundHeight + 1, worldZ);
                        if (hashValOak % 1000 < 1 && !treeCollision(oakTrunks, oakBase)) {
                            int trunkHeightOak = 7;
                            int trunkThicknessOak = 2;
                            for (int i = 1; i <= trunkHeightOak; ++i) {
                                for (int tx = 0; tx < trunkThicknessOak; ++tx) {
                                    for (int tz = 0; tz < trunkThicknessOak; ++tz) {
                                        glm::vec3 pos(worldX + tx, groundHeight + i, worldZ + tz);
                                        oakTrunks.push_back(pos);
                                        addBlock(blockProto->prototypeID, pos, woodColor);
                                    }
                                }
                            }
                            std::vector<glm::vec3> oakCanopy = generateOakCanopy(groundHeight, trunkHeightOak, trunkThicknessOak, worldX, worldZ);
                            for (const auto& leaf : oakCanopy) {
                                addBlock(leafProto->prototypeID, leaf, leafColor);
                            }
                        }

                        int hashValAncient = hashPair(intWorldX, intWorldZ, 112233, 445566);
                        glm::vec3 ancientBase(worldX, groundHeight + 1, worldZ);
                        if (hashValAncient % 3000 < 1 && !treeCollision(ancientTrunks, ancientBase)) {
                            int trunkHeightAncient = 30;
                            int trunkThicknessAncient = 3;
                            for (int i = 1; i <= trunkHeightAncient; ++i) {
                                for (int tx = 0; tx < trunkThicknessAncient; ++tx) {
                                    for (int tz = 0; tz < trunkThicknessAncient; ++tz) {
                                        glm::vec3 pos(worldX + tx, groundHeight + i, worldZ + tz);
                                        ancientTrunks.push_back(pos);
                                        addBlock(blockProto->prototypeID, pos, woodColor);
                                    }
                                }
                            }
                            int centerY = groundHeight + trunkHeightAncient;
                            float canopyRadius = 5.0f;
                            for (int dy = -static_cast<int>(canopyRadius); dy <= static_cast<int>(canopyRadius); ++dy) {
                                for (int dx = -static_cast<int>(canopyRadius); dx <= static_cast<int>(canopyRadius); ++dx) {
                                    for (int dz = -static_cast<int>(canopyRadius); dz <= static_cast<int>(canopyRadius); ++dz) {
                                        float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy + dz * dz));
                                        if (dist < canopyRadius) {
                                            addBlock(leafProto->prototypeID,
                                                     glm::vec3(worldX + trunkThicknessAncient / 2.0f + dx, centerY + dy, worldZ + trunkThicknessAncient / 2.0f + dz),
                                                     leafColor);
                                        }
                                    }
                                }
                            }

                            int branchBaseHeights[4] = {7, 13, 19, 25};
                            for (int b = 0; b < 4; ++b) {
                                int seed = hashValAncient ^ (b * 92821);
                                int randomOffset = (seed % 3) - 1;
                                int branchStart = branchBaseHeights[b] + randomOffset;
                                float branchRot = (b * 90.0f) * (3.14159f / 180.0f);
                                glm::vec3 branchStartPos(worldX + trunkThicknessAncient / 2.0f, groundHeight + branchStart, worldZ + trunkThicknessAncient / 2.0f);
                                int branchLength = 10 + (seed % 3);
                                for (int i = 1; i <= branchLength; ++i) {
                                    float bx = std::cos(branchRot) * i;
                                    float bz = std::sin(branchRot) * i;
                                    glm::vec3 branchPos = branchStartPos + glm::vec3(bx, 0.0f, bz);
                                    addBlock(blockProto->prototypeID, branchPos, woodColor);
                                }
                                glm::vec3 tip = branchStartPos + glm::vec3(std::cos(branchRot) * (branchLength + 1), 0.0f, std::sin(branchRot) * (branchLength + 1));
                                for (int dx = -1; dx <= 1; ++dx) {
                                    for (int dy = -1; dy <= 1; ++dy) {
                                        for (int dz = -1; dz <= 1; ++dz) {
                                            if (glm::length(glm::vec3(dx, dy, dz)) < 1.5f) {
                                                addBlock(leafProto->prototypeID, tip + glm::vec3(dx, dy, dz), leafColor);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    int hashValFallen = hashPair(intWorldX, intWorldZ, 92821, 68917);
                    bool nearWater = false;
                    for (int dx = -1; dx <= 1; ++dx) {
                        for (int dz = -1; dz <= 1; ++dz) {
                            float neighborHeight = 0.0f;
                            if (!ExpanseBiomeSystemLogic::SampleTerrain(worldCtx, worldX + dx, worldZ + dz, neighborHeight)) {
                                nearWater = true;
                                break;
                            }
                        }
                        if (nearWater) break;
                    }
                    if (nearWater && hashValFallen % 500 < 1) {
                        int maxSearch = 20;
                        float angle = static_cast<float>(hashValFallen % 360);
                        float rad = glm::radians(angle);
                        int backLength = 0;
                        for (; backLength < maxSearch; ++backLength) {
                            float sampleX = worldX - (backLength + 1) * std::cos(rad);
                            float sampleZ = worldZ - (backLength + 1) * std::sin(rad);
                            float sampleHeight = 0.0f;
                            if (!ExpanseBiomeSystemLogic::SampleTerrain(worldCtx, sampleX, sampleZ, sampleHeight)) break;
                        }
                        int forwardLength = 0;
                        for (; forwardLength < maxSearch; ++forwardLength) {
                            float sampleX = worldX + (forwardLength + 1) * std::cos(rad);
                            float sampleZ = worldZ + (forwardLength + 1) * std::sin(rad);
                            float sampleHeight = 0.0f;
                            if (!ExpanseBiomeSystemLogic::SampleTerrain(worldCtx, sampleX, sampleZ, sampleHeight)) break;
                        }
                        int totalLength = backLength + forwardLength + 1;
                        if (totalLength >= 6) {
                            int thickness = 2;
                            for (int i = 0; i < totalLength; ++i) {
                                float posX = worldX - backLength * std::cos(rad) + i * std::cos(rad);
                                float posZ = worldZ - backLength * std::sin(rad) + i * std::sin(rad);
                                for (int tx = 0; tx < thickness; ++tx) {
                                    for (int tz = 0; tz < thickness; ++tz) {
                                        float localX = posX + tx - thickness / 2.0f;
                                        float localZ = posZ + tz - thickness / 2.0f;
                                        addBlock(blockProto->prototypeID, glm::vec3(localX, groundHeight + 1, localZ), woodColor);
                                    }
                                }
                            }
                        }
                    }

                    int hashValPile = hashPair(intWorldX, intWorldZ, 412871, 167591);
                    if (hashValPile % 300 < 1) {
                        int pileSize = (hashValPile % 4) + 3;
                        for (int i = 0; i < pileSize; ++i) {
                            int px = (hashValPile + i * 13) % 3 - 1;
                            int pz = (hashValPile + i * 7) % 3 - 1;
                            float placeX = worldX + px;
                            float placeZ = worldZ + pz;
                            addBlock(leafProto->prototypeID, glm::vec3(placeX, groundHeight + 1, placeZ), leafColor);
                        }
                    }

                    int hashValBushSmall = hashPair(intWorldX, intWorldZ, 17771, 55117);
                    if (hashValBushSmall % 700 < 1) {
                        int centerY = groundHeight + 1;
                        float radius = 1.0f;
                        for (int dx = -1; dx <= 1; ++dx) {
                            for (int dz = -1; dz <= 1; ++dz) {
                                if (glm::length(glm::vec2(dx, dz)) <= radius) {
                                    addBlock(leafProto->prototypeID, glm::vec3(worldX + dx, centerY, worldZ + dz), leafColor);
                                }
                            }
                        }
                    }

                    int hashValBushMed = hashPair(intWorldX, intWorldZ, 18323, 51511);
                    if (hashValBushMed % 1000 < 2) {
                        int centerY = groundHeight + 1;
                        float radius = 2.0f;
                        for (int dx = -2; dx <= 2; ++dx) {
                            for (int dz = -2; dz <= 2; ++dz) {
                                if (glm::length(glm::vec2(dx, dz)) <= radius) {
                                    addBlock(leafProto->prototypeID, glm::vec3(worldX + dx, centerY, worldZ + dz), leafColor);
                                }
                            }
                        }
                    }

                    int hashValBushLarge = hashPair(intWorldX, intWorldZ, 23719, 41389);
                    if (hashValBushLarge % 1200 < 1) {
                        int centerY = groundHeight + 1;
                        float radius = 3.0f;
                        for (int dx = -3; dx <= 3; ++dx) {
                            for (int dz = -3; dz <= 3; ++dz) {
                                if (glm::length(glm::vec2(dx, dz)) <= radius) {
                                    addBlock(leafProto->prototypeID, glm::vec3(worldX + dx, centerY, worldZ + dz), leafColor);
                                }
                            }
                        }
                    }

                    int hashValBranch = hashPair(intWorldX, intWorldZ, 12345, 6789);
                    if (hashValBranch % 1000 < 1) {
                        float rot = static_cast<float>(hashValBranch % 360);
                        addInstance(branchProto->prototypeID, glm::vec3(worldX + 0.5f, groundHeight + 0.5f, worldZ + 0.5f), woodColor, rot);
                    }
                }
            }

            if (out.minY == std::numeric_limits<int>::max()) {
                out.minY = cfg.minY;
                out.maxY = cfg.minY;
            }
            return out;
        }

        void RebuildWorldInstances(Entity& world, const std::unordered_map<TreeChunkKey, TreeChunkData, TreeChunkKeyHash>& chunks) {
            world.instances.clear();
            for (const auto& [_, chunk] : chunks) {
                world.instances.insert(world.instances.end(), chunk.instances.begin(), chunk.instances.end());
            }
        }

        void MarkColumnDirty(BaseSystem& baseSystem,
                             int worldIndex,
                             int chunkX,
                             int chunkZ,
                             int minY,
                             int maxY,
                             const glm::ivec3& chunkSize) {
            int minChunkY = floorDivInt(minY, chunkSize.y);
            int maxChunkY = floorDivInt(maxY, chunkSize.y);
            for (int cy = minChunkY; cy <= maxChunkY; ++cy) {
                glm::vec3 marker(
                    static_cast<float>(chunkX * chunkSize.x),
                    static_cast<float>(cy * chunkSize.y),
                    static_cast<float>(chunkZ * chunkSize.z)
                );
                ChunkSystemLogic::MarkChunkDirty(baseSystem, worldIndex, marker);
            }
        }
    }

    void UpdateExpanseTrees(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt; (void)win;
        if (!baseSystem.level || !baseSystem.instance || !baseSystem.world || !baseSystem.player || !baseSystem.chunk) return;
        WorldContext& worldCtx = *baseSystem.world;
        if (!worldCtx.expanse.loaded) return;

        std::string levelKey;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("level");
            if (it != baseSystem.registry->end() && std::holds_alternative<std::string>(it->second)) {
                levelKey = std::get<std::string>(it->second);
            }
        }
        if (g_treeState.levelKey != levelKey) {
            g_treeState.levelKey = levelKey;
            g_treeState.chunks.clear();
        }

        LevelContext& level = *baseSystem.level;
        int treeWorldIndex = findWorldIndexByName(level, worldCtx.expanse.treesWorld);
        if (treeWorldIndex < 0) return;

        ChunkContext& chunkCtx = *baseSystem.chunk;
        int renderDist = chunkCtx.renderDistanceChunks > 0 ? chunkCtx.renderDistanceChunks : 6;
        int unloadDist = chunkCtx.unloadDistanceChunks > renderDist ? chunkCtx.unloadDistanceChunks : renderDist + 1;

        int centerChunkX = chunkIndexFromCoord(baseSystem.player->cameraPosition.x, chunkCtx.chunkSize.x);
        int centerChunkZ = chunkIndexFromCoord(baseSystem.player->cameraPosition.z, chunkCtx.chunkSize.z);

        std::unordered_set<TreeChunkKey, TreeChunkKeyHash> desired;
        desired.reserve(static_cast<size_t>((renderDist * 2 + 1) * (renderDist * 2 + 1)));
        int renderDistSq = renderDist * renderDist;
        for (int dx = -renderDist; dx <= renderDist; ++dx) {
            for (int dz = -renderDist; dz <= renderDist; ++dz) {
                if (dx * dx + dz * dz > renderDistSq) continue;
                desired.insert({centerChunkX + dx, centerChunkZ + dz});
            }
        }

        struct DirtyColumn { TreeChunkKey key; int minY; int maxY; };
        std::vector<DirtyColumn> dirtyColumns;
        bool changed = false;

        int unloadDistSq = unloadDist * unloadDist;
        for (auto it = g_treeState.chunks.begin(); it != g_treeState.chunks.end();) {
            int dx = it->first.x - centerChunkX;
            int dz = it->first.z - centerChunkZ;
            if (dx * dx + dz * dz > unloadDistSq) {
                dirtyColumns.push_back({it->first, it->second.minY, it->second.maxY});
                it = g_treeState.chunks.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }

        for (const auto& key : desired) {
            if (g_treeState.chunks.find(key) != g_treeState.chunks.end()) continue;
            TreeChunkData chunk = GenerateTreeColumn(
                baseSystem, prototypes, worldCtx, worldCtx.expanse, key.x, key.z, chunkCtx.chunkSize
            );
            dirtyColumns.push_back({key, chunk.minY, chunk.maxY});
            g_treeState.chunks.emplace(key, std::move(chunk));
            changed = true;
        }

        if (!changed) return;

        Entity& treeWorld = level.worlds[treeWorldIndex];
        RebuildWorldInstances(treeWorld, g_treeState.chunks);
        BlockSelectionSystemLogic::InvalidateWorldCache(treeWorldIndex);

        for (const auto& dirty : dirtyColumns) {
            MarkColumnDirty(baseSystem, treeWorldIndex, dirty.key.x, dirty.key.z, dirty.minY, dirty.maxY, chunkCtx.chunkSize);
        }
    }
}
