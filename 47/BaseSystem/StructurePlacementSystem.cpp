#pragma once

#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <iostream>
#include <glm/glm.hpp>

namespace HostLogic { const Entity* findPrototype(const std::string& name, const std::vector<Entity>& prototypes); EntityInstance CreateInstance(BaseSystem& baseSystem, int prototypeID, glm::vec3 position, glm::vec3 color); }
namespace BlockSelectionSystemLogic { void RemoveBlockFromCache(BaseSystem& baseSystem, const std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position); void AddBlockToCache(BaseSystem& baseSystem, std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position, int prototypeID); }

namespace StructurePlacementSystemLogic {

    struct BenchDef {
        std::string id;
        std::string worldName;
        glm::ivec3 minCorner{0};
        glm::ivec3 size{0};
        std::string outputPath;
        int worldIndex = -1;
        bool loaded = false;
    };

    static std::vector<BenchDef> g_benches;
    static bool g_configLoaded = false;

    namespace fs = std::filesystem;

    namespace {
        glm::ivec3 vecFromJson(const json& arr) {
            if (!arr.is_array() || arr.size() != 3) return glm::ivec3(0);
            return glm::ivec3(arr[0].get<int>(), arr[1].get<int>(), arr[2].get<int>());
        }

        void loadConfig() {
            if (g_configLoaded) return;
            g_benches.clear();
            fs::path dir("Procedures/Scaffolding");
            if (!fs::exists(dir)) {
                std::cerr << "StructurePlacementSystem: Procedures/Scaffolding directory missing\n";
                g_configLoaded = true;
                return;
            }
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
                try {
                    std::ifstream f(entry.path());
                    if (!f.is_open()) {
                        std::cerr << "StructurePlacementSystem: cannot open " << entry.path() << "\n";
                        continue;
                    }
                    json benchData = json::parse(f);
                    BenchDef bench;
                    bench.id = benchData.value("id", entry.path().stem().string());
                    bench.worldName = benchData.value("world", "");
                    if (benchData.contains("bounds")) {
                        auto bounds = benchData["bounds"];
                        if (bounds.contains("min")) bench.minCorner = vecFromJson(bounds["min"]);
                        if (bounds.contains("size")) bench.size = vecFromJson(bounds["size"]);
                    }
                    bench.outputPath = benchData.value("output", "");
                    bool placementEnabled = benchData.value("placement", true);
                    if (!placementEnabled) continue;
                    if (!bench.id.empty() && !bench.worldName.empty() && bench.size != glm::ivec3(0)) {
                        g_benches.push_back(bench);
                    } else {
                        std::cerr << "StructurePlacementSystem: skipping invalid bench config " << entry.path() << "\n";
                    }
                } catch (const std::exception& e) {
                    std::cerr << "StructurePlacementSystem: failed to parse " << entry.path() << " (" << e.what() << ")\n";
                }
            }
            g_configLoaded = true;
        }

        bool contains(const BenchDef& bench, const glm::ivec3& cell) {
            glm::ivec3 maxCorner = bench.minCorner + bench.size;
            return cell.x >= bench.minCorner.x && cell.x < maxCorner.x &&
                   cell.y >= bench.minCorner.y && cell.y < maxCorner.y &&
                   cell.z >= bench.minCorner.z && cell.z < maxCorner.z;
        }

        void ensureWorldIndices(LevelContext& level) {
            for (auto& bench : g_benches) {
                bench.worldIndex = -1;
                for (size_t i = 0; i < level.worlds.size(); ++i) {
                    if (level.worlds[i].name == bench.worldName) {
                        bench.worldIndex = static_cast<int>(i);
                        break;
                    }
                }
            }
        }

