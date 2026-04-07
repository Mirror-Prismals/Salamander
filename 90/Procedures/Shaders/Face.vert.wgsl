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

struct VSIn {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) texCoord: vec2<f32>,
    @location(3) offset: vec3<f32>,
    @location(4) color: vec3<f32>,
    @location(5) tileIndex: i32,
    @location(6) alpha: f32,
    @location(7) ao: vec4<f32>,
    @location(8) scale: vec2<f32>,
    @location(9) uvScale: vec2<f32>,
};

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) texCoord: vec2<f32>,
    @location(1) fragColor: vec3<f32>,
    @location(2) instanceDistance: f32,
    @location(3) normal: vec3<f32>,
    @location(4) worldPos: vec3<f32>,
    @location(5) instanceCell: vec3<f32>,
    @location(6) @interpolate(flat) tileIndex: i32,
    @location(7) alpha: f32,
    @location(8) ao: f32,
};

fn rotateY(v: vec3<f32>, r: f32) -> vec3<f32> {
    let c = cos(r);
    let s = sin(r);
    return vec3<f32>(
        c * v.x - s * v.z,
        v.y,
        s * v.x + c * v.z
    );
}

fn rotateX(v: vec3<f32>, r: f32) -> vec3<f32> {
    let c = cos(r);
    let s = sin(r);
    return vec3<f32>(
        v.x,
        c * v.y + s * v.z,
        -s * v.y + c * v.z
    );
}

fn hash12(p: vec2<f32>) -> f32 {
    return fract(sin(dot(p, vec2<f32>(127.1, 311.7))) * 43758.5453123);
}

fn decodeWaterWaveClassFromAo(ao: vec4<f32>) -> i32 {
    let maxAo = max(max(ao.x, ao.y), max(ao.z, ao.w));
    let waveClassStride = 8.0;
    let cls = i32(floor(maxAo / waveClassStride));
    return clamp(cls, 0, 4);
}

