#pragma once
#include <chrono>
#include <unordered_map>
#include <vector>
#include <algorithm>

void Host::run() { init(); mainLoop(); cleanup(); }

void Host::registerSystemFunctions() {
    functionRegistry["LoadProcedureAssets"] = HostLogic::LoadProcedureAssets;
    functionRegistry["SetPlayerSpawn"] = SpawnSystemLogic::SetPlayerSpawn;
    functionRegistry["InitializeAudio"] = AudioSystemLogic::InitializeAudio;
    functionRegistry["CleanupAudio"] = AudioSystemLogic::CleanupAudio;
    functionRegistry["ProcessRayTracedAudio"] = RayTracedAudioSystemLogic::ProcessRayTracedAudio;
    functionRegistry["ProcessPinkNoiseAudicle"] = PinkNoiseSystemLogic::ProcessPinkNoiseAudicle;
    functionRegistry["ProcessAudicles"] = AudicleSystemLogic::ProcessAudicles;
    functionRegistry["ResolveCollisions"] = CollisionSystemLogic::ResolveCollisions;
    functionRegistry["ProcessWalkMovement"] = WalkModeSystemLogic::ProcessWalkMovement;
    functionRegistry["ApplyGravity"] = GravitySystemLogic::ApplyGravity;
    functionRegistry["UpdateCameraMatrices"] = CameraSystemLogic::UpdateCameraMatrices;
    functionRegistry["ProcessKeyboardInput"] = KeyboardInputSystemLogic::ProcessKeyboardInput;
    functionRegistry["ProcessVolumeFills"] = VolumeFillSystemLogic::ProcessVolumeFills;
    functionRegistry["UpdateCameraRotationFromMouse"] = MouseInputSystemLogic::UpdateCameraRotationFromMouse;
    functionRegistry["ProcessUAVMovement"] = UAVSystemLogic::ProcessUAVMovement;
    functionRegistry["InitializeRenderer"] = RenderSystemLogic::InitializeRenderer;
    functionRegistry["RenderScene"] = RenderSystemLogic::RenderScene;
    functionRegistry["CleanupRenderer"] = RenderSystemLogic::CleanupRenderer;
    functionRegistry["GenerateTerrain"] = TerrainSystemLogic::GenerateTerrain;
    functionRegistry["UpdateExpanseTerrain"] = TerrainSystemLogic::UpdateExpanseTerrain;
    functionRegistry["LoadExpanseConfig"] = ExpanseBiomeSystemLogic::LoadExpanseConfig;
    functionRegistry["UpdateExpanseTrees"] = TreeGenerationSystemLogic::UpdateExpanseTrees;
    functionRegistry["UpdateChunks"] = ChunkSystemLogic::UpdateChunks;
    functionRegistry["UpdateFaces"] = FaceCullingSystemLogic::UpdateFaces;
    functionRegistry["ProcessStructurePlacement"] = StructurePlacementSystemLogic::ProcessStructurePlacement;
    functionRegistry["ProcessStructureCapture"] = StructureCaptureSystemLogic::ProcessStructureCapture;
    functionRegistry["UpdateBlockSelection"] = BlockSelectionSystemLogic::UpdateBlockSelection;
    functionRegistry["UpdateBlockCharge"] = BlockChargeSystemLogic::UpdateBlockCharge;
    functionRegistry["UpdateHUD"] = HUDSystemLogic::UpdateHUD;
    functionRegistry["UpdateBuildMode"] = BuildSystemLogic::UpdateBuildMode;
    functionRegistry["UpdateUIScreen"] = UIScreenSystemLogic::UpdateUIScreen;
    functionRegistry["UpdateGlyphs"] = GlyphSystemLogic::UpdateGlyphs;
    functionRegistry["UpdateDebugHud"] = DebugHudSystemLogic::UpdateDebugHud;
    functionRegistry["UpdateFonts"] = FontSystemLogic::UpdateFonts;
    functionRegistry["CleanupFonts"] = FontSystemLogic::CleanupFonts;
    functionRegistry["UpdateComputerCursor"] = ComputerCursorSystemLogic::UpdateComputerCursor;
    functionRegistry["UpdateButtons"] = ButtonSystemLogic::UpdateButtons;
    functionRegistry["UpdateChucK"] = ChucKSystemLogic::UpdateChucK;
    functionRegistry["UpdateAudioRayVisualizer"] = AudioRayVisualizerSystemLogic::UpdateAudioRayVisualizer;
    functionRegistry["UpdateRegistry"] = RegistryEditorSystemLogic::UpdateRegistry;
    functionRegistry["UpdateDawState"] = DawStateSystemLogic::UpdateDawState;
    functionRegistry["CleanupDawState"] = DawStateSystemLogic::CleanupDawState;
    functionRegistry["UpdateDawLanes"] = DawLaneSystemLogic::UpdateDawLanes;
    functionRegistry["UpdateBootSequence"] = BootSequenceSystemLogic::UpdateBootSequence;
    functionRegistry["LoadBlockTextures"] = BlockTextureSystemLogic::LoadBlockTextures;
}

