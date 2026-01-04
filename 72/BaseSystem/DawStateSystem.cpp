#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace ButtonSystemLogic {
    void SetButtonToggled(int instanceID, bool toggled);
}

namespace DawStateSystemLogic {

    namespace {
        constexpr int kRecordRingSeconds = 2;
        constexpr size_t kWaveformBlockSize = 256;

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

        void rebuildWaveform(DawTrack& track) {
            track.waveformMin.clear();
            track.waveformMax.clear();
            if (track.audio.empty()) {
                track.waveformVersion += 1;
                return;
            }
            size_t blockCount = (track.audio.size() + kWaveformBlockSize - 1) / kWaveformBlockSize;
            track.waveformMin.assign(blockCount, 0.0f);
            track.waveformMax.assign(blockCount, 0.0f);
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
            }
            track.waveformVersion += 1;
        }

        int parseTrackIndex(const std::string& value) {
            if (value.empty()) return -1;
            try {
                int idx = std::stoi(value);
                if (idx < 0 || idx >= DawContext::kTrackCount) return -1;
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

        bool parseTrackAndBus(const std::string& value, int& outTrack, int& outBus) {
            size_t sep = value.find(':');
            if (sep == std::string::npos) return false;
            int trackIdx = parseTrackIndex(value.substr(0, sep));
            int busIdx = parseBus(value.substr(sep + 1));
            if (trackIdx < 0 || busIdx < 0) return false;
            outTrack = trackIdx;
            outBus = busIdx;
            return true;
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

        void drainRecordRings(DawContext& daw) {
            for (auto& track : daw.tracks) {
                if (!track.recordRing) continue;
                size_t bytes = jack_ringbuffer_read_space(track.recordRing);
                if (bytes == 0) continue;
                size_t frames = bytes / sizeof(float);
                size_t oldSize = track.pendingRecord.size();
                track.pendingRecord.resize(oldSize + frames);
                jack_ringbuffer_read(track.recordRing,
                                     reinterpret_cast<char*>(track.pendingRecord.data() + oldSize),
                                     frames * sizeof(float));
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
                rebuildWaveform(track);
            }
        }

        void writeTracksIfNeeded(DawContext& daw) {
            if (!daw.mirrorAvailable) return;
            for (int i = 0; i < DawContext::kTrackCount; ++i) {
                const auto& data = daw.tracks[i].audio;
                std::filesystem::path outPath = std::filesystem::path(daw.mirrorPath) / ("track_" + std::to_string(i + 1) + ".wav");
                writeWavMonoFloat(outPath.string(), data, static_cast<uint32_t>(daw.sampleRate));
            }
        }

        void reconnectTrackInput(AudioContext& audio, DawTrack& track);
        void refreshPhysicalInputs(AudioContext& audio);

        void loadTracksIfAvailable(DawContext& daw) {
            if (!daw.mirrorAvailable) return;
            for (int i = 0; i < DawContext::kTrackCount; ++i) {
                std::filesystem::path inPath = std::filesystem::path(daw.mirrorPath) / ("track_" + std::to_string(i + 1) + ".wav");
                uint32_t rate = 0;
                std::vector<float> data;
                if (loadWavMonoFloat(inPath.string(), data, rate)) {
                    daw.tracks[i].audio = std::move(data);
                    rebuildWaveform(daw.tracks[i]);
                } else {
                    daw.tracks[i].waveformMin.clear();
                    daw.tracks[i].waveformMax.clear();
                    daw.tracks[i].waveformVersion += 1;
                }
            }
        }

        void initializeDaw(DawContext& daw, AudioContext& audio) {
            if (daw.initialized) return;
            refreshPhysicalInputs(audio);
            daw.sampleRate = audio.sampleRate > 0.0f ? audio.sampleRate : 44100.0f;
            int physicalCount = static_cast<int>(audio.physicalInputPorts.size());
            for (int i = 0; i < DawContext::kTrackCount; ++i) {
                daw.tracks[i].inputIndex = i;
                daw.tracks[i].outputBus.store(2, std::memory_order_relaxed);
                if (physicalCount > 0) {
                    daw.tracks[i].physicalInputIndex = static_cast<int>(i % physicalCount);
                } else {
                    daw.tracks[i].physicalInputIndex = 0;
                }
                daw.tracks[i].useVirtualInput.store(physicalCount == 0, std::memory_order_relaxed);
                if (!daw.tracks[i].recordRing) {
                    size_t ringBytes = static_cast<size_t>(daw.sampleRate) * kRecordRingSeconds * sizeof(float);
                    daw.tracks[i].recordRing = jack_ringbuffer_create(ringBytes);
                    if (daw.tracks[i].recordRing) {
                        jack_ringbuffer_mlock(daw.tracks[i].recordRing);
                    }
                }
                if (physicalCount > 0) {
                    reconnectTrackInput(audio, daw.tracks[i]);
                }
            }
            resolveMirrorPath(daw);
            loadTracksIfAvailable(daw);
            daw.playheadSample.store(0, std::memory_order_relaxed);
            daw.transportPlaying.store(false, std::memory_order_relaxed);
            daw.transportRecording.store(false, std::memory_order_relaxed);
            daw.audioThreadIdle.store(true, std::memory_order_relaxed);
            daw.transportLatch = kTransportNone;
            audio.daw = &daw;
            daw.initialized = true;
        }

        void updateTrackButtonVisuals(BaseSystem& baseSystem, DawContext& daw) {
            (void)baseSystem;
            const glm::vec3 idleColor(0.3f, 0.3f, 0.3f);
            const glm::vec3 armOverdub(0.75f, 0.25f, 0.25f);
            const glm::vec3 armReplace(0.95f, 0.55f, 0.25f);
            const glm::vec3 soloActive(0.95f, 0.9f, 0.25f);
            const glm::vec3 muteActive(0.6f, 0.2f, 0.2f);
            const glm::vec3 busColors[DawContext::kBusCount] = {
                glm::vec3(0.35f, 0.6f, 0.95f),
                glm::vec3(0.7f, 0.4f, 0.9f),
                glm::vec3(0.4f, 0.9f, 0.4f),
                glm::vec3(0.95f, 0.45f, 0.45f)
            };

            for (auto* instPtr : daw.trackInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.actionKey == "arm") {
                    int trackIndex = parseTrackIndex(inst.actionValue);
                    if (trackIndex < 0) continue;
                    int armMode = daw.tracks[trackIndex].armMode.load(std::memory_order_relaxed);
                    if (armMode == 1) inst.color = armOverdub;
                    else if (armMode == 2) inst.color = armReplace;
                    else inst.color = idleColor;
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, armMode > 0);
                } else if (inst.actionKey == "solo") {
                    int trackIndex = parseTrackIndex(inst.actionValue);
                    if (trackIndex < 0) continue;
                    bool active = daw.tracks[trackIndex].solo.load(std::memory_order_relaxed);
                    inst.color = active ? soloActive : idleColor;
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, active);
                } else if (inst.actionKey == "mute") {
                    int trackIndex = parseTrackIndex(inst.actionValue);
                    if (trackIndex < 0) continue;
                    bool active = daw.tracks[trackIndex].mute.load(std::memory_order_relaxed);
                    inst.color = active ? muteActive : idleColor;
                    ButtonSystemLogic::SetButtonToggled(inst.instanceID, active);
                } else if (inst.actionKey == "output") {
                    int trackIndex = -1;
                    int busIndex = -1;
                    if (!parseTrackAndBus(inst.actionValue, trackIndex, busIndex)) continue;
                    int currentBus = daw.tracks[trackIndex].outputBus.load(std::memory_order_relaxed);
                    inst.color = (currentBus == busIndex) ? busColors[busIndex] : idleColor;
                } else if (inst.actionKey == "input" || inst.actionKey == "clear") {
                    inst.color = idleColor;
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
            }
        }

        void updateDawUILayout(DawContext& daw, GLFWwindow* win) {
            if (!win) return;
            int windowWidth = 0, windowHeight = 0;
            glfwGetWindowSize(win, &windowWidth, &windowHeight);
            (void)windowHeight;
            double screenWidth = windowWidth > 0 ? static_cast<double>(windowWidth) : 1920.0;
            float outputRight = static_cast<float>(screenWidth) - 60.0f;
            float outputSpacing = 40.0f;
            float outputX[DawContext::kBusCount] = {
                outputRight - 3.0f * outputSpacing,
                outputRight - 2.0f * outputSpacing,
                outputRight - outputSpacing,
                outputRight
            };
            float outputThreshold = outputX[0] - 200.0f;

            for (auto* instPtr : daw.trackInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.actionKey != "output") continue;
                int trackIndex = -1;
                int busIndex = -1;
                if (!parseTrackAndBus(inst.actionValue, trackIndex, busIndex)) continue;
                if (busIndex >= 0 && busIndex < DawContext::kBusCount) {
                    inst.position.x = outputX[busIndex];
                }
            }
            for (auto* instPtr : daw.outputLabelInstances) {
                if (!instPtr) continue;
                EntityInstance& inst = *instPtr;
                if (inst.textType != "UIOnly" || inst.text.size() != 1 || inst.position.x <= outputThreshold) continue;
                int busIndex = -1;
                if (inst.text == "L") busIndex = 0;
                else if (inst.text == "S") busIndex = 1;
                else if (inst.text == "F") busIndex = 2;
                else if (inst.text == "R") busIndex = 3;
                if (busIndex >= 0 && busIndex < DawContext::kBusCount) {
                    inst.position.x = outputX[busIndex];
                }
            }
        }

