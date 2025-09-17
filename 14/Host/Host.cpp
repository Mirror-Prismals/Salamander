#pragma once

void Host::run() {
    init();
    mainLoop();
    cleanup();
}

void Host::registerSystemFunctions() {
    functionRegistry["LoadProcedureAssets"] = HostLogic::LoadProcedureAssets;
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

    // Restore the mouse cursor behavior
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
    
    const std::vector<std::string> entityFiles = { "Entities/World.json", "Entities/Star.json", "Entities/DebugWorldGenerator.json", "Entities/Block.json", "Entities/Water.json", "Entities/Branch.json", "Entities/WireframeBlock.json", "Entities/TransparentWave.json" };
    for (const auto& filePath : entityFiles) {
        std::ifstream f(filePath);
        if (f.is_open()) { Entity newProto = nlohmann::json::parse(f).get<Entity>(); newProto.prototypeID = this->entityPrototypes.size(); this->entityPrototypes.push_back(newProto); }
    }

    // --- TEMPORARY WORLD-GENERATION LOGIC (Data-Driven and Corrected) ---
    auto findProto = [&](const std::string& name) -> Entity* { for (auto& proto : entityPrototypes) { if (proto.name == name) return &proto; } return nullptr; };
    Entity* worldEntity = findProto("World");
    if (worldEntity && registry.count("AudicleSystem") && registry["AudicleSystem"]) {
        Entity* worldGenAudicleProto = findProto("DebugWorldGenerator");
        if(worldGenAudicleProto && baseSystem.world) {
            Entity* blockProto = findProto("Block");
            Entity* waterProto = findProto("Water");
            Entity* branchProto = findProto("Branch");
            Entity* wireframeProto = findProto("WireframeBlock");
            
            if(blockProto && waterProto && branchProto && wireframeProto && baseSystem.world->colorLibrary.count("Grass") > 0) {
                int worldSize = 20;
                for (int x = -worldSize; x <= worldSize; ++x) {
                    for (int z = -worldSize; z <= worldSize; ++z) {
                        glm::vec3 pos(x, 0.0f, z);
                        if (x > 5 && x < 10 && z > 5 && z < 10) {
                             worldGenAudicleProto->instances.push_back(HostLogic::CreateInstance(baseSystem, waterProto->prototypeID, pos, baseSystem.world->colorLibrary["Water"]));
                        } else {
                             worldGenAudicleProto->instances.push_back(HostLogic::CreateInstance(baseSystem, blockProto->prototypeID, pos, baseSystem.world->colorLibrary["Grass"]));
                        }
                    }
                }
                worldGenAudicleProto->instances.push_back(HostLogic::CreateInstance(baseSystem, blockProto->prototypeID, {-3, 1, -3}, baseSystem.world->colorLibrary["Wood"]));
                EntityInstance branchInst = HostLogic::CreateInstance(baseSystem, branchProto->prototypeID, {-3, 2, -3}, baseSystem.world->colorLibrary["Leaf"]);
                branchInst.rotation = 45.0f;
                worldGenAudicleProto->instances.push_back(branchInst);
                worldGenAudicleProto->instances.push_back(HostLogic::CreateInstance(baseSystem, wireframeProto->prototypeID, {0, 2, 5}, baseSystem.world->colorLibrary["PulsingGrid"]));
            }
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
    for(const auto& step : cleanupFunctions) {
        if(functionRegistry.count(step.name) && checkDependencies(step.dependencies)) {
            functionRegistry[step.name](baseSystem, entityPrototypes, 0.0f, window);
        }
    }
    if (window) glfwTerminate();
}
