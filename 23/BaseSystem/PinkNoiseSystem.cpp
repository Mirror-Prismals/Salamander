#pragma once

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <random>
#include <vector>
#include <iostream>
#include <string.h>
#include <thread>
#include <cmath>
#include <algorithm>

// Forward declarations
struct AudioContext; 
struct BaseSystem;
struct Entity;
struct RayTracedAudioContext;
struct PlayerContext;
struct AudioSourceState;

namespace PinkNoiseSystemLogic {

    // These are now owned by the system, not global state.
    float last_output = 0.0f;
    float alpha = 1.0f; 
    float distance_gain = 1.0f; 

    float generate_filtered_pink_noise(AudioContext* audioContext) {
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
        float raw_pink = audioContext->pink_running_sum / audioContext->PINK_NOISE_OCTAVES;
        
        float filtered_output = alpha * raw_pink + (1.0f - alpha) * last_output;
        last_output = filtered_output;
        
        // Apply master gain and the distance gain calculated by the RayTracedAudioSystem
        return filtered_output * audioContext->output_gain * distance_gain;
    }

    void ProcessPinkNoiseAudicle(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.audio || !baseSystem.world || !baseSystem.rayTracedAudio) return;

        AudioContext& audio = *baseSystem.audio;
        RayTracedAudioContext& rtAudio = *baseSystem.rayTracedAudio;

        Entity* worldEntity = nullptr;
        int visualizerProtoID = -1;
        for (auto& proto : prototypes) {
            if (proto.isWorld) worldEntity = &proto;
            if (proto.name == "AudioVisualizer") visualizerProtoID = proto.prototypeID;
        }

        if (!worldEntity || visualizerProtoID == -1) {
            std::lock_guard<std::mutex> lock(audio.audio_state_mutex);
            audio.active_generators = 0;
            return;
        }

        bool instance_found = false;
        for (auto& instance : worldEntity->instances) {
            if (instance.prototypeID == visualizerProtoID) {
                instance_found = true;

                // READ the state calculated by the RayTracedAudioSystem
                AudioSourceState currentState;
                if (rtAudio.sourceStates.count(instance.instanceID)) {
                    currentState = rtAudio.sourceStates[instance.instanceID];
                }
                
                // APPLY the state to this system's variables
                alpha = currentState.isOccluded ? 0.15f : 1.0f;
                distance_gain = currentState.distanceGain;

                // Color change logic (no changes here)
                float peak_amplitude = 0.0f;
                float sample;
                while(jack_ringbuffer_read_space(audio.ring_buffer) >= sizeof(float)) {
                    jack_ringbuffer_read(audio.ring_buffer, (char*)&sample, sizeof(float));
                    peak_amplitude = std::max(peak_amplitude, std::fabs(sample));
                }
                
                glm::vec3 magenta = baseSystem.world->colorLibrary["Magenta"];
                glm::vec3 white = baseSystem.world->colorLibrary["White"];
                float clamped_amplitude = std::min(1.0f, peak_amplitude / audio.output_gain * 4.0f); // Normalize by gain for better sensitivity
                instance.color = glm::mix(magenta, white, clamped_amplitude);
                
                break; 
            }
        }
        
        std::lock_guard<std::mutex> lock(audio.audio_state_mutex);
        audio.active_generators = instance_found ? 1 : 0;
    }
}