void Host::init() {
    loadRegistry();
    if (!std::get<bool>(registry["Program"])) { std::cerr << "FATAL: Program not installed. Halting." << std::endl; return; }

    baseSystem.level = std::make_unique<LevelContext>();
    baseSystem.app = std::make_unique<AppContext>();
    baseSystem.world = std::make_unique<WorldContext>();
    baseSystem.chunk = std::make_unique<ChunkContext>();
    baseSystem.face = std::make_unique<FaceContext>();
    baseSystem.player = std::make_unique<PlayerContext>();
    baseSystem.instance = std::make_unique<InstanceContext>();
    baseSystem.renderer = std::make_unique<RendererContext>();
    baseSystem.audio = std::make_unique<AudioContext>();
    baseSystem.rayTracedAudio = std::make_unique<RayTracedAudioContext>();
    baseSystem.hud = std::make_unique<HUDContext>();
    baseSystem.ui = std::make_unique<UIContext>();
    baseSystem.font = std::make_unique<FontContext>();
    baseSystem.daw = std::make_unique<DawContext>();
    baseSystem.registry = &registry;
    baseSystem.reloadRequested = &reloadRequested;
    baseSystem.reloadTarget = &reloadTarget;

    if (baseSystem.level && registry.count("spawn") && std::holds_alternative<std::string>(registry["spawn"])) {
        baseSystem.level->spawnKey = std::get<std::string>(registry["spawn"]);
    }
    if (registry.count("gamemode") && std::holds_alternative<std::string>(registry["gamemode"])) {
        baseSystem.gamemode = std::get<std::string>(registry["gamemode"]);
    }

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
    glfwSetScrollCallback(window, [](GLFWwindow* w, double xoff, double yoff){ static_cast<Host*>(glfwGetWindowUserPointer(w))->processScroll(xoff, yoff); });

    PopulateWorldsFromLevel();

    for(const auto& step : initFunctions) {
        if (step.name == "InitializeRenderer" && rendererInitialized) continue;
        if (step.name == "InitializeAudio" && audioInitialized) continue;
        if(functionRegistry.count(step.name)) {
            if (checkDependencies(step.dependencies)) {
                functionRegistry[step.name](baseSystem, entityPrototypes, 0.0f, window);
                if (step.name == "InitializeRenderer") rendererInitialized = true;
                if (step.name == "InitializeAudio") audioInitialized = true;
            }
        }
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Host::runCleanupSteps() {
    std::vector<SystemStep> reversedCleanup = cleanupFunctions;
    std::reverse(reversedCleanup.begin(), reversedCleanup.end());
    for(const auto& step : reversedCleanup) {
        if(functionRegistry.count(step.name) && checkDependencies(step.dependencies)) {
            functionRegistry[step.name](baseSystem, entityPrototypes, 0.0f, window);
        }
    }
}

void Host::reloadLevel(const std::string& levelName) {
    // In-place level reload: keep contexts alive, just reload worlds and reset caches/state.
    if (!baseSystem.level) baseSystem.level = std::make_unique<LevelContext>();
    baseSystem.level->worlds.clear();
    baseSystem.level->activeWorldIndex = 0;
    if (registry.count("spawn") && std::holds_alternative<std::string>(registry["spawn"])) {
        baseSystem.level->spawnKey = std::get<std::string>(registry["spawn"]);
    }

    // Reset per-level caches/contexts.
    if (baseSystem.chunk) baseSystem.chunk = std::make_unique<ChunkContext>();
    if (baseSystem.face) baseSystem.face = std::make_unique<FaceContext>();
    if (baseSystem.instance) baseSystem.instance = std::make_unique<InstanceContext>();
    if (baseSystem.hud) baseSystem.hud = std::make_unique<HUDContext>();
    if (baseSystem.font) baseSystem.font = std::make_unique<FontContext>();
    if (baseSystem.rayTracedAudio) {
        baseSystem.rayTracedAudio->sourceCacheBuilt = false;
        baseSystem.rayTracedAudio->sourceInstances.clear();
        baseSystem.rayTracedAudio->sourceStates.clear();
        baseSystem.rayTracedAudio->debugRays.clear();
        baseSystem.rayTracedAudio->debugVoxels.clear();
        baseSystem.rayTracedAudio->lastHeatmapTime = -1.0;
    }
    // Keep UIContext but clear runtime flags.
    if (baseSystem.ui) {
        baseSystem.ui->active = false;
        baseSystem.ui->fullscreenActive = false;
        baseSystem.ui->loadingActive = false;
        baseSystem.ui->levelSwitchPending = false;
        baseSystem.ui->bootLoadingStarted = false;
        baseSystem.ui->cursorReleased = false;
        baseSystem.ui->uiLeftDown = baseSystem.ui->uiLeftPressed = baseSystem.ui->uiLeftReleased = false;
        baseSystem.ui->computerCacheBuilt = false;
        baseSystem.ui->computerInstances.clear();
    }
    if (baseSystem.daw) {
        baseSystem.daw->uiCacheBuilt = false;
    }

    // Rebuild world prototypes and instances for the new level.
    baseSystem.world = std::make_unique<WorldContext>();
    entityPrototypes.clear();
    initFunctions.clear();
    updateFunctions.clear();
    cleanupFunctions.clear();
    loadSystems();
    HostLogic::LoadProcedureAssets(baseSystem, entityPrototypes, 0.0f, nullptr);
    // Override registry level if an explicit target is provided.
    if (!levelName.empty()) {
        registry["level"] = levelName;
    }
    PopulateWorldsFromLevel();

    for(const auto& step : initFunctions) {
        if (step.name == "InitializeRenderer" && rendererInitialized) continue;
        if (step.name == "InitializeAudio" && audioInitialized) continue;
        if(functionRegistry.count(step.name)) {
            if (checkDependencies(step.dependencies)) {
                functionRegistry[step.name](baseSystem, entityPrototypes, 0.0f, window);
                if (step.name == "InitializeRenderer") rendererInitialized = true;
                if (step.name == "InitializeAudio") audioInitialized = true;
            }
        }
    }

    // Recapture cursor after reload if UI is not active.
    if (window && baseSystem.ui && !baseSystem.ui->active) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        baseSystem.ui->cursorReleased = false;
    }
}

void Host::PopulateWorldsFromLevel() {
    const std::vector<std::string> entityFiles = {
        "Entities/Block.json", "Entities/Leaf.json", "Entities/Branch.json", "Entities/TexturedBlock.json", "Entities/Star.json", "Entities/Water.json",
        "Entities/World.json", "Entities/DebugWorldGenerator.json",
        "Entities/Audicles/UAV.json", "Entities/AudioVisualizer.json", "Entities/Computer.json",
        "Entities/Screen.json", "Entities/Button.json", "Entities/ActionButton.json",
        "Entities/LoadingBar.json",
        "Entities/Text.json",
        "Entities/ScaffoldBlock.json",
        "Entities/Faces.json"
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
        std::vector<std::string> searchPaths = {
            "Entities/Worlds/" + path_str,
            "Entities/Audicles/Worlds/" + path_str,
            "Entities/Worlds/Audicles/" + path_str // Adding your new path
        };

        std::ifstream worldFile;
        std::string foundPath;
        for(const auto& path : searchPaths) {
            worldFile.open(path);
            if(worldFile.is_open()) {
                foundPath = path;
                break;
            }
        }

        if(worldFile.is_open()){
            Entity worldProto = json::parse(worldFile).get<Entity>();
            baseSystem.level->worlds.push_back(worldProto);
            worldFile.close();
        } else {
            std::cerr << "Warning: Could not find world file '" << path_str << "' in any known directory." << std::endl;
        }
    }

    // Process instance declarations in non-volume worlds
    for (auto& worldProto : baseSystem.level->worlds) {
        if (!worldProto.isVolume && !worldProto.instances.empty()) {
            std::vector<EntityInstance> processedInstances;
            std::vector<EntityInstance> templates = worldProto.instances;

            for (const auto& instTemplate : templates) {
                const Entity* entityProto = HostLogic::findPrototype(instTemplate.name, entityPrototypes);
                if (!entityProto) {
                    std::cerr << "Warning: Could not find prototype '" << instTemplate.name << "' for instance in world '" << worldProto.name << "'." << std::endl;
                    continue;
                }

                int count = entityProto->count > 1 ? entityProto->count : 1;
                // Allow instance definition in JSON to override prototype's count
                if (instTemplate.prototypeID != 0 && instTemplate.prototypeID != -1) {
                     // This is a placeholder for a better way to handle instance-specific data
                }

                if (entityProto->name == "Star") count = 2000;

                for (int i = 0; i < count; ++i) {
                    glm::vec3 pos = instTemplate.position;
                    glm::vec3 instColor = instTemplate.color;
                    if (baseSystem.world) {
                        if (!instTemplate.colorName.empty()) {
                            auto it = baseSystem.world->colorLibrary.find(instTemplate.colorName);
                            if (it != baseSystem.world->colorLibrary.end()) instColor = it->second;
                        }
                    }
                    if (entityProto->isStar) {
                        std::random_device rd; std::mt19937 gen(rd()); std::uniform_real_distribution<> distrib(0, 1);
                        float theta = distrib(gen) * 2.0f * 3.14159f; float phi = distrib(gen) * 3.14159f;
                        pos = glm::vec3(sin(phi)*cos(theta), cos(phi), sin(phi)*sin(theta)) * 200.0f;
                    }
                    EntityInstance inst = HostLogic::CreateInstance(baseSystem, entityProto->prototypeID, pos, instColor);
                    inst.size = instTemplate.size;
                    inst.text = instTemplate.text;
                    inst.textType = instTemplate.textType;
                    inst.textKey = instTemplate.textKey;
                    inst.font = instTemplate.font;
                    inst.colorName = instTemplate.colorName;
                    inst.topColorName = instTemplate.topColorName;
                    inst.sideColorName = instTemplate.sideColorName;
                    inst.actionType = instTemplate.actionType;
                    inst.actionKey = instTemplate.actionKey;
                    inst.actionValue = instTemplate.actionValue;
                    processedInstances.push_back(inst);
                }
            }
            worldProto.instances = processedInstances;
        }
    }
}


void Host::mainLoop() {
    if (!std::get<bool>(registry["Program"])) { return; }
    static double lastPerfReport = 0.0;
    static std::unordered_map<std::string, double> perfTotalsMs;
    static std::unordered_map<std::string, int> perfCounts;
    static int perfFrames = 0;
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
        if (reloadRequested) {
            std::string target = reloadTarget;
            reloadRequested = false;
            reloadLevel(target);
        }
        for(const auto& step : updateFunctions) {
            if(functionRegistry.count(step.name)) {
                if (checkDependencies(step.dependencies)) {
                    auto start = std::chrono::steady_clock::now();
                    functionRegistry[step.name](baseSystem, entityPrototypes, deltaTime, window);
                    double elapsedMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start
                    ).count();
                    perfTotalsMs[step.name] += elapsedMs;
                    perfCounts[step.name] += 1;
                }
            }
        }
        perfFrames += 1;
        double now = glfwGetTime();
        if (lastPerfReport <= 0.0) {
            lastPerfReport = now;
        } else if (now - lastPerfReport >= 1.0) {
            std::vector<std::pair<std::string, double>> sortedTotals;
            sortedTotals.reserve(perfTotalsMs.size());
            for (const auto& [name, total] : perfTotalsMs) {
                sortedTotals.emplace_back(name, total);
            }
            std::sort(sortedTotals.begin(), sortedTotals.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });
            double interval = now - lastPerfReport;
            double fps = interval > 0.0 ? static_cast<double>(perfFrames) / interval : 0.0;
            std::cout << "[Perf] " << perfFrames << " frames in "
                      << interval << "s (~" << fps << " fps)" << std::endl;
            size_t limit = std::min<size_t>(8, sortedTotals.size());
            for (size_t i = 0; i < limit; ++i) {
                const auto& entry = sortedTotals[i];
                int count = perfCounts.count(entry.first) ? perfCounts[entry.first] : 0;
                double avg = count > 0 ? entry.second / static_cast<double>(count) : 0.0;
                std::cout << "[Perf] " << entry.first << ": total "
                          << entry.second << " ms, avg " << avg << " ms" << std::endl;
            }
            perfTotalsMs.clear();
            perfCounts.clear();
            perfFrames = 0;
            lastPerfReport = now;
        }
        if (baseSystem.player) {
            PlayerContext& player = *baseSystem.player;
            player.mouseOffsetX = 0.0f;
            player.mouseOffsetY = 0.0f;
            player.scrollYOffset = 0.0;
            player.rightMousePressed = false;
            player.leftMousePressed = false;
            player.rightMouseReleased = false;
            player.leftMouseReleased = false;
        }
        glfwSwapBuffers(window);
    }
}

void Host::cleanup() {
    runCleanupSteps();
    if (window) glfwTerminate();
}
