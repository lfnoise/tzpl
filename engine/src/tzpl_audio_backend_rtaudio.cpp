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
//  tzpl_audio_backend_rtaudio.cpp
//  audio engine
//

#include "tzpl_audio_backend_rtaudio.hpp"
#include "tzpl_engine.hpp"
#include "RtAudio.h"
#include <cstring>

#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace engine {

#ifdef __APPLE__

#if defined(MAC_OS_VERSION_12_0) && (MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_12_0)
constexpr AudioObjectPropertyElement kElement = kAudioObjectPropertyElementMain;
#else
constexpr AudioObjectPropertyElement kElement = kAudioObjectPropertyElementMaster;
#endif

// Find the CoreAudio AudioDeviceID for the device RtAudio is using as the
// engine's output. Matches "default" via kAudioHardwarePropertyDefaultOutputDevice
// and named devices by enumerating and comparing kAudioObjectPropertyName.
// Returns kAudioObjectUnknown if not found.
static AudioDeviceID resolveOutputAudioDeviceID(const char* deviceName) {
    bool isDefault = !deviceName || strlen(deviceName) == 0
        || strcmp(deviceName, "default") == 0;

    if (isDefault) {
        AudioObjectPropertyAddress prop = {
            kAudioHardwarePropertyDefaultOutputDevice,
            kAudioObjectPropertyScopeGlobal,
            kElement
        };
        AudioDeviceID devId = kAudioObjectUnknown;
        UInt32 size = sizeof(devId);
        if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop, 0,
                                       nullptr, &size, &devId) != noErr) {
            return kAudioObjectUnknown;
        }
        return devId;
    }

    AudioObjectPropertyAddress listProp = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kElement
    };
    UInt32 dataSize = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &listProp,
                                       0, nullptr, &dataSize) != noErr) {
        return kAudioObjectUnknown;
    }
    UInt32 nDevices = dataSize / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> ids(nDevices);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &listProp, 0,
                                   nullptr, &dataSize, ids.data()) != noErr) {
        return kAudioObjectUnknown;
    }

    AudioObjectPropertyAddress nameProp = {
        kAudioObjectPropertyName,
        kAudioObjectPropertyScopeGlobal,
        kElement
    };
    for (AudioDeviceID id : ids) {
        CFStringRef name = nullptr;
        UInt32 nameSize = sizeof(name);
        if (AudioObjectGetPropertyData(id, &nameProp, 0, nullptr,
                                       &nameSize, &name) != noErr || !name) {
            continue;
        }
        char buf[256];
        bool match = CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8)
                     && strcmp(buf, deviceName) == 0;
        CFRelease(name);
        if (match) return id;
    }
    return kAudioObjectUnknown;
}

// Called by CoreAudio on an internal HAL dispatch queue when the device's
// nominal sample rate changes. Must do as little as possible. We just set
// an atomic flag; processNRTCommands picks it up on the next tick.
static OSStatus sampleRateChangeListener(AudioObjectID /*inObjectID*/,
                                         UInt32 /*inNumAddresses*/,
                                         const AudioObjectPropertyAddress* /*inAddresses*/,
                                         void* inClientData) {
    Engine* e = static_cast<Engine*>(inClientData);
    e->sampleRateChanged_.store(true, std::memory_order_relaxed);
    return noErr;
}

static void installSampleRateListener(RtAudioBackend* b) {
    Engine* e = b->engine_;
    AudioDeviceID devId = resolveOutputAudioDeviceID(e->streamParams_.deviceName);
    if (devId == kAudioObjectUnknown) return;

    AudioObjectPropertyAddress prop = {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kElement
    };
    OSStatus result = AudioObjectAddPropertyListener(devId, &prop,
                                                     sampleRateChangeListener, e);
    if (result == noErr) {
        b->monitoredOutputDeviceID_ = (u32)devId;
    }
}

static void removeSampleRateListener(RtAudioBackend* b) {
    if (b->monitoredOutputDeviceID_ == 0) return;
    AudioObjectPropertyAddress prop = {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kElement
    };
    AudioObjectRemovePropertyListener((AudioDeviceID)b->monitoredOutputDeviceID_,
                                      &prop, sampleRateChangeListener, b->engine_);
    b->monitoredOutputDeviceID_ = 0;
}

#endif // __APPLE__

static void errorCallback(RtAudioErrorType type, const std::string& errorText) {
    printf("error %d '%s'\n", type, errorText.c_str());
}

