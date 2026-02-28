#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace DawClipSystemLogic {
    void RebuildTrackCacheFromClips(DawContext& daw, DawTrack& track);
}
namespace MidiWaveformSystemLogic {
    void RebuildWaveform(MidiTrack& track, float sampleRate);
}
namespace DawTrackSystemLogic {
    bool InsertTrackAt(BaseSystem& baseSystem, int trackIndex);
    bool RemoveTrackAt(BaseSystem& baseSystem, int trackIndex);
}
namespace MidiTrackSystemLogic {
    bool InsertTrackAt(BaseSystem& baseSystem, int trackIndex);
    bool RemoveTrackAt(BaseSystem& baseSystem, int trackIndex);
}
namespace AutomationTrackSystemLogic {
    bool InsertTrackAt(BaseSystem& baseSystem, int trackIndex);
    bool RemoveTrackAt(BaseSystem& baseSystem, int trackIndex);
}

namespace Vst3SystemLogic {
    bool ProcessEffectChainStereo(Vst3Context& ctx, Vst3TrackChain& chain,
                                  const float* inputLeft, const float* inputRight,
                                  float* outputLeft, float* outputRight,
                                  int numFrames, int64_t sampleOffset, bool playing);
    bool ProcessEffectChain(Vst3Context& ctx, Vst3TrackChain& chain, const float* inputMono,
                            float* outputMono, int numFrames, int64_t sampleOffset, bool playing);
    bool ProcessInstrument(Vst3Context& ctx, Vst3TrackChain& chain, Vst3Plugin& instrument, float* outputMono,
                           int numFrames, int64_t sampleOffset, bool playing,
                           const std::array<float, 128>& desiredHeldVelocities,
                           std::array<float, 128>& lastHeldVelocities);
    void EnsureAudioTrackCount(Vst3Context& ctx, int trackCount);
    void EnsureMidiTrackCount(Vst3Context& ctx, int trackCount);
    bool AddPluginToTrack(Vst3Context& ctx, const Vst3AvailablePlugin& available, int trackIndex, int audioTrackCount);
    bool RemovePluginFromTrack(Vst3Context& ctx, Vst3Plugin* plugin, int trackIndex, int audioTrackCount);
}

namespace DawIOSystemLogic {

    namespace {
        struct WavInfo {
            uint16_t audioFormat = 0;
            uint16_t numChannels = 0;
            uint32_t sampleRate = 0;
            uint16_t bitsPerSample = 0;
            uint32_t dataSize = 0;
            std::streampos dataPos = 0;
        };

        std::string getExecutableDir() {
#if defined(__APPLE__)
            uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            if (size > 0) {
                std::string buffer(size, '\0');
                if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
                    return std::filesystem::path(buffer.c_str()).parent_path().string();
                }
            }
#elif defined(__linux__)
            char buffer[4096] = {0};
            ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
            if (len > 0) {
                buffer[len] = '\0';
                return std::filesystem::path(buffer).parent_path().string();
            }
#endif
            return std::filesystem::current_path().string();
        }

        bool readChunkHeader(std::ifstream& file, char outId[4], uint32_t& outSize) {
            if (!file.read(outId, 4)) return false;
            if (!file.read(reinterpret_cast<char*>(&outSize), sizeof(outSize))) return false;
            return true;
        }

        bool readWavInfo(std::ifstream& file, WavInfo& info) {
            char riff[4] = {0};
            if (!file.read(riff, 4)) return false;
            uint32_t riffSize = 0;
            if (!file.read(reinterpret_cast<char*>(&riffSize), sizeof(riffSize))) return false;
            char wave[4] = {0};
            if (!file.read(wave, 4)) return false;
            if (std::string(riff, 4) != "RIFF" || std::string(wave, 4) != "WAVE") return false;

            bool fmtFound = false;
            bool dataFound = false;
            while (file && (!fmtFound || !dataFound)) {
                char chunkId[4] = {0};
                uint32_t chunkSize = 0;
                if (!readChunkHeader(file, chunkId, chunkSize)) break;
                std::string id(chunkId, 4);
                if (id == "fmt ") {
                    fmtFound = true;
                    uint16_t audioFormat = 0;
                    uint16_t numChannels = 0;
                    uint32_t sampleRate = 0;
                    uint32_t byteRate = 0;
                    uint16_t blockAlign = 0;
                    uint16_t bitsPerSample = 0;
                    file.read(reinterpret_cast<char*>(&audioFormat), sizeof(audioFormat));
                    file.read(reinterpret_cast<char*>(&numChannels), sizeof(numChannels));
                    file.read(reinterpret_cast<char*>(&sampleRate), sizeof(sampleRate));
                    file.read(reinterpret_cast<char*>(&byteRate), sizeof(byteRate));
                    file.read(reinterpret_cast<char*>(&blockAlign), sizeof(blockAlign));
                    file.read(reinterpret_cast<char*>(&bitsPerSample), sizeof(bitsPerSample));
                    if (chunkSize > 16) {
                        file.seekg(chunkSize - 16, std::ios::cur);
                    }
                    info.audioFormat = audioFormat;
                    info.numChannels = numChannels;
                    info.sampleRate = sampleRate;
                    info.bitsPerSample = bitsPerSample;
                } else if (id == "data") {
                    dataFound = true;
                    info.dataSize = chunkSize;
                    info.dataPos = file.tellg();
                    file.seekg(chunkSize, std::ios::cur);
                } else {
                    file.seekg(chunkSize, std::ios::cur);
                }
            }
            return fmtFound && dataFound;
        }

