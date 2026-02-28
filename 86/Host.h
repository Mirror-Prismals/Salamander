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
enum class BuildModeType : int { Pickup = 0, Color = 1, Texture = 2, Destroy = 3, Fishing = 4, Bouldering = 5 };
enum class BlockChargeAction : int { None = 0, Pickup = 1, Destroy = 2, Fishing = 3, BoulderPrimary = 4, BoulderSecondary = 5 };
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
struct LeyPlate {
    glm::vec2 seed = glm::vec2(0.0f);
    glm::vec2 velocity = glm::vec2(0.0f);
};
struct LeyLineContext {
    bool enabled = true;
    bool loaded = false;
    bool precomputeField = true;
    bool compressionOnly = true;
    int seed = 1337;
    int plateCount = 12;
    int blendCount = 2;
    float domainMinX = -1024.0f;
    float domainMaxX = 1024.0f;
    float domainMinZ = -1024.0f;
    float domainMaxZ = 1024.0f;
    float sampleStep = 8.0f;
    float plateInfluenceRadius = 64.0f;
    float domeRadius = 64.0f;
    float domeHeight = 5.0f;
    float baseHeight = 10.0f;
    float stressHeightScale = 15.0f;
    float opposingCompressionScale = 0.5f;
    float noiseScale = 0.05f;
    int noiseOctaves = 4;
    float noisePersistence = 0.5f;
    float noiseLacunarity = 2.0f;
    float upliftGain = 8.0f;
    float upliftMax = 28.0f;
    float stressClamp = 10.0f;
    int fieldWidth = 0;
    int fieldHeight = 0;
    std::vector<LeyPlate> plates;
    std::vector<float> stressField;
    std::vector<float> upliftField;
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
    LeyLineContext leyLines;
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
    BlockChargeAction blockChargeAction=BlockChargeAction::None;
    bool blockChargeControlsSwapped=false;
    BuildModeType buildMode=BuildModeType::Pickup;
    int buildChannel=0;
    glm::vec3 buildColor=glm::vec3(1.0f);
    int buildTextureIndex=0;
    double scrollYOffset=0.0;
    bool isHoldingBlock=false;
    int heldPrototypeID=-1;
    glm::vec3 heldBlockColor=glm::vec3(1.0f, 1.0f, 1.0f);
    bool hatchetHeld = false;
    std::array<int, 5> hatchetInventoryByMaterial = {0, 0, 0, 0, 0};
    int hatchetSelectedMaterial = 0;
    int hatchetInventoryCount = 0;
    bool hatchetPlacedInWorld = false;
    glm::ivec3 hatchetPlacedCell = glm::ivec3(0);
    int hatchetPlacedWorldIndex = -1;
    int hatchetPlacedMaterial = 0;
    glm::vec3 hatchetPlacedPosition = glm::vec3(0.0f);
    glm::vec3 hatchetPlacedNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 hatchetPlacedDirection = glm::vec3(1.0f, 0.0f, 0.0f);
    bool onGround=false;
    float verticalVelocity=0.0f;
    bool boulderPrimaryLatched = false;
    bool boulderSecondaryLatched = false;
    glm::vec3 boulderPrimaryAnchor = glm::vec3(0.0f);
    glm::vec3 boulderSecondaryAnchor = glm::vec3(0.0f);
    glm::vec3 boulderPrimaryNormal = glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 boulderSecondaryNormal = glm::vec3(0.0f, 0.0f, 1.0f);
    glm::ivec3 boulderPrimaryCell = glm::ivec3(0);
    glm::ivec3 boulderSecondaryCell = glm::ivec3(0);
    float boulderPrimaryRestLength = 0.0f;
    float boulderSecondaryRestLength = 0.0f;
    int boulderPrimaryWorldIndex = -1;
    int boulderSecondaryWorldIndex = -1;
    glm::vec3 boulderLaunchVelocity = glm::vec3(0.0f);
};
struct InstanceContext { int nextInstanceID = 0; };
struct RendererContext {
    std::unique_ptr<Shader> blockShader, skyboxShader, sunMoonShader, starShader, selectionShader, hudShader, crosshairShader, colorEmotionShader;
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
    GLuint colorEmotionVAO = 0;
    GLuint colorEmotionVBO = 0;
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
    GLuint uiPianoRollVAO = 0;
    GLuint uiPianoRollVBO = 0;
    GLuint glyphVAO = 0;
    GLuint fontVAO = 0;
    GLuint fontVBO = 0;
    // Audio ray visualization
    std::unique_ptr<Shader> audioRayShader;
    GLuint audioRayVAO = 0;
    GLuint audioRayVBO = 0;
    int audioRayVertexCount = 0;
    GLuint leyLineDebugVAO = 0;
    GLuint leyLineDebugVBO = 0;
    int leyLineDebugVertexCount = 0;
    // Fishing wireframe overlays (fish shadows + bobber + ripples)
    GLuint fishingVAO = 0;
    GLuint fishingVBO = 0;
    int fishingVertexCount = 0;
    // Procedural ore gem drops
    GLuint gemVAO = 0;
    GLuint gemVBO = 0;
    int gemVertexCount = 0;
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
    std::array<GLuint, 3> grassTextures{0, 0, 0};
    int grassTextureCount = 0;
    std::array<GLuint, 3> shortGrassTextures{0, 0, 0};
    int shortGrassTextureCount = 0;
    std::array<GLuint, 4> oreTextures{0, 0, 0, 0};
    int oreTextureCount = 0;
    std::array<GLuint, 2> terrainTextures{0, 0};
    int terrainTextureCount = 0;
    GLuint waterOverlayTexture = 0;
};
struct GameplaySfxEvent {
    uint16_t clipIndex = 0;
    float gain = 1.0f;
};
struct GameplaySfxVoice {
    int clipIndex = -1;
    double position = 0.0;
    float gain = 1.0f;
    bool active = false;
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
    std::string chuckMainScript = "Procedures/chuck/head.ck";
    std::string chuckHeadScript = "Procedures/chuck/main.ck";
    std::string chuckNoiseScript = "Procedures/chuck/music.ck";
    int chuckMainChannel = 2; // channel index for head.ck
    int chuckHeadChannel = 0; // channel index for main.ck (ray-traced head source)
    int chuckNoiseChannel = 0; // channel index for pink.ck
    bool chuckBypass = false;
    bool chuckMainCompileRequested = false;
    bool chuckHeadCompileRequested = false;
    bool chuckNoiseShouldRun = false;
    t_CKUINT chuckMainShredId = 0;
    t_CKUINT chuckHeadShredId = 0;
    t_CKUINT chuckNoiseShredId = 0;
    std::atomic<int> chuckMainActiveShredCount{0};
    std::atomic<bool> chuckMainHasActiveShreds{false};
    std::atomic<int> chuckHeadActiveShredCount{0};
    std::atomic<bool> chuckHeadHasActiveShreds{false};
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
    bool headRayActive = false;
    float headRayGain = 0.0f;
    float headRayPan = 0.0f;
    float headRayHfAlpha = 0.0f;
    float headRayHfState = 0.0f;
    std::atomic<float> headUnderwaterMix{0.0f};
    std::atomic<float> headUnderwaterLowpassHz{500.0f};
    std::atomic<float> headUnderwaterLowpassStrength{1.0f};
    float headUnderwaterLpState = 0.0f;
    float headRayEchoDelaySeconds = 0.0f;
    float headRayEchoGain = 0.0f;
    size_t headRayEchoWriteIndex = 0;
    std::vector<float> headRayEchoBuffer;
    size_t headRayItdWriteIndex = 0;
    std::vector<float> headRayItdBuffer;
    std::string headTrackPath;
    std::vector<float> headTrackBuffer;
    uint32_t headTrackSampleRate = 0;
    double headTrackPos = 0.0;
    float headTrackGain = 0.0f;
    bool headTrackActive = false;
    bool headTrackLoop = true;
    std::atomic<float> chuckMainLevelGain{0.0f};
    std::atomic<float> soundtrackLevelGain{0.0f};
    std::atomic<float> speakerBlockLevelGain{0.0f};
    std::atomic<float> playerHeadSpeakerLevelGain{1.0f};
    std::atomic<float> chuckMainMeterLevel{0.0f};
    std::atomic<float> soundtrackMeterLevel{0.0f};
    std::atomic<float> speakerBlockMeterLevel{0.0f};
    std::atomic<float> playerHeadSpeakerMeterLevel{0.0f};
    std::atomic<bool> offlineRenderMute{false};
    std::vector<std::string> gameplaySfxNames;
    std::vector<std::vector<float>> gameplaySfxBuffers;
    std::vector<uint32_t> gameplaySfxSampleRates;
    std::vector<GameplaySfxVoice> gameplaySfxVoices;
    jack_ringbuffer_t* gameplaySfxEventRing = nullptr;
    std::atomic<float> gameplaySfxMasterGain{1.0f};
    std::atomic<bool> gameplaySfxEnabled{true};
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
struct ColorEmotionContext {
    bool enabled = true;
    bool active = false;
    bool underwater = false;
    bool fishingDirectionHint = false;
    int mode = 0;
    int chargeAction = 0;
    float chargeValue = 0.0f;
    float underwaterMix = 0.0f;
    float underwaterDepth = 0.0f;
    float underwaterSurfaceY = 0.0f;
    float underwaterLineUV = 0.62f;
    float underwaterLineStrength = 0.0f;
    float underwaterHazeStrength = 0.0f;
    float fishingDirectionAngle = 1.5707963f;
    float fishingDirectionStrength = 0.0f;
    float timeSeconds = 0.0f;
    float intensity = 0.0f;
    float pulse = 0.0f;
    glm::vec3 color = glm::vec3(0.0f);
};
struct FishingShadowState {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec2 velocity = glm::vec2(1.0f, 0.0f);
    glm::vec2 facing = glm::vec2(1.0f, 0.0f);
    glm::vec2 schoolOffset = glm::vec2(0.0f);
    float orbitRadius = 3.0f;
    float length = 1.0f;
    float thickness = 0.25f;
    float speed = 1.0f;
    float phase = 0.0f;
    float depth = 0.06f;
};
struct FishingContext {
    bool rodPlacedInWorld = false;
    glm::vec3 rodPlacedPosition = glm::vec3(0.0f);
    glm::vec3 rodPlacedNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 rodPlacedDirection = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::ivec3 rodPlacedCell = glm::ivec3(0);
    int rodPlacedWorldIndex = -1;
    bool bobberActive = false;
    glm::vec3 bobberPosition = glm::vec3(0.0f);
    glm::vec3 bobberVelocity = glm::vec3(0.0f);
    bool bobberInWater = false;
    bool dailySchoolValid = false;
    int dailySchoolDayKey = -1;
    glm::vec3 dailySchoolPosition = glm::vec3(0.0f);
    int dailyFishRemaining = 0;
    float spookTimer = 0.0f;
    float ripplePulse = 0.0f;
    float rippleTime = 0.0f;
    bool hooked = false;
    bool biteActive = false;
    float biteTimer = 0.0f;
    int interestedFishIndex = -1;
    int hookedFishIndex = -1;
    int nibbleCount = 0;
    int nibbleTarget = 0;
    float nibbleTimer = 0.0f;
    uint32_t rngState = 0x12345678u;
    std::vector<FishingShadowState> fishShadows;
    std::vector<glm::vec3> lineAnchors;
    std::vector<glm::vec3> linePath;
    float rodChargeBend = 0.0f;
    float rodCastKick = 0.0f;
    float reelRemainingDistance = 0.0f;
    float reelCastDistance = 0.0f;
    bool previewBobberInitialized = false;
    glm::vec3 previewBobberPosition = glm::vec3(0.0f);
    glm::vec3 previewBobberVelocity = glm::vec3(0.0f);
    std::string lastCatchText;
    float lastCatchTimer = 0.0f;
};
struct GemDropNode {
    glm::vec3 offset = glm::vec3(0.0f);
    glm::vec3 color = glm::vec3(1.0f);
    glm::vec3 edgeColor = glm::vec3(0.0f);
};
struct GemDropState {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    float age = 0.0f;
    float lifetime = 10.0f;
    float spin = 0.0f;
    float spinSpeed = 0.0f;
    float scale = 1.0f;
    float collisionRadius = 0.12f;
    int kind = -1;
    std::vector<GemDropNode> nodes;
    std::vector<glm::ivec2> edges;
    std::vector<glm::ivec3> triangles;
    std::vector<glm::ivec3> voxelCells;
};
struct GemContext {
    uint32_t rngState = 0xA17C9E3Du;
    std::vector<GemDropState> drops;
    bool blockModeHoldingGem = false;
    GemDropState heldDrop;
    std::string lastDropText;
    float lastDropTimer = 0.0f;
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
    std::string automationSourceWorldName = "AutomationTrackRowWorld";
    int automationSourceWorldIndex = -1;
    int stampedRows = 0;
    float rowSpacing = 72.0f;
    float scrollY = 0.0f;
    float panelScrollY = 0.0f;
    std::vector<EntityInstance> sourceInstances;
    std::vector<float> sourceBaseY;
    std::vector<EntityInstance> midiSourceInstances;
    std::vector<float> midiSourceBaseY;
    std::vector<EntityInstance> automationSourceInstances;
    std::vector<float> automationSourceBaseY;
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
    std::array<float, 4> masterDisplayLevels{0.0f, 0.0f, 0.0f, 0.0f};
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
struct DawClip {
    int audioId = -1;
    uint64_t startSample = 0;
    uint64_t length = 0;
    uint64_t sourceOffset = 0;
    int takeId = -1;
};
struct DawClipAudio {
    int channels = 1;
    std::vector<float> left;
    std::vector<float> right;
};
struct MidiNote {
    int pitch = 0;
    uint64_t startSample = 0;
    uint64_t length = 0;
    float velocity = 1.0f;
};
struct MidiClip {
    uint64_t startSample = 0;
    uint64_t length = 0;
    std::vector<MidiNote> notes;
    int takeId = -1;
};
struct AutomationPoint {
    uint64_t offsetSample = 0;
    float value = 0.5f;
};
struct AutomationClip {
    uint64_t startSample = 0;
    uint64_t length = 0;
    std::vector<AutomationPoint> points;
    int takeId = -1;
};
struct AutomationTrack {
    std::vector<AutomationClip> clips;
    bool clearPending = false;
    int armMode = 0;
    int targetLaneType = 0;     // 0 audio, 1 midi
    int targetLaneTrack = 0;    // index in target lane type
    int targetDeviceSlot = 0;   // 0..N-1 in target lane device list
    int targetParameterSlot = 0;// 0..N-1 in target device parameter list
    int targetParameterId = -1;
    std::string targetLaneLabel = "AUD1";
    std::string targetDeviceLabel = "NONE";
    std::string targetParameterLabel = "NONE";
};
struct DawTrack {
    std::vector<float> audio;
    std::vector<float> audioRight;
    std::vector<float> pendingRecord;
    std::vector<float> pendingRecordRight;
    std::vector<DawClip> clips;
    std::vector<DawClip> loopTakeClips;
    std::vector<float> waveformMin;
    std::vector<float> waveformMax;
    std::vector<float> waveformMinRight;
    std::vector<float> waveformMaxRight;
    std::vector<glm::vec3> waveformColor;
    uint64_t waveformVersion = 0;
    uint64_t recordStartSample = 0;
    bool recordLoopCapture = false;
    uint64_t recordLoopStartSample = 0;
    uint64_t recordLoopEndSample = 0;
    uint64_t loopTakeRangeStartSample = 0;
    uint64_t loopTakeRangeLength = 0;
    int recordArmMode = 0;
    bool recordingActive = false;
    int activeLoopTakeIndex = -1;
    bool takeStackExpanded = false;
    int nextTakeId = 1;
    std::atomic<bool> recordEnabled{false};
    std::atomic<int> armMode{0};
    std::atomic<bool> mute{false};
    std::atomic<bool> solo{false};
    std::atomic<int> outputBus{2};
    std::atomic<int> outputBusL{2};
    std::atomic<int> outputBusR{2};
    std::atomic<bool> useVirtualInput{false};
    std::atomic<float> meterLevel{0.0f};
    std::atomic<float> gain{1.0f};
    int physicalInputIndex = 0;
    bool stereoInputPair12 = false;
    bool clearPending = false;
    int inputIndex = 0;
    jack_ringbuffer_t* recordRing = nullptr;
    jack_ringbuffer_t* recordRingRight = nullptr;

