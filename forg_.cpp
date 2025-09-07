//======================================================================
// prismals_debug.cpp he c
// A simplified, single-file debug world to showcase all block types
// with a fly-mode camera and dynamic sky. (Corrected Shaders - FINAL)
// ======================================================================

#define GLM_ENABLE_EXPERIMENTAL

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <ctime>
#include <cstdlib>

// --- Global Constants ---
const unsigned int WINDOW_WIDTH = 1920;
const unsigned int WINDOW_HEIGHT = 1080;
const int NUM_BLOCK_TYPES = 25;

// --- Global Variables ---
glm::vec3 cameraPos = glm::vec3(6.0f, 5.0f, 15.0f); // Adjusted starting position
float cameraYaw = -90.0f;
float pitch = 0.0f;
float lastX = WINDOW_WIDTH / 2.0f;
float lastY = WINDOW_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// --- Forward Declarations ---
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xposIn, double yposIn);
void processInput(GLFWwindow* window);
GLuint compileShaderProgram(const char* vShaderSrc, const char* fShaderSrc);

// ======================================================================
// SHADER COMPILATION (Corrected)
// ======================================================================
GLuint compileShaderProgram(const char* vShaderSrc, const char* fShaderSrc) {
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vShaderSrc, NULL);
    glCompileShader(vertexShader);
    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) { glGetShaderInfoLog(vertexShader, 512, NULL, infoLog); std::cout << "VERTEX SHADER ERROR\n" << infoLog << "\n"; }
    
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fShaderSrc, NULL);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) { glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog); std::cout << "FRAGMENT SHADER ERROR\n" << infoLog << "\n"; }
    
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        std::cout << "SHADER PROGRAM LINKING ERROR\n" << infoLog << "\n";
    }
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return program;
}

// ======================================================================
// SKY, SUN, MOON, AND STARS
// ======================================================================

// --- Star Data ---
const int numStars = 1000;
const float starDistance = 1000.0f;
std::vector<glm::vec3> starPositions;

void generateStarPositions() {
    starPositions.reserve(numStars);
    for (int i = 0; i < numStars; i++) {
        float theta = static_cast<float>(rand()) / RAND_MAX * 2.0f * 3.14159f;
        float phi = static_cast<float>(rand()) / RAND_MAX * 3.14159f * 0.5f;
        float x = sin(phi) * cos(theta);
        float y = cos(phi);
        float z = sin(phi) * sin(theta);
        starPositions.push_back(glm::vec3(x, y, z) * starDistance);
    }
}

// --- Skybox Data ---
float skyboxQuadVertices[] = { -1.0f,  1.0f, -1.0f, -1.0f, 1.0f, -1.0f, -1.0f,  1.0f, 1.0f, -1.0f, 1.0f,  1.0f };
const char* skyboxVertexShaderSource = R"V0G0N(
#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 TexCoord;
void main(){ TexCoord = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }
)V0G0N";
const char* skyboxFragmentShaderSource = R"V0G0N(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform vec3 skyTop;
uniform vec3 skyBottom;
void main(){ vec3 color = mix(skyBottom, skyTop, TexCoord.y); FragColor = vec4(color, 1.0); }
)V0G0N";

// --- Sun/Moon Data (CORRECTED SHADERS) ---
const char* sunMoonVertexShaderSource = R"V0G0N(
#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 TexCoord; // Pass TexCoord to fragment shader
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main(){ TexCoord = aPos * 0.5 + 0.5; gl_Position = projection * view * model * vec4(aPos, 0.0, 1.0); }
)V0G0N";
const char* sunMoonFragmentShaderSource = R"V0G0N(
#version 330 core
in vec2 TexCoord; // Receive TexCoord from vertex shader
out vec4 FragColor;
uniform vec3 color;
uniform float brightness;
void main(){
    float d = distance(TexCoord, vec2(0.5));
    float diskAlpha = smoothstep(0.5, 0.45, d);
    float glowAlpha = 1.0 - smoothstep(0.45, 0.5, d);
    float finalAlpha = clamp(diskAlpha + 0.3 * glowAlpha, 0.0, 1.0);
    FragColor = vec4(color * brightness, finalAlpha * brightness);
}
)V0G0N";

