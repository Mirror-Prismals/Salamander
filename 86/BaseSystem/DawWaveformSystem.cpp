#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace DawWaveformSystemLogic {

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

    void RebuildWaveform(DawTrack& track, float sampleRate) {
        track.waveformMin.clear();
        track.waveformMax.clear();
        track.waveformMinRight.clear();
        track.waveformMaxRight.clear();
        track.waveformColor.clear();
        if (track.audio.empty() && track.audioRight.empty()) {
            track.waveformVersion += 1;
            return;
        }
        size_t frameCount = std::max(track.audio.size(), track.audioRight.size());
        size_t blockCount = (frameCount + kWaveformBlockSize - 1) / kWaveformBlockSize;
        track.waveformMin.assign(blockCount, 0.0f);
        track.waveformMax.assign(blockCount, 0.0f);
        track.waveformMinRight.assign(blockCount, 0.0f);
        track.waveformMaxRight.assign(blockCount, 0.0f);
        track.waveformColor.assign(blockCount, kWaveformFallbackColor);
        std::vector<float> fftSamples(kWaveformBlockSize, 0.0f);
        std::vector<Complex> fftBuffer(kWaveformBlockSize);
        for (size_t block = 0; block < blockCount; ++block) {
            size_t start = block * kWaveformBlockSize;
            size_t end = std::min(start + kWaveformBlockSize, frameCount);
            float minValL = 1.0f;
            float maxValL = -1.0f;
            float minValR = 1.0f;
            float maxValR = -1.0f;
            for (size_t i = start; i < end; ++i) {
                float left = (i < track.audio.size()) ? track.audio[i] : 0.0f;
                float right = (i < track.audioRight.size()) ? track.audioRight[i] : left;
                minValL = std::min(minValL, left);
                maxValL = std::max(maxValL, left);
                minValR = std::min(minValR, right);
                maxValR = std::max(maxValR, right);
            }
            if (end == start) {
                minValL = 0.0f;
                maxValL = 0.0f;
                minValR = 0.0f;
                maxValR = 0.0f;
            }
            track.waveformMin[block] = minValL;
            track.waveformMax[block] = maxValL;
            track.waveformMinRight[block] = minValR;
            track.waveformMaxRight[block] = maxValR;

            if (end == start) {
                track.waveformColor[block] = kWaveformFallbackColor;
                continue;
            }
            for (size_t i = 0; i < kWaveformBlockSize; ++i) {
                size_t sampleIndex = start + i;
                if (sampleIndex < end) {
                    float left = (sampleIndex < track.audio.size()) ? track.audio[sampleIndex] : 0.0f;
                    float right = (sampleIndex < track.audioRight.size()) ? track.audioRight[sampleIndex] : left;
                    fftSamples[i] = 0.5f * (left + right);
                } else if (end > start) {
                    size_t tail = end - 1;
                    float left = (tail < track.audio.size()) ? track.audio[tail] : 0.0f;
                    float right = (tail < track.audioRight.size()) ? track.audioRight[tail] : left;
                    fftSamples[i] = 0.5f * (left + right);
                }
            }
            float domFreq = computeDominantFrequency(fftSamples.data(), fftSamples.size(), sampleRate, fftBuffer);
            if (domFreq <= 0.0f) {
                track.waveformColor[block] = kWaveformFallbackColor;
            } else {
                track.waveformColor[block] = frequencyToColor(domFreq);
            }
        }
        track.waveformVersion += 1;
    }

    void UpdateWaveformRange(DawTrack& track, size_t startSample, size_t endSample) {
        size_t recordedStart = static_cast<size_t>(track.recordStartSample);
        size_t recordedEnd = recordedStart + track.pendingRecord.size();
        size_t recordedEndRight = recordedStart + track.pendingRecordRight.size();
        size_t combinedLength = std::max(std::max(track.audio.size(), track.audioRight.size()),
                                         std::max(recordedEnd, recordedEndRight));
        if (combinedLength == 0 || endSample <= startSample) return;

        size_t blockCount = (combinedLength + kWaveformBlockSize - 1) / kWaveformBlockSize;
        if (track.waveformMin.size() < blockCount) {
            track.waveformMin.resize(blockCount, 0.0f);
            track.waveformMax.resize(blockCount, 0.0f);
            track.waveformMinRight.resize(blockCount, 0.0f);
            track.waveformMaxRight.resize(blockCount, 0.0f);
        }
        size_t blockStart = startSample / kWaveformBlockSize;
        size_t blockEnd = (endSample + kWaveformBlockSize - 1) / kWaveformBlockSize;
        blockEnd = std::min(blockEnd, blockCount);
        for (size_t block = blockStart; block < blockEnd; ++block) {
            size_t sampleStart = block * kWaveformBlockSize;
            size_t sampleEnd = std::min(sampleStart + kWaveformBlockSize, combinedLength);
            float minValL = 1.0f;
            float maxValL = -1.0f;
            float minValR = 1.0f;
            float maxValR = -1.0f;
            for (size_t i = sampleStart; i < sampleEnd; ++i) {
                float baseL = (i < track.audio.size()) ? track.audio[i] : 0.0f;
                float baseR = (i < track.audioRight.size()) ? track.audioRight[i] : baseL;
                float recL = 0.0f;
                float recR = 0.0f;
                if (i >= recordedStart && i < recordedEnd) {
                    recL = track.pendingRecord[i - recordedStart];
                }
                if (i >= recordedStart && i < recordedEndRight) {
                    recR = track.pendingRecordRight[i - recordedStart];
                } else {
                    recR = recL;
                }
                float vL = (track.recordArmMode == 2 && i >= recordedStart && i < recordedEnd)
                    ? recL
                    : (baseL + recL);
                float vR = (track.recordArmMode == 2 && i >= recordedStart && i < recordedEnd)
                    ? recR
                    : (baseR + recR);
                minValL = std::min(minValL, vL);
                maxValL = std::max(maxValL, vL);
                minValR = std::min(minValR, vR);
                maxValR = std::max(maxValR, vR);
            }
            if (sampleEnd == sampleStart) {
                minValL = 0.0f;
                maxValL = 0.0f;
                minValR = 0.0f;
                maxValR = 0.0f;
            }
            track.waveformMin[block] = minValL;
            track.waveformMax[block] = maxValL;
            track.waveformMinRight[block] = minValR;
            track.waveformMaxRight[block] = maxValR;
        }
        track.waveformVersion += 1;
    }

    void UpdateDawWaveforms(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*) {
    }
}
