#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec3 aOffset;
layout (location = 4) in vec3 aColor;
layout (location = 5) in int aTileIndex;

out vec2 TexCoord;
out vec3 FragColor_in;
out float instanceDistance;
out vec3 Normal;
out vec3 WorldPos;
flat out int TileIndex;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform int faceType;
uniform vec3 cameraPos;

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

void main() {
    vec3 pos = aPos;
    vec3 normal = aNormal;
    mat3 r = mat3(1.0);

    if (faceType == 0) { // +X
        r = rotY(-1.57079632679);
    } else if (faceType == 1) { // -X
        r = rotY(1.57079632679);
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

    vec3 finalPos = pos + aOffset;
    vec4 worldPos4 = model * vec4(finalPos, 1.0);
    WorldPos = worldPos4.xyz;
    gl_Position = projection * view * worldPos4;

    FragColor_in = aColor;
    TexCoord = aTexCoord;
    instanceDistance = length(aOffset - cameraPos);
    Normal = normalize(mat3(model) * normal);
    TileIndex = aTileIndex;
}
