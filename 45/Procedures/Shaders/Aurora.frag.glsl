#version 330 core
in float vU; in float vV; in vec3 worldPos;
out vec4 FragColor;
uniform vec3 magPal[4]; uniform vec3 limePal[4]; uniform int paletteIndex;
uniform float time; uniform float passAlpha;
vec3 samplePalette(vec3 p[4], float t){
    t = clamp(t,0.0,1.0);
    if(t<0.333){ float tt = smoothstep(0.0,0.333,t)/0.333; return mix(p[0],p[1],tt); }
    else if(t<0.666){ float tt = smoothstep(0.333,0.666,t); return mix(p[1],p[2],tt); }
    else { float tt = smoothstep(0.666,1.0,t); return mix(p[2],p[3],tt); }
}
void main(){
    vec3 pal[4];
    if(paletteIndex==0){ pal[0]=magPal[0]; pal[1]=magPal[1]; pal[2]=magPal[2]; pal[3]=magPal[3]; }
    else { pal[0]=limePal[0]; pal[1]=limePal[1]; pal[2]=limePal[2]; pal[3]=limePal[3]; }
    float t = clamp(vV * 0.98 + 0.02 * sin(worldPos.x*0.01 + time*0.02), 0.0, 1.0);
    vec3 col = samplePalette(pal, t);
    // Luminance normalization so magenta (two-channel) doesn't overpower lime (one-channel)
    if (paletteIndex == 0) col *= 0.65;
    float band = 0.72 + 0.48 * sin(vU * 6.2831853 * 0.9 + time * 0.03 + worldPos.x * 0.0015);
    float fall = pow(1.0 - vV, 0.9);
    float side = smoothstep(0.99, 0.18, 1.0 - abs(vU - 0.5) * 2.0);
    float alpha = clamp(fall * band * passAlpha, 0.0, 1.0) * mix(0.96, 1.0, side);
    // Keep saturation by limiting the boost that pushes magenta toward white
    float boost = 1.0 + 0.18 * band;
    col = pow(col * boost, vec3(0.95));
    FragColor = vec4(col, alpha);
    if(FragColor.a < 0.001) discard;
}

