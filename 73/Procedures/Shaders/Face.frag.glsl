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
uniform int atlasEnabled;
uniform sampler2D atlasTexture;
uniform vec2 atlasTileSize;
uniform vec2 atlasTextureSize;
uniform int tilesPerRow;
uniform int tilesPerCol;

float noise(vec2 p){ return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main() {
    vec3 bc = FragColor_in;
    bool useAtlas = (atlasEnabled == 1) && TileIndex >= 0 && tilesPerRow > 0 && tilesPerCol > 0 && atlasTextureSize.x > 0.0 && atlasTextureSize.y > 0.0;

    if (useAtlas) {
        vec2 tileSizeUV = atlasTileSize / atlasTextureSize;
        int tileX = TileIndex % tilesPerRow;
        int tileY = tilesPerCol - 1 - (TileIndex / tilesPerRow);
        vec2 base = vec2(tileX, tileY) * tileSizeUV;
        vec2 uv = base + TexCoord * tileSizeUV;
        bc = texture(atlasTexture, uv).rgb;
    } else {
        float grid = 24.0;
        float line = 0.03;
        vec2 f = fract(TexCoord * grid);
        if (f.x < line || f.y < line) {
            bc = vec3(0.0);
        } else {
            float d = instanceDistance / 100.0;
            bc = FragColor_in + vec3(0.03 * d);
            bc = clamp(bc, 0.0, 1.0);
        }
    }
    FragColor = vec4(bc * AO, Alpha);
}
