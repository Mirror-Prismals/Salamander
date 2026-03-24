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
    @location(0) uv: vec2<f32>,
};

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let uv = input.uv - vec2<f32>(0.5, 0.5);
    let r = length(uv) * 2.0;
    let disk = 1.0 - smoothstep(0.16, 0.22, r);
    if (disk < 0.001) {
        discard;
    }
    let base = u.color.rgb;
    let rgb = base * disk;
    let alpha = disk * clamp(u.color.a, 0.0, 1.0);
    return vec4<f32>(rgb, alpha);
}
