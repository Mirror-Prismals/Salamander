#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace ButtonSystemLogic {
    void SetButtonToggled(int instanceID, bool toggled);
}

namespace MidiStateSystemLogic {
    namespace {
        constexpr int kRecordRingSeconds = 2;
        constexpr size_t kWaveformBlockSize = 256;
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

        void refreshPhysicalMidiInputs(AudioContext& audio);
        void reconnectMidiInput(AudioContext& audio, const MidiTrack& track);

        int getTrackCount(const MidiContext& midi) {
            return static_cast<int>(midi.tracks.size());
        }

        void replaceAll(std::string& value, const std::string& token, const std::string& replacement) {
            if (value.empty() || token.empty()) return;
            size_t pos = 0;
            while ((pos = value.find(token, pos)) != std::string::npos) {
                value.replace(pos, token.size(), replacement);
                pos += replacement.size();
            }
        }

        std::string replaceTrackTokens(const std::string& value, int trackIndex) {
            std::string result = value;
            replaceAll(result, "{track+1}", std::to_string(trackIndex + 1));
            replaceAll(result, "{track}", std::to_string(trackIndex));
            return result;
        }

        void applyTokens(EntityInstance& target, const EntityInstance& source, int trackIndex) {
            target.actionType = replaceTrackTokens(source.actionType, trackIndex);
            target.actionKey = replaceTrackTokens(source.actionKey, trackIndex);
            target.actionValue = replaceTrackTokens(source.actionValue, trackIndex);
            target.textKey = replaceTrackTokens(source.textKey, trackIndex);
            target.text = replaceTrackTokens(source.text, trackIndex);
            target.controlId = replaceTrackTokens(source.controlId, trackIndex);
            target.controlRole = replaceTrackTokens(source.controlRole, trackIndex);
            target.styleId = replaceTrackTokens(source.styleId, trackIndex);
            target.uiState = replaceTrackTokens(source.uiState, trackIndex);
        }

