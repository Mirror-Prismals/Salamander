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

struct FSIn {
    @location(0) texCoord: vec2<f32>,
    @location(1) fragColor: vec3<f32>,
    @location(2) instanceDistance: f32,
    @location(3) normal: vec3<f32>,
    @location(4) worldPos: vec3<f32>,
    @location(5) instanceCell: vec3<f32>,
};

fn noise(p: vec2<f32>) -> f32 {
    return fract(sin(dot(p, vec2<f32>(12.9898, 78.233))) * 43758.5453);
}

fn faceIndexFromNormal(n: vec3<f32>) -> i32 {
    let an = abs(n);
    if (an.x >= an.y && an.x >= an.z) {
        return select(1, 0, n.x >= 0.0);
    }
    if (an.y >= an.z) {
        return select(3, 2, n.y >= 0.0);
    }
    return select(5, 4, n.z >= 0.0);
}

fn damagePattern(cellUv: vec2<i32>, blockCell: vec3<i32>, faceId: i32) -> f32 {
    let c = vec2<f32>(cellUv);
    let seedA = vec2<f32>(
        f32(blockCell.x) * 0.173 + f32(blockCell.z) * 0.293 + f32(faceId) * 7.0,
        f32(blockCell.y) * 0.271 + f32(faceId) * 3.0
    );
    let seedB = vec2<f32>(
        f32(blockCell.z) * 0.619 + f32(faceId) * 11.0,
        f32(blockCell.x) * 0.887 + f32(blockCell.y) * 0.411
    );
    let coarse = noise(floor((c + seedA) * 0.5));
    let fine = noise(c + seedB);
    return mix(coarse, fine, 0.62);
}

fn blockDamageProgressAt(index: i32) -> f32 {
    let packedIndex = index / 4;
    let lane = index % 4;
    let packed = u.blockDamageProgress[packedIndex];
    if (lane == 0) {
        return packed.x;
    }
    if (lane == 1) {
        return packed.y;
    }
    if (lane == 2) {
        return packed.z;
    }
    return packed.w;
}

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let behaviorType = u.intParams0.x;
    let wireframeDebug = u.intParams0.y;
    let blockDamageEnabled = u.intParams0.z;
    let blockDamageCount = clamp(u.intParams0.w, 0, 64);
    let voxelGridLinesEnabled = (u.intParams6.x != 0);
    let voxelGridLineInvertColorEnabled = (u.intParams6.y & 2) != 0;
    let blockDamageGrid = max(1.0, u.lightAndGrid.w);

    if (wireframeDebug == 1) {
        return vec4<f32>(input.fragColor, 1.0);
    }

    if (blockDamageEnabled == 1
        && behaviorType == 0
        && u.cameraAndScale.w <= 1.001
        && blockDamageCount > 0) {
        let cell = vec3<i32>(round(input.instanceCell));
        var progress = -1.0;
        for (var i = 0; i < 64; i = i + 1) {
            if (i >= blockDamageCount) {
                break;
            }
            if (all(u.blockDamageCells[i].xyz == cell)) {
                progress = clamp(blockDamageProgressAt(i), 0.0, 1.0);
                break;
            }
        }
        if (progress > 0.001) {
            let uv = clamp(fract(input.texCoord), vec2<f32>(0.0), vec2<f32>(0.9999));
            let cellUv = vec2<i32>(floor(uv * blockDamageGrid));
            let faceId = faceIndexFromNormal(normalize(input.normal));
            let threshold = damagePattern(cellUv, cell, faceId);
            let erosion = pow(progress, 0.82);
            if (threshold < erosion) {
                discard;
            }
        }
    }

    if (behaviorType == 4) {
        return vec4<f32>(input.fragColor, 0.4);
    }

    let grid = 24.0;
    let line = 0.03;

    if (behaviorType == 2) {
        let f = fract(input.texCoord * grid);
        let g = voxelGridLinesEnabled && (f.x < line || f.y < line);
        let n = noise(floor((input.texCoord + fract(floor(input.worldPos.xy) * 0.12345)) * 12.0));
        let a = select(1.0, 0.0, n > 0.8);
        let fa = select(a, 1.0, g);
        let lineColor = select(
            vec3<f32>(0.0),
            clamp(vec3<f32>(1.0) - input.fragColor, vec3<f32>(0.0), vec3<f32>(1.0)),
            voxelGridLineInvertColorEnabled
        );
        let c = select(input.fragColor, lineColor, g);
        return vec4<f32>(c, fa);
    }

    if (behaviorType == 1) {
        return vec4<f32>(input.fragColor, 0.6);
    }

    let f = fract(input.texCoord * grid);
    var bc = input.fragColor;
    if (voxelGridLinesEnabled && (f.x < line || f.y < line)) {
        bc = select(
            vec3<f32>(0.0),
            clamp(vec3<f32>(1.0) - input.fragColor, vec3<f32>(0.0), vec3<f32>(1.0)),
            voxelGridLineInvertColorEnabled
        );
    } else {
        let d = input.instanceDistance / 100.0;
        bc = clamp(input.fragColor + vec3<f32>(0.03 * d), vec3<f32>(0.0), vec3<f32>(1.0));
    }
    return vec4<f32>(bc, 1.0);
}
