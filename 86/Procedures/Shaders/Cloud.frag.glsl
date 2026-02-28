#version 330 core
in vec2 uv;
out vec4 FragColor;
uniform mat4 invVP;
uniform vec3 cameraPos;
uniform vec3 sunDir;
uniform float time;
uniform float dayPhase;
uniform float cloudMaskCyclesPerDay;
uniform float cloudMaskRotationTurnsPerCycle;
uniform float cloudMaskDriftDistance;
uniform float cloudBase; uniform float cloudThickness; uniform float cloudScale;
uniform float cloudBaseJitter;
uniform float cloudThicknessJitter;
uniform float cloudMacroFreq;
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
    float maskPhase = dayPhase * cloudMaskCyclesPerDay;
    float rotPhase = maskPhase * cloudMaskRotationTurnsPerCycle;
    float rotA = rotPhase * 6.28318530718;
    float rc = cos(rotA);
    float rs = sin(rotA);
    mat2 dayRot = mat2(rc, -rs, rs, rc);
    vec2 dayDrift = cloudMaskDriftDistance *
        vec2(cos(maskPhase * 6.28318530718), sin(maskPhase * 6.28318530718));
    vec2 rotatedXZ = dayRot * p.xz;
    vec3 pd = vec3(rotatedXZ.x + dayDrift.x, p.y, rotatedXZ.y + dayDrift.y);

    // Cheap broad macro modulation (avoids extra noise samples per ray step).
    float macro = 0.5 * (sin((pd.x + pd.z) * cloudMacroFreq * 2.3 + time * 0.0028) +
                         cos((pd.x - pd.z) * cloudMacroFreq * 1.9 - time * 0.0021));
    float localBase = cloudBase + macro * cloudBaseJitter;
    float localThickness = cloudThickness * (1.0 + cloudThicknessJitter * macro);
    localThickness = max(localThickness, cloudThickness * 0.40);

    vec3 q = vec3(pd.xz * (0.00155 * cloudScale), pd.y * (0.00155 * cloudScale)) + vec3(0.0, 0.0, time * 0.006);
    float nRaw = snoise(q) * 0.5 + 0.5;
    // Restore internal cloud breakup from noise only (no world-space coverage mask).
    float n = mix(0.10, 1.0, smoothstep(0.30, 0.78, nRaw));

    float h = clamp((p.y - localBase) / max(localThickness, 1.0), 0.0, 1.0);
    float heightFactor = smoothstep(0.05, 0.30, h) * (1.0 - smoothstep(0.62, 0.98, h));

    return n * heightFactor * densityMultiplier;
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
    float maxBaseOffset = abs(cloudBaseJitter);
    float maxThicknessOffset = cloudThickness * abs(cloudThicknessJitter);
    float slabMin = cloudBase - maxBaseOffset - maxThicknessOffset * 0.25;
    float slabMax = cloudBase + cloudThickness + maxBaseOffset + maxThicknessOffset;
    if (abs(rd.y) > 1e-5) {
        float t1 = (slabMin - ro.y) / rd.y;
        float t2 = (slabMax - ro.y) / rd.y;
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
        float rayProgress = clamp((t - tMin) / max(tMax - tMin, 1e-4), 0.0, 1.0);
        float adaptFactor = 1.0 + rayProgress * maxSkip;
        float dtAdaptive = dt * adaptFactor;
        vec3 p = ro + rd * (t + dtAdaptive * 0.5);
        float d = cloudDensity(p);
        if(d > 0.0018) {
            float alpha = 1.0 - exp(-d * dtAdaptive * 0.76);
            alpha = clamp(alpha, 0.0, 1.0);
            float lightWrap = 0.88 + 0.12 * sunPhase;
            vec3 col = vec3(1.0) * (lightMultiplier * lightWrap);
            accColor += col * alpha * T;
            T *= (1.0 - alpha);
            t += dtAdaptive;
        } else {
            // Empty regions can skip farther with no visible loss.
            t += dtAdaptive * 2.40;
        }
        if(T < 0.02) break;
    }

    float alphaOut = 1.0 - T;
    if(alphaOut <= 0.001) discard;
    FragColor = vec4(accColor, alphaOut);
}
