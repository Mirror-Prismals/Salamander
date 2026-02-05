#pragma once

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <mutex>
#include <string.h>
#include <thread>
#include <cmath>
#include <vector>
#include "chuck.h"
#include "BaseSystem/Vst3Host.h"

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

    bool needMicBuffer = false;
    if (audioContext->daw) {
        DawContext& daw = *audioContext->daw;
        if (daw.transportRecording.load(std::memory_order_relaxed)) {
            for (const auto& track : daw.tracks) {
                if (!track.recordEnabled.load(std::memory_order_relaxed)) continue;
                if (track.useVirtualInput.load(std::memory_order_relaxed)) {
                    needMicBuffer = true;
                    break;
                }
            }
        }
    }
    if (needMicBuffer) {
        if (audioContext->micCaptureBuffer.size() < nframes) {
            audioContext->micCaptureBuffer.assign(nframes, 0.0f);
        } else {
            std::fill(audioContext->micCaptureBuffer.begin(),
                      audioContext->micCaptureBuffer.begin() + nframes, 0.0f);
        }
    }

    MidiContext* midiContext = audioContext->midi;
    if (midiContext && !audioContext->midi_input_ports.empty()) {
        jack_port_t* port = audioContext->midi_input_ports[0];
        if (port) {
            void* midiBuf = jack_port_get_buffer(port, nframes);
            uint32_t eventCount = jack_midi_get_event_count(midiBuf);
            for (uint32_t i = 0; i < eventCount; ++i) {
                jack_midi_event_t event;
                if (jack_midi_event_get(&event, midiBuf, i) != 0) continue;
                if (event.size < 2 || !event.buffer) continue;
                uint8_t status = event.buffer[0] & 0xF0;
                uint8_t note = event.buffer[1];
                uint8_t velocity = (event.size > 2) ? event.buffer[2] : 0;
                if (status == 0x90 && velocity > 0) {
                    midiContext->activeNote.store(static_cast<int>(note), std::memory_order_relaxed);
                    midiContext->activeVelocity.store(static_cast<float>(velocity) / 127.0f, std::memory_order_relaxed);
                } else if (status == 0x80 || (status == 0x90 && velocity == 0)) {
                    if (midiContext->activeNote.load(std::memory_order_relaxed) == static_cast<int>(note)) {
                        midiContext->activeNote.store(-1, std::memory_order_relaxed);
                        midiContext->activeVelocity.store(0.0f, std::memory_order_relaxed);
                    }
                }
            }
        }
    }

    // Mix ChucK channels to L/R with per-channel pan (keep visualizer sample on ch 0)
    float ring_sample = 0.0f;
    bool ring_sample_set = false;
    if (audioContext->channelGains.size() < static_cast<size_t>(chuckChannels)) {
        audioContext->channelGains.assign(chuckChannels, 1.0f);
    }
    if (audioContext->channelPans.size() < static_cast<size_t>(chuckChannels)) {
        audioContext->channelPans.assign(chuckChannels, 0.0f);
    }
    jack_default_audio_sample_t* chuckOutL = (totalOutputs > 0) ? outBuffers[0] : nullptr;
    jack_default_audio_sample_t* chuckOutR = (totalOutputs > 1) ? outBuffers[1] : nullptr;
    if (chuckOutL || chuckOutR) {
        const float monoScale = 0.5f;
        const int echoChannel = audioContext->rayEchoChannel;
        const float echoGain = audioContext->rayEchoGain;
        const float echoDelaySeconds = audioContext->rayEchoDelaySeconds;
        const int hfChannel = audioContext->rayHfChannel;
        const float hfAlpha = audioContext->rayHfAlpha;
        const int itdChannel = audioContext->rayItdChannel;
        const float panStrength = audioContext->rayPanStrength;
        const float itdMaxMs = audioContext->rayItdMaxMs;
        const size_t itdBufferSize = audioContext->rayItdBuffer.size();
        size_t itdMaxSamples = 0;
        if (itdBufferSize > 1 && itdMaxMs > 0.0f) {
            itdMaxSamples = static_cast<size_t>(audioContext->sampleRate * (itdMaxMs / 1000.0f));
            if (itdMaxSamples >= itdBufferSize) itdMaxSamples = itdBufferSize - 1;
        }
        if (hfChannel < 0 || hfAlpha <= 0.0f) {
            audioContext->rayHfState = 0.0f;
        }
        const size_t echoBufferSize = audioContext->rayEchoBuffer.size();
        size_t echoDelaySamples = 0;
        if (echoBufferSize > 1 && echoDelaySeconds > 0.0f) {
            echoDelaySamples = static_cast<size_t>(echoDelaySeconds * audioContext->sampleRate);
            if (echoDelaySamples >= echoBufferSize) echoDelaySamples = echoBufferSize - 1;
        }

        for (jack_nframes_t i = 0; i < nframes; ++i) {
            float outL = 0.0f;
            float outR = 0.0f;
            for (int ch = 0; ch < chuckChannels; ++ch) {
                float chGain = (ch < static_cast<int>(audioContext->channelGains.size())) ? audioContext->channelGains[ch] : 1.0f;
                float sample = audioContext->chuckInterleavedBuffer[i * chuckChannels + ch] * chGain;
                if (!ring_sample_set && ch == 0 && i == 0) {
                    ring_sample = sample;
                    ring_sample_set = true;
                }

                if (ch == echoChannel && echoBufferSize > 1) {
                    float delayed = 0.0f;
                    if (echoDelaySamples > 0) {
                        size_t readIndex = (audioContext->rayEchoWriteIndex + echoBufferSize - echoDelaySamples) % echoBufferSize;
                        delayed = audioContext->rayEchoBuffer[readIndex];
                    }
                    audioContext->rayEchoBuffer[audioContext->rayEchoWriteIndex] = sample;
                    audioContext->rayEchoWriteIndex = (audioContext->rayEchoWriteIndex + 1) % echoBufferSize;
                    if (echoDelaySamples > 0 && echoGain > 0.0f) {
                        sample += delayed * echoGain;
                    }
                }

                if (ch == hfChannel && hfAlpha > 0.0f) {
                    float filtered = audioContext->rayHfState + hfAlpha * (sample - audioContext->rayHfState);
                    audioContext->rayHfState = filtered;
                    sample = filtered;
                }

                float pan = (ch < static_cast<int>(audioContext->channelPans.size())) ? audioContext->channelPans[ch] : 0.0f;
                pan = std::clamp(pan, -1.0f, 1.0f);
                float effectivePan = pan;
                if (ch == itdChannel) {
                    effectivePan = std::clamp(pan * panStrength, -1.0f, 1.0f);
                }
                float lGain = monoScale * (1.0f - effectivePan);
                float rGain = monoScale * (1.0f + effectivePan);
                float sampleL = sample;
                float sampleR = sample;
                if (ch == itdChannel && itdMaxSamples > 0 && itdBufferSize > 1) {
                    size_t delaySamples = static_cast<size_t>(std::abs(effectivePan) * itdMaxSamples + 0.5f);
                    if (delaySamples > 0 && delaySamples < itdBufferSize) {
                        size_t readIndex = (audioContext->rayItdWriteIndex + itdBufferSize - delaySamples) % itdBufferSize;
                        float delayed = audioContext->rayItdBuffer[readIndex];
                        if (effectivePan >= 0.0f) {
                            sampleL = delayed;
                        } else {
                            sampleR = delayed;
                        }
                    }
                    audioContext->rayItdBuffer[audioContext->rayItdWriteIndex] = sample;
                    audioContext->rayItdWriteIndex = (audioContext->rayItdWriteIndex + 1) % itdBufferSize;
                }
                outL += sampleL * lGain;
                outR += sampleR * rGain;
            }
            if (chuckOutL) chuckOutL[i] += outL;
            if (chuckOutR) chuckOutR[i] += outR;
        }
    } else if (chuckChannels > 0 && !audioContext->chuckInterleavedBuffer.empty()) {
        ring_sample = audioContext->chuckInterleavedBuffer[0];
        ring_sample_set = true;
    }

    if (audioContext->rayTestActive && !audioContext->rayTestBuffer.empty()) {
        jack_default_audio_sample_t* outL = (totalOutputs > 0) ? outBuffers[0] : nullptr;
        jack_default_audio_sample_t* outR = (totalOutputs > 1) ? outBuffers[1] : nullptr;
        if (outL || outR) {
            const float monoScale = 0.5f;
            const float echoGain = audioContext->rayEchoGain;
            const float echoDelaySeconds = audioContext->rayEchoDelaySeconds;
            const float hfAlpha = audioContext->rayHfAlpha;
            const float panStrength = audioContext->rayPanStrength;
            const float itdMaxMs = audioContext->rayItdMaxMs;
            const size_t echoBufferSize = audioContext->rayEchoBuffer.size();
            const size_t itdBufferSize = audioContext->rayItdBuffer.size();
            const bool micActive = needMicBuffer && audioContext->micRayActive;
            float* micBuffer = micActive ? audioContext->micCaptureBuffer.data() : nullptr;
            const float micGain = audioContext->micRayGain;
            const float micEchoGain = audioContext->micRayEchoGain;
            const float micEchoDelaySeconds = audioContext->micRayEchoDelaySeconds;
            const float micHfAlpha = audioContext->micRayHfAlpha;
            const size_t micEchoBufferSize = audioContext->micRayEchoBuffer.size();
            size_t echoDelaySamples = 0;
            size_t itdMaxSamples = 0;
            size_t micEchoDelaySamples = 0;
            if (echoBufferSize > 1 && echoDelaySeconds > 0.0f) {
                echoDelaySamples = static_cast<size_t>(echoDelaySeconds * audioContext->sampleRate);
                if (echoDelaySamples >= echoBufferSize) echoDelaySamples = echoBufferSize - 1;
            }
            if (itdBufferSize > 1 && itdMaxMs > 0.0f) {
                itdMaxSamples = static_cast<size_t>(audioContext->sampleRate * (itdMaxMs / 1000.0f));
                if (itdMaxSamples >= itdBufferSize) itdMaxSamples = itdBufferSize - 1;
            }
            if (micEchoBufferSize > 1 && micEchoDelaySeconds > 0.0f) {
                micEchoDelaySamples = static_cast<size_t>(micEchoDelaySeconds * audioContext->sampleRate);
                if (micEchoDelaySamples >= micEchoBufferSize) micEchoDelaySamples = micEchoBufferSize - 1;
            }

            const double step = (audioContext->rayTestSampleRate > 0)
                ? static_cast<double>(audioContext->rayTestSampleRate) / static_cast<double>(audioContext->sampleRate)
                : 1.0;
            const size_t sampleCount = audioContext->rayTestBuffer.size();
            for (jack_nframes_t i = 0; i < nframes; ++i) {
                if (sampleCount == 0) break;
                size_t idx = static_cast<size_t>(audioContext->rayTestPos);
                if (idx >= sampleCount) {
                    if (audioContext->rayTestLoop) {
                        audioContext->rayTestPos = 0.0;
                        idx = 0;
                    } else {
                        audioContext->rayTestActive = false;
                        break;
                    }
                }
                size_t idxNext = (idx + 1 < sampleCount) ? idx + 1 : idx;
                double frac = audioContext->rayTestPos - static_cast<double>(idx);
                float rawSample = static_cast<float>((1.0 - frac) * audioContext->rayTestBuffer[idx] +
                                                     frac * audioContext->rayTestBuffer[idxNext]);
                float sample = rawSample * audioContext->rayTestGain;

                if (echoBufferSize > 1) {
                    float delayed = 0.0f;
                    if (echoDelaySamples > 0) {
                        size_t readIndex = (audioContext->rayEchoWriteIndex + echoBufferSize - echoDelaySamples) % echoBufferSize;
                        delayed = audioContext->rayEchoBuffer[readIndex];
                    }
                    audioContext->rayEchoBuffer[audioContext->rayEchoWriteIndex] = sample;
                    audioContext->rayEchoWriteIndex = (audioContext->rayEchoWriteIndex + 1) % echoBufferSize;
                    if (echoDelaySamples > 0 && echoGain > 0.0f) {
                        sample += delayed * echoGain;
                    }
                }

                if (hfAlpha > 0.0f) {
                    float filtered = audioContext->rayTestHfState + hfAlpha * (sample - audioContext->rayTestHfState);
                    audioContext->rayTestHfState = filtered;
                    sample = filtered;
                }

                float pan = std::clamp(audioContext->rayTestPan, -1.0f, 1.0f);
                pan = std::clamp(pan * panStrength, -1.0f, 1.0f);
                float lGain = monoScale * (1.0f - pan);
                float rGain = monoScale * (1.0f + pan);
                float sampleL = sample;
                float sampleR = sample;
                if (itdMaxSamples > 0 && itdBufferSize > 1) {
                    size_t delaySamples = static_cast<size_t>(std::abs(pan) * itdMaxSamples + 0.5f);
                    if (delaySamples > 0 && delaySamples < itdBufferSize) {
                        size_t readIndex = (audioContext->rayItdWriteIndex + itdBufferSize - delaySamples) % itdBufferSize;
                        float delayed = audioContext->rayItdBuffer[readIndex];
                        if (pan >= 0.0f) {
                            sampleL = delayed;
                        } else {
                            sampleR = delayed;
                        }
                    }
                    audioContext->rayItdBuffer[audioContext->rayItdWriteIndex] = sample;
                    audioContext->rayItdWriteIndex = (audioContext->rayItdWriteIndex + 1) % itdBufferSize;
                }
                if (outL) outL[i] += sampleL * lGain;
                if (outR) outR[i] += sampleR * rGain;

                if (micActive && micBuffer) {
                    float micSample = rawSample * micGain;
                    if (micEchoBufferSize > 1) {
                        float delayed = 0.0f;
                        if (micEchoDelaySamples > 0) {
                            size_t readIndex = (audioContext->micRayEchoWriteIndex + micEchoBufferSize - micEchoDelaySamples) % micEchoBufferSize;
                            delayed = audioContext->micRayEchoBuffer[readIndex];
                        }
                        audioContext->micRayEchoBuffer[audioContext->micRayEchoWriteIndex] = micSample;
                        audioContext->micRayEchoWriteIndex = (audioContext->micRayEchoWriteIndex + 1) % micEchoBufferSize;
                        if (micEchoDelaySamples > 0 && micEchoGain > 0.0f) {
                            micSample += delayed * micEchoGain;
                        }
                    }
                    if (micHfAlpha > 0.0f) {
                        float filtered = audioContext->micRayHfState + micHfAlpha * (micSample - audioContext->micRayHfState);
                        audioContext->micRayHfState = filtered;
                        micSample = filtered;
                    }
                    micBuffer[i] = micSample;
                }

                audioContext->rayTestPos += step;
            }
        }
    }

    if (audioContext->headTrackActive && !audioContext->headTrackBuffer.empty()) {
        jack_default_audio_sample_t* outL = (totalOutputs > 0) ? outBuffers[0] : nullptr;
        jack_default_audio_sample_t* outR = (totalOutputs > 1) ? outBuffers[1] : nullptr;
        if (outL || outR) {
            const float monoScale = 0.5f;
            const double step = (audioContext->headTrackSampleRate > 0)
                ? static_cast<double>(audioContext->headTrackSampleRate) / static_cast<double>(audioContext->sampleRate)
                : 1.0;
            const size_t sampleCount = audioContext->headTrackBuffer.size();
            for (jack_nframes_t i = 0; i < nframes; ++i) {
                if (sampleCount == 0) break;
                size_t idx = static_cast<size_t>(audioContext->headTrackPos);
                if (idx >= sampleCount) {
                    if (audioContext->headTrackLoop) {
                        audioContext->headTrackPos = 0.0;
                        idx = 0;
                    } else {
                        audioContext->headTrackActive = false;
                        break;
                    }
                }
                size_t idxNext = (idx + 1 < sampleCount) ? idx + 1 : idx;
                double frac = audioContext->headTrackPos - static_cast<double>(idx);
                float sample = static_cast<float>((1.0 - frac) * audioContext->headTrackBuffer[idx] +
                                                   frac * audioContext->headTrackBuffer[idxNext]);
                sample *= audioContext->headTrackGain;
                if (outL) outL[i] += sample * monoScale;
                if (outR) outR[i] += sample * monoScale;
                audioContext->headTrackPos += step;
            }
        }
    }

    // DAW playback + recording
    if (audioContext->daw) {
        DawContext& daw = *audioContext->daw;
        std::lock_guard<std::mutex> lock(daw.trackMutex);
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
            bool loopEnabled = daw.loopEnabled.load(std::memory_order_relaxed);
            uint64_t loopStart = daw.loopStartSamples;
            uint64_t loopEnd = daw.loopEndSamples;
            if (loopEnd <= loopStart) {
                loopEnabled = false;
            }
            uint64_t loopLength = loopEnabled ? (loopEnd - loopStart) : 0;
            if (loopEnabled && playhead >= loopEnd) {
                playhead = loopStart;
            }
            auto loopIndex = [&](uint64_t sample) -> uint64_t {
                if (loopEnabled && loopLength > 0 && sample >= loopEnd) {
                    return loopStart + (sample - loopStart) % loopLength;
                }
                return sample;
            };
            bool anySolo = false;
            for (const auto& track : daw.tracks) {
                if (track.solo.load(std::memory_order_relaxed)) {
                    anySolo = true;
                    break;
                }
            }
            if (midiContext) {
                std::lock_guard<std::mutex> midiLock(midiContext->trackMutex);
                for (const auto& mTrack : midiContext->tracks) {
                    if (mTrack.solo.load(std::memory_order_relaxed)) {
                        anySolo = true;
                        break;
                    }
                }
            }
            Vst3Context* vst3 = audioContext->vst3;

            int trackCount = static_cast<int>(daw.tracks.size());
            thread_local std::vector<float> clipBuffer;
            for (int t = 0; t < trackCount; ++t) {
                DawTrack& track = daw.tracks[static_cast<size_t>(t)];
                bool solo = track.solo.load(std::memory_order_relaxed);
                bool mute = track.mute.load(std::memory_order_relaxed);
                if (anySolo && !solo) {
                    track.meterLevel.store(0.0f, std::memory_order_relaxed);
                    continue;
                }
                if (!anySolo && mute) {
                    track.meterLevel.store(0.0f, std::memory_order_relaxed);
                    continue;
                }

                int bus = track.outputBus.load(std::memory_order_relaxed);
                jack_default_audio_sample_t* busBuffer = (bus >= 0 && bus < DawContext::kBusCount) ? busOut[bus] : nullptr;
                if (!busBuffer) continue;

                if (clipBuffer.size() < static_cast<size_t>(nframes)) {
                    clipBuffer.assign(static_cast<size_t>(nframes), 0.0f);
                } else {
                    std::fill(clipBuffer.begin(), clipBuffer.begin() + nframes, 0.0f);
                }
                const auto& clips = track.clips;
                if (!clips.empty()) {
                    uint64_t windowStart = playhead;
                    uint64_t windowEnd = playhead + nframes;
                    auto renderClips = [&](uint64_t segStart, uint64_t segEnd, size_t outOffset) {
                        for (const auto& clip : clips) {
                            if (clip.length == 0) continue;
                            if (clip.audioId < 0 || clip.audioId >= static_cast<int>(daw.clipAudio.size())) continue;
                            uint64_t clipStart = clip.startSample;
                            uint64_t clipEnd = clip.startSample + clip.length;
                            if (clipEnd <= segStart || clipStart >= segEnd) continue;
                            uint64_t overlapStart = std::max(clipStart, segStart);
                            uint64_t overlapEnd = std::min(clipEnd, segEnd);
                            if (overlapEnd <= overlapStart) continue;
                            const auto& data = daw.clipAudio[clip.audioId];
                            uint64_t srcOffset = clip.sourceOffset + (overlapStart - clipStart);
                            uint64_t count = overlapEnd - overlapStart;
                            if (srcOffset >= data.size()) continue;
                            uint64_t maxCopy = std::min<uint64_t>(count, data.size() - srcOffset);
                            size_t dstBase = outOffset + static_cast<size_t>(overlapStart - segStart);
                            for (uint64_t i = 0; i < maxCopy; ++i) {
                                size_t dst = dstBase + static_cast<size_t>(i);
                                if (dst >= clipBuffer.size()) break;
                                clipBuffer[dst] = data[static_cast<size_t>(srcOffset + i)];
                            }
                        }
                    };
                    if (loopEnabled && loopLength > 0 && playhead >= loopStart && windowEnd > loopEnd) {
                        uint64_t firstEnd = loopEnd;
                        size_t firstCount = static_cast<size_t>(firstEnd - windowStart);
                        renderClips(windowStart, firstEnd, 0);
                        uint64_t remainder = windowEnd - firstEnd;
                        renderClips(loopStart, loopStart + remainder, firstCount);
                    } else {
                        renderClips(windowStart, windowEnd, 0);
                    }
                }
                float maxAbs = 0.0f;
                float gain = track.gain.load(std::memory_order_relaxed);
                const float* source = nullptr;
                if (vst3 && static_cast<size_t>(t) < vst3->audioTracks.size()) {
                    Vst3TrackChain& chain = vst3->audioTracks[t];
                    if (chain.monoInput.size() >= static_cast<size_t>(nframes)) {
                        for (jack_nframes_t i = 0; i < nframes; ++i) {
                            chain.monoInput[i] = clipBuffer[i];
                        }
                        bool processed = Vst3SystemLogic::ProcessEffectChain(*vst3, chain,
                                                                             chain.monoInput.data(),
                                                                             chain.monoOutput.data(),
                                                                             static_cast<int>(nframes),
                                                                             static_cast<int64_t>(playhead),
                                                                             true);
                        source = processed ? chain.monoOutput.data() : chain.monoInput.data();
                    }
                }
                if (!source) {
                    for (jack_nframes_t i = 0; i < nframes; ++i) {
                        float sample = clipBuffer[i];
                        float outSample = sample * gain;
                        busBuffer[i] += outSample;
                        float absSample = std::fabs(outSample);
                        if (absSample > maxAbs) maxAbs = absSample;
                    }
                } else {
                    for (jack_nframes_t i = 0; i < nframes; ++i) {
                        float outSample = source[i] * gain;
                        busBuffer[i] += outSample;
                        float absSample = std::fabs(outSample);
                        if (absSample > maxAbs) maxAbs = absSample;
                    }
                }
                track.meterLevel.store(maxAbs, std::memory_order_relaxed);
            }

            if (midiContext) {
                std::lock_guard<std::mutex> midiLock(midiContext->trackMutex);
                int midiNote = midiContext->activeNote.load(std::memory_order_relaxed);
                float midiVelocity = midiContext->activeVelocity.load(std::memory_order_relaxed);
                if (midiVelocity <= 0.0f) midiNote = -1;
                thread_local std::vector<float> midiTemp;

                for (auto& mTrack : midiContext->tracks) {
                    float midiMaxAbs = 0.0f;
                    float midiRecordMax = 0.0f;
                    bool solo = mTrack.solo.load(std::memory_order_relaxed);
                    bool mute = mTrack.mute.load(std::memory_order_relaxed);
                    bool midiAllowed = (!anySolo && !mute) || (anySolo && solo);

                    if (midiAllowed) {
                        int bus = mTrack.outputBus.load(std::memory_order_relaxed);
                        jack_default_audio_sample_t* busBuffer = (bus >= 0 && bus < DawContext::kBusCount) ? busOut[bus] : nullptr;
                        if (busBuffer) {
                            const std::vector<float>& data = mTrack.audio;
                            float gain = mTrack.gain.load(std::memory_order_relaxed);
                            const float* midiSource = nullptr;
                            if (vst3 && vst3->midiFx.monoInput.size() >= static_cast<size_t>(nframes)) {
                                for (jack_nframes_t i = 0; i < nframes; ++i) {
                                    size_t idx = static_cast<size_t>(loopIndex(playhead + i));
                                    vst3->midiFx.monoInput[i] = (idx < data.size()) ? data[idx] : 0.0f;
                                }
                                bool processed = Vst3SystemLogic::ProcessEffectChain(*vst3, vst3->midiFx,
                                                                                     vst3->midiFx.monoInput.data(),
                                                                                     vst3->midiFx.monoOutput.data(),
                                                                                     static_cast<int>(nframes),
                                                                                     static_cast<int64_t>(playhead),
                                                                                     true);
                                midiSource = processed ? vst3->midiFx.monoOutput.data() : vst3->midiFx.monoInput.data();
                            }
                            if (!midiSource) {
                                for (jack_nframes_t i = 0; i < nframes; ++i) {
                                    size_t idx = static_cast<size_t>(loopIndex(playhead + i));
                                    float sample = (idx < data.size()) ? data[idx] : 0.0f;
                                    float outSample = sample * gain;
                                    busBuffer[i] += outSample;
                                    float absSample = std::fabs(outSample);
                                    if (absSample > midiMaxAbs) midiMaxAbs = absSample;
                                }
                            } else {
                                for (jack_nframes_t i = 0; i < nframes; ++i) {
                                    float outSample = midiSource[i] * gain;
                                    busBuffer[i] += outSample;
                                    float absSample = std::fabs(outSample);
                                    if (absSample > midiMaxAbs) midiMaxAbs = absSample;
                                }
                            }
                        }
                    }

                    if (daw.transportRecording.load(std::memory_order_relaxed)
                        && mTrack.recordEnabled.load(std::memory_order_relaxed)) {
                        bool usedInstrument = false;
                        const float* recordSource = nullptr;
                        if (vst3 && vst3->midiInstrument && vst3->midiFx.monoOutput.size() >= static_cast<size_t>(nframes)) {
                            usedInstrument = Vst3SystemLogic::ProcessInstrument(*vst3, *vst3->midiInstrument,
                                                                                vst3->midiFx.monoOutput.data(),
                                                                                static_cast<int>(nframes),
                                                                                static_cast<int64_t>(playhead),
                                                                                true,
                                                                                midiNote,
                                                                                midiVelocity);
                            if (usedInstrument) {
                                recordSource = vst3->midiFx.monoOutput.data();
                                for (jack_nframes_t i = 0; i < nframes; ++i) {
                                    float absSample = std::fabs(recordSource[i]);
                                    if (absSample > midiRecordMax) midiRecordMax = absSample;
                                }
                            }
                        }

                        if (!usedInstrument) {
                            if (midiTemp.size() < nframes) midiTemp.assign(nframes, 0.0f);
                            else std::fill(midiTemp.begin(), midiTemp.begin() + nframes, 0.0f);
                            if (midiNote >= 0 && midiVelocity > 0.0f) {
                                const double freq = 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);
                                const double phaseInc = 2.0 * 3.14159265358979 * freq / static_cast<double>(daw.sampleRate);
                                const float amp = midiVelocity * 0.2f;
                                for (jack_nframes_t i = 0; i < nframes; ++i) {
                                    float sample = std::sin(midiContext->phase) * amp;
                                    midiTemp[i] = sample;
                                    midiContext->phase += phaseInc;
                                    if (midiContext->phase > 2.0 * 3.14159265358979) {
                                        midiContext->phase -= 2.0 * 3.14159265358979;
                                    }
                                    float absSample = std::fabs(sample);
                                    if (absSample > midiRecordMax) midiRecordMax = absSample;
                                }
                            }
                            recordSource = midiTemp.data();
                        }

                        if (recordSource && mTrack.recordRing) {
                            size_t writeSpace = jack_ringbuffer_write_space(mTrack.recordRing);
                            size_t framesToWrite = std::min<size_t>(nframes, writeSpace / sizeof(float));
                            if (framesToWrite > 0) {
                                jack_ringbuffer_write(mTrack.recordRing,
                                                      reinterpret_cast<const char*>(recordSource),
                                                      framesToWrite * sizeof(float));
                            }
                        }
                    }

                    float meterValue = std::max(midiMaxAbs, midiRecordMax);
                    mTrack.meterLevel.store(meterValue, std::memory_order_relaxed);
                }
            }
            if (daw.metronomeEnabled.load(std::memory_order_relaxed) && daw.metronomeLoaded
                && !daw.metronomeSamples.empty() && daw.sampleRate > 0.0f) {
                double bpm = daw.bpm.load(std::memory_order_relaxed);
                if (bpm <= 0.0) bpm = 120.0;
                double beatSamples = (60.0 / bpm) * daw.sampleRate;
                if (beatSamples < 1.0) beatSamples = 1.0;
                auto nextBeatAtOrAfter = [&](uint64_t sample) -> uint64_t {
                    double div = static_cast<double>(sample) / beatSamples;
                    uint64_t beatIndex = static_cast<uint64_t>(std::ceil(div));
                    return static_cast<uint64_t>(std::llround(beatIndex * beatSamples));
                };
                if (!daw.metronomePrimed || daw.metronomeNextSample < playhead) {
                    daw.metronomeNextSample = nextBeatAtOrAfter(playhead);
                    daw.metronomePrimed = true;
                }
                double step = daw.metronomeSampleStep;
                const auto& click = daw.metronomeSamples;
                size_t clickCount = click.size();
                bool loopHasBeat = true;
                if (loopEnabled && loopLength > 0) {
                    loopHasBeat = nextBeatAtOrAfter(loopStart) < loopEnd;
                }
                uint64_t prevSample = playhead;
                bool prevLoopPhase = false;
                for (jack_nframes_t i = 0; i < nframes; ++i) {
                    uint64_t rawSample = playhead + i;
                    bool loopPhase = loopEnabled && loopLength > 0 && rawSample >= loopStart;
                    uint64_t currentSample = loopPhase ? loopIndex(rawSample) : rawSample;
                    if (loopPhase && !prevLoopPhase) {
                        daw.metronomeNextSample = nextBeatAtOrAfter(currentSample);
                    }
                    if (loopPhase && currentSample < prevSample) {
                        daw.metronomeNextSample = nextBeatAtOrAfter(currentSample);
                    }
                    prevSample = currentSample;
                    prevLoopPhase = loopPhase;
                    if (loopPhase) {
                        if (!loopHasBeat) {
                            daw.metronomeSampleActive = false;
                            continue;
                        }
                        if (daw.metronomeNextSample < loopStart || daw.metronomeNextSample >= loopEnd) {
                            daw.metronomeNextSample = nextBeatAtOrAfter(currentSample);
                            if (daw.metronomeNextSample < loopStart || daw.metronomeNextSample >= loopEnd) {
                                daw.metronomeNextSample = loopEnd + 1;
                            }
                        }
                    }
                    while (currentSample >= daw.metronomeNextSample) {
                        if (loopPhase && daw.metronomeNextSample > loopEnd) {
                            daw.metronomeSampleActive = false;
                            break;
                        }
                        daw.metronomeSamplePos = 0.0;
                        daw.metronomeSampleActive = true;
                        daw.metronomeNextSample += static_cast<uint64_t>(beatSamples);
                        if (loopPhase && daw.metronomeNextSample >= loopEnd) {
                            daw.metronomeNextSample = loopEnd + 1;
                        }
                        if (daw.metronomeNextSample == currentSample) break;
                    }
                    if (daw.metronomeSampleActive) {
                        size_t idx = static_cast<size_t>(daw.metronomeSamplePos);
                        if (idx >= clickCount) {
                            daw.metronomeSampleActive = false;
                        } else {
                            size_t idxNext = (idx + 1 < clickCount) ? idx + 1 : idx;
                            double frac = daw.metronomeSamplePos - static_cast<double>(idx);
                            float sample = static_cast<float>((1.0 - frac) * click[idx] + frac * click[idxNext]);
                            if (busOut[0]) busOut[0][i] += sample;
                            if (busOut[3]) busOut[3][i] += sample;
                            daw.metronomeSamplePos += step;
                            if (daw.metronomeSamplePos >= static_cast<double>(clickCount)) {
                                daw.metronomeSampleActive = false;
                            }
                        }
                    }
                }
            }
            uint64_t advanced = playhead + nframes;
            if (loopEnabled && loopLength > 0) {
                if (playhead >= loopStart || advanced >= loopEnd) {
                    advanced = loopStart + (advanced - loopStart) % loopLength;
                }
            }
            daw.playheadSample.store(advanced, std::memory_order_relaxed);
            if (vst3) {
                vst3->continuousSamples += static_cast<int64_t>(nframes);
            }
        }
        if (!playing) {
            daw.metronomePrimed = false;
            daw.metronomeSampleActive = false;
            for (auto& track : daw.tracks) {
                track.meterLevel.store(0.0f, std::memory_order_relaxed);
            }
            if (midiContext) {
                std::lock_guard<std::mutex> midiLock(midiContext->trackMutex);
                for (auto& mTrack : midiContext->tracks) {
                    mTrack.meterLevel.store(0.0f, std::memory_order_relaxed);
                }
            }
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
            const float* micBuf = (needMicBuffer && !audioContext->micCaptureBuffer.empty())
                ? audioContext->micCaptureBuffer.data()
                : nullptr;

            int trackCount = static_cast<int>(daw.tracks.size());
            for (int t = 0; t < trackCount; ++t) {
                DawTrack& track = daw.tracks[static_cast<size_t>(t)];
                if (!track.recordEnabled.load(std::memory_order_relaxed)) continue;
                if (track.useVirtualInput.load(std::memory_order_relaxed)) {
                    if (!micBuf || !track.recordRing) continue;
                    size_t writeSpace = jack_ringbuffer_write_space(track.recordRing);
                    size_t framesToWrite = std::min<size_t>(nframes, writeSpace / sizeof(float));
                    if (framesToWrite > 0) {
                        jack_ringbuffer_write(track.recordRing,
                                              reinterpret_cast<const char*>(micBuf),
                                              framesToWrite * sizeof(float));
                    }
                    continue;
                }
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
        audio.midi_input_ports.clear();
        {
            std::string name = "midi_in_1";
            jack_port_t* p = jack_port_register(audio.client, name.c_str(), JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
            if (p) {
                audio.midi_input_ports.push_back(p);
            }
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
        audio.channelPans.assign(audio.chuckOutputChannels, 0.0f);
        audio.rayEchoChannel = audio.chuckNoiseChannel;
        const float maxEchoSeconds = 2.0f;
        size_t echoSamples = static_cast<size_t>(audio.sampleRate * maxEchoSeconds) + 1;
        audio.rayEchoBuffer.assign(echoSamples, 0.0f);
        audio.rayEchoWriteIndex = 0;
        audio.rayHfChannel = -1;
        audio.rayHfAlpha = 0.0f;
        audio.rayHfState = 0.0f;
        audio.rayTestHfState = 0.0f;
        audio.micRayActive = false;
        audio.micRayGain = 0.0f;
        audio.micRayHfAlpha = 0.0f;
        audio.micRayHfState = 0.0f;
        audio.micRayEchoDelaySeconds = 0.0f;
        audio.micRayEchoGain = 0.0f;
        audio.micRayEchoBuffer.assign(echoSamples, 0.0f);
        audio.micRayEchoWriteIndex = 0;
        audio.micCaptureBuffer.assign(static_cast<size_t>(bufferFrames), 0.0f);
        audio.rayPanStrength = 0.35f;
        audio.rayItdMaxMs = 0.5f;
        audio.rayItdChannel = -1;
        size_t itdSamples = static_cast<size_t>(audio.sampleRate * (audio.rayItdMaxMs / 1000.0f)) + 4;
        if (itdSamples < 1) itdSamples = 1;
        audio.rayItdBuffer.assign(itdSamples, 0.0f);
        audio.rayItdWriteIndex = 0;
        audio.rayTestBuffer.clear();
        audio.rayTestSampleRate = 0;
        audio.rayTestPos = 0.0;
        audio.rayTestGain = 0.0f;
        audio.rayTestPan = 0.0f;
        audio.rayTestActive = false;
        audio.rayTestLoop = true;
        audio.headTrackBuffer.clear();
        audio.headTrackSampleRate = 0;
        audio.headTrackPos = 0.0;
        audio.headTrackGain = 1.0f;
        audio.headTrackActive = false;
        audio.headTrackLoop = true;
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
