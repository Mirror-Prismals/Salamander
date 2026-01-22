#pragma once

// --- Core Includes ---
#include "glad/glad.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <algorithm>
#include <memory>
#include <functional>
#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>
#include <mutex>
#include <queue>
#include <atomic>
#include "json.hpp"
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <array>
#include <cstdint>
#include "Structures/VoxelWorld.h"
#include <variant>
#include "chuck.h"

// --- Forward Declarations ---
struct Entity; struct EntityInstance; struct DawContext; struct Vst3Context;
using json = nlohmann::json; using vec4 = glm::vec4;

enum class RenderBehavior { STATIC_DEFAULT, ANIMATED_WATER, ANIMATED_WIREFRAME, STATIC_BRANCH, ANIMATED_TRANSPARENT_WAVE, COUNT };
enum class BuildModeType : int { Pickup = 0, Color = 1, Texture = 2, Destroy = 3 };
struct InstanceData { glm::vec3 position; glm::vec3 color; };
struct BranchInstanceData { glm::vec3 position; float rotation; glm::vec3 color; };
class Shader { public: unsigned int ID; Shader(const char* v, const char* f); void use(); void setMat4(const std::string&n,const glm::mat4&m)const; void setVec3(const std::string&n,const glm::vec3&v)const; void setVec2(const std::string&n,const glm::vec2&v)const; void setFloat(const std::string&n,float v)const; void setInt(const std::string&n,int v)const; private: void check(unsigned int s,std::string t); };
struct SkyColorKey { float time; glm::vec3 top; glm::vec3 bottom; };
struct FaceTextureSet { int all = -1; int top = -1; int bottom = -1; int side = -1; };
struct FaceInstanceRenderData { glm::vec3 position; glm::vec3 color; int tileIndex = -1; float alpha = 1.0f; glm::vec4 ao = glm::vec4(1.0f); glm::vec2 scale = glm::vec2(1.0f); glm::vec2 uvScale = glm::vec2(1.0f); };
struct ExpanseOceanBand { float minZ = 0.0f; float maxZ = 0.0f; };
struct ExpanseConfig {
    std::string terrainWorld = "ExpanseTerrainWorld";
    std::string waterWorld = "ExpanseWaterWorld";
    std::string treesWorld = "ExpanseTreesWorld";
    int continentalSeed = 1;
    int elevationSeed = 2;
    int ridgeSeed = 3;
    float continentalScale = 100.0f;
    float elevationScale = 50.0f;
    float ridgeScale = 25.0f;
    float landThreshold = 0.48f;
    float waterSurface = 0.0f;
    float waterFloor = -4.0f;
    int minY = -1;
    float islandCenterX = 0.0f;
    float islandCenterZ = 0.0f;
    float islandRadius = 0.0f;
    float islandFalloff = 0.0f;
    float islandMaxHeight = 64.0f;
    float islandNoiseScale = 120.0f;
    float islandNoiseAmp = 24.0f;
    float beachHeight = 3.0f;
    float baseElevation = 8.0f;
    float baseRidge = 12.0f;
    float mountainElevation = 128.0f;
    float mountainRidge = 96.0f;
    float mountainMinX = -40.0f;
    float mountainMaxX = -20.0f;
    std::vector<ExpanseOceanBand> oceanBands{};
    float desertStartX = 160.0f;
    float snowStartZ = -160.0f;
    int soilDepth = 1;
    int stoneDepth = 1;
    std::string colorGrass = "Grass";
    std::string colorSand = "Sand";
    std::string colorSnow = "White";
    std::string colorSoil = "Soil";
    std::string colorStone = "Stone";
    std::string colorWater = "Water";
    std::string colorWood = "Wood";
    std::string colorLeaf = "Leaf";
    std::string colorSeabed = "Sand";
    bool loaded = false;
};