    DawTrack() = default;
    DawTrack(const DawTrack&) = delete;
    DawTrack& operator=(const DawTrack&) = delete;
    DawTrack(DawTrack&& other) noexcept { *this = std::move(other); }
    DawTrack& operator=(DawTrack&& other) noexcept {
        if (this == &other) return *this;
        audio = std::move(other.audio);
        audioRight = std::move(other.audioRight);
        pendingRecord = std::move(other.pendingRecord);
        pendingRecordRight = std::move(other.pendingRecordRight);
        clips = std::move(other.clips);
        loopTakeClips = std::move(other.loopTakeClips);
        waveformMin = std::move(other.waveformMin);
        waveformMax = std::move(other.waveformMax);
        waveformMinRight = std::move(other.waveformMinRight);
        waveformMaxRight = std::move(other.waveformMaxRight);
        waveformColor = std::move(other.waveformColor);
        waveformVersion = other.waveformVersion;
        recordStartSample = other.recordStartSample;
        recordLoopCapture = other.recordLoopCapture;
        recordLoopStartSample = other.recordLoopStartSample;
        recordLoopEndSample = other.recordLoopEndSample;
        loopTakeRangeStartSample = other.loopTakeRangeStartSample;
        loopTakeRangeLength = other.loopTakeRangeLength;
        recordArmMode = other.recordArmMode;
        recordingActive = other.recordingActive;
        activeLoopTakeIndex = other.activeLoopTakeIndex;
        takeStackExpanded = other.takeStackExpanded;
        nextTakeId = other.nextTakeId;
        recordEnabled.store(other.recordEnabled.load(std::memory_order_relaxed), std::memory_order_relaxed);
        armMode.store(other.armMode.load(std::memory_order_relaxed), std::memory_order_relaxed);
        mute.store(other.mute.load(std::memory_order_relaxed), std::memory_order_relaxed);
        solo.store(other.solo.load(std::memory_order_relaxed), std::memory_order_relaxed);
        outputBus.store(other.outputBus.load(std::memory_order_relaxed), std::memory_order_relaxed);
        outputBusL.store(other.outputBusL.load(std::memory_order_relaxed), std::memory_order_relaxed);
        outputBusR.store(other.outputBusR.load(std::memory_order_relaxed), std::memory_order_relaxed);
        useVirtualInput.store(other.useVirtualInput.load(std::memory_order_relaxed), std::memory_order_relaxed);
        meterLevel.store(other.meterLevel.load(std::memory_order_relaxed), std::memory_order_relaxed);
        gain.store(other.gain.load(std::memory_order_relaxed), std::memory_order_relaxed);
        physicalInputIndex = other.physicalInputIndex;
        stereoInputPair12 = other.stereoInputPair12;
        clearPending = other.clearPending;
        inputIndex = other.inputIndex;
        recordRing = other.recordRing;
        recordRingRight = other.recordRingRight;
        other.recordRing = nullptr;
        other.recordRingRight = nullptr;
        return *this;
    }
};

struct DawThemePreset {
    std::string name = "Default";
    glm::vec4 background = glm::vec4(0.90f, 0.88f, 0.83f, 0.85f);
    glm::vec4 panel = glm::vec4(0.90f, 0.88f, 0.83f, 0.80f);
    glm::vec4 button = glm::vec4(0.90f, 0.88f, 0.83f, 0.85f);
    glm::vec4 pianoRoll = glm::vec4(0.00f, 0.12f, 0.12f, 1.00f);
    glm::vec4 pianoRollAccent = glm::vec4(0.00f, 0.20f, 0.20f, 1.00f);
    glm::vec4 lane = glm::vec4(230.0f / 255.0f, 224.0f / 255.0f, 212.0f / 255.0f, 1.00f);
    bool isBuiltin = false;
};

struct DawContext {
    static constexpr int kBusCount = 4;
    struct LaneEntry {
        int type = 0; // 0 = audio, 1 = midi, 2 = automation
        int trackIndex = 0;
    };
    int trackCount = 0;
    int automationTrackCount = 0;
    double timelineSecondsPerScreen = 10.0;
    int64_t timelineOffsetSamples = 0;
    int64_t timelineZeroSample = 0;
    bool initialized = false;
    bool mirrorAvailable = false;
    std::string mirrorPath;
    std::vector<DawTrack> tracks;
    std::vector<AutomationTrack> automationTracks;
    std::vector<DawClipAudio> clipAudio;
    std::mutex trackMutex;
    std::atomic<bool> transportPlaying{false};
    std::atomic<bool> transportRecording{false};
    std::atomic<bool> audioThreadIdle{true};
    std::atomic<uint64_t> playheadSample{0};
    float sampleRate = 44100.0f;
    std::array<std::atomic<float>, kBusCount> masterBusLevels;
    bool recordStopPending = false;
    int transportLatch = 0;
    bool uiCacheBuilt = false;
    LevelContext* uiLevel = nullptr;
    std::vector<EntityInstance*> trackInstances;
    std::vector<EntityInstance*> trackLabelInstances;
    std::vector<EntityInstance*> transportInstances;
    std::vector<EntityInstance*> tempoInstances;
    std::vector<EntityInstance*> loopInstances;
    std::vector<EntityInstance*> exportInstances;
    std::vector<EntityInstance*> settingsInstances;
    std::vector<EntityInstance*> outputLabelInstances;
    std::vector<EntityInstance*> timelineLabelInstances;
    std::vector<EntityInstance*> timelineBarLabelInstances;
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
    int selectedClipTrack = -1;
    int selectedClipIndex = -1;
    int selectedAutomationClipTrack = -1;
    int selectedAutomationClipIndex = -1;
    bool automationParamMenuOpen = false;
    int automationParamMenuTrack = -1;
    int automationParamMenuHoverIndex = -1;
    std::vector<std::string> automationParamMenuLabels;
    bool clipDragActive = false;
    int clipDragTrack = -1;
    int clipDragIndex = -1;
    int clipDragTargetTrack = -1;
    uint64_t clipDragTargetStart = 0;
    int64_t clipDragOffsetSamples = 0;
    bool clipTrimActive = false;
    bool clipTrimLeftEdge = false;
    int clipTrimTrack = -1;
    int clipTrimIndex = -1;
    uint64_t clipTrimOriginalStart = 0;
    uint64_t clipTrimOriginalLength = 0;
    uint64_t clipTrimOriginalSourceOffset = 0;
    uint64_t clipTrimTargetStart = 0;
    uint64_t clipTrimTargetLength = 0;
    uint64_t clipTrimTargetSourceOffset = 0;
    bool takeCompDragActive = false;
    int takeCompLaneType = -1;
    int takeCompTrack = -1;
    int takeCompTakeIndex = -1;
    uint64_t takeCompStartSample = 0;
    uint64_t takeCompEndSample = 0;
    bool timelineSelectionActive = false;
    bool timelineSelectionDragActive = false;
    bool timelineSelectionFromPlayhead = false;
    uint64_t timelineSelectionStartSample = 0;
    uint64_t timelineSelectionEndSample = 0;
    int timelineSelectionStartLane = -1;
    int timelineSelectionEndLane = -1;
    uint64_t timelineSelectionAnchorSample = 0;
    int timelineSelectionAnchorLane = -1;
    int externalDropIndex = -1;
    bool externalDropActive = false;
    int externalDropType = -1;
    bool playheadDragActive = false;
    float playheadDragOffsetX = 0.0f;
    bool allowEmptySelection = true;
    bool rulerDragActive = false;
    double rulerDragStartY = 0.0;
    double rulerDragLastX = 0.0;
    double rulerDragLastY = 0.0;
    double rulerDragStartSeconds = 10.0;
    double rulerDragAccumDY = 0.0;
    double rulerDragAnchorT = 0.0;
    double rulerDragAnchorTimeSec = 0.0;
    int rulerDragEdgeDirection = 0;
    bool verticalRulerDragActive = false;
    double verticalRulerDragLastX = 0.0;
    double verticalRulerDragLastY = 0.0;
    float verticalRulerDragStartLaneHeight = 60.0f;
    double verticalRulerDragAccumDX = 0.0;
    int verticalRulerDragXEdgeDirection = 0;
    int verticalRulerDragEdgeDirection = 0;
    float timelineLaneHeight = 60.0f;
    float timelineLaneOffset = 0.0f;
    bool bpmDragActive = false;
    double bpmDragLastY = 0.0;
    std::atomic<double> bpm{120.0};
    std::atomic<bool> metronomeEnabled{false};
    bool metronomeLoaded = false;
    uint32_t metronomeSampleRate = 0;
    std::vector<float> metronomeSamples;
    double metronomeSamplePos = 0.0;
    double metronomeSampleStep = 1.0;
    uint64_t metronomeNextSample = 0;
    bool metronomePrimed = false;
    bool metronomeSampleActive = false;
    std::atomic<bool> loopEnabled{false};
    uint64_t loopStartSamples = 0;
    uint64_t loopEndSamples = 0;
    bool loopDragActive = false;
    int loopDragMode = 0; // 0 none, 1 left, 2 right
    int64_t loopDragOffsetSamples = 0;
    uint64_t loopDragLengthSamples = 0;
    bool exportMenuOpen = false;
    int exportStartBar = 1;
    int exportEndBar = 5;
    std::array<std::string, kBusCount> exportStemNames{
        "master_L", "master_S", "master_F", "master_R"
    };
    std::string exportFolderPath;
    std::string exportStatusMessage;
    std::atomic<bool> exportInProgress{false};
    std::atomic<float> exportProgress{0.0f};
    bool exportSucceeded = false;
    bool exportJobActive = false;
    uint64_t exportJobStartSample = 0;
    uint64_t exportJobEndSample = 0;
    uint64_t exportJobCursorSample = 0;
    std::array<std::vector<float>, kBusCount> exportStemBuffers;
    std::vector<std::array<float, 128>> exportMidiHeldVelocities;
    int64_t exportSavedContinuousSamples = 0;
    bool exportSavedTransportPlaying = false;
    bool exportSavedTransportRecording = false;
    uint64_t exportSavedPlayheadSample = 0;
    bool exportSavedLoopEnabled = false;
    int exportSavedTransportLatch = 0;
    int exportSelectedStem = -1;

