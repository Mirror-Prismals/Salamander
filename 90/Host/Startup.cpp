#pragma once

namespace {
    std::string lowerCopyStartup(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    bool parseBoolLikeString(const std::string& raw) {
        const std::string value = lowerCopyStartup(raw);
        return value == "1" || value == "true" || value == "yes" || value == "on";
    }

    bool preferWebGpuShaderSources(const BaseSystem& baseSystem) {
        GraphicsAPI api = GraphicsAPI::WebGPU;
        if (baseSystem.registry) {
            auto parseApiValue = [](const std::string& raw) -> GraphicsAPI {
                const std::string value = lowerCopyStartup(raw);
                if (value == "webgpu" || value == "wgpu") return GraphicsAPI::WebGPU;
                // Keep WebGPU selected for all shader source resolution.
                return GraphicsAPI::WebGPU;
            };
            auto it = baseSystem.registry->find("GraphicsAPI");
            if (it != baseSystem.registry->end() && std::holds_alternative<std::string>(it->second)) {
                api = parseApiValue(std::get<std::string>(it->second));
            } else {
                it = baseSystem.registry->find("graphics_api");
                if (it != baseSystem.registry->end() && std::holds_alternative<std::string>(it->second)) {
                    api = parseApiValue(std::get<std::string>(it->second));
                }
            }
            auto forceIt = baseSystem.registry->find("PreferWebGPUShaders");
            if (forceIt != baseSystem.registry->end()) {
                if (std::holds_alternative<bool>(forceIt->second)) {
                    if (std::get<bool>(forceIt->second)) return true;
                } else if (std::holds_alternative<std::string>(forceIt->second)) {
                    if (parseBoolLikeString(std::get<std::string>(forceIt->second))) return true;
                }
            }
        }

        bool preferWebGpu = (api == GraphicsAPI::WebGPU);
        return preferWebGpu;
    }

    bool readTextFile(const std::string& path, std::string& outSource) {
        std::ifstream file(path);
        if (!file.is_open()) return false;
        std::stringstream buffer;
        buffer << file.rdbuf();
        outSource = buffer.str();
        return true;
    }

    std::string deriveWgslPath(const std::string& path) {
        static constexpr const char* kWgslSuffix = ".wgsl";
        if (path.size() >= 5 && path.compare(path.size() - 5, 5, kWgslSuffix) == 0) {
            return path;
        }
        static constexpr const char* kSuffix = ".glsl";
        if (path.size() >= 5 && path.compare(path.size() - 5, 5, kSuffix) == 0) {
            return path.substr(0, path.size() - 5) + ".wgsl";
        }
        return path + ".wgsl";
    }

    const std::vector<std::pair<std::string, std::string>>& shaderFileTable() {
        static const std::vector<std::pair<std::string, std::string>> kShaderFiles = {
            {"BLOCK_VERTEX_SHADER", "Procedures/Shaders/Block.vert.wgsl"},
            {"BLOCK_FRAGMENT_SHADER", "Procedures/Shaders/Block.frag.wgsl"},
            {"FACE_VERTEX_SHADER", "Procedures/Shaders/Face.vert.wgsl"},
            {"FACE_FRAGMENT_SHADER", "Procedures/Shaders/Face.frag.wgsl"},
            {"SKYBOX_VERTEX_SHADER", "Procedures/Shaders/Skybox.vert.wgsl"},
            {"SKYBOX_FRAGMENT_SHADER", "Procedures/Shaders/Skybox.frag.wgsl"},
            {"SUNMOON_VERTEX_SHADER", "Procedures/Shaders/SunMoon.vert.wgsl"},
            {"SUNMOON_FRAGMENT_SHADER", "Procedures/Shaders/SunMoon.frag.wgsl"},
            {"GODRAY_VERTEX_SHADER", "Procedures/Shaders/Godray.vert.wgsl"},
            {"GODRAY_RADIAL_FRAGMENT_SHADER", "Procedures/Shaders/GodrayRadial.frag.wgsl"},
            {"GODRAY_COMPOSITE_FRAGMENT_SHADER", "Procedures/Shaders/GodrayComposite.frag.wgsl"},
            {"STAR_VERTEX_SHADER", "Procedures/Shaders/Star.vert.wgsl"},
            {"STAR_FRAGMENT_SHADER", "Procedures/Shaders/Star.frag.wgsl"},
            {"SELECTION_VERTEX_SHADER", "Procedures/Shaders/Selection.vert.wgsl"},
            {"SELECTION_FRAGMENT_SHADER", "Procedures/Shaders/Selection.frag.wgsl"},
            {"HUD_VERTEX_SHADER", "Procedures/Shaders/HUD.vert.wgsl"},
            {"HUD_FRAGMENT_SHADER", "Procedures/Shaders/HUD.frag.wgsl"},
            {"COLOR_EMOTION_VERTEX_SHADER", "Procedures/Shaders/ColorEmotion.vert.wgsl"},
            {"COLOR_EMOTION_FRAGMENT_SHADER", "Procedures/Shaders/ColorEmotion.frag.wgsl"},
            {"CROSSHAIR_VERTEX_SHADER", "Procedures/Shaders/Crosshair.vert.wgsl"},
            {"CROSSHAIR_FRAGMENT_SHADER", "Procedures/Shaders/Crosshair.frag.wgsl"},
            {"UI_VERTEX_SHADER", "Procedures/Shaders/UI.vert.wgsl"},
            {"UI_FRAGMENT_SHADER", "Procedures/Shaders/UI.frag.wgsl"},
            {"UI_COLOR_VERTEX_SHADER", "Procedures/Shaders/UIColor.vert.wgsl"},
            {"UI_COLOR_FRAGMENT_SHADER", "Procedures/Shaders/UIColor.frag.wgsl"},
            {"GLYPH_VERTEX_SHADER", "Procedures/Shaders/Glyph.vert.wgsl"},
            {"GLYPH_FRAGMENT_SHADER", "Procedures/Shaders/Glyph.frag.wgsl"},
            {"FONT_VERTEX_SHADER", "Procedures/Shaders/Font.vert.wgsl"},
            {"FONT_FRAGMENT_SHADER", "Procedures/Shaders/Font.frag.wgsl"},
            {"AUDIORAY_VERTEX_SHADER", "Procedures/Shaders/AudioRay.vert.wgsl"},
            {"AUDIORAY_FRAGMENT_SHADER", "Procedures/Shaders/AudioRay.frag.wgsl"},
            {"AUDIORAY_VOXEL_VERTEX_SHADER", "Procedures/Shaders/AudioRayVoxel.vert.wgsl"},
            {"AUDIORAY_VOXEL_FRAGMENT_SHADER", "Procedures/Shaders/AudioRayVoxel.frag.wgsl"}
        };
        return kShaderFiles;
    }

    bool loadShaderLibrary(const BaseSystem& baseSystem, WorldContext& world, std::string* outError) {
        const bool preferWebGpu = preferWebGpuShaderSources(baseSystem);
        std::unordered_map<std::string, std::string> loaded;
        loaded.reserve(shaderFileTable().size());
        int wgslOverrides = 0;
        for (const auto& [key, path] : shaderFileTable()) {
            std::string source;
            if (preferWebGpu) {
                const std::string wgslPath = deriveWgslPath(path);
                if (readTextFile(wgslPath, source)) {
                    ++wgslOverrides;
                }
            }
            if (source.empty() && !readTextFile(path, source)) {
                if (outError) *outError = "Could not open shader file " + path + " for key " + key;
                return false;
            }
            loaded[key] = source;
        }
        for (const auto& [key, source] : loaded) {
            world.shaders[key] = source;
        }
        if (preferWebGpu && wgslOverrides > 0) {
            std::cout << "ShaderLibrary: using " << wgslOverrides << " WGSL override source(s)." << std::endl;
        }
        if (outError) outError->clear();
        return true;
    }
}

namespace HostLogic {
    void LoadProcedureAssets(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.app || !baseSystem.world) { std::cerr << "FATAL: AppContext or WorldContext not available for StartupSystem." << std::endl; exit(-1); }
        AppContext& app = *baseSystem.app;
        WorldContext& world = *baseSystem.world;

