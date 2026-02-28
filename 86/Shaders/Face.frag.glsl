#version 330 core
in vec2 TexCoord;
in vec3 FragColor_in;
in float instanceDistance;
in vec3 Normal;
in vec3 WorldPos;
flat in int TileIndex;
in float Alpha;
in float AO;
out vec4 FragColor;

uniform vec3 lightDir;
uniform vec3 ambientLight;
uniform vec3 diffuseLight;
uniform int faceType;
uniform int atlasEnabled;
uniform sampler2D atlasTexture;
uniform vec2 atlasTileSize;
uniform vec2 atlasTextureSize;
uniform int tilesPerRow;
uniform int tilesPerCol;
uniform int grassTextureEnabled;
uniform sampler2D grassTexture0;
uniform sampler2D grassTexture1;
uniform sampler2D grassTexture2;
uniform int shortGrassTextureEnabled;
uniform sampler2D shortGrassTexture0;
uniform sampler2D shortGrassTexture1;
uniform sampler2D shortGrassTexture2;
uniform int oreTextureEnabled;
uniform sampler2D oreTexture0;
uniform sampler2D oreTexture1;
uniform sampler2D oreTexture2;
uniform sampler2D oreTexture3;
uniform int terrainTextureEnabled;
uniform sampler2D terrainTextureDirt;
uniform sampler2D terrainTextureStone;
uniform int waterOverlayTextureEnabled;
uniform sampler2D waterOverlayTexture;
uniform int wireframeDebug;
uniform int sectionLod;
uniform int leafOpaqueOutsideLod0;
uniform int waterCascadeBrightnessEnabled;
uniform float waterCascadeBrightnessStrength;
uniform float waterCascadeBrightnessSpeed;
uniform float waterCascadeBrightnessScale;
uniform int wallStoneUvJitterEnabled;
uniform int wallStoneUvJitterTile0;
uniform int wallStoneUvJitterTile1;
uniform int wallStoneUvJitterTile2;
uniform float wallStoneUvJitterMinPixels;
uniform float wallStoneUvJitterMaxPixels;
uniform float time;