// --- CONTEXT STRUCTS ---
struct LevelContext {
    int activeWorldIndex = 0;
    std::vector<Entity> worlds;
    std::string spawnKey = "frog_spawn";
};
struct AppContext { unsigned int windowWidth = 1920; unsigned int windowHeight = 1080; };
struct WorldContext {
    std::map<std::string, glm::vec3> colorLibrary;
    std::vector<SkyColorKey> skyKeys;
    std::vector<float> cubeVertices;
    std::map<std::string, std::string> shaders;
    glm::ivec2 atlasTileSize{24, 24};
    glm::ivec2 atlasTextureSize{0, 0};
    int atlasTilesPerRow = 0;
    int atlasTilesPerCol = 0;
    std::unordered_map<std::string, FaceTextureSet> atlasMappings;
    std::vector<FaceTextureSet> prototypeTextureSets;
    ExpanseConfig expanse;
};
struct ChunkKey {
    int worldIndex = -1;
    glm::ivec3 chunkIndex{0};
    bool operator==(const ChunkKey& other) const noexcept {
        return worldIndex == other.worldIndex && chunkIndex == other.chunkIndex;
    }
};
struct ChunkKeyHash {
    std::size_t operator()(const ChunkKey& k) const noexcept {
        std::size_t hw = std::hash<int>()(k.worldIndex);
        std::size_t hx = std::hash<int>()(k.chunkIndex.x);
        std::size_t hy = std::hash<int>()(k.chunkIndex.y);
        std::size_t hz = std::hash<int>()(k.chunkIndex.z);
        return hw ^ (hx << 1) ^ (hy << 2) ^ (hz << 3);
    }
};
struct ChunkRenderBuffers {
    std::array<GLuint, static_cast<int>(RenderBehavior::COUNT)> vaos{};
    std::array<GLuint, static_cast<int>(RenderBehavior::COUNT)> instanceVBOs{};
    std::array<int, static_cast<int>(RenderBehavior::COUNT)> counts{};
    bool builtWithFaceCulling = false;
};
struct VoxelGreedyRenderBuffers {
    std::array<GLuint, 6> opaqueVaos{};
    std::array<GLuint, 6> opaqueVBOs{};
    std::array<int, 6> opaqueCounts{};
    std::array<GLuint, 6> alphaVaos{};
    std::array<GLuint, 6> alphaVBOs{};
    std::array<int, 6> alphaCounts{};
};
struct VoxelRenderContext {
    std::unordered_map<VoxelSectionKey, ChunkRenderBuffers, VoxelSectionKeyHash> renderBuffers;
    std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> renderBuffersDirty;
    bool initialized = false;
};
struct GreedyChunkData;
struct VoxelGreedyContext {
    std::unordered_map<VoxelSectionKey, GreedyChunkData, VoxelSectionKeyHash> chunks;
    std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> dirtySections;
    std::vector<GreedyChunkData> chunkPool;
    std::unordered_map<VoxelSectionKey, VoxelGreedyRenderBuffers, VoxelSectionKeyHash> renderBuffers;
    std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> renderBuffersDirty;
    bool initialized = false;
};
struct GreedyChunkData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> colors;
    std::vector<int> faceTypes;
    std::vector<int> tileIndices;
    std::vector<float> alphas;
    std::vector<glm::vec4> ao;
    std::vector<glm::vec2> scales;
    std::vector<glm::vec2> uvScales;
};
struct PlayerContext {
    float cameraYaw=-90.0f;
    float cameraPitch=0.0f;
    glm::vec3 cameraPosition=glm::vec3(0.0f,1.0f,5.0f);
    glm::vec3 prevCameraPosition=glm::vec3(0.0f,1.0f,5.0f);
    float mouseOffsetX=0.0f;
    float mouseOffsetY=0.0f;
    float lastX=1920/2.0f;
    float lastY=1080/2.0f;
    bool firstMouse=true;
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    bool rightMouseDown=false;
    bool leftMouseDown=false;
    bool middleMouseDown=false;
    bool rightMousePressed=false;
    bool leftMousePressed=false;
    bool middleMousePressed=false;
    bool rightMouseReleased=false;
    bool leftMouseReleased=false;
    bool middleMouseReleased=false;
    glm::ivec3 targetedBlock=glm::ivec3(0);
    glm::vec3 targetedBlockPosition=glm::vec3(0.0f);
    glm::vec3 targetedBlockNormal=glm::vec3(0.0f);
    bool hasBlockTarget=false;
    int targetedWorldIndex=-1;
    bool isChargingBlock=false;
    bool blockChargeReady=false;
    float blockChargeValue=0.0f;
    BuildModeType buildMode=BuildModeType::Pickup;
    int buildChannel=0;
    glm::vec3 buildColor=glm::vec3(1.0f);
    int buildTextureIndex=0;
    double scrollYOffset=0.0;
    bool isHoldingBlock=false;
    int heldPrototypeID=-1;
    glm::vec3 heldBlockColor=glm::vec3(1.0f, 1.0f, 1.0f);
    bool onGround=false;
    float verticalVelocity=0.0f;
};
struct InstanceContext { int nextInstanceID = 0; };
struct RendererContext {
    std::unique_ptr<Shader> blockShader, skyboxShader, sunMoonShader, starShader, selectionShader, hudShader, crosshairShader;
    std::unique_ptr<Shader> faceShader;
    std::unique_ptr<Shader> fontShader;
    GLuint cubeVBO;
    std::vector<GLuint> behaviorVAOs;
    std::vector<GLuint> behaviorInstanceVBOs;
    GLuint faceVAO = 0;
    GLuint faceVBO = 0;
    GLuint faceInstanceVBO = 0;
    GLuint skyboxVAO, skyboxVBO, sunMoonVAO, sunMoonVBO, starVAO, starVBO;
    GLuint selectionVAO = 0;
    GLuint selectionVBO = 0;
    int selectionVertexCount = 0;
    GLuint hudVAO = 0;
    GLuint hudVBO = 0;
    GLuint crosshairVAO = 0;
    GLuint crosshairVBO = 0;
    int crosshairVertexCount = 0;
    std::unique_ptr<Shader> uiShader;
    std::unique_ptr<Shader> uiColorShader;
    std::unique_ptr<Shader> glyphShader;
    GLuint uiVAO = 0;
    GLuint uiVBO = 0;
    GLuint uiButtonVAO = 0;
    GLuint uiButtonVBO = 0;
    GLuint uiPanelVAO = 0;
    GLuint uiPanelVBO = 0;
    GLuint uiMeterVAO = 0;
    GLuint uiMeterVBO = 0;
    GLuint uiFaderVAO = 0;
    GLuint uiFaderVBO = 0;
    GLuint uiLaneVAO = 0;
    GLuint uiLaneVBO = 0;
    GLuint uiMidiLaneVAO = 0;
    GLuint uiMidiLaneVBO = 0;
    GLuint glyphVAO = 0;
    GLuint fontVAO = 0;
    GLuint fontVBO = 0;
    // Audio ray visualization
    std::unique_ptr<Shader> audioRayShader;
    GLuint audioRayVAO = 0;
    GLuint audioRayVBO = 0;
    int audioRayVertexCount = 0;
    std::unique_ptr<Shader> audioRayVoxelShader;
    GLuint audioRayVoxelVAO = 0;
    GLuint audioRayVoxelInstanceVBO = 0;
    int audioRayVoxelCount = 0;
    // Godray resources
    GLuint godrayQuadVAO = 0;
    GLuint godrayQuadVBO = 0;
    GLuint godrayOcclusionFBO = 0;
    GLuint godrayOcclusionTex = 0;
    GLuint godrayBlurFBO = 0;
    GLuint godrayBlurTex = 0;
    std::unique_ptr<Shader> godrayRadialShader;
    std::unique_ptr<Shader> godrayCompositeShader;
    int godrayWidth = 0;
    int godrayHeight = 0;
    int godrayDownsample = 2;
    // Clouds
    GLuint cloudVAO = 0;
    GLuint cloudVBO = 0;
    std::unique_ptr<Shader> cloudShader;
    // Auroras
    struct AuroraRibbon {
        GLuint vao = 0;
        GLuint vbo = 0;
        int vertexCount = 0;
        glm::vec3 pos{0};
        float yaw = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        int palette = 0;
        float bend = 0.0f;
        float seed = 0.0f;
    };
    std::vector<AuroraRibbon> auroras;
    std::unique_ptr<Shader> auroraShader;
    GLuint atlasTexture = 0;
    glm::ivec2 atlasTextureSize{0, 0};
    glm::ivec2 atlasTileSize{24, 24};
    int atlasTilesPerRow = 0;
    int atlasTilesPerCol = 0;
};
struct AudioContext {
    jack_client_t* client = nullptr;
    std::vector<jack_port_t*> output_ports;
    std::vector<jack_port_t*> input_ports;
    std::vector<jack_port_t*> midi_input_ports;
    std::vector<float> channelGains;
    std::vector<std::string> physicalInputPorts;
    std::vector<std::string> physicalMidiInputPorts;
    jack_ringbuffer_t* ring_buffer = nullptr;
    float output_gain = 0.8f;
    std::mutex audio_state_mutex;
    int active_generators = 0;
    static const int PINK_NOISE_OCTAVES = 7;
    std::array<float, PINK_NOISE_OCTAVES> pink_rows{};
    int pink_counter = 0;
    float pink_running_sum = 0.0f;
    // ChucK integration state
    ChucK* chuck = nullptr;
    SAMPLE* chuckInput = nullptr;
    t_CKINT chuckBufferFrames = 0;
    int chuckInputChannels = 0;
    int chuckOutputChannels = 12;
    bool chuckRunning = false;
    std::vector<SAMPLE> chuckInterleavedBuffer;
    // ChucK script management
    std::string chuckMainScript = "Procedures/chuck/main.ck";
    std::string chuckNoiseScript = "Procedures/chuck/music.ck";
    int chuckMainChannel = 1; // channel index for main.ck
    int chuckNoiseChannel = 0; // channel index for pink.ck
    bool chuckBypass = false;
    bool chuckMainCompileRequested = false;
    bool chuckNoiseShouldRun = false;
    t_CKUINT chuckMainShredId = 0;
    t_CKUINT chuckNoiseShredId = 0;
    DawContext* daw = nullptr;
    struct MidiContext* midi = nullptr;
    Vst3Context* vst3 = nullptr;
    int jackOutputChannels = 16;
    int jackInputChannels = 8;
    int dawOutputStart = 12;
    float sampleRate = 44100.0f;
    std::vector<float> channelPans;
    float rayEchoDelaySeconds = 0.0f;
    float rayEchoGain = 0.0f;
    int rayEchoChannel = -1;
    size_t rayEchoWriteIndex = 0;
    std::vector<float> rayEchoBuffer;
    int rayHfChannel = -1;
    float rayHfAlpha = 0.0f;
    float rayHfState = 0.0f;
    float rayTestHfState = 0.0f;
    float rayPanStrength = 0.35f;
    float rayItdMaxMs = 0.5f;
    int rayItdChannel = -1;
    size_t rayItdWriteIndex = 0;
    std::vector<float> rayItdBuffer;
    std::string rayTestPath = "Procedures/assets/music.wav";
    std::vector<float> rayTestBuffer;
    uint32_t rayTestSampleRate = 0;
    double rayTestPos = 0.0;
    float rayTestGain = 0.0f;
    float rayTestPan = 0.0f;
    bool rayTestActive = false;
    bool rayTestLoop = true;
    bool micRayActive = false;
    float micRayGain = 0.0f;
    float micRayHfAlpha = 0.0f;
    float micRayHfState = 0.0f;
    float micRayEchoDelaySeconds = 0.0f;
    float micRayEchoGain = 0.0f;
    size_t micRayEchoWriteIndex = 0;
    std::vector<float> micRayEchoBuffer;
    std::vector<float> micCaptureBuffer;
    std::string headTrackPath;
    std::vector<float> headTrackBuffer;
    uint32_t headTrackSampleRate = 0;
    double headTrackPos = 0.0;
    float headTrackGain = 0.0f;
    bool headTrackActive = false;
    bool headTrackLoop = true;
};
struct AudioSourceState {
    bool isOccluded = false;
    float distanceGain = 1.0f;
    float preFalloffGain = 1.0f;
    float echoDelaySeconds = 0.0f;
    float echoGain = 0.0f;
    float escapeRatio = 0.0f;
    float pan = 0.0f;
    float hfAlpha = 0.0f;
    glm::vec3 sourcePos = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f);
};
struct RayDebugSegment { glm::vec3 from; glm::vec3 to; glm::vec3 color; };
struct RayTraceSourceAccum {
    int greenHits = 0;
    int greenChecks = 0;
    float orangeVisibleWallSum = 0.0f;
    int orangeVisibleChecks = 0;
    float orangeOccludedWallSum = 0.0f;
    int orangeOccludedChecks = 0;
    glm::vec3 dirSum = glm::vec3(0.0f);
    int dirCount = 0;
};
struct RayTraceRayState {
    glm::vec3 pos = glm::vec3(0.0f);
    glm::vec3 dir = glm::vec3(0.0f);
    int bounceIndex = 0;
    bool initDone = false;
    bool finished = false;
    bool escaped = false;
    bool debug = false;
    float blueLenSum = 0.0f;
    int blueLenCount = 0;
    float blueLenMax = 0.0f;
    std::vector<glm::vec3> lastDirPerSource;
    std::vector<bool> hasLastDirPerSource;
};
struct RayTraceBatch {
    bool active = false;
    bool debugActive = false;
    int totalRays = 0;
    int nextRayIndex = 0;
    int finishedRays = 0;
    int lastPublishedFinishedRays = 0;
    int escapeCount = 0;
    float blueLenSum = 0.0f;
    int blueLenCount = 0;
    float blueLenMax = 0.0f;
    glm::vec3 listenerPos = glm::vec3(0.0f);
    glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
    std::vector<int> sourceIds;
    std::vector<glm::vec3> sourcePositions;
    std::vector<RayTraceSourceAccum> accum;
    std::vector<RayTraceRayState> rays;
};
struct MicrophoneInstance {
    int worldIndex = -1;
    int instanceID = -1;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 forward = glm::vec3(0.0f, 0.0f, -1.0f);
};
struct RayTracedAudioContext {
    std::map<int, AudioSourceState> sourceStates;
    std::map<int, AudioSourceState> micSourceStates;
    int sourceStatesVersion = 0;
    int micStatesVersion = 0;
    uint64_t lastCacheEnsureFrame = 0;
    std::vector<RayDebugSegment> debugSegments;
    double lastDebugTime = -1.0;
    bool sourceCacheBuilt = false;
    std::vector<std::pair<int, int>> sourceInstances;
    RayTraceBatch batch;
    RayTraceBatch micBatch;
    double lastBatchCompleteTime = -1.0;
    double lastMicBatchCompleteTime = -1.0;
    bool micCaptureActive = false;
    bool micListenerValid = false;
    int micActiveInstanceID = -1;
    glm::vec3 micListenerPos = glm::vec3(0.0f);
    glm::vec3 micListenerForward = glm::vec3(0.0f, 0.0f, -1.0f);
    std::vector<MicrophoneInstance> microphones;
    std::unordered_map<int, glm::vec3> microphoneDirections;
};
struct HUDContext {
    bool showCharge = false;
    bool chargeReady = false;
    float chargeValue = 0.0f;
    bool buildModeActive = false;
    int buildModeType = 0;
    glm::vec3 buildPreviewColor = glm::vec3(1.0f);
    int buildChannel = 0;
    int buildPreviewTileIndex = -1;
    float displayTimer = 0.0f;
};
struct UIContext {
    bool active = false;
    bool fullscreenActive = false;
    glm::vec3 fullscreenColor = glm::vec3(0.0f);
    int activeWorldIndex = -1;
    int activeInstanceID = -1;
    bool cursorReleased = false;
    double cursorX = 0.0;
    double cursorY = 0.0;
    double cursorNDCX = 0.0;
    double cursorNDCY = 0.0;
    bool uiLeftDown = false;
    bool uiLeftPressed = false;
    bool uiLeftReleased = false;
    bool consumeClick = false;
    bool bootLoadingStarted = false;
    bool loadingActive = false;
    float loadingTimer = 0.0f;
    float loadingDuration = 0.0f;
    bool levelSwitchPending = false;
    std::string levelSwitchTarget;
    int actionDelayFrames = 0;
    std::string pendingActionType;
    std::string pendingActionKey;
    std::string pendingActionValue;
    double mainScrollDelta = 0.0;
    double panelScrollDelta = 0.0;
    double bottomPanelScrollDelta = 0.0;
    int bottomPanelPage = 0;
    int bottomPanelTrack = 0;
    bool computerCacheBuilt = false;
    std::vector<std::pair<int, int>> computerInstances;
    bool buttonCacheBuilt = false;
    LevelContext* buttonCacheLevel = nullptr;
    std::vector<EntityInstance*> buttonInstances;
};
struct UIStampingContext {
    bool cacheBuilt = false;
    LevelContext* level = nullptr;
    std::string sourceWorldName = "TrackRowWorld";
    int sourceWorldIndex = -1;
    std::string midiSourceWorldName = "MidiTrackRowWorld";
    int midiSourceWorldIndex = -1;
    int stampedRows = 0;
    float rowSpacing = 72.0f;
    float scrollY = 0.0f;
    float panelScrollY = 0.0f;
    std::vector<EntityInstance> sourceInstances;
    std::vector<float> sourceBaseY;
    std::vector<EntityInstance> midiSourceInstances;
    std::vector<float> midiSourceBaseY;
    std::vector<int> rowWorldIndices;
    std::vector<struct MirrorRowOverride> rowOverrides;
};
struct PanelRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};
struct PanelContext {
    float topState = 0.0f;
    float bottomState = 0.0f;
    float leftState = 0.0f;
    float rightState = 0.0f;
    float topTarget = 0.0f;
    float bottomTarget = 0.0f;
    float leftTarget = 0.0f;
    float rightTarget = 0.0f;
    bool topOpen = false;
    bool bottomOpen = false;
    bool leftOpen = false;
    bool rightOpen = false;
    bool upHoldActive = false;
    bool downHoldActive = false;
    bool leftHoldActive = false;
    bool rightHoldActive = false;
    float upHoldTimer = 0.0f;
    float downHoldTimer = 0.0f;
    float leftHoldTimer = 0.0f;
    float rightHoldTimer = 0.0f;
    bool upCommitted = false;
    bool downCommitted = false;
    bool leftCommitted = false;
    bool rightCommitted = false;
    float holdThreshold = 0.5f;
    float stateSpeed = 4.0f;
    PanelRect topRect;
    PanelRect bottomRect;
    PanelRect leftRect;
    PanelRect rightRect;
    PanelRect topRenderRect;
    PanelRect bottomRenderRect;
    PanelRect leftRenderRect;
    PanelRect rightRenderRect;
    PanelRect mainRect;
    bool uiWasActive = false;
    bool cacheBuilt = false;
    int panelWorldIndex = -1;
    int screenWorldIndex = -1;
    int transportWorldIndex = -1;
    int panelTopIndex = -1;
    int panelBottomIndex = -1;
    int panelLeftIndex = -1;
    int panelRightIndex = -1;
    std::vector<int> timelineLeftIndices;
    std::vector<glm::vec3> timelineLeftBasePositions;
    std::vector<int> timelineRightIndices;
    std::vector<glm::vec3> timelineRightBasePositions;
    std::vector<glm::vec3> transportBasePositions;
    float transportMinX = 0.0f;
    float transportOffsetY = 0.0f;
    float timelineOffsetY = 0.0f;
    LevelContext* cachedLevel = nullptr;
    size_t cachedWorldCount = 0;
};
struct DecibelMeterContext {
    std::vector<float> displayLevels;
    float attackSpeed = 12.0f;
    float decayPerSecond = 1.5f;
};
struct DawFaderContext {
    std::vector<float> values;
    std::vector<float> pressAnim;
    int activeIndex = -1;
    float dragOffsetY = 0.0f;
    float scrollX = 0.0f;
};
struct MirrorOverride {
    std::string matchControlId;
    std::string matchControlRole;
    std::string matchName;
    json set;
};
struct MirrorRowOverride {
    int row = -1;
    std::string matchControlId;
    std::string matchControlRole;
    std::string matchName;
    json set;
};
struct MirrorWorldInstance {
    std::string worldName;
    int repeatCount = 1;
    glm::vec3 repeatOffset{0.0f};
    std::vector<MirrorOverride> overrides;
    std::vector<MirrorRowOverride> rowOverrides;
};
struct MirrorDefinition {
    std::string name;
    float uiScale = 1.0f;
    glm::vec2 uiOffset{0.0f};
    std::vector<MirrorWorldInstance> worldInstances;
};
struct MirrorContext {
    std::vector<MirrorDefinition> mirrors;
    int activeMirrorIndex = -1;
    int activeDeviceInstanceID = -1;
    glm::vec2 uiOffset{0.0f};
    float uiScale = 1.0f;
    int expandedMirrorIndex = -1;
    bool expanded = false;
    std::vector<int> expandedWorldIndices;
    std::unordered_map<int, int> deviceMirrorIndex;
};
struct DawTrack {
    std::vector<float> audio;
    std::vector<float> pendingRecord;
    std::vector<float> waveformMin;
    std::vector<float> waveformMax;
    std::vector<glm::vec3> waveformColor;
    uint64_t waveformVersion = 0;
    uint64_t recordStartSample = 0;
    int recordArmMode = 0;
    bool recordingActive = false;
    std::atomic<bool> recordEnabled{false};
    std::atomic<int> armMode{0};
    std::atomic<bool> mute{false};
    std::atomic<bool> solo{false};
    std::atomic<int> outputBus{2};
    std::atomic<bool> useVirtualInput{false};
    std::atomic<float> meterLevel{0.0f};
    std::atomic<float> gain{1.0f};
    int physicalInputIndex = 0;
    bool clearPending = false;
    int inputIndex = 0;
    jack_ringbuffer_t* recordRing = nullptr;

