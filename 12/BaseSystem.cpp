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

// An internal enum for the rendering system to categorize and batch draw calls.
// This is derived from the Entity's properties, not defined in JSON.
enum class RenderBehavior {
    STATIC_DEFAULT,
    ANIMATED_WATER,
    ANIMATED_WIREFRAME,
    STATIC_BRANCH,
    ANIMATED_TRANSPARENT_WAVE,
    COUNT 
};

// Data layout for standard instanced blocks, now including color.
struct InstanceData {
    glm::vec3 position;
    glm::vec3 color;
};

// Data layout for branch blocks, which have unique rotation.
struct BranchInstanceData {
    glm::vec3 position;
    float rotation;
    glm::vec3 color;
};

// --- Helper Classes and Structs ---

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

// --- Helper Functions ---

glm::vec3 hexToVec3(const std::string& hex) {
    std::string fullHex = hex.substr(1);
    if (fullHex.length() == 3) {
        char r = fullHex[0], g = fullHex[1], b = fullHex[2];
        fullHex = {r, r, g, g, b, b};
    }
    unsigned int hexValue = std::stoul(fullHex, nullptr, 16);
    return glm::vec3(
        ((hexValue >> 16) & 0xFF) / 255.0f,
        ((hexValue >> 8) & 0xFF) / 255.0f,
        (hexValue & 0xFF) / 255.0f
    );
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


// --- The Main Data Struct ---
struct BaseSystem {
    // --- Configuration Data ---
    unsigned int windowWidth = 1920;
    unsigned int windowHeight = 1080;
    int numStars = 1000;
    float starDistance = 1000.0f;
    std::map<std::string, glm::vec3> colorLibrary; // NEW: Global color library
    std::vector<SkyColorKey> skyKeys;
    std::vector<float> cubeVertices;
    std::map<std::string, std::string> shaders;

    // --- Player & Camera State ---
    float cameraYaw = -90.0f;
    float cameraPitch = 0.0f;
    glm::vec3 cameraPosition = glm::vec3(6.0f, 5.0f, 15.0f);
    float mouseOffsetX = 0.0f;
    float mouseOffsetY = 0.0f;
    float lastX = windowWidth / 2.0f;
    float lastY = windowHeight / 2.0f;
    bool firstMouse = true;
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    
    // --- Instance State ---
    int nextInstanceID = 0;

    // --- Renderer State ---
    std::unique_ptr<Shader> blockShader, skyboxShader, sunMoonShader, starShader;
    GLuint cubeVBO;
    std::vector<GLuint> behaviorVAOs;
    std::vector<GLuint> behaviorInstanceVBOs;
    GLuint skyboxVAO, skyboxVBO, sunMoonVAO, sunMoonVBO, starVAO, starVBO;
};

// --- The Library of Logic Functions ---
namespace SystemLogic {

    // CreateInstance now accepts a color
    EntityInstance CreateInstance(BaseSystem& baseSystem, int prototypeID, glm::vec3 position, glm::vec3 color) {
        EntityInstance inst;
        inst.instanceID = baseSystem.nextInstanceID++;
        inst.prototypeID = prototypeID;
        inst.position = position;
        inst.color = color;
        return inst;
    }

    void LoadProcedureAssets(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        // Load main procedures file
        std::ifstream f("Procedures/procedures.json");
        if (!f.is_open()) { std::cerr << "FATAL ERROR: Could not open Procedures/procedures.json" << std::endl; exit(-1); }
        try {
            json data = json::parse(f);
            baseSystem.windowWidth = data["window"]["width"];
            baseSystem.windowHeight = data["window"]["height"];
            baseSystem.numStars = data["world"]["num_stars"];
            baseSystem.starDistance = data["world"]["star_distance"];
            baseSystem.cubeVertices = data["cube_vertices"].get<std::vector<float>>();
            for (const auto& key : data["sky_color_keys"]) { baseSystem.skyKeys.push_back({key["time"], key["top"].get<glm::vec3>(), key["bottom"].get<glm::vec3>()}); }
        } catch (json::parse_error& e) { std::cerr << "FATAL ERROR: Failed to parse procedures.json: " << e.what() << std::endl; exit(-1); }

        // NEW: Load color library
        std::ifstream cf("Procedures/colors.json");
        if (!cf.is_open()) { std::cerr << "FATAL ERROR: Could not open Procedures/colors.json" << std::endl; exit(-1); }
        try {
            json colorData = json::parse(cf);
            for (auto& [name, hex] : colorData["colors"].items()) {
                baseSystem.colorLibrary[name] = hexToVec3(hex.get<std::string>());
            }
        } catch (json::parse_error& e) { std::cerr << "FATAL ERROR: Failed to parse colors.json: " << e.what() << std::endl; exit(-1); }
        
        // Load GLSL shaders
        std::ifstream file("Procedures/procedures.glsl");
        if (!file.is_open()) { std::cerr << "FATAL ERROR: Could not open shader file Procedures/procedures.glsl" << std::endl; exit(-1); }
        std::stringstream buffer; buffer << file.rdbuf(); std::string content = buffer.str();
        std::string currentShaderName; std::stringstream currentShaderSource;
        std::stringstream contentStream(content); std::string line;
        while (std::getline(contentStream, line)) {
            if (line.rfind("@@", 0) == 0) {
                if (!currentShaderName.empty()) { baseSystem.shaders[currentShaderName] = currentShaderSource.str(); }
                currentShaderName = line.substr(2); currentShaderSource.str(""); currentShaderSource.clear();
            } else { currentShaderSource << line << '\n'; }
        }
        if (!currentShaderName.empty()) { baseSystem.shaders[currentShaderName] = currentShaderSource.str(); }
    }
    
    void ProcessMouseInput(BaseSystem& baseSystem, double xpos, double ypos) {
        if (baseSystem.firstMouse) { baseSystem.lastX = xpos; baseSystem.lastY = ypos; baseSystem.firstMouse = false; }
        baseSystem.mouseOffsetX = xpos - baseSystem.lastX;
        baseSystem.mouseOffsetY = baseSystem.lastY - ypos;
        baseSystem.lastX = xpos;
        baseSystem.lastY = ypos;
    }
    
    void UpdateCameraRotationFromMouse(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        const float sensitivity = 0.1f;
        baseSystem.cameraYaw += baseSystem.mouseOffsetX * sensitivity;
        baseSystem.cameraPitch += baseSystem.mouseOffsetY * sensitivity;
        if (baseSystem.cameraPitch > 89.0f) baseSystem.cameraPitch = 89.0f;
        if (baseSystem.cameraPitch < -89.0f) baseSystem.cameraPitch = -89.0f;
    }
    
    void ProcessPlayerMovement(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        float speed = 5.0f * dt;
        glm::vec3 front(cos(glm::radians(baseSystem.cameraYaw)), 0.0f, sin(glm::radians(baseSystem.cameraYaw)));
        glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) baseSystem.cameraPosition += front * speed;
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) baseSystem.cameraPosition -= front * speed;
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) baseSystem.cameraPosition -= right * speed;
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) baseSystem.cameraPosition += right * speed;
        if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) baseSystem.cameraPosition.y += speed;
        if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) baseSystem.cameraPosition.y -= speed;
    }

    void UpdateCameraMatrices(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        glm::vec3 front;
        front.x = cos(glm::radians(baseSystem.cameraYaw)) * cos(glm::radians(baseSystem.cameraPitch));
        front.y = sin(glm::radians(baseSystem.cameraPitch));
        front.z = sin(glm::radians(baseSystem.cameraYaw)) * cos(glm::radians(baseSystem.cameraPitch));
        front = glm::normalize(front);
        baseSystem.viewMatrix = glm::lookAt(baseSystem.cameraPosition, baseSystem.cameraPosition + front, glm::vec3(0.0f, 1.0f, 0.0f));
        baseSystem.projectionMatrix = glm::perspective(glm::radians(103.0f), (float)baseSystem.windowWidth / (float)baseSystem.windowHeight, 0.1f, 2000.0f);
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
                        // The event carries all necessary data, including color, to create the new world instance.
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
        baseSystem.blockShader = std::make_unique<Shader>(baseSystem.shaders["BLOCK_VERTEX_SHADER"].c_str(), baseSystem.shaders["BLOCK_FRAGMENT_SHADER"].c_str());
        baseSystem.skyboxShader = std::make_unique<Shader>(baseSystem.shaders["SKYBOX_VERTEX_SHADER"].c_str(), baseSystem.shaders["SKYBOX_FRAGMENT_SHADER"].c_str());
        baseSystem.sunMoonShader = std::make_unique<Shader>(baseSystem.shaders["SUNMOON_VERTEX_SHADER"].c_str(), baseSystem.shaders["SUNMOON_FRAGMENT_SHADER"].c_str());
        baseSystem.starShader = std::make_unique<Shader>(baseSystem.shaders["STAR_VERTEX_SHADER"].c_str(), baseSystem.shaders["STAR_FRAGMENT_SHADER"].c_str());
        
        int behaviorCount = static_cast<int>(RenderBehavior::COUNT);
        baseSystem.behaviorVAOs.resize(behaviorCount);
        baseSystem.behaviorInstanceVBOs.resize(behaviorCount);
        glGenVertexArrays(behaviorCount, baseSystem.behaviorVAOs.data());
        glGenBuffers(behaviorCount, baseSystem.behaviorInstanceVBOs.data());

        glGenBuffers(1, &baseSystem.cubeVBO);
        glBindBuffer(GL_ARRAY_BUFFER, baseSystem.cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, baseSystem.cubeVertices.size() * sizeof(float), baseSystem.cubeVertices.data(), GL_STATIC_DRAW);
        
        for (int i = 0; i < behaviorCount; ++i) {
            glBindVertexArray(baseSystem.behaviorVAOs[i]);
            glBindBuffer(GL_ARRAY_BUFFER, baseSystem.cubeVBO);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
            
            glBindBuffer(GL_ARRAY_BUFFER, baseSystem.behaviorInstanceVBOs[i]);
            if (static_cast<RenderBehavior>(i) == RenderBehavior::STATIC_BRANCH) {
                // Layout for BranchInstanceData: {vec3 pos, float rot, vec3 color}
                glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, position));
                glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, rotation));
                glEnableVertexAttribArray(5); glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(BranchInstanceData), (void*)offsetof(BranchInstanceData, color));
                glVertexAttribDivisor(3, 1);
                glVertexAttribDivisor(4, 1);
                glVertexAttribDivisor(5, 1);
            } else {
                // Layout for InstanceData: {vec3 pos, vec3 color}
                glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, position));
                glEnableVertexAttribArray(4); glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, color));
                glVertexAttribDivisor(3, 1);
                glVertexAttribDivisor(4, 1);
            }
        }
        
        float skyboxQuadVertices[]={-1,1,-1,-1,1,-1,-1,1,1,-1,1,1};
        glGenVertexArrays(1,&baseSystem.skyboxVAO);glGenBuffers(1,&baseSystem.skyboxVBO);
        glBindVertexArray(baseSystem.skyboxVAO);glBindBuffer(GL_ARRAY_BUFFER,baseSystem.skyboxVBO);
        glBufferData(GL_ARRAY_BUFFER,sizeof(skyboxQuadVertices),skyboxQuadVertices,GL_STATIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
        
        glGenVertexArrays(1,&baseSystem.sunMoonVAO);glGenBuffers(1,&baseSystem.sunMoonVBO);
        glBindVertexArray(baseSystem.sunMoonVAO);glBindBuffer(GL_ARRAY_BUFFER,baseSystem.sunMoonVBO);
        glBufferData(GL_ARRAY_BUFFER,sizeof(skyboxQuadVertices),skyboxQuadVertices,GL_STATIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
        
        glGenVertexArrays(1,&baseSystem.starVAO);glGenBuffers(1,&baseSystem.starVBO);
    }
    
    void CleanupRenderer(BaseSystem& baseSystem) {
        int behaviorCount = static_cast<int>(RenderBehavior::COUNT);
        glDeleteVertexArrays(behaviorCount, baseSystem.behaviorVAOs.data());
        glDeleteBuffers(behaviorCount, baseSystem.behaviorInstanceVBOs.data());
        glDeleteVertexArrays(1, &baseSystem.skyboxVAO);
        glDeleteVertexArrays(1, &baseSystem.sunMoonVAO);
        glDeleteVertexArrays(1, &baseSystem.starVAO);
        glDeleteBuffers(1, &baseSystem.cubeVBO);
        glDeleteBuffers(1, &baseSystem.skyboxVBO);
        glDeleteBuffers(1, &baseSystem.sunMoonVBO);
        glDeleteBuffers(1, &baseSystem.starVBO);
    }

    void RenderScene(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        float time = static_cast<float>(glfwGetTime());
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        Entity* worldEntity = nullptr;
        for (auto& proto : prototypes) { if (proto.isWorld) { worldEntity = &proto; break; } }
        if (!worldEntity) return;

        glm::mat4 view = baseSystem.viewMatrix;
        glm::mat4 projection = baseSystem.projectionMatrix;
        glm::vec3 playerPos = baseSystem.cameraPosition;
        
        time_t ct; std::time(&ct); tm lt;
        #ifdef _WIN32
        localtime_s(&lt, &ct);
        #else
        localtime_r(&ct, &lt);
        #endif
        float dayFraction = (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec) / 86400.0f;
        
        // --- Render Skybox ---
        glm::vec3 skyTop, skyBottom;
        getCurrentSkyColors(dayFraction, baseSystem.skyKeys, skyTop, skyBottom);
        glDepthMask(GL_FALSE);
        baseSystem.skyboxShader->use();
        baseSystem.skyboxShader->setVec3("sT", skyTop);
        baseSystem.skyboxShader->setVec3("sB", skyBottom);
        glBindVertexArray(baseSystem.skyboxVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        // --- Render Stars ---
        std::vector<glm::vec3> starPositions;
        for(const auto& inst : worldEntity->instances) { if(prototypes[inst.prototypeID].isStar) starPositions.push_back(inst.position); }
        if (!starPositions.empty()) {
            glBindVertexArray(baseSystem.starVAO);
            glBindBuffer(GL_ARRAY_BUFFER, baseSystem.starVBO);
            glBufferData(GL_ARRAY_BUFFER, starPositions.size() * sizeof(glm::vec3), starPositions.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec3),(void*)0);glEnableVertexAttribArray(0);
            baseSystem.starShader->use();
            baseSystem.starShader->setFloat("t", time);
            glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));
            baseSystem.starShader->setMat4("v", viewNoTranslation);
            baseSystem.starShader->setMat4("p", projection);
            glEnable(GL_PROGRAM_POINT_SIZE);
            glDrawArrays(GL_POINTS, 0, starPositions.size());
        }
        
        // --- Render Sun/Moon ---
        float hour = dayFraction * 24.0f;
        glm::vec3 sunDir, moonDir;
        float sunBrightness = 0.0f, moonBrightness = 0.0f;
        if(hour>=6&&hour<18){float u=(hour-6)/12.f;sunDir=glm::normalize(glm::vec3(0,sin(u*3.14159f),-cos(u*3.14159f)));sunBrightness=sin(u*3.14159f);}
        else{float aH=(hour<6)?hour+24:hour;float u=(aH-18)/12.f;moonDir=glm::normalize(glm::vec3(0,sin(u*3.14159f),-cos(u*3.14159f)));moonBrightness=sin(u*3.14159f);}
        baseSystem.sunMoonShader->use();
        baseSystem.sunMoonShader->setMat4("v", view);
        baseSystem.sunMoonShader->setMat4("p", projection);
        if(sunBrightness>0.01f){glm::mat4 m=glm::translate(glm::mat4(1),playerPos+sunDir*500.f);m=glm::scale(m,glm::vec3(50));baseSystem.sunMoonShader->setMat4("m",m);baseSystem.sunMoonShader->setVec3("c",glm::vec3(1,1,0.8f));baseSystem.sunMoonShader->setFloat("b",sunBrightness);glBindVertexArray(baseSystem.sunMoonVAO);glDrawArrays(GL_TRIANGLES,0,6);}
        if(moonBrightness>0.01f){glm::mat4 m=glm::translate(glm::mat4(1),playerPos+moonDir*500.f);m=glm::scale(m,glm::vec3(40));baseSystem.sunMoonShader->setMat4("m",m);baseSystem.sunMoonShader->setVec3("c",glm::vec3(0.9f,0.9f,1));baseSystem.sunMoonShader->setFloat("b",moonBrightness);glBindVertexArray(baseSystem.sunMoonVAO);glDrawArrays(GL_TRIANGLES,0,6);}
        
        // --- Render Blocks ---
        glDepthMask(GL_TRUE);
        baseSystem.blockShader->use();
        baseSystem.blockShader->setMat4("view", view);
        baseSystem.blockShader->setMat4("projection", projection);
        baseSystem.blockShader->setVec3("cameraPos", playerPos);
        baseSystem.blockShader->setFloat("time", time);
        glm::vec3 lightDir = sunBrightness > 0.0f ? sunDir : moonDir;
        baseSystem.blockShader->setVec3("lightDir", lightDir);
        baseSystem.blockShader->setVec3("ambientLight", glm::vec3(0.4f));
        baseSystem.blockShader->setVec3("diffuseLight", glm::vec3(0.6f));
        baseSystem.blockShader->setMat4("model", glm::mat4(1.0f));

        // --- Batch Instances by Behavior ---
        std::vector<std::vector<InstanceData>> behaviorInstances(static_cast<int>(RenderBehavior::COUNT));
        std::vector<BranchInstanceData> branchInstances;

        for (const auto& instance : worldEntity->instances) {
            const Entity& proto = prototypes[instance.prototypeID];
            if (proto.isRenderable && proto.isBlock) {
                // Determine behavior from prototype's properties
                RenderBehavior behavior = RenderBehavior::STATIC_DEFAULT;
                if (proto.name == "Branch") {
                    behavior = RenderBehavior::STATIC_BRANCH;
                } else if (proto.name == "Water") {
                    behavior = RenderBehavior::ANIMATED_WATER;
                } else if (proto.name == "TransparentWave") {
                    behavior = RenderBehavior::ANIMATED_TRANSPARENT_WAVE;
                } else if (proto.hasWireframe && proto.isAnimated) {
                    behavior = RenderBehavior::ANIMATED_WIREFRAME;
                }

                if (behavior == RenderBehavior::STATIC_BRANCH) {
                    branchInstances.push_back({instance.position, instance.rotation, instance.color});
                } else {
                    behaviorInstances[static_cast<int>(behavior)].push_back({instance.position, instance.color});
                }
            }
        }

        // --- Draw Batches ---
        for (int i = 0; i < static_cast<int>(RenderBehavior::COUNT); ++i) {
            RenderBehavior currentBehavior = static_cast<RenderBehavior>(i);
            
            if (currentBehavior == RenderBehavior::STATIC_BRANCH) {
                if (!branchInstances.empty()) {
                    baseSystem.blockShader->setInt("behaviorType", i);
                    glBindVertexArray(baseSystem.behaviorVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, baseSystem.behaviorInstanceVBOs[i]);
                    glBufferData(GL_ARRAY_BUFFER, branchInstances.size() * sizeof(BranchInstanceData), branchInstances.data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, branchInstances.size());
                }
            } else {
                if (!behaviorInstances[i].empty()) {
                    baseSystem.blockShader->setInt("behaviorType", i);
                    glBindVertexArray(baseSystem.behaviorVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, baseSystem.behaviorInstanceVBOs[i]);
                    glBufferData(GL_ARRAY_BUFFER, behaviorInstances[i].size() * sizeof(InstanceData), behaviorInstances[i].data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, behaviorInstances[i].size());
                }
            }
        }
    }
}
