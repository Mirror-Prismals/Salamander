#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#include "json.hpp"
#include "BaseSystem/Vst3Host.h"

namespace Vst3SystemLogic {
    namespace {
        using json = nlohmann::json;
        using PluginDescriptor = std::pair<VST3::Hosting::Module::Ptr, VST3::Hosting::ClassInfo>;

        struct Vst3Config {
            std::vector<std::vector<std::string>> audioTrackFx;
            std::string midiInstrument;
            std::vector<std::string> midiFx;
        };

        std::string getExecutableDir() {
#if defined(__APPLE__)
            uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            if (size > 0) {
                std::string buffer(size, '\0');
                if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
                    return std::filesystem::path(buffer.c_str()).parent_path().string();
                }
            }
#elif defined(__linux__)
            char buffer[4096] = {0};
            ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
            if (len > 0) {
                buffer[len] = '\0';
                return std::filesystem::path(buffer).parent_path().string();
            }
#endif
            return std::filesystem::current_path().string();
        }

        void ensureChainBuffers(Vst3TrackChain& chain, int blockSize) {
            if (blockSize <= 0) return;
            chain.monoInput.assign(blockSize, 0.0f);
            chain.monoOutput.assign(blockSize, 0.0f);
            chain.bufferA.assign(static_cast<size_t>(blockSize) * 2, 0.0f);
            chain.bufferB.assign(static_cast<size_t>(blockSize) * 2, 0.0f);
        }

        bool loadConfig(const std::filesystem::path& path, Vst3Config& out) {
            if (!std::filesystem::exists(path)) {
                std::cout << "VST3 config not found at " << path << std::endl;
                return false;
            }
            std::ifstream f(path);
            if (!f.is_open()) {
                std::cerr << "Failed to open VST3 config at " << path << std::endl;
                return false;
            }
            json data;
            try {
                data = json::parse(f);
            } catch (...) {
                std::cerr << "Failed to parse VST3 config at " << path << std::endl;
                return false;
            }

            out.audioTrackFx.assign(DawContext::kTrackCount, {});
            if (data.contains("audio_tracks") && data["audio_tracks"].is_array()) {
                const auto& arr = data["audio_tracks"];
                size_t count = std::min(arr.size(), static_cast<size_t>(DawContext::kTrackCount));
                for (size_t i = 0; i < count; ++i) {
                    if (arr[i].is_array()) {
                        for (const auto& item : arr[i]) {
                            if (item.is_string()) {
                                out.audioTrackFx[i].push_back(item.get<std::string>());
                            }
                        }
                    } else if (arr[i].is_object() && arr[i].contains("effects")) {
                        for (const auto& item : arr[i]["effects"]) {
                            if (item.is_string()) {
                                out.audioTrackFx[i].push_back(item.get<std::string>());
                            }
                        }
                    }
                }
            }

            if (data.contains("midi_track") && data["midi_track"].is_object()) {
                const auto& midi = data["midi_track"];
                if (midi.contains("instrument") && midi["instrument"].is_string()) {
                    out.midiInstrument = midi["instrument"].get<std::string>();
                }
                if (midi.contains("effects") && midi["effects"].is_array()) {
                    for (const auto& item : midi["effects"]) {
                        if (item.is_string()) {
                            out.midiFx.push_back(item.get<std::string>());
                        }
                    }
                }
            }
            return true;
        }