    bool settingsMenuOpen = false;
    int settingsTab = 0;
    int settingsSelectedTheme = 0;
    std::vector<DawThemePreset> themes;
    std::string selectedThemeName = "Default";
    bool themesLoaded = false;
    bool themeCreateMode = false;
    int themeEditField = -1; // 0 name, 1 background, 2 panel, 3 button, 4 piano, 5 piano accent, 6 lane
    std::string themeDraftName;
    std::string themeDraftBackgroundHex;
    std::string themeDraftPanelHex;
    std::string themeDraftButtonHex;
    std::string themeDraftPianoRollHex;
    std::string themeDraftPianoRollAccentHex;
    std::string themeDraftLaneHex;
    std::string themeStatusMessage;
    bool themeAppliedToWorld = false;
    uint64_t themeRevision = 0;
    glm::vec4 activeThemeBackground = glm::vec4(0.90f, 0.88f, 0.83f, 0.85f);
    glm::vec4 activeThemePanel = glm::vec4(0.90f, 0.88f, 0.83f, 0.80f);
    glm::vec4 activeThemeButton = glm::vec4(0.90f, 0.88f, 0.83f, 0.85f);
    glm::vec4 activeThemePianoRoll = glm::vec4(0.00f, 0.12f, 0.12f, 1.00f);
    glm::vec4 activeThemePianoRollAccent = glm::vec4(0.00f, 0.20f, 0.20f, 1.00f);
    glm::vec4 activeThemeLane = glm::vec4(230.0f / 255.0f, 224.0f / 255.0f, 212.0f / 255.0f, 1.00f);