    DawTrack() = default;
    DawTrack(const DawTrack&) = delete;
    DawTrack& operator=(const DawTrack&) = delete;
    DawTrack(DawTrack&& other) noexcept { *this = std::move(other); }
    DawTrack& operator=(DawTrack&& other) noexcept {
        if (this == &other) return *this;
        audio = std::move(other.audio);
        pendingRecord = std::move(other.pendingRecord);
        waveformMin = std::move(other.waveformMin);
        waveformMax = std::move(other.waveformMax);
        waveformColor = std::move(other.waveformColor);
        waveformVersion = other.waveformVersion;
        recordStartSample = other.recordStartSample;
        recordArmMode = other.recordArmMode;
        recordingActive = other.recordingActive;
        recordEnabled.store(other.recordEnabled.load(std::memory_order_relaxed), std::memory_order_relaxed);
        armMode.store(other.armMode.load(std::memory_order_relaxed), std::memory_order_relaxed);
        mute.store(other.mute.load(std::memory_order_relaxed), std::memory_order_relaxed);
        solo.store(other.solo.load(std::memory_order_relaxed), std::memory_order_relaxed);
        outputBus.store(other.outputBus.load(std::memory_order_relaxed), std::memory_order_relaxed);
        useVirtualInput.store(other.useVirtualInput.load(std::memory_order_relaxed), std::memory_order_relaxed);
        meterLevel.store(other.meterLevel.load(std::memory_order_relaxed), std::memory_order_relaxed);
        gain.store(other.gain.load(std::memory_order_relaxed), std::memory_order_relaxed);
        physicalInputIndex = other.physicalInputIndex;
        clearPending = other.clearPending;
        inputIndex = other.inputIndex;
        recordRing = other.recordRing;
        other.recordRing = nullptr;
        return *this;
    }
};
struct DawContext {
    static constexpr int kBusCount = 4;
    struct LaneEntry {
        int type = 0; // 0 = audio, 1 = midi
        int trackIndex = 0;
    };
    int trackCount = 0;
    double timelineSecondsPerScreen = 10.0;
    int64_t timelineOffsetSamples = 0;
    bool initialized = false;
    bool mirrorAvailable = false;
    std::string mirrorPath;
    std::vector<DawTrack> tracks;
    std::mutex trackMutex;
    std::atomic<bool> transportPlaying{false};
    std::atomic<bool> transportRecording{false};
    std::atomic<bool> audioThreadIdle{true};
    std::atomic<uint64_t> playheadSample{0};
    float sampleRate = 44100.0f;
    bool recordStopPending = false;
    int transportLatch = 0;
    bool uiCacheBuilt = false;
    LevelContext* uiLevel = nullptr;
    std::vector<EntityInstance*> trackInstances;
    std::vector<EntityInstance*> trackLabelInstances;
    std::vector<EntityInstance*> transportInstances;
    std::vector<EntityInstance*> outputLabelInstances;
    std::vector<EntityInstance*> timelineLabelInstances;
    std::vector<LaneEntry> laneOrder;
    int selectedLaneIndex = -1;
    int selectedLaneType = -1;
    int selectedLaneTrack = -1;
    int dragLaneIndex = -1;
    int dragLaneType = -1;
    int dragLaneTrack = -1;
    int dragDropIndex = -1;
    float dragStartY = 0.0f;
    bool dragActive = false;
    bool dragPending = false;
    int externalDropIndex = -1;
    bool externalDropActive = false;
    int externalDropType = -1;
};
struct MidiTrack {
    std::vector<float> audio;
    std::vector<float> pendingRecord;
    std::vector<float> waveformMin;
    std::vector<float> waveformMax;
    std::vector<glm::vec3> waveformColor;
    uint64_t waveformVersion = 0;
    uint64_t recordStartSample = 0;
    int recordArmMode = 0;
    bool recordingActive = false;
    std::atomic<bool> recordEnabled{false};
    std::atomic<int> armMode{0};
    std::atomic<bool> mute{false};
    std::atomic<bool> solo{false};
    std::atomic<int> outputBus{2};
    std::atomic<float> meterLevel{0.0f};
    std::atomic<float> gain{1.0f};
    int physicalInputIndex = 0;
    bool clearPending = false;
    jack_ringbuffer_t* recordRing = nullptr;

