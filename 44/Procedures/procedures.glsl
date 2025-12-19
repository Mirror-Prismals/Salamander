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
#version 330 core
layout(location=0)in vec2 a;
out vec2 t;
void main(){
    t = a*0.5+0.5;
    gl_Position = vec4(a,0,1);
}

@@SKYBOX_FRAGMENT_SHADER
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

@@SUNMOON_VERTEX_SHADER
#version 330 core
layout(location=0) in vec2 a;
out vec2 t;
uniform mat4 m;
uniform mat4 v;
uniform mat4 p;
void main(){
    t = a*0.5+0.5;
    gl_Position = p*v*m*vec4(a,0,1);
}

@@SUNMOON_FRAGMENT_SHADER
#version 330 core
in vec2 t;
out vec4 f;
uniform vec3 c;
uniform float time;
void main(){
    vec2 uv = t - vec2(0.5);
    float r = length(uv) * 2.0;

    // Solid disk with sharp-ish edge, no outer glow
    float disk = 1.0 - smoothstep(0.16, 0.22, r);
    if (disk < 0.001) discard;
    vec3 base = c;
    vec3 color = base * disk;
    float alpha = disk;
    f = vec4(color, alpha);
}

@@GODRAY_VERTEX_SHADER
#version 330 core
layout(location=0) in vec2 a;
out vec2 vUV;
void main(){
    vUV = a * 0.5 + 0.5;
    gl_Position = vec4(a, 0.0, 1.0);
}

@@GODRAY_RADIAL_FRAGMENT_SHADER
#version 330 core
in vec2 vUV;
out vec4 f;
uniform sampler2D occlusionTex;
uniform vec2 lightPos; // in 0..1 screen space
uniform float exposure;
uniform float decay;
uniform float density;
uniform float weight;
uniform int samples;
void main(){
    vec2 delta = (lightPos - vUV) * density / float(samples);
    vec2 coord = vUV;
    float illumination = 0.0;
    float curDecay = 1.0;
    for(int i=0;i<samples;i++){
        coord += delta;
        float sample = texture(occlusionTex, coord).r;
        illumination += sample * curDecay * weight;
        curDecay *= decay;
    }
    f = vec4(vec3(illumination * exposure), 1.0);
}

@@GODRAY_COMPOSITE_FRAGMENT_SHADER
#version 330 core
in vec2 vUV;
out vec4 f;
uniform sampler2D godrayTex;
void main(){
    vec3 col = texture(godrayTex, vUV).rgb;
    f = vec4(col, 1.0);
}

@@CLOUD_VERTEX_SHADER
#version 330 core
layout(location=0) in vec2 a;
out vec2 uv;
void main(){ uv = a * 0.5 + 0.5; gl_Position = vec4(a, 0.0, 1.0); }

@@CLOUD_FRAGMENT_SHADER
#version 330 core
in vec2 uv;
out vec4 FragColor;
uniform mat4 invVP;
uniform mat4 view; uniform mat4 proj;
uniform vec3 cameraPos;
uniform vec3 sunDir;
uniform float time;
uniform float cloudBase; uniform float cloudThickness; uniform float cloudScale;
uniform int steps; uniform float densityMultiplier; uniform float lightMultiplier;
uniform float cloudRadius;
uniform float fadeBand;
uniform float maxSkip;

