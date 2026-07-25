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
//  tzpl_audio_engine_ffi.cpp
//  bridge
//
//  FFI bridge: wraps engine client functions into the CFun signature
//  expected by the Tzopilotl VM, and registers them with the compiler.
//

#include "tzpl_audio_engine_ffi.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_ui_state.hpp"
#include "tzpl_nrt_render.hpp"
#include "tzpl.hpp"
#include "value.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_engine.hpp"
#include "tzpl_vm_commands.hpp"
#include "nrt_vm.hpp"
#include "module_compiler.hpp"
#include "incremental_compiler.hpp"
#include "diagnostic.hpp"
#include <thread>
#include <string>
#include <utility>
#include <cmath>
#include <fstream>
#include <sstream>

// Both tzpl and engine define i64/f64/etc. in different ways.
// engine: namespace engine { using i64 = long; }
// tzpl:        using i64 = int64_t;  (which is long long on macOS)
// We use explicit namespace qualification and casts where needed.

namespace bridge {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Retrieve the AppContext stored in the VM's userData.
static AppContext* getAppContext(ts::VM& vm) {
    return static_cast<AppContext*>(vm.userData());
}

// Retrieve the Engine* to target. If a render context is active on this
// thread (we're inside a render's setup closure or one of its scheduled
// handlers), commands route to the render's engine. Otherwise they target
// the live engine via the AppContext.
static engine::Engine* getEngine(ts::VM& vm) {
    if (auto* r = bridge::currentRenderContext()) {
        return r->engine;
    }
    auto* ctx = getAppContext(vm);
    return ctx ? ctx->engine : nullptr;
}

// Map a tzpl_SErr to its enumerator name for diagnostic output.
static char const* errName(tzpl_SErr err) {
    switch (err) {
        case tzpl_errNone:                   return "errNone";
        case tzpl_errInternal:               return "errInternal";
        case tzpl_errNodeIDAlreadyTaken:     return "errNodeIDAlreadyTaken";
        case tzpl_errNodeDefNotFound:        return "errNodeDefNotFound";
        case tzpl_errNodeNotFound:           return "errNodeNotFound";
        case tzpl_errNoteNotFound:           return "errNoteNotFound";
        case tzpl_errControlNotFound:        return "errControlNotFound";
        case tzpl_errDeviceNotFound:         return "errDeviceNotFound";
        case tzpl_errAlreadyAdded:           return "errAlreadyAdded";
        case tzpl_errAlreadyRemoved:         return "errAlreadyRemoved";
        case tzpl_errSiloOutOfRange:         return "errSiloOutOfRange";
        case tzpl_errInputOutOfRange:        return "errInputOutOfRange";
        case tzpl_errOutputOutOfRange:       return "errOutputOutOfRange";
        case tzpl_errNoAudioDevices:         return "errNoAudioDevices";
        case tzpl_errAudioNotInitialized:    return "errAudioNotInitialized";
        case tzpl_errCommandsQueuedButNotSent: return "errCommandsQueuedButNotSent";
        case tzpl_errNoActiveBundle:         return "errNoActiveBundle";
        case tzpl_errEngineInUse:            return "errEngineInUse";
        case tzpl_errCyclicConnection:       return "errCyclicConnection";
        case tzpl_errTypeMismatch:           return "errTypeMismatch";
        case tzpl_errRateMismatch:           return "errRateMismatch";
        case tzpl_errChanMismatch:           return "errChanMismatch";
        case tzpl_errNumPortsMismatch:       return "errNumPortsMismatch";
        case tzpl_errNotImplemented:         return "errNotImplemented";
        case tzpl_errTooLate:                return "errTooLate";
        case tzpl_errClockOutOfRange:        return "errClockOutOfRange";
        case tzpl_errResourceLimit:          return "errResourceLimit";
    }
    return "errUnknown";
}

// Strip the "ffi_" prefix compilers add to __func__ so the log message
// matches the name callers see in Tzopilotl.
static char const* stripFfiPrefix(char const* name) {
    if (name && name[0] == 'f' && name[1] == 'f' && name[2] == 'i' && name[3] == '_') {
        return name + 4;
    }
    return name;
}

// Convert a tzpl_SErr to an Int return value (0 = success). If the engine
// returned an error, log it to stderr so the failure is not silent.
static void returnErr(ts::VM& vm, u16 dst, tzpl_SErr err, char const* fnName) {
    if (err != tzpl_errNone) {
        std::fprintf(stderr, "audio_engine.%s: %s\n",
                     stripFfiPrefix(fnName), errName(err));
    }
    vm.reg(dst).i = static_cast<i64>(err);
}

// Extract a C string from a Tzopilotl String object in a register.
static const char* regString(ts::VM& vm, u16 reg) {
    return ts::stringData(vm.reg(reg).o);
}

// Unbind ui widgets whose engine fast path targets `nodeID` (all nodes if
// nodeID < 0). Freeing a node otherwise leaves its widgets sending
// setControl at a dead ID, spamming errNodeNotFound on every drag; unbound
// widgets are silent until code rebinds them.
static void unbindWidgetsForNode(AppContext* ctx, std::int64_t nodeID) {
    if (!ctx || !ctx->uiState) return;
    std::lock_guard<std::mutex> lock(ctx->uiState->mtx);
    for (auto& w : ctx->uiState->widgets) {
        if (w->target && (nodeID < 0 || w->target->nodeID == (long)nodeID))
            w->target.reset();
        if (w->target2 && (nodeID < 0 || w->target2->nodeID == (long)nodeID))
            w->target2.reset();
    }
}


// ---------------------------------------------------------------------------
// Engine lifecycle
// ---------------------------------------------------------------------------

// fn engineStart() -> Void
static void ffi_engineStart(ts::VM& vm, u16 dst, u16, u16) {
    engine::startAudio(getEngine(vm));
}

// fn engineStop() -> Void
static void ffi_engineStop(ts::VM& vm, u16 dst, u16, u16) {
    if (bridge::currentRenderContext()) {
        bridge::requestEndCurrentRender();
        return;
    }
    engine::stopAudio(getEngine(vm));
}

// fn isAudioRunning() -> Bool
static void ffi_isAudioRunning(ts::VM& vm, u16 dst, u16, u16) {
    vm.reg(dst).i = engine::isAudioRunning(getEngine(vm)) ? 1 : 0;
}

// fn getStreamTime() -> Float
static void ffi_getStreamTime(ts::VM& vm, u16 dst, u16, u16) {
    vm.reg(dst).f = engine::getStreamTime(getEngine(vm));
}

// fn masterGain(gain: Float) -> Void
static void ffi_masterGain(ts::VM& vm, u16 dst, u16, u16 argBase) {
    f32 gain = static_cast<f32>(vm.reg(argBase).f);
    engine::masterGain(getEngine(vm), gain);
}

// fn safetyLimiter(on: Bool) -> Void
static void ffi_safetyLimiter(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::Enable en = vm.reg(argBase).i ? engine::kOn : engine::kOff;
    engine::safetyLimiter(getEngine(vm), en);
}

// fn inputChannels() -> Int
// Returns the number of hardware input channels (0 if input is disabled).
static void ffi_inputChannels(ts::VM& vm, u16 dst, u16, u16) {
    engine::Engine* eng = getEngine(vm);
    // Access streamParams_ directly -- it's set at init time, safe to read.
    vm.reg(dst).i = eng ? eng->streamParams_.inputChannels : 0;
}

// ---------------------------------------------------------------------------
// Plugin loading
// ---------------------------------------------------------------------------

// fn loadPlugins(path: String) -> Bool
static void ffi_loadPlugins(ts::VM& vm, u16 dst, u16, u16 argBase) {
    bool ok = engine::loadDefs(getEngine(vm), regString(vm, argBase));
    vm.reg(dst).i = ok ? 1 : 0;
}

// fn loadPlugin(path: String, name: String) -> Bool
static void ffi_loadPlugin(ts::VM& vm, u16 dst, u16, u16 argBase) {
    const char* path = regString(vm, argBase);
    const char* name = regString(vm, argBase + 1);
    bool ok = engine::loadDef(getEngine(vm), path, name);
    vm.reg(dst).i = ok ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Command bundling
// ---------------------------------------------------------------------------

// fn begin() -> Int   (returns error code, 0 = success)
// The target silo is chosen at submit time: go(silo) / sched(silo, ...).
static void ffi_begin(ts::VM& vm, u16 dst, u16, u16) {
    returnErr(vm, dst, engine::begin(getEngine(vm)), __func__);
}

// fn go(silo: Int) -> Int  (send bundle to silo for immediate execution)
static void ffi_sched_immediate(ts::VM& vm, u16 dst, u16, u16 argBase) {
    int silo = static_cast<int>(vm.reg(argBase).i);
    returnErr(vm, dst, engine::go(silo), __func__);
}

// fn sched(silo: Int, clock: Int, beat: Float) -> Int
static void ffi_sched(ts::VM& vm, u16 dst, u16, u16 argBase) {
    int silo = static_cast<int>(vm.reg(argBase).i);
    int clock = static_cast<int>(vm.reg(argBase + 1).i);
    f64 beat = vm.reg(argBase + 2).f;
    returnErr(vm, dst, engine::sched(silo, clock, beat), __func__);
}

// fn schedPolicy(silo: Int, clock: Int, beat: Float, policy: Int) -> Int
static void ffi_schedPolicy(ts::VM& vm, u16 dst, u16, u16 argBase) {
    int silo = static_cast<int>(vm.reg(argBase).i);
    int clock = static_cast<int>(vm.reg(argBase + 1).i);
    f64 beat = vm.reg(argBase + 2).f;
    auto policy = static_cast<engine::SchedPolicy>(vm.reg(argBase + 3).i);
    returnErr(vm, dst, engine::sched(silo, clock, beat, policy), __func__);
}

// fn setTempo(clock: Int, bpm: Float) -> Int
static void ffi_setTempo(ts::VM& vm, u16 dst, u16, u16 argBase) {
    int clock = static_cast<int>(vm.reg(argBase).i);
    f64 bpm = vm.reg(argBase + 1).f;
    returnErr(vm, dst, engine::setTempo(getEngine(vm), clock, bpm), __func__);
}

// fn schedTempoChange(clock: Int, atBeat: Float, targetBPM: Float, rampBeats: Float) -> Int
static void ffi_schedTempoChange(ts::VM& vm, u16 dst, u16, u16 argBase) {
    int clock = static_cast<int>(vm.reg(argBase).i);
    f64 atBeat = vm.reg(argBase + 1).f;
    f64 targetBPM = vm.reg(argBase + 2).f;
    f64 rampBeats = vm.reg(argBase + 3).f;
    returnErr(vm, dst,
        engine::schedTempoChange(getEngine(vm), clock, atBeat, targetBPM, rampBeats),
        __func__);
}

// fn clockBeats(clock: Int) -> Float
static void ffi_clockBeats(ts::VM& vm, u16 dst, u16, u16 argBase) {
    int clock = static_cast<int>(vm.reg(argBase).i);
    vm.reg(dst).f = engine::clockBeats(getEngine(vm), clock);
}

// fn clockTempo(clock: Int) -> Float  (BPM)
static void ffi_clockTempo(ts::VM& vm, u16 dst, u16, u16 argBase) {
    int clock = static_cast<int>(vm.reg(argBase).i);
    vm.reg(dst).f = engine::clockTempoBPM(getEngine(vm), clock);
}

// ---------------------------------------------------------------------------
// Node operations
// ---------------------------------------------------------------------------

// fn newNode(defName: String, nodeID: Int) -> Int
static void ffi_newNode(ts::VM& vm, u16 dst, u16, u16 argBase) {
    const char* defName = regString(vm, argBase);
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase + 1).i);
    tzpl_SErr err = engine::newNode(defName, nodeID);
    // Record nodeID -> def name for the live engine so ui.control(node, name)
    // can look up the def's control specs. (Bundles are validated at submit,
    // so a failed go() can leave a stale entry; lookups re-validate anyway.)
    if (err == tzpl_errNone && !bridge::currentRenderContext()) {
        if (auto* ctx = getAppContext(vm)) {
            std::lock_guard<std::mutex> lock(ctx->nodeDefNamesMtx);
            ctx->nodeDefNames[static_cast<std::int64_t>(nodeID)] = defName;
        }
    }
    returnErr(vm, dst, err, __func__);
}

