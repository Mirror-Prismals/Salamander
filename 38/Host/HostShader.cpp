#pragma once

// --- Shader Class Implementation ---
Shader::Shader(const char* v, const char* f){ID=glCreateProgram();unsigned int vs=glCreateShader(GL_VERTEX_SHADER);glShaderSource(vs,1,&v,0);glCompileShader(vs);check(vs,"V");unsigned int fs=glCreateShader(GL_FRAGMENT_SHADER);glShaderSource(fs,1,&f,0);glCompileShader(fs);check(fs,"F");glAttachShader(ID,vs);glAttachShader(ID,fs);glLinkProgram(ID);check(ID,"P");glDeleteShader(vs);glDeleteShader(fs);}
void Shader::use(){glUseProgram(ID);}
void Shader::setMat4(const std::string&n,const glm::mat4&m)const{glUniformMatrix4fv(glGetUniformLocation(ID,n.c_str()),1,GL_FALSE,&m[0][0]);}
void Shader::setVec3(const std::string&n,const glm::vec3&v)const{glUniform3fv(glGetUniformLocation(ID,n.c_str()),1,&v[0]);}
void Shader::setFloat(const std::string&n,float v)const{glUniform1f(glGetUniformLocation(ID,n.c_str()),v);}
void Shader::setInt(const std::string&n,int v)const{glUniform1i(glGetUniformLocation(ID,n.c_str()),v);}
void Shader::check(unsigned int s,std::string t){int c;char i[1024];if(t!="P"){glGetShaderiv(s,GL_COMPILE_STATUS,&c);if(!c){glGetShaderInfoLog(s,1024,0,i); std::cout << "SHADER COMPILE ERROR: " << i << std::endl;}}else{glGetProgramiv(s,GL_LINK_STATUS,&c);if(!c){glGetProgramInfoLog(s,1024,0,i); std::cout << "SHADER LINK ERROR: " << i << std::endl;}}}