vec3 mod289(vec3 x){return x - floor(x*(1.0/289.0))*289.0;}
vec4 mod289(vec4 x){return x - floor(x*(1.0/289.0))*289.0;}
vec4 permute(vec4 x){return mod289(((x*34.0)+1.0)*x);}
vec4 taylorInvSqrt(vec4 r){return 1.79284291400159 - 0.85373472095314 * r;}
float snoise(vec3 v){
    const vec2  C = vec2(1.0/6.0, 1.0/3.0);
    const vec4  D = vec4(0.0, 0.5, 1.0, 2.0);
    vec3 i  = floor(v + dot(v, C.yyy) );
    vec3 x0 = v - i + dot(i, C.xxx);
    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min( g.xyz, l.zxy );
    vec3 i2 = max( g.xyz, l.zxy );
    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;
    i = mod289(i);
    vec4 p = permute( permute( permute(
                i.z + vec4(0.0, i1.z, i2.z, 1.0 ))
              + i.y + vec4(0.0, i1.y, i2.y, 1.0 ))
              + i.x + vec4(0.0, i1.x, i2.x, 1.0 ));
    float n_ = 0.142857142857;
    vec3 ns = n_ * D.wyz - D.xzx;
    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);
    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);
    vec4 x = x_ *ns.x + ns.y;
    vec4 y = y_ *ns.x + ns.y;
    vec4 h = 1.0 - abs(x) - abs(y);
    vec4 b0 = vec4( x.xy, y.xy );
    vec4 b1 = vec4( x.zw, y.zw );
    vec4 s0 = floor(b0)*2.0 + 1.0;
    vec4 s1 = floor(b1)*2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));
    vec4 a0 = b0.xzyw + s0.xzyw*sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw*sh.zzww;
    vec3 p0 = vec3(a0.x, a0.y, h.x);
    vec3 p1 = vec3(a0.z, a0.w, h.y);
    vec3 p2 = vec3(a1.x, a1.y, h.z);
    vec3 p3 = vec3(a1.z, a1.w, h.w);
    vec4 norm = taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2,p2), dot(p3,p3)));
    p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;
    vec4 m = max(0.6 - vec4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0);
    m = m * m;
    return 42.0 * dot( m*m, vec4( dot(p0,x0), dot(p1,x1), dot(p2,x2), dot(p3,x3) ) );
}

float cloudDensity(vec3 p) {
    float n1 = snoise(p * 0.0015 * cloudScale + vec3(0.0, time*0.008, 0.0)) * 0.6 + 0.4;
    float n2 = snoise(p * 0.0045 * cloudScale + vec3(time*0.015)) * 0.5 + 0.5;
    float heightFactor = smoothstep(cloudBase, cloudBase + cloudThickness, p.y) * (1.0 - smoothstep(cloudBase + cloudThickness*0.6, cloudBase + cloudThickness, p.y));
    float d = (n1 * 0.7 + n2 * 0.3);
    d = clamp(d, 0.0, 1.0);
    d *= heightFactor;
    return d * densityMultiplier;
}

void main(){
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 nearPt = invVP * vec4(ndc.xy, -1.0, 1.0);
    nearPt /= nearPt.w;
    vec4 farPt = invVP * vec4(ndc.xy, 1.0, 1.0);
    farPt /= farPt.w;
    vec3 ro = cameraPos;
    vec3 rd = normalize(farPt.xyz - nearPt.xyz);

    float tMin = 0.0;
    float tMax = cloudRadius;
    if (abs(rd.y) > 1e-5) {
        float t1 = (cloudBase - ro.y) / rd.y;
        float t2 = (cloudBase + cloudThickness - ro.y) / rd.y;
        float ta = min(t1,t2), tb = max(t1,t2);
        tMin = max(tMin, ta);
        tMax = min(tMax, tb);
    }
    if (tMax <= tMin) discard;

    float dt = (tMax - tMin) / float(max(steps,1));
    float T = 1.0;
    vec3 accColor = vec3(0.0);
    float t = max(tMin, 0.0);
    float sunPhase = max(dot(normalize(sunDir), -rd), 0.0);

    for(int i=0;i<200;i++){
        if(i>=steps) break;
        if(t > tMax) break;
        vec3 midPoint = ro + rd * (t + dt * 0.5);
        float horizDist = length(midPoint.xz - cameraPos.xz);
        if(horizDist > cloudRadius) { t += dt; continue; }
        float horizFade = 1.0 - smoothstep(cloudRadius - fadeBand, cloudRadius, horizDist);
        float fadeStart = cloudRadius * 0.45;
        float adaptFactor = 1.0 + smoothstep(fadeStart, cloudRadius, horizDist) * maxSkip;
        float dtAdaptive = dt * adaptFactor;
        vec3 p = ro + rd * (t + dtAdaptive * 0.5);
        float d = cloudDensity(p) * horizFade;
        if(d > 0.002) {
            float alpha = 1.0 - exp(-d * dtAdaptive * 0.4);
            alpha = clamp(alpha, 0.0, 1.0);
            vec3 col = vec3(1.0) * lightMultiplier;
            accColor += col * alpha * T;
            T *= (1.0 - alpha);
        }
        t += dtAdaptive;
        if(T < 0.02) break;
    }

    float alphaOut = 1.0 - T;
    if(alphaOut <= 0.001) discard;
    FragColor = vec4(accColor, alphaOut);

    float tHit = t;
    vec4 clip = proj * view * vec4(ro + rd * tHit, 1.0);
    float ndcZ = clip.z / clip.w;
    float depth = ndcZ * 0.5 + 0.5;
    gl_FragDepth = clamp(depth, 0.0, 1.0);
}

