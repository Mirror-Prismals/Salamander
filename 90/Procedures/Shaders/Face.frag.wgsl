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
@group(0) @binding(3)
var grassTexture0: texture_2d<f32>;
@group(0) @binding(4)
var grassTexture1: texture_2d<f32>;
@group(0) @binding(5)
var grassTexture2: texture_2d<f32>;
@group(0) @binding(6)
var shortGrassTexture0: texture_2d<f32>;
@group(0) @binding(7)
var shortGrassTexture1: texture_2d<f32>;
@group(0) @binding(8)
var shortGrassTexture2: texture_2d<f32>;
@group(0) @binding(9)
var oreTexture0: texture_2d<f32>;
@group(0) @binding(10)
var oreTexture1: texture_2d<f32>;
@group(0) @binding(11)
var oreTexture2: texture_2d<f32>;
@group(0) @binding(12)
var oreTexture3: texture_2d<f32>;
@group(0) @binding(13)
var terrainTextureDirt: texture_2d<f32>;
@group(0) @binding(14)
var terrainTextureStone: texture_2d<f32>;
@group(0) @binding(15)
var waterOverlayTexture: texture_2d<f32>;
@group(0) @binding(16)
var waterReflectionTexture: texture_2d<f32>;

struct FSIn {
    @location(0) texCoord: vec2<f32>,
    @location(1) fragColor: vec3<f32>,
    @location(2) instanceDistance: f32,
    @location(3) normal: vec3<f32>,
    @location(4) worldPos: vec3<f32>,
    @location(5) instanceCell: vec3<f32>,
    @location(6) @interpolate(flat) tileIndex: i32,
    @location(7) alpha: f32,
    @location(8) ao: f32,
    @builtin(front_facing) frontFacing: bool,
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

fn rotateUvQuarterTurns(uv: vec2<f32>, turns: i32) -> vec2<f32> {
    let t = turns & 3;
    if (t == 1) {
        return vec2<f32>(1.0 - uv.y, uv.x);
    }
    if (t == 2) {
        return vec2<f32>(1.0 - uv.x, 1.0 - uv.y);
    }
    if (t == 3) {
        return vec2<f32>(uv.y, 1.0 - uv.x);
    }
    return uv;
}

fn hash3dToU32(cell: vec3<i32>) -> u32 {
    var h = bitcast<u32>(cell.x) * 0x8da6b343u;
    h = h ^ (bitcast<u32>(cell.y) * 0xd8163841u);
    h = h ^ (bitcast<u32>(cell.z) * 0xcb1ab31fu);
    h = h ^ (h >> 16u);
    h = h * 0x7feb352du;
    h = h ^ (h >> 15u);
    h = h * 0x846ca68bu;
    h = h ^ (h >> 16u);
    return h;
}

fn hashToUnit01(h: u32) -> f32 {
    return f32(h & 0x00ffffffu) * (1.0 / 16777215.0);
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

fn sampleAtlas(tileIndex: i32, uv: vec2<f32>, tilesPerRow: i32, tilesPerCol: i32, atlasTileSize: vec2<f32>, atlasTextureSize: vec2<f32>) -> vec4<f32> {
    let tileSizeUV = atlasTileSize / atlasTextureSize;
    let tileX = tileIndex % tilesPerRow;
    let tileY = tilesPerCol - 1 - (tileIndex / tilesPerRow);
    let base = vec2<f32>(f32(tileX), f32(tileY)) * tileSizeUV;
    let atlasUv = base + uv * tileSizeUV;
    return textureSample(atlasTexture, sceneSampler, atlasUv);
}

fn sampleAtlasLod0(tileIndex: i32, uv: vec2<f32>, tilesPerRow: i32, tilesPerCol: i32, atlasTileSize: vec2<f32>, atlasTextureSize: vec2<f32>) -> vec4<f32> {
    let tileSizeUV = atlasTileSize / atlasTextureSize;
    let tileX = tileIndex % tilesPerRow;
    let tileY = tilesPerCol - 1 - (tileIndex / tilesPerRow);
    let base = vec2<f32>(f32(tileX), f32(tileY)) * tileSizeUV;
    let atlasUv = base + uv * tileSizeUV;
    return textureSampleLevel(atlasTexture, sceneSampler, atlasUv, 0.0);
}

fn sampleOre(variant: i32, uv: vec2<f32>) -> vec4<f32> {
    if (variant == 1) {
        return textureSample(oreTexture1, sceneSampler, uv);
    }
    if (variant == 2) {
        return textureSample(oreTexture2, sceneSampler, uv);
    }
    if (variant == 3) {
        return textureSample(oreTexture3, sceneSampler, uv);
    }
    return textureSample(oreTexture0, sceneSampler, uv);
}

fn sampleTerrain(variant: i32, uv: vec2<f32>) -> vec4<f32> {
    if (variant == 1) {
        return textureSample(terrainTextureStone, sceneSampler, uv);
    }
    return textureSample(terrainTextureDirt, sceneSampler, uv);
}

fn sampleGrass(variant: i32, uv: vec2<f32>, useShortSet: bool) -> vec4<f32> {
    if (useShortSet) {
        if (variant == 1) {
            return textureSample(shortGrassTexture1, sceneSampler, uv);
        }
        if (variant == 2) {
            return textureSample(shortGrassTexture2, sceneSampler, uv);
        }
        return textureSample(shortGrassTexture0, sceneSampler, uv);
    }
    if (variant == 1) {
        return textureSample(grassTexture1, sceneSampler, uv);
    }
    if (variant == 2) {
        return textureSample(grassTexture2, sceneSampler, uv);
    }
    return textureSample(grassTexture0, sceneSampler, uv);
}

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let behaviorType = u.intParams0.x;
    let wireframeDebug = u.intParams0.y;
    let blockDamageEnabled = u.intParams0.z;
    let blockDamageCount = clamp(u.intParams0.w, 0, 64);

    let faceType = u.intParams1.x;
    let sectionLod = u.intParams1.y;
    let atlasEnabled = u.intParams1.z;
    let tilesPerRow = u.intParams1.w;

    let tilesPerCol = u.intParams2.x;
    let grassTextureEnabled = u.intParams2.y;
    let shortGrassTextureEnabled = u.intParams2.z;
    let oreTextureEnabled = u.intParams2.w;

    let terrainTextureEnabled = u.intParams3.x;
    let waterOverlayTextureEnabled = u.intParams3.y;
    let leafOpaqueOutsideLod0 = u.intParams3.z;
    let leafBackfacesWhenInside = u.intParams3.w;

    let leafDirectionalLightingEnabled = u.intParams4.x;
    let waterCascadeBrightnessEnabled = u.intParams4.y;
    let wallStoneUvJitterEnabled = u.intParams4.z;
    let grassAtlasShortBaseTile = u.intParams4.w;

    let grassAtlasTallBaseTile = u.intParams5.x;
    let wallStoneUvJitterTile0 = u.intParams5.y;
    let wallStoneUvJitterTile1 = u.intParams5.z;
    let wallStoneUvJitterTile2 = u.intParams5.w;
    let voxelGridLinesEnabled = (u.intParams6.x != 0);
    let lightingAndGridMode = u.intParams6.y;
    let foliageLightingEnabled = (lightingAndGridMode & 1) != 0;
    let voxelGridLineInvertColorEnabled = (lightingAndGridMode & 2) != 0;
    let waterUnderwaterCausticsEnabled = (lightingAndGridMode & 8) != 0;

    let lightDir = normalize(u.lightAndGrid.xyz);
    let ambientLight = u.ambientAndLeaf.xyz;
    let diffuseLight = u.diffuseAndWater.xyz;

    let atlasTileSize = u.atlasInfo.xy;
    let atlasTextureSize = u.atlasInfo.zw;

    let blockDamageGrid = max(1.0, u.lightAndGrid.w);
    let leafDirectionalLightingIntensity = clamp(u.ambientAndLeaf.w, 0.0, 1.0);
    let waterCascadeBrightnessStrength = u.diffuseAndWater.w;
    let waterCascadeBrightnessSpeed = u.wallStoneAndWater2.z;
    let waterCascadeBrightnessScale = u.wallStoneAndWater2.w;
    let wallStoneUvJitterMinPixels = max(0.0, u.wallStoneAndWater2.x);
    let wallStoneUvJitterMaxPixels = max(wallStoneUvJitterMinPixels, u.wallStoneAndWater2.y);

    let noGrassVariantEncodeStride = 16384;
    let chalkTileEncodeStride = 2048;
    var decodedTileIndex = input.tileIndex;
    var disableGrassVariant = false;
    if (decodedTileIndex >= noGrassVariantEncodeStride) {
        disableGrassVariant = true;
        decodedTileIndex = decodedTileIndex - noGrassVariantEncodeStride;
    }
    let waterWaveClassAoEncodeStride = 8.0;
    let underwaterCausticsAoEncodeStride = 2.0;
    let shorelineWaterAoEncodeStride = 4.0;
    let waterfallFoamAoEncodeStride = 64.0;
    var decodedAo = input.ao;
    var waterfallFoamFaceTagged = false;
    if (decodedAo >= waterfallFoamAoEncodeStride) {
        waterfallFoamFaceTagged = true;
        decodedAo = decodedAo - waterfallFoamAoEncodeStride;
    }
    var waterWaveClass = 0;
    if (decodedAo >= waterWaveClassAoEncodeStride) {
        waterWaveClass = clamp(i32(floor(decodedAo / waterWaveClassAoEncodeStride)), 0, 4);
        decodedAo = decodedAo - f32(waterWaveClass) * waterWaveClassAoEncodeStride;
    }
    var shorelineWaterFaceTagged = false;
    if (decodedAo >= shorelineWaterAoEncodeStride) {
        shorelineWaterFaceTagged = true;
        decodedAo = decodedAo - shorelineWaterAoEncodeStride;
    }
    var underwaterCausticFaceTagged = false;
    if (decodedAo >= underwaterCausticsAoEncodeStride) {
        underwaterCausticFaceTagged = true;
        decodedAo = decodedAo - underwaterCausticsAoEncodeStride;
    }
    var chalkQuarterTurns = 0;
    var encodedChalkTile = false;
    if (decodedTileIndex >= chalkTileEncodeStride) {
        encodedChalkTile = true;
        chalkQuarterTurns = (decodedTileIndex / chalkTileEncodeStride) & 3;
        decodedTileIndex = decodedTileIndex % chalkTileEncodeStride;
    }

    if (wireframeDebug == 1) {
        let lineColor = select(input.fragColor, vec3<f32>(0.5), decodedTileIndex >= 0);
        return vec4<f32>(lineColor, 1.0);
    }

    var bc = input.fragColor;
    let isBookPage = (input.alpha <= -39.5);
    let alphaTag = select(input.alpha, 1.0, isBookPage);
    let isWallDecal = (alphaTag <= -15.5);
    let isChalkDust = (alphaTag <= -14.5) && !isWallDecal;
    let isGrassCover = (alphaTag <= -13.5) && !isChalkDust && !isWallDecal;
    let isCavePot = (alphaTag <= -9.5) && !isGrassCover && !isChalkDust && !isWallDecal;
    let isSlopeFaceRegular = (alphaTag <= -3.5) && (alphaTag > -9.5);
    let isWaterSlopeFace = (alphaTag <= -23.5) && (alphaTag > -33.5);
    let isSlopeFace = isSlopeFaceRegular || isWaterSlopeFace;
    let isSlopeCapA = ((alphaTag <= -3.5) && (alphaTag > -4.5))
        || ((alphaTag <= -23.5) && (alphaTag > -24.5));
    let isSlopeCapB = ((alphaTag <= -4.5) && (alphaTag > -5.5))
        || ((alphaTag <= -24.5) && (alphaTag > -25.5));
    var outAlpha = select(
        max(alphaTag, 0.0),
        1.0,
        ((isSlopeFace && !isWaterSlopeFace) || isCavePot || isGrassCover || isChalkDust || isWallDecal || isBookPage)
    );
    let isFlower = (alphaTag > -3.5) && (alphaTag <= -2.5);
    let isShortGrass = (alphaTag > -2.5) && (alphaTag <= -2.15);
    let isTallGrass = (alphaTag > -2.15) && (alphaTag <= -1.5);
    let isGrass = isTallGrass || isShortGrass;
    let isLeaf = (alphaTag > -1.5) && (alphaTag < 0.0);
    let isTranslucentFace = (alphaTag > 0.0) && (alphaTag < 0.999);
    let useAtlas = (atlasEnabled == 1)
        && (decodedTileIndex >= 0)
        && (tilesPerRow > 0)
        && (tilesPerCol > 0)
        && (atlasTextureSize.x > 0.0)
        && (atlasTextureSize.y > 0.0);

    if (leafBackfacesWhenInside == 1 && !input.frontFacing && !isLeaf) {
        discard;
    }
    // Foliage cards are authored as opposite face pairs (+X/-X, +Z/-Z).
    // When global alpha culling is disabled, drawing both front and back fragments on each card
    // produces coplanar fighting shimmer while looking around. Keep only front-facing fragments;
    // the opposite paired face still makes the card appear two-sided.
    let isCardFoliage = isGrass || isFlower || isCavePot;
    if (isCardFoliage && !input.frontFacing) {
        discard;
    }
    if (isWallDecal && !input.frontFacing) {
        discard;
    }

    var atlasSampleAlpha = 1.0;
    if (useAtlas) {
        var localUv = fract(input.texCoord);
        let isWallStoneTile = (decodedTileIndex == wallStoneUvJitterTile0)
            || (decodedTileIndex == wallStoneUvJitterTile1)
            || (decodedTileIndex == wallStoneUvJitterTile2);
        if (wallStoneUvJitterEnabled == 1 && isWallStoneTile) {
            let cell = vec3<i32>(floor(input.worldPos + vec3<f32>(0.5)));
            let span = max(1.0, wallStoneUvJitterMaxPixels - wallStoneUvJitterMinPixels + 1.0);
            let rx = noise(vec2<f32>(
                f32(cell.x) * 0.173 + f32(cell.y) * 0.731 + f32(cell.z) * 0.197,
                f32(cell.z) * 0.293 + 17.123
            ));
            let ry = noise(vec2<f32>(
                f32(cell.x) * 0.619 + f32(cell.y) * 0.271 + f32(cell.z) * 0.887,
                f32(cell.x) * 0.411 + 53.701
            ));
            let offsetXPx = floor(rx * span) + wallStoneUvJitterMinPixels;
            let offsetYPx = floor(ry * span) + wallStoneUvJitterMinPixels;
            var localPx = localUv * atlasTileSize;
            let shiftedLocalPx = localPx + vec2<f32>(offsetXPx, offsetYPx);
            localPx = shiftedLocalPx - floor(shiftedLocalPx / atlasTileSize) * atlasTileSize;
            localUv = localPx / atlasTileSize;
        }

        if (encodedChalkTile) {
            localUv = rotateUvQuarterTurns(localUv, chalkQuarterTurns);
        }
        if (isChalkDust || isWallDecal) {
            let halfTexelInset = vec2<f32>(0.5) / atlasTileSize;
            localUv = clamp(localUv, halfTexelInset, vec2<f32>(1.0) - halfTexelInset);
        }

        var atlasTileIndex = decodedTileIndex;
        if (isLeaf && atlasTileIndex >= 382 && atlasTileIndex <= 385) {
            let leafCell = vec3<i32>(round(input.instanceCell));
            let leafVariant = i32(hash3dToU32(leafCell + vec3<i32>(173, -29, 947)) & 3u);
            atlasTileIndex = 382 + leafVariant;
        }
        // Depth lava tiles (atlas 544..552) scroll slowly on a diagonal.
        if (atlasTileIndex >= 544 && atlasTileIndex <= 552) {
            localUv = fract(localUv + vec2<f32>(u.params.x * 0.018, -u.params.x * 0.013));
        }

        var texel = sampleAtlas(atlasTileIndex, localUv, tilesPerRow, tilesPerCol, atlasTileSize, atlasTextureSize);
        if (isChalkDust || isWallDecal) {
            texel = sampleAtlasLod0(atlasTileIndex, localUv, tilesPerRow, tilesPerCol, atlasTileSize, atlasTextureSize);
        }
        atlasSampleAlpha = texel.a;
        bc = texel.rgb * input.fragColor;
        outAlpha = outAlpha * texel.a;
    }

    if (isGrassCover && useAtlas) {
        let tileSizeUV = atlasTileSize / atlasTextureSize;
        let tileX = decodedTileIndex % tilesPerRow;
        let tileY = tilesPerCol - 1 - (decodedTileIndex / tilesPerRow);
        let base = vec2<f32>(f32(tileX), f32(tileY)) * tileSizeUV;
        let blockUv = fract(input.worldPos.xz + vec2<f32>(0.5));
        let snappedUv = (floor(blockUv * 24.0) + vec2<f32>(0.5)) / 24.0;
        let atlasUv = base + snappedUv * tileSizeUV;
        let texel = textureSample(atlasTexture, sceneSampler, atlasUv);
        if (texel.a <= 0.001) {
            discard;
        }
        bc = texel.rgb * input.fragColor;
        outAlpha = texel.a;
    }

    let useOreTexture = (decodedTileIndex <= -10 && decodedTileIndex >= -13) && (oreTextureEnabled == 1);
    if (useOreTexture) {
        let oreVariant = -10 - decodedTileIndex;
        let uv = fract(input.texCoord);
        let texel = sampleOre(oreVariant, uv);
        bc = texel.rgb * input.fragColor;
        outAlpha = outAlpha * texel.a;
    }

    let useTerrainTexture = (decodedTileIndex <= -20 && decodedTileIndex >= -21) && (terrainTextureEnabled == 1);
    if (useTerrainTexture) {
        let terrainVariant = -20 - decodedTileIndex;
        let uv = fract(input.texCoord);
        let texel = sampleTerrain(terrainVariant, uv);
        bc = texel.rgb * input.fragColor;
        outAlpha = outAlpha * texel.a;
    }

    if (isSlopeCapA || isSlopeCapB) {
        let localUv = fract(input.texCoord);
        let keepFragment = select(
            localUv.y <= (1.0 - localUv.x + 0.001),
            localUv.y <= (localUv.x + 0.001),
            isSlopeCapA
        );
        if (!keepFragment) {
            discard;
        }
    }

    if (blockDamageEnabled == 1
        && blockDamageCount > 0
        && !isLeaf
        && !isSlopeFace
        && !isTranslucentFace) {
        let useDirectInstanceCell = isGrass || isFlower || isCavePot || isGrassCover || isChalkDust || isWallDecal;
        var cell = vec3<i32>(0, 0, 0);
        if (useDirectInstanceCell) {
            cell = vec3<i32>(round(input.instanceCell));
        } else {
            cell = vec3<i32>(floor(input.worldPos + vec3<f32>(0.5)));
            let halfStep = 0.5 * exp2(f32(max(sectionLod, 0)));
            if (faceType == 0) {
                cell.x = i32(round(input.instanceCell.x - halfStep));
            } else if (faceType == 1) {
                cell.x = i32(round(input.instanceCell.x + halfStep));
            } else if (faceType == 2) {
                cell.y = i32(round(input.instanceCell.y - halfStep));
            } else if (faceType == 3) {
                cell.y = i32(round(input.instanceCell.y + halfStep));
            } else if (faceType == 4) {
                cell.z = i32(round(input.instanceCell.z - halfStep));
            } else if (faceType == 5) {
                cell.z = i32(round(input.instanceCell.z + halfStep));
            }
        }

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

    if (!isBookPage && !isLeaf && !isGrass && !isFlower && !isCavePot && !isTranslucentFace && !isChalkDust && !isWallDecal) {
        let grid = 24.0;
        let line = 0.03;
        let f = fract(input.texCoord * grid);
        if (voxelGridLinesEnabled && (f.x < line || f.y < line)) {
            bc = select(
                vec3<f32>(0.0),
                clamp(vec3<f32>(1.0) - bc, vec3<f32>(0.0), vec3<f32>(1.0)),
                voxelGridLineInvertColorEnabled
            );
        } else if (!(useAtlas || useOreTexture || useTerrainTexture)) {
            let d = input.instanceDistance / 100.0;
            bc = clamp(input.fragColor + vec3<f32>(0.03 * d), vec3<f32>(0.0), vec3<f32>(1.0));
        }
    }

    if (isGrass) {
        let uv = fract(input.texCoord);
        var grassColor = bc;
        var bladeMask = 1.0;

        let plantCell = vec3<i32>(floor(input.worldPos + vec3<f32>(0.5)));
        let picker = noise(vec2<f32>(
            f32(plantCell.x) * 0.173 + f32(plantCell.y) * 0.731,
            f32(plantCell.z) * 0.293 + f32(plantCell.y) * 0.121
        ));
        let variant = clamp(i32(floor(picker * 3.0)), 0, 2);

        let canUseGrassAtlas = (atlasEnabled == 1)
            && (tilesPerRow > 0)
            && (tilesPerCol > 0)
            && (atlasTextureSize.x > 0.0)
            && (atlasTextureSize.y > 0.0);
        if (canUseGrassAtlas) {
            var shortBase = 39;
            var tallBase = 43;
            if (grassAtlasShortBaseTile >= 0) {
                shortBase = grassAtlasShortBaseTile;
            }
            if (grassAtlasTallBaseTile >= 0) {
                tallBase = grassAtlasTallBaseTile;
            }
            if (decodedTileIndex >= 0) {
                if (isShortGrass) {
                    shortBase = decodedTileIndex;
                } else {
                    tallBase = decodedTileIndex;
                }
            }
            var atlasTile = select(tallBase, shortBase, isShortGrass);
            if (!disableGrassVariant) {
                // Kelp is authored as GrassTuftKelp and should use its exact atlas tile (242),
                // not randomized grass variants.
                if (decodedTileIndex == 242) {
                    atlasTile = decodedTileIndex;
                } else {
                    atlasTile = atlasTile + variant;
                }
            }
            let isLeafFanOak = (decodedTileIndex == 328);
            let isLeafFanPine = (decodedTileIndex == 337);
            var texel = vec4<f32>(0.0);
            if (isLeafFanOak || isLeafFanPine) {
                // Compose a 72x72 fan from the 3x3 tile strip:
                // top row 328/337..330/339, middle .., bottom ..336/345.
                let baseTile = select(337, 328, isLeafFanOak);
                let gridUv = clamp(uv * 3.0, vec2<f32>(0.0), vec2<f32>(2.9999));
                let cellX = clamp(i32(floor(gridUv.x)), 0, 2);
                let cellY = clamp(i32(floor(gridUv.y)), 0, 2);
                let rowFromTop = 2 - cellY;
                let tile = baseTile + rowFromTop * 3 + cellX;
                let localUv = fract(gridUv);
                texel = sampleAtlasLod0(tile, localUv, tilesPerRow, tilesPerCol, atlasTileSize, atlasTextureSize);
            } else {
                texel = sampleAtlas(atlasTile, uv, tilesPerRow, tilesPerCol, atlasTileSize, atlasTextureSize);
            }
            grassColor = texel.rgb * input.fragColor;
            bladeMask = texel.a;
            if (bladeMask <= 0.001) {
                discard;
            }
        } else {
            let hasTallTextures = (grassTextureEnabled == 1);
            let hasShortTextures = (shortGrassTextureEnabled == 1);
            let useShortTextureSet = isShortGrass && hasShortTextures;
            let useTallTextureSet = hasTallTextures && !useShortTextureSet;

            if (useTallTextureSet || useShortTextureSet) {
                let texel = sampleGrass(variant, uv, useShortTextureSet);
                grassColor = texel.rgb * input.fragColor;
                bladeMask = texel.a;
                if (bladeMask <= 0.001) {
                    discard;
                }
            } else {
                let x = uv.x - 0.5;
                let y = clamp(uv.y, 0.0, 1.0);
                let bladeHalf = mix(0.44, 0.07, y);
                bladeMask = 1.0 - smoothstep(bladeHalf, bladeHalf + 0.02, abs(x));
                if (bladeMask <= 0.001) {
                    discard;
                }
                grassColor = mix(bc * 0.62, bc * 1.12, y);
            }
        }

        let grassGrid = 24.0;
        let grassLine = 0.03;
        let gf = fract(uv * grassGrid);
        let grassWire = voxelGridLinesEnabled && (gf.x < grassLine || gf.y < grassLine);
        if (grassWire && bladeMask > 0.05) {
            grassColor = select(
                vec3<f32>(0.0),
                clamp(vec3<f32>(1.0) - grassColor, vec3<f32>(0.0), vec3<f32>(1.0)),
                voxelGridLineInvertColorEnabled
            );
        }

        var lighting = vec3<f32>(1.0);
        if (foliageLightingEnabled) {
            let norm = normalize(input.normal);
            let diff = max(dot(norm, lightDir), 0.0);
            let directional = 0.65 + 0.35 * diff;
            lighting = vec3<f32>(directional);
        }
        return vec4<f32>(grassColor * lighting * decodedAo, bladeMask);
    }

    if (isFlower) {
        if (useAtlas && decodedTileIndex >= 0) {
            var uv = fract(input.texCoord);
            let halfTexelInset = vec2<f32>(0.5) / atlasTileSize;
            uv = clamp(uv, halfTexelInset, vec2<f32>(1.0) - halfTexelInset);
            let texel = sampleAtlasLod0(decodedTileIndex, uv, tilesPerRow, tilesPerCol, atlasTileSize, atlasTextureSize);
            if (texel.a <= 0.001) {
                discard;
            }

            var finalColor = texel.rgb * input.fragColor;
            let flowerLine = 0.03;
            let ff = fract(uv * 24.0);
            let flowerWire = voxelGridLinesEnabled && (ff.x < flowerLine || ff.y < flowerLine);
            if (flowerWire) {
                finalColor = select(
                    vec3<f32>(0.0),
                    clamp(vec3<f32>(1.0) - finalColor, vec3<f32>(0.0), vec3<f32>(1.0)),
                    voxelGridLineInvertColorEnabled
                );
            }

            var lighting = vec3<f32>(1.0);
            if (foliageLightingEnabled) {
                let norm = normalize(input.normal);
                let diff = max(dot(norm, lightDir), 0.0);
                let directional = 0.65 + 0.35 * diff;
                lighting = vec3<f32>(directional);
            }
            return vec4<f32>(finalColor * lighting * decodedAo, texel.a);
        }

        let uvFull = fract(input.texCoord);
        let flowerPixelRes = 24.0;
        let uv = (floor(uvFull * flowerPixelRes) + vec2<f32>(0.5)) / flowerPixelRes;
        let x = uv.x - 0.5;
        let y = clamp(uv.y, 0.0, 1.0);

        let stem = select(0.0, 1.0 - smoothstep(0.045, 0.07, abs(x)), y < 0.74);
        let p = vec2<f32>(x, y - 0.79);
        let ang = atan2(p.y, p.x);
        let rad = length(p);

        let plantCell = vec3<i32>(floor(input.worldPos + vec3<f32>(0.5)));
        let quarterPicker = noise(vec2<f32>(
            f32(plantCell.x) * 0.173 + f32(plantCell.y) * 0.731,
            f32(plantCell.z) * 0.293 + f32(plantCell.y) * 0.121
        ));
        let quarterTurns = clamp(i32(floor(quarterPicker * 4.0)), 0, 3);
        let phase = noise(floor(input.worldPos.xz * 0.75)) * 6.2831853 + f32(quarterTurns) * 1.57079632679;
        let petalRadius = 0.17 + 0.05 * cos(5.0 * ang + phase);
        let blossom = 1.0 - smoothstep(petalRadius, petalRadius + 0.03, rad);
        let center = 1.0 - smoothstep(0.05, 0.075, rad);
        var alphaShape = max(stem, blossom);
        alphaShape = select(0.0, 1.0, alphaShape > 0.5);
        if (alphaShape <= 0.001) {
            discard;
        }

        let stemColor = vec3<f32>(0.16, 0.56, 0.20);
        let petalColor = mix(bc * 0.9, bc * 1.12, y);
        let centerColor = vec3<f32>(0.98, 0.84, 0.22);
        var finalColor = select(petalColor, stemColor, stem > blossom);
        finalColor = mix(finalColor, centerColor, center);

        let flowerLine = 0.03;
        let ff = fract(uvFull * flowerPixelRes);
        let flowerWire = voxelGridLinesEnabled && (ff.x < flowerLine || ff.y < flowerLine);
        if (flowerWire && alphaShape > 0.05) {
            finalColor = select(
                vec3<f32>(0.0),
                clamp(vec3<f32>(1.0) - finalColor, vec3<f32>(0.0), vec3<f32>(1.0)),
                voxelGridLineInvertColorEnabled
            );
        }

        var lighting = vec3<f32>(1.0);
        if (foliageLightingEnabled) {
            let norm = normalize(input.normal);
            let diff = max(dot(norm, lightDir), 0.0);
            let directional = 0.65 + 0.35 * diff;
            lighting = vec3<f32>(directional);
        }
        return vec4<f32>(finalColor * lighting * decodedAo, alphaShape);
    }

    if (isCavePot) {
        if (outAlpha <= 0.001) {
            discard;
        }
        var lighting = vec3<f32>(1.0);
        if (foliageLightingEnabled) {
            let norm = normalize(input.normal);
            let diff = max(dot(norm, lightDir), 0.0);
            let directional = 0.65 + 0.35 * diff;
            lighting = vec3<f32>(directional);
        }
        return vec4<f32>(bc * lighting * decodedAo, outAlpha);
    }

    if (isLeaf) {
        let gridSize = 24.0;
        let lineWidth = 0.03;
        let f = fract(input.texCoord * gridSize);
        let isGridLine = voxelGridLinesEnabled && (f.x < lineWidth || f.y < lineWidth);
        let forceOpaqueLeaf = (leafOpaqueOutsideLod0 == 1) && (sectionLod > 0);
        let atlasLeaf = useAtlas;
        let leafLineColor = select(
            vec3<f32>(0.0),
            clamp(vec3<f32>(1.0) - bc, vec3<f32>(0.0), vec3<f32>(1.0)),
            voxelGridLineInvertColorEnabled
        );

        var finalAlpha = 1.0;
        var finalColor = select(bc, leafLineColor, isGridLine);
        if (atlasLeaf) {
            if (atlasSampleAlpha <= 0.001) {
                discard;
            }
            if (!forceOpaqueLeaf) {
                finalAlpha = atlasSampleAlpha;
            }
        } else if (!forceOpaqueLeaf) {
            let blockCoord = floor(input.worldPos.xy);
            let seed = fract(blockCoord * 0.12345);
            let cell = floor((input.texCoord + seed) * 24.0);
            let n = noise(cell);
            let noiseAlpha = select(1.0, 0.0, n > 0.8);
            finalAlpha = select(noiseAlpha, 1.0, isGridLine);
            if (finalAlpha <= 0.001) {
                discard;
            }
        }

        var lighting = vec3<f32>(1.0);
        if (foliageLightingEnabled && leafDirectionalLightingEnabled == 1) {
            let norm = normalize(input.normal);
            let diff = max(dot(norm, lightDir), 0.0);
            let shadowAmount = 1.0 - diff;
            let shadowMultiplier = mix(1.0, 1.0 - 0.6 * shadowAmount, leafDirectionalLightingIntensity);
            lighting = vec3<f32>(shadowMultiplier);
        }
        finalColor = finalColor * lighting;
        return vec4<f32>(finalColor * decodedAo, finalAlpha);
    }

    if (outAlpha <= 0.001) {
        discard;
    }

    let isSlopeTop = ((alphaTag <= -5.5) && (alphaTag > -9.5))
        || ((alphaTag <= -25.5) && (alphaTag > -33.5));
    let isWaterSurfaceFace = (isTranslucentFace && (waterWaveClass > 0) && (faceType == 2))
        || (isWaterSlopeFace && isSlopeTop && (faceType == 2));
    if (isWaterSurfaceFace
        && waterCascadeBrightnessEnabled == 1
        && sectionLod == 0) {
        let t = u.params.x * waterCascadeBrightnessSpeed;
        let spatial = max(0.0001, waterCascadeBrightnessScale);
        let c0 = sin((input.worldPos.x * 1.00 + input.worldPos.z * 0.82) * spatial + t);
        let c1 = sin((input.worldPos.x * -0.61 + input.worldPos.z * 1.27) * (spatial * 1.73) - t * 1.21);
        let c2 = sin((input.worldPos.x * 0.37 + input.worldPos.z * -0.48) * (spatial * 0.79) + t * 0.67);
        let cascade = c0 * 0.52 + c1 * 0.33 + c2 * 0.15;
        let brightness = 1.0 + waterCascadeBrightnessStrength * cascade;
        bc = clamp(bc * brightness, vec3<f32>(0.0), vec3<f32>(1.0));
    }

    if (isWaterSurfaceFace && waterOverlayTextureEnabled == 1) {
        var uv = fract(input.texCoord);
        let waterCell = vec3<i32>(floor(input.worldPos + vec3<f32>(0.5)));
        let h = (waterCell.x * 73856093) ^ (waterCell.y * 19349663) ^ (waterCell.z * 83492791);
        let quarterTurns = h & 3;
        uv = rotateUvQuarterTurns(uv, quarterTurns);
        let texel = textureSample(waterOverlayTexture, sceneSampler, uv);
        let overlayBlend = clamp(texel.a, 0.0, 1.0);
        bc = mix(bc, texel.rgb * input.fragColor, overlayBlend);
    }

    if (isWaterSurfaceFace) {
        let t = u.params.x;
        var waveSpatial = 0.085;
        var waveNormalStrength = 2.2;
        var waveSpeedMul = 1.0;
        if (waterWaveClass == 1) {
            // pond
            waveSpatial = 0.135;
            waveNormalStrength = 1.6;
            waveSpeedMul = 0.78;
        } else if (waterWaveClass == 2) {
            // lake
            waveSpatial = 0.095;
            waveNormalStrength = 2.0;
            waveSpeedMul = 0.95;
        } else if (waterWaveClass == 3) {
            // river
            waveSpatial = 0.065;
            waveNormalStrength = 2.6;
            waveSpeedMul = 1.22;
        } else if (waterWaveClass == 4) {
            // ocean
            waveSpatial = 0.045;
            waveNormalStrength = 3.1;
            waveSpeedMul = 1.45;
        }

        let waveUv = input.worldPos.xz * waveSpatial;
        let waveA = sin(waveUv.x + t * 0.92 * waveSpeedMul);
        let waveB = cos(waveUv.y * 1.31 - t * 0.71 * waveSpeedMul);
        let waveC = sin((waveUv.x + waveUv.y) * 0.73 + t * 0.47 * waveSpeedMul);
        let wave0 = waveA * 0.58 + waveB * 0.32 + waveC * 0.26;

        let dWaveDx = waveSpatial * (
            0.58 * cos(waveUv.x + t * 0.92 * waveSpeedMul)
            + 0.26 * 0.73 * cos((waveUv.x + waveUv.y) * 0.73 + t * 0.47 * waveSpeedMul)
        );
        let dWaveDz = waveSpatial * (
            -0.32 * 1.31 * sin(waveUv.y * 1.31 - t * 0.71 * waveSpeedMul)
            + 0.26 * 0.73 * cos((waveUv.x + waveUv.y) * 0.73 + t * 0.47 * waveSpeedMul)
        );
        let waveNormal = normalize(vec3<f32>(-dWaveDx * waveNormalStrength, 1.0, -dWaveDz * waveNormalStrength));
        let nDotL = max(dot(waveNormal, lightDir), 0.0);

        let ambientLevel = clamp(dot(ambientLight, vec3<f32>(0.33333334)), 0.0, 1.0);
        let diffuseLevel = clamp(dot(diffuseLight, vec3<f32>(0.33333334)), 0.0, 1.0);
        let dayFactor = clamp(diffuseLevel * 1.2, 0.0, 1.0);

        let baseLuma = clamp(dot(bc, vec3<f32>(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
        let depthFromColor = 1.0 - baseLuma;
        let depthFromAo = clamp((1.0 - decodedAo) * 0.55, 0.0, 1.0);
        let bedVariation = 0.5 + 0.5 * sin(input.worldPos.x * 0.061 + input.worldPos.z * 0.049);
        let depthFactor = clamp(depthFromColor * 0.40 + depthFromAo * 0.14 + bedVariation * 0.06, 0.0, 1.0);
        let shallowFactor = 1.0 - depthFactor;

        let baseShallowTint = vec3<f32>(0.0, 0.5019608, 1.0);                     // #0080FF
        let baseDeepTint = vec3<f32>(0.0, 0.44705883, 0.8980392);                 // #0072E5
        var waterColor = mix(baseShallowTint, baseDeepTint, depthFactor * 0.35);

        let refractDir = vec2<f32>(waveNormal.x, waveNormal.z);
        let refractUv = fract(
            input.worldPos.xz * (0.055 * (waveSpatial / 0.085))
            + refractDir * 0.006
            + vec2<f32>(waveB, waveA) * 0.003
        );
        if (terrainTextureEnabled == 1) {
            let bedTex = textureSample(terrainTextureDirt, sceneSampler, refractUv).rgb;
            var bedTint = clamp(bedTex * vec3<f32>(0.34, 0.50, 0.60), vec3<f32>(0.0), vec3<f32>(1.0));
            let refractAmount = 0.03 * shallowFactor;
            waterColor = mix(waterColor, bedTint, refractAmount);
        }

        if (shorelineWaterFaceTagged
            && atlasEnabled == 1
            && tilesPerRow > 0
            && tilesPerCol > 0
            && atlasTextureSize.x > 0.0
            && atlasTextureSize.y > 0.0) {
            let shorelineFoamTile = 214;
            let foamCell = vec3<i32>(floor(input.worldPos + vec3<f32>(0.5)));
            let foamHash = hash3dToU32(foamCell);
            let shorelineFoamDropoutChance = 0.16;
            let foamKeep = hashToUnit01(foamHash ^ 0x9e3779b9u) >= shorelineFoamDropoutChance;
            if (foamKeep) {
                var foamUv = fract(
                    input.worldPos.xz * 1.00
                    + vec2<f32>(waveB, waveA) * 0.02
                    + vec2<f32>(t * 0.020, -t * 0.013)
                );
                let foamTurns = i32((foamHash >> 29u) & 3u);
                foamUv = rotateUvQuarterTurns(foamUv, foamTurns);
                if (((foamHash >> 3u) & 1u) != 0u) {
                    foamUv = vec2<f32>(1.0 - foamUv.x, foamUv.y);
                }
                let foamTexel = sampleAtlasLod0(
                    shorelineFoamTile,
                    foamUv,
                    tilesPerRow,
                    tilesPerCol,
                    atlasTileSize,
                    atlasTextureSize
                );
                let foamLuma = clamp(dot(foamTexel.rgb, vec3<f32>(0.33333334)), 0.0, 1.0);
                let foamStrength = clamp(foamTexel.a * mix(0.35, 0.90, foamLuma), 0.0, 1.0);
                let foamColor = clamp(foamTexel.rgb * vec3<f32>(1.02, 1.05, 1.06), vec3<f32>(0.0), vec3<f32>(1.0));
                waterColor = mix(waterColor, foamColor, foamStrength * 0.72);
            }
        }

        // Keep water closer to authored hue and avoid over-darkening from directional lighting.
        let lightLevel = clamp(0.74 + ambientLevel * 0.16 + diffuseLevel * (0.08 + 0.12 * nDotL), 0.74, 1.00);
        bc = clamp(waterColor * lightLevel, vec3<f32>(0.0), vec3<f32>(1.0));
        let waterGrid = 24.0;
        let waterLine = 0.03;
        let wf = fract(input.texCoord * waterGrid);
        let waterWire = voxelGridLinesEnabled && (wf.x < waterLine || wf.y < waterLine);
        if (waterWire) {
            bc = select(
                vec3<f32>(0.0),
                clamp(vec3<f32>(1.0) - bc, vec3<f32>(0.0), vec3<f32>(1.0)),
                voxelGridLineInvertColorEnabled
            );
        }

        let targetAlpha = mix(0.15, 0.35, depthFactor);
        outAlpha = clamp(max(outAlpha, targetAlpha), 0.12, 0.44);
    }

    let isWaterSideFace = (isTranslucentFace && (waterWaveClass > 0) && !isWaterSurfaceFace)
        || (isWaterSlopeFace && !isWaterSurfaceFace);
    if (isWaterSideFace) {
        let ambientLevel = clamp(dot(ambientLight, vec3<f32>(0.33333334)), 0.0, 1.0);
        let diffuseLevel = clamp(dot(diffuseLight, vec3<f32>(0.33333334)), 0.0, 1.0);
        let nDotL = max(dot(normalize(input.normal), lightDir), 0.0);

        var sideShallowTint = vec3<f32>(0.0, 0.5019608, 1.0);                    // #0080FF
        var sideDeepTint = vec3<f32>(0.0, 0.44705883, 0.8980392);                // #0072E5
        let sideDepth = clamp(0.48 + (1.0 - decodedAo) * 0.62, 0.0, 1.0);
        let sideTint = mix(sideShallowTint, sideDeepTint, sideDepth);

        let sideLight = clamp(
            0.76 + ambientLevel * 0.14 + diffuseLevel * (0.06 + 0.10 * nDotL),
            0.76,
            1.00
        );
        bc = clamp(sideTint * sideLight, vec3<f32>(0.0), vec3<f32>(1.0));
        if (waterfallFoamFaceTagged
            && atlasEnabled == 1
            && tilesPerRow > 0
            && tilesPerCol > 0
            && atlasTextureSize.x > 0.0
            && atlasTextureSize.y > 0.0) {
            let waterfallFoamTile = 214;
            let cell = vec3<i32>(floor(input.worldPos - input.normal * 0.5 + vec3<f32>(0.5)));
            let foamHash = hash3dToU32(cell ^ vec3<i32>(31, 113, 241));
            let axisCoord = select(input.worldPos.x, input.worldPos.z, faceType == 0 || faceType == 1);
            let speed = max(1.0, waterCascadeBrightnessSpeed) * 2.35;
            var foamUv = fract(vec2<f32>(
                axisCoord * 1.0 + hashToUnit01(foamHash ^ 0xa24baeddu) * 0.35,
                input.worldPos.y * 0.9 + u.params.x * speed
            ));
            let foamTurns = i32((foamHash >> 30u) & 3u);
            foamUv = rotateUvQuarterTurns(foamUv, foamTurns);
            if (((foamHash >> 5u) & 1u) != 0u) {
                foamUv = vec2<f32>(1.0 - foamUv.x, foamUv.y);
            }
            let foamTexel = sampleAtlasLod0(
                waterfallFoamTile,
                foamUv,
                tilesPerRow,
                tilesPerCol,
                atlasTileSize,
                atlasTextureSize
            );
            let trainPhase = fract(input.worldPos.y * 0.9 + u.params.x * speed);
            let trainSlots = trainPhase * 9.0;
            let burstRegion = step(trainSlots, 3.0);
            let slotPhase = fract(trainSlots);
            let pulse = smoothstep(0.10, 0.28, slotPhase) * (1.0 - smoothstep(0.55, 0.78, slotPhase));
            let ringMask = pulse * burstRegion;
            let foamLuma = clamp(dot(foamTexel.rgb, vec3<f32>(0.33333334)), 0.0, 1.0);
            let foamStrength = clamp(foamTexel.a * mix(0.45, 0.95, foamLuma), 0.0, 1.0) * ringMask;
            let foamColor = clamp(foamTexel.rgb * vec3<f32>(1.06, 1.08, 1.10), vec3<f32>(0.0), vec3<f32>(1.0));
            bc = mix(bc, foamColor, foamStrength * 0.86);
        }
        let waterGrid = 24.0;
        let waterLine = 0.03;
        let wf = fract(input.texCoord * waterGrid);
        let waterWire = voxelGridLinesEnabled && (wf.x < waterLine || wf.y < waterLine);
        if (waterWire) {
            bc = select(
                vec3<f32>(0.0),
                clamp(vec3<f32>(1.0) - bc, vec3<f32>(0.0), vec3<f32>(1.0)),
                voxelGridLineInvertColorEnabled
            );
        }
        outAlpha = clamp(max(outAlpha, mix(0.22, 0.40, sideDepth)), 0.20, 0.46);
    }

    if (!isWaterSurfaceFace && underwaterCausticFaceTagged && waterUnderwaterCausticsEnabled) {
        let t = u.params.x;
        let causticSpeed = max(0.25, waterCascadeBrightnessSpeed);
        let causticScale = max(0.35, waterCascadeBrightnessScale * 1.8 + 0.55);
        let causticUv = input.worldPos.xz * causticScale + vec2<f32>(t * 0.68 * causticSpeed, -t * 0.54 * causticSpeed);
        let cA = sin(causticUv.x);
        let cB = sin(causticUv.y * 1.19);
        let cC = sin((causticUv.x + causticUv.y) * 0.73 + t * 0.33 * causticSpeed);
        let causticRaw = clamp(cA * 0.50 + cB * 0.34 + cC * 0.16, -1.0, 1.0) * 0.5 + 0.5;
        let causticPattern = pow(causticRaw, 2.5);
        let causticStrength = 0.16 + waterCascadeBrightnessStrength * 0.56;
        bc = clamp(
            bc + vec3<f32>(0.09, 0.14, 0.19) * (causticPattern * causticStrength),
            vec3<f32>(0.0),
            vec3<f32>(1.0)
        );
    }

    let _unusedBehavior = behaviorType;
    let translucentLikeFace = isTranslucentFace || isWaterSlopeFace;
    let finalAo = select(decodedAo, mix(1.0, decodedAo, 0.30), translucentLikeFace);
    return vec4<f32>(bc * finalAo, outAlpha);
}