        std::ifstream f("Procedures/procedures.json");
        if (!f.is_open()) { std::cerr << "FATAL ERROR: Could not open Procedures/procedures.json" << std::endl; exit(-1); }
        json data = json::parse(f);
        app.windowWidth = data["window"]["width"];
        app.windowHeight = data["window"]["height"];
        
        // --- REMOVED OBSOLETE STAR LOADING ---
        // world.numStars = data["world"]["num_stars"];
        // world.starDistance = data["world"]["star_distance"];
        
        world.cubeVertices = data["cube_vertices"].get<std::vector<float>>();
        for (const auto& key : data["sky_color_keys"]) { world.skyKeys.push_back({key["time"], glm::vec3(key["top"][0], key["top"][1], key["top"][2]), glm::vec3(key["bottom"][0], key["bottom"][1], key["bottom"][2])}); }
        
        std::ifstream cf("Procedures/colors.json");
        if (!cf.is_open()) { std::cerr << "FATAL ERROR: Could not open Procedures/colors.json" << std::endl; exit(-1); }
        json colorData = json::parse(cf);
        for (auto& [name, hex] : colorData["colors"].items()) {
            world.colorLibrary[name] = HostLogic::hexToVec3(hex.get<std::string>());
        }
        
        std::string shaderError;
        if (!loadShaderLibrary(baseSystem, world, &shaderError)) {
            std::cerr << "FATAL ERROR: " << shaderError << std::endl;
            exit(-1);
        }
    }

    bool ReloadShaderSources(BaseSystem& baseSystem, std::string* outError) {
        if (!baseSystem.world) {
            if (outError) *outError = "WorldContext not available.";
            return false;
        }
        return loadShaderLibrary(baseSystem, *baseSystem.world, outError);
    }
}
