#pragma once

#include "WebGPUBackend.h"

#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <limits>

#include <glm/glm.hpp>

#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#endif

#include "Host/PlatformInput.h"

#if defined(SALAMANDER_ENABLE_WEBGPU) && defined(SALAMANDER_LINK_WEBGPU)
#if __has_include(<webgpu/webgpu.h>)
#include <webgpu/webgpu.h>
#define SALAMANDER_WEBGPU_NATIVE 1
#elif __has_include(<webgpu.h>)
#include <webgpu.h>
#define SALAMANDER_WEBGPU_NATIVE 1
#endif
#endif

#ifndef SALAMANDER_WEBGPU_NATIVE
#define SALAMANDER_WEBGPU_NATIVE 0
#endif

struct WebGPUBackend::BackendState {
    PlatformWindowHandle window = nullptr;
    float clearColor[4]{0.0f, 0.0f, 0.0f, 1.0f};
    bool clearRequested = false;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    uint32_t nextHandle = 1;
    int nextUniformLocation = 1;

#if SALAMANDER_WEBGPU_NATIVE
    struct BufferResource {
        std::vector<uint8_t> cpuBytes;
        bool dynamic = false;
        bool dirty = false;
        WGPUBuffer gpuBuffer = nullptr;
        size_t gpuSize = 0;
    };

    struct TextureResource {
        WGPUTexture texture = nullptr;
        WGPUTextureView defaultView = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        WGPUTextureFormat format = WGPUTextureFormat_Undefined;
        WGPUTextureUsage usage = 0;
    };

    struct FramebufferResource {
        RenderHandle textureHandle = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        WGPUTexture depthTexture = nullptr;
        WGPUTextureView depthTextureView = nullptr;
    };

    struct VertexArrayResource {
        RenderHandle vertexBuffer = 0;
        std::vector<VertexAttribLayout> vertexAttribs;
        RenderHandle instanceBuffer = 0;
        std::vector<VertexAttribLayout> instanceAttribs;
    };

    struct UniformLocationBinding {
        RenderHandle program = 0;
        std::string name;
    };

    struct ProgramUniforms {
        std::array<float, 16> model{
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        };
        std::array<float, 16> view{
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        };
        std::array<float, 16> projection{
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        };
        std::array<float, 4> color{1, 1, 1, 1};
        std::array<float, 4> topColor{0.52f, 0.66f, 0.95f, 1.0f};
        std::array<float, 4> bottomColor{0.03f, 0.06f, 0.18f, 1.0f};
        float time = 0.0f;
        std::array<float, 2> lightPos{0.5f, 0.5f};
        std::array<float, 2> mapCenter{0.5f, 0.5f};
        float mapZoom = 1.0f;
        float exposure = 0.6f;
        float decay = 0.94f;
        float density = 0.96f;
        float weight = 0.3f;
        float samples = 64.0f;
        std::array<float, 3> cameraPos{0.0f, 0.0f, 0.0f};
        float instanceScale = 1.0f;
        std::array<float, 3> lightDir{0.0f, -1.0f, 0.0f};
        float blockDamageGrid = 24.0f;
        std::array<float, 3> ambientLight{0.4f, 0.4f, 0.4f};
        float leafDirectionalLightingIntensity = 1.0f;
        std::array<float, 3> diffuseLight{0.6f, 0.6f, 0.6f};
        float waterCascadeBrightnessStrength = 0.1f;
        std::array<float, 2> atlasTileSize{1.0f, 1.0f};
        std::array<float, 2> atlasTextureSize{1.0f, 1.0f};
        float wallStoneUvJitterMinPixels = 0.0f;
        float wallStoneUvJitterMaxPixels = 0.0f;
        float waterCascadeBrightnessSpeed = 1.0f;
        float waterCascadeBrightnessScale = 1.0f;
        float waterReflectionPlaneY = 0.0f;
        std::array<int32_t, 64 * 4> blockDamageCells{};
        std::array<float, 64> blockDamageProgress{};
    };

    struct ProgramResource {
        ProgramUniforms uniforms{};
        std::unordered_map<std::string, int> uniformNameToLocation;
        std::unordered_map<int, std::string> uniformLocationToName;
        std::unordered_map<std::string, int> uniformIntValues;
        std::shared_ptr<const std::unordered_map<std::string, int>> uniformIntSnapshot;
        uint64_t uniformIntValuesVersion = 1;
        uint64_t uniformIntSnapshotVersion = 0;
        std::string vertexSource;
        std::string fragmentSource;
        std::string vertexEntryPoint = "vs_main";
        std::string fragmentEntryPoint = "fs_main";
        bool sourceIsWgsl = false;
        bool usingFallbackShaders = true;
        bool loggedFallbackWarning = false;
        WGPUShaderModule vertexShaderModule = nullptr;
        WGPUShaderModule fragmentShaderModule = nullptr;
        WGPUBuffer uniformBuffer = nullptr;
        WGPUBindGroupLayout bindGroupLayout = nullptr;
        WGPUPipelineLayout pipelineLayout = nullptr;
        WGPUBindGroup bindGroup = nullptr;
        std::unordered_map<std::string, WGPURenderPipeline> layoutPipelines;
        WGPURenderPipeline trianglesPipeline = nullptr;
        WGPURenderPipeline linesPipeline = nullptr;
        WGPURenderPipeline pointsPipeline = nullptr;
    };

    struct RenderState {
        enum class BlendMode {
            Alpha,
            ConstantAlpha,
            SrcAlphaOne,
            Additive,
        };

        bool depthTestEnabled = false;
        bool depthWriteEnabled = true;
        bool blendEnabled = false;
        BlendMode blendMode = BlendMode::Alpha;
        float blendConstantAlpha = 1.0f;
        bool cullEnabled = false;
        bool frontFaceCCW = true;
        bool cullBackFace = true;
        bool scissorEnabled = false;
        int scissorX = 0;
        int scissorY = 0;
        int scissorWidth = 0;
        int scissorHeight = 0;
        float lineWidth = 1.0f;
        bool programPointSizeEnabled = false;
        bool wireframeEnabled = false;
    };

    struct DrawCommand {
        enum class Primitive {
            Triangles,
            Lines,
            Points,
        };
        Primitive primitive = Primitive::Triangles;
        RenderHandle program = 0;
        RenderHandle vertexArray = 0;
        RenderHandle framebuffer = 0;
        ProgramUniforms uniforms{};
        std::shared_ptr<const std::unordered_map<std::string, int>> uniformIntValues;
        std::array<RenderHandle, 32> textureUnits{};
        std::vector<uint8_t> vertexBufferSnapshot;
        std::vector<uint8_t> instanceBufferSnapshot;
        RenderState renderState{};
        std::array<float, 4> passClearColor{0.0f, 0.0f, 0.0f, 0.0f};
        bool passClearColorRequested = false;
        bool passClearDepthRequested = false;
        int first = 0;
        int count = 0;
        int instanceCount = 1;
    };

    bool initialized = false;
    bool surfaceConfigured = false;
    WGPUInstance instance = nullptr;
    WGPUSurface surface = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;
    WGPUPresentMode presentMode = WGPUPresentMode_Fifo;
    WGPUCompositeAlphaMode alphaMode = WGPUCompositeAlphaMode_Auto;
    WGPUTextureFormat depthFormat = WGPUTextureFormat_Depth24Plus;
    RenderHandle currentProgram = 0;
    RenderHandle currentVertexArray = 0;
    RenderState currentRenderState{};
    std::unordered_map<RenderHandle, ProgramResource> programs;
    std::unordered_map<int, UniformLocationBinding> uniformLocations;
    std::unordered_map<RenderHandle, VertexArrayResource> vertexArrayResources;
    std::unordered_map<RenderHandle, BufferResource> bufferResources;
    std::vector<DrawCommand> drawCommands;
    std::unordered_map<RenderHandle, TextureResource> textures;
    std::unordered_map<RenderHandle, FramebufferResource> framebuffers;
    struct OffscreenScope {
        RenderHandle framebuffer = 0;
        std::array<float, 4> clearColor{0.0f, 0.0f, 0.0f, 0.0f};
        bool pendingClear = false;
    };
    std::vector<OffscreenScope> offscreenScopes;
    std::unordered_set<RenderHandle> vertexArrays;
    std::unordered_set<RenderHandle> arrayBuffers;
    std::array<RenderHandle, 32> boundTextureUnits{};
    WGPUSampler defaultSampler = nullptr;
    WGPUTexture fallbackTexture = nullptr;
    WGPUTextureView fallbackTextureView = nullptr;
    bool clearDepthRequested = true;
    WGPUTexture depthTexture = nullptr;
    WGPUTextureView depthTextureView = nullptr;
    uint32_t depthTextureWidth = 0;
    uint32_t depthTextureHeight = 0;
#endif
};

#if SALAMANDER_WEBGPU_NATIVE
namespace {
    WGPUStringView stringView(const char* value) {
        WGPUStringView out{};
        out.data = value;
        out.length = value ? WGPU_STRLEN : 0;
        return out;
    }

    std::string fromStringView(WGPUStringView view) {
        if (!view.data) return {};
        if (view.length == WGPU_STRLEN) return std::string(view.data);
        return std::string(view.data, view.length);
    }

    bool parseEnvBool(const char* value) {
        if (!value || !*value) return false;
        std::string lowered(value);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return lowered == "1"
            || lowered == "true"
            || lowered == "yes"
            || lowered == "on";
    }

    bool webgpuPassStatsEnabled() {
        static int cached = -1;
        if (cached < 0) {
            cached = parseEnvBool(std::getenv("SALAMANDER_WEBGPU_PASS_LOG")) ? 1 : 0;
        }
        return cached == 1;
    }

    bool webgpuFaceDebugEnabled() {
        static int cached = -1;
        if (cached < 0) {
            cached = parseEnvBool(std::getenv("SALAMANDER_WEBGPU_FACE_LOG")) ? 1 : 0;
        }
        return cached == 1;
    }

    bool shouldTrackUniformIntKey(const std::string& lower) {
        return lower == "behaviortype"
            || lower == "ready"
            || lower == "wireframedebug"
            || lower == "buildmodetype"
            || lower == "blockdamageenabled"
            || lower == "channelindex"
            || lower == "blockdamagecount"
            || lower == "previewtileindex"
            || lower == "facetype"
            || lower == "utype"
            || lower == "sectionlod"
            || lower == "atlasenabled"
            || lower == "tilesperrow"
            || lower == "tilespercol"
            || lower == "grasstextureenabled"
            || lower == "shortgrasstextureenabled"
            || lower == "oretextureenabled"
            || lower == "terraintextureenabled"
            || lower == "wateroverlaytextureenabled"
            || lower == "leafopaqueoutsidelod0"
            || lower == "leafbackfaceswheninside"
            || lower == "leafdirectionallightingenabled"
            || lower == "watercascadebrightnessenabled"
            || lower == "wallstoneuvjitterenabled"
            || lower == "grassatlasshortbasetile"
            || lower == "grassatlastallbasetile"
            || lower == "wallstoneuvjittertile0"
            || lower == "wallstoneuvjittertile1"
            || lower == "wallstoneuvjittertile2"
            || lower == "voxelgridlinesenabled"
            || lower == "foliagelightingenabled"
            || lower == "voxelgridlineinvertcolorenabled"
            || lower == "leaffanalignenabled"
            || lower == "foliagewindenabled"
            || lower == "waterplanarreflectionenabled"
            || lower == "waterunderwatercausticsenabled"
            || lower == "waterpalettevariant"
            || lower == "chargedualtoneenabled";
    }

    struct AdapterRequestResult {
        bool done = false;
        WGPURequestAdapterStatus status = WGPURequestAdapterStatus_Unknown;
        WGPUAdapter adapter = nullptr;
        std::string message;
    };

    struct DeviceRequestResult {
        bool done = false;
        WGPURequestDeviceStatus status = WGPURequestDeviceStatus_Unknown;
        WGPUDevice device = nullptr;
        std::string message;
    };


    void onRequestAdapter(WGPURequestAdapterStatus status,
                          WGPUAdapter adapter,
                          WGPUStringView message,
                          void* userdata1,
                          void* userdata2) {
        (void)userdata2;
        if (!userdata1) return;
        AdapterRequestResult& result = *static_cast<AdapterRequestResult*>(userdata1);
        result.status = status;
        result.adapter = adapter;
        result.message = fromStringView(message);
        result.done = true;
    }

    void onRequestDevice(WGPURequestDeviceStatus status,
                         WGPUDevice device,
                         WGPUStringView message,
                         void* userdata1,
                         void* userdata2) {
        (void)userdata2;
        if (!userdata1) return;
        DeviceRequestResult& result = *static_cast<DeviceRequestResult*>(userdata1);
        result.status = status;
        result.device = device;
        result.message = fromStringView(message);
        result.done = true;
    }


    void onDeviceLost(WGPUDevice const* device,
                      WGPUDeviceLostReason reason,
                      WGPUStringView message,
                      void* userdata1,
                      void* userdata2) {
        (void)device;
        (void)userdata1;
        (void)userdata2;
        std::cerr << "WebGPU device lost (reason=" << static_cast<int>(reason)
                  << "): " << fromStringView(message) << "\n";
    }

    void onUncapturedError(WGPUDevice const* device,
                           WGPUErrorType type,
                           WGPUStringView message,
                           void* userdata1,
                           void* userdata2) {
        (void)device;
        (void)userdata1;
        (void)userdata2;
        std::cerr << "WebGPU uncaptured error (type=" << static_cast<int>(type)
                  << "): " << fromStringView(message) << "\n";
    }

