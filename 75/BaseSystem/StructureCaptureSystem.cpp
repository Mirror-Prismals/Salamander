#pragma once

#include <filesystem>
#include <unordered_map>
#include <fstream>
#include <vector>
#include <sstream>
#include <iomanip>
#include <glm/glm.hpp>

namespace HostLogic { const Entity* findPrototype(const std::string& name, const std::vector<Entity>& prototypes); EntityInstance CreateInstance(BaseSystem& baseSystem, int prototypeID, glm::vec3 position, glm::vec3 color); }
namespace BlockSelectionSystemLogic { void EnsureAllCaches(BaseSystem& baseSystem, const std::vector<Entity>& prototypes); void AddBlockToCache(BaseSystem& baseSystem, std::vector<Entity>& prototypes, int worldIndex, const glm::vec3& position, int prototypeID); }

namespace StructureCaptureSystemLogic {

    struct IVec3Hash {
        std::size_t operator()(const glm::ivec3& v) const noexcept {
            std::size_t hx = std::hash<int>()(v.x);
            std::size_t hy = std::hash<int>()(v.y);
            std::size_t hz = std::hash<int>()(v.z);
            return hx ^ (hy << 1) ^ (hz << 2);
        }
    };

    struct WorkbenchConfig {
        std::string id;
        std::string worldName;
        glm::ivec3 minCorner{0};
        glm::ivec3 size{0};
        std::string outputPath;
        int worldIndex = -1;
        bool scaffoldsSpawned = false;
        bool dirty = true;
    };

    static std::vector<WorkbenchConfig> g_workbenches;
    static bool g_loadedConfig = false;
    static int g_scaffoldProtoID = -1;
    static glm::vec3 g_scaffoldColor(0.8f);
    static std::vector<std::pair<int, glm::ivec3>> g_pendingChanges;

    namespace fs = std::filesystem;

    namespace {
        glm::ivec3 to_ivec3(const std::vector<int>& v) {
            if (v.size() != 3) return glm::ivec3(0);
            return glm::ivec3(v[0], v[1], v[2]);
        }

        bool contains(const WorkbenchConfig& bench, const glm::ivec3& cell) {
            glm::ivec3 maxCorner = bench.minCorner + bench.size;
            return cell.x >= bench.minCorner.x && cell.x < maxCorner.x &&
                   cell.y >= bench.minCorner.y && cell.y < maxCorner.y &&
                   cell.z >= bench.minCorner.z && cell.z < maxCorner.z;
        }

        void loadConfig() {
            if (g_loadedConfig) return;
            g_workbenches.clear();
            fs::path dir("Procedures/Scaffolding");
            if (!fs::exists(dir)) {
                std::cerr << "StructureCaptureSystem: Procedures/Scaffolding directory missing.\n";
                g_loadedConfig = true;
                return;
            }
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
                try {
                    std::ifstream f(entry.path());
                    if (!f.is_open()) {
                        std::cerr << "StructureCaptureSystem: cannot open " << entry.path() << "\n";
                        continue;
                    }
                    json benchData = json::parse(f);
                    WorkbenchConfig bench;
                    bench.id = benchData.value("id", entry.path().stem().string());
                    bench.worldName = benchData.value("world", "");
                    if (benchData.contains("bounds")) {
                        auto bounds = benchData["bounds"];
                        if (bounds.contains("min")) {
                            auto minVec = bounds["min"].get<std::vector<int>>();
                            bench.minCorner = to_ivec3(minVec);
                        }
                        if (bounds.contains("size")) {
                            auto sizeVec = bounds["size"].get<std::vector<int>>();
                            bench.size = to_ivec3(sizeVec);
                        }
                    }
                    bench.outputPath = benchData.value("output", "");
                    bool captureEnabled = benchData.value("capture", true);
                    if (!captureEnabled) continue;
                    if (!bench.id.empty() && !bench.worldName.empty() && bench.size != glm::ivec3(0)) {
                        g_workbenches.push_back(bench);
                    } else {
                        std::cerr << "StructureCaptureSystem: skipping invalid bench config " << entry.path() << "\n";
                    }
                } catch (const std::exception& e) {
                    std::cerr << "StructureCaptureSystem: failed to parse " << entry.path() << " (" << e.what() << ")\n";
                }
            }
            g_loadedConfig = true;
        }