    MidiTrack() = default;
    MidiTrack(const MidiTrack&) = delete;
    MidiTrack& operator=(const MidiTrack&) = delete;
    MidiTrack(MidiTrack&& other) noexcept { *this = std::move(other); }
    MidiTrack& operator=(MidiTrack&& other) noexcept {
        if (this == &other) return *this;
        audio = std::move(other.audio);
        pendingRecord = std::move(other.pendingRecord);
        waveformMin = std::move(other.waveformMin);
        waveformMax = std::move(other.waveformMax);
        waveformColor = std::move(other.waveformColor);
        waveformVersion = other.waveformVersion;
        recordStartSample = other.recordStartSample;
        recordArmMode = other.recordArmMode;
        recordingActive = other.recordingActive;
        recordEnabled.store(other.recordEnabled.load(std::memory_order_relaxed), std::memory_order_relaxed);
        armMode.store(other.armMode.load(std::memory_order_relaxed), std::memory_order_relaxed);
        mute.store(other.mute.load(std::memory_order_relaxed), std::memory_order_relaxed);
        solo.store(other.solo.load(std::memory_order_relaxed), std::memory_order_relaxed);
        outputBus.store(other.outputBus.load(std::memory_order_relaxed), std::memory_order_relaxed);
        meterLevel.store(other.meterLevel.load(std::memory_order_relaxed), std::memory_order_relaxed);
        gain.store(other.gain.load(std::memory_order_relaxed), std::memory_order_relaxed);
        physicalInputIndex = other.physicalInputIndex;
        clearPending = other.clearPending;
        recordRing = other.recordRing;
        other.recordRing = nullptr;
        return *this;
    }
};
struct MidiContext {
    int trackCount = 0;
    float sampleRate = 44100.0f;
    bool initialized = false;
    bool recordingActive = false;
    bool recordStopPending = false;
    std::vector<MidiTrack> tracks;
    std::mutex trackMutex;
    std::vector<EntityInstance*> trackInstances;
    std::vector<EntityInstance*> trackLabelInstances;
    std::vector<EntityInstance*> outputLabelInstances;
    bool uiCacheBuilt = false;
    LevelContext* uiLevel = nullptr;
    int worldIndex = -1;
    std::vector<glm::vec3> basePositions;
    std::vector<glm::vec3> baseLabelPositions;
    float baseScrollY = 0.0f;
    bool stampCacheBuilt = false;
    std::string stampSourceWorldName = "MidiTrackRowWorld";
    int stampSourceWorldIndex = -1;
    int stampRowCount = 0;
    float stampRowSpacing = 72.0f;
    std::vector<EntityInstance> stampSourceInstances;
    std::vector<float> stampSourceBaseY;
    std::vector<int> stampRowWorldIndices;
    int selectedTrackIndex = -1;
    // Synth state lives on the audio thread.
    std::atomic<int> activeNote{-1};
    std::atomic<float> activeVelocity{0.0f};
    double phase = 0.0;
};
struct FontContext {
    std::unordered_map<std::string, std::string> variables;
    bool textCacheBuilt = false;
    std::vector<std::pair<int, int>> textInstances;
};
struct PerfContext {
    bool enabled = false;
    bool configLoaded = false;
    double reportInterval = 1.0;
    double lastReportTime = 0.0;
    int frameCount = 0;
    double hitchThresholdMs = 16.0;
    std::unordered_set<std::string> allowlist;
    std::unordered_map<std::string, double> totalsMs;
    std::unordered_map<std::string, double> maxMs;
    std::unordered_map<std::string, int> counts;
    std::unordered_map<std::string, int> hitchCounts;
};