// fn freeNode(nodeID: Int) -> Int
static void ffi_freeNode(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    if (!bridge::currentRenderContext()) {
        auto* ctx = getAppContext(vm);
        if (ctx) {
            std::lock_guard<std::mutex> lock(ctx->nodeDefNamesMtx);
            ctx->nodeDefNames.erase(static_cast<std::int64_t>(nodeID));
        }
        unbindWidgetsForNode(ctx, static_cast<std::int64_t>(nodeID));
    }
    returnErr(vm, dst, engine::freeNode(nodeID), __func__);
}

// fn freeAllNodes() -> Int
static void ffi_freeAllNodes(ts::VM& vm, u16 dst, u16, u16) {
    if (!bridge::currentRenderContext()) {
        auto* ctx = getAppContext(vm);
        if (ctx) {
            std::lock_guard<std::mutex> lock(ctx->nodeDefNamesMtx);
            ctx->nodeDefNames.clear();
        }
        unbindWidgetsForNode(ctx, -1);
    }
    returnErr(vm, dst, engine::freeAllNodes(), __func__);
}

// fn channelOffset(offset: Int) -> Int
static void ffi_channelOffset(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto offset = static_cast<engine::i32>(vm.reg(argBase).i);
    returnErr(vm, dst, engine::channelOffset(offset), __func__);
}

// ---------------------------------------------------------------------------
// Connections
// ---------------------------------------------------------------------------

// fn connect(srcNode: Int, srcPort: Int, dstNode: Int, dstPort: Int) -> Int
static void ffi_connect(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::PortAddr src{static_cast<engine::i64>(vm.reg(argBase).i),
                         static_cast<int>(vm.reg(argBase + 1).i)};
    engine::PortAddr dst_port{static_cast<engine::i64>(vm.reg(argBase + 2).i),
                              static_cast<int>(vm.reg(argBase + 3).i)};
    returnErr(vm, dst, engine::connect(src, dst_port), __func__);
}

// fn connectX(srcNode: Int, srcPort: Int, dstNode: Int, dstPort: Int,
//             xfade: Float, curve: Int) -> Int
static void ffi_connectX(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::PortAddr src{static_cast<engine::i64>(vm.reg(argBase).i),
                         static_cast<int>(vm.reg(argBase + 1).i)};
    engine::PortAddr dst_port{static_cast<engine::i64>(vm.reg(argBase + 2).i),
                              static_cast<int>(vm.reg(argBase + 3).i)};
    f64 xfade = vm.reg(argBase + 4).f;
    auto curve = static_cast<engine::FadeCurve>(vm.reg(argBase + 5).i);
    returnErr(vm, dst, engine::connect(src, dst_port, xfade, curve), __func__);
}

// fn disconnectInput(dstNode: Int, dstPort: Int) -> Int
static void ffi_disconnectInput(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::PortAddr dst_port{static_cast<engine::i64>(vm.reg(argBase).i),
                              static_cast<int>(vm.reg(argBase + 1).i)};
    returnErr(vm, dst, engine::disconnectInput(dst_port), __func__);
}

// fn disconnectInputX(dstNode: Int, dstPort: Int, xfade: Float, curve: Int) -> Int
static void ffi_disconnectInputX(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::PortAddr dst_port{static_cast<engine::i64>(vm.reg(argBase).i),
                              static_cast<int>(vm.reg(argBase + 1).i)};
    f64 xfade = vm.reg(argBase + 2).f;
    auto curve = static_cast<engine::FadeCurve>(vm.reg(argBase + 3).i);
    returnErr(vm, dst, engine::disconnectInput(dst_port, xfade, curve), __func__);
}

// fn disconnectSource(srcNode: Int, srcPort: Int, dstNode: Int, dstPort: Int) -> Int
static void ffi_disconnectSource(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::PortAddr src{static_cast<engine::i64>(vm.reg(argBase).i),
                         static_cast<int>(vm.reg(argBase + 1).i)};
    engine::PortAddr dst_port{static_cast<engine::i64>(vm.reg(argBase + 2).i),
                              static_cast<int>(vm.reg(argBase + 3).i)};
    returnErr(vm, dst, engine::disconnectSource(src, dst_port), __func__);
}

// fn disconnectSourceX(srcNode: Int, srcPort: Int, dstNode: Int, dstPort: Int,
//                      xfade: Float, curve: Int) -> Int
static void ffi_disconnectSourceX(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::PortAddr src{static_cast<engine::i64>(vm.reg(argBase).i),
                         static_cast<int>(vm.reg(argBase + 1).i)};
    engine::PortAddr dst_port{static_cast<engine::i64>(vm.reg(argBase + 2).i),
                              static_cast<int>(vm.reg(argBase + 3).i)};
    f64 xfade = vm.reg(argBase + 4).f;
    auto curve = static_cast<engine::FadeCurve>(vm.reg(argBase + 5).i);
    returnErr(vm, dst, engine::disconnectSource(src, dst_port, xfade, curve), __func__);
}

// fn disconnectOutput(srcNode: Int, srcPort: Int) -> Int
static void ffi_disconnectOutput(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::PortAddr src{static_cast<engine::i64>(vm.reg(argBase).i),
                         static_cast<int>(vm.reg(argBase + 1).i)};
    returnErr(vm, dst, engine::disconnectOutput(src), __func__);
}

// fn disconnectNode(nodeID: Int) -> Int
static void ffi_disconnectNode(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    returnErr(vm, dst, engine::disconnectNode(nodeID), __func__);
}

