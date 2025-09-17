#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include "json.hpp"
#include <algorithm>

// --- CORE FRAMEWORK ENUMS AND DATA LAYOUTS (UNCHANGED) ---

enum class RenderBehavior {
    STATIC_DEFAULT,
    ANIMATED_WATER,
    ANIMATED_WIREFRAME,
    STATIC_BRANCH,
    ANIMATED_TRANSPARENT_WAVE,
    COUNT 
};

struct InstanceData {
    glm::vec3 position;
    glm::vec3 color;
};

struct BranchInstanceData {
    glm::vec3 position;
    float rotation;
    glm::vec3 color;
};

class Shader {
public:
    unsigned int ID;
    Shader(const char* v, const char* f){ID=glCreateProgram();unsigned int vs=glCreateShader(GL_VERTEX_SHADER);glShaderSource(vs,1,&v,0);glCompileShader(vs);check(vs,"V");unsigned int fs=glCreateShader(GL_FRAGMENT_SHADER);glShaderSource(fs,1,&f,0);glCompileShader(fs);check(fs,"F");glAttachShader(ID,vs);glAttachShader(ID,fs);glLinkProgram(ID);check(ID,"P");glDeleteShader(vs);glDeleteShader(fs);}
    void use(){glUseProgram(ID);}
    void setMat4(const std::string&n,const glm::mat4&m)const{glUniformMatrix4fv(glGetUniformLocation(ID,n.c_str()),1,GL_FALSE,&m[0][0]);}
    void setVec3(const std::string&n,const glm::vec3&v)const{glUniform3fv(glGetUniformLocation(ID,n.c_str()),1,&v[0]);}
    void setFloat(const std::string&n,float v)const{glUniform1f(glGetUniformLocation(ID,n.c_str()),v);}
    void setInt(const std::string&n,int v)const{glUniform1i(glGetUniformLocation(ID,n.c_str()),v);}
private:
    void check(unsigned int s,std::string t){int c;char i[1024];if(t!="P"){glGetShaderiv(s,GL_COMPILE_STATUS,&c);if(!c){glGetShaderInfoLog(s,1024,0,i); std::cout << "SHADER COMPILE ERROR: " << i << std::endl;}}else{glGetProgramiv(s,GL_LINK_STATUS,&c);if(!c){glGetProgramInfoLog(s,1024,0,i); std::cout << "SHADER LINK ERROR: " << i << std::endl;}}}
};

struct SkyColorKey { 
    float time; 
    glm::vec3 top; 
    glm::vec3 bottom; 
};

// Forward declarations
struct Entity;
struct EntityInstance;

// --- REFACTOR: DECONSTRUCTED STATE CONTEXTS ---
// The monolithic BaseSystem state has been broken down into these logical structs.

struct AppContext {
    unsigned int windowWidth = 1920;
    unsigned int windowHeight = 1080;
};

struct WorldContext {
    int numStars = 1000;
    float starDistance = 1000.0f;
    std::map<std::string, glm::vec3> colorLibrary;
    std::vector<SkyColorKey> skyKeys;
    std::vector<float> cubeVertices;
    std::map<std::string, std::string> shaders;
};

struct PlayerContext {
    float cameraYaw = -90.0f;
    float cameraPitch = 0.0f;
    glm::vec3 cameraPosition = glm::vec3(6.0f, 5.0f, 15.0f);
    
    // Input state is now clearly part of the PlayerContext
    float mouseOffsetX = 0.0f;
    float mouseOffsetY = 0.0f;
    float lastX = 1920 / 2.0f;
    float lastY = 1080 / 2.0f;
    bool firstMouse = true;
    
    // Matrices are generated from player state
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
};

struct InstanceContext {
    int nextInstanceID = 0;
};

struct RendererContext {
    std::unique_ptr<Shader> blockShader, skyboxShader, sunMoonShader, starShader;
    GLuint cubeVBO;
    std::vector<GLuint> behaviorVAOs;
    std::vector<GLuint> behaviorInstanceVBOs;
    GLuint skyboxVAO, skyboxVBO, sunMoonVAO, sunMoonVBO, starVAO, starVBO;
};


