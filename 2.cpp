// ======================================================================
// prismals_refactor_v1.cpp
// A single-file, refactored debug world to showcase all block types.
// This version uses classes for better organization as a first step.
// ======================================================================

#define GLM_ENABLE_EXPERIMENTAL

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <ctime>
#include <cstdlib>

// --- Constants ---
const unsigned int WINDOW_WIDTH = 1920;
const unsigned int WINDOW_HEIGHT = 1080;
const int NUM_BLOCK_TYPES = 25;
const int NUM_STARS = 1000;
const float STAR_DISTANCE = 1000.0f;

// --- Forward Declarations ---
class Shader;
class Camera;
class World;
class Renderer;
// --- THE FIX IS HERE ---
void getCurrentSkyColors(float dayFraction, glm::vec3& currentTop, glm::vec3& currentBottom);
// --- END OF FIX ---

// --- Global Pointer for Callbacks ---
Camera* g_Camera = nullptr;

// ======================================================================
// Shader Class
// ======================================================================
class Shader {
public:
    unsigned int ID;

    Shader(const char* vertexCode, const char* fragmentCode) {
        unsigned int vertex, fragment;
        vertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex, 1, &vertexCode, NULL);
        glCompileShader(vertex);
        checkCompileErrors(vertex, "VERTEX");

        fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment, 1, &fragmentCode, NULL);
        glCompileShader(fragment);
        checkCompileErrors(fragment, "FRAGMENT");

        ID = glCreateProgram();
        glAttachShader(ID, vertex);
        glAttachShader(ID, fragment);
        glLinkProgram(ID);
        checkCompileErrors(ID, "PROGRAM");

        glDeleteShader(vertex);
        glDeleteShader(fragment);
    }

    void use() {
        glUseProgram(ID);
    }

    void setMat4(const std::string& name, const glm::mat4& mat) const {
        glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
    }
    void setVec3(const std::string& name, const glm::vec3& value) const {
        glUniform3fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
    }
    void setFloat(const std::string& name, float value) const {
        glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
    }
    void setInt(const std::string& name, int value) const {
        glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
    }

private:
    void checkCompileErrors(unsigned int shader, std::string type) {
        int success;
        char infoLog[1024];
        if (type != "PROGRAM") {
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success) {
                glGetShaderInfoLog(shader, 1024, NULL, infoLog);
                std::cout << "SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << "\n";
            }
        } else {
            glGetProgramiv(shader, GL_LINK_STATUS, &success);
            if (!success) {
                glGetProgramInfoLog(shader, 1024, NULL, infoLog);
                std::cout << "PROGRAM_LINKING_ERROR of type: " << type << "\n" << infoLog << "\n";
            }
        }
    }
};

// ======================================================================
// Camera Class (Fly Mode)
// ======================================================================
class Camera {
public:
    glm::vec3 Position;
    float Yaw;
    float Pitch;

    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f)) : Position(position), Yaw(-90.0f), Pitch(0.0f) {}

    glm::mat4 getViewMatrix() {
        glm::vec3 front;
        front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        front.y = sin(glm::radians(Pitch));
        front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        front = glm::normalize(front);
        return glm::lookAt(Position, Position + front, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    void processKeyboard(GLFWwindow* window, float deltaTime) {
        float cameraSpeed = 5.0f * deltaTime;
        glm::vec3 front;
        front.x = cos(glm::radians(Yaw));
        front.y = 0.0f; // Fly mode: horizontal movement
        front.z = sin(glm::radians(Yaw));
        front = glm::normalize(front);
        glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) Position += front * cameraSpeed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) Position -= front * cameraSpeed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) Position -= right * cameraSpeed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) Position += right * cameraSpeed;
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) Position.y += cameraSpeed;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) Position.y -= cameraSpeed;
    }

    void processMouseMovement(float xoffset, float yoffset) {
        const float sensitivity = 0.1f;
        xoffset *= sensitivity;
        yoffset *= sensitivity;

        Yaw += xoffset;
        Pitch += yoffset;

        if (Pitch > 89.0f) Pitch = 89.0f;
        if (Pitch < -89.0f) Pitch = -89.0f;
    }
};