// fn reconnectOutput(oldSrcNode: Int, oldSrcPort: Int,
//                    newSrcNode: Int, newSrcPort: Int,
//                    xfade: Float, curve: Int) -> Int
static void ffi_reconnectOutput(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::PortAddr oldSrc{static_cast<engine::i64>(vm.reg(argBase).i),
                            static_cast<int>(vm.reg(argBase + 1).i)};
    engine::PortAddr newSrc{static_cast<engine::i64>(vm.reg(argBase + 2).i),
                            static_cast<int>(vm.reg(argBase + 3).i)};
    f64 xfade = vm.reg(argBase + 4).f;
    auto curve = static_cast<engine::FadeCurve>(vm.reg(argBase + 5).i);
    returnErr(vm, dst, engine::reconnectOutput(oldSrc, newSrc, xfade, curve), __func__);
}

// fn replaceNode(oldNodeID: Int, newNodeID: Int, xfade: Float, curve: Int) -> Int
static void ffi_replaceNode(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto oldID = static_cast<engine::i64>(vm.reg(argBase).i);
    auto newID = static_cast<engine::i64>(vm.reg(argBase + 1).i);
    f64 xfade = vm.reg(argBase + 2).f;
    auto curve = static_cast<engine::FadeCurve>(vm.reg(argBase + 3).i);
    returnErr(vm, dst, engine::replaceNode(oldID, newID, xfade, curve), __func__);
}

// ---------------------------------------------------------------------------
// Parameter control
// ---------------------------------------------------------------------------

// fn setInput(nodeID: Int, portIndex: Int, value: Float) -> Int
// Convenience: sets a single float value on an input port.
// Passes as f32 to match the engine's native sample type.
static void ffi_setInput(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::PortAddr port{static_cast<engine::i64>(vm.reg(argBase).i),
                          static_cast<int>(vm.reg(argBase + 1).i)};
    engine::f32 val = static_cast<engine::f32>(vm.reg(argBase + 2).f);
    returnErr(vm, dst, engine::setInput(port, 1, &val), __func__);
}

// fn setInputX(nodeID: Int, portIndex: Int, value: Float,
//              xfade: Float, curve: Int) -> Int
static void ffi_setInputX(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::PortAddr port{static_cast<engine::i64>(vm.reg(argBase).i),
                          static_cast<int>(vm.reg(argBase + 1).i)};
    engine::f32 val = static_cast<engine::f32>(vm.reg(argBase + 2).f);
    f64 xfade = vm.reg(argBase + 3).f;
    auto curve = static_cast<engine::FadeCurve>(vm.reg(argBase + 4).i);
    returnErr(vm, dst, engine::setInput(port, 1, &val, xfade, curve), __func__);
}

// fn setControl(nodeID: Int, controlID: Int, value: Float) -> Int
// Convenience: sets a single float control value.
static void ffi_setControl(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    auto controlID = static_cast<engine::i64>(vm.reg(argBase + 1).i);
    engine::f32 val = static_cast<engine::f32>(vm.reg(argBase + 2).f);
    returnErr(vm, dst, engine::setControl(nodeID, controlID, 1, &val), __func__);
}

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------

// fn resizeBuffer(nodeID: Int, bufID: Int, numChannels: Int, length: Int) -> Int
static void ffi_resizeBuffer(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    auto bufID = static_cast<engine::i64>(vm.reg(argBase + 1).i);
    int numChannels = static_cast<int>(vm.reg(argBase + 2).i);
    auto length = static_cast<engine::i64>(vm.reg(argBase + 3).i);
    returnErr(vm, dst, engine::resizeBuffer(nodeID, bufID, numChannels, length),
              __func__);
}

// fn loadBuffer(nodeID: Int, bufID: Int, path: String) -> Int
// Loads an audio file into a node's buffer slot (bundled command; the file
// is read at bundle submit on the calling thread).
static void ffi_loadBuffer(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    auto bufID = static_cast<engine::i64>(vm.reg(argBase + 1).i);
    const char* path = regString(vm, argBase + 2);
    tzpl_SErr err = engine::loadBuffer(nodeID, bufID, path);
    // Record the path so ui.waveform can re-read the file for display.
    if (err == tzpl_errNone && !bridge::currentRenderContext()) {
        if (auto* ctx = getAppContext(vm)) {
            std::lock_guard<std::mutex> lock(ctx->bufferPathsMtx);
            ctx->bufferPaths[{static_cast<std::int64_t>(nodeID),
                              static_cast<std::int64_t>(bufID)}] = path;
        }
    }
    returnErr(vm, dst, err, __func__);
}

// ---------------------------------------------------------------------------
// Note / voice management
// ---------------------------------------------------------------------------

// fn noteOn(nodeID: Int, noteID: Int, params: Array[Float]) -> Int
static void ffi_noteOn(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    int noteID = static_cast<int>(vm.reg(argBase + 1).i);

    // Extract float array from Tzopilotl Array[Float] object
    auto* arr = vm.reg(argBase + 2).o;
    int length = static_cast<int>(ts::arraySize(arr));

    // Convert f64 array to f32 (engine noteOn expects f32*)
    // Use a small stack buffer to avoid allocation
    constexpr int kMaxStackParams = 64;
    engine::f32 stackBuf[kMaxStackParams];
    engine::f32* params = stackBuf;
    std::unique_ptr<engine::f32[]> heapBuf;
    if (length > kMaxStackParams) {
        heapBuf = std::make_unique<engine::f32[]>(length);
        params = heapBuf.get();
    }
    for (int i = 0; i < length; ++i) {
        params[i] = static_cast<engine::f32>(ts::arrayGetFloat(arr, i));
    }

    returnErr(vm, dst, engine::noteOn(nodeID, noteID, length, params), __func__);
}

// fn noteOff(nodeID: Int, noteID: Int) -> Int
static void ffi_noteOff(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    int noteID = static_cast<int>(vm.reg(argBase + 1).i);
    returnErr(vm, dst, engine::noteOff(nodeID, noteID), __func__);
}

// fn allNotesOff(nodeID: Int) -> Int
static void ffi_allNotesOff(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    returnErr(vm, dst, engine::allNotesOff(nodeID), __func__);
}

// fn noteSetParams(nodeID: Int, noteID: Int, firstParam: Int,
//                  values: Array[Float]) -> Int
static void ffi_noteSetParams(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    int noteID = static_cast<int>(vm.reg(argBase + 1).i);
    int first  = static_cast<int>(vm.reg(argBase + 2).i);

    auto* arr = vm.reg(argBase + 3).o;
    int length = static_cast<int>(ts::arraySize(arr));

    constexpr int kMaxStackParams = 64;
    engine::f32 stackBuf[kMaxStackParams];
    engine::f32* params = stackBuf;
    std::unique_ptr<engine::f32[]> heapBuf;
    if (length > kMaxStackParams) {
        heapBuf = std::make_unique<engine::f32[]>(length);
        params = heapBuf.get();
    }
    for (int i = 0; i < length; ++i) {
        params[i] = static_cast<engine::f32>(ts::arrayGetFloat(arr, i));
    }

    returnErr(vm, dst, engine::noteSetParamRange(nodeID, noteID, first, length, params), __func__);
}


// ---------------------------------------------------------------------------
// Introspection
// ---------------------------------------------------------------------------

// fn listSynthDefs() Array[String]
// Returns an array of all registered node def names.
static void ffi_listSynthDefs(ts::VM& vm, u16 dst, u16, u16) {
    engine::Engine* eng = getEngine(vm);

    std::vector<std::string> names;
    if (eng) {
        engine::listNodeDefs(eng, names);
    }

    // Create a Tzopilotl Array[String]
    auto* arrType = vm.arrayType(vm.stringType());
    auto* arr = new ts::ObjArray(arrType);
    for (auto const& name : names) {
        auto* s = new ts::StringObj(name);
        arr->push(s);
    }
    vm.reg(dst).o = arr;
}

// ---------------------------------------------------------------------------
// Silo VM management
// ---------------------------------------------------------------------------

// Command that installs compiled code on the RT VM, owning the CompileResult.
// doNRT deletes the CompileResult after the RT thread has processed it.
// ---------------------------------------------------------------------------
// Silo task scheduler: self-rescheduling coroutine tasks on a silo's RT thread.
// ---------------------------------------------------------------------------
//
// `spawn(clock, coro)` (silo-side) adds a `fn() Float` trampoline (resumes the
// coroutine, returns the next beat-delta) to the owning silo's pool. The silo's
// per-sample taskTickFn_ fires every task whose beat (on its tempo clock) has
// arrived: it calls the handler, and reschedules at beat + returned-delta, or
// drops it when the delta is <= 0 (coroutine done / explicit stop). No RT
// allocation: entries come from a fixed pool. Handlers are GC-rooted via a root
// scanner registered on the silo VM (markRoots).

// The silo whose VM code is currently executing (start() / a task handler), so
// silo-side FFIs (spawn, note events) target the right silo. Thread-local
// because silos 1..N run on separate worker threads.
static thread_local engine::Silo* gCurrentSilo = nullptr;

