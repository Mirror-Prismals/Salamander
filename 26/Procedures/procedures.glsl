@@BLOCK_VERTEX_SHADER
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
    
    vec3 finalPos = pos + instancePos;

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

@@BLOCK_FRAGMENT_SHADER
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

@@SKYBOX_VERTEX_SHADER
#version 330 core layout(location=0)in vec2 a;out vec2 t;void main(){t=a*0.5+0.5;gl_Position=vec4(a,0,1);}

@@SKYBOX_FRAGMENT_SHADER
#version 330 core out vec4 f;in vec2 t;uniform vec3 sT;uniform vec3 sB;void main(){f=vec4(mix(sB,sT,t.y),1);}

@@SUNMOON_VERTEX_SHADER
#version 330 core layout(location=0)in vec2 a;out vec2 t;uniform mat4 m;uniform mat4 v;uniform mat4 p;void main(){t=a*0.5+0.5;gl_Position=p*v*m*vec4(a,0,1);}

@@SUNMOON_FRAGMENT_SHADER
#version 330 core in vec2 t;out vec4 f;uniform vec3 c;uniform float b;void main(){float d=distance(t,vec2(0.5));float da=smoothstep(0.5,0.45,d);float ga=1.0-smoothstep(0.45,0.5,d);float fa=clamp(da+0.3*ga,0,1);f=vec4(c*b,fa*b);}

@@STAR_VERTEX_SHADER
#version 330 core layout(location=0)in vec3 a;uniform mat4 v;uniform mat4 p;out float s;float cs(vec3 o){return fract(sin(dot(o,vec3(12.9898,78.233,37.719)))*43758.5453);}void main(){gl_Position=p*v*vec4(a,1);s=cs(a);gl_PointSize=2.0;}

@@STAR_FRAGMENT_SHADER
#version 330 core in float s;uniform float t;out vec4 f;void main(){float b=0.8+0.2*sin(t*3.0+s*10.0);if(length(gl_PointCoord-vec2(0.5))>0.5)discard;f=vec4(vec3(b),1);}
