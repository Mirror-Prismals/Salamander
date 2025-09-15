#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>
#include <map>

struct SkyColorKey { 
    float time; 
    glm::vec3 top; 
    glm::vec3 bottom; 
};

struct BaseSystem {
    // --- Data loaded from Procedures/ ---
    unsigned int windowWidth = 1920;
    unsigned int windowHeight = 1080;
    int numBlockPrototypes = 25;
    int numStars = 1000;
    float starDistance = 1000.0f;
    std::vector<glm::vec3> blockColors;
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
};
