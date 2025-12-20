#version 330 core
in vec2 TexCoord; 
in vec3 FragColor_in; 
in float instanceDistance; 
in vec3 Normal; 
in vec3 WorldPos;
out vec4 FragColor;

uniform int behaviorType;
uniform vec3 lightDir;
uniform vec3 ambientLight; 
uniform vec3 diffuseLight; 
uniform float time;

float noise(vec2 p){ return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

// RenderBehavior enum in C++:
// 0: STATIC_DEFAULT
// 1: ANIMATED_WATER
// 2: ANIMATED_WIREFRAME
// 3: STATIC_BRANCH
// 4: ANIMATED_TRANSPARENT_WAVE

void main(){
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
        vec3 l=ambientLight+diffuseLight*max(dot(normalize(Normal),normalize(lightDir)),0.0); 
        c*=l;
        FragColor=vec4(c,fa); 
        return;
    }

    if(behaviorType == 1) { // ANIMATED_WATER
        vec3 wc = FragColor_in; 
        float w=sin(WorldPos.x*0.1+time*2.0)+cos(WorldPos.z*0.1+time*2.0); 
        wc*=(1.0+w*0.05);
        vec3 l=ambientLight+diffuseLight*max(dot(normalize(Normal),normalize(lightDir)),0.0); 
        vec3 fc=wc*l;
        FragColor=vec4(fc,0.6); // Semi-transparent
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
        vec3 l=ambientLight+diffuseLight*max(dot(normalize(Normal),normalize(lightDir)),0.0); 
        vec3 fc=bc*l;
        FragColor=vec4(fc,1.0);
    }
}

