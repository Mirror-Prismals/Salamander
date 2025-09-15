#pragma once

#include <memory>
#include <ctime>
#include <algorithm> 
#include <fstream>      
#include <sstream>
#include <string>
#include "json.hpp"

using json = nlohmann::json;

// --- Fix for JSON errors: Teach the library about GLM ---
namespace glm {
    void from_json(const json& j, vec3& v) {
        j.at(0).get_to(v.x);
        j.at(1).get_to(v.y);
        j.at(2).get_to(v.z);
    }
}

// --- Helper function moved from Procedures.cpp ---
void getCurrentSkyColors(float dayFraction, const std::vector<SkyColorKey>& skyKeys, glm::vec3& top, glm::vec3& bottom) {
    if (skyKeys.empty() || skyKeys.size() < 2) return;
    size_t i = 0;
    for (; i < skyKeys.size() - 1; i++) {
        if (dayFraction >= skyKeys[i].time && dayFraction <= skyKeys[i + 1].time) break;
    }
    if (i >= skyKeys.size() - 1) i = skyKeys.size() - 2;

    float t = (dayFraction - skyKeys[i].time) / (skyKeys[i + 1].time - skyKeys[i].time);
    top = glm::mix(skyKeys[i].top, skyKeys[i + 1].top, t);
    bottom = glm::mix(skyKeys[i].bottom, skyKeys[i + 1].bottom, t);
}


// --- System Interface ---
class ISystem {
public:
    virtual ~ISystem() {}
    virtual void update(std::vector<Entity>& prototypes, BaseSystem& baseSystem, float deltaTime, GLFWwindow* window) = 0;
};

class AssetLoadingSystem : public ISystem {
private:
    bool dataLoaded = false;
    void loadShaders(BaseSystem& baseSystem, const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) { std::cerr << "FATAL ERROR: Could not open shader file " << path << std::endl; exit(-1); }
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
public:
    void update(std::vector<Entity>& prototypes, BaseSystem& baseSystem, float deltaTime, GLFWwindow* window) override {
        if (dataLoaded) return;
        std::ifstream f("Procedures/procedures.json");
        if (!f.is_open()) { std::cerr << "FATAL ERROR: Could not open Procedures/procedures.json" << std::endl; exit(-1); }
        try {
            json data = json::parse(f);
            baseSystem.windowWidth = data["window"]["width"];
            baseSystem.windowHeight = data["window"]["height"];
            baseSystem.numBlockPrototypes = data["world"]["num_block_prototypes"];
            baseSystem.numStars = data["world"]["num_stars"];
            baseSystem.starDistance = data["world"]["star_distance"];
            baseSystem.blockColors = data["block_colors"].get<std::vector<glm::vec3>>();
            baseSystem.cubeVertices = data["cube_vertices"].get<std::vector<float>>();
            for (const auto& key : data["sky_color_keys"]) { baseSystem.skyKeys.push_back({key["time"], key["top"].get<glm::vec3>(), key["bottom"].get<glm::vec3>()}); }
        } catch (json::parse_error& e) { std::cerr << "FATAL ERROR: Failed to parse procedures.json: " << e.what() << std::endl; exit(-1); }
        loadShaders(baseSystem, "Procedures/procedures.glsl");
        dataLoaded = true;
    }
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

class InstanceSystem : public ISystem {
private:
    int nextInstanceID = 0;
public:
    EntityInstance createInstance(int prototypeID, glm::vec3 position) {
        EntityInstance inst; inst.instanceID = nextInstanceID++; inst.prototypeID = prototypeID; inst.position = position; return inst;
    }
    void update(std::vector<Entity>& prototypes, BaseSystem& baseSystem, float deltaTime, GLFWwindow* window) override {}
};

class PlayerControlSystem : public ISystem {
public:
    void processMouse(BaseSystem& baseSystem, double xpos, double ypos) {
        if (baseSystem.firstMouse) { baseSystem.lastX = xpos; baseSystem.lastY = ypos; baseSystem.firstMouse = false; }
        baseSystem.mouseOffsetX = xpos - baseSystem.lastX; baseSystem.mouseOffsetY = baseSystem.lastY - ypos; baseSystem.lastX = xpos; baseSystem.lastY = ypos;
    }
    void update(std::vector<Entity>& prototypes, BaseSystem& baseSystem, float deltaTime, GLFWwindow* window) override {
        const float sensitivity = 0.1f; baseSystem.cameraYaw += baseSystem.mouseOffsetX * sensitivity; baseSystem.cameraPitch += baseSystem.mouseOffsetY * sensitivity;
        if (baseSystem.cameraPitch > 89.0f) baseSystem.cameraPitch = 89.0f; if (baseSystem.cameraPitch < -89.0f) baseSystem.cameraPitch = -89.0f;
        float speed = 5.0f * deltaTime;
        glm::vec3 front(cos(glm::radians(baseSystem.cameraYaw)), 0.0f, sin(glm::radians(baseSystem.cameraYaw)));
        glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) baseSystem.cameraPosition += front * speed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) baseSystem.cameraPosition -= front * speed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) baseSystem.cameraPosition -= right * speed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) baseSystem.cameraPosition += right * speed;
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) baseSystem.cameraPosition.y += speed;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) baseSystem.cameraPosition.y -= speed;
    }
};

