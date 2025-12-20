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

