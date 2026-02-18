#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace MidiWaveformSystemLogic {

    namespace {
        constexpr size_t kWaveformBlockSize = 256;
        constexpr int kFrequencyStepsPerColor = 23;
        constexpr int kFrequencyBaseColorCount = 4;
        constexpr int kFrequencyTotalSteps = kFrequencyStepsPerColor * (kFrequencyBaseColorCount - 1);
        constexpr float kFrequencyMinHz = 20.0f;
        constexpr float kFrequencyMaxHz = 1200.0f;
        constexpr float kFrequencySilenceThreshold = 1e-6f;
        const glm::vec3 kWaveformFallbackColor(0.188235f, 0.164706f, 0.145098f);
        const std::array<glm::vec3, kFrequencyBaseColorCount> kFrequencyBaseColors = {{
            glm::vec3(1.0f, 0.2f, 0.3f),
            glm::vec3(1.0f, 0.6f, 0.2f),
            glm::vec3(1.0f, 1.0f, 0.2f),
            glm::vec3(0.2f, 1.0f, 0.2f)
        }};

        struct Complex {
            float r = 0.0f;
            float i = 0.0f;
        };

        float lerp(float a, float b, float t) {
            return a + t * (b - a);
        }

        float smoothstep(float t) {
            return t * t * (3.0f - 2.0f * t);
        }

        Complex add(const Complex& a, const Complex& b) {
            return { a.r + b.r, a.i + b.i };
        }

        Complex sub(const Complex& a, const Complex& b) {
            return { a.r - b.r, a.i - b.i };
        }

        Complex mul(const Complex& a, const Complex& b) {
            return { a.r * b.r - a.i * b.i, a.r * b.i + a.i * b.r };
        }

        void fft(std::vector<Complex>& data) {
            size_t n = data.size();
            if (n <= 1) return;

            std::vector<Complex> even(n / 2), odd(n / 2);
            for (size_t i = 0; i < n / 2; ++i) {
                even[i] = data[2 * i];
                odd[i] = data[2 * i + 1];
            }
            fft(even);
            fft(odd);
            for (size_t k = 0; k < n / 2; ++k) {
                float angle = -2.0f * 3.14159265358979f * static_cast<float>(k) / static_cast<float>(n);
                Complex wk = { std::cos(angle), std::sin(angle) };
                Complex temp = mul(wk, odd[k]);
                data[k] = add(even[k], temp);
                data[k + n / 2] = sub(even[k], temp);
            }
        }

        float computeDominantFrequency(const float* samples,
                                       size_t sampleCount,
                                       float sampleRate,
                                       std::vector<Complex>& buffer) {
            if (sampleCount == 0) return 0.0f;
            if (buffer.size() != sampleCount) buffer.resize(sampleCount);
            for (size_t i = 0; i < sampleCount; ++i) {
                buffer[i] = { samples[i], 0.0f };
            }
            fft(buffer);
            size_t half = sampleCount / 2;
            float maxMagSq = 0.0f;
            size_t maxIdx = 0;
            for (size_t k = 1; k < half; ++k) {
                float magSq = buffer[k].r * buffer[k].r + buffer[k].i * buffer[k].i;
                if (magSq > maxMagSq) {
                    maxMagSq = magSq;
                    maxIdx = k;
                }
            }
            if (maxMagSq <= kFrequencySilenceThreshold) return 0.0f;
            if (sampleRate <= 0.0f) sampleRate = 44100.0f;
            return (sampleRate * static_cast<float>(maxIdx)) / static_cast<float>(sampleCount);
        }

        glm::vec3 frequencyToColor(float freq) {
            float clampedFreq = std::clamp(freq, kFrequencyMinHz, kFrequencyMaxHz);
            float norm = (std::log10(clampedFreq) - std::log10(kFrequencyMinHz)) /
                (std::log10(kFrequencyMaxHz) - std::log10(kFrequencyMinHz));
            float colorIndex = norm * static_cast<float>(kFrequencyTotalSteps);
            float stepIndex = colorIndex / static_cast<float>(kFrequencyStepsPerColor);
            int colorSegment = static_cast<int>(stepIndex);
            if (colorSegment >= kFrequencyBaseColorCount - 1) {
                colorSegment = kFrequencyBaseColorCount - 2;
                stepIndex = static_cast<float>(kFrequencyBaseColorCount - 1);
            }
            float t = stepIndex - static_cast<float>(colorSegment);
            t = smoothstep(t);
            const glm::vec3& a = kFrequencyBaseColors[colorSegment];
            const glm::vec3& b = kFrequencyBaseColors[colorSegment + 1];
            return glm::vec3(lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t));
        }
    }

    void RebuildWaveform(MidiTrack& track, float sampleRate) {
        track.waveformMin.clear();
        track.waveformMax.clear();
        track.waveformColor.clear();
        if (track.audio.empty()) {
            track.waveformVersion += 1;
            return;
        }
        size_t blockCount = (track.audio.size() + kWaveformBlockSize - 1) / kWaveformBlockSize;
        track.waveformMin.assign(blockCount, 0.0f);
        track.waveformMax.assign(blockCount, 0.0f);
        track.waveformColor.assign(blockCount, kWaveformFallbackColor);
        std::vector<float> fftSamples(kWaveformBlockSize, 0.0f);
        std::vector<Complex> fftBuffer(kWaveformBlockSize);
        for (size_t block = 0; block < blockCount; ++block) {
            size_t start = block * kWaveformBlockSize;
            size_t end = std::min(start + kWaveformBlockSize, track.audio.size());
            float minVal = 1.0f;
            float maxVal = -1.0f;
            for (size_t i = start; i < end; ++i) {
                float v = track.audio[i];
                minVal = std::min(minVal, v);
                maxVal = std::max(maxVal, v);
            }
            if (end == start) {
                minVal = 0.0f;
                maxVal = 0.0f;
            }
            track.waveformMin[block] = minVal;
            track.waveformMax[block] = maxVal;

            if (end == start) {
                track.waveformColor[block] = kWaveformFallbackColor;
                continue;
            }
            size_t sampleCount = end - start;
            for (size_t i = 0; i < kWaveformBlockSize; ++i) {
                if (i < sampleCount) {
                    fftSamples[i] = track.audio[start + i];
                } else {
                    fftSamples[i] = fftSamples[sampleCount - 1];
                }
            }
            float domFreq = computeDominantFrequency(fftSamples.data(), fftSamples.size(), 44100.0f, fftBuffer);
            track.waveformColor[block] = (domFreq <= 0.0f) ? kWaveformFallbackColor : frequencyToColor(domFreq);
        }
        track.waveformVersion += 1;
    }

    void UpdateWaveformRange(MidiTrack& track, size_t startSample, size_t endSample) {
        size_t recordedStart = static_cast<size_t>(track.recordStartSample);
        size_t recordedEnd = recordedStart + track.pendingRecord.size();
        size_t combinedLength = std::max(track.audio.size(), recordedEnd);
        if (combinedLength == 0 || endSample <= startSample) return;

        size_t blockCount = (combinedLength + kWaveformBlockSize - 1) / kWaveformBlockSize;
        if (track.waveformMin.size() < blockCount) {
            track.waveformMin.resize(blockCount, 0.0f);
            track.waveformMax.resize(blockCount, 0.0f);
        }
        size_t blockStart = startSample / kWaveformBlockSize;
        size_t blockEnd = (endSample + kWaveformBlockSize - 1) / kWaveformBlockSize;
        blockEnd = std::min(blockEnd, blockCount);
        for (size_t block = blockStart; block < blockEnd; ++block) {
            size_t sampleStart = block * kWaveformBlockSize;
            size_t sampleEnd = std::min(sampleStart + kWaveformBlockSize, combinedLength);
            float minVal = 1.0f;
            float maxVal = -1.0f;
            for (size_t i = sampleStart; i < sampleEnd; ++i) {
                float base = (i < track.audio.size()) ? track.audio[i] : 0.0f;
                float rec = 0.0f;
                if (i >= recordedStart && i < recordedEnd) {
                    rec = track.pendingRecord[i - recordedStart];
                }
                float v = (track.recordArmMode == 2 && i >= recordedStart && i < recordedEnd)
                    ? rec
                    : base + rec;
                minVal = std::min(minVal, v);
                maxVal = std::max(maxVal, v);
            }
            if (sampleEnd == sampleStart) {
                minVal = 0.0f;
                maxVal = 0.0f;
            }
            track.waveformMin[block] = minVal;
            track.waveformMax[block] = maxVal;
        }
        track.waveformVersion += 1;
    }

    void UpdateMidiWaveforms(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*) {
    }
}
