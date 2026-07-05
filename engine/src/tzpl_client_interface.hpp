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
    int numTempoClocks = 1; // beat-based TempoClock slots per silo
};

Engine* newEngine(EngineConfig const& config, AudioStreamParameters& streamParams);

// Construct an engine for non-real-time (offline) rendering. No audio device
// is opened; the caller drives processing via renderNRTBlock(). The provided
// AudioStreamParameters fields used are: sampleRate, bufferFrames, channels.
// Other fields (deviceName, input*) are ignored.
Engine* newEngineNRT(EngineConfig const& config, AudioStreamParameters& streamParams);

// Render one block of NRT audio into outBuffer. Writes
// streamParams.bufferFrames * streamParams.channels float32 samples (interleaved).
// Drains NRT command and dead-node queues after the block.
// The engine must have been created via newEngineNRT().
void renderNRTBlock(Engine* e, f32* outBuffer);

// Copy all non-superseded node defs from one engine to another. The new
// engine gets its own NodeDef objects but shares the underlying PortInfo
// arrays. Skips "Audio Out" / "Audio In" since each engine creates its own.
// Each copy retains its own dlopen refcount so the dylib stays loaded even
// if the source engine hot-reloads (and dlcloses) the original.
void copyNodeDefs(Engine* from, Engine* to);

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
void addSynthDef(Engine* e, tzpl_SynthDef const& def, void* dlHandle = nullptr);

// Collect the names of all registered node defs.
void listNodeDefs(Engine* e, std::vector<std::string>& names);

// Control metadata for a registered node def (for UI/introspection).
struct ControlDesc {
    std::string name;
    i64 controlID;
    tzpl_ControlSpec spec;
};

// Collect control metadata for def `defName`. Returns false if no such def.
bool listDefControls(Engine* e, const char* defName, std::vector<ControlDesc>& out);
    
f64 getStreamTime(Engine* e); // audio must be initialized, else exception.

// real time commands

// Real time commands are queued up and are not submitted for execution
// until either go() or sched() are called.
// This allows to ensure that a group of commands can be executed atomically in real time.

// begin a bundle. The target silo is chosen at submit time (go/sched).
tzpl_SErr begin(Engine* e);

// sched() submits the bundle to `silo`, scheduled at the given beat on tempo
// clock `clock` (0 .. numTempoClocks-1). The bundle is late-bound: it fires when
// that clock's beat reaches `beat`, tracking any tempo changes made in the meantime.
// If SchedPolicy is immediate, the bundle executes as soon as received (clock/beat ignored).
// If SchedPolicy is onTimeOnly, the bundle is discarded if the beat is already past.
// If SchedPolicy is betterLateThanNever (the default) it executes even if the beat is past.
//
// Submit validates and materializes every queued command against the chosen
// silo. On the first error the ENTIRE bundle is discarded (nothing is applied)
// and that error is returned. The bundle is always closed on return, success
// or failure. Beat-scheduled bundles are re-checked on the audio thread at
// execution time; a command whose target no longer exists then is silently
// dropped.
tzpl_SErr sched(int silo, int clock, f64 beat, SchedPolicy policy = schedBetterLateThanNever);

// go() is a convenience for immediate execution (SchedPolicy::immediate).
tzpl_SErr go(int silo);

// Tempo control. These are broadcast to clock slot `clock` on every silo so the
// slot stays in sync across silos. Not part of a begin()/sched() bundle.
// setTempo sets the tempo immediately; schedTempoChange ramps toward targetBPM
// over rampBeats beats, starting when the clock reaches atBeat.
tzpl_SErr setTempo(Engine* e, int clock, f64 bpm);
tzpl_SErr schedTempoChange(Engine* e, int clock, f64 atBeat, f64 targetBPM, f64 rampBeats);

// Query the current beat / tempo (BPM) of a clock slot (read from silo 0; all
// silos are kept in sync). Returns 0 on an invalid clock index.
f64 clockBeats(Engine* e, int clock);
f64 clockTempoBPM(Engine* e, int clock);

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
tzpl_SErr disconnectSource(PortAddr src, PortAddr dst, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);
tzpl_SErr disconnectOutput(PortAddr src);
tzpl_SErr disconnectNode(i64 nodeID);

// all inputs connected to oldSrc will be moved or crossfaded to newSrc
tzpl_SErr reconnectOutput(PortAddr oldSrc, PortAddr newSrc, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);

tzpl_SErr replaceNode(i64 oldNodeID, i64 newNodeID, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);

// set the input to a constant value. This will have no effect if the input is normalled internally.
// The values are captured with the caller's element type and converted to the
// port's element type at submit if they differ.
tzpl_SErr setInput(PortAddr inPort, int numValues, f32 const* values, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);
tzpl_SErr setInput(PortAddr inPort, int numValues, f64 const* values, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);
tzpl_SErr setInput(PortAddr inPort, int numValues, i32 const* values, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);
tzpl_SErr setInput(PortAddr inPort, int numValues, i64 const* values, f64 xfadeTime = 0., FadeCurve curve = fadeLinear);

// controls
tzpl_SErr setControl(i64 nodeID, i64 controlID, int numValues, f32 const* values);
tzpl_SErr setControl(i64 nodeID, i64 controlID, int numValues, f64 const* values);
tzpl_SErr setControl(i64 nodeID, i64 controlID, int numValues, i32 const* values);
tzpl_SErr setControl(i64 nodeID, i64 controlID, int numValues, i64 const* values);

// Signal taps: RT -> NRT readback of a node outlet for meters/scopes.
// tapOutlet is a bundled command (record between begin() and go()/sched()):
// it creates tap `tapID` (caller-chosen, unique) on outlet `outlet` of node
// `nodeID`. mode 0 = meter (peak/rms only), 1 = scope (peak/rms + a sample
// FIFO of channel 0). The outlet's element type must be f32. untap removes
// the tap and frees it.
tzpl_SErr tapOutlet(i64 nodeID, int outlet, i64 tapID, int mode);
tzpl_SErr untap(i64 tapID);

// Tap reads (any thread). Unknown tapIDs read as false / 0 / no samples.
bool tapExists(Engine* e, i64 tapID);
f32 tapPeak(Engine* e, i64 tapID);
f32 tapRms(Engine* e, i64 tapID);
// Channel count captured by the tap (scope frames are interleaved).
int tapChans(Engine* e, i64 tapID);
// Drain up to maxSamples pending scope samples into dst; returns the count.
// Scope data is interleaved frames of tapChans() channels; drain in
// multiples of the channel count to keep frame alignment across drains.
int tapDrain(Engine* e, i64 tapID, f32* dst, int maxSamples);

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
