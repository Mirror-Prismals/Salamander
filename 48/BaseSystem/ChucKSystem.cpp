#pragma once
#include <fstream>
#include <iostream>

namespace ChucKSystemLogic {

    static bool file_mtime(const std::string& path, std::time_t& out) {
        struct stat st{};
        if (stat(path.c_str(), &st) == 0) { out = st.st_mtime; return true; }
        return false;
    }

    static bool compile_script(AudioContext& audio, const std::string& path, t_CKUINT& outShredId) {
        std::ifstream f(path);
        if (!f.is_open()) {
            std::cerr << "ChucK script not found at '" << path << "'. Skipping compile." << std::endl;
            return false;
        }
        std::vector<t_CKUINT> ids;
        bool ok = audio.chuck->compileFile(path, "", 1, TRUE, &ids);
        if (!ok || ids.empty()) {
            std::cerr << "ChucK failed to compile script: " << path << std::endl;
            return false;
        }
        outShredId = ids.front();
        std::cout << "ChucK script compiled: " << path << " (shred " << outShredId << ")" << std::endl;
        return true;
    }

    // Update loop: handle pending compile/bypass requests
    void UpdateChucK(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.audio || !baseSystem.audio->chuck) return;
        AudioContext& audio = *baseSystem.audio;

        if (audio.chuckBypass) {
            if (audio.chuckMainShredId) {
                if (auto* sh = audio.chuck->vm()->shreduler()->lookup(audio.chuckMainShredId)) {
                    audio.chuck->vm()->shreduler()->remove(sh);
                }
                audio.chuckMainShredId = 0;
            }
            if (audio.chuckNoiseShredId) {
                if (auto* sh = audio.chuck->vm()->shreduler()->lookup(audio.chuckNoiseShredId)) {
                    audio.chuck->vm()->shreduler()->remove(sh);
                }
                audio.chuckNoiseShredId = 0;
            }
            return;
        }

        // Hot reload based on file mtime without rebuild
        {
            static std::time_t lastMainMTime = 0;
            std::time_t m;
            if (file_mtime(audio.chuckMainScript, m) && m != lastMainMTime) {
                audio.chuckMainCompileRequested = true;
                lastMainMTime = m;
            }

            static std::time_t lastNoiseMTime = 0;
            if (file_mtime(audio.chuckNoiseScript, m) && m != lastNoiseMTime) {
                if (audio.chuckNoiseShredId) {
                    if (auto* sh = audio.chuck->vm()->shreduler()->lookup(audio.chuckNoiseShredId)) {
                        audio.chuck->vm()->shreduler()->remove(sh);
                    }
                    audio.chuckNoiseShredId = 0;
                }
                lastNoiseMTime = m;
            }
        }

        // Compile main script on request
        if (audio.chuckMainCompileRequested) {
            if (audio.chuckMainShredId) {
                if (auto* sh = audio.chuck->vm()->shreduler()->lookup(audio.chuckMainShredId)) {
                    audio.chuck->vm()->shreduler()->remove(sh);
                }
                audio.chuckMainShredId = 0;
            }
            compile_script(audio, audio.chuckMainScript, audio.chuckMainShredId);
            audio.chuckMainCompileRequested = false;
        }

        // Manage noise script based on flag
        if (audio.chuckNoiseShouldRun && audio.chuckNoiseShredId == 0) {
            compile_script(audio, audio.chuckNoiseScript, audio.chuckNoiseShredId);
        } else if (!audio.chuckNoiseShouldRun && audio.chuckNoiseShredId != 0) {
            if (auto* sh = audio.chuck->vm()->shreduler()->lookup(audio.chuckNoiseShredId)) {
                audio.chuck->vm()->shreduler()->remove(sh);
            }
            audio.chuckNoiseShredId = 0;
        }
    }

}