// ======================================================================
// World Class
// ======================================================================
class World {
public:
    std::vector<std::vector<glm::vec3>> blockInstances;
    std::vector<std::vector<glm::vec4>> branchInstances;

    World() : blockInstances(NUM_BLOCK_TYPES), branchInstances(1) {}

    void createDebugWorld() {
        int grid_size = 5;
        float spacing = 3.0f;
        for (int i = 0; i < NUM_BLOCK_TYPES; ++i) {
            int x = i % grid_size;
            int z = i / grid_size;
            glm::vec3 position(x * spacing, 0.0f, z * spacing);
            
            if (i == 14) { // Branch block
                branchInstances[0].push_back(glm::vec4(position, 0.0f));
            } else {
                blockInstances[i].push_back(position);
            }
        }
        std::cout << "Debug world created with " << NUM_BLOCK_TYPES << " block types." << std::endl;
    }
};

// ======================================================================
// Renderer Class
// ======================================================================
class Renderer {
private:
    Shader* blockShader = nullptr;
    Shader* skyboxShader = nullptr;
    Shader* sunMoonShader = nullptr;
    Shader* starShader = nullptr;

    GLuint cubeVBO = 0;
    std::vector<GLuint> blockVAOs;
    GLuint instanceVBO = 0, branchInstanceVBO = 0;
    
    GLuint skyboxVAO = 0, skyboxVBO = 0;
    GLuint sunMoonVAO = 0, sunMoonVBO = 0;
    GLuint starVAO = 0, starVBO = 0;
    std::vector<glm::vec3> starPositions;

public:
    Renderer() : blockVAOs(NUM_BLOCK_TYPES) {}

    ~Renderer() {
        delete blockShader;
        delete skyboxShader;
        delete sunMoonShader;
        delete starShader;
    }