class CameraSystem : public ISystem {
public:
    void update(std::vector<Entity>& prototypes, BaseSystem& baseSystem, float deltaTime, GLFWwindow* window) override {
        glm::vec3 front;
        front.x = cos(glm::radians(baseSystem.cameraYaw)) * cos(glm::radians(baseSystem.cameraPitch));
        front.y = sin(glm::radians(baseSystem.cameraPitch));
        front.z = sin(glm::radians(baseSystem.cameraYaw)) * cos(glm::radians(baseSystem.cameraPitch));
        front = glm::normalize(front);
        baseSystem.viewMatrix = glm::lookAt(baseSystem.cameraPosition, baseSystem.cameraPosition + front, glm::vec3(0.0f, 1.0f, 0.0f));
        baseSystem.projectionMatrix = glm::perspective(glm::radians(103.0f), (float)baseSystem.windowWidth / (float)baseSystem.windowHeight, 0.1f, 2000.0f);
    }
};

class AudicleSystem : public ISystem {
private:
    InstanceSystem instance_factory; 
public:
    void update(std::vector<Entity>& prototypes, BaseSystem& baseSystem, float deltaTime, GLFWwindow* window) override {
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
                    if (!blockExists) { worldEntity->instances.push_back(instance_factory.createInstance(event.prototypeID, event.position)); }
                }
                audicleProto.instances.clear();
                finishedAudicleInstanceIDs.push_back(inst.instanceID);
            }
        }
        if (!finishedAudicleInstanceIDs.empty()) {
            worldEntity->instances.erase(
                std::remove_if(worldEntity->instances.begin(), worldEntity->instances.end(),
                    [&](const EntityInstance& inst) {
                        for (int finishedID : finishedAudicleInstanceIDs) { if (inst.instanceID == finishedID) return true; }
                        return false;
                    }),
                worldEntity->instances.end()
            );
        }
    }
};