        bool loadWavMonoFloat(const std::string& path, std::vector<float>& outSamples, uint32_t& outRate) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) return false;
            WavInfo info;
            if (!readWavInfo(file, info)) return false;
            if (info.audioFormat != 3 || info.numChannels != 1 || info.bitsPerSample != 32) return false;
            if (info.dataSize == 0) return false;

            outRate = info.sampleRate;
            size_t sampleCount = info.dataSize / sizeof(float);
            outSamples.resize(sampleCount);
            file.seekg(info.dataPos);
            file.read(reinterpret_cast<char*>(outSamples.data()), static_cast<std::streamsize>(info.dataSize));
            return true;
        }

        bool loadWavClipAudio(const std::string& path, DawClipAudio& outClip, uint32_t& outRate) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) return false;
            WavInfo info;
            if (!readWavInfo(file, info)) return false;
            if (info.audioFormat != 3 || info.bitsPerSample != 32) return false;
            if (info.numChannels != 1 && info.numChannels != 2) return false;
            if (info.dataSize == 0) return false;

            outRate = info.sampleRate;
            file.seekg(info.dataPos);
            size_t sampleCount = info.dataSize / sizeof(float);
            if (info.numChannels == 1) {
                outClip.channels = 1;
                outClip.left.resize(sampleCount);
                outClip.right.clear();
                file.read(reinterpret_cast<char*>(outClip.left.data()), static_cast<std::streamsize>(info.dataSize));
                return true;
            }
            if (sampleCount < 2) return false;
            size_t frameCount = sampleCount / 2;
            std::vector<float> interleaved(sampleCount, 0.0f);
            file.read(reinterpret_cast<char*>(interleaved.data()), static_cast<std::streamsize>(info.dataSize));
            outClip.channels = 2;
            outClip.left.resize(frameCount);
            outClip.right.resize(frameCount);
            for (size_t i = 0; i < frameCount; ++i) {
                outClip.left[i] = interleaved[i * 2];
                outClip.right[i] = interleaved[i * 2 + 1];
            }
            return true;
        }

        bool writeWavMonoFloat(const std::string& path, const std::vector<float>& samples, uint32_t sampleRate) {
            std::ofstream file(path, std::ios::binary);
            if (!file.is_open()) return false;

            uint32_t dataSize = static_cast<uint32_t>(samples.size() * sizeof(float));
            uint32_t riffSize = 36 + dataSize;
            uint16_t audioFormat = 3;
            uint16_t numChannels = 1;
            uint16_t bitsPerSample = 32;
            uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);
            uint16_t blockAlign = numChannels * (bitsPerSample / 8);

            file.write("RIFF", 4);
            file.write(reinterpret_cast<const char*>(&riffSize), sizeof(riffSize));
            file.write("WAVE", 4);
            file.write("fmt ", 4);
            uint32_t fmtSize = 16;
            file.write(reinterpret_cast<const char*>(&fmtSize), sizeof(fmtSize));
            file.write(reinterpret_cast<const char*>(&audioFormat), sizeof(audioFormat));
            file.write(reinterpret_cast<const char*>(&numChannels), sizeof(numChannels));
            file.write(reinterpret_cast<const char*>(&sampleRate), sizeof(sampleRate));
            file.write(reinterpret_cast<const char*>(&byteRate), sizeof(byteRate));
            file.write(reinterpret_cast<const char*>(&blockAlign), sizeof(blockAlign));
            file.write(reinterpret_cast<const char*>(&bitsPerSample), sizeof(bitsPerSample));
            file.write("data", 4);
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            if (!samples.empty()) {
                file.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(dataSize));
            }
            return true;
        }

        bool writeWavClipAudio(const std::string& path, const DawClipAudio& clipAudio, uint32_t sampleRate) {
            if (clipAudio.left.empty()) return false;
            const bool stereo = (clipAudio.channels > 1) && !clipAudio.right.empty();
            std::ofstream file(path, std::ios::binary);
            if (!file.is_open()) return false;

            uint16_t numChannels = stereo ? 2 : 1;
            uint16_t bitsPerSample = 32;
            size_t frameCount = stereo ? std::min(clipAudio.left.size(), clipAudio.right.size()) : clipAudio.left.size();
            uint32_t dataSize = static_cast<uint32_t>(frameCount * numChannels * sizeof(float));
            uint32_t riffSize = 36 + dataSize;
            uint16_t audioFormat = 3;
            uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);
            uint16_t blockAlign = numChannels * (bitsPerSample / 8);

            file.write("RIFF", 4);
            file.write(reinterpret_cast<const char*>(&riffSize), sizeof(riffSize));
            file.write("WAVE", 4);
            file.write("fmt ", 4);
            uint32_t fmtSize = 16;
            file.write(reinterpret_cast<const char*>(&fmtSize), sizeof(fmtSize));
            file.write(reinterpret_cast<const char*>(&audioFormat), sizeof(audioFormat));
            file.write(reinterpret_cast<const char*>(&numChannels), sizeof(numChannels));
            file.write(reinterpret_cast<const char*>(&sampleRate), sizeof(sampleRate));
            file.write(reinterpret_cast<const char*>(&byteRate), sizeof(byteRate));
            file.write(reinterpret_cast<const char*>(&blockAlign), sizeof(blockAlign));
            file.write(reinterpret_cast<const char*>(&bitsPerSample), sizeof(bitsPerSample));
            file.write("data", 4);
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));

            if (stereo) {
                std::vector<float> interleaved(frameCount * 2, 0.0f);
                for (size_t i = 0; i < frameCount; ++i) {
                    interleaved[i * 2] = clipAudio.left[i];
                    interleaved[i * 2 + 1] = clipAudio.right[i];
                }
                file.write(reinterpret_cast<const char*>(interleaved.data()), static_cast<std::streamsize>(dataSize));
            } else {
                file.write(reinterpret_cast<const char*>(clipAudio.left.data()), static_cast<std::streamsize>(dataSize));
            }
            return true;
        }

        int64_t displayBarToZeroBased(int displayBar) {
            return (displayBar > 0) ? static_cast<int64_t>(displayBar - 1) : static_cast<int64_t>(displayBar);
        }

        int64_t zeroBasedToDisplayBar(int64_t zeroBased) {
            return (zeroBased >= 0) ? (zeroBased + 1) : zeroBased;
        }

        std::string trimLineEnd(std::string value) {
            while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) {
                value.pop_back();
            }
            return value;
        }

        std::string commandOutputFirstLine(const char* command) {
            if (!command || command[0] == '\0') return {};
            std::string out;
            FILE* pipe = popen(command, "r");
            if (!pipe) return out;
            char buffer[1024];
            if (fgets(buffer, static_cast<int>(sizeof(buffer)), pipe)) {
                out = buffer;
            }
            pclose(pipe);
            return trimLineEnd(out);
        }

        std::string sanitizeStemName(const std::string& rawName, int fallbackIndex) {
            std::string out = rawName;
            if (out.empty()) {
                out = "stem_" + std::to_string(fallbackIndex + 1);
            }
            for (char& ch : out) {
                switch (ch) {
                    case '/':
                    case '\\':
                    case ':':
                    case '*':
                    case '?':
                    case '\"':
                    case '<':
                    case '>':
                    case '|':
                        ch = '_';
                        break;
                    default:
                        break;
                }
            }
            if (out.empty()) out = "stem_" + std::to_string(fallbackIndex + 1);
            return out;
        }

        constexpr const char* kSessionFormat = "salamander_session";
        constexpr int kSessionVersion = 1;
        constexpr const char* kSessionExtension = ".salmproj";
        constexpr const char* kThemeFormat = "salamander_theme_presets";
        constexpr int kThemeVersion = 1;
        constexpr const char* kThemeFileName = "daw_themes.json";
        constexpr float kDefaultLaneAlpha = 0.85f;
        constexpr float kDefaultPanelAlpha = 0.80f;
        constexpr float kDefaultButtonAlpha = 0.85f;
        constexpr float kDefaultThemeColorAlpha = 1.0f;
        const glm::vec3 kDefaultLaneBase(230.0f / 255.0f, 224.0f / 255.0f, 212.0f / 255.0f);
        const glm::vec3 kDefaultPianoRollBase(0.0f, 0.12f, 0.12f);
        const glm::vec3 kDefaultPianoRollAccent(0.0f, 0.20f, 0.20f);

        bool readBool(const json& j, const char* key, bool fallback);
        std::string readString(const json& j, const char* key, const std::string& fallback);

        glm::vec4 clampThemeColor(const glm::vec4& color) {
            return glm::vec4(
                std::clamp(color.r, 0.0f, 1.0f),
                std::clamp(color.g, 0.0f, 1.0f),
                std::clamp(color.b, 0.0f, 1.0f),
                std::clamp(color.a, 0.0f, 1.0f)
            );
        }

        glm::vec3 clampColorOffset(const glm::vec3& color, float offset) {
            return glm::clamp(color + glm::vec3(offset), glm::vec3(0.0f), glm::vec3(1.0f));
        }

        bool isThemeDeleteProtected(const DawThemePreset& preset) {
            return preset.name == "Default"
                || preset.name == "Default 2"
                || preset.name == "Default 3";
        }

        std::string rgbaToHex(const glm::vec4& color) {
            glm::vec4 c = clampThemeColor(color);
            auto toByte = [](float v) -> int {
                return std::clamp(static_cast<int>(std::lround(v * 255.0f)), 0, 255);
            };
            char out[9] = {0};
            std::snprintf(out,
                          sizeof(out),
                          "%02X%02X%02X%02X",
                          toByte(c.r),
                          toByte(c.g),
                          toByte(c.b),
                          toByte(c.a));
            return std::string(out);
        }

        bool parseHexNibble(char c, int& out) {
            if (c >= '0' && c <= '9') {
                out = c - '0';
                return true;
            }
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (c >= 'A' && c <= 'F') {
                out = 10 + (c - 'A');
                return true;
            }
            return false;
        }

        bool parseHexByte(const std::string& value, size_t index, int& out) {
            if (index + 1 >= value.size()) return false;
            int hi = 0;
            int lo = 0;
            if (!parseHexNibble(value[index], hi)) return false;
            if (!parseHexNibble(value[index + 1], lo)) return false;
            out = (hi << 4) | lo;
            return true;
        }

        bool parseRgbaHex(std::string value, glm::vec4& out) {
            if (value.empty()) return false;
            if (!value.empty() && value[0] == '#') {
                value.erase(value.begin());
            }
            if (value.size() != 8) return false;
            int r = 0;
            int g = 0;
            int b = 0;
            int a = 0;
            if (!parseHexByte(value, 0, r)) return false;
            if (!parseHexByte(value, 2, g)) return false;
            if (!parseHexByte(value, 4, b)) return false;
            if (!parseHexByte(value, 6, a)) return false;
            out = glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
            return true;
        }

        bool findDawScreenColor(const BaseSystem& baseSystem, glm::vec3& outColor) {
            if (!baseSystem.level) return false;
            for (const auto& worldEntity : baseSystem.level->worlds) {
                if (worldEntity.name != "DAWScreenWorld") continue;
                for (const auto& inst : worldEntity.instances) {
                    if (inst.name != "Screen") continue;
                    outColor = inst.color;
                    return true;
                }
            }
            return false;
        }

        void applyDawScreenColor(BaseSystem& baseSystem, const glm::vec3& color) {
            if (!baseSystem.level) return;
            for (auto& worldEntity : baseSystem.level->worlds) {
                if (worldEntity.name != "DAWScreenWorld") continue;
                for (auto& inst : worldEntity.instances) {
                    if (inst.name != "Screen") continue;
                    inst.color = color;
                }
            }
        }

        DawThemePreset makeDefaultThemePreset(const BaseSystem& baseSystem) {
            DawThemePreset preset;
            preset.name = "Default";
            preset.isBuiltin = true;
            glm::vec3 backgroundRgb(0.22f, 0.53f, 0.53f);
            glm::vec3 panelRgb(0.90f, 0.88f, 0.83f);
            glm::vec3 buttonRgb(0.90f, 0.88f, 0.83f);
            glm::vec3 pianoRollRgb = kDefaultPianoRollBase;
            glm::vec3 pianoRollAccentRgb = kDefaultPianoRollAccent;
            glm::vec3 laneRgb = kDefaultLaneBase;
            if (baseSystem.world) {
                auto itBg = baseSystem.world->colorLibrary.find("Teal");
                if (itBg != baseSystem.world->colorLibrary.end()) {
                    backgroundRgb = itBg->second;
                }
            } else if (findDawScreenColor(baseSystem, backgroundRgb)) {
                // Keep instance fallback only when color library is unavailable.
            }
            if (baseSystem.world) {
                auto itButton = baseSystem.world->colorLibrary.find("ButtonFront");
                if (itButton != baseSystem.world->colorLibrary.end()) {
                    panelRgb = itButton->second;
                    buttonRgb = itButton->second;
                }
                auto itPiano = baseSystem.world->colorLibrary.find("MiraPianoRollBase");
                if (itPiano != baseSystem.world->colorLibrary.end()) {
                    pianoRollRgb = itPiano->second;
                }
                auto itPianoAccent = baseSystem.world->colorLibrary.find("MiraPianoRollAccent");
                if (itPianoAccent != baseSystem.world->colorLibrary.end()) {
                    pianoRollAccentRgb = itPianoAccent->second;
                }
                auto itLane = baseSystem.world->colorLibrary.find("MiraLaneBase");
                if (itLane != baseSystem.world->colorLibrary.end()) {
                    laneRgb = itLane->second;
                }
            }
            preset.background = glm::vec4(backgroundRgb, kDefaultLaneAlpha);
            preset.panel = glm::vec4(panelRgb, kDefaultPanelAlpha);
            preset.button = glm::vec4(buttonRgb, kDefaultButtonAlpha);
            preset.pianoRoll = glm::vec4(pianoRollRgb, kDefaultThemeColorAlpha);
            preset.pianoRollAccent = glm::vec4(pianoRollAccentRgb, kDefaultThemeColorAlpha);
            preset.lane = glm::vec4(laneRgb, kDefaultThemeColorAlpha);
            return preset;
        }

        DawThemePreset makeBuiltinThemePreset(const std::string& name) {
            DawThemePreset preset;
            preset.name = name;
            preset.isBuiltin = true;
            if (name == "Default 2") {
                preset.background = glm::vec4(26.0f / 255.0f, 13.0f / 255.0f, 13.0f / 255.0f, 230.0f / 255.0f);
                preset.panel = glm::vec4(185.0f / 255.0f, 35.0f / 255.0f, 35.0f / 255.0f, 204.0f / 255.0f);
                preset.button = glm::vec4(204.0f / 255.0f, 38.0f / 255.0f, 38.0f / 255.0f, 204.0f / 255.0f);
                preset.pianoRoll = glm::vec4(43.0f / 255.0f, 14.0f / 255.0f, 14.0f / 255.0f, 1.0f);
                preset.pianoRollAccent = glm::vec4(122.0f / 255.0f, 26.0f / 255.0f, 26.0f / 255.0f, 1.0f);
                preset.lane = glm::vec4(204.0f / 255.0f, 90.0f / 255.0f, 90.0f / 255.0f, 1.0f);
                return preset;
            }
            if (name == "Default 3") {
                preset.background = glm::vec4(14.0f / 255.0f, 22.0f / 255.0f, 32.0f / 255.0f, 230.0f / 255.0f);
                preset.panel = glm::vec4(199.0f / 255.0f, 146.0f / 255.0f, 60.0f / 255.0f, 204.0f / 255.0f);
                preset.button = glm::vec4(230.0f / 255.0f, 179.0f / 255.0f, 95.0f / 255.0f, 204.0f / 255.0f);
                preset.pianoRoll = glm::vec4(20.0f / 255.0f, 34.0f / 255.0f, 51.0f / 255.0f, 1.0f);
                preset.pianoRollAccent = glm::vec4(39.0f / 255.0f, 71.0f / 255.0f, 102.0f / 255.0f, 1.0f);
                preset.lane = glm::vec4(228.0f / 255.0f, 210.0f / 255.0f, 178.0f / 255.0f, 1.0f);
                return preset;
            }
            return preset;
        }

        std::filesystem::path themeFilePath() {
            std::filesystem::path path = std::filesystem::path(getExecutableDir()) / "Procedures" / kThemeFileName;
            if (std::filesystem::exists(path.parent_path())) {
                return path;
            }
            return std::filesystem::current_path() / "Procedures" / kThemeFileName;
        }

        json serializeThemePreset(const DawThemePreset& preset) {
            json out = json::object();
            out["name"] = preset.name;
            out["background"] = rgbaToHex(preset.background);
            out["panel"] = rgbaToHex(preset.panel);
            out["button"] = rgbaToHex(preset.button);
            out["piano_roll"] = rgbaToHex(preset.pianoRoll);
            out["piano_roll_accent"] = rgbaToHex(preset.pianoRollAccent);
            out["lane"] = rgbaToHex(preset.lane);
            out["builtin"] = preset.isBuiltin;
            return out;
        }

        bool deserializeThemePreset(const json& in, DawThemePreset& out) {
            if (!in.is_object()) return false;
            const std::string name = readString(in, "name", "");
            if (name.empty()) return false;
            glm::vec4 background{};
            glm::vec4 panel{};
            glm::vec4 button{};
            glm::vec4 pianoRoll = glm::vec4(kDefaultPianoRollBase, kDefaultThemeColorAlpha);
            glm::vec4 pianoRollAccent = glm::vec4(kDefaultPianoRollAccent, kDefaultThemeColorAlpha);
            glm::vec4 lane = glm::vec4(kDefaultLaneBase, kDefaultThemeColorAlpha);
            if (!parseRgbaHex(readString(in, "background", ""), background)) return false;
            if (!parseRgbaHex(readString(in, "panel", ""), panel)) return false;
            if (!parseRgbaHex(readString(in, "button", ""), button)) return false;
            const std::string pianoRollHex = readString(in, "piano_roll", readString(in, "pianoRoll", ""));
            const std::string pianoRollAccentHex = readString(in, "piano_roll_accent", readString(in, "pianoRollAccent", ""));
            const std::string laneHex = readString(in, "lane", "");
            if (!pianoRollHex.empty() && !parseRgbaHex(pianoRollHex, pianoRoll)) return false;
            if (!pianoRollAccentHex.empty() && !parseRgbaHex(pianoRollAccentHex, pianoRollAccent)) return false;
            if (!laneHex.empty() && !parseRgbaHex(laneHex, lane)) return false;
            out.name = name;
            out.background = clampThemeColor(background);
            out.panel = clampThemeColor(panel);
            out.button = clampThemeColor(button);
            out.pianoRoll = clampThemeColor(pianoRoll);
            out.pianoRollAccent = clampThemeColor(pianoRollAccent);
            out.lane = clampThemeColor(lane);
            out.isBuiltin = readBool(in, "builtin", false);
            return true;
        }

        int findThemeIndexByName(const DawContext& daw, const std::string& name) {
            if (name.empty()) return -1;
            for (int i = 0; i < static_cast<int>(daw.themes.size()); ++i) {
                if (daw.themes[static_cast<size_t>(i)].name == name) {
                    return i;
                }
            }
            return -1;
        }

        void applyThemeToWorld(BaseSystem& baseSystem, DawContext& daw, const DawThemePreset& preset) {
            const DawThemePreset safe = {
                preset.name,
                clampThemeColor(preset.background),
                clampThemeColor(preset.panel),
                clampThemeColor(preset.button),
                clampThemeColor(preset.pianoRoll),
                clampThemeColor(preset.pianoRollAccent),
                clampThemeColor(preset.lane),
                preset.isBuiltin
            };
            daw.themeRevision += 1;
            daw.activeThemeBackground = safe.background;
            daw.activeThemePanel = safe.panel;
            daw.activeThemeButton = safe.button;
            daw.activeThemePianoRoll = safe.pianoRoll;
            daw.activeThemePianoRollAccent = safe.pianoRollAccent;
            daw.activeThemeLane = safe.lane;
            daw.selectedThemeName = safe.name;
            WorldContext* world = baseSystem.world ? baseSystem.world.get() : nullptr;
            if (!world) {
                daw.themeAppliedToWorld = false;
                return;
            }

            const glm::vec3 bg = glm::vec3(safe.background);
            world->colorLibrary["Teal"] = bg;
            applyDawScreenColor(baseSystem, bg);

            const glm::vec3 pianoRoll = glm::vec3(safe.pianoRoll);
            const glm::vec3 pianoRollAccent = glm::vec3(safe.pianoRollAccent);
            world->colorLibrary["MiraPianoRollBase"] = pianoRoll;
            world->colorLibrary["MiraPianoRollAccent"] = pianoRollAccent;

            const glm::vec3 laneBase = glm::vec3(safe.lane);
            const glm::vec3 laneHighlight = clampColorOffset(laneBase, 0.07f);
            const glm::vec3 laneShadow = clampColorOffset(laneBase, -0.10f);
            world->colorLibrary["MiraLaneBase"] = laneBase;
            world->colorLibrary["MiraLaneHighlight"] = laneHighlight;
            world->colorLibrary["MiraLaneShadow"] = laneShadow;

            const glm::vec3 button = glm::vec3(safe.button);
            world->colorLibrary["ButtonFront"] = button;
            world->colorLibrary["ButtonTopHighlight"] = clampColorOffset(button, 0.10f);
            world->colorLibrary["ButtonSideShadow"] = clampColorOffset(button, -0.03f);
            daw.themeAppliedToWorld = true;
        }

        bool writeThemesToDisk(const DawContext& daw) {
            std::filesystem::path path = themeFilePath();
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) return false;
            json root = json::object();
            root["format"] = kThemeFormat;
            root["version"] = kThemeVersion;
            root["selected_theme"] = daw.selectedThemeName;
            json list = json::array();
            for (const auto& theme : daw.themes) {
                list.push_back(serializeThemePreset(theme));
            }
            root["themes"] = std::move(list);
            std::ofstream out(path);
            if (!out.is_open()) return false;
            out << root.dump(2);
            return out.good();
        }

        void fillThemeDraftFromPreset(DawContext& daw, const DawThemePreset& preset, bool keepName) {
            daw.themeDraftName = keepName ? daw.themeDraftName : preset.name;
            daw.themeDraftBackgroundHex = rgbaToHex(preset.background);
            daw.themeDraftPanelHex = rgbaToHex(preset.panel);
            daw.themeDraftButtonHex = rgbaToHex(preset.button);
            daw.themeDraftPianoRollHex = rgbaToHex(preset.pianoRoll);
            daw.themeDraftPianoRollAccentHex = rgbaToHex(preset.pianoRollAccent);
            daw.themeDraftLaneHex = rgbaToHex(preset.lane);
        }

        std::string ensureSessionExtension(const std::string& path) {
            if (path.empty()) return path;
            std::filesystem::path fsPath(path);
            if (fsPath.extension() == kSessionExtension) return fsPath.string();
            fsPath += kSessionExtension;
            return fsPath.string();
        }

        std::string defaultAssetDirName(const std::filesystem::path& sessionPath) {
            std::string stem = sessionPath.stem().string();
            if (stem.empty()) stem = "Untitled";
            return stem + ".salmassets";
        }

        uint64_t readU64(const json& j, const char* key, uint64_t fallback = 0) {
            if (!j.is_object()) return fallback;
            auto it = j.find(key);
            if (it == j.end() || !it->is_number_unsigned()) return fallback;
            return it->get<uint64_t>();
        }

        int readInt(const json& j, const char* key, int fallback = 0) {
            if (!j.is_object()) return fallback;
            auto it = j.find(key);
            if (it == j.end() || !it->is_number_integer()) return fallback;
            return it->get<int>();
        }

        float readFloat(const json& j, const char* key, float fallback = 0.0f) {
            if (!j.is_object()) return fallback;
            auto it = j.find(key);
            if (it == j.end() || !it->is_number()) return fallback;
            return it->get<float>();
        }

        double readDouble(const json& j, const char* key, double fallback = 0.0) {
            if (!j.is_object()) return fallback;
            auto it = j.find(key);
            if (it == j.end() || !it->is_number()) return fallback;
            return it->get<double>();
        }

        bool readBool(const json& j, const char* key, bool fallback = false) {
            if (!j.is_object()) return fallback;
            auto it = j.find(key);
            if (it == j.end() || !it->is_boolean()) return fallback;
            return it->get<bool>();
        }

        std::string readString(const json& j, const char* key, const std::string& fallback = {}) {
            if (!j.is_object()) return fallback;
            auto it = j.find(key);
            if (it == j.end() || !it->is_string()) return fallback;
            return it->get<std::string>();
        }

        json serializeDawClip(const DawClip& clip) {
            json out = json::object();
            out["audio_id"] = clip.audioId;
            out["start_sample"] = clip.startSample;
            out["length"] = clip.length;
            out["source_offset"] = clip.sourceOffset;
            out["take_id"] = clip.takeId;
            return out;
        }

        DawClip deserializeDawClip(const json& in) {
            DawClip clip{};
            clip.audioId = readInt(in, "audio_id", -1);
            clip.startSample = readU64(in, "start_sample", 0);
            clip.length = readU64(in, "length", 0);
            clip.sourceOffset = readU64(in, "source_offset", 0);
            clip.takeId = readInt(in, "take_id", -1);
            return clip;
        }

        json serializeMidiNote(const MidiNote& note) {
            json out = json::object();
            out["pitch"] = note.pitch;
            out["start_sample"] = note.startSample;
            out["length"] = note.length;
            out["velocity"] = note.velocity;
            return out;
        }

        MidiNote deserializeMidiNote(const json& in) {
            MidiNote note{};
            note.pitch = readInt(in, "pitch", 0);
            note.startSample = readU64(in, "start_sample", 0);
            note.length = readU64(in, "length", 0);
            note.velocity = std::clamp(readFloat(in, "velocity", 1.0f), 0.0f, 1.0f);
            return note;
        }

        json serializeMidiClip(const MidiClip& clip) {
            json out = json::object();
            out["start_sample"] = clip.startSample;
            out["length"] = clip.length;
            out["take_id"] = clip.takeId;
            json notes = json::array();
            for (const auto& note : clip.notes) {
                notes.push_back(serializeMidiNote(note));
            }
            out["notes"] = std::move(notes);
            return out;
        }

        MidiClip deserializeMidiClip(const json& in) {
            MidiClip clip{};
            clip.startSample = readU64(in, "start_sample", 0);
            clip.length = readU64(in, "length", 0);
            clip.takeId = readInt(in, "take_id", -1);
            auto itNotes = in.find("notes");
            if (itNotes != in.end() && itNotes->is_array()) {
                clip.notes.reserve(itNotes->size());
                for (const auto& item : *itNotes) {
                    clip.notes.push_back(deserializeMidiNote(item));
                }
            }
            return clip;
        }

        json serializeAutomationClip(const AutomationClip& clip) {
            json out = json::object();
            out["start_sample"] = clip.startSample;
            out["length"] = clip.length;
            out["take_id"] = clip.takeId;
            json points = json::array();
            for (const auto& point : clip.points) {
                json p = json::object();
                p["offset_sample"] = point.offsetSample;
                p["value"] = point.value;
                points.push_back(std::move(p));
            }
            out["points"] = std::move(points);
            return out;
        }

        AutomationClip deserializeAutomationClip(const json& in) {
            AutomationClip clip{};
            clip.startSample = readU64(in, "start_sample", 0);
            clip.length = readU64(in, "length", 0);
            clip.takeId = readInt(in, "take_id", -1);
            auto itPoints = in.find("points");
            if (itPoints != in.end() && itPoints->is_array()) {
                clip.points.reserve(itPoints->size());
                for (const auto& item : *itPoints) {
                    AutomationPoint point{};
                    point.offsetSample = readU64(item, "offset_sample", 0);
                    point.value = std::clamp(readFloat(item, "value", 0.5f), 0.0f, 1.0f);
                    clip.points.push_back(point);
                }
            }
            return clip;
        }

        json serializePlugin(const Vst3Plugin* plugin) {
            json out = json::object();
            if (!plugin) return out;
            out["name"] = plugin->name;
            out["class_id"] = plugin->classInfo.ID().toString();
            out["is_instrument"] = plugin->isInstrument;
            json params = json::array();
            if (plugin->controller) {
                int32_t count = plugin->controller->getParameterCount();
                for (int32_t i = 0; i < count; ++i) {
                    Steinberg::Vst::ParameterInfo info{};
                    if (plugin->controller->getParameterInfo(i, info) != Steinberg::kResultOk) continue;
                    json pj = json::object();
                    pj["id"] = static_cast<int>(info.id);
                    pj["value"] = static_cast<double>(plugin->controller->getParamNormalized(info.id));
                    params.push_back(std::move(pj));
                }
            }
            out["parameters"] = std::move(params);
            return out;
        }

        void applyPluginParameters(Vst3Plugin* plugin, const json& saved) {
            if (!plugin || !plugin->controller) return;
            auto itParams = saved.find("parameters");
            if (itParams == saved.end() || !itParams->is_array()) return;
            for (const auto& item : *itParams) {
                if (!item.is_object()) continue;
                auto itId = item.find("id");
                auto itValue = item.find("value");
                if (itId == item.end() || itValue == item.end()) continue;
                if (!itId->is_number_integer() || !itValue->is_number()) continue;
                auto id = static_cast<Steinberg::Vst::ParamID>(itId->get<int>());
                double value = std::clamp(itValue->get<double>(), 0.0, 1.0);
                plugin->controller->setParamNormalized(id, value);
            }
        }

        const Vst3AvailablePlugin* findAvailablePluginByState(const Vst3Context& ctx, const json& saved, bool instrument) {
            if (!saved.is_object()) return nullptr;
            std::string desiredClassId = readString(saved, "class_id", "");
            std::string desiredName = readString(saved, "name", "");
            const Vst3AvailablePlugin* fallback = nullptr;
            for (const auto& available : ctx.availablePlugins) {
                if (available.isInstrument != instrument) continue;
                if (!desiredClassId.empty() && available.classInfo.ID().toString() == desiredClassId) {
                    return &available;
                }
                if (!desiredName.empty() && available.name == desiredName && !fallback) {
                    fallback = &available;
                }
            }
            return fallback;
        }

        Vst3Plugin* findNewlyAddedPlugin(Vst3Context& ctx, int trackIndex, int audioTrackCount, bool instrument) {
            if (trackIndex < audioTrackCount) {
                if (trackIndex < 0 || trackIndex >= static_cast<int>(ctx.audioTracks.size())) return nullptr;
                auto& chain = ctx.audioTracks[static_cast<size_t>(trackIndex)].effects;
                if (chain.empty()) return nullptr;
                return chain.back();
            }
            int midiIndex = trackIndex - audioTrackCount;
            if (midiIndex < 0 || midiIndex >= static_cast<int>(ctx.midiTracks.size())) return nullptr;
            if (instrument) {
                if (midiIndex >= static_cast<int>(ctx.midiInstruments.size())) return nullptr;
                return ctx.midiInstruments[static_cast<size_t>(midiIndex)];
            }
            auto& chain = ctx.midiTracks[static_cast<size_t>(midiIndex)].effects;
            if (chain.empty()) return nullptr;
            return chain.back();
        }

        void clearAllVst3TrackPlugins(Vst3Context& ctx) {
            int currentAudioTrackCount = static_cast<int>(ctx.audioTracks.size());
            int audioCount = currentAudioTrackCount;
            for (int i = 0; i < audioCount; ++i) {
                auto& chain = ctx.audioTracks[static_cast<size_t>(i)].effects;
                while (!chain.empty()) {
                    Vst3Plugin* plugin = chain.back();
                    if (!Vst3SystemLogic::RemovePluginFromTrack(ctx, plugin, i, currentAudioTrackCount)) {
                        break;
                    }
                }
            }
            int midiCount = static_cast<int>(ctx.midiTracks.size());
            for (int i = 0; i < midiCount; ++i) {
                int trackIndex = currentAudioTrackCount + i;
                auto& chain = ctx.midiTracks[static_cast<size_t>(i)].effects;
                while (!chain.empty()) {
                    Vst3Plugin* plugin = chain.back();
                    if (!Vst3SystemLogic::RemovePluginFromTrack(ctx, plugin, trackIndex, currentAudioTrackCount)) {
                        break;
                    }
                }
                if (i < static_cast<int>(ctx.midiInstruments.size())) {
                    Vst3Plugin* instrument = ctx.midiInstruments[static_cast<size_t>(i)];
                    if (instrument) {
                        Vst3SystemLogic::RemovePluginFromTrack(ctx, instrument, trackIndex, currentAudioTrackCount);
                    }
                }
            }
        }

        bool setTrackCountsForSession(BaseSystem& baseSystem, int audioTracks, int midiTracks, int automationTracks) {
            if (audioTracks < 0 || midiTracks < 0 || automationTracks < 0) return false;
            if (!baseSystem.daw || !baseSystem.midi) return false;
            DawContext& daw = *baseSystem.daw;
            MidiContext& midi = *baseSystem.midi;

            while (static_cast<int>(daw.tracks.size()) > audioTracks) {
                if (!DawTrackSystemLogic::RemoveTrackAt(baseSystem, static_cast<int>(daw.tracks.size()) - 1)) return false;
            }
            while (static_cast<int>(daw.tracks.size()) < audioTracks) {
                if (!DawTrackSystemLogic::InsertTrackAt(baseSystem, static_cast<int>(daw.tracks.size()))) return false;
            }

            while (static_cast<int>(midi.tracks.size()) > midiTracks) {
                if (!MidiTrackSystemLogic::RemoveTrackAt(baseSystem, static_cast<int>(midi.tracks.size()) - 1)) return false;
            }
            while (static_cast<int>(midi.tracks.size()) < midiTracks) {
                if (!MidiTrackSystemLogic::InsertTrackAt(baseSystem, static_cast<int>(midi.tracks.size()))) return false;
            }

            while (daw.automationTrackCount > automationTracks) {
                if (!AutomationTrackSystemLogic::RemoveTrackAt(baseSystem, daw.automationTrackCount - 1)) return false;
            }
            while (daw.automationTrackCount < automationTracks) {
                if (!AutomationTrackSystemLogic::InsertTrackAt(baseSystem, daw.automationTrackCount)) return false;
            }
            return true;
        }

        void rebuildDefaultLaneOrder(DawContext& daw, int audioTrackCount, int midiTrackCount, int automationTrackCount) {
            daw.laneOrder.clear();
            daw.laneOrder.reserve(static_cast<size_t>(audioTrackCount + midiTrackCount + automationTrackCount));
            for (int i = 0; i < audioTrackCount; ++i) {
                daw.laneOrder.push_back({0, i});
            }
            for (int i = 0; i < midiTrackCount; ++i) {
                daw.laneOrder.push_back({1, i});
            }
            for (int i = 0; i < automationTrackCount; ++i) {
                daw.laneOrder.push_back({2, i});
            }
        }

        void restoreLaneOrder(DawContext& daw, const json& laneOrder, int audioTrackCount, int midiTrackCount, int automationTrackCount) {
            if (!laneOrder.is_array()) {
                rebuildDefaultLaneOrder(daw, audioTrackCount, midiTrackCount, automationTrackCount);
                return;
            }
            std::vector<DawContext::LaneEntry> restored;
            restored.reserve(laneOrder.size());
            std::unordered_map<int, std::unordered_set<int>> seenByType;
            for (const auto& entry : laneOrder) {
                if (!entry.is_object()) continue;
                int type = readInt(entry, "type", -1);
                int trackIndex = readInt(entry, "track_index", -1);
                bool valid = false;
                if (type == 0) valid = trackIndex >= 0 && trackIndex < audioTrackCount;
                if (type == 1) valid = trackIndex >= 0 && trackIndex < midiTrackCount;
                if (type == 2) valid = trackIndex >= 0 && trackIndex < automationTrackCount;
                if (!valid) continue;
                auto& seen = seenByType[type];
                if (seen.find(trackIndex) != seen.end()) continue;
                seen.insert(trackIndex);
                restored.push_back({type, trackIndex});
            }
            if (restored.empty()) {
                rebuildDefaultLaneOrder(daw, audioTrackCount, midiTrackCount, automationTrackCount);
                return;
            }
            auto appendMissing = [&](int type, int count) {
                auto& seen = seenByType[type];
                for (int i = 0; i < count; ++i) {
                    if (seen.find(i) != seen.end()) continue;
                    restored.push_back({type, i});
                }
            };
            appendMissing(0, audioTrackCount);
            appendMissing(1, midiTrackCount);
            appendMissing(2, automationTrackCount);
            daw.laneOrder = std::move(restored);
        }

        bool processStemExportBlock(BaseSystem& baseSystem, uint32_t maxFrames) {
            if (!baseSystem.daw) return false;
            DawContext& daw = *baseSystem.daw;
            if (!daw.exportJobActive) return false;
            if (daw.exportJobCursorSample >= daw.exportJobEndSample) return false;

            uint64_t remaining = daw.exportJobEndSample - daw.exportJobCursorSample;
            uint32_t nframes = static_cast<uint32_t>(std::min<uint64_t>(remaining, maxFrames));
            if (nframes == 0) return false;

            std::array<std::vector<float>, DawContext::kBusCount> busBuffers;
            for (auto& bus : busBuffers) {
                bus.assign(static_cast<size_t>(nframes), 0.0f);
            }

            std::lock_guard<std::mutex> dawLock(daw.trackMutex);
            std::unique_ptr<std::lock_guard<std::mutex>> midiGuard;
            MidiContext* midiContext = baseSystem.midi.get();
            if (midiContext) {
                midiGuard = std::make_unique<std::lock_guard<std::mutex>>(midiContext->trackMutex);
            }

            const uint64_t playhead = daw.exportJobCursorSample;
            const uint64_t windowEnd = playhead + static_cast<uint64_t>(nframes);
            const Vst3Context* vst3Const = baseSystem.vst3.get();
            Vst3Context* vst3 = baseSystem.vst3.get();

            bool anySolo = false;
            for (const auto& track : daw.tracks) {
                if (track.solo.load(std::memory_order_relaxed)) {
                    anySolo = true;
                    break;
                }
            }
            if (!anySolo && midiContext) {
                for (const auto& track : midiContext->tracks) {
                    if (track.solo.load(std::memory_order_relaxed)) {
                        anySolo = true;
                        break;
                    }
                }
            }

            thread_local std::vector<float> clipBufferL;
            thread_local std::vector<float> clipBufferR;
            thread_local std::vector<float> clipBufferMono;
            for (int t = 0; t < static_cast<int>(daw.tracks.size()); ++t) {
                DawTrack& track = daw.tracks[static_cast<size_t>(t)];
                bool solo = track.solo.load(std::memory_order_relaxed);
                bool mute = track.mute.load(std::memory_order_relaxed);
                if ((anySolo && !solo) || (!anySolo && mute)) {
                    continue;
                }

                int busLIndex = track.outputBusL.load(std::memory_order_relaxed);
                int busRIndex = track.outputBusR.load(std::memory_order_relaxed);
                std::vector<float>* busL = (busLIndex >= 0 && busLIndex < DawContext::kBusCount)
                    ? &busBuffers[static_cast<size_t>(busLIndex)] : nullptr;
                std::vector<float>* busR = (busRIndex >= 0 && busRIndex < DawContext::kBusCount)
                    ? &busBuffers[static_cast<size_t>(busRIndex)] : nullptr;
                if (!busL && !busR) continue;

                if (clipBufferL.size() < static_cast<size_t>(nframes)) {
                    clipBufferL.assign(static_cast<size_t>(nframes), 0.0f);
                } else {
                    std::fill(clipBufferL.begin(), clipBufferL.begin() + static_cast<size_t>(nframes), 0.0f);
                }
                if (clipBufferR.size() < static_cast<size_t>(nframes)) {
                    clipBufferR.assign(static_cast<size_t>(nframes), 0.0f);
                } else {
                    std::fill(clipBufferR.begin(), clipBufferR.begin() + static_cast<size_t>(nframes), 0.0f);
                }

                for (const auto& clip : track.clips) {
                    if (clip.length == 0) continue;
                    if (clip.audioId < 0 || clip.audioId >= static_cast<int>(daw.clipAudio.size())) continue;
                    uint64_t clipStart = clip.startSample;
                    uint64_t clipEnd = clip.startSample + clip.length;
                    if (clipEnd <= playhead || clipStart >= windowEnd) continue;
                    uint64_t overlapStart = std::max<uint64_t>(clipStart, playhead);
                    uint64_t overlapEnd = std::min<uint64_t>(clipEnd, windowEnd);
                    if (overlapEnd <= overlapStart) continue;
                    const auto& data = daw.clipAudio[clip.audioId];
                    uint64_t srcOffset = clip.sourceOffset + (overlapStart - clipStart);
                    uint64_t copyCount = overlapEnd - overlapStart;
                    if (srcOffset >= data.left.size()) continue;
                    copyCount = std::min<uint64_t>(copyCount, static_cast<uint64_t>(data.left.size()) - srcOffset);
                    size_t dstBase = static_cast<size_t>(overlapStart - playhead);
                    for (uint64_t i = 0; i < copyCount; ++i) {
                        size_t dst = dstBase + static_cast<size_t>(i);
                        if (dst >= clipBufferL.size()) break;
                        const size_t src = static_cast<size_t>(srcOffset + i);
                        const float left = data.left[src];
                        const float right = (data.channels > 1 && src < data.right.size()) ? data.right[src] : left;
                        clipBufferL[dst] = left;
                        clipBufferR[dst] = right;
                    }
                }

                float gain = track.gain.load(std::memory_order_relaxed);
                const float* sourceL = clipBufferL.data();
                const float* sourceR = clipBufferR.data();
                if (vst3 && static_cast<size_t>(t) < vst3->audioTracks.size()) {
                    Vst3TrackChain& chain = vst3->audioTracks[static_cast<size_t>(t)];
                    bool processedStereo = Vst3SystemLogic::ProcessEffectChainStereo(*vst3,
                                                                                     chain,
                                                                                     clipBufferL.data(),
                                                                                     clipBufferR.data(),
                                                                                     clipBufferL.data(),
                                                                                     clipBufferR.data(),
                                                                                     static_cast<int>(nframes),
                                                                                     static_cast<int64_t>(playhead),
                                                                                     true);
                    if (processedStereo) {
                        sourceL = clipBufferL.data();
                        sourceR = clipBufferR.data();
                    } else if (chain.monoInput.size() >= static_cast<size_t>(nframes)
                               && chain.monoOutput.size() >= static_cast<size_t>(nframes)) {
                        if (clipBufferMono.size() < static_cast<size_t>(nframes)) {
                            clipBufferMono.resize(static_cast<size_t>(nframes), 0.0f);
                        }
                        for (uint32_t i = 0; i < nframes; ++i) {
                            clipBufferMono[static_cast<size_t>(i)] =
                                0.5f * (clipBufferL[static_cast<size_t>(i)] + clipBufferR[static_cast<size_t>(i)]);
                        }
                        std::copy(clipBufferMono.begin(),
                                  clipBufferMono.begin() + static_cast<size_t>(nframes),
                                  chain.monoInput.begin());
                        bool processed = Vst3SystemLogic::ProcessEffectChain(*vst3,
                                                                             chain,
                                                                             chain.monoInput.data(),
                                                                             chain.monoOutput.data(),
                                                                             static_cast<int>(nframes),
                                                                             static_cast<int64_t>(playhead),
                                                                             true);
                        const float* mono = processed ? chain.monoOutput.data() : chain.monoInput.data();
                        sourceL = mono;
                        sourceR = mono;
                    }
                }
                for (uint32_t i = 0; i < nframes; ++i) {
                    const float outL = sourceL[static_cast<size_t>(i)] * gain;
                    const float outR = sourceR[static_cast<size_t>(i)] * gain;
                    if (busLIndex == busRIndex) {
                        if (busL) {
                            (*busL)[static_cast<size_t>(i)] += (outL + outR);
                        }
                    } else {
                        if (busL) {
                            (*busL)[static_cast<size_t>(i)] += outL;
                        }
                        if (busR) {
                            (*busR)[static_cast<size_t>(i)] += outR;
                        }
                    }
                }
            }

            if (midiContext) {
                if (vst3 && vst3Const) {
                    Vst3SystemLogic::EnsureMidiTrackCount(*vst3, static_cast<int>(midiContext->tracks.size()));
                    if (daw.exportMidiHeldVelocities.size() < midiContext->tracks.size()) {
                        daw.exportMidiHeldVelocities.resize(midiContext->tracks.size(), {});
                    }
                }

                auto collectActiveClipNotesAtSample = [](const MidiTrack& track,
                                                         uint64_t sample,
                                                         std::array<float, 128>& outHeldVelocities) {
                    for (const auto& clip : track.clips) {
                        if (clip.length == 0) continue;
                        uint64_t clipStart = clip.startSample;
                        uint64_t clipEnd = clip.startSample + clip.length;
                        if (sample < clipStart || sample >= clipEnd) continue;
                        uint64_t localSample = sample - clipStart;
                        for (const auto& note : clip.notes) {
                            if (note.length == 0) continue;
                            if (note.pitch < 0 || note.pitch > 127) continue;
                            uint64_t noteStart = note.startSample;
                            uint64_t noteEnd = note.startSample + note.length;
                            if (localSample < noteStart || localSample >= noteEnd) continue;
                            float vel = std::clamp(note.velocity, 0.0f, 1.0f);
                            size_t idx = static_cast<size_t>(note.pitch);
                            if (vel > outHeldVelocities[idx]) outHeldVelocities[idx] = vel;
                        }
                    }
                };

                thread_local std::vector<float> midiPlaybackScratch;
                for (int t = 0; t < static_cast<int>(midiContext->tracks.size()); ++t) {
                    MidiTrack& track = midiContext->tracks[static_cast<size_t>(t)];
                    bool solo = track.solo.load(std::memory_order_relaxed);
                    bool mute = track.mute.load(std::memory_order_relaxed);
                    bool midiAllowed = (!anySolo && !mute) || (anySolo && solo);
                    if (!midiAllowed) continue;

                    int busLIndex = track.outputBusL.load(std::memory_order_relaxed);
                    int busRIndex = track.outputBusR.load(std::memory_order_relaxed);
                    std::vector<float>* busL = (busLIndex >= 0 && busLIndex < DawContext::kBusCount)
                        ? &busBuffers[static_cast<size_t>(busLIndex)] : nullptr;
                    std::vector<float>* busR = (busRIndex >= 0 && busRIndex < DawContext::kBusCount)
                        ? &busBuffers[static_cast<size_t>(busRIndex)] : nullptr;
                    if (!busL && !busR) continue;

                    Vst3TrackChain* midiChain = nullptr;
                    Vst3Plugin* midiInstrument = nullptr;
                    std::array<float, 128>* heldVelocities = nullptr;
                    if (vst3 && static_cast<size_t>(t) < vst3->midiTracks.size()) {
                        midiChain = &vst3->midiTracks[static_cast<size_t>(t)];
                    }
                    if (vst3 && static_cast<size_t>(t) < vst3->midiInstruments.size()) {
                        midiInstrument = vst3->midiInstruments[static_cast<size_t>(t)];
                    }
                    if (static_cast<size_t>(t) < daw.exportMidiHeldVelocities.size()) {
                        heldVelocities = &daw.exportMidiHeldVelocities[static_cast<size_t>(t)];
                    }

                    const std::vector<float>& renderedData = track.audio;
                    const float* playbackSource = nullptr;
                    const float* liveSource = nullptr;

                    if (midiChain
                        && midiChain->monoInput.size() >= static_cast<size_t>(nframes)
                        && midiChain->monoOutput.size() >= static_cast<size_t>(nframes)) {
                        for (uint32_t i = 0; i < nframes; ++i) {
                            uint64_t idx = playhead + static_cast<uint64_t>(i);
                            midiChain->monoInput[static_cast<size_t>(i)] =
                                (idx < renderedData.size()) ? renderedData[static_cast<size_t>(idx)] : 0.0f;
                        }
                        bool processed = Vst3SystemLogic::ProcessEffectChain(*vst3,
                                                                             *midiChain,
                                                                             midiChain->monoInput.data(),
                                                                             midiChain->monoOutput.data(),
                                                                             static_cast<int>(nframes),
                                                                             static_cast<int64_t>(playhead),
                                                                             true);
                        playbackSource = processed ? midiChain->monoOutput.data() : midiChain->monoInput.data();
                        if (playbackSource) {
                            if (midiPlaybackScratch.size() < static_cast<size_t>(nframes)) {
                                midiPlaybackScratch.resize(static_cast<size_t>(nframes), 0.0f);
                            }
                            std::copy(playbackSource,
                                      playbackSource + static_cast<size_t>(nframes),
                                      midiPlaybackScratch.begin());
                            playbackSource = midiPlaybackScratch.data();
                        }
                    }

                    std::array<float, 128> desiredHeldVelocities{};
                    collectActiveClipNotesAtSample(track, playhead, desiredHeldVelocities);
                    bool processLiveInstrument = midiChain
                        && midiInstrument
                        && heldVelocities
                        && midiChain->monoInput.size() >= static_cast<size_t>(nframes)
                        && midiChain->monoOutput.size() >= static_cast<size_t>(nframes);
                    if (processLiveInstrument) {
                        bool anyDesired = false;
                        for (float value : desiredHeldVelocities) {
                            if (value > 0.0001f) {
                                anyDesired = true;
                                break;
                            }
                        }
                        bool anyLast = false;
                        for (float value : *heldVelocities) {
                            if (value > 0.0001f) {
                                anyLast = true;
                                break;
                            }
                        }
                        if (anyDesired || anyLast) {
                            bool generated = Vst3SystemLogic::ProcessInstrument(*vst3,
                                                                                *midiChain,
                                                                                *midiInstrument,
                                                                                midiChain->monoInput.data(),
                                                                                static_cast<int>(nframes),
                                                                                static_cast<int64_t>(playhead),
                                                                                true,
                                                                                desiredHeldVelocities,
                                                                                *heldVelocities);
                            if (generated) {
                                bool fxProcessed = Vst3SystemLogic::ProcessEffectChain(*vst3,
                                                                                       *midiChain,
                                                                                       midiChain->monoInput.data(),
                                                                                       midiChain->monoOutput.data(),
                                                                                       static_cast<int>(nframes),
                                                                                       static_cast<int64_t>(playhead),
                                                                                       true);
                                liveSource = fxProcessed ? midiChain->monoOutput.data() : midiChain->monoInput.data();
                            }
                        }
                    }

                    float gain = track.gain.load(std::memory_order_relaxed);
                    for (uint32_t i = 0; i < nframes; ++i) {
                        float sample = 0.0f;
                        if (playbackSource) {
                            sample += playbackSource[static_cast<size_t>(i)];
                        } else {
                            uint64_t idx = playhead + static_cast<uint64_t>(i);
                            if (idx < renderedData.size()) {
                                sample += renderedData[static_cast<size_t>(idx)];
                            }
                        }
                        if (liveSource) {
                            sample += liveSource[static_cast<size_t>(i)];
                        }
                        const float outL = sample * gain;
                        const float outR = outL;
                        if (busLIndex == busRIndex) {
                            if (busL) {
                                (*busL)[static_cast<size_t>(i)] += (outL + outR);
                            }
                        } else {
                            if (busL) {
                                (*busL)[static_cast<size_t>(i)] += outL;
                            }
                            if (busR) {
                                (*busR)[static_cast<size_t>(i)] += outR;
                            }
                        }
                    }
                }
            }

            for (int busIndex = 0; busIndex < DawContext::kBusCount; ++busIndex) {
                auto& dst = daw.exportStemBuffers[static_cast<size_t>(busIndex)];
                const auto& src = busBuffers[static_cast<size_t>(busIndex)];
                size_t oldSize = dst.size();
                dst.resize(oldSize + src.size());
                std::copy(src.begin(), src.end(), dst.begin() + oldSize);
            }

            daw.exportJobCursorSample += static_cast<uint64_t>(nframes);
            const uint64_t total = daw.exportJobEndSample - daw.exportJobStartSample;
            const uint64_t done = daw.exportJobCursorSample - daw.exportJobStartSample;
            const float progress = (total > 0)
                ? static_cast<float>(static_cast<double>(done) / static_cast<double>(total))
                : 1.0f;
            daw.exportProgress.store(std::clamp(progress, 0.0f, 1.0f), std::memory_order_relaxed);

            if (vst3) {
                vst3->continuousSamples += static_cast<int64_t>(nframes);
            }
            return true;
        }

        void flushExportMidiNotes(BaseSystem& baseSystem) {
            if (!baseSystem.daw || !baseSystem.midi || !baseSystem.vst3) return;
            DawContext& daw = *baseSystem.daw;
            MidiContext& midi = *baseSystem.midi;
            Vst3Context& vst3 = *baseSystem.vst3;
            if (daw.exportMidiHeldVelocities.empty()) return;

            const int numFrames = std::max(64, vst3.blockSize > 0 ? vst3.blockSize : 512);
            std::array<float, 128> desired{};
            for (size_t i = 0; i < midi.tracks.size() && i < daw.exportMidiHeldVelocities.size(); ++i) {
                if (i >= vst3.midiTracks.size() || i >= vst3.midiInstruments.size()) continue;
                Vst3Plugin* instrument = vst3.midiInstruments[i];
                Vst3TrackChain& chain = vst3.midiTracks[i];
                if (!instrument) continue;
                if (chain.monoInput.size() < static_cast<size_t>(numFrames)) continue;
                bool anyHeld = false;
                for (float value : daw.exportMidiHeldVelocities[i]) {
                    if (value > 0.0001f) {
                        anyHeld = true;
                        break;
                    }
                }
                if (!anyHeld) continue;
                Vst3SystemLogic::ProcessInstrument(vst3,
                                                   chain,
                                                   *instrument,
                                                   chain.monoInput.data(),
                                                   numFrames,
                                                   static_cast<int64_t>(daw.exportJobCursorSample),
                                                   false,
                                                   desired,
                                                   daw.exportMidiHeldVelocities[i]);
            }
        }

        void finishStemExport(BaseSystem& baseSystem, bool success, const std::string& message) {
            if (!baseSystem.daw) return;
            DawContext& daw = *baseSystem.daw;
            if (baseSystem.audio) {
                baseSystem.audio->offlineRenderMute.store(false, std::memory_order_relaxed);
            }
            if (baseSystem.vst3) {
                baseSystem.vst3->continuousSamples = daw.exportSavedContinuousSamples;
            }
            daw.transportPlaying.store(false, std::memory_order_relaxed);
            daw.transportRecording.store(false, std::memory_order_relaxed);
            daw.transportLatch = 0;
            daw.playheadSample.store(daw.exportSavedPlayheadSample, std::memory_order_relaxed);
            daw.exportJobActive = false;
            daw.exportInProgress.store(false, std::memory_order_relaxed);
            daw.exportSucceeded = success;
            daw.exportStatusMessage = message;
            if (success) {
                daw.exportProgress.store(1.0f, std::memory_order_relaxed);
            } else {
                daw.exportProgress.store(0.0f, std::memory_order_relaxed);
            }
            for (auto& stem : daw.exportStemBuffers) {
                stem.clear();
                stem.shrink_to_fit();
            }
            daw.exportMidiHeldVelocities.clear();
        }
    }

    bool ResolveMirrorPath(DawContext& daw) {
        std::filesystem::path mirror = std::filesystem::path(getExecutableDir()) / "Mirror";
        daw.mirrorPath = mirror.string();
        daw.mirrorAvailable = std::filesystem::exists(mirror) && std::filesystem::is_directory(mirror);
        return daw.mirrorAvailable;
    }

    void WriteTracksIfNeeded(DawContext& daw) {
        if (!daw.mirrorAvailable) return;
        for (int i = 0; i < static_cast<int>(daw.tracks.size()); ++i) {
            const auto& data = daw.tracks[static_cast<size_t>(i)].audio;
            std::filesystem::path outPath = std::filesystem::path(daw.mirrorPath) / ("track_" + std::to_string(i + 1) + ".wav");
            writeWavMonoFloat(outPath.string(), data, static_cast<uint32_t>(daw.sampleRate));
        }
    }

    void WriteTrackAt(DawContext& daw, int trackIndex) {
        if (!daw.mirrorAvailable) return;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(daw.tracks.size())) return;
        const auto& data = daw.tracks[static_cast<size_t>(trackIndex)].audio;
        std::filesystem::path outPath = std::filesystem::path(daw.mirrorPath) / ("track_" + std::to_string(trackIndex + 1) + ".wav");
        writeWavMonoFloat(outPath.string(), data, static_cast<uint32_t>(daw.sampleRate));
    }

    void LoadTracksIfAvailable(DawContext& daw) {
        if (!daw.mirrorAvailable) return;
        for (int i = 0; i < static_cast<int>(daw.tracks.size()); ++i) {
            std::filesystem::path inPath = std::filesystem::path(daw.mirrorPath) / ("track_" + std::to_string(i + 1) + ".wav");
            uint32_t rate = 0;
            DawClipAudio clipData;
            if (loadWavClipAudio(inPath.string(), clipData, rate)) {
                DawTrack& track = daw.tracks[static_cast<size_t>(i)];
                track.clips.clear();
                track.loopTakeClips.clear();
                track.activeLoopTakeIndex = -1;
                track.takeStackExpanded = false;
                track.nextTakeId = 1;
                track.loopTakeRangeStartSample = 0;
                track.loopTakeRangeLength = 0;
                int audioId = static_cast<int>(daw.clipAudio.size());
                daw.clipAudio.emplace_back(std::move(clipData));
                DawClip clip{};
                clip.audioId = audioId;
                clip.startSample = 0;
                clip.sourceOffset = 0;
                clip.length = (audioId >= 0 && audioId < static_cast<int>(daw.clipAudio.size()))
                    ? static_cast<uint64_t>(daw.clipAudio[audioId].left.size())
                    : 0;
                clip.takeId = -1;
                if (clip.length > 0) {
                    track.clips.push_back(clip);
                }
                DawClipSystemLogic::RebuildTrackCacheFromClips(daw, track);
            } else {
                daw.tracks[static_cast<size_t>(i)].clips.clear();
                daw.tracks[static_cast<size_t>(i)].loopTakeClips.clear();
                daw.tracks[static_cast<size_t>(i)].activeLoopTakeIndex = -1;
                daw.tracks[static_cast<size_t>(i)].takeStackExpanded = false;
                daw.tracks[static_cast<size_t>(i)].nextTakeId = 1;
                daw.tracks[static_cast<size_t>(i)].loopTakeRangeStartSample = 0;
                daw.tracks[static_cast<size_t>(i)].loopTakeRangeLength = 0;
                daw.tracks[static_cast<size_t>(i)].audio.clear();
                daw.tracks[static_cast<size_t>(i)].audioRight.clear();
                daw.tracks[static_cast<size_t>(i)].waveformMin.clear();
                daw.tracks[static_cast<size_t>(i)].waveformMax.clear();
                daw.tracks[static_cast<size_t>(i)].waveformMinRight.clear();
                daw.tracks[static_cast<size_t>(i)].waveformMaxRight.clear();
                daw.tracks[static_cast<size_t>(i)].waveformColor.clear();
                daw.tracks[static_cast<size_t>(i)].waveformVersion += 1;
            }
        }
    }

    void LoadMetronomeSample(DawContext& daw) {
        daw.metronomeSamples.clear();
        daw.metronomeLoaded = false;
        daw.metronomeSampleRate = 0;
        if (daw.sampleRate <= 0.0f) return;
        constexpr double kClickHz = 1200.0;
        constexpr double kClickSeconds = 0.03;
        constexpr double kFadeSeconds = 0.01;
        int sampleRate = static_cast<int>(std::round(daw.sampleRate));
        int totalSamples = std::max(1, static_cast<int>(std::round(kClickSeconds * sampleRate)));
        int fadeSamples = std::max(1, static_cast<int>(std::round(kFadeSeconds * sampleRate)));
        daw.metronomeSamples.resize(static_cast<size_t>(totalSamples), 0.0f);
        double phase = 0.0;
        double phaseInc = 2.0 * 3.14159265358979 * kClickHz / static_cast<double>(sampleRate);
        for (int i = 0; i < totalSamples; ++i) {
            double env = 1.0;
            if (i >= totalSamples - fadeSamples) {
                double t = static_cast<double>(totalSamples - i) / static_cast<double>(fadeSamples);
                env = std::clamp(t, 0.0, 1.0);
            }
            float sample = static_cast<float>(std::sin(phase) * env * 0.6);
            daw.metronomeSamples[static_cast<size_t>(i)] = sample;
            phase += phaseInc;
            if (phase > 2.0 * 3.14159265358979) {
                phase -= 2.0 * 3.14159265358979;
            }
        }
        daw.metronomeSampleRate = static_cast<uint32_t>(sampleRate);
        daw.metronomeLoaded = !daw.metronomeSamples.empty();
        daw.metronomeSampleStep = 1.0;
    }

    double BarSamples(const DawContext& daw) {
        const double sampleRate = (daw.sampleRate > 0.0f) ? static_cast<double>(daw.sampleRate) : 44100.0;
        double bpm = daw.bpm.load(std::memory_order_relaxed);
        if (bpm <= 0.0) bpm = 120.0;
        return std::max(1.0, (60.0 / bpm) * 4.0 * sampleRate);
    }

    int64_t BarDisplayToSample(const DawContext& daw, int displayBar) {
        const double barSamples = BarSamples(daw);
        const int64_t zeroBased = displayBarToZeroBased(displayBar);
        const int64_t sample = static_cast<int64_t>(std::llround(static_cast<double>(daw.timelineZeroSample)
            + static_cast<double>(zeroBased) * barSamples));
        return sample;
    }

    int64_t SampleToBarDisplay(const DawContext& daw, int64_t sample) {
        const double barSamples = BarSamples(daw);
        const double barPos = (static_cast<double>(sample) - static_cast<double>(daw.timelineZeroSample)) / barSamples;
        const int64_t zeroBased = static_cast<int64_t>(std::floor(barPos));
        return zeroBasedToDisplayBar(zeroBased);
    }

    bool OpenExportFolderDialog(std::string& ioPath) {
#if defined(__APPLE__)
        const char* cmd =
            "osascript -e 'try' "
            "-e 'POSIX path of (choose folder with prompt \"Choose export folder\")' "
            "-e 'on error number -128' "
            "-e 'return \"\"' "
            "-e 'end try'";
        std::string selected = commandOutputFirstLine(cmd);
        if (selected.empty()) return false;
        ioPath = selected;
        return true;
#elif defined(__linux__)
        const char* cmd = "zenity --file-selection --directory --title='Choose export folder' 2>/dev/null";
        std::string selected = commandOutputFirstLine(cmd);
        if (selected.empty()) return false;
        ioPath = selected;
        return true;
#else
        (void)ioPath;
        return false;
#endif
    }

    bool OpenSaveSessionDialog(std::string& ioPath) {
#if defined(__APPLE__)
        const char* cmd =
            "osascript -e 'try' "
            "-e 'POSIX path of (choose file name with prompt \"Save Salamander Session\" default name \"Untitled.salmproj\")' "
            "-e 'on error number -128' "
            "-e 'return \"\"' "
            "-e 'end try'";
        std::string selected = commandOutputFirstLine(cmd);
        if (selected.empty()) return false;
        ioPath = ensureSessionExtension(selected);
        return true;
#elif defined(__linux__)
        const char* cmd =
            "zenity --file-selection --save --confirm-overwrite "
            "--filename=\"Untitled.salmproj\" --title='Save Salamander Session' 2>/dev/null";
        std::string selected = commandOutputFirstLine(cmd);
        if (selected.empty()) return false;
        ioPath = ensureSessionExtension(selected);
        return true;
#else
        (void)ioPath;
        return false;
#endif
    }

    bool OpenLoadSessionDialog(std::string& ioPath) {
#if defined(__APPLE__)
        const char* cmd =
            "osascript -e 'try' "
            "-e 'POSIX path of (choose file with prompt \"Load Salamander Session\")' "
            "-e 'on error number -128' "
            "-e 'return \"\"' "
            "-e 'end try'";
        std::string selected = commandOutputFirstLine(cmd);
        if (selected.empty()) return false;
        ioPath = selected;
        return true;
#elif defined(__linux__)
        const char* cmd =
            "zenity --file-selection --title='Load Salamander Session' "
            "--file-filter='Salamander Session | *.salmproj' 2>/dev/null";
        std::string selected = commandOutputFirstLine(cmd);
        if (selected.empty()) return false;
        ioPath = selected;
        return true;
#else
        (void)ioPath;
        return false;
#endif
    }

    bool SaveSession(BaseSystem& baseSystem, const std::string& rawPath) {
        if (!baseSystem.daw || !baseSystem.midi) return false;
        DawContext& daw = *baseSystem.daw;
        MidiContext& midi = *baseSystem.midi;

        std::filesystem::path sessionPath = ensureSessionExtension(rawPath);
        if (!sessionPath.is_absolute()) {
            sessionPath = std::filesystem::absolute(sessionPath);
        }
        if (sessionPath.empty()) return false;
        std::filesystem::path sessionDir = sessionPath.parent_path();
        if (sessionDir.empty()) {
            sessionDir = std::filesystem::current_path();
            sessionPath = sessionDir / sessionPath.filename();
        }

        std::error_code ec;
        std::filesystem::create_directories(sessionDir, ec);
        if (ec) {
            std::cerr << "Session save failed: could not create directory " << sessionDir << std::endl;
            return false;
        }

        std::filesystem::path assetDir = sessionDir / defaultAssetDirName(sessionPath);
        std::filesystem::path audioDir = assetDir / "audio";
        std::filesystem::remove_all(audioDir, ec);
        ec.clear();
        std::filesystem::create_directories(audioDir, ec);
        if (ec) {
            std::cerr << "Session save failed: could not create asset audio directory " << audioDir << std::endl;
            return false;
        }

        json root = json::object();
        root["format"] = kSessionFormat;
        root["version"] = kSessionVersion;
        root["asset_dir"] = assetDir.filename().string();
        root["sample_rate"] = daw.sampleRate;
        root["timeline"] = {
            {"seconds_per_screen", daw.timelineSecondsPerScreen},
            {"offset_samples", daw.timelineOffsetSamples},
            {"zero_sample", daw.timelineZeroSample},
            {"lane_height", daw.timelineLaneHeight},
            {"lane_offset", daw.timelineLaneOffset},
            {"playhead_sample", daw.playheadSample.load(std::memory_order_relaxed)}
        };
        root["transport"] = {
            {"bpm", daw.bpm.load(std::memory_order_relaxed)},
            {"metronome_enabled", daw.metronomeEnabled.load(std::memory_order_relaxed)},
            {"loop_enabled", daw.loopEnabled.load(std::memory_order_relaxed)},
            {"loop_start_samples", daw.loopStartSamples},
            {"loop_end_samples", daw.loopEndSamples}
        };
        root["export"] = {
            {"start_bar", daw.exportStartBar},
            {"end_bar", daw.exportEndBar},
            {"folder_path", daw.exportFolderPath},
            {"stem_names", json::array({
                daw.exportStemNames[0],
                daw.exportStemNames[1],
                daw.exportStemNames[2],
                daw.exportStemNames[3]
            })}
        };

        json audioPool = json::array();
        json audioTracks = json::array();
        json midiTracks = json::array();
        json automationTracks = json::array();

        std::lock_guard<std::mutex> dawLock(daw.trackMutex);
        std::lock_guard<std::mutex> midiLock(midi.trackMutex);

        const uint32_t sampleRate = static_cast<uint32_t>(std::max(1.0f, daw.sampleRate));
        for (size_t i = 0; i < daw.clipAudio.size(); ++i) {
            std::string fileName = "clip_" + std::to_string(i + 1) + ".wav";
            std::filesystem::path outPath = audioDir / fileName;
            bool ok = writeWavClipAudio(outPath.string(), daw.clipAudio[i], sampleRate);
            json entry = json::object();
            entry["id"] = static_cast<int>(i);
            entry["file"] = std::string("audio/") + fileName;
            entry["valid"] = ok;
            const bool stereo = (daw.clipAudio[i].channels > 1) && !daw.clipAudio[i].right.empty();
            entry["channels"] = stereo ? 2 : 1;
            entry["frames"] = stereo
                ? static_cast<uint64_t>(std::min(daw.clipAudio[i].left.size(), daw.clipAudio[i].right.size()))
                : static_cast<uint64_t>(daw.clipAudio[i].left.size());
            audioPool.push_back(std::move(entry));
        }

        for (size_t i = 0; i < daw.tracks.size(); ++i) {
            const DawTrack& track = daw.tracks[i];
            json trackJson = json::object();
            trackJson["arm_mode"] = track.armMode.load(std::memory_order_relaxed);
            trackJson["mute"] = track.mute.load(std::memory_order_relaxed);
            trackJson["solo"] = track.solo.load(std::memory_order_relaxed);
            trackJson["output_bus"] = track.outputBus.load(std::memory_order_relaxed);
            trackJson["output_bus_l"] = track.outputBusL.load(std::memory_order_relaxed);
            trackJson["output_bus_r"] = track.outputBusR.load(std::memory_order_relaxed);
            trackJson["gain"] = track.gain.load(std::memory_order_relaxed);
            trackJson["physical_input_index"] = track.physicalInputIndex;
            trackJson["stereo_input_pair12"] = track.stereoInputPair12;
            trackJson["next_take_id"] = track.nextTakeId;
            trackJson["active_loop_take_index"] = track.activeLoopTakeIndex;
            trackJson["take_stack_expanded"] = track.takeStackExpanded;
            trackJson["loop_take_range_start_sample"] = track.loopTakeRangeStartSample;
            trackJson["loop_take_range_length"] = track.loopTakeRangeLength;
            trackJson["clips"] = json::array();
            trackJson["loop_take_clips"] = json::array();
            for (const auto& clip : track.clips) {
                trackJson["clips"].push_back(serializeDawClip(clip));
            }
            for (const auto& clip : track.loopTakeClips) {
                trackJson["loop_take_clips"].push_back(serializeDawClip(clip));
            }
            audioTracks.push_back(std::move(trackJson));
        }

        for (size_t i = 0; i < midi.tracks.size(); ++i) {
            const MidiTrack& track = midi.tracks[i];
            json trackJson = json::object();
            trackJson["arm_mode"] = track.armMode.load(std::memory_order_relaxed);
            trackJson["mute"] = track.mute.load(std::memory_order_relaxed);
            trackJson["solo"] = track.solo.load(std::memory_order_relaxed);
            trackJson["output_bus"] = track.outputBus.load(std::memory_order_relaxed);
            trackJson["output_bus_l"] = track.outputBusL.load(std::memory_order_relaxed);
            trackJson["output_bus_r"] = track.outputBusR.load(std::memory_order_relaxed);
            trackJson["gain"] = track.gain.load(std::memory_order_relaxed);
            trackJson["next_take_id"] = track.nextTakeId;
            trackJson["active_loop_take_index"] = track.activeLoopTakeIndex;
            trackJson["take_stack_expanded"] = track.takeStackExpanded;
            trackJson["loop_take_range_start_sample"] = track.loopTakeRangeStartSample;
            trackJson["loop_take_range_length"] = track.loopTakeRangeLength;
            trackJson["clips"] = json::array();
            trackJson["loop_take_clips"] = json::array();
            for (const auto& clip : track.clips) {
                trackJson["clips"].push_back(serializeMidiClip(clip));
            }
            for (const auto& clip : track.loopTakeClips) {
                trackJson["loop_take_clips"].push_back(serializeMidiClip(clip));
            }
            midiTracks.push_back(std::move(trackJson));
        }

        for (const auto& track : daw.automationTracks) {
            json trackJson = json::object();
            trackJson["target_lane_type"] = track.targetLaneType;
            trackJson["target_lane_track"] = track.targetLaneTrack;
            trackJson["target_device_slot"] = track.targetDeviceSlot;
            trackJson["target_parameter_slot"] = track.targetParameterSlot;
            trackJson["target_parameter_id"] = track.targetParameterId;
            trackJson["target_lane_label"] = track.targetLaneLabel;
            trackJson["target_device_label"] = track.targetDeviceLabel;
            trackJson["target_parameter_label"] = track.targetParameterLabel;
            trackJson["clips"] = json::array();
            for (const auto& clip : track.clips) {
                trackJson["clips"].push_back(serializeAutomationClip(clip));
            }
            automationTracks.push_back(std::move(trackJson));
        }

        if (baseSystem.vst3) {
            Vst3Context& vst3 = *baseSystem.vst3;
            for (size_t i = 0; i < audioTracks.size(); ++i) {
                json fx = json::array();
                if (i < vst3.audioTracks.size()) {
                    for (const auto* plugin : vst3.audioTracks[i].effects) {
                        fx.push_back(serializePlugin(plugin));
                    }
                }
                audioTracks[i]["fx_chain"] = std::move(fx);
            }
            for (size_t i = 0; i < midiTracks.size(); ++i) {
                json fx = json::array();
                if (i < vst3.midiTracks.size()) {
                    for (const auto* plugin : vst3.midiTracks[i].effects) {
                        fx.push_back(serializePlugin(plugin));
                    }
                }
                if (i < vst3.midiInstruments.size() && vst3.midiInstruments[i]) {
                    midiTracks[i]["instrument"] = serializePlugin(vst3.midiInstruments[i]);
                } else {
                    midiTracks[i]["instrument"] = nullptr;
                }
                midiTracks[i]["fx_chain"] = std::move(fx);
            }
        }

        root["audio_pool"] = std::move(audioPool);
        root["audio_tracks"] = std::move(audioTracks);
        root["midi_tracks"] = std::move(midiTracks);
        root["automation_tracks"] = std::move(automationTracks);
        root["lane_order"] = json::array();
        for (const auto& lane : daw.laneOrder) {
            root["lane_order"].push_back({
                {"type", lane.type},
                {"track_index", lane.trackIndex}
            });
        }

        std::filesystem::path tmpPath = sessionPath;
        tmpPath += ".tmp";
        {
            std::ofstream out(tmpPath);
            if (!out.is_open()) {
                std::cerr << "Session save failed: could not open " << tmpPath << std::endl;
                return false;
            }
            out << root.dump(2);
        }
        std::filesystem::rename(tmpPath, sessionPath, ec);
        if (ec) {
            std::filesystem::remove(sessionPath, ec);
            ec.clear();
            std::filesystem::rename(tmpPath, sessionPath, ec);
            if (ec) {
                std::filesystem::remove(tmpPath, ec);
                std::cerr << "Session save failed: could not replace " << sessionPath << std::endl;
                return false;
            }
        }

        std::cerr << "Session saved: " << sessionPath << std::endl;
        return true;
    }

    bool LoadSession(BaseSystem& baseSystem, const std::string& rawPath) {
        if (!baseSystem.daw || !baseSystem.midi) return false;
        DawContext& daw = *baseSystem.daw;
        MidiContext& midi = *baseSystem.midi;

        std::filesystem::path sessionPath(rawPath);
        if (!sessionPath.is_absolute()) {
            sessionPath = std::filesystem::absolute(sessionPath);
        }
        if (!std::filesystem::exists(sessionPath)) {
            std::cerr << "Session load failed: file not found " << sessionPath << std::endl;
            return false;
        }

        json root;
        try {
            std::ifstream in(sessionPath);
            if (!in.is_open()) return false;
            root = json::parse(in);
        } catch (...) {
            std::cerr << "Session load failed: invalid JSON " << sessionPath << std::endl;
            return false;
        }

        if (readString(root, "format", "") != kSessionFormat) {
            std::cerr << "Session load failed: unsupported format in " << sessionPath << std::endl;
            return false;
        }
        int version = readInt(root, "version", -1);
        if (version != kSessionVersion) {
            std::cerr << "Session load failed: unsupported version " << version << std::endl;
            return false;
        }

        std::filesystem::path sessionDir = sessionPath.parent_path();
        std::filesystem::path assetDir = sessionDir / readString(root, "asset_dir", defaultAssetDirName(sessionPath));

        if (daw.transportRecording.load(std::memory_order_relaxed)) {
            std::cerr << "Session load failed: stop recording first." << std::endl;
            return false;
        }
        daw.transportPlaying.store(false, std::memory_order_relaxed);
        daw.transportRecording.store(false, std::memory_order_relaxed);
        daw.transportLatch = 0;
        daw.recordStopPending = false;

        const auto& audioTracksJson = root.contains("audio_tracks") && root["audio_tracks"].is_array()
            ? root["audio_tracks"] : json::array();
        const auto& midiTracksJson = root.contains("midi_tracks") && root["midi_tracks"].is_array()
            ? root["midi_tracks"] : json::array();
        const auto& automationTracksJson = root.contains("automation_tracks") && root["automation_tracks"].is_array()
            ? root["automation_tracks"] : json::array();

        if (!setTrackCountsForSession(baseSystem,
                                      static_cast<int>(audioTracksJson.size()),
                                      static_cast<int>(midiTracksJson.size()),
                                      static_cast<int>(automationTracksJson.size()))) {
            std::cerr << "Session load failed: could not apply track counts." << std::endl;
            return false;
        }

        daw.trackCount = static_cast<int>(daw.tracks.size());
        daw.automationTrackCount = static_cast<int>(daw.automationTracks.size());
        midi.trackCount = static_cast<int>(midi.tracks.size());

        {
            std::lock_guard<std::mutex> dawLock(daw.trackMutex);
            std::lock_guard<std::mutex> midiLock(midi.trackMutex);

            daw.clipAudio.clear();
            for (auto& track : daw.tracks) {
                track.audio.clear();
                track.audioRight.clear();
                track.pendingRecord.clear();
                track.pendingRecordRight.clear();
                track.clips.clear();
                track.loopTakeClips.clear();
                track.waveformMin.clear();
                track.waveformMax.clear();
                track.waveformMinRight.clear();
                track.waveformMaxRight.clear();
                track.waveformColor.clear();
                track.waveformVersion = 0;
                track.recordEnabled.store(false, std::memory_order_relaxed);
                track.meterLevel.store(0.0f, std::memory_order_relaxed);
                track.recordingActive = false;
            }
            for (auto& track : midi.tracks) {
                track.audio.clear();
                track.pendingRecord.clear();
                track.pendingNotes.clear();
                track.clips.clear();
                track.loopTakeClips.clear();
                track.waveformMin.clear();
                track.waveformMax.clear();
                track.waveformColor.clear();
                track.waveformVersion = 0;
                track.recordEnabled.store(false, std::memory_order_relaxed);
                track.meterLevel.store(0.0f, std::memory_order_relaxed);
                track.recordingActive = false;
                track.activeRecordNote = -1;
            }

            const auto& audioPoolJson = root.contains("audio_pool") && root["audio_pool"].is_array()
                ? root["audio_pool"] : json::array();
            daw.clipAudio.reserve(audioPoolJson.size());
            for (const auto& clipEntry : audioPoolJson) {
                std::string rel = readString(clipEntry, "file", "");
                DawClipAudio data;
                uint32_t rate = 0;
                if (!rel.empty()) {
                    std::filesystem::path inPath = assetDir / rel;
                    loadWavClipAudio(inPath.string(), data, rate);
                }
                daw.clipAudio.push_back(std::move(data));
            }

            const json timeline = root.contains("timeline") && root["timeline"].is_object()
                ? root["timeline"] : json::object();
            daw.timelineSecondsPerScreen = std::max(0.01, readDouble(timeline, "seconds_per_screen", 10.0));
            daw.timelineOffsetSamples = static_cast<int64_t>(readDouble(timeline, "offset_samples", 0.0));
            daw.timelineZeroSample = static_cast<int64_t>(readDouble(timeline, "zero_sample", 0.0));
            daw.timelineLaneHeight = std::clamp(readFloat(timeline, "lane_height", 60.0f), 24.0f, 180.0f);
            daw.timelineLaneOffset = readFloat(timeline, "lane_offset", 0.0f);

            const json transport = root.contains("transport") && root["transport"].is_object()
                ? root["transport"] : json::object();
            daw.bpm.store(std::clamp(readDouble(transport, "bpm", 120.0), 40.0, 240.0), std::memory_order_relaxed);
            daw.metronomeEnabled.store(readBool(transport, "metronome_enabled", false), std::memory_order_relaxed);
            daw.loopEnabled.store(readBool(transport, "loop_enabled", false), std::memory_order_relaxed);
            daw.loopStartSamples = readU64(transport, "loop_start_samples", 0);
            daw.loopEndSamples = readU64(transport, "loop_end_samples", daw.loopStartSamples + 1);
            if (daw.loopEndSamples <= daw.loopStartSamples) {
                daw.loopEndSamples = daw.loopStartSamples + 1;
            }

            const json exportJson = root.contains("export") && root["export"].is_object()
                ? root["export"] : json::object();
            daw.exportStartBar = readInt(exportJson, "start_bar", 1);
            daw.exportEndBar = readInt(exportJson, "end_bar", 5);
            daw.exportFolderPath = readString(exportJson, "folder_path", daw.exportFolderPath);
            auto itStemNames = exportJson.find("stem_names");
            if (itStemNames != exportJson.end() && itStemNames->is_array()) {
                for (size_t i = 0; i < daw.exportStemNames.size() && i < itStemNames->size(); ++i) {
                    if ((*itStemNames)[i].is_string()) {
                        daw.exportStemNames[i] = (*itStemNames)[i].get<std::string>();
                    }
                }
            }

            for (size_t i = 0; i < daw.tracks.size() && i < audioTracksJson.size(); ++i) {
                DawTrack& track = daw.tracks[i];
                const json& trackJson = audioTracksJson[i];
                track.armMode.store(readInt(trackJson, "arm_mode", 0), std::memory_order_relaxed);
                track.mute.store(readBool(trackJson, "mute", false), std::memory_order_relaxed);
                track.solo.store(readBool(trackJson, "solo", false), std::memory_order_relaxed);
                int outputBus = std::clamp(readInt(trackJson, "output_bus", 2), -1, DawContext::kBusCount - 1);
                int outputBusL = std::clamp(readInt(trackJson, "output_bus_l", outputBus), -1, DawContext::kBusCount - 1);
                int outputBusR = std::clamp(readInt(trackJson, "output_bus_r", outputBus), -1, DawContext::kBusCount - 1);
                track.outputBus.store(outputBusL, std::memory_order_relaxed);
                track.outputBusL.store(outputBusL, std::memory_order_relaxed);
                track.outputBusR.store(outputBusR, std::memory_order_relaxed);
                track.gain.store(std::max(0.0f, readFloat(trackJson, "gain", 1.0f)), std::memory_order_relaxed);
                track.physicalInputIndex = std::max(0, readInt(trackJson, "physical_input_index", 0));
                track.stereoInputPair12 = readBool(trackJson, "stereo_input_pair12", false);
                track.nextTakeId = std::max(1, readInt(trackJson, "next_take_id", 1));
                track.activeLoopTakeIndex = readInt(trackJson, "active_loop_take_index", -1);
                track.takeStackExpanded = readBool(trackJson, "take_stack_expanded", false);
                track.loopTakeRangeStartSample = readU64(trackJson, "loop_take_range_start_sample", 0);
                track.loopTakeRangeLength = readU64(trackJson, "loop_take_range_length", 0);
                auto itClips = trackJson.find("clips");
                if (itClips != trackJson.end() && itClips->is_array()) {
                    track.clips.reserve(itClips->size());
                    for (const auto& clipJson : *itClips) {
                        track.clips.push_back(deserializeDawClip(clipJson));
                    }
                }
                auto itTakes = trackJson.find("loop_take_clips");
                if (itTakes != trackJson.end() && itTakes->is_array()) {
                    track.loopTakeClips.reserve(itTakes->size());
                    for (const auto& clipJson : *itTakes) {
                        track.loopTakeClips.push_back(deserializeDawClip(clipJson));
                    }
                }
                if (track.activeLoopTakeIndex < -1 || track.activeLoopTakeIndex >= static_cast<int>(track.loopTakeClips.size())) {
                    track.activeLoopTakeIndex = track.loopTakeClips.empty() ? -1 : 0;
                }
                DawClipSystemLogic::RebuildTrackCacheFromClips(daw, track);
            }

            for (size_t i = 0; i < midi.tracks.size() && i < midiTracksJson.size(); ++i) {
                MidiTrack& track = midi.tracks[i];
                const json& trackJson = midiTracksJson[i];
                track.armMode.store(readInt(trackJson, "arm_mode", 0), std::memory_order_relaxed);
                track.mute.store(readBool(trackJson, "mute", false), std::memory_order_relaxed);
                track.solo.store(readBool(trackJson, "solo", false), std::memory_order_relaxed);
                int outputBus = std::clamp(readInt(trackJson, "output_bus", 2), -1, DawContext::kBusCount - 1);
                int outputBusL = std::clamp(readInt(trackJson, "output_bus_l", outputBus), -1, DawContext::kBusCount - 1);
                int outputBusR = std::clamp(readInt(trackJson, "output_bus_r", outputBus), -1, DawContext::kBusCount - 1);
                track.outputBus.store(outputBusL, std::memory_order_relaxed);
                track.outputBusL.store(outputBusL, std::memory_order_relaxed);
                track.outputBusR.store(outputBusR, std::memory_order_relaxed);
                track.gain.store(std::max(0.0f, readFloat(trackJson, "gain", 1.0f)), std::memory_order_relaxed);
                track.nextTakeId = std::max(1, readInt(trackJson, "next_take_id", 1));
                track.activeLoopTakeIndex = readInt(trackJson, "active_loop_take_index", -1);
                track.takeStackExpanded = readBool(trackJson, "take_stack_expanded", false);
                track.loopTakeRangeStartSample = readU64(trackJson, "loop_take_range_start_sample", 0);
                track.loopTakeRangeLength = readU64(trackJson, "loop_take_range_length", 0);

                auto itClips = trackJson.find("clips");
                if (itClips != trackJson.end() && itClips->is_array()) {
                    track.clips.reserve(itClips->size());
                    for (const auto& clipJson : *itClips) {
                        track.clips.push_back(deserializeMidiClip(clipJson));
                    }
                }
                auto itTakes = trackJson.find("loop_take_clips");
                if (itTakes != trackJson.end() && itTakes->is_array()) {
                    track.loopTakeClips.reserve(itTakes->size());
                    for (const auto& clipJson : *itTakes) {
                        track.loopTakeClips.push_back(deserializeMidiClip(clipJson));
                    }
                }
                if (track.activeLoopTakeIndex < -1 || track.activeLoopTakeIndex >= static_cast<int>(track.loopTakeClips.size())) {
                    track.activeLoopTakeIndex = track.loopTakeClips.empty() ? -1 : 0;
                }
                MidiWaveformSystemLogic::RebuildWaveform(track, daw.sampleRate);
            }

            for (size_t i = 0; i < daw.automationTracks.size() && i < automationTracksJson.size(); ++i) {
                AutomationTrack& track = daw.automationTracks[i];
                const json& trackJson = automationTracksJson[i];
                track.clips.clear();
                track.targetLaneType = std::clamp(readInt(trackJson, "target_lane_type", 0), 0, 1);
                track.targetLaneTrack = std::max(0, readInt(trackJson, "target_lane_track", 0));
                track.targetDeviceSlot = std::max(0, readInt(trackJson, "target_device_slot", 0));
                track.targetParameterSlot = std::max(0, readInt(trackJson, "target_parameter_slot", 0));
                track.targetParameterId = readInt(trackJson, "target_parameter_id", -1);
                track.targetLaneLabel = readString(trackJson, "target_lane_label", track.targetLaneLabel);
                track.targetDeviceLabel = readString(trackJson, "target_device_label", track.targetDeviceLabel);
                track.targetParameterLabel = readString(trackJson, "target_parameter_label", track.targetParameterLabel);
                auto itClips = trackJson.find("clips");
                if (itClips != trackJson.end() && itClips->is_array()) {
                    track.clips.reserve(itClips->size());
                    for (const auto& clipJson : *itClips) {
                        track.clips.push_back(deserializeAutomationClip(clipJson));
                    }
                }
            }

            restoreLaneOrder(daw,
                             root.contains("lane_order") ? root["lane_order"] : json::array(),
                             static_cast<int>(daw.tracks.size()),
                             static_cast<int>(midi.tracks.size()),
                             static_cast<int>(daw.automationTracks.size()));
        }

        if (baseSystem.vst3) {
            Vst3Context& vst3 = *baseSystem.vst3;
            const int audioTrackCount = static_cast<int>(daw.tracks.size());
            const int midiTrackCount = static_cast<int>(midi.tracks.size());
            clearAllVst3TrackPlugins(vst3);
            Vst3SystemLogic::EnsureAudioTrackCount(vst3, audioTrackCount);
            Vst3SystemLogic::EnsureMidiTrackCount(vst3, midiTrackCount);

            for (int t = 0; t < audioTrackCount && t < static_cast<int>(audioTracksJson.size()); ++t) {
                const json& trackJson = audioTracksJson[static_cast<size_t>(t)];
                auto itFx = trackJson.find("fx_chain");
                if (itFx == trackJson.end() || !itFx->is_array()) continue;
                for (const auto& pluginJson : *itFx) {
                    const Vst3AvailablePlugin* available = findAvailablePluginByState(vst3, pluginJson, false);
                    if (!available) continue;
                    if (!Vst3SystemLogic::AddPluginToTrack(vst3, *available, t, audioTrackCount)) continue;
                    Vst3Plugin* plugin = findNewlyAddedPlugin(vst3, t, audioTrackCount, false);
                    applyPluginParameters(plugin, pluginJson);
                }
            }

            for (int t = 0; t < midiTrackCount && t < static_cast<int>(midiTracksJson.size()); ++t) {
                const json& trackJson = midiTracksJson[static_cast<size_t>(t)];
                int trackIndex = audioTrackCount + t;
                auto itInstrument = trackJson.find("instrument");
                if (itInstrument != trackJson.end() && itInstrument->is_object()) {
                    const Vst3AvailablePlugin* available = findAvailablePluginByState(vst3, *itInstrument, true);
                    if (available && Vst3SystemLogic::AddPluginToTrack(vst3, *available, trackIndex, audioTrackCount)) {
                        Vst3Plugin* plugin = findNewlyAddedPlugin(vst3, trackIndex, audioTrackCount, true);
                        applyPluginParameters(plugin, *itInstrument);
                    }
                }
                auto itFx = trackJson.find("fx_chain");
                if (itFx == trackJson.end() || !itFx->is_array()) continue;
                for (const auto& pluginJson : *itFx) {
                    const Vst3AvailablePlugin* available = findAvailablePluginByState(vst3, pluginJson, false);
                    if (!available) continue;
                    if (!Vst3SystemLogic::AddPluginToTrack(vst3, *available, trackIndex, audioTrackCount)) continue;
                    Vst3Plugin* plugin = findNewlyAddedPlugin(vst3, trackIndex, audioTrackCount, false);
                    applyPluginParameters(plugin, pluginJson);
                }
            }
        }

        uint64_t loadedPlayhead = 0;
        if (root.contains("timeline") && root["timeline"].is_object()) {
            loadedPlayhead = readU64(root["timeline"], "playhead_sample", 0);
        }
        daw.playheadSample.store(loadedPlayhead, std::memory_order_relaxed);
        daw.selectedLaneIndex = -1;
        daw.selectedLaneType = -1;
        daw.selectedLaneTrack = -1;
        daw.selectedClipTrack = -1;
        daw.selectedClipIndex = -1;
        daw.selectedAutomationClipTrack = -1;
        daw.selectedAutomationClipIndex = -1;
        midi.selectedTrackIndex = -1;
        midi.selectedClipTrack = -1;
        midi.selectedClipIndex = -1;
        midi.pianoRollActive = false;
        if (baseSystem.ui) {
            baseSystem.ui->buttonCacheBuilt = false;
        }
        if (baseSystem.font) {
            baseSystem.font->textCacheBuilt = false;
        }
        daw.uiCacheBuilt = false;
        midi.uiCacheBuilt = false;
        std::cerr << "Session loaded: " << sessionPath << std::endl;
        return true;
    }

    bool PromptAndSaveSession(BaseSystem& baseSystem) {
        std::string path;
        if (!OpenSaveSessionDialog(path)) return false;
        return SaveSession(baseSystem, path);
    }

    bool PromptAndLoadSession(BaseSystem& baseSystem) {
        std::string path;
        if (!OpenLoadSessionDialog(path)) return false;
        return LoadSession(baseSystem, path);
    }

    bool StartStemExport(BaseSystem& baseSystem) {
        if (!baseSystem.daw || !baseSystem.audio) return false;
        DawContext& daw = *baseSystem.daw;
        AudioContext& audio = *baseSystem.audio;
        if (daw.exportInProgress.load(std::memory_order_relaxed) || daw.exportJobActive) {
            return false;
        }

        int64_t startSampleSigned = BarDisplayToSample(daw, daw.exportStartBar);
        int64_t endSampleSigned = BarDisplayToSample(daw, daw.exportEndBar);
        if (endSampleSigned <= startSampleSigned) {
            daw.exportSucceeded = false;
            daw.exportStatusMessage = "Export failed: end bar must be after start bar.";
            return false;
        }

        uint64_t startSample = static_cast<uint64_t>(std::max<int64_t>(0, startSampleSigned));
        uint64_t endSample = static_cast<uint64_t>(std::max<int64_t>(0, endSampleSigned));
        if (endSample <= startSample) {
            daw.exportSucceeded = false;
            daw.exportStatusMessage = "Export failed: invalid range.";
            return false;
        }

        if (daw.exportFolderPath.empty()) {
            daw.exportFolderPath = daw.mirrorAvailable ? daw.mirrorPath : std::filesystem::current_path().string();
        }
        std::error_code ec;
        std::filesystem::create_directories(daw.exportFolderPath, ec);
        if (ec) {
            daw.exportSucceeded = false;
            daw.exportStatusMessage = "Export failed: could not create folder.";
            return false;
        }

        daw.exportSavedPlayheadSample = daw.playheadSample.load(std::memory_order_relaxed);
        daw.exportSavedTransportPlaying = daw.transportPlaying.load(std::memory_order_relaxed);
        daw.exportSavedTransportRecording = daw.transportRecording.load(std::memory_order_relaxed);
        daw.exportSavedLoopEnabled = daw.loopEnabled.load(std::memory_order_relaxed);
        daw.exportSavedTransportLatch = daw.transportLatch;
        daw.transportPlaying.store(false, std::memory_order_relaxed);
        daw.transportRecording.store(false, std::memory_order_relaxed);
        daw.transportLatch = 0;

        daw.exportJobStartSample = startSample;
        daw.exportJobEndSample = endSample;
        daw.exportJobCursorSample = startSample;
        daw.exportJobActive = true;
        daw.exportInProgress.store(true, std::memory_order_relaxed);
        daw.exportProgress.store(0.0f, std::memory_order_relaxed);
        daw.exportSucceeded = false;
        daw.exportStatusMessage = "Exporting stems...";

        const uint64_t reserveSamples = endSample - startSample;
        for (auto& stem : daw.exportStemBuffers) {
            stem.clear();
            stem.reserve(static_cast<size_t>(reserveSamples));
        }

        daw.exportMidiHeldVelocities.clear();
        if (baseSystem.vst3) {
            daw.exportSavedContinuousSamples = baseSystem.vst3->continuousSamples;
            if (baseSystem.midi) {
                Vst3SystemLogic::EnsureMidiTrackCount(*baseSystem.vst3,
                                                      static_cast<int>(baseSystem.midi->tracks.size()));
                daw.exportMidiHeldVelocities.resize(baseSystem.vst3->midiTracks.size(), {});
            }
        } else {
            daw.exportSavedContinuousSamples = 0;
        }

        audio.offlineRenderMute.store(true, std::memory_order_relaxed);
        return true;
    }

    void TickStemExport(BaseSystem& baseSystem, float) {
        if (!baseSystem.daw) return;
        DawContext& daw = *baseSystem.daw;
        if (!daw.exportJobActive) return;

        uint32_t blockFrames = 512;
        if (baseSystem.vst3 && baseSystem.vst3->blockSize > 0) {
            blockFrames = static_cast<uint32_t>(std::max(64, baseSystem.vst3->blockSize));
        }
        constexpr int kBlocksPerTick = 8;
        for (int i = 0; i < kBlocksPerTick; ++i) {
            if (!daw.exportJobActive) break;
            if (daw.exportJobCursorSample >= daw.exportJobEndSample) break;
            if (!processStemExportBlock(baseSystem, blockFrames)) {
                finishStemExport(baseSystem, false, "Export failed during render.");
                return;
            }
        }

        if (daw.exportJobCursorSample < daw.exportJobEndSample) {
            return;
        }

        flushExportMidiNotes(baseSystem);

        const uint32_t sampleRate = static_cast<uint32_t>(std::max(1.0f, daw.sampleRate));
        bool wroteAll = true;
        std::filesystem::path outDir(daw.exportFolderPath);
        for (int i = 0; i < DawContext::kBusCount; ++i) {
            const std::string stemName = sanitizeStemName(daw.exportStemNames[static_cast<size_t>(i)], i);
            std::filesystem::path outPath = outDir / (stemName + ".wav");
            if (!writeWavMonoFloat(outPath.string(),
                                   daw.exportStemBuffers[static_cast<size_t>(i)],
                                   sampleRate)) {
                wroteAll = false;
            }
        }
        if (wroteAll) {
            finishStemExport(baseSystem, true, "Export complete.");
        } else {
            finishStemExport(baseSystem, false, "Export failed while writing files.");
        }
    }

    bool ParseThemeColorHex(const std::string& value, glm::vec4& outColor) {
        return parseRgbaHex(value, outColor);
    }

    std::string ThemeColorToHex(const glm::vec4& color) {
        return rgbaToHex(color);
    }

    bool ApplyThemeByIndex(BaseSystem& baseSystem, int themeIndex, bool persistToDisk) {
        if (!baseSystem.daw) return false;
        DawContext& daw = *baseSystem.daw;
        if (themeIndex < 0 || themeIndex >= static_cast<int>(daw.themes.size())) return false;
        applyThemeToWorld(baseSystem,
                          daw,
                          daw.themes[static_cast<size_t>(themeIndex)]);
        daw.settingsSelectedTheme = themeIndex;
        daw.selectedThemeName = daw.themes[static_cast<size_t>(themeIndex)].name;
        if (persistToDisk) {
            return writeThemesToDisk(daw);
        }
        return true;
    }

    bool RemoveThemeByIndex(BaseSystem& baseSystem, int themeIndex, std::string& outMessage) {
        outMessage.clear();
        if (!baseSystem.daw) {
            outMessage = "Theme delete failed.";
            return false;
        }
        DawContext& daw = *baseSystem.daw;
        if (themeIndex < 0 || themeIndex >= static_cast<int>(daw.themes.size())) {
            outMessage = "Theme delete failed.";
            return false;
        }
        if (isThemeDeleteProtected(daw.themes[static_cast<size_t>(themeIndex)])) {
            outMessage = "Default, Default 2, and Default 3 cannot be deleted.";
            return false;
        }

        int nextSelected = daw.settingsSelectedTheme;
        if (nextSelected == themeIndex) {
            nextSelected = themeIndex - 1;
        } else if (nextSelected > themeIndex) {
            nextSelected -= 1;
        }

        daw.themes.erase(daw.themes.begin() + themeIndex);
        if (daw.themes.empty()) {
            outMessage = "No themes available.";
            return false;
        }
        nextSelected = std::clamp(nextSelected, 0, static_cast<int>(daw.themes.size()) - 1);
        if (!ApplyThemeByIndex(baseSystem, nextSelected, false)) {
            outMessage = "Theme delete failed.";
            return false;
        }
        daw.settingsSelectedTheme = nextSelected;

        if (!writeThemesToDisk(daw)) {
            outMessage = "Failed to write themes file.";
            return false;
        }
        outMessage = "Theme deleted.";
        return true;
    }

    bool SaveThemeFromDraft(BaseSystem& baseSystem,
                            const std::string& rawName,
                            const std::string& backgroundHex,
                            const std::string& panelHex,
                            const std::string& buttonHex,
                            const std::string& pianoRollHex,
                            const std::string& pianoRollAccentHex,
                            const std::string& laneHex,
                            std::string& outMessage) {
        outMessage.clear();
        if (!baseSystem.daw) {
            outMessage = "Theme save failed.";
            return false;
        }
        DawContext& daw = *baseSystem.daw;

        std::string name = trimLineEnd(rawName);
        if (name.empty()) {
            outMessage = "Theme name is required.";
            return false;
        }
        if (name == "Default") {
            outMessage = "Default theme is reserved.";
            return false;
        }

        glm::vec4 bg{};
        glm::vec4 panel{};
        glm::vec4 button{};
        glm::vec4 pianoRoll{};
        glm::vec4 pianoRollAccent{};
        glm::vec4 lane{};
        if (!parseRgbaHex(backgroundHex, bg)) {
            outMessage = "Background must be 8-digit RGBA hex (optional #).";
            return false;
        }
        if (!parseRgbaHex(panelHex, panel)) {
            outMessage = "Panel must be 8-digit RGBA hex (optional #).";
            return false;
        }
        if (!parseRgbaHex(buttonHex, button)) {
            outMessage = "Button must be 8-digit RGBA hex (optional #).";
            return false;
        }
        if (!parseRgbaHex(pianoRollHex, pianoRoll)) {
            outMessage = "Piano roll must be 8-digit RGBA hex (optional #).";
            return false;
        }
        if (!parseRgbaHex(pianoRollAccentHex, pianoRollAccent)) {
            outMessage = "Piano accent must be 8-digit RGBA hex (optional #).";
            return false;
        }
        if (!parseRgbaHex(laneHex, lane)) {
            outMessage = "Lane must be 8-digit RGBA hex (optional #).";
            return false;
        }

        DawThemePreset preset;
        preset.name = name;
        preset.background = clampThemeColor(bg);
        preset.panel = clampThemeColor(panel);
        preset.button = clampThemeColor(button);
        preset.pianoRoll = clampThemeColor(pianoRoll);
        preset.pianoRollAccent = clampThemeColor(pianoRollAccent);
        preset.lane = clampThemeColor(lane);
        preset.isBuiltin = false;

        int existing = findThemeIndexByName(daw, name);
        if (existing == 0) {
            outMessage = "Default theme cannot be overwritten.";
            return false;
        }
        if (existing > 0) {
            daw.themes[static_cast<size_t>(existing)] = preset;
        } else {
            daw.themes.push_back(preset);
            existing = static_cast<int>(daw.themes.size()) - 1;
        }
        daw.settingsSelectedTheme = existing;
        if (!writeThemesToDisk(daw)) {
            outMessage = "Failed to write themes file.";
            return false;
        }
        outMessage = "Theme saved.";
        return true;
    }

    void BeginThemeDraftFromDefault(BaseSystem& baseSystem) {
        if (!baseSystem.daw) return;
        DawContext& daw = *baseSystem.daw;
        if (daw.themes.empty()) return;
        fillThemeDraftFromPreset(daw, daw.themes.front(), false);
        daw.themeDraftName.clear();
        daw.themeEditField = 0;
        daw.themeStatusMessage.clear();
    }

    void EnsureThemeState(BaseSystem& baseSystem) {
        if (!baseSystem.daw) return;
        DawContext& daw = *baseSystem.daw;
        if (daw.themesLoaded) {
            if (!daw.themeAppliedToWorld && baseSystem.world && !daw.themes.empty()) {
                int selectedIndex = std::clamp(daw.settingsSelectedTheme, 0, static_cast<int>(daw.themes.size()) - 1);
                ApplyThemeByIndex(baseSystem, selectedIndex, false);
            }
            return;
        }

        DawThemePreset defaultPreset = makeDefaultThemePreset(baseSystem);
        defaultPreset.isBuiltin = true;
        daw.themes.clear();
        daw.themes.push_back(defaultPreset);
        daw.selectedThemeName = "Default";

        std::filesystem::path path = themeFilePath();
        std::ifstream in(path);
        if (in.is_open()) {
            json root;
            try {
                root = json::parse(in);
            } catch (...) {
                root = json();
            }
            if (root.is_object() && readString(root, "format", "") == kThemeFormat) {
                auto itThemes = root.find("themes");
                if (itThemes != root.end() && itThemes->is_array()) {
                    for (const auto& item : *itThemes) {
                        DawThemePreset preset;
                        if (!deserializeThemePreset(item, preset)) continue;
                        if (preset.name == "Default") continue;
                        if (findThemeIndexByName(daw, preset.name) >= 0) continue;
                        daw.themes.push_back(preset);
                    }
                }
                const std::string selectedName = readString(root, "selected_theme", "Default");
                if (!selectedName.empty()) daw.selectedThemeName = selectedName;
            }
        }

        auto ensureBuiltinTheme = [&](const std::string& name) {
            if (findThemeIndexByName(daw, name) >= 0) return;
            daw.themes.push_back(makeBuiltinThemePreset(name));
        };
        ensureBuiltinTheme("Default 2");
        ensureBuiltinTheme("Default 3");

        int selectedIndex = findThemeIndexByName(daw, daw.selectedThemeName);
        if (selectedIndex < 0) selectedIndex = 0;
        daw.settingsSelectedTheme = selectedIndex;
        daw.themesLoaded = true;
        daw.themeAppliedToWorld = false;
        ApplyThemeByIndex(baseSystem, selectedIndex, false);

        if (!std::filesystem::exists(path)) {
            writeThemesToDisk(daw);
        }
    }

    void UpdateDawIO(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*) {
    }
}
