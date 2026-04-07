#pragma once

#include "IRenderBackend.h"

class WebGPUBackend final : public IRenderBackend {
public:
    struct BackendState;
    ~WebGPUBackend() override;
    const char* name() const override;
    bool createShaderProgram(const char* vertexSource,
                             const char* fragmentSource,
                             RenderHandle& outProgram,
                             std::string& outError) override;
    bool rebuildShaderProgram(RenderHandle& ioProgram,
                              const char* vertexSource,
                              const char* fragmentSource,
                              std::string& outError) override;
    void destroyShaderProgram(RenderHandle& program) override;
    void useShaderProgram(RenderHandle program) override;
    int getShaderUniformLocation(RenderHandle program, const char* name) const override;
    void setShaderUniformMat4(int location, const float* value16) override;
    void setShaderUniformVec3(int location, const float* value3) override;
    void setShaderUniformVec2(int location, const float* value2) override;
    void setShaderUniformFloat(int location, float value) override;
    void setShaderUniformInt(int location, int value) override;
    void setShaderUniformInt3Array(int location, int count, const int* values) override;
    void setShaderUniformFloatArray(int location, int count, const float* values) override;
    bool initialize(int width, int height, const char* title, PlatformWindowHandle& outWindow) override;
    void beginFrame() override;
    void endFrame(PlatformWindowHandle window) override;
    void shutdown(PlatformWindowHandle window) override;
    void onFramebufferResize(int width, int height) override;
    void clearDefaultFramebuffer(float r, float g, float b, float a, bool clearDepth) override;
    void getFramebufferSize(PlatformWindowHandle window, int& width, int& height) const override;
    bool createColorRenderTarget(int width, int height, RenderHandle& outFramebuffer, RenderHandle& outTexture) override;
    void destroyColorRenderTarget(RenderHandle& framebuffer, RenderHandle& texture) override;
    void beginOffscreenColorPass(RenderHandle framebuffer, int width, int height, float r, float g, float b, float a) override;
    void endOffscreenColorPass() override;
    bool readDefaultFramebufferRgb(PlatformWindowHandle window, std::vector<unsigned char>& outPixels, int& width, int& height) const override;
    bool uploadRgbTexture2D(RenderHandle& ioTexture, int width, int height, const std::vector<unsigned char>& pixels) override;
    bool uploadRgbaTexture2D(RenderHandle& ioTexture,
                             int width,
                             int height,
                             const std::vector<unsigned char>& pixels,
                             const TextureUploadParams& params) override;
    bool uploadRgbaTextureSubImage2D(RenderHandle texture,
                                     int x,
                                     int y,
                                     int width,
                                     int height,
                                     const std::vector<unsigned char>& pixels) override;
    bool readTexture2DRgba(RenderHandle texture,
                           int width,
                           int height,
                           std::vector<unsigned char>& outPixels) const override;
    void ensureVertexArray(RenderHandle& ioVertexArray) override;
    void destroyVertexArray(RenderHandle& vertexArray) override;
    void ensureArrayBuffer(RenderHandle& ioBuffer) override;
    void destroyArrayBuffer(RenderHandle& buffer) override;
    void uploadArrayBufferData(RenderHandle buffer, const void* data, size_t sizeBytes, bool dynamic) override;
    void configureVertexArray(RenderHandle vertexArray,
                              RenderHandle vertexBuffer,
                              const std::vector<VertexAttribLayout>& vertexAttribs,
                              RenderHandle instanceBuffer,
                              const std::vector<VertexAttribLayout>& instanceAttribs) override;
    void bindVertexArray(RenderHandle vertexArray) override;
    void unbindVertexArray() override;
    int getMaxTextureImageUnits() const override;
    void drawArraysTriangles(int first, int count) override;
    void drawArraysLines(int first, int count) override;
    void drawArraysPoints(int first, int count) override;
    void drawArraysTrianglesInstanced(int first, int count, int instanceCount) override;
    void destroyTexture(RenderHandle& texture) override;
    bool bindTexture2D(RenderHandle texture, int unit) override;
    void setDepthTestEnabled(bool enabled) override;
    void setDepthWriteEnabled(bool enabled) override;
    void setBlendEnabled(bool enabled) override;
    void setBlendEquationAdd() override;
    void setBlendModeAlpha() override;
    void setBlendModeConstantAlpha(float alpha) override;
    void setBlendModeSrcAlphaOne() override;
    void setBlendModeAdditive() override;
    void setCullEnabled(bool enabled) override;
    void setCullBackFaceCCW() override;
    void setScissorEnabled(bool enabled) override;
    void setScissorRect(int x, int y, int width, int height) override;
    void setLineWidth(float width) override;
    void setProgramPointSizeEnabled(bool enabled) override;
    void setWireframeEnabled(bool enabled) override;

private:
    BackendState* state = nullptr;
};
