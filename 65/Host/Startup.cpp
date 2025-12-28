#pragma once

namespace HostLogic {
    void LoadProcedureAssets(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
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
        
        std::vector<std::pair<std::string, std::string>> shaderFiles = {
            {"BLOCK_VERTEX_SHADER", "Procedures/Shaders/Block.vert.glsl"},
            {"BLOCK_FRAGMENT_SHADER", "Procedures/Shaders/Block.frag.glsl"},
            {"FACE_VERTEX_SHADER", "Procedures/Shaders/Face.vert.glsl"},
            {"FACE_FRAGMENT_SHADER", "Procedures/Shaders/Face.frag.glsl"},
            {"SKYBOX_VERTEX_SHADER", "Procedures/Shaders/Skybox.vert.glsl"},
            {"SKYBOX_FRAGMENT_SHADER", "Procedures/Shaders/Skybox.frag.glsl"},
            {"SUNMOON_VERTEX_SHADER", "Procedures/Shaders/SunMoon.vert.glsl"},
            {"SUNMOON_FRAGMENT_SHADER", "Procedures/Shaders/SunMoon.frag.glsl"},
            {"GODRAY_VERTEX_SHADER", "Procedures/Shaders/Godray.vert.glsl"},
            {"GODRAY_RADIAL_FRAGMENT_SHADER", "Procedures/Shaders/GodrayRadial.frag.glsl"},
            {"GODRAY_COMPOSITE_FRAGMENT_SHADER", "Procedures/Shaders/GodrayComposite.frag.glsl"},
            {"CLOUD_VERTEX_SHADER", "Procedures/Shaders/Cloud.vert.glsl"},
            {"CLOUD_FRAGMENT_SHADER", "Procedures/Shaders/Cloud.frag.glsl"},
            {"AURORA_VERTEX_SHADER", "Procedures/Shaders/Aurora.vert.glsl"},
            {"AURORA_FRAGMENT_SHADER", "Procedures/Shaders/Aurora.frag.glsl"},
            {"STAR_VERTEX_SHADER", "Procedures/Shaders/Star.vert.glsl"},
            {"STAR_FRAGMENT_SHADER", "Procedures/Shaders/Star.frag.glsl"},
            {"SELECTION_VERTEX_SHADER", "Procedures/Shaders/Selection.vert.glsl"},
            {"SELECTION_FRAGMENT_SHADER", "Procedures/Shaders/Selection.frag.glsl"},
            {"HUD_VERTEX_SHADER", "Procedures/Shaders/HUD.vert.glsl"},
            {"HUD_FRAGMENT_SHADER", "Procedures/Shaders/HUD.frag.glsl"},
            {"CROSSHAIR_VERTEX_SHADER", "Procedures/Shaders/Crosshair.vert.glsl"},
            {"CROSSHAIR_FRAGMENT_SHADER", "Procedures/Shaders/Crosshair.frag.glsl"},
            {"UI_VERTEX_SHADER", "Procedures/Shaders/UI.vert.glsl"},
            {"UI_FRAGMENT_SHADER", "Procedures/Shaders/UI.frag.glsl"},
            {"UI_COLOR_VERTEX_SHADER", "Procedures/Shaders/UIColor.vert.glsl"},
            {"UI_COLOR_FRAGMENT_SHADER", "Procedures/Shaders/UIColor.frag.glsl"},
            {"FONT_VERTEX_SHADER", "Procedures/Shaders/Font.vert.glsl"},
            {"FONT_FRAGMENT_SHADER", "Procedures/Shaders/Font.frag.glsl"},
            {"AUDIORAY_VERTEX_SHADER", "Procedures/Shaders/AudioRay.vert.glsl"},
            {"AUDIORAY_FRAGMENT_SHADER", "Procedures/Shaders/AudioRay.frag.glsl"},
            {"AUDIORAY_VOXEL_VERTEX_SHADER", "Procedures/Shaders/AudioRayVoxel.vert.glsl"},
            {"AUDIORAY_VOXEL_FRAGMENT_SHADER", "Procedures/Shaders/AudioRayVoxel.frag.glsl"}
        };

        for (const auto& [key, path] : shaderFiles) {
            std::ifstream file(path);
            if (!file.is_open()) {
                std::cerr << "FATAL ERROR: Could not open shader file " << path << " for key " << key << std::endl;
                exit(-1);
            }
            std::stringstream buffer; buffer << file.rdbuf();
            world.shaders[key] = buffer.str();
        }
    }
}
