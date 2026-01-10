#version 330 core
in vec2 vUV;
uniform float fillAmount;
uniform bool ready;
uniform int buildModeType;
uniform vec3 previewColor;
uniform int channelIndex;
uniform int previewTileIndex;
uniform int atlasEnabled;
uniform sampler2D atlasTexture;
uniform vec2 atlasTileSize;
uniform vec2 atlasTextureSize;
uniform int tilesPerRow;
uniform int tilesPerCol;
out vec4 FragColor;
const vec3 channelColors[3] = vec3[](vec3(1.0, 0.3, 0.3), vec3(0.3, 1.0, 0.3), vec3(0.3, 0.6, 1.0));
void main(){
    float y = clamp(vUV.y, 0.0, 1.0);
    float bend = sin(y * 3.14159) * -0.15;
    float x = (vUV.x - 0.5 + bend * 0.4) * 2.0;
    float outerHalf = mix(0.35, 0.6, y);
    float innerHalf = outerHalf - 0.18;
    float outerMask = smoothstep(outerHalf, outerHalf - 0.015, abs(x));
    float innerMask = smoothstep(innerHalf - 0.02, innerHalf + 0.005, abs(x));
    float rim = outerMask * (1.0 - innerMask);
    bool isBuildMode = (buildModeType == 1) || (buildModeType == 2);
    bool isTextureMode = buildModeType == 2;
    bool isDestroyMode = buildModeType == 3;
    float targetFill = isBuildMode ? 1.0 : clamp(fillAmount, 0.0, 1.0);
    float fillMask = step(y, targetFill);
    rim *= fillMask;
    if(rim < 0.02) discard;
    vec3 color;
    if (isBuildMode) {
        if (isTextureMode) {
            bool useAtlas = (atlasEnabled == 1) && previewTileIndex >= 0 && tilesPerRow > 0 && tilesPerCol > 0 &&
                            atlasTextureSize.x > 0.0 && atlasTextureSize.y > 0.0;
            if (useAtlas) {
                vec2 tileSizeUV = atlasTileSize / atlasTextureSize;
                int tileX = previewTileIndex % tilesPerRow;
                int tileY = tilesPerCol - 1 - (previewTileIndex / tilesPerRow);
                vec2 base = vec2(tileX, tileY) * tileSizeUV;
                vec2 uv = base + vUV * tileSizeUV;
                color = texture(atlasTexture, uv).rgb;
            } else {
                color = previewColor;
            }
        } else {
            int idx = clamp(channelIndex, 0, 2);
            vec3 bottomColor = channelColors[idx];
            color = mix(bottomColor, previewColor, y);
        }
    } else {
        vec3 fillStart = isDestroyMode ? vec3(0.0, 1.0, 1.0) : vec3(0.1, 0.8, 0.2);
        vec3 fillEnd = isDestroyMode ? vec3(1.0, 0.0, 1.0) : vec3(1.0, 0.5, 0.0);
        color = mix(fillStart, fillEnd, y);
    }
    float alpha = 0.85 * rim * (ready ? 1.02 : 1.0);
    FragColor = vec4(color, alpha);
}