// A cross-silo actor message sitting in a silo's outbox. A fixed POD (no heap)
// so it can be pushed by value on the RT thread into the lock-free outbox ring;
// the main NRT thread pops it and forwards it to the destination silo (or an NRT
// actor). A message whose encoded payload exceeds kMaxBytes is REJECTED by
// siloOutbox (it returns an error, not enqueued) -- never truncated.
struct OutboxMsg {
    // Max encoded-Msg (TZB -- see shared/tzpl_sexpr_bin.hpp for the layout and
    // lang/docs/FFI_Guide.html section 15 for the format) payload per message.
    // At ~9 bytes per immediate
    // value (1 tag + 8 data) this holds a few hundred elements, e.g. a control
    // vector or a chord, with headroom. Sized to the outbox ring: kOutboxCap
    // entries are preallocated, so this is the per-slot cost (see kOutboxCap).
    static constexpr int kMaxBytes = 4096;
    int           targetSilo = -1;   // destination silo index (-1 = NRT actor)
    ts::SymbolPtr name = nullptr;    // destination actor name
    uint32_t      len = 0;
    uint8_t       buf[kMaxBytes];    // encoded Msg bytes (rejected if over)
};

struct SiloTaskScheduler {
    static constexpr int kMaxTasks = 256;
    static constexpr int kOutboxCap = 256;
    struct Entry {
        int    clock = 0;
        f64    beatTime = 0.;
        ts::Obj* handler = nullptr;   // fn() Float trampoline (null = free slot)
        i64    id = 0;
        Entry* next = nullptr;
    };
    Entry  pool_[kMaxTasks];
    Entry* free_ = nullptr;
    Entry* active_ = nullptr;
    ts::Obj* inFlight_ = nullptr;     // handler mid-call (GC root)
    i64    nextId_ = 1;
    ts::VM* vm_ = nullptr;
    // Outbound actor messages: pushed by this silo's RT thread, drained by the
    // main NRT thread (single-producer / single-consumer).
    engine::AtomicFifo<OutboxMsg> outbox_{kOutboxCap};

    explicit SiloTaskScheduler(ts::VM* vm) : vm_(vm) {
        for (int i = 0; i < kMaxTasks - 1; ++i) pool_[i].next = &pool_[i + 1];
        pool_[kMaxTasks - 1].next = nullptr;
        free_ = &pool_[0];
    }

    i64 addTask(int clock, f64 beatTime, ts::Obj* handler) {
        if (!free_) return -1;
        Entry* e = free_; free_ = free_->next;
        e->clock = clock; e->beatTime = beatTime; e->handler = handler;
        e->id = nextId_++; e->next = active_; active_ = e;
        return e->id;
    }

    bool cancel(i64 id) {
        for (Entry** pp = &active_; *pp; pp = &(*pp)->next) {
            if ((*pp)->id == id) {
                Entry* e = *pp; *pp = e->next;
                e->handler = nullptr; e->next = free_; free_ = e;
                return true;
            }
        }
        return false;
    }

    // Called per sample from the silo loop (gCurrentSilo already set by the
    // tick wrapper). Fires due tasks and reschedules them.
    void tick(i64 sampleTime, engine::Silo* s) {
        Entry** pp = &active_;
        while (*pp) {
            Entry* e = *pp;
            bool badClock = e->clock < 0 || e->clock >= (int)s->tempoClocks_.size();
            f64 beat = badClock ? 0.0 : s->tempoClocks_[e->clock].beatAtSample(sampleTime);
            if (!badClock && beat >= e->beatTime) {
                inFlight_ = e->handler;
                vm_->makeCurrent();
                ts::Word r = vm_->callCallable(e->handler, nullptr, 0);
                inFlight_ = nullptr;
                f64 delta = r.f;
                if (delta > 0.0 && std::isfinite(delta)) {
                    e->beatTime = beat + delta;     // reschedule
                    pp = &e->next;
                } else {
                    *pp = e->next; e->handler = nullptr; e->next = free_; free_ = e;
                }
            } else {
                pp = &e->next;
            }
        }

        // Drive any actors living in this silo's VM against tempo clock 0, so a
        // silo actor's `await delay(n)` resolves sample-accurately on the audio
        // beat. Bounded per sample so a burst can't overrun the block.
        if (vm_ && !s->tempoClocks_.empty()) {
            f64 beat = s->tempoClocks_[0].beatAtSample(sampleTime);
            vm_->makeCurrent();
            vm_->tickActors(beat, /*budget=*/64);
        }
    }

    void markRoots(ts::TracingGC& gc) {
        for (Entry* e = active_; e; e = e->next) {
            if (e->handler) gc.mark(static_cast<ts::GCObj*>(e->handler));
        }
        if (inFlight_) gc.mark(static_cast<ts::GCObj*>(inFlight_));
    }
};

// Silo loop calls this per sample (matches Silo::TaskTickFn).
static void siloTaskTick(void* sched, i64 sampleTime, engine::Silo* s) {
    if (!sched) return;
    engine::Silo* prev = gCurrentSilo;
    gCurrentSilo = s;
    static_cast<SiloTaskScheduler*>(sched)->tick(sampleTime, s);
    gCurrentSilo = prev;
}

// Installs newly compiled silo code. The CODE half is swapped wholesale: the
// caller builds the complete new image off the audio thread (from the compiler
// target's authoritative code layout) and ships the pointer, so doRT only does an
// O(1) pointer swap -- the heavy work (builtins + modules + functions) never
// touches the RT thread. The DATA half (var/let, much smaller) is appended in
// place via the RT-safe TLSF segment. doNRT retires the old image + the result.
struct SiloCodeSwapCmd : engine::Command {
    ts::CodeImage*     newImage_;   // built on NRT, swapped in on RT
    ts::CompileResult* dataResult_; // newDataGlobals + dynvars for installData
    ts::CodeImage*     oldImage_ = nullptr;
    SiloCodeSwapCmd(ts::CodeImage* img, ts::CompileResult* dr)
        : newImage_(img), dataResult_(dr) {}
    void doRT(engine::Silo* s) override {
        auto* vm = static_cast<ts::VM*>(s->vm_);
        if (vm) {
            vm->makeCurrent();
            oldImage_ = vm->swapCodeImage(newImage_);   // O(1)
            vm->installData(*dataResult_);              // append data in place
        } else {
            oldImage_ = newImage_;  // no VM: hand the image straight to retirement
        }
    }
    bool doNRT(engine::Silo* s) override {
        delete oldImage_;     // retire the replaced image (frees only its slots)
        delete dataResult_;
        return true;
    }
};

// Build a fresh immutable code image from the compiler target's authoritative
// code layout. Read on the NRT thread from the target (never from the live silo
// VM's image, which the RT thread may be swapping) -- the target is NRT-owned.
// Captures every code slot, including this compile's redefinitions (codegen wrote
// the new CodeBlock* into the target's existing slot). The work is O(image size)
// but runs off the audio thread; the RT thread only swaps the resulting pointer.
static ts::CodeImage* buildSiloCodeImage(const ts::VMTarget& target) {
    auto* img = new ts::CodeImage();
    img->slots.reserve(target->codeGlobals.size());
    for (auto const& slot : target->codeGlobals) img->slots.push_back(slot.value);
    return img;
}

// Resolves a siloLoad completion Future once the preceding install (+ optional
// main-block run) command has landed on the silo. Chained AFTER those commands,
// so the silo's ordered FIFO guarantees everything queued before it -- the part's
// node graph, attachVM, and the code install -- has been processed.
//
// doNRT runs on the engine's NRT cleanup thread when audio is running, or inline
// on the caller (script) thread when audio is stopped. The inline case already
// holds the main NRTVM mutex (the script runs under it), so re-locking would
// deadlock -- guard by comparing to the registering thread id (the renderDone
// pattern). Either way the main VM is made current so the result String and the
// future resolution land in the main VM's heap, not the silo's.
struct SiloLoadCompleteCmd : engine::Command {
    ts::NRTVM*       nrtvm_;
    ts::Future*      future_;
    std::string      result_;       // "" on success, error message otherwise
    std::thread::id  regThread_;
    SiloLoadCompleteCmd(ts::NRTVM* nrtvm, ts::Future* fut, std::string result,
                        std::thread::id regThread)
        : nrtvm_(nrtvm), future_(fut), result_(std::move(result)), regThread_(regThread) {}
    void doRT(engine::Silo*) override {}   // nothing on the RT thread
    bool doNRT(engine::Silo*) override {
        if (std::this_thread::get_id() == regThread_) {
            // Inline (audio stopped): script thread, main VM current + mutex held.
            nrtvm_->vm.makeCurrent();
            ts::Word w; w.o = new ts::StringObj(result_);
            nrtvm_->vm.resolveExternalFuture(future_, &w, 1);
        } else {
            std::lock_guard<std::mutex> lk(nrtvm_->mtx);
            nrtvm_->vm.makeCurrent();
            ts::Word w; w.o = new ts::StringObj(result_);
            nrtvm_->vm.resolveExternalFuture(future_, &w, 1);
            nrtvm_->cv.notify_all();
        }
        return true;
    }
};