struct BaseSystem {
    std::unique_ptr<LevelContext> level;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<WorldContext> world;
    std::unique_ptr<VoxelWorldContext> voxelWorld;
    std::unique_ptr<VoxelRenderContext> voxelRender;
    std::unique_ptr<VoxelGreedyContext> voxelGreedy;
    std::unique_ptr<PlayerContext> player;
    std::unique_ptr<InstanceContext> instance;
    std::unique_ptr<RendererContext> renderer;
    std::unique_ptr<AudioContext> audio;
    std::unique_ptr<Vst3Context> vst3;
    std::unique_ptr<RayTracedAudioContext> rayTracedAudio;
    std::unique_ptr<HUDContext> hud;
    std::unique_ptr<UIContext> ui;
    std::unique_ptr<UIStampingContext> uiStamp;
    std::unique_ptr<PanelContext> panel;
    std::unique_ptr<DecibelMeterContext> decibelMeter;
    std::unique_ptr<DawFaderContext> fader;
    std::unique_ptr<MirrorContext> mirror;
    std::unique_ptr<FontContext> font;
    std::unique_ptr<DawContext> daw;
    std::unique_ptr<MidiContext> midi;
    std::unique_ptr<PerfContext> perf;
    uint64_t frameIndex = 0;
    std::string gamemode = "creative";
    std::map<std::string, std::variant<bool, std::string>>* registry = nullptr;
    bool* reloadRequested = nullptr;
    std::string* reloadTarget = nullptr;
};
using SystemFunction = std::function<void(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*)>;

