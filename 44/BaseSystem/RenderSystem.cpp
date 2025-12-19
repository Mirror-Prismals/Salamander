#pragma once

// Forward declare the function from the new system.
namespace SkyboxSystemLogic {
    void getCurrentSkyColors(float dayFraction, const std::vector<SkyColorKey>& skyKeys, glm::vec3& top, glm::vec3& bottom);
}

namespace RenderSystemLogic {
    
    void InitializeRenderer(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.renderer || !baseSystem.world) { std::cerr << "ERROR: RenderSystem cannot init without RendererContext or WorldContext." << std::endl; return; }
        WorldContext& world = *baseSystem.world;
        RendererContext& renderer = *baseSystem.renderer;
        renderer.blockShader = std::make_unique<Shader>(world.shaders["BLOCK_VERTEX_SHADER"].c_str(), world.shaders["BLOCK_FRAGMENT_SHADER"].c_str());
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

        for (const auto& worldProto : level.worlds) {
            for (const auto& instance : worldProto.instances) {
                const Entity& proto = prototypes[instance.prototypeID];
                if (proto.isStar) {
                    starPositions.push_back(instance.position);
                }
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
        
        glm::vec3 lightDir;
        SkyboxSystemLogic::RenderSkyAndCelestials(baseSystem, prototypes, starPositions, time, dayFraction, view, projection, playerPos, lightDir);
        AuroraSystemLogic::RenderAuroras(baseSystem, time, view, projection);
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

        for (int i = 0; i < static_cast<int>(RenderBehavior::COUNT); ++i) {
            RenderBehavior currentBehavior = static_cast<RenderBehavior>(i);
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
    }
}
