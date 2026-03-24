#pragma once

#include <array>
#include <iostream>
#include <vector>

namespace VoxelMeshingSystemLogic { void StopGreedyAsync(); }

namespace RenderInitSystemLogic {

    RenderBehavior BehaviorForPrototype(const Entity& proto) {
        if (proto.name == "Branch") return RenderBehavior::STATIC_BRANCH;
        if (proto.name == "Water") return RenderBehavior::ANIMATED_WATER;
        if (proto.name == "TransparentWave") return RenderBehavior::ANIMATED_TRANSPARENT_WAVE;
        if (proto.hasWireframe && proto.isAnimated) return RenderBehavior::ANIMATED_WIREFRAME;
        return RenderBehavior::STATIC_DEFAULT;
    }

    void DestroyVoxelGreedyRenderBuffers(VoxelGreedyRenderBuffers& buffers, IRenderBackend& renderBackend) {
        for (size_t i = 0; i < buffers.opaqueVaos.size(); ++i) {
            renderBackend.destroyVertexArray(buffers.opaqueVaos[i]);
            renderBackend.destroyArrayBuffer(buffers.opaqueVBOs[i]);
            buffers.opaqueCounts[i] = 0;
            renderBackend.destroyVertexArray(buffers.alphaVaos[i]);
            renderBackend.destroyArrayBuffer(buffers.alphaVBOs[i]);
            buffers.alphaCounts[i] = 0;
        }
    }

    int FaceTileIndexFor(const WorldContext* worldCtx, const Entity& proto, int faceType) {
        if (!proto.useTexture) return -1;
        int legacyExternalTile = -1;
        if (proto.textureKey == "DirtExternal") legacyExternalTile = -20;
        if (proto.textureKey == "StoneExternal") legacyExternalTile = -21;
        if (!worldCtx) return legacyExternalTile;
        if (proto.prototypeID < 0 || proto.prototypeID >= static_cast<int>(worldCtx->prototypeTextureSets.size())) return legacyExternalTile;
        const FaceTextureSet& set = worldCtx->prototypeTextureSets[proto.prototypeID];
        auto resolve = [&](int specific) -> int {
            if (specific >= 0) return specific;
            if (set.all >= 0) return set.all;
            return legacyExternalTile;
        };
        int side = resolve(set.side);
        int top = resolve(set.top);
        int bottom = resolve(set.bottom);
        const bool isFirLogX = (proto.name == "FirLog1TexX" || proto.name == "FirLog2TexX");
        const bool isFirLogZ = (proto.name == "FirLog1TexZ" || proto.name == "FirLog2TexZ");
        const bool isStickX = (proto.name == "StickTexX");
        const bool isStickZ = (proto.name == "StickTexZ");
        const bool isWorkbenchPosX = (proto.name == "WorkbenchTexPosX");
        const bool isWorkbenchNegX = (proto.name == "WorkbenchTexNegX");
        const bool isWorkbenchPosZ = (proto.name == "WorkbenchTexPosZ");
        const bool isWorkbenchNegZ = (proto.name == "WorkbenchTexNegZ");
        const bool isFirNubX = (proto.name == "FirLog1NubTexX" || proto.name == "FirLog2NubTexX");
        const bool isFirNubZ = (proto.name == "FirLog1NubTexZ" || proto.name == "FirLog2NubTexZ");
        const bool isFirTopY = (proto.name == "FirLog1TopTex" || proto.name == "FirLog2TopTex");
        if (isFirNubX) {
            if (faceType == 0 || faceType == 1) return (side >= 0) ? side : top;
            return side;
        }
        if (isFirNubZ) {
            if (faceType == 4 || faceType == 5) return (side >= 0) ? side : top;
            return side;
        }
        if (isFirLogX) {
            if (faceType == 0 || faceType == 1) return (top >= 0) ? top : side;
            return side;
        }
        if (isFirLogZ) {
            if (faceType == 4 || faceType == 5) return (top >= 0) ? top : side;
            return side;
        }
        if (isStickX) {
            if (faceType == 0 || faceType == 1) return (top >= 0) ? top : side;
            return side;
        }
        if (isStickZ) {
            if (faceType == 4 || faceType == 5) return (top >= 0) ? top : side;
            return side;
        }
        if (isWorkbenchPosX || isWorkbenchNegX || isWorkbenchPosZ || isWorkbenchNegZ) {
            if (faceType == 2) return top;
            if (faceType == 3) return bottom;
            const int sideTile = (side >= 0) ? side : top;
            const int frontTile = 276;
            if (isWorkbenchPosX && faceType == 0) return frontTile;
            if (isWorkbenchNegX && faceType == 1) return frontTile;
            if (isWorkbenchPosZ && faceType == 4) return frontTile;
            if (isWorkbenchNegZ && faceType == 5) return frontTile;
            return sideTile;
        }
        if (isFirTopY) {
            if (faceType == 2) return (side >= 0) ? side : top;
            if (faceType == 3) return (bottom >= 0) ? bottom : side;
            return side;
        }
        switch (faceType) {
            case 2: return top;
            case 3: return bottom;
            default: return side;
        }
    }

