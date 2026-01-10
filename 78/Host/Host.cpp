#pragma once
#include <chrono>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <filesystem>

void Host::run() { init(); mainLoop(); cleanup(); }

void Host::registerSystemFunctions() {
    functionRegistry["LoadProcedureAssets"] = HostLogic::LoadProcedureAssets;
    functionRegistry["SetPlayerSpawn"] = SpawnSystemLogic::SetPlayerSpawn;
    functionRegistry["InitializeAudio"] = AudioSystemLogic::InitializeAudio;
    functionRegistry["CleanupAudio"] = AudioSystemLogic::CleanupAudio;
    functionRegistry["UpdateSoundtracks"] = SoundtrackSystemLogic::UpdateSoundtracks;
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
    functionRegistry["UpdateGreedyMeshes"] = GreedyMeshingSystemLogic::UpdateGreedyMeshes;
    functionRegistry["ProcessStructurePlacement"] = StructurePlacementSystemLogic::ProcessStructurePlacement;
    functionRegistry["ProcessStructureCapture"] = StructureCaptureSystemLogic::ProcessStructureCapture;
    functionRegistry["UpdateBlockSelection"] = BlockSelectionSystemLogic::UpdateBlockSelection;
    functionRegistry["UpdateBlockCharge"] = BlockChargeSystemLogic::UpdateBlockCharge;
    functionRegistry["UpdateHUD"] = HUDSystemLogic::UpdateHUD;
    functionRegistry["UpdateBuildMode"] = BuildSystemLogic::UpdateBuildMode;
    functionRegistry["UpdateUIScreen"] = UIScreenSystemLogic::UpdateUIScreen;
    functionRegistry["UpdateGlyphs"] = GlyphSystemLogic::UpdateGlyphs;
    functionRegistry["UpdateDecibelMeters"] = DecibelMeterSystemLogic::UpdateDecibelMeters;
    functionRegistry["UpdateDawFaders"] = DawFaderSystemLogic::UpdateDawFaders;
    functionRegistry["UpdateDebugHud"] = DebugHudSystemLogic::UpdateDebugHud;
    functionRegistry["UpdateDebugWireframe"] = DebugWireframeSystemLogic::UpdateDebugWireframe;
    functionRegistry["UpdateFonts"] = FontSystemLogic::UpdateFonts;
    functionRegistry["RenderFontsTimeline"] = FontSystemLogic::RenderFontsTimeline;
    functionRegistry["RenderFontsSideButtons"] = FontSystemLogic::RenderFontsSideButtons;
    functionRegistry["RenderFontsTopButtons"] = FontSystemLogic::RenderFontsTopButtons;
    functionRegistry["01_RenderFontsTimeline"] = FontSystemLogic::RenderFontsTimeline;
    functionRegistry["04_RenderFontsSideButtons"] = FontSystemLogic::RenderFontsSideButtons;
    functionRegistry["07_RenderFontsTopButtons"] = FontSystemLogic::RenderFontsTopButtons;
    functionRegistry["CleanupFonts"] = FontSystemLogic::CleanupFonts;
    functionRegistry["UpdateComputerCursor"] = ComputerCursorSystemLogic::UpdateComputerCursor;
    functionRegistry["UpdateButtons"] = ButtonSystemLogic::UpdateButtons;
    functionRegistry["UpdateButtonInput"] = ButtonSystemLogic::UpdateButtonInput;
    functionRegistry["RenderButtonsSide"] = ButtonSystemLogic::RenderButtonsSide;
    functionRegistry["RenderButtonsTopBottom"] = ButtonSystemLogic::RenderButtonsTopBottom;
    functionRegistry["03_RenderButtonsSide"] = ButtonSystemLogic::RenderButtonsSide;
    functionRegistry["06_RenderButtonsTopBottom"] = ButtonSystemLogic::RenderButtonsTopBottom;
    functionRegistry["UpdatePanels"] = PanelSystemLogic::UpdatePanels;
    functionRegistry["RenderPanels"] = PanelSystemLogic::RenderPanels;
    functionRegistry["RenderSidePanels"] = PanelSystemLogic::RenderSidePanels;
    functionRegistry["RenderTopBottomPanels"] = PanelSystemLogic::RenderTopBottomPanels;
    functionRegistry["02_RenderSidePanels"] = PanelSystemLogic::RenderSidePanels;
    functionRegistry["05_RenderTopBottomPanels"] = PanelSystemLogic::RenderTopBottomPanels;
    functionRegistry["RenderDecibelMeters"] = DecibelMeterSystemLogic::RenderDecibelMeters;
    functionRegistry["RenderDawFaders"] = DawFaderSystemLogic::RenderDawFaders;
    functionRegistry["05a_RenderDecibelMeters"] = DecibelMeterSystemLogic::RenderDecibelMeters;
    functionRegistry["05b_RenderDawFaders"] = DawFaderSystemLogic::RenderDawFaders;
    functionRegistry["UpdateUIStamping"] = UIStampingSystemLogic::UpdateUIStamping;
    functionRegistry["UpdateChucK"] = ChucKSystemLogic::UpdateChucK;
    functionRegistry["UpdateAudioRayVisualizer"] = AudioRayVisualizerSystemLogic::UpdateAudioRayVisualizer;
    functionRegistry["UpdateSoundPhysics"] = SoundPhysicsSystemLogic::UpdateSoundPhysics;
    functionRegistry["UpdateRegistry"] = RegistryEditorSystemLogic::UpdateRegistry;
    functionRegistry["UpdateMirrors"] = MirrorSystemLogic::UpdateMirrors;
    functionRegistry["UpdateDawState"] = DawStateSystemLogic::UpdateDawState;
    functionRegistry["CleanupDawState"] = DawStateSystemLogic::CleanupDawState;
    functionRegistry["UpdateDawLanes"] = DawLaneSystemLogic::UpdateDawLanes;
    functionRegistry["UpdateMidiState"] = MidiStateSystemLogic::UpdateMidiState;
    functionRegistry["CleanupMidiState"] = MidiStateSystemLogic::CleanupMidiState;
    functionRegistry["UpdateMidiLane"] = MidiLaneSystemLogic::UpdateMidiLane;
    functionRegistry["UpdateMicrophoneBlocks"] = MicrophoneBlockSystemLogic::UpdateMicrophoneBlocks;
    functionRegistry["UpdatePerf"] = PerfSystemLogic::UpdatePerf;
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
    baseSystem.greedy = std::make_unique<GreedyContext>();
    baseSystem.player = std::make_unique<PlayerContext>();
    baseSystem.instance = std::make_unique<InstanceContext>();
    baseSystem.renderer = std::make_unique<RendererContext>();
    baseSystem.audio = std::make_unique<AudioContext>();
    baseSystem.rayTracedAudio = std::make_unique<RayTracedAudioContext>();
    baseSystem.hud = std::make_unique<HUDContext>();
    baseSystem.ui = std::make_unique<UIContext>();
    baseSystem.uiStamp = std::make_unique<UIStampingContext>();
    baseSystem.panel = std::make_unique<PanelContext>();
    baseSystem.decibelMeter = std::make_unique<DecibelMeterContext>();
    baseSystem.fader = std::make_unique<DawFaderContext>();
    baseSystem.mirror = std::make_unique<MirrorContext>();
    baseSystem.font = std::make_unique<FontContext>();
    baseSystem.daw = std::make_unique<DawContext>();
    baseSystem.midi = std::make_unique<MidiContext>();
    baseSystem.perf = std::make_unique<PerfContext>();
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
    if (baseSystem.uiStamp) baseSystem.uiStamp = std::make_unique<UIStampingContext>();
    if (baseSystem.panel) baseSystem.panel = std::make_unique<PanelContext>();
    if (baseSystem.rayTracedAudio) {
        baseSystem.rayTracedAudio->sourceCacheBuilt = false;
        baseSystem.rayTracedAudio->sourceInstances.clear();
        baseSystem.rayTracedAudio->sourceStates.clear();
        baseSystem.rayTracedAudio->debugSegments.clear();
        baseSystem.rayTracedAudio->lastDebugTime = -1.0;
        baseSystem.rayTracedAudio->batch = RayTraceBatch{};
        baseSystem.rayTracedAudio->lastBatchCompleteTime = -1.0;
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
        baseSystem.ui->mainScrollDelta = 0.0;
        baseSystem.ui->panelScrollDelta = 0.0;
    }
    if (baseSystem.mirror) {
        baseSystem.mirror->mirrors.clear();
        baseSystem.mirror->activeMirrorIndex = -1;
        baseSystem.mirror->activeDeviceInstanceID = -1;
        baseSystem.mirror->uiOffset = glm::vec2(0.0f);
        baseSystem.mirror->uiScale = 1.0f;
        baseSystem.mirror->expandedMirrorIndex = -1;
        baseSystem.mirror->expanded = false;
        baseSystem.mirror->expandedWorldIndices.clear();
        baseSystem.mirror->deviceMirrorIndex.clear();
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

namespace {
    glm::vec2 readVec2(const json& value, const glm::vec2& fallback) {
        if (!value.is_array() || value.size() < 2) return fallback;
        return glm::vec2(value[0].get<float>(), value[1].get<float>());
    }

    glm::vec3 readVec3(const json& value, const glm::vec3& fallback) {
        if (!value.is_array() || value.size() < 3) return fallback;
        return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
    }

    MirrorDefinition parseMirrorDefinition(const json& data, const std::string& fallbackName) {
        MirrorDefinition mirror;
        mirror.name = fallbackName;

        if (data.contains("name")) {
            mirror.name = data["name"].get<std::string>();
        }
        if (data.contains("uiTransform") && data["uiTransform"].is_object()) {
            const auto& t = data["uiTransform"];
            if (t.contains("scale")) {
                mirror.uiScale = t["scale"].get<float>();
            }
            if (t.contains("offset")) {
                mirror.uiOffset = readVec2(t["offset"], mirror.uiOffset);
            }
        }

        if (data.contains("worldInstances") && data["worldInstances"].is_array()) {
            for (const auto& instData : data["worldInstances"]) {
                if (!instData.is_object() || !instData.contains("world")) continue;
                MirrorWorldInstance inst;
                inst.worldName = instData["world"].get<std::string>();

                if (instData.contains("repeat") && instData["repeat"].is_object()) {
                    const auto& repeat = instData["repeat"];
                    if (repeat.contains("count")) {
                        inst.repeatCount = std::max(1, repeat["count"].get<int>());
                    }
                    if (repeat.contains("offset")) {
                        inst.repeatOffset = readVec3(repeat["offset"], inst.repeatOffset);
                    }
                }

                if (instData.contains("overrides") && instData["overrides"].is_array()) {
                    for (const auto& ovData : instData["overrides"]) {
                        if (!ovData.is_object()) continue;
                        MirrorOverride ov;
                        if (ovData.contains("match") && ovData["match"].is_object()) {
                            const auto& match = ovData["match"];
                            if (match.contains("controlId")) ov.matchControlId = match["controlId"].get<std::string>();
                            if (match.contains("controlRole")) ov.matchControlRole = match["controlRole"].get<std::string>();
                            if (match.contains("name")) ov.matchName = match["name"].get<std::string>();
                        }
                        if (ovData.contains("set")) {
                            ov.set = ovData["set"];
                        }
                        inst.overrides.push_back(std::move(ov));
                    }
                }
                if (instData.contains("rowOverrides") && instData["rowOverrides"].is_array()) {
                    for (const auto& rowData : instData["rowOverrides"]) {
                        if (!rowData.is_object()) continue;
                        MirrorRowOverride rowOv;
                        if (rowData.contains("row")) rowOv.row = rowData["row"].get<int>();
                        else if (rowData.contains("track")) rowOv.row = rowData["track"].get<int>();
                        if (rowOv.row < 0) continue;
                        if (rowData.contains("match") && rowData["match"].is_object()) {
                            const auto& match = rowData["match"];
                            if (match.contains("controlId")) rowOv.matchControlId = match["controlId"].get<std::string>();
                            if (match.contains("controlRole")) rowOv.matchControlRole = match["controlRole"].get<std::string>();
                            if (match.contains("name")) rowOv.matchName = match["name"].get<std::string>();
                        }
                        if (rowData.contains("set")) {
                            rowOv.set = rowData["set"];
                        }
                        inst.rowOverrides.push_back(std::move(rowOv));
                    }
                }
                mirror.worldInstances.push_back(std::move(inst));
            }
        }

        return mirror;
    }
}

void Host::PopulateWorldsFromLevel() {
    const std::vector<std::string> entityFiles = {
        "Entities/Block.json", "Entities/Leaf.json", "Entities/Branch.json", "Entities/TexturedBlock.json", "Entities/Star.json", "Entities/Water.json",
        "Entities/World.json", "Entities/DebugWorldGenerator.json",
        "Entities/Audicles/UAV.json", "Entities/AudioVisualizer.json", "Entities/Microphone.json", "Entities/Computer.json",
        "Entities/ScaffoldBlock.json",
        "Entities/Faces.json"
    };
    auto loadEntityFile = [&](const std::string& filePath) {
        std::ifstream f(filePath);
        if (!f.is_open()) {
            std::cerr << "Warning: Could not open entity file " << filePath << std::endl;
            return;
        }
        json data = json::parse(f);
        if (data.is_array()) {
            for (const auto& item : data) {
                Entity newProto = item.get<Entity>();
                newProto.prototypeID = entityPrototypes.size();
                entityPrototypes.push_back(newProto);
            }
        } else {
            Entity newProto = data.get<Entity>();
            newProto.prototypeID = entityPrototypes.size();
            entityPrototypes.push_back(newProto);
        }
    };
    auto loadEntityDirectory = [&](const std::string& dirPath) {
        std::error_code ec;
        if (!std::filesystem::exists(dirPath, ec)) return;
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dirPath, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            auto path = entry.path();
            if (path.extension() == ".json") files.push_back(path);
        }
        std::sort(files.begin(), files.end());
        for (const auto& path : files) {
            loadEntityFile(path.string());
        }
    };
    for (const auto& filePath : entityFiles) {
        loadEntityFile(filePath);
    }
    loadEntityDirectory("Entities/UI");

    std::string levelName = std::get<std::string>(registry["level"]);
    std::string levelPath = "Levels/" + levelName + "_level.json";
    std::ifstream levelFile(levelPath);
    if (!levelFile.is_open()) { std::cerr << "FATAL: Could not open level file " << levelPath << std::endl; exit(-1); }
    json levelData = json::parse(levelFile);

    if (baseSystem.mirror) {
        baseSystem.mirror->mirrors.clear();
        baseSystem.mirror->activeMirrorIndex = -1;
        baseSystem.mirror->activeDeviceInstanceID = -1;
        baseSystem.mirror->uiOffset = glm::vec2(0.0f);
        baseSystem.mirror->uiScale = 1.0f;
        baseSystem.mirror->expandedMirrorIndex = -1;
        baseSystem.mirror->expanded = false;
        baseSystem.mirror->expandedWorldIndices.clear();
        if (levelData.contains("mirrors") && levelData["mirrors"].is_array()) {
            for (const auto& mirrorFilename : levelData["mirrors"]) {
                if (!mirrorFilename.is_string()) continue;
                std::string mirrorPath = "Mirrors/" + mirrorFilename.get<std::string>();
                std::ifstream mirrorFile(mirrorPath);
                if (!mirrorFile.is_open()) {
                    std::cerr << "Warning: Could not open mirror file " << mirrorPath << std::endl;
                    continue;
                }
                json mirrorData = json::parse(mirrorFile);
                MirrorDefinition mirror = parseMirrorDefinition(mirrorData, mirrorFilename.get<std::string>());
                baseSystem.mirror->mirrors.push_back(std::move(mirror));
            }
        }
    }

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
                    inst.rotation = instTemplate.rotation;
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
                    inst.buttonMode = instTemplate.buttonMode;
                    inst.controlId = instTemplate.controlId;
                    inst.controlRole = instTemplate.controlRole;
                    inst.styleId = instTemplate.styleId;
                    inst.uiState = instTemplate.uiState;
                    inst.uiStates = instTemplate.uiStates;
                    processedInstances.push_back(inst);
                }
            }
            worldProto.instances = processedInstances;
        }
    }
}


