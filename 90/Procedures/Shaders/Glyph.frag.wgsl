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
    cameraAndScale: vec4<f32>,
    lightAndGrid: vec4<f32>,
    ambientAndLeaf: vec4<f32>,
    diffuseAndWater: vec4<f32>,
    atlasInfo: vec4<f32>,
    wallStoneAndWater2: vec4<f32>,
    intParams0: vec4<i32>,
    intParams1: vec4<i32>,
    intParams2: vec4<i32>,
    intParams3: vec4<i32>,
    intParams4: vec4<i32>,
    intParams5: vec4<i32>,
    intParams6: vec4<i32>,
    blockDamageCells: array<vec4<i32>, 64>,
    blockDamageProgress: array<vec4<f32>, 16>,
};

@group(0) @binding(0)
var<uniform> u: Uniforms;

fn tri(p: vec2<f32>, c: vec2<f32>, s: f32, dir: f32) -> f32 {
    let h = s * 0.86602540378;
    let a = c + vec2<f32>(-dir * h / 3.0, s / 2.0);
    let b = c + vec2<f32>(-dir * h / 3.0, -s / 2.0);
    let cpt = c + vec2<f32>(dir * 2.0 * h / 3.0, 0.0);
    let v0 = b - a;
    let v1 = cpt - a;
    let v2 = p - a;
    let d00 = dot(v0, v0);
    let d01 = dot(v0, v1);
    let d11 = dot(v1, v1);
    let d20 = dot(v2, v0);
    let d21 = dot(v2, v1);
    let denom = d00 * d11 - d01 * d01;
    if (abs(denom) < 0.000001) {
        return 0.0;
    }
    let v = (d11 * d20 - d01 * d21) / denom;
    let w = (d00 * d21 - d01 * d20) / denom;
    let ub = 1.0 - v - w;
    return step(0.0, min(ub, min(v, w)));
}

fn squareMask(p: vec2<f32>, c: vec2<f32>, s: f32) -> f32 {
    let d = abs(p - c);
    return step(max(d.x, d.y), s * 0.5);
}

fn circleMask(p: vec2<f32>, c: vec2<f32>, r: f32) -> f32 {
    return step(length(p - c), r);
}

fn bar(p: vec2<f32>, c: vec2<f32>, size: vec2<f32>) -> f32 {
    let d = abs(p - c);
    return step(max(d.x - size.x, d.y - size.y), 0.0);
}

@fragment
fn fs_main(@builtin(position) fragCoord: vec4<f32>) -> @location(0) vec4<f32> {
    let resolution = u.vec2Data.zw;
    let centerIn = u.vec2Data.xy;
    let buttonSize = u.atlasInfo.xy;
    let pressOffset = u.params.z;
    let typeId = u.intParams1.x;
    let glyphColor = u.color.xyz;

    let p = fragCoord.xy;
    let pressVec = vec2<f32>(-pressOffset, pressOffset * 0.5);
    let center = centerIn + pressVec;

    let base = min(buttonSize.x, buttonSize.y) * 0.8;
    let s = base * 0.85;

    var mask = 0.0;
    if (typeId == 0) {
        mask = squareMask(p, center, s);
    } else if (typeId == 1) {
        mask = tri(p, center, s, 1.0);
    } else if (typeId == 2) {
        mask = circleMask(p, center, s * 0.5);
    } else if (typeId == 3) {
        let mTri = tri(p, center + vec2<f32>(s * 0.18, 0.0), s, -1.0);
        let mBar = bar(p, center + vec2<f32>(-s * 0.42, 0.0), vec2<f32>(s * 0.027, s * 0.5));
        mask = max(mTri, mBar);
    }

    if (mask < 0.5) {
        discard;
    }
    return vec4<f32>(glyphColor, 1.0);
}