// --- REFACTOR: THE NEW BASESYSTEM ---
// BaseSystem is now a clean container for the state contexts.
// It acts as the single "world state" parameter passed to all systems.
struct BaseSystem {
    std::unique_ptr<AppContext> app;
    std::unique_ptr<WorldContext> world;
    std::unique_ptr<PlayerContext> player;
    std::unique_ptr<InstanceContext> instance;
    std::unique_ptr<RendererContext> renderer;
    
    BaseSystem() {
        app = std::make_unique<AppContext>();
        world = std::make_unique<WorldContext>();
        player = std::make_unique<PlayerContext>();
        instance = std::make_unique<InstanceContext>();
        renderer = std::make_unique<RendererContext>();
    }
};

// --- HELPER FUNCTIONS (UNCHANGED) ---

glm::vec3 hexToVec3(const std::string& hex) {
    std::string fullHex = hex.substr(1);
    if (fullHex.length() == 3) {
        char r = fullHex[0], g = fullHex[1], b = fullHex[2];
        fullHex = {r, r, g, g, b, b};
    }
    unsigned int hexValue = std::stoul(fullHex, nullptr, 16);
    return glm::vec3(((hexValue >> 16) & 0xFF) / 255.0f, ((hexValue >> 8) & 0xFF) / 255.0f, (hexValue & 0xFF) / 255.0f);
}

void getCurrentSkyColors(float dayFraction, const std::vector<SkyColorKey>& skyKeys, glm::vec3& top, glm::vec3& bottom) {
    if (skyKeys.empty() || skyKeys.size() < 2) return;
    size_t i = 0;
    for (; i < skyKeys.size() - 1; i++) { if (dayFraction >= skyKeys[i].time && dayFraction <= skyKeys[i + 1].time) break; }
    if (i >= skyKeys.size() - 1) i = skyKeys.size() - 2;
    float t = (dayFraction - skyKeys[i].time) / (skyKeys[i + 1].time - skyKeys[i].time);
    top = glm::mix(skyKeys[i].top, skyKeys[i + 1].top, t);
    bottom = glm::mix(skyKeys[i].bottom, skyKeys[i + 1].bottom, t);
}

// --- REFACTOR: THE LIBRARY OF LOGIC FUNCTIONS ---
// All functions are updated to access state via the new contexts.
// The function signatures remain the same, preserving your core rule.
namespace SystemLogic {

    EntityInstance CreateInstance(BaseSystem& baseSystem, int prototypeID, glm::vec3 position, glm::vec3 color) {
        EntityInstance inst;
        inst.instanceID = baseSystem.instance->nextInstanceID++; // REFACTORED
        inst.prototypeID = prototypeID;
        inst.position = position;
        inst.color = color;
        return inst;
    }

    void LoadProcedureAssets(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        // REFACTORED: Accessing specific context members
        AppContext& app = *baseSystem.app;
        WorldContext& world = *baseSystem.world;

        std::ifstream f("Procedures/procedures.json");
        if (!f.is_open()) { std::cerr << "FATAL ERROR: Could not open Procedures/procedures.json" << std::endl; exit(-1); }
        try {
            json data = json::parse(f);
            app.windowWidth = data["window"]["width"];
            app.windowHeight = data["window"]["height"];
            world.numStars = data["world"]["num_stars"];
            world.starDistance = data["world"]["star_distance"];
            world.cubeVertices = data["cube_vertices"].get<std::vector<float>>();
            for (const auto& key : data["sky_color_keys"]) { world.skyKeys.push_back({key["time"], key["top"].get<glm::vec3>(), key["bottom"].get<glm::vec3>()}); }
        } catch (json::parse_error& e) { std::cerr << "FATAL ERROR: Failed to parse procedures.json: " << e.what() << std::endl; exit(-1); }
        
        std::ifstream cf("Procedures/colors.json");
        if (!cf.is_open()) { std::cerr << "FATAL ERROR: Could not open Procedures/colors.json" << std::endl; exit(-1); }
        try {
            json colorData = json::parse(cf);
            for (auto& [name, hex] : colorData["colors"].items()) {
                world.colorLibrary[name] = hexToVec3(hex.get<std::string>());
            }
        } catch (json::parse_error& e) { std::cerr << "FATAL ERROR: Failed to parse colors.json: " << e.what() << std::endl; exit(-1); }
        
        std::ifstream file("Procedures/procedures.glsl");
        if (!file.is_open()) { std::cerr << "FATAL ERROR: Could not open shader file Procedures/procedures.glsl" << std::endl; exit(-1); }
        std::stringstream buffer; buffer << file.rdbuf(); std::string content = buffer.str();
        std::string currentShaderName; std::stringstream currentShaderSource;
        std::stringstream contentStream(content); std::string line;
        while (std::getline(contentStream, line)) {
            if (line.rfind("@@", 0) == 0) {
                if (!currentShaderName.empty()) { world.shaders[currentShaderName] = currentShaderSource.str(); }
                currentShaderName = line.substr(2); currentShaderSource.str(""); currentShaderSource.clear();
            } else { currentShaderSource << line << '\n'; }
        }
        if (!currentShaderName.empty()) { world.shaders[currentShaderName] = currentShaderSource.str(); }
    }
    
