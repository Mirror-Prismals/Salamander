#version 330 core
in vec2 t;
out vec4 f;
uniform vec3 c;
uniform float time;
void main(){
    vec2 uv = t - vec2(0.5);
    float r = length(uv) * 2.0;

    // Solid disk with sharp-ish edge, no outer glow
    float disk = 1.0 - smoothstep(0.16, 0.22, r);
    if (disk < 0.001) discard;
    vec3 base = c;
    vec3 color = base * disk;
    float alpha = disk;
    f = vec4(color, alpha);
}

