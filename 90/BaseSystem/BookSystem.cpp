#pragma once
#include "../Host.h"
#include "Host/PlatformInput.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace OreMiningSystemLogic { bool IsMiningActive(const BaseSystem& baseSystem); }
namespace GroundCraftingSystemLogic { bool IsRitualActive(const BaseSystem& baseSystem); }
namespace GemChiselSystemLogic { bool IsChiselActive(const BaseSystem& baseSystem); }
namespace VoxelMeshingSystemLogic {
    void RequestPriorityVoxelRemesh(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const glm::ivec3& worldCell);
}

namespace BookSystemLogic {

    namespace {
        int getRegistryInt(const BaseSystem& baseSystem, const char* key, int fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stoi(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        bool isSolidVoxelId(const std::vector<Entity>& prototypes, uint32_t id) {
            if (id == 0u) return false;
            if (id >= prototypes.size()) return false;
            const Entity& proto = prototypes[static_cast<size_t>(id)];
            if (!proto.isBlock || !proto.isSolid) return false;
            if (proto.name == "Water" || proto.name.rfind("WaterSlope", 0) == 0) return false;
            return true;
        }

        bool findTopSolidAtCell(const BaseSystem& baseSystem,
                                const std::vector<Entity>& prototypes,
                                int cellX,
                                int cellZ,
                                int minY,
                                int maxY,
                                int* outTopSolidY) {
            if (!baseSystem.voxelWorld || !outTopSolidY) return false;
            for (int y = maxY; y >= minY; --y) {
                uint32_t id = baseSystem.voxelWorld->getBlockWorld(glm::ivec3(cellX, y, cellZ));
                if (isSolidVoxelId(prototypes, id)) {
                    *outTopSolidY = y;
                    return true;
                }
            }
            return false;
        }

        int findPrototypeIDByName(const std::vector<Entity>& prototypes, const std::string& name) {
            for (size_t i = 0; i < prototypes.size(); ++i) {
                if (prototypes[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        uint32_t packWhiteColor() {
            return (255u << 16) | (255u << 8) | 255u;
        }

        bool canReplaceForSpawnBook(const std::vector<Entity>& prototypes, uint32_t id) {
            if (id == 0u) return true;
            if (id >= prototypes.size()) return false;
            const Entity& proto = prototypes[static_cast<size_t>(id)];
            if (!proto.isBlock) return false;
            if (proto.name == "Water" || proto.name.rfind("WaterSlope", 0) == 0) return false;
            return !proto.isSolid;
        }

        struct PageFileEntry {
            int numericOrder = std::numeric_limits<int>::max();
            std::string name;
            std::filesystem::path path;
        };

        std::vector<std::string> loadBookPageTextFiles() {
            std::vector<std::string> pages;
            const std::array<std::filesystem::path, 2> searchDirs = {
                std::filesystem::path("Procedures/books/test_book"),
                std::filesystem::path("Procedures/books/text_book/pages")
            };

            for (const auto& dirPath : searchDirs) {
                if (!std::filesystem::exists(dirPath) || !std::filesystem::is_directory(dirPath)) continue;
                std::vector<PageFileEntry> entries;
                for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
                    if (!entry.is_regular_file()) continue;
                    const std::filesystem::path path = entry.path();
                    if (path.extension() != ".txt") continue;
                    const std::string stem = path.stem().string();
                    int numericOrder = std::numeric_limits<int>::max();
                    std::string numericToken = stem;
                    const size_t underscorePos = stem.find_last_of('_');
                    if (underscorePos != std::string::npos && underscorePos + 1 < stem.size()) {
                        numericToken = stem.substr(underscorePos + 1);
                    }
                    try {
                        numericOrder = std::stoi(numericToken);
                    } catch (...) {
                        numericOrder = std::numeric_limits<int>::max();
                    }
                    entries.push_back(PageFileEntry{numericOrder, path.filename().string(), path});
                }
                std::sort(entries.begin(), entries.end(), [](const PageFileEntry& a, const PageFileEntry& b) {
                    if (a.numericOrder != b.numericOrder) return a.numericOrder < b.numericOrder;
                    return a.name < b.name;
                });

                for (const auto& pageEntry : entries) {
                    std::ifstream in(pageEntry.path);
                    if (!in.is_open()) continue;
                    std::ostringstream ss;
                    ss << in.rdbuf();
                    std::string text = ss.str();
                    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
                        text.pop_back();
                    }
                    pages.push_back(text);
                }
                if (!pages.empty()) return pages;
            }
            return pages;
        }

        const std::vector<std::string>& bookPageTextCache() {
            static std::vector<std::string> s_pages = []() {
                std::vector<std::string> loaded = loadBookPageTextFiles();
                if (!loaded.empty()) return loaded;
                return std::vector<std::string>{
                    "Salamander Prototype Book\n\nWelcome to Cardinal.",
                    "Page 2\n\nBooks are physical items.\nInspect with R.",
                    "Page 3\n\nUse Left/Right arrows\nto change pages."
                };
            }();
            return s_pages;
        }
    }

    bool IsBookPrototypeName(const std::string& name) {
        return name.rfind("StonePebblePetalsBookTex", 0) == 0;
    }

    std::string ResolveBookPageText(int inspectPage) {
        const auto& pages = bookPageTextCache();
        if (pages.empty()) return std::string();
        const int pageIndex = std::max(0, inspectPage - 1);
        const int clamped = std::min(pageIndex, static_cast<int>(pages.size()) - 1);
        return pages[static_cast<size_t>(clamped)];
    }

    void PlaceSpawnBookCluster(BaseSystem& baseSystem,
                               std::vector<Entity>& prototypes,
                               const glm::ivec2& centerXZ,
                               int nominalSurfaceY) {
        if (!baseSystem.voxelWorld) return;
        const int bookPrototypeX = findPrototypeIDByName(prototypes, "StonePebblePetalsBookTexX");
        const int bookPrototypeZ = findPrototypeIDByName(prototypes, "StonePebblePetalsBookTexZ");
        if (bookPrototypeX < 0 || bookPrototypeZ < 0) return;

        static const std::array<glm::ivec2, 8> kBookOffsets = {
            glm::ivec2(1, 1),
            glm::ivec2(-1, 1),
            glm::ivec2(1, -1),
            glm::ivec2(-1, -1),
            glm::ivec2(3, 0),
            glm::ivec2(-3, 0),
            glm::ivec2(0, 3),
            glm::ivec2(0, -3)
        };

        int placedCount = 0;
        for (const glm::ivec2& offset : kBookOffsets) {
            const int cellX = centerXZ.x + offset.x;
            const int cellZ = centerXZ.y + offset.y;
            int supportY = std::numeric_limits<int>::min();
            if (!findTopSolidAtCell(baseSystem,
                                    prototypes,
                                    cellX,
                                    cellZ,
                                    nominalSurfaceY - 8,
                                    nominalSurfaceY + 8,
                                    &supportY)) {
                continue;
            }

            const glm::ivec3 placeCell(cellX, supportY + 1, cellZ);
            const uint32_t currentId = baseSystem.voxelWorld->getBlockWorld(placeCell);
            if (!canReplaceForSpawnBook(prototypes, currentId)) continue;

            const int placePrototypeID = (((cellX + cellZ) & 1) == 0) ? bookPrototypeX : bookPrototypeZ;
            baseSystem.voxelWorld->setBlockWorld(
                placeCell,
                static_cast<uint32_t>(placePrototypeID),
                packWhiteColor()
            );
            VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, placeCell);
            placedCount += 1;
            if (placedCount >= 4) break;
        }
    }

    void UpdateBookSystem(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt;
        if (!baseSystem.level || baseSystem.level->worlds.empty()) return;
        if (baseSystem.ui && baseSystem.ui->active) return;
        if (OreMiningSystemLogic::IsMiningActive(baseSystem)) return;
        if (GroundCraftingSystemLogic::IsRitualActive(baseSystem)) return;
        if (GemChiselSystemLogic::IsChiselActive(baseSystem)) return;
        if (!baseSystem.player) return;

        PlayerContext& player = *baseSystem.player;
        const bool holdingBook = player.isHoldingBlock
            && player.heldPrototypeID >= 0
            && player.heldPrototypeID < static_cast<int>(prototypes.size())
            && IsBookPrototypeName(prototypes[static_cast<size_t>(player.heldPrototypeID)].name);

        static bool rPressedLastFrame = false;
        static bool leftPressedLastFrame = false;
        static bool rightPressedLastFrame = false;
        const bool rPressed = PlatformInput::IsKeyDown(win, PlatformInput::Key::R);
        const bool leftPressed = PlatformInput::IsKeyDown(win, PlatformInput::Key::ArrowLeft);
        const bool rightPressed = PlatformInput::IsKeyDown(win, PlatformInput::Key::ArrowRight);

        if (!holdingBook) {
            player.bookInspectActive = false;
            player.bookInspectPage = 0;
            rPressedLastFrame = rPressed;
            leftPressedLastFrame = leftPressed;
            rightPressedLastFrame = rightPressed;
            return;
        }

        if (rPressed && !rPressedLastFrame) {
            player.bookInspectActive = !player.bookInspectActive;
            if (player.bookInspectActive) {
                player.bookInspectPage = 0;
            }
        }
        rPressedLastFrame = rPressed;

        const int maxSpread = std::max(0, getRegistryInt(baseSystem, "BookInspectMaxSpread", 8));
        if (player.bookInspectActive) {
            if (rightPressed && !rightPressedLastFrame) {
                player.bookInspectPage = std::min(maxSpread, player.bookInspectPage + 1);
            }
            if (leftPressed && !leftPressedLastFrame) {
                player.bookInspectPage = std::max(0, player.bookInspectPage - 1);
            }
        }
        leftPressedLastFrame = leftPressed;
        rightPressedLastFrame = rightPressed;
    }
}
