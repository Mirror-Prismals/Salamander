#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "Platform/PlatformTypes.h"

using RenderHandle = uint32_t;

enum class TextureFilterMode {
    Linear,
    Nearest
};

enum class TextureWrapMode {
    ClampToEdge,
    Repeat
};

struct TextureUploadParams {
    TextureFilterMode minFilter = TextureFilterMode::Linear;
    TextureFilterMode magFilter = TextureFilterMode::Linear;
    TextureWrapMode wrapS = TextureWrapMode::ClampToEdge;
    TextureWrapMode wrapT = TextureWrapMode::ClampToEdge;
};

enum class VertexAttribType {
    Float,
    Int
};

struct VertexAttribLayout {
    uint32_t index = 0;
    int components = 0;
    VertexAttribType type = VertexAttribType::Float;
    bool normalized = false;
    uint32_t stride = 0;
    size_t offset = 0;
    uint32_t divisor = 0;
};

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
    virtual const char* name() const = 0;
    virtual bool createShaderProgram(const char* vertexSource,
                                     const char* fragmentSource,
                                     RenderHandle& outProgram,
                                     std::string& outError) = 0;
    virtual bool rebuildShaderProgram(RenderHandle& ioProgram,
                                      const char* vertexSource,
                                      const char* fragmentSource,
                                      std::string& outError) = 0;
    virtual void destroyShaderProgram(RenderHandle& program) = 0;
    virtual void useShaderProgram(RenderHandle program) = 0;
    virtual int getShaderUniformLocation(RenderHandle program, const char* name) const = 0;
    virtual void setShaderUniformMat4(int location, const float* value16) = 0;
    virtual void setShaderUniformVec3(int location, const float* value3) = 0;
    virtual void setShaderUniformVec2(int location, const float* value2) = 0;
    virtual void setShaderUniformFloat(int location, float value) = 0;
    virtual void setShaderUniformInt(int location, int value) = 0;
    virtual void setShaderUniformInt3Array(int location, int count, const int* values) = 0;
    virtual void setShaderUniformFloatArray(int location, int count, const float* values) = 0;
    virtual bool initialize(int width, int height, const char* title, PlatformWindowHandle& outWindow) = 0;
    virtual void beginFrame() = 0;
    virtual void endFrame(PlatformWindowHandle window) = 0;
    virtual void shutdown(PlatformWindowHandle window) = 0;
    virtual void onFramebufferResize(int width, int height) = 0;
    virtual void clearDefaultFramebuffer(float r, float g, float b, float a, bool clearDepth) = 0;
    virtual void getFramebufferSize(PlatformWindowHandle window, int& width, int& height) const = 0;
    virtual bool createColorRenderTarget(int width, int height, RenderHandle& outFramebuffer, RenderHandle& outTexture) = 0;
    virtual void destroyColorRenderTarget(RenderHandle& framebuffer, RenderHandle& texture) = 0;
    virtual void beginOffscreenColorPass(RenderHandle framebuffer, int width, int height, float r, float g, float b, float a) = 0;
    virtual void endOffscreenColorPass() = 0;
    virtual bool readDefaultFramebufferRgb(PlatformWindowHandle window, std::vector<unsigned char>& outPixels, int& width, int& height) const = 0;
    virtual bool uploadRgbTexture2D(RenderHandle& ioTexture, int width, int height, const std::vector<unsigned char>& pixels) = 0;
    virtual bool uploadRgbaTexture2D(RenderHandle& ioTexture,
                                     int width,
                                     int height,
                                     const std::vector<unsigned char>& pixels,
                                     const TextureUploadParams& params) = 0;
    virtual bool uploadRgbaTextureSubImage2D(RenderHandle texture,
                                             int x,
                                             int y,
                                             int width,
                                             int height,
                                             const std::vector<unsigned char>& pixels) = 0;
    virtual bool readTexture2DRgba(RenderHandle texture,
                                   int width,
                                   int height,
                                   std::vector<unsigned char>& outPixels) const = 0;
    virtual void ensureVertexArray(RenderHandle& ioVertexArray) = 0;
    virtual void destroyVertexArray(RenderHandle& vertexArray) = 0;
    virtual void ensureArrayBuffer(RenderHandle& ioBuffer) = 0;
    virtual void destroyArrayBuffer(RenderHandle& buffer) = 0;
    virtual void uploadArrayBufferData(RenderHandle buffer, const void* data, size_t sizeBytes, bool dynamic) = 0;
    virtual void configureVertexArray(RenderHandle vertexArray,
                                      RenderHandle vertexBuffer,
                                      const std::vector<VertexAttribLayout>& vertexAttribs,
                                      RenderHandle instanceBuffer,
                                      const std::vector<VertexAttribLayout>& instanceAttribs) = 0;
    virtual void bindVertexArray(RenderHandle vertexArray) = 0;
    virtual void unbindVertexArray() = 0;
    virtual int getMaxTextureImageUnits() const = 0;
    virtual void drawArraysTriangles(int first, int count) = 0;
    virtual void drawArraysLines(int first, int count) = 0;
    virtual void drawArraysPoints(int first, int count) = 0;
    virtual void drawArraysTrianglesInstanced(int first, int count, int instanceCount) = 0;
    virtual void destroyTexture(RenderHandle& texture) = 0;
    virtual bool bindTexture2D(RenderHandle texture, int unit) = 0;
    virtual void setDepthTestEnabled(bool enabled) = 0;
    virtual void setDepthWriteEnabled(bool enabled) = 0;
    virtual void setBlendEnabled(bool enabled) = 0;
    virtual void setBlendEquationAdd() = 0;
    virtual void setBlendModeAlpha() = 0;
    virtual void setBlendModeConstantAlpha(float alpha) = 0;
    virtual void setBlendModeSrcAlphaOne() = 0;
    virtual void setBlendModeAdditive() = 0;
    virtual void setCullEnabled(bool enabled) = 0;
    virtual void setCullBackFaceCCW() = 0;
    virtual void setScissorEnabled(bool enabled) = 0;
    virtual void setScissorRect(int x, int y, int width, int height) = 0;
    virtual void setLineWidth(float width) = 0;
    virtual void setProgramPointSizeEnabled(bool enabled) = 0;
    virtual void setWireframeEnabled(bool enabled) = 0;
};
