#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace ButtonSystemLogic {
    void SetButtonToggled(int instanceID, bool toggled);
}
namespace Vst3SystemLogic {
    void EnsureAudioTrackCount(Vst3Context& ctx, int trackCount);
    void RemoveAudioTrackChain(Vst3Context& ctx, int trackIndex);
    void MoveAudioTrackChain(Vst3Context& ctx, int fromIndex, int toIndex);
}
namespace MidiStateSystemLogic {
    bool RemoveTrackAt(BaseSystem& baseSystem, int trackIndex);
}

namespace DawStateSystemLogic {

    namespace {
        constexpr int kRecordRingSeconds = 2;
        constexpr size_t kWaveformBlockSize = 256;
        constexpr double kTimelineScrollSeconds = 5.0;
        constexpr float kLaneLeftMargin = 40.0f;
        constexpr float kLaneRightMargin = 40.0f;
        constexpr int kFrequencyStepsPerColor = 23;
        constexpr int kFrequencyBaseColorCount = 4;
        constexpr int kFrequencyTotalSteps = kFrequencyStepsPerColor * (kFrequencyBaseColorCount - 1);
        constexpr float kFrequencyMinHz = 20.0f;
        constexpr float kFrequencyMaxHz = 1200.0f;
        constexpr float kFrequencySilenceThreshold = 1e-6f;
        const glm::vec3 kWaveformFallbackColor(0.188235f, 0.164706f, 0.145098f);
        const std::array<glm::vec3, kFrequencyBaseColorCount> kFrequencyBaseColors = {{
            glm::vec3(1.0f, 0.2f, 0.3f),
            glm::vec3(1.0f, 0.6f, 0.2f),
            glm::vec3(1.0f, 1.0f, 0.2f),
            glm::vec3(0.2f, 1.0f, 0.2f)
        }};

        enum TransportLatch {
            kTransportNone = 0,
            kTransportStop = 1,
            kTransportPlay = 2,
            kTransportRecord = 3
        };

        struct WavInfo {
            uint16_t audioFormat = 0;
            uint16_t numChannels = 0;
            uint32_t sampleRate = 0;
            uint16_t bitsPerSample = 0;
            uint32_t dataSize = 0;
            std::streampos dataPos = 0;
        };

        struct Complex {
            float r = 0.0f;
            float i = 0.0f;
        };

        float lerp(float a, float b, float t) {
            return a + t * (b - a);
        }

        float smoothstep(float t) {
            return t * t * (3.0f - 2.0f * t);
        }

        Complex add(const Complex& a, const Complex& b) {
            return { a.r + b.r, a.i + b.i };
        }

        Complex sub(const Complex& a, const Complex& b) {
            return { a.r - b.r, a.i - b.i };
        }

        Complex mul(const Complex& a, const Complex& b) {
            return { a.r * b.r - a.i * b.i, a.r * b.i + a.i * b.r };
        }

        void fft(std::vector<Complex>& data) {
            size_t n = data.size();
            if (n <= 1) return;

            std::vector<Complex> even(n / 2), odd(n / 2);
            for (size_t i = 0; i < n / 2; ++i) {
                even[i] = data[2 * i];
                odd[i] = data[2 * i + 1];
            }
            fft(even);
            fft(odd);
            for (size_t k = 0; k < n / 2; ++k) {
                float angle = -2.0f * 3.14159265358979f * static_cast<float>(k) / static_cast<float>(n);
                Complex wk = { std::cos(angle), std::sin(angle) };
                Complex temp = mul(wk, odd[k]);
                data[k] = add(even[k], temp);
                data[k + n / 2] = sub(even[k], temp);
            }
        }

        float computeDominantFrequency(const float* samples,
                                       size_t sampleCount,
                                       float sampleRate,
                                       std::vector<Complex>& buffer) {
            if (sampleCount == 0) return 0.0f;
            if (buffer.size() != sampleCount) buffer.resize(sampleCount);
            for (size_t i = 0; i < sampleCount; ++i) {
                buffer[i] = { samples[i], 0.0f };
            }
            fft(buffer);
            size_t half = sampleCount / 2;
            float maxMagSq = 0.0f;
            size_t maxIdx = 0;
            for (size_t k = 1; k < half; ++k) {
                float magSq = buffer[k].r * buffer[k].r + buffer[k].i * buffer[k].i;
                if (magSq > maxMagSq) {
                    maxMagSq = magSq;
                    maxIdx = k;
                }
            }
            if (maxMagSq <= kFrequencySilenceThreshold) return 0.0f;
            if (sampleRate <= 0.0f) sampleRate = 44100.0f;
            return (sampleRate * static_cast<float>(maxIdx)) / static_cast<float>(sampleCount);
        }

        glm::vec3 frequencyToColor(float freq) {
            float clampedFreq = std::clamp(freq, kFrequencyMinHz, kFrequencyMaxHz);
            float norm = (std::log10(clampedFreq) - std::log10(kFrequencyMinHz)) /
                (std::log10(kFrequencyMaxHz) - std::log10(kFrequencyMinHz));
            float colorIndex = norm * static_cast<float>(kFrequencyTotalSteps);
            float stepIndex = colorIndex / static_cast<float>(kFrequencyStepsPerColor);
            int colorSegment = static_cast<int>(stepIndex);
            if (colorSegment >= kFrequencyBaseColorCount - 1) {
                colorSegment = kFrequencyBaseColorCount - 2;
                stepIndex = static_cast<float>(kFrequencyBaseColorCount - 1);
            }
            float t = stepIndex - static_cast<float>(colorSegment);
            t = smoothstep(t);
            const glm::vec3& a = kFrequencyBaseColors[colorSegment];
            const glm::vec3& b = kFrequencyBaseColors[colorSegment + 1];
            return glm::vec3(lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t));
        }

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