    void init() {
        #pragma region Shader Sources
        const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos; layout (location = 1) in vec3 aNormal; layout (location = 2) in vec2 aTexCoord;
        layout (location = 3) in vec3 aOffset; layout (location = 4) in float aRotation;
        out vec2 TexCoord; out vec3 ourColor; out float instanceDistance; out vec3 Normal; out vec3 WorldPos;
        uniform mat4 model; uniform mat4 view; uniform mat4 projection; uniform int blockType;
        uniform vec3 blockColors[25]; uniform vec3 cameraPos; uniform float time;
        void main(){
            vec3 pos; vec3 normal = aNormal;
            if(blockType != 14 && blockType != 18) {
                if(blockType == 1){ vec3 d; d.x=sin(time*0.5+aOffset.x*1.3+aOffset.y*0.7)*0.2; d.y=sin(time*0.5+aOffset.y*1.3+aOffset.z*0.7)*0.2; d.z=sin(time*0.5+aOffset.z*1.3+aOffset.x*0.7)*0.2; pos=aPos*1.2+aOffset+d; }
                else if(blockType == 5){ vec3 d; d.x=sin(time*0.5+aOffset.x)*0.1; d.y=sin(time*0.5+aOffset.y)*0.1; d.z=sin(time*0.5+aOffset.z)*0.1; pos=aPos*1.2+aOffset+d; }
                else if(blockType==3||blockType==7||blockType==9||blockType==17){ vec3 d; d.x=sin((aOffset.x+time)*0.3)*0.05; d.y=cos((aOffset.y+time)*0.3)*0.05; d.z=sin((aOffset.z+time)*0.3)*0.05; pos=aPos+aOffset+d; }
                else { pos = aPos + aOffset; }
            } else {
                if(blockType == 14) { mat3 r=mat3(cos(aRotation),0,sin(aRotation),0,1,0,-sin(aRotation),0,cos(aRotation)); mat3 s=mat3(0.3,0,0,0,0.8,0,0,0,0.3); pos=r*(s*aPos)+aOffset; normal=r*aNormal; }
                else { pos = aPos + aOffset; }
            }
            if(blockType == 19){ pos.y += sin(time + aOffset.x * 0.1) * 0.5; }
            vec4 worldPos4 = model * vec4(pos, 1.0); WorldPos = worldPos4.xyz;
            gl_Position = projection * view * worldPos4;
            ourColor = blockColors[blockType]; TexCoord = aTexCoord;
            instanceDistance = length(aOffset - cameraPos); Normal = normalize(mat3(model) * normal);
        })";
        const char* fragmentShaderSource = R"(
        #version 330 core
        in vec2 TexCoord; in vec3 ourColor; in float instanceDistance; in vec3 Normal; in vec3 WorldPos;
        out vec4 FragColor;
        uniform int blockType; uniform vec3 blockColors[25]; uniform vec3 lightDir;
        uniform vec3 ambientLight; uniform vec3 diffuseLight; uniform float time;
        float noise(vec2 p){ return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
        void main(){
            if(blockType == 19){ FragColor = vec4(ourColor, 0.1); return; }
            float grid = 24.0; float line = 0.03;
            if(blockType==3||blockType==7||blockType==9||blockType==17){
                vec2 f=fract(TexCoord*12.0); bool g=(f.x<line||f.y<line);
                float n=noise(floor((TexCoord+fract(floor(WorldPos.xy)*0.12345))*12.0));
                float a=(n>0.8)?0.0:1.0; float fa=g?1.0:a; vec3 c=g?vec3(0):ourColor;
                vec3 l=ambientLight+diffuseLight*max(dot(normalize(Normal),normalize(lightDir)),0.0); c*=l;
                FragColor=vec4(c,fa); return;
            }
            if(blockType == 1){
                vec3 wc=blockColors[1]; float w=sin(WorldPos.x*0.1+time*2.0)+cos(WorldPos.z*0.1+time*2.0); wc*=(1.0+w*0.05);
                vec3 l=ambientLight+diffuseLight*max(dot(normalize(Normal),normalize(lightDir)),0.0); vec3 fc=wc*l;
                FragColor=vec4(fc,0.3);
            } else {
                vec2 f=fract(TexCoord*grid); vec3 bc;
                if(f.x<line||f.y<line){bc=vec3(0);}else{float d=instanceDistance/100.0;bc=ourColor+vec3(0.03*d);bc=clamp(bc,0,1);}
                vec3 l=ambientLight+diffuseLight*max(dot(normalize(Normal),normalize(lightDir)),0.0); vec3 fc=bc*l;
                FragColor=vec4(fc,1.0);
            }
        })";
        const char* skyboxVertexShaderSource = R"(#version 330 core layout(location=0)in vec2 a;out vec2 t;void main(){t=a*0.5+0.5;gl_Position=vec4(a,0,1);})";
        const char* skyboxFragmentShaderSource = R"(#version 330 core out vec4 f;in vec2 t;uniform vec3 sT;uniform vec3 sB;void main(){f=vec4(mix(sB,sT,t.y),1);})";
        const char* sunMoonVertexShaderSource = R"(#version 330 core layout(location=0)in vec2 a;out vec2 t;uniform mat4 m;uniform mat4 v;uniform mat4 p;void main(){t=a*0.5+0.5;gl_Position=p*v*m*vec4(a,0,1);})";
        const char* sunMoonFragmentShaderSource = R"(#version 330 core in vec2 t;out vec4 f;uniform vec3 c;uniform float b;void main(){float d=distance(t,vec2(0.5));float da=smoothstep(0.5,0.45,d);float ga=1.0-smoothstep(0.45,0.5,d);float fa=clamp(da+0.3*ga,0,1);f=vec4(c*b,fa*b);})";
        const char* starVertexShaderSource = R"(#version 330 core layout(location=0)in vec3 a;uniform mat4 v;uniform mat4 p;out float s;float cs(vec3 o){return fract(sin(dot(o,vec3(12.9898,78.233,37.719)))*43758.5453);}void main(){gl_Position=p*v*vec4(a,1);s=cs(a);gl_PointSize=2.0;})";
        const char* starFragmentShaderSource = R"(#version 330 core in float s;uniform float t;out vec4 f;void main(){float b=0.8+0.2*sin(t*3.0+s*10.0);if(length(gl_PointCoord-vec2(0.5))>0.5)discard;f=vec4(vec3(b),1);})";
        #pragma endregion
        
        blockShader = new Shader(vertexShaderSource, fragmentShaderSource);
        skyboxShader = new Shader(skyboxVertexShaderSource, skyboxFragmentShaderSource);
        sunMoonShader = new Shader(sunMoonVertexShaderSource, sunMoonFragmentShaderSource);
        starShader = new Shader(starVertexShaderSource, starFragmentShaderSource);

