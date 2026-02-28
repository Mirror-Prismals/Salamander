#pragma once

namespace CloudSystemLogic {

    namespace {
        uint32_t mixBits(uint32_t x) {
            x ^= x >> 16;
            x *= 0x7feb352du;
            x ^= x >> 15;
            x *= 0x846ca68bu;
            x ^= x >> 16;
            return x;
        }

        float hash01(int x, int y, int z, int salt) {
            uint32_t h = 0x9e3779b9u;
            h ^= mixBits(static_cast<uint32_t>(x) + 0x85ebca6bu);
            h ^= mixBits(static_cast<uint32_t>(y) + 0xc2b2ae35u);
            h ^= mixBits(static_cast<uint32_t>(z) + 0x27d4eb2fu);
            h ^= mixBits(static_cast<uint32_t>(salt) + 0x165667b1u);
            return static_cast<float>(h & 0x00ffffffu) / 16777216.0f;
        }
    }

    void RenderClouds(BaseSystem& baseSystem, const glm::vec3& lightDir, float time, float dayFraction) {
        if (!baseSystem.renderer || !baseSystem.player || !baseSystem.voxelWorld) return;
        RendererContext& renderer = *baseSystem.renderer;
        PlayerContext& player = *baseSystem.player;
        VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
        if (!voxelWorld.enabled || voxelWorld.sections.empty()) return;
        if (!renderer.faceShader || !renderer.faceVAO || !renderer.faceInstanceVBO) return;

        glm::mat4 view = player.viewMatrix;
        float fovY = 2.0f * std::atan(1.0f / std::max(player.projectionMatrix[1][1], 1e-5f));
        float aspect = std::abs(player.projectionMatrix[0][0]) > 1e-5f
            ? (player.projectionMatrix[1][1] / player.projectionMatrix[0][0])
            : 16.0f / 9.0f;
        glm::mat4 projection = glm::perspective(fovY, aspect, 0.1f, 12000.0f);
        glm::vec3 playerPos = player.cameraPosition;

        constexpr float kCloudBase = 1650.0f;
        constexpr float kCloudHeightRange = 850.0f;
        constexpr float kCloudSpawnRadius = 4200.0f;
        constexpr float kCloudSpawnRadiusSq = kCloudSpawnRadius * kCloudSpawnRadius;
        constexpr size_t kMaxCloudInstances = 2800;

        float driftPhase = dayFraction * 6.28318530718f * 100.0f;
        glm::vec2 drift = glm::vec2(std::cos(driftPhase), std::sin(driftPhase)) * 1800.0f;

        std::vector<FaceInstanceRenderData> cloudInstances;
        cloudInstances.reserve(768);
        for (const auto& [key, section] : voxelWorld.sections) {
            if (section.nonAirCount <= 0) continue;
            int scale = 1 << section.lod;
            float sectionWorldSize = static_cast<float>(section.size * scale);
            float sectionCenterX = (static_cast<float>(section.coord.x) + 0.5f) * sectionWorldSize;
            float sectionCenterZ = (static_cast<float>(section.coord.z) + 0.5f) * sectionWorldSize;
            float dx = sectionCenterX - playerPos.x;
            float dz = sectionCenterZ - playerPos.z;
            if (dx * dx + dz * dz > kCloudSpawnRadiusSq) continue;

            int saltBase = 97 + section.lod * 131;
            float presence = hash01(section.coord.x, section.coord.y, section.coord.z, saltBase);
            if (presence < 0.42f) continue;

            int clusters = 1;
            if (hash01(section.coord.x, section.coord.y, section.coord.z, saltBase + 1) > 0.65f) ++clusters;
            if (hash01(section.coord.x, section.coord.y, section.coord.z, saltBase + 2) > 0.88f) ++clusters;

            for (int i = 0; i < clusters; ++i) {
                float seedA = hash01(section.coord.x, section.coord.y, section.coord.z, saltBase + 11 + i * 7);
                float seedB = hash01(section.coord.x, section.coord.y, section.coord.z, saltBase + 21 + i * 11);
                float seedC = hash01(section.coord.x, section.coord.y, section.coord.z, saltBase + 31 + i * 13);
                float angle = seedA * 6.28318530718f + static_cast<float>(i) * 1.61803f;
                float radius = (0.15f + 0.70f * seedB) * sectionWorldSize * 0.45f;
                glm::vec2 off = glm::vec2(std::cos(angle), std::sin(angle)) * radius;

                glm::vec3 basePos(sectionCenterX + off.x + drift.x,
                                  kCloudBase + kCloudHeightRange * seedC,
                                  sectionCenterZ + off.y + drift.y);
                float tint = 0.95f + 0.05f * hash01(section.coord.x, section.coord.y, section.coord.z, saltBase + 41 + i * 17);
                float scaleX = 180.0f + 220.0f * hash01(section.coord.x, section.coord.y, section.coord.z, saltBase + 51 + i * 19);
                float scaleY = 120.0f + 170.0f * hash01(section.coord.x, section.coord.y, section.coord.z, saltBase + 61 + i * 23);
                float alpha = 0.32f + 0.20f * hash01(section.coord.x, section.coord.y, section.coord.z, saltBase + 71 + i * 29);

                FaceInstanceRenderData instA;
                instA.position = basePos;
                instA.color = glm::vec3(tint);
                instA.tileIndex = -1;
                instA.alpha = alpha;
                instA.ao = glm::vec4(1.0f);
                instA.scale = glm::vec2(scaleX, scaleY);
                instA.uvScale = glm::vec2(3.0f, 2.0f);
                cloudInstances.push_back(instA);
                if (cloudInstances.size() >= kMaxCloudInstances) break;

                FaceInstanceRenderData instB = instA;
                instB.position += glm::vec3((hash01(section.coord.x, section.coord.y, section.coord.z, saltBase + 81 + i * 31) - 0.5f) * 80.0f,
                                            45.0f,
                                            (hash01(section.coord.x, section.coord.y, section.coord.z, saltBase + 91 + i * 37) - 0.5f) * 80.0f);
                instB.scale *= glm::vec2(0.72f, 0.62f);
                instB.alpha *= 0.85f;
                instB.color = glm::vec3(tint * 0.98f);
                cloudInstances.push_back(instB);
                if (cloudInstances.size() >= kMaxCloudInstances) break;
            }
            if (cloudInstances.size() >= kMaxCloudInstances) break;
        }

        if (cloudInstances.empty()) return;

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);

