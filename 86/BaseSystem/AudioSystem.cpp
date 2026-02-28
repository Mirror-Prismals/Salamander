#pragma once

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string.h>
#include <thread>
#include <cmath>
#include <vector>
#include "chuck.h"
#include "BaseSystem/Vst3Host.h"

// Forward declarations
struct AudioContext;
namespace PinkNoiseSystemLogic {
    // This function must be declared so the audio callback can call it.
    // It's defined in PinkNoiseSystem.cpp.
    float generate_filtered_pink_noise(AudioContext*);
}

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
                outSamples[i] = static_cast<float>(sum) / (static_cast<float>(info.numChannels) * 32768.0f);
            }
            return true;
        }
        return false;
    }

    std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return s;
    }

    std::string cueBaseName(const std::string& cue) {
        std::string out = cue;
        size_t slash = out.find_last_of("/\\");
        if (slash != std::string::npos) out = out.substr(slash + 1);
        size_t dot = out.find_last_of('.');
        if (dot != std::string::npos) out = out.substr(0, dot);
        return toLower(out);
    }

    bool parseBool(const std::string& s, bool fallback) {
        std::string v = toLower(s);
        if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
        if (v == "0" || v == "false" || v == "no" || v == "off") return false;
        return fallback;
    }

    bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
        if (!baseSystem.registry) return fallback;
        auto it = baseSystem.registry->find(key);
        if (it == baseSystem.registry->end()) return fallback;
        if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
        if (std::holds_alternative<std::string>(it->second)) {
            return parseBool(std::get<std::string>(it->second), fallback);
        }
        return fallback;
    }

    std::string getRegistryString(const BaseSystem& baseSystem, const std::string& key, const std::string& fallback) {
        if (!baseSystem.registry) return fallback;
        auto it = baseSystem.registry->find(key);
        if (it == baseSystem.registry->end()) return fallback;
        if (std::holds_alternative<std::string>(it->second)) return std::get<std::string>(it->second);
        if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second) ? "true" : "false";
        return fallback;
    }

    float getRegistryFloat(const BaseSystem& baseSystem, const std::string& key, float fallback) {
        if (!baseSystem.registry) return fallback;
        auto it = baseSystem.registry->find(key);
        if (it == baseSystem.registry->end()) return fallback;
        if (std::holds_alternative<std::string>(it->second)) {
            try {
                return std::stof(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }
        if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second) ? 1.0f : 0.0f;
        return fallback;
    }

    std::vector<float> makeProceduralSfx(const std::string& cue, uint32_t sampleRate) {
        const uint32_t sr = std::max<uint32_t>(8000u, sampleRate);
        auto secondsToFrames = [&](float seconds) {
            return static_cast<size_t>(std::max(1.0f, seconds * static_cast<float>(sr)));
        };
        if (cue == "place_block") {
            size_t n = secondsToFrames(0.065f);
            std::vector<float> out(n, 0.0f);
            for (size_t i = 0; i < n; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(sr);
                float env = std::exp(-42.0f * t);
                float tone = std::sin(2.0f * 3.14159265f * 560.0f * t);
                float click = std::sin(2.0f * 3.14159265f * 1800.0f * t);
                out[i] = 0.18f * env * (0.78f * tone + 0.22f * click);
            }
            return out;
        }
        if (cue == "pickup_block") {
            size_t n = secondsToFrames(0.085f);
            std::vector<float> out(n, 0.0f);
            for (size_t i = 0; i < n; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(sr);
                float env = std::exp(-30.0f * t);
                float toneA = std::sin(2.0f * 3.14159265f * (760.0f - 220.0f * t) * t);
                float toneB = std::sin(2.0f * 3.14159265f * (1240.0f - 420.0f * t) * t);
                out[i] = 0.15f * env * (0.64f * toneA + 0.36f * toneB);
            }
            return out;
        }
        if (cue == "break_stone") {
            size_t n = secondsToFrames(0.14f);
            std::vector<float> out(n, 0.0f);
            uint32_t state = 0x6a09e667u;
            auto nextNoise = [&]() {
                state ^= state << 13u;
                state ^= state >> 17u;
                state ^= state << 5u;
                return (static_cast<float>(state & 0xffffu) / 32768.0f) - 1.0f;
            };
            float lp = 0.0f;
            for (size_t i = 0; i < n; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(sr);
                float env = std::exp(-18.0f * t);
                float strike = std::sin(2.0f * 3.14159265f * 240.0f * t) * std::exp(-34.0f * t);
                float noise = nextNoise();
                lp += 0.22f * (noise - lp);
                out[i] = 0.24f * env * (0.70f * lp + 0.30f * strike);
            }
            return out;
        }
        return {};
    }

    int findGameplaySfxClipIndex(const AudioContext& audio, const std::string& cue) {
        const std::string key = cueBaseName(cue);
        for (size_t i = 0; i < audio.gameplaySfxNames.size(); ++i) {
            if (audio.gameplaySfxNames[i] == key) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void preloadGameplaySfx(BaseSystem& baseSystem, AudioContext& audio) {
        audio.gameplaySfxNames.clear();
        audio.gameplaySfxBuffers.clear();
        audio.gameplaySfxSampleRates.clear();
        audio.gameplaySfxVoices.clear();
        audio.gameplaySfxVoices.resize(16);

        const bool enabled = getRegistryBool(baseSystem, "GameplaySfxEnabled", true);
        const float masterGain = std::clamp(getRegistryFloat(baseSystem, "GameplaySfxGain", 1.0f), 0.0f, 4.0f);
        const bool proceduralFallback = getRegistryBool(baseSystem, "GameplaySfxProceduralFallback", true);
        const std::string folder = getRegistryString(baseSystem, "GameplaySfxFolder", "Procedures/assets/gameplay_sfx");
        audio.gameplaySfxEnabled.store(enabled, std::memory_order_relaxed);
        audio.gameplaySfxMasterGain.store(masterGain, std::memory_order_relaxed);

        static const std::array<const char*, 3> kCues = {
            "break_stone",
            "pickup_block",
            "place_block"
        };

        int loadedCount = 0;
        int generatedCount = 0;
        for (const char* cue : kCues) {
            const std::string cueName(cue);
            std::vector<float> clip;
            uint32_t rate = 0;
            bool loaded = false;
            const std::filesystem::path wavPath = std::filesystem::path(folder) / (cueName + ".wav");
            if (loadWavMono(wavPath.string(), clip, rate) && !clip.empty() && rate > 0) {
                loaded = true;
                loadedCount += 1;
            } else if (proceduralFallback) {
                rate = static_cast<uint32_t>(std::max(8000.0f, audio.sampleRate));
                clip = makeProceduralSfx(cueName, rate);
                if (!clip.empty()) generatedCount += 1;
            }
            audio.gameplaySfxNames.push_back(cueName);
            audio.gameplaySfxBuffers.push_back(std::move(clip));
            audio.gameplaySfxSampleRates.push_back(rate);
        }
        if (enabled) {
            std::cout << "AudioSystem: gameplay SFX ready (wav=" << loadedCount
                      << ", procedural=" << generatedCount << ")." << std::endl;
        }
    }
}

// --- JACK CALLBACKS ---
int jack_process_callback(jack_nframes_t nframes, void* arg) {
    auto* audioContext = static_cast<AudioContext*>(arg);
    float chuckMainPeak = 0.0f;
    float soundtrackPeak = 0.0f;
    float speakerBlockPeak = 0.0f;
    float playerHeadSpeakerPeak = 0.0f;
    // Prepare ChucK interleaved buffer
    const int chuckChannels = audioContext->chuckOutputChannels;
    const size_t chuckFrames = static_cast<size_t>(chuckChannels) * nframes;
    if (audioContext->chuckInterleavedBuffer.size() < chuckFrames) {
        audioContext->chuckInterleavedBuffer.assign(chuckFrames, 0.0f);
    } else {
        std::fill(audioContext->chuckInterleavedBuffer.begin(), audioContext->chuckInterleavedBuffer.begin() + chuckFrames, 0.0f);
    }

    if (audioContext->chuck && audioContext->chuckRunning) {
        audioContext->chuck->run(nullptr, audioContext->chuckInterleavedBuffer.data(), nframes);
    } else {
        std::fill(audioContext->chuckInterleavedBuffer.begin(), audioContext->chuckInterleavedBuffer.begin() + chuckFrames, 0.0f);
    }

    constexpr int kMaxOutputs = 32;
    std::array<jack_default_audio_sample_t*, kMaxOutputs> outBuffers{};
    const int totalOutputs = std::min<int>(static_cast<int>(audioContext->output_ports.size()), kMaxOutputs);
    for (int ch = 0; ch < totalOutputs; ++ch) {
        jack_port_t* port = audioContext->output_ports[ch];
        if (!port) continue;
        auto* out = (jack_default_audio_sample_t*)jack_port_get_buffer(port, nframes);
        outBuffers[ch] = out;
        if (out) {
            std::fill(out, out + nframes, 0.0f);
        }
    }

    if (audioContext->offlineRenderMute.load(std::memory_order_relaxed)) {
        audioContext->chuckMainMeterLevel.store(0.0f, std::memory_order_relaxed);
        audioContext->soundtrackMeterLevel.store(0.0f, std::memory_order_relaxed);
        audioContext->speakerBlockMeterLevel.store(0.0f, std::memory_order_relaxed);
        audioContext->playerHeadSpeakerMeterLevel.store(0.0f, std::memory_order_relaxed);
        if (audioContext->daw) {
            DawContext& daw = *audioContext->daw;
            daw.audioThreadIdle.store(true, std::memory_order_relaxed);
            for (int b = 0; b < DawContext::kBusCount; ++b) {
                daw.masterBusLevels[b].store(0.0f, std::memory_order_relaxed);
            }
        }
        if (audioContext->midi) {
            MidiContext& midi = *audioContext->midi;
            for (auto& track : midi.tracks) {
                track.meterLevel.store(0.0f, std::memory_order_relaxed);
            }
        }
        return 0;
    }

    bool needMicBuffer = false;
    if (audioContext->daw) {
        DawContext& daw = *audioContext->daw;
        if (daw.transportRecording.load(std::memory_order_relaxed)) {
            for (const auto& track : daw.tracks) {
                if (!track.recordEnabled.load(std::memory_order_relaxed)) continue;
                if (track.useVirtualInput.load(std::memory_order_relaxed)) {
                    needMicBuffer = true;
                    break;
                }
            }
        }
    }
    if (needMicBuffer) {
        if (audioContext->micCaptureBuffer.size() < nframes) {
            audioContext->micCaptureBuffer.assign(nframes, 0.0f);
        } else {
            std::fill(audioContext->micCaptureBuffer.begin(),
                      audioContext->micCaptureBuffer.begin() + nframes, 0.0f);
        }
    }

    MidiContext* midiContext = audioContext->midi;

    // Mix ChucK channels to L/R with per-channel pan (keep visualizer sample on ch 0)
    float ring_sample = 0.0f;
    bool ring_sample_set = false;
    if (audioContext->channelGains.size() < static_cast<size_t>(chuckChannels)) {
        audioContext->channelGains.assign(chuckChannels, 1.0f);
    }
    if (audioContext->channelPans.size() < static_cast<size_t>(chuckChannels)) {
        audioContext->channelPans.assign(chuckChannels, 0.0f);
    }
    jack_default_audio_sample_t* chuckOutL = (totalOutputs > 0) ? outBuffers[0] : nullptr;
    jack_default_audio_sample_t* chuckOutR = (totalOutputs > 1) ? outBuffers[1] : nullptr;
    if (chuckOutL || chuckOutR) {
        const float monoScale = 0.5f;
        const float chuckMainGain = audioContext->chuckMainLevelGain.load(std::memory_order_relaxed);
        const int echoChannel = audioContext->rayEchoChannel;
        const float echoGain = audioContext->rayEchoGain;
        const float echoDelaySeconds = audioContext->rayEchoDelaySeconds;
        const int hfChannel = audioContext->rayHfChannel;
        const float hfAlpha = audioContext->rayHfAlpha;
        const int itdChannel = audioContext->rayItdChannel;
        const float panStrength = audioContext->rayPanStrength;
        const float itdMaxMs = audioContext->rayItdMaxMs;
        const size_t itdBufferSize = audioContext->rayItdBuffer.size();
        size_t itdMaxSamples = 0;
        if (itdBufferSize > 1 && itdMaxMs > 0.0f) {
            itdMaxSamples = static_cast<size_t>(audioContext->sampleRate * (itdMaxMs / 1000.0f));
            if (itdMaxSamples >= itdBufferSize) itdMaxSamples = itdBufferSize - 1;
        }
        if (hfChannel < 0 || hfAlpha <= 0.0f) {
            audioContext->rayHfState = 0.0f;
        }
        const size_t echoBufferSize = audioContext->rayEchoBuffer.size();
        size_t echoDelaySamples = 0;
        if (echoBufferSize > 1 && echoDelaySeconds > 0.0f) {
            echoDelaySamples = static_cast<size_t>(echoDelaySeconds * audioContext->sampleRate);
            if (echoDelaySamples >= echoBufferSize) echoDelaySamples = echoBufferSize - 1;
        }

        for (jack_nframes_t i = 0; i < nframes; ++i) {
            float outL = 0.0f;
            float outR = 0.0f;
            for (int ch = 0; ch < chuckChannels; ++ch) {
                float chGain = (ch < static_cast<int>(audioContext->channelGains.size())) ? audioContext->channelGains[ch] : 1.0f;
                float sample = audioContext->chuckInterleavedBuffer[i * chuckChannels + ch] * chGain;
                sample *= chuckMainGain;
                float absSample = std::fabs(sample);
                if (absSample > chuckMainPeak) chuckMainPeak = absSample;
                if (!ring_sample_set && ch == 0 && i == 0) {
                    ring_sample = sample;
                    ring_sample_set = true;
                }

                if (ch == echoChannel && echoBufferSize > 1) {
                    float delayed = 0.0f;
                    if (echoDelaySamples > 0) {
                        size_t readIndex = (audioContext->rayEchoWriteIndex + echoBufferSize - echoDelaySamples) % echoBufferSize;
                        delayed = audioContext->rayEchoBuffer[readIndex];
                    }
                    audioContext->rayEchoBuffer[audioContext->rayEchoWriteIndex] = sample;
                    audioContext->rayEchoWriteIndex = (audioContext->rayEchoWriteIndex + 1) % echoBufferSize;
                    if (echoDelaySamples > 0 && echoGain > 0.0f) {
                        sample += delayed * echoGain;
                    }
                }

                if (ch == hfChannel && hfAlpha > 0.0f) {
                    float filtered = audioContext->rayHfState + hfAlpha * (sample - audioContext->rayHfState);
                    audioContext->rayHfState = filtered;
                    sample = filtered;
                }

                float pan = (ch < static_cast<int>(audioContext->channelPans.size())) ? audioContext->channelPans[ch] : 0.0f;
                pan = std::clamp(pan, -1.0f, 1.0f);
                float effectivePan = pan;
                if (ch == itdChannel) {
                    effectivePan = std::clamp(pan * panStrength, -1.0f, 1.0f);
                }
                float lGain = monoScale * (1.0f - effectivePan);
                float rGain = monoScale * (1.0f + effectivePan);
                float sampleL = sample;
                float sampleR = sample;
                if (ch == itdChannel && itdMaxSamples > 0 && itdBufferSize > 1) {
                    size_t delaySamples = static_cast<size_t>(std::abs(effectivePan) * itdMaxSamples + 0.5f);
                    if (delaySamples > 0 && delaySamples < itdBufferSize) {
                        size_t readIndex = (audioContext->rayItdWriteIndex + itdBufferSize - delaySamples) % itdBufferSize;
                        float delayed = audioContext->rayItdBuffer[readIndex];
                        if (effectivePan >= 0.0f) {
                            sampleL = delayed;
                        } else {
                            sampleR = delayed;
                        }
                    }
                    audioContext->rayItdBuffer[audioContext->rayItdWriteIndex] = sample;
                    audioContext->rayItdWriteIndex = (audioContext->rayItdWriteIndex + 1) % itdBufferSize;
                }
                outL += sampleL * lGain;
                outR += sampleR * rGain;
            }
            if (chuckOutL) chuckOutL[i] += outL;
            if (chuckOutR) chuckOutR[i] += outR;
        }
    } else if (chuckChannels > 0 && !audioContext->chuckInterleavedBuffer.empty()) {
        ring_sample = audioContext->chuckInterleavedBuffer[0];
        ring_sample_set = true;
    }

    if (audioContext->headRayActive
        && audioContext->chuckHeadHasActiveShreds.load(std::memory_order_relaxed)
        && chuckChannels > 0
        && !audioContext->chuckInterleavedBuffer.empty()) {
        jack_default_audio_sample_t* outL = (totalOutputs > 0) ? outBuffers[0] : nullptr;
        jack_default_audio_sample_t* outR = (totalOutputs > 1) ? outBuffers[1] : nullptr;
        if (outL || outR) {
            const int sourceChannel = std::clamp(audioContext->chuckHeadChannel, 0, chuckChannels - 1);
            const float playerHeadGain = audioContext->playerHeadSpeakerLevelGain.load(std::memory_order_relaxed);
            const float monoScale = 0.5f;
            const float panStrength = audioContext->rayPanStrength;
            const float itdMaxMs = audioContext->rayItdMaxMs;
            const float echoGain = audioContext->headRayEchoGain;
            const float echoDelaySeconds = audioContext->headRayEchoDelaySeconds;
            const float hfAlpha = audioContext->headRayHfAlpha;
            const float underwaterMix = std::clamp(
                audioContext->headUnderwaterMix.load(std::memory_order_relaxed)
                    * audioContext->headUnderwaterLowpassStrength.load(std::memory_order_relaxed),
                0.0f, 1.0f
            );
            const float underwaterHz = std::clamp(
                audioContext->headUnderwaterLowpassHz.load(std::memory_order_relaxed),
                20.0f, 20000.0f
            );
            constexpr float kTau = 6.28318530718f;
            float underwaterAlpha = 1.0f;
            if (audioContext->sampleRate > 1.0f) {
                underwaterAlpha = 1.0f - std::exp(-(kTau * underwaterHz / audioContext->sampleRate));
            }
            underwaterAlpha = std::clamp(underwaterAlpha, 0.0001f, 1.0f);
            const size_t echoBufferSize = audioContext->headRayEchoBuffer.size();
            const size_t itdBufferSize = audioContext->headRayItdBuffer.size();
            size_t echoDelaySamples = 0;
            size_t itdMaxSamples = 0;
            if (echoBufferSize > 1 && echoDelaySeconds > 0.0f) {
                echoDelaySamples = static_cast<size_t>(echoDelaySeconds * audioContext->sampleRate);
                if (echoDelaySamples >= echoBufferSize) echoDelaySamples = echoBufferSize - 1;
            }
            if (itdBufferSize > 1 && itdMaxMs > 0.0f) {
                itdMaxSamples = static_cast<size_t>(audioContext->sampleRate * (itdMaxMs / 1000.0f));
                if (itdMaxSamples >= itdBufferSize) itdMaxSamples = itdBufferSize - 1;
            }

            for (jack_nframes_t i = 0; i < nframes; ++i) {
                float raw = audioContext->chuckInterleavedBuffer[static_cast<size_t>(i) * static_cast<size_t>(chuckChannels)
                                                                 + static_cast<size_t>(sourceChannel)];
                float sample = raw * audioContext->headRayGain * playerHeadGain;

                if (echoBufferSize > 1) {
                    float delayed = 0.0f;
                    if (echoDelaySamples > 0) {
                        size_t readIndex = (audioContext->headRayEchoWriteIndex + echoBufferSize - echoDelaySamples) % echoBufferSize;
                        delayed = audioContext->headRayEchoBuffer[readIndex];
                    }
                    audioContext->headRayEchoBuffer[audioContext->headRayEchoWriteIndex] = sample;
                    audioContext->headRayEchoWriteIndex = (audioContext->headRayEchoWriteIndex + 1) % echoBufferSize;
                    if (echoDelaySamples > 0 && echoGain > 0.0f) {
                        sample += delayed * echoGain;
                    }
                }

                if (hfAlpha > 0.0f) {
                    float filtered = audioContext->headRayHfState + hfAlpha * (sample - audioContext->headRayHfState);
                    audioContext->headRayHfState = filtered;
                    sample = filtered;
                }
                if (underwaterMix > 0.001f) {
                    float lowPassed = audioContext->headUnderwaterLpState
                        + underwaterAlpha * (sample - audioContext->headUnderwaterLpState);
                    audioContext->headUnderwaterLpState = lowPassed;
                    sample = sample + (lowPassed - sample) * underwaterMix;
                } else {
                    audioContext->headUnderwaterLpState = sample;
                }

                float pan = std::clamp(audioContext->headRayPan, -1.0f, 1.0f);
                pan = std::clamp(pan * panStrength, -1.0f, 1.0f);
                float lGain = monoScale * (1.0f - pan);
                float rGain = monoScale * (1.0f + pan);
                float sampleL = sample;
                float sampleR = sample;
                if (itdMaxSamples > 0 && itdBufferSize > 1) {
                    size_t delaySamples = static_cast<size_t>(std::abs(pan) * itdMaxSamples + 0.5f);
                    if (delaySamples > 0 && delaySamples < itdBufferSize) {
                        size_t readIndex = (audioContext->headRayItdWriteIndex + itdBufferSize - delaySamples) % itdBufferSize;
                        float delayed = audioContext->headRayItdBuffer[readIndex];
                        if (pan >= 0.0f) {
                            sampleL = delayed;
                        } else {
                            sampleR = delayed;
                        }
                    }
                    audioContext->headRayItdBuffer[audioContext->headRayItdWriteIndex] = sample;
                    audioContext->headRayItdWriteIndex = (audioContext->headRayItdWriteIndex + 1) % itdBufferSize;
                }

                float absSample = std::fabs(sample);
                if (absSample > playerHeadSpeakerPeak) playerHeadSpeakerPeak = absSample;
                if (outL) outL[i] += sampleL * lGain;
                if (outR) outR[i] += sampleR * rGain;
            }
        }
    } else {
        audioContext->headRayHfState = 0.0f;
        audioContext->headUnderwaterLpState = 0.0f;
    }

    if (audioContext->rayTestActive && !audioContext->rayTestBuffer.empty()) {
        jack_default_audio_sample_t* outL = (totalOutputs > 0) ? outBuffers[0] : nullptr;
        jack_default_audio_sample_t* outR = (totalOutputs > 1) ? outBuffers[1] : nullptr;
        if (outL || outR) {
            const float monoScale = 0.5f;
            const float speakerBlockGain = audioContext->speakerBlockLevelGain.load(std::memory_order_relaxed);
            const float echoGain = audioContext->rayEchoGain;
            const float echoDelaySeconds = audioContext->rayEchoDelaySeconds;
            const float hfAlpha = audioContext->rayHfAlpha;
            const float panStrength = audioContext->rayPanStrength;
            const float itdMaxMs = audioContext->rayItdMaxMs;
            const size_t echoBufferSize = audioContext->rayEchoBuffer.size();
            const size_t itdBufferSize = audioContext->rayItdBuffer.size();
            const bool micActive = needMicBuffer && audioContext->micRayActive;
            float* micBuffer = micActive ? audioContext->micCaptureBuffer.data() : nullptr;
            const float micGain = audioContext->micRayGain;
            const float micEchoGain = audioContext->micRayEchoGain;
            const float micEchoDelaySeconds = audioContext->micRayEchoDelaySeconds;
            const float micHfAlpha = audioContext->micRayHfAlpha;
            const size_t micEchoBufferSize = audioContext->micRayEchoBuffer.size();
            size_t echoDelaySamples = 0;
            size_t itdMaxSamples = 0;
            size_t micEchoDelaySamples = 0;
            if (echoBufferSize > 1 && echoDelaySeconds > 0.0f) {
                echoDelaySamples = static_cast<size_t>(echoDelaySeconds * audioContext->sampleRate);
                if (echoDelaySamples >= echoBufferSize) echoDelaySamples = echoBufferSize - 1;
            }
            if (itdBufferSize > 1 && itdMaxMs > 0.0f) {
                itdMaxSamples = static_cast<size_t>(audioContext->sampleRate * (itdMaxMs / 1000.0f));
                if (itdMaxSamples >= itdBufferSize) itdMaxSamples = itdBufferSize - 1;
            }
            if (micEchoBufferSize > 1 && micEchoDelaySeconds > 0.0f) {
                micEchoDelaySamples = static_cast<size_t>(micEchoDelaySeconds * audioContext->sampleRate);
                if (micEchoDelaySamples >= micEchoBufferSize) micEchoDelaySamples = micEchoBufferSize - 1;
            }

            const double step = (audioContext->rayTestSampleRate > 0)
                ? static_cast<double>(audioContext->rayTestSampleRate) / static_cast<double>(audioContext->sampleRate)
                : 1.0;
            const size_t sampleCount = audioContext->rayTestBuffer.size();
            for (jack_nframes_t i = 0; i < nframes; ++i) {
                if (sampleCount == 0) break;
                size_t idx = static_cast<size_t>(audioContext->rayTestPos);
                if (idx >= sampleCount) {
                    if (audioContext->rayTestLoop) {
                        audioContext->rayTestPos = 0.0;
                        idx = 0;
                    } else {
                        audioContext->rayTestActive = false;
                        break;
                    }
                }
                size_t idxNext = (idx + 1 < sampleCount) ? idx + 1 : idx;
                double frac = audioContext->rayTestPos - static_cast<double>(idx);
                float rawSample = static_cast<float>((1.0 - frac) * audioContext->rayTestBuffer[idx] +
                                                     frac * audioContext->rayTestBuffer[idxNext]);
                float sample = rawSample * audioContext->rayTestGain * speakerBlockGain;

                if (echoBufferSize > 1) {
                    float delayed = 0.0f;
                    if (echoDelaySamples > 0) {
                        size_t readIndex = (audioContext->rayEchoWriteIndex + echoBufferSize - echoDelaySamples) % echoBufferSize;
                        delayed = audioContext->rayEchoBuffer[readIndex];
                    }
                    audioContext->rayEchoBuffer[audioContext->rayEchoWriteIndex] = sample;
                    audioContext->rayEchoWriteIndex = (audioContext->rayEchoWriteIndex + 1) % echoBufferSize;
                    if (echoDelaySamples > 0 && echoGain > 0.0f) {
                        sample += delayed * echoGain;
                    }
                }

                if (hfAlpha > 0.0f) {
                    float filtered = audioContext->rayTestHfState + hfAlpha * (sample - audioContext->rayTestHfState);
                    audioContext->rayTestHfState = filtered;
                    sample = filtered;
                }

                float pan = std::clamp(audioContext->rayTestPan, -1.0f, 1.0f);
                pan = std::clamp(pan * panStrength, -1.0f, 1.0f);
                float lGain = monoScale * (1.0f - pan);
                float rGain = monoScale * (1.0f + pan);
                float sampleL = sample;
                float sampleR = sample;
                if (itdMaxSamples > 0 && itdBufferSize > 1) {
                    size_t delaySamples = static_cast<size_t>(std::abs(pan) * itdMaxSamples + 0.5f);
                    if (delaySamples > 0 && delaySamples < itdBufferSize) {
                        size_t readIndex = (audioContext->rayItdWriteIndex + itdBufferSize - delaySamples) % itdBufferSize;
                        float delayed = audioContext->rayItdBuffer[readIndex];
                        if (pan >= 0.0f) {
                            sampleL = delayed;
                        } else {
                            sampleR = delayed;
                        }
                    }
                    audioContext->rayItdBuffer[audioContext->rayItdWriteIndex] = sample;
                    audioContext->rayItdWriteIndex = (audioContext->rayItdWriteIndex + 1) % itdBufferSize;
                }
                float absSample = std::fabs(sample);
                if (absSample > speakerBlockPeak) speakerBlockPeak = absSample;
                if (outL) outL[i] += sampleL * lGain;
                if (outR) outR[i] += sampleR * rGain;

                if (micActive && micBuffer) {
                    float micSample = rawSample * micGain * speakerBlockGain;
                    if (micEchoBufferSize > 1) {
                        float delayed = 0.0f;
                        if (micEchoDelaySamples > 0) {
                            size_t readIndex = (audioContext->micRayEchoWriteIndex + micEchoBufferSize - micEchoDelaySamples) % micEchoBufferSize;
                            delayed = audioContext->micRayEchoBuffer[readIndex];
                        }
                        audioContext->micRayEchoBuffer[audioContext->micRayEchoWriteIndex] = micSample;
                        audioContext->micRayEchoWriteIndex = (audioContext->micRayEchoWriteIndex + 1) % micEchoBufferSize;
                        if (micEchoDelaySamples > 0 && micEchoGain > 0.0f) {
                            micSample += delayed * micEchoGain;
                        }
                    }
                    if (micHfAlpha > 0.0f) {
                        float filtered = audioContext->micRayHfState + micHfAlpha * (micSample - audioContext->micRayHfState);
                        audioContext->micRayHfState = filtered;
                        micSample = filtered;
                    }
                    micBuffer[i] = micSample;
                }

                audioContext->rayTestPos += step;
            }
        }
    }

    if (audioContext->headTrackActive && !audioContext->headTrackBuffer.empty()) {
        jack_default_audio_sample_t* outL = (totalOutputs > 0) ? outBuffers[0] : nullptr;
        jack_default_audio_sample_t* outR = (totalOutputs > 1) ? outBuffers[1] : nullptr;
        if (outL || outR) {
            const float monoScale = 0.5f;
            const float soundtrackGain = audioContext->soundtrackLevelGain.load(std::memory_order_relaxed);
            const double step = (audioContext->headTrackSampleRate > 0)
                ? static_cast<double>(audioContext->headTrackSampleRate) / static_cast<double>(audioContext->sampleRate)
                : 1.0;
            const size_t sampleCount = audioContext->headTrackBuffer.size();
            for (jack_nframes_t i = 0; i < nframes; ++i) {
                if (sampleCount == 0) break;
                size_t idx = static_cast<size_t>(audioContext->headTrackPos);
                if (idx >= sampleCount) {
                    if (audioContext->headTrackLoop) {
                        audioContext->headTrackPos = 0.0;
                        idx = 0;
                    } else {
                        audioContext->headTrackActive = false;
                        break;
                    }
                }
                size_t idxNext = (idx + 1 < sampleCount) ? idx + 1 : idx;
                double frac = audioContext->headTrackPos - static_cast<double>(idx);
                float sample = static_cast<float>((1.0 - frac) * audioContext->headTrackBuffer[idx] +
                                                   frac * audioContext->headTrackBuffer[idxNext]);
                sample *= audioContext->headTrackGain;
                sample *= soundtrackGain;
                float absSample = std::fabs(sample);
                if (absSample > soundtrackPeak) soundtrackPeak = absSample;
                if (outL) outL[i] += sample * monoScale;
                if (outR) outR[i] += sample * monoScale;
                audioContext->headTrackPos += step;
            }
        }
    }

    if (audioContext->gameplaySfxEnabled.load(std::memory_order_relaxed)
        && audioContext->gameplaySfxEventRing
        && !audioContext->gameplaySfxVoices.empty()) {
        GameplaySfxEvent event{};
        while (jack_ringbuffer_read_space(audioContext->gameplaySfxEventRing) >= sizeof(GameplaySfxEvent)) {
            jack_ringbuffer_read(audioContext->gameplaySfxEventRing,
                                 reinterpret_cast<char*>(&event),
                                 sizeof(GameplaySfxEvent));
            int clipIndex = static_cast<int>(event.clipIndex);
            if (clipIndex < 0 || clipIndex >= static_cast<int>(audioContext->gameplaySfxBuffers.size())) continue;
            if (audioContext->gameplaySfxBuffers[static_cast<size_t>(clipIndex)].empty()) continue;
            int voiceIndex = -1;
            for (size_t i = 0; i < audioContext->gameplaySfxVoices.size(); ++i) {
                if (!audioContext->gameplaySfxVoices[i].active) {
                    voiceIndex = static_cast<int>(i);
                    break;
                }
            }
            if (voiceIndex < 0) voiceIndex = 0; // recycle oldest slot under heavy bursts
            GameplaySfxVoice& voice = audioContext->gameplaySfxVoices[static_cast<size_t>(voiceIndex)];
            voice.clipIndex = clipIndex;
            voice.position = 0.0;
            voice.gain = std::clamp(event.gain, 0.0f, 4.0f);
            voice.active = true;
        }

        jack_default_audio_sample_t* outL = (totalOutputs > 0) ? outBuffers[0] : nullptr;
        jack_default_audio_sample_t* outR = (totalOutputs > 1) ? outBuffers[1] : nullptr;
        if (outL || outR) {
            const float monoScale = 0.5f;
            const float masterGain = audioContext->gameplaySfxMasterGain.load(std::memory_order_relaxed);
            for (auto& voice : audioContext->gameplaySfxVoices) {
                if (!voice.active) continue;
                if (voice.clipIndex < 0 || voice.clipIndex >= static_cast<int>(audioContext->gameplaySfxBuffers.size())) {
                    voice.active = false;
                    continue;
                }
                const std::vector<float>& clip = audioContext->gameplaySfxBuffers[static_cast<size_t>(voice.clipIndex)];
                if (clip.empty()) {
                    voice.active = false;
                    continue;
                }
                const uint32_t clipRate = (voice.clipIndex < static_cast<int>(audioContext->gameplaySfxSampleRates.size()))
                    ? audioContext->gameplaySfxSampleRates[static_cast<size_t>(voice.clipIndex)]
                    : 0u;
                const double step = (clipRate > 0)
                    ? static_cast<double>(clipRate) / static_cast<double>(audioContext->sampleRate)
                    : 1.0;
                for (jack_nframes_t i = 0; i < nframes; ++i) {
                    size_t idx = static_cast<size_t>(voice.position);
                    if (idx >= clip.size()) {
                        voice.active = false;
                        break;
                    }
                    size_t idxNext = (idx + 1 < clip.size()) ? (idx + 1) : idx;
                    double frac = voice.position - static_cast<double>(idx);
                    float sample = static_cast<float>((1.0 - frac) * clip[idx] + frac * clip[idxNext]);
                    sample *= voice.gain * masterGain;
                    if (outL) outL[i] += sample * monoScale;
                    if (outR) outR[i] += sample * monoScale;
                    voice.position += step;
                }
            }
        }
    }

    audioContext->chuckMainMeterLevel.store(std::clamp(chuckMainPeak, 0.0f, 1.0f), std::memory_order_relaxed);
    audioContext->soundtrackMeterLevel.store(std::clamp(soundtrackPeak, 0.0f, 1.0f), std::memory_order_relaxed);
    audioContext->speakerBlockMeterLevel.store(std::clamp(speakerBlockPeak, 0.0f, 1.0f), std::memory_order_relaxed);
    audioContext->playerHeadSpeakerMeterLevel.store(std::clamp(playerHeadSpeakerPeak, 0.0f, 1.0f), std::memory_order_relaxed);

    // DAW playback + recording
    if (audioContext->daw) {
        DawContext& daw = *audioContext->daw;
        std::lock_guard<std::mutex> lock(daw.trackMutex);
        bool playing = daw.transportPlaying.load(std::memory_order_relaxed);
        if (!playing) {
            daw.audioThreadIdle.store(true, std::memory_order_relaxed);
        } else {
            daw.audioThreadIdle.store(false, std::memory_order_relaxed);
        }

        const int busStart = audioContext->dawOutputStart;
        std::array<jack_default_audio_sample_t*, DawContext::kBusCount> busOut{};
        for (int b = 0; b < DawContext::kBusCount; ++b) {
            int idx = busStart + b;
            busOut[b] = (idx >= 0 && idx < totalOutputs) ? outBuffers[idx] : nullptr;
        }

        if (playing) {
            uint64_t playhead = daw.playheadSample.load(std::memory_order_relaxed);
            bool loopEnabled = daw.loopEnabled.load(std::memory_order_relaxed);
            uint64_t loopStart = daw.loopStartSamples;
            uint64_t loopEnd = daw.loopEndSamples;
            if (loopEnd <= loopStart) {
                loopEnabled = false;
            }
            uint64_t loopLength = loopEnabled ? (loopEnd - loopStart) : 0;
            if (loopEnabled && playhead >= loopEnd) {
                playhead = loopStart;
            }
            auto loopIndex = [&](uint64_t sample) -> uint64_t {
                if (loopEnabled && loopLength > 0 && sample >= loopEnd) {
                    return loopStart + (sample - loopStart) % loopLength;
                }
                return sample;
            };
            bool anySolo = false;
            for (const auto& track : daw.tracks) {
                if (track.solo.load(std::memory_order_relaxed)) {
                    anySolo = true;
                    break;
                }
            }
            if (midiContext) {
                std::lock_guard<std::mutex> midiLock(midiContext->trackMutex);
                for (const auto& mTrack : midiContext->tracks) {
                    if (mTrack.solo.load(std::memory_order_relaxed)) {
                        anySolo = true;
                        break;
                    }
                }
            }
            Vst3Context* vst3 = audioContext->vst3;

            int trackCount = static_cast<int>(daw.tracks.size());
            thread_local std::vector<float> clipBufferL;
            thread_local std::vector<float> clipBufferR;
            thread_local std::vector<float> clipBufferMono;
            for (int t = 0; t < trackCount; ++t) {
                DawTrack& track = daw.tracks[static_cast<size_t>(t)];
                bool solo = track.solo.load(std::memory_order_relaxed);
                bool mute = track.mute.load(std::memory_order_relaxed);
                if (anySolo && !solo) {
                    track.meterLevel.store(0.0f, std::memory_order_relaxed);
                    continue;
                }
                if (!anySolo && mute) {
                    track.meterLevel.store(0.0f, std::memory_order_relaxed);
                    continue;
                }

                int busLIndex = track.outputBusL.load(std::memory_order_relaxed);
                int busRIndex = track.outputBusR.load(std::memory_order_relaxed);
                jack_default_audio_sample_t* busLBuffer =
                    (busLIndex >= 0 && busLIndex < DawContext::kBusCount) ? busOut[busLIndex] : nullptr;
                jack_default_audio_sample_t* busRBuffer =
                    (busRIndex >= 0 && busRIndex < DawContext::kBusCount) ? busOut[busRIndex] : nullptr;
                if (!busLBuffer && !busRBuffer) continue;

                if (clipBufferL.size() < static_cast<size_t>(nframes)) {
                    clipBufferL.assign(static_cast<size_t>(nframes), 0.0f);
                } else {
                    std::fill(clipBufferL.begin(), clipBufferL.begin() + nframes, 0.0f);
                }
                if (clipBufferR.size() < static_cast<size_t>(nframes)) {
                    clipBufferR.assign(static_cast<size_t>(nframes), 0.0f);
                } else {
                    std::fill(clipBufferR.begin(), clipBufferR.begin() + nframes, 0.0f);
                }
                const auto& clips = track.clips;
                if (!clips.empty()) {
                    uint64_t windowStart = playhead;
                    uint64_t windowEnd = playhead + nframes;
                    auto renderClips = [&](uint64_t segStart, uint64_t segEnd, size_t outOffset) {
                        for (const auto& clip : clips) {
                            if (clip.length == 0) continue;
                            if (clip.audioId < 0 || clip.audioId >= static_cast<int>(daw.clipAudio.size())) continue;
                            uint64_t clipStart = clip.startSample;
                            uint64_t clipEnd = clip.startSample + clip.length;
                            if (clipEnd <= segStart || clipStart >= segEnd) continue;
                            uint64_t overlapStart = std::max(clipStart, segStart);
                            uint64_t overlapEnd = std::min(clipEnd, segEnd);
                            if (overlapEnd <= overlapStart) continue;
                            const auto& data = daw.clipAudio[clip.audioId];
                            uint64_t srcOffset = clip.sourceOffset + (overlapStart - clipStart);
                            uint64_t count = overlapEnd - overlapStart;
                            if (srcOffset >= data.left.size()) continue;
                            uint64_t maxCopy = std::min<uint64_t>(count, data.left.size() - srcOffset);
                            size_t dstBase = outOffset + static_cast<size_t>(overlapStart - segStart);
                            for (uint64_t i = 0; i < maxCopy; ++i) {
                                size_t dst = dstBase + static_cast<size_t>(i);
                                if (dst >= clipBufferL.size()) break;
                                const size_t src = static_cast<size_t>(srcOffset + i);
                                float left = data.left[src];
                                float right = (data.channels > 1 && src < data.right.size()) ? data.right[src] : left;
                                clipBufferL[dst] = left;
                                clipBufferR[dst] = right;
                            }
                        }
                    };
                    if (loopEnabled && loopLength > 0 && playhead >= loopStart && windowEnd > loopEnd) {
                        uint64_t firstEnd = loopEnd;
                        size_t firstCount = static_cast<size_t>(firstEnd - windowStart);
                        renderClips(windowStart, firstEnd, 0);
                        uint64_t remainder = windowEnd - firstEnd;
                        renderClips(loopStart, loopStart + remainder, firstCount);
                    } else {
                        renderClips(windowStart, windowEnd, 0);
                    }
                }
                float maxAbs = 0.0f;
                float gain = track.gain.load(std::memory_order_relaxed);
                const float* sourceL = clipBufferL.data();
                const float* sourceR = clipBufferR.data();
                if (vst3 && static_cast<size_t>(t) < vst3->audioTracks.size()) {
                    Vst3TrackChain& chain = vst3->audioTracks[t];
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
                        for (jack_nframes_t i = 0; i < nframes; ++i) {
                            clipBufferMono[static_cast<size_t>(i)] =
                                0.5f * (clipBufferL[static_cast<size_t>(i)] + clipBufferR[static_cast<size_t>(i)]);
                            chain.monoInput[i] = clipBufferMono[static_cast<size_t>(i)];
                        }
                        bool processedMono = Vst3SystemLogic::ProcessEffectChain(*vst3, chain,
                                                                                 chain.monoInput.data(),
                                                                                 chain.monoOutput.data(),
                                                                                 static_cast<int>(nframes),
                                                                                 static_cast<int64_t>(playhead),
                                                                                 true);
                        const float* sourceMono = processedMono ? chain.monoOutput.data() : chain.monoInput.data();
                        sourceL = sourceMono;
                        sourceR = sourceMono;
                    }
                }
                for (jack_nframes_t i = 0; i < nframes; ++i) {
                    float left = sourceL[static_cast<size_t>(i)] * gain;
                    float right = sourceR[static_cast<size_t>(i)] * gain;
                    float meterSample = 0.0f;
                    if (busLIndex == busRIndex) {
                        if (busLBuffer) {
                            float sum = left + right;
                            busLBuffer[i] += sum;
                            meterSample = std::fabs(sum);
                        }
                    } else {
                        if (busLBuffer) {
                            busLBuffer[i] += left;
                            meterSample = std::max(meterSample, std::fabs(left));
                        }
                        if (busRBuffer) {
                            busRBuffer[i] += right;
                            meterSample = std::max(meterSample, std::fabs(right));
                        }
                    }
                    if (meterSample > maxAbs) maxAbs = meterSample;
                }
                track.meterLevel.store(maxAbs, std::memory_order_relaxed);
            }

            if (midiContext) {
                std::lock_guard<std::mutex> midiLock(midiContext->trackMutex);
                int midiNote = midiContext->activeNote.load(std::memory_order_relaxed);
                float midiVelocity = midiContext->activeVelocity.load(std::memory_order_relaxed);
                if (midiVelocity <= 0.0f) midiNote = -1;
                int previewTrackIndex = midiContext->pianoRollActive
                    ? midiContext->pianoRollTrack
                    : midiContext->selectedTrackIndex;
                int midiTrackCount = static_cast<int>(midiContext->tracks.size());
                if (previewTrackIndex < 0 || previewTrackIndex >= midiTrackCount) {
                    previewTrackIndex = -1;
                }
                if (vst3 && midiContext->initialized) {
                    Vst3SystemLogic::EnsureMidiTrackCount(*vst3, midiTrackCount);
                }
                const bool transportRecording = daw.transportRecording.load(std::memory_order_relaxed);
                auto collectActiveClipNotesAtSample = [&](const MidiTrack& track,
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

                for (int midiIndex = 0; midiIndex < midiTrackCount; ++midiIndex) {
                    auto& mTrack = midiContext->tracks[static_cast<size_t>(midiIndex)];
                    float midiMaxAbs = 0.0f;
                    float midiRecordMax = 0.0f;
                    bool solo = mTrack.solo.load(std::memory_order_relaxed);
                    bool mute = mTrack.mute.load(std::memory_order_relaxed);
                    bool midiAllowed = (!anySolo && !mute) || (anySolo && solo);
                    bool recordingTrack = transportRecording
                        && mTrack.recordEnabled.load(std::memory_order_relaxed);

                    Vst3TrackChain* midiChain = nullptr;
                    Vst3Plugin* midiInstrument = nullptr;
                    std::array<float, 128>* lastHeldVelocities = nullptr;
                    if (vst3 && midiIndex >= 0 && midiIndex < static_cast<int>(vst3->midiTracks.size())) {
                        midiChain = &vst3->midiTracks[static_cast<size_t>(midiIndex)];
                        if (midiIndex < static_cast<int>(vst3->midiInstruments.size())) {
                            midiInstrument = vst3->midiInstruments[static_cast<size_t>(midiIndex)];
                        }
                        if (midiIndex < static_cast<int>(vst3->midiHeldVelocities.size())) {
                            lastHeldVelocities = &vst3->midiHeldVelocities[static_cast<size_t>(midiIndex)];
                        }
                    }

                    const std::vector<float>& data = mTrack.audio;
                    const float* playbackSource = nullptr;
                    const float* liveSource = nullptr;
                    const float* recordSource = nullptr;

                    if (midiChain && midiChain->monoInput.size() >= static_cast<size_t>(nframes)) {
                        for (jack_nframes_t i = 0; i < nframes; ++i) {
                            size_t idx = static_cast<size_t>(loopIndex(playhead + i));
                            midiChain->monoInput[i] = (idx < data.size()) ? data[idx] : 0.0f;
                        }
                        bool processed = Vst3SystemLogic::ProcessEffectChain(*vst3, *midiChain,
                                                                             midiChain->monoInput.data(),
                                                                             midiChain->monoOutput.data(),
                                                                             static_cast<int>(nframes),
                                                                             static_cast<int64_t>(playhead),
                                                                             true);
                        playbackSource = processed ? midiChain->monoOutput.data() : midiChain->monoInput.data();
                    }

                    int liveNote = (midiIndex == previewTrackIndex) ? midiNote : -1;
                    float liveVelocity = (liveNote >= 0) ? midiVelocity : 0.0f;
                    std::array<float, 128> desiredHeldVelocities{};
                    collectActiveClipNotesAtSample(mTrack, loopIndex(playhead), desiredHeldVelocities);
                    if (liveNote >= 0) {
                        float liveVel = std::clamp(liveVelocity > 0.0f ? liveVelocity : 0.8f, 0.0f, 1.0f);
                        desiredHeldVelocities[static_cast<size_t>(liveNote)] = std::max(
                            desiredHeldVelocities[static_cast<size_t>(liveNote)],
                            liveVel
                        );
                    }
                    bool anyDesired = false;
                    for (float vel : desiredHeldVelocities) {
                        if (vel > 0.0001f) {
                            anyDesired = true;
                            break;
                        }
                    }
                    bool anyLastHeld = false;
                    if (lastHeldVelocities) {
                        for (float vel : *lastHeldVelocities) {
                            if (vel > 0.0001f) {
                                anyLastHeld = true;
                                break;
                            }
                        }
                    }
                    bool processLiveInstrument = midiChain && midiInstrument && lastHeldVelocities
                        && midiChain->monoInput.size() >= static_cast<size_t>(nframes)
                        && midiChain->monoOutput.size() >= static_cast<size_t>(nframes)
                        && (anyDesired || anyLastHeld || recordingTrack);
                    if (processLiveInstrument) {
                        bool generated = Vst3SystemLogic::ProcessInstrument(*vst3, *midiChain, *midiInstrument,
                                                                            midiChain->monoInput.data(),
                                                                            static_cast<int>(nframes),
                                                                            static_cast<int64_t>(playhead),
                                                                            true,
                                                                            desiredHeldVelocities,
                                                                            *lastHeldVelocities);
                        if (generated) {
                            bool fxProcessed = Vst3SystemLogic::ProcessEffectChain(*vst3, *midiChain,
                                                                                   midiChain->monoInput.data(),
                                                                                   midiChain->monoOutput.data(),
                                                                                   static_cast<int>(nframes),
                                                                                   static_cast<int64_t>(playhead),
                                                                                   true);
                            liveSource = fxProcessed ? midiChain->monoOutput.data() : midiChain->monoInput.data();
                            if (recordingTrack && liveNote >= 0) {
                                recordSource = liveSource;
                                for (jack_nframes_t i = 0; i < nframes; ++i) {
                                    float absSample = std::fabs(recordSource[i]);
                                    if (absSample > midiRecordMax) midiRecordMax = absSample;
                                }
                            }
                        }
                    }

                    if (midiAllowed) {
                        int busL = mTrack.outputBusL.load(std::memory_order_relaxed);
                        int busR = mTrack.outputBusR.load(std::memory_order_relaxed);
                        jack_default_audio_sample_t* busLBuffer =
                            (busL >= 0 && busL < DawContext::kBusCount) ? busOut[busL] : nullptr;
                        jack_default_audio_sample_t* busRBuffer =
                            (busR >= 0 && busR < DawContext::kBusCount) ? busOut[busR] : nullptr;
                        if (busLBuffer || busRBuffer) {
                            float gain = mTrack.gain.load(std::memory_order_relaxed);
                            for (jack_nframes_t i = 0; i < nframes; ++i) {
                                float sample = 0.0f;
                                if (playbackSource) {
                                    sample += playbackSource[i];
                                } else {
                                    size_t idx = static_cast<size_t>(loopIndex(playhead + i));
                                    sample += (idx < data.size()) ? data[idx] : 0.0f;
                                }
                                if (liveSource) {
                                    sample += liveSource[i];
                                }
                                float outL = sample * gain;
                                float outR = outL;
                                float meterSample = 0.0f;
                                if (busL == busR) {
                                    if (busLBuffer) {
                                        float sum = outL + outR;
                                        busLBuffer[i] += sum;
                                        meterSample = std::fabs(sum);
                                    }
                                } else {
                                    if (busLBuffer) {
                                        busLBuffer[i] += outL;
                                        meterSample = std::max(meterSample, std::fabs(outL));
                                    }
                                    if (busRBuffer) {
                                        busRBuffer[i] += outR;
                                        meterSample = std::max(meterSample, std::fabs(outR));
                                    }
                                }
                                if (meterSample > midiMaxAbs) midiMaxAbs = meterSample;
                            }
                        }
                    }

                    if (recordingTrack) {
                        if (recordSource && mTrack.recordRing) {
                            size_t writeSpace = jack_ringbuffer_write_space(mTrack.recordRing);
                            size_t framesToWrite = std::min<size_t>(nframes, writeSpace / sizeof(float));
                            if (framesToWrite > 0) {
                                jack_ringbuffer_write(mTrack.recordRing,
                                                      reinterpret_cast<const char*>(recordSource),
                                                      framesToWrite * sizeof(float));
                            }
                        }
                    }

                    float meterValue = std::max(midiMaxAbs, midiRecordMax);
                    mTrack.meterLevel.store(meterValue, std::memory_order_relaxed);
                }
            }
            if (daw.metronomeEnabled.load(std::memory_order_relaxed) && daw.metronomeLoaded
                && !daw.metronomeSamples.empty() && daw.sampleRate > 0.0f) {
                double bpm = daw.bpm.load(std::memory_order_relaxed);
                if (bpm <= 0.0) bpm = 120.0;
                double beatSamples = (60.0 / bpm) * daw.sampleRate;
                if (beatSamples < 1.0) beatSamples = 1.0;
                auto nextBeatAtOrAfter = [&](uint64_t sample) -> uint64_t {
                    double div = static_cast<double>(sample) / beatSamples;
                    uint64_t beatIndex = static_cast<uint64_t>(std::ceil(div));
                    return static_cast<uint64_t>(std::llround(beatIndex * beatSamples));
                };
                if (!daw.metronomePrimed || daw.metronomeNextSample < playhead) {
                    daw.metronomeNextSample = nextBeatAtOrAfter(playhead);
                    daw.metronomePrimed = true;
                }
                double step = daw.metronomeSampleStep;
                const auto& click = daw.metronomeSamples;
                size_t clickCount = click.size();
                bool loopHasBeat = true;
                if (loopEnabled && loopLength > 0) {
                    loopHasBeat = nextBeatAtOrAfter(loopStart) < loopEnd;
                }
                uint64_t prevSample = playhead;
                bool prevLoopPhase = false;
                for (jack_nframes_t i = 0; i < nframes; ++i) {
                    uint64_t rawSample = playhead + i;
                    bool loopPhase = loopEnabled && loopLength > 0 && rawSample >= loopStart;
                    uint64_t currentSample = loopPhase ? loopIndex(rawSample) : rawSample;
                    if (loopPhase && !prevLoopPhase) {
                        daw.metronomeNextSample = nextBeatAtOrAfter(currentSample);
                    }
                    if (loopPhase && currentSample < prevSample) {
                        daw.metronomeNextSample = nextBeatAtOrAfter(currentSample);
                    }
                    prevSample = currentSample;
                    prevLoopPhase = loopPhase;
                    if (loopPhase) {
                        if (!loopHasBeat) {
                            daw.metronomeSampleActive = false;
                            continue;
                        }
                        if (daw.metronomeNextSample < loopStart || daw.metronomeNextSample >= loopEnd) {
                            daw.metronomeNextSample = nextBeatAtOrAfter(currentSample);
                            if (daw.metronomeNextSample < loopStart || daw.metronomeNextSample >= loopEnd) {
                                daw.metronomeNextSample = loopEnd + 1;
                            }
                        }
                    }
                    while (currentSample >= daw.metronomeNextSample) {
                        if (loopPhase && daw.metronomeNextSample > loopEnd) {
                            daw.metronomeSampleActive = false;
                            break;
                        }
                        daw.metronomeSamplePos = 0.0;
                        daw.metronomeSampleActive = true;
                        daw.metronomeNextSample += static_cast<uint64_t>(beatSamples);
                        if (loopPhase && daw.metronomeNextSample >= loopEnd) {
                            daw.metronomeNextSample = loopEnd + 1;
                        }
                        if (daw.metronomeNextSample == currentSample) break;
                    }
                    if (daw.metronomeSampleActive) {
                        size_t idx = static_cast<size_t>(daw.metronomeSamplePos);
                        if (idx >= clickCount) {
                            daw.metronomeSampleActive = false;
                        } else {
                            size_t idxNext = (idx + 1 < clickCount) ? idx + 1 : idx;
                            double frac = daw.metronomeSamplePos - static_cast<double>(idx);
                            float sample = static_cast<float>((1.0 - frac) * click[idx] + frac * click[idxNext]);
                            if (busOut[0]) busOut[0][i] += sample;
                            if (busOut[3]) busOut[3][i] += sample;
                            daw.metronomeSamplePos += step;
                            if (daw.metronomeSamplePos >= static_cast<double>(clickCount)) {
                                daw.metronomeSampleActive = false;
                            }
                        }
                    }
                }
            }
            uint64_t advanced = playhead + nframes;
            if (loopEnabled && loopLength > 0) {
                if (playhead >= loopStart || advanced >= loopEnd) {
                    advanced = loopStart + (advanced - loopStart) % loopLength;
                }
            }
            daw.playheadSample.store(advanced, std::memory_order_relaxed);
            if (vst3) {
                vst3->continuousSamples += static_cast<int64_t>(nframes);
            }
        }
        if (!playing) {
            daw.metronomePrimed = false;
            daw.metronomeSampleActive = false;
            for (auto& track : daw.tracks) {
                track.meterLevel.store(0.0f, std::memory_order_relaxed);
            }
            if (midiContext) {
                std::lock_guard<std::mutex> midiLock(midiContext->trackMutex);
                bool anySolo = false;
                for (const auto& track : daw.tracks) {
                    if (track.solo.load(std::memory_order_relaxed)) {
                        anySolo = true;
                        break;
                    }
                }
                if (!anySolo) {
                    for (const auto& mTrack : midiContext->tracks) {
                        if (mTrack.solo.load(std::memory_order_relaxed)) {
                            anySolo = true;
                            break;
                        }
                    }
                }
                int midiNote = midiContext->activeNote.load(std::memory_order_relaxed);
                float midiVelocity = midiContext->activeVelocity.load(std::memory_order_relaxed);
                if (midiVelocity <= 0.0f) midiNote = -1;
                int previewTrackIndex = midiContext->pianoRollActive
                    ? midiContext->pianoRollTrack
                    : midiContext->selectedTrackIndex;
                int midiTrackCount = static_cast<int>(midiContext->tracks.size());
                if (previewTrackIndex < 0 || previewTrackIndex >= midiTrackCount) {
                    previewTrackIndex = -1;
                }
                Vst3Context* vst3 = audioContext->vst3;
                if (vst3 && midiContext->initialized) {
                    Vst3SystemLogic::EnsureMidiTrackCount(*vst3, midiTrackCount);
                }
                for (auto& mTrack : midiContext->tracks) {
                    mTrack.meterLevel.store(0.0f, std::memory_order_relaxed);
                }
                if (vst3) {
                    uint64_t playhead = daw.playheadSample.load(std::memory_order_relaxed);
                    for (int midiIndex = 0; midiIndex < midiTrackCount; ++midiIndex) {
                        auto& mTrack = midiContext->tracks[static_cast<size_t>(midiIndex)];
                        bool solo = mTrack.solo.load(std::memory_order_relaxed);
                        bool mute = mTrack.mute.load(std::memory_order_relaxed);
                        bool midiAllowed = (!anySolo && !mute) || (anySolo && solo);

                        Vst3TrackChain* midiChain = nullptr;
                        Vst3Plugin* midiInstrument = nullptr;
                        std::array<float, 128>* lastHeldVelocities = nullptr;
                        if (midiIndex >= 0 && midiIndex < static_cast<int>(vst3->midiTracks.size())) {
                            midiChain = &vst3->midiTracks[static_cast<size_t>(midiIndex)];
                            if (midiIndex < static_cast<int>(vst3->midiInstruments.size())) {
                                midiInstrument = vst3->midiInstruments[static_cast<size_t>(midiIndex)];
                            }
                            if (midiIndex < static_cast<int>(vst3->midiHeldVelocities.size())) {
                                lastHeldVelocities = &vst3->midiHeldVelocities[static_cast<size_t>(midiIndex)];
                            }
                        }
                        if (!midiChain || !midiInstrument || !lastHeldVelocities) {
                            continue;
                        }
                        if (midiChain->monoInput.size() < static_cast<size_t>(nframes)
                            || midiChain->monoOutput.size() < static_cast<size_t>(nframes)) {
                            continue;
                        }

                        int liveNote = (midiIndex == previewTrackIndex) ? midiNote : -1;
                        float liveVelocity = (liveNote >= 0) ? midiVelocity : 0.0f;
                        std::array<float, 128> desiredHeldVelocities{};
                        if (liveNote >= 0) {
                            desiredHeldVelocities[static_cast<size_t>(liveNote)] = std::clamp(
                                liveVelocity > 0.0f ? liveVelocity : 0.8f,
                                0.0f,
                                1.0f
                            );
                        }
                        bool anyDesired = false;
                        for (float vel : desiredHeldVelocities) {
                            if (vel > 0.0001f) {
                                anyDesired = true;
                                break;
                            }
                        }
                        bool anyLastHeld = false;
                        for (float vel : *lastHeldVelocities) {
                            if (vel > 0.0001f) {
                                anyLastHeld = true;
                                break;
                            }
                        }
                        if (!anyDesired && !anyLastHeld) {
                            continue;
                        }

                        std::fill(midiChain->monoInput.begin(), midiChain->monoInput.begin() + nframes, 0.0f);
                        bool generated = Vst3SystemLogic::ProcessInstrument(*vst3, *midiChain, *midiInstrument,
                                                                            midiChain->monoInput.data(),
                                                                            static_cast<int>(nframes),
                                                                            static_cast<int64_t>(playhead),
                                                                            false,
                                                                            desiredHeldVelocities,
                                                                            *lastHeldVelocities);
                        if (!generated) continue;
                        bool fxProcessed = Vst3SystemLogic::ProcessEffectChain(*vst3, *midiChain,
                                                                               midiChain->monoInput.data(),
                                                                               midiChain->monoOutput.data(),
                                                                               static_cast<int>(nframes),
                                                                               static_cast<int64_t>(playhead),
                                                                               false);
                        const float* source = fxProcessed ? midiChain->monoOutput.data() : midiChain->monoInput.data();
                        float maxAbs = 0.0f;
                        if (midiAllowed) {
                            int busL = mTrack.outputBusL.load(std::memory_order_relaxed);
                            int busR = mTrack.outputBusR.load(std::memory_order_relaxed);
                            jack_default_audio_sample_t* busLBuffer =
                                (busL >= 0 && busL < DawContext::kBusCount) ? busOut[busL] : nullptr;
                            jack_default_audio_sample_t* busRBuffer =
                                (busR >= 0 && busR < DawContext::kBusCount) ? busOut[busR] : nullptr;
                            if (busLBuffer || busRBuffer) {
                                float gain = mTrack.gain.load(std::memory_order_relaxed);
                                for (jack_nframes_t i = 0; i < nframes; ++i) {
                                    float outL = source[i] * gain;
                                    float outR = outL;
                                    float meterSample = 0.0f;
                                    if (busL == busR) {
                                        if (busLBuffer) {
                                            float sum = outL + outR;
                                            busLBuffer[i] += sum;
                                            meterSample = std::fabs(sum);
                                        }
                                    } else {
                                        if (busLBuffer) {
                                            busLBuffer[i] += outL;
                                            meterSample = std::max(meterSample, std::fabs(outL));
                                        }
                                        if (busRBuffer) {
                                            busRBuffer[i] += outR;
                                            meterSample = std::max(meterSample, std::fabs(outR));
                                        }
                                    }
                                    if (meterSample > maxAbs) maxAbs = meterSample;
                                }
                            }
                        }
                        mTrack.meterLevel.store(maxAbs, std::memory_order_relaxed);
                    }
                }
            }
        }

        jack_default_audio_sample_t* outL = (totalOutputs > 0) ? outBuffers[0] : nullptr;
        jack_default_audio_sample_t* outR = (totalOutputs > 1) ? outBuffers[1] : nullptr;
        if (outL || outR) {
            jack_default_audio_sample_t* busL = busOut[0];
            jack_default_audio_sample_t* busS = busOut[1];
            jack_default_audio_sample_t* busFF = busOut[2];
            jack_default_audio_sample_t* busR = busOut[3];
            for (jack_nframes_t i = 0; i < nframes; ++i) {
                float l = busL ? busL[i] : 0.0f;
                float s = busS ? busS[i] : 0.0f;
                float ff = busFF ? busFF[i] : 0.0f;
                float r = busR ? busR[i] : 0.0f;
                float center = 0.5f * (s + ff);
                if (outL) outL[i] += l + center;
                if (outR) outR[i] += r + center;
            }
        }

        for (int b = 0; b < DawContext::kBusCount; ++b) {
            float maxAbs = 0.0f;
            jack_default_audio_sample_t* busBuffer = busOut[b];
            if (busBuffer) {
                for (jack_nframes_t i = 0; i < nframes; ++i) {
                    float absSample = std::fabs(busBuffer[i]);
                    if (absSample > maxAbs) maxAbs = absSample;
                }
            }
            daw.masterBusLevels[b].store(maxAbs, std::memory_order_relaxed);
        }

        if (daw.transportRecording.load(std::memory_order_relaxed)) {
            constexpr int kMaxInputs = 32;
            std::array<jack_default_audio_sample_t*, kMaxInputs> inBuffers{};
            const int totalInputs = std::min<int>(static_cast<int>(audioContext->input_ports.size()), kMaxInputs);
            for (int ch = 0; ch < totalInputs; ++ch) {
                jack_port_t* port = audioContext->input_ports[ch];
                if (!port) continue;
                inBuffers[ch] = (jack_default_audio_sample_t*)jack_port_get_buffer(port, nframes);
            }
            const float* micBuf = (needMicBuffer && !audioContext->micCaptureBuffer.empty())
                ? audioContext->micCaptureBuffer.data()
                : nullptr;

            int trackCount = static_cast<int>(daw.tracks.size());
            for (int t = 0; t < trackCount; ++t) {
                DawTrack& track = daw.tracks[static_cast<size_t>(t)];
                if (!track.recordEnabled.load(std::memory_order_relaxed)) continue;
                const float* leftSource = nullptr;
                const float* rightSource = nullptr;
                if (track.useVirtualInput.load(std::memory_order_relaxed)) {
                    leftSource = micBuf;
                    rightSource = micBuf;
                } else if (track.stereoInputPair12) {
                    // Stable JACK path: read from this client's owned input buffers.
                    if (totalInputs >= 2) {
                        leftSource = inBuffers[0];
                        rightSource = inBuffers[1];
                    } else if (totalInputs >= 1) {
                        leftSource = inBuffers[0];
                        rightSource = inBuffers[0];
                    }
                } else {
                    int inputIndex = track.inputIndex;
                    if (inputIndex >= 0 && inputIndex < totalInputs) {
                        leftSource = inBuffers[inputIndex];
                        rightSource = leftSource;
                    }
                }
                if (!leftSource || !track.recordRing) continue;
                if (!rightSource) {
                    rightSource = leftSource;
                }
                size_t writeSpaceLeft = jack_ringbuffer_write_space(track.recordRing);
                size_t framesToWriteLeft = std::min<size_t>(nframes, writeSpaceLeft / sizeof(float));
                if (framesToWriteLeft > 0) {
                    jack_ringbuffer_write(track.recordRing,
                                          reinterpret_cast<const char*>(leftSource),
                                          framesToWriteLeft * sizeof(float));
                }
                if (track.recordRingRight && rightSource) {
                    size_t writeSpaceRight = jack_ringbuffer_write_space(track.recordRingRight);
                    size_t framesToWriteRight = std::min<size_t>(nframes, writeSpaceRight / sizeof(float));
                    if (framesToWriteRight > 0) {
                        jack_ringbuffer_write(track.recordRingRight,
                                              reinterpret_cast<const char*>(rightSource),
                                              framesToWriteRight * sizeof(float));
                    }
                }
            }
        }
    }
    // push first sample to ringbuffer for visualization
    if (ring_sample_set && audioContext->ring_buffer) {
        if (jack_ringbuffer_write_space(audioContext->ring_buffer) >= sizeof(float)) {
            jack_ringbuffer_write(audioContext->ring_buffer, (char*)&ring_sample, sizeof(float));
        }
    }
    return 0;
}

void jack_shutdown_callback(void* arg) {
    std::cerr << "JACK server has shut down." << std::endl;
}

// --- SYSTEM LOGIC ---
namespace AudioSystemLogic {
    void InitializeAudio(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.audio) { std::cerr << "FATAL: AudioContext not available." << std::endl; exit(-1); }
        AudioContext& audio = *baseSystem.audio;
        srand(time(NULL));
        const char* client_name = "cardinal_eds";
        jack_status_t status;
        audio.client = jack_client_open(client_name, JackNullOption, &status);
        if (audio.client == nullptr) { std::cerr << "FATAL: jack_client_open() failed." << std::endl; exit(1); }
        jack_set_process_callback(audio.client, jack_process_callback, &audio);
        jack_on_shutdown(audio.client, jack_shutdown_callback, &audio);
        audio.ring_buffer = jack_ringbuffer_create(2048 * sizeof(float));
        jack_ringbuffer_mlock(audio.ring_buffer);
        audio.gameplaySfxEventRing = jack_ringbuffer_create(sizeof(GameplaySfxEvent) * 256);
        if (audio.gameplaySfxEventRing) {
            jack_ringbuffer_mlock(audio.gameplaySfxEventRing);
        }
        audio.output_ports.clear();
        audio.input_ports.clear();
        for (int ch = 0; ch < audio.jackOutputChannels; ++ch) {
            std::string name;
            if (ch >= audio.dawOutputStart) {
                int dawIndex = ch - audio.dawOutputStart;
                if (dawIndex == 0) {
                    name = "daw_L";
                } else if (dawIndex == 1) {
                    name = "daw_S";
                } else if (dawIndex == 2) {
                    name = "daw_FF";
                } else if (dawIndex == 3) {
                    name = "daw_R";
                } else {
                    name = "output_" + std::to_string(ch + 1);
                }
            } else {
                name = "output_" + std::to_string(ch + 1);
            }
            jack_port_t* p = jack_port_register(audio.client, name.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
            audio.output_ports.push_back(p);
        }
        for (int ch = 0; ch < audio.jackInputChannels; ++ch) {
            std::string name = "input_" + std::to_string(ch + 1);
            jack_port_t* p = jack_port_register(audio.client, name.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
            audio.input_ports.push_back(p);
        }
        audio.midi_input_ports.clear();
        if (jack_activate(audio.client)) { std::cerr << "FATAL: Cannot activate client." << std::endl; exit(1); }
        // Auto-connect outputs to physical playback ports.
        if (const char** playbackPorts = jack_get_ports(audio.client, nullptr, JACK_DEFAULT_AUDIO_TYPE,
                                                        JackPortIsInput | JackPortIsPhysical)) {
            for (size_t i = 0; i < audio.output_ports.size() && playbackPorts[i] && i < 2; ++i) {
                if (!audio.output_ports[i]) continue;
                jack_connect(audio.client, jack_port_name(audio.output_ports[i]), playbackPorts[i]);
            }
            jack_free(playbackPorts);
        }
        // Auto-connect physical capture ports to DAW inputs.
        audio.physicalInputPorts.clear();
        if (const char** capturePorts = jack_get_ports(audio.client, nullptr, JACK_DEFAULT_AUDIO_TYPE,
                                                       JackPortIsOutput | JackPortIsPhysical)) {
            for (size_t i = 0; capturePorts[i]; ++i) {
                audio.physicalInputPorts.emplace_back(capturePorts[i]);
            }
            for (size_t i = 0; i < audio.input_ports.size() && i < audio.physicalInputPorts.size(); ++i) {
                if (!audio.input_ports[i]) continue;
                jack_connect(audio.client, audio.physicalInputPorts[i].c_str(), jack_port_name(audio.input_ports[i]));
            }
            jack_free(capturePorts);
        }
        std::cout << "Audio I/O System Initialized." << std::endl;

        if (!audio.physicalInputPorts.empty()) {
            std::cout << "JACK physical inputs:" << std::endl;
            for (const auto& port : audio.physicalInputPorts) {
                std::cout << "  " << port << std::endl;
            }
        }

        // Initialize ChucK engine to render via JACK
        const t_CKINT sampleRate = static_cast<t_CKINT>(jack_get_sample_rate(audio.client));
        const t_CKINT bufferFrames = static_cast<t_CKINT>(jack_get_buffer_size(audio.client));
        audio.sampleRate = static_cast<float>(sampleRate);
        audio.chuckBufferFrames = bufferFrames;
        audio.chuckInputChannels = 0;
        audio.chuckOutputChannels = 12; // multi-channel for routing
        audio.chuck = new ChucK();
        audio.chuck->setParam(CHUCK_PARAM_SAMPLE_RATE, sampleRate);
        audio.chuck->setParam(CHUCK_PARAM_INPUT_CHANNELS, audio.chuckInputChannels);
        audio.chuck->setParam(CHUCK_PARAM_OUTPUT_CHANNELS, audio.chuckOutputChannels);
        // Keep VM alive even when no shreds are currently active; prevents startup race-to-halt.
        audio.chuck->setParam(CHUCK_PARAM_VM_HALT, FALSE);
        audio.chuck->setParam(CHUCK_PARAM_IS_REALTIME_AUDIO_HINT, TRUE);
        // Allocate buffers expected by ChucK::run (input optional, output will be provided by JACK)
        if (audio.chuckInputChannels > 0) {
            audio.chuckInput = new SAMPLE[bufferFrames * audio.chuckInputChannels]();
        }
        audio.channelGains.assign(audio.chuckOutputChannels, 0.0f);
        audio.channelPans.assign(audio.chuckOutputChannels, 0.0f);
        audio.rayEchoChannel = audio.chuckNoiseChannel;
        const float maxEchoSeconds = 2.0f;
        size_t echoSamples = static_cast<size_t>(audio.sampleRate * maxEchoSeconds) + 1;
        audio.rayEchoBuffer.assign(echoSamples, 0.0f);
        audio.rayEchoWriteIndex = 0;
        audio.rayHfChannel = -1;
        audio.rayHfAlpha = 0.0f;
        audio.rayHfState = 0.0f;
        audio.rayTestHfState = 0.0f;
        audio.headRayActive = false;
        audio.headRayGain = 0.0f;
        audio.headRayPan = 0.0f;
        audio.headRayHfAlpha = 0.0f;
        audio.headRayHfState = 0.0f;
        audio.headUnderwaterMix.store(0.0f, std::memory_order_relaxed);
        audio.headUnderwaterLowpassHz.store(500.0f, std::memory_order_relaxed);
        audio.headUnderwaterLowpassStrength.store(1.0f, std::memory_order_relaxed);
        audio.headUnderwaterLpState = 0.0f;
        audio.headRayEchoDelaySeconds = 0.0f;
        audio.headRayEchoGain = 0.0f;
        audio.headRayEchoBuffer.assign(echoSamples, 0.0f);
        audio.headRayEchoWriteIndex = 0;
        audio.micRayActive = false;
        audio.micRayGain = 0.0f;
        audio.micRayHfAlpha = 0.0f;
        audio.micRayHfState = 0.0f;
        audio.micRayEchoDelaySeconds = 0.0f;
        audio.micRayEchoGain = 0.0f;
        audio.micRayEchoBuffer.assign(echoSamples, 0.0f);
        audio.micRayEchoWriteIndex = 0;
        audio.micCaptureBuffer.assign(static_cast<size_t>(bufferFrames), 0.0f);
        audio.rayPanStrength = 0.35f;
        audio.rayItdMaxMs = 0.5f;
        audio.rayItdChannel = -1;
        size_t itdSamples = static_cast<size_t>(audio.sampleRate * (audio.rayItdMaxMs / 1000.0f)) + 4;
        if (itdSamples < 1) itdSamples = 1;
        audio.rayItdBuffer.assign(itdSamples, 0.0f);
        audio.rayItdWriteIndex = 0;
        audio.headRayItdBuffer.assign(itdSamples, 0.0f);
        audio.headRayItdWriteIndex = 0;
        audio.rayTestBuffer.clear();
        audio.rayTestSampleRate = 0;
        audio.rayTestPos = 0.0;
        audio.rayTestGain = 0.0f;
        audio.rayTestPan = 0.0f;
        audio.rayTestActive = false;
        audio.rayTestLoop = true;
        audio.headTrackBuffer.clear();
        audio.headTrackSampleRate = 0;
        audio.headTrackPos = 0.0;
        audio.headTrackGain = 1.0f;
        audio.headTrackActive = false;
        audio.headTrackLoop = true;
        audio.chuckMainLevelGain.store(0.0f, std::memory_order_relaxed);
        audio.soundtrackLevelGain.store(0.0f, std::memory_order_relaxed);
        audio.speakerBlockLevelGain.store(0.0f, std::memory_order_relaxed);
        audio.playerHeadSpeakerLevelGain.store(1.0f, std::memory_order_relaxed);
        audio.chuckMainMeterLevel.store(0.0f, std::memory_order_relaxed);
        audio.soundtrackMeterLevel.store(0.0f, std::memory_order_relaxed);
        audio.speakerBlockMeterLevel.store(0.0f, std::memory_order_relaxed);
        audio.playerHeadSpeakerMeterLevel.store(0.0f, std::memory_order_relaxed);
        audio.chuckMainActiveShredCount.store(0, std::memory_order_relaxed);
        audio.chuckMainHasActiveShreds.store(false, std::memory_order_relaxed);
        audio.chuckHeadActiveShredCount.store(0, std::memory_order_relaxed);
        audio.chuckHeadHasActiveShreds.store(false, std::memory_order_relaxed);
        preloadGameplaySfx(baseSystem, audio);
        // Keep default ChucK stereo/mono dac routing audible.
        if (audio.chuckMainChannel >= 0 && audio.chuckMainChannel < audio.chuckOutputChannels) {
            audio.channelGains[audio.chuckMainChannel] = 1.0f;
        }
        if (audio.chuckOutputChannels > 0) {
            audio.channelGains[0] = 1.0f;
        }
        if (audio.chuckOutputChannels > 1) {
            audio.channelGains[1] = 1.0f;
        }
        audio.chuckInterleavedBuffer.assign(audio.chuckOutputChannels * bufferFrames, 0.0f);
        audio.chuck->init();
        audio.chuck->start();
        audio.chuckRunning = true;
        audio.chuckMainCompileRequested = true; // compile default on next update
        audio.chuckHeadCompileRequested = true; // compile player-head source script on next update
    }

    bool TriggerGameplaySfx(BaseSystem& baseSystem, const std::string& cueName, float gain) {
        if (!baseSystem.audio) return false;
        AudioContext& audio = *baseSystem.audio;
        if (!audio.gameplaySfxEnabled.load(std::memory_order_relaxed)) return false;
        if (!audio.gameplaySfxEventRing) return false;
        int clipIndex = findGameplaySfxClipIndex(audio, cueName);
        if (clipIndex < 0 || clipIndex >= static_cast<int>(audio.gameplaySfxBuffers.size())) return false;
        if (audio.gameplaySfxBuffers[static_cast<size_t>(clipIndex)].empty()) return false;
        GameplaySfxEvent ev;
        ev.clipIndex = static_cast<uint16_t>(clipIndex);
        ev.gain = std::clamp(gain, 0.0f, 4.0f);
        if (jack_ringbuffer_write_space(audio.gameplaySfxEventRing) < sizeof(GameplaySfxEvent)) {
            return false;
        }
        jack_ringbuffer_write(audio.gameplaySfxEventRing,
                              reinterpret_cast<const char*>(&ev),
                              sizeof(GameplaySfxEvent));
        return true;
    }

    void CleanupAudio(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.audio || !baseSystem.audio->client) return;
        jack_deactivate(baseSystem.audio->client);
        jack_client_close(baseSystem.audio->client);
        baseSystem.audio->output_ports.clear();
        baseSystem.audio->input_ports.clear();
        if (baseSystem.audio->ring_buffer) {
            jack_ringbuffer_free(baseSystem.audio->ring_buffer);
            baseSystem.audio->ring_buffer = nullptr;
        }
        if (baseSystem.audio->gameplaySfxEventRing) {
            jack_ringbuffer_free(baseSystem.audio->gameplaySfxEventRing);
            baseSystem.audio->gameplaySfxEventRing = nullptr;
        }
        if (baseSystem.audio->chuckInput) {
            delete[] baseSystem.audio->chuckInput;
            baseSystem.audio->chuckInput = nullptr;
        }
        baseSystem.audio->chuckRunning = false;
        if (baseSystem.audio->chuck) {
            delete baseSystem.audio->chuck;
            baseSystem.audio->chuck = nullptr;
        }
        baseSystem.audio->gameplaySfxNames.clear();
        baseSystem.audio->gameplaySfxBuffers.clear();
        baseSystem.audio->gameplaySfxSampleRates.clear();
        baseSystem.audio->gameplaySfxVoices.clear();
        std::cout << "Audio I/O System Cleaned Up." << std::endl;
    }
}
