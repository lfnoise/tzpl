// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

//
//  juce_audio_backend.cpp
//  app (JUCE)
//
//  engine::AudioBackend implemented on juce::AudioDeviceManager. The engine
//  processes interleaved f32; JUCE delivers planar buffers, so the callback
//  interleaves input into a staging buffer, runs processAudioBlock, and
//  deinterleaves the result. Unlike the RtAudio backend, a separate input
//  device needs no second stream: AudioDeviceManager handles it natively.
//

#include "juce_audio_backend.hpp"
#include "tzpl_engine.hpp"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <vector>

namespace tzplapp {

using engine::f32;
using engine::f64;
using engine::i64;

namespace {

class JuceAudioBackend : public engine::AudioBackend,
                         private juce::AudioIODeviceCallback {
public:
    JuceAudioBackend() = default;
    ~JuceAudioBackend() override { uninit_(); }

    void init(engine::Engine* e) override {
        engine_ = e;
        auto& sp = e->streamParams_;

        bool defaultOutput = !sp.deviceName || strlen(sp.deviceName) == 0
            || strcmp(sp.deviceName, "default") == 0;
        bool defaultInput = !sp.inputDeviceName || strlen(sp.inputDeviceName) == 0
            || strcmp(sp.inputDeviceName, "default") == 0;

        juce::AudioDeviceManager::AudioDeviceSetup setup;
        if (!defaultOutput) setup.outputDeviceName = sp.deviceName;
        if (sp.inputChannels > 0 && !defaultInput) {
            setup.inputDeviceName = sp.inputDeviceName;
        }
        setup.sampleRate = sp.sampleRate;
        setup.bufferSize = sp.bufferFrames;
        if (sp.firstChannel > 0) {
            setup.useDefaultOutputChannels = false;
            setup.outputChannels.setRange(sp.firstChannel, sp.channels, true);
        }
        if (sp.inputChannels > 0 && sp.firstInputChannel > 0) {
            setup.useDefaultInputChannels = false;
            setup.inputChannels.setRange(sp.firstInputChannel, sp.inputChannels, true);
        }

        juce::String err = deviceManager_.initialise(
            sp.inputChannels, sp.channels, /*savedState=*/nullptr,
            /*selectDefaultDeviceOnFailure=*/defaultOutput, {}, &setup);

        juce::AudioIODevice* dev = deviceManager_.getCurrentAudioDevice();
        if (err.isNotEmpty() || !dev) {
            fprintf(stderr, "JUCE audio init failed: %s\n", err.toRawUTF8());
            throw defaultOutput ? tzpl_errNoAudioDevices : tzpl_errDeviceNotFound;
        }

        // Write the negotiated stream parameters back, like the RtAudio
        // backend does; the engine sizes its silo/limiter buffers from these.
        sp.sampleRate = dev->getCurrentSampleRate();
        sp.bufferFrames = dev->getCurrentBufferSizeSamples();
        int outCh = dev->getActiveOutputChannels().countNumberOfSetBits();
        int inCh = dev->getActiveInputChannels().countNumberOfSetBits();
        if (outCh > 0) sp.channels = outCh;
        sp.inputChannels = inCh;

        // The interleave staging buffers are sized generously in case the
        // device delivers an occasional larger block than negotiated.
        int maxFrames = sp.bufferFrames * 4;
        outScratch_.assign((size_t)maxFrames * sp.channels, 0.f);
        if (sp.inputChannels > 0) {
            inScratch_.assign((size_t)maxFrames * sp.inputChannels, 0.f);
        }
    }

    void uninit() override { uninit_(); }

    void start() override {
        // The device runs from init; callbacks flow only while registered,
        // matching RtAudio's startStream/stopStream semantics.
        deviceManager_.addAudioCallback(this);
    }

    void stop() override {
        // Blocks until any in-flight callback has returned.
        deviceManager_.removeAudioCallback(this);
    }

    f64 streamTime() override {
        // Accumulated processed time, like RtAudio's getStreamTime(): it
        // advances only while running and persists across stop/start.
        return (f64)framesProcessed_.load(std::memory_order_relaxed)
             / engine_->streamParams_.sampleRate;
    }

    engine::u64 deviceXruns() const override {
        // JUCE returns -1 when the device type can't report xruns.
        int n = deviceManager_.getXRunCount();
        return n < 0 ? 0 : (engine::u64)n;
    }

    engine::f64 deviceCpu() const override { return deviceManager_.getCpuUsage(); }

    bool hasTelemetry() const override { return true; }

    void printDevices() override {
        for (auto* type : deviceManager_.getAvailableDeviceTypes()) {
            type->scanForDevices();
            juce::StringArray outs = type->getDeviceNames(false);
            juce::StringArray ins = type->getDeviceNames(true);
            int defaultOut = type->getDefaultDeviceIndex(false);
            int defaultIn = type->getDefaultDeviceIndex(true);
            printf("[%s]\n", type->getTypeName().toRawUTF8());
            for (int i = 0; i < outs.size(); ++i) {
                printf("%2d output device: '%s'%s\n", i, outs[i].toRawUTF8(),
                       i == defaultOut ? "  * default output" : "");
            }
            for (int i = 0; i < ins.size(); ++i) {
                printf("%2d input device:  '%s'%s\n", i, ins[i].toRawUTF8(),
                       i == defaultIn ? "  * default input" : "");
            }
        }
    }

private:
    void uninit_() {
        deviceManager_.removeAudioCallback(this);
        deviceManager_.closeAudioDevice();
    }

    void audioDeviceIOCallbackWithContext(
        float const* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples, juce::AudioIODeviceCallbackContext const&) override
    {
        auto zeroOutput = [&] {
            for (int c = 0; c < numOutputChannels; ++c) {
                if (outputChannelData[c]) {
                    memset(outputChannelData[c], 0, sizeof(f32) * numSamples);
                }
            }
        };

        // The engine's safety limiter is sized for exactly bufferFrames per
        // block, so a block of any other size must not reach it.
        if (numSamples != (int)engine_->streamParams_.bufferFrames
            || (size_t)numSamples * numOutputChannels > outScratch_.size()) {
            // Silence for a whole block. Count it: this used to be an
            // undetectable dropout.
            engine_->stats_.badBlockSizeCount.fetch_add(1, std::memory_order_relaxed);
            engine_->stats_.dropoutCount.fetch_add(1, std::memory_order_relaxed);
            zeroOutput();
            return;
        }

        try {
            f32 const* in = nullptr;
            if (numInputChannels > 0 && !inScratch_.empty()) {
                for (int c = 0; c < numInputChannels; ++c) {
                    f32 const* src = inputChannelData[c];
                    f32* dst = inScratch_.data() + c;
                    for (int i = 0; i < numSamples; ++i) {
                        dst[(size_t)i * numInputChannels] = src ? src[i] : 0.f;
                    }
                }
                in = inScratch_.data();
            }

            f64 t = (f64)framesProcessed_.load(std::memory_order_relaxed)
                  / engine_->streamParams_.sampleRate;
            engine::processAudioBlock(engine_, in, outScratch_.data(),
                                      (unsigned int)numSamples, t);
            framesProcessed_.fetch_add(numSamples, std::memory_order_relaxed);

            for (int c = 0; c < numOutputChannels; ++c) {
                f32* dst = outputChannelData[c];
                if (!dst) continue;
                f32 const* src = outScratch_.data() + c;
                for (int i = 0; i < numSamples; ++i) {
                    dst[i] = src[(size_t)i * numOutputChannels];
                }
            }
        } catch (...) {
            fprintf(stderr, "exception on real time thread");
            engine_->stats_.rtExceptionCount.fetch_add(1, std::memory_order_relaxed);
            engine_->stats_.dropoutCount.fetch_add(1, std::memory_order_relaxed);
            zeroOutput();
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    // Keeps the JUCE runtime (MessageManager) alive for the device manager.
    // Ref-counted, so it nests with the initialiser in runGui().
    juce::ScopedJuceInitialiser_GUI juceInit_;
    engine::Engine* engine_ = nullptr;
    juce::AudioDeviceManager deviceManager_;
    std::vector<f32> inScratch_, outScratch_;
    std::atomic<i64> framesProcessed_{0};
};

}

// Defined in juce_gui_main.cpp. Must run before JuceAudioBackend's
// ScopedJuceInitialiser_GUI creates the MessageManager: JUCE only installs
// its NSApp delegate (Cmd+Q / Dock-quit -> systemRequestedQuit routing)
// when an application factory is registered at that moment.
void registerJuceAppFactory();

std::unique_ptr<engine::AudioBackend> makeJuceAudioBackend() {
    registerJuceAppFactory();
    return std::make_unique<JuceAudioBackend>();
}

}
