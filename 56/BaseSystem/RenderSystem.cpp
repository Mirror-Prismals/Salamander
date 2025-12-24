#pragma once

// Forward declare the function from the new system.
namespace SkyboxSystemLogic {
    void getCurrentSkyColors(float dayFraction, const std::vector<SkyColorKey>& skyKeys, glm::vec3& top, glm::vec3& bottom);
}

namespace RenderSystemLogic {

    namespace {
        RenderBehavior BehaviorForPrototype(const Entity& proto) {
            if (proto.name == "Branch") return RenderBehavior::STATIC_BRANCH;
            if (proto.name == "Water") return RenderBehavior::ANIMATED_WATER;
            if (proto.name == "TransparentWave") return RenderBehavior::ANIMATED_TRANSPARENT_WAVE;
            if (proto.hasWireframe && proto.isAnimated) return RenderBehavior::ANIMATED_WIREFRAME;
            return RenderBehavior::STATIC_DEFAULT;
        }

        bool ShouldRenderChunkBlock(const Entity& proto, bool faceCullingInitialized) {
            if (!proto.isRenderable || !proto.isBlock || !proto.isChunkable) return false;
            bool culledByFaces = faceCullingInitialized && (proto.isSolid || proto.name == "Water") && proto.name != "TransparentWave";
            return !culledByFaces;
        }