class RenderSystem : public ISystem {
private:
    Shader *blockShader, *skyboxShader, *sunMoonShader, *starShader;
    GLuint cubeVBO, instanceVBO, branchInstanceVBO;
    std::vector<GLuint> blockVAOs;
    GLuint skyboxVAO, skyboxVBO, sunMoonVAO, sunMoonVBO, starVAO, starVBO;
    bool isInitialized = false;
public:
    RenderSystem() {} 
    ~RenderSystem() { delete blockShader; delete skyboxShader; delete sunMoonShader; delete starShader; }
    void init(BaseSystem& baseSystem) {
        blockVAOs.resize(baseSystem.numBlockPrototypes);
        blockShader = new Shader(baseSystem.shaders["BLOCK_VERTEX_SHADER"].c_str(), baseSystem.shaders["BLOCK_FRAGMENT_SHADER"].c_str());
        skyboxShader = new Shader(baseSystem.shaders["SKYBOX_VERTEX_SHADER"].c_str(), baseSystem.shaders["SKYBOX_FRAGMENT_SHADER"].c_str());
        sunMoonShader = new Shader(baseSystem.shaders["SUNMOON_VERTEX_SHADER"].c_str(), baseSystem.shaders["SUNMOON_FRAGMENT_SHADER"].c_str());
        starShader = new Shader(baseSystem.shaders["STAR_VERTEX_SHADER"].c_str(), baseSystem.shaders["STAR_FRAGMENT_SHADER"].c_str());
        glGenBuffers(1, &cubeVBO);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, baseSystem.cubeVertices.size() * sizeof(float), baseSystem.cubeVertices.data(), GL_STATIC_DRAW);
        glGenVertexArrays(baseSystem.numBlockPrototypes, blockVAOs.data());
        glGenBuffers(1, &instanceVBO);
        glGenBuffers(1, &branchInstanceVBO);
        for (int i = 0; i < baseSystem.numBlockPrototypes; ++i) {
            glBindVertexArray(blockVAOs[i]);
            glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
            glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
            glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(3*sizeof(float)));glEnableVertexAttribArray(1);
            glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(6*sizeof(float)));glEnableVertexAttribArray(2);
            if(i==14){glBindBuffer(GL_ARRAY_BUFFER,branchInstanceVBO);glVertexAttribPointer(3,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec4),(void*)0);glEnableVertexAttribArray(3);glVertexAttribDivisor(3,1);glVertexAttribPointer(4,1,GL_FLOAT,GL_FALSE,sizeof(glm::vec4),(void*)(sizeof(glm::vec3)));glEnableVertexAttribArray(4);glVertexAttribDivisor(4,1);}
            else{glBindBuffer(GL_ARRAY_BUFFER,instanceVBO);glVertexAttribPointer(3,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec3),(void*)0);glEnableVertexAttribArray(3);glVertexAttribDivisor(3,1);}
        }
        float skyboxQuadVertices[]={-1,1,-1,-1,1,-1,-1,1,1,-1,1,1};
        glGenVertexArrays(1,&skyboxVAO);glGenBuffers(1,&skyboxVBO);
        glBindVertexArray(skyboxVAO);glBindBuffer(GL_ARRAY_BUFFER,skyboxVBO);
        glBufferData(GL_ARRAY_BUFFER,sizeof(skyboxQuadVertices),skyboxQuadVertices,GL_STATIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
        glGenVertexArrays(1,&sunMoonVAO);glGenBuffers(1,&sunMoonVBO);
        glBindVertexArray(sunMoonVAO);glBindBuffer(GL_ARRAY_BUFFER,sunMoonVBO);
        glBufferData(GL_ARRAY_BUFFER,sizeof(skyboxQuadVertices),skyboxQuadVertices,GL_STATIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);glEnableVertexAttribArray(0);
        glGenVertexArrays(1,&starVAO);glGenBuffers(1,&starVBO);
        isInitialized = true;
    }
    void update(std::vector<Entity>& prototypes, BaseSystem& baseSystem, float deltaTime, GLFWwindow* window) override {
        if (!isInitialized) { init(baseSystem); }
        float time = static_cast<float>(glfwGetTime());
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glm::mat4 view = baseSystem.viewMatrix;
        glm::mat4 projection = baseSystem.projectionMatrix;
        glm::vec3 playerPos = baseSystem.cameraPosition;
        Entity* worldEntity = nullptr;
        for (auto& proto : prototypes) { if (proto.isWorld) { worldEntity = &proto; break; } }
        if (!worldEntity) return;
        time_t ct; std::time(&ct); tm lt;
        #ifdef _WIN32
        localtime_s(&lt, &ct);
        #else
        localtime_r(&ct, &lt);
        #endif
        float dayFraction = (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec) / 86400.0f;
        glm::vec3 skyTop, skyBottom;
        getCurrentSkyColors(dayFraction, baseSystem.skyKeys, skyTop, skyBottom);
        glDepthMask(GL_FALSE);
        skyboxShader->use();
        skyboxShader->setVec3("sT", skyTop);
        skyboxShader->setVec3("sB", skyBottom);
        glBindVertexArray(skyboxVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        std::vector<glm::vec3> starPositions;
        for(const auto& inst : worldEntity->instances) { if(prototypes[inst.prototypeID].isStar) starPositions.push_back(inst.position); }
        glBindVertexArray(starVAO);
        glBindBuffer(GL_ARRAY_BUFFER, starVBO);
        glBufferData(GL_ARRAY_BUFFER, starPositions.size() * sizeof(glm::vec3), starPositions.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec3),(void*)0);glEnableVertexAttribArray(0);
        starShader->use();
        starShader->setFloat("t", time);
        glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));
        starShader->setMat4("v", viewNoTranslation);
        starShader->setMat4("p", projection);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glDrawArrays(GL_POINTS, 0, starPositions.size());
        float hour = dayFraction * 24.0f;
        glm::vec3 sunDir, moonDir;
        float sunBrightness = 0.0f, moonBrightness = 0.0f;
        if(hour>=6&&hour<18){float u=(hour-6)/12.f;sunDir=glm::normalize(glm::vec3(0,sin(u*3.14159f),-cos(u*3.14159f)));sunBrightness=sin(u*3.14159f);}
        else{float aH=(hour<6)?hour+24:hour;float u=(aH-18)/12.f;moonDir=glm::normalize(glm::vec3(0,sin(u*3.14159f),-cos(u*3.14159f)));moonBrightness=sin(u*3.14159f);}
        sunMoonShader->use();
        sunMoonShader->setMat4("v", view);
        sunMoonShader->setMat4("p", projection);
        if(sunBrightness>0.01f){glm::mat4 m=glm::translate(glm::mat4(1),playerPos+sunDir*500.f);m=glm::scale(m,glm::vec3(50));sunMoonShader->setMat4("m",m);sunMoonShader->setVec3("c",glm::vec3(1,1,0.8f));sunMoonShader->setFloat("b",sunBrightness);glBindVertexArray(sunMoonVAO);glDrawArrays(GL_TRIANGLES,0,6);}
        if(moonBrightness>0.01f){glm::mat4 m=glm::translate(glm::mat4(1),playerPos+moonDir*500.f);m=glm::scale(m,glm::vec3(40));sunMoonShader->setMat4("m",m);sunMoonShader->setVec3("c",glm::vec3(0.9f,0.9f,1));sunMoonShader->setFloat("b",moonBrightness);glBindVertexArray(sunMoonVAO);glDrawArrays(GL_TRIANGLES,0,6);}
        glDepthMask(GL_TRUE);
        blockShader->use();
        blockShader->setMat4("view", view);
        blockShader->setMat4("projection", projection);
        blockShader->setVec3("cameraPos", playerPos);
        blockShader->setFloat("time", time);
        glm::vec3 lightDir = sunBrightness > 0.0f ? sunDir : moonDir;
        blockShader->setVec3("lightDir", lightDir);
        blockShader->setVec3("ambientLight", glm::vec3(0.4f));
        blockShader->setVec3("diffuseLight", glm::vec3(0.6f));
        glUniform3fv(glGetUniformLocation(blockShader->ID, "blockColors"), baseSystem.blockColors.size(), glm::value_ptr(baseSystem.blockColors[0]));
        blockShader->setMat4("model", glm::mat4(1.0f));
        std::vector<std::vector<glm::vec3>> blockInstances(baseSystem.numBlockPrototypes);
        std::vector<glm::vec4> branchInstances;
        for (const auto& instance : worldEntity->instances) {
            const Entity& proto = prototypes[instance.prototypeID];
            if (proto.isRenderable) {
                if (proto.blockType == 14) {
                    branchInstances.push_back(glm::vec4(instance.position, instance.rotation));
                } else if (proto.blockType >= 0 && proto.blockType < baseSystem.numBlockPrototypes) {
                    blockInstances[proto.blockType].push_back(instance.position);
                }
            }
        }
        for (int i = 0; i < baseSystem.numBlockPrototypes; ++i) {
            if (i == 14) {
                if (!branchInstances.empty()) {
                    blockShader->setInt("blockType", i);
                    glBindVertexArray(blockVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, branchInstanceVBO);
                    glBufferData(GL_ARRAY_BUFFER, branchInstances.size() * sizeof(glm::vec4), branchInstances.data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, branchInstances.size());
                }
            } else {
                if (!blockInstances[i].empty()) {
                    blockShader->setInt("blockType", i);
                    glBindVertexArray(blockVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
                    glBufferData(GL_ARRAY_BUFFER, blockInstances[i].size() * sizeof(glm::vec3), blockInstances[i].data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, blockInstances[i].size());
                }
            }
        }
    }
};
