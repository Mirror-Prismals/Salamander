#version 330 core
layout(location=0) in vec2 a;
out vec2 uv;
void main(){ uv = a * 0.5 + 0.5; gl_Position = vec4(a, 0.0, 1.0); }

