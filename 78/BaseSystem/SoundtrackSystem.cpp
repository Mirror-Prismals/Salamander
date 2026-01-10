#pragma once

#include <fstream>
#include <vector>
#include <string>
#include <iostream>

namespace SoundtrackSystemLogic {

    namespace {
        struct WavInfo {
            uint16_t audioFormat = 0;
            uint16_t numChannels = 0;
            uint32_t sampleRate = 0;
            uint16_t bitsPerSample = 0;
            uint32_t dataSize = 0;
            std::streampos dataPos = 0;
        };

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

        bool loadWavMono(const std::string& path, std::vector<float>& outSamples, uint32_t& outRate) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) return false;
            WavInfo info;
            if (!readWavInfo(file, info)) return false;
            if (info.dataSize == 0 || info.numChannels == 0) return false;

            outRate = info.sampleRate;
            file.seekg(info.dataPos);

            if (info.audioFormat == 3 && info.bitsPerSample == 32) {
                size_t frameCount = info.dataSize / (sizeof(float) * info.numChannels);
                outSamples.assign(frameCount, 0.0f);
                for (size_t i = 0; i < frameCount; ++i) {
                    float sample = 0.0f;
                    for (uint16_t ch = 0; ch < info.numChannels; ++ch) {
                        float v = 0.0f;
                        file.read(reinterpret_cast<char*>(&v), sizeof(float));
                        sample += v;
                    }
                    outSamples[i] = sample / static_cast<float>(info.numChannels);
                }
                return true;
            }

            if (info.audioFormat == 1 && info.bitsPerSample == 16) {
                size_t frameCount = info.dataSize / (sizeof(int16_t) * info.numChannels);
                outSamples.assign(frameCount, 0.0f);
                for (size_t i = 0; i < frameCount; ++i) {
                    int32_t sum = 0;
                    for (uint16_t ch = 0; ch < info.numChannels; ++ch) {
                        int16_t v = 0;
                        file.read(reinterpret_cast<char*>(&v), sizeof(int16_t));
                        sum += v;
                    }
                    float sample = static_cast<float>(sum) / (static_cast<float>(info.numChannels) * 32768.0f);
                    outSamples[i] = sample;
                }
                return true;
            }

            return false;
        }
    }

    void UpdateSoundtracks(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow*) {
        if (!baseSystem.audio) return;
        AudioContext& audio = *baseSystem.audio;
        static std::string lastRayPath;
        static std::string lastHeadPath;
        static std::string lastRayError;
        static std::string lastHeadError;

        auto loadTrack = [&](const std::string& path,
                             std::vector<float>& buffer,
                             uint32_t& sampleRate,
                             double& pos,
                             const char* label,
                             std::string& lastPath,
                             std::string& lastError) {
            if (path.empty()) return;
            if (!buffer.empty() && lastPath == path) return;
            std::vector<float> samples;
            uint32_t rate = 0;
            if (!loadWavMono(path, samples, rate)) {
                if (lastError != path) {
                    std::cerr << "SoundtrackSystem: failed to load " << label << " '" << path
                              << "' (expect mono 16-bit PCM or 32-bit float WAV)." << std::endl;
                    lastError = path;
                }
                return;
            }
            buffer = std::move(samples);
            sampleRate = rate;
            pos = 0.0;
            lastPath = path;
        };

        loadTrack(audio.rayTestPath, audio.rayTestBuffer, audio.rayTestSampleRate, audio.rayTestPos,
                  "ray track", lastRayPath, lastRayError);
        loadTrack(audio.headTrackPath, audio.headTrackBuffer, audio.headTrackSampleRate, audio.headTrackPos,
                  "head track", lastHeadPath, lastHeadError);
    }
}