// Calls the silo module's start() entry -- the global at `startIdx_`, recorded
// by siloLoad from the module's exported functions. Scheduled on the silo's
// TempoClock so all silos' starts fire on one common beat. The global holds an
// immortal CodeBlock (functions aren't GC objects), so no rooting is needed.
struct SiloRunStartCmd : engine::Command {
    int startIdx_;
    explicit SiloRunStartCmd(int startIdx) : startIdx_(startIdx) {}
    void doRT(engine::Silo* s) override {
        auto* vm = static_cast<ts::VM*>(s->vm_);
        if (vm && startIdx_ >= 0) {
            engine::Silo* prev = gCurrentSilo;
            gCurrentSilo = s;                 // so start() -> spawn targets this silo
            vm->makeCurrent();
            auto* cb = static_cast<ts::CodeBlock*>(vm->global((u32)startIdx_).p);
            if (cb) vm->callFunction(cb, nullptr, 0);
            vm->gcHeartbeat();
            gCurrentSilo = prev;
        }
    }
    bool doNRT(engine::Silo*) override { return true; }
};

// Installs the per-silo task scheduler + per-sample tick onto the silo (RT
// thread). The scheduler itself is owned by SiloVMState and freed on detach.
struct SetSiloTaskSchedCmd : engine::Command {
    SiloTaskScheduler* sched_;
    explicit SetSiloTaskSchedCmd(SiloTaskScheduler* sched) : sched_(sched) {}
    void doRT(engine::Silo* s) override {
        s->taskSched_ = sched_;
        s->taskTickFn_ = &siloTaskTick;
    }
    bool doNRT(engine::Silo*) override { return true; }
};

// Command that detaches the VM from the silo and deletes it on the NRT thread.
struct DetachAndDeleteVMCmd : engine::Command {
    ts::VM* vm_;
    explicit DetachAndDeleteVMCmd(ts::VM* vm) : vm_(vm) {}
    void doRT(engine::Silo* s) override {
        s->vm_ = nullptr;
        s->heartbeatFn_ = nullptr;
    }
    bool doNRT(engine::Silo* s) override {
        delete vm_;
        return true;
    }
};

// Send a linked list of commands directly to a silo, bypassing the bundle API.
// When audio is not running, commands execute synchronously on the caller's thread.
static void sendCmdListToSilo(engine::Engine* eng, int siloIndex,
                              engine::Command* head) {
    engine::Silo& silo = eng->silos_[siloIndex];
    if (engine::isAudioRunning(eng)) {
        silo.from_nrt_.push(head);
    } else {
        engine::Command* cmd = head;
        while (cmd) {
            engine::Command* next = cmd->next_;
            while (!cmd->run(&silo)) {}
            delete cmd;
            cmd = next;
        }
    }
}

// Send a single command directly to a silo.
static void sendCmdToSilo(engine::Engine* eng, int siloIndex,
                          engine::Command* cmd) {
    cmd->next_ = nullptr;
    sendCmdListToSilo(eng, siloIndex, cmd);
}

// fn attachVM(siloIndex: Int) -> Int
static void ffi_attachVM(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    auto* eng = ctx->engine;
    int siloIndex = static_cast<int>(vm.reg(argBase).i);

    if (siloIndex < 0 || siloIndex >= (int)ctx->siloVMs.size()) {
        returnErr(vm, dst, tzpl_errSiloOutOfRange, __func__);
        return;
    }
    auto& state = ctx->siloVMs[siloIndex];
    if (state.vm) {
        returnErr(vm, dst, tzpl_errAlreadyAdded, __func__);
        return;
    }

    // Create rt_restricted target and VM (16 MB TLSF pool)
    state.target = ctx->compiler->createTarget(/*rtRestricted=*/true);
    ts::TypeUniverse& types = ctx->nrtvm->vm.typeUniverse();
    state.vm = new ts::VM(16 * 1024 * 1024, types, state.target);
    state.vm->setUserData(ctx);

    // Create a separate module compiler so modules compiled for this
    // target get their own cache (distinct from the NRT target's cache).
    // includePaths()/systemPaths() return snapshots; a project registered
    // after this attach is propagated by the GUI (registerProjectFor adds
    // to every attached silo's compiler).
    state.moduleCompiler = std::make_unique<ts::ModuleCompiler>(
        *ctx->compiler,
        ctx->moduleCompiler->includePaths(),
        ctx->moduleCompiler->systemPaths());

    // Persistent incremental compile context: one TypeChecker shared across all
    // siloLoad/siloEval calls on this silo, so redefining a function reuses its
    // global index (live code picks up the new body) instead of allocating a new
    // slot the old code never sees. Builtins/imports are registered once (on the
    // first compile) and installed into the silo VM like any other new global.
    state.incCompiler = std::make_unique<ts::IncrementalCompiler>(
        *ctx->compiler, state.target, *state.moduleCompiler);

    // Per-silo task scheduler (spawn'd coroutine tasks). GC-rooted on the silo VM.
    auto* sched = new SiloTaskScheduler(state.vm);
    state.taskSched = sched;
    state.vm->addExtraRootScanner([sched](ts::TracingGC& gc) { sched->markRoots(gc); });

    // Attach VM to silo (sets vm_ and heartbeatFn_ on the RT thread)
    sendCmdToSilo(eng, siloIndex, new AttachVMCmd(state.vm));
    // Wire the task scheduler + per-sample tick onto the silo (RT thread).
    sendCmdToSilo(eng, siloIndex, new SetSiloTaskSchedCmd(sched));

    returnErr(vm, dst, tzpl_errNone, __func__);
}

// fn detachVM(siloIndex: Int) -> Int
static void ffi_detachVM(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    auto* eng = ctx->engine;
    int siloIndex = static_cast<int>(vm.reg(argBase).i);

    if (siloIndex < 0 || siloIndex >= (int)ctx->siloVMs.size()) {
        returnErr(vm, dst, tzpl_errSiloOutOfRange, __func__);
        return;
    }
    auto& state = ctx->siloVMs[siloIndex];
    if (!state.vm) {
        returnErr(vm, dst, tzpl_errNodeNotFound, __func__);
        return;
    }

    // Transfer VM ownership to the command. doRT nulls silo->vm_,
    // doNRT deletes the VM on the engine's NRT command thread.
    sendCmdToSilo(eng, siloIndex, new DetachAndDeleteVMCmd(state.vm));

    state.vm = nullptr;
    // Tear down the incremental compile context before its moduleCompiler/target.
    state.incCompiler.reset();
    state.target.reset();
    state.moduleCompiler.reset();

    returnErr(vm, dst, tzpl_errNone, __func__);
}

// fn siloEval(siloIndex: Int, code: String) -> Int
static void ffi_siloEval(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    auto* eng = ctx->engine;
    int siloIndex = static_cast<int>(vm.reg(argBase).i);
    const char* code = regString(vm, argBase + 1);

    if (siloIndex < 0 || siloIndex >= (int)ctx->siloVMs.size()) {
        returnErr(vm, dst, tzpl_errSiloOutOfRange, __func__);
        return;
    }
    auto& state = ctx->siloVMs[siloIndex];
    if (!state.vm) {
        returnErr(vm, dst, tzpl_errNodeNotFound, __func__);
        return;
    }

    // Compile against the silo's persistent incremental context (on the calling
    // NRT thread). Going through incCompiler -- not a one-shot compiler->compile --
    // means a function redefined by a later siloEval reuses its existing global
    // slot, so already-installed silo code picks up the new body.
    std::string source(code);
    ts::CompileResult compiled = state.incCompiler->compile(source, "<silo>");

    // The incremental compile left this thread's gCurrentVM null (its internal
    // endCurrent clears the compile context without restoring a VM). Unlike
    // siloLoad, siloEval is not awaited, so nothing re-establishes it; restore the
    // calling VM now, else the next op on this thread (e.g. a print, whose
    // wordToString dereferences gCurrentVM) segfaults on null.
    vm.makeCurrent();

    if (!compiled.success) {
        ts::printDiagnostics(compiled.errors, source, "<silo>", std::cerr, true);
        returnErr(vm, dst, tzpl_errInternal, __func__);
        return;
    }

    ts::CodeBlock* mainBlock = compiled.mainBlock;

    // Build the new code image off the audio thread; the RT swap is O(1).
    ts::CodeImage* newImage = buildSiloCodeImage(state.target);
    auto* result = new ts::CompileResult(std::move(compiled));  // for installData
    auto* installCmd = new SiloCodeSwapCmd(newImage, result);

    if (mainBlock) {
        auto* execCmd = new VMEventCmd(VMEventCmd::Custom, mainBlock);
        installCmd->next_ = execCmd;
        execCmd->next_ = nullptr;
    } else {
        installCmd->next_ = nullptr;
    }

    sendCmdListToSilo(eng, siloIndex, installCmd);

    returnErr(vm, dst, tzpl_errNone, __func__);
}