// --- Star Data ---
const char* starVertexShaderSource = R"V0G0N(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 view;
uniform mat4 projection;
out float starSeed;
float computeSeed(vec3 pos) { return fract(sin(dot(pos, vec3(12.9898, 78.233, 37.719))) * 43758.5453); }
void main(){ gl_Position = projection * view * vec4(aPos, 1.0); starSeed = computeSeed(aPos); gl_PointSize = 2.0; }
)V0G0N";
const char* starFragmentShaderSource = R"V0G0N(
#version 330 core
in float starSeed;
uniform float time;
out vec4 FragColor;
void main(){
    float brightness = 0.8 + 0.2 * sin(time * 3.0 + starSeed * 10.0);
    float dist = length(gl_PointCoord - vec2(0.5));
    if(dist > 0.5) discard;
    FragColor = vec4(vec3(brightness), 1.0);
}
)V0G0N";

// --- Sky Color Data ---
struct SkyColorKey { float time; glm::vec3 top; glm::vec3 bottom; };
SkyColorKey skyKeys[5] = {
    { 0.0f,  glm::vec3(16/255.0f, 16/255.0f, 48/255.0f), glm::vec3(0.0f, 0.0f, 0.0f) },
    { 0.25f, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(128/255.0f, 128/255.0f, 1.0f) },
    { 0.5f,  glm::vec3(135/255.0f, 206/255.0f, 235/255.0f), glm::vec3(1.0f, 1.0f, 1.0f) },
    { 0.75f, glm::vec3(0.0f, 128/255.0f, 128/255.0f), glm::vec3(1.0f, 71/255.0f, 0.0f) },
    { 1.0f,  glm::vec3(16/255.0f, 16/255.0f, 48/255.0f), glm::vec3(0.0f, 0.0f, 0.0f) }
};

void getCurrentSkyColors(float dayFraction, glm::vec3& currentTop, glm::vec3& currentBottom) {
    int lowerIndex = 0;
    for (int i = 0; i < 4; i++) {
        if (dayFraction >= skyKeys[i].time && dayFraction <= skyKeys[i + 1].time) {
            lowerIndex = i;
            break;
        }
    }
    float t = (dayFraction - skyKeys[lowerIndex].time) / (skyKeys[lowerIndex + 1].time - skyKeys[lowerIndex].time);
    currentTop = glm::mix(skyKeys[lowerIndex].top, skyKeys[lowerIndex + 1].top, t);
    currentBottom = glm::mix(skyKeys[lowerIndex].bottom, skyKeys[lowerIndex + 1].bottom, t);
}

// ======================================================================
// BLOCK RENDERING
// ======================================================================

