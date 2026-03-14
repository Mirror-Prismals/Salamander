#version 330 core
in vec3 vNormal;
in float vGain;
in float vOcc;
uniform float baseAlpha;
out vec4 FragColor;
void main(){
    vec3 clearColor = vec3(0.1, 0.9, 1.0);
    vec3 occColor = vec3(1.0, 0.3, 0.6);
    float light = clamp(dot(normalize(vec3(0.4,1.0,0.2)), normalize(vNormal)) * 0.5 + 0.5, 0.3, 1.0);
    float gain = clamp(vGain, 0.0, 1.0);
    vec3 color = mix(clearColor, occColor, clamp(vOcc, 0.0, 1.0));
    color *= mix(0.2, 1.0, gain);
    float a = clamp(baseAlpha * gain * 1.4, 0.08, 0.85);
    FragColor = vec4(color * light, a);
}
