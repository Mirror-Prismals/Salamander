#define GLM_ENABLE_EXPERIMENTAL
#include "glad/glad.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <cstdlib>
#include <map>
#include <functional>
#include <memory>
#include <algorithm> 
#include <fstream>      
#include <sstream>
#include <ctime>
#include "json.hpp"

using json = nlohmann::json;

#include "BaseEntity.cpp"
#include "BaseSystem.cpp"


class Application {
private:
    GLFWwindow* window;
    std::vector<Entity> entityPrototypes;
    BaseSystem baseSystem;

    // --- Data-Driven System Members ---
    using SystemFunction = std::function<void(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*)>;
    std::map<std::string, SystemFunction> functionRegistry;
    std::vector<std::string> initFunctions;
    std::vector<std::string> updateFunctions;
    
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;

public:
    void run() {
        init();
        mainLoop();
        cleanup();
    }

private:
    // --- System Function Registration ---
    void registerSystemFunctions() {
        // Asset/Startup Functions
        functionRegistry["LoadProcedureAssets"] = SystemLogic::LoadProcedureAssets;

        // Player/Camera Functions
        functionRegistry["UpdateCameraRotationFromMouse"] = SystemLogic::UpdateCameraRotationFromMouse;
        functionRegistry["ProcessPlayerMovement"] = SystemLogic::ProcessPlayerMovement;
        functionRegistry["UpdateCameraMatrices"] = SystemLogic::UpdateCameraMatrices;

        // Game Logic Functions
        functionRegistry["ProcessAudicles"] = SystemLogic::ProcessAudicles;
        
        // Render Functions
        functionRegistry["InitializeRenderer"] = SystemLogic::InitializeRenderer;
        functionRegistry["RenderScene"] = SystemLogic::RenderScene;
    }

    // --- System Loading from JSON ---
    void loadSystems() {
        std::ifstream f("Procedures/systems.json");
        if (!f.is_open()) { std::cerr << "FATAL ERROR: Could not open Procedures/systems.json" << std::endl; exit(-1); }
        json data = json::parse(f);
        
        for (const auto& systemFile : data["systems_order"]) {
            std::string path = "Systems/" + systemFile.get<std::string>();
            std::ifstream sys_f(path);
            if (!sys_f.is_open()) { std::cerr << "FATAL ERROR: Could not open system file " << path << std::endl; continue; }
            
            json sys_data = json::parse(sys_f);
            if (sys_data.contains("init_steps")) {
                for (const auto& step : sys_data["init_steps"]) {
                    initFunctions.push_back(step);
                }
            }
            if (sys_data.contains("update_steps")) {
                for (const auto& step : sys_data["update_steps"]) {
                    updateFunctions.push_back(step);
                }
            }
        }
    }

