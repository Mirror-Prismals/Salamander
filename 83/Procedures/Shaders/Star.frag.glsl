#version 330 core in float s;uniform float t;out vec4 f;void main(){float b=0.8+0.2*sin(t*3.0+s*10.0);if(length(gl_PointCoord-vec2(0.5))>0.5)discard;f=vec4(vec3(b),1);}

