#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec3 aOffset;
layout (location = 4) in vec3 aColor;
layout (location = 5) in int aTileIndex;
layout (location = 6) in float aAlpha;
layout (location = 7) in vec4 aAO;
layout (location = 8) in vec2 aScale;
layout (location = 9) in vec2 aUVScale;

out vec2 TexCoord;
out vec3 FragColor_in;
out float instanceDistance;
out vec3 Normal;
out vec3 WorldPos;
flat out int TileIndex;
out float Alpha;
out float AO;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform int faceType;
uniform int sectionLod;
uniform vec3 cameraPos;
uniform float time;

mat3 rotY(float r) {
    float c = cos(r);
    float s = sin(r);
    return mat3(
        c, 0, s,
        0, 1, 0,
       -s, 0, c
    );
}

mat3 rotX(float r) {
    float c = cos(r);
    float s = sin(r);
    return mat3(
        1, 0, 0,
        0, c, -s,
        0, s, c
    );
}

float hash12(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main() {
    vec3 pos = aPos;
    vec3 normal = aNormal;
    mat3 r = mat3(1.0);
    vec2 baseTex = aTexCoord;

    pos.xy *= aScale;

    bool isSlopeFace = (aAlpha <= -3.5);
    bool isSlopeCapA = (aAlpha <= -3.5) && (aAlpha > -4.5);
    bool isSlopeCapB = (aAlpha <= -4.5) && (aAlpha > -5.5);
    bool isSlopeTopPosX = (aAlpha <= -5.5) && (aAlpha > -6.5);
    bool isSlopeTopNegX = (aAlpha <= -6.5) && (aAlpha > -7.5);
    bool isSlopeTopPosZ = (aAlpha <= -7.5) && (aAlpha > -8.5);
    bool isSlopeTopNegZ = (aAlpha <= -8.5) && (aAlpha > -9.5);
    bool isSlopeTop = isSlopeTopPosX || isSlopeTopNegX || isSlopeTopPosZ || isSlopeTopNegZ;

    if (isSlopeCapA || isSlopeCapB) {
        bool cornerTopLeft = (baseTex.x <= 0.001) && (baseTex.y >= 0.999);
        bool cornerTopRight = (baseTex.x >= 0.999) && (baseTex.y >= 0.999);
        bool collapseTopLeft = isSlopeCapA && cornerTopLeft;
        bool collapseTopRight = isSlopeCapB && cornerTopRight;
        if (collapseTopLeft || collapseTopRight) {
            pos.y = -0.5 * aScale.y;
            baseTex.y = 0.0;
        }
    }

    if (faceType == 0) { // +X
        r = rotY(1.57079632679);  // rotate +90deg around Y to face +X
    } else if (faceType == 1) { // -X
        r = rotY(-1.57079632679); // rotate -90deg around Y to face -X
    } else if (faceType == 2) { // +Y
        r = rotX(-1.57079632679);
    } else if (faceType == 3) { // -Y
        r = rotX(1.57079632679);
    } else if (faceType == 4) { // +Z
        r = mat3(1.0);
    } else if (faceType == 5) { // -Z
        r = rotY(3.14159265359);
    }

    pos = r * pos;
    normal = normalize(r * normal);

    // Fix winding per axis: mirror Z for X faces; mirror X for Y faces.
    if (faceType == 0 || faceType == 1) {
        pos.z *= -1.0;
        normal.z *= -1.0;
    }
    if (faceType == 2 || faceType == 3) {
        pos.x *= -1.0;
        normal.x *= -1.0;
    }

    if (isSlopeTop && faceType == 2) {
        float slopeHeight = 0.5;
        if (isSlopeTopPosX) slopeHeight = 1.0 - baseTex.x;
        else if (isSlopeTopNegX) slopeHeight = baseTex.x;
        else if (isSlopeTopPosZ) slopeHeight = 1.0 - baseTex.y;
        else if (isSlopeTopNegZ) slopeHeight = baseTex.y;
        slopeHeight = clamp(slopeHeight, 0.0, 1.0);
        // Top faces are emitted from the +Y face center (+0.5 block), so keep
        // the slope inside its block by shifting the local top-face Y down.
        pos.y = (slopeHeight - 1.0) * aScale.y;
        if (isSlopeTopPosX) normal = normalize(vec3(-1.0, 1.0, 0.0));
        else if (isSlopeTopNegX) normal = normalize(vec3(1.0, 1.0, 0.0));
        else if (isSlopeTopPosZ) normal = normalize(vec3(0.0, 1.0, -1.0));
        else if (isSlopeTopNegZ) normal = normalize(vec3(0.0, 1.0, 1.0));
    }

    bool isFlower = (aAlpha > -3.5) && (aAlpha <= -2.5);
    bool isGrass = (aAlpha > -2.5) && (aAlpha <= -1.5);
    bool isPlant = isGrass || isFlower;
    if (isPlant) {
        // Crossed foliage should be diagonal (X) rather than axis-aligned (+).
        float plantAngle = 0.78539816339; // 45 degrees base.
        if (isGrass) {
            // Deterministic per-cell grass yaw variation.
            vec2 cell = floor(aOffset.xz + vec2(0.5));
            float randYaw = hash12(cell + vec2(37.2, 91.7));
            plantAngle += randYaw * 6.28318530718;
        }
        mat3 plantRot = rotY(plantAngle);
        pos = plantRot * pos;
        // Option 1: force plant lighting to use an upward normal.
        normal = vec3(0.0, 1.0, 0.0);
    }

    vec3 finalPos = pos + aOffset;
    if (isPlant) {
        // Visual-only per-cell variation; collision/hitbox stays aligned to voxel.
        vec2 cell = floor(aOffset.xz + vec2(0.5));
        vec2 jitter = vec2(hash12(cell + vec2(13.7, 5.9)), hash12(cell + vec2(2.3, 19.1))) - vec2(0.5);
        finalPos.xz += jitter * 0.24;

        if (isGrass) {
            // Per-vertex wind bend (root anchored, tip bends most) instead of rigid whole-tuft motion.
            float heightMask = clamp(baseTex.y, 0.0, 1.0);
            float bendMask = heightMask * heightMask;
            float edgeMask = clamp(abs(baseTex.x - 0.5) * 2.0, 0.0, 1.0);
            float profileMask = mix(0.45, 1.0, edgeMask);
            float shortScale = (aAlpha <= -2.2) ? 0.66 : 1.0; // short grass bends a bit less.

            float hA = hash12(cell + vec2(41.3, 17.9));
            float hB = hash12(cell + vec2(73.1, 29.4));
            float hC = hash12(cell + vec2(11.6, 53.8));

            float windSpeed = mix(0.95, 1.55, hA);
            float baseAmp = mix(0.030, 0.085, hB) * shortScale;
            float gust = 0.5 + 0.5 * sin(time * 0.47 + hC * 6.28318530718 + dot(cell, vec2(0.09, 0.13)));
            float phase = time * windSpeed
                        + dot(cell, vec2(0.21, 0.16))
                        + baseTex.y * 2.2
                        + (baseTex.x - 0.5) * 1.8;

            vec2 windDir = normalize(vec2(cos(hA * 6.28318530718), sin(hA * 6.28318530718)) + vec2(0.6, 0.25));
            float lateral = sin(phase) * baseAmp * mix(0.6, 1.25, gust) * bendMask * profileMask;
            float flutter = sin(time * (2.0 + hB * 1.5) + dot(cell, vec2(0.33, 0.27)) + baseTex.x * 7.0)
                          * 0.018 * bendMask * edgeMask * shortScale;

            vec2 perp = vec2(-windDir.y, windDir.x);
            finalPos.xz += windDir * lateral + perp * flutter;
            finalPos.y -= abs(lateral) * 0.17 * bendMask;
        }
    }
    if (aAlpha < 0.0) {
        // Keep grass/flowers still; only leaves retain subtle motion.
        if (!isPlant && !isSlopeFace) {
            float swayAmount = 0.05;
            float swaySpeed = 0.30;
            float phaseA = aOffset.x * 0.13 + aOffset.z * 0.17;
            float phaseB = aOffset.z * 0.19 - aOffset.x * 0.11;
            finalPos.x += sin(time * swaySpeed + phaseA) * swayAmount;
            finalPos.z += cos(time * (swaySpeed * 0.83) + phaseB) * swayAmount * 0.72;
            finalPos.y += cos((aOffset.y + time) * 0.3) * 0.05;
        }
    }
    vec4 worldPos4 = model * vec4(finalPos, 1.0);
    WorldPos = worldPos4.xyz;
    gl_Position = projection * view * worldPos4;

    vec2 finalTexCoord = baseTex * aUVScale;
    // Center cropped cut-log texture on narrow logs so the ring stays centered.
    bool centerCutLogTile = (aTileIndex == 19)
        && (aUVScale.x > 0.0) && (aUVScale.y > 0.0)
        && (aUVScale.x <= 1.0) && (aUVScale.y <= 1.0)
        && ((aUVScale.x < 0.9999) || (aUVScale.y < 0.9999));
    if (centerCutLogTile) {
        finalTexCoord += 0.5 * (vec2(1.0) - aUVScale);
    }

    FragColor_in = aColor;
    TexCoord = finalTexCoord;
    instanceDistance = length(aOffset - cameraPos);
    Normal = normalize(mat3(model) * normal);
    TileIndex = aTileIndex;
    Alpha = aAlpha;
    if (baseTex.x < 0.5) {
        AO = (baseTex.y < 0.5) ? aAO.x : aAO.w;
    } else {
        AO = (baseTex.y < 0.5) ? aAO.y : aAO.z;
    }
}
