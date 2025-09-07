#pragma once

// --- Constants ---
const unsigned int WINDOW_WIDTH = 1920;
const unsigned int WINDOW_HEIGHT = 1080;
const int NUM_BLOCK_PROTOTYPES = 25;
const int NUM_STARS = 1000;
const float STAR_DISTANCE = 1000.0f;

// --- Block Color Data ---
glm::vec3 blockColors[NUM_BLOCK_PROTOTYPES];

void initializeBlockColors() {
    blockColors[0]={0.19,0.66,0.32}; blockColors[1]={0,0.5,1}; blockColors[2]={0.29,0.21,0.13}; 
    blockColors[3]={0.07,0.46,0.34}; blockColors[4]={1,0,0}; blockColors[5]={0.2,0.7,0.2}; 
    blockColors[6]={0.45,0.22,0.07}; blockColors[7]={0.13,0.54,0.13}; blockColors[8]={0.55,0.27,0.07}; 
    blockColors[9]={0.36,0.6,0.33}; blockColors[10]={0.44,0.39,0.32}; blockColors[11]={0.35,0.43,0.30}; 
    blockColors[12]={0.52,0.54,0.35}; blockColors[13]={0.6,0.61,0.35}; blockColors[14]={0.4,0.3,0.2}; 
    blockColors[15]={0.43,0.39,0.34}; blockColors[16]={0.4,0.25,0.1}; blockColors[17]={0.2,0.5,0.2}; 
    blockColors[18]={0.3,0.2,0.1}; blockColors[19]={0.2,0.8,0.9}; blockColors[20]={0.5,0.5,0.5}; 
    blockColors[21]={1,0.5,0}; blockColors[22]={0.93,0.79,0.69}; blockColors[23]={0.95,0.95,1}; 
    blockColors[24]={0.8,0.9,1};
}

// --- Sky Color Data ---
struct SkyColorKey { float time; glm::vec3 top; glm::vec3 bottom; };
SkyColorKey skyKeys[5] = {
    {0.0f,glm::vec3(16/255.f,16/255.f,48/255.f),glm::vec3(0,0,0)}, {0.25f,glm::vec3(0,0,1),glm::vec3(128/255.f,128/255.f,1)},
    {0.5f,glm::vec3(135/255.f,206/255.f,235/255.f),glm::vec3(1,1,1)}, {0.75f,glm::vec3(0,128/255.f,128/255.f),glm::vec3(1,71/255.f,0)},
    {1.0f,glm::vec3(16/255.f,16/255.f,48/255.f),glm::vec3(0,0,0)}
};
void getCurrentSkyColors(float dayFraction, glm::vec3& top, glm::vec3& bottom) {
    int i=0; for(;i<4;i++){if(dayFraction>=skyKeys[i].time&&dayFraction<=skyKeys[i+1].time)break;}
    float t=(dayFraction-skyKeys[i].time)/(skyKeys[i+1].time-skyKeys[i].time);
    top=glm::mix(skyKeys[i].top,skyKeys[i+1].top,t); bottom=glm::mix(skyKeys[i].bottom,skyKeys[i+1].bottom,t);
}

// --- Cube Vertex Data ---
float cubeVertices[]={-0.5f,-0.5f,0.5f,0,0,1,0,0,0.5f,-0.5f,0.5f,0,0,1,1,0,0.5f,0.5f,0.5f,0,0,1,1,1,0.5f,0.5f,0.5f,0,0,1,1,1,-0.5f,0.5f,0.5f,0,0,1,0,1,-0.5f,-0.5f,0.5f,0,0,1,0,0,0.5f,-0.5f,0.5f,1,0,0,0,0,0.5f,-0.5f,-0.5f,1,0,0,1,0,0.5f,0.5f,-0.5f,1,0,0,1,1,0.5f,0.5f,-0.5f,1,0,0,1,1,0.5f,0.5f,0.5f,1,0,0,0,1,0.5f,-0.5f,0.5f,1,0,0,0,0,0.5f,-0.5f,-0.5f,0,0,-1,0,0,-0.5f,-0.5f,-0.5f,0,0,-1,1,0,-0.5f,0.5f,-0.5f,0,0,-1,1,1,-0.5f,0.5f,-0.5f,0,0,-1,1,1,0.5f,0.5f,-0.5f,0,0,-1,0,1,0.5f,-0.5f,-0.5f,0,0,-1,0,0,-0.5f,-0.5f,-0.5f,-1,0,0,0,0,-0.5f,-0.5f,0.5f,-1,0,0,1,0,-0.5f,0.5f,0.5f,-1,0,0,1,1,-0.5f,0.5f,0.5f,-1,0,0,1,1,-0.5f,0.5f,-0.5f,-1,0,0,0,1,-0.5f,-0.5f,-0.5f,-1,0,0,0,0,-0.5f,0.5f,0.5f,0,1,0,0,0,0.5f,0.5f,0.5f,0,1,0,1,0,0.5f,0.5f,-0.5f,0,1,0,1,1,0.5f,0.5f,-0.5f,0,1,0,1,1,-0.5f,0.5f,-0.5f,0,1,0,0,1,-0.5f,0.5f,0.5f,0,1,0,0,0,-0.5f,-0.5f,-0.5f,0,-1,0,0,0,0.5f,-0.5f,-0.5f,0,-1,0,1,0,0.5f,-0.5f,0.5f,0,-1,0,1,1,0.5f,-0.5f,0.5f,0,-1,0,1,1,-0.5f,-0.5f,0.5f,0,-1,0,0,1,-0.5f,-0.5f,-0.5f,0,-1,0,0,0};

// --- GLSL Shader Sources ---
namespace Shaders {
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
                vec2 f=fract(TexCoord*grid);
                bool g=(f.x<line||f.y<line);
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
}