// --- Main Scene Shaders (Preserved from original) ---
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec3 aOffset;
layout (location = 4) in float aRotation;
out vec2 TexCoord;
out vec3 ourColor;
out float instanceDistance;
out vec3 Normal;
out vec3 WorldPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform int blockType;
uniform vec3 blockColors[25];
uniform vec3 cameraPos;
uniform float time;
void main(){
    vec3 pos;
    vec3 normal = aNormal;
    if(blockType != 14 && blockType != 18) {
        if(blockType == 1){
            vec3 waterDisplacement;
            waterDisplacement.x = sin(time * 0.5 + aOffset.x * 1.3 + aOffset.y * 0.7) * 0.2;
            waterDisplacement.y = sin(time * 0.5 + aOffset.y * 1.3 + aOffset.z * 0.7) * 0.2;
            waterDisplacement.z = sin(time * 0.5 + aOffset.z * 1.3 + aOffset.x * 0.7) * 0.2;
            pos = aPos * 1.2 + aOffset + waterDisplacement;
        } else if(blockType == 5) {
            vec3 lilyDisplacement;
            lilyDisplacement.x = sin(time * 0.5 + aOffset.x) * 0.1;
            lilyDisplacement.y = sin(time * 0.5 + aOffset.y) * 0.1;
            lilyDisplacement.z = sin(time * 0.5 + aOffset.z) * 0.1;
            pos = aPos * 1.2 + aOffset + lilyDisplacement;
        } else if(blockType == 3 || blockType == 7 || blockType == 9 || blockType == 17){
            vec3 leafDisplacement;
            leafDisplacement.x = sin((aOffset.x + time) * 0.3) * 0.05;
            leafDisplacement.y = cos((aOffset.y + time) * 0.3) * 0.05;
            leafDisplacement.z = sin((aOffset.z + time) * 0.3) * 0.05;
            pos = aPos + aOffset + leafDisplacement;
        } else {
            pos = aPos + aOffset;
        }
    } else {
        if(blockType == 14) {
            mat3 rot = mat3(cos(aRotation), 0.0, sin(aRotation), 0.0, 1.0, 0.0, -sin(aRotation), 0.0, cos(aRotation));
            mat3 scaleMat = mat3(0.3, 0.0, 0.0, 0.0, 0.8, 0.0, 0.0, 0.0, 0.3);
            pos = rot * (scaleMat * aPos) + aOffset;
            normal = rot * aNormal;
        } else {
            pos = aPos + aOffset;
        }
    }
    if(blockType == 19){ pos.y += sin(time + aOffset.x * 0.1) * 0.5; }
    vec4 worldPos4 = model * vec4(pos, 1.0);
    WorldPos = worldPos4.xyz;
    gl_Position = projection * view * worldPos4;
    ourColor = blockColors[blockType];
    TexCoord = aTexCoord;
    instanceDistance = length(aOffset - cameraPos);
    Normal = normalize(mat3(model) * normal);
}
)";
const char* fragmentShaderSource = R"(
#version 330 core
in vec2 TexCoord;
in vec3 ourColor;
in float instanceDistance;
in vec3 Normal;
in vec3 WorldPos;
out vec4 FragColor;
uniform int blockType;
uniform vec3 blockColors[25];
uniform vec3 lightDir;
uniform vec3 ambientLight;
uniform vec3 diffuseLight;
uniform float time;
uniform float blockGridSize; 

