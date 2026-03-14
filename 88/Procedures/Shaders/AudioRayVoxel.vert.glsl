#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 iPos;
layout (location = 3) in float iGain;
layout (location = 4) in float iOcc;
uniform mat4 view;
uniform mat4 projection;
out vec3 vNormal;
out float vGain;
out float vOcc;
void main(){
    vec3 worldPos = iPos + aPos * 0.9; // slightly smaller than a block
    vNormal = aNormal;
    vGain = iGain;
    vOcc = iOcc;
    gl_Position = projection * view * vec4(worldPos, 1.0);
}

