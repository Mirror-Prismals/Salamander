#pragma once

void Host::run() {
    init();
    mainLoop();
    cleanup();
}

void Host::registerSystemFunctions() {
    functionRegistry["LoadProcedureAssets"] = HostLogic::LoadProcedureAssets;
    functionRegistry["InitializeAudio"] = AudioSystemLogic::InitializeAudio;
    functionRegistry["CleanupAudio"] = AudioSystemLogic::CleanupAudio;
    functionRegistry["ProcessRayTracedAudio"] = RayTracedAudioSystemLogic::ProcessRayTracedAudio;
    functionRegistry["ProcessPinkNoiseAudicle"] = PinkNoiseSystemLogic::ProcessPinkNoiseAudicle;
    functionRegistry["ProcessAudicles"] = AudicleSystemLogic::ProcessAudicles;
    functionRegistry["UpdateCameraMatrices"] = CameraSystemLogic::UpdateCameraMatrices;
    functionRegistry["ProcessPlayerMovement"] = PlayerControlSystemLogic::ProcessPlayerMovement;
    functionRegistry["UpdateCameraRotationFromMouse"] = PlayerControlSystemLogic::UpdateCameraRotationFromMouse;
    functionRegistry["InitializeRenderer"] = RenderSystemLogic::InitializeRenderer;
    functionRegistry["RenderScene"] = RenderSystemLogic::RenderScene;
    functionRegistry["CleanupRenderer"] = RenderSystemLogic::CleanupRenderer;
}

