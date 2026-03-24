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
};

@group(0) @binding(0)
var<uniform> u: Uniforms;

@fragment
fn fs_main() -> @location(0) vec4<f32> {
    return u.color;
}
