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
