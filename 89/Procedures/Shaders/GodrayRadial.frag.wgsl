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
var occlusionTex: texture_2d<f32>;

struct FSIn {
    @location(0) uv: vec2<f32>,
};

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let lightPos = u.vec2Data.xy;
    let exposure = u.params.z;
    let decay = u.params.w;
    let density = u.extra.x;
    let weight = u.extra.y;
    let sampleCount = max(1, i32(round(u.extra.z)));

    let delta = (lightPos - input.uv) * density / f32(sampleCount);
    var coord = input.uv;
    var illumination = 0.0;
    var curDecay = 1.0;

    for (var i = 0; i < 256; i = i + 1) {
        if (i >= sampleCount) {
            break;
        }
        coord = coord + delta;
        let sample = textureSample(occlusionTex, sceneSampler, coord).r;
        illumination = illumination + sample * curDecay * weight;
        curDecay = curDecay * decay;
    }

    let rgb = vec3<f32>(illumination * exposure);
    return vec4<f32>(rgb, 1.0);
}
