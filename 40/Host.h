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
#include <memory>
#include <functional>
#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>
#include <mutex>
#include <queue>
#include "json.hpp"
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <array>
#include <variant>
#include "chuck.h"

// --- Forward Declarations ---
struct Entity; struct EntityInstance;
using json = nlohmann::json; using vec4 = glm::vec4;

enum class RenderBehavior { STATIC_DEFAULT, ANIMATED_WATER, ANIMATED_WIREFRAME, STATIC_BRANCH, ANIMATED_TRANSPARENT_WAVE, COUNT };
struct InstanceData { glm::vec3 position; glm::vec3 color; };
struct BranchInstanceData { glm::vec3 position; float rotation; glm::vec3 color; };
class Shader { public: unsigned int ID; Shader(const char* v, const char* f); void use(); void setMat4(const std::string&n,const glm::mat4&m)const; void setVec3(const std::string&n,const glm::vec3&v)const; void setFloat(const std::string&n,float v)const; void setInt(const std::string&n,int v)const; private: void check(unsigned int s,std::string t); };
struct SkyColorKey { float time; glm::vec3 top; glm::vec3 bottom; };

// --- CONTEXT STRUCTS ---
struct LevelContext {
    int activeWorldIndex = 0;
    std::vector<Entity> worlds;
};
struct AppContext { unsigned int windowWidth = 1920; unsigned int windowHeight = 1080; };
struct WorldContext { std::map<std::string, glm::vec3> colorLibrary; std::vector<SkyColorKey> skyKeys; std::vector<float> cubeVertices; std::map<std::string, std::string> shaders; };
struct PlayerContext {
    float cameraYaw=-90.0f;
    float cameraPitch=0.0f;
    glm::vec3 cameraPosition=glm::vec3(0.0f,1.0f,5.0f);
    float mouseOffsetX=0.0f;
    float mouseOffsetY=0.0f;
    float lastX=1920/2.0f;
    float lastY=1080/2.0f;
    bool firstMouse=true;
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    bool rightMouseDown=false;
    bool leftMouseDown=false;
    bool rightMousePressed=false;
    bool leftMousePressed=false;
    bool rightMouseReleased=false;
    bool leftMouseReleased=false;
    glm::ivec3 targetedBlock=glm::ivec3(0);
    glm::vec3 targetedBlockPosition=glm::vec3(0.0f);
    glm::vec3 targetedBlockNormal=glm::vec3(0.0f);
    bool hasBlockTarget=false;
    int targetedWorldIndex=-1;
    bool isChargingBlock=false;
    bool blockChargeReady=false;
    float blockChargeValue=0.0f;
    bool buildMode=false;
    int buildChannel=0;
    glm::vec3 buildColor=glm::vec3(1.0f);
    double scrollYOffset=0.0;
    bool isHoldingBlock=false;
    int heldPrototypeID=-1;
    glm::vec3 heldBlockColor=glm::vec3(1.0f, 1.0f, 1.0f);
};
struct InstanceContext { int nextInstanceID = 0; };
struct RendererContext {
    std::unique_ptr<Shader> blockShader, skyboxShader, sunMoonShader, starShader, selectionShader, hudShader, crosshairShader;
    GLuint cubeVBO;
    std::vector<GLuint> behaviorVAOs;
    std::vector<GLuint> behaviorInstanceVBOs;
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
};
struct AudioContext {
    jack_client_t* client = nullptr;
    jack_port_t* output_port = nullptr;
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
    SAMPLE* chuckOutput = nullptr;
    t_CKINT chuckBufferFrames = 0;
    int chuckInputChannels = 0;
    int chuckOutputChannels = 2;
    bool chuckRunning = false;
};
struct AudioSourceState { bool isOccluded = false; float distanceGain = 1.0f; };
struct RayTracedAudioContext { std::map<int, AudioSourceState> sourceStates; };
struct HUDContext {
    bool showCharge = false;
    bool chargeReady = false;
    float chargeValue = 0.0f;
    bool buildModeActive = false;
    glm::vec3 buildPreviewColor = glm::vec3(1.0f);
    int buildChannel = 0;
    float displayTimer = 0.0f;
};
struct UIContext {
    bool active = false;
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
};

struct BaseSystem {
    std::unique_ptr<LevelContext> level;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<WorldContext> world;
    std::unique_ptr<PlayerContext> player;
    std::unique_ptr<InstanceContext> instance;
    std::unique_ptr<RendererContext> renderer;
    std::unique_ptr<AudioContext> audio;
    std::unique_ptr<RayTracedAudioContext> rayTracedAudio;
    std::unique_ptr<HUDContext> hud;
    std::unique_ptr<UIContext> ui;
};
using SystemFunction = std::function<void(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*)>;

// --- SYSTEM FUNCTION DECLARATIONS ---
namespace HostLogic { void LoadProcedureAssets(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); EntityInstance CreateInstance(BaseSystem&, const std::vector<Entity>&, const std::string&, glm::vec3, glm::vec3); EntityInstance CreateInstance(BaseSystem&, int, glm::vec3, glm::vec3); glm::vec3 hexToVec3(const std::string& hex); void ProcessFillCommands(BaseSystem&, const std::vector<Entity>&); }
namespace RayTracedAudioSystemLogic { void ProcessRayTracedAudio(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace PinkNoiseSystemLogic { void ProcessPinkNoiseAudicle(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace AudioSystemLogic { void InitializeAudio(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void CleanupAudio(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace AudicleSystemLogic { void ProcessAudicles(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace CameraSystemLogic { void UpdateCameraMatrices(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace KeyboardInputSystemLogic { void ProcessKeyboardInput(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace MouseInputSystemLogic { void UpdateCameraRotationFromMouse(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace UAVSystemLogic { void ProcessUAVMovement(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
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
namespace ComputerCursorSystemLogic { void UpdateComputerCursor(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace ButtonSystemLogic { void UpdateButtons(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace GlyphSystemLogic { void UpdateGlyphs(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace ChucKSystemLogic { void CompileDefaultScript(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }

class Host {
private:
    GLFWwindow* window = nullptr; BaseSystem baseSystem; std::vector<Entity> entityPrototypes; std::map<std::string, std::variant<bool, std::string>> registry;
    std::map<std::string, SystemFunction> functionRegistry;
    struct SystemStep { std::string name; std::vector<std::string> dependencies; };
    std::vector<SystemStep> initFunctions, updateFunctions, cleanupFunctions;
    float deltaTime = 0.0f, lastFrame = 0.0f;
    void loadRegistry(); void loadSystems(); bool checkDependencies(const std::vector<std::string>& deps);
    void registerSystemFunctions(); void init(); void mainLoop(); void cleanup();
    void PopulateWorldsFromLevel();
public:
    void run();
    void processMouseInput(double xpos, double ypos);
    void processScroll(double xoffset, double yoffset);
};