    void ProcessMouseInput(BaseSystem& baseSystem, double xpos, double ypos) {
        // REFACTORED: All input state is now in PlayerContext
        PlayerContext& player = *baseSystem.player;
        if (player.firstMouse) { player.lastX = xpos; player.lastY = ypos; player.firstMouse = false; }
        player.mouseOffsetX = xpos - player.lastX;
        player.mouseOffsetY = player.lastY - ypos;
        player.lastX = xpos;
        player.lastY = ypos;
    }
    
    void UpdateCameraRotationFromMouse(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        // REFACTORED: All camera and input state is in PlayerContext
        PlayerContext& player = *baseSystem.player;
        const float sensitivity = 0.1f;
        player.cameraYaw += player.mouseOffsetX * sensitivity;
        player.cameraPitch += player.mouseOffsetY * sensitivity;
        if (player.cameraPitch > 89.0f) player.cameraPitch = 89.0f;
        if (player.cameraPitch < -89.0f) player.cameraPitch = -89.0f;
    }
    
    void ProcessPlayerMovement(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        // REFACTORED: PlayerContext holds camera and position
        PlayerContext& player = *baseSystem.player;
        float speed = 5.0f * dt;
        glm::vec3 front(cos(glm::radians(player.cameraYaw)), 0.0f, sin(glm::radians(player.cameraYaw)));
        glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) player.cameraPosition += front * speed;
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) player.cameraPosition -= front * speed;
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) player.cameraPosition -= right * speed;
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) player.cameraPosition += right * speed;
        if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) player.cameraPosition.y += speed;
        if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) player.cameraPosition.y -= speed;
    }

    void UpdateCameraMatrices(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        // REFACTORED: Explicitly using AppContext and PlayerContext
        AppContext& app = *baseSystem.app;
        PlayerContext& player = *baseSystem.player;
        glm::vec3 front;
        front.x = cos(glm::radians(player.cameraYaw)) * cos(glm::radians(player.cameraPitch));
        front.y = sin(glm::radians(player.cameraPitch));
        front.z = sin(glm::radians(player.cameraYaw)) * cos(glm::radians(player.cameraPitch));
        front = glm::normalize(front);
        player.viewMatrix = glm::lookAt(player.cameraPosition, player.cameraPosition + front, glm::vec3(0.0f, 1.0f, 0.0f));
        player.projectionMatrix = glm::perspective(glm::radians(103.0f), (float)app.windowWidth / (float)app.windowHeight, 0.1f, 2000.0f);
    }
    
    void ProcessAudicles(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        Entity* worldEntity = nullptr;
        for (auto& proto : prototypes) { if (proto.isWorld) { worldEntity = &proto; break; } }
        if (!worldEntity) return;

        std::vector<int> finishedAudicleInstanceIDs;
        for (const auto& inst : worldEntity->instances) {
            if (prototypes[inst.prototypeID].isAudicle) {
                Entity& audicleProto = prototypes[inst.prototypeID];
                for (const auto& event : audicleProto.instances) {
                    bool blockExists = false;
                    for (const auto& worldInst : worldEntity->instances) {
                        if (worldInst.prototypeID != audicleProto.prototypeID && glm::distance(worldInst.position, event.position) < 0.1f) {
                            blockExists = true; break;
                        }
                    }
                    if (!blockExists) {
                        worldEntity->instances.push_back(CreateInstance(baseSystem, event.prototypeID, event.position, event.color));
                    }
                }
                audicleProto.instances.clear();
                finishedAudicleInstanceIDs.push_back(inst.instanceID);
            }
        }

        if (!finishedAudicleInstanceIDs.empty()) {
            worldEntity->instances.erase(
                std::remove_if(worldEntity->instances.begin(), worldEntity->instances.end(),
                    [&](const EntityInstance& inst) {
                        return std::find(finishedAudicleInstanceIDs.begin(), finishedAudicleInstanceIDs.end(), inst.instanceID) != finishedAudicleInstanceIDs.end();
                    }),
                worldEntity->instances.end()
            );
        }
    }

    void InitializeRenderer(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        // REFACTORED: This function now populates the RendererContext
        // It reads from the WorldContext.
        WorldContext& world = *baseSystem.world;
        RendererContext& renderer = *baseSystem.renderer;

        renderer.blockShader = std::make_unique<Shader>(world.shaders["BLOCK_VERTEX_SHADER"].c_str(), world.shaders["BLOCK_FRAGMENT_SHADER"].c_str());
        renderer.skyboxShader = std::make_unique<Shader>(world.shaders["SKYBOX_VERTEX_SHADER"].c_str(), world.shaders["SKYBOX_FRAGMENT_SHADER"].c_str());
        renderer.sunMoonShader = std::make_unique<Shader>(world.shaders["SUNMOON_VERTEX_SHADER"].c_str(), world.shaders["SUNMOON_FRAGMENT_SHADER"].c_str());
        renderer.starShader = std::make_unique<Shader>(world.shaders["STAR_VERTEX_SHADER"].c_str(), world.shaders["STAR_FRAGMENT_SHADER"].c_str());
        
        int behaviorCount = static_cast<int>(RenderBehavior::COUNT);
        renderer.behaviorVAOs.resize(behaviorCount);
        renderer.behaviorInstanceVBOs.resize(behaviorCount);
        glGenVertexArrays(behaviorCount, renderer.behaviorVAOs.data());
        glGenBuffers(behaviorCount, renderer.behaviorInstanceVBOs.data());

        glGenBuffers(1, &renderer.cubeVBO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, world.cubeVertices.size() * sizeof(float), world.cubeVertices.data(), GL_STATIC_DRAW);
        
        for (int i = 0; i < behaviorCount; ++i) {
            glBindVertexArray(renderer.behaviorVAOs[i]);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.cubeVBO);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
            
            glBindBuffer(GL_ARRAY_BUFFER, renderer.behaviorInstanceVBOs[i]);
            if (static_cast<RenderBehavior>(i) == RenderBehavior::STATIC_BRANCH) {
                glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, position));
                glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, rotation));
                glEnableVertexAttribArray(5); glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, color));
                glVertexAttribDivisor(3, 1); glVertexAttribDivisor(4, 1); glVertexAttribDivisor(5, 1);
            } else {
                glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, position));
                glEnableVertexAttribArray(4); glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, color));
                glVertexAttribDivisor(3, 1); glVertexAttribDivisor(4, 1);
            }
        }
        
        float skyboxQuadVertices[]={-1,1,-1,-1,1,-1,-1,1,1,-1,1,1};
        glGenVertexArrays(1,&renderer.skyboxVAO);glGenBuffers(1,&renderer.skyboxVBO);
        glBindVertexArray(renderer.skyboxVAO);glBindBuffer(GL_ARRAY_BUFFER,renderer.skyboxVBO);
        glBufferData(GL_ARRAY_BUFFER,sizeof(skyboxQuadVertices),skyboxQuadVertices,GL_STATIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
        
        glGenVertexArrays(1,&renderer.sunMoonVAO);glGenBuffers(1,&renderer.sunMoonVBO);
        glBindVertexArray(renderer.sunMoonVAO);glBindBuffer(GL_ARRAY_BUFFER,renderer.sunMoonVBO);
        glBufferData(GL_ARRAY_BUFFER,sizeof(skyboxQuadVertices),skyboxQuadVertices,GL_STATIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
        
        glGenVertexArrays(1,&renderer.starVAO);glGenBuffers(1,&renderer.starVBO);
    }
    
    void CleanupRenderer(BaseSystem& baseSystem) {
        // REFACTORED: Reads entirely from RendererContext
        RendererContext& renderer = *baseSystem.renderer;
        int behaviorCount = static_cast<int>(RenderBehavior::COUNT);
        glDeleteVertexArrays(behaviorCount, renderer.behaviorVAOs.data());
        glDeleteBuffers(behaviorCount, renderer.behaviorInstanceVBOs.data());
        glDeleteVertexArrays(1, &renderer.skyboxVAO);
        glDeleteVertexArrays(1, &renderer.sunMoonVAO);
        glDeleteVertexArrays(1, &renderer.starVAO);
        glDeleteBuffers(1, &renderer.cubeVBO);
        glDeleteBuffers(1, &renderer.skyboxVBO);
        glDeleteBuffers(1, &renderer.sunMoonVBO);
        glDeleteBuffers(1, &renderer.starVBO);
    }

    void RenderScene(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        // REFACTORED: This is the biggest change, showing the clarity of the new system.
        // We get the contexts we need at the top.
        PlayerContext& player = *baseSystem.player;
        WorldContext& world = *baseSystem.world;
        RendererContext& renderer = *baseSystem.renderer;

        float time = static_cast<float>(glfwGetTime());
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        Entity* worldEntity = nullptr;
        for (auto& proto : prototypes) { if (proto.isWorld) { worldEntity = &proto; break; } }
        if (!worldEntity) return;

        glm::mat4 view = player.viewMatrix;
        glm::mat4 projection = player.projectionMatrix;
        glm::vec3 playerPos = player.cameraPosition;
        
        time_t ct; std::time(&ct); tm lt;
        #ifdef _WIN32
        localtime_s(&lt, &ct);
        #else
        localtime_r(&ct, &lt);
        #endif
        float dayFraction = (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec) / 86400.0f;
        
        // --- Render Skybox ---
        glm::vec3 skyTop, skyBottom;
        getCurrentSkyColors(dayFraction, world.skyKeys, skyTop, skyBottom);
        glDepthMask(GL_FALSE);
        renderer.skyboxShader->use();
        renderer.skyboxShader->setVec3("sT", skyTop);
        renderer.skyboxShader->setVec3("sB", skyBottom);
        glBindVertexArray(renderer.skyboxVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        // --- Render Stars ---
        std::vector<glm::vec3> starPositions;
        for(const auto& inst : worldEntity->instances) { if(prototypes[inst.prototypeID].isStar) starPositions.push_back(inst.position); }
        if (!starPositions.empty()) {
            glBindVertexArray(renderer.starVAO);
            glBindBuffer(GL_ARRAY_BUFFER, renderer.starVBO);
            glBufferData(GL_ARRAY_BUFFER, starPositions.size() * sizeof(glm::vec3), starPositions.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec3),(void*)0);glEnableVertexAttribArray(0);
            renderer.starShader->use();
            renderer.starShader->setFloat("t", time);
            glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));
            renderer.starShader->setMat4("v", viewNoTranslation);
            renderer.starShader->setMat4("p", projection);
            glEnable(GL_PROGRAM_POINT_SIZE);
            glDrawArrays(GL_POINTS, 0, starPositions.size());
        }
        
        // --- Render Sun/Moon ---
        float hour = dayFraction * 24.0f;
        glm::vec3 sunDir, moonDir;
        float sunBrightness = 0.0f, moonBrightness = 0.0f;
        if(hour>=6&&hour<18){float u=(hour-6)/12.f;sunDir=glm::normalize(glm::vec3(0,sin(u*3.14159f),-cos(u*3.14159f)));sunBrightness=sin(u*3.14159f);}
        else{float aH=(hour<6)?hour+24:hour;float u=(aH-18)/12.f;moonDir=glm::normalize(glm::vec3(0,sin(u*3.14159f),-cos(u*3.14159f)));moonBrightness=sin(u*3.14159f);}
        renderer.sunMoonShader->use();
        renderer.sunMoonShader->setMat4("v", view);
        renderer.sunMoonShader->setMat4("p", projection);
        if(sunBrightness>0.01f){glm::mat4 m=glm::translate(glm::mat4(1),playerPos+sunDir*500.f);m=glm::scale(m,glm::vec3(50));renderer.sunMoonShader->setMat4("m",m);renderer.sunMoonShader->setVec3("c",glm::vec3(1,1,0.8f));renderer.sunMoonShader->setFloat("b",sunBrightness);glBindVertexArray(renderer.sunMoonVAO);glDrawArrays(GL_TRIANGLES,0,6);}
        if(moonBrightness>0.01f){glm::mat4 m=glm::translate(glm::mat4(1),playerPos+moonDir*500.f);m=glm::scale(m,glm::vec3(40));renderer.sunMoonShader->setMat4("m",m);renderer.sunMoonShader->setVec3("c",glm::vec3(0.9f,0.9f,1));renderer.sunMoonShader->setFloat("b",moonBrightness);glBindVertexArray(renderer.sunMoonVAO);glDrawArrays(GL_TRIANGLES,0,6);}
        
        // --- Render Blocks ---
        glDepthMask(GL_TRUE);
        renderer.blockShader->use();
        renderer.blockShader->setMat4("view", view);
        renderer.blockShader->setMat4("projection", projection);
        renderer.blockShader->setVec3("cameraPos", playerPos);
        renderer.blockShader->setFloat("time", time);
        glm::vec3 lightDir = sunBrightness > 0.0f ? sunDir : moonDir;
        renderer.blockShader->setVec3("lightDir", lightDir);
        renderer.blockShader->setVec3("ambientLight", glm::vec3(0.4f));
        renderer.blockShader->setVec3("diffuseLight", glm::vec3(0.6f));
        renderer.blockShader->setMat4("model", glm::mat4(1.0f));

        // --- Batch Instances by Behavior ---
        std::vector<std::vector<InstanceData>> behaviorInstances(static_cast<int>(RenderBehavior::COUNT));
        std::vector<BranchInstanceData> branchInstances;
        for (const auto& instance : worldEntity->instances) {
            const Entity& proto = prototypes[instance.prototypeID];
            if (proto.isRenderable && proto.isBlock) {
                RenderBehavior behavior = RenderBehavior::STATIC_DEFAULT;
                if (proto.name == "Branch") { behavior = RenderBehavior::STATIC_BRANCH; } 
                else if (proto.name == "Water") { behavior = RenderBehavior::ANIMATED_WATER; } 
                else if (proto.name == "TransparentWave") { behavior = RenderBehavior::ANIMATED_TRANSPARENT_WAVE; } 
                else if (proto.hasWireframe && proto.isAnimated) { behavior = RenderBehavior::ANIMATED_WIREFRAME; }

                if (behavior == RenderBehavior::STATIC_BRANCH) { branchInstances.push_back({instance.position, instance.rotation, instance.color}); } 
                else { behaviorInstances[static_cast<int>(behavior)].push_back({instance.position, instance.color}); }
            }
        }

        // --- Draw Batches ---
        for (int i = 0; i < static_cast<int>(RenderBehavior::COUNT); ++i) {
            RenderBehavior currentBehavior = static_cast<RenderBehavior>(i);
            if (currentBehavior == RenderBehavior::STATIC_BRANCH) {
                if (!branchInstances.empty()) {
                    renderer.blockShader->setInt("behaviorType", i);
                    glBindVertexArray(renderer.behaviorVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, renderer.behaviorInstanceVBOs[i]);
                    glBufferData(GL_ARRAY_BUFFER, branchInstances.size() * sizeof(BranchInstanceData), branchInstances.data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, branchInstances.size());
                }
            } else {
                if (!behaviorInstances[i].empty()) {
                    renderer.blockShader->setInt("behaviorType", i);
                    glBindVertexArray(renderer.behaviorVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, renderer.behaviorInstanceVBOs[i]);
                    glBufferData(GL_ARRAY_BUFFER, behaviorInstances[i].size() * sizeof(InstanceData), behaviorInstances[i].data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, behaviorInstances[i].size());
                }
            }
        }
    }
}
