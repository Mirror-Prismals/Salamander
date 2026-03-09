#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aU;
layout(location=2) in float aV;
uniform mat4 model; uniform mat4 view; uniform mat4 projection;
out float vU; out float vV; out vec3 worldPos;
void main(){
    vec4 w = model * vec4(aPos, 1.0);
    worldPos = w.xyz;
    vU = aU;
    vV = aV;
    gl_Position = projection * view * w;
}