    bool pumpInstanceUntil(WGPUInstance instance, bool& done, std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!done) {
            wgpuInstanceProcessEvents(instance);
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    bool requestAdapterBlocking(WGPUInstance instance,
                                const WGPURequestAdapterOptions& options,
                                WGPUAdapter& outAdapter,
                                std::string& outError) {
        outAdapter = nullptr;
        outError.clear();

        AdapterRequestResult result{};
        WGPURequestAdapterCallbackInfo callbackInfo{};
        callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
        callbackInfo.callback = onRequestAdapter;
        callbackInfo.userdata1 = &result;
        wgpuInstanceRequestAdapter(instance, &options, callbackInfo);

        if (!pumpInstanceUntil(instance, result.done, std::chrono::seconds(8))) {
            outError = "Timed out waiting for WebGPU adapter request.";
            return false;
        }
        if (result.status != WGPURequestAdapterStatus_Success || !result.adapter) {
            outError = "WebGPU adapter request failed";
            if (!result.message.empty()) outError += ": " + result.message;
            return false;
        }
        outAdapter = result.adapter;
        return true;
    }

    bool requestDeviceBlocking(WGPUInstance instance,
                               WGPUAdapter adapter,
                               const WGPUDeviceDescriptor& descriptor,
                               WGPUDevice& outDevice,
                               std::string& outError) {
        outDevice = nullptr;
        outError.clear();

        DeviceRequestResult result{};
        WGPURequestDeviceCallbackInfo callbackInfo{};
        callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
        callbackInfo.callback = onRequestDevice;
        callbackInfo.userdata1 = &result;
        wgpuAdapterRequestDevice(adapter, &descriptor, callbackInfo);

        if (!pumpInstanceUntil(instance, result.done, std::chrono::seconds(8))) {
            outError = "Timed out waiting for WebGPU device request.";
            return false;
        }
        if (result.status != WGPURequestDeviceStatus_Success || !result.device) {
            outError = "WebGPU device request failed";
            if (!result.message.empty()) outError += ": " + result.message;
            return false;
        }
        outDevice = result.device;
        return true;
    }

    template <typename T>
    bool containsValue(const T* values, size_t count, T expected) {
        if (!values || count == 0) return false;
        for (size_t i = 0; i < count; ++i) {
            if (values[i] == expected) return true;
        }
        return false;
    }

    WGPUTextureFormat chooseSurfaceFormat(const WGPUSurfaceCapabilities& caps) {
        const std::array<WGPUTextureFormat, 4> preferred{
            WGPUTextureFormat_BGRA8Unorm,
            WGPUTextureFormat_BGRA8UnormSrgb,
            WGPUTextureFormat_RGBA8Unorm,
            WGPUTextureFormat_RGBA8UnormSrgb,
        };
        for (WGPUTextureFormat format : preferred) {
            if (containsValue(caps.formats, caps.formatCount, format)) return format;
        }
        return (caps.formatCount > 0 && caps.formats) ? caps.formats[0] : WGPUTextureFormat_Undefined;
    }

    WGPUPresentMode choosePresentMode(const WGPUSurfaceCapabilities& caps) {
        if (containsValue(caps.presentModes, caps.presentModeCount, WGPUPresentMode_Fifo)) {
            return WGPUPresentMode_Fifo;
        }
        if (containsValue(caps.presentModes, caps.presentModeCount, WGPUPresentMode_Mailbox)) {
            return WGPUPresentMode_Mailbox;
        }
        if (containsValue(caps.presentModes, caps.presentModeCount, WGPUPresentMode_Immediate)) {
            return WGPUPresentMode_Immediate;
        }
        return (caps.presentModeCount > 0 && caps.presentModes)
            ? caps.presentModes[0]
            : WGPUPresentMode_Fifo;
    }

    WGPUCompositeAlphaMode chooseAlphaMode(const WGPUSurfaceCapabilities& caps) {
        if (containsValue(caps.alphaModes, caps.alphaModeCount, WGPUCompositeAlphaMode_Opaque)) {
            return WGPUCompositeAlphaMode_Opaque;
        }
        return (caps.alphaModeCount > 0 && caps.alphaModes)
            ? caps.alphaModes[0]
            : WGPUCompositeAlphaMode_Auto;
    }

    void releaseDepthAttachment(WebGPUBackend::BackendState& state) {
        if (state.depthTextureView) {
            wgpuTextureViewRelease(state.depthTextureView);
            state.depthTextureView = nullptr;
        }
        if (state.depthTexture) {
            wgpuTextureRelease(state.depthTexture);
            state.depthTexture = nullptr;
        }
        state.depthTextureWidth = 0;
        state.depthTextureHeight = 0;
    }

    bool ensureDepthAttachment(WebGPUBackend::BackendState& state, uint32_t width, uint32_t height) {
        if (!state.device || width == 0 || height == 0) return false;
        if (state.depthTexture
            && state.depthTextureView
            && state.depthTextureWidth == width
            && state.depthTextureHeight == height) {
            return true;
        }

        releaseDepthAttachment(state);

        WGPUTextureDescriptor depthDesc{};
        depthDesc.label = stringView("Cardinal Main Depth");
        depthDesc.size.width = width;
        depthDesc.size.height = height;
        depthDesc.size.depthOrArrayLayers = 1;
        depthDesc.mipLevelCount = 1;
        depthDesc.sampleCount = 1;
        depthDesc.dimension = WGPUTextureDimension_2D;
        depthDesc.format = state.depthFormat;
        depthDesc.usage = WGPUTextureUsage_RenderAttachment;

        state.depthTexture = wgpuDeviceCreateTexture(state.device, &depthDesc);
        if (!state.depthTexture) {
            releaseDepthAttachment(state);
            return false;
        }
        state.depthTextureView = wgpuTextureCreateView(state.depthTexture, nullptr);
        if (!state.depthTextureView) {
            releaseDepthAttachment(state);
            return false;
        }
        state.depthTextureWidth = width;
        state.depthTextureHeight = height;
        return true;
    }

    bool configureSurface(WebGPUBackend::BackendState& state, int width, int height) {
        if (!state.surface || !state.device || width <= 0 || height <= 0) return false;

        WGPUSurfaceConfiguration config{};
        config.device = state.device;
        config.format = state.surfaceFormat;
        config.usage = WGPUTextureUsage_RenderAttachment;
        config.width = static_cast<uint32_t>(width);
        config.height = static_cast<uint32_t>(height);
        config.alphaMode = state.alphaMode;
        config.presentMode = state.presentMode;
        wgpuSurfaceConfigure(state.surface, &config);
        if (!ensureDepthAttachment(state, static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
            return false;
        }
        state.surfaceConfigured = true;
        state.framebufferWidth = width;
        state.framebufferHeight = height;
        if (state.window) {
            PlatformInput::ResizeMetalLayerDrawable(state.window, width, height);
        }
        return true;
    }

    RenderHandle allocateHandle(WebGPUBackend::BackendState& state) {
        RenderHandle handle = state.nextHandle++;
        if (handle == 0) {
            handle = state.nextHandle++;
        }
        return handle;
    }

    void releaseTextureResource(WebGPUBackend::BackendState::TextureResource& resource) {
        if (resource.defaultView) {
            wgpuTextureViewRelease(resource.defaultView);
            resource.defaultView = nullptr;
        }
        if (resource.texture) {
            wgpuTextureRelease(resource.texture);
            resource.texture = nullptr;
        }
        resource.width = 0;
        resource.height = 0;
        resource.format = WGPUTextureFormat_Undefined;
        resource.usage = 0;
    }

    void releaseFramebufferResource(WebGPUBackend::BackendState::FramebufferResource& resource) {
        if (resource.depthTextureView) {
            wgpuTextureViewRelease(resource.depthTextureView);
            resource.depthTextureView = nullptr;
        }
        if (resource.depthTexture) {
            wgpuTextureRelease(resource.depthTexture);
            resource.depthTexture = nullptr;
        }
        resource.textureHandle = 0;
        resource.width = 0;
        resource.height = 0;
    }

    bool createOrReplaceTexture2D(WebGPUBackend::BackendState& state,
                                  RenderHandle requestedHandle,
                                  uint32_t width,
                                  uint32_t height,
                                  WGPUTextureFormat format,
                                  WGPUTextureUsage usage,
                                  const char* label,
                                  RenderHandle& outHandle) {
        outHandle = 0;
        if (!state.device || width == 0 || height == 0 || format == WGPUTextureFormat_Undefined) {
            return false;
        }

        WGPUTextureDescriptor desc{};
        desc.label = stringView(label ? label : "Cardinal WebGPU Texture");
        desc.size.width = width;
        desc.size.height = height;
        desc.size.depthOrArrayLayers = 1;
        desc.mipLevelCount = 1;
        desc.sampleCount = 1;
        desc.dimension = WGPUTextureDimension_2D;
        desc.format = format;
        desc.usage = usage;

        WGPUTexture texture = wgpuDeviceCreateTexture(state.device, &desc);
        if (!texture) {
            return false;
        }

        WGPUTextureView view = wgpuTextureCreateView(texture, nullptr);
        if (!view) {
            wgpuTextureRelease(texture);
            return false;
        }

        RenderHandle handle = requestedHandle;
        auto existingIt = state.textures.find(requestedHandle);
        if (handle == 0 || existingIt == state.textures.end()) {
            handle = allocateHandle(state);
        } else {
            releaseTextureResource(existingIt->second);
        }

        WebGPUBackend::BackendState::TextureResource& dst = state.textures[handle];
        dst.texture = texture;
        dst.defaultView = view;
        dst.width = width;
        dst.height = height;
        dst.format = format;
        dst.usage = usage;
        outHandle = handle;
        return true;
    }

    struct FallbackUniformBlock {
        float model[16];
        float view[16];
        float projection[16];
        float mvp[16];
        float color[4];
        float topColor[4];
        float bottomColor[4];
        float params[4];
        float vec2Data[4];
        float extra[4];
        float cameraAndScale[4];
        float lightAndGrid[4];
        float ambientAndLeaf[4];
        float diffuseAndWater[4];
        float atlasInfo[4];
        float wallStoneAndWater2[4];
        int32_t intParams0[4];
        int32_t intParams1[4];
        int32_t intParams2[4];
        int32_t intParams3[4];
        int32_t intParams4[4];
        int32_t intParams5[4];
        int32_t intParams6[4];
        int32_t blockDamageCells[64][4];
        float blockDamageProgress[64];
    };

    const char* kFallbackShaderWgsl = R"(
struct Uniforms {
    model: mat4x4<f32>,
    view: mat4x4<f32>,
    projection: mat4x4<f32>,
    mvp: mat4x4<f32>,
    color: vec4<f32>,
    topColor: vec4<f32>,
    bottomColor: vec4<f32>,
    params: vec4<f32>,
    vec2Data: vec4<f32>,
    extra: vec4<f32>,
    cameraAndScale: vec4<f32>,
    lightAndGrid: vec4<f32>,
    ambientAndLeaf: vec4<f32>,
    diffuseAndWater: vec4<f32>,
    atlasInfo: vec4<f32>,
    wallStoneAndWater2: vec4<f32>,
    intParams0: vec4<i32>,
    intParams1: vec4<i32>,
    intParams2: vec4<i32>,
    intParams3: vec4<i32>,
    intParams4: vec4<i32>,
    intParams5: vec4<i32>,
    intParams6: vec4<i32>,
    blockDamageCells: array<vec4<i32>, 64>,
    blockDamageProgress: array<vec4<f32>, 16>,
};

@group(0) @binding(0)
var<uniform> u: Uniforms;

struct VSIn {
    @location(0) position: vec3<f32>,
};

struct VSOut {
    @builtin(position) position: vec4<f32>,
};

@vertex
fn vs_main(input: VSIn) -> VSOut {
    var out: VSOut;
    out.position = u.mvp * vec4<f32>(input.position, 1.0);
    return out;
}

@fragment
fn fs_main() -> @location(0) vec4<f32> {
    return u.color;
}
)";

    void releaseBufferResource(WebGPUBackend::BackendState::BufferResource& resource) {
        if (resource.gpuBuffer) {
            wgpuBufferRelease(resource.gpuBuffer);
            resource.gpuBuffer = nullptr;
        }
        resource.cpuBytes.clear();
        resource.gpuSize = 0;
        resource.dirty = false;
        resource.dynamic = false;
    }

    void releaseProgramResource(WebGPUBackend::BackendState::ProgramResource& resource) {
        for (auto& [_, pipeline] : resource.layoutPipelines) {
            if (pipeline) {
                wgpuRenderPipelineRelease(pipeline);
            }
        }
        resource.layoutPipelines.clear();
        if (resource.trianglesPipeline) {
            wgpuRenderPipelineRelease(resource.trianglesPipeline);
            resource.trianglesPipeline = nullptr;
        }
        if (resource.linesPipeline) {
            wgpuRenderPipelineRelease(resource.linesPipeline);
            resource.linesPipeline = nullptr;
        }
        if (resource.pointsPipeline) {
            wgpuRenderPipelineRelease(resource.pointsPipeline);
            resource.pointsPipeline = nullptr;
        }
        if (resource.bindGroup) {
            wgpuBindGroupRelease(resource.bindGroup);
            resource.bindGroup = nullptr;
        }
        if (resource.pipelineLayout) {
            wgpuPipelineLayoutRelease(resource.pipelineLayout);
            resource.pipelineLayout = nullptr;
        }
        if (resource.bindGroupLayout) {
            wgpuBindGroupLayoutRelease(resource.bindGroupLayout);
            resource.bindGroupLayout = nullptr;
        }
        if (resource.uniformBuffer) {
            wgpuBufferRelease(resource.uniformBuffer);
            resource.uniformBuffer = nullptr;
        }
        if (resource.vertexShaderModule) {
            wgpuShaderModuleRelease(resource.vertexShaderModule);
            resource.vertexShaderModule = nullptr;
        }
        if (resource.fragmentShaderModule) {
            wgpuShaderModuleRelease(resource.fragmentShaderModule);
            resource.fragmentShaderModule = nullptr;
        }
        resource.uniformNameToLocation.clear();
        resource.uniformLocationToName.clear();
        resource.uniformIntValues.clear();
        resource.uniformIntSnapshot.reset();
        resource.uniformIntValuesVersion = 1;
        resource.uniformIntSnapshotVersion = 0;
        resource.vertexEntryPoint = "vs_main";
        resource.fragmentEntryPoint = "fs_main";
        resource.usingFallbackShaders = true;
    }

    bool sourceLooksLikeWgsl(const std::string& source) {
        if (source.empty()) return false;
        if (source.find("#version") != std::string::npos) return false;
        if (source.find("@vertex") != std::string::npos) return true;
        if (source.find("@fragment") != std::string::npos) return true;
        if (source.find("fn ") != std::string::npos && source.find("var<") != std::string::npos) return true;
        return false;
    }

    std::string extractWgslEntryPoint(const std::string& source,
                                      const std::string& stageTag,
                                      const std::string& fallbackName) {
        const size_t stagePos = source.find(stageTag);
        if (stagePos == std::string::npos) {
            return fallbackName;
        }
        size_t fnPos = source.find("fn", stagePos);
        if (fnPos == std::string::npos) {
            return fallbackName;
        }
        fnPos += 2;
        while (fnPos < source.size() && std::isspace(static_cast<unsigned char>(source[fnPos]))) {
            ++fnPos;
        }
        size_t end = fnPos;
        while (end < source.size()) {
            const char c = source[end];
            const bool isIdentChar = (c == '_')
                || (c >= 'a' && c <= 'z')
                || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9');
            if (!isIdentChar) break;
            ++end;
        }
        if (end <= fnPos) {
            return fallbackName;
        }
        return source.substr(fnPos, end - fnPos);
    }

    bool validateWgslShaderModule(WGPUInstance instance,
                                  WGPUShaderModule shaderModule,
                                  std::string& outError) {
        (void)instance;
        (void)shaderModule;
        outError.clear();
        return true;
    }

    WGPUShaderModule createWgslShaderModule(WGPUInstance instance,
                                            WGPUDevice device,
                                            const std::string& source,
                                            const char* label) {
        if (!device || source.empty()) return nullptr;
        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = stringView(source.c_str());

        WGPUShaderModuleDescriptor shaderDesc{};
        shaderDesc.label = stringView(label);
        shaderDesc.nextInChain = &wgsl.chain;
        WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
        if (!shaderModule) return nullptr;

        std::string compileError;
        if (!validateWgslShaderModule(instance, shaderModule, compileError)) {
            std::cerr << "WebGPU WGSL compile failed for "
                      << (label ? label : "<unnamed>")
                      << ": " << compileError << "\n";
            wgpuShaderModuleRelease(shaderModule);
            return nullptr;
        }
        return shaderModule;
    }

    constexpr uint32_t kTextureUnitCount = 16u;
    constexpr uint32_t kUniformBinding = 0u;
    constexpr uint32_t kSamplerBinding = 1u;
    constexpr uint32_t kTextureBindingBase = 2u;

    bool ensureTextureBindingResources(WebGPUBackend::BackendState& state) {
        if (!state.device || !state.queue) return false;
        if (state.defaultSampler && state.fallbackTexture && state.fallbackTextureView) {
            return true;
        }

        if (!state.fallbackTexture) {
            WGPUTextureDescriptor fallbackTextureDesc{};
            fallbackTextureDesc.label = stringView("Cardinal Fallback Sample Texture");
            fallbackTextureDesc.dimension = WGPUTextureDimension_2D;
            fallbackTextureDesc.size.width = 1;
            fallbackTextureDesc.size.height = 1;
            fallbackTextureDesc.size.depthOrArrayLayers = 1;
            fallbackTextureDesc.sampleCount = 1;
            fallbackTextureDesc.mipLevelCount = 1;
            fallbackTextureDesc.format = WGPUTextureFormat_RGBA8Unorm;
            fallbackTextureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            state.fallbackTexture = wgpuDeviceCreateTexture(state.device, &fallbackTextureDesc);
            if (!state.fallbackTexture) return false;

            uint8_t whitePixel[4]{255, 255, 255, 255};
            WGPUTexelCopyTextureInfo writeDst{};
            writeDst.texture = state.fallbackTexture;
            writeDst.mipLevel = 0;
            writeDst.origin = {0, 0, 0};
            writeDst.aspect = WGPUTextureAspect_All;

            WGPUTexelCopyBufferLayout writeLayout{};
            writeLayout.offset = 0;
            writeLayout.bytesPerRow = 4;
            writeLayout.rowsPerImage = 1;

            WGPUExtent3D writeSize{};
            writeSize.width = 1;
            writeSize.height = 1;
            writeSize.depthOrArrayLayers = 1;
            wgpuQueueWriteTexture(state.queue, &writeDst, whitePixel, sizeof(whitePixel), &writeLayout, &writeSize);
        }
        if (!state.fallbackTextureView && state.fallbackTexture) {
            state.fallbackTextureView = wgpuTextureCreateView(state.fallbackTexture, nullptr);
            if (!state.fallbackTextureView) return false;
        }
        if (!state.defaultSampler) {
            WGPUSamplerDescriptor samplerDesc{};
            samplerDesc.label = stringView("Cardinal Default Sampler");
            samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
            samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
            samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
            // Atlas/UI textures are authored as pixel-art tiles; nearest avoids
            // cross-tile bleeding seams until per-texture samplers are split out.
            samplerDesc.magFilter = WGPUFilterMode_Nearest;
            samplerDesc.minFilter = WGPUFilterMode_Nearest;
            samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
            samplerDesc.maxAnisotropy = 1;
            state.defaultSampler = wgpuDeviceCreateSampler(state.device, &samplerDesc);
            if (!state.defaultSampler) return false;
        }
        return state.defaultSampler && state.fallbackTexture && state.fallbackTextureView;
    }

    WGPUTextureView resolveTextureViewForUnit(WebGPUBackend::BackendState& state, uint32_t unit) {
        if (unit < state.boundTextureUnits.size()) {
            const RenderHandle handle = state.boundTextureUnits[unit];
            if (handle != 0) {
                auto textureIt = state.textures.find(handle);
                if (textureIt != state.textures.end() && textureIt->second.defaultView) {
                    return textureIt->second.defaultView;
                }
            }
        }
        return state.fallbackTextureView;
    }

    bool refreshProgramBindGroup(WebGPUBackend::BackendState& state,
                                 WebGPUBackend::BackendState::ProgramResource& program,
                                 WGPUBuffer uniformBufferOverride = nullptr) {
        WGPUBuffer uniformBuffer = uniformBufferOverride ? uniformBufferOverride : program.uniformBuffer;
        if (!program.bindGroupLayout || !uniformBuffer) return false;
        if (!ensureTextureBindingResources(state)) return false;

        std::array<WGPUBindGroupEntry, 2 + kTextureUnitCount> entries{};
        entries[0].binding = kUniformBinding;
        entries[0].buffer = uniformBuffer;
        entries[0].offset = 0;
        entries[0].size = sizeof(FallbackUniformBlock);
        entries[1].binding = kSamplerBinding;
        entries[1].sampler = state.defaultSampler;
        for (uint32_t unit = 0; unit < kTextureUnitCount; ++unit) {
            entries[2 + unit].binding = kTextureBindingBase + unit;
            entries[2 + unit].textureView = resolveTextureViewForUnit(state, unit);
        }

        WGPUBindGroupDescriptor bindGroupDesc{};
        bindGroupDesc.label = stringView("Cardinal Program Bind Group");
        bindGroupDesc.layout = program.bindGroupLayout;
        bindGroupDesc.entryCount = static_cast<uint32_t>(entries.size());
        bindGroupDesc.entries = entries.data();

        WGPUBindGroup newBindGroup = wgpuDeviceCreateBindGroup(state.device, &bindGroupDesc);
        if (!newBindGroup) return false;
        if (program.bindGroup) {
            wgpuBindGroupRelease(program.bindGroup);
        }
        program.bindGroup = newBindGroup;
        return true;
    }

    bool ensureProgramGpuResources(WebGPUBackend::BackendState& state,
                                   WebGPUBackend::BackendState::ProgramResource& program) {
        if (!state.device || !state.queue) return false;
        if (program.vertexShaderModule && program.fragmentShaderModule
            && program.uniformBuffer && program.bindGroupLayout
            && program.pipelineLayout && program.bindGroup) {
            return true;
        }

        const bool canUseProvidedWgsl = program.sourceIsWgsl
            && !program.vertexSource.empty()
            && !program.fragmentSource.empty();
        program.usingFallbackShaders = true;
        if (canUseProvidedWgsl) {
            program.vertexEntryPoint = extractWgslEntryPoint(program.vertexSource, "@vertex", "vs_main");
            program.fragmentEntryPoint = extractWgslEntryPoint(program.fragmentSource, "@fragment", "fs_main");
            program.vertexShaderModule = createWgslShaderModule(
                state.instance,
                state.device,
                program.vertexSource,
                "Cardinal WGSL Vertex Program"
            );
            program.fragmentShaderModule = createWgslShaderModule(
                state.instance,
                state.device,
                program.fragmentSource,
                "Cardinal WGSL Fragment Program"
            );
            if (program.vertexShaderModule && program.fragmentShaderModule) {
                program.usingFallbackShaders = false;
            }
        }
        if (!program.vertexShaderModule || !program.fragmentShaderModule) {
            if (program.vertexShaderModule) {
                wgpuShaderModuleRelease(program.vertexShaderModule);
                program.vertexShaderModule = nullptr;
            }
            if (program.fragmentShaderModule) {
                wgpuShaderModuleRelease(program.fragmentShaderModule);
                program.fragmentShaderModule = nullptr;
            }
            program.vertexEntryPoint = "vs_main";
            program.fragmentEntryPoint = "fs_main";
            program.vertexShaderModule = createWgslShaderModule(
                state.instance,
                state.device,
                kFallbackShaderWgsl,
                "Cardinal Fallback WGSL Vertex Program"
            );
            program.fragmentShaderModule = createWgslShaderModule(
                state.instance,
                state.device,
                kFallbackShaderWgsl,
                "Cardinal Fallback WGSL Fragment Program"
            );
            if (!program.loggedFallbackWarning) {
                std::cerr << "WebGPU shader fallback active for one program (source not WGSL-compatible yet)." << std::endl;
                program.loggedFallbackWarning = true;
            }
            program.usingFallbackShaders = true;
        }
        if (!program.vertexShaderModule || !program.fragmentShaderModule) return false;

        WGPUBufferDescriptor uniformBufferDesc{};
        uniformBufferDesc.label = stringView("Cardinal Fallback Uniform Buffer");
        uniformBufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uniformBufferDesc.size = sizeof(FallbackUniformBlock);
        uniformBufferDesc.mappedAtCreation = false;
        program.uniformBuffer = wgpuDeviceCreateBuffer(state.device, &uniformBufferDesc);
        if (!program.uniformBuffer) return false;

        if (!ensureTextureBindingResources(state)) return false;

        std::array<WGPUBindGroupLayoutEntry, 2 + kTextureUnitCount> layoutEntries{};
        layoutEntries[0].binding = kUniformBinding;
        layoutEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
        layoutEntries[0].buffer.hasDynamicOffset = true;
        layoutEntries[0].buffer.minBindingSize = sizeof(FallbackUniformBlock);

        layoutEntries[1].binding = kSamplerBinding;
        layoutEntries[1].visibility = WGPUShaderStage_Fragment;
        layoutEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        for (uint32_t unit = 0; unit < kTextureUnitCount; ++unit) {
            WGPUBindGroupLayoutEntry& entry = layoutEntries[2 + unit];
            entry.binding = kTextureBindingBase + unit;
            entry.visibility = WGPUShaderStage_Fragment;
            entry.texture.sampleType = WGPUTextureSampleType_Float;
            entry.texture.viewDimension = WGPUTextureViewDimension_2D;
            entry.texture.multisampled = false;
        }

        WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc{};
        bindGroupLayoutDesc.label = stringView("Cardinal Program BGL");
        bindGroupLayoutDesc.entryCount = static_cast<uint32_t>(layoutEntries.size());
        bindGroupLayoutDesc.entries = layoutEntries.data();
        program.bindGroupLayout = wgpuDeviceCreateBindGroupLayout(state.device, &bindGroupLayoutDesc);
        if (!program.bindGroupLayout) return false;

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
        pipelineLayoutDesc.label = stringView("Cardinal Program Pipeline Layout");
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &program.bindGroupLayout;
        program.pipelineLayout = wgpuDeviceCreatePipelineLayout(state.device, &pipelineLayoutDesc);
        if (!program.pipelineLayout) return false;

        return refreshProgramBindGroup(state, program);
    }

    WGPUPrimitiveTopology toTopology(WebGPUBackend::BackendState::DrawCommand::Primitive primitive) {
        switch (primitive) {
            case WebGPUBackend::BackendState::DrawCommand::Primitive::Lines:
                return WGPUPrimitiveTopology_LineList;
            case WebGPUBackend::BackendState::DrawCommand::Primitive::Points:
                return WGPUPrimitiveTopology_PointList;
            case WebGPUBackend::BackendState::DrawCommand::Primitive::Triangles:
            default:
                return WGPUPrimitiveTopology_TriangleList;
        }
    }

    WGPUFrontFace toFrontFace(const WebGPUBackend::BackendState::RenderState& state) {
        return state.frontFaceCCW ? WGPUFrontFace_CCW : WGPUFrontFace_CW;
    }

    WGPUCullMode toCullMode(const WebGPUBackend::BackendState::RenderState& state) {
        (void)state;
        // Temporary stabilization: disable culling while WebGPU face winding parity
        // is finalized. This avoids inside-out and missing-face artifacts.
        return WGPUCullMode_None;
    }

    bool toBlendState(const WebGPUBackend::BackendState::RenderState& drawState, WGPUBlendState& outBlend) {
        if (!drawState.blendEnabled) return false;

        WGPUBlendFactor src = WGPUBlendFactor_One;
        WGPUBlendFactor dst = WGPUBlendFactor_Zero;
        switch (drawState.blendMode) {
            case WebGPUBackend::BackendState::RenderState::BlendMode::ConstantAlpha:
                src = WGPUBlendFactor_Constant;
                dst = WGPUBlendFactor_OneMinusConstant;
                break;
            case WebGPUBackend::BackendState::RenderState::BlendMode::SrcAlphaOne:
                src = WGPUBlendFactor_SrcAlpha;
                dst = WGPUBlendFactor_One;
                break;
            case WebGPUBackend::BackendState::RenderState::BlendMode::Additive:
                src = WGPUBlendFactor_One;
                dst = WGPUBlendFactor_One;
                break;
            case WebGPUBackend::BackendState::RenderState::BlendMode::Alpha:
            default:
                src = WGPUBlendFactor_SrcAlpha;
                dst = WGPUBlendFactor_OneMinusSrcAlpha;
                break;
        }

        outBlend.color.srcFactor = src;
        outBlend.color.dstFactor = dst;
        outBlend.color.operation = WGPUBlendOperation_Add;
        outBlend.alpha.srcFactor = src;
        outBlend.alpha.dstFactor = dst;
        outBlend.alpha.operation = WGPUBlendOperation_Add;
        return true;
    }

    size_t vertexAttribElementSize(VertexAttribType type) {
        switch (type) {
            case VertexAttribType::Int: return sizeof(int32_t);
            case VertexAttribType::Float:
            default:
                return sizeof(float);
        }
    }

    bool toWgpuVertexFormat(const VertexAttribLayout& attrib, WGPUVertexFormat& outFormat) {
        if (attrib.components < 1 || attrib.components > 4) {
            return false;
        }
        if (attrib.type == VertexAttribType::Int) {
            switch (attrib.components) {
                case 1: outFormat = WGPUVertexFormat_Sint32; return true;
                case 2: outFormat = WGPUVertexFormat_Sint32x2; return true;
                case 3: outFormat = WGPUVertexFormat_Sint32x3; return true;
                case 4: outFormat = WGPUVertexFormat_Sint32x4; return true;
                default: break;
            }
        } else {
            switch (attrib.components) {
                case 1: outFormat = WGPUVertexFormat_Float32; return true;
                case 2: outFormat = WGPUVertexFormat_Float32x2; return true;
                case 3: outFormat = WGPUVertexFormat_Float32x3; return true;
                case 4: outFormat = WGPUVertexFormat_Float32x4; return true;
                default: break;
            }
        }
        return false;
    }

    uint64_t resolveAttribStride(const std::vector<VertexAttribLayout>& attribs) {
        if (attribs.empty()) return 0u;
        if (attribs.front().stride != 0u) {
            return static_cast<uint64_t>(attribs.front().stride);
        }
        uint64_t maxExtent = 0u;
        for (const VertexAttribLayout& attrib : attribs) {
            const uint64_t size = static_cast<uint64_t>(attrib.components) * vertexAttribElementSize(attrib.type);
            const uint64_t extent = static_cast<uint64_t>(attrib.offset) + size;
            if (extent > maxExtent) {
                maxExtent = extent;
            }
        }
        return maxExtent;
    }

    WGPUVertexStepMode resolveStepMode(const std::vector<VertexAttribLayout>& attribs, WGPUVertexStepMode fallback) {
        for (const VertexAttribLayout& attrib : attribs) {
            if (attrib.divisor > 0u) {
                return WGPUVertexStepMode_Instance;
            }
        }
        return fallback;
    }

    bool buildVertexBufferLayout(const std::vector<VertexAttribLayout>& attribs,
                                 WGPUVertexStepMode defaultStepMode,
                                 std::vector<WGPUVertexAttribute>& outAttributes,
                                 WGPUVertexBufferLayout& outLayout) {
        outAttributes.clear();
        if (attribs.empty()) return false;

        outAttributes.reserve(attribs.size());
        for (const VertexAttribLayout& attrib : attribs) {
            WGPUVertexFormat format{};
            if (!toWgpuVertexFormat(attrib, format)) {
                return false;
            }
            WGPUVertexAttribute wgpuAttrib{};
            wgpuAttrib.shaderLocation = attrib.index;
            wgpuAttrib.offset = static_cast<uint64_t>(attrib.offset);
            wgpuAttrib.format = format;
            outAttributes.push_back(wgpuAttrib);
        }

        const uint64_t stride = resolveAttribStride(attribs);
        if (stride == 0u) {
            return false;
        }

        outLayout.arrayStride = stride;
        outLayout.stepMode = resolveStepMode(attribs, defaultStepMode);
        outLayout.attributeCount = static_cast<uint32_t>(outAttributes.size());
        outLayout.attributes = outAttributes.data();
        return true;
    }

    std::string buildPipelineLayoutKey(const WebGPUBackend::BackendState& state,
                                       const WebGPUBackend::BackendState::ProgramResource& program,
                                       const WebGPUBackend::BackendState::VertexArrayResource* vao,
                                       WebGPUBackend::BackendState::DrawCommand::Primitive primitive,
                                       const WebGPUBackend::BackendState::RenderState& drawState,
                                       WGPUTextureFormat colorFormat,
                                       bool hasDepth) {
        std::string key;
        key.reserve(320);
        key += std::to_string(static_cast<int>(colorFormat));
        key += "|";
        key += std::to_string(hasDepth ? 1 : 0);
        key += "|";
        key += std::to_string(hasDepth ? static_cast<int>(state.depthFormat) : 0);
        key += "|";
        key += std::to_string(static_cast<int>(primitive));
        key += "|";
        key += program.vertexEntryPoint;
        key += "|";
        key += program.fragmentEntryPoint;
        key += "|";
        key += std::to_string(drawState.depthTestEnabled ? 1 : 0);
        key += ",";
        key += std::to_string(drawState.depthWriteEnabled ? 1 : 0);
        key += ",";
        key += std::to_string(drawState.blendEnabled ? 1 : 0);
        key += ",";
        key += std::to_string(static_cast<int>(drawState.blendMode));
        key += ",";
        key += std::to_string(drawState.cullEnabled ? 1 : 0);
        key += ",";
        key += std::to_string(drawState.frontFaceCCW ? 1 : 0);
        key += ",";
        key += std::to_string(drawState.cullBackFace ? 1 : 0);

        auto appendAttribs = [&](const std::vector<VertexAttribLayout>& attribs) {
            key += "|";
            key += std::to_string(attribs.size());
            key += ":";
            for (const VertexAttribLayout& attrib : attribs) {
                key += std::to_string(attrib.index);
                key += ",";
                key += std::to_string(attrib.components);
                key += ",";
                key += std::to_string(static_cast<int>(attrib.type));
                key += ",";
                key += std::to_string(attrib.normalized ? 1 : 0);
                key += ",";
                key += std::to_string(attrib.stride);
                key += ",";
                key += std::to_string(static_cast<unsigned long long>(attrib.offset));
                key += ",";
                key += std::to_string(attrib.divisor);
                key += ";";
            }
        };
        if (vao) {
            appendAttribs(vao->vertexAttribs);
            appendAttribs(vao->instanceAttribs);
        } else {
            key += "|novao";
        }
        return key;
    }

    bool ensureProgramPipeline(WebGPUBackend::BackendState& state,
                               WebGPUBackend::BackendState::ProgramResource& program,
                               const WebGPUBackend::BackendState::VertexArrayResource* vao,
                               WebGPUBackend::BackendState::DrawCommand::Primitive primitive,
                               const WebGPUBackend::BackendState::RenderState& drawState,
                               WGPUTextureFormat colorFormat,
                               bool hasDepth,
                               WGPURenderPipeline& outPipeline) {
        outPipeline = nullptr;
        if (!ensureProgramGpuResources(state, program)) return false;

        const std::string pipelineKey = buildPipelineLayoutKey(
            state, program, vao, primitive, drawState, colorFormat, hasDepth);
        const auto existingPipelineIt = program.layoutPipelines.find(pipelineKey);
        if (existingPipelineIt != program.layoutPipelines.end() && existingPipelineIt->second) {
            outPipeline = existingPipelineIt->second;
            return true;
        }

        std::array<WGPUVertexBufferLayout, 2> vertexBufferLayouts{};
        std::vector<WGPUVertexAttribute> vertexAttributes;
        std::vector<WGPUVertexAttribute> instanceAttributes;
        uint32_t bufferCount = 0u;

        if (program.usingFallbackShaders || !vao) {
            WGPUVertexAttribute fallbackAttribute{};
            fallbackAttribute.shaderLocation = 0;
            fallbackAttribute.offset = 0;
            fallbackAttribute.format = WGPUVertexFormat_Float32x3;
            vertexAttributes.push_back(fallbackAttribute);

            WGPUVertexBufferLayout fallbackBufferLayout{};
            fallbackBufferLayout.arrayStride = sizeof(float) * 3u;
            fallbackBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
            fallbackBufferLayout.attributeCount = static_cast<uint32_t>(vertexAttributes.size());
            fallbackBufferLayout.attributes = vertexAttributes.data();
            vertexBufferLayouts[0] = fallbackBufferLayout;
            bufferCount = 1u;
        } else {
            if (!vao->vertexAttribs.empty()) {
                if (!vao->vertexBuffer) {
                    return false;
                }
                if (!buildVertexBufferLayout(vao->vertexAttribs,
                                             WGPUVertexStepMode_Vertex,
                                             vertexAttributes,
                                             vertexBufferLayouts[bufferCount])) {
                    return false;
                }
                ++bufferCount;
            }

            if (vao->instanceBuffer && !vao->instanceAttribs.empty()) {
                if (vao->vertexAttribs.empty()) {
                    return false;
                }
                if (!buildVertexBufferLayout(vao->instanceAttribs,
                                             WGPUVertexStepMode_Instance,
                                             instanceAttributes,
                                             vertexBufferLayouts[bufferCount])) {
                    return false;
                }
                ++bufferCount;
            }
        }

        WGPUVertexState vertexState{};
        vertexState.module = program.vertexShaderModule;
        vertexState.entryPoint = stringView(program.vertexEntryPoint.c_str());
        vertexState.bufferCount = bufferCount;
        vertexState.buffers = bufferCount > 0u ? vertexBufferLayouts.data() : nullptr;

        WGPUColorTargetState colorTarget{};
        colorTarget.format = colorFormat;
        colorTarget.writeMask = WGPUColorWriteMask_All;
        WGPUBlendState blendState{};
        if (toBlendState(drawState, blendState)) {
            colorTarget.blend = &blendState;
        }

        WGPUFragmentState fragmentState{};
        fragmentState.module = program.fragmentShaderModule;
        fragmentState.entryPoint = stringView(program.fragmentEntryPoint.c_str());
        fragmentState.targetCount = 1;
        fragmentState.targets = &colorTarget;

        WGPUPrimitiveState primitiveState{};
        primitiveState.topology = toTopology(primitive);
        primitiveState.frontFace = toFrontFace(drawState);
        primitiveState.cullMode = toCullMode(drawState);

        WGPUMultisampleState multisample{};
        multisample.count = 1;
        multisample.mask = 0xFFFFFFFFu;
        multisample.alphaToCoverageEnabled = false;

        WGPUStencilFaceState defaultStencilFace{};
        defaultStencilFace.compare = WGPUCompareFunction_Always;
        defaultStencilFace.failOp = WGPUStencilOperation_Keep;
        defaultStencilFace.depthFailOp = WGPUStencilOperation_Keep;
        defaultStencilFace.passOp = WGPUStencilOperation_Keep;

        WGPUDepthStencilState depthState{};
        if (hasDepth) {
            depthState.format = state.depthFormat;
            const bool depthTestEnabled = drawState.depthTestEnabled;
            depthState.depthWriteEnabled = (depthTestEnabled && drawState.depthWriteEnabled)
                ? WGPUOptionalBool_True
                : WGPUOptionalBool_False;
            depthState.depthCompare = depthTestEnabled ? WGPUCompareFunction_Less : WGPUCompareFunction_Always;
            depthState.stencilFront = defaultStencilFace;
            depthState.stencilBack = defaultStencilFace;
            depthState.stencilReadMask = 0xFFFFFFFFu;
            depthState.stencilWriteMask = 0xFFFFFFFFu;
            depthState.depthBias = 0;
            depthState.depthBiasSlopeScale = 0.0f;
            depthState.depthBiasClamp = 0.0f;
        }

        WGPURenderPipelineDescriptor pipelineDesc{};
        pipelineDesc.label = stringView(program.usingFallbackShaders
            ? "Cardinal Fallback Pipeline"
            : "Cardinal WGSL Pipeline");
        pipelineDesc.layout = program.pipelineLayout;
        pipelineDesc.vertex = vertexState;
        pipelineDesc.primitive = primitiveState;
        pipelineDesc.multisample = multisample;
        pipelineDesc.fragment = &fragmentState;
        pipelineDesc.depthStencil = hasDepth ? &depthState : nullptr;
        WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(state.device, &pipelineDesc);
        if (!pipeline) {
            return false;
        }
        program.layoutPipelines[pipelineKey] = pipeline;
        outPipeline = pipeline;
        return true;
    }

    bool ensureGpuVertexBuffer(WebGPUBackend::BackendState& state,
                               WebGPUBackend::BackendState::BufferResource& buffer) {
        if (!state.device || !state.queue || buffer.cpuBytes.empty()) return false;
        const uint64_t requiredSize = static_cast<uint64_t>(buffer.cpuBytes.size());
        if (!buffer.gpuBuffer || buffer.gpuSize < requiredSize) {
            if (buffer.gpuBuffer) {
                wgpuBufferRelease(buffer.gpuBuffer);
                buffer.gpuBuffer = nullptr;
                buffer.gpuSize = 0;
            }
            WGPUBufferDescriptor bufferDesc{};
            bufferDesc.label = stringView("Cardinal Vertex Buffer");
            bufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            bufferDesc.size = requiredSize;
            bufferDesc.mappedAtCreation = false;
            buffer.gpuBuffer = wgpuDeviceCreateBuffer(state.device, &bufferDesc);
            if (!buffer.gpuBuffer) return false;
            buffer.gpuSize = static_cast<size_t>(requiredSize);
            buffer.dirty = true;
        }
        if (buffer.dirty) {
            wgpuQueueWriteBuffer(state.queue,
                                 buffer.gpuBuffer,
                                 0,
                                 buffer.cpuBytes.data(),
                                 buffer.cpuBytes.size());
            buffer.dirty = false;
        }
        return true;
    }

    const VertexAttribLayout* resolvePositionAttrib(const WebGPUBackend::BackendState::VertexArrayResource& vao) {
        for (const auto& attrib : vao.vertexAttribs) {
            if (attrib.index == 0u && attrib.components >= 2) {
                return &attrib;
            }
        }
        if (!vao.vertexAttribs.empty() && vao.vertexAttribs.front().components >= 2) {
            return &vao.vertexAttribs.front();
        }
        return nullptr;
    }

    bool extractPositions(const WebGPUBackend::BackendState::BufferResource& buffer,
                          const VertexAttribLayout& positionAttrib,
                          int first,
                          int count,
                          std::vector<float>& outPositions) {
        outPositions.clear();
        if (count <= 0 || positionAttrib.components <= 0) return false;
        if (first < 0) first = 0;
        const size_t elementSize = positionAttrib.type == VertexAttribType::Int ? sizeof(int32_t) : sizeof(float);
        const size_t stride = positionAttrib.stride != 0u
            ? static_cast<size_t>(positionAttrib.stride)
            : static_cast<size_t>(positionAttrib.components) * elementSize;
        const size_t offset = positionAttrib.offset;
        if (stride == 0 || buffer.cpuBytes.empty()) return false;

        outPositions.reserve(static_cast<size_t>(count) * 3u);
        const uint8_t* raw = buffer.cpuBytes.data();
        const size_t totalSize = buffer.cpuBytes.size();
        for (int i = 0; i < count; ++i) {
            const size_t vertexIndex = static_cast<size_t>(first + i);
            const size_t base = vertexIndex * stride + offset;
            const size_t needed = static_cast<size_t>(positionAttrib.components) * elementSize;
            if (base + needed > totalSize) break;

            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (positionAttrib.type == VertexAttribType::Int) {
                int32_t values[4]{0, 0, 0, 0};
                std::memcpy(values, raw + base, needed);
                x = static_cast<float>(values[0]);
                y = positionAttrib.components >= 2 ? static_cast<float>(values[1]) : 0.0f;
                z = positionAttrib.components >= 3 ? static_cast<float>(values[2]) : 0.0f;
            } else {
                float values[4]{0.0f, 0.0f, 0.0f, 0.0f};
                std::memcpy(values, raw + base, needed);
                x = values[0];
                y = positionAttrib.components >= 2 ? values[1] : 0.0f;
                z = positionAttrib.components >= 3 ? values[2] : 0.0f;
            }
            outPositions.push_back(x);
            outPositions.push_back(y);
            outPositions.push_back(z);
        }
        return outPositions.size() >= 3u;
    }

    void applyScissorState(WGPURenderPassEncoder pass,
                           const WebGPUBackend::BackendState::RenderState& drawState,
                           int framebufferWidth,
                           int framebufferHeight) {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t width = static_cast<uint32_t>(std::max(0, framebufferWidth));
        uint32_t height = static_cast<uint32_t>(std::max(0, framebufferHeight));
        if (drawState.scissorEnabled && framebufferWidth > 0 && framebufferHeight > 0) {
            int scissorX = std::max(0, drawState.scissorX);
            int scissorY = std::max(0, drawState.scissorY);
            int scissorW = std::max(0, drawState.scissorWidth);
            int scissorH = std::max(0, drawState.scissorHeight);
            // Legacy authored scissor rectangles used lower-left origin.
            int topY = framebufferHeight - (scissorY + scissorH);

            if (topY < 0) {
                scissorH += topY;
                topY = 0;
            }

            if (scissorX > framebufferWidth) {
                scissorX = framebufferWidth;
                scissorW = 0;
            } else if (scissorX + scissorW > framebufferWidth) {
                scissorW = framebufferWidth - scissorX;
            }

            if (topY > framebufferHeight) {
                topY = framebufferHeight;
                scissorH = 0;
            } else if (topY + scissorH > framebufferHeight) {
                scissorH = framebufferHeight - topY;
            }

            x = static_cast<uint32_t>(std::max(0, scissorX));
            y = static_cast<uint32_t>(std::max(0, topY));
            width = static_cast<uint32_t>(std::max(0, scissorW));
            height = static_cast<uint32_t>(std::max(0, scissorH));
        }
        wgpuRenderPassEncoderSetScissorRect(pass, x, y, width, height);
    }

    void applyBlendState(WGPURenderPassEncoder pass,
                         const WebGPUBackend::BackendState::RenderState& drawState) {
        if (!drawState.blendEnabled
            || drawState.blendMode != WebGPUBackend::BackendState::RenderState::BlendMode::ConstantAlpha) {
            return;
        }
        const float clampedAlpha = std::max(0.0f, std::min(1.0f, drawState.blendConstantAlpha));
        WGPUColor blendColor{};
        // WebGPU's Constant factor uses RGB for color blending. Set all channels
        // to alpha so this matches GL_CONSTANT_ALPHA semantics.
        blendColor.r = static_cast<double>(clampedAlpha);
        blendColor.g = static_cast<double>(clampedAlpha);
        blendColor.b = static_cast<double>(clampedAlpha);
        blendColor.a = static_cast<double>(clampedAlpha);
        wgpuRenderPassEncoderSetBlendConstant(pass, &blendColor);
    }

    std::string toLowerCase(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    void setMatrixFromArray(std::array<float, 16>& dst, const float* src16) {
        if (!src16) return;
        std::copy(src16, src16 + 16, dst.begin());
    }

    void updateProgramUniformBuffer(WebGPUBackend::BackendState& state,
                                    WebGPUBackend::BackendState::ProgramResource& program,
                                    WGPUBuffer destinationBuffer = nullptr,
                                    uint64_t destinationOffset = 0u,
                                    const std::unordered_map<std::string, int>* uniformIntValuesOverride = nullptr) {
        if (!state.queue) return;
        WGPUBuffer targetBuffer = destinationBuffer ? destinationBuffer : program.uniformBuffer;
        if (!targetBuffer) return;

        glm::mat4 model(1.0f);
        glm::mat4 view(1.0f);
        glm::mat4 projection(1.0f);
        std::memcpy(&model[0][0], program.uniforms.model.data(), sizeof(float) * 16u);
        std::memcpy(&view[0][0], program.uniforms.view.data(), sizeof(float) * 16u);
        std::memcpy(&projection[0][0], program.uniforms.projection.data(), sizeof(float) * 16u);
        // Legacy projection matrices differ from WebGPU conventions:
        // - clip-space Z in [-1, 1] must map to [0, 1]
        glm::mat4 projectionWgpu = projection;
        for (int col = 0; col < 4; ++col) {
            projectionWgpu[col][2] = projection[col][2] * 0.5f + projection[col][3] * 0.5f;
        }
        const glm::mat4 mvp = projectionWgpu * view * model;

        FallbackUniformBlock block{};
        std::copy(program.uniforms.model.begin(), program.uniforms.model.end(), block.model);
        std::copy(program.uniforms.view.begin(), program.uniforms.view.end(), block.view);
        std::memcpy(block.projection, &projectionWgpu[0][0], sizeof(block.projection));
        std::memcpy(block.mvp, &mvp[0][0], sizeof(block.mvp));
        std::copy(program.uniforms.color.begin(), program.uniforms.color.end(), block.color);
        std::copy(program.uniforms.topColor.begin(), program.uniforms.topColor.end(), block.topColor);
        std::copy(program.uniforms.bottomColor.begin(), program.uniforms.bottomColor.end(), block.bottomColor);
        block.params[0] = program.uniforms.time;
        block.params[1] = program.uniforms.mapZoom;
        block.params[2] = program.uniforms.exposure;
        block.params[3] = program.uniforms.decay;
        block.vec2Data[0] = program.uniforms.lightPos[0];
        block.vec2Data[1] = program.uniforms.lightPos[1];
        block.vec2Data[2] = program.uniforms.mapCenter[0];
        block.vec2Data[3] = program.uniforms.mapCenter[1];
        block.extra[0] = program.uniforms.density;
        block.extra[1] = program.uniforms.weight;
        block.extra[2] = program.uniforms.samples;
        block.extra[3] = program.uniforms.waterReflectionPlaneY;

        block.cameraAndScale[0] = program.uniforms.cameraPos[0];
        block.cameraAndScale[1] = program.uniforms.cameraPos[1];
        block.cameraAndScale[2] = program.uniforms.cameraPos[2];
        block.cameraAndScale[3] = program.uniforms.instanceScale;

        block.lightAndGrid[0] = program.uniforms.lightDir[0];
        block.lightAndGrid[1] = program.uniforms.lightDir[1];
        block.lightAndGrid[2] = program.uniforms.lightDir[2];
        block.lightAndGrid[3] = program.uniforms.blockDamageGrid;

        block.ambientAndLeaf[0] = program.uniforms.ambientLight[0];
        block.ambientAndLeaf[1] = program.uniforms.ambientLight[1];
        block.ambientAndLeaf[2] = program.uniforms.ambientLight[2];
        block.ambientAndLeaf[3] = program.uniforms.leafDirectionalLightingIntensity;

        block.diffuseAndWater[0] = program.uniforms.diffuseLight[0];
        block.diffuseAndWater[1] = program.uniforms.diffuseLight[1];
        block.diffuseAndWater[2] = program.uniforms.diffuseLight[2];
        block.diffuseAndWater[3] = program.uniforms.waterCascadeBrightnessStrength;

        block.atlasInfo[0] = program.uniforms.atlasTileSize[0];
        block.atlasInfo[1] = program.uniforms.atlasTileSize[1];
        block.atlasInfo[2] = program.uniforms.atlasTextureSize[0];
        block.atlasInfo[3] = program.uniforms.atlasTextureSize[1];

        block.wallStoneAndWater2[0] = program.uniforms.wallStoneUvJitterMinPixels;
        block.wallStoneAndWater2[1] = program.uniforms.wallStoneUvJitterMaxPixels;
        block.wallStoneAndWater2[2] = program.uniforms.waterCascadeBrightnessSpeed;
        block.wallStoneAndWater2[3] = program.uniforms.waterCascadeBrightnessScale;

        const std::unordered_map<std::string, int>& uniformIntValues =
            uniformIntValuesOverride ? *uniformIntValuesOverride : program.uniformIntValues;
        auto readInt = [&](const char* key, int fallback = 0) -> int32_t {
            const auto it = uniformIntValues.find(key);
            if (it == uniformIntValues.end()) return static_cast<int32_t>(fallback);
            return static_cast<int32_t>(it->second);
        };

        block.intParams0[0] = readInt("behaviortype", readInt("ready", 0));
        block.intParams0[1] = readInt("wireframedebug", readInt("buildmodetype", 0));
        block.intParams0[2] = readInt("blockdamageenabled", readInt("channelindex", 0));
        block.intParams0[3] = readInt("blockdamagecount", readInt("previewtileindex", -1));

        block.intParams1[0] = readInt("facetype", readInt("utype", -1));
        block.intParams1[1] = readInt("sectionlod", 0);
        block.intParams1[2] = readInt("atlasenabled", 0);
        block.intParams1[3] = readInt("tilesperrow", 0);

        block.intParams2[0] = readInt("tilespercol", 0);
        block.intParams2[1] = readInt("grasstextureenabled", 0);
        block.intParams2[2] = readInt("shortgrasstextureenabled", 0);
        block.intParams2[3] = readInt("oretextureenabled", 0);

        block.intParams3[0] = readInt("terraintextureenabled", 0);
        block.intParams3[1] = readInt("wateroverlaytextureenabled", 0);
        block.intParams3[2] = readInt("leafopaqueoutsidelod0", 0);
        block.intParams3[3] = readInt("leafbackfaceswheninside", 0);

        block.intParams4[0] = readInt("leafdirectionallightingenabled", 0);
        block.intParams4[1] = readInt("watercascadebrightnessenabled", 0);
        block.intParams4[2] = readInt("wallstoneuvjitterenabled", 0);
        block.intParams4[3] = readInt("grassatlasshortbasetile", -1);

        block.intParams5[0] = readInt("grassatlastallbasetile", -1);
        block.intParams5[1] = readInt("wallstoneuvjittertile0", -1);
        block.intParams5[2] = readInt("wallstoneuvjittertile1", -1);
        block.intParams5[3] = readInt("wallstoneuvjittertile2", -1);

        block.intParams6[0] = readInt("voxelgridlinesenabled", 1);
        const int32_t foliageLightingEnabled = readInt("foliagelightingenabled", 1) != 0 ? 1 : 0;
        const int32_t voxelGridLineInvertColorEnabled = readInt("voxelgridlineinvertcolorenabled", 0) != 0 ? 1 : 0;
        const int32_t waterPlanarReflectionEnabled = readInt("waterplanarreflectionenabled", 0) != 0 ? 1 : 0;
        const int32_t waterUnderwaterCausticsEnabled = readInt("waterunderwatercausticsenabled", 0) != 0 ? 1 : 0;
        const int32_t leafFanAlignEnabled = readInt("leaffanalignenabled", 1) != 0 ? 1 : 0;
        block.intParams6[1] = foliageLightingEnabled
            | (voxelGridLineInvertColorEnabled << 1)
            | (waterPlanarReflectionEnabled << 2)
            | (waterUnderwaterCausticsEnabled << 3)
            | (leafFanAlignEnabled << 4);
        block.intParams6[2] = readInt("foliagewindenabled", 1);
        block.intParams6[3] = readInt("waterpalettevariant", readInt("chargedualtoneenabled", 0));

        for (size_t i = 0; i < 64; ++i) {
            const size_t base = i * 4u;
            block.blockDamageCells[i][0] = program.uniforms.blockDamageCells[base + 0u];
            block.blockDamageCells[i][1] = program.uniforms.blockDamageCells[base + 1u];
            block.blockDamageCells[i][2] = program.uniforms.blockDamageCells[base + 2u];
            block.blockDamageCells[i][3] = program.uniforms.blockDamageCells[base + 3u];
            block.blockDamageProgress[i] = program.uniforms.blockDamageProgress[i];
        }
        wgpuQueueWriteBuffer(state.queue, targetBuffer, destinationOffset, &block, sizeof(block));
    }

    void appendDrawCommand(WebGPUBackend::BackendState& state,
                           WebGPUBackend::BackendState::DrawCommand::Primitive primitive,
                           int first,
                           int count,
                           int instanceCount) {
        if (count <= 0 || !state.currentProgram || !state.currentVertexArray) {
            return;
        }
        const auto progIt = state.programs.find(state.currentProgram);
        if (progIt == state.programs.end()) {
            return;
        }
        WebGPUBackend::BackendState::DrawCommand cmd{};
        cmd.primitive = primitive;
        cmd.program = state.currentProgram;
        cmd.vertexArray = state.currentVertexArray;
        if (!state.offscreenScopes.empty()) {
            WebGPUBackend::BackendState::OffscreenScope& scope = state.offscreenScopes.back();
            cmd.framebuffer = scope.framebuffer;
            if (scope.pendingClear) {
                cmd.passClearColorRequested = true;
                cmd.passClearDepthRequested = true;
                cmd.passClearColor = scope.clearColor;
                scope.pendingClear = false;
            }
        }
        cmd.uniforms = progIt->second.uniforms;
        auto& program = progIt->second;
        if (!program.uniformIntSnapshot
            || program.uniformIntSnapshotVersion != program.uniformIntValuesVersion) {
            program.uniformIntSnapshot =
                std::make_shared<const std::unordered_map<std::string, int>>(program.uniformIntValues);
            program.uniformIntSnapshotVersion = program.uniformIntValuesVersion;
        }
        cmd.uniformIntValues = program.uniformIntSnapshot;
        cmd.textureUnits = state.boundTextureUnits;
        const auto vaoIt = state.vertexArrayResources.find(state.currentVertexArray);
        if (vaoIt != state.vertexArrayResources.end()) {
            auto snapshotDynamicBuffer = [&](RenderHandle bufferHandle, std::vector<uint8_t>& outSnapshot) {
                outSnapshot.clear();
                if (!bufferHandle) return;
                const auto bufferIt = state.bufferResources.find(bufferHandle);
                if (bufferIt == state.bufferResources.end()) return;
                const WebGPUBackend::BackendState::BufferResource& buffer = bufferIt->second;
                if (!buffer.dynamic || buffer.cpuBytes.empty()) return;
                outSnapshot = buffer.cpuBytes;
            };
            snapshotDynamicBuffer(vaoIt->second.vertexBuffer, cmd.vertexBufferSnapshot);
            snapshotDynamicBuffer(vaoIt->second.instanceBuffer, cmd.instanceBufferSnapshot);
        }
        cmd.renderState = state.currentRenderState;
        cmd.first = first;
        cmd.count = count;
        cmd.instanceCount = std::max(1, instanceCount);
        state.drawCommands.push_back(cmd);
    }

    void releaseNativeResources(WebGPUBackend::BackendState& state) {
        for (auto& [_, program] : state.programs) {
            releaseProgramResource(program);
        }
        state.programs.clear();
        state.uniformLocations.clear();
        state.drawCommands.clear();
        state.currentProgram = 0;
        state.currentVertexArray = 0;

        for (auto& [_, buffer] : state.bufferResources) {
            releaseBufferResource(buffer);
        }
        state.bufferResources.clear();
        state.vertexArrayResources.clear();

        for (auto& [_, texture] : state.textures) {
            releaseTextureResource(texture);
        }
        state.textures.clear();
        for (auto& [_, framebuffer] : state.framebuffers) {
            releaseFramebufferResource(framebuffer);
        }
        state.framebuffers.clear();
        state.offscreenScopes.clear();
        state.vertexArrays.clear();
        state.arrayBuffers.clear();
        state.boundTextureUnits.fill(0);
        if (state.fallbackTextureView) {
            wgpuTextureViewRelease(state.fallbackTextureView);
            state.fallbackTextureView = nullptr;
        }
        if (state.fallbackTexture) {
            wgpuTextureRelease(state.fallbackTexture);
            state.fallbackTexture = nullptr;
        }
        if (state.defaultSampler) {
            wgpuSamplerRelease(state.defaultSampler);
            state.defaultSampler = nullptr;
        }
        releaseDepthAttachment(state);

        if (state.surface && state.surfaceConfigured) {
            wgpuSurfaceUnconfigure(state.surface);
            state.surfaceConfigured = false;
        }
        if (state.queue) {
            wgpuQueueRelease(state.queue);
            state.queue = nullptr;
        }
        if (state.device) {
            wgpuDeviceRelease(state.device);
            state.device = nullptr;
        }
        if (state.adapter) {
            wgpuAdapterRelease(state.adapter);
            state.adapter = nullptr;
        }
        if (state.surface) {
            wgpuSurfaceRelease(state.surface);
            state.surface = nullptr;
        }
        if (state.instance) {
            wgpuInstanceRelease(state.instance);
            state.instance = nullptr;
        }
        state.initialized = false;
    }
}
#endif

WebGPUBackend::~WebGPUBackend() {
    if (state) {
        shutdown(state->window);
    }
}

const char* WebGPUBackend::name() const {
    return "WebGPU";
}

bool WebGPUBackend::createShaderProgram(const char* vertexSource,
                                        const char* fragmentSource,
                                        RenderHandle& outProgram,
                                        std::string& outError) {
    if (!state) {
        state = new BackendState();
    }
    outProgram = state->nextHandle++;
    if (outProgram == 0) {
        outProgram = state->nextHandle++;
    }
#if SALAMANDER_WEBGPU_NATIVE
    BackendState::ProgramResource& program = state->programs[outProgram];
    program.vertexSource = vertexSource ? vertexSource : "";
    program.fragmentSource = fragmentSource ? fragmentSource : "";
    program.sourceIsWgsl = sourceLooksLikeWgsl(program.vertexSource)
        && sourceLooksLikeWgsl(program.fragmentSource);
    program.usingFallbackShaders = !program.sourceIsWgsl;
    program.loggedFallbackWarning = false;
#endif
    outError.clear();
    return true;
}

bool WebGPUBackend::rebuildShaderProgram(RenderHandle& ioProgram,
                                         const char* vertexSource,
                                         const char* fragmentSource,
                                         std::string& outError) {
    if (!state) {
        state = new BackendState();
    }
    if (!ioProgram) {
        ioProgram = state->nextHandle++;
        if (ioProgram == 0) {
            ioProgram = state->nextHandle++;
        }
#if SALAMANDER_WEBGPU_NATIVE
        state->programs[ioProgram] = BackendState::ProgramResource{};
#endif
    }
#if SALAMANDER_WEBGPU_NATIVE
    auto programIt = state->programs.find(ioProgram);
    if (programIt == state->programs.end()) {
        state->programs[ioProgram] = BackendState::ProgramResource{};
        programIt = state->programs.find(ioProgram);
    }
    BackendState::ProgramResource& program = programIt->second;
    releaseProgramResource(program);
    program.vertexSource = vertexSource ? vertexSource : "";
    program.fragmentSource = fragmentSource ? fragmentSource : "";
    program.sourceIsWgsl = sourceLooksLikeWgsl(program.vertexSource)
        && sourceLooksLikeWgsl(program.fragmentSource);
    program.usingFallbackShaders = !program.sourceIsWgsl;
    program.loggedFallbackWarning = false;
#endif
    outError.clear();
    return true;
}

void WebGPUBackend::destroyShaderProgram(RenderHandle& program) {
#if SALAMANDER_WEBGPU_NATIVE
    if (state && program) {
        auto it = state->programs.find(program);
        if (it != state->programs.end()) {
            releaseProgramResource(it->second);
            state->programs.erase(it);
        }
        for (auto locIt = state->uniformLocations.begin(); locIt != state->uniformLocations.end();) {
            if (locIt->second.program == program) {
                locIt = state->uniformLocations.erase(locIt);
            } else {
                ++locIt;
            }
        }
        if (state->currentProgram == program) {
            state->currentProgram = 0;
        }
    }
#endif
    program = 0;
}

void WebGPUBackend::useShaderProgram(RenderHandle program) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    if (!program || state->programs.find(program) == state->programs.end()) {
        state->currentProgram = 0;
        return;
    }
    state->currentProgram = program;
#else
    (void)program;
#endif
}

int WebGPUBackend::getShaderUniformLocation(RenderHandle program, const char* name) const {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state || !program || !name || !*name) return -1;
    auto progIt = state->programs.find(program);
    if (progIt == state->programs.end()) return -1;
    BackendState::ProgramResource& prog = progIt->second;
    auto existing = prog.uniformNameToLocation.find(name);
    if (existing != prog.uniformNameToLocation.end()) {
        return existing->second;
    }
    const int location = state->nextUniformLocation++;
    prog.uniformNameToLocation[name] = location;
    prog.uniformLocationToName[location] = name;
    BackendState::UniformLocationBinding binding{};
    binding.program = program;
    binding.name = name;
    state->uniformLocations[location] = std::move(binding);
    return location;
#else
    (void)program;
    (void)name;
    return -1;
#endif
}