// fn siloLoad(siloIndex: Int, code: String) -> Future<String>
//
// Compiles `code` (the silo task module's definitions) on this NRT thread, queues
// the install onto the silo, and returns a Future that resolves to "" once the
// install has landed on the silo (or to an error message). Because the silo's
// command FIFO is ordered, awaiting this one handle also gates everything queued
// before it for that silo (its node graph + attachVM) -- so `prepare` returns
// just this handle. Unlike siloEval, the completion is awaitable (the async/await
// load barrier); like siloEval it runs the module's main block to install defs.
static void ffi_siloLoad(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    auto* eng = ctx->engine;
    int siloIndex = static_cast<int>(vm.reg(argBase).i);
    const char* code = regString(vm, argBase + 1);

    // Result Future<String>, GC-rooted while in flight.
    auto& tu = ctx->nrtvm->vm.typeUniverse();
    ts::Type* strT = tu.types().stringType;
    ts::FutureType* futT = tu.futureType(strT);
    auto* future = ts::Future::create(futT, strT, 1);
    vm.registerExternalFuture(future);
    vm.reg(dst).o = future;

    // Resolve inline (we're on the script thread, main VM current + mutex held).
    auto resolveNow = [&](std::string const& err) {
        ts::Word w; w.o = new ts::StringObj(err);
        vm.resolveExternalFuture(future, &w, 1);
    };

    if (siloIndex < 0 || siloIndex >= (int)ctx->siloVMs.size()) {
        resolveNow("siloLoad: silo index out of range");
        return;
    }
    auto& state = ctx->siloVMs[siloIndex];
    if (!state.vm) { resolveNow("siloLoad: no VM attached to silo"); return; }

    // Compile the module on this NRT thread with the silo's rt-restricted target.
    // Inject the cross-VM actor delivery trampoline as a preamble so any silo
    // module can receive messages sent by name from the NRT side.
    std::string source =
        "import std.message.*;\n"
        "fn tzpl_actor_deliver(name Symbol, b Bytes) Void { sendByName(name, decode(b)); }\n"
        + std::string(code);
    // Compile against the silo's persistent incremental context so functions
    // redefined across successive loads reuse their global slots (see attachVM /
    // siloEval). The injected preamble is idempotent: after the first load it
    // simply redefines tzpl_actor_deliver to the same body (a no-op reused slot).
    ts::CompileResult compiled = state.incCompiler->compile(source, "<siloLoad>");
    // Restore the calling VM as current: the incremental compile left this
    // thread's gCurrentVM null. The success path is normally masked (the await
    // suspends and resume re-establishes it), but a compile error resolves the
    // future immediately, so the await does not suspend and the next op on this
    // thread would hit null.
    vm.makeCurrent();
    if (!compiled.success) {
        ts::printDiagnostics(compiled.errors, source, "<siloLoad>", std::cerr, true);
        resolveNow("siloLoad: compile error");
        return;
    }

    // Record the module's start() entry (Void, no params) so siloStartAt can
    // call it later -- by global index, no re-resolution needed.
    state.startGlobalIndex = -1;
    state.deliverGlobalIndex = -1;
    for (auto& ef : compiled.exportedFunctions) {
        if (ef.name == "start" && ef.paramTypes.empty()) {
            state.startGlobalIndex = (int)ef.globalIndex;
        } else if (ef.name == "tzpl_actor_deliver") {
            state.deliverGlobalIndex = (int)ef.globalIndex;
        }
    }

    ts::CodeBlock* mainBlock = compiled.mainBlock;
    // Build the new code image off the audio thread; the RT swap is O(1).
    ts::CodeImage* newImage = buildSiloCodeImage(state.target);
    auto* result = new ts::CompileResult(std::move(compiled));  // for installData
    auto* installCmd = new SiloCodeSwapCmd(newImage, result);
    engine::Command* tail = installCmd;
    if (mainBlock) {
        auto* execCmd = new VMEventCmd(VMEventCmd::Custom, mainBlock);
        tail->next_ = execCmd;
        tail = execCmd;
    }
    auto* completeCmd = new SiloLoadCompleteCmd(ctx->nrtvm, future, "",
                                                std::this_thread::get_id());
    tail->next_ = completeCmd;
    completeCmd->next_ = nullptr;
    sendCmdListToSilo(eng, siloIndex, installCmd);

    // When audio is stopped, sendCmdListToSilo ran the install/main-block on the
    // SILO VM inline (makeCurrent(silo)), leaving the thread-local current VM
    // pointing at the silo. Restore the main VM before returning to its bytecode.
    vm.makeCurrent();
}

// fn siloStartAt(beat: Float, silos: [Int]) -> Void
//
// Broadcast a beat-scheduled call to each listed silo's start() at one common
// beat -- the synchronized downbeat after the load barrier. For each silo we
// compile a tiny "start();" block against its target (which resolves the start()
// global installed by siloLoad) and schedule it on TempoClock 0 at `beat`. When
// audio is running the calls fire sample-accurately on the RT thread at `beat`;
// when stopped they run immediately (no clock is advancing).
static void ffi_siloStartAt(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    auto* eng = ctx->engine;
    double beat = vm.reg(argBase).f;
    ts::Obj* silosArr = vm.reg(argBase + 1).o;
    size_t n = ts::arraySize(silosArr);

    for (size_t k = 0; k < n; ++k) {
        int siloIndex = static_cast<int>(ts::arrayGetInt(silosArr, k));
        if (siloIndex < 0 || siloIndex >= (int)ctx->siloVMs.size()) continue;
        auto& state = ctx->siloVMs[siloIndex];
        if (!state.vm || state.startGlobalIndex < 0) {
            std::fprintf(stderr, "siloStartAt: silo %d has no start() entry\n", siloIndex);
            continue;
        }
        // NB: if a user bundle is already open on this thread, begin fails
        // with errCommandsQueuedButNotSent and the start command would join
        // the user's bundle -- pre-existing hazard, unchanged by the
        // silo-at-submit refactor.
        engine::begin(eng);
        engine::sendCommand(new SiloRunStartCmd(state.startGlobalIndex));
        engine::sched(siloIndex, 0, beat, engine::schedBetterLateThanNever);
    }
    // When audio is stopped the scheduled start() ran inline on the silo VM
    // (makeCurrent(silo)); restore the main VM before returning to its bytecode.
    vm.makeCurrent();
}

// fn _scheduleTask(clock Int, handler Fn() Float) Int  -- silo-side primitive
//
// Adds a beat task to the CURRENT silo's scheduler (gCurrentSilo), scheduled at
// the current beat of `clock` so it fires on the next tick. `handler` is a
// fn() Float that returns the next beat-delta (<= 0 to stop). The `spawn`
// wrapper builds this handler around a coroutine. Runs on the silo VM (RT thread
// for live audio, or inline during start()). Returns a task id, or -1.
static void ffi_scheduleTask(ts::VM& vm, u16 dst, u16, u16 argBase) {
    int clock = static_cast<int>(vm.reg(argBase).i);
    ts::Obj* handler = vm.reg(argBase + 1).o;
    engine::Silo* s = gCurrentSilo;
    if (!s || !s->taskSched_) { vm.reg(dst).i = -1; return; }
    int nclk = (int)s->tempoClocks_.size();
    int c = (clock >= 0 && clock < nclk) ? clock : 0;
    f64 beat = s->tempoClocks_[c].beatAtSample(s->sampleTime_);
    auto* sched = static_cast<SiloTaskScheduler*>(s->taskSched_);
    vm.reg(dst).i = static_cast<i64>(sched->addTask(clock, beat, handler));
}

// fn playNote(node Int, noteID Int, params [Float]) Int  -- silo-side, immediate
//
// Triggers a note on `node` of the CURRENT silo right now (the silo's sample
// time), straight on the RT thread -- no bundle, no allocation. Meant to be
// called from a spawned task. (The orchestration-layer noteOn queues a bundle
// command instead; this is the in-task version.)
static void ffi_playNote(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::Silo* s = gCurrentSilo;
    if (!s) { vm.reg(dst).i = (i64)tzpl_errNodeNotFound; return; }
    i64 nodeID = vm.reg(argBase).i;
    int noteID = static_cast<int>(vm.reg(argBase + 1).i);
    ts::Obj* arr = vm.reg(argBase + 2).o;
    int len = static_cast<int>(ts::arraySize(arr));
    if (len > 64) len = 64;
    engine::f32 params[64];
    for (int i = 0; i < len; ++i) params[i] = (engine::f32)ts::arrayGetFloat(arr, i);
    engine::Node* node = s->rt_getNode(nodeID);
    if (!node) { vm.reg(dst).i = (i64)tzpl_errNodeNotFound; return; }
    node->funs.noteOn(node->synth, s->sampleTime_, noteID, len, params);
    vm.reg(dst).i = (i64)tzpl_errNone;
}

