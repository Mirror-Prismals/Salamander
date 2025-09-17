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

    // Struct to hold richer system data, including dependencies
    struct SystemStep {
        std::string name;
        std::vector<std::string> dependencies;
    };
    std::vector<SystemStep> initFunctions;
    std::vector<SystemStep> updateFunctions;

    float deltaTime = 0.0f;
    float lastFrame = 0.0f;

public:
    void run() {
        init();
        mainLoop();
        cleanup();
    }

private:
    void registerSystemFunctions() {
        functionRegistry["LoadProcedureAssets"] = SystemLogic::LoadProcedureAssets;
        functionRegistry["UpdateCameraRotationFromMouse"] = SystemLogic::UpdateCameraRotationFromMouse;
        functionRegistry["ProcessPlayerMovement"] = SystemLogic::ProcessPlayerMovement;
        functionRegistry["UpdateCameraMatrices"] = SystemLogic::UpdateCameraMatrices;
        functionRegistry["ProcessAudicles"] = SystemLogic::ProcessAudicles;
        functionRegistry["InitializeRenderer"] = SystemLogic::InitializeRenderer;
        functionRegistry["RenderScene"] = SystemLogic::RenderScene;
    }

    // FINAL VERSION: This function robustly parses the new JSON format
    void loadSystems() {
        std::ifstream f("Procedures/systems.json");
        if (!f.is_open()) { std::cerr << "FATAL ERROR: Could not open Procedures/systems.json" << std::endl; exit(-1); }
        json data = json::parse(f);

        std::cout << "--- Loading Salamander Architecture ---" << std::endl;

        for (const auto& systemFile : data["systems_order"]) {
            std::string path = "Systems/" + systemFile.get<std::string>();
            std::ifstream sys_f(path);
            if (!sys_f.is_open()) { std::cerr << "FATAL ERROR: Could not open system file " << path << std::endl; continue; }

            json sys_data = json::parse(sys_f);

            // ROBUST PARSING: Check if 'init_steps' exists and is a JSON object
            if (sys_data.contains("init_steps") && sys_data["init_steps"].is_object()) {
                for (auto& [name, details] : sys_data["init_steps"].items()) {
                    SystemStep step;
                    step.name = name;
                    if (details.contains("dependencies")) {
                        step.dependencies = details["dependencies"].get<std::vector<std::string>>();
                    }
                    initFunctions.push_back(step);
                }
            }
            // ROBUST PARSING: Check if 'update_steps' exists and is a JSON object
            if (sys_data.contains("update_steps") && sys_data["update_steps"].is_object()) {
                for (auto& [name, details] : sys_data["update_steps"].items()) {
                    SystemStep step;
                    step.name = name;
                    if (details.contains("dependencies")) {
                        step.dependencies = details["dependencies"].get<std::vector<std::string>>();
                    }
                    updateFunctions.push_back(step);
                }
            }
        }

        // Architectural Audit Log
        std::cout << "[INIT STEPS]" << std::endl;
        for (const auto& step : initFunctions) {
            std::cout << "  - " << step.name;
            if (!step.dependencies.empty()) {
                std::cout << " (Depends on: ";
                for (size_t i = 0; i < step.dependencies.size(); ++i) {
                    std::cout << step.dependencies[i] << (i == step.dependencies.size() - 1 ? "" : ", ");
                }
                std::cout << ")";
            }
            std::cout << std::endl;
        }

        std::cout << "[UPDATE STEPS]" << std::endl;
        for (const auto& step : updateFunctions) {
            std::cout << "  - " << step.name;
            if (!step.dependencies.empty()) {
                std::cout << " (Depends on: ";
                for (size_t i = 0; i < step.dependencies.size(); ++i) {
                    std::cout << step.dependencies[i] << (i == step.dependencies.size() - 1 ? "" : ", ");
                }
                std::cout << ")";
            }
            std::cout << std::endl;
        }
        std::cout << "---------------------------------------" << std::endl;
    }

    void init() {
        registerSystemFunctions();
        loadSystems();

        // LoadProcedureAssets is called first and separately
        SystemLogic::LoadProcedureAssets(baseSystem, entityPrototypes, 0.0f, nullptr);

        if (!glfwInit()) { std::cout << "Failed to initialize GLFW\n"; exit(-1); }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        #ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        #endif

        window = glfwCreateWindow(baseSystem.app->windowWidth, baseSystem.app->windowHeight, "Salamander Engine", NULL, NULL);
        if (!window) { std::cout << "Failed to create GLFW window\n"; glfwTerminate(); exit(-1); }

        glfwMakeContextCurrent(window);
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::cout << "Failed to initialize GLAD\n"; exit(-1); }

        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwSetWindowUserPointer(window, &baseSystem);
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int width, int height){ glViewport(0, 0, width, height); });
        glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y){
            SystemLogic::ProcessMouseInput(*static_cast<BaseSystem*>(glfwGetWindowUserPointer(w)), x, y);
        });

        // Execute init functions from the new SystemStep struct
        for(const auto& step : initFunctions) {
            if (step.name == "LoadProcedureAssets") continue;
            if(functionRegistry.count(step.name)) {
                functionRegistry[step.name](baseSystem, entityPrototypes, 0.0f, window);
            } else {
                std::cout << "WARNING: Init function '" << step.name << "' not found in registry." << std::endl;
            }
        }

        // --- Create Prototypes and Instances (No changes needed here) ---
        {
            const std::vector<std::string> entityFiles = {
                "Entities/World.json", "Entities/Star.json", "Entities/DebugWorldGenerator.json", "Entities/Block.json",
                "Entities/Water.json", "Entities/Branch.json", "Entities/WireframeBlock.json", "Entities/TransparentWave.json"
            };
            for (const auto& filePath : entityFiles) {
                std::ifstream f(filePath);
                if (!f.is_open()) { std::cerr << "ERROR: Could not open entity file " << filePath << std::endl; continue; }
                Entity newProto = nlohmann::json::parse(f).get<Entity>();
                newProto.prototypeID = this->entityPrototypes.size();
                this->entityPrototypes.push_back(newProto);
            }
        }

        auto findProto = [&](const std::string& name) -> Entity* {
            for (auto& proto : entityPrototypes) { if (proto.name == name) return &proto; } return nullptr;
        };

        Entity* worldEntity = findProto("World");
        Entity* starProto = findProto("Star");
        Entity* worldGenAudicleProto = findProto("DebugWorldGenerator");

        if (worldEntity && worldGenAudicleProto) {
            Entity* blockProto = findProto("Block");
            Entity* waterProto = findProto("Water");
            Entity* branchProto = findProto("Branch");
            Entity* wireframeProto = findProto("WireframeBlock");

            if (blockProto && waterProto && branchProto && wireframeProto &&
                baseSystem.world->colorLibrary.count("Grass") > 0 &&
                baseSystem.world->colorLibrary.count("Water") > 0 &&
                baseSystem.world->colorLibrary.count("Wood") > 0)
            {
                int worldSize = 20;
                for (int x = -worldSize; x <= worldSize; ++x) {
                    for (int z = -worldSize; z <= worldSize; ++z) {
                        glm::vec3 pos(x, 0.0f, z);
                        if (x > 5 && x < 10 && z > 5 && z < 10) {
                             worldGenAudicleProto->instances.push_back(SystemLogic::CreateInstance(baseSystem, waterProto->prototypeID, pos, baseSystem.world->colorLibrary["Water"]));
                        } else {
                             worldGenAudicleProto->instances.push_back(SystemLogic::CreateInstance(baseSystem, blockProto->prototypeID, pos, baseSystem.world->colorLibrary["Grass"]));
                        }
                    }
                }
                worldGenAudicleProto->instances.push_back(SystemLogic::CreateInstance(baseSystem, blockProto->prototypeID, {-3, 1, -3}, baseSystem.world->colorLibrary["Wood"]));
                EntityInstance branchInst = SystemLogic::CreateInstance(baseSystem, branchProto->prototypeID, {-3, 2, -3}, baseSystem.world->colorLibrary["Leaf"]);
                branchInst.rotation = 45.0f;
                worldGenAudicleProto->instances.push_back(branchInst);
                worldGenAudicleProto->instances.push_back(SystemLogic::CreateInstance(baseSystem, wireframeProto->prototypeID, {0, 2, 5}, baseSystem.world->colorLibrary["PulsingGrid"]));
            }
            worldEntity->instances.push_back(SystemLogic::CreateInstance(baseSystem, worldGenAudicleProto->prototypeID, glm::vec3(0), {0,0,0}));
        }

        if (worldEntity && starProto) {
             for (int i = 0; i < baseSystem.world->numStars; i++) {
                float theta = static_cast<float>(rand())/RAND_MAX * 2.0f * 3.14159f;
                float phi = static_cast<float>(rand())/RAND_MAX * 3.14159f;
                glm::vec3 pos = glm::vec3(sin(phi)*cos(theta), cos(phi), sin(phi)*sin(theta)) * baseSystem.world->starDistance;
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

            // Execute update functions from the new SystemStep struct
            for(const auto& step : updateFunctions) {
                if(functionRegistry.count(step.name)) {
                    functionRegistry[step.name](baseSystem, entityPrototypes, deltaTime, window);
                } else {
                    std::cout << "WARNING: Update function '" << step.name << "' not found in registry." << std::endl;
                }
            }

            baseSystem.player->mouseOffsetX = 0.0f;
            baseSystem.player->mouseOffsetY = 0.0f;
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
