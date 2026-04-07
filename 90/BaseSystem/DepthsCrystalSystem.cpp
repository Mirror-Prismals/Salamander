#pragma once
#include "../Host.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

namespace VoxelMeshingSystemLogic {
    void RequestPriorityVoxelRemesh(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const glm::ivec3& worldCell);
}

namespace DepthsCrystalSystemLogic {

    namespace {
        int findPrototypeIDByName(const std::vector<Entity>& prototypes, const std::string& name) {
            for (size_t i = 0; i < prototypes.size(); ++i) {
                if (prototypes[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        uint32_t packWhiteColor() {
            return (255u << 16) | (255u << 8) | 255u;
        }

        glm::vec3 loadFrogSpawnPosition() {
            glm::vec3 fallback(0.0f, 144.0f, 0.0f);
            std::ifstream f("Procedures/spawns.json");
            if (!f.is_open()) return fallback;
            json data;
            try {
                data = json::parse(f);
            } catch (...) {
                return fallback;
            }
            if (!data.contains("frog_spawn")) return fallback;
            const auto& spawn = data["frog_spawn"];
            if (!spawn.contains("position")) return fallback;
            try {
                glm::vec3 pos = fallback;
                spawn.at("position").get_to(pos);
                return pos;
            } catch (...) {
                return fallback;
            }
        }
    }

    void UpdateDepthsCrystals(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt;
        (void)win;
        if (!baseSystem.voxelWorld) return;
        if (!baseSystem.registry) return;
        auto levelIt = baseSystem.registry->find("level");
        if (levelIt != baseSystem.registry->end()
            && std::holds_alternative<std::string>(levelIt->second)) {
            if (std::get<std::string>(levelIt->second) != "the_expanse") return;
        }

        const int depthStonePrototypeID = findPrototypeIDByName(prototypes, "DepthStoneBlockTex");
        const int crystalPrototypeID = findPrototypeIDByName(prototypes, "DepthCrystalClusterTex");
        const int crystalBluePrototypeID = findPrototypeIDByName(prototypes, "DepthCrystalClusterBlueTex");
        const int crystalBlueBigPrototypeID = findPrototypeIDByName(prototypes, "DepthCrystalClusterBlueBigTex");
        const int crystalMagentaBigPrototypeID = findPrototypeIDByName(prototypes, "DepthCrystalClusterMagentaBigTex");
        if (depthStonePrototypeID < 0 || crystalPrototypeID < 0 || crystalBluePrototypeID < 0
            || crystalBlueBigPrototypeID < 0 || crystalMagentaBigPrototypeID < 0) return;

        static float refreshTimer = 0.0f;
        refreshTimer -= dt;
        if (refreshTimer > 0.0f) return;
        refreshTimer = 0.35f;

        const glm::vec3 frogSpawn = loadFrogSpawnPosition();
        const glm::ivec3 supportCell(
            static_cast<int>(std::floor(frogSpawn.x)),
            static_cast<int>(std::floor(frogSpawn.y)) + 8,
            static_cast<int>(std::floor(frogSpawn.z))
        );
        const glm::ivec3 crystalCell = supportCell + glm::ivec3(0, 1, 0);
        const glm::ivec3 supportBlueCell = supportCell + glm::ivec3(2, 0, 0);
        const glm::ivec3 crystalBlueCell = supportBlueCell + glm::ivec3(0, 1, 0);
        const glm::ivec3 supportBlueBigCell = supportCell + glm::ivec3(4, 0, 0);
        const glm::ivec3 crystalBlueBigCell = supportBlueBigCell + glm::ivec3(0, 1, 0);
        const glm::ivec3 supportMagentaBigCell = supportCell + glm::ivec3(6, 0, 0);
        const glm::ivec3 crystalMagentaBigCell = supportMagentaBigCell + glm::ivec3(0, 1, 0);

        const uint32_t supportId = baseSystem.voxelWorld->getBlockWorld(supportCell);
        const uint32_t crystalId = baseSystem.voxelWorld->getBlockWorld(crystalCell);
        const uint32_t supportBlueId = baseSystem.voxelWorld->getBlockWorld(supportBlueCell);
        const uint32_t crystalBlueId = baseSystem.voxelWorld->getBlockWorld(crystalBlueCell);
        const uint32_t supportBlueBigId = baseSystem.voxelWorld->getBlockWorld(supportBlueBigCell);
        const uint32_t crystalBlueBigId = baseSystem.voxelWorld->getBlockWorld(crystalBlueBigCell);
        const uint32_t supportMagentaBigId = baseSystem.voxelWorld->getBlockWorld(supportMagentaBigCell);
        const uint32_t crystalMagentaBigId = baseSystem.voxelWorld->getBlockWorld(crystalMagentaBigCell);
        const bool supportMissing = supportId != static_cast<uint32_t>(depthStonePrototypeID);
        const bool crystalMissing = crystalId != static_cast<uint32_t>(crystalPrototypeID);
        const bool supportBlueMissing = supportBlueId != static_cast<uint32_t>(depthStonePrototypeID);
        const bool crystalBlueMissing = crystalBlueId != static_cast<uint32_t>(crystalBluePrototypeID);
        const bool supportBlueBigMissing = supportBlueBigId != static_cast<uint32_t>(depthStonePrototypeID);
        const bool crystalBlueBigMissing = crystalBlueBigId != static_cast<uint32_t>(crystalBlueBigPrototypeID);
        const bool supportMagentaBigMissing = supportMagentaBigId != static_cast<uint32_t>(depthStonePrototypeID);
        const bool crystalMagentaBigMissing = crystalMagentaBigId != static_cast<uint32_t>(crystalMagentaBigPrototypeID);
        if (!supportMissing && !crystalMissing
            && !supportBlueMissing && !crystalBlueMissing
            && !supportBlueBigMissing && !crystalBlueBigMissing
            && !supportMagentaBigMissing && !crystalMagentaBigMissing) return;

        if (supportMissing) {
            baseSystem.voxelWorld->setBlockWorld(
                supportCell,
                static_cast<uint32_t>(depthStonePrototypeID),
                packWhiteColor()
            );
            VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, supportCell);
        }
        if (crystalMissing) {
            baseSystem.voxelWorld->setBlockWorld(
                crystalCell,
                static_cast<uint32_t>(crystalPrototypeID),
                packWhiteColor()
            );
            VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, crystalCell);
        }
        if (supportBlueMissing) {
            baseSystem.voxelWorld->setBlockWorld(
                supportBlueCell,
                static_cast<uint32_t>(depthStonePrototypeID),
                packWhiteColor()
            );
            VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, supportBlueCell);
        }
        if (crystalBlueMissing) {
            baseSystem.voxelWorld->setBlockWorld(
                crystalBlueCell,
                static_cast<uint32_t>(crystalBluePrototypeID),
                packWhiteColor()
            );
            VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, crystalBlueCell);
        }
        if (supportBlueBigMissing) {
            baseSystem.voxelWorld->setBlockWorld(
                supportBlueBigCell,
                static_cast<uint32_t>(depthStonePrototypeID),
                packWhiteColor()
            );
            VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, supportBlueBigCell);
        }
        if (crystalBlueBigMissing) {
            baseSystem.voxelWorld->setBlockWorld(
                crystalBlueBigCell,
                static_cast<uint32_t>(crystalBlueBigPrototypeID),
                packWhiteColor()
            );
            VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, crystalBlueBigCell);
        }
        if (supportMagentaBigMissing) {
            baseSystem.voxelWorld->setBlockWorld(
                supportMagentaBigCell,
                static_cast<uint32_t>(depthStonePrototypeID),
                packWhiteColor()
            );
            VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, supportMagentaBigCell);
        }
        if (crystalMagentaBigMissing) {
            baseSystem.voxelWorld->setBlockWorld(
                crystalMagentaBigCell,
                static_cast<uint32_t>(crystalMagentaBigPrototypeID),
                packWhiteColor()
            );
            VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, crystalMagentaBigCell);
        }
    }
}