// fn releaseNote(node Int, noteID Int) Int  -- silo-side, immediate noteOff
static void ffi_releaseNote(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::Silo* s = gCurrentSilo;
    if (!s) { vm.reg(dst).i = (i64)tzpl_errNodeNotFound; return; }
    i64 nodeID = vm.reg(argBase).i;
    int noteID = static_cast<int>(vm.reg(argBase + 1).i);
    engine::Node* node = s->rt_getNode(nodeID);
    if (!node) { vm.reg(dst).i = (i64)tzpl_errNodeNotFound; return; }
    node->funs.noteOff(node->synth, s->sampleTime_, noteID);
    vm.reg(dst).i = (i64)tzpl_errNone;
}

// fn readFile(path String) String  -- read a text file into a String.
//
// Returns the file's contents, or "" if it can't be opened. A host/NRT utility
// (file I/O is not RT-safe); handy for loading silo task code from a .x file:
//   await siloLoad(0, readFile("tasks/bass_task.x"))
static void ffi_readFile(ts::VM& vm, u16 dst, u16, u16 argBase) {
    const char* path = regString(vm, argBase);
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "readFile: cannot open '%s'\n", path ? path : "(null)");
        vm.reg(dst).o = new ts::StringObj(std::string());
        return;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    vm.reg(dst).o = new ts::StringObj(ss.str());
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

// Build + dispatch a cross-VM actor message to a silo. `clock<0` => immediate;
// otherwise the DeliverActorMsgCmd is beat-scheduled on the silo's TempoClock
// slot `clock` and its decode+enqueue fires when that clock reaches `beat`.
static tzpl_SErr siloDeliverImpl(AppContext* ctx, int silo, ts::SymbolPtr name,
                                 ts::BytesObj* bytes, int clock, f64 beat,
                                 engine::SchedPolicy policy) {
    auto* eng = ctx ? ctx->engine : nullptr;
    if (!ctx || !eng || silo < 0 || silo >= (int)ctx->siloVMs.size())
        return tzpl_errSiloOutOfRange;
    auto& state = ctx->siloVMs[silo];
    if (!state.vm || state.deliverGlobalIndex < 0 || !bytes)
        return tzpl_errInternal;
    auto* deliverFn = static_cast<ts::CodeBlock*>(
        state.vm->global((u32)state.deliverGlobalIndex).p);
    auto* cmd = new DeliverActorMsgCmd(deliverFn, name,
                                       bytes->data.data(), bytes->data.size());
    cmd->clock_ = clock;
    cmd->beatTime_ = beat;
    cmd->schedPolicy_ = policy;
    sendCmdListToSilo(eng, silo, cmd);
    return tzpl_errNone;
}

// fn siloDeliverBytes(silo Int, name Symbol, b Bytes) Int  -- deliver now.
static void ffi_siloDeliverBytes(ts::VM& vm, u16 dst, u16, u16 argBase) {
    tzpl_SErr err = siloDeliverImpl(
        getAppContext(vm), static_cast<int>(vm.reg(argBase).i),
        vm.reg(argBase + 1).s, static_cast<ts::BytesObj*>(vm.reg(argBase + 2).o),
        /*clock=*/-1, /*beat=*/0.0, engine::schedImmediate);
    vm.makeCurrent();   // restore main VM if it ran inline (audio stopped)
    returnErr(vm, dst, err, __func__);
}

// fn siloDeliverBytesAt(silo Int, clock Int, beat Float, name Symbol, b Bytes) Int
// Beat-scheduled delivery: the message lands in the silo actor's mailbox when
// TempoClock `clock` reaches `beat` (sample-accurate), driven by the same
// late-bound queue engine commands use. schedBetterLateThanNever so a slightly
// late conductor message still plays rather than being dropped.
static void ffi_siloDeliverBytesAt(ts::VM& vm, u16 dst, u16, u16 argBase) {
    tzpl_SErr err = siloDeliverImpl(
        getAppContext(vm), static_cast<int>(vm.reg(argBase).i),
        vm.reg(argBase + 3).s, static_cast<ts::BytesObj*>(vm.reg(argBase + 4).o),
        static_cast<int>(vm.reg(argBase + 1).i), vm.reg(argBase + 2).f,
        engine::schedBetterLateThanNever);
    vm.makeCurrent();
    returnErr(vm, dst, err, __func__);
}

// fn _siloOutbox(targetSilo Int, name Symbol, b Bytes) Int  -- SILO RT side.
// Push an encoded actor message into this silo's outbox for the main NRT thread
// to forward. RT-safe: a by-value push into a pre-allocated lock-free ring.
// Returns errInternal (without enqueuing) for a payload over OutboxMsg::kMaxBytes;
// a full outbox ring also drops. Returns errNone on success.
static void ffi_siloOutbox(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::Silo* s = gCurrentSilo;
    int target = static_cast<int>(vm.reg(argBase).i);
    ts::SymbolPtr name = vm.reg(argBase + 1).s;
    auto* bytes = static_cast<ts::BytesObj*>(vm.reg(argBase + 2).o);
    if (!s || !s->taskSched_ || !bytes) { vm.reg(dst).i = (i64)tzpl_errInternal; return; }
    auto* sched = static_cast<SiloTaskScheduler*>(s->taskSched_);
    OutboxMsg m;
    if (bytes->data.size() > sizeof(m.buf)) { vm.reg(dst).i = (i64)tzpl_errInternal; return; }
    m.targetSilo = target;
    m.name = name;
    m.len = static_cast<uint32_t>(bytes->data.size());
    std::memcpy(m.buf, bytes->data.data(), m.len);
    sched->outbox_.push(m);
    vm.reg(dst).i = (i64)tzpl_errNone;
}

// fn _pumpSiloOutboxes() Int  -- main NRT thread: drain every silo's outbox.
// A message bound for another silo (targetSilo >= 0) is forwarded to that silo's
// actor via the same beat-unaware path NRT->silo uses (the target silo decodes
// via its trampoline). A message bound for an NRT actor (targetSilo < 0) is
// stashed in ctx->nrtActorInbox for the lang actor server to decode + sendByName
// (decode must run as lang on this VM, which we cannot do here without clobbering
// the running frame). Returns the number routed (forwarded + stashed).
static void ffi_pumpSiloOutboxes(ts::VM& vm, u16 dst, u16, u16) {
    auto* ctx = getAppContext(vm);
    auto* eng = ctx ? ctx->engine : nullptr;
    int routed = 0;
    if (ctx && eng) {
        for (auto& state : ctx->siloVMs) {
            if (!state.taskSched) continue;
            auto* sched = static_cast<SiloTaskScheduler*>(state.taskSched);
            OutboxMsg m;
            while (sched->outbox_.pop(m)) {
                if (m.targetSilo < 0) {
                    // Bound for an NRT actor: hand off to the lang server.
                    AppContext::NrtActorMsg nm;
                    nm.name = m.name;
                    nm.bytes.assign(m.buf, m.buf + m.len);
                    ctx->nrtActorInbox.push_back(std::move(nm));
                    ++routed;
                    continue;
                }
                if (m.targetSilo >= (int)ctx->siloVMs.size()) continue;
                auto& tstate = ctx->siloVMs[(size_t)m.targetSilo];
                if (!tstate.vm || tstate.deliverGlobalIndex < 0) continue;
                auto* deliverFn = static_cast<ts::CodeBlock*>(
                    tstate.vm->global((u32)tstate.deliverGlobalIndex).p);
                auto* cmd = new DeliverActorMsgCmd(deliverFn, m.name, m.buf, m.len);
                sendCmdListToSilo(eng, m.targetSilo, cmd);
                ++routed;
            }
        }
    }
    vm.makeCurrent();
    vm.reg(dst).i = routed;
}

// fn _nrtActorMsgCount() Int -- number of silo->NRT messages waiting in the
// inbox (stashed by pumpSiloOutboxes). Main thread only.
static void ffi_nrtActorMsgCount(ts::VM& vm, u16 dst, u16, u16) {
    auto* ctx = getAppContext(vm);
    vm.reg(dst).i = ctx ? (i64)ctx->nrtActorInbox.size() : 0;
}

// fn _nrtActorMsgName() Symbol -- the destination actor name of the head
// silo->NRT message (peek, does not pop). Empty symbol if none.
static void ffi_nrtActorMsgName(ts::VM& vm, u16 dst, u16, u16) {
    auto* ctx = getAppContext(vm);
    ts::SymbolPtr name = (ctx && !ctx->nrtActorInbox.empty())
                             ? ctx->nrtActorInbox.front().name
                             : ts::intern("");
    vm.reg(dst).s = name;
}

// fn _nrtActorMsgTake() Bytes -- the encoded Msg of the head silo->NRT message,
// popping it. The lang server decodes this and sendByName's it to the actor named
// by _nrtActorMsgName(). Empty Bytes if none.
static void ffi_nrtActorMsgTake(ts::VM& vm, u16 dst, u16, u16) {
    auto* ctx = getAppContext(vm);
    if (ctx && !ctx->nrtActorInbox.empty()) {
        auto& front = ctx->nrtActorInbox.front();
        auto* b = new ts::BytesObj(front.bytes.data(), front.bytes.size());
        ctx->nrtActorInbox.pop_front();
        vm.reg(dst).o = b;
    } else {
        vm.reg(dst).o = new ts::BytesObj(nullptr, 0);
    }
}

