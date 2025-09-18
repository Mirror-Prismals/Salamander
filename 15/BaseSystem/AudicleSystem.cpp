#pragma once
#include <cmath> // For fabs
#include <algorithm> // for std::max

namespace AudicleSystemLogic {
    void ProcessAudicles(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, GLFWwindow* win) {
        if (!baseSystem.world || !baseSystem.instance || !baseSystem.audio) { 
            return; 
        }

        AudioContext& audio = *baseSystem.audio;
        if (!audio.ring_buffer) return;

        // Find the peak amplitude from all available samples for this frame
        float peak_amplitude = 0.0f;
        float sample;
        size_t samples_available = jack_ringbuffer_read_space(audio.ring_buffer);
        while (jack_ringbuffer_read(audio.ring_buffer, (char*)&sample, sizeof(float)) == sizeof(float)) {
            peak_amplitude = std::max(peak_amplitude, std::fabs(sample));
        }
        
        // If no samples were read, there's nothing to do
        if (samples_available == 0) return;

        // Find the World entity and the colors
        Entity* worldEntity = nullptr;
        for (auto& proto : prototypes) { if (proto.isWorld) { worldEntity = &proto; break; } }
        if (!worldEntity) return;

        glm::vec3 magenta = baseSystem.world->colorLibrary["Magenta"];
        glm::vec3 white = baseSystem.world->colorLibrary["White"];

        // Find the visualizer audicle prototype ID
        int visualizerProtoID = -1;
        for (const auto& proto : prototypes) {
            if (proto.name == "AudioVisualizer") {
                visualizerProtoID = proto.prototypeID;
                break;
            }
        }
        if (visualizerProtoID == -1) return;

        // Iterate through all instances in the world and update our visualizer
        for (auto& instance : worldEntity->instances) {
            if (instance.prototypeID == visualizerProtoID) {
                // Interpolate color based on peak amplitude
                // Clamp amplitude to 1.0 to avoid overshooting the color mix
                float clamped_amplitude = std::min(1.0f, peak_amplitude * 4.0f); // Multiply by 4 for more sensitivity
                instance.color = glm::mix(magenta, white, clamped_amplitude);
            }
        }
    }
}
