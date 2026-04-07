#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace SoundtrackSystemLogic {

    namespace {
        struct WavInfo {
            uint16_t audioFormat = 0;
            uint16_t numChannels = 0;
            uint32_t sampleRate = 0;
            uint16_t bitsPerSample = 0;
            uint32_t dataSize = 0;
            std::streampos dataPos = 0;
        };

        bool readChunkHeader(std::ifstream& file, char outId[4], uint32_t& outSize) {
            if (!file.read(outId, 4)) return false;
            if (!file.read(reinterpret_cast<char*>(&outSize), sizeof(outSize))) return false;
            return true;
        }

        bool readWavInfo(std::ifstream& file, WavInfo& info) {
            char riff[4] = {0};
            if (!file.read(riff, 4)) return false;
            uint32_t riffSize = 0;
            if (!file.read(reinterpret_cast<char*>(&riffSize), sizeof(riffSize))) return false;
            char wave[4] = {0};
            if (!file.read(wave, 4)) return false;
            if (std::string(riff, 4) != "RIFF" || std::string(wave, 4) != "WAVE") return false;

            bool fmtFound = false;
            bool dataFound = false;
            while (file && (!fmtFound || !dataFound)) {
                char chunkId[4] = {0};
                uint32_t chunkSize = 0;
                if (!readChunkHeader(file, chunkId, chunkSize)) break;
                std::string id(chunkId, 4);
                if (id == "fmt ") {
                    fmtFound = true;
                    uint16_t audioFormat = 0;
                    uint16_t numChannels = 0;
                    uint32_t sampleRate = 0;
                    uint32_t byteRate = 0;
                    uint16_t blockAlign = 0;
                    uint16_t bitsPerSample = 0;
                    file.read(reinterpret_cast<char*>(&audioFormat), sizeof(audioFormat));
                    file.read(reinterpret_cast<char*>(&numChannels), sizeof(numChannels));
                    file.read(reinterpret_cast<char*>(&sampleRate), sizeof(sampleRate));
                    file.read(reinterpret_cast<char*>(&byteRate), sizeof(byteRate));
                    file.read(reinterpret_cast<char*>(&blockAlign), sizeof(blockAlign));
                    file.read(reinterpret_cast<char*>(&bitsPerSample), sizeof(bitsPerSample));
                    if (chunkSize > 16) {
                        file.seekg(chunkSize - 16, std::ios::cur);
                    }
                    info.audioFormat = audioFormat;
                    info.numChannels = numChannels;
                    info.sampleRate = sampleRate;
                    info.bitsPerSample = bitsPerSample;
                } else if (id == "data") {
                    dataFound = true;
                    info.dataSize = chunkSize;
                    info.dataPos = file.tellg();
                    file.seekg(chunkSize, std::ios::cur);
                } else {
                    file.seekg(chunkSize, std::ios::cur);
                }
            }
            return fmtFound && dataFound;
        }

        bool loadWavMono(const std::string& path, std::vector<float>& outSamples, uint32_t& outRate) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) return false;
            WavInfo info;
            if (!readWavInfo(file, info)) return false;
            if (info.dataSize == 0 || info.numChannels == 0) return false;

            outRate = info.sampleRate;
            file.seekg(info.dataPos);

            if (info.audioFormat == 3 && info.bitsPerSample == 32) {
                size_t frameCount = info.dataSize / (sizeof(float) * info.numChannels);
                outSamples.assign(frameCount, 0.0f);
                for (size_t i = 0; i < frameCount; ++i) {
                    float sample = 0.0f;
                    for (uint16_t ch = 0; ch < info.numChannels; ++ch) {
                        float v = 0.0f;
                        file.read(reinterpret_cast<char*>(&v), sizeof(float));
                        sample += v;
                    }
                    outSamples[i] = sample / static_cast<float>(info.numChannels);
                }
                return true;
            }

            if (info.audioFormat == 1 && info.bitsPerSample == 16) {
                size_t frameCount = info.dataSize / (sizeof(int16_t) * info.numChannels);
                outSamples.assign(frameCount, 0.0f);
                for (size_t i = 0; i < frameCount; ++i) {
                    int32_t sum = 0;
                    for (uint16_t ch = 0; ch < info.numChannels; ++ch) {
                        int16_t v = 0;
                        file.read(reinterpret_cast<char*>(&v), sizeof(int16_t));
                        sum += v;
                    }
                    float sample = static_cast<float>(sum) / (static_cast<float>(info.numChannels) * 32768.0f);
                    outSamples[i] = sample;
                }
                return true;
            }

            return false;
        }

        std::string toLower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return s;
        }

        bool parseBool(const std::string& s, bool fallback) {
            std::string v = toLower(s);
            if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
            if (v == "0" || v == "false" || v == "no" || v == "off") return false;
            return fallback;
        }

        bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            if (std::holds_alternative<std::string>(it->second)) {
                return parseBool(std::get<std::string>(it->second), fallback);
            }
            return fallback;
        }

        std::string getRegistryString(const BaseSystem& baseSystem, const std::string& key, const std::string& fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<std::string>(it->second)) return std::get<std::string>(it->second);
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second) ? "true" : "false";
            return fallback;
        }

        double getRegistryDouble(const BaseSystem& baseSystem, const std::string& key, double fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<std::string>(it->second)) {
                try {
                    return std::stod(std::get<std::string>(it->second));
                } catch (...) {
                    return fallback;
                }
            }
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second) ? 1.0 : 0.0;
            return fallback;
        }

        bool hasWavExtension(const std::filesystem::path& path) {
            return toLower(path.extension().string()) == ".wav";
        }

        bool hasCkExtension(const std::filesystem::path& path) {
            return toLower(path.extension().string()) == ".ck";
        }

        bool isSupportedSoundtrackFile(const std::filesystem::path& path) {
            return hasWavExtension(path) || hasCkExtension(path);
        }

        void scanSoundtrackFolder(const std::string& folder,
                                  std::vector<std::string>& outTracks,
                                  std::string& lastScanError) {
            outTracks.clear();
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path root(folder);
            if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
                std::string key = "missing:" + folder;
                if (lastScanError != key) {
                    std::cerr << "SoundtrackSystem: soundtrack folder missing '" << folder << "'." << std::endl;
                    lastScanError = key;
                }
                return;
            }

            fs::directory_iterator it(root, ec);
            fs::directory_iterator end;
            for (; !ec && it != end; it.increment(ec)) {
                const fs::directory_entry& entry = *it;
                if (!entry.is_regular_file(ec)) continue;
                if (!isSupportedSoundtrackFile(entry.path())) continue;
                outTracks.push_back(entry.path().string());
            }

            if (ec) {
                std::string key = "scan:" + folder + ":" + ec.message();
                if (lastScanError != key) {
                    std::cerr << "SoundtrackSystem: failed scanning '" << folder
                              << "' (" << ec.message() << ")." << std::endl;
                    lastScanError = key;
                }
                outTracks.clear();
                return;
            }

            std::sort(outTracks.begin(), outTracks.end());
            lastScanError.clear();
        }

        bool prepareCkSoundtrackScript(const std::string& sourcePath,
                                       int soundtrackChannel,
                                       std::string& outScriptPath,
                                       std::string& lastPrepError) {
            namespace fs = std::filesystem;
            std::ifstream in(sourcePath, std::ios::binary);
            if (!in.is_open()) {
                std::string key = "open:" + sourcePath;
                if (lastPrepError != key) {
                    std::cerr << "SoundtrackSystem: failed to open soundtrack script '"
                              << sourcePath << "'." << std::endl;
                    lastPrepError = key;
                }
                return false;
            }

            std::string scriptSource((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            if (scriptSource.empty()) {
                std::string key = "empty:" + sourcePath;
                if (lastPrepError != key) {
                    std::cerr << "SoundtrackSystem: soundtrack script is empty '"
                              << sourcePath << "'." << std::endl;
                    lastPrepError = key;
                }
                return false;
            }

            const std::string routeTarget = "=> dac.chan(" + std::to_string(soundtrackChannel) + ")";
            scriptSource = std::regex_replace(
                scriptSource,
                std::regex(R"(=>\s*dac\s*\.\s*chan\s*\(\s*[^)]+\s*\))"),
                routeTarget
            );
            scriptSource = std::regex_replace(
                scriptSource,
                std::regex(R"(=>\s*dac\s*\.\s*(left|right)\b)"),
                routeTarget
            );
            scriptSource = std::regex_replace(
                scriptSource,
                std::regex(R"(=>\s*dac(?!\s*\.\s*(chan\s*\(|left\b|right\b)))"),
                routeTarget
            );

            std::error_code ec;
            fs::path sourceAbs = fs::absolute(fs::path(sourcePath), ec);
            if (ec || sourceAbs.empty()) {
                ec.clear();
                sourceAbs = fs::path(sourcePath);
            }
            fs::path generatedDir = sourceAbs.parent_path();
            if (generatedDir.empty()) {
                generatedDir = fs::path("Procedures") / "soundtrack";
            }
            fs::create_directories(generatedDir, ec);
            if (ec) {
                generatedDir = fs::temp_directory_path(ec) / "salamander_soundtrack_ck";
                ec.clear();
                fs::create_directories(generatedDir, ec);
                if (ec) {
                    std::string key = "mkdir:" + generatedDir.string() + ":" + ec.message();
                    if (lastPrepError != key) {
                        std::cerr << "SoundtrackSystem: failed creating generated soundtrack script folder ("
                                  << ec.message() << ")." << std::endl;
                        lastPrepError = key;
                    }
                    return false;
                }
            }

            std::string mtimeToken = "0";
            auto mtime = fs::last_write_time(sourcePath, ec);
            if (!ec) {
                const auto mtimeCount = static_cast<long long>(mtime.time_since_epoch().count());
                mtimeToken = std::to_string(mtimeCount);
            } else {
                ec.clear();
            }
            const std::string hashKey = sourcePath + "|" + std::to_string(soundtrackChannel) + "|" + mtimeToken;
            const size_t hashValue = std::hash<std::string>{}(hashKey);
            const fs::path generatedPath = generatedDir / (".salamander_soundtrack_wrapped_" + std::to_string(hashValue) + ".ck");

            bool shouldWrite = true;
            std::ifstream existing(generatedPath, std::ios::binary);
            if (existing.is_open()) {
                std::string existingText((std::istreambuf_iterator<char>(existing)), std::istreambuf_iterator<char>());
                if (existingText == scriptSource) {
                    shouldWrite = false;
                }
            }
            if (shouldWrite) {
                std::ofstream out(generatedPath, std::ios::binary | std::ios::trunc);
                if (!out.is_open()) {
                    std::string key = "write:" + generatedPath.string();
                    if (lastPrepError != key) {
                        std::cerr << "SoundtrackSystem: failed writing generated soundtrack script '"
                                  << generatedPath.string() << "'." << std::endl;
                        lastPrepError = key;
                    }
                    return false;
                }
                out.write(scriptSource.data(), static_cast<std::streamsize>(scriptSource.size()));
                out.close();
            }

            outScriptPath = generatedPath.string();
            lastPrepError.clear();
            return true;
        }

        double randomRange(std::mt19937& rng, double minValue, double maxValue) {
            if (maxValue <= minValue) return minValue;
            std::uniform_real_distribution<double> dist(minValue, maxValue);
            return dist(rng);
        }

        enum class PlaylistTrackKind {
            None = 0,
            Wav = 1,
            Ck = 2
        };
    }

    void UpdateSoundtracks(BaseSystem& baseSystem, std::vector<Entity>&, float dt, PlatformWindowHandle) {
        if (!baseSystem.audio) return;
        AudioContext& audio = *baseSystem.audio;
        static std::string lastRayPath;
        static std::string lastHeadPath;
        static std::string lastRayError;
        static std::string lastHeadError;
        static std::vector<std::string> playlistTracks;
        static std::string playlistFolder;
        static std::string playlistScanError;
        static std::string playlistPrepError;
        static size_t lastTrackIndex = std::numeric_limits<size_t>::max();
        static double playlistRescanTimerSec = 0.0;
        static PlaylistTrackKind activeTrackKind = PlaylistTrackKind::None;
        static std::string activeTrackPath;
        static double activeTrackElapsedSec = 0.0;
        static double activeTrackTargetSec = 0.0;
        static bool fadeOutActive = false;
        static double fadeRemainingSec = 0.0;
        static bool warnedNoTracks = false;
        static std::mt19937 rng(std::random_device{}());

        auto loadTrack = [&](const std::string& path,
                             std::vector<float>& buffer,
                             uint32_t& sampleRate,
                             double& pos,
                             const char* label,
                             std::string& lastPath,
                             std::string& lastError) {
            if (path.empty()) return;
            if (!buffer.empty() && lastPath == path) return;
            std::vector<float> samples;
            uint32_t rate = 0;
            if (!loadWavMono(path, samples, rate)) {
                if (lastError != path) {
                    std::cerr << "SoundtrackSystem: failed to load " << label << " '" << path
                              << "' (expect mono 16-bit PCM or 32-bit float WAV)." << std::endl;
                    lastError = path;
                }
                return;
            }
            buffer = std::move(samples);
            sampleRate = rate;
            pos = 0.0;
            lastPath = path;
        };

        loadTrack(audio.rayTestPath, audio.rayTestBuffer, audio.rayTestSampleRate, audio.rayTestPos,
                  "ray track", lastRayPath, lastRayError);

        bool playlistEnabled = getRegistryBool(baseSystem, "SoundtrackPlaylistEnabled", true);
        double gapMinSec = getRegistryDouble(baseSystem, "SoundtrackGapMinSeconds", 120.0);
        double gapMaxSec = getRegistryDouble(baseSystem, "SoundtrackGapMaxSeconds", 240.0);
        double soundtrackGain = getRegistryDouble(baseSystem, "SoundtrackGain", 1.0);
        double playlistTrackMinSec = getRegistryDouble(baseSystem, "SoundtrackTrackMinSeconds", 180.0);
        double playlistTrackMaxSec = getRegistryDouble(baseSystem, "SoundtrackTrackMaxSeconds", 300.0);
        double soundtrackFadeSeconds = getRegistryDouble(baseSystem, "SoundtrackFadeSeconds", 8.0);
        bool skipRequested = getRegistryBool(baseSystem, "SoundtrackNextRequested", false);
        if (skipRequested && baseSystem.registry) {
            (*baseSystem.registry)["SoundtrackNextRequested"] = false;
        }
        if (gapMinSec < 0.0) gapMinSec = 0.0;
        if (gapMaxSec < 0.0) gapMaxSec = 0.0;
        if (gapMaxSec < gapMinSec) std::swap(gapMinSec, gapMaxSec);
        if (soundtrackGain < 0.0) soundtrackGain = 0.0;
        if (soundtrackGain > 4.0) soundtrackGain = 4.0;
        if (playlistTrackMinSec < 5.0) playlistTrackMinSec = 5.0;
        if (playlistTrackMaxSec < playlistTrackMinSec) playlistTrackMaxSec = playlistTrackMinSec;
        if (soundtrackFadeSeconds < 0.05) soundtrackFadeSeconds = 0.05;
        if (soundtrackFadeSeconds > playlistTrackMinSec * 0.8) {
            soundtrackFadeSeconds = std::max(0.05, playlistTrackMinSec * 0.8);
        }

        double dtSec = (std::isfinite(dt) && dt > 0.0f) ? static_cast<double>(dt) : 0.0;

        auto stopActiveTrack = [&]() {
            audio.headTrackActive = false;
            audio.headTrackGain = 0.0f;
            audio.headTrackPos = static_cast<double>(audio.headTrackBuffer.size());
            audio.soundtrackChuckStopRequested = true;
            audio.soundtrackChuckStartRequested = false;
            audio.soundtrackChuckActive = false;
            audio.soundtrackChuckGain = 0.0f;
            activeTrackKind = PlaylistTrackKind::None;
            activeTrackPath.clear();
            activeTrackElapsedSec = 0.0;
            activeTrackTargetSec = 0.0;
            fadeOutActive = false;
            fadeRemainingSec = 0.0;
        };

        auto applyTrackGain = [&](double gain) {
            const float clamped = static_cast<float>(std::clamp(gain, 0.0, 4.0));
            if (activeTrackKind == PlaylistTrackKind::Wav) {
                audio.headTrackGain = clamped;
            } else if (activeTrackKind == PlaylistTrackKind::Ck) {
                audio.soundtrackChuckGain = clamped;
            }
        };

        auto beginTrackWindow = [&]() {
            activeTrackElapsedSec = 0.0;
            activeTrackTargetSec = randomRange(rng, playlistTrackMinSec, playlistTrackMaxSec);
            fadeOutActive = false;
            fadeRemainingSec = 0.0;
        };

        if (!playlistEnabled) {
            audio.soundtrackChuckStopRequested = true;
            audio.soundtrackChuckStartRequested = false;
            audio.soundtrackChuckActive = false;
            audio.soundtrackChuckGain = 0.0f;
            loadTrack(audio.headTrackPath, audio.headTrackBuffer, audio.headTrackSampleRate, audio.headTrackPos,
                      "head track", lastHeadPath, lastHeadError);
            if (skipRequested) {
                audio.headTrackActive = false;
            }
            audio.headTrackGain = static_cast<float>(soundtrackGain);
            activeTrackKind = PlaylistTrackKind::None;
            activeTrackPath.clear();
            activeTrackElapsedSec = 0.0;
            activeTrackTargetSec = 0.0;
            fadeOutActive = false;
            fadeRemainingSec = 0.0;
            warnedNoTracks = false;
            return;
        }

        std::string desiredFolder = getRegistryString(baseSystem, "SoundtrackFolder", "Procedures/soundtrack");
        if (playlistFolder != desiredFolder) {
            playlistFolder = desiredFolder;
            playlistTracks.clear();
            playlistRescanTimerSec = 0.0;
            warnedNoTracks = false;
            lastTrackIndex = std::numeric_limits<size_t>::max();
        }

        playlistRescanTimerSec -= dtSec;
        if (playlistTracks.empty() || playlistRescanTimerSec <= 0.0) {
            scanSoundtrackFolder(playlistFolder, playlistTracks, playlistScanError);
            playlistRescanTimerSec = 2.0;
        }

        bool wavPlaying = audio.headTrackActive && !audio.headTrackBuffer.empty();
        bool ckPlaying = audio.soundtrackChuckActive;
        bool currentlyPlaying = wavPlaying || ckPlaying;

        if (skipRequested) {
            stopActiveTrack();
            currentlyPlaying = false;
        }

        if (activeTrackKind != PlaylistTrackKind::None && currentlyPlaying) {
            activeTrackElapsedSec += dtSec;
            if (!fadeOutActive && activeTrackElapsedSec >= activeTrackTargetSec) {
                fadeOutActive = true;
                fadeRemainingSec = soundtrackFadeSeconds;
            }
            double fadeMul = 1.0;
            if (fadeOutActive) {
                fadeRemainingSec = std::max(0.0, fadeRemainingSec - dtSec);
                fadeMul = std::clamp(fadeRemainingSec / soundtrackFadeSeconds, 0.0, 1.0);
                if (fadeRemainingSec <= 0.0) {
                    stopActiveTrack();
                    currentlyPlaying = false;
                }
            }
            if (activeTrackKind != PlaylistTrackKind::None) {
                applyTrackGain(soundtrackGain * fadeMul);
            }
        } else if (activeTrackKind != PlaylistTrackKind::None && !currentlyPlaying) {
            activeTrackKind = PlaylistTrackKind::None;
            activeTrackPath.clear();
            activeTrackElapsedSec = 0.0;
            activeTrackTargetSec = 0.0;
            fadeOutActive = false;
            fadeRemainingSec = 0.0;
        }

        if (!currentlyPlaying && !playlistTracks.empty()) {
            size_t count = playlistTracks.size();
            std::uniform_int_distribution<size_t> dist(0, count - 1);
            size_t pickIndex = dist(rng);
            if (count > 1 && lastTrackIndex < count && pickIndex == lastTrackIndex) {
                pickIndex = (pickIndex + 1 + (dist(rng) % (count - 1))) % count;
            }

            const std::string chosenTrack = playlistTracks[pickIndex];
            const std::string ext = toLower(std::filesystem::path(chosenTrack).extension().string());
            if (ext == ".ck") {
                std::string generatedTrackPath;
                if (!prepareCkSoundtrackScript(chosenTrack, audio.soundtrackChuckChannel, generatedTrackPath, playlistPrepError)) {
                    playlistTracks.erase(playlistTracks.begin() + static_cast<std::ptrdiff_t>(pickIndex));
                    if (lastTrackIndex >= playlistTracks.size()) {
                        lastTrackIndex = std::numeric_limits<size_t>::max();
                    }
                } else {
                    audio.headTrackActive = false;
                    audio.headTrackGain = 0.0f;
                    audio.soundtrackChuckScriptPath = generatedTrackPath;
                    audio.soundtrackChuckGain = static_cast<float>(soundtrackGain);
                    audio.soundtrackChuckStopRequested = false;
                    audio.soundtrackChuckStartRequested = true;
                    audio.soundtrackChuckActive = true;
                    activeTrackKind = PlaylistTrackKind::Ck;
                    activeTrackPath = chosenTrack;
                    beginTrackWindow();
                    lastTrackIndex = pickIndex;
                    warnedNoTracks = false;
                    std::cout << "SoundtrackSystem: playing script '" << chosenTrack << "' for "
                              << static_cast<int>(std::round(activeTrackTargetSec)) << "s." << std::endl;
                }
            } else {
                audio.soundtrackChuckStopRequested = true;
                audio.soundtrackChuckStartRequested = false;
                audio.soundtrackChuckActive = false;
                audio.soundtrackChuckGain = 0.0f;
                audio.headTrackPath = chosenTrack;
                loadTrack(audio.headTrackPath, audio.headTrackBuffer, audio.headTrackSampleRate, audio.headTrackPos,
                          "head track", lastHeadPath, lastHeadError);

                bool loaded = (lastHeadPath == chosenTrack)
                    && !audio.headTrackBuffer.empty()
                    && audio.headTrackSampleRate > 0;
                if (!loaded) {
                    playlistTracks.erase(playlistTracks.begin() + static_cast<std::ptrdiff_t>(pickIndex));
                    if (lastTrackIndex >= playlistTracks.size()) {
                        lastTrackIndex = std::numeric_limits<size_t>::max();
                    }
                } else {
                    audio.headTrackPos = 0.0;
                    audio.headTrackGain = static_cast<float>(soundtrackGain);
                    audio.headTrackLoop = true;
                    audio.headTrackActive = true;
                    activeTrackKind = PlaylistTrackKind::Wav;
                    activeTrackPath = chosenTrack;
                    beginTrackWindow();
                    lastTrackIndex = pickIndex;
                    warnedNoTracks = false;
                    std::cout << "SoundtrackSystem: playing '" << chosenTrack << "' for "
                              << static_cast<int>(std::round(activeTrackTargetSec)) << "s." << std::endl;
                }
            }
        }

        if (!currentlyPlaying && playlistTracks.empty() && !warnedNoTracks) {
            std::cerr << "SoundtrackSystem: no soundtrack .wav/.ck files found in '"
                      << playlistFolder << "'." << std::endl;
            warnedNoTracks = true;
        }

        if (!currentlyPlaying && activeTrackKind == PlaylistTrackKind::None) {
            // Optional gap support while idle in timed mode.
            if (gapMaxSec > 0.0 && gapMaxSec >= gapMinSec) {
                // Keep compatibility with previous gap controls by delaying only when idle.
                static bool gapArmed = false;
                static double gapRemaining = 0.0;
                if (!gapArmed) {
                    gapRemaining = randomRange(rng, gapMinSec, gapMaxSec);
                    gapArmed = true;
                } else {
                    gapRemaining = std::max(0.0, gapRemaining - dtSec);
                    if (gapRemaining <= 0.0) {
                        gapArmed = false;
                    }
                }
                if (gapArmed) {
                    return;
                }
            }
        }

        // If playlist has tracks and nothing active, let next update pick/start one.
        if (!currentlyPlaying && !playlistTracks.empty() && activeTrackKind == PlaylistTrackKind::None) {
            // no-op; start occurs at top of next frame
        }
    }
}