    DawContext() {
        for (auto& level : masterBusLevels) {
            level.store(0.0f, std::memory_order_relaxed);
        }
    }
};
struct MidiTrack {
    std::vector<float> audio;
    std::vector<float> pendingRecord;
    std::vector<MidiClip> clips;
    std::vector<MidiClip> loopTakeClips;
    std::vector<MidiNote> pendingNotes;
    std::vector<float> waveformMin;
    std::vector<float> waveformMax;
    std::vector<glm::vec3> waveformColor;
    uint64_t waveformVersion = 0;
    uint64_t recordStartSample = 0;
    uint64_t recordStopSample = 0;
    uint64_t recordLinearStartSample = 0;
    uint64_t recordLinearStopSample = 0;
    uint64_t recordPassOffsetSamples = 0;
    uint64_t recordLastPlayheadSample = 0;
    bool recordLoopCapture = false;
    uint64_t recordLoopStartSample = 0;
    uint64_t recordLoopEndSample = 0;
    uint64_t loopTakeRangeStartSample = 0;
    uint64_t loopTakeRangeLength = 0;
    int recordArmMode = 0;
    bool recordingActive = false;
    int activeLoopTakeIndex = -1;
    bool takeStackExpanded = false;
    int nextTakeId = 1;
    int activeRecordNote = -1;
    uint64_t activeRecordNoteStart = 0;
    std::atomic<bool> recordEnabled{false};
    std::atomic<int> armMode{0};
    std::atomic<bool> mute{false};
    std::atomic<bool> solo{false};
    std::atomic<int> outputBus{2};
    std::atomic<int> outputBusL{2};
    std::atomic<int> outputBusR{2};
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
        clips = std::move(other.clips);
        loopTakeClips = std::move(other.loopTakeClips);
        waveformMin = std::move(other.waveformMin);
        waveformMax = std::move(other.waveformMax);
        waveformColor = std::move(other.waveformColor);
        waveformVersion = other.waveformVersion;
        recordStartSample = other.recordStartSample;
        recordStopSample = other.recordStopSample;
        recordLinearStartSample = other.recordLinearStartSample;
        recordLinearStopSample = other.recordLinearStopSample;
        recordPassOffsetSamples = other.recordPassOffsetSamples;
        recordLastPlayheadSample = other.recordLastPlayheadSample;
        recordLoopCapture = other.recordLoopCapture;
        recordLoopStartSample = other.recordLoopStartSample;
        recordLoopEndSample = other.recordLoopEndSample;
        loopTakeRangeStartSample = other.loopTakeRangeStartSample;
        loopTakeRangeLength = other.loopTakeRangeLength;
        recordArmMode = other.recordArmMode;
        recordingActive = other.recordingActive;
        activeLoopTakeIndex = other.activeLoopTakeIndex;
        takeStackExpanded = other.takeStackExpanded;
        nextTakeId = other.nextTakeId;
        activeRecordNote = other.activeRecordNote;
        activeRecordNoteStart = other.activeRecordNoteStart;
        recordEnabled.store(other.recordEnabled.load(std::memory_order_relaxed), std::memory_order_relaxed);
        armMode.store(other.armMode.load(std::memory_order_relaxed), std::memory_order_relaxed);
        mute.store(other.mute.load(std::memory_order_relaxed), std::memory_order_relaxed);
        solo.store(other.solo.load(std::memory_order_relaxed), std::memory_order_relaxed);
        outputBus.store(other.outputBus.load(std::memory_order_relaxed), std::memory_order_relaxed);
        outputBusL.store(other.outputBusL.load(std::memory_order_relaxed), std::memory_order_relaxed);
        outputBusR.store(other.outputBusR.load(std::memory_order_relaxed), std::memory_order_relaxed);
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
    int selectedClipTrack = -1;
    int selectedClipIndex = -1;
    bool pianoRollActive = false;
    int pianoRollTrack = -1;
    int pianoRollClipIndex = -1;
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
    std::unique_ptr<ColorEmotionContext> colorEmotion;
    std::unique_ptr<FishingContext> fishing;
    std::unique_ptr<GemContext> gems;
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
namespace AudioSystemLogic {
    void InitializeAudio(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*);
    void CleanupAudio(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*);
    bool TriggerGameplaySfx(BaseSystem&, const std::string&, float gain = 1.0f);
}
namespace SoundtrackSystemLogic { void UpdateSoundtracks(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace AudicleSystemLogic { void ProcessAudicles(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace AudioVisualizerFollowerSystemLogic { void UpdateAudioVisualizerFollower(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace SpawnSystemLogic { void SetPlayerSpawn(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace CameraSystemLogic { void UpdateCameraMatrices(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace KeyboardInputSystemLogic { void ProcessKeyboardInput(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace MouseInputSystemLogic { void UpdateCameraRotationFromMouse(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace UAVSystemLogic { void ProcessUAVMovement(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace WalkModeSystemLogic { void ProcessWalkMovement(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace GravitySystemLogic { void ApplyGravity(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace CollisionSystemLogic { void ResolveCollisions(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace VolumeFillSystemLogic { void ProcessVolumeFills(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace RenderInitSystemLogic { void InitializeRenderer(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void CleanupRenderer(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace VoxelMeshInitSystemLogic { void UpdateVoxelMeshInit(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace VoxelMeshingSystemLogic { void UpdateVoxelMeshing(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void StopGreedyAsync(); size_t GetGreedyInFlightCount(); size_t GetGreedyQueueCount(); void GetGreedyStats(size_t& queued, size_t& applied, size_t& dropped); }
namespace VoxelMeshUploadSystemLogic { void UpdateVoxelMeshUpload(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace VoxelMeshDebugSystemLogic { void UpdateVoxelMeshDebug(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace WorldRenderSystemLogic { void RenderWorld(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace OverlayRenderSystemLogic { void RenderOverlays(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace LeyLineSystemLogic { void LoadLeyLines(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); float SampleLeyStress(const WorldContext&, float, float); float SampleLeyUplift(const WorldContext&, float, float); }
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
namespace ColorEmotionSystemLogic { void UpdateColorEmotions(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void RenderColorEmotions(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace FishingSystemLogic { void UpdateFishing(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void RenderFishing(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace GemSystemLogic {
    void UpdateGems(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*);
    void RenderGems(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*);
    void SpawnGemDropFromOre(BaseSystem&, std::vector<Entity>&, int, const glm::vec3&, const glm::vec3&);
    bool TryPickupGemFromRay(BaseSystem&, const glm::vec3&, const glm::vec3&, float, GemDropState*);
    void PlaceGemDrop(BaseSystem&, GemDropState&&, const glm::vec3&);
}
namespace MidiTrackSystemLogic { void UpdateMidiTracks(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void CleanupMidiTracks(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); bool InsertTrackAt(BaseSystem&, int trackIndex); bool RemoveTrackAt(BaseSystem&, int trackIndex); bool MoveTrack(BaseSystem&, int fromIndex, int toIndex); }
namespace AutomationTrackSystemLogic { void UpdateAutomationTracks(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); bool InsertTrackAt(BaseSystem&, int trackIndex); bool RemoveTrackAt(BaseSystem&, int trackIndex); bool MoveTrack(BaseSystem&, int fromIndex, int toIndex); }
namespace MidiTransportSystemLogic { void UpdateMidiTransport(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); bool CycleTrackLoopTake(MidiContext& midi, int trackIndex, int direction); }
namespace MidiWaveformSystemLogic { void UpdateMidiWaveforms(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace MidiUiSystemLogic { void UpdateMidiUi(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace MidiStampingSystemLogic { void UpdateMidiStamping(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace MidiIOSystemLogic { void UpdateMidiIO(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
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
namespace DawTrackSystemLogic { void UpdateDawTracks(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void CleanupDawTracks(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); bool InsertTrackAt(BaseSystem&, int trackIndex); bool RemoveTrackAt(BaseSystem&, int trackIndex); bool MoveTrack(BaseSystem&, int fromIndex, int toIndex); }
namespace DawTransportSystemLogic { void UpdateDawTransport(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace DawClipSystemLogic { void UpdateDawClips(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); bool CycleTrackLoopTake(DawContext& daw, int trackIndex, int direction); }
namespace DawWaveformSystemLogic { void UpdateDawWaveforms(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace DawUiSystemLogic { void UpdateDawUi(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void ClampTimelineOffset(DawContext& daw); }
namespace DawIOSystemLogic {
    void UpdateDawIO(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*);
    double BarSamples(const DawContext& daw);
    int64_t BarDisplayToSample(const DawContext& daw, int displayBar);
    int64_t SampleToBarDisplay(const DawContext& daw, int64_t sample);
    bool OpenExportFolderDialog(std::string& ioPath);
    bool OpenSaveSessionDialog(std::string& ioPath);
    bool OpenLoadSessionDialog(std::string& ioPath);
    bool StartStemExport(BaseSystem& baseSystem);
    bool SaveSession(BaseSystem& baseSystem, const std::string& path);
    bool LoadSession(BaseSystem& baseSystem, const std::string& path);
    bool PromptAndSaveSession(BaseSystem& baseSystem);
    bool PromptAndLoadSession(BaseSystem& baseSystem);
    void TickStemExport(BaseSystem& baseSystem, float dt);
}
namespace DawLaneTimelineSystemLogic { void UpdateDawLaneTimeline(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace DawLaneInputSystemLogic { void UpdateDawLaneInput(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace DawLaneResourceSystemLogic { void UpdateDawLaneResources(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace DawLaneRenderSystemLogic { void UpdateDawLaneRender(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace DawTimelineRebaseLogic { void ShiftTimelineRight(BaseSystem& baseSystem, uint64_t shiftSamples); }
namespace MidiLaneSystemLogic { void OnTimelineRebased(uint64_t shiftSamples); }
namespace AutomationLaneSystemLogic { void UpdateAutomationLane(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void OnTimelineRebased(uint64_t shiftSamples); }
namespace PianoRollResourceSystemLogic { void UpdatePianoRollResources(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace PianoRollLayoutSystemLogic { void UpdatePianoRollLayout(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace PianoRollInputSystemLogic { void UpdatePianoRollInput(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace PianoRollRenderSystemLogic { void UpdatePianoRollRender(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace SkyboxSystemLogic { void getCurrentSkyColors(float, const std::vector<SkyColorKey>&, glm::vec3&, glm::vec3&); void RenderSkyAndCelestials(BaseSystem&, const std::vector<Entity>&, const std::vector<glm::vec3>&, float, float, const glm::mat4&, const glm::mat4&, const glm::vec3&, glm::vec3&); }
namespace CloudSystemLogic { void RenderClouds(BaseSystem&, const glm::vec3& lightDir, float time, float dayFraction); }
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
