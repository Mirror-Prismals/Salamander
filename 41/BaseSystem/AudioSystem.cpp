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
    auto* out_buffer = (jack_default_audio_sample_t*)jack_port_get_buffer(audioContext->output_port, nframes);
    auto* out_noise_buffer = audioContext->output_noise_port
        ? (jack_default_audio_sample_t*)jack_port_get_buffer(audioContext->output_noise_port, nframes)
        : nullptr;

    // If ChucK is active, let it render audio; otherwise fall back to pink noise.
    if (audioContext->chuck && audioContext->chuckRunning) {
        audioContext->chuck->run(nullptr, (SAMPLE*)out_buffer, nframes);
    } else {
        int active_gens;
        {
            std::lock_guard<std::mutex> lock(audioContext->audio_state_mutex);
            active_gens = audioContext->active_generators;
        }

        memset(out_buffer, 0, sizeof(jack_default_audio_sample_t) * nframes);
    }

    // Pink noise output on dedicated port (or silence if none/disabled)
    if (out_noise_buffer) {
        int active_gens;
        {
            std::lock_guard<std::mutex> lock(audioContext->audio_state_mutex);
            active_gens = audioContext->active_generators;
        }
        if (active_gens > 0) {
            for (jack_nframes_t i = 0; i < nframes; i++) {
                float final_sample = PinkNoiseSystemLogic::generate_filtered_pink_noise(audioContext);
                out_noise_buffer[i] = final_sample;
                if (i == 0) {
                    if (jack_ringbuffer_write_space(audioContext->ring_buffer) >= sizeof(float)) {
                        jack_ringbuffer_write(audioContext->ring_buffer, (char*)&final_sample, sizeof(float));
                    }
                }
            }
        } else {
            memset(out_noise_buffer, 0, sizeof(jack_default_audio_sample_t) * nframes);
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
        audio.output_port = jack_port_register(audio.client, "output_main", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        audio.output_noise_port = jack_port_register(audio.client, "output_noise", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (jack_activate(audio.client)) { std::cerr << "FATAL: Cannot activate client." << std::endl; exit(1); }
        std::cout << "Audio I/O System Initialized." << std::endl;

        // Initialize ChucK engine to render via JACK
        const t_CKINT sampleRate = static_cast<t_CKINT>(jack_get_sample_rate(audio.client));
        const t_CKINT bufferFrames = static_cast<t_CKINT>(jack_get_buffer_size(audio.client));
        audio.chuckBufferFrames = bufferFrames;
        audio.chuckInputChannels = 0;
        audio.chuckOutputChannels = 1; // mono output to match single JACK port
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
        audio.chuck->init();
        audio.chuck->start();
        audio.chuckRunning = true;
    }

    void CleanupAudio(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.audio || !baseSystem.audio->client) return;
        jack_deactivate(baseSystem.audio->client);
        jack_client_close(baseSystem.audio->client);
        baseSystem.audio->output_port = nullptr;
        baseSystem.audio->output_noise_port = nullptr;
        if (baseSystem.audio->ring_buffer) {
            jack_ringbuffer_free(baseSystem.audio->ring_buffer);
        }
        if (baseSystem.audio->chuckInput) {
            delete[] baseSystem.audio->chuckInput;
            baseSystem.audio->chuckInput = nullptr;
        }
        baseSystem.audio->chuckOutput = nullptr;
        baseSystem.audio->chuckRunning = false;
        if (baseSystem.audio->chuck) {
            delete baseSystem.audio->chuck;
            baseSystem.audio->chuck = nullptr;
        }
        std::cout << "Audio I/O System Cleaned Up." << std::endl;
    }
}
