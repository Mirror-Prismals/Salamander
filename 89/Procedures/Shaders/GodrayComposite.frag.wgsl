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
@group(0) @binding(1)
var sceneSampler: sampler;
@group(0) @binding(2)
var godrayTex: texture_2d<f32>;

struct FSIn {
    @location(0) uv: vec2<f32>,
};

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let zoom = max(u.params.y, 0.0001);
    let mapCenter = u.vec2Data.zw;
    let uv = (input.uv - vec2<f32>(0.5, 0.5)) / zoom + mapCenter;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return vec4<f32>(0.0, 0.0, 0.0, 1.0);
    }
    let col = textureSample(godrayTex, sceneSampler, uv).rgb;
    return vec4<f32>(col, 1.0);
}
