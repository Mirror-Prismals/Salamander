#pragma once

#include <jack/jack.h>
#include <jack/ringbuffer.h>
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
    const int channels = audioContext->chuckOutputChannels;
    if (audioContext->chuckInterleavedBuffer.size() < static_cast<size_t>(channels * nframes)) {
        audioContext->chuckInterleavedBuffer.assign(channels * nframes, 0.0f);
    } else {
        std::fill(audioContext->chuckInterleavedBuffer.begin(), audioContext->chuckInterleavedBuffer.begin() + channels * nframes, 0.0f);
    }

    if (audioContext->chuck && audioContext->chuckRunning) {
        audioContext->chuck->run(nullptr, audioContext->chuckInterleavedBuffer.data(), nframes);
    } else {
        std::fill(audioContext->chuckInterleavedBuffer.begin(), audioContext->chuckInterleavedBuffer.begin() + channels * nframes, 0.0f);
    }

    // De-interleave into JACK ports
    float ring_sample = 0.0f;
    bool ring_sample_set = false;
    if (audioContext->channelGains.size() < static_cast<size_t>(channels)) {
        audioContext->channelGains.assign(channels, 1.0f);
    }
    for (int ch = 0; ch < channels; ++ch) {
        jack_port_t* port = (ch < static_cast<int>(audioContext->output_ports.size())) ? audioContext->output_ports[ch] : nullptr;
        if (!port) continue;
        auto* out = (jack_default_audio_sample_t*)jack_port_get_buffer(port, nframes);
        float chGain = (ch < static_cast<int>(audioContext->channelGains.size())) ? audioContext->channelGains[ch] : 1.0f;
        for (jack_nframes_t i = 0; i < nframes; ++i) {
            out[i] = audioContext->chuckInterleavedBuffer[i * channels + ch] * chGain;
            if (!ring_sample_set && ch == 0 && i == 0) {
                ring_sample = out[i];
                ring_sample_set = true;
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
        for (int ch = 0; ch < audio.chuckOutputChannels; ++ch) {
            std::string name = "output_" + std::to_string(ch + 1);
            jack_port_t* p = jack_port_register(audio.client, name.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
            audio.output_ports.push_back(p);
        }
        if (jack_activate(audio.client)) { std::cerr << "FATAL: Cannot activate client." << std::endl; exit(1); }
        // Auto-connect main (channel 2) and noise (channel 1) if available after activate
        auto auto_connect = [&](int chIndex, const std::string& playbackName){
            if (chIndex < static_cast<int>(audio.output_ports.size()) && audio.output_ports[chIndex]) {
                jack_connect(audio.client, jack_port_name(audio.output_ports[chIndex]), playbackName.c_str());
            }
        };
        auto_connect(0, "system:playback_1");
        auto_connect(0, "system:playback_2");
        auto_connect(1, "system:playback_1");
        auto_connect(1, "system:playback_2");
        std::cout << "Audio I/O System Initialized." << std::endl;

        // Initialize ChucK engine to render via JACK
        const t_CKINT sampleRate = static_cast<t_CKINT>(jack_get_sample_rate(audio.client));
        const t_CKINT bufferFrames = static_cast<t_CKINT>(jack_get_buffer_size(audio.client));
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
