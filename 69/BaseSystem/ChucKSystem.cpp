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

    static bool path_matches(const std::string& candidate, const std::string& target) {
        if (candidate == target) return true;
        auto normalize = [](std::string s) {
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        };
        std::string cand = normalize(candidate);
        std::string tgt = normalize(target);
        if (cand == tgt) return true;
        if (cand.size() >= tgt.size() &&
            cand.compare(cand.size() - tgt.size(), tgt.size(), tgt) == 0) {
            return true;
        }
        return false;
    }

    static bool find_noise_shred(AudioContext& audio, t_CKUINT& outId) {
        if (!audio.chuck) return false;
        auto* vm = audio.chuck->vm();
        if (!vm) return false;
        std::vector<Chuck_VM_Shred*> shreds;
        vm->shreduler()->get_all_shreds(shreds);
        for (auto* shred : shreds) {
            if (!shred || !shred->code_orig) continue;
            if (path_matches(shred->code_orig->filename, audio.chuckNoiseScript)) {
                outId = shred->get_id();
                return true;
            }
        }
        return false;
    }

    void StopNoiseShred(BaseSystem& baseSystem) {
        if (!baseSystem.audio || !baseSystem.audio->chuck) return;
        AudioContext& audio = *baseSystem.audio;
        auto* vm = audio.chuck->vm();
        if (!vm) return;
        std::vector<Chuck_VM_Shred*> shreds;
        vm->shreduler()->get_all_shreds(shreds);
        for (auto* shred : shreds) {
            if (!shred || !shred->code_orig) continue;
            if (path_matches(shred->code_orig->filename, audio.chuckNoiseScript)) {
                vm->shreduler()->remove(shred);
            }
        }
        audio.chuckNoiseShredId = 0;
        audio.chuckNoiseShouldRun = false;
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
        {
            t_CKUINT existingId = 0;
            bool hasNoise = find_noise_shred(audio, existingId);
            if (audio.chuckNoiseShouldRun) {
                if (!hasNoise) {
                    compile_script(audio, audio.chuckNoiseScript, audio.chuckNoiseShredId);
                } else {
                    audio.chuckNoiseShredId = existingId;
                }
            } else {
                if (hasNoise) {
                    StopNoiseShred(baseSystem);
                } else {
                    audio.chuckNoiseShredId = 0;
                }
            }
        }
    }

}
