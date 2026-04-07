struct Uniforms {
    model: mat4x4<f32>,
    view: mat4x4<f32>,
    projection: mat4x4<f32>,
    mvp: mat4x4<f32>,
    color: vec4<f32>,
    topColor: vec4<f32>,
    bottomColor: vec4<f32>,
    params: vec4<f32>,
    vec2Data: vec4<f32>,
    extra: vec4<f32>,
    cameraAndScale: vec4<f32>,
    lightAndGrid: vec4<f32>,
    ambientAndLeaf: vec4<f32>,
    diffuseAndWater: vec4<f32>,
    atlasInfo: vec4<f32>,
    wallStoneAndWater2: vec4<f32>,
    intParams0: vec4<i32>,
    intParams1: vec4<i32>,
    intParams2: vec4<i32>,
    intParams3: vec4<i32>,
    intParams4: vec4<i32>,
    intParams5: vec4<i32>,
    intParams6: vec4<i32>,
    blockDamageCells: array<vec4<i32>, 64>,
    blockDamageProgress: array<vec4<f32>, 16>,
};

@group(0) @binding(0)
var<uniform> u: Uniforms;

struct FSIn {
    @location(0) uv: vec2<f32>,
};

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let emotionColor = u.color.xyz;
    let emotionIntensity = clamp(u.params.y, 0.0, 1.0);
    let pulse = u.params.z;
    let chargeAmount = clamp(u.params.w, 0.0, 1.0);
    let underwaterMix = clamp(u.extra.x, 0.0, 1.0);
    let underwaterDepth = max(0.0, u.extra.y);
    let directionHintStrength = clamp(u.diffuseAndWater.w, 0.0, 1.0);
    let directionHintAngle = u.wallStoneAndWater2.z;
    let directionHintWidth = clamp(u.wallStoneAndWater2.w, 0.05, 1.2);
    let directionHintBaseColor = u.topColor.xyz;
    let directionHintAccentColor = u.bottomColor.xyz;
    let timeSeconds = u.params.x;
    let aspectRatio = max(0.01, u.wallStoneAndWater2.x);
    let opacityScale = clamp(u.color.w, 0.0, 1.0);
    let chargeDualToneEnabled = (u.intParams6.w != 0);
    let chargeDualTonePrimaryColor = u.ambientAndLeaf.xyz;
    let chargeDualToneSecondaryColor = u.diffuseAndWater.xyz;
    let chargeDualToneSpinSpeed = u.extra.z;
    let fireInvertMix = clamp(u.extra.w, 0.0, 1.0);

    var p = input.uv * 2.0 - 1.0;
    p = vec2<f32>(p.x * aspectRatio, p.y * 1.55);
    let r = length(p);

    let outer = 1.55;
    let edgeStart = outer - 0.07;
    let edgeEnd = outer + 0.55;
    var ringRamp = smoothstep(edgeStart, edgeEnd, r);
    ringRamp = pow(ringRamp, 0.85);
    let edge = ringRamp;
    let pulseShape = 0.92 + 0.08 * sin(pulse + r * 8.0);
    let underwaterFill = underwaterMix * smoothstep(0.0, 1.2, r) * 0.18;
    var alpha = clamp((edge * pulseShape + underwaterFill) * emotionIntensity, 0.0, 0.95);

    let depthMix = smoothstep(0.0, 2.4, underwaterDepth);
    let topMask = smoothstep(0.38, 1.0, input.uv.y);
    let shimmer = 0.90 + 0.10 * sin(input.uv.x * 24.0 + timeSeconds * 2.6 + input.uv.y * 11.0);
    let hazeMask = topMask * underwaterMix * u.ambientAndLeaf.w * (0.60 + 0.40 * depthMix) * shimmer;
    let lineMask = 0.0;
    let ringMask = smoothstep(edgeStart + 0.02, edgeEnd, r);
    let ang = atan2(p.y, p.x);
    let dAng = abs(atan2(sin(ang - directionHintAngle), cos(ang - directionHintAngle)));
    let accentSection = 1.0 - smoothstep(directionHintWidth * 0.35, directionHintWidth, dAng);
    let baseHintMask = directionHintStrength * ringMask;
    let accentHintMask = directionHintStrength * ringMask * accentSection;

    var outColor = emotionColor;
    if (chargeDualToneEnabled) {
        let spinAngle = timeSeconds * chargeDualToneSpinSpeed;
        let split = cos(ang - spinAngle);
        let primaryMask = smoothstep(-0.01, 0.01, split);
        let dualTone = mix(chargeDualToneSecondaryColor, chargeDualTonePrimaryColor, primaryMask);
        outColor = mix(outColor, dualTone, chargeAmount);
    }

    let hazeTint = mix(outColor, vec3<f32>(0.76, 0.90, 0.98), 0.62);
    let lineTint = vec3<f32>(0.95, 1.0, 1.0);
    outColor = mix(outColor, hazeTint, clamp(hazeMask, 0.0, 1.0));
    outColor = mix(outColor, lineTint, clamp(lineMask, 0.0, 1.0));
    outColor = mix(outColor, directionHintBaseColor, clamp(baseHintMask * 0.85, 0.0, 1.0));
    outColor = mix(outColor, directionHintAccentColor, clamp(accentHintMask, 0.0, 1.0));
    outColor = mix(outColor, vec3<f32>(1.0) - outColor, fireInvertMix);

    alpha = clamp(
        alpha + hazeMask * 0.72 + lineMask * 0.92 + baseHintMask * 0.18 + accentHintMask * 0.32 + fireInvertMix * 0.24,
        0.0,
        0.98
    );
    alpha = alpha * opacityScale;
    if (alpha <= 0.001) {
        discard;
    }

    return vec4<f32>(outColor, alpha);
}
