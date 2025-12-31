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

    // These values are consumed by the ChucK noise script via globals or gain.
    float alpha = 1.0f;
    float distance_gain = 1.0f;
    // use AudioContext::chuckNoiseChannel for channel index

    void ProcessPinkNoiseAudicle(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.audio || !baseSystem.world || !baseSystem.rayTracedAudio || !baseSystem.level || baseSystem.level->worlds.empty()) return;

        AudioContext& audio = *baseSystem.audio;
        RayTracedAudioContext& rtAudio = *baseSystem.rayTracedAudio;

        int visualizerProtoID = -1;
        for (auto& proto : prototypes) {
            if (proto.name == "AudioVisualizer") {
                visualizerProtoID = proto.prototypeID;
                break;
            }
        }

        if (visualizerProtoID == -1) {
            std::lock_guard<std::mutex> lock(audio.audio_state_mutex);
            audio.active_generators = 0;
            return;
        }

        LevelContext& level = *baseSystem.level;
        bool instance_found = false;
        EntityInstance* emitterInstance = nullptr;

        for (auto& world : level.worlds) {
            for (auto& instance : world.instances) {
                if (instance.prototypeID == visualizerProtoID) {
                    emitterInstance = &instance;
                    instance_found = true;
                    break;
                }
            }
            if (instance_found) break;
        }

        AudioSourceState currentState{};
        currentState.isOccluded = false;
        currentState.distanceGain = 1.0f;

        if (emitterInstance) {
            // READ the state calculated by the RayTracedAudioSystem
            if (rtAudio.sourceStates.count(emitterInstance->instanceID)) {
                currentState = rtAudio.sourceStates[emitterInstance->instanceID];
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
            emitterInstance->color = glm::mix(magenta, white, clamped_amplitude);
        }
        
        std::lock_guard<std::mutex> lock(audio.audio_state_mutex);
        static bool prevActive = false;
        audio.active_generators = instance_found ? 1 : 0;
        // Disable ChucK noise shred for the pink noise block.
        audio.chuckNoiseShouldRun = false;
        // Apply RT gain to the ray test file playback.
        if (instance_found && !prevActive) {
            audio.rayTestPos = 0.0;
        }
        audio.rayTestActive = instance_found;
        audio.rayTestGain = instance_found ? distance_gain * (currentState.isOccluded ? 0.2f : 1.0f) : 0.0f;
        audio.rayTestPan = instance_found ? currentState.pan : 0.0f;
        if (!instance_found) {
            audio.rayTestHfState = 0.0f;
        }
        audio.rayHfChannel = -1;
        audio.rayHfAlpha = instance_found ? currentState.hfAlpha : 0.0f;
        audio.rayItdChannel = -1;
        audio.rayEchoChannel = -1;
        audio.rayEchoDelaySeconds = instance_found ? currentState.echoDelaySeconds : 0.0f;
        audio.rayEchoGain = instance_found ? currentState.echoGain : 0.0f;
        prevActive = instance_found;
    }
}
