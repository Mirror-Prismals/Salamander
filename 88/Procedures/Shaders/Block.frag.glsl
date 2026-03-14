#version 330 core
in vec2 TexCoord; 
in vec3 FragColor_in; 
in float instanceDistance; 
in vec3 Normal; 
in vec3 WorldPos;
in vec3 InstanceCell;
out vec4 FragColor;

uniform int behaviorType;
uniform vec3 lightDir;
uniform vec3 ambientLight; 
uniform vec3 diffuseLight; 
uniform float time;
uniform int wireframeDebug;
uniform float instanceScale;
uniform int blockDamageEnabled;
uniform int blockDamageCount;
uniform ivec3 blockDamageCells[64];
uniform float blockDamageProgress[64];
uniform float blockDamageGrid;

float noise(vec2 p){ return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
int faceIndexFromNormal(vec3 n) {
    vec3 an = abs(n);
    if (an.x >= an.y && an.x >= an.z) return (n.x >= 0.0) ? 0 : 1;
    if (an.y >= an.z) return (n.y >= 0.0) ? 2 : 3;
    return (n.z >= 0.0) ? 4 : 5;
}
float damagePattern(ivec2 cellUv, ivec3 blockCell, int faceId) {
    vec2 c = vec2(cellUv);
    vec2 seedA = vec2(
        float(blockCell.x) * 0.173 + float(blockCell.z) * 0.293 + float(faceId) * 7.0,
        float(blockCell.y) * 0.271 + float(faceId) * 3.0
    );
    vec2 seedB = vec2(
        float(blockCell.z) * 0.619 + float(faceId) * 11.0,
        float(blockCell.x) * 0.887 + float(blockCell.y) * 0.411
    );
    float coarse = noise(floor((c + seedA) * 0.5));
    float fine = noise(c + seedB);
    return mix(coarse, fine, 0.62);
}

// RenderBehavior enum in C++:
// 0: STATIC_DEFAULT
// 1: ANIMATED_WATER
// 2: ANIMATED_WIREFRAME
// 3: STATIC_BRANCH
// 4: ANIMATED_TRANSPARENT_WAVE

void main(){
    if (wireframeDebug == 1) {
        FragColor = vec4(FragColor_in, 1.0);
        return;
    }

    if (blockDamageEnabled == 1
        && behaviorType == 0
        && instanceScale <= 1.001
        && blockDamageCount > 0) {
        ivec3 cell = ivec3(round(InstanceCell));
        float progress = -1.0;
        for (int i = 0; i < blockDamageCount; ++i) {
            if (all(equal(blockDamageCells[i], cell))) {
                progress = clamp(blockDamageProgress[i], 0.0, 1.0);
                break;
            }
        }
        if (progress > 0.001) {
            float gridRes = max(1.0, blockDamageGrid);
            vec2 uv = clamp(fract(TexCoord), vec2(0.0), vec2(0.9999));
            ivec2 cellUv = ivec2(floor(uv * gridRes));
            int faceId = faceIndexFromNormal(normalize(Normal));
            float threshold = damagePattern(cellUv, cell, faceId);
            float erosion = pow(progress, 0.82);
            if (threshold < erosion) discard;
        }
    }

    if(behaviorType == 4) { // ANIMATED_TRANSPARENT_WAVE
        FragColor = vec4(FragColor_in, 0.4); 
        return;
    }

    float grid = 24.0; 
    float line = 0.03;

    if(behaviorType == 2) { // ANIMATED_WIREFRAME
        vec2 f=fract(TexCoord*grid);
        bool g=(f.x<line||f.y<line);
        float n=noise(floor((TexCoord+fract(floor(WorldPos.xy)*0.12345))*12.0));
        float a=(n>0.8)?0.0:1.0; 
        float fa=g?1.0:a; 
        vec3 c=g?vec3(0):FragColor_in;
        FragColor=vec4(c,fa); 
        return;
    }

    if(behaviorType == 1) { // STILL_WATER
        vec3 wc = FragColor_in;
        FragColor=vec4(wc,0.6); // Semi-transparent
    } else { // STATIC_DEFAULT and STATIC_BRANCH
        vec2 f=fract(TexCoord*grid); 
        vec3 bc;
        if(f.x<line||f.y<line){
            bc=vec3(0); // Wireframe lines are black
        } else {
            float d=instanceDistance/100.0;
            bc=FragColor_in+vec3(0.03*d); // Fog/distance fade effect
            bc=clamp(bc,0,1);
        }
        FragColor=vec4(bc,1.0);
    }
}
