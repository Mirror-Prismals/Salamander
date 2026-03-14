#version 330 core
in vec2 vUV;
in vec3 vColor;
uniform sampler2D fontTex;
out vec4 FragColor;
void main() {
    float a = texture(fontTex, vUV).r;
    FragColor = vec4(vColor, a);
}
