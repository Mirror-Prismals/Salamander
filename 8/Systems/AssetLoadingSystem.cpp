#pragma once

class AssetLoadingSystem : public ISystem {
private:
    bool dataLoaded = false;
    void loadShaders(BaseSystem& baseSystem, const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) { std::cerr << "FATAL ERROR: Could not open shader file " << path << std::endl; exit(-1); }
        std::stringstream buffer; buffer << file.rdbuf(); std::string content = buffer.str();
        std::string currentShaderName; std::stringstream currentShaderSource;
        std::stringstream contentStream(content); std::string line;
        while (std::getline(contentStream, line)) {
            if (line.rfind("@@", 0) == 0) {
                if (!currentShaderName.empty()) { baseSystem.shaders[currentShaderName] = currentShaderSource.str(); }
                currentShaderName = line.substr(2); currentShaderSource.str(""); currentShaderSource.clear();
            } else { currentShaderSource << line << '\n'; }
        }
        if (!currentShaderName.empty()) { baseSystem.shaders[currentShaderName] = currentShaderSource.str(); }
    }
public:
    void update(std::vector<Entity>& prototypes, BaseSystem& baseSystem, float deltaTime, GLFWwindow* window) override {
        if (dataLoaded) return;
        std::ifstream f("Procedures/procedures.json");
        if (!f.is_open()) { std::cerr << "FATAL ERROR: Could not open Procedures/procedures.json" << std::endl; exit(-1); }
        try {
            json data = json::parse(f);
            baseSystem.windowWidth = data["window"]["width"];
            baseSystem.windowHeight = data["window"]["height"];
            baseSystem.numBlockPrototypes = data["world"]["num_block_prototypes"];
            baseSystem.numStars = data["world"]["num_stars"];
            baseSystem.starDistance = data["world"]["star_distance"];
            baseSystem.blockColors = data["block_colors"].get<std::vector<glm::vec3>>();
            baseSystem.cubeVertices = data["cube_vertices"].get<std::vector<float>>();
            for (const auto& key : data["sky_color_keys"]) { baseSystem.skyKeys.push_back({key["time"], key["top"].get<glm::vec3>(), key["bottom"].get<glm::vec3>()}); }
        } catch (json::parse_error& e) { std::cerr << "FATAL ERROR: Failed to parse procedures.json: " << e.what() << std::endl; exit(-1); }
        loadShaders(baseSystem, "Procedures/procedures.glsl");
        dataLoaded = true;
    }
};
