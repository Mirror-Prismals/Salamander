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
    @location(0) worldPos: vec3<f32>,
    @location(1) worldNormal: vec3<f32>,
};

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let cameraPos = u.cameraAndScale.xyz;
    let time = u.params.x;
    let viewDir = normalize(cameraPos - input.worldPos);
    if (dot(input.worldNormal, viewDir) <= 0.0) {
        discard;
    }

    let cycle = fract(time * 0.25) * 4.0;
    var pulse = 1.0;
    if (cycle < 3.0) {
        pulse = 1.0;
    } else if (cycle < 4.0) {
        let t = clamp(cycle - 3.0, 0.0, 1.0);
        let easeDown = 1.0 - pow(1.0 - t, 4.0);
        let fall = mix(1.0, 0.0, easeDown);
        let easeUp = pow(t, 4.0);
        let rise = mix(0.0, 1.0, easeUp);
        pulse = mix(fall, rise, t);
    }

    return vec4<f32>(vec3<f32>(pulse), 1.0);
}