void WebGPUBackend::setShaderUniformMat4(int location, const float* value16) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state || location < 0 || !value16) return;
    auto locIt = state->uniformLocations.find(location);
    if (locIt == state->uniformLocations.end()) return;
    auto progIt = state->programs.find(locIt->second.program);
    if (progIt == state->programs.end()) return;
    BackendState::ProgramResource& prog = progIt->second;
    const std::string lower = toLowerCase(locIt->second.name);
    if (lower == "m" || lower.find("model") != std::string::npos) {
        setMatrixFromArray(prog.uniforms.model, value16);
    } else if (lower == "v" || lower.find("view") != std::string::npos) {
        setMatrixFromArray(prog.uniforms.view, value16);
    } else if (lower == "p" || lower.find("proj") != std::string::npos) {
        setMatrixFromArray(prog.uniforms.projection, value16);
    } else if (lower.find("mvp") != std::string::npos) {
        setMatrixFromArray(prog.uniforms.projection, value16);
        prog.uniforms.view = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        };
        prog.uniforms.model = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        };
    }
#else
    (void)location;
    (void)value16;
#endif
}

void WebGPUBackend::setShaderUniformVec3(int location, const float* value3) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state || location < 0 || !value3) return;
    auto locIt = state->uniformLocations.find(location);
    if (locIt == state->uniformLocations.end()) return;
    auto progIt = state->programs.find(locIt->second.program);
    if (progIt == state->programs.end()) return;
    BackendState::ProgramResource& prog = progIt->second;
    const std::string lower = toLowerCase(locIt->second.name);
    if (lower.find("camerapos") != std::string::npos) {
        prog.uniforms.cameraPos[0] = value3[0];
        prog.uniforms.cameraPos[1] = value3[1];
        prog.uniforms.cameraPos[2] = value3[2];
        return;
    }
    if (lower.find("lightdir") != std::string::npos) {
        prog.uniforms.lightDir[0] = value3[0];
        prog.uniforms.lightDir[1] = value3[1];
        prog.uniforms.lightDir[2] = value3[2];
        return;
    }
    if (lower.find("ambientlight") != std::string::npos) {
        prog.uniforms.ambientLight[0] = value3[0];
        prog.uniforms.ambientLight[1] = value3[1];
        prog.uniforms.ambientLight[2] = value3[2];
        return;
    }
    if (lower.find("diffuselight") != std::string::npos) {
        prog.uniforms.diffuseLight[0] = value3[0];
        prog.uniforms.diffuseLight[1] = value3[1];
        prog.uniforms.diffuseLight[2] = value3[2];
        return;
    }
    if (lower.find("directionhintbasecolor") != std::string::npos) {
        prog.uniforms.topColor[0] = value3[0];
        prog.uniforms.topColor[1] = value3[1];
        prog.uniforms.topColor[2] = value3[2];
        return;
    }
    if (lower.find("directionhintaccentcolor") != std::string::npos) {
        prog.uniforms.bottomColor[0] = value3[0];
        prog.uniforms.bottomColor[1] = value3[1];
        prog.uniforms.bottomColor[2] = value3[2];
        return;
    }
    if (lower.find("chargedualtoneprimarycolor") != std::string::npos) {
        prog.uniforms.ambientLight[0] = value3[0];
        prog.uniforms.ambientLight[1] = value3[1];
        prog.uniforms.ambientLight[2] = value3[2];
        return;
    }
    if (lower.find("chargedualtonesecondarycolor") != std::string::npos) {
        prog.uniforms.diffuseLight[0] = value3[0];
        prog.uniforms.diffuseLight[1] = value3[1];
        prog.uniforms.diffuseLight[2] = value3[2];
        return;
    }
    if (lower == "st"
        || lower == "skytop"
        || lower.find("topcolor") != std::string::npos) {
        prog.uniforms.topColor[0] = value3[0];
        prog.uniforms.topColor[1] = value3[1];
        prog.uniforms.topColor[2] = value3[2];
        return;
    }
    if (lower == "sb"
        || lower == "skybottom"
        || lower.find("bottomcolor") != std::string::npos) {
        prog.uniforms.bottomColor[0] = value3[0];
        prog.uniforms.bottomColor[1] = value3[1];
        prog.uniforms.bottomColor[2] = value3[2];
        return;
    }
    if (lower == "c"
        || lower.find("color") != std::string::npos
        || lower.find("tint") != std::string::npos) {
        prog.uniforms.color[0] = value3[0];
        prog.uniforms.color[1] = value3[1];
        prog.uniforms.color[2] = value3[2];
    }
