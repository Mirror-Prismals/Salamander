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

// --- Forward Declarations ---
struct Entity; struct EntityInstance;
using json = nlohmann::json; using vec4 = glm::vec4;

// --- CORE DATA STRUCTURES ---
enum class RenderBehavior { STATIC_DEFAULT, ANIMATED_WATER, ANIMATED_WIREFRAME, STATIC_BRANCH, ANIMATED_TRANSPARENT_WAVE, COUNT };
struct InstanceData { glm::vec3 position; glm::vec3 color; };
struct BranchInstanceData { glm::vec3 position; float rotation; glm::vec3 color; };
class Shader { public: unsigned int ID; Shader(const char* v, const char* f); void use(); void setMat4(const std::string&n,const glm::mat4&m)const; void setVec3(const std::string&n,const glm::vec3&v)const; void setFloat(const std::string&n,float v)const; void setInt(const std::string&n,int v)const; private: void check(unsigned int s,std::string t); };
struct SkyColorKey { float time; glm::vec3 top; glm::vec3 bottom; };

// --- CONTEXT STRUCTS ---
struct AppContext { unsigned int windowWidth = 1920; unsigned int windowHeight = 1080; };
struct WorldContext { int numStars=1000; float starDistance=1000.0f; std::map<std::string, glm::vec3> colorLibrary; std::vector<SkyColorKey> skyKeys; std::vector<float> cubeVertices; std::map<std::string, std::string> shaders; };
struct PlayerContext { 
    float cameraYaw=-90.0f; float cameraPitch=0.0f; glm::vec3 cameraPosition=glm::vec3(6.0f,5.0f,15.0f); 
    float mouseOffsetX=0.0f; float mouseOffsetY=0.0f; float lastX=1920/2.0f; float lastY=1080/2.0f; 
    bool firstMouse=true; glm::mat4 viewMatrix; glm::mat4 projectionMatrix; 
};
struct InstanceContext { int nextInstanceID = 0; };
struct RendererContext { std::unique_ptr<Shader> blockShader, skyboxShader, sunMoonShader, starShader; GLuint cubeVBO; std::vector<GLuint> behaviorVAOs; std::vector<GLuint> behaviorInstanceVBOs; GLuint skyboxVAO, skyboxVBO, sunMoonVAO, sunMoonVBO, starVAO, starVBO; };
struct AudioContext { jack_client_t* client = nullptr; jack_port_t* output_port = nullptr; jack_ringbuffer_t* ring_buffer = nullptr; float output_gain = 0.8f; std::mutex audio_state_mutex; int active_generators = 0; static const int PINK_NOISE_OCTAVES = 7; std::array<float, PINK_NOISE_OCTAVES> pink_rows{}; int pink_counter = 0; float pink_running_sum = 0.0f; };
struct TesseractContext { 
    std::vector<vec4> vertices4D; std::vector<unsigned int> indices;
    std::unique_ptr<Shader> shader; GLuint VAO = 0, VBO = 0, EBO = 0;
    // --- REVERTED TO SIMPLE ANGLES ---
    float angleXW = 0.0f, angleYW = 0.0f, angleZW = 0.0f;
    float targetAngleXW = 0.0f, targetAngleYW = 0.0f, targetAngleZW = 0.0f;
};
struct AudioSourceState { bool isOccluded = false; float distanceGain = 1.0f; };
struct RayTracedAudioContext { std::map<int, AudioSourceState> sourceStates; };

struct BaseSystem { std::unique_ptr<AppContext> app; std::unique_ptr<WorldContext> world; std::unique_ptr<PlayerContext> player; std::unique_ptr<InstanceContext> instance; std::unique_ptr<RendererContext> renderer; std::unique_ptr<AudioContext> audio; std::unique_ptr<RayTracedAudioContext> rayTracedAudio; std::unique_ptr<TesseractContext> tesseract; };
using SystemFunction = std::function<void(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*)>;

// --- SYSTEM FUNCTION DECLARATIONS ---
namespace HostLogic { void LoadProcedureAssets(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); EntityInstance CreateInstance(BaseSystem&, int, glm::vec3, glm::vec3); glm::vec3 hexToVec3(const std::string& hex); }
namespace RayTracedAudioSystemLogic { void ProcessRayTracedAudio(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace PinkNoiseSystemLogic { void ProcessPinkNoiseAudicle(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace AudioSystemLogic { void InitializeAudio(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void CleanupAudio(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace AudicleSystemLogic { void ProcessAudicles(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace CameraSystemLogic { void UpdateCameraMatrices(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace PlayerControlSystemLogic { void ProcessPlayerMovement(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void UpdateCameraRotationFromMouse(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace RenderSystemLogic { void InitializeRenderer(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void RenderScene(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void CleanupRenderer(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }
namespace TesseractSystemLogic { void InitializeTesseract(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void UpdateTesseract(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void RenderTesseract(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); void CleanupTesseract(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*); }

class Host {
private:
    GLFWwindow* window = nullptr; BaseSystem baseSystem; std::vector<Entity> entityPrototypes; std::map<std::string, bool> registry; std::string programStatus = "Not Installed";
    std::map<std::string, SystemFunction> functionRegistry;
    struct SystemStep { std::string name; std::vector<std::string> dependencies; };
    std::vector<SystemStep> initFunctions, updateFunctions, cleanupFunctions;
    float deltaTime = 0.0f, lastFrame = 0.0f;
    void loadRegistry(); void loadSystems(); bool checkDependencies(const std::vector<std::string>& deps);
    void registerSystemFunctions(); void init(); void mainLoop(); void cleanup();
public:
    void run();
    void processMouseInput(double xpos, double ypos);
};
