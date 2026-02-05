#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <utility>
#include <fstream>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace DawClipSystemLogic {
    void RebuildTrackCacheFromClips(DawContext& daw, DawTrack& track);
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
            std::vector<float> data;
            if (loadWavMonoFloat(inPath.string(), data, rate)) {
                DawTrack& track = daw.tracks[static_cast<size_t>(i)];
                track.clips.clear();
                int audioId = static_cast<int>(daw.clipAudio.size());
                daw.clipAudio.emplace_back(std::move(data));
                DawClip clip{};
                clip.audioId = audioId;
                clip.startSample = 0;
                clip.sourceOffset = 0;
                clip.length = (audioId >= 0 && audioId < static_cast<int>(daw.clipAudio.size()))
                    ? static_cast<uint64_t>(daw.clipAudio[audioId].size())
                    : 0;
                if (clip.length > 0) {
                    track.clips.push_back(clip);
                }
                DawClipSystemLogic::RebuildTrackCacheFromClips(daw, track);
            } else {
                daw.tracks[static_cast<size_t>(i)].clips.clear();
                daw.tracks[static_cast<size_t>(i)].audio.clear();
                daw.tracks[static_cast<size_t>(i)].waveformMin.clear();
                daw.tracks[static_cast<size_t>(i)].waveformMax.clear();
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

    void UpdateDawIO(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*) {
    }
}
