#pragma once

// --- Shader Class Implementation (Backend delegated) ---
namespace {
    IRenderBackend*& shaderBackendRef() {
        static IRenderBackend* s_backend = nullptr;
        return s_backend;
    }

    void destroyShaderProgramHandle(RenderHandle& programId) {
        if (!programId) return;
        IRenderBackend* backend = shaderBackendRef();
        if (!backend) {
            std::cerr << "SHADER WARNING: backend unavailable during shader destruction." << std::endl;
            programId = 0;
            return;
        }
        backend->destroyShaderProgram(programId);
    }
}

void Shader::SetRenderBackend(IRenderBackend* backend) {
    shaderBackendRef() = backend;
}

IRenderBackend* Shader::GetRenderBackend() {
    return shaderBackendRef();
}

Shader::Shader(const char* v, const char* f) : backendHandle(0) {
    std::string error;
    if (!rebuild(v, f, &error)) {
        std::cerr << "SHADER ERROR: " << error << std::endl;
    }
}

Shader::~Shader() {
    destroyShaderProgramHandle(backendHandle);
}

Shader::Shader(Shader&& other) noexcept : backendHandle(other.backendHandle) {
    other.backendHandle = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this == &other) return *this;
    destroyShaderProgramHandle(backendHandle);
    backendHandle = other.backendHandle;
    other.backendHandle = 0;
    return *this;
}

bool Shader::rebuild(const char* vertexSource,
                     const char* fragmentSource,
                     std::string* outError) {
    IRenderBackend* backend = shaderBackendRef();
    if (!backend) {
        if (outError) *outError = "Render backend is not set.";
        return false;
    }
    std::string error;
    if (!backend->rebuildShaderProgram(backendHandle, vertexSource, fragmentSource, error)) {
        if (outError) *outError = error;
        return false;
    }
    if (outError) outError->clear();
    return true;
}

bool Shader::isValid() const {
    return backendHandle != 0;
}

void Shader::use() {
    IRenderBackend* backend = shaderBackendRef();
    if (!backend || !backendHandle) return;
    backend->useShaderProgram(backendHandle);
}

void Shader::setMat4(const std::string& n, const glm::mat4& m) const {
    const int loc = findUniform(n);
    if (loc < 0) return;
    IRenderBackend* backend = shaderBackendRef();
    if (!backend) return;
    backend->setShaderUniformMat4(loc, &m[0][0]);
}

void Shader::setVec3(const std::string& n, const glm::vec3& v) const {
    const int loc = findUniform(n);
    if (loc < 0) return;
    IRenderBackend* backend = shaderBackendRef();
    if (!backend) return;
    backend->setShaderUniformVec3(loc, &v[0]);
}

void Shader::setVec2(const std::string& n, const glm::vec2& v) const {
    const int loc = findUniform(n);
    if (loc < 0) return;
    IRenderBackend* backend = shaderBackendRef();
    if (!backend) return;
    backend->setShaderUniformVec2(loc, &v[0]);
}

void Shader::setFloat(const std::string& n, float v) const {
    const int loc = findUniform(n);
    if (loc < 0) return;
    IRenderBackend* backend = shaderBackendRef();
    if (!backend) return;
    backend->setShaderUniformFloat(loc, v);
}

void Shader::setInt(const std::string& n, int v) const {
    const int loc = findUniform(n);
    if (loc < 0) return;
    IRenderBackend* backend = shaderBackendRef();
    if (!backend) return;
    backend->setShaderUniformInt(loc, v);
}

int Shader::findUniform(const std::string& n) const {
    IRenderBackend* backend = shaderBackendRef();
    if (!backend || !backendHandle) return -1;
    return backend->getShaderUniformLocation(backendHandle, n.c_str());
}

void Shader::setIntUniform(int location, int v) const {
    IRenderBackend* backend = shaderBackendRef();
    if (!backend || location < 0) return;
    backend->setShaderUniformInt(location, v);
}

void Shader::setFloatUniform(int location, float v) const {
    IRenderBackend* backend = shaderBackendRef();
    if (!backend || location < 0) return;
    backend->setShaderUniformFloat(location, v);
}

void Shader::setInt3ArrayUniform(int location, int count, const int* values) const {
    IRenderBackend* backend = shaderBackendRef();
    if (!backend || location < 0) return;
    backend->setShaderUniformInt3Array(location, count, values);
}

void Shader::setFloatArrayUniform(int location, int count, const float* values) const {
    IRenderBackend* backend = shaderBackendRef();
    if (!backend || location < 0) return;
    backend->setShaderUniformFloatArray(location, count, values);
}
