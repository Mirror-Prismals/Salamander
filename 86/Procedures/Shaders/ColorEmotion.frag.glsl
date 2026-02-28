#version 330 core
in vec2 vUV;
uniform vec3 emotionColor;
uniform float emotionIntensity;
uniform float pulse;
uniform float chargeAmount;
uniform float underwaterMix;
uniform float underwaterDepth;
uniform float underwaterLineUV;
uniform float underwaterLineStrength;
uniform float underwaterHazeStrength;
uniform float directionHintStrength;
uniform float directionHintAngle;
uniform float directionHintWidth;
uniform vec3 directionHintBaseColor;
uniform vec3 directionHintAccentColor;
uniform float timeSeconds;
uniform float aspectRatio;
uniform float opacityScale;
uniform int chargeDualToneEnabled;
uniform vec3 chargeDualTonePrimaryColor;
uniform vec3 chargeDualToneSecondaryColor;
uniform float chargeDualToneSpinSpeed;
out vec4 FragColor;

void main() {
    vec2 p = vUV * 2.0 - 1.0;
    // Elliptical ring profile:
    // - Keep horizontal aspect correction.
    // - Boost vertical weighting so top/bottom arc remains visible at large outer radius.
    p = vec2(p.x * aspectRatio, p.y * 1.55);
    float r = length(p);

    float outer = 1.55;
    // Keep the inner clear boundary where it is now, but feather much farther outward.
    float edgeStart = outer - 0.07;
    float edgeEnd = outer + 0.55;
    float ringRamp = smoothstep(edgeStart, edgeEnd, r); // 0 at inner boundary -> 1 near outer/corners
    ringRamp = pow(ringRamp, 0.85);                     // slight lift so edge remains readable
    float edge = ringRamp;
    float pulseShape = 0.92 + 0.08 * sin(pulse + r * 8.0);
    float uw = clamp(underwaterMix, 0.0, 1.0);
    float underwaterFill = uw * smoothstep(0.0, 1.2, r) * 0.18;
    float alpha = clamp((edge * pulseShape + underwaterFill) * clamp(emotionIntensity, 0.0, 1.0), 0.0, 0.95);

    float depthMix = smoothstep(0.0, 2.4, max(0.0, underwaterDepth));
    float topMask = smoothstep(0.38, 1.0, vUV.y);
    float shimmer = 0.90 + 0.10 * sin(vUV.x * 24.0 + timeSeconds * 2.6 + vUV.y * 11.0);
    float hazeMask = topMask * uw * underwaterHazeStrength * (0.60 + 0.40 * depthMix) * shimmer;
    float lineMask = 0.0;
    float hintStrength = clamp(directionHintStrength, 0.0, 1.0);
    float ringMask = smoothstep(edgeStart + 0.02, edgeEnd, r);
    float ang = atan(p.y, p.x);
    float dAng = abs(atan(sin(ang - directionHintAngle), cos(ang - directionHintAngle)));
    float accentSection = 1.0 - smoothstep(directionHintWidth * 0.35, directionHintWidth, dAng);
    float baseHintMask = hintStrength * ringMask;
    float accentHintMask = hintStrength * ringMask * accentSection;

    vec3 outColor = emotionColor;

    if (chargeDualToneEnabled == 1) {
        float spinAngle = timeSeconds * chargeDualToneSpinSpeed;
        float split = cos(ang - spinAngle);
        float primaryMask = smoothstep(-0.01, 0.01, split); // 50/50 split with a tiny antialiased seam
        vec3 dualTone = mix(chargeDualToneSecondaryColor, chargeDualTonePrimaryColor, primaryMask);
        outColor = mix(outColor, dualTone, clamp(chargeAmount, 0.0, 1.0));
    }

    vec3 hazeTint = mix(outColor, vec3(0.76, 0.90, 0.98), 0.62);
    vec3 lineTint = vec3(0.95, 1.0, 1.0);
    outColor = mix(outColor, hazeTint, clamp(hazeMask, 0.0, 1.0));
    outColor = mix(outColor, lineTint, clamp(lineMask, 0.0, 1.0));
    outColor = mix(outColor, directionHintBaseColor, clamp(baseHintMask * 0.85, 0.0, 1.0));
    outColor = mix(outColor, directionHintAccentColor, clamp(accentHintMask, 0.0, 1.0));
    alpha = clamp(alpha + hazeMask * 0.72 + lineMask * 0.92 + baseHintMask * 0.18 + accentHintMask * 0.32, 0.0, 0.98);
    alpha *= clamp(opacityScale, 0.0, 1.0);
    if (alpha <= 0.001) discard;

    FragColor = vec4(outColor, alpha);
}