        bool resolveMirrorPath(DawContext& daw) {
            std::filesystem::path mirror = std::filesystem::path(getExecutableDir()) / "Mirror";
            daw.mirrorPath = mirror.string();
            daw.mirrorAvailable = std::filesystem::exists(mirror) && std::filesystem::is_directory(mirror);
            return daw.mirrorAvailable;
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

        void rebuildWaveform(DawTrack& track, float sampleRate) {
            track.waveformMin.clear();
            track.waveformMax.clear();
            track.waveformColor.clear();
            if (track.audio.empty()) {
                track.waveformVersion += 1;
                return;
            }
            size_t blockCount = (track.audio.size() + kWaveformBlockSize - 1) / kWaveformBlockSize;
            track.waveformMin.assign(blockCount, 0.0f);
            track.waveformMax.assign(blockCount, 0.0f);
            track.waveformColor.assign(blockCount, kWaveformFallbackColor);
            std::vector<float> fftSamples(kWaveformBlockSize, 0.0f);
            std::vector<Complex> fftBuffer(kWaveformBlockSize);
            for (size_t block = 0; block < blockCount; ++block) {
                size_t start = block * kWaveformBlockSize;
                size_t end = std::min(start + kWaveformBlockSize, track.audio.size());
                float minVal = 1.0f;
                float maxVal = -1.0f;
                for (size_t i = start; i < end; ++i) {
                    float v = track.audio[i];
                    minVal = std::min(minVal, v);
                    maxVal = std::max(maxVal, v);
                }
                if (end == start) {
                    minVal = 0.0f;
                    maxVal = 0.0f;
                }
                track.waveformMin[block] = minVal;
                track.waveformMax[block] = maxVal;

                if (end == start) {
                    track.waveformColor[block] = kWaveformFallbackColor;
                    continue;
                }
                size_t count = end - start;
                for (size_t i = 0; i < kWaveformBlockSize; ++i) {
                    if (i < count) {
                        fftSamples[i] = track.audio[start + i];
                    } else {
                        fftSamples[i] = track.audio[end - 1];
                    }
                }
                float domFreq = computeDominantFrequency(fftSamples.data(), fftSamples.size(), sampleRate, fftBuffer);
                if (domFreq <= 0.0f) {
                    track.waveformColor[block] = kWaveformFallbackColor;
                } else {
                    track.waveformColor[block] = frequencyToColor(domFreq);
                }
            }
            track.waveformVersion += 1;
        }

        void updateWaveformRange(DawTrack& track, size_t startSample, size_t endSample) {
            size_t recordedStart = static_cast<size_t>(track.recordStartSample);
            size_t recordedEnd = recordedStart + track.pendingRecord.size();
            size_t combinedLength = std::max(track.audio.size(), recordedEnd);
            if (combinedLength == 0 || endSample <= startSample) return;

            size_t blockCount = (combinedLength + kWaveformBlockSize - 1) / kWaveformBlockSize;
            if (track.waveformMin.size() < blockCount) {
                track.waveformMin.resize(blockCount, 0.0f);
                track.waveformMax.resize(blockCount, 0.0f);
            }
            size_t blockStart = startSample / kWaveformBlockSize;
            size_t blockEnd = (endSample + kWaveformBlockSize - 1) / kWaveformBlockSize;
            blockEnd = std::min(blockEnd, blockCount);
            for (size_t block = blockStart; block < blockEnd; ++block) {
                size_t sampleStart = block * kWaveformBlockSize;
                size_t sampleEnd = std::min(sampleStart + kWaveformBlockSize, combinedLength);
                float minVal = 1.0f;
                float maxVal = -1.0f;
                for (size_t i = sampleStart; i < sampleEnd; ++i) {
                    float base = (i < track.audio.size()) ? track.audio[i] : 0.0f;
                    float rec = 0.0f;
                    if (i >= recordedStart && i < recordedEnd) {
                        rec = track.pendingRecord[i - recordedStart];
                    }
                    float v = (track.recordArmMode == 2 && i >= recordedStart && i < recordedEnd)
                        ? rec
                        : (base + rec);
                    minVal = std::min(minVal, v);
                    maxVal = std::max(maxVal, v);
                }
                if (sampleEnd == sampleStart) {
                    minVal = 0.0f;
                    maxVal = 0.0f;
                }
                track.waveformMin[block] = minVal;
                track.waveformMax[block] = maxVal;
            }
            track.waveformVersion += 1;
        }

        int addClipAudio(DawContext& daw, std::vector<float>&& data) {
            daw.clipAudio.emplace_back(std::move(data));
            return static_cast<int>(daw.clipAudio.size()) - 1;
        }

        void sortClipsByStart(std::vector<DawClip>& clips) {
            std::sort(clips.begin(), clips.end(), [](const DawClip& a, const DawClip& b) {
                if (a.startSample == b.startSample) return a.sourceOffset < b.sourceOffset;
                return a.startSample < b.startSample;
            });
        }

        void trimClipsForNewClip(DawTrack& track, const DawClip& clip) {
            if (clip.length == 0) return;
            uint64_t newStart = clip.startSample;
            uint64_t newEnd = clip.startSample + clip.length;
            std::vector<DawClip> updated;
            updated.reserve(track.clips.size() + 1);
            for (const auto& existing : track.clips) {
                if (existing.length == 0) continue;
                uint64_t exStart = existing.startSample;
                uint64_t exEnd = existing.startSample + existing.length;
                if (exEnd <= newStart || exStart >= newEnd) {
                    updated.push_back(existing);
                    continue;
                }
                if (newStart <= exStart && newEnd >= exEnd) {
                    continue;
                }
                if (newStart > exStart && newEnd < exEnd) {
                    DawClip left = existing;
                    left.length = newStart - exStart;
                    DawClip right = existing;
                    right.startSample = newEnd;
                    right.length = exEnd - newEnd;
                    right.sourceOffset = existing.sourceOffset + (newEnd - exStart);
                    if (left.length > 0) updated.push_back(left);
                    if (right.length > 0) updated.push_back(right);
                } else if (newStart <= exStart) {
                    DawClip right = existing;
                    right.startSample = newEnd;
                    right.length = exEnd - newEnd;
                    right.sourceOffset = existing.sourceOffset + (newEnd - exStart);
                    if (right.length > 0) updated.push_back(right);
                } else {
                    DawClip left = existing;
                    left.length = newStart - exStart;
                    if (left.length > 0) updated.push_back(left);
                }
            }
            track.clips = std::move(updated);
        }

        void rebuildTrackCacheFromClips(DawContext& daw, DawTrack& track) {
            uint64_t maxEnd = 0;
            for (const auto& clip : track.clips) {
                uint64_t end = clip.startSample + clip.length;
                if (end > maxEnd) maxEnd = end;
            }
            track.audio.clear();
            track.audio.resize(static_cast<size_t>(maxEnd), 0.0f);
            for (const auto& clip : track.clips) {
                if (clip.audioId < 0 || clip.audioId >= static_cast<int>(daw.clipAudio.size())) continue;
                const auto& data = daw.clipAudio[clip.audioId];
                uint64_t clipStart = clip.startSample;
                uint64_t clipEnd = clip.startSample + clip.length;
                uint64_t srcOffset = clip.sourceOffset;
                if (clipEnd <= clipStart) continue;
                if (srcOffset >= data.size()) continue;
                uint64_t maxCopy = std::min<uint64_t>(clip.length, data.size() - srcOffset);
                for (uint64_t i = 0; i < maxCopy; ++i) {
                    size_t dst = static_cast<size_t>(clipStart + i);
                    if (dst >= track.audio.size()) break;
                    track.audio[dst] = data[static_cast<size_t>(srcOffset + i)];
                }
            }
            rebuildWaveform(track, daw.sampleRate);
        }

        int parseTrackIndex(const std::string& value, int trackCount) {
            if (value.empty()) return -1;
            try {
                int idx = std::stoi(value);
                if (idx < 0 || idx >= trackCount) return -1;
                return idx;
            } catch (...) {
                return -1;
            }
        }

        int parseBus(const std::string& value) {
            if (value == "L") return 0;
            if (value == "S" || value == "SUB") return 1;
            if (value == "FF" || value == "F") return 2;
            if (value == "R") return 3;
            return -1;
        }

        const char* busLabelForIndex(int busIndex) {
            switch (busIndex) {
                case 0: return "L";
                case 1: return "S";
                case 2: return "F";
                case 3: return "R";
                default: return "";
            }
        }

        const char* busStateForIndex(int busIndex) {
            switch (busIndex) {
                case 0: return "bus_L";
                case 1: return "bus_S";
                case 2: return "bus_F";
                case 3: return "bus_R";
                default: return "idle";
            }
        }

        bool parseTrackIndexFromKey(const std::string& key,
                                    const std::string& prefix,
                                    bool oneBased,
                                    int trackCount,
                                    int& outTrack) {
            if (key.rfind(prefix, 0) != 0) return false;
            std::string suffix = key.substr(prefix.size());
            if (suffix.empty()) return false;
            try {
                int idx = std::stoi(suffix);
                if (oneBased) idx -= 1;
                if (idx < 0 || idx >= trackCount) return false;
                outTrack = idx;
                return true;
            } catch (...) {
                return false;
            }
        }

        bool parseTrackAndBus(const std::string& value, int trackCount, int& outTrack, int& outBus) {
            size_t sep = value.find(':');
            if (sep == std::string::npos) return false;
            int trackIdx = parseTrackIndex(value.substr(0, sep), trackCount);
            int busIdx = parseBus(value.substr(sep + 1));
            if (trackIdx < 0 || busIdx < 0) return false;
            outTrack = trackIdx;
            outBus = busIdx;
            return true;
        }

        int getTrackCount(const DawContext& daw) {
            return static_cast<int>(daw.tracks.size());
        }

        int getMidiTrackCount(const BaseSystem& baseSystem) {
            if (!baseSystem.midi) return 0;
            return static_cast<int>(baseSystem.midi->tracks.size());
        }

        void rebuildLaneOrder(DawContext& daw, int audioCount, int midiCount) {
            daw.laneOrder.clear();
            daw.laneOrder.reserve(static_cast<size_t>(audioCount + midiCount));
            for (int i = 0; i < audioCount; ++i) {
                daw.laneOrder.push_back({0, i});
            }
            for (int i = 0; i < midiCount; ++i) {
                daw.laneOrder.push_back({1, i});
            }
        }

        void ensureLaneOrder(DawContext& daw, int audioCount, int midiCount) {
            int total = audioCount + midiCount;
            bool rebuild = daw.laneOrder.empty() || static_cast<int>(daw.laneOrder.size()) != total;
            if (!rebuild) {
                std::vector<int> audioSeen(audioCount, 0);
                std::vector<int> midiSeen(midiCount, 0);
                for (const auto& entry : daw.laneOrder) {
                    if (entry.type == 0) {
                        if (entry.trackIndex < 0 || entry.trackIndex >= audioCount) { rebuild = true; break; }
                        audioSeen[entry.trackIndex] += 1;
                    } else {
                        if (entry.trackIndex < 0 || entry.trackIndex >= midiCount) { rebuild = true; break; }
                        midiSeen[entry.trackIndex] += 1;
                    }
                }
                if (!rebuild) {
                    for (int c : audioSeen) { if (c != 1) { rebuild = true; break; } }
                    if (!rebuild) {
                        for (int c : midiSeen) { if (c != 1) { rebuild = true; break; } }
                    }
                }
            }
            if (rebuild) {
                rebuildLaneOrder(daw, audioCount, midiCount);
            }
            if (daw.selectedLaneIndex >= total) {
                daw.selectedLaneIndex = total - 1;
            }
            if (daw.selectedLaneIndex < 0 && total > 0 && !daw.allowEmptySelection) {
                daw.selectedLaneIndex = 0;
            }
        }

        void removeLaneEntryForTrack(DawContext& daw, int type, int trackIndex) {
            int removedIndex = -1;
            for (auto it = daw.laneOrder.begin(); it != daw.laneOrder.end(); ) {
                if (it->type == type && it->trackIndex == trackIndex) {
                    if (removedIndex < 0) {
                        removedIndex = static_cast<int>(std::distance(daw.laneOrder.begin(), it));
                    }
                    it = daw.laneOrder.erase(it);
                } else {
                    ++it;
                }
            }
            for (auto& entry : daw.laneOrder) {
                if (entry.type == type && entry.trackIndex > trackIndex) {
                    entry.trackIndex -= 1;
                }
            }
            if (removedIndex >= 0) {
                if (daw.selectedLaneIndex == removedIndex) {
                    daw.selectedLaneIndex = -1;
                    daw.selectedLaneType = -1;
                    daw.selectedLaneTrack = -1;
                } else if (daw.selectedLaneIndex > removedIndex) {
                    daw.selectedLaneIndex -= 1;
                }
            } else if (daw.selectedLaneIndex >= static_cast<int>(daw.laneOrder.size())) {
                daw.selectedLaneIndex = static_cast<int>(daw.laneOrder.size()) - 1;
            }
        }

        void insertLaneEntry(DawContext& daw, int laneIndex, int type, int trackIndex) {
            if (laneIndex < 0) laneIndex = 0;
            if (laneIndex > static_cast<int>(daw.laneOrder.size())) {
                laneIndex = static_cast<int>(daw.laneOrder.size());
            }
            DawContext::LaneEntry entry{type, trackIndex};
            daw.laneOrder.insert(daw.laneOrder.begin() + laneIndex, entry);
            if (daw.selectedLaneIndex >= laneIndex && daw.selectedLaneIndex >= 0) {
                daw.selectedLaneIndex += 1;
            }
        }

        void initTrack(DawTrack& track, int index, AudioContext& audio, float sampleRate);
        void refreshTrackRouting(DawContext& daw, AudioContext& audio);

        int addTrackInternal(BaseSystem& baseSystem, DawContext& daw, AudioContext& audio) {
            std::lock_guard<std::mutex> lock(daw.trackMutex);
            int index = getTrackCount(daw);
            daw.tracks.emplace_back();
            initTrack(daw.tracks.back(), index, audio, daw.sampleRate);
            daw.trackCount = getTrackCount(daw);
            refreshTrackRouting(daw, audio);
            if (baseSystem.vst3) {
                Vst3SystemLogic::EnsureAudioTrackCount(*baseSystem.vst3, daw.trackCount);
            }
            return index;
        }

        void reconnectTrackInput(AudioContext& audio, DawTrack& track);
        void refreshPhysicalInputs(AudioContext& audio);

        void initTrack(DawTrack& track, int index, AudioContext& audio, float sampleRate) {
            track.inputIndex = index;
            track.outputBus.store(2, std::memory_order_relaxed);
            int physicalCount = static_cast<int>(audio.physicalInputPorts.size());
            if (physicalCount > 0) {
                track.physicalInputIndex = index % physicalCount;
            } else {
                track.physicalInputIndex = 0;
            }
            track.useVirtualInput.store(physicalCount == 0, std::memory_order_relaxed);
            if (!track.recordRing) {
                size_t ringBytes = static_cast<size_t>(sampleRate) * kRecordRingSeconds * sizeof(float);
                track.recordRing = jack_ringbuffer_create(ringBytes);
                if (track.recordRing) {
                    jack_ringbuffer_mlock(track.recordRing);
                }
            }
            if (physicalCount > 0) {
                reconnectTrackInput(audio, track);
            }
        }

        void cleanupTrack(DawTrack& track) {
            if (track.recordRing) {
                jack_ringbuffer_free(track.recordRing);
                track.recordRing = nullptr;
            }
        }

        void refreshTrackRouting(DawContext& daw, AudioContext& audio) {
            int physicalCount = static_cast<int>(audio.physicalInputPorts.size());
            for (int i = 0; i < getTrackCount(daw); ++i) {
                DawTrack& track = daw.tracks[i];
                track.inputIndex = i;
                if (track.physicalInputIndex < 0 || track.physicalInputIndex >= std::max(1, physicalCount + 1)) {
                    track.physicalInputIndex = 0;
                }
                track.useVirtualInput.store(physicalCount == 0, std::memory_order_relaxed);
                if (physicalCount > 0) {
                    reconnectTrackInput(audio, track);
                }
            }
        }

        int detectMirrorTrackCount(const DawContext& daw) {
            if (!daw.mirrorAvailable) return 0;
            std::error_code ec;
            int maxIndex = 0;
            for (const auto& entry : std::filesystem::directory_iterator(daw.mirrorPath, ec)) {
                if (ec || !entry.is_regular_file()) continue;
                const std::string name = entry.path().filename().string();
                if (name.rfind("track_", 0) != 0) continue;
                if (name.size() <= 10 || name.substr(name.size() - 4) != ".wav") continue;
                std::string num = name.substr(6, name.size() - 10);
                try {
                    int idx = std::stoi(num);
                    if (idx > maxIndex) maxIndex = idx;
                } catch (...) {
                }
            }
            return maxIndex;
        }

        void deleteStaleTrackFile(const DawContext& daw, int oneBasedIndex) {
            if (!daw.mirrorAvailable || oneBasedIndex <= 0) return;
            std::filesystem::path outPath = std::filesystem::path(daw.mirrorPath)
                / ("track_" + std::to_string(oneBasedIndex) + ".wav");
            std::error_code ec;
            std::filesystem::remove(outPath, ec);
        }

        void ensureTrackCount(BaseSystem& baseSystem, DawContext& daw, AudioContext& audio, int desired) {
            if (desired < 0) desired = 0;
            std::lock_guard<std::mutex> lock(daw.trackMutex);
            int current = getTrackCount(daw);
            if (desired > current) {
                daw.tracks.resize(static_cast<size_t>(desired));
                for (int i = current; i < desired; ++i) {
                    initTrack(daw.tracks[i], i, audio, daw.sampleRate);
                }
            } else if (desired < current) {
                int oldCount = current;
                for (int i = current - 1; i >= desired; --i) {
                    cleanupTrack(daw.tracks[static_cast<size_t>(i)]);
                }
                daw.tracks.erase(daw.tracks.begin() + desired, daw.tracks.end());
                deleteStaleTrackFile(daw, oldCount);
            }
            daw.trackCount = getTrackCount(daw);
            refreshTrackRouting(daw, audio);
            if (baseSystem.vst3) {
                Vst3SystemLogic::EnsureAudioTrackCount(*baseSystem.vst3, daw.trackCount);
            }
        }

        bool removeTrackAt(BaseSystem& baseSystem, DawContext& daw, AudioContext& audio, int trackIndex) {
            int current = getTrackCount(daw);
            if (trackIndex < 0 || trackIndex >= current) return false;
            std::lock_guard<std::mutex> lock(daw.trackMutex);
            int oldCount = current;
            if (baseSystem.vst3) {
                Vst3SystemLogic::RemoveAudioTrackChain(*baseSystem.vst3, trackIndex);
            }
            cleanupTrack(daw.tracks[static_cast<size_t>(trackIndex)]);
            daw.tracks.erase(daw.tracks.begin() + trackIndex);
            daw.trackCount = getTrackCount(daw);
            refreshTrackRouting(daw, audio);
            deleteStaleTrackFile(daw, oldCount);
            if (baseSystem.vst3) {
                Vst3SystemLogic::EnsureAudioTrackCount(*baseSystem.vst3, daw.trackCount);
            }
            removeLaneEntryForTrack(daw, 0, trackIndex);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            daw.uiCacheBuilt = false;
            return true;
        }

        bool moveTrack(BaseSystem& baseSystem, DawContext& daw, AudioContext& audio, int fromIndex, int toIndex) {
            (void)audio;
            int count = static_cast<int>(daw.laneOrder.size());
            if (fromIndex < 0 || fromIndex >= count) return false;
            if (toIndex < 0) toIndex = 0;
            if (toIndex >= count) toIndex = count - 1;
            if (fromIndex == toIndex) return false;
            DawContext::LaneEntry moved = daw.laneOrder[static_cast<size_t>(fromIndex)];
            daw.laneOrder.erase(daw.laneOrder.begin() + fromIndex);
            daw.laneOrder.insert(daw.laneOrder.begin() + toIndex, moved);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            daw.uiCacheBuilt = false;
            return true;
        }

        bool addTrack(BaseSystem& baseSystem, DawContext& daw, AudioContext& audio) {
            int index = addTrackInternal(baseSystem, daw, audio);
            insertLaneEntry(daw, static_cast<int>(daw.laneOrder.size()), 0, index);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            daw.uiCacheBuilt = false;
            return true;
        }

        bool insertTrackAt(BaseSystem& baseSystem, DawContext& daw, AudioContext& audio, int trackIndex) {
            int laneIndex = trackIndex;
            int newIndex = addTrackInternal(baseSystem, daw, audio);
            insertLaneEntry(daw, laneIndex, 0, newIndex);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            daw.uiCacheBuilt = false;
            return true;
        }

        uint64_t maxTimelineSamples(const DawContext& daw) {
            uint64_t maxSamples = daw.playheadSample.load(std::memory_order_relaxed);
            for (const auto& track : daw.tracks) {
                maxSamples = std::max<uint64_t>(maxSamples, static_cast<uint64_t>(track.audio.size()));
            }
            return maxSamples;
        }

        void clampTimelineOffset(DawContext& daw) {
            double secondsPerScreen = (daw.timelineSecondsPerScreen > 0.0) ? daw.timelineSecondsPerScreen : 10.0;
            int64_t windowSamples = static_cast<int64_t>(secondsPerScreen * daw.sampleRate);
            if (windowSamples < 0) windowSamples = 0;
            uint64_t maxSamples = maxTimelineSamples(daw);
            int64_t maxOffset = (maxSamples > static_cast<uint64_t>(windowSamples))
                ? static_cast<int64_t>(maxSamples - static_cast<uint64_t>(windowSamples))
                : 0;
            if (daw.timelineOffsetSamples < 0) daw.timelineOffsetSamples = 0;
            if (daw.timelineOffsetSamples > maxOffset) daw.timelineOffsetSamples = maxOffset;
        }

        bool isTrackRowWorld(const std::string& name) {
            if (name == "TrackRowWorld") return true;
            return name.rfind("TrackRowWorld_", 0) == 0;
        }

        bool anyRecordEnabled(const DawContext& daw) {
            for (const auto& track : daw.tracks) {
                if (track.recordEnabled.load(std::memory_order_relaxed)) return true;
            }
            return false;
        }

        bool hasRingData(const DawContext& daw) {
            for (const auto& track : daw.tracks) {
                if (!track.recordRing) continue;
                if (jack_ringbuffer_read_space(track.recordRing) > 0) return true;
            }
            return false;
        }

        int findWorldIndex(const LevelContext& level, const std::string& name) {
            for (size_t i = 0; i < level.worlds.size(); ++i) {
                if (level.worlds[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        void ensureTimelineLabelCapacity(BaseSystem& baseSystem, DawContext& daw, int desiredTime, int desiredBar) {
            if (!baseSystem.level || !baseSystem.instance) return;
            LevelContext& level = *baseSystem.level;
            int screenWorldIndex = findWorldIndex(level, "DAWScreenWorld");
            if (screenWorldIndex < 0 || screenWorldIndex >= static_cast<int>(level.worlds.size())) return;
            auto& insts = level.worlds[screenWorldIndex].instances;

            const EntityInstance* timeTemplate = nullptr;
            const EntityInstance* barTemplate = nullptr;
            int timeCount = 0;
            int barCount = 0;
            for (auto& inst : insts) {
                if (inst.controlRole == "timeline_label") {
                    if (!timeTemplate) timeTemplate = &inst;
                    timeCount += 1;
                } else if (inst.controlRole == "timeline_bar_label") {
                    if (!barTemplate) barTemplate = &inst;
                    barCount += 1;
                }
            }
            if (!timeTemplate && !barTemplate) return;
            EntityInstance timeTemplateCopy{};
            EntityInstance barTemplateCopy{};
            if (timeTemplate) timeTemplateCopy = *timeTemplate;
            if (barTemplate) barTemplateCopy = *barTemplate;

            bool added = false;
            while (timeTemplate && timeCount < desiredTime) {
                EntityInstance inst = timeTemplateCopy;
                inst.instanceID = baseSystem.instance->nextInstanceID++;
                inst.controlId = "daw_time_" + std::to_string(timeCount);
                inst.textKey = "daw_time_" + std::to_string(timeCount);
                inst.controlRole = "timeline_label";
                inst.position = timeTemplateCopy.position;
                insts.push_back(std::move(inst));
                timeCount += 1;
                added = true;
            }
            while (barTemplate && barCount < desiredBar) {
                EntityInstance inst = barTemplateCopy;
                inst.instanceID = baseSystem.instance->nextInstanceID++;
                inst.controlId = "daw_bar_" + std::to_string(barCount);
                inst.textKey = "daw_bar_" + std::to_string(barCount);
                inst.controlRole = "timeline_bar_label";
                inst.position = barTemplateCopy.position;
                insts.push_back(std::move(inst));
                barCount += 1;
                added = true;
            }
            if (added) {
                if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
                if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
                daw.uiCacheBuilt = false;
            }
        }

        void drainRecordRings(DawContext& daw) {
            for (auto& track : daw.tracks) {
                if (!track.recordRing) continue;
                size_t bytes = jack_ringbuffer_read_space(track.recordRing);
                if (bytes == 0) continue;
                size_t frames = bytes / sizeof(float);
                if (frames == 0) continue;
                size_t oldSize = track.pendingRecord.size();
                track.pendingRecord.resize(oldSize + frames);
                jack_ringbuffer_read(track.recordRing,
                                     reinterpret_cast<char*>(track.pendingRecord.data() + oldSize),
                                     frames * sizeof(float));
                size_t start = static_cast<size_t>(track.recordStartSample + oldSize);
                size_t end = static_cast<size_t>(track.recordStartSample + track.pendingRecord.size());
                updateWaveformRange(track, start, end);
            }
        }

        void startRecording(DawContext& daw, uint64_t playhead) {
            for (auto& track : daw.tracks) {
                int armMode = track.armMode.load(std::memory_order_relaxed);
                bool enable = (armMode > 0);
                track.recordEnabled.store(enable, std::memory_order_relaxed);
                if (!enable) {
                    track.recordingActive = false;
                    continue;
                }
                track.recordingActive = true;
                track.recordStartSample = playhead;
                track.recordArmMode = armMode;
                track.pendingRecord.clear();
                if (track.recordRing) {
                    jack_ringbuffer_reset(track.recordRing);
                }
            }
        }

        void stopRecording(DawContext& daw) {
            for (auto& track : daw.tracks) {
                track.recordEnabled.store(false, std::memory_order_relaxed);
                if (track.recordingActive) {
                    track.recordingActive = false;
                }
            }
        }

        void mergePendingRecords(DawContext& daw) {
            for (auto& track : daw.tracks) {
                if (track.pendingRecord.empty()) continue;
                DawClip newClip{};
                newClip.audioId = addClipAudio(daw, std::move(track.pendingRecord));
                newClip.startSample = track.recordStartSample;
                newClip.length = (newClip.audioId >= 0 && newClip.audioId < static_cast<int>(daw.clipAudio.size()))
                    ? static_cast<uint64_t>(daw.clipAudio[newClip.audioId].size())
                    : 0;
                newClip.sourceOffset = 0;
                trimClipsForNewClip(track, newClip);
                if (newClip.length > 0) {
                    track.clips.push_back(newClip);
                    sortClipsByStart(track.clips);
                }
                track.pendingRecord.clear();
                rebuildTrackCacheFromClips(daw, track);
            }
        }

        void writeTracksIfNeeded(DawContext& daw) {
            if (!daw.mirrorAvailable) return;
            for (int i = 0; i < getTrackCount(daw); ++i) {
                const auto& data = daw.tracks[static_cast<size_t>(i)].audio;
                std::filesystem::path outPath = std::filesystem::path(daw.mirrorPath) / ("track_" + std::to_string(i + 1) + ".wav");
                writeWavMonoFloat(outPath.string(), data, static_cast<uint32_t>(daw.sampleRate));
            }
        }

        void loadTracksIfAvailable(DawContext& daw) {
            if (!daw.mirrorAvailable) return;
            for (int i = 0; i < getTrackCount(daw); ++i) {
                std::filesystem::path inPath = std::filesystem::path(daw.mirrorPath) / ("track_" + std::to_string(i + 1) + ".wav");
                uint32_t rate = 0;
                std::vector<float> data;
                if (loadWavMonoFloat(inPath.string(), data, rate)) {
                    DawTrack& track = daw.tracks[static_cast<size_t>(i)];
                    track.clips.clear();
                    int audioId = addClipAudio(daw, std::move(data));
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
                    rebuildTrackCacheFromClips(daw, track);
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

        void loadMetronomeSample(DawContext& daw) {
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

        void initializeDaw(DawContext& daw, AudioContext& audio) {
            if (daw.initialized) return;
            refreshPhysicalInputs(audio);
            daw.sampleRate = audio.sampleRate > 0.0f ? audio.sampleRate : 44100.0f;
            resolveMirrorPath(daw);
            if (daw.trackCount <= 0 && daw.mirrorAvailable) {
                daw.trackCount = detectMirrorTrackCount(daw);
            }
            if (daw.trackCount < 0) daw.trackCount = 0;
            if (daw.tracks.empty() && daw.trackCount > 0) {
                daw.tracks.resize(static_cast<size_t>(daw.trackCount));
            }
            if (daw.trackCount != getTrackCount(daw)) {
                daw.trackCount = getTrackCount(daw);
            }
            for (int i = 0; i < getTrackCount(daw); ++i) {
                initTrack(daw.tracks[static_cast<size_t>(i)], i, audio, daw.sampleRate);
            }
            loadTracksIfAvailable(daw);
            loadMetronomeSample(daw);
            if (daw.loopEndSamples == 0) {
                double bpm = daw.bpm.load(std::memory_order_relaxed);
                if (bpm <= 0.0) bpm = 120.0;
                double secondsPerBeat = 60.0 / bpm;
                uint64_t bars = 4;
                daw.loopStartSamples = 0;
                daw.loopEndSamples = static_cast<uint64_t>(std::llround(secondsPerBeat * 4.0 * bars * daw.sampleRate));
            }
            daw.playheadSample.store(0, std::memory_order_relaxed);
            daw.transportPlaying.store(false, std::memory_order_relaxed);
            daw.transportRecording.store(false, std::memory_order_relaxed);
            daw.audioThreadIdle.store(true, std::memory_order_relaxed);
            daw.transportLatch = kTransportNone;
            daw.timelineSecondsPerScreen = 10.0;
            daw.timelineOffsetSamples = 0;
            audio.daw = &daw;
            daw.initialized = true;
        }

        void updateTrackButtonVisuals(BaseSystem& baseSystem, DawContext& daw) {
            (void)baseSystem;
            int trackCount = getTrackCount(daw);
            for (auto* instPtr : daw.trackInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.actionKey == "arm") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0 && inst.controlId.rfind("track_", 0) == 0) {
                        size_t start = 6;
                        size_t end = inst.controlId.find('_', start);
                        if (end != std::string::npos) {
                            trackIndex = parseTrackIndex(inst.controlId.substr(start, end - start), trackCount);
                        }
                    }
                    if (trackIndex < 0) continue;
                    int armMode = daw.tracks[trackIndex].armMode.load(std::memory_order_relaxed);
                    if (armMode == 1) inst.uiState = "overdub";
                    else if (armMode == 2) inst.uiState = "replace";
                    else inst.uiState = "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, armMode > 0);
                } else if (inst.actionKey == "solo") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0 && inst.controlId.rfind("track_", 0) == 0) {
                        size_t start = 6;
                        size_t end = inst.controlId.find('_', start);
                        if (end != std::string::npos) {
                            trackIndex = parseTrackIndex(inst.controlId.substr(start, end - start), trackCount);
                        }
                    }
                    if (trackIndex < 0) continue;
                    bool active = daw.tracks[trackIndex].solo.load(std::memory_order_relaxed);
                    inst.uiState = active ? "active" : "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, active);
                } else if (inst.actionKey == "mute") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0 && inst.controlId.rfind("track_", 0) == 0) {
                        size_t start = 6;
                        size_t end = inst.controlId.find('_', start);
                        if (end != std::string::npos) {
                            trackIndex = parseTrackIndex(inst.controlId.substr(start, end - start), trackCount);
                        }
                    }
                    if (trackIndex < 0) continue;
                    bool active = daw.tracks[trackIndex].mute.load(std::memory_order_relaxed);
                    inst.uiState = active ? "active" : "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, active);
                } else if (inst.actionKey == "output") {
                    int trackIndex = -1;
                    int busIndex = -1;
                    if (parseTrackAndBus(inst.actionValue, trackCount, trackIndex, busIndex)) {
                        int currentBus = daw.tracks[trackIndex].outputBus.load(std::memory_order_relaxed);
                        inst.uiState = (currentBus == busIndex) ? "selected" : "idle";
                    } else {
                        trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                        if (trackIndex < 0 && inst.controlId.rfind("track_", 0) == 0) {
                            size_t start = 6;
                            size_t end = inst.controlId.find('_', start);
                            if (end != std::string::npos) {
                                trackIndex = parseTrackIndex(inst.controlId.substr(start, end - start), trackCount);
                            }
                        }
                        if (trackIndex < 0) continue;
                        int currentBus = daw.tracks[trackIndex].outputBus.load(std::memory_order_relaxed);
                        inst.uiState = busStateForIndex(currentBus);
                    }
                } else if (inst.actionKey == "input" || inst.actionKey == "clear") {
                    inst.uiState = "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, false);
                }
            }
        }

        void updateTransportButtonVisuals(BaseSystem& baseSystem, const DawContext& daw) {
            (void)baseSystem;
            for (auto* instPtr : daw.transportInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                bool toggled = false;
                if (inst.actionKey == "stop") {
                    toggled = (daw.transportLatch == kTransportStop);
                } else if (inst.actionKey == "play") {
                    toggled = (daw.transportLatch == kTransportPlay);
                } else if (inst.actionKey == "record") {
                    toggled = (daw.transportLatch == kTransportRecord);
                }
                ButtonSystemLogic::SetButtonToggled(inst.instanceID, toggled);
                inst.uiState = toggled ? "active" : "idle";
            }
        }

        void updateTempoButtonVisuals(BaseSystem& baseSystem, const DawContext& daw) {
            (void)baseSystem;
            for (auto* instPtr : daw.tempoInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.actionKey == "metronome") {
                    bool enabled = daw.metronomeEnabled.load(std::memory_order_relaxed);
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, enabled);
                    inst.uiState = enabled ? "active" : "idle";
                }
            }
        }

