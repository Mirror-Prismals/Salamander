#pragma once
#include <fstream>
#include <iostream>

namespace ChucKSystemLogic {

    // Compile and start a ChucK script located in the Procedures/chuck directory.
    void CompileDefaultScript(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.audio || !baseSystem.audio->chuck) {
            std::cerr << "ChucK engine not initialized; skipping script compile." << std::endl;
            return;
        }

        AudioContext& audio = *baseSystem.audio;
        const std::string scriptPath = "Procedures/chuck/main.ck";

        std::ifstream f(scriptPath);
        if (!f.is_open()) {
            std::cerr << "ChucK script not found at '" << scriptPath << "'. Skipping compile." << std::endl;
            return;
        }

        bool ok = audio.chuck->compileFile(scriptPath, "", 1);
        if (!ok) {
            std::cerr << "ChucK failed to compile script: " << scriptPath << std::endl;
        } else {
            std::cout << "ChucK script compiled and running: " << scriptPath << std::endl;
        }
    }

}