        int findWorldIndex(LevelContext& level, const std::string& name) {
            for (size_t i = 0; i < level.worlds.size(); ++i) {
                if (level.worlds[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        void ensureScaffolds(BaseSystem& baseSystem, std::vector<Entity>& prototypes, WorkbenchConfig& bench) {
            if (!baseSystem.level || !baseSystem.world || bench.worldIndex < 0 || bench.scaffoldsSpawned) return;
            LevelContext& level = *baseSystem.level;
            if (bench.worldIndex >= static_cast<int>(level.worlds.size())) return;
            Entity& world = level.worlds[bench.worldIndex];
            const Entity* scaffoldProto = HostLogic::findPrototype("ScaffoldBlock", prototypes);
            if (!scaffoldProto) return;
            if (g_scaffoldProtoID < 0) g_scaffoldProtoID = scaffoldProto->prototypeID;
            if (baseSystem.world->colorLibrary.count("Scaffold")) {
                g_scaffoldColor = baseSystem.world->colorLibrary["Scaffold"];
            }

            BlockSelectionSystemLogic::EnsureAllCaches(baseSystem, prototypes);

            int topY = bench.minCorner.y + bench.size.y;
            auto placePlane = [&](int y) {
                for (int x = 0; x < bench.size.x; ++x) {
                    for (int z = 0; z < bench.size.z; ++z) {
                        glm::vec3 pos = glm::vec3(bench.minCorner.x + x, y, bench.minCorner.z + z);
                        world.instances.push_back(HostLogic::CreateInstance(baseSystem, scaffoldProto->prototypeID, pos, g_scaffoldColor));
                        BlockSelectionSystemLogic::AddBlockToCache(baseSystem, prototypes, bench.worldIndex, pos, scaffoldProto->prototypeID);
                    }
                }
            };

            placePlane(bench.minCorner.y);
            placePlane(topY);
            bench.scaffoldsSpawned = true;
            bench.dirty = true;
        }

        void exportBench(BaseSystem& baseSystem, std::vector<Entity>& prototypes, WorkbenchConfig& bench) {
            if (!baseSystem.level || bench.worldIndex < 0) return;
            LevelContext& level = *baseSystem.level;
            if (bench.worldIndex >= static_cast<int>(level.worlds.size())) return;
            Entity& world = level.worlds[bench.worldIndex];

            glm::ivec3 dims = bench.size;
            if (dims.x <= 0 || dims.y <= 0 || dims.z <= 0) return;
            const int totalCells = dims.x * dims.y * dims.z;

            struct BlockInfo { std::string prototype; glm::vec3 color; };
            std::unordered_map<glm::ivec3, BlockInfo, IVec3Hash> blockMap;
            for (const auto& inst : world.instances) {
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[inst.prototypeID];
                if (!proto.isBlock || !proto.isMutable) continue;
                glm::ivec3 cell = glm::ivec3(glm::round(inst.position));
                if (!contains(bench, cell)) continue;
                blockMap[cell] = { proto.name, inst.color };
            }

            auto paletteKey = [](const std::string& proto, const glm::vec3& color) {
                std::ostringstream oss;
                oss << proto << "|" << std::fixed << std::setprecision(6)
                    << color.x << "," << color.y << "," << color.z;
                return oss.str();
            };

            std::unordered_map<std::string, int> paletteLookup;
            struct PaletteEntry { std::string prototype; glm::vec3 color; };
            std::vector<PaletteEntry> palette;
            auto getPaletteIndex = [&](const std::string& proto, const glm::vec3& color) {
                std::string key = paletteKey(proto, color);
                auto it = paletteLookup.find(key);
                if (it != paletteLookup.end()) return it->second;
                int idx = static_cast<int>(palette.size());
                paletteLookup[key] = idx;
                palette.push_back({ proto, color });
                return idx;
            };

            std::vector<int> stream;
            stream.reserve(totalCells);
            for (int y = 0; y < dims.y; ++y) {
                for (int x = 0; x < dims.x; ++x) {
                    for (int z = 0; z < dims.z; ++z) {
                        glm::ivec3 cell = bench.minCorner + glm::ivec3(x, y, z);
                        auto it = blockMap.find(cell);
                        if (it != blockMap.end()) {
                            stream.push_back(getPaletteIndex(it->second.prototype, it->second.color));
                        } else {
                            stream.push_back(-1);
                        }
                    }
                }
            }

            struct Run { int paletteIndex; int count; };
            std::vector<Run> runs;
            if (!stream.empty()) {
                int current = stream[0];
                int count = 1;
                for (size_t i = 1; i < stream.size(); ++i) {
                    if (stream[i] == current) {
                        count++;
                    } else {
                        runs.push_back({ current, count });
                        current = stream[i];
                        count = 1;
                    }
                }
                runs.push_back({ current, count });
            }

            json output;
            output["format"] = "palette_rle_v1";
            output["id"] = bench.id;
            output["world"] = bench.worldName;
            output["bounds"] = {
                {"min", {bench.minCorner.x, bench.minCorner.y, bench.minCorner.z}},
                {"size", {bench.size.x, bench.size.y, bench.size.z}}
            };
            output["size"] = { dims.x, dims.y, dims.z };

            json paletteJson = json::array();
            for (const auto& entry : palette) {
                paletteJson.push_back({
                    {"prototype", entry.prototype},
                    {"color", { entry.color.x, entry.color.y, entry.color.z }}
                });
            }
            output["palette"] = paletteJson;

            json runsJson = json::array();
            for (const auto& run : runs) {
                runsJson.push_back({
                    {"palette", run.paletteIndex},
                    {"count", run.count}
                });
            }
            output["runs"] = runsJson;

            try {
                fs::path outPath(bench.outputPath);
                if (outPath.is_relative()) {
                    outPath = fs::path(bench.outputPath);
                }
                if (outPath.has_parent_path()) {
                    fs::create_directories(outPath.parent_path());
                }
                std::ofstream ofs(outPath);
                if (!ofs.is_open()) {
                    std::cerr << "StructureCaptureSystem: Failed to write " << outPath << "\n";
                } else {
                    ofs << output.dump(2);
                }
            } catch (const std::exception& e) {
                std::cerr << "StructureCaptureSystem: export error (" << e.what() << ")\n";
            }

            bench.dirty = false;
        }
    }

    void NotifyBlockChanged(BaseSystem& baseSystem, int worldIndex, const glm::vec3& position) {
        (void)baseSystem;
        if (worldIndex < 0) return;
        g_pendingChanges.emplace_back(worldIndex, glm::ivec3(glm::round(position)));
    }

    void ProcessStructureCapture(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.level) return;
        loadConfig();
        if (g_workbenches.empty()) return;

        LevelContext& level = *baseSystem.level;

        for (auto& bench : g_workbenches) {
            bench.worldIndex = findWorldIndex(level, bench.worldName);
            if (bench.worldIndex < 0) continue;
            ensureScaffolds(baseSystem, prototypes, bench);
        }

        if (!g_pendingChanges.empty()) {
            for (const auto& [worldIndex, cell] : g_pendingChanges) {
                for (auto& bench : g_workbenches) {
                    if (bench.worldIndex == worldIndex && contains(bench, cell)) {
                        bench.dirty = true;
                    }
                }
            }
            g_pendingChanges.clear();
        }

        for (auto& bench : g_workbenches) {
            if (bench.worldIndex < 0 || !bench.scaffoldsSpawned) continue;
            if (bench.dirty) {
                exportBench(baseSystem, prototypes, bench);
            }
        }
    }
}