float generateNoise(vec2 cell) { return fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453); }
void main(){
    if(blockType == 19){ FragColor = vec4(ourColor, 0.1); return; }
    
    float lineWidth = 0.03;
    
    // --- THE FIX IS HERE ---
    // We now use the 'blockGridSize' uniform for ALL block types, including leaves.
    if(blockType == 3 || blockType == 7 || blockType == 9 || blockType == 17){ // Leaf blocks
        float leafNoiseSize = 12.0; // The noise pattern can remain finer
        vec2 f = fract(TexCoord * blockGridSize); // Use the uniform for the grid
        // --- END OF FIX ---
        
        bool isGridLine = (f.x < lineWidth || f.y < lineWidth);
        vec2 blockCoord = floor(WorldPos.xy);
        vec2 seed = fract(blockCoord * 0.12345);
        vec2 cell = floor((TexCoord + seed) * leafNoiseSize);
        float noiseVal = generateNoise(cell);
        float crackThreshold = 0.8;
        float noiseAlpha = (noiseVal > crackThreshold) ? 0.0 : 1.0;
        float finalAlpha = isGridLine ? 1.0 : noiseAlpha;
        vec3 finalColor = isGridLine ? vec3(0.0) : ourColor;
        vec3 norm = normalize(Normal);
        float diff = max(dot(norm, normalize(lightDir)), 0.0);
        vec3 lighting = ambientLight + diffuseLight * diff;
        finalColor *= lighting;
        FragColor = vec4(finalColor, finalAlpha);
        return;
    }
    
    if(blockType == 1){ // Water blocks
        vec3 waterColor = blockColors[1];
        float wave1 = sin(WorldPos.x * 0.1 + time * 2.0);
        float wave2 = cos(WorldPos.z * 0.1 + time * 2.0);
        float wave = (wave1 + wave2) * 0.5;
        waterColor *= (1.0 + wave * 0.1);
        vec3 norm = normalize(Normal);
        float diff = max(dot(norm, normalize(lightDir)), 0.0);
        vec3 lighting = ambientLight + diffuseLight * diff;
        vec3 finalColor = waterColor * lighting;
        FragColor = vec4(finalColor, 0.3);
    } else { // All other solid blocks
        vec2 f = fract(TexCoord * blockGridSize);
        vec3 baseColor;
        if(f.x < lineWidth || f.y < lineWidth)
            baseColor = vec3(0.0, 0.0, 0.0);
        else {
            float factor = instanceDistance / 100.0;
            vec3 offset = vec3(0.03 * factor, 0.03 * factor, 0.05 * factor);
            baseColor = ourColor + offset;
            baseColor = clamp(baseColor, 0.0, 1.0);
        }
        vec3 norm = normalize(Normal);
        float diff = max(dot(norm, normalize(lightDir)), 0.0);
        vec3 lighting = ambientLight + diffuseLight * diff;
        vec3 finalColor = baseColor * lighting;
        FragColor = vec4(finalColor, 1.0);
    }
}
)";
// --- Cube Vertex Data (Unchanged) ---
float cubeVertices[] = {
   -0.5f, -0.5f,  0.5f, 0,0,1, 0.0f, 0.0f, 0.5f, -0.5f,  0.5f, 0,0,1, 1.0f, 0.0f, 0.5f,  0.5f,  0.5f, 0,0,1, 1.0f, 1.0f, 0.5f,  0.5f,  0.5f, 0,0,1, 1.0f, 1.0f, -0.5f,  0.5f,  0.5f, 0,0,1, 0.0f, 1.0f, -0.5f, -0.5f,  0.5f, 0,0,1, 0.0f, 0.0f,
   0.5f, -0.5f,  0.5f, 1,0,0, 0.0f, 0.0f, 0.5f, -0.5f, -0.5f, 1,0,0, 1.0f, 0.0f, 0.5f,  0.5f, -0.5f, 1,0,0, 1.0f, 1.0f, 0.5f,  0.5f, -0.5f, 1,0,0, 1.0f, 1.0f, 0.5f,  0.5f,  0.5f, 1,0,0, 0.0f, 1.0f, 0.5f, -0.5f,  0.5f, 1,0,0, 0.0f, 0.0f,
   0.5f, -0.5f, -0.5f, 0,0,-1, 0.0f, 0.0f, -0.5f, -0.5f, -0.5f, 0,0,-1, 1.0f, 0.0f, -0.5f,  0.5f, -0.5f, 0,0,-1, 1.0f, 1.0f, -0.5f,  0.5f, -0.5f, 0,0,-1, 1.0f, 1.0f, 0.5f,  0.5f, -0.5f, 0,0,-1, 0.0f, 1.0f, 0.5f, -0.5f, -0.5f, 0,0,-1, 0.0f, 0.0f,
  -0.5f, -0.5f, -0.5f, -1,0,0, 0.0f, 0.0f, -0.5f, -0.5f,  0.5f, -1,0,0, 1.0f, 0.0f, -0.5f,  0.5f,  0.5f, -1,0,0, 1.0f, 1.0f, -0.5f,  0.5f,  0.5f, -1,0,0, 1.0f, 1.0f, -0.5f,  0.5f, -0.5f, -1,0,0, 0.0f, 1.0f, -0.5f, -0.5f, -0.5f, -1,0,0, 0.0f, 0.0f,
 -0.5f,  0.5f,  0.5f, 0,1,0, 0.0f, 0.0f, 0.5f,  0.5f,  0.5f, 0,1,0, 1.0f, 0.0f, 0.5f,  0.5f, -0.5f, 0,1,0, 1.0f, 1.0f, 0.5f,  0.5f, -0.5f, 0,1,0, 1.0f, 1.0f, -0.5f,  0.5f, -0.5f, 0,1,0, 0.0f, 1.0f, -0.5f,  0.5f,  0.5f, 0,1,0, 0.0f, 0.0f,
-0.5f, -0.5f, -0.5f, 0,-1,0, 0.0f, 0.0f, 0.5f, -0.5f, -0.5f, 0,-1,0, 1.0f, 0.0f, 0.5f, -0.5f,  0.5f, 0,-1,0, 1.0f, 1.0f, 0.5f, -0.5f,  0.5f, 0,-1,0, 1.0f, 1.0f, -0.5f, -0.5f,  0.5f, 0,-1,0, 0.0f, 1.0f, -0.5f, -0.5f, -0.5f, 0,-1,0, 0.0f, 0.0f
};

// --- Block Instance Data ---
std::vector<std::vector<glm::vec3>> blockInstances(NUM_BLOCK_TYPES);
std::vector<std::vector<glm::vec4>> branchInstances(1);

