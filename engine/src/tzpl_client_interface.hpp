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
#include "tzpl_audio_backend.hpp"
#include "tzpl_tap.hpp"   // TapMode, TapOwnerKind (used in default arguments)
#include <memory>
#include <span>

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
struct Silo;
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

// Create a real-time engine. `backend` selects the audio device backend;
// nullptr (the default) uses RtAudio (CoreAudio on macOS, ALSA on Linux).
Engine* newEngine(EngineConfig const& config, AudioStreamParameters& streamParams,
                  std::unique_ptr<AudioBackend> backend = nullptr);

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

void startAudio(Engine* e);
void stopAudio(Engine* e);

void printDevices(Engine* e);

bool isAudioRunning(Engine* e);

void masterGain(Engine* e, f32 gain); // post safety limiter
// Panic mute: forces the master stage to zero without touching the gain, so
// unmuting restores the previous level exactly.
void masterMute(Engine* e, bool mute);
bool masterMuted(Engine* e);
void safetyLimiter(Engine* e, Enable onoff); // default is on.

// Current values, for UI that has to show what the engine is doing (a script
// or an OSC message can set either at any time). Read-only and unsynchronized
// -- fine for display, not for RT decisions.
f32 masterGain(Engine* e);
bool safetyLimiterEnabled(Engine* e);

// Set the channel offset for a silo's output in the hardware buffer.
// Must be called inside a begin()/go() bundle.
tzpl_SErr channelOffset(i32 offset);

// non real time commands
bool loadDefs(Engine* e, const char* dirPath);
bool loadDef(Engine* e, const char* dirPath, const char* defName);

// Add a synthdef (from tzpl_plugin_abi) to the engine's def table.
// `bufs` / `tags` / `banks` (optional) carry the plugin's sample buffer
// descriptors, category tags, and sample bank descriptors from its
// "loadBufferDefs" / "loadTags" / "loadSampleBankDefs" symbols;
// `swapSampleBank` is its optional "swapSampleBank" symbol (required for the
// engine to service loadSampleBank/replaceSampleBank on the def's nodes).
void addSynthDef(Engine* e, tzpl_SynthDef const& def, void* dlHandle = nullptr,
                 tzpl_BufferDefList const* bufs = nullptr,
                 tzpl_TagList const* tags = nullptr,
                 tzpl_SampleBankDefList const* banks = nullptr,
                 tzpl_SwapSampleBankFun swapSampleBank = nullptr);

// Collect the names of all registered node defs.
void listNodeDefs(Engine* e, std::vector<std::string>& names);

// Control metadata for a registered node def (for UI/introspection).
struct ControlDesc {
    std::string name;
    i64 controlID;
    tzpl_ControlSpec spec;
    tzpl_SignalType type{};
};

// Collect control metadata for def `defName`. Returns false if no such def.
bool listDefControls(Engine* e, const char* defName, std::vector<ControlDesc>& out);

// Full def metadata for UI/introspection (plugin browser).
struct PortDesc {
    std::string name;
    tzpl_SignalType type{};
};

struct BufferDesc {
    std::string name;
    tzpl_SignalType type{};
    i64 bufID = 0;
};

struct SampleBankDesc {
    std::string name;
    i64 bankID = 0;
};

struct DefDesc {
    std::string name;
    std::vector<PortDesc> ins;
    std::vector<PortDesc> outs;
    std::vector<ControlDesc> controls;
    std::vector<BufferDesc> buffers;
    std::vector<SampleBankDesc> banks;
    std::vector<std::string> tags;  // embedded category tags (may be empty)
};

// Copy the full metadata of def `defName`. Returns false if no such def.
bool getDefDesc(Engine* e, const char* defName, DefDesc& out);

// Copy metadata for all non-superseded defs, sorted by name.
void listDefDescs(Engine* e, std::vector<DefDesc>& out);

// Live-graph snapshot (graph view). Copies the engine's NRT topology
// shadow, which tracks bundles as they EXECUTE (not as they are
// submitted): scheduled bundles appear when their beat fires. Hidden
// helper nodes (mixers/xfaders) never appear; fan-in shows as multiple
// conns into the same inlet.
struct LiveNodeDesc {
    i64 nodeID = 0;
    std::string defName;
};

struct ConnDesc {
    i64 srcNode = 0; int srcPort = 0;
    i64 dstNode = 0; int dstPort = 0;
};

struct GraphDesc {
    u64 generation = 0;             // graphGeneration() sampled with the copy
    std::vector<LiveNodeDesc> nodes; // sorted by nodeID
    std::vector<ConnDesc> conns;
};

// Monotonic counter, bumped whenever any silo's topology changes or a def
// is (re)registered. Lock-free; poll cheaply and re-snapshot on change.
u64 graphGeneration(Engine* e);

// Number of parallel silos (graph views are per-silo).
int numSilos(Engine* e);

// Deep-copy silo `silo`'s topology shadow. Returns false if out of range.
bool getGraphDesc(Engine* e, int silo, GraphDesc& out);

