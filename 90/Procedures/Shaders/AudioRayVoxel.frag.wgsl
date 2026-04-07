struct Uniforms {
    model: mat4x4<f32>,
    view: mat4x4<f32>,
    projection: mat4x4<f32>,
    mvp: mat4x4<f32>,
    color: vec4<f32>,
    topColor: vec4<f32>,
    bottomColor: vec4<f32>,
    params: vec4<f32>,
};

@group(0) @binding(0)
var<uniform> u: Uniforms;

struct FSIn {
    @location(0) normal: vec3<f32>,
    @location(1) gain: f32,
    @location(2) occ: f32,
};

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let clearColor = vec3<f32>(0.1, 0.9, 1.0);
    let occColor = vec3<f32>(1.0, 0.3, 0.6);
    let lightDir = normalize(vec3<f32>(0.4, 1.0, 0.2));
    let n = normalize(input.normal);
    let light = clamp(dot(lightDir, n) * 0.5 + 0.5, 0.3, 1.0);
    let gain = clamp(input.gain, 0.0, 1.0);
    let occ = clamp(input.occ, 0.0, 1.0);
    var color = mix(clearColor, occColor, occ);
    color *= mix(0.2, 1.0, gain);
    let baseAlpha = clamp(u.color.a, 0.0, 1.0);
    let alpha = clamp(baseAlpha * gain * 1.4, 0.08, 0.85);
    return vec4<f32>(color * light, alpha);
}