static int audioCallback(void* outputBuffer, void* inputBuffer,
                         unsigned int numFrames,
                         double streamTime,
                         RtAudioStreamStatus status,
                         void* userData)
{
    RtAudioBackend* b = (RtAudioBackend*)userData;

    // RTAUDIO_OUTPUT_UNDERFLOW / RTAUDIO_INPUT_OVERFLOW: the device ran dry or
    // overran. This is the only place a real dropout is visible, so record it
    // rather than discarding the status as this callback used to.
    if (status) {
        b->engine_->stats_.dropoutCount.fetch_add(1, std::memory_order_relaxed);
    }

    try {
        // For duplex mode, inputBuffer comes from the callback.
        // For separate input device, use the staging buffer.
        f32 const* in = b->inputRtaudio_
            ? (f32 const*)b->inputStagingBuf_
            : (f32 const*)inputBuffer;
        processAudioBlock(b->engine_, in, (f32*)outputBuffer, numFrames, streamTime);
    } catch (...) {
        fprintf(stderr, "exception on real time thread");
    }
    return 0;
}

// Callback for a separate input-only stream.
// Copies hardware input data into the backend's staging buffer.
// NOTE: No clock drift compensation -- the input and output devices run on
// independent sample clocks, so over time samples will be repeated or dropped.
// For drift-free operation, use an aggregate device (macOS) or JACK (Linux).
// A future improvement would be an adaptive resampler with a PLL to track the
// rate difference.
static int inputAudioCallback(void* /*outputBuffer*/, void* inputBuffer,
                              unsigned int numFrames,
                              double /*streamTime*/,
                              RtAudioStreamStatus /*status*/,
                              void* userData)
{
    RtAudioBackend* b = (RtAudioBackend*)userData;
    if (inputBuffer && b->inputStagingBuf_) {
        memcpy(b->inputStagingBuf_, inputBuffer,
               numFrames * b->engine_->streamParams_.inputChannels * sizeof(f32));
    }
    return 0;
}

// Find a device ID by name on a given RtAudio instance.
// Returns -1 if not found.
static int findDeviceByName(RtAudio* rta, const char* name) {
    int n = rta->getDeviceCount();
    for (int i = 0; i < n; ++i) {
        auto info = rta->getDeviceInfo(i);
        if (name == info.name) {
            return i;
        }
    }
    return -1;
}

static std::unique_ptr<RtAudio> makeRtAudio() {
#ifdef __APPLE__
    return std::make_unique<RtAudio>(RtAudio::MACOSX_CORE);
#else
    // Auto-select among the compiled-in APIs -- on Linux: JACK, then Pulse,
    // then ALSA, whichever is available and reports devices.
    return std::make_unique<RtAudio>(RtAudio::UNSPECIFIED);
#endif
}

// Constructed lazily in init(): creating RtAudio probes the machine's audio
// APIs, which a --no-audio / never-started engine must not do (on a system
// with no sound server the probe is noisy, and needless in any case).
RtAudioBackend::RtAudioBackend() {}

RtAudioBackend::~RtAudioBackend() = default;