        renderer.faceShader->use();
        renderer.faceShader->setMat4("view", view);
        renderer.faceShader->setMat4("projection", projection);
        renderer.faceShader->setMat4("model", glm::mat4(1.0f));
        renderer.faceShader->setVec3("cameraPos", playerPos);
        renderer.faceShader->setVec3("lightDir", lightDir);
        renderer.faceShader->setVec3("ambientLight", glm::vec3(0.95f));
        renderer.faceShader->setVec3("diffuseLight", glm::vec3(0.05f));
        renderer.faceShader->setInt("wireframeDebug", 0);
        renderer.faceShader->setInt("atlasEnabled", 0);
        renderer.faceShader->setVec2("atlasTileSize", glm::vec2(1.0f));
        renderer.faceShader->setVec2("atlasTextureSize", glm::vec2(1.0f));
        renderer.faceShader->setInt("tilesPerRow", 1);
        renderer.faceShader->setInt("tilesPerCol", 1);
        renderer.faceShader->setInt("atlasTexture", 0);
        renderer.faceShader->setInt("faceType", 2); // Horizontal (+Y) cloud sheet.

        glBindVertexArray(renderer.faceVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.faceInstanceVBO);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(cloudInstances.size() * sizeof(FaceInstanceRenderData)),
                     cloudInstances.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(cloudInstances.size()));

        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

}
