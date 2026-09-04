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
//  tzpl_audio_backend.hpp
//  audio engine
//

#ifndef tzpl_audio_backend_hpp
#define tzpl_audio_backend_hpp

#include "tzpl_common.hpp"

namespace engine {

struct Engine;

// Audio device backend: owns the audio device(s) and the RT callback that
// drives processAudioBlock(). The engine keeps its own state machine
// (audioState_, muteGain_) and the silo/limiter buffers; a backend only
// opens, starts, stops and closes streams. NRT engines have no backend.
struct AudioBackend {
    virtual ~AudioBackend() = default;

    // Open the device(s). May adjust e->streamParams_ (bufferFrames,
    // channels, inputChannels) to what the device negotiated. Retains e
    // for the backend's lifetime. Throws tzpl_SErr on failure.
    virtual void init(Engine* e) = 0;
    // Close the device(s). Called with streams stopped.
    virtual void uninit() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual f64 streamTime() = 0;
    virtual void printDevices() = 0;

    // Device-reported telemetry for the performance monitor, polled from the
    // GUI thread at a few Hz. Defaulted so a backend that has none compiles
    // unchanged; hasTelemetry() distinguishes "0 xruns" from "can't tell".
    // Note deviceCpu covers the whole device callback, including the
    // backend's own de/interleaving, so it reads higher than the engine's own
    // loadPercent -- report them separately rather than reconciling them.
    virtual u64  deviceXruns() const { return 0; }
    virtual f64  deviceCpu() const { return 0.; }
    virtual bool hasTelemetry() const { return false; }
};

// Deviceless backend: accepts the requested stream parameters unchanged and
// never opens or probes an audio device. Used for --no-audio sessions and
// environments with no sound hardware (CI, containers): the engine comes up
// normally, offline/NRT rendering works, and engineStart() simply has no
// device to drive.
struct NullAudioBackend : AudioBackend {
    void init(Engine*) override {}
    void uninit() override {}
    void start() override {}
    void stop() override {}
    f64 streamTime() override { return 0.0; }
    void printDevices() override;
};

// Process one block of audio through the silos: signals worker silos, runs
// silo 0 on the calling thread, applies the safety limiter, and advances
// anchorSampleTime_. `in` may be null when there is no input. Called from a
// backend's RT callback and from the NRT renderer.
void processAudioBlock(Engine* e, f32 const* in, f32* out,
                       unsigned int numFrames, f64 streamTime);

}

#endif /* tzpl_audio_backend_hpp */