        float cubeVertices[] = {
           -0.5f,-0.5f,0.5f,0,0,1,0,0,0.5f,-0.5f,0.5f,0,0,1,1,0,0.5f,0.5f,0.5f,0,0,1,1,1,0.5f,0.5f,0.5f,0,0,1,1,1,-0.5f,0.5f,0.5f,0,0,1,0,1,-0.5f,-0.5f,0.5f,0,0,1,0,0,
           0.5f,-0.5f,0.5f,1,0,0,0,0,0.5f,-0.5f,-0.5f,1,0,0,1,0,0.5f,0.5f,-0.5f,1,0,0,1,1,0.5f,0.5f,-0.5f,1,0,0,1,1,0.5f,0.5f,0.5f,1,0,0,0,1,0.5f,-0.5f,0.5f,1,0,0,0,0,
           0.5f,-0.5f,-0.5f,0,0,-1,0,0,-0.5f,-0.5f,-0.5f,0,0,-1,1,0,-0.5f,0.5f,-0.5f,0,0,-1,1,1,-0.5f,0.5f,-0.5f,0,0,-1,1,1,0.5f,0.5f,-0.5f,0,0,-1,0,1,0.5f,-0.5f,-0.5f,0,0,-1,0,0,
           -0.5f,-0.5f,-0.5f,-1,0,0,0,0,-0.5f,-0.5f,0.5f,-1,0,0,1,0,-0.5f,0.5f,0.5f,-1,0,0,1,1,-0.5f,0.5f,0.5f,-1,0,0,1,1,-0.5f,0.5f,-0.5f,-1,0,0,0,1,-0.5f,-0.5f,-0.5f,-1,0,0,0,0,
           -0.5f,0.5f,0.5f,0,1,0,0,0,0.5f,0.5f,0.5f,0,1,0,1,0,0.5f,0.5f,-0.5f,0,1,0,1,1,0.5f,0.5f,-0.5f,0,1,0,1,1,-0.5f,0.5f,-0.5f,0,1,0,0,1,-0.5f,0.5f,0.5f,0,1,0,0,0,
           -0.5f,-0.5f,-0.5f,0,-1,0,0,0,0.5f,-0.5f,-0.5f,0,-1,0,1,0,0.5f,-0.5f,0.5f,0,-1,0,1,1,0.5f,-0.5f,0.5f,0,-1,0,1,1,-0.5f,-0.5f,0.5f,0,-1,0,0,1,-0.5f,-0.5f,-0.5f,0,-1,0,0,0
        };
        glGenBuffers(1, &cubeVBO);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

        glGenVertexArrays(NUM_BLOCK_TYPES, blockVAOs.data());
        glGenBuffers(1, &instanceVBO);
        glGenBuffers(1, &branchInstanceVBO);

