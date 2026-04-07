#pragma once
#include <fstream>
#include <iostream>
#include <algorithm>

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
        // Called from the game thread; defer shred scheduling to the VM/audio thread.
        bool ok = audio.chuck->compileFile(path, "", 1, FALSE, &ids);
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

    static bool find_script_shred(AudioContext& audio, const std::string& scriptPath, t_CKUINT& outId) {
        if (!audio.chuck) return false;
        auto* vm = audio.chuck->vm();
        if (!vm) return false;
        std::vector<Chuck_VM_Shred*> shreds;
        vm->shreduler()->get_all_shreds(shreds);
        for (auto* shred : shreds) {
            if (!shred || !shred->code_orig) continue;
            if (path_matches(shred->code_orig->filename, scriptPath)) {
                outId = shred->get_id();
                return true;
            }
        }
        return false;
    }

    static int count_script_shreds(AudioContext& audio, const std::string& scriptPath) {
        if (!audio.chuck) return 0;
        auto* vm = audio.chuck->vm();
        if (!vm) return 0;
        std::vector<Chuck_VM_Shred*> shreds;
        vm->shreduler()->get_all_shreds(shreds);
        int count = 0;
        for (auto* shred : shreds) {
            if (!shred || !shred->code_orig) continue;
            if (path_matches(shred->code_orig->filename, scriptPath)) {
                ++count;
            }
        }
        return count;
    }

    static bool is_sparkle_wrapper_path(const std::string& path) {
        return path.find(".salamander_sparkle_wrapped_") != std::string::npos;
    }

    static bool is_sparkle_source_path(const std::string& path) {
        return path.find("ice5_growth.ck") != std::string::npos;
    }

    static bool is_transient_gameplay_path(const std::string& path) {
        return path.find("Procedures/chuck/gameplay/") != std::string::npos
            || path.find("Procedures/chuck/fishing/") != std::string::npos;
    }

    static int read_registry_int(const BaseSystem& baseSystem, const std::string& key, int fallback) {
        if (!baseSystem.registry) return fallback;
        auto it = baseSystem.registry->find(key);
        if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
        try {
            return std::stoi(std::get<std::string>(it->second));
        } catch (...) {
            return fallback;
        }
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

    void StopSoundtrackChuckShred(BaseSystem& baseSystem) {
        if (!baseSystem.audio || !baseSystem.audio->chuck) return;
        AudioContext& audio = *baseSystem.audio;
        auto* vm = audio.chuck->vm();
        if (!vm) return;
        if (audio.soundtrackChuckShredId) {
            if (auto* sh = vm->shreduler()->lookup(audio.soundtrackChuckShredId)) {
                vm->shreduler()->remove(sh);
            }
        } else if (!audio.soundtrackChuckScriptPath.empty()) {
            std::vector<Chuck_VM_Shred*> shreds;
            vm->shreduler()->get_all_shreds(shreds);
            for (auto* shred : shreds) {
                if (!shred || !shred->code_orig) continue;
                if (path_matches(shred->code_orig->filename, audio.soundtrackChuckScriptPath)) {
                    vm->shreduler()->remove(shred);
                }
            }
        }
        audio.soundtrackChuckShredId = 0;
        audio.soundtrackChuckActive = false;
        audio.soundtrackChuckGain = 0.0f;
        audio.soundtrackChuckStartRequested = false;
        audio.soundtrackChuckStopRequested = false;
    }

    void StopSparkleRayChuckShred(BaseSystem& baseSystem) {
        if (!baseSystem.audio || !baseSystem.audio->chuck) return;
        AudioContext& audio = *baseSystem.audio;
        auto* vm = audio.chuck->vm();
        if (!vm) return;
        const std::string sparkleScriptPath = audio.sparkleRayChuckScriptPath;
        if (audio.sparkleRayChuckShredId) {
            if (auto* sh = vm->shreduler()->lookup(audio.sparkleRayChuckShredId)) {
                vm->shreduler()->remove(sh);
            }
        }
        std::vector<Chuck_VM_Shred*> shreds;
        vm->shreduler()->get_all_shreds(shreds);
        for (auto* shred : shreds) {
            if (!shred || !shred->code_orig) continue;
            const std::string filename = shred->code_orig->filename;
            const bool matchesTracked = !sparkleScriptPath.empty() && path_matches(filename, sparkleScriptPath);
            if (matchesTracked || is_sparkle_wrapper_path(filename)) {
                vm->shreduler()->remove(shred);
            }
        }
        audio.sparkleRayChuckShredId = 0;
        audio.sparkleRayChuckActive = false;
        audio.sparkleRayChuckStartRequested = false;
        audio.sparkleRayChuckStopRequested = false;
        audio.sparkleRayChuckScriptPath.clear();
    }

    // Update loop: handle pending compile/bypass requests
    void UpdateChucK(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.audio || !baseSystem.audio->chuck) return;
        AudioContext& audio = *baseSystem.audio;

        if (audio.chuckBypass) {
            if (audio.chuckMainShredId) {
                if (auto* sh = audio.chuck->vm()->shreduler()->lookup(audio.chuckMainShredId)) {
                    audio.chuck->vm()->shreduler()->remove(sh);
                }
                audio.chuckMainShredId = 0;
            }
            if (audio.chuckHeadShredId) {
                if (auto* sh = audio.chuck->vm()->shreduler()->lookup(audio.chuckHeadShredId)) {
                    audio.chuck->vm()->shreduler()->remove(sh);
                }
                audio.chuckHeadShredId = 0;
            }
            if (audio.chuckNoiseShredId) {
                if (auto* sh = audio.chuck->vm()->shreduler()->lookup(audio.chuckNoiseShredId)) {
                    audio.chuck->vm()->shreduler()->remove(sh);
                }
                audio.chuckNoiseShredId = 0;
            }
            StopSoundtrackChuckShred(baseSystem);
            StopSparkleRayChuckShred(baseSystem);
            audio.chuckMainActiveShredCount.store(0, std::memory_order_relaxed);
            audio.chuckMainHasActiveShreds.store(false, std::memory_order_relaxed);
            audio.chuckHeadActiveShredCount.store(0, std::memory_order_relaxed);
            audio.chuckHeadHasActiveShreds.store(false, std::memory_order_relaxed);
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

            static std::time_t lastHeadMTime = 0;
            if (file_mtime(audio.chuckHeadScript, m) && m != lastHeadMTime) {
                audio.chuckHeadCompileRequested = true;
                lastHeadMTime = m;
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

        if (audio.chuckHeadCompileRequested) {
            if (audio.chuckHeadShredId) {
                if (auto* sh = audio.chuck->vm()->shreduler()->lookup(audio.chuckHeadShredId)) {
                    audio.chuck->vm()->shreduler()->remove(sh);
                }
                audio.chuckHeadShredId = 0;
            }
            compile_script(audio, audio.chuckHeadScript, audio.chuckHeadShredId);
            audio.chuckHeadCompileRequested = false;
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

        if (audio.soundtrackChuckStopRequested) {
            StopSoundtrackChuckShred(baseSystem);
        }

        if (audio.soundtrackChuckStartRequested) {
            StopSoundtrackChuckShred(baseSystem);
            if (!audio.soundtrackChuckScriptPath.empty()) {
                if (!compile_script(audio, audio.soundtrackChuckScriptPath, audio.soundtrackChuckShredId)) {
                    audio.soundtrackChuckShredId = 0;
                    audio.soundtrackChuckActive = false;
                    audio.soundtrackChuckGain = 0.0f;
                } else {
                    audio.soundtrackChuckActive = true;
                }
            } else {
                audio.soundtrackChuckShredId = 0;
                audio.soundtrackChuckActive = false;
                audio.soundtrackChuckGain = 0.0f;
            }
            audio.soundtrackChuckStartRequested = false;
        }

        if (audio.soundtrackChuckShredId != 0) {
            if (!audio.chuck->vm()->shreduler()->lookup(audio.soundtrackChuckShredId)) {
                audio.soundtrackChuckShredId = 0;
                audio.soundtrackChuckActive = false;
                audio.soundtrackChuckGain = 0.0f;
            }
        } else if (audio.soundtrackChuckActive && !audio.soundtrackChuckScriptPath.empty()) {
            t_CKUINT existingId = 0;
            if (find_script_shred(audio, audio.soundtrackChuckScriptPath, existingId)) {
                audio.soundtrackChuckShredId = existingId;
            } else {
                audio.soundtrackChuckActive = false;
                audio.soundtrackChuckGain = 0.0f;
            }
        }

        if (audio.sparkleRayChuckStopRequested) {
            StopSparkleRayChuckShred(baseSystem);
        }

        if (audio.sparkleRayChuckStartRequested) {
            StopSparkleRayChuckShred(baseSystem);
            if (!audio.sparkleRayChuckScriptPath.empty()) {
                if (!compile_script(audio, audio.sparkleRayChuckScriptPath, audio.sparkleRayChuckShredId)) {
                    audio.sparkleRayChuckShredId = 0;
                    audio.sparkleRayChuckActive = false;
                } else {
                    audio.sparkleRayChuckActive = true;
                }
            } else {
                audio.sparkleRayChuckShredId = 0;
                audio.sparkleRayChuckActive = false;
            }
            audio.sparkleRayChuckStartRequested = false;
        }

        if (audio.sparkleRayChuckShredId != 0) {
            if (!audio.chuck->vm()->shreduler()->lookup(audio.sparkleRayChuckShredId)) {
                audio.sparkleRayChuckShredId = 0;
                audio.sparkleRayChuckActive = false;
            }
        } else if (audio.sparkleRayChuckActive && !audio.sparkleRayChuckScriptPath.empty()) {
            t_CKUINT existingId = 0;
            if (find_script_shred(audio, audio.sparkleRayChuckScriptPath, existingId)) {
                audio.sparkleRayChuckShredId = existingId;
            } else {
                audio.sparkleRayChuckActive = false;
            }
        }

        if (audio.sparkleRayChuckActive && !audio.sparkleRayChuckScriptPath.empty()) {
            std::vector<Chuck_VM_Shred*> shreds;
            audio.chuck->vm()->shreduler()->get_all_shreds(shreds);
            Chuck_VM_Shred* keep = nullptr;
            for (auto* shred : shreds) {
                if (!shred || !shred->code_orig) continue;
                const std::string filename = shred->code_orig->filename;
                if (!path_matches(filename, audio.sparkleRayChuckScriptPath)
                    && !is_sparkle_wrapper_path(filename)
                    && !is_sparkle_source_path(filename)) {
                    continue;
                }
                if (!keep) {
                    keep = shred;
                } else {
                    audio.chuck->vm()->shreduler()->remove(shred);
                }
            }
            if (keep) {
                audio.sparkleRayChuckShredId = keep->get_id();
            }
        }

        // Fail-safe: if sparkle routing is disabled, there should be no sparkle shreds alive.
        if (!audio.sparkleRayEnabled) {
            std::vector<Chuck_VM_Shred*> shreds;
            audio.chuck->vm()->shreduler()->get_all_shreds(shreds);
            for (auto* shred : shreds) {
                if (!shred || !shred->code_orig) continue;
                const std::string filename = shred->code_orig->filename;
                if (is_sparkle_wrapper_path(filename) || is_sparkle_source_path(filename)) {
                    audio.chuck->vm()->shreduler()->remove(shred);
                }
            }
            audio.sparkleRayChuckShredId = 0;
            audio.sparkleRayChuckActive = false;
            audio.sparkleRayChuckStartRequested = false;
            audio.sparkleRayChuckStopRequested = false;
            audio.sparkleRayChuckScriptPath.clear();
        }

        {
            const int maxTransientShreds = std::clamp(
                read_registry_int(baseSystem, "ChuckMaxTransientGameplayShreds", 24),
                1,
                256
            );
            std::vector<Chuck_VM_Shred*> shreds;
            audio.chuck->vm()->shreduler()->get_all_shreds(shreds);
            std::vector<t_CKUINT> transientIds;
            transientIds.reserve(shreds.size());
            for (auto* shred : shreds) {
                if (!shred || !shred->code_orig) continue;
                const std::string filename = shred->code_orig->filename;
                if (!is_transient_gameplay_path(filename)) continue;
                if (!audio.soundtrackChuckScriptPath.empty() && path_matches(filename, audio.soundtrackChuckScriptPath)) continue;
                if (!audio.sparkleRayChuckScriptPath.empty() && path_matches(filename, audio.sparkleRayChuckScriptPath)) continue;
                transientIds.push_back(shred->get_id());
            }
            if (static_cast<int>(transientIds.size()) > maxTransientShreds) {
                std::sort(transientIds.begin(), transientIds.end());
                const int removeCount = static_cast<int>(transientIds.size()) - maxTransientShreds;
                for (int i = 0; i < removeCount; ++i) {
                    const t_CKUINT id = transientIds[static_cast<size_t>(i)];
                    if (auto* sh = audio.chuck->vm()->shreduler()->lookup(id)) {
                        audio.chuck->vm()->shreduler()->remove(sh);
                    }
                }
            }
        }

        int mainShredCount = count_script_shreds(audio, audio.chuckMainScript);
        audio.chuckMainActiveShredCount.store(mainShredCount, std::memory_order_relaxed);
        audio.chuckMainHasActiveShreds.store(mainShredCount > 0, std::memory_order_relaxed);
        int headShredCount = count_script_shreds(audio, audio.chuckHeadScript);
        audio.chuckHeadActiveShredCount.store(headShredCount, std::memory_order_relaxed);
        audio.chuckHeadHasActiveShreds.store(headShredCount > 0, std::memory_order_relaxed);
    }

}