        std::vector<std::filesystem::path> scanVst3Modules(const std::filesystem::path& dir) {
            std::vector<std::filesystem::path> modules;
            if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
                std::cout << "USER_VST3 not found at " << dir << std::endl;
                return modules;
            }
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (!entry.is_directory()) continue;
                auto path = entry.path();
                if (path.extension() == ".vst3") {
                    modules.push_back(path);
                }
            }
            return modules;
        }

        std::unordered_map<std::string, Vst3AvailablePlugin> buildPluginMap(
            Vst3Context& ctx, const std::vector<std::filesystem::path>& modulePaths) {
            std::unordered_map<std::string, Vst3AvailablePlugin> map;
            ctx.availablePlugins.clear();
            for (const auto& modulePath : modulePaths) {
                std::string error;
                auto module = VST3::Hosting::Module::create(modulePath.string(), error);
                if (!module) {
                    std::cerr << "VST3: Failed to load module " << modulePath << ": " << error << std::endl;
                    continue;
                }
                std::cout << "VST3: Scanning " << modulePath << std::endl;
                ctx.modules.push_back(module);
                auto factory = module->getFactory();
                for (const auto& info : factory.classInfos()) {
                    bool isInstrument = false;
                    for (const auto& sub : info.subCategories()) {
                        if (sub == "Instrument" || sub.rfind("Instrument|", 0) == 0) {
                            isInstrument = true;
                            break;
                        }
                    }
                    std::cout << "  " << info.name() << " [" << info.category() << "]"
                              << (isInstrument ? " (Instrument)" : "") << std::endl;
                    if (info.category() != kVstAudioEffectClass) continue;
                    Vst3AvailablePlugin available;
                    available.name = info.name();
                    available.module = module;
                    available.classInfo = info;
                    available.isInstrument = isInstrument;
                    map.emplace(available.name, available);
                }
            }
            ctx.availablePlugins.reserve(map.size());
            for (const auto& [_, available] : map) {
                ctx.availablePlugins.push_back(available);
            }
            std::sort(ctx.availablePlugins.begin(), ctx.availablePlugins.end(),
                      [](const Vst3AvailablePlugin& a, const Vst3AvailablePlugin& b) {
                          return a.name < b.name;
                      });
            return map;
        }

        bool configurePlugin(Vst3Context& ctx, Vst3Plugin& plugin) {
            if (!plugin.component || !plugin.processor) return false;

            plugin.inputBusses = plugin.component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
            plugin.outputBusses = plugin.component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);
            if (plugin.outputBusses <= 0) {
                std::cerr << "VST3: " << plugin.name << " has no output buses." << std::endl;
                return false;
            }
            if (!plugin.isInstrument && plugin.inputBusses <= 0) {
                std::cerr << "VST3: " << plugin.name << " has no input buses." << std::endl;
                return false;
            }

            Steinberg::Vst::BusInfo info{};
            if (plugin.inputBusses > 0 && plugin.component->getBusInfo(Steinberg::Vst::kAudio,
                                                                       Steinberg::Vst::kInput, 0, info) == Steinberg::kResultOk) {
                plugin.inputChannels = std::min(info.channelCount, 2);
                if (info.channelCount > 2) {
                    std::cout << "VST3: " << plugin.name << " input channels clamped to 2." << std::endl;
                }
            }
            if (plugin.component->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, info) == Steinberg::kResultOk) {
                plugin.outputChannels = std::min(info.channelCount, 2);
                if (info.channelCount > 2) {
                    std::cout << "VST3: " << plugin.name << " output channels clamped to 2." << std::endl;
                }
            }

            for (int i = 0; i < plugin.inputBusses; ++i) {
                plugin.component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, i, true);
            }
            for (int i = 0; i < plugin.outputBusses; ++i) {
                plugin.component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, i, true);
            }

            plugin.processData.prepare(*plugin.component, 0, Steinberg::Vst::kSample32);
            Steinberg::Vst::ProcessSetup setup{Steinberg::Vst::kRealtime, Steinberg::Vst::kSample32,
                                               ctx.blockSize, ctx.sampleRate};
            if (plugin.processor->setupProcessing(setup) != Steinberg::kResultOk) {
                std::cerr << "VST3: setupProcessing failed for " << plugin.name << std::endl;
                return false;
            }
            if (plugin.component->setActive(true) != Steinberg::kResultOk) {
                std::cerr << "VST3: setActive failed for " << plugin.name << std::endl;
                return false;
            }
            plugin.processor->setProcessing(true);
            plugin.active = true;
            return true;
        }

        void openPluginUIInternal(Vst3Plugin& plugin) {
            if (!plugin.controller) return;
            plugin.view = plugin.controller->createView(Steinberg::Vst::ViewType::kEditor);
            if (!plugin.view) return;
            if (plugin.view->isPlatformTypeSupported(Steinberg::kPlatformTypeNSView) != Steinberg::kResultTrue) {
                plugin.view.reset();
                return;
            }
            Steinberg::ViewRect size;
            if (plugin.view->getSize(&size) != Steinberg::kResultTrue) {
                size = Steinberg::ViewRect(0, 0, 640, 480);
            }
            plugin.uiWindow = Vst3UI_CreateWindow(plugin.name.c_str(), size.getWidth(), size.getHeight());
            if (!plugin.uiWindow) {
                plugin.view.reset();
                return;
            }
            void* parent = Vst3UI_GetContentView(plugin.uiWindow);
            if (!parent || plugin.view->attached(parent, Steinberg::kPlatformTypeNSView) != Steinberg::kResultTrue) {
                Vst3UI_CloseWindow(plugin.uiWindow);
                plugin.uiWindow = nullptr;
                plugin.view.reset();
                return;
            }
            Vst3UI_ShowWindow(plugin.uiWindow);
            plugin.uiVisible = true;
        }

        Vst3Plugin* createPlugin(Vst3Context& ctx, const PluginDescriptor& desc, bool instrument) {
            auto plugin = std::make_unique<Vst3Plugin>();
            plugin->module = desc.first;
            plugin->classInfo = desc.second;
            plugin->name = desc.second.name();
            plugin->isInstrument = instrument;
            auto factory = plugin->module->getFactory();
            plugin->provider = Steinberg::owned(new Steinberg::Vst::PlugProvider(factory, plugin->classInfo, true));
            if (!plugin->provider->initialize()) {
                return nullptr;
            }
            plugin->component = Steinberg::owned(plugin->provider->getComponent());
            plugin->controller = Steinberg::owned(plugin->provider->getController());
            plugin->processor = Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor>(plugin->component);
            if (!configurePlugin(ctx, *plugin)) {
                return nullptr;
            }
            ctx.plugins.push_back(std::move(plugin));
            return ctx.plugins.back().get();
        }

        void shutdownPlugin(Vst3Plugin& plugin) {
            if (plugin.view) {
                plugin.view->removed();
                plugin.view.reset();
            }
        if (plugin.uiWindow) {
            Vst3UI_CloseWindow(plugin.uiWindow);
            plugin.uiWindow = nullptr;
        }
        plugin.uiVisible = false;
            if (plugin.processor) {
                plugin.processor->setProcessing(false);
            }
            if (plugin.component) {
                plugin.component->setActive(false);
            }
            plugin.processData.unprepare();
            plugin.active = false;
        }
    } // namespace

    void Vst3_OpenPluginUI(Vst3Plugin& plugin) {
        if (plugin.uiWindow) {
            if (!plugin.uiVisible) {
                Vst3UI_ShowWindow(plugin.uiWindow);
                plugin.uiVisible = true;
            }
            return;
        }
        openPluginUIInternal(plugin);
    }

    bool RemovePluginFromTrack(Vst3Context& ctx, Vst3Plugin* plugin, int trackIndex, int audioTrackCount);

    bool AddPluginToTrack(Vst3Context& ctx, const Vst3AvailablePlugin& available, int trackIndex, int audioTrackCount) {
        if (trackIndex < 0) return false;
        if (trackIndex < audioTrackCount) {
            if (available.isInstrument) {
                std::cerr << "VST3: Instrument " << available.name << " cannot be added to an audio track." << std::endl;
                return false;
            }
            if (auto* plugin = createPlugin(ctx, {available.module, available.classInfo}, false)) {
                if (trackIndex >= 0 && trackIndex < static_cast<int>(ctx.audioTracks.size())) {
                    ctx.audioTracks[trackIndex].effects.push_back(plugin);
                    return true;
                }
            }
            return false;
        }
        if (available.isInstrument) {
            if (ctx.midiInstrument) {
                if (ctx.midiInstrument->uiWindow && ctx.midiInstrument->uiVisible) {
                    Vst3UI_HideWindow(ctx.midiInstrument->uiWindow);
                    ctx.midiInstrument->uiVisible = false;
                }
                ctx.midiInstrument = nullptr;
            }
            ctx.midiInstrument = createPlugin(ctx, {available.module, available.classInfo}, true);
            return ctx.midiInstrument != nullptr;
        }
        if (auto* plugin = createPlugin(ctx, {available.module, available.classInfo}, false)) {
            ctx.midiFx.effects.push_back(plugin);
            return true;
        }
        return false;
    }

    bool RemovePluginFromTrack(Vst3Context& ctx, Vst3Plugin* plugin, int trackIndex, int audioTrackCount) {
        if (!plugin || trackIndex < 0) return false;
        bool removed = false;
        if (trackIndex < audioTrackCount) {
            if (trackIndex >= 0 && trackIndex < static_cast<int>(ctx.audioTracks.size())) {
                auto& chain = ctx.audioTracks[trackIndex].effects;
                auto it = std::find(chain.begin(), chain.end(), plugin);
                if (it != chain.end()) {
                    chain.erase(it);
                    removed = true;
                }
            }
        } else {
            if (ctx.midiInstrument == plugin) {
                ctx.midiInstrument = nullptr;
                removed = true;
            } else {
                auto it = std::find(ctx.midiFx.effects.begin(), ctx.midiFx.effects.end(), plugin);
                if (it != ctx.midiFx.effects.end()) {
                    ctx.midiFx.effects.erase(it);
                    removed = true;
                }
            }
        }

        if (!removed) return false;
        for (auto it = ctx.plugins.begin(); it != ctx.plugins.end(); ++it) {
            if (it->get() == plugin) {
                shutdownPlugin(**it);
                ctx.plugins.erase(it);
                break;
            }
        }
        return true;
    }

    bool ProcessEffectChain(Vst3Context& ctx, Vst3TrackChain& chain, const float* inputMono,
                            float* outputMono, int numFrames, int64_t sampleOffset, bool playing) {
        if (chain.effects.empty()) return false;
        if (!inputMono || !outputMono || numFrames <= 0) return false;
        if (chain.bufferA.size() < static_cast<size_t>(numFrames) * 2) return false;
        if (chain.bufferB.size() < static_cast<size_t>(numFrames) * 2) return false;
        if (chain.monoOutput.size() < static_cast<size_t>(numFrames)) return false;

        float* inL = chain.bufferA.data();
        float* inR = inL + numFrames;
        for (int i = 0; i < numFrames; ++i) {
            inL[i] = inputMono[i];
            inR[i] = inputMono[i];
        }
        float* outL = chain.bufferB.data();
        float* outR = outL + numFrames;

        for (auto* plugin : chain.effects) {
            if (!plugin || !plugin->active || !plugin->processor) continue;
            if (plugin->inputBusses <= 0 || plugin->outputBusses <= 0) continue;

            plugin->eventList.clear();
            plugin->inputParameterChanges.clearQueue();
            plugin->processContext.sampleRate = ctx.sampleRate;
            plugin->processContext.projectTimeSamples = sampleOffset;
            plugin->processContext.continousTimeSamples = ctx.continuousSamples;
            plugin->processContext.state = (playing ? Steinberg::Vst::ProcessContext::kPlaying : 0)
                | Steinberg::Vst::ProcessContext::kContTimeValid;

            plugin->processData.processMode = Steinberg::Vst::kRealtime;
            plugin->processData.symbolicSampleSize = Steinberg::Vst::kSample32;
            plugin->processData.numSamples = numFrames;
            plugin->processData.inputEvents = &plugin->eventList;
            plugin->processData.inputParameterChanges = &plugin->inputParameterChanges;
            plugin->processData.outputEvents = nullptr;
            plugin->processData.outputParameterChanges = nullptr;
            plugin->processData.processContext = &plugin->processContext;

            float* inPtrs[2] = {inL, inR};
            float* outPtrs[2] = {outL, outR};
            if (plugin->inputChannels > 0) {
                plugin->processData.setChannelBuffers(Steinberg::Vst::kInput, 0, inPtrs, plugin->inputChannels);
            }
            if (plugin->outputChannels > 0) {
                plugin->processData.setChannelBuffers(Steinberg::Vst::kOutput, 0, outPtrs, plugin->outputChannels);
            }
            if (plugin->processor->process(plugin->processData) != Steinberg::kResultOk) {
                std::copy(inL, inL + numFrames, outL);
                std::copy(inR, inR + numFrames, outR);
            }
            if (plugin->outputChannels == 1) {
                std::copy(outL, outL + numFrames, outR);
            }
            std::swap(inL, outL);
            std::swap(inR, outR);
        }

        for (int i = 0; i < numFrames; ++i) {
            outputMono[i] = 0.5f * (inL[i] + inR[i]);
        }
        return true;
    }

    bool ProcessInstrument(Vst3Context& ctx, Vst3Plugin& instrument, float* outputMono,
                           int numFrames, int64_t sampleOffset, bool playing,
                           int activeNote, float velocity) {
        if (!instrument.active || !instrument.processor) return false;
        if (instrument.outputBusses <= 0 || instrument.outputChannels <= 0) return false;
        if (ctx.midiFx.bufferA.size() < static_cast<size_t>(numFrames) * 2) return false;
        if (ctx.midiFx.bufferB.size() < static_cast<size_t>(numFrames) * 2) return false;

        float* outL = ctx.midiFx.bufferA.data();
        float* outR = outL + numFrames;
        std::fill(outL, outL + numFrames, 0.0f);
        std::fill(outR, outR + numFrames, 0.0f);
        float* inL = ctx.midiFx.bufferB.data();
        float* inR = inL + numFrames;
        std::fill(inL, inL + numFrames, 0.0f);
        std::fill(inR, inR + numFrames, 0.0f);

        instrument.eventList.clear();
        instrument.inputParameterChanges.clearQueue();
        if (ctx.lastMidiNote >= 0 && (activeNote < 0 || activeNote != ctx.lastMidiNote)) {
            Steinberg::Vst::Event e{};
            e.type = Steinberg::Vst::Event::kNoteOffEvent;
            e.busIndex = 0;
            e.sampleOffset = 0;
            e.flags = Steinberg::Vst::Event::kIsLive;
            e.noteOff.channel = 0;
            e.noteOff.pitch = ctx.lastMidiNote;
            e.noteOff.velocity = 0.0f;
            instrument.eventList.addEvent(e);
        }
        if (activeNote >= 0 && activeNote != ctx.lastMidiNote) {
            Steinberg::Vst::Event e{};
            e.type = Steinberg::Vst::Event::kNoteOnEvent;
            e.busIndex = 0;
            e.sampleOffset = 0;
            e.flags = Steinberg::Vst::Event::kIsLive;
            e.noteOn.channel = 0;
            e.noteOn.pitch = activeNote;
            e.noteOn.velocity = std::clamp(velocity, 0.0f, 1.0f);
            instrument.eventList.addEvent(e);
        }
        ctx.lastMidiNote = activeNote;
        ctx.lastMidiVelocity = velocity;

        instrument.processContext.sampleRate = ctx.sampleRate;
        instrument.processContext.projectTimeSamples = sampleOffset;
        instrument.processContext.continousTimeSamples = ctx.continuousSamples;
        instrument.processContext.state = (playing ? Steinberg::Vst::ProcessContext::kPlaying : 0)
            | Steinberg::Vst::ProcessContext::kContTimeValid;

        instrument.processData.processMode = Steinberg::Vst::kRealtime;
        instrument.processData.symbolicSampleSize = Steinberg::Vst::kSample32;
        instrument.processData.numSamples = numFrames;
        instrument.processData.inputEvents = &instrument.eventList;
        instrument.processData.inputParameterChanges = &instrument.inputParameterChanges;
        instrument.processData.outputEvents = nullptr;
        instrument.processData.outputParameterChanges = nullptr;
        instrument.processData.processContext = &instrument.processContext;

        float* inPtrs[2] = {inL, inR};
        float* outPtrs[2] = {outL, outR};
        if (instrument.inputChannels > 0) {
            instrument.processData.setChannelBuffers(Steinberg::Vst::kInput, 0, inPtrs, instrument.inputChannels);
        }
        instrument.processData.setChannelBuffers(Steinberg::Vst::kOutput, 0, outPtrs, instrument.outputChannels);
        if (instrument.processor->process(instrument.processData) != Steinberg::kResultOk) {
            return false;
        }
        if (instrument.outputChannels == 1) {
            std::copy(outL, outL + numFrames, outR);
        }
        if (outputMono) {
            for (int i = 0; i < numFrames; ++i) {
                outputMono[i] = 0.5f * (outL[i] + outR[i]);
            }
        }
        return true;
    }

    void InitializeVst3(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow*) {
        if (!baseSystem.audio) return;
        if (!baseSystem.vst3) baseSystem.vst3 = std::make_unique<Vst3Context>();
        Vst3Context& ctx = *baseSystem.vst3;
        if (ctx.initialized) return;

        AudioContext& audio = *baseSystem.audio;
        ctx.blockSize = audio.chuckBufferFrames > 0 ? static_cast<int>(audio.chuckBufferFrames) : 512;
        ctx.sampleRate = audio.sampleRate > 0.0f ? audio.sampleRate : 44100.0f;
        ctx.audioTracks.assign(DawContext::kTrackCount, {});
        ensureChainBuffers(ctx.midiFx, ctx.blockSize);
        for (auto& chain : ctx.audioTracks) {
            ensureChainBuffers(chain, ctx.blockSize);
        }

        ctx.hostApp = Steinberg::owned(new Steinberg::Vst::HostApplication());
        Steinberg::Vst::PluginContextFactory::instance().setPluginContext(ctx.hostApp);

        std::filesystem::path userVst = std::filesystem::path(getExecutableDir()) / "USER_VST3";
        auto modulePaths = scanVst3Modules(userVst);
        auto pluginMap = buildPluginMap(ctx, modulePaths);

        std::filesystem::path configPath = std::filesystem::path("Procedures") / "vst3.json";
        Vst3Config config;
        bool hasConfig = loadConfig(configPath, config);
        if (!hasConfig) {
            audio.vst3 = &ctx;
            ctx.initialized = true;
            std::cout << "VST3 system initialized with " << ctx.plugins.size() << " plugins." << std::endl;
            return;
        }

        for (size_t i = 0; i < config.audioTrackFx.size() && i < ctx.audioTracks.size(); ++i) {
            for (const auto& name : config.audioTrackFx[i]) {
                auto it = pluginMap.find(name);
                if (it == pluginMap.end()) {
                    std::cerr << "VST3: Plugin not found: " << name << std::endl;
                    continue;
                }
                if (auto* plugin = createPlugin(ctx, {it->second.module, it->second.classInfo}, false)) {
                    ctx.audioTracks[i].effects.push_back(plugin);
                }
            }
        }

        if (!config.midiInstrument.empty()) {
            auto it = pluginMap.find(config.midiInstrument);
            if (it == pluginMap.end()) {
                std::cerr << "VST3: Instrument not found: " << config.midiInstrument << std::endl;
            } else {
                ctx.midiInstrument = createPlugin(ctx, {it->second.module, it->second.classInfo}, true);
            }
        }
        for (const auto& name : config.midiFx) {
            auto it = pluginMap.find(name);
            if (it == pluginMap.end()) {
                std::cerr << "VST3: Plugin not found: " << name << std::endl;
                continue;
            }
            if (auto* plugin = createPlugin(ctx, {it->second.module, it->second.classInfo}, false)) {
                ctx.midiFx.effects.push_back(plugin);
            }
        }

        audio.vst3 = &ctx;
        ctx.initialized = true;
        std::cout << "VST3 system initialized with " << ctx.plugins.size() << " plugins." << std::endl;
    }

    void UpdateVst3(BaseSystem&, std::vector<Entity>&, float, GLFWwindow*) {
        // UI wiring will be added later; plugin processing happens on the audio thread.
    }

    void CleanupVst3(BaseSystem& baseSystem, std::vector<Entity>&, float, GLFWwindow*) {
        if (!baseSystem.vst3) return;
        Vst3Context& ctx = *baseSystem.vst3;
        for (auto& plugin : ctx.plugins) {
            shutdownPlugin(*plugin);
        }
        ctx.plugins.clear();
        ctx.audioTracks.clear();
        ctx.midiFx.effects.clear();
        ctx.midiInstrument = nullptr;
        ctx.availablePlugins.clear();
        ctx.browserCacheBuilt = false;
        ctx.browserLevel = nullptr;
        ctx.browserWorldIndex = -1;
        ctx.browserListCount = 0;
        ctx.browserInstanceIds.clear();
        ctx.browserGhostId = -1;
        ctx.browserCollapsed = false;
        ctx.browserScroll = 0.0f;
        ctx.browserDragIndex = -1;
        ctx.browserDragging = false;
        Steinberg::Vst::PluginContextFactory::instance().setPluginContext(nullptr);
        ctx.hostApp.reset();
        ctx.initialized = false;
        if (baseSystem.audio) {
            baseSystem.audio->vst3 = nullptr;
        }
    }
}