        void updateLoopButtonVisuals(BaseSystem& baseSystem, const DawContext& daw) {
            (void)baseSystem;
            for (auto* instPtr : daw.loopInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.actionKey == "toggle") {
                    bool enabled = daw.loopEnabled.load(std::memory_order_relaxed);
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, enabled);
                    inst.uiState = enabled ? "active" : "idle";
                }
            }
        }

        void updateDawUILayout(BaseSystem& baseSystem, DawContext& daw, GLFWwindow* win) {
            if (!win) return;
            int windowWidth = 0, windowHeight = 0;
            glfwGetWindowSize(win, &windowWidth, &windowHeight);
            (void)windowHeight;
            double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
            float panelLeft = static_cast<float>(screenWidth) - 220.0f;
            float panelRight = static_cast<float>(screenWidth);
            if (baseSystem.panel) {
                const PanelRect& rect = (baseSystem.panel->rightRenderRect.w > 0.0f)
                    ? baseSystem.panel->rightRenderRect
                    : baseSystem.panel->rightRect;
                if (rect.w > 0.0f) {
                    panelLeft = rect.x;
                    panelRight = rect.x + rect.w;
                }
            }
            float leftMargin = 32.0f;
            float spacing = 44.0f;
            float clearX = panelLeft + leftMargin;
            float inputX = clearX + spacing;
            float armX = inputX + spacing;
            float soloX = armX + spacing;
            float muteX = soloX + spacing;

            std::unordered_map<std::string, float> controlX;
            controlX.reserve(daw.trackInstances.size());
            float outputX = panelRight - 32.0f;

            for (auto* instPtr : daw.trackInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.actionKey == "clear") {
                    inst.position.x = clearX;
                } else if (inst.actionKey == "input") {
                    inst.position.x = inputX;
                } else if (inst.actionKey == "arm") {
                    inst.position.x = armX;
                } else if (inst.actionKey == "solo") {
                    inst.position.x = soloX;
                } else if (inst.actionKey == "mute") {
                    inst.position.x = muteX;
                } else if (inst.actionKey == "output"
                           || inst.actionKey.rfind("output", 0) == 0
                           || inst.controlId.find("_output") != std::string::npos) {
                    inst.position.x = outputX;
                }
                if (!inst.controlId.empty()) {
                    controlX[inst.controlId] = inst.position.x;
                }
            }
            for (auto* instPtr : daw.trackLabelInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.controlId.find("_output") != std::string::npos) continue;
                auto it = controlX.find(inst.controlId);
                if (it != controlX.end()) {
                    inst.position.x = it->second;
                }
            }
            for (auto* instPtr : daw.outputLabelInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.controlId.find("_output") == std::string::npos) continue;
                int busIndex = -1;
                if (inst.textType == "VariableUI") {
                    int trackIndex = -1;
                    if (!parseTrackIndexFromKey(inst.textKey, "daw_out_", true, getTrackCount(daw), trackIndex)) continue;
                    inst.position.x = outputX;
                    continue;
                }
                if (inst.textType != "UIOnly" || inst.text.size() != 1) continue;
                if (inst.text == "L") busIndex = 0;
                else if (inst.text == "S") busIndex = 1;
                else if (inst.text == "F") busIndex = 2;
                else if (inst.text == "R") busIndex = 3;
                if (busIndex >= 0 && busIndex < DawContext::kBusCount) {
                    inst.position.x = outputX;
                }
            }
        }

    void buildDawUiCache(BaseSystem& baseSystem, std::vector<Entity>& prototypes, DawContext& daw) {
        daw.trackInstances.clear();
        daw.trackLabelInstances.clear();
        daw.transportInstances.clear();
        daw.tempoInstances.clear();
        daw.loopInstances.clear();
        daw.outputLabelInstances.clear();
        daw.timelineLabelInstances.clear();
        daw.timelineBarLabelInstances.clear();
            if (!baseSystem.level) return;
            static bool g_debugPrinted = false;
            int dawTrackActionCount = 0;
            int trackRowWorldCount = 0;
            for (auto& world : baseSystem.level->worlds) {
                bool isTrackControls = isTrackRowWorld(world.name);
                if (isTrackControls) {
                    trackRowWorldCount += 1;
                }
                for (auto& inst : world.instances) {
                    if (inst.actionType == "DawTrack") {
                        daw.trackInstances.push_back(&inst);
                        dawTrackActionCount += 1;
                    } else if (inst.actionType == "DawTransport") {
                        daw.transportInstances.push_back(&inst);
                    } else if (inst.actionType == "DawTempo") {
                        daw.tempoInstances.push_back(&inst);
                    } else if (inst.actionType == "DawLoop") {
                        daw.loopInstances.push_back(&inst);
                    }
                    if (inst.controlRole == "timeline_label") {
                        daw.timelineLabelInstances.push_back(&inst);
                    }
                    if (inst.controlRole == "timeline_bar_label") {
                        daw.timelineBarLabelInstances.push_back(&inst);
                    }
                    if (isTrackControls) {
                        if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                        if (prototypes[inst.prototypeID].name != "Text") continue;
                        if (inst.textType == "VariableUI" && inst.textKey.rfind("daw_out_", 0) == 0) {
                            if (inst.controlId.find("_output") != std::string::npos) {
                                daw.outputLabelInstances.push_back(&inst);
                            }
                        } else if (inst.textType == "UIOnly" && inst.text.size() == 1) {
                            if ((inst.text == "L" || inst.text == "S" || inst.text == "F" || inst.text == "R")
                                && inst.controlId.find("_output") != std::string::npos) {
                                daw.outputLabelInstances.push_back(&inst);
                            }
                        }
                        if (inst.controlRole == "label") {
                            daw.trackLabelInstances.push_back(&inst);
                        }
                    }
                }
            }
            if (!g_debugPrinted) {
                g_debugPrinted = true;
                std::cerr << "[DawUiCache] trackRowWorlds=" << trackRowWorldCount
                          << " DawTrackActions=" << dawTrackActionCount
                          << " trackInstances=" << daw.trackInstances.size()
                          << " trackLabels=" << daw.trackLabelInstances.size()
                          << " outputLabels=" << daw.outputLabelInstances.size()
                          << std::endl;
                int dumpCount = 0;
                for (auto* instPtr : daw.trackInstances) {
                    if (!instPtr) continue;
                    const EntityInstance& inst = *instPtr;
                    std::string protoName = "<invalid>";
                    bool isUIButton = false;
                    if (inst.prototypeID >= 0 && inst.prototypeID < static_cast<int>(prototypes.size())) {
                        protoName = prototypes[inst.prototypeID].name;
                        isUIButton = prototypes[inst.prototypeID].isUIButton;
                    }
                    std::cerr << "  [DawUiCache] inst name='" << inst.name
                              << "' action='" << inst.actionType
                              << "' key='" << inst.actionKey
                              << "' controlId='" << inst.controlId
                              << "' proto=" << inst.prototypeID
                              << " protoName='" << protoName
                              << "' isUIButton=" << (isUIButton ? "true" : "false")
                              << std::endl;
                    if (++dumpCount >= 3) break;
                }
            }
            daw.uiCacheBuilt = true;
            daw.uiLevel = baseSystem.level.get();
        }

        void updateInputLabels(BaseSystem& baseSystem, DawContext& daw, AudioContext& audio) {
            if (!baseSystem.font) return;
            FontContext& fontCtx = *baseSystem.font;
            int physicalCount = static_cast<int>(audio.physicalInputPorts.size());
            int totalInputs = physicalCount + 1;
            int trackCount = getTrackCount(daw);
            for (int i = 0; i < trackCount; ++i) {
                int idx = daw.tracks[static_cast<size_t>(i)].physicalInputIndex;
                std::string key = "daw_in_" + std::to_string(i + 1);
                if (idx < 0 || idx >= totalInputs) {
                    idx = 0;
                    daw.tracks[static_cast<size_t>(i)].physicalInputIndex = 0;
                }
                if (idx < physicalCount) {
                    fontCtx.variables[key] = "IN" + std::to_string(idx + 1);
                } else {
                    fontCtx.variables[key] = "VM1";
                }
                daw.tracks[static_cast<size_t>(i)].useVirtualInput.store(idx >= physicalCount, std::memory_order_relaxed);
                std::string outKey = "daw_out_" + std::to_string(i + 1);
                int busIndex = daw.tracks[static_cast<size_t>(i)].outputBus.load(std::memory_order_relaxed);
                std::string outLabel = busLabelForIndex(busIndex);
                if (outLabel.empty()) outLabel = "?";
                fontCtx.variables[outKey] = outLabel;
            }
        }

        void updateTimelineLabels(BaseSystem& baseSystem, DawContext& daw, GLFWwindow* win) {
            if (!baseSystem.font || !win) return;
            if (daw.timelineLabelInstances.empty()) return;
            FontContext& fontCtx = *baseSystem.font;

            clampTimelineOffset(daw);

            int windowWidth = 0, windowHeight = 0;
            glfwGetWindowSize(win, &windowWidth, &windowHeight);
            double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
            double screenHeight = windowHeight > 0 ? static_cast<double>(windowHeight) : 1080.0;

            const float laneHeight = 60.0f;
            const float laneHalfH = laneHeight * 0.5f;
            float laneLeft = kLaneLeftMargin;
            float laneRight = static_cast<float>(screenWidth) - kLaneRightMargin;
            if (laneRight < laneLeft + 200.0f) {
                laneRight = laneLeft + 200.0f;
            }
            float scrollY = 0.0f;
            if (baseSystem.uiStamp) {
                scrollY = baseSystem.uiStamp->scrollY;
            }
            float startY = 100.0f + scrollY;
            float labelY = startY - laneHalfH - 18.0f;
            int laneCount = static_cast<int>(daw.laneOrder.size());
            if (laneCount == 0) {
                laneCount = getTrackCount(daw) + getMidiTrackCount(baseSystem);
            }
            float rowSpan = laneHeight + 12.0f;
            float laneBottomBound = (laneCount > 0)
                ? (startY + (laneCount - 1) * rowSpan + laneHalfH)
                : (startY - laneHalfH + 1.0f);
            float visualBottomBound = std::max(laneBottomBound, static_cast<float>(screenHeight) - 40.0f);
            float barLabelY = std::min(visualBottomBound + 12.0f, static_cast<float>(screenHeight) - 6.0f);

            double secondsPerScreen = (daw.timelineSecondsPerScreen > 0.0) ? daw.timelineSecondsPerScreen : 10.0;
            if (secondsPerScreen <= 0.0) secondsPerScreen = 10.0;
            double offsetSec = (daw.sampleRate > 0.0)
                ? static_cast<double>(daw.timelineOffsetSamples) / static_cast<double>(daw.sampleRate)
                : 0.0;
            double gridSeconds = kTimelineScrollSeconds;
            double firstTick = std::floor(offsetSec / gridSeconds) * gridSeconds;
            double endSec = offsetSec + secondsPerScreen;

            for (size_t i = 0; i < daw.timelineLabelInstances.size(); ++i) {
                EntityInstance* inst = daw.timelineLabelInstances[i];
                if (!inst) continue;
                double tick = firstTick + static_cast<double>(i) * gridSeconds;
                if (tick < offsetSec - 0.001 || tick > endSec + 0.001) {
                    if (!inst->textKey.empty()) {
                        fontCtx.variables[inst->textKey] = "";
                    }
                    continue;
                }
                float t = static_cast<float>((tick - offsetSec) / secondsPerScreen);
                float x = laneLeft + (laneRight - laneLeft) * t;
                inst->position.x = x;
                inst->position.y = labelY;
                inst->position.z = -1.0f;
                int seconds = static_cast<int>(std::round(tick));
                if (!inst->textKey.empty()) {
                    fontCtx.variables[inst->textKey] = std::to_string(seconds);
                }
            }

            if (!daw.timelineBarLabelInstances.empty()) {
                double bpm = daw.bpm.load(std::memory_order_relaxed);
                if (bpm <= 0.0) bpm = 120.0;
                double secondsPerBeat = 60.0 / bpm;
                if (secondsPerBeat <= 0.0) secondsPerBeat = 0.5;
                double gridSeconds = secondsPerBeat;
                if (secondsPerScreen > 64.0) {
                    gridSeconds = secondsPerBeat * 4.0;
                } else if (secondsPerScreen > 32.0) {
                    gridSeconds = secondsPerBeat * 2.0;
                } else if (secondsPerScreen > 16.0) {
                    gridSeconds = secondsPerBeat;
                } else if (secondsPerScreen > 8.0) {
                    gridSeconds = secondsPerBeat * 0.5;
                } else if (secondsPerScreen > 4.0) {
                    gridSeconds = secondsPerBeat * 0.25;
                } else {
                    gridSeconds = secondsPerBeat * 0.125;
                }
                double labelStep = gridSeconds;
                double firstLabel = std::floor(offsetSec / labelStep) * labelStep;
                int subPerBeat = static_cast<int>(std::round(secondsPerBeat / gridSeconds));
                if (subPerBeat < 1) subPerBeat = 1;

                for (size_t i = 0; i < daw.timelineBarLabelInstances.size(); ++i) {
                    EntityInstance* inst = daw.timelineBarLabelInstances[i];
                    if (!inst) continue;
                    double tick = firstLabel + static_cast<double>(i) * labelStep;
                    if (tick < offsetSec - 0.001 || tick > endSec + 0.001) {
                        if (!inst->textKey.empty()) {
                            fontCtx.variables[inst->textKey] = "";
                        }
                        continue;
                    }
                    float t = static_cast<float>((tick - offsetSec) / secondsPerScreen);
                    float x = laneLeft + (laneRight - laneLeft) * t;
                    inst->position.x = x;
                    inst->position.y = barLabelY;
                    inst->position.z = -1.0f;
                    double beatStart = std::floor(tick / secondsPerBeat) * secondsPerBeat;
                    int beatIndex = static_cast<int>(std::floor(tick / secondsPerBeat)) + 1;
                    int barIndex = (beatIndex - 1) / 4 + 1;
                    int beatInBar = (beatIndex - 1) % 4 + 1;
                    int subIndex = static_cast<int>(std::floor((tick - beatStart) / gridSeconds)) + 1;
                    if (subIndex < 1) subIndex = 1;
                    if (subIndex > subPerBeat) subIndex = subPerBeat;
                    if (!inst->textKey.empty()) {
                        if (gridSeconds >= secondsPerBeat) {
                            fontCtx.variables[inst->textKey] = std::to_string(barIndex) + "." + std::to_string(beatInBar);
                        } else {
                            fontCtx.variables[inst->textKey] = std::to_string(barIndex) + "." + std::to_string(beatInBar)
                                + "." + std::to_string(subIndex);
                        }
                    }
                }
            }
        }

        void updateBpmLabel(BaseSystem& baseSystem, DawContext& daw) {
            if (!baseSystem.font) return;
            FontContext& fontCtx = *baseSystem.font;
            double bpm = daw.bpm.load(std::memory_order_relaxed);
            int bpmInt = static_cast<int>(std::round(bpm));
            fontCtx.variables["daw_bpm"] = std::to_string(bpmInt) + " BPM";
        }

        void reconnectTrackInput(AudioContext& audio, DawTrack& track) {
            if (!audio.client) return;
            if (track.inputIndex < 0 || track.inputIndex >= static_cast<int>(audio.input_ports.size())) return;
            jack_port_t* inputPort = audio.input_ports[track.inputIndex];
            if (!inputPort) return;
            std::string inputName = jack_port_name(inputPort);
            for (const auto& physical : audio.physicalInputPorts) {
                int rc = jack_disconnect(audio.client, physical.c_str(), inputName.c_str());
                if (rc != 0 && rc != EEXIST && rc != ENOENT) {
                    std::cerr << "JACK disconnect failed: " << physical << " -> " << inputName << " (" << rc << ")\n";
                }
            }
            int physicalCount = static_cast<int>(audio.physicalInputPorts.size());
            bool useVirtual = (track.physicalInputIndex < 0 || track.physicalInputIndex >= physicalCount);
            track.useVirtualInput.store(useVirtual, std::memory_order_relaxed);
            if (!useVirtual) {
                const std::string& src = audio.physicalInputPorts[track.physicalInputIndex];
                int rc = jack_connect(audio.client, src.c_str(), inputName.c_str());
                if (rc != 0 && rc != EEXIST) {
                    std::cerr << "JACK connect failed: " << src << " -> " << inputName << " (" << rc << ")\n";
                }
            }
        }

        void refreshPhysicalInputs(AudioContext& audio) {
            if (!audio.client) return;
            audio.physicalInputPorts.clear();
            if (const char** capturePorts = jack_get_ports(audio.client, nullptr, JACK_DEFAULT_AUDIO_TYPE,
                                                           JackPortIsOutput | JackPortIsPhysical)) {
                for (size_t i = 0; capturePorts[i]; ++i) {
                    audio.physicalInputPorts.emplace_back(capturePorts[i]);
                }
                jack_free(capturePorts);
            }
        }
    }

    void UpdateDawState(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt;
        if (!baseSystem.daw || !baseSystem.audio || !baseSystem.ui) return;
        DawContext& daw = *baseSystem.daw;
        AudioContext& audio = *baseSystem.audio;
        UIContext& ui = *baseSystem.ui;

        if (!daw.initialized && audio.client) {
            initializeDaw(daw, audio);
        }
        if (!daw.initialized) return;
        daw.trackCount = getTrackCount(daw);
        int midiCount = getMidiTrackCount(baseSystem);
        ensureLaneOrder(daw, daw.trackCount, midiCount);
        if (daw.selectedLaneIndex >= 0 && daw.selectedLaneIndex < static_cast<int>(daw.laneOrder.size())) {
            const auto& entry = daw.laneOrder[static_cast<size_t>(daw.selectedLaneIndex)];
            daw.selectedLaneType = entry.type;
            daw.selectedLaneTrack = entry.trackIndex;
        } else {
            daw.selectedLaneType = -1;
            daw.selectedLaneTrack = -1;
        }

        drainRecordRings(daw);

        if (ui.active && ui.actionDelayFrames == 0 && !ui.pendingActionType.empty()) {
            if (ui.pendingActionType == "DawTransport") {
                const std::string action = ui.pendingActionKey;
                if (action == "play") {
                    if (!daw.transportRecording.load(std::memory_order_relaxed)) {
                        daw.transportPlaying.store(true, std::memory_order_relaxed);
                        daw.transportRecording.store(false, std::memory_order_relaxed);
                        daw.transportLatch = kTransportPlay;
                    }
                } else if (action == "stop") {
                    if (daw.transportLatch != kTransportStop) {
                        if (daw.transportRecording.load(std::memory_order_relaxed)) {
                            daw.recordStopPending = true;
                            daw.transportRecording.store(false, std::memory_order_relaxed);
                            stopRecording(daw);
                        }
                        daw.transportPlaying.store(false, std::memory_order_relaxed);
                        daw.transportLatch = kTransportStop;
                    }
                } else if (action == "record") {
                    if (!daw.transportRecording.load(std::memory_order_relaxed)) {
                        daw.recordStopPending = false;
                        daw.transportRecording.store(true, std::memory_order_relaxed);
                        daw.transportPlaying.store(true, std::memory_order_relaxed);
                        startRecording(daw, daw.playheadSample.load(std::memory_order_relaxed));
                        daw.transportLatch = kTransportRecord;
                    }
                } else if (action == "rewind") {
                    if (daw.transportRecording.load(std::memory_order_relaxed)) {
                        daw.recordStopPending = true;
                        daw.transportRecording.store(false, std::memory_order_relaxed);
                        stopRecording(daw);
                    }
                    daw.transportPlaying.store(false, std::memory_order_relaxed);
                    daw.playheadSample.store(0, std::memory_order_relaxed);
                    daw.transportLatch = kTransportNone;
                }
                ui.pendingActionType.clear();
                ui.pendingActionKey.clear();
                ui.pendingActionValue.clear();
            } else if (ui.pendingActionType == "DawTimeline") {
                const std::string key = ui.pendingActionKey;
                if (key == "scroll") {
                    double deltaSec = 0.0;
                    if (!ui.pendingActionValue.empty()) {
                        try {
                            deltaSec = std::stod(ui.pendingActionValue);
                        } catch (...) {
                            deltaSec = 0.0;
                        }
                    }
                    if (std::abs(deltaSec) < 0.0001) {
                        deltaSec = kTimelineScrollSeconds;
                    }
                    int64_t deltaSamples = static_cast<int64_t>(deltaSec * daw.sampleRate);
                    daw.timelineOffsetSamples += deltaSamples;
                    clampTimelineOffset(daw);
                }
                ui.pendingActionType.clear();
                ui.pendingActionKey.clear();
                ui.pendingActionValue.clear();
            } else if (ui.pendingActionType == "DawTrack") {
                const std::string key = ui.pendingActionKey;
                int trackCount = getTrackCount(daw);
                if (key == "arm") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        int current = daw.tracks[trackIndex].armMode.load(std::memory_order_relaxed);
                        int next = (current + 1) % 3;
                        daw.tracks[trackIndex].armMode.store(next, std::memory_order_relaxed);
                    }
                } else if (key == "solo") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        bool newSolo = !daw.tracks[trackIndex].solo.load(std::memory_order_relaxed);
                        daw.tracks[trackIndex].solo.store(newSolo, std::memory_order_relaxed);
                        if (newSolo) {
                            daw.tracks[trackIndex].mute.store(false, std::memory_order_relaxed);
                        }
                    }
                } else if (key == "mute") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        bool newMute = !daw.tracks[trackIndex].mute.load(std::memory_order_relaxed);
                        daw.tracks[trackIndex].mute.store(newMute, std::memory_order_relaxed);
                        if (newMute) {
                            daw.tracks[trackIndex].solo.store(false, std::memory_order_relaxed);
                        }
                    }
                } else if (key == "output") {
                    int trackIndex = -1;
                    int busIndex = -1;
                    if (parseTrackAndBus(ui.pendingActionValue, trackCount, trackIndex, busIndex)) {
                        daw.tracks[trackIndex].outputBus.store(busIndex, std::memory_order_relaxed);
                    } else {
                        trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                        if (trackIndex >= 0) {
                            int current = daw.tracks[trackIndex].outputBus.load(std::memory_order_relaxed);
                            int next = (current + 1) % DawContext::kBusCount;
                            daw.tracks[trackIndex].outputBus.store(next, std::memory_order_relaxed);
                        }
                    }
                } else if (key == "input") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        refreshPhysicalInputs(audio);
                        int physicalCount = static_cast<int>(audio.physicalInputPorts.size());
                        int totalInputs = physicalCount + 1;
                        if (totalInputs > 0) {
                            int next = daw.tracks[trackIndex].physicalInputIndex + 1;
                            next %= totalInputs;
                            daw.tracks[trackIndex].physicalInputIndex = next;
                            daw.tracks[trackIndex].useVirtualInput.store(next >= physicalCount, std::memory_order_relaxed);
                            reconnectTrackInput(audio, daw.tracks[trackIndex]);
                        }
                    }
                } else if (key == "clear") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        daw.tracks[trackIndex].clearPending = true;
                    }
                } else if (key == "add") {
                    if (!daw.transportPlaying.load(std::memory_order_relaxed)
                        && !daw.transportRecording.load(std::memory_order_relaxed)) {
                        addTrack(baseSystem, daw, audio);
                    }
                } else if (key == "remove") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex < 0 && trackCount > 0) {
                        trackIndex = trackCount - 1;
                    }
                    if (trackIndex >= 0
                        && !daw.transportPlaying.load(std::memory_order_relaxed)
                        && !daw.transportRecording.load(std::memory_order_relaxed)) {
                        removeTrackAt(baseSystem, daw, audio, trackIndex);
                    }
                }
                ui.pendingActionType.clear();
                ui.pendingActionKey.clear();
                ui.pendingActionValue.clear();
            } else if (ui.pendingActionType == "DawTempo") {
                const std::string key = ui.pendingActionKey;
                double bpm = daw.bpm.load(std::memory_order_relaxed);
                if (key == "bpm_up") {
                    bpm += 1.0;
                } else if (key == "bpm_down") {
                    bpm -= 1.0;
                } else if (key == "metronome") {
                    bool enabled = daw.metronomeEnabled.load(std::memory_order_relaxed);
                    daw.metronomeEnabled.store(!enabled, std::memory_order_relaxed);
                }
                bpm = std::clamp(bpm, 40.0, 240.0);
                daw.bpm.store(bpm, std::memory_order_relaxed);
                ui.pendingActionType.clear();
                ui.pendingActionKey.clear();
                ui.pendingActionValue.clear();
            } else if (ui.pendingActionType == "DawLoop") {
                if (ui.pendingActionKey == "toggle") {
                    bool enabled = daw.loopEnabled.load(std::memory_order_relaxed);
                    daw.loopEnabled.store(!enabled, std::memory_order_relaxed);
                }
                ui.pendingActionType.clear();
                ui.pendingActionKey.clear();
                ui.pendingActionValue.clear();
            }
        }

        if (daw.recordStopPending) {
            if (!daw.transportPlaying.load(std::memory_order_relaxed) &&
                daw.audioThreadIdle.load(std::memory_order_relaxed) && !hasRingData(daw)) {
                mergePendingRecords(daw);
                writeTracksIfNeeded(daw);
                daw.recordStopPending = false;
            }
        }

        if (!daw.transportPlaying.load(std::memory_order_relaxed)) {
            for (int i = 0; i < getTrackCount(daw); ++i) {
                auto& track = daw.tracks[static_cast<size_t>(i)];
                if (!track.clearPending) continue;
                track.audio.clear();
                track.pendingRecord.clear();
                track.clips.clear();
                track.waveformMin.clear();
                track.waveformMax.clear();
                track.waveformColor.clear();
                track.waveformVersion += 1;
                if (track.recordRing) {
                    jack_ringbuffer_reset(track.recordRing);
                }
                if (daw.mirrorAvailable) {
                    std::filesystem::path outPath = std::filesystem::path(daw.mirrorPath) / ("track_" + std::to_string(i + 1) + ".wav");
                    writeWavMonoFloat(outPath.string(), track.audio, static_cast<uint32_t>(daw.sampleRate));
                }
                track.clearPending = false;
            }
        }

        if (baseSystem.rayTracedAudio) {
            bool micRequested = false;
            if (daw.transportRecording.load(std::memory_order_relaxed)) {
                for (const auto& track : daw.tracks) {
                    if (!track.recordEnabled.load(std::memory_order_relaxed)) continue;
                    if (track.useVirtualInput.load(std::memory_order_relaxed)) {
                        micRequested = true;
                        break;
                    }
                }
            }
            baseSystem.rayTracedAudio->micCaptureActive = micRequested;
        }

        if (ui.active && win) {
            static bool deletePressedLast = false;
            bool deletePressed = glfwGetKey(win, GLFW_KEY_DELETE) == GLFW_PRESS
                || glfwGetKey(win, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
            if (deletePressed && !deletePressedLast) {
                if (daw.selectedLaneType == 1 && baseSystem.midi && daw.selectedLaneTrack >= 0) {
                    int idx = daw.selectedLaneTrack;
                    if (MidiStateSystemLogic::RemoveTrackAt(baseSystem, idx)) {
                        daw.dragActive = false;
                        daw.dragPending = false;
                        daw.dragLaneIndex = -1;
                        daw.dragLaneType = -1;
                        daw.dragLaneTrack = -1;
                        daw.dragDropIndex = -1;
                    }
                } else if (daw.selectedLaneType == 0 && daw.selectedLaneTrack >= 0) {
                    int idx = daw.selectedLaneTrack;
                    if (RemoveTrackAt(baseSystem, idx)) {
                        daw.dragActive = false;
                        daw.dragPending = false;
                        daw.dragLaneIndex = -1;
                        daw.dragLaneType = -1;
                        daw.dragLaneTrack = -1;
                        daw.dragDropIndex = -1;
                    }
                }
            }
            deletePressedLast = deletePressed;
        }

        if (ui.active) {
            if (!daw.uiCacheBuilt || daw.uiLevel != baseSystem.level.get()) {
                buildDawUiCache(baseSystem, prototypes, daw);
            }
            clampTimelineOffset(daw);
            {
                double secondsPerScreen = (daw.timelineSecondsPerScreen > 0.0) ? daw.timelineSecondsPerScreen : 10.0;
                if (secondsPerScreen <= 0.0) secondsPerScreen = 10.0;
                int desiredTime = static_cast<int>(std::ceil(secondsPerScreen / kTimelineScrollSeconds)) + 2;
                desiredTime = std::clamp(desiredTime, 8, 64);
                double bpm = daw.bpm.load(std::memory_order_relaxed);
                if (bpm <= 0.0) bpm = 120.0;
                double secondsPerBeat = 60.0 / bpm;
                if (secondsPerBeat <= 0.0) secondsPerBeat = 0.5;
                int desiredBar = static_cast<int>(std::ceil(secondsPerScreen / secondsPerBeat)) + 2;
                desiredBar = std::clamp(desiredBar, 8, 256);
                ensureTimelineLabelCapacity(baseSystem, daw, desiredTime, desiredBar);
                if (!daw.uiCacheBuilt || daw.uiLevel != baseSystem.level.get()) {
                    buildDawUiCache(baseSystem, prototypes, daw);
                }
            }
            updateDawUILayout(baseSystem, daw, win);
            updateTransportButtonVisuals(baseSystem, daw);
            updateTempoButtonVisuals(baseSystem, daw);
            updateLoopButtonVisuals(baseSystem, daw);
            updateTrackButtonVisuals(baseSystem, daw);
            updateInputLabels(baseSystem, daw, audio);
            updateTimelineLabels(baseSystem, daw, win);
            updateBpmLabel(baseSystem, daw);
        }
    }

    void TrimClipsForNewClip(DawTrack& track, const DawClip& clip) {
        trimClipsForNewClip(track, clip);
    }

    void RebuildTrackCacheFromClips(DawContext& daw, DawTrack& track) {
        rebuildTrackCacheFromClips(daw, track);
    }

    void CleanupDawState(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt; (void)win;
        if (!baseSystem.daw) return;
        DawContext& daw = *baseSystem.daw;
        for (auto& track : daw.tracks) {
            if (track.recordRing) {
                jack_ringbuffer_free(track.recordRing);
                track.recordRing = nullptr;
            }
        }
        if (baseSystem.audio) {
            baseSystem.audio->daw = nullptr;
        }
        daw.initialized = false;
    }

    bool InsertTrackAt(BaseSystem& baseSystem, int trackIndex) {
        if (!baseSystem.daw || !baseSystem.audio) return false;
        return insertTrackAt(baseSystem, *baseSystem.daw, *baseSystem.audio, trackIndex);
    }

    bool RemoveTrackAt(BaseSystem& baseSystem, int trackIndex) {
        if (!baseSystem.daw || !baseSystem.audio) return false;
        return removeTrackAt(baseSystem, *baseSystem.daw, *baseSystem.audio, trackIndex);
    }

    bool MoveTrack(BaseSystem& baseSystem, int fromIndex, int toIndex) {
        if (!baseSystem.daw || !baseSystem.audio) return false;
        return moveTrack(baseSystem, *baseSystem.daw, *baseSystem.audio, fromIndex, toIndex);
    }
}