        for (int i = 0; i < NUM_BLOCK_TYPES; ++i) {
            glBindVertexArray(blockVAOs[i]);
            glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
            if (i == 14) {
                glBindBuffer(GL_ARRAY_BUFFER, branchInstanceVBO);
                glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), (void*)0); glEnableVertexAttribArray(3); glVertexAttribDivisor(3, 1);
                glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), (void*)(sizeof(glm::vec3))); glEnableVertexAttribArray(4); glVertexAttribDivisor(4, 1);
            } else {
                glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
                glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0); glEnableVertexAttribArray(3); glVertexAttribDivisor(3, 1);
            }
        }

        float skyboxQuadVertices[] = {-1,1,-1,-1,1,-1,-1,1,1,-1,1,1};
        glGenVertexArrays(1, &skyboxVAO); glGenBuffers(1, &skyboxVBO);
        glBindVertexArray(skyboxVAO); glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxQuadVertices), skyboxQuadVertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);

        glGenVertexArrays(1, &sunMoonVAO); glGenBuffers(1, &sunMoonVBO);
        glBindVertexArray(sunMoonVAO); glBindBuffer(GL_ARRAY_BUFFER, sunMoonVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxQuadVertices), skyboxQuadVertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);

        starPositions.reserve(NUM_STARS);
        for(int i=0;i<NUM_STARS;i++){float t=static_cast<float>(rand())/RAND_MAX*2.f*3.14159f;float p=static_cast<float>(rand())/RAND_MAX*3.14159f*0.5f;starPositions.push_back(glm::vec3(sin(p)*cos(t),cos(p),sin(p)*sin(t))*STAR_DISTANCE);}
        glGenVertexArrays(1, &starVAO); glGenBuffers(1, &starVBO);
        glBindVertexArray(starVAO); glBindBuffer(GL_ARRAY_BUFFER, starVBO);
        glBufferData(GL_ARRAY_BUFFER, starPositions.size() * sizeof(glm::vec3), starPositions.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0); glEnableVertexAttribArray(0);
    }

    void draw(const World& world, Camera& camera, float time) {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = camera.getViewMatrix();
        glm::mat4 projection = glm::perspective(glm::radians(103.0f), (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT, 0.1f, 2000.0f);

        time_t ct; std::time(&ct); tm lt;
        #ifdef _WIN32
        localtime_s(&lt, &ct);
        #else
        localtime_r(&ct, &lt);
        #endif
        float dayFraction = (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec) / 86400.0f;
        glm::vec3 skyTop, skyBottom;
        getCurrentSkyColors(dayFraction, skyTop, skyBottom);
        
        glDepthMask(GL_FALSE);
        skyboxShader->use();
        skyboxShader->setVec3("sT", skyTop);
        skyboxShader->setVec3("sB", skyBottom);
        glBindVertexArray(skyboxVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        starShader->use();
        starShader->setFloat("t", time);
        glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));
        starShader->setMat4("v", viewNoTranslation);
        starShader->setMat4("p", projection);
        glBindVertexArray(starVAO);
        glDrawArrays(GL_POINTS, 0, starPositions.size());
        
        float hour = dayFraction * 24.0f;
        glm::vec3 sunDir, moonDir;
        float sunBrightness = 0.0f, moonBrightness = 0.0f;
        if(hour>=6&&hour<18){float u=(hour-6)/12.f;sunDir=glm::normalize(glm::vec3(0,sin(u*3.14159f),-cos(u*3.14159f)));sunBrightness=sin(u*3.14159f);}
        else{float aH=(hour<6)?hour+24:hour;float u=(aH-18)/12.f;moonDir=glm::normalize(glm::vec3(0,sin(u*3.14159f),-cos(u*3.14159f)));moonBrightness=sin(u*3.14159f);}
        
        sunMoonShader->use();
        sunMoonShader->setMat4("v", view);
        sunMoonShader->setMat4("p", projection);
        if(sunBrightness>0.01f){glm::mat4 m=glm::translate(glm::mat4(1),camera.Position+sunDir*500.f);m=glm::scale(m,glm::vec3(50));sunMoonShader->setMat4("m",m);sunMoonShader->setVec3("c",glm::vec3(1,1,0.8f));sunMoonShader->setFloat("b",sunBrightness);glBindVertexArray(sunMoonVAO);glDrawArrays(GL_TRIANGLES,0,6);}
        if(moonBrightness>0.01f){glm::mat4 m=glm::translate(glm::mat4(1),camera.Position+moonDir*500.f);m=glm::scale(m,glm::vec3(40));sunMoonShader->setMat4("m",m);sunMoonShader->setVec3("c",glm::vec3(0.9f,0.9f,1));sunMoonShader->setFloat("b",moonBrightness);glBindVertexArray(sunMoonVAO);glDrawArrays(GL_TRIANGLES,0,6);}
        glDepthMask(GL_TRUE);

        blockShader->use();
        blockShader->setMat4("view", view);
        blockShader->setMat4("projection", projection);
        blockShader->setVec3("cameraPos", camera.Position);
        blockShader->setFloat("time", time);
        glm::vec3 lightDir = sunBrightness > 0.0f ? sunDir : moonDir;
        blockShader->setVec3("lightDir", lightDir);
        blockShader->setVec3("ambientLight", glm::vec3(0.4f));
        blockShader->setVec3("diffuseLight", glm::vec3(0.6f));
        
        glm::vec3 blockColors[NUM_BLOCK_TYPES];
        blockColors[0]={0.19,0.66,0.32}; blockColors[1]={0,0.5,1}; blockColors[2]={0.29,0.21,0.13}; blockColors[3]={0.07,0.46,0.34};
        blockColors[4]={1,0,0}; blockColors[5]={0.2,0.7,0.2}; blockColors[6]={0.45,0.22,0.07}; blockColors[7]={0.13,0.54,0.13};
        blockColors[8]={0.55,0.27,0.07}; blockColors[9]={0.36,0.6,0.33}; blockColors[10]={0.44,0.39,0.32}; blockColors[11]={0.35,0.43,0.30};
        blockColors[12]={0.52,0.54,0.35}; blockColors[13]={0.6,0.61,0.35}; blockColors[14]={0.4,0.3,0.2}; blockColors[15]={0.43,0.39,0.34};
        blockColors[16]={0.4,0.25,0.1}; blockColors[17]={0.2,0.5,0.2}; blockColors[18]={0.3,0.2,0.1}; blockColors[19]={0.2,0.8,0.9};
        blockColors[20]={0.5,0.5,0.5}; blockColors[21]={1,0.5,0}; blockColors[22]={0.93,0.79,0.69}; blockColors[23]={0.95,0.95,1};
        blockColors[24]={0.8,0.9,1};
        glUniform3fv(glGetUniformLocation(blockShader->ID, "blockColors"), NUM_BLOCK_TYPES, glm::value_ptr(blockColors[0]));

        blockShader->setMat4("model", glm::mat4(1.0f));

        for (int i = 0; i < NUM_BLOCK_TYPES; ++i) {
            if (i == 14) {
                if (!world.branchInstances[0].empty()) {
                    blockShader->setInt("blockType", i);
                    glBindVertexArray(blockVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, branchInstanceVBO);
                    glBufferData(GL_ARRAY_BUFFER, world.branchInstances[0].size() * sizeof(glm::vec4), world.branchInstances[0].data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, world.branchInstances[0].size());
                }
            } else {
                if (!world.blockInstances[i].empty()) {
                    blockShader->setInt("blockType", i);
                    glBindVertexArray(blockVAOs[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
                    glBufferData(GL_ARRAY_BUFFER, world.blockInstances[i].size() * sizeof(glm::vec3), world.blockInstances[i].data(), GL_DYNAMIC_DRAW);
                    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, world.blockInstances[i].size());
                }
            }
        }
    }
};

