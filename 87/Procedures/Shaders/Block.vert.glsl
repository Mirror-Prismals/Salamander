#version 330 core
// ATTRIBUTES FROM CUBE VBO
layout (location = 0) in vec3 aPos; 
layout (location = 1) in vec3 aNormal; 
layout (location = 2) in vec2 aTexCoord;

// ATTRIBUTES FROM INSTANCE VBO (DIFFERENT FOR BRANCHES)
layout (location = 3) in vec3 aOffset; // pos for normal, pos for branch
layout (location = 4) in vec3 aColor_or_aRotation; // color for normal, rot for branch (as vec3's x)
layout (location = 5) in vec3 aColor_branch; // color for branch

// OUTPUTS TO FRAGMENT SHADER
out vec2 TexCoord; 
out vec3 FragColor_in; 
out float instanceDistance; 
out vec3 Normal; 
out vec3 WorldPos;

// UNIFORMS
uniform mat4 model; 
uniform mat4 view; 
uniform mat4 projection; 
uniform int behaviorType;
uniform vec3 cameraPos; 
uniform float time;
uniform float instanceScale;

// RenderBehavior enum in C++:
// 0: STATIC_DEFAULT
// 1: ANIMATED_WATER
// 2: ANIMATED_WIREFRAME
// 3: STATIC_BRANCH
// 4: ANIMATED_TRANSPARENT_WAVE

void main(){
    vec3 pos = aPos;
    vec3 normal = aNormal;
    vec3 instancePos = aOffset;
    vec3 instanceColor = aColor_or_aRotation;
    float instanceRotation = 0.0;

    if(behaviorType == 3) { // STATIC_BRANCH
        instanceRotation = aColor_or_aRotation.x;
        instanceColor = aColor_branch;
    }

    // --- Vertex Animation based on Behavior ---
    if(behaviorType == 1) { // ANIMATED_WATER
        vec3 d; 
        d.x=sin(time*0.5+instancePos.x*1.3+instancePos.y*0.7)*0.2; 
        d.y=sin(time*0.5+instancePos.y*1.3+instancePos.z*0.7)*0.2; 
        d.z=sin(time*0.5+instancePos.z*1.3+instancePos.x*0.7)*0.2; 
        pos=aPos*1.2+d; 
    }
    else if(behaviorType == 2) { // ANIMATED_WIREFRAME
        vec3 d; 
        d.x=sin((instancePos.x+time)*0.3)*0.05; 
        d.y=cos((instancePos.y+time)*0.3)*0.05; 
        d.z=sin((instancePos.z+time)*0.3)*0.05; 
        pos=aPos+d;
    }
    else if(behaviorType == 3) { // STATIC_BRANCH
        float rotRads = radians(instanceRotation);
        mat3 r=mat3(cos(rotRads),0,sin(rotRads),0,1,0,-sin(rotRads),0,cos(rotRads)); 
        mat3 s=mat3(0.3,0,0,0,0.8,0,0,0,0.3); 
        pos=r*(s*aPos); 
        normal=r*aNormal; 
    }
    
    vec3 finalPos = instancePos + (pos * instanceScale);

    if(behaviorType == 4) { // ANIMATED_TRANSPARENT_WAVE
        finalPos.y += sin(time + instancePos.x * 0.1) * 0.5; 
    }

    vec4 worldPos4 = model * vec4(finalPos, 1.0); 
    WorldPos = worldPos4.xyz;
    gl_Position = projection * view * worldPos4;

    // Pass data to fragment shader
    FragColor_in = instanceColor; 
    TexCoord = aTexCoord;
    instanceDistance = length(instancePos - cameraPos); 
    Normal = normalize(mat3(model) * normal);
}
