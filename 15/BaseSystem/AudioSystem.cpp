#pragma once

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <random>
#include <vector>
#include <iostream>
#include <string.h> // For strcmp

// Forward declaration from Host.h
struct AudioContext; 

// --- Pink Noise Generation ---
float generate_pink_noise_sample(AudioContext* audioContext) {
    // Generate white noise
    float white = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;

    // Voss-McCartney Algorithm
    audioContext->pink_counter++;
    audioContext->pink_running_sum -= audioContext->pink_rows[0];
    audioContext->pink_rows[0] = white * 0.5f; // Scale white noise
    audioContext->pink_running_sum += audioContext->pink_rows[0];

    for (int i = 1; i < audioContext->PINK_NOISE_OCTAVES; i++) {
        if ((audioContext->pink_counter & ((1 << i) - 1)) == 0) {
            audioContext->pink_running_sum -= audioContext->pink_rows[i];
            audioContext->pink_rows[i] = white * 0.5f;
            audioContext->pink_running_sum += audioContext->pink_rows[i];
        }
    }
    
    return audioContext->pink_running_sum / audioContext->PINK_NOISE_OCTAVES;
}


// --- JACK CALLBACKS ---
int jack_process_callback(jack_nframes_t nframes, void* arg) {
    auto* audioContext = static_cast<AudioContext*>(arg);
    if (!audioContext || !audioContext->output_port) return 0;

    // Get the buffer for our output port
    auto* out = (jack_default_audio_sample_t*)jack_port_get_buffer(audioContext->output_port, nframes);

    // Generate noise and fill the buffers
    for (jack_nframes_t i = 0; i < nframes; i++) {
        float raw_sample = generate_pink_noise_sample(audioContext);
        
        // Apply gain
        float final_sample = raw_sample * audioContext->output_gain;

        // Write sample to the output port for audio playback
        out[i] = final_sample;

        // Write the same sample to the ring buffer for visualization
        if (jack_ringbuffer_write_space(audioContext->ring_buffer) >= sizeof(float)) {
            jack_ringbuffer_write(audioContext->ring_buffer, (char*)&final_sample, sizeof(float));
        }
    }

    return 0;
}

void jack_shutdown_callback(void* arg) {
    std::cerr << "JACK server has shut down. The audio system is now inactive." << std::endl;
}


// --- SYSTEM LOGIC ---
namespace AudioSystemLogic {

    void InitializeAudio(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.audio) {
            std::cerr << "FATAL: AudioContext not available for AudioSystem." << std::endl;
            exit(-1);
        }
        AudioContext& audio = *baseSystem.audio;
        srand(time(NULL)); // Seed random number generator

        const char* client_name = "cardinal_eds";
        const char* server_name = nullptr;
        jack_options_t options = JackNullOption;
        jack_status_t status;

        audio.client = jack_client_open(client_name, options, &status, server_name);
        if (audio.client == nullptr) {
            std::cerr << "FATAL: jack_client_open() failed, status = " << status << std::endl;
            if (status & JackServerFailed) std::cerr << "Unable to connect to JACK server." << std::endl;
            exit(1);
        }
        if (status & JackServerStarted) std::cout << "JACK server started" << std::endl;
        if (status & JackNameNotUnique) {
            client_name = jack_get_client_name(audio.client);
            std::cout << "Unique name '" << client_name << "' assigned." << std::endl;
        }

        jack_set_process_callback(audio.client, jack_process_callback, &audio);
        jack_on_shutdown(audio.client, jack_shutdown_callback, &audio);
        
        audio.ring_buffer = jack_ringbuffer_create(1024 * sizeof(float));
        if (audio.ring_buffer == nullptr) {
            std::cerr << "FATAL: Could not create ring buffer." << std::endl;
            jack_client_close(audio.client);
            exit(1);
        }
        jack_ringbuffer_mlock(audio.ring_buffer);

        // Create an audio OUTPUT port
        audio.output_port = jack_port_register(audio.client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (audio.output_port == nullptr) {
            std::cerr << "FATAL: No more JACK ports available." << std::endl;
            jack_client_close(audio.client);
            exit(1);
        }

        if (jack_activate(audio.client)) {
            std::cerr << "FATAL: Cannot activate client." << std::endl;
            jack_client_close(audio.client);
            exit(1);
        }

        // --- THE FAULTY AUTO-CONNECTION LOGIC HAS BEEN REMOVED ---
        
        std::cout << "JACK Audio System Initialized. Please connect output manually in qjackctl." << std::endl;
    }

    void CleanupAudio(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.audio || !baseSystem.audio->client) return;
        
        jack_deactivate(baseSystem.audio->client);
        jack_client_close(baseSystem.audio->client);
        
        if (baseSystem.audio->ring_buffer) {
            jack_ringbuffer_free(baseSystem.audio->ring_buffer);
        }

        std::cout << "JACK Audio System Cleaned Up." << std::endl;
    }
}
