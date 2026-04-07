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
    // Keep the horizon locked at screen midline (uv.y = 0.5), independent of camera rotation.
    let uMix = clamp(input.uv.y, 0.0, 1.0);
    let color = mix(u.bottomColor.rgb, u.topColor.rgb, uMix);
    return vec4<f32>(color, 1.0);
}