float noise(vec2 p){ return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

vec2 rotateUvQuarterTurns(vec2 uv, int turns) {
    int t = turns & 3;
    if (t == 1) return vec2(1.0 - uv.y, uv.x);
    if (t == 2) return vec2(1.0 - uv.x, 1.0 - uv.y);
    if (t == 3) return vec2(uv.y, 1.0 - uv.x);
    return uv;
}

void main() {
    if (wireframeDebug == 1) {
        vec3 lineColor = (TileIndex >= 0) ? vec3(0.5) : FragColor_in;
        FragColor = vec4(lineColor, 1.0);
        return;
    }

    vec3 bc = FragColor_in;
    bool isSlopeFace = (Alpha <= -3.5);
    bool isSlopeCapA = (Alpha <= -3.5) && (Alpha > -4.5);
    bool isSlopeCapB = (Alpha <= -4.5) && (Alpha > -5.5);
    float outAlpha = isSlopeFace ? 1.0 : max(Alpha, 0.0);
    bool isFlower = (Alpha > -3.5) && (Alpha <= -2.5);
    bool isShortGrass = (Alpha > -2.5) && (Alpha <= -2.15);
    bool isTallGrass = (Alpha > -2.15) && (Alpha <= -1.5);
    bool isGrass = isTallGrass || isShortGrass;
    bool isLeaf = (Alpha > -1.5) && (Alpha < 0.0);
    bool isTranslucentFace = (Alpha > 0.0) && (Alpha < 0.999);
    bool useAtlas = (atlasEnabled == 1) && TileIndex >= 0 && tilesPerRow > 0 && tilesPerCol > 0 && atlasTextureSize.x > 0.0 && atlasTextureSize.y > 0.0;

    if (useAtlas) {
        vec2 tileSizeUV = atlasTileSize / atlasTextureSize;
        int tileX = TileIndex % tilesPerRow;
        int tileY = tilesPerCol - 1 - (TileIndex / tilesPerRow);
        vec2 base = vec2(tileX, tileY) * tileSizeUV;
        vec2 localUv = fract(TexCoord);
        bool isWallStoneTile = TileIndex == wallStoneUvJitterTile0
            || TileIndex == wallStoneUvJitterTile1
            || TileIndex == wallStoneUvJitterTile2;
        if (wallStoneUvJitterEnabled == 1 && isWallStoneTile) {
            ivec3 cell = ivec3(floor(WorldPos + vec3(0.5)));
            float minPx = max(0.0, wallStoneUvJitterMinPixels);
            float maxPx = max(minPx, wallStoneUvJitterMaxPixels);
            float span = max(1.0, maxPx - minPx + 1.0);
            float rx = noise(vec2(
                float(cell.x) * 0.173 + float(cell.y) * 0.731 + float(cell.z) * 0.197,
                float(cell.z) * 0.293 + 17.123
            ));
            float ry = noise(vec2(
                float(cell.x) * 0.619 + float(cell.y) * 0.271 + float(cell.z) * 0.887,
                float(cell.x) * 0.411 + 53.701
            ));
            float offsetXPx = floor(rx * span) + minPx;
            float offsetYPx = floor(ry * span) + minPx;
            vec2 localPx = localUv * atlasTileSize;
            localPx = mod(localPx + vec2(offsetXPx, offsetYPx), atlasTileSize);
            localUv = localPx / atlasTileSize;
        }
        vec2 uv = base + localUv * tileSizeUV;
        vec4 texel = texture(atlasTexture, uv);
        bc = texel.rgb;
        outAlpha *= texel.a;
    }

    bool useOreTexture = (TileIndex <= -10 && TileIndex >= -13) && (oreTextureEnabled == 1);
    if (useOreTexture) {
        int oreVariant = -10 - TileIndex;
        vec2 uv = fract(TexCoord);
        vec4 texel = texture(oreTexture0, uv);
        if (oreVariant == 1) texel = texture(oreTexture1, uv);
        else if (oreVariant == 2) texel = texture(oreTexture2, uv);
        else if (oreVariant == 3) texel = texture(oreTexture3, uv);
        bc = texel.rgb;
        outAlpha *= texel.a;
    }

    bool useTerrainTexture = (TileIndex <= -20 && TileIndex >= -21) && (terrainTextureEnabled == 1);
    if (useTerrainTexture) {
        int terrainVariant = -20 - TileIndex;
        vec2 uv = fract(TexCoord);
        vec4 texel = texture(terrainTextureDirt, uv);
        if (terrainVariant == 1) texel = texture(terrainTextureStone, uv);
        bc = texel.rgb;
        outAlpha *= texel.a;
    }

    if (isSlopeCapA || isSlopeCapB) {
        vec2 localUv = fract(TexCoord);
        bool keepFragment = isSlopeCapA
            ? (localUv.y <= (localUv.x + 0.001))
            : (localUv.y <= (1.0 - localUv.x + 0.001));
        if (!keepFragment) discard;
    }

    if (!isLeaf && !isGrass && !isFlower && !isTranslucentFace) {
        float grid = 24.0;
        float line = 0.03;
        vec2 f = fract(TexCoord * grid);
        if (f.x < line || f.y < line) {
            bc = vec3(0.0);
        } else if (!(useAtlas || useOreTexture || useTerrainTexture)) {
            float d = instanceDistance / 100.0;
            bc = FragColor_in + vec3(0.03 * d);
            bc = clamp(bc, 0.0, 1.0);
        }
    }

    if (isGrass) {
        vec2 uv = fract(TexCoord);
        vec3 grassColor = bc;
        float bladeMask = 1.0;

        bool hasTallTextures = (grassTextureEnabled == 1);
        bool hasShortTextures = (shortGrassTextureEnabled == 1);
        bool useShortTextureSet = isShortGrass && hasShortTextures;
        bool useTallTextureSet = hasTallTextures && !useShortTextureSet;

        if (useTallTextureSet || useShortTextureSet) {
            ivec3 plantCell = ivec3(floor(WorldPos + vec3(0.5)));
            float picker = noise(vec2(
                float(plantCell.x) * 0.173 + float(plantCell.y) * 0.731,
                float(plantCell.z) * 0.293 + float(plantCell.y) * 0.121
            ));
            int variant = int(floor(picker * 3.0));
            variant = clamp(variant, 0, 2);
            vec4 texel = vec4(0.0);
            if (useShortTextureSet) {
                texel = texture(shortGrassTexture0, uv);
                if (variant == 1) texel = texture(shortGrassTexture1, uv);
                else if (variant == 2) texel = texture(shortGrassTexture2, uv);
            } else {
                texel = texture(grassTexture0, uv);
                if (variant == 1) texel = texture(grassTexture1, uv);
                else if (variant == 2) texel = texture(grassTexture2, uv);
            }
            grassColor = texel.rgb;
            bladeMask = texel.a;
            if (bladeMask <= 0.001) discard;
        } else {
            float x = uv.x - 0.5;
            float y = clamp(uv.y, 0.0, 1.0);
            float bladeHalf = mix(0.44, 0.07, y);
            bladeMask = 1.0 - smoothstep(bladeHalf, bladeHalf + 0.02, abs(x));
            float notch = noise(floor(vec2(
                (uv.x + floor(WorldPos.x) * 0.17) * 32.0,
                (uv.y + floor(WorldPos.z) * 0.21) * 32.0
            )));
            if (y > 0.15 && notch > 0.90) bladeMask = 0.0;
            if (bladeMask <= 0.001) discard;

            float centerVein = 1.0 - smoothstep(0.0, 0.055, abs(x));
            grassColor = mix(bc * 0.62, bc * 1.12, y);
            grassColor = mix(grassColor, vec3(0.06, 0.12, 0.05), centerVein * 0.25);
        }

        float grassGrid = 24.0;
        float grassLine = 0.03;
        vec2 gf = fract(uv * grassGrid);
        bool grassWire = (gf.x < grassLine || gf.y < grassLine);
        if (grassWire && bladeMask > 0.05) {
            grassColor = vec3(0.0);
        }

        vec3 norm = normalize(Normal);
        float diff = max(dot(norm, normalize(lightDir)), 0.0);
        vec3 lighting = ambientLight + diffuseLight * diff;
        FragColor = vec4(grassColor * lighting * AO, bladeMask);
        return;
    }

    if (isFlower) {
        vec2 uvFull = fract(TexCoord);
        const float flowerPixelRes = 24.0;
        // Downsample procedural flower output to a fixed 24x24 pixel grid.
        vec2 uv = (floor(uvFull * flowerPixelRes) + vec2(0.5)) / flowerPixelRes;
        float x = uv.x - 0.5;
        float y = clamp(uv.y, 0.0, 1.0);

        float stem = (y < 0.74) ? (1.0 - smoothstep(0.045, 0.07, abs(x))) : 0.0;
        vec2 p = vec2(x, y - 0.79);
        float ang = atan(p.y, p.x);
        float rad = length(p);
        float phase = noise(floor(WorldPos.xz * 0.75)) * 6.2831853;
        float petalRadius = 0.17 + 0.05 * cos(5.0 * ang + phase);
        float blossom = 1.0 - smoothstep(petalRadius, petalRadius + 0.03, rad);
        float center = 1.0 - smoothstep(0.05, 0.075, rad);
        float alphaShape = max(stem, blossom);
        alphaShape = (alphaShape > 0.5) ? 1.0 : 0.0;
        if (alphaShape <= 0.001) discard;

        vec3 stemColor = vec3(0.16, 0.56, 0.20);
        vec3 petalColor = mix(bc * 0.9, bc * 1.12, y);
        vec3 centerColor = vec3(0.98, 0.84, 0.22);
        vec3 finalColor = (stem > blossom) ? stemColor : petalColor;
        finalColor = mix(finalColor, centerColor, center);
        float flowerLine = 0.03;
        vec2 ff = fract(uvFull * flowerPixelRes);
        bool flowerWire = (ff.x < flowerLine || ff.y < flowerLine);
        if (flowerWire && alphaShape > 0.001) {
            finalColor = vec3(0.0);
        }

        vec3 norm = normalize(Normal);
        float diff = max(dot(norm, normalize(lightDir)), 0.0);
        vec3 lighting = ambientLight + diffuseLight * diff;
        FragColor = vec4(finalColor * lighting * AO, alphaShape);
        return;
    }

    if (isLeaf) {
        float gridSize = 24.0;
        float lineWidth = 0.03;
        vec2 f = fract(TexCoord * gridSize);
        bool isGridLine = (f.x < lineWidth || f.y < lineWidth);
        bool forceOpaqueLeaf = (leafOpaqueOutsideLod0 == 1) && (sectionLod > 0);

        float finalAlpha = 1.0;
        vec3 finalColor = isGridLine ? vec3(0.0) : bc;
        if (!forceOpaqueLeaf) {
            vec2 blockCoord = floor(WorldPos.xy);
            vec2 seed = fract(blockCoord * 0.12345);
            vec2 cell = floor((TexCoord + seed) * 24.0);
            float n = noise(cell);
            float crackThreshold = 0.8;
            float noiseAlpha = (n > crackThreshold) ? 0.0 : 1.0;
            finalAlpha = isGridLine ? 1.0 : noiseAlpha;
            if (finalAlpha <= 0.001) discard;
        }

        vec3 norm = normalize(Normal);
        float diff = max(dot(norm, normalize(lightDir)), 0.0);
        vec3 lighting = ambientLight + diffuseLight * diff;
        finalColor *= lighting;
        FragColor = vec4(finalColor * AO, finalAlpha);
        return;
    }

    if (outAlpha <= 0.001) discard;
    bool isWaterSurfaceFace = isTranslucentFace && (faceType == 2);
    if (isWaterSurfaceFace
        && waterCascadeBrightnessEnabled == 1
        && sectionLod == 0) {
        // Low-cost LOD0-only cascade shimmer for water brightness.
        float t = time * waterCascadeBrightnessSpeed;
        float spatial = max(0.0001, waterCascadeBrightnessScale);
        float c0 = sin((WorldPos.x * 1.00 + WorldPos.z * 0.82) * spatial + t);
        float c1 = sin((WorldPos.x * -0.61 + WorldPos.z * 1.27) * (spatial * 1.73) - t * 1.21);
        float c2 = sin((WorldPos.x * 0.37 + WorldPos.z * -0.48) * (spatial * 0.79) + t * 0.67);
        float cascade = c0 * 0.52 + c1 * 0.33 + c2 * 0.15;
        float brightness = 1.0 + waterCascadeBrightnessStrength * cascade;
        bc = clamp(bc * brightness, 0.0, 1.0);
    }
    if (isWaterSurfaceFace && waterOverlayTextureEnabled == 1) {
        vec2 uv = fract(TexCoord);
        ivec3 waterCell = ivec3(floor(WorldPos + vec3(0.5)));
        int h = (waterCell.x * 73856093) ^ (waterCell.y * 19349663) ^ (waterCell.z * 83492791);
        int quarterTurns = h & 3;
        uv = rotateUvQuarterTurns(uv, quarterTurns);
        vec4 texel = texture(waterOverlayTexture, uv);
        float overlayBlend = clamp(texel.a, 0.0, 1.0);
        bc = mix(bc, texel.rgb, overlayBlend);
    }
    FragColor = vec4(bc * AO, outAlpha);
}
