#version 330 core
in float vGain;
in float vOccluded;
out vec4 FragColor;
void main(){
    vec3 clearColor = vec3(0.1, 0.9, 1.0);
    vec3 occludedColor = vec3(1.0, 0.3, 0.6);
    float occlamped = clamp(vOccluded, 0.0, 1.0);
    vec3 color = mix(clearColor, occludedColor, occlamped);
    float a = clamp(vGain, 0.05, 1.0);
    FragColor = vec4(color * a, 0.95);
}

