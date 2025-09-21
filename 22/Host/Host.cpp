#pragma once

void Host::run() { init(); mainLoop(); cleanup(); }

void Host::registerSystemFunctions() {
    functionRegistry["LoadProcedureAssets"] = HostLogic::LoadProcedureAssets;
    functionRegistry["InitializeTesseract"] = TesseractSystemLogic::InitializeTesseract;
    functionRegistry["UpdateTesseract"] = TesseractSystemLogic::UpdateTesseract;
    functionRegistry["RenderTesseract"] = TesseractSystemLogic::RenderTesseract;
    functionRegistry["CleanupTesseract"] = TesseractSystemLogic::CleanupTesseract;
    functionRegistry["InitializeAudio"] = AudioSystemLogic::InitializeAudio;
    functionRegistry["CleanupAudio"] = AudioSystemLogic::CleanupAudio;
    functionRegistry["ProcessRayTracedAudio"] = RayTracedAudioSystemLogic::ProcessRayTracedAudio;
    functionRegistry["ProcessPinkNoiseAudicle"] = PinkNoiseSystemLogic::ProcessPinkNoiseAudicle;
    functionRegistry["ProcessAudicles"] = AudicleSystemLogic::ProcessAudicles;
    functionRegistry["UpdateCameraMatrices"] = CameraSystemLogic::UpdateCameraMatrices;
    functionRegistry["ProcessKeyboardInput"] = KeyboardInputSystemLogic::ProcessKeyboardInput;
    functionRegistry["UpdateCameraRotationFromMouse"] = MouseInputSystemLogic::UpdateCameraRotationFromMouse;
    functionRegistry["ProcessUAVMovement"] = UAVSystemLogic::ProcessUAVMovement;
    functionRegistry["InitializeRenderer"] = RenderSystemLogic::InitializeRenderer;
    functionRegistry["RenderScene"] = RenderSystemLogic::RenderScene;
    functionRegistry["CleanupRenderer"] = RenderSystemLogic::CleanupRenderer;
}

void Host::init() {
    loadRegistry();
    if (!std::get<bool>(registry["Program"])) { std::cerr << "FATAL: Program not installed. Halting." << std::endl; return; }

    baseSystem.level = std::make_unique<LevelContext>();
    baseSystem.app = std::make_unique<AppContext>();
    baseSystem.world = std::make_unique<WorldContext>();
    baseSystem.player = std::make_unique<PlayerContext>();
    baseSystem.instance = std::make_unique<InstanceContext>();
    baseSystem.renderer = std::make_unique<RendererContext>();
    baseSystem.audio = std::make_unique<AudioContext>();
    baseSystem.rayTracedAudio = std::make_unique<RayTracedAudioContext>();
    baseSystem.tesseract = std::make_unique<TesseractContext>();
    
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
    
    PopulateWorldsFromLevel();
    
    for(const auto& step : initFunctions) {
        if(functionRegistry.count(step.name)) {
            if (checkDependencies(step.dependencies)) {
                functionRegistry[step.name](baseSystem, entityPrototypes, 0.0f, window);
            }
        }
    }
    
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Host::PopulateWorldsFromLevel() {
    const std::vector<std::string> entityFiles = { 
        "Entities/Block.json", "Entities/Star.json", "Entities/Water.json",
        "Entities/Tesseract.json",
        "Entities/Audicles/UAV.json", "Entities/Audicles/TesseractControls.json"
    };
    for (const auto& filePath : entityFiles) {
        std::ifstream f(filePath);
        if (f.is_open()) {
            json data = json::parse(f);
            if (data.is_array()) {
                for(const auto& item : data) {
                    Entity newProto = item.get<Entity>();
                    newProto.prototypeID = entityPrototypes.size();
                    entityPrototypes.push_back(newProto);
                }
            } else {
                Entity newProto = data.get<Entity>();
                newProto.prototypeID = entityPrototypes.size();
                entityPrototypes.push_back(newProto);
            }
        } else { std::cerr << "Warning: Could not open entity file " << filePath << std::endl; }
    }

    std::string levelName = std::get<std::string>(registry["level"]);
    std::string levelPath = "Levels/" + levelName + "_level.json";
    std::ifstream levelFile(levelPath);
    if (!levelFile.is_open()) { std::cerr << "FATAL: Could not open level file " << levelPath << std::endl; exit(-1); }
    json levelData = json::parse(levelFile);

    for (const auto& worldFilename : levelData["worlds"]) {
        std::string path_str = worldFilename.get<std::string>();
        std::string worldPath = "Entities/Worlds/" + path_str;
        std::string audicleWorldPath = "Entities/Audicles/Worlds/" + path_str;
        std::ifstream worldFile(worldPath);
        std::ifstream audicleWorldFile(audicleWorldPath);
        
        Entity worldProto;
        if(worldFile.is_open()){
            worldProto = json::parse(worldFile).get<Entity>();
        } else if(audicleWorldFile.is_open()){
            worldProto = json::parse(audicleWorldFile).get<Entity>();
        } else {
            std::cerr << "Warning: Could not open world file " << worldPath << " or " << audicleWorldPath << std::endl;
            continue;
        }
        baseSystem.level->worlds.push_back(worldProto);
    }
    
    HostLogic::ProcessFillCommands(baseSystem, entityPrototypes);
}


void Host::mainLoop() {
    if (!std::get<bool>(registry["Program"])) { return; }
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
                }
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
