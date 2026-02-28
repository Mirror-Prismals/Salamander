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

    void DestroyVoxelGreedyRenderBuffers(VoxelGreedyRenderBuffers& buffers) {
        for (size_t i = 0; i < buffers.opaqueVaos.size(); ++i) {
            if (buffers.opaqueVaos[i]) {
                glDeleteVertexArrays(1, &buffers.opaqueVaos[i]);
                buffers.opaqueVaos[i] = 0;
            }
            if (buffers.opaqueVBOs[i]) {
                glDeleteBuffers(1, &buffers.opaqueVBOs[i]);
                buffers.opaqueVBOs[i] = 0;
            }
            buffers.opaqueCounts[i] = 0;
            if (buffers.alphaVaos[i]) {
                glDeleteVertexArrays(1, &buffers.alphaVaos[i]);
                buffers.alphaVaos[i] = 0;
            }
            if (buffers.alphaVBOs[i]) {
                glDeleteBuffers(1, &buffers.alphaVBOs[i]);
                buffers.alphaVBOs[i] = 0;
            }
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

    void DestroyChunkRenderBuffers(ChunkRenderBuffers& buffers) {
        for (GLuint vao : buffers.vaos) {
            if (vao) glDeleteVertexArrays(1, &vao);
        }
        for (GLuint vbo : buffers.instanceVBOs) {
            if (vbo) glDeleteBuffers(1, &vbo);
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

    void InitializeRenderer(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.renderer || !baseSystem.world) { std::cerr << "ERROR: RenderSystem cannot init without RendererContext or WorldContext." << std::endl; return; }
        WorldContext& world = *baseSystem.world;
        RendererContext& renderer = *baseSystem.renderer;
        renderer.blockShader = std::make_unique<Shader>(world.shaders["BLOCK_VERTEX_SHADER"].c_str(), world.shaders["BLOCK_FRAGMENT_SHADER"].c_str());
        renderer.faceShader = std::make_unique<Shader>(world.shaders["FACE_VERTEX_SHADER"].c_str(), world.shaders["FACE_FRAGMENT_SHADER"].c_str());
        renderer.skyboxShader = std::make_unique<Shader>(world.shaders["SKYBOX_VERTEX_SHADER"].c_str(), world.shaders["SKYBOX_FRAGMENT_SHADER"].c_str());
        renderer.sunMoonShader = std::make_unique<Shader>(world.shaders["SUNMOON_VERTEX_SHADER"].c_str(), world.shaders["SUNMOON_FRAGMENT_SHADER"].c_str());
        renderer.starShader = std::make_unique<Shader>(world.shaders["STAR_VERTEX_SHADER"].c_str(), world.shaders["STAR_FRAGMENT_SHADER"].c_str());
        renderer.godrayRadialShader = std::make_unique<Shader>(world.shaders["GODRAY_VERTEX_SHADER"].c_str(), world.shaders["GODRAY_RADIAL_FRAGMENT_SHADER"].c_str());
        renderer.godrayCompositeShader = std::make_unique<Shader>(world.shaders["GODRAY_VERTEX_SHADER"].c_str(), world.shaders["GODRAY_COMPOSITE_FRAGMENT_SHADER"].c_str());
        int behaviorCount = static_cast<int>(RenderBehavior::COUNT);
        renderer.behaviorVAOs.resize(behaviorCount);
        renderer.behaviorInstanceVBOs.resize(behaviorCount);
        glGenVertexArrays(behaviorCount, renderer.behaviorVAOs.data());
        glGenBuffers(behaviorCount, renderer.behaviorInstanceVBOs.data());
        glGenBuffers(1, &renderer.cubeVBO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, world.cubeVertices.size() * sizeof(float), world.cubeVertices.data(), GL_STATIC_DRAW);
        for (int i = 0; i < behaviorCount; ++i) {
            glBindVertexArray(renderer.behaviorVAOs[i]);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.cubeVBO);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.behaviorInstanceVBOs[i]);
            if (static_cast<RenderBehavior>(i) == RenderBehavior::STATIC_BRANCH) {
                glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, position));
                glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, rotation));
                glEnableVertexAttribArray(5); glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, color));
                glVertexAttribDivisor(3, 1); glVertexAttribDivisor(4, 1); glVertexAttribDivisor(5, 1);
            } else {
                glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, position));
                glEnableVertexAttribArray(4); glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, color));
                glVertexAttribDivisor(3, 1); glVertexAttribDivisor(4, 1);
            }
        }
        float skyboxQuadVertices[]={-1,1,-1,-1,1,-1,-1,1,1,-1,1,1};
        glGenVertexArrays(1,&renderer.skyboxVAO);glGenBuffers(1,&renderer.skyboxVBO);
        glBindVertexArray(renderer.skyboxVAO);glBindBuffer(GL_ARRAY_BUFFER,renderer.skyboxVBO);
        glBufferData(GL_ARRAY_BUFFER,sizeof(skyboxQuadVertices),skyboxQuadVertices,GL_STATIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
        glGenVertexArrays(1,&renderer.sunMoonVAO);glGenBuffers(1,&renderer.sunMoonVBO);
        glBindVertexArray(renderer.sunMoonVAO);glBindBuffer(GL_ARRAY_BUFFER,renderer.sunMoonVBO);
        glBufferData(GL_ARRAY_BUFFER,sizeof(skyboxQuadVertices),skyboxQuadVertices,GL_STATIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
        glGenVertexArrays(1,&renderer.starVAO);glGenBuffers(1,&renderer.starVBO);

        float faceVerts[] = {
            -0.5f, -0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
             0.5f, -0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f,
             0.5f,  0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
            -0.5f, -0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
             0.5f,  0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
            -0.5f,  0.5f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f
        };
        glGenVertexArrays(1, &renderer.faceVAO);
        glGenBuffers(1, &renderer.faceVBO);
        glGenBuffers(1, &renderer.faceInstanceVBO);
        glBindVertexArray(renderer.faceVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.faceVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(faceVerts), faceVerts, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.faceInstanceVBO);
        glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, position));
        glEnableVertexAttribArray(4); glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, color));
        glEnableVertexAttribArray(5); glVertexAttribIPointer(5, 1, GL_INT, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, tileIndex));
        glVertexAttribDivisor(3, 1); glVertexAttribDivisor(4, 1); glVertexAttribDivisor(5, 1);
        glEnableVertexAttribArray(6); glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, alpha));
        glVertexAttribDivisor(6, 1);
        glEnableVertexAttribArray(7); glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, ao));
        glVertexAttribDivisor(7, 1);
        glEnableVertexAttribArray(8); glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, scale));
        glVertexAttribDivisor(8, 1);
        glEnableVertexAttribArray(9); glVertexAttribPointer(9, 2, GL_FLOAT, GL_FALSE, sizeof(FaceInstanceRenderData), (void*)offsetof(FaceInstanceRenderData, uvScale));
        glVertexAttribDivisor(9, 1);

        // Godray quad
        float quadVerts[] = { -1,-1,  1,-1,  -1,1,  -1,1,  1,-1,  1,1 };
        glGenVertexArrays(1, &renderer.godrayQuadVAO);
        glGenBuffers(1, &renderer.godrayQuadVBO);
        glBindVertexArray(renderer.godrayQuadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.godrayQuadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        // Godray FBOs
        renderer.godrayWidth = baseSystem.app ? baseSystem.app->windowWidth / renderer.godrayDownsample : 960;
        renderer.godrayHeight = baseSystem.app ? baseSystem.app->windowHeight / renderer.godrayDownsample : 540;

        auto setupFBO = [](GLuint& fbo, GLuint& tex, int w, int h){
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        };

        setupFBO(renderer.godrayOcclusionFBO, renderer.godrayOcclusionTex, renderer.godrayWidth, renderer.godrayHeight);
        setupFBO(renderer.godrayBlurFBO, renderer.godrayBlurTex, renderer.godrayWidth, renderer.godrayHeight);

        renderer.selectionShader = std::make_unique<Shader>(world.shaders["SELECTION_VERTEX_SHADER"].c_str(), world.shaders["SELECTION_FRAGMENT_SHADER"].c_str());
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
        glGenVertexArrays(1, &renderer.selectionVAO);
        glGenBuffers(1, &renderer.selectionVBO);
        glBindVertexArray(renderer.selectionVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.selectionVBO);
        glBufferData(GL_ARRAY_BUFFER, selectionVertices.size() * sizeof(float), selectionVertices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        renderer.hudShader = std::make_unique<Shader>(world.shaders["HUD_VERTEX_SHADER"].c_str(), world.shaders["HUD_FRAGMENT_SHADER"].c_str());
        renderer.colorEmotionShader = std::make_unique<Shader>(
            world.shaders["COLOR_EMOTION_VERTEX_SHADER"].c_str(),
            world.shaders["COLOR_EMOTION_FRAGMENT_SHADER"].c_str()
        );
        renderer.crosshairShader = std::make_unique<Shader>(world.shaders["CROSSHAIR_VERTEX_SHADER"].c_str(), world.shaders["CROSSHAIR_FRAGMENT_SHADER"].c_str());
        float hudVertices[] = {
            0.035f, -0.05f, 0.0f, 0.0f,
            0.08f,  -0.05f, 1.0f, 0.0f,
            0.08f,   0.05f, 1.0f, 1.0f,
            0.035f, -0.05f, 0.0f, 0.0f,
            0.08f,   0.05f, 1.0f, 1.0f,
            0.035f,  0.05f, 0.0f, 1.0f
        };
        glGenVertexArrays(1, &renderer.hudVAO);
        glGenBuffers(1, &renderer.hudVBO);
        glBindVertexArray(renderer.hudVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.hudVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(hudVertices), hudVertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        float colorEmotionVertices[] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
            -1.0f,  1.0f, 0.0f, 1.0f
        };
        glGenVertexArrays(1, &renderer.colorEmotionVAO);
        glGenBuffers(1, &renderer.colorEmotionVBO);
        glBindVertexArray(renderer.colorEmotionVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.colorEmotionVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(colorEmotionVertices), colorEmotionVertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        float chLen = 0.02f;
        float chLenH = 0.016f;
        float crosshairVertices[] = {
            0.0f, -chLen, 1.0f, 1.0f, 1.0f,
            0.0f,  chLen, 1.0f, 1.0f, 1.0f,
            -chLenH, 0.0f, 1.0f, 1.0f, 1.0f,
             chLenH, 0.0f, 1.0f, 1.0f, 1.0f
        };
        glGenVertexArrays(1, &renderer.crosshairVAO);
        glGenBuffers(1, &renderer.crosshairVBO);
        glBindVertexArray(renderer.crosshairVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.crosshairVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(crosshairVertices), crosshairVertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        renderer.crosshairVertexCount = 4;

        glGenVertexArrays(1, &renderer.leyLineDebugVAO);
        glGenBuffers(1, &renderer.leyLineDebugVBO);
        glBindVertexArray(renderer.leyLineDebugVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.leyLineDebugVBO);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        renderer.leyLineDebugVertexCount = 0;
    }


    void CleanupRenderer(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.renderer) return;
        VoxelMeshingSystemLogic::StopGreedyAsync();
        RendererContext& renderer = *baseSystem.renderer;
        if (baseSystem.voxelGreedy) {
            for (auto& [_, buffers] : baseSystem.voxelGreedy->renderBuffers) {
                DestroyVoxelGreedyRenderBuffers(buffers);
            }
            baseSystem.voxelGreedy->renderBuffers.clear();
            baseSystem.voxelGreedy->renderBuffersDirty.clear();
        }
        int behaviorCount = static_cast<int>(RenderBehavior::COUNT);
        glDeleteVertexArrays(behaviorCount, renderer.behaviorVAOs.data());
        glDeleteBuffers(behaviorCount, renderer.behaviorInstanceVBOs.data());
        glDeleteVertexArrays(1, &renderer.skyboxVAO);
        glDeleteVertexArrays(1, &renderer.sunMoonVAO);
        glDeleteVertexArrays(1, &renderer.starVAO);
        glDeleteBuffers(1, &renderer.cubeVBO);
        glDeleteBuffers(1, &renderer.skyboxVBO);
        glDeleteBuffers(1, &renderer.sunMoonVBO);
        glDeleteBuffers(1, &renderer.starVBO);
        if (renderer.selectionVAO) glDeleteVertexArrays(1, &renderer.selectionVAO);
        if (renderer.selectionVBO) glDeleteBuffers(1, &renderer.selectionVBO);
        if (renderer.hudVAO) glDeleteVertexArrays(1, &renderer.hudVAO);
        if (renderer.hudVBO) glDeleteBuffers(1, &renderer.hudVBO);
        if (renderer.colorEmotionVAO) glDeleteVertexArrays(1, &renderer.colorEmotionVAO);
        if (renderer.colorEmotionVBO) glDeleteBuffers(1, &renderer.colorEmotionVBO);
        if (renderer.crosshairVAO) glDeleteVertexArrays(1, &renderer.crosshairVAO);
        if (renderer.crosshairVBO) glDeleteBuffers(1, &renderer.crosshairVBO);
        if (renderer.uiVAO) glDeleteVertexArrays(1, &renderer.uiVAO);
        if (renderer.uiVBO) glDeleteBuffers(1, &renderer.uiVBO);
        if (renderer.uiButtonVAO) glDeleteVertexArrays(1, &renderer.uiButtonVAO);
        if (renderer.uiButtonVBO) glDeleteBuffers(1, &renderer.uiButtonVBO);
        if (renderer.fontVAO) glDeleteVertexArrays(1, &renderer.fontVAO);
        if (renderer.fontVBO) glDeleteBuffers(1, &renderer.fontVBO);
        if (renderer.audioRayVAO) glDeleteVertexArrays(1, &renderer.audioRayVAO);
        if (renderer.audioRayVBO) glDeleteBuffers(1, &renderer.audioRayVBO);
        if (renderer.leyLineDebugVAO) glDeleteVertexArrays(1, &renderer.leyLineDebugVAO);
        if (renderer.leyLineDebugVBO) glDeleteBuffers(1, &renderer.leyLineDebugVBO);
        if (renderer.fishingVAO) glDeleteVertexArrays(1, &renderer.fishingVAO);
        if (renderer.fishingVBO) glDeleteBuffers(1, &renderer.fishingVBO);
        if (renderer.gemVAO) glDeleteVertexArrays(1, &renderer.gemVAO);
        if (renderer.gemVBO) glDeleteBuffers(1, &renderer.gemVBO);
        if (renderer.audioRayVoxelVAO) glDeleteVertexArrays(1, &renderer.audioRayVoxelVAO);
        if (renderer.audioRayVoxelInstanceVBO) glDeleteBuffers(1, &renderer.audioRayVoxelInstanceVBO);
        if (renderer.faceVAO) glDeleteVertexArrays(1, &renderer.faceVAO);
        if (renderer.faceVBO) glDeleteBuffers(1, &renderer.faceVBO);
        if (renderer.faceInstanceVBO) glDeleteBuffers(1, &renderer.faceInstanceVBO);
        if (renderer.atlasTexture) glDeleteTextures(1, &renderer.atlasTexture);
        for (GLuint& tex : renderer.grassTextures) {
            if (tex) glDeleteTextures(1, &tex);
            tex = 0;
        }
        renderer.grassTextureCount = 0;
        for (GLuint& tex : renderer.shortGrassTextures) {
            if (tex) glDeleteTextures(1, &tex);
            tex = 0;
        }
        renderer.shortGrassTextureCount = 0;
        for (GLuint& tex : renderer.oreTextures) {
            if (tex) glDeleteTextures(1, &tex);
            tex = 0;
        }
        renderer.oreTextureCount = 0;
        for (GLuint& tex : renderer.terrainTextures) {
            if (tex) glDeleteTextures(1, &tex);
            tex = 0;
        }
        renderer.terrainTextureCount = 0;
        if (renderer.waterOverlayTexture) {
            glDeleteTextures(1, &renderer.waterOverlayTexture);
            renderer.waterOverlayTexture = 0;
        }
    }
}