#else
    (void)location;
    (void)value3;
#endif
}

void WebGPUBackend::setShaderUniformVec2(int location, const float* value2) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state || location < 0 || !value2) return;
    auto locIt = state->uniformLocations.find(location);
    if (locIt == state->uniformLocations.end()) return;
    auto progIt = state->programs.find(locIt->second.program);
    if (progIt == state->programs.end()) return;
    BackendState::ProgramResource& prog = progIt->second;
    const std::string lower = toLowerCase(locIt->second.name);
    if (lower.find("lightpos") != std::string::npos) {
        prog.uniforms.lightPos[0] = value2[0];
        prog.uniforms.lightPos[1] = value2[1];
        return;
    }
    if (lower.find("uresolution") != std::string::npos) {
        prog.uniforms.mapCenter[0] = value2[0];
        prog.uniforms.mapCenter[1] = value2[1];
        return;
    }
    if (lower.find("ucenter") != std::string::npos) {
        prog.uniforms.lightPos[0] = value2[0];
        prog.uniforms.lightPos[1] = value2[1];
        return;
    }
    if (lower.find("ubuttonsize") != std::string::npos) {
        prog.uniforms.atlasTileSize[0] = value2[0];
        prog.uniforms.atlasTileSize[1] = value2[1];
        return;
    }
    if (lower.find("mapcenter") != std::string::npos) {
        prog.uniforms.mapCenter[0] = value2[0];
        prog.uniforms.mapCenter[1] = value2[1];
        return;
    }
    if (lower.find("atlastilesize") != std::string::npos) {
        prog.uniforms.atlasTileSize[0] = value2[0];
        prog.uniforms.atlasTileSize[1] = value2[1];
        return;
    }
    if (lower.find("atlastexturesize") != std::string::npos) {
        prog.uniforms.atlasTextureSize[0] = value2[0];
        prog.uniforms.atlasTextureSize[1] = value2[1];
    }
