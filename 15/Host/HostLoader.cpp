#pragma once

void Host::loadRegistry() {
    std::ifstream f("BaseSystem/registry.json");
    if (!f.is_open()) { programStatus = "Registry Not Found"; return; }
    json data;
    try { data = json::parse(f); } catch (...) { programStatus = "Invalid Registry"; return; }
    
    if (data.contains("Program") && data["Program"].is_string()) {
        programStatus = data["Program"].get<std::string>();
    } else { programStatus = "Invalid Registry"; }

    for (auto& [key, value] : data.items()) {
        if (key != "Program" && value.is_boolean()) { registry[key] = value.get<bool>(); }
    }
}

void Host::loadSystems() {
    std::ifstream f("Procedures/systems.json");
    if (!f.is_open()) { std::cerr << "FATAL: Could not open Procedures/systems.json" << std::endl; exit(-1); }
    json data = json::parse(f);

    std::cout << "--- Cardinal EDS Booting ---" << std::endl;
    std::cout << "Program Status: " << programStatus << std::endl;
    std::cout << "--- Loading Installed Systems ---" << std::endl;

    for (const auto& systemFile : data["systems_order"]) {
        std::string systemName = systemFile.get<std::string>();
        systemName = systemName.substr(0, systemName.find(".json"));

        if (registry.count(systemName) && registry[systemName]) {
             std::cout << "[INSTALLED] " << systemName << std::endl;
            std::string path = "Systems/" + systemFile.get<std::string>();
            std::ifstream sys_f(path);
            if (!sys_f.is_open()) { std::cerr << "ERROR: Could not find system file " << path << std::endl; continue; }
            json sys_data = json::parse(sys_f);

            if (sys_data.contains("init_steps")) for (auto& [name, details] : sys_data["init_steps"].items()) initFunctions.push_back({name, details.value("dependencies", std::vector<std::string>{})});
            if (sys_data.contains("update_steps")) for (auto& [name, details] : sys_data["update_steps"].items()) updateFunctions.push_back({name, details.value("dependencies", std::vector<std::string>{})});
            if (sys_data.contains("cleanup_steps")) for (auto& [name, details] : sys_data["cleanup_steps"].items()) cleanupFunctions.push_back({name, details.value("dependencies", std::vector<std::string>{})});
        }
    }
    std::cout << "---------------------------------" << std::endl;
}

bool Host::checkDependencies(const std::vector<std::string>& deps) {
    for (const auto& dep : deps) {
        if (dep == "AppContext" && !baseSystem.app) return false;
        if (dep == "WorldContext" && !baseSystem.world) return false;
        if (dep == "PlayerContext" && !baseSystem.player) return false;
        if (dep == "InstanceContext" && !baseSystem.instance) return false;
        if (dep == "RendererContext" && !baseSystem.renderer) return false;
        if (dep == "AudioContext" && !baseSystem.audio) return false; // <-- ADDED
    }
    return true;
}
