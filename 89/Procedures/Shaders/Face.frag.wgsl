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

    var decodedTileIndex = input.tileIndex;
    var chalkQuarterTurns = 0;
    var encodedChalkTile = false;
    if (decodedTileIndex >= 512) {
        encodedChalkTile = true;
        chalkQuarterTurns = (decodedTileIndex / 512) & 3;
        decodedTileIndex = decodedTileIndex % 512;
    }

    if (wireframeDebug == 1) {
        let lineColor = select(input.fragColor, vec3<f32>(0.5), decodedTileIndex >= 0);
        return vec4<f32>(lineColor, 1.0);
    }

    var bc = input.fragColor;
    let isChalkDust = (input.alpha <= -14.5);
    let isGrassCover = (input.alpha <= -13.5) && !isChalkDust;
    let isCavePot = (input.alpha <= -9.5) && !isGrassCover && !isChalkDust;
    let isSlopeFace = (input.alpha <= -3.5) && (input.alpha > -9.5);
    let isSlopeCapA = (input.alpha <= -3.5) && (input.alpha > -4.5);
    let isSlopeCapB = (input.alpha <= -4.5) && (input.alpha > -5.5);
    var outAlpha = select(max(input.alpha, 0.0), 1.0, (isSlopeFace || isCavePot || isGrassCover || isChalkDust));
    let isFlower = (input.alpha > -3.5) && (input.alpha <= -2.5);
    let isShortGrass = (input.alpha > -2.5) && (input.alpha <= -2.15);
    let isTallGrass = (input.alpha > -2.15) && (input.alpha <= -1.5);
    let isGrass = isTallGrass || isShortGrass;
    let isLeaf = (input.alpha > -1.5) && (input.alpha < 0.0);
    let isTranslucentFace = (input.alpha > 0.0) && (input.alpha < 0.999);
    let useAtlas = (atlasEnabled == 1)
        && (decodedTileIndex >= 0)
        && (tilesPerRow > 0)
        && (tilesPerCol > 0)
        && (atlasTextureSize.x > 0.0)
        && (atlasTextureSize.y > 0.0);

    if (leafBackfacesWhenInside == 1 && !input.frontFacing && !isLeaf) {
        discard;
    }

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

        if (isChalkDust && encodedChalkTile) {
            localUv = rotateUvQuarterTurns(localUv, chalkQuarterTurns);
        }

        let texel = sampleAtlas(decodedTileIndex, localUv, tilesPerRow, tilesPerCol, atlasTileSize, atlasTextureSize);
        bc = texel.rgb;
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
        bc = texel.rgb;
        outAlpha = 1.0;
    }

    let useOreTexture = (decodedTileIndex <= -10 && decodedTileIndex >= -13) && (oreTextureEnabled == 1);
    if (useOreTexture) {
        let oreVariant = -10 - decodedTileIndex;
        let uv = fract(input.texCoord);
        let texel = sampleOre(oreVariant, uv);
        bc = texel.rgb;
        outAlpha = outAlpha * texel.a;
    }

    let useTerrainTexture = (decodedTileIndex <= -20 && decodedTileIndex >= -21) && (terrainTextureEnabled == 1);
    if (useTerrainTexture) {
        let terrainVariant = -20 - decodedTileIndex;
        let uv = fract(input.texCoord);
        let texel = sampleTerrain(terrainVariant, uv);
        bc = texel.rgb;
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
        let useDirectInstanceCell = isGrass || isFlower || isCavePot || isGrassCover || isChalkDust;
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

    if (!isLeaf && !isGrass && !isFlower && !isCavePot && !isTranslucentFace && !isChalkDust) {
        let grid = 24.0;
        let line = 0.03;
        let f = fract(input.texCoord * grid);
        if (f.x < line || f.y < line) {
            bc = vec3<f32>(0.0);
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
            let atlasTile = select(tallBase, shortBase, isShortGrass) + variant;
            let texel = sampleAtlas(atlasTile, uv, tilesPerRow, tilesPerCol, atlasTileSize, atlasTextureSize);
            grassColor = texel.rgb;
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
                grassColor = texel.rgb;
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
        let grassWire = (gf.x < grassLine || gf.y < grassLine);
        if (grassWire && bladeMask > 0.05) {
            grassColor = vec3<f32>(0.0);
        }

        let norm = normalize(input.normal);
        let diff = max(dot(norm, lightDir), 0.0);
        let lighting = ambientLight + diffuseLight * diff;
        return vec4<f32>(grassColor * lighting * input.ao, bladeMask);
    }

    if (isFlower) {
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
        let flowerWire = (ff.x < flowerLine || ff.y < flowerLine);
        if (flowerWire && alphaShape > 0.05) {
            finalColor = vec3<f32>(0.0);
        }

        let norm = normalize(input.normal);
        let diff = max(dot(norm, lightDir), 0.0);
        let lighting = ambientLight + diffuseLight * diff;
        return vec4<f32>(finalColor * lighting * input.ao, alphaShape);
    }

    if (isCavePot) {
        if (outAlpha <= 0.001) {
            discard;
        }
        let norm = normalize(input.normal);
        let diff = max(dot(norm, lightDir), 0.0);
        let lighting = ambientLight + diffuseLight * diff;
        return vec4<f32>(bc * lighting * input.ao, outAlpha);
    }

    if (isLeaf) {
        let gridSize = 24.0;
        let lineWidth = 0.03;
        let f = fract(input.texCoord * gridSize);
        let isGridLine = (f.x < lineWidth || f.y < lineWidth);
        let forceOpaqueLeaf = (leafOpaqueOutsideLod0 == 1) && (sectionLod > 0);

        var finalAlpha = 1.0;
        var finalColor = select(bc, vec3<f32>(0.0), isGridLine);
        if (!forceOpaqueLeaf) {
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

        var lighting = ambientLight;
        if (leafDirectionalLightingEnabled == 1) {
            let norm = normalize(input.normal);
            let diff = max(dot(norm, lightDir), 0.0);
            let shadowAmount = 1.0 - diff;
            let shadowMultiplier = mix(1.0, 1.0 - 0.6 * shadowAmount, leafDirectionalLightingIntensity);
            lighting = lighting * shadowMultiplier;
        }
        finalColor = finalColor * lighting;
        return vec4<f32>(finalColor * input.ao, finalAlpha);
    }

    if (outAlpha <= 0.001) {
        discard;
    }

    let isWaterSurfaceFace = isTranslucentFace && (faceType == 2);
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
        bc = mix(bc, texel.rgb, overlayBlend);
    }

    let _unusedBehavior = behaviorType;
    return vec4<f32>(bc * input.ao, outAlpha);
}