#else
    (void)location;
    (void)value2;
#endif
}

void WebGPUBackend::setShaderUniformFloat(int location, float value) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state || location < 0) return;
    auto locIt = state->uniformLocations.find(location);
    if (locIt == state->uniformLocations.end()) return;
    auto progIt = state->programs.find(locIt->second.program);
    if (progIt == state->programs.end()) return;
    BackendState::ProgramResource& prog = progIt->second;
    const std::string lower = toLowerCase(locIt->second.name);
    if (lower == "t" || lower.find("time") != std::string::npos) {
        prog.uniforms.time = value;
        return;
    }
    if (lower.find("emotionintensity") != std::string::npos) {
        prog.uniforms.mapZoom = value;
        return;
    }
    if (lower == "pulse") {
        prog.uniforms.exposure = value;
        return;
    }
    if (lower.find("chargeamount") != std::string::npos) {
        prog.uniforms.decay = value;
        return;
    }
    if (lower.find("underwatermix") != std::string::npos) {
        prog.uniforms.density = value;
        return;
    }
    if (lower.find("underwaterdepth") != std::string::npos) {
        prog.uniforms.weight = value;
        return;
    }
    if (lower.find("underwaterlineuv") != std::string::npos) {
        prog.uniforms.instanceScale = value;
        return;
    }
    if (lower.find("underwaterlinestrength") != std::string::npos) {
        prog.uniforms.blockDamageGrid = value;
        return;
    }
    if (lower.find("underwaterhazestrength") != std::string::npos) {
        prog.uniforms.leafDirectionalLightingIntensity = value;
        return;
    }
    if (lower.find("directionhintstrength") != std::string::npos) {
        prog.uniforms.waterCascadeBrightnessStrength = value;
        return;
    }
    if (lower.find("directionhintangle") != std::string::npos) {
        prog.uniforms.waterCascadeBrightnessSpeed = value;
        return;
    }
    if (lower.find("directionhintwidth") != std::string::npos) {
        prog.uniforms.waterCascadeBrightnessScale = value;
        return;
    }
    if (lower.find("aspectratio") != std::string::npos) {
        prog.uniforms.wallStoneUvJitterMinPixels = value;
        return;
    }
    if (lower.find("fireinvertmix") != std::string::npos) {
        prog.uniforms.waterReflectionPlaneY = value;
        return;
    }
    if (lower.find("chargedualtonespinspeed") != std::string::npos) {
        prog.uniforms.samples = value;
        return;
    }
    if (lower.find("fillamount") != std::string::npos) {
        prog.uniforms.mapZoom = value;
        return;
    }
    if (lower.find("upressoffset") != std::string::npos) {
        prog.uniforms.exposure = value;
        return;
    }
    if (lower.find("mapzoom") != std::string::npos) {
        prog.uniforms.mapZoom = value;
        return;
    }
    if (lower.find("exposure") != std::string::npos) {
        prog.uniforms.exposure = value;
        return;
    }
    if (lower.find("decay") != std::string::npos) {
        prog.uniforms.decay = value;
        return;
    }
    if (lower.find("density") != std::string::npos) {
        prog.uniforms.density = value;
        return;
    }
    if (lower.find("weight") != std::string::npos) {
        prog.uniforms.weight = value;
        return;
    }
    if (lower.find("instancescale") != std::string::npos) {
        prog.uniforms.instanceScale = value;
        return;
    }
    if (lower.find("blockdamagegrid") != std::string::npos) {
        prog.uniforms.blockDamageGrid = value;
        return;
    }
    if (lower.find("leafdirectionallightingintensity") != std::string::npos) {
        prog.uniforms.leafDirectionalLightingIntensity = value;
        return;
    }
    if (lower.find("watercascadebrightnessstrength") != std::string::npos) {
        prog.uniforms.waterCascadeBrightnessStrength = value;
        return;
    }
    if (lower.find("watercascadebrightnessspeed") != std::string::npos) {
        prog.uniforms.waterCascadeBrightnessSpeed = value;
        return;
    }
    if (lower.find("watercascadebrightnessscale") != std::string::npos) {
        prog.uniforms.waterCascadeBrightnessScale = value;
        return;
    }
    if (lower.find("waterreflectionplaney") != std::string::npos) {
        prog.uniforms.waterReflectionPlaneY = value;
        return;
    }
    if (lower.find("wallstoneuvjitterminpixels") != std::string::npos) {
        prog.uniforms.wallStoneUvJitterMinPixels = value;
        return;
    }
    if (lower.find("wallstoneuvjittermaxpixels") != std::string::npos) {
        prog.uniforms.wallStoneUvJitterMaxPixels = value;
        return;
    }
    if (lower.find("alpha") != std::string::npos
        || lower.find("opacity") != std::string::npos) {
        prog.uniforms.color[3] = value;
    }
#else
    (void)location;
    (void)value;
#endif
}

void WebGPUBackend::setShaderUniformInt(int location, int value) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state || location < 0) return;
    auto locIt = state->uniformLocations.find(location);
    if (locIt == state->uniformLocations.end()) return;
    auto progIt = state->programs.find(locIt->second.program);
    if (progIt == state->programs.end()) return;
    const std::string lower = toLowerCase(locIt->second.name);
    if (lower.find("samples") != std::string::npos) {
        progIt->second.uniforms.samples = static_cast<float>(std::max(1, value));
    }
    if (!shouldTrackUniformIntKey(lower)) {
        return;
    }
    auto& prog = progIt->second;
    auto it = prog.uniformIntValues.find(lower);
    if (it != prog.uniformIntValues.end()) {
        if (it->second == value) return;
        it->second = value;
    } else {
        prog.uniformIntValues.emplace(lower, value);
    }
    prog.uniformIntValuesVersion += 1;
#else
    (void)location;
    (void)value;
#endif
}

void WebGPUBackend::setShaderUniformInt3Array(int location, int count, const int* values) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state || location < 0 || count <= 0 || !values) return;
    auto locIt = state->uniformLocations.find(location);
    if (locIt == state->uniformLocations.end()) return;
    auto progIt = state->programs.find(locIt->second.program);
    if (progIt == state->programs.end()) return;
    const std::string lower = toLowerCase(locIt->second.name);
    if (lower.find("blockdamagecells") == std::string::npos) {
        return;
    }
    auto& dst = progIt->second.uniforms.blockDamageCells;
    dst.fill(0);
    const int emitCount = std::min(count, 64);
    for (int i = 0; i < emitCount; ++i) {
        const size_t srcBase = static_cast<size_t>(i) * 3u;
        const size_t dstBase = static_cast<size_t>(i) * 4u;
        dst[dstBase + 0u] = values[srcBase + 0u];
        dst[dstBase + 1u] = values[srcBase + 1u];
        dst[dstBase + 2u] = values[srcBase + 2u];
        dst[dstBase + 3u] = 0;
    }
#else
    (void)location;
    (void)count;
    (void)values;
#endif
}

void WebGPUBackend::setShaderUniformFloatArray(int location, int count, const float* values) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state || location < 0 || count <= 0 || !values) return;
    auto locIt = state->uniformLocations.find(location);
    if (locIt == state->uniformLocations.end()) return;
    auto progIt = state->programs.find(locIt->second.program);
    if (progIt == state->programs.end()) return;
    const std::string lower = toLowerCase(locIt->second.name);
    if (lower.find("blockdamageprogress") == std::string::npos) {
        return;
    }
    auto& dst = progIt->second.uniforms.blockDamageProgress;
    dst.fill(0.0f);
    const int emitCount = std::min(count, 64);
    for (int i = 0; i < emitCount; ++i) {
        dst[static_cast<size_t>(i)] = values[static_cast<size_t>(i)];
    }
#else
    (void)location;
    (void)count;
    (void)values;
#endif
}

