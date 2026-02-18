#version 330 core
in vec2 vUV;
out vec4 f;
uniform sampler2D godrayTex;
void main(){
    vec3 col = texture(godrayTex, vUV).rgb;
    f = vec4(col, 1.0);
}

