#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aInfo; // x = gain, y = occluded flag
uniform mat4 view;
uniform mat4 projection;
out float vGain;
out float vOccluded;
void main(){
    vGain = aInfo.x;
    vOccluded = aInfo.y;
    gl_Position = projection * view * vec4(aPos, 1.0);
}