bool WebGPUBackend::initialize(int width, int height, const char* title, PlatformWindowHandle& outWindow) {
    outWindow = nullptr;

    if (state) {
        shutdown(state->window);
    }

#if SALAMANDER_WEBGPU_NATIVE
    if (!PlatformInput::InitializeWindowing()) {
        std::cerr << "WebGPU backend failed to initialize windowing.\n";
        return false;
    }

    PlatformInput::SetNoApiContextHint();
    PlatformWindowHandle window = PlatformInput::CreateWindow(width, height, title);
    if (!window) {
        std::cerr << "WebGPU backend failed to create window.\n";
        PlatformInput::TerminateWindowing();
        return false;
    }

    state = new BackendState();
    state->window = window;

    auto failAndCleanup = [&](const std::string& message) {
        if (!message.empty()) {
            std::cerr << message << "\n";
        }
        if (state) {
#if SALAMANDER_WEBGPU_NATIVE
            releaseNativeResources(*state);
#endif
            delete state;
            state = nullptr;
        }
        PlatformInput::DestroyWindow(window);
        PlatformInput::TerminateWindowing();
        outWindow = nullptr;
        return false;
    };

    WGPUInstanceDescriptor instanceDesc{};
    state->instance = wgpuCreateInstance(&instanceDesc);
    if (!state->instance) {
        return failAndCleanup("WebGPU backend failed to create instance.");
    }

#if defined(__APPLE__)
    void* layerHandle = PlatformInput::GetOrCreateMetalLayerHandle(window);
    if (!layerHandle) {
        return failAndCleanup("WebGPU backend failed to get CAMetalLayer from window.");
    }

    WGPUSurfaceSourceMetalLayer metalLayerSource{};
    metalLayerSource.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
    metalLayerSource.layer = layerHandle;

    WGPUSurfaceDescriptor surfaceDesc{};
    surfaceDesc.nextInChain = &metalLayerSource.chain;
    surfaceDesc.label = stringView("Cardinal WebGPU Surface");
    state->surface = wgpuInstanceCreateSurface(state->instance, &surfaceDesc);
#else
    return failAndCleanup("WebGPU native surface creation is not implemented for this platform yet.");
#endif

    if (!state->surface) {
        return failAndCleanup("WebGPU backend failed to create surface.");
    }

    WGPURequestAdapterOptions adapterOptions{};
    adapterOptions.featureLevel = WGPUFeatureLevel_Core;
    adapterOptions.powerPreference = WGPUPowerPreference_HighPerformance;
    adapterOptions.compatibleSurface = state->surface;

    std::string requestError;
    if (!requestAdapterBlocking(state->instance, adapterOptions, state->adapter, requestError)) {
        return failAndCleanup(requestError);
    }

    WGPUDeviceDescriptor deviceDesc{};
    deviceDesc.label = stringView("Cardinal WebGPU Device");
    deviceDesc.defaultQueue.label = stringView("Cardinal WebGPU Queue");
    deviceDesc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    deviceDesc.deviceLostCallbackInfo.callback = onDeviceLost;
    deviceDesc.uncapturedErrorCallbackInfo.callback = onUncapturedError;
    if (!requestDeviceBlocking(state->instance, state->adapter, deviceDesc, state->device, requestError)) {
        return failAndCleanup(requestError);
    }

    state->queue = wgpuDeviceGetQueue(state->device);
    if (!state->queue) {
        return failAndCleanup("WebGPU backend failed to get default queue from device.");
    }

    WGPUSurfaceCapabilities capabilities{};
    if (wgpuSurfaceGetCapabilities(state->surface, state->adapter, &capabilities) != WGPUStatus_Success) {
        return failAndCleanup("WebGPU backend failed to query surface capabilities.");
    }

    state->surfaceFormat = chooseSurfaceFormat(capabilities);
    state->presentMode = choosePresentMode(capabilities);
    state->alphaMode = chooseAlphaMode(capabilities);

    const bool hasRenderAttachmentUsage = (capabilities.usages & WGPUTextureUsage_RenderAttachment) != 0;
    const bool hasFormat = capabilities.formatCount > 0 && capabilities.formats != nullptr;
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);

    if (!hasRenderAttachmentUsage || !hasFormat || state->surfaceFormat == WGPUTextureFormat_Undefined) {
        return failAndCleanup("WebGPU backend did not find a usable swapchain format/usage.");
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    PlatformInput::GetFramebufferSize(window, framebufferWidth, framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0) {
        framebufferWidth = width;
        framebufferHeight = height;
    }
    if (!configureSurface(*state, framebufferWidth, framebufferHeight)) {
        return failAndCleanup("WebGPU backend failed to configure surface.");
    }

    state->initialized = true;
    outWindow = window;
    return true;
#elif defined(SALAMANDER_ENABLE_WEBGPU)
    (void)width;
    (void)height;
    (void)title;
#if defined(__APPLE__) || defined(__linux__)
    using DynamicWGPUInstance = void*;
    using PFN_wgpuCreateInstance = DynamicWGPUInstance(*)(const void* descriptor);
    using PFN_wgpuInstanceRelease = void(*)(DynamicWGPUInstance instance);

#if defined(__APPLE__)
    const std::array<const char*, 8> candidateLibraries{{
        "libwebgpu.dylib",
        "libwgpu_native.dylib",
        "/opt/homebrew/lib/libwebgpu.dylib",
        "/opt/homebrew/lib/libwgpu_native.dylib",
        "/usr/local/lib/libwebgpu.dylib",
        "/usr/local/lib/libwgpu_native.dylib",
        "/opt/homebrew/opt/wgpu-native/lib/libwgpu_native.dylib",
        "/usr/local/opt/wgpu-native/lib/libwgpu_native.dylib",
    }};
#else
    const std::array<const char*, 8> candidateLibraries{{
        "libwebgpu.so",
        "libwgpu_native.so",
        "/usr/lib/libwebgpu.so",
        "/usr/lib/libwgpu_native.so",
        "/usr/local/lib/libwebgpu.so",
        "/usr/local/lib/libwgpu_native.so",
        "/opt/homebrew/opt/wgpu-native/lib/libwgpu_native.so",
        "/usr/local/opt/wgpu-native/lib/libwgpu_native.so",
    }};
#endif

    void* webgpuLib = nullptr;
    const char* loadedLibraryPath = nullptr;
    for (const char* candidate : candidateLibraries) {
        webgpuLib = dlopen(candidate, RTLD_NOW | RTLD_LOCAL);
        if (webgpuLib) {
            loadedLibraryPath = candidate;
            break;
        }
    }
    if (!webgpuLib) {
        std::cerr << "SALAMANDER_ENABLE_WEBGPU is set, but no WebGPU runtime library was found.\n";
        return false;
    }

    auto createInstance = reinterpret_cast<PFN_wgpuCreateInstance>(dlsym(webgpuLib, "wgpuCreateInstance"));
    if (!createInstance) {
        std::cerr << "Loaded WebGPU runtime (" << (loadedLibraryPath ? loadedLibraryPath : "<unknown>")
                  << "), but symbol wgpuCreateInstance was not found.\n";
        dlclose(webgpuLib);
        return false;
    }

    DynamicWGPUInstance instance = createInstance(nullptr);
    if (!instance) {
        std::cerr << "WebGPU runtime loaded (" << (loadedLibraryPath ? loadedLibraryPath : "<unknown>")
                  << "), but wgpuCreateInstance returned null.\n";
        dlclose(webgpuLib);
        return false;
    }

    auto releaseInstance = reinterpret_cast<PFN_wgpuInstanceRelease>(dlsym(webgpuLib, "wgpuInstanceRelease"));
    if (releaseInstance) {
        releaseInstance(instance);
    }
    dlclose(webgpuLib);
    std::cerr << "WebGPU dynamic runtime probe succeeded ("
              << (loadedLibraryPath ? loadedLibraryPath : "<unknown>")
              << "), but render path requires SALAMANDER_LINK_WEBGPU=1 for now.\n";
    return false;
#else
    std::cerr << "SALAMANDER_ENABLE_WEBGPU is set, but dynamic WebGPU probing is not implemented on this platform.\n";
    return false;
#endif
#else
    (void)width;
    (void)height;
    (void)title;
    std::cerr << "WebGPU backend is not enabled in this build.\n";
    return false;
#endif
}

void WebGPUBackend::beginFrame() {
    if (!state) return;
    state->clearRequested = false;
#if SALAMANDER_WEBGPU_NATIVE
    state->drawCommands.clear();
    state->offscreenScopes.clear();
#endif
}

