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
#include <variant>
#include "chuck.h"

// --- Forward Declarations ---
struct Entity; struct EntityInstance; struct DawContext;
using json = nlohmann::json; using vec4 = glm::vec4;

enum class RenderBehavior { STATIC_DEFAULT, ANIMATED_WATER, ANIMATED_WIREFRAME, STATIC_BRANCH, ANIMATED_TRANSPARENT_WAVE, COUNT };
enum class BuildModeType : int { Pickup = 0, Color = 1, Texture = 2, Destroy = 3 };
struct InstanceData { glm::vec3 position; glm::vec3 color; };
struct BranchInstanceData { glm::vec3 position; float rotation; glm::vec3 color; };
class Shader { public: unsigned int ID; Shader(const char* v, const char* f); void use(); void setMat4(const std::string&n,const glm::mat4&m)const; void setVec3(const std::string&n,const glm::vec3&v)const; void setVec2(const std::string&n,const glm::vec2&v)const; void setFloat(const std::string&n,float v)const; void setInt(const std::string&n,int v)const; private: void check(unsigned int s,std::string t); };
struct SkyColorKey { float time; glm::vec3 top; glm::vec3 bottom; };
struct FaceTextureSet { int all = -1; int top = -1; int bottom = -1; int side = -1; };
struct FaceInstanceRenderData { glm::vec3 position; glm::vec3 color; int tileIndex = -1; float alpha = 1.0f; glm::vec4 ao = glm::vec4(1.0f); };
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
struct ChunkData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> colors;
    std::vector<float> rotations;
    std::vector<int> prototypeIDs;
    std::vector<int> worldIndices;
};
struct ChunkRenderBuffers {
    std::array<GLuint, static_cast<int>(RenderBehavior::COUNT)> vaos{};
    std::array<GLuint, static_cast<int>(RenderBehavior::COUNT)> instanceVBOs{};
    std::array<int, static_cast<int>(RenderBehavior::COUNT)> counts{};
    bool builtWithFaceCulling = false;
};
struct ChunkContext {
    glm::ivec3 chunkSize{24, 12, 24};
    int renderDistanceChunks = 6;
    int unloadDistanceChunks = 7; // default a bit beyond render distance
    std::unordered_map<ChunkKey, ChunkData, ChunkKeyHash> chunks;
    std::unordered_set<ChunkKey, ChunkKeyHash> dirtyChunks;
    std::unordered_map<ChunkKey, ChunkRenderBuffers, ChunkKeyHash> renderBuffers;
    std::unordered_set<ChunkKey, ChunkKeyHash> renderBuffersDirty;
    std::unordered_map<ChunkKey, std::vector<size_t>, ChunkKeyHash> chunkInstanceLUT;
    std::vector<bool> worldHasChunkable;
    std::vector<bool> worldHasNonChunkable;
    bool chunkIndexDirty = true;
    bool renderBuffersDirtyAll = true;
    bool renderBuffersFaceState = false;
    bool initialized = false;
};
struct FaceChunkData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> colors;
    std::vector<int> faceTypes;
    std::vector<int> tileIndices;
    std::vector<float> alphas;
    std::vector<glm::vec4> ao;
};
struct FaceContext {
    std::unordered_map<ChunkKey, FaceChunkData, ChunkKeyHash> faces;
    std::unordered_set<ChunkKey, ChunkKeyHash> dirtyChunks;
    bool initialized = false;
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
    GLuint uiVAO = 0;
    GLuint uiVBO = 0;
    GLuint uiButtonVAO = 0;
    GLuint uiButtonVBO = 0;
    GLuint uiLaneVAO = 0;
    GLuint uiLaneVBO = 0;
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
    std::vector<float> channelGains;
    std::vector<std::string> physicalInputPorts;
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
    int stampedRows = 0;
    float rowSpacing = 72.0f;
    float scrollY = 0.0f;
    std::vector<EntityInstance> sourceInstances;
    std::vector<float> sourceBaseY;
    std::vector<int> rowWorldIndices;
};
struct DawTrack {
    std::vector<float> audio;
    std::vector<float> pendingRecord;
    std::vector<float> waveformMin;
    std::vector<float> waveformMax;
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
    int physicalInputIndex = 0;
    bool clearPending = false;
    int inputIndex = 0;
    jack_ringbuffer_t* recordRing = nullptr;
};
struct DawContext {
    static constexpr int kTrackCount = 8;
    static constexpr int kBusCount = 4;
    int trackCount = kTrackCount;
    bool initialized = false;
    bool mirrorAvailable = false;
    std::string mirrorPath;
    std::array<DawTrack, kTrackCount> tracks;
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
    std::vector<EntityInstance*> transportInstances;
    std::vector<EntityInstance*> outputLabelInstances;
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
    std::unique_ptr<ChunkContext> chunk;
    std::unique_ptr<FaceContext> face;
    std::unique_ptr<PlayerContext> player;
    std::unique_ptr<InstanceContext> instance;
    std::unique_ptr<RendererContext> renderer;
    std::unique_ptr<AudioContext> audio;
    std::unique_ptr<RayTracedAudioContext> rayTracedAudio;
    std::unique_ptr<HUDContext> hud;
    std::unique_ptr<UIContext> ui;
    std::unique_ptr<UIStampingContext> uiStamp;
    std::unique_ptr<FontContext> font;
    std::unique_ptr<DawContext> daw;
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
namespace ChunkSystemLogic { void UpdateChunks(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void MarkChunkDirty(BaseSystem&, int, const glm::vec3&); }
namespace FaceCullingSystemLogic { void UpdateFaces(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void MarkChunkDirty(BaseSystem&, const ChunkKey&); }
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
namespace BuildSystemLogic { void UpdateBuildMode(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace UIScreenSystemLogic { void UpdateUIScreen(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace RegistryEditorSystemLogic { void UpdateRegistry(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace BootSequenceSystemLogic { void UpdateBootSequence(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace ComputerCursorSystemLogic { void UpdateComputerCursor(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace ButtonSystemLogic { void UpdateButtons(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); bool GetButtonToggled(int instanceID); void SetButtonToggled(int instanceID, bool toggled); }
namespace UIStampingSystemLogic { void UpdateUIStamping(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace GlyphSystemLogic { void UpdateGlyphs(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace FontSystemLogic { void UpdateFonts(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void CleanupFonts(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace DebugHudSystemLogic { void UpdateDebugHud(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace DebugWireframeSystemLogic { void UpdateDebugWireframe(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace PerfSystemLogic { void UpdatePerf(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace ChucKSystemLogic { void UpdateChucK(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace AudioRayVisualizerSystemLogic { void UpdateAudioRayVisualizer(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace DawStateSystemLogic { void UpdateDawState(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void CleanupDawState(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
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