    void DestroyChunkRenderBuffers(ChunkRenderBuffers& buffers, IRenderBackend& renderBackend) {
        for (RenderHandle& vao : buffers.vaos) {
            renderBackend.destroyVertexArray(vao);
        }
        for (RenderHandle& vbo : buffers.instanceVBOs) {
            renderBackend.destroyArrayBuffer(vbo);
        }
        buffers.vaos.fill(0);
        buffers.instanceVBOs.fill(0);
        buffers.counts.fill(0);
        buffers.builtWithFaceCulling = false;
    }

    int getRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback) {
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

    bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
        if (!baseSystem.registry) return fallback;
        auto it = baseSystem.registry->find(key);
        if (it == baseSystem.registry->end()) return fallback;
        if (!std::holds_alternative<bool>(it->second)) return fallback;
        return std::get<bool>(it->second);
    }

    float getRegistryFloat(const BaseSystem& baseSystem, const std::string& key, float fallback) {
        if (!baseSystem.registry) return fallback;
        auto it = baseSystem.registry->find(key);
        if (it == baseSystem.registry->end()) return fallback;
        if (!std::holds_alternative<std::string>(it->second)) return fallback;
        try {
            return std::stof(std::get<std::string>(it->second));
        } catch (...) {
            return fallback;
        }
    }

    namespace {
        struct FrustumCache {
            bool valid = false;
            glm::mat4 lastViewProj{1.0f};
            std::array<glm::vec4, 6> planes{};
        };

        void normalizePlane(glm::vec4& plane) {
            glm::vec3 n(plane.x, plane.y, plane.z);
            float len = glm::length(n);
            if (len <= 1e-6f) return;
            plane /= len;
        }

        void extractFrustumPlanes(const glm::mat4& viewProj, std::array<glm::vec4, 6>& planes) {
            // GLM is column-major: reconstruct matrix rows explicitly.
            glm::vec4 row0(viewProj[0][0], viewProj[1][0], viewProj[2][0], viewProj[3][0]);
            glm::vec4 row1(viewProj[0][1], viewProj[1][1], viewProj[2][1], viewProj[3][1]);
            glm::vec4 row2(viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]);
            glm::vec4 row3(viewProj[0][3], viewProj[1][3], viewProj[2][3], viewProj[3][3]);

            planes[0] = row3 + row0; // left
            planes[1] = row3 - row0; // right
            planes[2] = row3 + row1; // bottom
            planes[3] = row3 - row1; // top
            planes[4] = row3 + row2; // near
            planes[5] = row3 - row2; // far

            for (auto& p : planes) normalizePlane(p);
        }

        bool aabbIntersectsFrustum(const std::array<glm::vec4, 6>& planes,
                                   const glm::vec3& minB,
                                   const glm::vec3& maxB,
                                   float margin) {
            glm::vec3 expandedMin = minB - glm::vec3(margin);
            glm::vec3 expandedMax = maxB + glm::vec3(margin);
            for (const glm::vec4& plane : planes) {
                glm::vec3 n(plane.x, plane.y, plane.z);
                glm::vec3 positive(
                    n.x >= 0.0f ? expandedMax.x : expandedMin.x,
                    n.y >= 0.0f ? expandedMax.y : expandedMin.y,
                    n.z >= 0.0f ? expandedMax.z : expandedMin.z
                );
                if (glm::dot(n, positive) + plane.w < 0.0f) return false;
            }
            return true;
        }

        bool shouldRenderByFrustum(const BaseSystem& baseSystem,
                                   const glm::vec3& minB,
                                   const glm::vec3& maxB) {
            if (!getRegistryBool(baseSystem, "voxelFrustumCulling", true)) return true;
            if (!baseSystem.player) return true;
            const PlayerContext& player = *baseSystem.player;

            static FrustumCache cache;
            glm::mat4 viewProj = player.projectionMatrix * player.viewMatrix;
            if (!cache.valid || cache.lastViewProj != viewProj) {
                cache.lastViewProj = viewProj;
                extractFrustumPlanes(viewProj, cache.planes);
                cache.valid = true;
            }
            float margin = glm::clamp(getRegistryFloat(baseSystem, "voxelFrustumMargin", 12.0f), 0.0f, 128.0f);
            return aabbIntersectsFrustum(cache.planes, minB, maxB, margin);
        }
    }

    bool shouldRenderVoxelSection(const BaseSystem& baseSystem,
                                  const VoxelSection& section,
                                  const glm::vec3& cameraPos) {
        int scale = 1 << section.lod;
        glm::vec3 minB3(
            static_cast<float>(section.coord.x * section.size * scale),
            static_cast<float>(section.coord.y * section.size * scale),
            static_cast<float>(section.coord.z * section.size * scale)
        );
        glm::vec3 maxB3 = minB3 + glm::vec3(static_cast<float>(section.size * scale));
        if (section.lod == 0) {
            // Keep LOD0 available but still skip fully off-screen sections.
            return shouldRenderByFrustum(baseSystem, minB3, maxB3);
        }
        int radius = getRegistryInt(baseSystem, "voxelLod" + std::to_string(section.lod) + "Radius", 0);
        if (radius <= 0) return false;
        int prevRadius = (section.lod > 0)
            ? getRegistryInt(baseSystem, "voxelLod" + std::to_string(section.lod - 1) + "Radius", 0)
            : 0;
        glm::vec2 minB(section.coord.x * section.size * scale,
                       section.coord.z * section.size * scale);
        glm::vec2 maxB = minB + glm::vec2(section.size * scale);
        glm::vec2 camXZ(cameraPos.x, cameraPos.z);
        float dx = 0.0f;
        if (camXZ.x < minB.x) dx = minB.x - camXZ.x;
        else if (camXZ.x > maxB.x) dx = camXZ.x - maxB.x;
        float dz = 0.0f;
        if (camXZ.y < minB.y) dz = minB.y - camXZ.y;
        else if (camXZ.y > maxB.y) dz = camXZ.y - maxB.y;
        float dist = std::sqrt(dx * dx + dz * dz);
        if (dist > static_cast<float>(radius)) return false;
        if (prevRadius > 0) {
            float dxMax = std::max(std::abs(camXZ.x - minB.x), std::abs(camXZ.x - maxB.x));
            float dzMax = std::max(std::abs(camXZ.y - minB.y), std::abs(camXZ.y - maxB.y));
            float maxDist = std::sqrt(dxMax * dxMax + dzMax * dzMax);
            if (maxDist <= static_cast<float>(prevRadius)) return false;
        }
        return shouldRenderByFrustum(baseSystem, minB3, maxB3);
    }

    bool shouldRenderVoxelSectionSized(const BaseSystem& baseSystem,
                                       int lod,
                                       const glm::ivec3& sectionCoord,
                                       int sectionSize,
                                       int sizeMultiplier,
                                       const glm::vec3& cameraPos) {
        int scale = 1 << lod;
        int size = sectionSize * sizeMultiplier * scale;
        glm::vec3 minB3(
            static_cast<float>(sectionCoord.x * sectionSize * scale),
            static_cast<float>(sectionCoord.y * sectionSize * scale),
            static_cast<float>(sectionCoord.z * sectionSize * scale)
        );
        glm::vec3 maxB3 = minB3 + glm::vec3(static_cast<float>(size));
        if (lod == 0) {
            return shouldRenderByFrustum(baseSystem, minB3, maxB3);
        }
        int radius = getRegistryInt(baseSystem, "voxelLod" + std::to_string(lod) + "Radius", 0);
        if (radius <= 0) return false;
        int prevRadius = (lod > 0)
            ? getRegistryInt(baseSystem, "voxelLod" + std::to_string(lod - 1) + "Radius", 0)
            : 0;
        glm::vec2 minB(sectionCoord.x * sectionSize * scale,
                       sectionCoord.z * sectionSize * scale);
        glm::vec2 maxB = minB + glm::vec2(size);
        glm::vec2 camXZ(cameraPos.x, cameraPos.z);
        float dx = 0.0f;
        if (camXZ.x < minB.x) dx = minB.x - camXZ.x;
        else if (camXZ.x > maxB.x) dx = camXZ.x - maxB.x;
        float dz = 0.0f;
        if (camXZ.y < minB.y) dz = minB.y - camXZ.y;
        else if (camXZ.y > maxB.y) dz = camXZ.y - maxB.y;
        float dist = std::sqrt(dx * dx + dz * dz);
        if (dist > static_cast<float>(radius)) return false;
        if (prevRadius > 0) {
            float dxMax = std::max(std::abs(camXZ.x - minB.x), std::abs(camXZ.x - maxB.x));
            float dzMax = std::max(std::abs(camXZ.y - minB.y), std::abs(camXZ.y - maxB.y));
            float maxDist = std::sqrt(dxMax * dxMax + dzMax * dzMax);
            if (maxDist <= static_cast<float>(prevRadius)) return false;
        }
        return shouldRenderByFrustum(baseSystem, minB3, maxB3);
    }

    void InitializeRenderer(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.renderBackend) {
            std::cerr << "ERROR: RenderSystem cannot init without RendererContext, WorldContext, and render backend." << std::endl;
            return;
        }
        WorldContext& world = *baseSystem.world;
        RendererContext& renderer = *baseSystem.renderer;
        IRenderBackend& renderBackend = *baseSystem.renderBackend;
        auto ensureShader = [&](std::unique_ptr<Shader>& shader,
                                const char* vertexKey,
                                const char* fragmentKey,
                                const char* label) {
            const auto vertexIt = world.shaders.find(vertexKey);
            const auto fragmentIt = world.shaders.find(fragmentKey);
            if (vertexIt == world.shaders.end() || fragmentIt == world.shaders.end()) {
                std::cerr << "RenderInitSystem: missing shader source for " << label << std::endl;
                return;
            }
            const char* vertexSource = vertexIt->second.c_str();
            const char* fragmentSource = fragmentIt->second.c_str();
            if (!shader) {
                shader = std::make_unique<Shader>(vertexSource, fragmentSource);
                if (!shader->isValid()) {
                    std::cerr << "RenderInitSystem: failed to create shader for " << label << std::endl;
                    shader.reset();
                }
                return;
            }
            std::string rebuildError;
            if (!shader->rebuild(vertexSource, fragmentSource, &rebuildError)) {
                std::cerr << "RenderInitSystem: failed to rebuild shader for " << label
                          << " (" << rebuildError << ")" << std::endl;
            }
        };

        ensureShader(renderer.blockShader, "BLOCK_VERTEX_SHADER", "BLOCK_FRAGMENT_SHADER", "blockShader");
        ensureShader(renderer.faceShader, "FACE_VERTEX_SHADER", "FACE_FRAGMENT_SHADER", "faceShader");
        ensureShader(renderer.skyboxShader, "SKYBOX_VERTEX_SHADER", "SKYBOX_FRAGMENT_SHADER", "skyboxShader");
        ensureShader(renderer.sunMoonShader, "SUNMOON_VERTEX_SHADER", "SUNMOON_FRAGMENT_SHADER", "sunMoonShader");
        ensureShader(renderer.starShader, "STAR_VERTEX_SHADER", "STAR_FRAGMENT_SHADER", "starShader");
        ensureShader(renderer.godrayRadialShader, "GODRAY_VERTEX_SHADER", "GODRAY_RADIAL_FRAGMENT_SHADER", "godrayRadialShader");
        ensureShader(renderer.godrayCompositeShader, "GODRAY_VERTEX_SHADER", "GODRAY_COMPOSITE_FRAGMENT_SHADER", "godrayCompositeShader");
        const std::vector<VertexAttribLayout> kMeshVertexLayout = {
            {0u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(8 * sizeof(float)), 0u, 0u},
            {1u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(8 * sizeof(float)), static_cast<size_t>(3 * sizeof(float)), 0u},
            {2u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(8 * sizeof(float)), static_cast<size_t>(6 * sizeof(float)), 0u},
        };
        const std::vector<VertexAttribLayout> kBranchInstanceLayout = {
            {3u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(BranchInstanceData)), offsetof(BranchInstanceData, position), 1u},
            {4u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(BranchInstanceData)), offsetof(BranchInstanceData, rotation), 1u},
            {5u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(BranchInstanceData)), offsetof(BranchInstanceData, color), 1u},
        };
        const std::vector<VertexAttribLayout> kBlockInstanceLayout = {
            {3u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(InstanceData)), offsetof(InstanceData, position), 1u},
            {4u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(InstanceData)), offsetof(InstanceData, color), 1u},
            {5u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(InstanceData)), offsetof(InstanceData, color), 1u},
        };
        const std::vector<VertexAttribLayout> kFaceInstanceLayout = {
            {3u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, position), 1u},
            {4u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, color), 1u},
            {5u, 1, VertexAttribType::Int,   false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, tileIndex), 1u},
            {6u, 1, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, alpha), 1u},
            {7u, 4, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, ao), 1u},
            {8u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, scale), 1u},
            {9u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(FaceInstanceRenderData)), offsetof(FaceInstanceRenderData, uvScale), 1u},
        };
        const std::vector<VertexAttribLayout> kPos2Layout = {
            {0u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(2 * sizeof(float)), 0u, 0u},
        };
        const std::vector<VertexAttribLayout> kPos3Norm3Layout = {
            {0u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(6 * sizeof(float)), 0u, 0u},
            {1u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(6 * sizeof(float)), static_cast<size_t>(3 * sizeof(float)), 0u},
        };
        const std::vector<VertexAttribLayout> kPos2Uv2Layout = {
            {0u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(4 * sizeof(float)), 0u, 0u},
            {1u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(4 * sizeof(float)), static_cast<size_t>(2 * sizeof(float)), 0u},
        };
        const std::vector<VertexAttribLayout> kPos2Color3Layout = {
            {0u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(5 * sizeof(float)), 0u, 0u},
            {1u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(5 * sizeof(float)), static_cast<size_t>(2 * sizeof(float)), 0u},
        };
        int behaviorCount = static_cast<int>(RenderBehavior::COUNT);
        renderer.behaviorVAOs.resize(behaviorCount);
        renderer.behaviorInstanceVBOs.resize(behaviorCount);
        renderBackend.ensureArrayBuffer(renderer.cubeVBO);
        renderBackend.uploadArrayBufferData(
            renderer.cubeVBO,
            world.cubeVertices.data(),
            world.cubeVertices.size() * sizeof(float),
            false
        );
        for (int i = 0; i < behaviorCount; ++i) {
            renderBackend.ensureVertexArray(renderer.behaviorVAOs[i]);
            renderBackend.ensureArrayBuffer(renderer.behaviorInstanceVBOs[i]);
            if (static_cast<RenderBehavior>(i) == RenderBehavior::STATIC_BRANCH) {
                renderBackend.configureVertexArray(
                    renderer.behaviorVAOs[i],
                    renderer.cubeVBO,
                    kMeshVertexLayout,
                    renderer.behaviorInstanceVBOs[i],
                    kBranchInstanceLayout
                );
            } else {
                renderBackend.configureVertexArray(
                    renderer.behaviorVAOs[i],
                    renderer.cubeVBO,
                    kMeshVertexLayout,
                    renderer.behaviorInstanceVBOs[i],
                    kBlockInstanceLayout
                );
            }
        }
        float skyboxQuadVertices[]={-1,1,-1,-1,1,-1,-1,1,1,-1,1,1};
        renderBackend.ensureVertexArray(renderer.skyboxVAO);
        renderBackend.ensureArrayBuffer(renderer.skyboxVBO);
        renderBackend.uploadArrayBufferData(renderer.skyboxVBO, skyboxQuadVertices, sizeof(skyboxQuadVertices), false);
        renderBackend.configureVertexArray(renderer.skyboxVAO, renderer.skyboxVBO, kPos2Layout, 0, {});
        renderBackend.ensureVertexArray(renderer.sunMoonVAO);
        renderBackend.ensureArrayBuffer(renderer.sunMoonVBO);
        renderBackend.uploadArrayBufferData(renderer.sunMoonVBO, skyboxQuadVertices, sizeof(skyboxQuadVertices), false);
        renderBackend.configureVertexArray(renderer.sunMoonVAO, renderer.sunMoonVBO, kPos2Layout, 0, {});
        renderBackend.ensureVertexArray(renderer.starVAO);
        renderBackend.ensureArrayBuffer(renderer.starVBO);

        float faceVerts[] = {
            -0.5f, -0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
             0.5f, -0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f,
             0.5f,  0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
            -0.5f, -0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
             0.5f,  0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
            -0.5f,  0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f
        };
        renderBackend.ensureVertexArray(renderer.faceVAO);
        renderBackend.ensureArrayBuffer(renderer.faceVBO);
        renderBackend.ensureArrayBuffer(renderer.faceInstanceVBO);
        renderBackend.uploadArrayBufferData(renderer.faceVBO, faceVerts, sizeof(faceVerts), false);
        renderBackend.configureVertexArray(
            renderer.faceVAO,
            renderer.faceVBO,
            kMeshVertexLayout,
            renderer.faceInstanceVBO,
            kFaceInstanceLayout
        );

        // Godray quad
        float quadVerts[] = { -1,-1,  1,-1,  -1,1,  -1,1,  1,-1,  1,1 };
        renderBackend.ensureVertexArray(renderer.godrayQuadVAO);
        renderBackend.ensureArrayBuffer(renderer.godrayQuadVBO);
        renderBackend.uploadArrayBufferData(renderer.godrayQuadVBO, quadVerts, sizeof(quadVerts), false);
        renderBackend.configureVertexArray(renderer.godrayQuadVAO, renderer.godrayQuadVBO, kPos2Layout, 0, {});

        // Godray FBOs
        renderer.godrayWidth = baseSystem.app ? baseSystem.app->windowWidth / renderer.godrayDownsample : 960;
        renderer.godrayHeight = baseSystem.app ? baseSystem.app->windowHeight / renderer.godrayDownsample : 540;

        auto setupFBO = [&](RenderHandle& fbo, RenderHandle& tex, int w, int h){
            if (renderBackend.createColorRenderTarget(w, h, fbo, tex)) {
                return;
            }
            std::cerr << "RenderInitSystem: failed to create color render target " << w << "x" << h << '\n';
            fbo = 0;
            tex = 0;
        };

        setupFBO(renderer.godrayOcclusionFBO, renderer.godrayOcclusionTex, renderer.godrayWidth, renderer.godrayHeight);
        setupFBO(renderer.godrayBlurFBO, renderer.godrayBlurTex, renderer.godrayWidth, renderer.godrayHeight);

        ensureShader(renderer.selectionShader, "SELECTION_VERTEX_SHADER", "SELECTION_FRAGMENT_SHADER", "selectionShader");
        std::vector<float> selectionVertices;
        selectionVertices.reserve((12 + 12) * 2 * 6);
        auto pushVertex = [&](const glm::vec3& pos, const glm::vec3& normal){
            selectionVertices.push_back(pos.x);
            selectionVertices.push_back(pos.y);
            selectionVertices.push_back(pos.z);
            selectionVertices.push_back(normal.x);
            selectionVertices.push_back(normal.y);
            selectionVertices.push_back(normal.z);
        };
        auto addLine = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& normal){
            pushVertex(a, normal);
            pushVertex(b, normal);
        };
        auto addFace = [&](const glm::vec3& normal, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d){
            addLine(a, c, normal);
            addLine(b, d, normal);
            addLine(a, b, normal);
            addLine(b, c, normal);
            addLine(c, d, normal);
            addLine(d, a, normal);
        };
        addFace(glm::vec3(0,0,1),
                glm::vec3(-0.5f,-0.5f,0.5f),
                glm::vec3(0.5f,-0.5f,0.5f),
                glm::vec3(0.5f,0.5f,0.5f),
                glm::vec3(-0.5f,0.5f,0.5f));
        addFace(glm::vec3(0,0,-1),
                glm::vec3(-0.5f,-0.5f,-0.5f),
                glm::vec3(0.5f,-0.5f,-0.5f),
                glm::vec3(0.5f,0.5f,-0.5f),
                glm::vec3(-0.5f,0.5f,-0.5f));
        addFace(glm::vec3(1,0,0),
                glm::vec3(0.5f,-0.5f,-0.5f),
                glm::vec3(0.5f,-0.5f,0.5f),
                glm::vec3(0.5f,0.5f,0.5f),
                glm::vec3(0.5f,0.5f,-0.5f));
        addFace(glm::vec3(-1,0,0),
                glm::vec3(-0.5f,-0.5f,-0.5f),
                glm::vec3(-0.5f,-0.5f,0.5f),
                glm::vec3(-0.5f,0.5f,0.5f),
                glm::vec3(-0.5f,0.5f,-0.5f));
        addFace(glm::vec3(0,1,0),
                glm::vec3(-0.5f,0.5f,-0.5f),
                glm::vec3(0.5f,0.5f,-0.5f),
                glm::vec3(0.5f,0.5f,0.5f),
                glm::vec3(-0.5f,0.5f,0.5f));
        addFace(glm::vec3(0,-1,0),
                glm::vec3(-0.5f,-0.5f,-0.5f),
                glm::vec3(0.5f,-0.5f,-0.5f),
                glm::vec3(0.5f,-0.5f,0.5f),
                glm::vec3(-0.5f,-0.5f,0.5f));

        renderer.selectionVertexCount = static_cast<int>(selectionVertices.size() / 6);
        renderBackend.ensureVertexArray(renderer.selectionVAO);
        renderBackend.ensureArrayBuffer(renderer.selectionVBO);
        renderBackend.uploadArrayBufferData(
            renderer.selectionVBO,
            selectionVertices.data(),
            selectionVertices.size() * sizeof(float),
            false
        );
        renderBackend.configureVertexArray(renderer.selectionVAO, renderer.selectionVBO, kPos3Norm3Layout, 0, {});

        ensureShader(renderer.hudShader, "HUD_VERTEX_SHADER", "HUD_FRAGMENT_SHADER", "hudShader");
        ensureShader(renderer.colorEmotionShader,
                     "COLOR_EMOTION_VERTEX_SHADER",
                     "COLOR_EMOTION_FRAGMENT_SHADER",
                     "colorEmotionShader");
        ensureShader(renderer.crosshairShader, "CROSSHAIR_VERTEX_SHADER", "CROSSHAIR_FRAGMENT_SHADER", "crosshairShader");
        float hudVertices[] = {
            0.035f, -0.05f, 0.0f, 0.0f,
            0.08f,  -0.05f, 1.0f, 0.0f,
            0.08f,   0.05f, 1.0f, 1.0f,
            0.035f, -0.05f, 0.0f, 0.0f,
            0.08f,   0.05f, 1.0f, 1.0f,
            0.035f,  0.05f, 0.0f, 1.0f
        };
        renderBackend.ensureVertexArray(renderer.hudVAO);
        renderBackend.ensureArrayBuffer(renderer.hudVBO);
        renderBackend.uploadArrayBufferData(renderer.hudVBO, hudVertices, sizeof(hudVertices), false);
        renderBackend.configureVertexArray(renderer.hudVAO, renderer.hudVBO, kPos2Uv2Layout, 0, {});

        float colorEmotionVertices[] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
            -1.0f,  1.0f, 0.0f, 1.0f
        };
        renderBackend.ensureVertexArray(renderer.colorEmotionVAO);
        renderBackend.ensureArrayBuffer(renderer.colorEmotionVBO);
        renderBackend.uploadArrayBufferData(renderer.colorEmotionVBO, colorEmotionVertices, sizeof(colorEmotionVertices), false);
        renderBackend.configureVertexArray(renderer.colorEmotionVAO, renderer.colorEmotionVBO, kPos2Uv2Layout, 0, {});

        float chLen = 0.02f;
        float chLenH = 0.016f;
        float crosshairVertices[] = {
            0.0f, -chLen, 1.0f, 1.0f, 1.0f,
            0.0f,  chLen, 1.0f, 1.0f, 1.0f,
            -chLenH, 0.0f, 1.0f, 1.0f, 1.0f,
             chLenH, 0.0f, 1.0f, 1.0f, 1.0f
        };
        renderBackend.ensureVertexArray(renderer.crosshairVAO);
        renderBackend.ensureArrayBuffer(renderer.crosshairVBO);
        renderBackend.uploadArrayBufferData(renderer.crosshairVBO, crosshairVertices, sizeof(crosshairVertices), false);
        renderBackend.configureVertexArray(renderer.crosshairVAO, renderer.crosshairVBO, kPos2Color3Layout, 0, {});
        renderer.crosshairVertexCount = 4;

        renderBackend.ensureVertexArray(renderer.leyLineDebugVAO);
        renderBackend.ensureArrayBuffer(renderer.leyLineDebugVBO);
        renderBackend.uploadArrayBufferData(renderer.leyLineDebugVBO, nullptr, 0, true);
        renderBackend.configureVertexArray(renderer.leyLineDebugVAO, renderer.leyLineDebugVBO, kPos3Norm3Layout, 0, {});
        renderer.leyLineDebugVertexCount = 0;
        renderBackend.unbindVertexArray();
    }


    void CleanupRenderer(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.renderer || !baseSystem.renderBackend) return;
        VoxelMeshingSystemLogic::StopGreedyAsync();
        RendererContext& renderer = *baseSystem.renderer;
        IRenderBackend& renderBackend = *baseSystem.renderBackend;
        if (baseSystem.voxelGreedy) {
            for (auto& [_, buffers] : baseSystem.voxelGreedy->renderBuffers) {
                DestroyVoxelGreedyRenderBuffers(buffers, renderBackend);
            }
            baseSystem.voxelGreedy->renderBuffers.clear();
            baseSystem.voxelGreedy->renderBuffersDirty.clear();
        }
        for (RenderHandle& vao : renderer.behaviorVAOs) {
            renderBackend.destroyVertexArray(vao);
        }
        for (RenderHandle& vbo : renderer.behaviorInstanceVBOs) {
            renderBackend.destroyArrayBuffer(vbo);
        }
        renderBackend.destroyVertexArray(renderer.skyboxVAO);
        renderBackend.destroyVertexArray(renderer.sunMoonVAO);
        renderBackend.destroyVertexArray(renderer.starVAO);
        renderBackend.destroyArrayBuffer(renderer.cubeVBO);
        renderBackend.destroyArrayBuffer(renderer.skyboxVBO);
        renderBackend.destroyArrayBuffer(renderer.sunMoonVBO);
        renderBackend.destroyArrayBuffer(renderer.starVBO);
        renderBackend.destroyVertexArray(renderer.selectionVAO);
        renderBackend.destroyArrayBuffer(renderer.selectionVBO);
        renderBackend.destroyVertexArray(renderer.hudVAO);
        renderBackend.destroyArrayBuffer(renderer.hudVBO);
        renderBackend.destroyVertexArray(renderer.colorEmotionVAO);
        renderBackend.destroyArrayBuffer(renderer.colorEmotionVBO);
        renderBackend.destroyVertexArray(renderer.crosshairVAO);
        renderBackend.destroyArrayBuffer(renderer.crosshairVBO);
        renderBackend.destroyVertexArray(renderer.uiVAO);
        renderBackend.destroyArrayBuffer(renderer.uiVBO);
        renderBackend.destroyVertexArray(renderer.uiButtonVAO);
        renderBackend.destroyArrayBuffer(renderer.uiButtonVBO);
        renderBackend.destroyVertexArray(renderer.fontVAO);
        renderBackend.destroyArrayBuffer(renderer.fontVBO);
        renderBackend.destroyVertexArray(renderer.audioRayVAO);
        renderBackend.destroyArrayBuffer(renderer.audioRayVBO);
        renderBackend.destroyVertexArray(renderer.leyLineDebugVAO);
        renderBackend.destroyArrayBuffer(renderer.leyLineDebugVBO);
        renderBackend.destroyVertexArray(renderer.fishingVAO);
        renderBackend.destroyArrayBuffer(renderer.fishingVBO);
        renderBackend.destroyVertexArray(renderer.gemVAO);
        renderBackend.destroyArrayBuffer(renderer.gemVBO);
        renderBackend.destroyVertexArray(renderer.audioRayVoxelVAO);
        renderBackend.destroyArrayBuffer(renderer.audioRayVoxelInstanceVBO);
        renderBackend.destroyVertexArray(renderer.faceVAO);
        renderBackend.destroyArrayBuffer(renderer.faceVBO);
        renderBackend.destroyArrayBuffer(renderer.faceInstanceVBO);
        auto destroyTexture = [&](RenderHandle& texture) {
            if (!texture) return;
            renderBackend.destroyTexture(texture);
        };
        destroyTexture(renderer.atlasTexture);
        for (RenderHandle& tex : renderer.grassTextures) {
            destroyTexture(tex);
        }
        renderer.grassTextureCount = 0;
        for (RenderHandle& tex : renderer.shortGrassTextures) {
            destroyTexture(tex);
        }
        renderer.shortGrassTextureCount = 0;
        for (RenderHandle& tex : renderer.oreTextures) {
            destroyTexture(tex);
        }
        renderer.oreTextureCount = 0;
        for (RenderHandle& tex : renderer.terrainTextures) {
            destroyTexture(tex);
        }
        renderer.terrainTextureCount = 0;
        destroyTexture(renderer.waterOverlayTexture);
        destroyTexture(renderer.isometricMapCachedTexture);
        renderer.isometricMapCachedWidth = 0;
        renderer.isometricMapCachedHeight = 0;
        destroyTexture(renderer.topDownMapTexture);
        renderer.topDownMapTextureWidth = 0;
        renderer.topDownMapTextureHeight = 0;
        renderBackend.destroyColorRenderTarget(renderer.godrayOcclusionFBO, renderer.godrayOcclusionTex);
        renderBackend.destroyColorRenderTarget(renderer.godrayBlurFBO, renderer.godrayBlurTex);

        renderer.blockShader.reset();
        renderer.skyboxShader.reset();
        renderer.sunMoonShader.reset();
        renderer.starShader.reset();
        renderer.selectionShader.reset();
        renderer.hudShader.reset();
        renderer.crosshairShader.reset();
        renderer.colorEmotionShader.reset();
        renderer.faceShader.reset();
        renderer.fontShader.reset();
        renderer.uiShader.reset();
        renderer.uiColorShader.reset();
        renderer.glyphShader.reset();
        renderer.audioRayShader.reset();
        renderer.audioRayVoxelShader.reset();
        renderer.godrayRadialShader.reset();
        renderer.godrayCompositeShader.reset();
        renderer.cloudShader.reset();
        renderer.auroraShader.reset();
    }
}
