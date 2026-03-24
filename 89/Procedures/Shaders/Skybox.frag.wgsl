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
    let ndc = input.uv * 2.0 - 1.0;
    let viewDir = normalize(vec3<f32>(ndc.x, ndc.y, 1.0));
    // View matrix is orthonormal rotation + translation, so inverse(rotation) = transpose(rotation).
    let viewBasisInv = mat3x3<f32>(
        vec3<f32>(u.view[0].x, u.view[1].x, u.view[2].x),
        vec3<f32>(u.view[0].y, u.view[1].y, u.view[2].y),
        vec3<f32>(u.view[0].z, u.view[1].z, u.view[2].z)
    );
    let worldDir = normalize(viewBasisInv * viewDir);
    let uMix = clamp(worldDir.y * 0.5 + 0.5, 0.0, 1.0);
    let color = mix(u.bottomColor.rgb, u.topColor.rgb, uMix);
    return vec4<f32>(color, 1.0);
}
