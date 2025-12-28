#pragma once

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <string.h>
#include <thread>
#include "chuck.h"

// Forward declarations
struct AudioContext;
namespace PinkNoiseSystemLogic {
    // This function must be declared so the audio callback can call it.
    // It's defined in PinkNoiseSystem.cpp.
    float generate_filtered_pink_noise(AudioContext*);
}

// --- JACK CALLBACKS ---
int jack_process_callback(jack_nframes_t nframes, void* arg) {
    auto* audioContext = static_cast<AudioContext*>(arg);
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

    // Mix ChucK channels to dual-mono on output_1/2 (keep visualizer sample on ch 0)
    float ring_sample = 0.0f;
    bool ring_sample_set = false;
    if (audioContext->channelGains.size() < static_cast<size_t>(chuckChannels)) {
        audioContext->channelGains.assign(chuckChannels, 1.0f);
    }
    jack_default_audio_sample_t* chuckOutL = (totalOutputs > 0) ? outBuffers[0] : nullptr;
    jack_default_audio_sample_t* chuckOutR = (totalOutputs > 1) ? outBuffers[1] : nullptr;
    if (chuckOutL || chuckOutR) {
        const float monoScale = 0.5f;
        for (jack_nframes_t i = 0; i < nframes; ++i) {
            float monoSample = 0.0f;
            for (int ch = 0; ch < chuckChannels; ++ch) {
                float chGain = (ch < static_cast<int>(audioContext->channelGains.size())) ? audioContext->channelGains[ch] : 1.0f;
                float sample = audioContext->chuckInterleavedBuffer[i * chuckChannels + ch] * chGain;
                monoSample += sample;
                if (!ring_sample_set && ch == 0 && i == 0) {
                    ring_sample = sample;
                    ring_sample_set = true;
                }
            }
            float outSample = monoSample * monoScale;
            if (chuckOutL) chuckOutL[i] += outSample;
            if (chuckOutR) chuckOutR[i] += outSample;
        }
    } else if (chuckChannels > 0 && !audioContext->chuckInterleavedBuffer.empty()) {
        ring_sample = audioContext->chuckInterleavedBuffer[0];
        ring_sample_set = true;
    }

    // DAW playback + recording
    if (audioContext->daw) {
        DawContext& daw = *audioContext->daw;
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
            bool anySolo = false;
            for (const auto& track : daw.tracks) {
                if (track.solo.load(std::memory_order_relaxed)) {
                    anySolo = true;
                    break;
                }
            }

            for (int t = 0; t < DawContext::kTrackCount; ++t) {
                const DawTrack& track = daw.tracks[t];
                bool solo = track.solo.load(std::memory_order_relaxed);
                bool mute = track.mute.load(std::memory_order_relaxed);
                if (anySolo && !solo) continue;
                if (!anySolo && mute) continue;

                int bus = track.outputBus.load(std::memory_order_relaxed);
                jack_default_audio_sample_t* busBuffer = (bus >= 0 && bus < DawContext::kBusCount) ? busOut[bus] : nullptr;
                if (!busBuffer) continue;

                const std::vector<float>& data = track.audio;
                for (jack_nframes_t i = 0; i < nframes; ++i) {
                    size_t idx = static_cast<size_t>(playhead + i);
                    float sample = (idx < data.size()) ? data[idx] : 0.0f;
                    busBuffer[i] += sample;
                }
            }
            daw.playheadSample.store(playhead + nframes, std::memory_order_relaxed);
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

        if (daw.transportRecording.load(std::memory_order_relaxed)) {
            constexpr int kMaxInputs = 32;
            std::array<jack_default_audio_sample_t*, kMaxInputs> inBuffers{};
            const int totalInputs = std::min<int>(static_cast<int>(audioContext->input_ports.size()), kMaxInputs);
            for (int ch = 0; ch < totalInputs; ++ch) {
                jack_port_t* port = audioContext->input_ports[ch];
                if (!port) continue;
                inBuffers[ch] = (jack_default_audio_sample_t*)jack_port_get_buffer(port, nframes);
            }

            for (int t = 0; t < DawContext::kTrackCount; ++t) {
                DawTrack& track = daw.tracks[t];
                if (!track.recordEnabled.load(std::memory_order_relaxed)) continue;
                int inputIndex = track.inputIndex;
                if (inputIndex < 0 || inputIndex >= totalInputs) continue;
                auto* inBuf = inBuffers[inputIndex];
                if (!inBuf || !track.recordRing) continue;
                size_t writeSpace = jack_ringbuffer_write_space(track.recordRing);
                size_t framesToWrite = std::min<size_t>(nframes, writeSpace / sizeof(float));
                if (framesToWrite > 0) {
                    jack_ringbuffer_write(track.recordRing,
                                          reinterpret_cast<const char*>(inBuf),
                                          framesToWrite * sizeof(float));
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
        audio.chuck->setParam(CHUCK_PARAM_VM_HALT, TRUE);
        audio.chuck->setParam(CHUCK_PARAM_IS_REALTIME_AUDIO_HINT, TRUE);
        // Allocate buffers expected by ChucK::run (input optional, output will be provided by JACK)
        if (audio.chuckInputChannels > 0) {
            audio.chuckInput = new SAMPLE[bufferFrames * audio.chuckInputChannels]();
        }
        audio.channelGains.assign(audio.chuckOutputChannels, 0.0f);
        if (audio.chuckMainChannel >= 0 && audio.chuckMainChannel < audio.chuckOutputChannels) {
            audio.channelGains[audio.chuckMainChannel] = 1.0f;
        }
        audio.chuckInterleavedBuffer.assign(audio.chuckOutputChannels * bufferFrames, 0.0f);
        audio.chuck->init();
        audio.chuck->start();
        audio.chuckRunning = true;
        audio.chuckMainCompileRequested = true; // compile default on next update
    }

    void CleanupAudio(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.audio || !baseSystem.audio->client) return;
        jack_deactivate(baseSystem.audio->client);
        jack_client_close(baseSystem.audio->client);
        baseSystem.audio->output_ports.clear();
        baseSystem.audio->input_ports.clear();
        if (baseSystem.audio->ring_buffer) {
            jack_ringbuffer_free(baseSystem.audio->ring_buffer);
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
        std::cout << "Audio I/O System Cleaned Up." << std::endl;
    }
}
