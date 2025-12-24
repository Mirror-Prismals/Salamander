#version 330 core
in vec2 vUV;
out vec4 f;
uniform sampler2D occlusionTex;
uniform vec2 lightPos; // in 0..1 screen space
uniform float exposure;
uniform float decay;
uniform float density;
uniform float weight;
uniform int samples;
void main(){
    vec2 delta = (lightPos - vUV) * density / float(samples);
    vec2 coord = vUV;
    float illumination = 0.0;
    float curDecay = 1.0;
    for(int i=0;i<samples;i++){
        coord += delta;
        float sample = texture(occlusionTex, coord).r;
        illumination += sample * curDecay * weight;
        curDecay *= decay;
    }
    f = vec4(vec3(illumination * exposure), 1.0);
}