void Host::mainLoop() {
    if (!std::get<bool>(registry["Program"])) { return; }
    PerfContext* perf = baseSystem.perf ? baseSystem.perf.get() : nullptr;
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        baseSystem.frameIndex += 1;
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
        if (reloadRequested) {
            std::string target = reloadTarget;
            reloadRequested = false;
            reloadLevel(target);
        }
        bool perfEnabled = perf && perf->enabled;
        for(const auto& step : updateFunctions) {
            if(functionRegistry.count(step.name)) {
                if (checkDependencies(step.dependencies)) {
                    bool trackPerf = perfEnabled && !perf->allowlist.empty() &&
                                     perf->allowlist.count(step.name) > 0;
                    if (trackPerf) {
                        auto start = std::chrono::steady_clock::now();
                        functionRegistry[step.name](baseSystem, entityPrototypes, deltaTime, window);
                        double elapsedMs = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - start
                        ).count();
                        perf->totalsMs[step.name] += elapsedMs;
                        if (elapsedMs > perf->maxMs[step.name]) {
                            perf->maxMs[step.name] = elapsedMs;
                        }
                        if (perf->hitchThresholdMs > 0.0 && elapsedMs >= perf->hitchThresholdMs) {
                            perf->hitchCounts[step.name] += 1;
                        }
                        perf->counts[step.name] += 1;
                    } else {
                        functionRegistry[step.name](baseSystem, entityPrototypes, deltaTime, window);
                    }
                }
            }
        }
        if (perfEnabled) {
            perf->frameCount += 1;
        }
        if (baseSystem.player) {
            PlayerContext& player = *baseSystem.player;
            player.mouseOffsetX = 0.0f;
            player.mouseOffsetY = 0.0f;
            player.scrollYOffset = 0.0;
            player.rightMousePressed = false;
            player.leftMousePressed = false;
            player.middleMousePressed = false;
            player.rightMouseReleased = false;
            player.leftMouseReleased = false;
            player.middleMouseReleased = false;
        }
        glfwSwapBuffers(window);
    }
}

void Host::cleanup() {
    runCleanupSteps();
    if (window) glfwTerminate();
}