        int findWorldIndex(const LevelContext& level, const std::string& name) {
            for (size_t i = 0; i < level.worlds.size(); ++i) {
                if (level.worlds[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        void stampRow(Entity& world,
                      const MidiContext& midi,
                      int row,
                      bool preserveIds,
                      BaseSystem& baseSystem) {
            world.instances.resize(midi.stampSourceInstances.size());
            for (size_t i = 0; i < midi.stampSourceInstances.size(); ++i) {
                EntityInstance inst = midi.stampSourceInstances[i];
                if (preserveIds && i < world.instances.size()) {
                    inst.instanceID = world.instances[i].instanceID;
                    if (inst.instanceID <= 0 && baseSystem.instance) {
                        inst.instanceID = baseSystem.instance->nextInstanceID++;
                    }
                } else if (baseSystem.instance) {
                    inst.instanceID = baseSystem.instance->nextInstanceID++;
                }
                inst.position.y = midi.stampSourceBaseY[i] + static_cast<float>(row) * midi.stampRowSpacing;
                applyTokens(inst, midi.stampSourceInstances[i], row);
                world.instances[i] = std::move(inst);
            }
        }

        void updateMidiStamping(BaseSystem& baseSystem, MidiContext& midi, const DawContext& daw) {
            if (!baseSystem.level) return;
            if (!daw.laneOrder.empty()) {
                LevelContext& level = *baseSystem.level;
                for (int worldIndex : midi.stampRowWorldIndices) {
                    if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) continue;
                    level.worlds[worldIndex].instances.clear();
                }
                midi.stampRowWorldIndices.clear();
                midi.stampRowCount = 0;
                midi.stampCacheBuilt = false;
                midi.stampSourceWorldIndex = -1;
                return;
            }
            LevelContext& level = *baseSystem.level;

            if (!midi.stampCacheBuilt || midi.uiLevel != baseSystem.level.get()) {
                midi.stampSourceWorldIndex = findWorldIndex(level, midi.stampSourceWorldName);
                midi.stampSourceInstances.clear();
                midi.stampSourceBaseY.clear();
                midi.stampRowWorldIndices.clear();
                midi.stampRowCount = 0;
                if (midi.stampSourceWorldIndex >= 0) {
                    Entity& sourceWorld = level.worlds[midi.stampSourceWorldIndex];
                    midi.stampSourceInstances = sourceWorld.instances;
                    midi.stampSourceBaseY.reserve(sourceWorld.instances.size());
                    for (const auto& inst : sourceWorld.instances) {
                        midi.stampSourceBaseY.push_back(inst.position.y);
                    }
                    midi.stampRowWorldIndices.push_back(midi.stampSourceWorldIndex);
                    midi.stampRowCount = 1;
                }
            midi.stampRowSpacing = baseSystem.uiStamp ? baseSystem.uiStamp->rowSpacing : 72.0f;
            midi.stampCacheBuilt = true;
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            midi.uiCacheBuilt = false;
        }

            int desiredRows = getTrackCount(midi);
            if (midi.stampSourceWorldIndex >= 0 && midi.stampSourceWorldIndex < static_cast<int>(level.worlds.size())) {
                Entity& sourceWorld = level.worlds[midi.stampSourceWorldIndex];
                if (desiredRows > 0) {
                    stampRow(sourceWorld, midi, 0, true, baseSystem);
                } else {
                    sourceWorld.instances.clear();
                }
            }

            if (desiredRows > static_cast<int>(midi.stampRowWorldIndices.size())) {
                if (midi.stampSourceWorldIndex >= 0 && midi.stampSourceWorldIndex < static_cast<int>(level.worlds.size())) {
                    Entity& sourceWorld = level.worlds[midi.stampSourceWorldIndex];
                    for (int row = static_cast<int>(midi.stampRowWorldIndices.size()); row < desiredRows; ++row) {
                        Entity newWorld = sourceWorld;
                        newWorld.name = midi.stampSourceWorldName + "_" + std::to_string(row);
                        stampRow(newWorld, midi, row, false, baseSystem);
                        level.worlds.push_back(std::move(newWorld));
                        midi.stampRowWorldIndices.push_back(static_cast<int>(level.worlds.size() - 1));
                    }
                    midi.stampRowCount = static_cast<int>(midi.stampRowWorldIndices.size());
                }
            } else if (desiredRows < static_cast<int>(midi.stampRowWorldIndices.size())) {
                for (int row = desiredRows; row < static_cast<int>(midi.stampRowWorldIndices.size()); ++row) {
                    int worldIndex = midi.stampRowWorldIndices[row];
                    if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) continue;
                    level.worlds[worldIndex].instances.clear();
                }
            }

            float scrollY = baseSystem.uiStamp ? baseSystem.uiStamp->panelScrollY : 0.0f;
            float rowSpacing = baseSystem.uiStamp ? baseSystem.uiStamp->rowSpacing : 72.0f;
            int audioRowCount = baseSystem.uiStamp
                ? baseSystem.uiStamp->stampedRows
                : static_cast<int>(daw.tracks.size());
            float baseOffset = static_cast<float>(audioRowCount) * rowSpacing + scrollY;
            for (int row = 0; row < desiredRows; ++row) {
                if (row < 0 || row >= static_cast<int>(midi.stampRowWorldIndices.size())) continue;
                int worldIndex = midi.stampRowWorldIndices[row];
                if (worldIndex < 0 || worldIndex >= static_cast<int>(level.worlds.size())) continue;
                Entity& world = level.worlds[worldIndex];
                size_t count = std::min(world.instances.size(), midi.stampSourceBaseY.size());
                float rowOffset = baseOffset + static_cast<float>(row) * rowSpacing;
                for (size_t i = 0; i < count; ++i) {
                    world.instances[i].position.y = midi.stampSourceBaseY[i] + rowOffset;
                }
            }
        }

        void initTrack(MidiTrack& track, int index, MidiContext& midi, AudioContext& audio) {
            track.outputBus.store(2, std::memory_order_relaxed);
            track.physicalInputIndex = 0;
            if (!track.recordRing) {
                size_t ringBytes = static_cast<size_t>(kRecordRingSeconds * midi.sampleRate * sizeof(float));
                track.recordRing = jack_ringbuffer_create(ringBytes);
                if (track.recordRing) {
                    jack_ringbuffer_mlock(track.recordRing);
                }
            }
            if (index == 0) {
                refreshPhysicalMidiInputs(audio);
                reconnectMidiInput(audio, track);
            }
        }

        void cleanupTrack(MidiTrack& track) {
            if (track.recordRing) {
                jack_ringbuffer_free(track.recordRing);
                track.recordRing = nullptr;
            }
        }

        int detectMirrorTrackCount(const DawContext& daw) {
            if (!daw.mirrorAvailable) return 0;
            std::error_code ec;
            int maxIndex = 0;
            for (const auto& entry : std::filesystem::directory_iterator(daw.mirrorPath, ec)) {
                if (ec || !entry.is_regular_file()) continue;
                const std::string name = entry.path().filename().string();
                if (name.rfind("midi_track_", 0) != 0) continue;
                if (name.size() <= 14 || name.substr(name.size() - 4) != ".wav") continue;
                std::string num = name.substr(11, name.size() - 15);
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
                / ("midi_track_" + std::to_string(oneBasedIndex) + ".wav");
            std::error_code ec;
            std::filesystem::remove(outPath, ec);
        }

        void ensureTrackCount(BaseSystem& baseSystem, MidiContext& midi, AudioContext& audio, const DawContext& daw, int desired) {
            if (desired < 0) desired = 0;
            std::lock_guard<std::mutex> lock(midi.trackMutex);
            int current = getTrackCount(midi);
            if (desired > current) {
                midi.tracks.resize(static_cast<size_t>(desired));
                for (int i = current; i < desired; ++i) {
                    initTrack(midi.tracks[static_cast<size_t>(i)], i, midi, audio);
                }
            } else if (desired < current) {
                int oldCount = current;
                for (int i = current - 1; i >= desired; --i) {
                    cleanupTrack(midi.tracks[static_cast<size_t>(i)]);
                }
                midi.tracks.erase(midi.tracks.begin() + desired, midi.tracks.end());
                deleteStaleTrackFile(daw, oldCount);
            }
            midi.trackCount = getTrackCount(midi);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            midi.uiCacheBuilt = false;
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

        int addTrackInternal(BaseSystem& baseSystem, MidiContext& midi, AudioContext& audio) {
            std::lock_guard<std::mutex> lock(midi.trackMutex);
            int index = getTrackCount(midi);
            midi.tracks.emplace_back();
            initTrack(midi.tracks.back(), index, midi, audio);
            midi.trackCount = getTrackCount(midi);
            return index;
        }

        bool addTrack(BaseSystem& baseSystem, MidiContext& midi, AudioContext& audio, DawContext& daw) {
            int index = addTrackInternal(baseSystem, midi, audio);
            insertLaneEntry(daw, static_cast<int>(daw.laneOrder.size()), 1, index);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            midi.uiCacheBuilt = false;
            return true;
        }

        bool insertTrackAt(BaseSystem& baseSystem, MidiContext& midi, AudioContext& audio, DawContext& daw, int trackIndex) {
            int laneIndex = trackIndex;
            int newIndex = addTrackInternal(baseSystem, midi, audio);
            insertLaneEntry(daw, laneIndex, 1, newIndex);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            midi.uiCacheBuilt = false;
            return true;
        }

        bool removeTrackAt(BaseSystem& baseSystem, MidiContext& midi, AudioContext& audio, DawContext& daw, int trackIndex) {
            int current = getTrackCount(midi);
            if (trackIndex < 0 || trackIndex >= current) return false;
            std::lock_guard<std::mutex> lock(midi.trackMutex);
            int oldCount = current;
            cleanupTrack(midi.tracks[static_cast<size_t>(trackIndex)]);
            midi.tracks.erase(midi.tracks.begin() + trackIndex);
            midi.trackCount = getTrackCount(midi);
            deleteStaleTrackFile(daw, oldCount);
            removeLaneEntryForTrack(daw, 1, trackIndex);
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            midi.uiCacheBuilt = false;
            return true;
        }

        bool moveTrack(BaseSystem& baseSystem, MidiContext& midi, AudioContext& audio, DawContext& daw, int fromIndex, int toIndex) {
            (void)midi; (void)audio;
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
            if (baseSystem.daw) baseSystem.daw->uiCacheBuilt = false;
            return true;
        }

        bool isMidiTrackRowWorld(const std::string& name) {
            if (name == "MidiTrackRowWorld") return true;
            if (name.rfind("MidiTrackRowWorld_", 0) == 0) return true;
            return false;
        }

        void rebuildWaveform(MidiTrack& track, float sampleRate) {
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
            std::vector<Complex> fftBuffer;
            std::vector<float> fftSamples;
            for (size_t block = 0; block < blockCount; ++block) {
                size_t start = block * kWaveformBlockSize;
                size_t end = std::min(start + kWaveformBlockSize, track.audio.size());
                float minVal = 0.0f;
                float maxVal = 0.0f;
                for (size_t i = start; i < end; ++i) {
                    float v = track.audio[i];
                    if (i == start) {
                        minVal = v;
                        maxVal = v;
                    } else {
                        minVal = std::min(minVal, v);
                        maxVal = std::max(maxVal, v);
                    }
                }
                track.waveformMin[block] = minVal;
                track.waveformMax[block] = maxVal;
                if (end <= start) {
                    track.waveformColor[block] = kWaveformFallbackColor;
                } else {
                    fftSamples.resize(kWaveformBlockSize);
                    for (size_t i = 0; i < kWaveformBlockSize; ++i) {
                        if (start + i < end) {
                            fftSamples[i] = track.audio[start + i];
                        } else {
                            fftSamples[i] = track.audio[end - 1];
                        }
                    }
                    float domFreq = computeDominantFrequency(fftSamples.data(), fftSamples.size(), sampleRate, fftBuffer);
                    track.waveformColor[block] = (domFreq <= 0.0f) ? kWaveformFallbackColor : frequencyToColor(domFreq);
                }
            }
            track.waveformVersion += 1;
        }

        void updateWaveformRange(MidiTrack& track, size_t startSample, size_t endSample) {
            size_t recordedStart = static_cast<size_t>(track.recordStartSample);
            size_t recordedEnd = recordedStart + track.pendingRecord.size();
            size_t combinedLength = std::max(track.audio.size(), recordedEnd);
            if (combinedLength == 0) return;
            size_t blockCount = (combinedLength + kWaveformBlockSize - 1) / kWaveformBlockSize;
            if (track.waveformMin.size() < blockCount) {
                track.waveformMin.resize(blockCount, 0.0f);
                track.waveformMax.resize(blockCount, 0.0f);
                track.waveformColor.resize(blockCount, kWaveformFallbackColor);
            }
            std::vector<Complex> fftBuffer;
            std::vector<float> fftSamples;
            size_t startBlock = startSample / kWaveformBlockSize;
            size_t endBlock = std::min(blockCount, (endSample + kWaveformBlockSize - 1) / kWaveformBlockSize);
            for (size_t block = startBlock; block < endBlock; ++block) {
                size_t blockStart = block * kWaveformBlockSize;
                size_t blockEnd = std::min(blockStart + kWaveformBlockSize, combinedLength);
                float minVal = 0.0f;
                float maxVal = 0.0f;
                for (size_t i = blockStart; i < blockEnd; ++i) {
                    float base = (i < track.audio.size()) ? track.audio[i] : 0.0f;
                    float rec = 0.0f;
                    if (i >= recordedStart && i < recordedEnd) {
                        rec = track.pendingRecord[i - recordedStart];
                    }
                    float v = (track.recordArmMode == 2 && i >= recordedStart && i < recordedEnd)
                        ? rec
                        : base + rec;
                    if (i == blockStart) {
                        minVal = v;
                        maxVal = v;
                    } else {
                        minVal = std::min(minVal, v);
                        maxVal = std::max(maxVal, v);
                    }
                }
                track.waveformMin[block] = minVal;
                track.waveformMax[block] = maxVal;
                fftSamples.resize(kWaveformBlockSize);
                size_t sampleCount = blockEnd - blockStart;
                for (size_t i = 0; i < kWaveformBlockSize; ++i) {
                    if (i < sampleCount) {
                        size_t idx = blockStart + i;
                        float base = (idx < track.audio.size()) ? track.audio[idx] : 0.0f;
                        float rec = 0.0f;
                        if (idx >= recordedStart && idx < recordedEnd) {
                            rec = track.pendingRecord[idx - recordedStart];
                        }
                        fftSamples[i] = (track.recordArmMode == 2 && idx >= recordedStart && idx < recordedEnd)
                            ? rec
                            : base + rec;
                    } else {
                        fftSamples[i] = fftSamples[sampleCount - 1];
                    }
                }
                float domFreq = computeDominantFrequency(fftSamples.data(), fftSamples.size(), 44100.0f, fftBuffer);
                track.waveformColor[block] = (domFreq <= 0.0f) ? kWaveformFallbackColor : frequencyToColor(domFreq);
            }
            track.waveformVersion += 1;
        }

        void drainRecordRings(MidiContext& midi) {
            for (auto& track : midi.tracks) {
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

        void startRecording(MidiContext& midi, uint64_t playhead) {
            for (auto& track : midi.tracks) {
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

        void stopRecording(MidiContext& midi) {
            for (auto& track : midi.tracks) {
                track.recordEnabled.store(false, std::memory_order_relaxed);
                if (track.recordingActive) {
                    track.recordingActive = false;
                }
            }
        }

        void mergePendingRecords(MidiContext& midi) {
            for (auto& track : midi.tracks) {
                if (track.pendingRecord.empty()) continue;
                size_t start = static_cast<size_t>(track.recordStartSample);
                size_t needed = start + track.pendingRecord.size();
                if (track.audio.size() < needed) {
                    track.audio.resize(needed, 0.0f);
                }
                if (track.recordArmMode == 2) {
                    std::copy(track.pendingRecord.begin(), track.pendingRecord.end(), track.audio.begin() + start);
                } else {
                    for (size_t i = 0; i < track.pendingRecord.size(); ++i) {
                        track.audio[start + i] += track.pendingRecord[i];
                    }
                }
                track.pendingRecord.clear();
                rebuildWaveform(track, midi.sampleRate);
            }
        }

        bool hasRingData(const MidiContext& midi) {
            for (const auto& track : midi.tracks) {
                if (!track.recordRing) continue;
                if (jack_ringbuffer_read_space(track.recordRing) > 0) return true;
            }
            return false;
        }

        void writeTracksIfNeeded(MidiContext& midi, const DawContext& daw) {
            if (!daw.mirrorAvailable) return;
            for (int i = 0; i < getTrackCount(midi); ++i) {
                const auto& data = midi.tracks[static_cast<size_t>(i)].audio;
                std::filesystem::path outPath = std::filesystem::path(daw.mirrorPath) / ("midi_track_" + std::to_string(i + 1) + ".wav");
                writeWavMonoFloat(outPath.string(), data, static_cast<uint32_t>(midi.sampleRate));
            }
        }

        void loadTracksIfAvailable(MidiContext& midi, const DawContext& daw) {
            if (!daw.mirrorAvailable) return;
            for (int i = 0; i < getTrackCount(midi); ++i) {
                std::filesystem::path inPath = std::filesystem::path(daw.mirrorPath) / ("midi_track_" + std::to_string(i + 1) + ".wav");
                float sampleRate = 0.0f;
                std::vector<float> data;
                if (readWavMonoFloat(inPath.string(), data, sampleRate)) {
                    midi.tracks[static_cast<size_t>(i)].audio = std::move(data);
                    rebuildWaveform(midi.tracks[static_cast<size_t>(i)], sampleRate);
                } else {
                    midi.tracks[static_cast<size_t>(i)].audio.clear();
                    midi.tracks[static_cast<size_t>(i)].waveformMin.clear();
                    midi.tracks[static_cast<size_t>(i)].waveformMax.clear();
                    midi.tracks[static_cast<size_t>(i)].waveformColor.clear();
                    midi.tracks[static_cast<size_t>(i)].waveformVersion += 1;
                }
            }
        }

        void updateMidiButtonVisuals(BaseSystem& baseSystem, MidiContext& midi) {
            (void)baseSystem;
            int trackCount = getTrackCount(midi);
            for (auto* instPtr : midi.trackInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.actionKey == "arm") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0) continue;
                    int armMode = midi.tracks[trackIndex].armMode.load(std::memory_order_relaxed);
                    if (armMode == 1) inst.uiState = "overdub";
                    else if (armMode == 2) inst.uiState = "replace";
                    else inst.uiState = "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, armMode > 0);
                } else if (inst.actionKey == "solo") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0) continue;
                    bool active = midi.tracks[trackIndex].solo.load(std::memory_order_relaxed);
                    inst.uiState = active ? "active" : "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, active);
                } else if (inst.actionKey == "mute") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0) continue;
                    bool active = midi.tracks[trackIndex].mute.load(std::memory_order_relaxed);
                    inst.uiState = active ? "active" : "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, active);
                } else if (inst.actionKey == "output") {
                    int trackIndex = parseTrackIndex(inst.actionValue, trackCount);
                    if (trackIndex < 0) continue;
                    int currentBus = midi.tracks[trackIndex].outputBus.load(std::memory_order_relaxed);
                    inst.uiState = busStateForIndex(currentBus);
                } else if (inst.actionKey == "input" || inst.actionKey == "clear") {
                    inst.uiState = "idle";
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, false);
                }
            }
        }