    void init() {
        registerSystemFunctions();
        loadSystems();
        
        // --- STEP 1: Load non-OpenGL assets first ---
        // We know "LoadProcedureAssets" is safe to run before an OpenGL context exists.
        SystemLogic::LoadProcedureAssets(baseSystem, entityPrototypes, 0.0f, nullptr);

        // --- STEP 2: Initialize GLFW and GLAD ---
        if (!glfwInit()) { std::cout << "Failed to initialize GLFW\n"; exit(-1); }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        #ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        #endif
        
        window = glfwCreateWindow(baseSystem.windowWidth, baseSystem.windowHeight, "Prismals Engine", NULL, NULL);
        if (!window) { std::cout << "Failed to create GLFW window\n"; glfwTerminate(); exit(-1); }
        
        glfwMakeContextCurrent(window);
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::cout << "Failed to initialize GLAD\n"; exit(-1); }
        
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwSetWindowUserPointer(window, &baseSystem);
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int width, int height){ glViewport(0,0,width,height); });
        glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y){
            SystemLogic::ProcessMouseInput(*static_cast<BaseSystem*>(glfwGetWindowUserPointer(w)), x, y);
        });

        // --- STEP 3: Execute the rest of the init functions (now that OpenGL is ready) ---
        for(const auto& funcName : initFunctions) {
            // Skip the one we already ran
            if (funcName == "LoadProcedureAssets") continue;

            if(functionRegistry.count(funcName)) {
                functionRegistry[funcName](baseSystem, entityPrototypes, 0.0f, window);
            } else {
                std::cout << "WARNING: Init function '" << funcName << "' not found in registry." << std::endl;
            }
        }
        
        // --- STEP 4: Create Prototypes and Instances ---
        {
            // --- Procedural Block Generation (for now) ---
            for (int i = 0; i < baseSystem.numBlockPrototypes; ++i) {
                Entity p;
                p.prototypeID = this->entityPrototypes.size();
                p.isRenderable = true;
                p.blockType = i;
                p.name = "BlockType_" + std::to_string(i);
                this->entityPrototypes.push_back(p);
            }

            // --- JSON-based Entity Loading ---
            const std::vector<std::string> entityFiles = {
                "Entities/World.json",
                "Entities/Star.json",
                "Entities/DebugWorldGenerator.json"
            }; 

            for (const auto& filePath : entityFiles) {
                std::ifstream f(filePath);
                if (!f.is_open()) {
                    // Should probably handle this error more gracefully
                    continue;
                }
                nlohmann::json data = nlohmann::json::parse(f);
                Entity newProto = data.get<Entity>();
                newProto.prototypeID = this->entityPrototypes.size();
                this->entityPrototypes.push_back(newProto);
            }
        }
        
        Entity* worldEntity = nullptr;
        Entity* starProto = nullptr;
        Entity* worldGenAudicleProto = nullptr;
        
        for (auto& proto : entityPrototypes) {
            if (proto.isWorld) worldEntity = &proto;
            if (proto.isStar) starProto = &proto;
            if (proto.name == "DebugWorldGenerator") worldGenAudicleProto = &proto;
        }

        if (worldEntity && worldGenAudicleProto) {
            int grid_size = 5;
            float spacing = 3.0f;
            for (int i = 0; i < baseSystem.numBlockPrototypes; ++i) {
                glm::vec3 pos((i % grid_size) * spacing, 0.0f, (i / grid_size) * spacing);
                worldGenAudicleProto->instances.push_back(SystemLogic::CreateInstance(baseSystem, i, pos));
            }
            worldEntity->instances.push_back(SystemLogic::CreateInstance(baseSystem, worldGenAudicleProto->prototypeID, glm::vec3(0)));
        }

        if (worldEntity && starProto) {
             for (int i = 0; i < baseSystem.numStars; i++) {
                float theta = static_cast<float>(rand())/RAND_MAX * 2.0f * 3.14159f;
                float phi = static_cast<float>(rand())/RAND_MAX * 3.14159f * 0.5f;
                glm::vec3 pos = glm::vec3(sin(phi)*cos(theta), cos(phi), sin(phi)*sin(theta)) * baseSystem.starDistance;
                worldEntity->instances.push_back(SystemLogic::CreateInstance(baseSystem, starProto->prototypeID, pos));
            }
        }

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            float currentFrame = static_cast<float>(glfwGetTime());
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(window, true);

            // --- Execute per-frame update functions ---
            for(const auto& funcName : updateFunctions) {
                if(functionRegistry.count(funcName)) {
                    functionRegistry[funcName](baseSystem, entityPrototypes, deltaTime, window);
                } else {
                    std::cout << "WARNING: Update function '" << funcName << "' not found in registry." << std::endl;
                }
            }
            
            baseSystem.mouseOffsetX = 0.0f;
            baseSystem.mouseOffsetY = 0.0f;
            glfwSwapBuffers(window);
            glfwPollEvents();
        }
    }

    void cleanup() {
        SystemLogic::CleanupRenderer(baseSystem);
        glfwTerminate();
    }
};

int main() {
    Application app;
    app.run();
    return 0;
}