@@AURORA_VERTEX_SHADER
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

@@AURORA_FRAGMENT_SHADER
#version 330 core
in float vU; in float vV; in vec3 worldPos;
out vec4 FragColor;
uniform vec3 magPal[4]; uniform vec3 limePal[4]; uniform int paletteIndex;
uniform float time; uniform float passAlpha;
vec3 samplePalette(vec3 p[4], float t){
    t = clamp(t,0.0,1.0);
    if(t<0.333){ float tt = smoothstep(0.0,0.333,t)/0.333; return mix(p[0],p[1],tt); }
    else if(t<0.666){ float tt = smoothstep(0.333,0.666,t); return mix(p[1],p[2],tt); }
    else { float tt = smoothstep(0.666,1.0,t); return mix(p[2],p[3],tt); }
}
void main(){
    vec3 pal[4];
    if(paletteIndex==0){ pal[0]=magPal[0]; pal[1]=magPal[1]; pal[2]=magPal[2]; pal[3]=magPal[3]; }
    else { pal[0]=limePal[0]; pal[1]=limePal[1]; pal[2]=limePal[2]; pal[3]=limePal[3]; }
    float t = clamp(vV * 0.98 + 0.02 * sin(worldPos.x*0.01 + time*0.02), 0.0, 1.0);
    vec3 col = samplePalette(pal, t);
    // Luminance normalization so magenta (two-channel) doesn't overpower lime (one-channel)
    if (paletteIndex == 0) col *= 0.65;
    float band = 0.72 + 0.48 * sin(vU * 6.2831853 * 0.9 + time * 0.03 + worldPos.x * 0.0015);
    float fall = pow(1.0 - vV, 0.9);
    float side = smoothstep(0.99, 0.18, 1.0 - abs(vU - 0.5) * 2.0);
    float alpha = clamp(fall * band * passAlpha, 0.0, 1.0) * mix(0.96, 1.0, side);
    // Keep saturation by limiting the boost that pushes magenta toward white
    float boost = 1.0 + 0.18 * band;
    col = pow(col * boost, vec3(0.95));
    FragColor = vec4(col, alpha);
    if(FragColor.a < 0.001) discard;
}

@@STAR_VERTEX_SHADER
#version 330 core layout(location=0)in vec3 a;uniform mat4 v;uniform mat4 p;out float s;float cs(vec3 o){return fract(sin(dot(o,vec3(12.9898,78.233,37.719)))*43758.5453);}void main(){gl_Position=p*v*vec4(a,1);s=cs(a);gl_PointSize=2.0;}

@@STAR_FRAGMENT_SHADER
#version 330 core in float s;uniform float t;out vec4 f;void main(){float b=0.8+0.2*sin(t*3.0+s*10.0);if(length(gl_PointCoord-vec2(0.5))>0.5)discard;f=vec4(vec3(b),1);}

@@SELECTION_VERTEX_SHADER
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec3 vWorldPos;
out vec3 vWorldNormal;
void main(){
    vec4 worldPos = model * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vWorldNormal = normalize(mat3(model) * aNormal);
    gl_Position = projection * view * worldPos;
}

@@SELECTION_FRAGMENT_SHADER
#version 330 core
in vec3 vWorldPos;
in vec3 vWorldNormal;
uniform vec3 cameraPos;
uniform float time;
out vec4 FragColor;
void main(){
    vec3 viewDir = normalize(cameraPos - vWorldPos);
    if(dot(vWorldNormal, viewDir) <= 0.0) discard;
    float cycle = mod(time, 4.0);
    float pulse;
    if(cycle < 3.0){
        pulse = 1.0;
    } else if(cycle < 4.0){
        float t = clamp(cycle - 3.0, 0.0, 1.0);
        float easeDown = 1.0 - pow(1.0 - t, 4.0);
        float fall = mix(1.0, 0.0, easeDown);
        float easeUp = pow(t, 4.0);
        float rise = mix(0.0, 1.0, easeUp);
        pulse = mix(fall, rise, t);
    } else {
        pulse = 1.0;
    }
    FragColor = vec4(vec3(pulse), 1.0);
}