        void updateMidiUILayout(BaseSystem& baseSystem, MidiContext& midi, GLFWwindow* win) {
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
            controlX.reserve(midi.trackInstances.size());
            float outputX = panelRight - 32.0f;

            for (auto* instPtr : midi.trackInstances) {
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
            for (auto* instPtr : midi.trackLabelInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.controlId.find("_output") != std::string::npos) continue;
                auto it = controlX.find(inst.controlId);
                if (it != controlX.end()) {
                    inst.position.x = it->second;
                }
            }
            for (auto* instPtr : midi.outputLabelInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.controlId.find("_output") == std::string::npos) continue;
                int busIndex = -1;
                if (inst.textType == "VariableUI") {
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

        void buildMidiUiCache(BaseSystem& baseSystem, std::vector<Entity>& prototypes, MidiContext& midi) {
            midi.trackInstances.clear();
            midi.trackLabelInstances.clear();
            midi.outputLabelInstances.clear();
            midi.worldIndex = -1;
            midi.basePositions.clear();
            midi.baseLabelPositions.clear();
            if (!baseSystem.level) return;
            for (size_t w = 0; w < baseSystem.level->worlds.size(); ++w) {
                auto& world = baseSystem.level->worlds[w];
                bool isTrackControls = isMidiTrackRowWorld(world.name);
                if (isTrackControls && midi.worldIndex < 0) {
                    midi.worldIndex = static_cast<int>(w);
                    midi.basePositions.reserve(world.instances.size());
                    for (const auto& inst : world.instances) {
                        midi.basePositions.push_back(inst.position);
                    }
                }
                for (auto& inst : world.instances) {
                    if (inst.actionType == "DawMidiTrack") {
                        midi.trackInstances.push_back(&inst);
                    }
                    if (isTrackControls) {
                        if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                        if (prototypes[inst.prototypeID].name != "Text") continue;
                        if (inst.textType == "VariableUI" && inst.textKey.rfind("midi_out_", 0) == 0) {
                            if (inst.controlId.find("_output") != std::string::npos) {
                                midi.outputLabelInstances.push_back(&inst);
                            }
                        } else if (inst.controlRole == "label") {
                            midi.trackLabelInstances.push_back(&inst);
                        }
                    }
                }
            }
            midi.uiCacheBuilt = true;
            midi.uiLevel = baseSystem.level.get();
        }

        void updateMidiInputLabels(BaseSystem& baseSystem, MidiContext& midi, AudioContext& audio) {
            if (!baseSystem.font) return;
            FontContext& fontCtx = *baseSystem.font;
            int physicalCount = static_cast<int>(audio.physicalMidiInputPorts.size());
            int totalInputs = physicalCount;
            int trackCount = getTrackCount(midi);
            for (int i = 0; i < trackCount; ++i) {
                int idx = midi.tracks[static_cast<size_t>(i)].physicalInputIndex;
                std::string key = "midi_in_" + std::to_string(i + 1);
                if (idx < 0 || idx >= totalInputs) {
                    idx = 0;
                    midi.tracks[static_cast<size_t>(i)].physicalInputIndex = 0;
                }
                if (physicalCount == 0) {
                    fontCtx.variables[key] = "NONE";
                } else {
                    fontCtx.variables[key] = audio.physicalMidiInputPorts[idx];
                }
                std::string outKey = "midi_out_" + std::to_string(i + 1);
                int busIndex = midi.tracks[static_cast<size_t>(i)].outputBus.load(std::memory_order_relaxed);
                std::string outLabel = busLabelForIndex(busIndex);
                if (outLabel.empty()) outLabel = "?";
                fontCtx.variables[outKey] = outLabel;
            }
        }

        void refreshPhysicalMidiInputs(AudioContext& audio) {
            if (!audio.client) return;
            audio.physicalMidiInputPorts.clear();
            if (const char** capturePorts = jack_get_ports(audio.client, nullptr, JACK_DEFAULT_MIDI_TYPE,
                                                           JackPortIsOutput)) {
                for (size_t i = 0; capturePorts[i]; ++i) {
                    audio.physicalMidiInputPorts.emplace_back(capturePorts[i]);
                }
                jack_free(capturePorts);
            }
            if (audio.physicalMidiInputPorts.empty()) {
                if (const char** ports = jack_get_ports(audio.client, nullptr, nullptr, JackPortIsOutput)) {
                    for (size_t i = 0; ports[i]; ++i) {
                        jack_port_t* port = jack_port_by_name(audio.client, ports[i]);
                        if (!port) continue;
                        const char* type = jack_port_type(port);
                        if (type && std::string(type) == JACK_DEFAULT_MIDI_TYPE) {
                            audio.physicalMidiInputPorts.emplace_back(ports[i]);
                        }
                    }
                    jack_free(ports);
                }
            }
        }

        void reconnectMidiInput(AudioContext& audio, const MidiTrack& track) {
            if (!audio.client) return;
            if (audio.midi_input_ports.empty()) return;
            jack_port_t* inputPort = audio.midi_input_ports[0];
            if (!inputPort) return;
            std::string inputName = jack_port_name(inputPort);
            for (const auto& physical : audio.physicalMidiInputPorts) {
                int rc = jack_disconnect(audio.client, physical.c_str(), inputName.c_str());
                if (rc != 0 && rc != EEXIST && rc != ENOENT) {
                    std::cerr << "JACK MIDI disconnect failed: " << physical << " -> " << inputName << " (" << rc << ")\n";
                }
            }
            int physicalCount = static_cast<int>(audio.physicalMidiInputPorts.size());
            if (track.physicalInputIndex >= 0 && track.physicalInputIndex < physicalCount) {
                const std::string& src = audio.physicalMidiInputPorts[track.physicalInputIndex];
                int rc = jack_connect(audio.client, src.c_str(), inputName.c_str());
                if (rc != 0 && rc != EEXIST) {
                    std::cerr << "JACK MIDI connect failed: " << src << " -> " << inputName << " (" << rc << ")\n";
                }
            }
        }

        void initializeMidi(MidiContext& midi, AudioContext& audio, DawContext& daw) {
            midi.sampleRate = audio.sampleRate > 0.0f ? audio.sampleRate : 44100.0f;
            if (midi.trackCount <= 0 && daw.mirrorAvailable) {
                midi.trackCount = detectMirrorTrackCount(daw);
            }
            if (midi.trackCount < 0) midi.trackCount = 0;
            if (midi.tracks.empty() && midi.trackCount > 0) {
                midi.tracks.resize(static_cast<size_t>(midi.trackCount));
            }
            if (midi.trackCount != getTrackCount(midi)) {
                midi.trackCount = getTrackCount(midi);
            }
            for (int i = 0; i < getTrackCount(midi); ++i) {
                initTrack(midi.tracks[static_cast<size_t>(i)], i, midi, audio);
            }
            refreshPhysicalMidiInputs(audio);
            if (!midi.tracks.empty()) {
                reconnectMidiInput(audio, midi.tracks[0]);
            }
            loadTracksIfAvailable(midi, daw);
            audio.midi = &midi;
            midi.initialized = true;
        }

        void updateMidiRowPosition(BaseSystem&, MidiContext&, const DawContext&) {}
    }

    void UpdateMidiState(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)dt;
        if (!baseSystem.midi || !baseSystem.audio || !baseSystem.ui || !baseSystem.daw) return;
        MidiContext& midi = *baseSystem.midi;
        AudioContext& audio = *baseSystem.audio;
        UIContext& ui = *baseSystem.ui;
        DawContext& daw = *baseSystem.daw;

        if (!midi.initialized && audio.client) {
            initializeMidi(midi, audio, daw);
        }
        if (!midi.initialized) return;
        midi.trackCount = getTrackCount(midi);

        if (!midi.uiCacheBuilt || midi.uiLevel != baseSystem.level.get()) {
            buildMidiUiCache(baseSystem, prototypes, midi);
        }

        updateMidiStamping(baseSystem, midi, daw);

        drainRecordRings(midi);

        if (win && ui.active && !ui.loadingActive) {
            int newNote = -1;
            if (glfwGetKey(win, GLFW_KEY_1) == GLFW_PRESS) newNote = 60;
            else if (glfwGetKey(win, GLFW_KEY_2) == GLFW_PRESS) newNote = 62;
            else if (glfwGetKey(win, GLFW_KEY_3) == GLFW_PRESS) newNote = 64;
            else if (glfwGetKey(win, GLFW_KEY_4) == GLFW_PRESS) newNote = 65;
            else if (glfwGetKey(win, GLFW_KEY_5) == GLFW_PRESS) newNote = 67;
            else if (glfwGetKey(win, GLFW_KEY_6) == GLFW_PRESS) newNote = 69;
            else if (glfwGetKey(win, GLFW_KEY_7) == GLFW_PRESS) newNote = 71;
            else if (glfwGetKey(win, GLFW_KEY_8) == GLFW_PRESS) newNote = 72;
            int prevNote = midi.activeNote.load(std::memory_order_relaxed);
            if (newNote != prevNote) {
                midi.activeNote.store(newNote, std::memory_order_relaxed);
                midi.activeVelocity.store(newNote >= 0 ? 0.8f : 0.0f, std::memory_order_relaxed);
            }
        }

        bool recording = daw.transportRecording.load(std::memory_order_relaxed);
        if (recording && !midi.recordingActive) {
            startRecording(midi, daw.playheadSample.load(std::memory_order_relaxed));
            midi.recordingActive = true;
        } else if (!recording && midi.recordingActive) {
            stopRecording(midi);
            midi.recordStopPending = true;
            midi.recordingActive = false;
        }

        if (ui.active && ui.actionDelayFrames == 0 && !ui.pendingActionType.empty()) {
            if (ui.pendingActionType == "DawMidiTrack") {
                const std::string key = ui.pendingActionKey;
                int trackCount = getTrackCount(midi);
                if (key == "arm") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        int current = midi.tracks[trackIndex].armMode.load(std::memory_order_relaxed);
                        int next = (current + 1) % 3;
                        midi.tracks[trackIndex].armMode.store(next, std::memory_order_relaxed);
                    }
                } else if (key == "solo") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        bool newSolo = !midi.tracks[trackIndex].solo.load(std::memory_order_relaxed);
                        midi.tracks[trackIndex].solo.store(newSolo, std::memory_order_relaxed);
                        if (newSolo) {
                            midi.tracks[trackIndex].mute.store(false, std::memory_order_relaxed);
                        }
                    }
                } else if (key == "mute") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        bool newMute = !midi.tracks[trackIndex].mute.load(std::memory_order_relaxed);
                        midi.tracks[trackIndex].mute.store(newMute, std::memory_order_relaxed);
                        if (newMute) {
                            midi.tracks[trackIndex].solo.store(false, std::memory_order_relaxed);
                        }
                    }
                } else if (key == "output") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        int current = midi.tracks[trackIndex].outputBus.load(std::memory_order_relaxed);
                        int next = (current + 1) % DawContext::kBusCount;
                        midi.tracks[trackIndex].outputBus.store(next, std::memory_order_relaxed);
                    }
                } else if (key == "input") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        refreshPhysicalMidiInputs(audio);
                        int physicalCount = static_cast<int>(audio.physicalMidiInputPorts.size());
                        if (physicalCount > 0) {
                            int next = midi.tracks[trackIndex].physicalInputIndex + 1;
                            next %= physicalCount;
                            midi.tracks[trackIndex].physicalInputIndex = next;
                            reconnectMidiInput(audio, midi.tracks[trackIndex]);
                        }
                    }
                } else if (key == "clear") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex >= 0) {
                        midi.tracks[trackIndex].clearPending = true;
                    }
                } else if (key == "add") {
                    if (!daw.transportPlaying.load(std::memory_order_relaxed)
                        && !daw.transportRecording.load(std::memory_order_relaxed)) {
                        addTrack(baseSystem, midi, audio, daw);
                    }
                } else if (key == "remove") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue, trackCount);
                    if (trackIndex < 0 && trackCount > 0) {
                        trackIndex = trackCount - 1;
                    }
                    if (trackIndex >= 0
                        && !daw.transportPlaying.load(std::memory_order_relaxed)
                        && !daw.transportRecording.load(std::memory_order_relaxed)) {
                        removeTrackAt(baseSystem, midi, audio, daw, trackIndex);
                    }
                }
                ui.pendingActionType.clear();
                ui.pendingActionKey.clear();
                ui.pendingActionValue.clear();
            }
        }

        if (midi.recordStopPending) {
            if (!daw.transportPlaying.load(std::memory_order_relaxed) &&
                daw.audioThreadIdle.load(std::memory_order_relaxed) && !hasRingData(midi)) {
                mergePendingRecords(midi);
                writeTracksIfNeeded(midi, daw);
                midi.recordStopPending = false;
            }
        }

        for (auto& track : midi.tracks) {
            if (!track.clearPending) continue;
            track.audio.clear();
            track.pendingRecord.clear();
            track.waveformMin.clear();
            track.waveformMax.clear();
            track.waveformColor.clear();
            track.waveformVersion += 1;
            if (track.recordRing) {
                jack_ringbuffer_reset(track.recordRing);
            }
            std::filesystem::path outPath = std::filesystem::path(daw.mirrorPath) / ("midi_track_1.wav");
            writeWavMonoFloat(outPath.string(), track.audio, static_cast<uint32_t>(midi.sampleRate));
            track.clearPending = false;
        }

        updateMidiInputLabels(baseSystem, midi, audio);
        updateMidiButtonVisuals(baseSystem, midi);
        updateMidiUILayout(baseSystem, midi, win);
    }

    void CleanupMidiState(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        (void)prototypes; (void)dt; (void)win;
        if (!baseSystem.midi) return;
        MidiContext& midi = *baseSystem.midi;
        for (auto& track : midi.tracks) {
            if (track.recordRing) {
                jack_ringbuffer_free(track.recordRing);
                track.recordRing = nullptr;
            }
        }
        if (baseSystem.audio) {
            baseSystem.audio->midi = nullptr;
        }
    }

    bool InsertTrackAt(BaseSystem& baseSystem, int trackIndex) {
        if (!baseSystem.midi || !baseSystem.audio || !baseSystem.daw) return false;
        return insertTrackAt(baseSystem, *baseSystem.midi, *baseSystem.audio, *baseSystem.daw, trackIndex);
    }

    bool RemoveTrackAt(BaseSystem& baseSystem, int trackIndex) {
        if (!baseSystem.midi || !baseSystem.audio || !baseSystem.daw) return false;
        return removeTrackAt(baseSystem, *baseSystem.midi, *baseSystem.audio, *baseSystem.daw, trackIndex);
    }

    bool MoveTrack(BaseSystem& baseSystem, int fromIndex, int toIndex) {
        if (!baseSystem.midi || !baseSystem.audio || !baseSystem.daw) return false;
        return moveTrack(baseSystem, *baseSystem.midi, *baseSystem.audio, *baseSystem.daw, fromIndex, toIndex);
    }
}