void Host::init() {
    loadRegistry();
    if (programStatus != "Installed") {
        std::cerr << "FATAL: Program is not 'Installed' in registry.json. Halting." << std::endl; return;
    }

    baseSystem.app = std::make_unique<AppContext>();
    baseSystem.world = std::make_unique<WorldContext>();
    baseSystem.player = std::make_unique<PlayerContext>();
    baseSystem.instance = std::make_unique<InstanceContext>();
    baseSystem.renderer = std::make_unique<RendererContext>();
    baseSystem.audio = std::make_unique<AudioContext>();
    baseSystem.rayTracedAudio = std::make_unique<RayTracedAudioContext>();
    
    registerSystemFunctions();
    loadSystems();
    HostLogic::LoadProcedureAssets(baseSystem, entityPrototypes, 0.0f, nullptr);

    if (!glfwInit()) { std::cerr << "Failed to initialize GLFW\n"; exit(-1); }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    #ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #endif

    window = glfwCreateWindow(baseSystem.app->windowWidth, baseSystem.app->windowHeight, "Cardinal EDS", NULL, NULL);
    if (!window) { std::cout << "Failed to create GLFW window\n"; glfwTerminate(); exit(-1); }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::cout << "Failed to initialize GLAD\n"; exit(-1); }

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int width, int height){ glViewport(0, 0, width, height); });
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y){ static_cast<Host*>(glfwGetWindowUserPointer(w))->processMouseInput(x, y); });
    
    for(const auto& step : initFunctions) {
        if(functionRegistry.count(step.name)) {
            if (checkDependencies(step.dependencies)) {
                functionRegistry[step.name](baseSystem, entityPrototypes, 0.0f, window);
            } else { std::cerr << "ERROR: Cannot run init step '" << step.name << "' due to missing dependencies. Skipping." << std::endl; programStatus = "Dependency Failure"; }
        }
    }
    
    const std::vector<std::string> entityFiles = { 
        "Entities/World.json", "Entities/Star.json", "Entities/DebugWorldGenerator.json", 
        "Entities/Block.json", "Entities/Water.json", "Entities/Branch.json", 
        "Entities/WireframeBlock.json", "Entities/TransparentWave.json",
        "Entities/AudioVisualizer.json"
    };
    for (const auto& filePath : entityFiles) {
        std::ifstream f(filePath);
        if (f.is_open()) { 
            Entity newProto = nlohmann::json::parse(f).get<Entity>(); 
            newProto.prototypeID = this->entityPrototypes.size(); 
            this->entityPrototypes.push_back(newProto); 
        }
    }

    auto findProto = [&](const std::string& name) -> Entity* { for (auto& proto : entityPrototypes) { if (proto.name == name) return &proto; } return nullptr; };
    Entity* worldEntity = findProto("World");

    // Place the Audio Visualizer Audicle
    if (worldEntity && baseSystem.world) {
        Entity* vizProto = findProto("AudioVisualizer");
        if (vizProto) {
            glm::vec3 vizPos = {0, 4, 0}; 
            glm::vec3 initialColor = baseSystem.world->colorLibrary["Magenta"];
            worldEntity->instances.push_back(HostLogic::CreateInstance(baseSystem, vizProto->prototypeID, vizPos, initialColor));
        }
    }

    // Restore the full world generation to create a proper testbed for occlusion
    if (worldEntity && registry.count("AudicleSystem") && registry["AudicleSystem"]) {
        Entity* worldGenAudicleProto = findProto("DebugWorldGenerator");
        if(worldGenAudicleProto && baseSystem.world) {
            Entity* blockProto = findProto("Block");
            if(blockProto && baseSystem.world->colorLibrary.count("Grass") > 0) {
                int worldSize = 15;
                for (int x = -worldSize; x <= worldSize; ++x) {
                    for (int z = -worldSize; z <= worldSize; ++z) {
                        // Don't place a block at the origin or where the audicle is
                        if (abs(x) < 2 && abs(z) < 2) continue;
                        glm::vec3 pos(x, 4.0f, z); // Place blocks at the same height as the audicle
                        worldGenAudicleProto->instances.push_back(HostLogic::CreateInstance(baseSystem, blockProto->prototypeID, pos, baseSystem.world->colorLibrary["Grass"]));
                    }
                }
            }
            // Add the spawner audicle to the world so the AudicleSystem can process it
            worldEntity->instances.push_back(HostLogic::CreateInstance(baseSystem, worldGenAudicleProto->prototypeID, glm::vec3(0), {0,0,0}));
        }
    }
    
    Entity* starProto = findProto("Star");
    if (worldEntity && starProto && baseSystem.world) {
         for (int i = 0; i < baseSystem.world->numStars; i++) {
            float theta = static_cast<float>(rand())/RAND_MAX * 2.0f * 3.14159f;
            float phi = static_cast<float>(rand())/RAND_MAX * 3.14159f;
            glm::vec3 pos = glm::vec3(sin(phi)*cos(theta), cos(phi), sin(phi)*sin(theta)) * baseSystem.world->starDistance;
            worldEntity->instances.push_back(HostLogic::CreateInstance(baseSystem, starProto->prototypeID, pos, {1,1,1}));
        }
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Host::mainLoop() {
    if (programStatus != "Installed") {
        while (window && !glfwWindowShouldClose(window)) {
            glfwPollEvents();
            glClearColor(0.1f, 0.0f, 0.0f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
            glfwSwapBuffers(window);
        }
        return;
    }
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);

        for(const auto& step : updateFunctions) {
            if(functionRegistry.count(step.name)) {
                if (checkDependencies(step.dependencies)) {
                    functionRegistry[step.name](baseSystem, entityPrototypes, deltaTime, window);
                } else { programStatus = "Dependency Failure"; mainLoop(); return; }
            }
        }
        
        if (baseSystem.player) { baseSystem.player->mouseOffsetX = 0.0f; baseSystem.player->mouseOffsetY = 0.0f; }
        glfwSwapBuffers(window);
    }
}

void Host::cleanup() {
    std::vector<SystemStep> reversedCleanup = cleanupFunctions;
    std::reverse(reversedCleanup.begin(), reversedCleanup.end());

    for(const auto& step : reversedCleanup) {
        if(functionRegistry.count(step.name) && checkDependencies(step.dependencies)) {
            functionRegistry[step.name](baseSystem, entityPrototypes, 0.0f, window);
        }
    }
    if (window) glfwTerminate();
}