// ======================================================================
// Application Class
// ======================================================================
class Application {
private:
    GLFWwindow* window;
    Camera camera;
    World world;
    Renderer renderer;
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    bool firstMouse = true;
    float lastX = WINDOW_WIDTH / 2.0f;
    float lastY = WINDOW_HEIGHT / 2.0f;

public:
    Application() : camera(glm::vec3(6.0f, 5.0f, 15.0f)) {
        g_Camera = &camera;
    }

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
        window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Prismals Refactor v1", NULL, NULL);
        if (!window) { std::cout << "Failed to create GLFW window\n"; glfwTerminate(); exit(-1); }
        glfwMakeContextCurrent(window);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
        glfwSetCursorPosCallback(window, mouse_callback);

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::cout << "Failed to initialize GLAD\n"; exit(-1); }

        renderer.init();
        world.createDebugWorld();

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_PROGRAM_POINT_SIZE);
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            float currentFrame = static_cast<float>(glfwGetTime());
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;
            
            processInput();
            
            renderer.draw(world, camera, currentFrame);

            glfwSwapBuffers(window);
            glfwPollEvents();
        }
    }

    void processInput() {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);
        camera.processKeyboard(window, deltaTime);
    }

    void cleanup() {
        glfwTerminate();
    }

    static void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
        glViewport(0, 0, width, height);
    }

    static void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
        Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
        if (app) {
            if (app->firstMouse) {
                app->lastX = xpos;
                app->lastY = ypos;
                app->firstMouse = false;
            }
            float xoffset = xpos - app->lastX;
            float yoffset = app->lastY - ypos;
            app->lastX = xpos;
            app->lastY = ypos;
            app->camera.processMouseMovement(xoffset, yoffset);
        }
    }
};

// ======================================================================
// Sky Color Implementation
// ======================================================================
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
// Main Entry Point
// ======================================================================
int main() {
    Application app;
    app.run();
    return 0;
}