        float ChunkCenterDistance(const ChunkKey& key, const glm::vec3& cameraPos, const glm::ivec3& chunkSize) {
            glm::vec3 minCorner = glm::vec3(key.chunkIndex * chunkSize);
            glm::vec3 center = minCorner + (glm::vec3(chunkSize) * 0.5f);
            return glm::length(center - cameraPos);
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

        void BuildChunkRenderBuffers(BaseSystem& baseSystem,
                                     std::vector<Entity>& prototypes,
                                     const ChunkKey& chunkKey,
                                     bool faceCullingInitialized) {
            if (!baseSystem.chunk || !baseSystem.renderer) return;
            ChunkContext& chunkCtx = *baseSystem.chunk;
            RendererContext& renderer = *baseSystem.renderer;
            auto chunkIt = chunkCtx.chunks.find(chunkKey);
            if (chunkIt == chunkCtx.chunks.end()) return;
            const ChunkData& chunk = chunkIt->second;
            ChunkRenderBuffers& buffers = chunkCtx.renderBuffers[chunkKey];
            const int behaviorCount = static_cast<int>(RenderBehavior::COUNT);

            std::array<std::vector<InstanceData>, static_cast<int>(RenderBehavior::COUNT)> behaviorData;
            std::vector<BranchInstanceData> branchData;
            buffers.counts.fill(0);

            for (size_t i = 0; i < chunk.positions.size(); ++i) {
                if (i >= chunk.prototypeIDs.size()) continue;
                int protoID = chunk.prototypeIDs[i];
                if (protoID < 0 || protoID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[protoID];
                if (!ShouldRenderChunkBlock(proto, faceCullingInitialized)) continue;
                RenderBehavior behavior = BehaviorForPrototype(proto);
                if (behavior == RenderBehavior::STATIC_BRANCH) {
                    BranchInstanceData inst;
                    inst.position = chunk.positions[i];
                    inst.rotation = (i < chunk.rotations.size()) ? chunk.rotations[i] : 0.0f;
                    inst.color = (i < chunk.colors.size()) ? chunk.colors[i] : glm::vec3(1.0f);
                    branchData.push_back(inst);
                } else {
                    InstanceData inst;
                    inst.position = chunk.positions[i];
                    inst.color = (i < chunk.colors.size()) ? chunk.colors[i] : glm::vec3(1.0f);
                    behaviorData[static_cast<int>(behavior)].push_back(inst);
                }
            }

            for (int i = 0; i < behaviorCount; ++i) {
                RenderBehavior behavior = static_cast<RenderBehavior>(i);
                bool isBranch = behavior == RenderBehavior::STATIC_BRANCH;
                int count = isBranch ? static_cast<int>(branchData.size()) : static_cast<int>(behaviorData[i].size());
                buffers.counts[i] = count;
                if (count == 0) continue;

                if (buffers.vaos[i] == 0) glGenVertexArrays(1, &buffers.vaos[i]);
                if (buffers.instanceVBOs[i] == 0) glGenBuffers(1, &buffers.instanceVBOs[i]);

                glBindVertexArray(buffers.vaos[i]);

                // Shared cube geometry
                glBindBuffer(GL_ARRAY_BUFFER, renderer.cubeVBO);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);

                glBindBuffer(GL_ARRAY_BUFFER, buffers.instanceVBOs[i]);
                if (isBranch) {
                    glBufferData(GL_ARRAY_BUFFER, branchData.size() * sizeof(BranchInstanceData), branchData.data(), GL_STATIC_DRAW);
                    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, position));
                    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, rotation));
                    glEnableVertexAttribArray(5); glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, color));
                    glVertexAttribDivisor(3, 1); glVertexAttribDivisor(4, 1); glVertexAttribDivisor(5, 1);
                } else {
                    glBufferData(GL_ARRAY_BUFFER, behaviorData[i].size() * sizeof(InstanceData), behaviorData[i].data(), GL_STATIC_DRAW);
                    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, position));
                    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, color));
                    glVertexAttribDivisor(3, 1); glVertexAttribDivisor(4, 1);
                }
            }

            glBindVertexArray(0);
            buffers.builtWithFaceCulling = faceCullingInitialized;
        }
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
    }

    void RenderScene(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.player || !baseSystem.level) return;
        PlayerContext& player = *baseSystem.player;
        WorldContext& world = *baseSystem.world;
        RendererContext& renderer = *baseSystem.renderer;
        LevelContext& level = *baseSystem.level;

        float time = static_cast<float>(glfwGetTime());
        glm::mat4 view = player.viewMatrix;
        glm::mat4 projection = player.projectionMatrix;
        glm::vec3 playerPos = player.cameraPosition;
        glm::vec3 cameraForward;
        cameraForward.x = cos(glm::radians(player.cameraYaw)) * cos(glm::radians(player.cameraPitch));
        cameraForward.y = sin(glm::radians(player.cameraPitch));
        cameraForward.z = sin(glm::radians(player.cameraYaw)) * cos(glm::radians(player.cameraPitch));
        cameraForward = glm::normalize(cameraForward);
        
        time_t ct; std::time(&ct); tm lt;
        #ifdef _WIN32
        localtime_s(&lt, &ct);
        #else
        localtime_r(&ct, &lt);
        #endif
        float dayFraction = (lt.tm_hour*3600+lt.tm_min*60+lt.tm_sec)/86400.0f;
        std::vector<glm::vec3> starPositions;
        std::vector<std::vector<InstanceData>> behaviorInstances(static_cast<int>(RenderBehavior::COUNT));
        std::vector<BranchInstanceData> branchInstances;
        std::array<std::vector<FaceInstanceRenderData>, 6> faceInstances;

        bool useChunkRendering = baseSystem.chunk && baseSystem.chunk->initialized;

        for (const auto& worldProto : level.worlds) {
            for (const auto& instance : worldProto.instances) {
                if (instance.prototypeID < 0 || instance.prototypeID >= static_cast<int>(prototypes.size())) continue;
                const Entity& proto = prototypes[instance.prototypeID];
                if (useChunkRendering && proto.isChunkable) continue;
                if (proto.isStar) {
                    starPositions.push_back(instance.position);
                }
                if (proto.name == "Face_PosX") { faceInstances[0].push_back({instance.position, instance.color, -1, 1.0f}); continue; }
                if (proto.name == "Face_NegX") { faceInstances[1].push_back({instance.position, instance.color, -1, 1.0f}); continue; }
                if (proto.name == "Face_PosY") { faceInstances[2].push_back({instance.position, instance.color, -1, 1.0f}); continue; }
                if (proto.name == "Face_NegY") { faceInstances[3].push_back({instance.position, instance.color, -1, 1.0f}); continue; }
                if (proto.name == "Face_PosZ") { faceInstances[4].push_back({instance.position, instance.color, -1, 1.0f}); continue; }
                if (proto.name == "Face_NegZ") { faceInstances[5].push_back({instance.position, instance.color, -1, 1.0f}); continue; }
                if (proto.isRenderable && proto.isBlock) {
                    RenderBehavior behavior = RenderBehavior::STATIC_DEFAULT;
                    if (proto.name == "Branch") behavior = RenderBehavior::STATIC_BRANCH;
                    else if (proto.name == "Water") behavior = RenderBehavior::ANIMATED_WATER;
                    else if (proto.name == "TransparentWave") behavior = RenderBehavior::ANIMATED_TRANSPARENT_WAVE;
                    else if (proto.hasWireframe && proto.isAnimated) behavior = RenderBehavior::ANIMATED_WIREFRAME;
                    if (behavior == RenderBehavior::STATIC_BRANCH) branchInstances.push_back({instance.position, instance.rotation, instance.color});
                    else behaviorInstances[static_cast<int>(behavior)].push_back({instance.position, instance.color});
                }
            }
        }

        float maxChunkDist = 0.0f;
        auto shouldRenderChunk = [&](const ChunkKey& chunkKey) {
            if (!useChunkRendering) return false;
            ChunkContext& chunkCtx = *baseSystem.chunk;
            if (chunkCtx.renderDistanceChunks <= 0) return true;
            return ChunkCenterDistance(chunkKey, playerPos, chunkCtx.chunkSize) <= maxChunkDist;
        };

        if (useChunkRendering) {
            ChunkContext& chunkCtx = *baseSystem.chunk;
            maxChunkDist = static_cast<float>(chunkCtx.renderDistanceChunks) * static_cast<float>(std::max(chunkCtx.chunkSize.x, std::max(chunkCtx.chunkSize.y, chunkCtx.chunkSize.z)));
            bool faceInitialized = baseSystem.face && baseSystem.face->initialized;

            // Drop render buffers for chunks that were removed.
            std::vector<ChunkKey> staleBuffers;
            for (const auto& [idx, _] : chunkCtx.renderBuffers) {
                if (chunkCtx.chunks.find(idx) == chunkCtx.chunks.end()) staleBuffers.push_back(idx);
            }
            for (const auto& idx : staleBuffers) {
                DestroyChunkRenderBuffers(chunkCtx.renderBuffers[idx]);
                chunkCtx.renderBuffers.erase(idx);
            }

            // If face culling state changed, rebuild all chunk buffers so we don't double-draw culled blocks.
            if (chunkCtx.renderBuffersFaceState != faceInitialized) {
                chunkCtx.renderBuffersFaceState = faceInitialized;
                chunkCtx.renderBuffersDirtyAll = true;
            }

            if (chunkCtx.renderBuffersDirtyAll) {
                chunkCtx.renderBuffersDirtyAll = false;
                chunkCtx.renderBuffersDirty.clear();
                for (const auto& [idx, _] : chunkCtx.chunks) {
                    chunkCtx.renderBuffersDirty.insert(idx);
                }
            }

            if (!chunkCtx.renderBuffersDirty.empty()) {
                for (const auto& idx : chunkCtx.renderBuffersDirty) {
                    BuildChunkRenderBuffers(baseSystem, prototypes, idx, faceInitialized);
                }
                chunkCtx.renderBuffersDirty.clear();
            }
        }
        
        glm::vec3 lightDir;
        SkyboxSystemLogic::RenderSkyAndCelestials(baseSystem, prototypes, starPositions, time, dayFraction, view, projection, playerPos, lightDir);
        bool auroraEnabled = true;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("AuroraSystem");
            if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second)) {
                auroraEnabled = std::get<bool>(it->second);
            }
        }
        if (auroraEnabled) {
            AuroraSystemLogic::RenderAuroras(baseSystem, time, view, projection);
        }
        CloudSystemLogic::RenderClouds(baseSystem, lightDir, time);

        renderer.blockShader->use();
        renderer.blockShader->setMat4("view", view);
        renderer.blockShader->setMat4("projection", projection);
        renderer.blockShader->setVec3("cameraPos", playerPos);
        renderer.blockShader->setFloat("time", time);
        renderer.blockShader->setVec3("lightDir",lightDir);
        renderer.blockShader->setVec3("ambientLight",glm::vec3(0.4f));
        renderer.blockShader->setVec3("diffuseLight",glm::vec3(0.6f));
        renderer.blockShader->setMat4("model", glm::mat4(1.0f));
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        for (int i = 0; i < static_cast<int>(RenderBehavior::COUNT); ++i) {
            RenderBehavior currentBehavior = static_cast<RenderBehavior>(i);
            bool translucent = (currentBehavior == RenderBehavior::ANIMATED_WATER || currentBehavior == RenderBehavior::ANIMATED_TRANSPARENT_WAVE);
            if (translucent) {
                // Let translucent passes read depth but avoid writing it so surfaces beneath stay visible.
                glDepthMask(GL_FALSE);
            }
            if (useChunkRendering) {
                ChunkContext& chunkCtx = *baseSystem.chunk;
                for (const auto& [chunkKey, chunk] : chunkCtx.chunks) {
                    (void)chunk;
                    if (!shouldRenderChunk(chunkKey)) continue;
                    auto it = chunkCtx.renderBuffers.find(chunkKey);
                    if (it == chunkCtx.renderBuffers.end()) continue;
                    const ChunkRenderBuffers& buffers = it->second;
                    int count = (i == static_cast<int>(RenderBehavior::STATIC_BRANCH))
                        ? buffers.counts[i]
                        : buffers.counts[i];
                    if (count <= 0) continue;
                    renderer.blockShader->setInt("behaviorType", i);
                    if (buffers.vaos[i] == 0) continue;
                    glBindVertexArray(buffers.vaos[i]);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, count);
                }
            }
            if (currentBehavior == RenderBehavior::STATIC_BRANCH) {
                if (!branchInstances.empty()) {
                    renderer.blockShader->setInt("behaviorType", i);
                    glBindVertexArray(renderer.behaviorVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, renderer.behaviorInstanceVBOs[i]);
                    glBufferData(GL_ARRAY_BUFFER, branchInstances.size() * sizeof(BranchInstanceData), branchInstances.data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, branchInstances.size());
                }
            } else {
                if (!behaviorInstances[i].empty()) {
                    renderer.blockShader->setInt("behaviorType", i);
                    glBindVertexArray(renderer.behaviorVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, renderer.behaviorInstanceVBOs[i]);
                    glBufferData(GL_ARRAY_BUFFER, behaviorInstances[i].size() * sizeof(InstanceData), behaviorInstances[i].data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, behaviorInstances[i].size());
                }
            }
            if (translucent) {
                glDepthMask(GL_TRUE);
            }
        }

        auto bindFaceTextureUniforms = [&](Shader& shader){
            shader.setInt("atlasEnabled", (renderer.atlasTexture != 0 && renderer.atlasTilesPerRow > 0 && renderer.atlasTilesPerCol > 0) ? 1 : 0);
            shader.setVec2("atlasTileSize", glm::vec2(renderer.atlasTileSize));
            shader.setVec2("atlasTextureSize", glm::vec2(renderer.atlasTextureSize));
            shader.setInt("tilesPerRow", renderer.atlasTilesPerRow);
            shader.setInt("tilesPerCol", renderer.atlasTilesPerCol);
            shader.setInt("atlasTexture", 0);
            if (renderer.atlasTexture != 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, renderer.atlasTexture);
            }
        };

        auto drawFaceBatches = [&](const std::array<std::vector<FaceInstanceRenderData>, 6>& batches, bool depthWrite){
            if (!renderer.faceShader || !renderer.faceVAO) return;
            if (!depthWrite) glDepthMask(GL_FALSE);
            glEnable(GL_CULL_FACE);
            glFrontFace(GL_CCW);
            glCullFace(GL_BACK);

            renderer.faceShader->use();
            renderer.faceShader->setMat4("view", view);
            renderer.faceShader->setMat4("projection", projection);
            renderer.faceShader->setMat4("model", glm::mat4(1.0f));
            renderer.faceShader->setVec3("cameraPos", playerPos);
            renderer.faceShader->setVec3("lightDir", lightDir);
            renderer.faceShader->setVec3("ambientLight", glm::vec3(0.4f));
            renderer.faceShader->setVec3("diffuseLight", glm::vec3(0.6f));
            renderer.faceShader->setInt("faceType", 0);
            bindFaceTextureUniforms(*renderer.faceShader);
            glBindVertexArray(renderer.faceVAO);
            for (int faceType = 0; faceType < 6; ++faceType) {
                const auto& instances = batches[faceType];
                if (instances.empty()) continue;
                renderer.faceShader->setInt("faceType", faceType);
                glBindBuffer(GL_ARRAY_BUFFER, renderer.faceInstanceVBO);
                glBufferData(GL_ARRAY_BUFFER, instances.size() * sizeof(FaceInstanceRenderData), instances.data(), GL_DYNAMIC_DRAW);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, instances.size());
            }

            glDisable(GL_CULL_FACE);
            if (!depthWrite) glDepthMask(GL_TRUE);
        };

        if (renderer.faceShader && renderer.faceVAO) {
            std::array<std::vector<FaceInstanceRenderData>, 6> faceInstancesOpaque;
            std::array<std::vector<FaceInstanceRenderData>, 6> faceInstancesAlpha;
            for (int f = 0; f < 6; ++f) {
                for (const auto& inst : faceInstances[f]) {
                    if (inst.alpha < 0.999f) faceInstancesAlpha[f].push_back(inst);
                    else faceInstancesOpaque[f].push_back(inst);
                }
            }
            drawFaceBatches(faceInstancesOpaque, true);
            drawFaceBatches(faceInstancesAlpha, false);
        }

        if (baseSystem.face && baseSystem.face->initialized && renderer.faceShader && renderer.faceVAO) {
            FaceContext& faceCtx = *baseSystem.face;
            std::array<std::vector<FaceInstanceRenderData>, 6> faceChunkInstances;
            for (const auto& [chunkKey, faceChunk] : faceCtx.faces) {
                if (!shouldRenderChunk(chunkKey)) continue;
                (void)chunkKey;
                for (size_t i = 0; i < faceChunk.positions.size(); ++i) {
                    int faceType = (i < faceChunk.faceTypes.size()) ? faceChunk.faceTypes[i] : -1;
                    if (faceType < 0 || faceType >= 6) continue;
                    glm::vec3 color = (i < faceChunk.colors.size()) ? faceChunk.colors[i] : glm::vec3(1.0f);
                    int tileIndex = (i < faceChunk.tileIndices.size()) ? faceChunk.tileIndices[i] : -1;
                    float alpha = (i < faceChunk.alphas.size()) ? faceChunk.alphas[i] : 1.0f;
                    faceChunkInstances[faceType].push_back({faceChunk.positions[i], color, tileIndex, alpha});
                }
            }

            std::array<std::vector<FaceInstanceRenderData>, 6> faceChunkOpaque;
            std::array<std::vector<FaceInstanceRenderData>, 6> faceChunkAlpha;
            for (int f = 0; f < 6; ++f) {
                for (const auto& inst : faceChunkInstances[f]) {
                    if (inst.alpha < 0.999f) faceChunkAlpha[f].push_back(inst);
                    else faceChunkOpaque[f].push_back(inst);
                }
            }
            drawFaceBatches(faceChunkOpaque, true);
            drawFaceBatches(faceChunkAlpha, false);
        }

        if (player.isHoldingBlock && player.heldPrototypeID >= 0) {
            InstanceData heldInstance;
            heldInstance.position = player.cameraPosition + cameraForward * 0.8f + glm::vec3(0.0f, -0.2f, 0.0f);
            heldInstance.color = player.heldBlockColor;
            int behaviorIndex = static_cast<int>(RenderBehavior::STATIC_DEFAULT);
            renderer.blockShader->setInt("behaviorType", behaviorIndex);
            glBindVertexArray(renderer.behaviorVAOs[behaviorIndex]);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.behaviorInstanceVBOs[behaviorIndex]);
            glBufferData(GL_ARRAY_BUFFER, sizeof(InstanceData), &heldInstance, GL_DYNAMIC_DRAW);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 36, 1);
        }

        if (player.hasBlockTarget && renderer.selectionShader && renderer.selectionVAO && renderer.selectionVertexCount > 0) {
            renderer.selectionShader->use();
            glm::mat4 selectionModel = glm::translate(glm::mat4(1.0f), player.targetedBlockPosition);
            selectionModel = glm::scale(selectionModel, glm::vec3(1.02f));
            renderer.selectionShader->setMat4("model", selectionModel);
            renderer.selectionShader->setMat4("view", view);
            renderer.selectionShader->setMat4("projection", projection);
            renderer.selectionShader->setVec3("cameraPos", playerPos);
            renderer.selectionShader->setFloat("time", time);
            glBindVertexArray(renderer.selectionVAO);
            glDrawArrays(GL_LINES, 0, renderer.selectionVertexCount);
        }

        if (renderer.audioRayVoxelShader && renderer.audioRayVoxelVAO && renderer.audioRayVoxelCount > 0) {
            glEnable(GL_BLEND);
            renderer.audioRayVoxelShader->use();
            renderer.audioRayVoxelShader->setMat4("view", view);
            renderer.audioRayVoxelShader->setMat4("projection", projection);
            renderer.audioRayVoxelShader->setFloat("baseAlpha", 0.22f);
            glBindVertexArray(renderer.audioRayVoxelVAO);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 36, renderer.audioRayVoxelCount);
        }

        if (renderer.crosshairShader && renderer.crosshairVAO && renderer.crosshairVertexCount > 0) {
            glDisable(GL_DEPTH_TEST);
            renderer.crosshairShader->use();
            glBindVertexArray(renderer.crosshairVAO);
            glLineWidth(1.0f);
            glDrawArrays(GL_LINES, 0, renderer.crosshairVertexCount);
            glLineWidth(1.0f);
            glEnable(GL_DEPTH_TEST);
        }

        if (baseSystem.hud && renderer.hudShader && renderer.hudVAO) {
            HUDContext& hud = *baseSystem.hud;
            if (hud.showCharge) {
                glDisable(GL_DEPTH_TEST);
                renderer.hudShader->use();
                renderer.hudShader->setFloat("fillAmount", glm::clamp(hud.chargeValue, 0.0f, 1.0f));
                renderer.hudShader->setInt("ready", hud.chargeReady ? 1 : 0);
                 renderer.hudShader->setInt("buildMode", hud.buildModeActive ? 1 : 0);
                 renderer.hudShader->setVec3("previewColor", hud.buildPreviewColor);
                 renderer.hudShader->setInt("channelIndex", hud.buildChannel);
                glBindVertexArray(renderer.hudVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glEnable(GL_DEPTH_TEST);
            }
        }
    }

    void CleanupRenderer(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.renderer) return;
        RendererContext& renderer = *baseSystem.renderer;
        if (baseSystem.chunk) {
            for (auto& [_, buffers] : baseSystem.chunk->renderBuffers) {
                DestroyChunkRenderBuffers(buffers);
            }
            baseSystem.chunk->renderBuffers.clear();
            baseSystem.chunk->renderBuffersDirty.clear();
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
        if (renderer.crosshairVAO) glDeleteVertexArrays(1, &renderer.crosshairVAO);
        if (renderer.crosshairVBO) glDeleteBuffers(1, &renderer.crosshairVBO);
        if (renderer.uiVAO) glDeleteVertexArrays(1, &renderer.uiVAO);
        if (renderer.uiVBO) glDeleteBuffers(1, &renderer.uiVBO);
        if (renderer.uiButtonVAO) glDeleteVertexArrays(1, &renderer.uiButtonVAO);
        if (renderer.uiButtonVBO) glDeleteBuffers(1, &renderer.uiButtonVBO);
        if (renderer.audioRayVAO) glDeleteVertexArrays(1, &renderer.audioRayVAO);
        if (renderer.audioRayVBO) glDeleteBuffers(1, &renderer.audioRayVBO);
        if (renderer.audioRayVoxelVAO) glDeleteVertexArrays(1, &renderer.audioRayVoxelVAO);
        if (renderer.audioRayVoxelInstanceVBO) glDeleteBuffers(1, &renderer.audioRayVoxelInstanceVBO);
        if (renderer.faceVAO) glDeleteVertexArrays(1, &renderer.faceVAO);
        if (renderer.faceVBO) glDeleteBuffers(1, &renderer.faceVBO);
        if (renderer.faceInstanceVBO) glDeleteBuffers(1, &renderer.faceInstanceVBO);
        if (renderer.atlasTexture) glDeleteTextures(1, &renderer.atlasTexture);
    }
}