void createDebugWorld() {
    int grid_size = 5;
    float spacing = 3.0f;
    for (int i = 0; i < NUM_BLOCK_TYPES; ++i) {
        int x = i % grid_size;
        int z = i / grid_size;
        glm::vec3 position(x * spacing, 0.0f, z * spacing);
        
        if (i == 14) {
            branchInstances[0].push_back(glm::vec4(position, 0.0f));
        } else {
            blockInstances[i].push_back(position);
        }
    }
}

// ======================================================================
// MAIN APPLICATION
// ======================================================================
int main() {
    // --- GLFW & GLAD Initialization ---
    if (!glfwInit()) { std::cout << "Failed to initialize GLFW\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    #ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #endif
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Prismals Debug World", NULL, NULL);
    if (!window) { std::cout << "Failed to create GLFW window\n"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::cout << "Failed to initialize GLAD\n"; return -1; }

    // --- Compile Shaders ---
    GLuint shaderProgram = compileShaderProgram(vertexShaderSource, fragmentShaderSource);
    GLuint skyboxShaderProgram = compileShaderProgram(skyboxVertexShaderSource, skyboxFragmentShaderSource);
    GLuint sunMoonShaderProgram = compileShaderProgram(sunMoonVertexShaderSource, sunMoonFragmentShaderSource);
    GLuint starShaderProgram = compileShaderProgram(starVertexShaderSource, starFragmentShaderSource);

    // --- Setup Geometry and Buffers ---
    GLuint cubeVBO;
    glGenBuffers(1, &cubeVBO);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

    std::vector<GLuint> blockVAOs(NUM_BLOCK_TYPES);
    glGenVertexArrays(NUM_BLOCK_TYPES, blockVAOs.data());
    
    GLuint instanceVBO, branchInstanceVBO;
    glGenBuffers(1, &instanceVBO);
    glGenBuffers(1, &branchInstanceVBO);

    for (int i = 0; i < NUM_BLOCK_TYPES; ++i) {
        glBindVertexArray(blockVAOs[i]);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);

        if (i == 14) {
            glBindBuffer(GL_ARRAY_BUFFER, branchInstanceVBO);
            glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), (void*)0);
            glEnableVertexAttribArray(3);
            glVertexAttribDivisor(3, 1);
            glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), (void*)(sizeof(glm::vec3)));
            glEnableVertexAttribArray(4);
            glVertexAttribDivisor(4, 1);
        } else {
            glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
            glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
            glEnableVertexAttribArray(3);
            glVertexAttribDivisor(3, 1);
        }
    }

    GLuint skyboxVAO, skyboxVBO;
    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);
    glBindVertexArray(skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxQuadVertices), skyboxQuadVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    GLuint sunMoonVAO, sunMoonVBO;
    glGenVertexArrays(1, &sunMoonVAO);
    glGenBuffers(1, &sunMoonVBO);
    glBindVertexArray(sunMoonVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sunMoonVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxQuadVertices), skyboxQuadVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    generateStarPositions();
    GLuint starVAO, starVBO;
    glGenVertexArrays(1, &starVAO);
    glGenBuffers(1, &starVBO);
    glBindVertexArray(starVAO);
    glBindBuffer(GL_ARRAY_BUFFER, starVBO);
    glBufferData(GL_ARRAY_BUFFER, starPositions.size() * sizeof(glm::vec3), starPositions.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(0);

    createDebugWorld();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_PROGRAM_POINT_SIZE);

    // --- Main Render Loop ---
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        
        processInput(window);

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::vec3 front;
        front.x = cos(glm::radians(cameraYaw)) * cos(glm::radians(pitch));
        front.y = sin(glm::radians(pitch));
        front.z = sin(glm::radians(cameraYaw)) * cos(glm::radians(pitch));
        front = glm::normalize(front);
        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + front, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(glm::radians(103.0f), (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT, 0.1f, 2000.0f);

        time_t currentTimeT = time(0);
        tm localTimeInfo;
        #ifdef _WIN32
        localtime_s(&localTimeInfo, &currentTimeT);
        #else
        localtime_r(&currentTimeT, &localTimeInfo);
        #endif
        float dayFraction = (localTimeInfo.tm_hour * 3600 + localTimeInfo.tm_min * 60 + localTimeInfo.tm_sec) / 86400.0f;
        glm::vec3 skyTop, skyBottom;
        getCurrentSkyColors(dayFraction, skyTop, skyBottom);
        
        glDepthMask(GL_FALSE);
        glUseProgram(skyboxShaderProgram);
        glUniform3fv(glGetUniformLocation(skyboxShaderProgram, "skyTop"), 1, glm::value_ptr(skyTop));
        glUniform3fv(glGetUniformLocation(skyboxShaderProgram, "skyBottom"), 1, glm::value_ptr(skyBottom));
        glBindVertexArray(skyboxVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        glUseProgram(starShaderProgram);
        glUniform1f(glGetUniformLocation(starShaderProgram, "time"), currentFrame);
        glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));
        glUniformMatrix4fv(glGetUniformLocation(starShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(viewNoTranslation));
        glUniformMatrix4fv(glGetUniformLocation(starShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glBindVertexArray(starVAO);
        glDrawArrays(GL_POINTS, 0, starPositions.size());
        
        float hour = dayFraction * 24.0f;
        glm::vec3 sunDir, moonDir;
        float sunBrightness = 0.0f, moonBrightness = 0.0f;
        if (hour >= 6.0f && hour < 18.0f) {
            float u = (hour - 6.0f) / 12.0f;
            sunDir = glm::normalize(glm::vec3(0.0f, sin(u * 3.14159f), -cos(u * 3.14159f)));
            sunBrightness = sin(u * 3.14159f);
        } else {
            float adjustedHour = (hour < 6.0f) ? hour + 24.0f : hour;
            float u = (adjustedHour - 18.0f) / 12.0f;
            moonDir = glm::normalize(glm::vec3(0.0f, sin(u * 3.14159f), -cos(u * 3.14159f)));
            moonBrightness = sin(u * 3.14159f);
        }
        
        glUseProgram(sunMoonShaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(sunMoonShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(sunMoonShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        if (sunBrightness > 0.01f) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), cameraPos + sunDir * 500.0f);
            model = glm::scale(model, glm::vec3(50.0f));
            glUniformMatrix4fv(glGetUniformLocation(sunMoonShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniform3f(glGetUniformLocation(sunMoonShaderProgram, "color"), 1.0f, 1.0f, 0.8f);
            glUniform1f(glGetUniformLocation(sunMoonShaderProgram, "brightness"), sunBrightness);
            glBindVertexArray(sunMoonVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        if (moonBrightness > 0.01f) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), cameraPos + moonDir * 500.0f);
            model = glm::scale(model, glm::vec3(40.0f));
            glUniformMatrix4fv(glGetUniformLocation(sunMoonShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniform3f(glGetUniformLocation(sunMoonShaderProgram, "color"), 0.9f, 0.9f, 1.0f);
            glUniform1f(glGetUniformLocation(sunMoonShaderProgram, "brightness"), moonBrightness);
            glBindVertexArray(sunMoonVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        glDepthMask(GL_TRUE);

        // --- Draw Blocks ---
        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(glGetUniformLocation(shaderProgram, "cameraPos"), 1, glm::value_ptr(cameraPos));
        glUniform1f(glGetUniformLocation(shaderProgram, "time"), currentFrame);
        
        glm::vec3 lightDirVec = sunBrightness > 0.0f ? sunDir : moonDir;
        glUniform3fv(glGetUniformLocation(shaderProgram, "lightDir"), 1, glm::value_ptr(lightDirVec));
        glUniform3fv(glGetUniformLocation(shaderProgram, "ambientLight"), 1, glm::value_ptr(glm::vec3(0.4f)));
        glUniform3fv(glGetUniformLocation(shaderProgram, "diffuseLight"), 1, glm::value_ptr(glm::vec3(0.6f)));
        
        glm::vec3 blockColors[NUM_BLOCK_TYPES];
        blockColors[0] = glm::vec3(0.19f, 0.66f, 0.32f); blockColors[1] = glm::vec3(0.0f, 0.5f, 1.0f);
        blockColors[2] = glm::vec3(0.29f, 0.21f, 0.13f); blockColors[3] = glm::vec3(0.07f, 0.46f, 0.34f);
        blockColors[4] = glm::vec3(1.0f, 0.0f, 0.0f); blockColors[5] = glm::vec3(0.2f, 0.7f, 0.2f);
        blockColors[6] = glm::vec3(0.45f, 0.22f, 0.07f); blockColors[7] = glm::vec3(0.13f, 0.54f, 0.13f);
        blockColors[8] = glm::vec3(0.55f, 0.27f, 0.07f); blockColors[9] = glm::vec3(0.36f, 0.6f, 0.33f);
        blockColors[10] = glm::vec3(0.44f, 0.39f, 0.32f); blockColors[11] = glm::vec3(0.35f, 0.43f, 0.30f);
        blockColors[12] = glm::vec3(0.52f, 0.54f, 0.35f); blockColors[13] = glm::vec3(0.6f, 0.61f, 0.35f);
        blockColors[14] = glm::vec3(0.4f, 0.3f, 0.2f); blockColors[15] = glm::vec3(0.43f, 0.39f, 0.34f);
        blockColors[16] = glm::vec3(0.4f, 0.25f, 0.1f); blockColors[17] = glm::vec3(0.2f, 0.5f, 0.2f);
        blockColors[18] = glm::vec3(0.3f, 0.2f, 0.1f); blockColors[19] = glm::vec3(0.2f, 0.8f, 0.9f);
        blockColors[20] = glm::vec3(0.5f, 0.5f, 0.5f); blockColors[21] = glm::vec3(1.0f, 0.5f, 0.0f);
        blockColors[22] = glm::vec3(0.93f, 0.79f, 0.69f); blockColors[23] = glm::vec3(0.95f, 0.95f, 1.0f);
        blockColors[24] = glm::vec3(0.8f, 0.9f, 1.0f);
        glUniform3fv(glGetUniformLocation(shaderProgram, "blockColors"), NUM_BLOCK_TYPES, glm::value_ptr(blockColors[0]));

        glm::mat4 model = glm::mat4(1.0f);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));

        for (int i = 0; i < NUM_BLOCK_TYPES; ++i) {
            // --- THE DEFINITIVE FIX IS HERE ---
            // All blocks, including leaves, will now use the 24.0f grid size.
            glUniform1f(glGetUniformLocation(shaderProgram, "blockGridSize"), 24.0f);
            // --- END OF FIX ---

            if (i == 14) {
                if (!branchInstances[0].empty()) {
                    glUniform1i(glGetUniformLocation(shaderProgram, "blockType"), i);
                    glBindVertexArray(blockVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, branchInstanceVBO);
                    glBufferData(GL_ARRAY_BUFFER, branchInstances[0].size() * sizeof(glm::vec4), branchInstances[0].data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, branchInstances[0].size());
                }
            } else {
                if (!blockInstances[i].empty()) {
                    glUniform1i(glGetUniformLocation(shaderProgram, "blockType"), i);
                    glBindVertexArray(blockVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
                    glBufferData(GL_ARRAY_BUFFER, blockInstances[i].size() * sizeof(glm::vec3), blockInstances[i].data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, blockInstances[i].size());
                }
            }
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // --- Cleanup ---
    glDeleteVertexArrays(NUM_BLOCK_TYPES, blockVAOs.data());
    glDeleteBuffers(1, &cubeVBO);
    glDeleteBuffers(1, &instanceVBO);
    glDeleteBuffers(1, &branchInstanceVBO);
    glDeleteProgram(shaderProgram);
    glfwTerminate();
    return 0;
}

// --- Input and Window Callbacks ---
void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    float cameraSpeed = 5.0f * deltaTime;
    glm::vec3 front = glm::vec3(cos(glm::radians(cameraYaw)), 0.0f, sin(glm::radians(cameraYaw)));
    front = glm::normalize(front);
    glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
    
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        cameraPos += front * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        cameraPos -= front * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        cameraPos -= right * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        cameraPos += right * cameraSpeed;

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        cameraPos.y += cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        cameraPos.y -= cameraSpeed;
}

void mouse_callback(GLFWwindow* window, double xposIn, double yposIn) {
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;
    lastX = xpos; lastY = ypos;
    const float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;
    cameraYaw += xoffset;
    pitch += yoffset;
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}
