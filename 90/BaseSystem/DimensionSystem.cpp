#pragma once

#include <array>
#include <cmath>
#include <iostream>
#include <string>

namespace ExpanseBiomeSystemLogic { bool SampleTerrain(const WorldContext& worldCtx, float x, float z, float& outHeight); }

namespace DimensionSystemLogic {

    namespace {
        struct PendingDimensionTravel {
            bool awaitingArrival = false;
            std::string destinationLevel;
            glm::ivec2 anchorXZ = glm::ivec2(0);
        };

        PendingDimensionTravel g_pendingTravel;
        float g_portalCooldownSeconds = 0.0f;

        std::string getRegistryString(const BaseSystem& baseSystem,
                                      const std::string& key,
                                      const std::string& fallback = "") {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            return std::get<std::string>(it->second);
        }

        int findPrototypeIDByName(const std::vector<Entity>& prototypes, const char* name) {
            if (!name) return -1;
            for (const Entity& proto : prototypes) {
                if (proto.name == name) return proto.prototypeID;
            }
            return -1;
        }

        bool isTouchingPortal(const BaseSystem& baseSystem,
                              int portalPrototypeID,
                              glm::ivec3* outTouchedCell) {
            if (!baseSystem.player || !baseSystem.voxelWorld) return false;
            const PlayerContext& player = *baseSystem.player;
            const VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            if (!voxelWorld.enabled) return false;

            const glm::vec3 camera = player.cameraPosition;
            const float feetY = camera.y - 1.5f;
            static const std::array<float, 5> kOffsets = {0.0f, -0.45f, 0.45f, -0.2f, 0.2f};
            // Sample from below feet through head-space so non-solid portal contact
            // still registers even while moving/falling through the layer.
            static const std::array<float, 6> kHeights = {-0.6f, -0.1f, 0.0f, 0.9f, 1.4f, 1.9f};

            for (float oy : kHeights) {
                for (float ox : kOffsets) {
                    for (float oz : kOffsets) {
                        glm::ivec3 cell(
                            static_cast<int>(std::floor(camera.x + ox)),
                            static_cast<int>(std::floor(feetY + oy)),
                            static_cast<int>(std::floor(camera.z + oz))
                        );
                        const uint32_t id = voxelWorld.getBlockWorld(cell);
                        if (id == static_cast<uint32_t>(portalPrototypeID)) {
                            if (outTouchedCell) *outTouchedCell = cell;
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        bool placeArrivalPocket(BaseSystem& baseSystem,
                                const std::vector<Entity>& prototypes,
                                const glm::ivec2& anchorXZ) {
            if (!baseSystem.world || !baseSystem.voxelWorld || !baseSystem.player) return false;
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            WorldContext& worldCtx = *baseSystem.world;
            PlayerContext& player = *baseSystem.player;
            if (!voxelWorld.enabled) return false;
            if (!worldCtx.expanse.loaded) return false;

            float sampledHeight = 0.0f;
            const bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(
                worldCtx,
                static_cast<float>(anchorXZ.x),
                static_cast<float>(anchorXZ.y),
                sampledHeight
            );
            int surfaceY = isLand
                ? static_cast<int>(std::floor(sampledHeight))
                : static_cast<int>(std::floor(worldCtx.expanse.waterSurface));
            int pocketCenterY = surfaceY - 8;
            pocketCenterY = std::max(pocketCenterY, worldCtx.expanse.minY + 4);

            const int depthStonePrototypeID = [&]() {
                int id = findPrototypeIDByName(prototypes, "DepthStoneBlockTex");
                if (id >= 0) return id;
                return findPrototypeIDByName(prototypes, "StoneBlockTex");
            }();
            const int portalPrototypeID = findPrototypeIDByName(prototypes, "VoidPortalBlockTex");
            const uint32_t stoneID = depthStonePrototypeID >= 0
                ? static_cast<uint32_t>(depthStonePrototypeID)
                : 0u;

            constexpr uint32_t kWhiteColor = 0x00ffffffu;
            constexpr int kRadius = 2;

            for (int dz = -kRadius; dz <= kRadius; ++dz) {
                for (int dx = -kRadius; dx <= kRadius; ++dx) {
                    if (dx * dx + dz * dz > (kRadius * kRadius + 1)) continue;
                    for (int dy = 0; dy <= 3; ++dy) {
                        const glm::ivec3 airCell(anchorXZ.x + dx, pocketCenterY + dy, anchorXZ.y + dz);
                        voxelWorld.setBlockWorld(airCell, 0u, 0u);
                    }
                    if (stoneID != 0u) {
                        const glm::ivec3 floorCell(anchorXZ.x + dx, pocketCenterY - 1, anchorXZ.y + dz);
                        voxelWorld.setBlockWorld(floorCell, stoneID, kWhiteColor);
                    }
                }
            }

            const std::string levelKey = getRegistryString(baseSystem, "level", "");
            if (levelKey == "the_depths" && portalPrototypeID >= 0) {
                // Place a return portal ceiling in the spawned pocket.
                // This supports two-way travel while avoiding immediate re-trigger on arrival.
                const uint32_t portalID = static_cast<uint32_t>(portalPrototypeID);
                constexpr int kPortalY = 2;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const glm::ivec3 portalCell(anchorXZ.x + dx, pocketCenterY + kPortalY, anchorXZ.y + dz);
                        voxelWorld.setBlockWorld(portalCell, portalID, kWhiteColor);
                    }
                }
            }

            player.cameraPosition = glm::vec3(
                static_cast<float>(anchorXZ.x) + 0.5f,
                static_cast<float>(pocketCenterY) + 1.501f,
                static_cast<float>(anchorXZ.y) + 0.5f
            );
            player.prevCameraPosition = player.cameraPosition;
            player.verticalVelocity = 0.0f;
            player.onGround = false;
            if (baseSystem.registry) {
                (*baseSystem.registry)["spawn_ready"] = true;
                (*baseSystem.registry)["DimensionArrivalPending"] = false;
            }

            std::cout << "DimensionSystem: arrived in "
                      << levelKey
                      << " at [" << anchorXZ.x << ", " << pocketCenterY << ", " << anchorXZ.y << "]"
                      << std::endl;
            return true;
        }
    } // namespace

    void UpdateDimension(BaseSystem& baseSystem,
                         std::vector<Entity>& prototypes,
                         float dt,
                         PlatformWindowHandle win) {
        (void)win;
        if (g_portalCooldownSeconds > 0.0f) {
            g_portalCooldownSeconds = std::max(0.0f, g_portalCooldownSeconds - std::max(0.0f, dt));
        }

        const std::string levelKey = getRegistryString(baseSystem, "level", "");
        const bool inExpanse = (levelKey == "the_expanse");
        const bool inDepths = (levelKey == "the_depths");

        if (g_pendingTravel.awaitingArrival) {
            if (levelKey == g_pendingTravel.destinationLevel) {
                const bool arrived = placeArrivalPocket(baseSystem, prototypes, g_pendingTravel.anchorXZ);
                if (arrived) {
                    g_pendingTravel.awaitingArrival = false;
                    g_pendingTravel.destinationLevel.clear();
                    g_portalCooldownSeconds = std::max(g_portalCooldownSeconds, 0.75f);
                }
            } else if (baseSystem.reloadRequested && baseSystem.reloadTarget) {
                // Fail-safe: keep requesting the pending level change until host consumes it.
                *baseSystem.reloadTarget = g_pendingTravel.destinationLevel;
                *baseSystem.reloadRequested = true;
            }
            return;
        }

        if (!inExpanse && !inDepths) return;
        if (!baseSystem.player || !baseSystem.voxelWorld || !baseSystem.reloadRequested || !baseSystem.reloadTarget) return;
        if (!baseSystem.voxelWorld->enabled) return;
        if (g_portalCooldownSeconds > 0.0f) return;

        glm::ivec3 touchedCell(0);
        bool touchedPortal = false;
        const int portalPrototypeID = findPrototypeIDByName(prototypes, "VoidPortalBlockTex");
        if (portalPrototypeID >= 0) {
            touchedPortal = isTouchingPortal(baseSystem, portalPrototypeID, &touchedCell);
        }
        if (!touchedPortal) return;

        const std::string destinationLevel = inExpanse ? "the_depths" : "the_expanse";

        g_pendingTravel.awaitingArrival = true;
        g_pendingTravel.destinationLevel = destinationLevel;
        g_pendingTravel.anchorXZ = glm::ivec2(touchedCell.x, touchedCell.z);
        *baseSystem.reloadTarget = g_pendingTravel.destinationLevel;
        *baseSystem.reloadRequested = true;
        if (baseSystem.registry) {
            (*baseSystem.registry)["spawn_ready"] = false;
            (*baseSystem.registry)["DimensionArrivalPending"] = true;
        }
        if (baseSystem.ui) {
            baseSystem.ui->levelSwitchPending = false;
            baseSystem.ui->pendingActionType.clear();
            baseSystem.ui->pendingActionKey.clear();
            baseSystem.ui->pendingActionValue.clear();
            baseSystem.ui->actionDelayFrames = 0;
            baseSystem.ui->loadingActive = false;
            baseSystem.ui->loadingTimer = 0.0f;
        }

        g_portalCooldownSeconds = 1.0f;
        std::cout << "DimensionSystem: portal touched at ["
                  << touchedCell.x << ", " << touchedCell.y << ", " << touchedCell.z
                  << "], loading " << g_pendingTravel.destinationLevel
                  << std::endl;
    }
}
