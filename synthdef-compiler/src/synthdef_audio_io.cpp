//
//  synthdef_audio_io.cpp
//  synthdef-compiler
//
//  Created by James McCartney on 8/5/24.
//

#include "synthdef_audio_io.hpp"
#include <mutex>

namespace synthdef {

enum {
    kAudioOff,
    kAudioInitted,
    kAudioRunning,
};

std::mutex nrt_lock_;
int audioState_ = kAudioOff;

AudioEngine::AudioEngine() 
    : rtaudio_(std::make_unique<RtAudio>(RtAudio::MACOSX_CORE))
{
}

void fillZero(f32* out, f32 const* in, int numFrames, int numChannels) {
    int numSamples = numFrames * numChannels;
    size_t numBytes = sizeof(f32) * numSamples;
    memset(out, 0, numBytes);
}

int audioCallback( void *outputBuffer, void *inputBuffer,
                    unsigned int numFrames,
                    double streamTime,
                    RtAudioStreamStatus status,
                    void *userData )
{    
    try {
        f32 const* in = (f32 const*)inputBuffer;
        f32* out = (f32*)outputBuffer;
        
        AudioEngine* e = (AudioEngine*)userData;
  
        e->processFun(e, out, in, numFrames);
        
//        e->in_ = in;
//        e->out_ = out;
//        e->anchorStreamTime_ = streamTime;
        
        // release silos to work, then .
        // then mix all together.
//        int numSilos = int(e->silos_.size());
//        for (int i = 1; i < numSilos; ++i) {
//            e->silos_[i].done_ = false;
//            dispatch_semaphore_signal(e->silos_[i].run_sem_);
//        }

//        int numSamples = numFrames * e->streamParams.channels;
//        memset(out, 0, sizeof(f32) * numSamples);
  
        

//        e->silos_[0].processFrames(); // silo 0 is done on the main thread.
//
//        e->safetyLimiter_->process(out, e->enableSafetyLimiter_, e->postSafetyGain_);
//        e->anchorSampleTime_ += numFrames;
    } catch (...) {
        fprintf(stderr, "exception on real time thread");
    }
    return 0;
}

void errorCallback( RtAudioErrorType type, const std::string &errorText ) {
    printf("error %d '%s'\n", type, errorText.c_str());
}


void startAudio(AudioEngine* e) {
    std::lock_guard<std::mutex> lck(nrt_lock_);

    if (audioState_ == kAudioRunning) return;
    if (audioState_ != kAudioInitted) {
        throw std::runtime_error("audio not initialized");
    }
    e->rtaudio_->startStream();
    audioState_ = kAudioRunning;
}

void stopAudio(AudioEngine* e) {
    std::lock_guard<std::mutex> lck(nrt_lock_);

    if (audioState_ != kAudioRunning) return;
    e->rtaudio_->stopStream();
    audioState_ = kAudioInitted;
}

void initAudio(AudioEngine* e) {
    //printf(">initAudio\n");
    auto& rta = e->rtaudio_;
        
    int numDevices = rta->getDeviceCount();
    if (numDevices == 0) {
        throw std::runtime_error("no audio devices");
    }
    
    if (0) {
        //int n = rta->getDeviceCount();
        auto deviceIDs = e->rtaudio_->getDeviceIds();
        for (auto i : deviceIDs) {
            auto info = rta->getDeviceInfo(i);
            printf("%2d device: '%s'\n", i, info.name.c_str());
            printf("   %2d output ch, %2d input ch, %2d max duplex ch.\n",
                info.outputChannels, info.inputChannels, info.duplexChannels);
            if (info.isDefaultOutput) printf("   *** This is the default output device.\n");
            if (info.isDefaultInput)  printf("   *** This is the default input device.\n");
            printf("   sample rates: ");
            { int j = 0; for (auto sr : info.sampleRates) {
                if (j>0) {
                    printf(", ");
                    if ((j%8)==0) printf("\n   ");
                }
                printf("%d", sr);
                ++j;
            }}
            printf("\n");
            printf("   preferred sample rate: %d\n", info.preferredSampleRate);
        }
    }
    
    int deviceID = -1;
    if (e->streamParams.deviceName == "default") {
        deviceID = rta->getDefaultOutputDevice();
    } else {
        int n = rta->getDeviceCount();
        for (int i = 0; i < n; ++i) {
            auto info = rta->getDeviceInfo(i);
            if (e->streamParams.deviceName == info.name) {
                deviceID = i;
            }
        }
        if (deviceID < 0) {
            throw std::runtime_error("device not found");
        }
    }
            
    RtAudio::StreamParameters outputParams;
    outputParams.deviceId = deviceID;
    outputParams.nChannels = e->streamParams.channels;
    outputParams.firstChannel = e->streamParams.firstChannel;

    void* userData = (void*)e;

    {
//  ( RtAudio::StreamParameters *outputParameters,
//                                        RtAudio::StreamParameters *inputParameters,
//                                        RtAudioFormat format, unsigned int sampleRate,
//                                        unsigned int *bufferFrames,
//                                        RtAudioCallback callback, void *userData,
//                                        RtAudio::StreamOptions *options )

        unsigned int bufferFrames = e->streamParams.bufferFrames;
        auto err = rta->openStream(&outputParams, nullptr, RTAUDIO_FLOAT32, e->streamParams.sampleRate, &bufferFrames, audioCallback,
            userData, nullptr);
            
        printf("openStream err %d\n", err);
        
        e->streamParams.bufferFrames = bufferFrames;
        e->streamParams.channels = outputParams.nChannels;
    }

//    int byteSize = e->streamParams.bufferFrames * e->streamParams.channels * sizeof(f32);
    
    //e->safetyLimiter_.reset(new SafetyLimiter(streamParams.bufferFrames, streamParams.channels));
//    e->safetyLimiter_ = std::make_unique<SafetyLimiter>(
//        e->streamParams.bufferFrames,
//        e->streamParams.channels,
//        int((.25 * e->streamParams.sampleRate) / e->streamParams.bufferFrames));
 
//    for (Silo& s : e->silos_) {
//        s.sampleTime_ = 0;
//        if (s.index_ > 0) {
//            s.outbuf_ = (f32*)malloc(byteSize);
//        }
//    }
    audioState_ = kAudioInitted;
    //printf("<initAudio\n");
}

void uninitAudio(AudioEngine* e) {
    stopAudio(e); // in case it was running.

    std::lock_guard<std::mutex> lck(nrt_lock_);

    if (audioState_ == kAudioOff) return;

//    for (Silo& s : e->silos_) {
//        if (s.index_ > 0) {
//            free(s.outbuf_);
//            s.outbuf_ = nullptr;
//        }
//    }

//    e->safetyLimiter_.reset();

    e->rtaudio_->closeStream();
    audioState_ = kAudioOff;
}

} // namespace synthdef

