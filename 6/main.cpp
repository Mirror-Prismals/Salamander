#define GLM_ENABLE_EXPERIMENTAL
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <cstdlib>
#include <map>

#include "Procedures.cpp"
#include "BaseEntity.cpp"
#include "Entities.cpp"
#include "BaseSystem.cpp"
#include "Systems.cpp"

// --- Application Class (merged into main.cpp) ---
class Application {
private:
    GLFWwindow* window;
    std::vector<Entity> entityPrototypes;
    BaseSystem baseSystem;
    std::vector<std::unique_ptr<ISystem>> systems;
    PlayerControlSystem* playerControlSystem;
    
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;

public:
    void run() {
        init();
        mainLoop();
        cleanup();
    }

private:
    void init() {
        if (!glfwInit()) { std::cout << "Failed to initialize GLFW\n"; exit(-1); }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        #ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        #endif
        window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Prismals Engine", NULL, NULL);
        if (!window) { std::cout << "Failed to create GLFW window\n"; glfwTerminate(); exit(-1); }
        glfwMakeContextCurrent(window);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int width, int height){ glViewport(0,0,width,height); });
        glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y){
            static_cast<Application*>(glfwGetWindowUserPointer(w))->mouse_callback(x, y);
        });

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::cout << "Failed to initialize GLAD\n"; exit(-1); }

        initializeBlockColors();

        // --- Create systems ---
        systems.push_back(std::make_unique<InstanceSystem>());
        auto playerSys = std::make_unique<PlayerControlSystem>();
        playerControlSystem = playerSys.get();
        systems.push_back(std::move(playerSys));
        systems.push_back(std::make_unique<CameraSystem>());
        systems.push_back(std::make_unique<AudicleSystem>());
        auto renderSys = std::make_unique<RenderSystem>();
        renderSys->init();
        systems.push_back(std::move(renderSys));

        // --- Create entity prototypes ---
        entityPrototypes = createAllEntityPrototypes();

        // --- Get pointers to our special prototypes ---
        Entity* worldEntity = nullptr;
        Entity* starProto = nullptr;
        Entity* worldGenAudicleProto = nullptr;
        Entity* redBlockAudicleProto = nullptr; // <-- ADDED

        for (auto& proto : entityPrototypes) {
            if (proto.isWorld) worldEntity = &proto;
            if (proto.isStar) starProto = &proto;
            if (proto.name == "DebugWorldGenerator") worldGenAudicleProto = &proto;
            if (proto.name == "RedBlockMover") redBlockAudicleProto = &proto; // <-- ADDED
        }
        
        InstanceSystem instanceFactory;

        // --- World Generation via Audicle ---
        if (worldEntity && worldGenAudicleProto) {
            int grid_size = 5;
            float spacing = 3.0f;
            for (int i = 0; i < NUM_BLOCK_PROTOTYPES; ++i) {
                glm::vec3 pos((i % grid_size) * spacing, 0.0f, (i / grid_size) * spacing);
                worldGenAudicleProto->instances.push_back(instanceFactory.createInstance(i, pos));
            }
            worldEntity->instances.push_back(instanceFactory.createInstance(worldGenAudicleProto->prototypeID, glm::vec3(0)));
        }

        // new code could do this


        // Spawn stars
        if (worldEntity && starProto) {
             for (int i = 0; i < NUM_STARS; i++) {
                float theta = static_cast<float>(rand())/RAND_MAX * 2.0f * 3.14159f;
                float phi = static_cast<float>(rand())/RAND_MAX * 3.14159f * 0.5f;
                glm::vec3 pos = glm::vec3(sin(phi)*cos(theta), cos(phi), sin(phi)*sin(theta)) * STAR_DISTANCE;
                worldEntity->instances.push_back(instanceFactory.createInstance(starProto->prototypeID, pos));
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
            for (auto& system : systems) {
                system->update(entityPrototypes, baseSystem, deltaTime, window);
            }
            baseSystem.mouseOffsetX = 0.0f;
            baseSystem.mouseOffsetY = 0.0f;
            glfwSwapBuffers(window);
            glfwPollEvents();
        }
    }
    void mouse_callback(double xpos, double ypos) {
        playerControlSystem->processMouse(baseSystem, xpos, ypos);
    }
    void cleanup() {
        glfwTerminate();
    }
};

int main() {
    Application app;
    app.run();
    return 0;
}
