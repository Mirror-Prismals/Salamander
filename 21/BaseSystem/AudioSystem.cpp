#pragma once

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <iostream>
#include <string.h>
#include <thread>

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

    int active_gens;
    {
        std::lock_guard<std::mutex> lock(audioContext->audio_state_mutex);
        active_gens = audioContext->active_generators;
    }

    if (active_gens > 0) {
        for (jack_nframes_t i = 0; i < nframes; i++) {
            // Call the complete generator from the PinkNoiseSystem.
            // This sample already has the filter and distance gain applied.
            float final_sample = PinkNoiseSystemLogic::generate_filtered_pink_noise(audioContext);
            
            out_buffer[i] = final_sample;
            
            // Only write one sample per frame to the ringbuffer for the visualization.
            // This prevents the buffer from overflowing and keeps the visualization responsive.
            if (i == 0) { 
                if (jack_ringbuffer_write_space(audioContext->ring_buffer) >= sizeof(float)) {
                    jack_ringbuffer_write(audioContext->ring_buffer, (char*)&final_sample, sizeof(float));
                }
            }
        }
    } else {
        // If no generators are active, output silence.
        memset(out_buffer, 0, sizeof(jack_default_audio_sample_t) * nframes);
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
        audio.output_port = jack_port_register(audio.client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (jack_activate(audio.client)) { std::cerr << "FATAL: Cannot activate client." << std::endl; exit(1); }
        std::cout << "Audio I/O System Initialized." << std::endl;
    }

    void CleanupAudio(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.audio || !baseSystem.audio->client) return;
        jack_deactivate(baseSystem.audio->client);
        jack_client_close(baseSystem.audio->client);
        if (baseSystem.audio->ring_buffer) {
            jack_ringbuffer_free(baseSystem.audio->ring_buffer);
        }
        std::cout << "Audio I/O System Cleaned Up." << std::endl;
    }
}
