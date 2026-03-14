#version 330 core
in vec3 vWorldPos;
in vec3 vWorldNormal;
uniform vec3 cameraPos;
uniform float time;
out vec4 FragColor;
void main(){
    vec3 viewDir = normalize(cameraPos - vWorldPos);
    if(dot(vWorldNormal, viewDir) <= 0.0) discard;
    float cycle = mod(time, 4.0);
    float pulse;
    if(cycle < 3.0){
        pulse = 1.0;
    } else if(cycle < 4.0){
        float t = clamp(cycle - 3.0, 0.0, 1.0);
        float easeDown = 1.0 - pow(1.0 - t, 4.0);
        float fall = mix(1.0, 0.0, easeDown);
        float easeUp = pow(t, 4.0);
        float rise = mix(0.0, 1.0, easeUp);
        pulse = mix(fall, rise, t);
    } else {
        pulse = 1.0;
    }
    FragColor = vec4(vec3(pulse), 1.0);
}