        void buildDawUiCache(BaseSystem& baseSystem, std::vector<Entity>& prototypes, DawContext& daw) {
            daw.trackInstances.clear();
            daw.transportInstances.clear();
            daw.outputLabelInstances.clear();
            if (!baseSystem.level) return;
            for (auto& world : baseSystem.level->worlds) {
                bool isTrackControls = isTrackRowWorld(world.name);
                for (auto& inst : world.instances) {
                    if (inst.actionType == "DawTrack") {
                        daw.trackInstances.push_back(&inst);
                    } else if (inst.actionType == "DawTransport") {
                        daw.transportInstances.push_back(&inst);
                    }
                    if (isTrackControls) {
                        if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                        if (prototypes[inst.prototypeID].name != "Text") continue;
                        if (inst.textType != "UIOnly" || inst.text.size() != 1) continue;
                        if (inst.text == "L" || inst.text == "S" || inst.text == "F" || inst.text == "R") {
                            daw.outputLabelInstances.push_back(&inst);
                        }
                    }
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
            for (int i = 0; i < DawContext::kTrackCount; ++i) {
                int idx = daw.tracks[i].physicalInputIndex;
                std::string key = "daw_in_" + std::to_string(i + 1);
                if (idx < 0 || idx >= totalInputs) {
                    idx = 0;
                    daw.tracks[i].physicalInputIndex = 0;
                }
                if (idx < physicalCount) {
                    fontCtx.variables[key] = "IN" + std::to_string(idx + 1);
                } else {
                    fontCtx.variables[key] = "VM1";
                }
                daw.tracks[i].useVirtualInput.store(idx >= physicalCount, std::memory_order_relaxed);
            }
        }

        void reconnectTrackInput(AudioContext& audio, DawTrack& track) {
            if (!audio.client) return;
            if (track.inputIndex < 0 || track.inputIndex >= static_cast<int>(audio.input_ports.size())) return;
            jack_port_t* inputPort = audio.input_ports[track.inputIndex];
            if (!inputPort) return;
            std::string inputName = jack_port_name(inputPort);
            for (const auto& physical : audio.physicalInputPorts) {
                jack_disconnect(audio.client, physical.c_str(), inputName.c_str());
            }
            int physicalCount = static_cast<int>(audio.physicalInputPorts.size());
            bool useVirtual = (track.physicalInputIndex < 0 || track.physicalInputIndex >= physicalCount);
            track.useVirtualInput.store(useVirtual, std::memory_order_relaxed);
            if (!useVirtual) {
                jack_connect(audio.client, audio.physicalInputPorts[track.physicalInputIndex].c_str(), inputName.c_str());
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
            } else if (ui.pendingActionType == "DawTrack") {
                const std::string key = ui.pendingActionKey;
                if (key == "arm") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue);
                    if (trackIndex >= 0) {
                        int current = daw.tracks[trackIndex].armMode.load(std::memory_order_relaxed);
                        int next = (current + 1) % 3;
                        daw.tracks[trackIndex].armMode.store(next, std::memory_order_relaxed);
                    }
                } else if (key == "solo") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue);
                    if (trackIndex >= 0) {
                        bool newSolo = !daw.tracks[trackIndex].solo.load(std::memory_order_relaxed);
                        daw.tracks[trackIndex].solo.store(newSolo, std::memory_order_relaxed);
                        if (newSolo) {
                            daw.tracks[trackIndex].mute.store(false, std::memory_order_relaxed);
                        }
                    }
                } else if (key == "mute") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue);
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
                    if (parseTrackAndBus(ui.pendingActionValue, trackIndex, busIndex)) {
                        daw.tracks[trackIndex].outputBus.store(busIndex, std::memory_order_relaxed);
                    }
                } else if (key == "input") {
                    int trackIndex = parseTrackIndex(ui.pendingActionValue);
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
                    int trackIndex = parseTrackIndex(ui.pendingActionValue);
                    if (trackIndex >= 0) {
                        daw.tracks[trackIndex].clearPending = true;
                    }
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
            for (int i = 0; i < DawContext::kTrackCount; ++i) {
                auto& track = daw.tracks[i];
                if (!track.clearPending) continue;
                track.audio.clear();
                track.pendingRecord.clear();
                track.waveformMin.clear();
                track.waveformMax.clear();
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

        if (ui.active) {
            if (!daw.uiCacheBuilt || daw.uiLevel != baseSystem.level.get()) {
                buildDawUiCache(baseSystem, prototypes, daw);
            }
            updateDawUILayout(daw, win);
            updateTransportButtonVisuals(baseSystem, daw);
            updateTrackButtonVisuals(baseSystem, daw);
            updateInputLabels(baseSystem, daw, audio);
        }
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
}