// A loadable plugin dylib discovered on disk (not necessarily loaded).
struct PluginFile {
    std::string name;  // derived from the filename stem ("<name>_synth[_rN]")
    std::string path;  // newest revision found
};

// Scan `dirs` recursively for plugin dylibs, one entry per name (highest
// revision wins within a dir; earlier dirs shadow later ones). Sorted by
// name. Pure directory listing -- never dlopens anything.
void listPluginFiles(std::vector<std::string> const& dirs,
                     std::vector<PluginFile>& out);

// Introspect a plugin file without registering it: dlopen, read the def
// metadata, dlclose. Results (including failures) are cached by path + file
// mtime, so repeated calls never reload the same plugin. Returns false if
// the file is missing or does not export a plugin `load` symbol.
bool getPluginFileDesc(char const* path, DefDesc& out);

// Load one plugin dylib file and register its def with the engine.
bool loadOneDef(Engine* e, const char* path);
    
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

// Snapshot engine clock `clock` for an NRT follower (the lang tempo
// scheduler): current beat position and the seconds until the clock reaches
// `targetBeat` under its CURRENT ramp (in-flight ramps included; tempo
// changes still queued for a future beat are not -- followers re-check).
// Returns false when the clock cannot advance (no engine, slot out of
// range, audio not running), so the follower can fall back to its own
// internal timeline.
bool clockQuery(Engine* e, int clock, f64 targetBeat,
                f64& beatsNow, f64& secsUntil);

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

// Shared input: one process-global table of continuously varying values
// (mouse position in the reserved slots, free slots for user values). Write
// from any non-RT thread; plugin graphs read it at audio rate via the
// sharedIn ugen -- no command traffic, no scheduling. Unlike setControl,
// changes are NOT sample-accurately scheduled and are not bundled. See
// tzpl_SharedInput in tzpl_plugin_abi.h for slot assignments.
tzpl_SharedInput* sharedInput();
tzpl_SErr setSharedInput(int slot, f32 value);
f32 getSharedInput(int slot);

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
// Each silo's RT tap table holds Silo::kMaxTaps taps; tapOutlet returns
// tzpl_errResourceLimit from go()/sched() when the target silo is full.
// `ownerKind`/`ownerSilo` tag the creator for bulk cleanup; the default
// (tapOwnerHost) is never swept by freeTapsByOwner's per-VM forms.
tzpl_SErr tapOutlet(i64 nodeID, int outlet, i64 tapID, int mode,
                    int ownerKind = tapOwnerHost, int ownerSilo = 0);
// Tap the master output bus -- post safety-limiter, post master gain, i.e.
// what the device plays. No node required, so this reaches signal that
// tapOutlet cannot (node 0 "Audio Out" has no outlets). Must be submitted to
// SILO 0: that silo's thread runs the post-limiter section. untap likewise.
// Limited to Engine::kMaxMasterTaps; returns tzpl_errResourceLimit past that.
tzpl_SErr tapMaster(i64 tapID, int mode,
                    int ownerKind = tapOwnerHost, int ownerSilo = 0);
// Must be submitted to the SAME silo the tap was installed on: the removal
// runs on that silo's RT thread, and freeing the slot from anywhere else
// would leave that silo's tap table holding a dangling pointer. Mismatches
// are rejected with tzpl_errSiloOutOfRange.
tzpl_SErr untap(i64 tapID);

// Remove every tap matching an owner filter, submitting one untap bundle per
// owning silo. Returns the number removed.
//
// This is NOT part of a bundle the caller is building -- it opens its own.
// Calling it with a bundle already open removes nothing and returns 0.
//
// `ownerKind` is a TapOwnerKind; tapOwnerAny matches everything, which is the
// "reset the world" form and will also drop taps the app owns (ui widgets,
// graph-view meters), leaving them reading silence until they are recreated.
// tapOwnerSiloVM additionally matches on `ownerSilo`.
int freeTapsByOwner(Engine* e, int ownerKind, int ownerSilo);

// Allocate a process-unique tapID. Use this rather than a private counter so
// independent features (ui widgets, the graph view) can never collide.
i64 allocTapID(Engine* e);

// ---------------------------------------------------------------------------
// Metering & monitoring
// ---------------------------------------------------------------------------

// Master output level, measured post-limiter and post-gain -- what the device
// actually plays. Always available: no tap, node or widget required. `ch` < 0
// asks for the summary across channels. All lock-free; callable from any
// thread. Note that with the safety limiter ENABLED these trail the node taps
// by one block (the limiter carries a block of latency).
int  masterChans(Engine* e);
f32  masterPeak(Engine* e, int ch);
f32  masterRms(Engine* e, int ch);
// Peak with a ~1.5s fall, so a reader polling at any rate sees the true
// maximum instead of whichever block it happened to sample.
f32  masterPeakHold(Engine* e, int ch);
// Monotone count of samples at or over full scale. Latch on a change rather
// than reading and clearing -- there may be several readers.
u32  masterClipCount(Engine* e);
// Clear that count (a monitor's clip indicator being reset). Narrower than
// resetEngineStats, which restarts the timing and dropout counters too.
void resetMasterClip(Engine* e);

