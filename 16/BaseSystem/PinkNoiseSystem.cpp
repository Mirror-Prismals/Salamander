#pragma once

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <random>
#include <vector>
#include <iostream>
#include <string.h> // For memset
#include <thread>   // For std::lock_guard
#include <cmath>    // for std::fabs
#include <algorithm>// for std::max

// Forward declarations
struct AudioContext; 
struct BaseSystem;
struct Entity;

namespace PinkNoiseSystemLogic {

    // --- Pink Noise Generation ---
    float generate_pink_noise_sample(AudioContext* audioContext) {
        float white = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
        audioContext->pink_counter++;
        audioContext->pink_running_sum -= audioContext->pink_rows[0];
        audioContext->pink_rows[0] = white * 0.5f;
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

    // --- System Logic ---
    void ProcessPinkNoiseAudicle(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.audio || !baseSystem.world) return;

        AudioContext& audio = *baseSystem.audio;

        Entity* worldEntity = nullptr;
        int visualizerProtoID = -1;
        for (auto& proto : prototypes) {
            if (proto.isWorld) worldEntity = &proto;
            if (proto.name == "AudioVisualizer") visualizerProtoID = proto.prototypeID;
        }

        if (!worldEntity || visualizerProtoID == -1) {
            std::lock_guard<std::mutex> lock(audio.audio_state_mutex);
            audio.active_generators = 0; // No generators are active
            return;
        }

        bool instance_found = false;
        for (auto& instance : worldEntity->instances) {
            if (instance.prototypeID == visualizerProtoID) {
                instance_found = true;

                // --- COLOR CHANGE LOGIC ---
                float peak_amplitude = 0.0f;
                float sample;
                // We only *read* from the ring buffer here. AudioSystem writes to it.
                while(jack_ringbuffer_peek(audio.ring_buffer, (char*)&sample, sizeof(float)) == sizeof(float)) {
                    peak_amplitude = std::max(peak_amplitude, std::fabs(sample));
                    jack_ringbuffer_read_advance(audio.ring_buffer, sizeof(float));
                }
                
                glm::vec3 magenta = baseSystem.world->colorLibrary["Magenta"];
                glm::vec3 white = baseSystem.world->colorLibrary["White"];
                
                float clamped_amplitude = std::min(1.0f, peak_amplitude * 4.0f); 
                instance.color = glm::mix(magenta, white, clamped_amplitude);
                
                break; 
            }
        }
        
        // Update the audio thread's state
        std::lock_guard<std::mutex> lock(audio.audio_state_mutex);
        if (instance_found) {
            audio.active_generators = 1; // For now, just a simple flag.
        } else {
            audio.active_generators = 0;
        }
    }
}