void RtAudioBackend::init(Engine* e) {
    engine_ = e;
    if (!rtaudio_) rtaudio_ = makeRtAudio();
    auto& rta = rtaudio_;

    int numDevices = rta->getDeviceCount();
    if (numDevices == 0) {
        throw tzpl_errNoAudioDevices;
    }

    // --- Resolve output device ---
    int outputDeviceID = -1;
    if (strcmp(e->streamParams_.deviceName, "default") == 0) {
        outputDeviceID = rta->getDefaultOutputDevice();
    } else {
        outputDeviceID = findDeviceByName(rta.get(), e->streamParams_.deviceName);
        if (outputDeviceID < 0) {
            throw tzpl_errDeviceNotFound;
        }
    }

    RtAudio::StreamParameters outputParams;
    outputParams.deviceId = outputDeviceID;
    outputParams.nChannels = e->streamParams_.channels;
    outputParams.firstChannel = e->streamParams_.firstChannel;

    // --- Determine input configuration ---
    int inputChannels = e->streamParams_.inputChannels;
    bool hasInput = inputChannels > 0;
    bool separateInputDevice = false;

    if (hasInput) {
        const char* inputDevName = e->streamParams_.inputDeviceName;
        bool inputIsDefault = !inputDevName || strlen(inputDevName) == 0;
        bool inputSameAsOutput = inputIsDefault
            || strcmp(inputDevName, e->streamParams_.deviceName) == 0;

        if (!inputSameAsOutput) {
            // Different input device -- need a separate RtAudio instance
            separateInputDevice = true;
        }
    }

    void* userData = (void*)this;

    if (hasInput && !separateInputDevice) {
        // --- Duplex mode: same device for input and output ---
        RtAudio::StreamParameters inputParams;
        inputParams.deviceId = outputDeviceID;
        inputParams.nChannels = inputChannels;
        inputParams.firstChannel = e->streamParams_.firstInputChannel;

        rta->setErrorCallback(errorCallback);
        unsigned int bufferFrames = e->streamParams_.bufferFrames;
        rta->openStream(&outputParams, &inputParams, RTAUDIO_FLOAT32,
                        e->streamParams_.sampleRate, &bufferFrames,
                        audioCallback, userData);

        e->streamParams_.bufferFrames = bufferFrames;
        e->streamParams_.channels = outputParams.nChannels;
        e->streamParams_.inputChannels = inputParams.nChannels;
    } else {
        // --- Output-only stream (or separate input device) ---
        rta->setErrorCallback(errorCallback);
        unsigned int bufferFrames = e->streamParams_.bufferFrames;
        rta->openStream(&outputParams, nullptr, RTAUDIO_FLOAT32,
                        e->streamParams_.sampleRate, &bufferFrames,
                        audioCallback, userData);

        e->streamParams_.bufferFrames = bufferFrames;
        e->streamParams_.channels = outputParams.nChannels;

        if (separateInputDevice) {
            // --- Open a separate input-only stream ---
            const char* inputDevName = e->streamParams_.inputDeviceName;

#ifdef __APPLE__
            inputRtaudio_ = std::make_unique<RtAudio>(RtAudio::MACOSX_CORE);
#else
            inputRtaudio_ = std::make_unique<RtAudio>(RtAudio::UNSPECIFIED);
#endif
            inputRtaudio_->setErrorCallback(errorCallback);

            int inputDeviceID = -1;
            if (strcmp(inputDevName, "default") == 0) {
                inputDeviceID = inputRtaudio_->getDefaultInputDevice();
            } else {
                inputDeviceID = findDeviceByName(inputRtaudio_.get(), inputDevName);
                if (inputDeviceID < 0) {
                    throw tzpl_errDeviceNotFound;
                }
            }

            RtAudio::StreamParameters inputParams;
            inputParams.deviceId = inputDeviceID;
            inputParams.nChannels = inputChannels;
            inputParams.firstChannel = e->streamParams_.firstInputChannel;

            unsigned int inputBufFrames = e->streamParams_.bufferFrames;
            inputRtaudio_->openStream(nullptr, &inputParams, RTAUDIO_FLOAT32,
                                      e->streamParams_.sampleRate, &inputBufFrames,
                                      inputAudioCallback, userData);

            e->streamParams_.inputChannels = inputParams.nChannels;

            // Allocate staging buffer for the input callback to write into
            inputStagingBuf_ = (f32*)calloc(
                e->streamParams_.bufferFrames * inputChannels, sizeof(f32));
        }
    }

#ifdef __APPLE__
    installSampleRateListener(this);
#endif
}

void RtAudioBackend::uninit() {
#ifdef __APPLE__
    removeSampleRateListener(this);
#endif

    // Close separate input stream if present
    if (inputRtaudio_) {
        if (inputRtaudio_->isStreamOpen()) {
            inputRtaudio_->closeStream();
        }
        inputRtaudio_.reset();
    }
    free(inputStagingBuf_);
    inputStagingBuf_ = nullptr;

    if (rtaudio_) rtaudio_->closeStream();
}

void RtAudioBackend::start() {
    if (inputRtaudio_) {
        inputRtaudio_->startStream();
    }
    if (rtaudio_) rtaudio_->startStream();
}

void RtAudioBackend::stop() {
    if (rtaudio_) rtaudio_->stopStream();
    if (inputRtaudio_) {
        inputRtaudio_->stopStream();
    }
}

f64 RtAudioBackend::streamTime() {
    return rtaudio_ ? rtaudio_->getStreamTime() : 0.0;
}

void RtAudioBackend::printDevices() {
    if (!rtaudio_) rtaudio_ = makeRtAudio();
    auto& rta = rtaudio_;
    int n = rta->getDeviceCount();
    for (int i = 0; i < n; ++i) {
        auto info = rta->getDeviceInfo(i);
        printf("%2d device: '%s'\n", i, info.name.c_str());
        printf("   %2d output ch, %2d input ch, %2d max duplex ch.\n",
            info.outputChannels, info.inputChannels, info.duplexChannels);
        if (info.isDefaultOutput) printf("   * This is the default output device.\n");
        if (info.isDefaultInput)  printf("   * This is the default input device.\n");
        printf("   sample rates: ");
        { int i = 0; for (auto sr : info.sampleRates) {
            if (i>0) {
                printf(", ");
                if ((i%8)==0) printf("\n   ");
            }
            printf("%d", sr);
            ++i;
        }}
        printf("\n");
        printf("   preferred sample rate: %d\n", info.preferredSampleRate);
    }
}

}
