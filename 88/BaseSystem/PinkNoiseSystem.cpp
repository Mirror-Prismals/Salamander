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
#include <limits>
#include <string>

// Forward declarations
struct AudioContext; 
struct BaseSystem;
struct Entity;
struct RayTracedAudioContext;
struct PlayerContext;
struct AudioSourceState;

namespace PinkNoiseSystemLogic {

    namespace {
        constexpr const char* kFollowerWorldName = "PlayerHeadAudioVisualizerWorld";
    }

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
            audio.rayTestActive = false;
            audio.headRayActive = false;
            return;
        }

        LevelContext& level = *baseSystem.level;
        EntityInstance* blockEmitter = nullptr;
        EntityInstance* headEmitter = nullptr;
        float bestBlockDistSq = std::numeric_limits<float>::max();
        float bestHeadDistSq = std::numeric_limits<float>::max();
        glm::vec3 listenerPos = baseSystem.player ? baseSystem.player->cameraPosition : glm::vec3(0.0f);

        for (auto& world : level.worlds) {
            bool isHeadFollowerWorld = (world.name == kFollowerWorldName);
            for (auto& instance : world.instances) {
                if (instance.prototypeID != visualizerProtoID) continue;
                glm::vec3 delta = instance.position - listenerPos;
                float distSq = glm::dot(delta, delta);
                if (isHeadFollowerWorld) {
                    if (distSq < bestHeadDistSq) {
                        bestHeadDistSq = distSq;
                        headEmitter = &instance;
                    }
                } else {
                    if (distSq < bestBlockDistSq) {
                        bestBlockDistSq = distSq;
                        blockEmitter = &instance;
                    }
                }
            }
        }

        bool blockFound = (blockEmitter != nullptr);
        bool headFound = (headEmitter != nullptr);

        AudioSourceState blockState{};
        blockState.isOccluded = false;
        blockState.distanceGain = 1.0f;
        AudioSourceState headState{};
        headState.isOccluded = false;
        headState.distanceGain = 1.0f;
        AudioSourceState micState{};
        bool micStateFound = false;

        if (blockEmitter) {
            if (rtAudio.sourceStates.count(blockEmitter->instanceID)) {
                blockState = rtAudio.sourceStates[blockEmitter->instanceID];
            }
            if (rtAudio.micSourceStates.count(blockEmitter->instanceID)) {
                micState = rtAudio.micSourceStates[blockEmitter->instanceID];
                micStateFound = true;
            }
        }
        if (headEmitter) {
            if (rtAudio.sourceStates.count(headEmitter->instanceID)) {
                headState = rtAudio.sourceStates[headEmitter->instanceID];
            }
        }

        AudioSourceState primaryState = blockFound ? blockState : headState;
        if (!blockFound && !headFound) {
            primaryState = AudioSourceState{};
            primaryState.isOccluded = false;
            primaryState.distanceGain = 1.0f;
        }
        alpha = primaryState.isOccluded ? 0.15f : 1.0f;
        distance_gain = primaryState.distanceGain;

        if (blockEmitter || headEmitter) {
            float peak_amplitude = 0.0f;
            float sample;
            if (audio.ring_buffer) {
                while(jack_ringbuffer_read_space(audio.ring_buffer) >= sizeof(float)) {
                    jack_ringbuffer_read(audio.ring_buffer, (char*)&sample, sizeof(float));
                    peak_amplitude = std::max(peak_amplitude, std::fabs(sample));
                }
            }

            glm::vec3 magenta = baseSystem.world->colorLibrary["Magenta"];
            glm::vec3 white = baseSystem.world->colorLibrary["White"];
            float clamped_amplitude = std::min(1.0f, peak_amplitude / audio.output_gain * 4.0f); // Normalize by gain for better sensitivity
            glm::vec3 animatedColor = glm::mix(magenta, white, clamped_amplitude);
            if (blockEmitter) {
                blockEmitter->color = animatedColor;
            }
            if (headEmitter) {
                headEmitter->color = animatedColor;
            }
        }
        
        std::lock_guard<std::mutex> lock(audio.audio_state_mutex);
        static bool prevBlockActive = false;
        static bool prevHeadActive = false;
        static bool prevMicActive = false;
        audio.active_generators = 0;
        // Disable ChucK noise shred for the pink noise block.
        audio.chuckNoiseShouldRun = false;
        // Speaker block RT WAV path (non-follower visualizer).
        if (blockFound && !prevBlockActive) {
            audio.rayTestPos = 0.0;
        }
        audio.rayTestActive = blockFound;
        audio.rayTestGain = blockFound ? blockState.distanceGain * (blockState.isOccluded ? 0.2f : 1.0f) : 0.0f;
        audio.rayTestPan = blockFound ? blockState.pan : 0.0f;
        if (!blockFound) {
            audio.rayTestHfState = 0.0f;
        }
        audio.rayHfChannel = -1;
        audio.rayHfAlpha = blockFound ? blockState.hfAlpha : 0.0f;
        audio.rayItdChannel = -1;
        audio.rayEchoChannel = -1;
        audio.rayEchoDelaySeconds = blockFound ? blockState.echoDelaySeconds : 0.0f;
        audio.rayEchoGain = blockFound ? blockState.echoGain : 0.0f;

        // Player-head RT path fed from main.ck ChucK output.
        bool headShredsActive = audio.chuckHeadHasActiveShreds.load(std::memory_order_relaxed);
        bool headActive = headFound && headShredsActive;
        if (headActive && !prevHeadActive) {
            if (!audio.headRayEchoBuffer.empty()) {
                std::fill(audio.headRayEchoBuffer.begin(), audio.headRayEchoBuffer.end(), 0.0f);
            }
            if (!audio.headRayItdBuffer.empty()) {
                std::fill(audio.headRayItdBuffer.begin(), audio.headRayItdBuffer.end(), 0.0f);
            }
            audio.headRayEchoWriteIndex = 0;
            audio.headRayItdWriteIndex = 0;
            audio.headRayHfState = 0.0f;
        }
        audio.headRayActive = headActive;
        audio.headRayGain = headActive ? headState.distanceGain * (headState.isOccluded ? 0.2f : 1.0f) : 0.0f;
        audio.headRayPan = headActive ? headState.pan : 0.0f;
        audio.headRayHfAlpha = headActive ? headState.hfAlpha : 0.0f;
        audio.headRayEchoDelaySeconds = headActive ? headState.echoDelaySeconds : 0.0f;
        audio.headRayEchoGain = headActive ? headState.echoGain : 0.0f;
        if (!headActive) {
            audio.headRayHfState = 0.0f;
        }
        audio.active_generators = (blockFound ? 1 : 0) + (headActive ? 1 : 0);
        prevHeadActive = headActive;
        prevBlockActive = blockFound;

        bool micActive = blockFound && rtAudio.micCaptureActive && micStateFound;
        audio.micRayActive = micActive;
        audio.micRayGain = micActive ? micState.distanceGain : 0.0f;
        audio.micRayHfAlpha = micActive ? micState.hfAlpha : 0.0f;
        audio.micRayEchoDelaySeconds = micActive ? micState.echoDelaySeconds : 0.0f;
        audio.micRayEchoGain = micActive ? micState.echoGain : 0.0f;
        if (!micActive) {
            audio.micRayHfState = 0.0f;
        }
        if (micActive && !prevMicActive) {
            if (!audio.micRayEchoBuffer.empty()) {
                std::fill(audio.micRayEchoBuffer.begin(), audio.micRayEchoBuffer.end(), 0.0f);
            }
            audio.micRayEchoWriteIndex = 0;
        }
        prevMicActive = micActive;
    }
}
