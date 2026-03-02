//
//  jscs_client_interface.hpp
//  audio engine
//
//  Created by James McCartney on 2/4/21.
//

#ifndef jscs_client_interface_h
#define jscs_client_interface_h

#include "jscs_plugin_abi.h"
#include "jscs_common.hpp"

class RtAudio;

namespace engine {

enum Enable { kOff, kOn };

enum SchedPolicy {
    schedImmediate,
    schedBetterLateThanNever,
    schedOnTimeOnly,
};

enum FadeCurve {
    fadeLinear,
    fadeExponential,
    fadeSmoothstep,
    fadeEqualPower,
    fadeOutIn,
    fadeEaseInCubic,
    fadeEaseOutCubic,
};


struct Engine;
struct NodeDef;
struct Buffer;

using LoadNodeDefFun = void (*)(Engine* e);

struct AudioStreamParameters {
    const char* deviceName;
    int channels;
    int firstChannel;
    int bufferFrames;
    f64 sampleRate;
};

struct PortAddr {
    i64 nodeID; // the node containing the port.
    int index; // the index of the port, starting at zero.
};

struct EngineConfig {
    int numSilos = 4;
};

Engine* newEngine(EngineConfig const& config, AudioStreamParameters& streamParams);
void freeEngine(Engine* e);

RtAudio* getRTAudio(Engine* e);

void startAudio(Engine* e);
void stopAudio(Engine* e);

void printDevices(Engine* e);

bool isAudioRunning(Engine* e);

void masterGain(Engine* e, f32 gain); // post safety limiter
void safetyLimiter(Engine* e, Enable onoff); // default is on.

// non real time commands
bool loadDefs(Engine* e, const char* dirPath);
bool loadDef(Engine* e, const char* dirPath, const char* defName);
    
f64 getStreamTime(Engine* e); // audio must be initialized, else exception.

// real time commands

// Real time commands are queued up and are not submitted for execution
// until either doBundle() or sched() are called.
// This allows to ensure that a group of commands can be executed atomically in real time.

// begin a bundle to be executed on a silo.
jscs_SErr begin(Engine* e, int silo);

// sched() schedules all bundled commands at the given time in seconds since start.
// If SchedPolicy is immediate, the command will be executed on as soon as received. time is ignored.
// If SchedPolicy is onTimeOnly, the command will be discarded if it is too late to be executed on time.
// If SchedPolicy is betterLateThanNever (the default) the command will be executed even if it is too late.
jscs_SErr sched(f64 time, SchedPolicy policy = schedBetterLateThanNever);

// go() is a convenience for calling sched(0., SchedPolicy::immediate)
jscs_SErr go();

// All of the following commands are queued up and are not submitted for execution
// until either go() or sched() are called.
// create or free nodes.
jscs_SErr newNode(const char* defName, i64 nodeID);
jscs_SErr freeNode(i64 nodeID);
jscs_SErr freeAllNodes();

// node connections
jscs_SErr connect(PortAddr src, PortAddr dst, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);
jscs_SErr disconnectInput(PortAddr dst, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);
jscs_SErr disconnectOutput(PortAddr src);
jscs_SErr disconnectNode(i64 nodeID);

// all inputs connected to oldSrc will be moved or crossfaded to newSrc
jscs_SErr reconnectOutput(PortAddr oldSrc, PortAddr newSrc, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);

jscs_SErr replaceNode(i64 oldNodeID, i64 newNodeID, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);

// set the input to a constant value. This will have no effect if the input is normalled internally.
jscs_SErr setInput(PortAddr inPort, int numValues, void* values, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);

// controls
jscs_SErr setControl(i64 nodeID, i64 controlID, int numValues, void* values);

// notes
jscs_SErr allNotesOff(i64 nodeID);
jscs_SErr noteOn(i64 nodeID, int noteID, int length, f32* paramValues);
jscs_SErr noteOff(i64 nodeID, int noteID);
jscs_SErr noteSetParams(i64 nodeID, int noteID, int n, jscs_ParamPair* params);
jscs_SErr noteSetParamRange(i64 nodeID, int noteID, int first, int length, f32* values);

// buffers
Buffer* newBuffer(i64 bufID, int numChannels, i64 length);
Buffer* newBuffer(i64 bufID, const char* path);
jscs_SErr freeBuffer(i64 bufID);
jscs_SErr resizeBuffer(i64 bufID, int numChannels, i64 length);
jscs_SErr loadBuffer(i64 bufID, const char* path, int channelOffset = 0, i64 frameOffset = 0, i64 numFrames = INT64_MAX);
jscs_SErr zeroBuffer(i64 bufID, int channelOffset = 0, i64 frameOffset = 0, i64 numFrames = INT64_MAX);

inline bool isFloat(jscs_ElemType e) {
    switch (e) {
        case jscs_kF32 :
        case jscs_kF64 :
            return true;
        default:
            return false;
    }
}

inline int elemSize(jscs_ElemType e) {
    switch (e) {
        case jscs_kI32 :
            return sizeof(i32);
        case jscs_kF32 :
            return sizeof(f32);
        case jscs_kI64 :
            return sizeof(i64);
        case jscs_kF64 :
            return sizeof(f64);
    }
}

template <typename T>
inline T* getIn(jscs_SynthData* synth, int inletIndex) {
    return (T*)synth->inlets[inletIndex];
}

template <typename T>
inline T* getOut(jscs_SynthData* synth, int outletIndex) {
    return (T*)synth->outlets[outletIndex];
}

inline int calcByteSize(jscs_SignalType const& t) {
    return t.chans * elemSize(t.elem);
}

}

#endif /* jscs_client_interface_h */