@vertex
fn vs_main(input: VSIn) -> VSOut {
    var out: VSOut;

    let faceType = u.intParams1.x;
    let sectionLod = u.intParams1.y;
    let waterCascadeBrightnessEnabled = u.intParams4.y;
    let foliageWindEnabled = u.intParams6.z != 0;
    let leafFanAlignEnabled = (u.intParams6.y & 16) != 0;
    let time = u.params.x;
    let cameraPos = u.cameraAndScale.xyz;
    let waterCascadeBrightnessStrength = u.diffuseAndWater.w;
    let waterCascadeBrightnessSpeed = u.wallStoneAndWater2.z;
    let waterCascadeBrightnessScale = u.wallStoneAndWater2.w;

    var pos = input.position;
    var normal = input.normal;
    var baseTex = input.texCoord;

    pos.x = pos.x * input.scale.x;
    pos.y = pos.y * input.scale.y;

    let isWallDecal = (input.alpha <= -15.5);
    let isChalkDust = (input.alpha <= -14.5) && !isWallDecal;
    let isGrassCover = (input.alpha <= -13.5) && !isChalkDust && !isWallDecal;
    let isCavePot = (input.alpha <= -9.5) && !isGrassCover && !isChalkDust && !isWallDecal;
    let isSlopeFaceRegular = (input.alpha <= -3.5) && (input.alpha > -9.5);
    let isWaterSlopeFace = (input.alpha <= -23.5) && (input.alpha > -33.5);
    let isSlopeFace = isSlopeFaceRegular || isWaterSlopeFace;
    let isSlopeCapA = ((input.alpha <= -3.5) && (input.alpha > -4.5))
        || ((input.alpha <= -23.5) && (input.alpha > -24.5));
    let isSlopeCapB = ((input.alpha <= -4.5) && (input.alpha > -5.5))
        || ((input.alpha <= -24.5) && (input.alpha > -25.5));
    let isSlopeTopPosX = ((input.alpha <= -5.5) && (input.alpha > -6.5))
        || ((input.alpha <= -25.5) && (input.alpha > -26.5));
    let isSlopeTopNegX = ((input.alpha <= -6.5) && (input.alpha > -7.5))
        || ((input.alpha <= -26.5) && (input.alpha > -27.5));
    let isSlopeTopPosZ = ((input.alpha <= -7.5) && (input.alpha > -8.5))
        || ((input.alpha <= -27.5) && (input.alpha > -28.5));
    let isSlopeTopNegZ = ((input.alpha <= -8.5) && (input.alpha > -9.5))
        || ((input.alpha <= -28.5) && (input.alpha > -29.5));
    let isSlopeTopCornerPosXPosZ = (input.alpha <= -29.5) && (input.alpha > -30.5);
    let isSlopeTopCornerPosXNegZ = (input.alpha <= -30.5) && (input.alpha > -31.5);
    let isSlopeTopCornerNegXPosZ = (input.alpha <= -31.5) && (input.alpha > -32.5);
    let isSlopeTopCornerNegXNegZ = (input.alpha <= -32.5) && (input.alpha > -33.5);
    let isSlopeTopCorner = isSlopeTopCornerPosXPosZ
        || isSlopeTopCornerPosXNegZ
        || isSlopeTopCornerNegXPosZ
        || isSlopeTopCornerNegXNegZ;
    let isSlopeTop = isSlopeTopPosX || isSlopeTopNegX || isSlopeTopPosZ || isSlopeTopNegZ || isSlopeTopCorner;

    if (isSlopeCapA || isSlopeCapB) {
        let cornerTopLeft = (baseTex.x <= 0.001) && (baseTex.y >= 0.999);
        let cornerTopRight = (baseTex.x >= 0.999) && (baseTex.y >= 0.999);
        let collapseTopLeft = isSlopeCapA && cornerTopLeft;
        let collapseTopRight = isSlopeCapB && cornerTopRight;
        if (collapseTopLeft || collapseTopRight) {
            pos.y = -0.5 * input.scale.y;
            baseTex.y = 0.0;
        }
    }

    if (faceType == 0) {
        pos = rotateY(pos, 1.57079632679);
        normal = normalize(rotateY(normal, 1.57079632679));
    } else if (faceType == 1) {
        pos = rotateY(pos, -1.57079632679);
        normal = normalize(rotateY(normal, -1.57079632679));
    } else if (faceType == 2) {
        pos = rotateX(pos, -1.57079632679);
        normal = normalize(rotateX(normal, -1.57079632679));
    } else if (faceType == 3) {
        pos = rotateX(pos, 1.57079632679);
        normal = normalize(rotateX(normal, 1.57079632679));
    } else if (faceType == 5) {
        pos = rotateY(pos, 3.14159265359);
        normal = normalize(rotateY(normal, 3.14159265359));
    }

    if (faceType == 0 || faceType == 1) {
        pos.z = -pos.z;
        normal.z = -normal.z;
    }
    if (faceType == 2 || faceType == 3) {
        pos.x = -pos.x;
        normal.x = -normal.x;
    }

    if (isSlopeTop && faceType == 2) {
        var slopeHeight = 0.5;
        if (isSlopeTopPosX) {
            slopeHeight = 1.0 - baseTex.x;
        } else if (isSlopeTopNegX) {
            slopeHeight = baseTex.x;
        } else if (isSlopeTopPosZ) {
            slopeHeight = baseTex.y;
        } else if (isSlopeTopNegZ) {
            slopeHeight = 1.0 - baseTex.y;
        } else if (isSlopeTopCornerPosXPosZ) {
            slopeHeight = 1.0 - max(baseTex.x, baseTex.y);
        } else if (isSlopeTopCornerPosXNegZ) {
            slopeHeight = 1.0 - max(baseTex.x, 1.0 - baseTex.y);
        } else if (isSlopeTopCornerNegXPosZ) {
            slopeHeight = 1.0 - max(1.0 - baseTex.x, baseTex.y);
        } else if (isSlopeTopCornerNegXNegZ) {
            slopeHeight = 1.0 - max(1.0 - baseTex.x, 1.0 - baseTex.y);
        }
        slopeHeight = clamp(slopeHeight, 0.0, 1.0);
        pos.y = (slopeHeight - 1.0) * input.scale.y;
        if (isSlopeTopPosX) {
            normal = normalize(vec3<f32>(-1.0, 1.0, 0.0));
        } else if (isSlopeTopNegX) {
            normal = normalize(vec3<f32>(1.0, 1.0, 0.0));
        } else if (isSlopeTopPosZ) {
            normal = normalize(vec3<f32>(0.0, 1.0, -1.0));
        } else if (isSlopeTopNegZ) {
            normal = normalize(vec3<f32>(0.0, 1.0, 1.0));
        } else if (isSlopeTopCornerPosXPosZ) {
            normal = normalize(vec3<f32>(-1.0, 1.0, -1.0));
        } else if (isSlopeTopCornerPosXNegZ) {
            normal = normalize(vec3<f32>(-1.0, 1.0, 1.0));
        } else if (isSlopeTopCornerNegXPosZ) {
            normal = normalize(vec3<f32>(1.0, 1.0, -1.0));
        } else if (isSlopeTopCornerNegXNegZ) {
            normal = normalize(vec3<f32>(1.0, 1.0, 1.0));
        }
    }

    let isFlower = (input.alpha > -3.5) && (input.alpha <= -2.5);
    let isGrass = (input.alpha > -2.5) && (input.alpha <= -1.5);
    let isTranslucentFace = (input.alpha > 0.0) && (input.alpha < 0.999);
    let noGrassVariantEncodeStride = 16384;
    let chalkTileEncodeStride = 2048;
    var decodedTileIndex = input.tileIndex;
    if (decodedTileIndex >= noGrassVariantEncodeStride) {
        decodedTileIndex = decodedTileIndex - noGrassVariantEncodeStride;
    }
    if (decodedTileIndex >= chalkTileEncodeStride) {
        decodedTileIndex = decodedTileIndex % chalkTileEncodeStride;
    }
    let isLeafFanGrass = isGrass && ((decodedTileIndex == 328) || (decodedTileIndex == 337));
    let waveClassFromAo = decodeWaterWaveClassFromAo(input.ao);
    let isWaterSurfaceFace = (isTranslucentFace && (waveClassFromAo > 0) && (faceType == 2))
        || (isWaterSlopeFace && isSlopeTop && (faceType == 2));
    let isVerticalFace = (faceType == 0) || (faceType == 1) || (faceType == 4) || (faceType == 5);
    let isWaterVerticalSideFace = isVerticalFace
        && ((isTranslucentFace && (waveClassFromAo > 0)) || isWaterSlopeFace);
    let isSmallLilypadFace = (decodedTileIndex == 36);
    let isBigLilypadFace = (decodedTileIndex >= 428) && (decodedTileIndex <= 431);
    let isLilypadFace = (isSmallLilypadFace || isBigLilypadFace) && (waveClassFromAo > 0);
    let isPlant = isGrass || isFlower || isCavePot;
    if (isPlant) {
        var plantAngle = 0.78539816339;
        let cell = floor(input.offset.xz + vec2<f32>(0.5));
        if (isGrass) {
            if (!(isLeafFanGrass && leafFanAlignEnabled)) {
                let randYaw = hash12(cell + vec2<f32>(37.2, 91.7));
                plantAngle = plantAngle + randYaw * 6.28318530718;
            }
        } else if (isFlower) {
            let randQuarter = floor(hash12(cell + vec2<f32>(17.4, 83.2)) * 4.0);
            plantAngle = plantAngle + randQuarter * 1.57079632679;
        }
        pos = rotateY(pos, plantAngle);
        normal = vec3<f32>(0.0, 1.0, 0.0);
    }

    var finalPos = pos + input.offset;
    out.instanceCell = input.offset;
    if ((isWaterSurfaceFace || isWaterSlopeFace || isWaterVerticalSideFace) && sectionLod == 0) {
        let waveClass = waveClassFromAo;
        var ampScale = 2.10;
        var wavelength = 36.0;
        var steepness = 0.46;
        var speedScale = 1.46;
        var dirA = normalize(vec2<f32>(0.86, 0.51));
        var dirB = normalize(vec2<f32>(-0.34, 0.94));

        if (waveClass == 1) {
            // Ponds: new minimum is previous "huge ocean" profile.
            ampScale = 2.10;
            wavelength = 36.0;
            steepness = 0.46;
            speedScale = 1.46;
            dirA = normalize(vec2<f32>(0.74, 0.67));
            dirB = normalize(vec2<f32>(-0.62, 0.78));
        } else if (waveClass == 2) {
            // Lakes: larger than ponds.
            ampScale = 2.90;
            wavelength = 44.0;
            steepness = 0.55;
            speedScale = 1.70;
            dirA = normalize(vec2<f32>(0.88, 0.47));
            dirB = normalize(vec2<f32>(-0.29, 0.96));
        } else if (waveClass == 3) {
            // Rivers: bigger and directional.
            ampScale = 3.80;
            wavelength = 52.0;
            steepness = 0.64;
            speedScale = 1.95;
            dirA = normalize(vec2<f32>(0.99, 0.14));
            dirB = normalize(vec2<f32>(0.93, 0.37));
        } else if (waveClass == 4) {
            // Ocean: largest and longest.
            ampScale = 5.00;
            wavelength = 64.0;
            steepness = 0.74;
            speedScale = 2.20;
            dirA = normalize(vec2<f32>(0.95, 0.31));
            dirB = normalize(vec2<f32>(-0.42, 0.91));
        }

        let globalStrength = clamp(waterCascadeBrightnessStrength, 0.0, 1.0);
        // Water cascade scale was tuned for a different shading effect; remap it for geometry waves
        // so default registry values still yield visible motion.
        let globalScale = clamp(waterCascadeBrightnessScale * 5.0, 1.0, 8.0);
        let waveSpeed = max(0.25, waterCascadeBrightnessSpeed) * speedScale;

        // Aggressive baseline so class differences are obvious at default settings.
        let ampBase = mix(0.035, 0.110, globalStrength) * ampScale;
        let ampA = ampBase * 0.65;
        let ampB = ampBase * 0.35;
        let lenA = max(6.0, wavelength / globalScale);
        let lenB = max(5.0, (wavelength * 0.62) / globalScale);
        let kA = 6.28318530718 / lenA;
        let kB = 6.28318530718 / lenB;
        let qA = clamp(steepness, 0.0, 0.85);
        let qB = clamp(steepness * 0.75, 0.0, 0.85);

        let phaseA = dot(dirA, finalPos.xz) * kA + time * waveSpeed;
        let phaseB = dot(dirB, finalPos.xz) * kB - time * waveSpeed * 0.83;
        let sA = sin(phaseA);
        let sB = sin(phaseB);
        let cA = cos(phaseA);
        let cB = cos(phaseB);

        let dispX = dirA.x * (qA * ampA) * cA + dirB.x * (qB * ampB) * cB;
        let dispZ = dirA.y * (qA * ampA) * cA + dirB.y * (qB * ampB) * cB;
        let dispY = ampA * sA + ampB * sB;
        if (isWaterSurfaceFace) {
            finalPos = finalPos + vec3<f32>(dispX, dispY, dispZ);
            let dYdx = ampA * kA * dirA.x * cA + ampB * kB * dirB.x * cB;
            let dYdz = ampA * kA * dirA.y * cA + ampB * kB * dirB.y * cB;
            normal = normalize(vec3<f32>(-dYdx, 1.0, -dYdz));
        } else if (isWaterVerticalSideFace) {
            // Waterfall side faces: side-to-side sway with a downward marquee delay by height.
            // Using +y and +time in phase makes the band centers drift downward as time increases.
            let marqueeSpeed = waveSpeed * 1.32;
            let sidePhaseA = dot(dirA, finalPos.xz) * (kA * 0.42) + finalPos.y * (kA * 1.08) + time * marqueeSpeed;
            let sidePhaseB = dot(dirB, finalPos.xz) * (kB * 0.37) + finalPos.y * (kB * 0.81) + time * marqueeSpeed * 0.73;
            let sideDisp = ((ampA * 0.52) * sin(sidePhaseA) + (ampB * 0.48) * sin(sidePhaseB)) * 0.72;
            let sideAxis = select(
                vec2<f32>(0.0, 1.0),
                vec2<f32>(1.0, 0.0),
                (faceType == 4) || (faceType == 5)
            );
            finalPos.x = finalPos.x + sideAxis.x * sideDisp;
            finalPos.z = finalPos.z + sideAxis.y * sideDisp;
        } else if (isWaterSlopeFace) {
            finalPos = finalPos + vec3<f32>(dispX, dispY, dispZ);
        }
    }
    if (isLilypadFace && sectionLod == 0) {
        let waveClass = waveClassFromAo;
        var ampScale = 2.10;
        var wavelength = 36.0;
        var steepness = 0.46;
        var speedScale = 1.46;
        var dirA = normalize(vec2<f32>(0.86, 0.51));
        var dirB = normalize(vec2<f32>(-0.34, 0.94));

        if (waveClass == 1) {
            ampScale = 2.10;
            wavelength = 36.0;
            steepness = 0.46;
            speedScale = 1.46;
            dirA = normalize(vec2<f32>(0.74, 0.67));
            dirB = normalize(vec2<f32>(-0.62, 0.78));
        } else if (waveClass == 2) {
            ampScale = 2.90;
            wavelength = 44.0;
            steepness = 0.55;
            speedScale = 1.70;
            dirA = normalize(vec2<f32>(0.88, 0.47));
            dirB = normalize(vec2<f32>(-0.29, 0.96));
        } else if (waveClass == 3) {
            ampScale = 3.80;
            wavelength = 52.0;
            steepness = 0.64;
            speedScale = 1.95;
            dirA = normalize(vec2<f32>(0.99, 0.14));
            dirB = normalize(vec2<f32>(0.93, 0.37));
        } else if (waveClass == 4) {
            ampScale = 5.00;
            wavelength = 64.0;
            steepness = 0.74;
            speedScale = 2.20;
            dirA = normalize(vec2<f32>(0.95, 0.31));
            dirB = normalize(vec2<f32>(-0.42, 0.91));
        }

        let globalStrength = clamp(waterCascadeBrightnessStrength, 0.0, 1.0);
        let globalScale = clamp(waterCascadeBrightnessScale * 5.0, 1.0, 8.0);
        let waveSpeed = max(0.25, waterCascadeBrightnessSpeed) * speedScale;
        let ampBase = mix(0.035, 0.110, globalStrength) * ampScale;
        let ampA = ampBase * 0.65;
        let ampB = ampBase * 0.35;
        let lenA = max(6.0, wavelength / globalScale);
        let lenB = max(5.0, (wavelength * 0.62) / globalScale);
        let kA = 6.28318530718 / lenA;
        let kB = 6.28318530718 / lenB;

        let phaseA = dot(dirA, finalPos.xz) * kA + time * waveSpeed;
        let phaseB = dot(dirB, finalPos.xz) * kB - time * waveSpeed * 0.83;
        let sA = sin(phaseA);
        let sB = sin(phaseB);
        finalPos.y = finalPos.y + ampA * sA + ampB * sB;
    }
    if (isPlant) {
        let cell = floor(input.offset.xz + vec2<f32>(0.5));
        if (!isCavePot) {
            let jitter = vec2<f32>(
                hash12(cell + vec2<f32>(13.7, 5.9)),
                hash12(cell + vec2<f32>(2.3, 19.1))
            ) - vec2<f32>(0.5);
            finalPos.x = finalPos.x + jitter.x * 0.24;
            finalPos.z = finalPos.z + jitter.y * 0.24;
        }

        if (foliageWindEnabled && isGrass && sectionLod == 0) {
            let heightMask = clamp(baseTex.y, 0.0, 1.0);
            let bendMask = heightMask * heightMask;
            let edgeMask = clamp(abs(baseTex.x - 0.5) * 2.0, 0.0, 1.0);
            let profileMask = mix(0.45, 1.0, edgeMask);
            let shortScale = select(1.0, 0.66, input.alpha <= -2.2);

            let hA = hash12(cell + vec2<f32>(41.3, 17.9));
            let hB = hash12(cell + vec2<f32>(73.1, 29.4));
            let hC = hash12(cell + vec2<f32>(11.6, 53.8));

            let windSpeed = mix(0.95, 1.55, hA);
            let baseAmp = mix(0.030, 0.085, hB) * shortScale;
            let gust = 0.5 + 0.5 * sin(
                time * 0.47 + hC * 6.28318530718 + dot(cell, vec2<f32>(0.09, 0.13))
            );
            let phase = time * windSpeed
                + dot(cell, vec2<f32>(0.21, 0.16))
                + baseTex.y * 2.2
                + (baseTex.x - 0.5) * 1.8;

            let windDir = normalize(
                vec2<f32>(cos(hA * 6.28318530718), sin(hA * 6.28318530718))
                    + vec2<f32>(0.6, 0.25)
            );
            let lateral = sin(phase) * baseAmp * mix(0.6, 1.25, gust) * bendMask * profileMask;
            let flutter = sin(
                time * (2.0 + hB * 1.5) + dot(cell, vec2<f32>(0.33, 0.27)) + baseTex.x * 7.0
            ) * 0.018 * bendMask * edgeMask * shortScale;

            let perp = vec2<f32>(-windDir.y, windDir.x);
            finalPos.x = finalPos.x + windDir.x * lateral + perp.x * flutter;
            finalPos.z = finalPos.z + windDir.y * lateral + perp.y * flutter;
            finalPos.y = finalPos.y - abs(lateral) * 0.17 * bendMask;
        }
    }

    if (foliageWindEnabled && input.alpha < 0.0) {
        if (!isPlant && !isSlopeFace && !isGrassCover && !isChalkDust && !isWallDecal && sectionLod == 0) {
            let swayAmount = 0.05;
            let swaySpeed = 0.30;
            let phaseA = input.offset.x * 0.13 + input.offset.z * 0.17;
            let phaseB = input.offset.z * 0.19 - input.offset.x * 0.11;
            finalPos.x = finalPos.x + sin(time * swaySpeed + phaseA) * swayAmount;
            finalPos.z = finalPos.z + cos(time * (swaySpeed * 0.83) + phaseB) * swayAmount * 0.72;
            finalPos.y = finalPos.y + cos((input.offset.y + time) * 0.3) * 0.05;
        }
    }

    let worldPos4 = u.model * vec4<f32>(finalPos, 1.0);
    out.worldPos = worldPos4.xyz;
    out.position = u.projection * u.view * worldPos4;

    var finalTexCoord = baseTex * input.uvScale;
    let isLanternTile = (input.tileIndex == 292)
        || (input.tileIndex == 293)
        || (input.tileIndex == 294)
        || (input.tileIndex == 295)
        || (input.tileIndex == 304);
    let centerCutLogTile = (input.tileIndex == 19)
        && (input.uvScale.x > 0.0) && (input.uvScale.y > 0.0)
        && (input.uvScale.x <= 1.0) && (input.uvScale.y <= 1.0)
        && ((input.uvScale.x < 0.9999) || (input.uvScale.y < 0.9999));
    let centerCutLanternTile = isLanternTile
        && (input.uvScale.x > 0.0) && (input.uvScale.y > 0.0)
        && (input.uvScale.x <= 1.0) && (input.uvScale.y <= 1.0)
        && ((input.uvScale.x < 0.9999) || (input.uvScale.y < 0.9999));
    if (centerCutLogTile || centerCutLanternTile) {
        finalTexCoord = finalTexCoord + 0.5 * (vec2<f32>(1.0) - input.uvScale);
    }

    out.fragColor = input.color;
    out.texCoord = finalTexCoord;
    out.instanceDistance = length(input.offset - cameraPos);
    out.normal = normalize((u.model * vec4<f32>(normal, 0.0)).xyz);
    out.tileIndex = input.tileIndex;
    out.alpha = input.alpha;

    if (baseTex.x < 0.5) {
        out.ao = select(input.ao.w, input.ao.x, baseTex.y < 0.5);
    } else {
        out.ao = select(input.ao.z, input.ao.y, baseTex.y < 0.5);
    }
    return out;
}