// --- SYSTEM FUNCTION DECLARATIONS ---
namespace HostLogic { void LoadProcedureAssets(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); EntityInstance CreateInstance(BaseSystem&, const std::vector<Entity>&, const std::string&, glm::vec3, glm::vec3); EntityInstance CreateInstance(BaseSystem&, int, glm::vec3, glm::vec3); glm::vec3 hexToVec3(const std::string& hex); const Entity* findPrototype(const std::string&, const std::vector<Entity>&); }
namespace RayTracedAudioSystemLogic { void ProcessRayTracedAudio(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace PinkNoiseSystemLogic { void ProcessPinkNoiseAudicle(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace AudioSystemLogic { void InitializeAudio(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void CleanupAudio(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace SoundtrackSystemLogic { void UpdateSoundtracks(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace AudicleSystemLogic { void ProcessAudicles(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace SpawnSystemLogic { void SetPlayerSpawn(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace CameraSystemLogic { void UpdateCameraMatrices(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace KeyboardInputSystemLogic { void ProcessKeyboardInput(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace MouseInputSystemLogic { void UpdateCameraRotationFromMouse(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace UAVSystemLogic { void ProcessUAVMovement(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace WalkModeSystemLogic { void ProcessWalkMovement(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace GravitySystemLogic { void ApplyGravity(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace CollisionSystemLogic { void ResolveCollisions(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace VolumeFillSystemLogic { void ProcessVolumeFills(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace RenderSystemLogic { void InitializeRenderer(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void RenderScene(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void CleanupRenderer(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace TerrainSystemLogic { void GenerateTerrain(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace BlockSelectionSystemLogic {
    void UpdateBlockSelection(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*);
    bool HasBlockAt(BaseSystem&, const std::vector<Entity>&, int, const glm::vec3&);
    void AddBlockToCache(BaseSystem&, std::vector<Entity>&, int, const glm::vec3&, int prototypeID);
    void RemoveBlockFromCache(BaseSystem&, const std::vector<Entity>&, int, const glm::vec3&);
    void EnsureAllCaches(BaseSystem&, const std::vector<Entity>&);
    bool SampleBlockDamping(BaseSystem&, const glm::ivec3&, float&);
}
namespace StructureCaptureSystemLogic { void ProcessStructureCapture(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void NotifyBlockChanged(BaseSystem&, int, const glm::vec3&); }
namespace StructurePlacementSystemLogic { void ProcessStructurePlacement(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace BlockChargeSystemLogic { void UpdateBlockCharge(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace HUDSystemLogic { void UpdateHUD(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace MidiStateSystemLogic { void UpdateMidiState(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void CleanupMidiState(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); bool InsertTrackAt(BaseSystem&, int trackIndex); bool RemoveTrackAt(BaseSystem&, int trackIndex); bool MoveTrack(BaseSystem&, int fromIndex, int toIndex); }
namespace MidiLaneSystemLogic { void UpdateMidiLane(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace BuildSystemLogic { void UpdateBuildMode(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace UIScreenSystemLogic { void UpdateUIScreen(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace RegistryEditorSystemLogic { void UpdateRegistry(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace MirrorSystemLogic { void UpdateMirrors(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace BootSequenceSystemLogic { void UpdateBootSequence(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace ComputerCursorSystemLogic { void UpdateComputerCursor(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace ButtonSystemLogic { void UpdateButtons(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); bool GetButtonToggled(int instanceID); void SetButtonToggled(int instanceID, bool toggled); }
namespace PanelSystemLogic { void UpdatePanels(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void RenderPanels(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace UIStampingSystemLogic { void UpdateUIStamping(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace GlyphSystemLogic { void UpdateGlyphs(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace DecibelMeterSystemLogic { void UpdateDecibelMeters(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void RenderDecibelMeters(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace DawFaderSystemLogic { void UpdateDawFaders(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void RenderDawFaders(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace FontSystemLogic { void UpdateFonts(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void CleanupFonts(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace DebugHudSystemLogic { void UpdateDebugHud(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace DebugWireframeSystemLogic { void UpdateDebugWireframe(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace PerfSystemLogic { void UpdatePerf(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace ChucKSystemLogic { void UpdateChucK(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace Vst3SystemLogic { void InitializeVst3(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void UpdateVst3(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void CleanupVst3(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace Vst3BrowserSystemLogic { void UpdateVst3Browser(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace AudioRayVisualizerSystemLogic { void UpdateAudioRayVisualizer(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace DawStateSystemLogic { void UpdateDawState(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void CleanupDawState(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); bool InsertTrackAt(BaseSystem&, int trackIndex); bool RemoveTrackAt(BaseSystem&, int trackIndex); bool MoveTrack(BaseSystem&, int fromIndex, int toIndex); }
namespace DawLaneSystemLogic { void UpdateDawLanes(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace SkyboxSystemLogic { void getCurrentSkyColors(float, const std::vector<SkyColorKey>&, glm::vec3&, glm::vec3&); void RenderSkyAndCelestials(BaseSystem&, const std::vector<Entity>&, const std::vector<glm::vec3>&, float, float, const glm::mat4&, const glm::mat4&, const glm::vec3&, glm::vec3&); }
namespace CloudSystemLogic { void RenderClouds(BaseSystem&, const glm::vec3& lightDir, float time); }
namespace AuroraSystemLogic { void RenderAuroras(BaseSystem&, float time, const glm::mat4& view, const glm::mat4& projection); }
namespace BlockTextureSystemLogic { void LoadBlockTextures(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }

class Host {
private:
    GLFWwindow* window = nullptr; BaseSystem baseSystem; std::vector<Entity> entityPrototypes; std::map<std::string, std::variant<bool, std::string>> registry;
    std::map<std::string, SystemFunction> functionRegistry;
    bool reloadRequested = false;
    std::string reloadTarget;
    struct SystemStep { std::string name; std::vector<std::string> dependencies; };
    std::vector<SystemStep> initFunctions, updateFunctions, cleanupFunctions;
    float deltaTime = 0.0f, lastFrame = 0.0f;
    bool rendererInitialized = false;
    bool audioInitialized = false;
    void loadRegistry(); void loadSystems(); bool checkDependencies(const std::vector<std::string>& deps);
    void registerSystemFunctions(); void init(); void mainLoop(); void cleanup();
    void reloadLevel(const std::string& levelName);
    void runCleanupSteps();
    void PopulateWorldsFromLevel();
public:
    void run();
    void processMouseInput(double xpos, double ypos);
    void processScroll(double xoffset, double yoffset);
};