void WebGPUBackend::endFrame(PlatformWindowHandle window) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state || !state->initialized || !state->surface || !state->device || !state->queue) {
        return;
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    PlatformInput::GetFramebufferSize(window ? window : state->window, framebufferWidth, framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0) {
        return;
    }

    if (!state->surfaceConfigured
        || framebufferWidth != state->framebufferWidth
        || framebufferHeight != state->framebufferHeight) {
        if (!configureSurface(*state, framebufferWidth, framebufferHeight)) {
            std::cerr << "WebGPU backend failed to reconfigure surface.\n";
            return;
        }
    }
    if (!ensureDepthAttachment(*state, static_cast<uint32_t>(framebufferWidth), static_cast<uint32_t>(framebufferHeight))) {
        std::cerr << "WebGPU backend failed to create depth attachment.\n";
        return;
    }

    WGPUSurfaceTexture surfaceTexture{};
    wgpuSurfaceGetCurrentTexture(state->surface, &surfaceTexture);
    const bool textureReady = surfaceTexture.texture
        && (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal
            || surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal);

    if (!textureReady) {
        if (surfaceTexture.texture) {
            wgpuTextureRelease(surfaceTexture.texture);
            surfaceTexture.texture = nullptr;
        }
        if (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Outdated
            || surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Lost) {
            configureSurface(*state, framebufferWidth, framebufferHeight);
            return;
        }
        std::cerr << "WebGPU failed to acquire swapchain texture (status="
                  << static_cast<int>(surfaceTexture.status) << ").\n";
        return;
    }

    WGPUTextureView backbufferView = wgpuTextureCreateView(surfaceTexture.texture, nullptr);
    if (!backbufferView) {
        wgpuTextureRelease(surfaceTexture.texture);
        return;
    }

    WGPUCommandEncoderDescriptor encoderDesc{};
    encoderDesc.label = stringView("Cardinal Clear Encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(state->device, &encoderDesc);
    if (!encoder) {
        wgpuTextureViewRelease(backbufferView);
        wgpuTextureRelease(surfaceTexture.texture);
        return;
    }

    const WGPUColor clearColorValue{
        state->clearColor[0],
        state->clearColor[1],
        state->clearColor[2],
        state->clearColor[3],
    };
    const bool passStatsEnabled = webgpuPassStatsEnabled();
    struct PassStats {
        uint64_t frames = 0;
        uint64_t queuedDrawCommands = 0;
        uint64_t submittedDrawCalls = 0;
        uint64_t submittedDefaultDrawCalls = 0;
        uint64_t submittedOffscreenDrawCalls = 0;
        uint64_t renderPasses = 0;
        uint64_t defaultPasses = 0;
        uint64_t offscreenPasses = 0;
        uint64_t clearedPasses = 0;
        uint64_t clearedDefaultPasses = 0;
        uint64_t clearedOffscreenPasses = 0;
        uint64_t boundsAdjustedDraws = 0;
        uint64_t boundsDroppedDraws = 0;
    };
    PassStats frameStats{};
    if (passStatsEnabled) {
        frameStats.frames = 1;
        frameStats.queuedDrawCommands = static_cast<uint64_t>(state->drawCommands.size());
    }

    WGPURenderPassEncoder pass = nullptr;
    RenderHandle activeFramebuffer = 0;
    int activeTargetWidth = framebufferWidth;
    int activeTargetHeight = framebufferHeight;
    bool defaultPassOpened = false;

    auto closeActivePass = [&]() {
        if (!pass) return;
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        pass = nullptr;
    };

    auto beginPass = [&](RenderHandle targetFramebuffer,
                         WGPUTextureView colorView,
                         WGPUTextureView depthView,
                         bool clearColor,
                         bool clearDepth,
                         const WGPUColor& passClearColor,
                         int targetWidth,
                         int targetHeight) -> bool {
        closeActivePass();

        WGPURenderPassColorAttachment colorAttachment{};
        colorAttachment.view = colorView;
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachment.loadOp = clearColor ? WGPULoadOp_Clear : WGPULoadOp_Load;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = passClearColor;

        WGPURenderPassDepthStencilAttachment depthAttachment{};
        if (depthView) {
            depthAttachment.view = depthView;
            depthAttachment.depthLoadOp = clearDepth ? WGPULoadOp_Clear : WGPULoadOp_Load;
            depthAttachment.depthStoreOp = WGPUStoreOp_Store;
            depthAttachment.depthClearValue = 1.0f;
            depthAttachment.depthReadOnly = false;
            depthAttachment.stencilLoadOp = WGPULoadOp_Load;
            depthAttachment.stencilStoreOp = WGPUStoreOp_Store;
            depthAttachment.stencilReadOnly = true;
        }

        WGPURenderPassDescriptor passDesc{};
        passDesc.label = stringView("Cardinal Render Pass");
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        passDesc.depthStencilAttachment = depthView ? &depthAttachment : nullptr;

        pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        if (!pass) {
            return false;
        }
        if (passStatsEnabled) {
            ++frameStats.renderPasses;
            if (targetFramebuffer == 0) ++frameStats.defaultPasses;
            else ++frameStats.offscreenPasses;
            if (clearColor || clearDepth) {
                ++frameStats.clearedPasses;
                if (targetFramebuffer == 0) ++frameStats.clearedDefaultPasses;
                else ++frameStats.clearedOffscreenPasses;
            }
        }
        activeFramebuffer = targetFramebuffer;
        activeTargetWidth = targetWidth;
        activeTargetHeight = targetHeight;
        return true;
    };

    std::vector<float> drawPositions;
    const uint64_t uniformAlignment = 256u;
    const uint64_t uniformBlockSize = static_cast<uint64_t>(sizeof(FallbackUniformBlock));
    const uint64_t uniformStride = ((uniformBlockSize + uniformAlignment - 1u) / uniformAlignment) * uniformAlignment;
    const uint64_t uniformEntryCount = std::max<uint64_t>(1u, static_cast<uint64_t>(state->drawCommands.size()));
    const uint64_t frameUniformBufferSize = uniformStride * uniformEntryCount;
    WGPUBuffer frameUniformBuffer = nullptr;
    if (frameUniformBufferSize > 0u) {
        WGPUBufferDescriptor uniformBufferDesc{};
        uniformBufferDesc.label = stringView("Cardinal Frame Uniform Buffer");
        uniformBufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uniformBufferDesc.size = frameUniformBufferSize;
        uniformBufferDesc.mappedAtCreation = false;
        frameUniformBuffer = wgpuDeviceCreateBuffer(state->device, &uniformBufferDesc);
    }
    if (!frameUniformBuffer) {
        wgpuCommandEncoderRelease(encoder);
        wgpuTextureViewRelease(backbufferView);
        wgpuTextureRelease(surfaceTexture.texture);
        return;
    }

    auto releaseFrameUniformBuffer = [&]() {
        if (frameUniformBuffer) {
            wgpuBufferRelease(frameUniformBuffer);
            frameUniformBuffer = nullptr;
        }
    };

    RenderHandle lastBindGroupProgram = 0;
    std::array<RenderHandle, 32> lastBindGroupTextureUnits{};
    bool hasLastBindGroupKey = false;
    uint64_t uniformWriteIndex = 0u;
    for (const BackendState::DrawCommand& cmd : state->drawCommands) {
        auto progIt = state->programs.find(cmd.program);
        if (progIt == state->programs.end()) {
            continue;
        }
        auto vaoIt = state->vertexArrayResources.find(cmd.vertexArray);
        if (vaoIt == state->vertexArrayResources.end()) {
            continue;
        }

        WGPUTextureView targetColorView = backbufferView;
        WGPUTextureView targetDepthView = state->depthTextureView;
        WGPUTextureFormat targetColorFormat = state->surfaceFormat;
        int targetWidth = framebufferWidth;
        int targetHeight = framebufferHeight;
        if (cmd.framebuffer != 0) {
            const auto fbIt = state->framebuffers.find(cmd.framebuffer);
            if (fbIt == state->framebuffers.end()) {
                continue;
            }
            const auto texIt = state->textures.find(fbIt->second.textureHandle);
            if (texIt == state->textures.end() || !texIt->second.defaultView) {
                continue;
            }
            targetColorView = texIt->second.defaultView;
            targetDepthView = fbIt->second.depthTextureView;
            targetColorFormat = texIt->second.format;
            targetWidth = static_cast<int>(fbIt->second.width);
            targetHeight = static_cast<int>(fbIt->second.height);
        }

        const bool hasDepth = (targetDepthView != nullptr);
        BackendState::ProgramResource& program = progIt->second;
        program.uniforms = cmd.uniforms;
        const std::unordered_map<std::string, int>* cmdUniformIntValues =
            cmd.uniformIntValues ? cmd.uniformIntValues.get() : nullptr;
        state->boundTextureUnits = cmd.textureUnits;
        const BackendState::VertexArrayResource& vao = vaoIt->second;
        if (webgpuFaceDebugEnabled()) {
            static int drawDebugBudget = 256;
            if (drawDebugBudget > 0) {
                int faceTypeDbg = 0;
                int behaviorTypeDbg = 0;
                int sectionLodDbg = 0;
                int atlasEnabledDbg = 0;
                auto findInt = [&](const char* key, int& outValue) {
                    if (!cmdUniformIntValues) return;
                    const auto it = cmdUniformIntValues->find(key);
                    if (it != cmdUniformIntValues->end()) outValue = it->second;
                };
                findInt("facetype", faceTypeDbg);
                findInt("behaviortype", behaviorTypeDbg);
                findInt("sectionlod", sectionLodDbg);
                findInt("atlasenabled", atlasEnabledDbg);
                std::cerr << "[WebGPU][FaceDbg] prog=" << cmd.program
                          << " vao=" << cmd.vertexArray
                          << " fb=" << cmd.framebuffer
                          << " first/count/inst=" << cmd.first << "/" << cmd.count << "/" << cmd.instanceCount
                          << " behaviorType=" << behaviorTypeDbg
                          << " faceType=" << faceTypeDbg
                          << " sectionLod=" << sectionLodDbg
                          << " atlasEnabled=" << atlasEnabledDbg
                          << " vSnapshot=" << cmd.vertexBufferSnapshot.size()
                          << " iSnapshot=" << cmd.instanceBufferSnapshot.size()
                          << std::endl;
                --drawDebugBudget;
            }
        }

        const uint64_t uniformOffset = uniformStride * uniformWriteIndex++;
        if (uniformOffset > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
            continue;
        }
        const uint32_t uniformDynamicOffset = static_cast<uint32_t>(uniformOffset);

        WGPURenderPipeline pipeline = nullptr;
        if (!ensureProgramPipeline(*state,
                                   program,
                                   &vao,
                                   cmd.primitive,
                                   cmd.renderState,
                                   targetColorFormat,
                                   hasDepth,
                                   pipeline)) {
            continue;
        }
        if (!pipeline || !program.bindGroup) {
            continue;
        }

        const bool switchingTarget = !pass || (activeFramebuffer != cmd.framebuffer);
        if (switchingTarget) {
            bool clearColor = false;
            bool clearDepth = false;
            WGPUColor passClearColor = clearColorValue;
            if (cmd.framebuffer == 0 && !defaultPassOpened) {
                clearColor = state->clearRequested;
                clearDepth = state->clearRequested && state->clearDepthRequested;
            } else if (cmd.framebuffer != 0 && cmd.passClearColorRequested) {
                clearColor = true;
                clearDepth = cmd.passClearDepthRequested;
                passClearColor = {
                    static_cast<double>(cmd.passClearColor[0]),
                    static_cast<double>(cmd.passClearColor[1]),
                    static_cast<double>(cmd.passClearColor[2]),
                    static_cast<double>(cmd.passClearColor[3]),
                };
            }
            if (!beginPass(cmd.framebuffer,
                           targetColorView,
                           targetDepthView,
                           clearColor,
                           clearDepth,
                           passClearColor,
                           targetWidth,
                           targetHeight)) {
                continue;
            }
            if (cmd.framebuffer == 0) {
                defaultPassOpened = true;
            }
        }

        updateProgramUniformBuffer(*state, program, frameUniformBuffer, uniformOffset, cmdUniformIntValues);
        const bool bindGroupNeedsRefresh =
            !hasLastBindGroupKey
            || lastBindGroupProgram != cmd.program
            || lastBindGroupTextureUnits != cmd.textureUnits;
        if (bindGroupNeedsRefresh) {
            if (!refreshProgramBindGroup(*state, program, frameUniformBuffer)) {
                continue;
            }
            hasLastBindGroupKey = true;
            lastBindGroupProgram = cmd.program;
            lastBindGroupTextureUnits = cmd.textureUnits;
        }

        auto vertexBufferIt = state->bufferResources.find(vao.vertexBuffer);
        if (program.usingFallbackShaders) {
            const VertexAttribLayout* positionAttrib = resolvePositionAttrib(vao);
            if (!positionAttrib || !vao.vertexBuffer || vertexBufferIt == state->bufferResources.end()) {
                continue;
            }
            if (!extractPositions(vertexBufferIt->second, *positionAttrib, cmd.first, cmd.count, drawPositions)) {
                continue;
            }

            BackendState::BufferResource drawBuffer{};
            drawBuffer.cpuBytes.resize(drawPositions.size() * sizeof(float));
            std::memcpy(drawBuffer.cpuBytes.data(), drawPositions.data(), drawBuffer.cpuBytes.size());
            drawBuffer.dirty = true;
            if (!ensureGpuVertexBuffer(*state, drawBuffer)) {
                releaseBufferResource(drawBuffer);
                continue;
            }

            const uint32_t vertexCount = static_cast<uint32_t>(drawPositions.size() / 3u);
            if (vertexCount == 0u) {
                releaseBufferResource(drawBuffer);
                continue;
            }

            applyScissorState(pass, cmd.renderState, activeTargetWidth, activeTargetHeight);
            applyBlendState(pass, cmd.renderState);
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, program.bindGroup, 1, &uniformDynamicOffset);
            wgpuRenderPassEncoderSetVertexBuffer(
                pass,
                0,
                drawBuffer.gpuBuffer,
                0,
                static_cast<uint64_t>(drawBuffer.gpuSize)
            );
            wgpuRenderPassEncoderDraw(
                pass,
                vertexCount,
                static_cast<uint32_t>(std::max(1, cmd.instanceCount)),
                0,
                0
            );
            if (passStatsEnabled) {
                ++frameStats.submittedDrawCalls;
                if (cmd.framebuffer == 0) ++frameStats.submittedDefaultDrawCalls;
                else ++frameStats.submittedOffscreenDrawCalls;
            }

            releaseBufferResource(drawBuffer);
            continue;
        }

        if (vao.vertexAttribs.empty()) {
            if (!vao.instanceAttribs.empty()) {
                continue;
            }
            if (cmd.count <= 0) {
                continue;
            }

            const uint64_t requestedFirstVertex = static_cast<uint64_t>(std::max(0, cmd.first));
            const uint64_t requestedVertexCount = static_cast<uint64_t>(std::max(0, cmd.count));
            const uint64_t requestedInstanceCount = static_cast<uint64_t>(std::max(1, cmd.instanceCount));
            if (requestedVertexCount == 0u) {
                continue;
            }

            const uint64_t safeFirstVertex64 = std::min(
                requestedFirstVertex,
                static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
            );
            const uint64_t safeVertexCount64 = std::min(
                requestedVertexCount,
                static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
            );
            const uint64_t safeInstanceCount64 = std::min(
                requestedInstanceCount,
                static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
            );

            if (safeVertexCount64 == 0u || safeInstanceCount64 == 0u) {
                if (passStatsEnabled) ++frameStats.boundsDroppedDraws;
                continue;
            }
            if (passStatsEnabled
                && (safeFirstVertex64 != requestedFirstVertex
                    || safeVertexCount64 != requestedVertexCount
                    || safeInstanceCount64 != requestedInstanceCount)) {
                ++frameStats.boundsAdjustedDraws;
            }

            applyScissorState(pass, cmd.renderState, activeTargetWidth, activeTargetHeight);
            applyBlendState(pass, cmd.renderState);
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, program.bindGroup, 1, &uniformDynamicOffset);
            wgpuRenderPassEncoderDraw(
                pass,
                static_cast<uint32_t>(safeVertexCount64),
                static_cast<uint32_t>(safeInstanceCount64),
                static_cast<uint32_t>(safeFirstVertex64),
                0
            );
            if (passStatsEnabled) {
                ++frameStats.submittedDrawCalls;
                if (cmd.framebuffer == 0) ++frameStats.submittedDefaultDrawCalls;
                else ++frameStats.submittedOffscreenDrawCalls;
            }
            continue;
        }

        if (!vao.vertexBuffer || vertexBufferIt == state->bufferResources.end()) {
            continue;
        }

        BackendState::BufferResource vertexSnapshotBuffer{};
        BackendState::BufferResource instanceSnapshotBuffer{};
        auto releaseSnapshotBuffers = [&]() {
            releaseBufferResource(vertexSnapshotBuffer);
            releaseBufferResource(instanceSnapshotBuffer);
        };

        BackendState::BufferResource* vertexBufferResource = nullptr;
        if (!cmd.vertexBufferSnapshot.empty()) {
            vertexSnapshotBuffer.cpuBytes = cmd.vertexBufferSnapshot;
            vertexSnapshotBuffer.dynamic = true;
            vertexSnapshotBuffer.dirty = true;
            if (!ensureGpuVertexBuffer(*state, vertexSnapshotBuffer)) {
                releaseSnapshotBuffers();
                continue;
            }
            vertexBufferResource = &vertexSnapshotBuffer;
        } else {
            if (!ensureGpuVertexBuffer(*state, vertexBufferIt->second)) {
                continue;
            }
            vertexBufferResource = &vertexBufferIt->second;
        }

        BackendState::BufferResource* instanceBufferResource = nullptr;
        if (vao.instanceBuffer && !vao.instanceAttribs.empty()) {
            if (!cmd.instanceBufferSnapshot.empty()) {
                instanceSnapshotBuffer.cpuBytes = cmd.instanceBufferSnapshot;
                instanceSnapshotBuffer.dynamic = true;
                instanceSnapshotBuffer.dirty = true;
                if (!ensureGpuVertexBuffer(*state, instanceSnapshotBuffer)) {
                    releaseSnapshotBuffers();
                    continue;
                }
                instanceBufferResource = &instanceSnapshotBuffer;
            } else {
                auto instanceBufferIt = state->bufferResources.find(vao.instanceBuffer);
                if (instanceBufferIt == state->bufferResources.end()) {
                    releaseSnapshotBuffers();
                    continue;
                }
                if (!ensureGpuVertexBuffer(*state, instanceBufferIt->second)) {
                    releaseSnapshotBuffers();
                    continue;
                }
                instanceBufferResource = &instanceBufferIt->second;
            }
        }

        if (cmd.count <= 0) {
            releaseSnapshotBuffers();
            continue;
        }
        const uint64_t vertexStride = resolveAttribStride(vao.vertexAttribs);
        if (vertexStride == 0u) {
            if (passStatsEnabled) ++frameStats.boundsDroppedDraws;
            releaseSnapshotBuffers();
            continue;
        }
        const uint64_t maxVertices = static_cast<uint64_t>(vertexBufferResource->gpuSize) / vertexStride;
        const uint64_t requestedFirstVertex = static_cast<uint64_t>(std::max(0, cmd.first));
        if (requestedFirstVertex >= maxVertices) {
            if (passStatsEnabled) ++frameStats.boundsDroppedDraws;
            releaseSnapshotBuffers();
            continue;
        }

        const uint64_t requestedVertexCount = static_cast<uint64_t>(std::max(0, cmd.count));
        uint64_t safeVertexCount64 = std::min(requestedVertexCount, maxVertices - requestedFirstVertex);
        if (safeVertexCount64 == 0u) {
            if (passStatsEnabled) ++frameStats.boundsDroppedDraws;
            releaseSnapshotBuffers();
            continue;
        }
        safeVertexCount64 = std::min(safeVertexCount64, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()));
        const uint32_t firstVertex = static_cast<uint32_t>(
            std::min(requestedFirstVertex, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
        );

        const uint64_t requestedInstanceCount = static_cast<uint64_t>(std::max(1, cmd.instanceCount));
        uint64_t safeInstanceCount64 = requestedInstanceCount;
        if (instanceBufferResource) {
            const uint64_t instanceStride = resolveAttribStride(vao.instanceAttribs);
            if (instanceStride == 0u) {
                if (passStatsEnabled) ++frameStats.boundsDroppedDraws;
                releaseSnapshotBuffers();
                continue;
            }
            const uint64_t maxInstances = static_cast<uint64_t>(instanceBufferResource->gpuSize) / instanceStride;
            if (maxInstances == 0u) {
                if (passStatsEnabled) ++frameStats.boundsDroppedDraws;
                releaseSnapshotBuffers();
                continue;
            }
            safeInstanceCount64 = std::min(safeInstanceCount64, maxInstances);
        }
        if (safeInstanceCount64 == 0u) {
            if (passStatsEnabled) ++frameStats.boundsDroppedDraws;
            releaseSnapshotBuffers();
            continue;
        }
        safeInstanceCount64 = std::min(safeInstanceCount64, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()));
        const uint32_t vertexCount = static_cast<uint32_t>(safeVertexCount64);
        const uint32_t instanceCount = static_cast<uint32_t>(safeInstanceCount64);
        if (passStatsEnabled
            && (requestedVertexCount != safeVertexCount64 || requestedInstanceCount != safeInstanceCount64)) {
            ++frameStats.boundsAdjustedDraws;
        }

        applyScissorState(pass, cmd.renderState, activeTargetWidth, activeTargetHeight);
        applyBlendState(pass, cmd.renderState);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, program.bindGroup, 1, &uniformDynamicOffset);
        wgpuRenderPassEncoderSetVertexBuffer(
            pass,
            0,
            vertexBufferResource->gpuBuffer,
            0,
            static_cast<uint64_t>(vertexBufferResource->gpuSize)
        );
        if (instanceBufferResource) {
            wgpuRenderPassEncoderSetVertexBuffer(
                pass,
                1,
                instanceBufferResource->gpuBuffer,
                0,
                static_cast<uint64_t>(instanceBufferResource->gpuSize)
            );
        }
        wgpuRenderPassEncoderDraw(pass, vertexCount, instanceCount, firstVertex, 0);
        if (passStatsEnabled) {
            ++frameStats.submittedDrawCalls;
            if (cmd.framebuffer == 0) ++frameStats.submittedDefaultDrawCalls;
            else ++frameStats.submittedOffscreenDrawCalls;
        }
        releaseSnapshotBuffers();
    }

    if (!defaultPassOpened && state->clearRequested) {
        if (beginPass(0,
                      backbufferView,
                      state->depthTextureView,
                      true,
                      state->clearDepthRequested,
                      clearColorValue,
                      framebufferWidth,
                      framebufferHeight)) {
            defaultPassOpened = true;
        }
    }
    closeActivePass();

    if (passStatsEnabled) {
        static PassStats totals{};
        static auto lastReportAt = std::chrono::steady_clock::now();

        totals.frames += frameStats.frames;
        totals.queuedDrawCommands += frameStats.queuedDrawCommands;
        totals.submittedDrawCalls += frameStats.submittedDrawCalls;
        totals.submittedDefaultDrawCalls += frameStats.submittedDefaultDrawCalls;
        totals.submittedOffscreenDrawCalls += frameStats.submittedOffscreenDrawCalls;
        totals.renderPasses += frameStats.renderPasses;
        totals.defaultPasses += frameStats.defaultPasses;
        totals.offscreenPasses += frameStats.offscreenPasses;
        totals.clearedPasses += frameStats.clearedPasses;
        totals.clearedDefaultPasses += frameStats.clearedDefaultPasses;
        totals.clearedOffscreenPasses += frameStats.clearedOffscreenPasses;
        totals.boundsAdjustedDraws += frameStats.boundsAdjustedDraws;
        totals.boundsDroppedDraws += frameStats.boundsDroppedDraws;

        const auto now = std::chrono::steady_clock::now();
        if (now - lastReportAt >= std::chrono::seconds(1)) {
            std::cerr
                << "[WebGPU][PassStats] frames=" << totals.frames
                << " queued=" << totals.queuedDrawCommands
                << " submitted=" << totals.submittedDrawCalls
                << " submitted(default/offscreen)="
                << totals.submittedDefaultDrawCalls << "/" << totals.submittedOffscreenDrawCalls
                << " passes=" << totals.renderPasses
                << " passes(default/offscreen)=" << totals.defaultPasses << "/" << totals.offscreenPasses
                << " clears=" << totals.clearedPasses
                << " clears(default/offscreen)="
                << totals.clearedDefaultPasses << "/" << totals.clearedOffscreenPasses
                << " bounds(adjusted/dropped)="
                << totals.boundsAdjustedDraws << "/" << totals.boundsDroppedDraws
                << " openScopes=" << state->offscreenScopes.size()
                << std::endl;
            totals = {};
            lastReportAt = now;
        }
    }

    state->drawCommands.clear();

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuCommandEncoderRelease(encoder);
    if (!commandBuffer) {
        releaseFrameUniformBuffer();
        wgpuTextureViewRelease(backbufferView);
        wgpuTextureRelease(surfaceTexture.texture);
        return;
    }

    wgpuQueueSubmit(state->queue, 1, &commandBuffer);
    const WGPUStatus presentStatus = wgpuSurfacePresent(state->surface);
    if (presentStatus != WGPUStatus_Success) {
        std::cerr << "WebGPU present failed (status=" << static_cast<int>(presentStatus) << ").\n";
    }

    wgpuCommandBufferRelease(commandBuffer);
    releaseFrameUniformBuffer();
    wgpuTextureViewRelease(backbufferView);
    wgpuTextureRelease(surfaceTexture.texture);

    if (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        configureSurface(*state, framebufferWidth, framebufferHeight);
    }
#else
    (void)window;
#endif
}

void WebGPUBackend::shutdown(PlatformWindowHandle window) {
    if (!state) {
        if (window) {
            PlatformInput::DestroyWindow(window);
            PlatformInput::TerminateWindowing();
        }
        return;
    }

#if SALAMANDER_WEBGPU_NATIVE
    releaseNativeResources(*state);
#endif

    PlatformWindowHandle targetWindow = window ? window : state->window;
    PlatformInput::DestroyWindow(targetWindow);
    PlatformInput::TerminateWindowing();

    delete state;
    state = nullptr;
}

void WebGPUBackend::onFramebufferResize(int width, int height) {
    if (!state) return;
    state->framebufferWidth = width;
    state->framebufferHeight = height;
#if SALAMANDER_WEBGPU_NATIVE
    if (state->initialized && width > 0 && height > 0) {
        configureSurface(*state, width, height);
    }
#endif
}

void WebGPUBackend::clearDefaultFramebuffer(float r, float g, float b, float a, bool clearDepth) {
    if (!state) {
        state = new BackendState();
    }
    state->clearColor[0] = r;
    state->clearColor[1] = g;
    state->clearColor[2] = b;
    state->clearColor[3] = a;
    state->clearRequested = true;
#if SALAMANDER_WEBGPU_NATIVE
    state->clearDepthRequested = clearDepth;
#else
    (void)clearDepth;
#endif
}

void WebGPUBackend::getFramebufferSize(PlatformWindowHandle window, int& width, int& height) const {
    width = 0;
    height = 0;
    if (window) {
        PlatformInput::GetFramebufferSize(window, width, height);
        return;
    }
    if (!state) return;
    width = state->framebufferWidth;
    height = state->framebufferHeight;
}

bool WebGPUBackend::createColorRenderTarget(int width, int height, RenderHandle& outFramebuffer, RenderHandle& outTexture) {
    outFramebuffer = 0;
    outTexture = 0;
    if (width <= 0 || height <= 0 || !state) {
        return false;
    }
#if SALAMANDER_WEBGPU_NATIVE
    if (!state->device) {
        return false;
    }
    RenderHandle textureHandle = 0;
    const WGPUTextureUsage usage = WGPUTextureUsage_RenderAttachment
        | WGPUTextureUsage_TextureBinding
        | WGPUTextureUsage_CopySrc
        | WGPUTextureUsage_CopyDst;
    if (!createOrReplaceTexture2D(*state,
                                  0,
                                  static_cast<uint32_t>(width),
                                  static_cast<uint32_t>(height),
                                  WGPUTextureFormat_RGBA8Unorm,
                                  usage,
                                  "Cardinal Offscreen Color Target",
                                  textureHandle)) {
        return false;
    }

    RenderHandle framebufferHandle = allocateHandle(*state);
    BackendState::FramebufferResource framebufferResource{};
    framebufferResource.textureHandle = textureHandle;
    framebufferResource.width = static_cast<uint32_t>(width);
    framebufferResource.height = static_cast<uint32_t>(height);

    WGPUTextureDescriptor depthDesc{};
    depthDesc.label = stringView("Cardinal Offscreen Depth");
    depthDesc.size.width = framebufferResource.width;
    depthDesc.size.height = framebufferResource.height;
    depthDesc.size.depthOrArrayLayers = 1;
    depthDesc.mipLevelCount = 1;
    depthDesc.sampleCount = 1;
    depthDesc.dimension = WGPUTextureDimension_2D;
    depthDesc.format = state->depthFormat;
    depthDesc.usage = WGPUTextureUsage_RenderAttachment;
    framebufferResource.depthTexture = wgpuDeviceCreateTexture(state->device, &depthDesc);
    if (!framebufferResource.depthTexture) {
        destroyTexture(textureHandle);
        return false;
    }
    framebufferResource.depthTextureView = wgpuTextureCreateView(framebufferResource.depthTexture, nullptr);
    if (!framebufferResource.depthTextureView) {
        releaseFramebufferResource(framebufferResource);
        destroyTexture(textureHandle);
        return false;
    }
    state->framebuffers[framebufferHandle] = framebufferResource;

    outFramebuffer = framebufferHandle;
    outTexture = textureHandle;
    return true;
#else
    return false;
#endif
}

