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
            if (funcName == "LoadProcedureAssets") continue;
            if(functionRegistry.count(funcName)) {
                functionRegistry[funcName](baseSystem, entityPrototypes, 0.0f, window);
            } else {
                std::cout << "WARNING: Init function '" << funcName << "' not found in registry." << std::endl;
            }
        }
        
        // --- STEP 4: Create Prototypes and Instances ---
        {
            // --- Drastically reduced and more meaningful list of prototypes ---
            const std::vector<std::string> entityFiles = {
                "Entities/World.json",
                "Entities/Star.json",
                "Entities/DebugWorldGenerator.json",
                "Entities/Block.json",
                "Entities/Water.json",
                "Entities/Branch.json",
                "Entities/WireframeBlock.json",
                "Entities/TransparentWave.json"
            }; 

            for (const auto& filePath : entityFiles) {
                std::ifstream f(filePath);
                if (!f.is_open()) {
                    std::cerr << "ERROR: Could not open entity file " << filePath << std::endl;
                    continue;
                }
                nlohmann::json data = nlohmann::json::parse(f);
                Entity newProto = data.get<Entity>();
                newProto.prototypeID = this->entityPrototypes.size();
                this->entityPrototypes.push_back(newProto);
            }
        }
        
        // Helper function to find a prototype by name
        auto findProto = [&](const std::string& name) -> Entity* {
            for (auto& proto : entityPrototypes) {
                if (proto.name == name) return &proto;
            }
            return nullptr;
        };

        Entity* worldEntity = findProto("World");
        Entity* starProto = findProto("Star");
        Entity* worldGenAudicleProto = findProto("DebugWorldGenerator");
        
        // --- New World Generation Logic ---
        if (worldEntity && worldGenAudicleProto) {
            // Find our new block prototypes
            Entity* blockProto = findProto("Block");
            Entity* waterProto = findProto("Water");
            Entity* branchProto = findProto("Branch");
            Entity* wireframeProto = findProto("WireframeBlock");

            // Check if all required prototypes and colors exist before proceeding
            if (blockProto && waterProto && branchProto && wireframeProto &&
                baseSystem.colorLibrary.count("Grass") > 0 && 
                baseSystem.colorLibrary.count("Water") > 0 &&
                baseSystem.colorLibrary.count("Wood") > 0)
            {
                // Create a floor
                int worldSize = 20;
                for (int x = -worldSize; x <= worldSize; ++x) {
                    for (int z = -worldSize; z <= worldSize; ++z) {
                        glm::vec3 pos(x, 0.0f, z);
                        if (x > 5 && x < 10 && z > 5 && z < 10) {
                             worldGenAudicleProto->instances.push_back(SystemLogic::CreateInstance(baseSystem, waterProto->prototypeID, pos, baseSystem.colorLibrary["Water"]));
                        } else {
                             worldGenAudicleProto->instances.push_back(SystemLogic::CreateInstance(baseSystem, blockProto->prototypeID, pos, baseSystem.colorLibrary["Grass"]));
                        }
                    }
                }
                // Add a few "trees"
                worldGenAudicleProto->instances.push_back(SystemLogic::CreateInstance(baseSystem, blockProto->prototypeID, {-3, 1, -3}, baseSystem.colorLibrary["Wood"]));
                EntityInstance branchInst = SystemLogic::CreateInstance(baseSystem, branchProto->prototypeID, {-3, 2, -3}, baseSystem.colorLibrary["Leaf"]);
                branchInst.rotation = 45.0f;
                worldGenAudicleProto->instances.push_back(branchInst);
                
                // Add a pulsing wireframe block
                worldGenAudicleProto->instances.push_back(SystemLogic::CreateInstance(baseSystem, wireframeProto->prototypeID, {0, 2, 5}, baseSystem.colorLibrary["PulsingGrid"]));
            }
            
            // Add the populated audicle to the world to be processed
            worldEntity->instances.push_back(SystemLogic::CreateInstance(baseSystem, worldGenAudicleProto->prototypeID, glm::vec3(0), {0,0,0}));
        }

        if (worldEntity && starProto) {
             for (int i = 0; i < baseSystem.numStars; i++) {
                float theta = static_cast<float>(rand())/RAND_MAX * 2.0f * 3.14159f;
                float phi = static_cast<float>(rand())/RAND_MAX * 3.14159f;
                glm::vec3 pos = glm::vec3(sin(phi)*cos(theta), cos(phi), sin(phi)*sin(theta)) * baseSystem.starDistance;
                worldEntity->instances.push_back(SystemLogic::CreateInstance(baseSystem, starProto->prototypeID, pos, {1,1,1}));
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
