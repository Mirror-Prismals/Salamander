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

struct VSIn {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) instancePos: vec3<f32>,
    @location(3) instanceGain: f32,
    @location(4) instanceOcc: f32,
};

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) normal: vec3<f32>,
    @location(1) gain: f32,
    @location(2) occ: f32,
};

@vertex
fn vs_main(input: VSIn) -> VSOut {
    var out: VSOut;
    let worldPos = input.instancePos + input.position * 0.9;
    out.position = u.mvp * vec4<f32>(worldPos, 1.0);
    out.normal = input.normal;
    out.gain = input.instanceGain;
    out.occ = input.instanceOcc;
    return out;
}
