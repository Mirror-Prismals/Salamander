#version 330 core
layout(location=0) in vec2 a;
out vec2 t;
uniform mat4 m;
uniform mat4 v;
uniform mat4 p;
void main(){
    t = a*0.5+0.5;
    gl_Position = p*v*m*vec4(a,0,1);
}

