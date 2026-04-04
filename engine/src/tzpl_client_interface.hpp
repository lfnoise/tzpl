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
//  tzpl_client_interface.hpp
//  audio engine
//
//  Created by James McCartney on 2/4/21.
//

#ifndef tzpl_client_interface_h
#define tzpl_client_interface_h

#include "tzpl_plugin_abi.h"
#include "tzpl_common.hpp"

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
// struct Buffer removed -- use tzpl_Buffer from plugin ABI

using LoadNodeDefFun = void (*)(Engine* e);

struct AudioStreamParameters {
    const char* deviceName;
    const char* inputDeviceName = nullptr; // nullptr or "" = same as output device
    int channels;
    int firstChannel;
    int inputChannels = 0;
    int firstInputChannel = 0;
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

// Set the channel offset for a silo's output in the hardware buffer.
// Must be called inside a begin()/go() bundle.
tzpl_SErr channelOffset(i32 offset);

// non real time commands
bool loadDefs(Engine* e, const char* dirPath);
bool loadDef(Engine* e, const char* dirPath, const char* defName);

// Add a synthdef (from tzpl_plugin_abi) to the engine's def table.
void addSynthDef(Engine* e, tzpl_SynthDef const& def);

// Collect the names of all registered node defs.
void listNodeDefs(Engine* e, std::vector<std::string>& names);
    
f64 getStreamTime(Engine* e); // audio must be initialized, else exception.

// real time commands

// Real time commands are queued up and are not submitted for execution
// until either doBundle() or sched() are called.
// This allows to ensure that a group of commands can be executed atomically in real time.

// begin a bundle to be executed on a silo.
tzpl_SErr begin(Engine* e, int silo);

// sched() schedules all bundled commands at the given time in seconds since start.
// If SchedPolicy is immediate, the command will be executed on as soon as received. time is ignored.
// If SchedPolicy is onTimeOnly, the command will be discarded if it is too late to be executed on time.
// If SchedPolicy is betterLateThanNever (the default) the command will be executed even if it is too late.
tzpl_SErr sched(f64 time, SchedPolicy policy = schedBetterLateThanNever);

// go() is a convenience for calling sched(0., SchedPolicy::immediate)
tzpl_SErr go();

// Add a pre-built Command to the current bundle.
// The Command must be heap-allocated; ownership is transferred.
struct Command;
void sendCommand(Command* cmd);

// All of the following commands are queued up and are not submitted for execution
// until either go() or sched() are called.
// create or free nodes.
tzpl_SErr newNode(const char* defName, i64 nodeID);
tzpl_SErr freeNode(i64 nodeID);
tzpl_SErr freeAllNodes();

// node connections
tzpl_SErr connect(PortAddr src, PortAddr dst, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);
tzpl_SErr disconnectInput(PortAddr dst, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);
tzpl_SErr disconnectOutput(PortAddr src);
tzpl_SErr disconnectNode(i64 nodeID);

// all inputs connected to oldSrc will be moved or crossfaded to newSrc
tzpl_SErr reconnectOutput(PortAddr oldSrc, PortAddr newSrc, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);

tzpl_SErr replaceNode(i64 oldNodeID, i64 newNodeID, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);

// set the input to a constant value. This will have no effect if the input is normalled internally.
tzpl_SErr setInput(PortAddr inPort, int numValues, void* values, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);

// controls
tzpl_SErr setControl(i64 nodeID, i64 controlID, int numValues, void* values);

// notes
tzpl_SErr allNotesOff(i64 nodeID);
tzpl_SErr noteOn(i64 nodeID, int noteID, int length, f32* paramValues);
tzpl_SErr noteOff(i64 nodeID, int noteID);
tzpl_SErr noteSetParams(i64 nodeID, int noteID, int n, tzpl_ParamPair* params);
tzpl_SErr noteSetParamRange(i64 nodeID, int noteID, int first, int length, f32* values);

// buffers
tzpl_SErr resizeBuffer(i64 nodeID, i64 bufID, int numChannels, i64 length);
tzpl_SErr loadBuffer(i64 nodeID, i64 bufID, const char* path,
                     int channelOffset = 0, i64 frameOffset = 0, i64 numFrames = INT64_MAX);
tzpl_SErr replaceBuffer(i64 nodeID, i64 bufID, tzpl_Buffer* buffer);

inline bool isFloat(tzpl_ElemType e) {
    switch (e) {
        case tzpl_kF32 :
        case tzpl_kF64 :
            return true;
        default:
            return false;
    }
}

inline int elemSize(tzpl_ElemType e) {
    switch (e) {
        case tzpl_kI32 :
            return sizeof(i32);
        case tzpl_kF32 :
            return sizeof(f32);
        case tzpl_kI64 :
            return sizeof(i64);
        case tzpl_kF64 :
            return sizeof(f64);
    }
}

template <typename T>
inline T* getIn(tzpl_SynthData* synth, int inletIndex) {
    return (T*)synth->inlets[inletIndex];
}

template <typename T>
inline T* getOut(tzpl_SynthData* synth, int outletIndex) {
    return (T*)synth->outlets[outletIndex];
}

inline int calcByteSize(tzpl_SignalType const& t) {
    return t.chans * elemSize(t.elem);
}

}

#endif /* tzpl_client_interface_h */
