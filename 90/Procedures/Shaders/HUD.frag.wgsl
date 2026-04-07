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
@group(0) @binding(1)
var sceneSampler: sampler;
@group(0) @binding(2)
var atlasTexture: texture_2d<f32>;

struct FSIn {
    @location(0) uv: vec2<f32>,
};

const channelColors: array<vec3<f32>, 3> = array<vec3<f32>, 3>(
    vec3<f32>(1.0, 0.3, 0.3),
    vec3<f32>(0.3, 1.0, 0.3),
    vec3<f32>(0.3, 0.6, 1.0)
);

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let fillAmount = u.params.y;
    let ready = (u.intParams0.x != 0);
    let buildModeType = u.intParams0.y;
    let channelIndex = u.intParams0.z;
    let previewTileIndex = u.intParams0.w;
    let previewColor = u.color.xyz;

    let atlasEnabled = u.intParams1.z;
    let tilesPerRow = u.intParams1.w;
    let tilesPerCol = u.intParams2.x;
    let atlasTileSize = u.atlasInfo.xy;
    let atlasTextureSize = u.atlasInfo.zw;

    let y = clamp(input.uv.y, 0.0, 1.0);
    let bend = sin(y * 3.14159) * -0.15;
    let x = (input.uv.x - 0.5 + bend * 0.4) * 2.0;
    let outerHalf = mix(0.35, 0.6, y);
    let innerHalf = outerHalf - 0.18;
    let outerMask = smoothstep(outerHalf, outerHalf - 0.015, abs(x));
    let innerMask = smoothstep(innerHalf - 0.02, innerHalf + 0.005, abs(x));
    var rim = outerMask * (1.0 - innerMask);

    let isBuildMode = (buildModeType == 1) || (buildModeType == 2);
    let isTextureMode = (buildModeType == 2);
    let isDestroyMode = (buildModeType == 3);
    let isFishingMode = (buildModeType == 4);
    let targetFill = select(clamp(fillAmount, 0.0, 1.0), 1.0, isBuildMode);
    let fillMask = select(0.0, 1.0, y <= targetFill);
    rim = rim * fillMask;
    if (rim < 0.02) {
        discard;
    }

    var color = vec3<f32>(1.0, 1.0, 1.0);
    if (isBuildMode) {
        if (isTextureMode) {
            let useAtlas = (atlasEnabled == 1)
                && (previewTileIndex >= 0)
                && (tilesPerRow > 0)
                && (tilesPerCol > 0)
                && (atlasTextureSize.x > 0.0)
                && (atlasTextureSize.y > 0.0);
            if (useAtlas) {
                let tileSizeUV = atlasTileSize / atlasTextureSize;
                let tileX = previewTileIndex % tilesPerRow;
                let tileY = tilesPerCol - 1 - (previewTileIndex / tilesPerRow);
                let base = vec2<f32>(f32(tileX), f32(tileY)) * tileSizeUV;
                let atlasUv = base + input.uv * tileSizeUV;
                color = textureSample(atlasTexture, sceneSampler, atlasUv).rgb;
            } else {
                color = previewColor;
            }
        } else {
            let idx = clamp(channelIndex, 0, 2);
            let bottomColor = channelColors[idx];
            color = mix(bottomColor, previewColor, y);
        }
    } else {
        let fillStart = select(
            vec3<f32>(0.1, 0.8, 0.2),
            vec3<f32>(0.0, 1.0, 1.0),
            isDestroyMode
        );
        let fillStart2 = select(fillStart, vec3<f32>(0.0, 0.9, 1.0), isFishingMode);
        let fillEnd = select(
            vec3<f32>(1.0, 0.5, 0.0),
            vec3<f32>(1.0, 0.0, 1.0),
            isDestroyMode
        );
        let fillEnd2 = select(fillEnd, vec3<f32>(1.0, 0.52, 0.0), isFishingMode);
        color = mix(fillStart2, fillEnd2, y);
    }

    let readyBoost = select(1.0, 1.02, ready);
    let alpha = 0.85 * rim * readyBoost;
    return vec4<f32>(color, alpha);
}