// fn _sleepMs(ms Int) Void -- NRT only; used by the actor-server poll loop.
static void ffi_sleepMs(ts::VM& vm, u16 dst, u16, u16 argBase) {
    int ms = static_cast<int>(vm.reg(argBase).i);
    if (ms > 0) usleep(static_cast<useconds_t>(ms) * 1000);
    vm.reg(dst).i = 0;
}

void registerAudioEngineFFI(ts::Compiler& compiler) {
    auto* Void   = compiler.voidType();
    auto* Int    = compiler.intType();
    auto* Float  = compiler.floatType();
    auto* Bool   = compiler.boolType();
    auto* String = compiler.stringType();
    auto* Symbol = compiler.symbolType();
    auto* Bytes  = compiler.bytesType();
    // ArrayType is forward-declared; we avoid including type_system.hpp
    // to prevent TLS-related link issues. The cast is safe because
    // ArrayType inherits from Type.
    ts::Type* FloatArray = reinterpret_cast<ts::Type*>(compiler.arrayType(Float));
    ts::Type* IntArray = reinterpret_cast<ts::Type*>(compiler.arrayType(Int));
    // Future<String>, returned by siloLoad for the async load barrier.
    ts::Type* FutureString = reinterpret_cast<ts::Type*>(compiler.futureType(String));
    // Fn() Float -- the silo task handler type (returns the next beat-delta).
    ts::Vec<ts::Type*> noArgs;
    ts::Type* FnFloat = reinterpret_cast<ts::Type*>(compiler.functionType(noArgs, Float));

    using R = void (*)(ts::VM&, u16, u16, u16);

    // Helper to reduce registration boilerplate.
    // All functions go into the "audio_engine_ffi" module namespace.
    // The script wrapper `bridge/modules/audio_engine.x` re-exports these as
    // `audio_engine.*` so users can write `import audio_engine.*;`.
    // pure=false for all (side-effecting), rtSafe varies.
    auto reg = [&](const char* name, ts::Type* retType,
                   std::vector<ts::Type*> params, R fn, bool rtSafe = false) {
        compiler.registerForeignModuleFunction("audio_engine_ffi", name, retType,
                                               std::move(params), fn,
                                               /*pure=*/false, rtSafe);
    };

    // Engine lifecycle
    reg("engineStart",      Void, {},              ffi_engineStart);
    reg("engineStop",       Void, {},              ffi_engineStop);
    reg("isAudioRunning",   Bool, {},              ffi_isAudioRunning);
    reg("getStreamTime",    Float, {},             ffi_getStreamTime);
    reg("masterGain",       Void, {Float},         ffi_masterGain);
    reg("safetyLimiter",    Void, {Bool},          ffi_safetyLimiter);
    reg("inputChannels",    Int, {},               ffi_inputChannels);
    // NRT-render FFI is registered separately by the bridge::registerNRTRenderFFI
    // entry point (see bridge/include/tzpl_nrt_render.hpp). It defines:
    //   renderNRT(path, setup), renderNRT(path, duration, setup),
    //   isRenderDone(h), onRenderDone(h, cb),
    //   stopRender(h), stopRender(h, tail),
    //   endRender(), endRender(tail).

    // Plugin loading (NRT only)
    reg("loadPlugins",      Bool, {String},                ffi_loadPlugins);
    reg("loadPlugin",       Bool, {String, String},        ffi_loadPlugin);

    // Command bundling (rtSafe — these just queue commands via lock-free FIFO)
    reg("begin",            Int, {},               ffi_begin,         true);
    reg("go",               Int, {Int},            ffi_sched_immediate, true);
    reg("sched",            Int, {Int},            ffi_sched_immediate, true);
    reg("sched",            Int, {Int, Int, Float}, ffi_sched,        true);
    reg("schedPolicy",      Int, {Int, Int, Float, Int}, ffi_schedPolicy, true);
    reg("setTempo",         Int, {Int, Float},     ffi_setTempo,      true);
    reg("schedTempoChange", Int, {Int, Float, Float, Float}, ffi_schedTempoChange, true);
    reg("clockBeats",       Float, {Int},          ffi_clockBeats,    true);
    reg("clockTempo",       Float, {Int},          ffi_clockTempo,    true);

    // Node operations (rtSafe — queued via command bundling)
    reg("newNode",          Int, {String, Int},    ffi_newNode,       true);
    reg("freeNode",         Int, {Int},            ffi_freeNode,      true);
    reg("freeAllNodes",     Int, {},               ffi_freeAllNodes,  true);
    reg("channelOffset",    Int, {Int},            ffi_channelOffset, true);

    // Buffers (bundled; loadBuffer reads the file at submit on the caller)
    reg("resizeBuffer",     Int, {Int, Int, Int, Int},    ffi_resizeBuffer, true);
    reg("loadBuffer",       Int, {Int, Int, String},      ffi_loadBuffer);

    // Connections (rtSafe)
    reg("connect",          Int, {Int, Int, Int, Int},             ffi_connect,          true);
    reg("connectX",         Int, {Int, Int, Int, Int, Float, Int}, ffi_connectX,         true);
    reg("disconnectInput",  Int, {Int, Int},                       ffi_disconnectInput,  true);
    reg("disconnectInputX", Int, {Int, Int, Float, Int},           ffi_disconnectInputX, true);
    reg("disconnectSource",  Int, {Int, Int, Int, Int},             ffi_disconnectSource,  true);
    reg("disconnectSourceX",Int, {Int, Int, Int, Int, Float, Int}, ffi_disconnectSourceX, true);
    reg("disconnectOutput", Int, {Int, Int},                       ffi_disconnectOutput, true);
    reg("disconnectNode",   Int, {Int},                            ffi_disconnectNode,   true);
    reg("reconnectOutput",  Int, {Int, Int, Int, Int, Float, Int}, ffi_reconnectOutput,  true);
    reg("replaceNode",      Int, {Int, Int, Float, Int},           ffi_replaceNode,      true);

    // Parameter control (rtSafe)
    reg("setInput",         Int, {Int, Int, Float},             ffi_setInput,    true);
    reg("setInputX",        Int, {Int, Int, Float, Float, Int}, ffi_setInputX,   true);
    reg("setControl",       Int, {Int, Int, Float},             ffi_setControl,  true);

    // Note / voice management (rtSafe)
    reg("noteOn",           Int, {Int, Int, FloatArray},  ffi_noteOn,       true);
    reg("noteOff",          Int, {Int, Int},              ffi_noteOff,      true);
    reg("allNotesOff",      Int, {Int},                   ffi_allNotesOff,  true);

    reg("noteSetParams",    Int, {Int, Int, Int, FloatArray}, ffi_noteSetParams, true);

    // Introspection
    ts::Type* StringArray = reinterpret_cast<ts::Type*>(compiler.arrayType(String));
    reg("listSynthDefs",    StringArray, {},  ffi_listSynthDefs);

    // Silo VM management (NRT only)
    reg("attachVM",         Int, {Int},            ffi_attachVM);
    reg("detachVM",         Int, {Int},            ffi_detachVM);
    reg("siloEval",         Int, {Int, String},    ffi_siloEval);
    reg("siloLoad",         FutureString, {Int, String}, ffi_siloLoad);
    reg("siloStartAt",      Void, {Float, IntArray},      ffi_siloStartAt);
    reg("siloDeliverBytes",   Int, {Int, Symbol, Bytes},              ffi_siloDeliverBytes);
    reg("siloDeliverBytesAt", Int, {Int, Int, Float, Symbol, Bytes},  ffi_siloDeliverBytesAt);
    reg("siloOutbox",         Int,  {Int, Symbol, Bytes},             ffi_siloOutbox, true);
    reg("pumpSiloOutboxes",   Int,  {},                               ffi_pumpSiloOutboxes);
    reg("nrtActorMsgCount",   Int,    {},                             ffi_nrtActorMsgCount);
    reg("nrtActorMsgName",    Symbol, {},                             ffi_nrtActorMsgName);
    reg("nrtActorMsgTake",    Bytes,  {},                             ffi_nrtActorMsgTake);
    reg("sleepMs",            Void, {Int},                            ffi_sleepMs);
    reg("readFile",         String, {String},             ffi_readFile);
    reg("scheduleTask",     Int,  {Int, FnFloat},         ffi_scheduleTask, true);
    reg("playNote",         Int,  {Int, Int, FloatArray}, ffi_playNote,     true);
    reg("releaseNote",      Int,  {Int, Int},             ffi_releaseNote,  true);

    // Enum constants (SchedPolicy, FadeCurve, Err, Enable) are defined
    // in the Tzopilotl module: bridge/modules/audio_engine.x
    // The foreign functions above merge into that module, so
    // `import audio_engine.*;` gives access to both enums and functions.
}

void setAppContextOnVM(void* vm_ptr, AppContext* ctx) {
    auto* vm = static_cast<ts::VM*>(vm_ptr);
    vm->setUserData(ctx);
}

} // namespace bridge