@@HUD_VERTEX_SHADER
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 vUV;
void main(){
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}

@@HUD_FRAGMENT_SHADER
#version 330 core
in vec2 vUV;
uniform float fillAmount;
uniform bool ready;
uniform bool buildMode;
uniform vec3 previewColor;
uniform int channelIndex;
out vec4 FragColor;
const vec3 channelColors[3] = vec3[](vec3(1.0, 0.3, 0.3), vec3(0.3, 1.0, 0.3), vec3(0.3, 0.6, 1.0));
void main(){
    float y = clamp(vUV.y, 0.0, 1.0);
    float bend = sin(y * 3.14159) * -0.15;
    float x = (vUV.x - 0.5 + bend * 0.4) * 2.0;
    float outerHalf = mix(0.35, 0.6, y);
    float innerHalf = outerHalf - 0.18;
    float outerMask = smoothstep(outerHalf, outerHalf - 0.015, abs(x));
    float innerMask = smoothstep(innerHalf - 0.02, innerHalf + 0.005, abs(x));
    float rim = outerMask * (1.0 - innerMask);
    float targetFill = buildMode ? 1.0 : clamp(fillAmount, 0.0, 1.0);
    float fillMask = step(y, targetFill);
    rim *= fillMask;
    if(rim < 0.02) discard;
    vec3 color;
    if (buildMode) {
        int idx = clamp(channelIndex, 0, 2);
        vec3 bottomColor = channelColors[idx];
        color = mix(bottomColor, previewColor, y);
    } else {
        vec3 fillStart = vec3(0.1, 0.8, 0.2);
        vec3 fillEnd = vec3(1.0, 0.5, 0.0);
        color = mix(fillStart, fillEnd, y);
    }
    float alpha = 0.85 * rim * (ready ? 1.02 : 1.0);
    FragColor = vec4(color, alpha);
}

@@CROSSHAIR_VERTEX_SHADER
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aColor;
out vec3 vColor;
void main(){
    vColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);
}

@@CROSSHAIR_FRAGMENT_SHADER
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main(){
    FragColor = vec4(vColor, 1.0);
}

@@UI_VERTEX_SHADER
#version 330 core
layout (location = 0) in vec2 aPos;
void main(){
    gl_Position = vec4(aPos, 0.0, 1.0);
}

@@UI_FRAGMENT_SHADER
#version 330 core
uniform vec3 color;
out vec4 FragColor;
void main(){
    FragColor = vec4(color, 1.0);
}

@@UI_COLOR_VERTEX_SHADER
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aColor;
out vec3 vColor;
void main(){
    vColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);
}

@@UI_COLOR_FRAGMENT_SHADER
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main(){
    FragColor = vec4(vColor, 1.0);
}

@@AUDIORAY_VERTEX_SHADER
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

@@AUDIORAY_FRAGMENT_SHADER
#version 330 core
in float vGain;
in float vOccluded;
out vec4 FragColor;
void main(){
    vec3 clearColor = vec3(0.1, 0.9, 1.0);
    vec3 occludedColor = vec3(1.0, 0.3, 0.6);
    float occlamped = clamp(vOccluded, 0.0, 1.0);
    vec3 color = mix(clearColor, occludedColor, occlamped);
    float a = clamp(vGain, 0.05, 1.0);
    FragColor = vec4(color * a, 0.95);
}

@@AUDIORAY_VOXEL_VERTEX_SHADER
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

@@AUDIORAY_VOXEL_FRAGMENT_SHADER
#version 330 core
in vec3 vNormal;
in float vGain;
in float vOcc;
uniform float baseAlpha;
out vec4 FragColor;
void main(){
    vec3 clearColor = vec3(0.1, 0.9, 1.0);
    vec3 occColor = vec3(1.0, 0.3, 0.6);
    float light = clamp(dot(normalize(vec3(0.4,1.0,0.2)), normalize(vNormal)) * 0.5 + 0.5, 0.3, 1.0);
    float gain = clamp(vGain, 0.0, 1.0);
    vec3 color = mix(clearColor, occColor, clamp(vOcc, 0.0, 1.0));
    color *= mix(0.2, 1.0, gain);
    float a = clamp(baseAlpha * gain * 1.4, 0.08, 0.85);
    FragColor = vec4(color * light, a);
}
