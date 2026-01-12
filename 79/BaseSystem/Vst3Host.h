#pragma once

#include <memory>
#include <string>
#include <vector>

#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

struct Vst3UiWindow;
Vst3UiWindow* Vst3UI_CreateWindow(const char* title, int width, int height);
void Vst3UI_ShowWindow(Vst3UiWindow* window);
void Vst3UI_CloseWindow(Vst3UiWindow* window);
void* Vst3UI_GetContentView(Vst3UiWindow* window);
void Vst3UI_SetWindowSize(Vst3UiWindow* window, int width, int height);

struct Vst3Plugin {
    std::string name;
    VST3::Hosting::Module::Ptr module;
    VST3::Hosting::ClassInfo classInfo;
    Steinberg::IPtr<Steinberg::Vst::PlugProvider> provider;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
    Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> processor;
    Steinberg::IPtr<Steinberg::IPlugView> view;
    Steinberg::Vst::HostProcessData processData;
    Steinberg::Vst::EventList eventList;
    Steinberg::Vst::ParameterChanges inputParameterChanges;
    Steinberg::Vst::ProcessContext processContext{};
    Vst3UiWindow* uiWindow = nullptr;
    int inputChannels = 0;
    int outputChannels = 0;
    int inputBusses = 0;
    int outputBusses = 0;
    bool isInstrument = false;
    bool active = false;
};

struct Vst3TrackChain {
    std::vector<Vst3Plugin*> effects;
    std::vector<float> monoInput;
    std::vector<float> monoOutput;
    std::vector<float> bufferA;
    std::vector<float> bufferB;
};

struct Vst3Context {
    bool initialized = false;
    int blockSize = 0;
    float sampleRate = 44100.0f;
    int64_t continuousSamples = 0;
    int lastMidiNote = -1;
    float lastMidiVelocity = 0.0f;
    std::vector<Vst3TrackChain> audioTracks;
    Vst3TrackChain midiFx;
    Vst3Plugin* midiInstrument = nullptr;
    std::vector<std::unique_ptr<Vst3Plugin>> plugins;
    std::vector<VST3::Hosting::Module::Ptr> modules;
    Steinberg::IPtr<Steinberg::Vst::HostApplication> hostApp;
};

namespace Vst3SystemLogic {
    bool ProcessEffectChain(Vst3Context& ctx, Vst3TrackChain& chain, const float* inputMono,
                            float* outputMono, int numFrames, int64_t sampleOffset, bool playing);
    bool ProcessInstrument(Vst3Context& ctx, Vst3Plugin& instrument, float* outputMono,
                           int numFrames, int64_t sampleOffset, bool playing,
                           int activeNote, float velocity);
}
