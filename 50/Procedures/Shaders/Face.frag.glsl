#version 330 core
in vec2 TexCoord;
in vec3 FragColor_in;
in float instanceDistance;
in vec3 Normal;
in vec3 WorldPos;
out vec4 FragColor;

uniform vec3 lightDir;
uniform vec3 ambientLight;
uniform vec3 diffuseLight;

float noise(vec2 p){ return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main() {
    float grid = 24.0;
    float line = 0.03;
    vec2 f = fract(TexCoord * grid);
    vec3 bc;
    if (f.x < line || f.y < line) {
        bc = vec3(0.0);
    } else {
        float d = instanceDistance / 100.0;
        bc = FragColor_in + vec3(0.03 * d);
        bc = clamp(bc, 0.0, 1.0);
    }
    vec3 l = ambientLight + diffuseLight * max(dot(normalize(Normal), normalize(lightDir)), 0.0);
    vec3 fc = bc * l;
    FragColor = vec4(fc, 1.0);
}
