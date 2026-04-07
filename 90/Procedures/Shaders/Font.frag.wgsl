@group(0) @binding(1)
var sceneSampler: sampler;

@group(0) @binding(2)
var fontTex: texture_2d<f32>;

struct FSIn {
    @location(0) uv: vec2<f32>,
    @location(1) color: vec3<f32>,
};

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let alpha = textureSample(fontTex, sceneSampler, input.uv).r;
    return vec4<f32>(input.color, alpha);
}
