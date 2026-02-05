#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace MidiWaveformSystemLogic {
    void RebuildWaveform(MidiTrack& track, float sampleRate);
}

namespace MidiIOSystemLogic {

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

        bool readWavMonoFloat(const std::string& path, std::vector<float>& outData, float& outSampleRate) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) return false;
            WavInfo info;
            if (!readWavInfo(file, info)) return false;
            if (info.audioFormat != 3 || info.numChannels != 1 || info.bitsPerSample != 32) return false;
            file.seekg(info.dataPos);
            size_t sampleCount = info.dataSize / sizeof(float);
            outData.resize(sampleCount);
            file.read(reinterpret_cast<char*>(outData.data()), info.dataSize);
            outSampleRate = static_cast<float>(info.sampleRate);
            return true;
        }

        bool writeWavMonoFloat(const std::string& path, const std::vector<float>& data, uint32_t sampleRate) {
            std::ofstream file(path, std::ios::binary);
            if (!file.is_open()) return false;

            uint32_t dataSize = static_cast<uint32_t>(data.size() * sizeof(float));
            uint32_t riffSize = 36 + dataSize;
            file.write("RIFF", 4);
            file.write(reinterpret_cast<const char*>(&riffSize), sizeof(riffSize));
            file.write("WAVE", 4);
            file.write("fmt ", 4);
            uint32_t fmtSize = 16;
            uint16_t audioFormat = 3;
            uint16_t numChannels = 1;
            uint32_t byteRate = sampleRate * numChannels * sizeof(float);
            uint16_t blockAlign = numChannels * sizeof(float);
            uint16_t bitsPerSample = 32;
            file.write(reinterpret_cast<const char*>(&fmtSize), sizeof(fmtSize));
            file.write(reinterpret_cast<const char*>(&audioFormat), sizeof(audioFormat));
            file.write(reinterpret_cast<const char*>(&numChannels), sizeof(numChannels));
            file.write(reinterpret_cast<const char*>(&sampleRate), sizeof(sampleRate));
            file.write(reinterpret_cast<const char*>(&byteRate), sizeof(byteRate));
            file.write(reinterpret_cast<const char*>(&blockAlign), sizeof(blockAlign));
            file.write(reinterpret_cast<const char*>(&bitsPerSample), sizeof(bitsPerSample));
            file.write("data", 4);
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            file.write(reinterpret_cast<const char*>(data.data()), dataSize);
            return true;
        }
    }

    void WriteTracksIfNeeded(MidiContext& midi, const DawContext& daw) {
        if (!daw.mirrorAvailable) return;
        for (int i = 0; i < static_cast<int>(midi.tracks.size()); ++i) {
            const auto& data = midi.tracks[static_cast<size_t>(i)].audio;
            std::filesystem::path outPath = std::filesystem::path(daw.mirrorPath) / ("midi_track_" + std::to_string(i + 1) + ".wav");
            writeWavMonoFloat(outPath.string(), data, static_cast<uint32_t>(midi.sampleRate));
        }
    }

    void WriteDefaultTrackFile(MidiContext& midi, const DawContext& daw, const std::vector<float>& data) {
        std::filesystem::path outPath = std::filesystem::path(daw.mirrorPath) / "midi_track_1.wav";
        writeWavMonoFloat(outPath.string(), data, static_cast<uint32_t>(midi.sampleRate));
    }

    void LoadTracksIfAvailable(MidiContext& midi, const DawContext& daw) {
        if (!daw.mirrorAvailable) return;
        for (int i = 0; i < static_cast<int>(midi.tracks.size()); ++i) {
            std::filesystem::path inPath = std::filesystem::path(daw.mirrorPath) / ("midi_track_" + std::to_string(i + 1) + ".wav");
            float sampleRate = 0.0f;
            std::vector<float> data;
            if (readWavMonoFloat(inPath.string(), data, sampleRate)) {
                midi.tracks[static_cast<size_t>(i)].audio = std::move(data);
                MidiWaveformSystemLogic::RebuildWaveform(midi.tracks[static_cast<size_t>(i)], sampleRate);
            } else {
                midi.tracks[static_cast<size_t>(i)].audio.clear();
                midi.tracks[static_cast<size_t>(i)].clips.clear();
                midi.tracks[static_cast<size_t>(i)].waveformMin.clear();
                midi.tracks[static_cast<size_t>(i)].waveformMax.clear();
                midi.tracks[static_cast<size_t>(i)].waveformColor.clear();
                midi.tracks[static_cast<size_t>(i)].waveformVersion += 1;
            }
        }
    }

    void UpdateMidiIO(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*) {
    }
}
