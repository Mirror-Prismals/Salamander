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
    @location(0) sparkle: f32,
};

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let b = 0.8 + 0.2 * sin(u.params.x * 3.0 + input.sparkle * 10.0);
    return vec4<f32>(vec3<f32>(b), 1.0);
}
