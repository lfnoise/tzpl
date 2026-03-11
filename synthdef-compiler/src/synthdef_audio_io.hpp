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
//  synthdef_audio_io.hpp
//  synthdef-compiler
//
//  Created by James McCartney on 8/5/24.
//

#ifndef synthdef_audio_io_hpp
#define synthdef_audio_io_hpp

#include "synthdef_types2.hpp"
#include "RtAudio.h"

namespace synthdef {

struct AudioStreamParameters {
    std::string deviceName;
    int channels;
    int firstChannel;
    int bufferFrames;
    f64 sampleRate;
};

struct AudioEngine {
    std::unique_ptr<RtAudio> rtaudio_;
    AudioStreamParameters streamParams;
    std::function<void(AudioEngine*, f32*, f32 const*, int)> processFun;
    
    AudioEngine();
};

void initAudio(AudioEngine* e);
void startAudio(AudioEngine* e);
void stopAudio(AudioEngine* e);
void uninitAudio(AudioEngine* e);

}

#endif /* synthdef_audio_io_hpp */