struct SiloStatsSnap {
    int index = 0;
    u64 blockCount = 0;
    f64 lastMs = 0, avgMs = 0, maxMs = 0, mixWaitMs = 0;
    f64 loadPercent = 0;          // avgMs as a share of one block's budget
    int numTaps = 0;
    int toNrtDepth = 0, fromNrtDepth = 0, deadNodesDepth = 0;
    bool hasVM = false;
    // Monotone GC counters from the silo's attached VM; take deltas.
    u64 gcStepCount = 0, gcCycles = 0, gcRtStepCount = 0, gcRtMaxNanos = 0;
};

struct EngineStats {
    bool audioRunning = false;
    f64 sampleRate = 0;
    int bufferFrames = 0, channels = 0;
    u64 blockCount = 0;
    f64 blockBudgetMs = 0, blockLastMs = 0, blockAvgMs = 0, blockMaxMs = 0;
    f64 loadPercent = 0, loadPeakPercent = 0;
    u64 overBudgetCount = 0;
    // RT-counted: over-budget blocks, wrong-size blocks, escaped exceptions,
    // and device-reported under/overflows.
    u64 engineDropouts = 0;
    u64 badBlockSizeCount = 0, rtExceptionCount = 0;
    // Device-reported, polled from the backend. Kept separate from
    // engineDropouts: they measure different things, and deviceCpu includes
    // the backend's own per-block work, so it reads higher than loadPercent.
    // Counted from the last resetEngineStats: the driver's own counter is
    // free-running and cannot be zeroed, so this is a difference from the
    // value it held then.
    u64 deviceXruns = 0;
    f64 deviceCpu = 0;
    bool deviceTelemetry = false;
    u32 clipCount = 0;
    f32 limiterGain = 1.f;        // < 1 while the safety limiter is pulling down
    std::vector<SiloStatsSnap> silos;
};

// Snapshot everything under nrt_lock_ (the audio thread never takes it, so a
// GUI poll cannot block audio). Do not call with UIState::mtx held.
void getEngineStats(Engine* e, EngineStats& out);
// Restart every max-since-read and latched counter. blockCount and the GC
// counters stay monotone.
void resetEngineStats(Engine* e);

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

// Real-time-safe tap reads, for a VM running on a silo's own RT thread.
//
// These take no lock and touch no map: the tap is resolved through `s`'s own
// RT tap table (Silo::rt_findTap), which is owned by the calling thread. That
// same thread publishes the values in processTaps, so there is no concurrency
// to guard against -- no torn multichannel read, and no chance of the slot
// being freed underneath the reader.
//
// The cost is scope: a silo sees only ITS OWN taps (silo 0 additionally sees
// master taps). A tap belonging to another silo reads as absent, exactly as
// an unknown tapID does. Cross-silo reads must use the locking forms above,
// from a non-RT thread.
//
// rtTapDrain has the same single-consumer requirement as tapDrain: give each
// consumer its own tap rather than sharing one, or the SPSC FIFO races.
bool rtTapExists(Silo* s, i64 tapID);
f32  rtTapPeak(Silo* s, i64 tapID);
f32  rtTapRms(Silo* s, i64 tapID);
int  rtTapChans(Silo* s, i64 tapID);
int  rtTapDrain(Silo* s, i64 tapID, f32* dst, int maxSamples);

// notes
tzpl_SErr allNotesOff(i64 nodeID);
// Panic: allNotesOff on every note-capable node in the target silo.
tzpl_SErr allNotesOffAll();
// Panic: drop every beat-scheduled command still pending in the target silo.
tzpl_SErr clearSched();
tzpl_SErr noteOn(i64 nodeID, int noteID, int length, f32* paramValues);
tzpl_SErr noteOff(i64 nodeID, int noteID);
tzpl_SErr noteSetParams(i64 nodeID, int noteID, int n, tzpl_ParamPair* params);
tzpl_SErr noteSetParamRange(i64 nodeID, int noteID, int first, int length, f32* values);

// buffers
tzpl_SErr resizeBuffer(i64 nodeID, i64 bufID, int numChannels, i64 length);
tzpl_SErr loadBuffer(i64 nodeID, i64 bufID, const char* path,
                     int channelOffset = 0, i64 frameOffset = 0, i64 numFrames = INT64_MAX);
tzpl_SErr replaceBuffer(i64 nodeID, i64 bufID, tzpl_Buffer* buffer);

// sample banks (see engine/src/tzpl_sample_bank.hpp for the zone spec).
// loadSampleBank builds the bank at record time -- files load and zones
// validate on the calling thread, so spec errors (tzpl_errBadSampleBank)
// return synchronously. Both require an active bundle, like buffers.
struct SampleBankZoneSpec;
tzpl_SErr loadSampleBank(i64 nodeID, i64 bankID,
                         std::span<SampleBankZoneSpec const> zones);
tzpl_SErr replaceSampleBank(i64 nodeID, i64 bankID, tzpl_SampleBank* bank);

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
