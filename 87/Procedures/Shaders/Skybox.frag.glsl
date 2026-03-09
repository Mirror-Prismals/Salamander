#version 330 core
out vec4 f;
in vec2 t;
uniform vec3 sT;
uniform vec3 sB;
uniform mat4 projection;
uniform mat4 view;
void main(){
    vec2 ndc = t*2.0-1.0;
    vec4 clip = vec4(ndc,1.0,1.0);
    vec3 viewDir = normalize((inverse(projection)*clip).xyz);
    vec3 worldDir = normalize(inverse(mat3(view))*viewDir);
    float u = clamp(worldDir.y*0.5+0.5,0.0,1.0);
    f = vec4(mix(sB,sT,u),1);
}