        void clearBenchArea(BaseSystem& baseSystem, std::vector<Entity>& prototypes, BenchDef& bench) {
            if (!baseSystem.level || bench.worldIndex < 0) return;
            LevelContext& level = *baseSystem.level;
            Entity& world = level.worlds[bench.worldIndex];
            auto& instances = world.instances;
            instances.erase(std::remove_if(instances.begin(), instances.end(),
                [&](const EntityInstance& inst) {
                    if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) return false;
                    const Entity& proto = prototypes[inst.prototypeID];
                    if (!proto.isBlock || !proto.isMutable) return false;
                    glm::ivec3 cell = glm::ivec3(glm::round(inst.position));
                    if (!contains(bench, cell)) return false;
                    BlockSelectionSystemLogic::RemoveBlockFromCache(baseSystem, prototypes, bench.worldIndex, inst.position);
                    return true;
                }), instances.end());
        }

        glm::ivec3 coordFromIndex(int index, const glm::ivec3& dims) {
            int layerSize = dims.x * dims.z;
            int y = index / layerSize;
            int rem = index % layerSize;
            int x = rem / dims.z;
            int z = rem % dims.z;
            return glm::ivec3(x, y, z);
        }

        void placeBlocks(BaseSystem& baseSystem, std::vector<Entity>& prototypes, BenchDef& bench) {
            if (!baseSystem.level || bench.worldIndex < 0) return;
            LevelContext& level = *baseSystem.level;
            Entity& world = level.worlds[bench.worldIndex];

            fs::path outPath(bench.outputPath);
            if (outPath.is_relative()) {
                outPath = fs::path(bench.outputPath);
            }
            if (!fs::exists(outPath)) {
                bench.loaded = true;
                return;
            }

            std::ifstream f(outPath);
            if (!f.is_open()) {
                std::cerr << "StructurePlacementSystem: cannot read " << outPath << "\n";
                bench.loaded = true;
                return;
            }

            json data;
            try {
                data = json::parse(f);
            } catch (const std::exception& e) {
                std::cerr << "StructurePlacementSystem: invalid structure file (" << e.what() << ")\n";
                bench.loaded = true;
                return;
            }

            bool placed = false;

            if (data.contains("runs") && data.contains("palette") && data.contains("size")) {
                glm::ivec3 dims = vecFromJson(data["size"]);
                if (dims.x > 0 && dims.y > 0 && dims.z > 0) {
                    std::vector<std::pair<std::string, glm::vec3>> palette;
                    for (const auto& entry : data["palette"]) {
                        std::string protoName = entry.value("prototype", "");
                        glm::vec3 color(1.0f);
                        if (entry.contains("color")) {
                            const auto& c = entry["color"];
                            if (c.is_array() && c.size() == 3) {
                                color = glm::vec3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>());
                            }
                        }
                        palette.push_back({ protoName, color });
                    }

                    clearBenchArea(baseSystem, prototypes, bench);
                    const int totalCells = dims.x * dims.y * dims.z;
                    int cellIndex = 0;
                    for (const auto& run : data["runs"]) {
                        int paletteIndex = run.value("palette", -1);
                        int count = run.value("count", 0);
                        for (int i = 0; i < count && cellIndex < totalCells; ++i, ++cellIndex) {
                            if (paletteIndex < 0 || paletteIndex >= static_cast<int>(palette.size())) continue;
                            const auto& paletteEntry = palette[paletteIndex];
                            if (paletteEntry.first.empty()) continue;
                            const Entity* proto = HostLogic::findPrototype(paletteEntry.first, prototypes);
                            if (!proto) continue;
                            glm::ivec3 local = coordFromIndex(cellIndex, dims);
                            glm::vec3 position = glm::vec3(bench.minCorner + local);
                            world.instances.push_back(HostLogic::CreateInstance(baseSystem, proto->prototypeID, position, paletteEntry.second));
                            BlockSelectionSystemLogic::AddBlockToCache(baseSystem, prototypes, bench.worldIndex, position, proto->prototypeID);
                        }
                    }
                    if (cellIndex < totalCells) {
                        std::cerr << "StructurePlacementSystem: run data shorter than expected for " << bench.id << "\n";
                    }
                    placed = true;
                }
            }

            if (!placed && data.contains("blocks")) {
                clearBenchArea(baseSystem, prototypes, bench);
                const auto& blocks = data["blocks"];
                for (const auto& entry : blocks) {
                    std::string protoName = entry.value("prototype", "");
                    if (protoName.empty()) continue;
                    const Entity* proto = HostLogic::findPrototype(protoName, prototypes);
                    if (!proto) continue;
                    glm::ivec3 offset = vecFromJson(entry["offset"]);
                    glm::vec3 color(1.0f);
                    if (entry.contains("color")) {
                        const auto& c = entry["color"];
                        if (c.is_array() && c.size() == 3) {
                            color = glm::vec3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>());
                        }
                    }
                    glm::vec3 position = glm::vec3(bench.minCorner + offset);
                    world.instances.push_back(HostLogic::CreateInstance(baseSystem, proto->prototypeID, position, color));
                    BlockSelectionSystemLogic::AddBlockToCache(baseSystem, prototypes, bench.worldIndex, position, proto->prototypeID);
                }
                placed = true;
            }

            if (!placed) {
                std::cerr << "StructurePlacementSystem: no usable data found for " << bench.id << "\n";
            }
            bench.loaded = true;
        }
    }

    void ProcessStructurePlacement(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.level) return;
        loadConfig();
        if (g_benches.empty()) return;

        ensureWorldIndices(*baseSystem.level);
        for (auto& bench : g_benches) {
            if (bench.loaded || bench.worldIndex < 0) continue;
            placeBlocks(baseSystem, prototypes, bench);
        }
    }
}
