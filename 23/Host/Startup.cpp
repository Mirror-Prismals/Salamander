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
        
        std::ifstream file("Procedures/procedures.glsl");
        if (!file.is_open()) { std::cerr << "FATAL ERROR: Could not open shader file Procedures/procedures.glsl" << std::endl; exit(-1); }
        std::stringstream buffer; buffer << file.rdbuf(); std::string content = buffer.str();
        std::string currentShaderName; std::stringstream currentShaderSource;
        std::stringstream contentStream(content); std::string line;
        while (std::getline(contentStream, line)) {
            if (line.rfind("@@", 0) == 0) {
                if (!currentShaderName.empty()) { world.shaders[currentShaderName] = currentShaderSource.str(); }
                currentShaderName = line.substr(2); currentShaderSource.str(""); currentShaderSource.clear();
            } else { currentShaderSource << line << '\n'; }
        }
        if (!currentShaderName.empty()) { world.shaders[currentShaderName] = currentShaderSource.str(); }
    }
}
