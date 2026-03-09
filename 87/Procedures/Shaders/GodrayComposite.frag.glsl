#version 330 core
in vec2 vUV;
out vec4 f;
uniform sampler2D godrayTex;
uniform float mapZoom;
uniform vec2 mapCenter;
void main(){
    float zoom = max(mapZoom, 0.0001);
    vec2 uv = (vUV - vec2(0.5)) / zoom + mapCenter;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        f = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec3 col = texture(godrayTex, uv).rgb;
    f = vec4(col, 1.0);
}
