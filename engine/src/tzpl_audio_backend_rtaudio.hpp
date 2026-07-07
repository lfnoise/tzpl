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
//  tzpl_audio_backend_rtaudio.hpp
//  audio engine
//

#ifndef tzpl_audio_backend_rtaudio_hpp
#define tzpl_audio_backend_rtaudio_hpp

#include "tzpl_audio_backend.hpp"
#include <memory>

class RtAudio;

namespace engine {

// RtAudio device backend (CoreAudio on macOS, ALSA on Linux). Supports a
// separate input device via a second input-only stream and a staging buffer
// (no clock drift compensation -- see inputAudioCallback in the .cpp).
struct RtAudioBackend : AudioBackend {
    RtAudioBackend();
    ~RtAudioBackend() override;

    void init(Engine* e) override;
    void uninit() override;
    void start() override;
    void stop() override;
    f64 streamTime() override;
    void printDevices() override;

    Engine* engine_ = nullptr;
    std::unique_ptr<RtAudio> rtaudio_;
    // Separate input device support
    std::unique_ptr<RtAudio> inputRtaudio_; // non-null when using a separate input device
    f32* inputStagingBuf_ = nullptr;        // intermediate buffer for separate input device
    // CoreAudio device whose nominal-sample-rate changes we listen for.
    u32 monitoredOutputDeviceID_ = 0;
};

}

#endif /* tzpl_audio_backend_rtaudio_hpp */