void WebGPUBackend::destroyColorRenderTarget(RenderHandle& framebuffer, RenderHandle& texture) {
    if (!state) {
        framebuffer = 0;
        texture = 0;
        return;
    }
#if SALAMANDER_WEBGPU_NATIVE
    if (framebuffer) {
        auto fbIt = state->framebuffers.find(framebuffer);
        if (fbIt != state->framebuffers.end()) {
            if (!texture) texture = fbIt->second.textureHandle;
            releaseFramebufferResource(fbIt->second);
            state->framebuffers.erase(fbIt);
        }
        state->offscreenScopes.erase(std::remove_if(state->offscreenScopes.begin(),
                                                    state->offscreenScopes.end(),
                                                    [&](const BackendState::OffscreenScope& scope) {
                                                        return scope.framebuffer == framebuffer;
                                                    }),
                                     state->offscreenScopes.end());
    }
#endif
    destroyTexture(texture);
    framebuffer = 0;
}

void WebGPUBackend::beginOffscreenColorPass(RenderHandle framebuffer, int width, int height, float r, float g, float b, float a) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state || !state->device || !framebuffer || width <= 0 || height <= 0) {
        return;
    }

    const auto fbIt = state->framebuffers.find(framebuffer);
    if (fbIt == state->framebuffers.end()) return;
    const auto texIt = state->textures.find(fbIt->second.textureHandle);
    if (texIt == state->textures.end() || !texIt->second.defaultView || !fbIt->second.depthTextureView) return;

    BackendState::OffscreenScope scope{};
    scope.framebuffer = framebuffer;
    scope.clearColor = {r, g, b, a};
    scope.pendingClear = true;
    state->offscreenScopes.push_back(scope);
#else
    (void)framebuffer;
    (void)width;
    (void)height;
    (void)r;
    (void)g;
    (void)b;
    (void)a;
#endif
}

void WebGPUBackend::endOffscreenColorPass() {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state || state->offscreenScopes.empty()) {
        return;
    }
    const BackendState::OffscreenScope scope = state->offscreenScopes.back();
    state->offscreenScopes.pop_back();

    if (!scope.pendingClear || !state->device || !state->queue) {
        return;
    }

    const auto fbIt = state->framebuffers.find(scope.framebuffer);
    if (fbIt == state->framebuffers.end()) return;
    const auto texIt = state->textures.find(fbIt->second.textureHandle);
    if (texIt == state->textures.end() || !texIt->second.defaultView || !fbIt->second.depthTextureView) return;

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(state->device, nullptr);
    if (!encoder) return;

    WGPURenderPassColorAttachment colorAttachment{};
    colorAttachment.view = texIt->second.defaultView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {
        static_cast<double>(scope.clearColor[0]),
        static_cast<double>(scope.clearColor[1]),
        static_cast<double>(scope.clearColor[2]),
        static_cast<double>(scope.clearColor[3]),
    };

    WGPURenderPassDepthStencilAttachment depthAttachment{};
    depthAttachment.view = fbIt->second.depthTextureView;
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;
    depthAttachment.depthReadOnly = false;
    depthAttachment.stencilLoadOp = WGPULoadOp_Load;
    depthAttachment.stencilStoreOp = WGPUStoreOp_Store;
    depthAttachment.stencilReadOnly = true;

    WGPURenderPassDescriptor passDesc{};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = &depthAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (pass) {
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuCommandEncoderRelease(encoder);
    if (!commandBuffer) return;
    wgpuQueueSubmit(state->queue, 1, &commandBuffer);
    wgpuCommandBufferRelease(commandBuffer);
#endif
}

bool WebGPUBackend::readDefaultFramebufferRgb(PlatformWindowHandle window, std::vector<unsigned char>& outPixels, int& width, int& height) const {
    (void)window;
    outPixels.clear();
    width = 0;
    height = 0;
    return false;
}

bool WebGPUBackend::uploadRgbTexture2D(RenderHandle& ioTexture, int width, int height, const std::vector<unsigned char>& pixels) {
    if (width <= 0 || height <= 0 || !state) {
        return false;
    }
    const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
    if (pixels.size() < expectedSize) {
        return false;
    }
    std::vector<unsigned char> rgbaPixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 255u);
    for (int i = 0; i < width * height; ++i) {
        const size_t src = static_cast<size_t>(i) * 3u;
        const size_t dst = static_cast<size_t>(i) * 4u;
        rgbaPixels[dst + 0] = pixels[src + 0];
        rgbaPixels[dst + 1] = pixels[src + 1];
        rgbaPixels[dst + 2] = pixels[src + 2];
        rgbaPixels[dst + 3] = 255u;
    }
    const TextureUploadParams params{};
    return uploadRgbaTexture2D(ioTexture, width, height, rgbaPixels, params);
}

bool WebGPUBackend::uploadRgbaTexture2D(RenderHandle& ioTexture,
                                        int width,
                                        int height,
                                        const std::vector<unsigned char>& pixels,
                                        const TextureUploadParams& params) {
    (void)params;
    if (width <= 0 || height <= 0 || !state) {
        return false;
    }
    const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    if (pixels.size() < expectedSize) {
        return false;
    }
#if SALAMANDER_WEBGPU_NATIVE
    if (!state->device || !state->queue) {
        return false;
    }
    const WGPUTextureUsage usage = WGPUTextureUsage_TextureBinding
        | WGPUTextureUsage_CopyDst
        | WGPUTextureUsage_CopySrc;
    RenderHandle textureHandle = 0;
    if (!createOrReplaceTexture2D(*state,
                                  ioTexture,
                                  static_cast<uint32_t>(width),
                                  static_cast<uint32_t>(height),
                                  WGPUTextureFormat_RGBA8Unorm,
                                  usage,
                                  "Cardinal RGBA Texture",
                                  textureHandle)) {
        return false;
    }

    const auto texIt = state->textures.find(textureHandle);
    if (texIt == state->textures.end() || !texIt->second.texture) {
        return false;
    }

    WGPUTexelCopyTextureInfo destination{};
    destination.texture = texIt->second.texture;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout{};
    layout.offset = 0;
    layout.bytesPerRow = static_cast<uint32_t>(width * 4);
    layout.rowsPerImage = static_cast<uint32_t>(height);

    WGPUExtent3D writeSize{};
    writeSize.width = static_cast<uint32_t>(width);
    writeSize.height = static_cast<uint32_t>(height);
    writeSize.depthOrArrayLayers = 1;

    wgpuQueueWriteTexture(state->queue, &destination, pixels.data(), expectedSize, &layout, &writeSize);
    ioTexture = textureHandle;
    return true;
#else
    return false;
#endif
}

bool WebGPUBackend::uploadRgbaTextureSubImage2D(RenderHandle texture,
                                                int x,
                                                int y,
                                                int width,
                                                int height,
                                                const std::vector<unsigned char>& pixels) {
    if (width <= 0 || height <= 0 || x < 0 || y < 0 || !state || !texture) {
        return false;
    }
    const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    if (pixels.size() < expectedSize) {
        return false;
    }
#if SALAMANDER_WEBGPU_NATIVE
    if (!state->queue) {
        return false;
    }
    const auto texIt = state->textures.find(texture);
    if (texIt == state->textures.end() || !texIt->second.texture) {
        return false;
    }
    if (static_cast<uint32_t>(x + width) > texIt->second.width
        || static_cast<uint32_t>(y + height) > texIt->second.height) {
        return false;
    }

    WGPUTexelCopyTextureInfo destination{};
    destination.texture = texIt->second.texture;
    destination.mipLevel = 0;
    destination.origin = {static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0};
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout{};
    layout.offset = 0;
    layout.bytesPerRow = static_cast<uint32_t>(width * 4);
    layout.rowsPerImage = static_cast<uint32_t>(height);

    WGPUExtent3D writeSize{};
    writeSize.width = static_cast<uint32_t>(width);
    writeSize.height = static_cast<uint32_t>(height);
    writeSize.depthOrArrayLayers = 1;
    wgpuQueueWriteTexture(state->queue, &destination, pixels.data(), expectedSize, &layout, &writeSize);
    return true;
#else
    return false;
#endif
}

bool WebGPUBackend::readTexture2DRgba(RenderHandle texture,
                                      int width,
                                      int height,
                                      std::vector<unsigned char>& outPixels) const {
    (void)texture;
    (void)width;
    (void)height;
    outPixels.clear();
    return false;
}

void WebGPUBackend::ensureVertexArray(RenderHandle& ioVertexArray) {
    if (!state || ioVertexArray) return;
    ioVertexArray = state->nextHandle++;
    if (ioVertexArray == 0) {
        ioVertexArray = state->nextHandle++;
    }
#if SALAMANDER_WEBGPU_NATIVE
    state->vertexArrayResources[ioVertexArray] = BackendState::VertexArrayResource{};
#endif
}

void WebGPUBackend::destroyVertexArray(RenderHandle& vertexArray) {
#if SALAMANDER_WEBGPU_NATIVE
    if (state && vertexArray) {
        state->vertexArrayResources.erase(vertexArray);
        if (state->currentVertexArray == vertexArray) {
            state->currentVertexArray = 0;
        }
    }
#endif
    vertexArray = 0;
}

void WebGPUBackend::ensureArrayBuffer(RenderHandle& ioBuffer) {
    if (!state || ioBuffer) return;
    ioBuffer = state->nextHandle++;
    if (ioBuffer == 0) {
        ioBuffer = state->nextHandle++;
    }
#if SALAMANDER_WEBGPU_NATIVE
    state->bufferResources[ioBuffer] = BackendState::BufferResource{};
#endif
}

void WebGPUBackend::destroyArrayBuffer(RenderHandle& buffer) {
#if SALAMANDER_WEBGPU_NATIVE
    if (state && buffer) {
        auto bufferIt = state->bufferResources.find(buffer);
        if (bufferIt != state->bufferResources.end()) {
            releaseBufferResource(bufferIt->second);
            state->bufferResources.erase(bufferIt);
        }
        for (auto& [_, vao] : state->vertexArrayResources) {
            if (vao.vertexBuffer == buffer) {
                vao.vertexBuffer = 0;
                vao.vertexAttribs.clear();
            }
            if (vao.instanceBuffer == buffer) {
                vao.instanceBuffer = 0;
                vao.instanceAttribs.clear();
            }
        }
    }
#endif
    buffer = 0;
}

void WebGPUBackend::uploadArrayBufferData(RenderHandle buffer, const void* data, size_t sizeBytes, bool dynamic) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state || !buffer) return;
    auto it = state->bufferResources.find(buffer);
    if (it == state->bufferResources.end()) {
        return;
    }
    BackendState::BufferResource& resource = it->second;
    if (sizeBytes == 0u) {
        resource.cpuBytes.clear();
        resource.dynamic = dynamic;
        resource.dirty = false;
        return;
    }
    if (!data) return;
    resource.cpuBytes.resize(sizeBytes);
    std::memcpy(resource.cpuBytes.data(), data, sizeBytes);
    resource.dynamic = dynamic;
    resource.dirty = true;
#else
    (void)buffer;
    (void)data;
    (void)sizeBytes;
    (void)dynamic;
#endif
}

void WebGPUBackend::configureVertexArray(RenderHandle vertexArray,
                                         RenderHandle vertexBuffer,
                                         const std::vector<VertexAttribLayout>& vertexAttribs,
                                         RenderHandle instanceBuffer,
                                         const std::vector<VertexAttribLayout>& instanceAttribs) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state || !vertexArray) return;
    BackendState::VertexArrayResource& vao = state->vertexArrayResources[vertexArray];
    vao.vertexBuffer = vertexBuffer;
    vao.vertexAttribs = vertexAttribs;
    vao.instanceBuffer = instanceBuffer;
    vao.instanceAttribs = instanceAttribs;
#else
    (void)vertexArray;
    (void)vertexBuffer;
    (void)vertexAttribs;
    (void)instanceBuffer;
    (void)instanceAttribs;
#endif
}

void WebGPUBackend::bindVertexArray(RenderHandle vertexArray) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    if (!vertexArray || state->vertexArrayResources.find(vertexArray) == state->vertexArrayResources.end()) {
        state->currentVertexArray = 0;
        return;
    }
    state->currentVertexArray = vertexArray;
#else
    (void)vertexArray;
#endif
}

void WebGPUBackend::unbindVertexArray() {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    state->currentVertexArray = 0;
#endif
}

int WebGPUBackend::getMaxTextureImageUnits() const {
    return 16;
}

void WebGPUBackend::drawArraysTriangles(int first, int count) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    appendDrawCommand(*state, BackendState::DrawCommand::Primitive::Triangles, first, count, 1);
#else
    (void)first;
    (void)count;
#endif
}

void WebGPUBackend::drawArraysLines(int first, int count) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    appendDrawCommand(*state, BackendState::DrawCommand::Primitive::Lines, first, count, 1);
#else
    (void)first;
    (void)count;
#endif
}

void WebGPUBackend::drawArraysPoints(int first, int count) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    appendDrawCommand(*state, BackendState::DrawCommand::Primitive::Points, first, count, 1);
#else
    (void)first;
    (void)count;
#endif
}

void WebGPUBackend::drawArraysTrianglesInstanced(int first, int count, int instanceCount) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    appendDrawCommand(*state,
                      BackendState::DrawCommand::Primitive::Triangles,
                      first,
                      count,
                      std::max(1, instanceCount));
#else
    (void)first;
    (void)count;
    (void)instanceCount;
#endif
}

void WebGPUBackend::destroyTexture(RenderHandle& texture) {
#if SALAMANDER_WEBGPU_NATIVE
    if (state && texture) {
        auto texIt = state->textures.find(texture);
        if (texIt != state->textures.end()) {
            releaseTextureResource(texIt->second);
            state->textures.erase(texIt);
        }
        std::vector<RenderHandle> staleFramebuffers;
        for (const auto& [framebufferHandle, framebufferResource] : state->framebuffers) {
            if (framebufferResource.textureHandle == texture) {
                staleFramebuffers.push_back(framebufferHandle);
            }
        }
        for (RenderHandle framebufferHandle : staleFramebuffers) {
            state->offscreenScopes.erase(std::remove_if(state->offscreenScopes.begin(),
                                                        state->offscreenScopes.end(),
                                                        [&](const BackendState::OffscreenScope& scope) {
                                                            return scope.framebuffer == framebufferHandle;
                                                        }),
                                         state->offscreenScopes.end());
            auto fbIt = state->framebuffers.find(framebufferHandle);
            if (fbIt != state->framebuffers.end()) {
                releaseFramebufferResource(fbIt->second);
                state->framebuffers.erase(fbIt);
            }
        }
    }
#endif
    texture = 0;
}

bool WebGPUBackend::bindTexture2D(RenderHandle texture, int unit) {
    if (unit < 0 || unit >= 32) return false;
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return false;
    if (texture == 0) {
        state->boundTextureUnits[static_cast<size_t>(unit)] = 0;
        return true;
    }
    if (state->textures.find(texture) == state->textures.end()) {
        state->boundTextureUnits[static_cast<size_t>(unit)] = 0;
        return false;
    }
    state->boundTextureUnits[static_cast<size_t>(unit)] = texture;
    return true;
#else
    return texture != 0;
#endif
}

void WebGPUBackend::setDepthTestEnabled(bool enabled) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    state->currentRenderState.depthTestEnabled = enabled;
#else
    (void)enabled;
#endif
}

void WebGPUBackend::setDepthWriteEnabled(bool enabled) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    state->currentRenderState.depthWriteEnabled = enabled;
#else
    (void)enabled;
#endif
}

void WebGPUBackend::setBlendEnabled(bool enabled) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    state->currentRenderState.blendEnabled = enabled;
#else
    (void)enabled;
#endif
}

void WebGPUBackend::setBlendEquationAdd() {
}

void WebGPUBackend::setBlendModeAlpha() {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    state->currentRenderState.blendMode = BackendState::RenderState::BlendMode::Alpha;
#endif
}

void WebGPUBackend::setBlendModeConstantAlpha(float alpha) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    state->currentRenderState.blendMode = BackendState::RenderState::BlendMode::ConstantAlpha;
    state->currentRenderState.blendConstantAlpha = std::max(0.0f, std::min(1.0f, alpha));
#else
    (void)alpha;
#endif
}

void WebGPUBackend::setBlendModeSrcAlphaOne() {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    state->currentRenderState.blendMode = BackendState::RenderState::BlendMode::SrcAlphaOne;
#endif
}

void WebGPUBackend::setBlendModeAdditive() {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    state->currentRenderState.blendMode = BackendState::RenderState::BlendMode::Additive;
#endif
}

void WebGPUBackend::setCullEnabled(bool enabled) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    state->currentRenderState.cullEnabled = enabled;
#else
    (void)enabled;
#endif
}

void WebGPUBackend::setCullBackFaceCCW() {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    state->currentRenderState.frontFaceCCW = true;
    state->currentRenderState.cullBackFace = true;
#endif
}

void WebGPUBackend::setScissorEnabled(bool enabled) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    state->currentRenderState.scissorEnabled = enabled;
#else
    (void)enabled;
#endif
}

void WebGPUBackend::setScissorRect(int x, int y, int width, int height) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    state->currentRenderState.scissorX = x;
    state->currentRenderState.scissorY = y;
    state->currentRenderState.scissorWidth = width;
    state->currentRenderState.scissorHeight = height;
#else
    (void)x;
    (void)y;
    (void)width;
    (void)height;
#endif
}

void WebGPUBackend::setLineWidth(float width) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    state->currentRenderState.lineWidth = width;
#else
    (void)width;
#endif
}

void WebGPUBackend::setProgramPointSizeEnabled(bool enabled) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    state->currentRenderState.programPointSizeEnabled = enabled;
#else
    (void)enabled;
#endif
}

void WebGPUBackend::setWireframeEnabled(bool enabled) {
#if SALAMANDER_WEBGPU_NATIVE
    if (!state) return;
    state->currentRenderState.wireframeEnabled = enabled;
#else
    (void)enabled;
#endif
}
