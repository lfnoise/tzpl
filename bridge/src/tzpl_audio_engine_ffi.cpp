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
#include "tzpl_nrt_render.hpp"
#include "tzpl.hpp"
#include "value.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_engine.hpp"
#include "tzpl_vm_commands.hpp"
#include "nrt_vm.hpp"
#include "module_compiler.hpp"
#include "diagnostic.hpp"
#include <thread>
#include <string>
#include <utility>

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

// fn begin(silo: Int) -> Int   (returns error code, 0 = success)
static void ffi_begin(ts::VM& vm, u16 dst, u16, u16 argBase) {
    int silo = static_cast<int>(vm.reg(argBase).i);
    returnErr(vm, dst, engine::begin(getEngine(vm), silo), __func__);
}

// fn go() -> Int  (send bundle for immediate execution)
static void ffi_sched_immediate(ts::VM& vm, u16 dst, u16, u16) {
    returnErr(vm, dst, engine::go(), __func__);
}

// fn sched(clock: Int, beat: Float) -> Int
static void ffi_sched(ts::VM& vm, u16 dst, u16, u16 argBase) {
    int clock = static_cast<int>(vm.reg(argBase).i);
    f64 beat = vm.reg(argBase + 1).f;
    returnErr(vm, dst, engine::sched(clock, beat), __func__);
}

// fn schedPolicy(clock: Int, beat: Float, policy: Int) -> Int
static void ffi_schedPolicy(ts::VM& vm, u16 dst, u16, u16 argBase) {
    int clock = static_cast<int>(vm.reg(argBase).i);
    f64 beat = vm.reg(argBase + 1).f;
    auto policy = static_cast<engine::SchedPolicy>(vm.reg(argBase + 2).i);
    returnErr(vm, dst, engine::sched(clock, beat, policy), __func__);
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
    returnErr(vm, dst, engine::newNode(defName, nodeID), __func__);
}

// fn freeNode(nodeID: Int) -> Int
static void ffi_freeNode(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    returnErr(vm, dst, engine::freeNode(nodeID), __func__);
}

// fn freeAllNodes() -> Int
static void ffi_freeAllNodes(ts::VM& vm, u16 dst, u16, u16) {
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
struct SiloCodeInstallCmd : engine::Command {
    ts::CompileResult* code_;
    explicit SiloCodeInstallCmd(ts::CompileResult* code) : code_(code) {}
    void doRT(engine::Silo* s) override {
        auto* vm = static_cast<ts::VM*>(s->vm_);
        if (vm && code_) {
            vm->makeCurrent();
            vm->install(*code_);
        }
    }
    bool doNRT(engine::Silo* s) override {
        delete code_;
        return true;
    }
};

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
            vm->makeCurrent();
            auto* cb = static_cast<ts::CodeBlock*>(vm->global((u32)startIdx_).p);
            if (cb) vm->callFunction(cb, nullptr, 0);
            vm->gcHeartbeat();
        }
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
    state.moduleCompiler = std::make_unique<ts::ModuleCompiler>(
        *ctx->compiler,
        std::vector<std::string>(ctx->moduleCompiler->includePaths()));

    // Attach VM to silo (sets vm_ and heartbeatFn_ on the RT thread)
    sendCmdToSilo(eng, siloIndex, new AttachVMCmd(state.vm));

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

    // Compile with the silo's rt_restricted target (on the calling NRT thread)
    std::string source(code);
    ts::CompileResult compiled = ctx->compiler->compile(
        source, "<silo>", state.target, state.moduleCompiler.get());

    if (!compiled.success) {
        ts::printDiagnostics(compiled.errors, source, "<silo>", std::cerr, true);
        returnErr(vm, dst, tzpl_errInternal, __func__);
        return;
    }

    ts::CodeBlock* mainBlock = compiled.mainBlock;

    // Heap-allocate for async transfer to RT thread
    auto* result = new ts::CompileResult(std::move(compiled));
    auto* installCmd = new SiloCodeInstallCmd(result);

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
    std::string source(code);
    ts::CompileResult compiled = ctx->compiler->compile(
        source, "<siloLoad>", state.target, state.moduleCompiler.get());
    if (!compiled.success) {
        ts::printDiagnostics(compiled.errors, source, "<siloLoad>", std::cerr, true);
        resolveNow("siloLoad: compile error");
        return;
    }

    // Record the module's start() entry (Void, no params) so siloStartAt can
    // call it later -- by global index, no re-resolution needed.
    state.startGlobalIndex = -1;
    for (auto& ef : compiled.exportedFunctions) {
        if (ef.name == "start" && ef.paramTypes.empty()) {
            state.startGlobalIndex = (int)ef.globalIndex;
            break;
        }
    }

    ts::CodeBlock* mainBlock = compiled.mainBlock;
    auto* result = new ts::CompileResult(std::move(compiled));
    auto* installCmd = new SiloCodeInstallCmd(result);
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
        engine::begin(eng, siloIndex);
        engine::sendCommand(new SiloRunStartCmd(state.startGlobalIndex));
        engine::sched(0, beat, engine::schedBetterLateThanNever);
    }
    // When audio is stopped the scheduled start() ran inline on the silo VM
    // (makeCurrent(silo)); restore the main VM before returning to its bytecode.
    vm.makeCurrent();
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerAudioEngineFFI(ts::Compiler& compiler) {
    auto* Void   = compiler.voidType();
    auto* Int    = compiler.intType();
    auto* Float  = compiler.floatType();
    auto* Bool   = compiler.boolType();
    auto* String = compiler.stringType();
    // ArrayType is forward-declared; we avoid including type_system.hpp
    // to prevent TLS-related link issues. The cast is safe because
    // ArrayType inherits from Type.
    ts::Type* FloatArray = reinterpret_cast<ts::Type*>(compiler.arrayType(Float));
    ts::Type* IntArray = reinterpret_cast<ts::Type*>(compiler.arrayType(Int));
    // Future<String>, returned by siloLoad for the async load barrier.
    ts::Type* FutureString = reinterpret_cast<ts::Type*>(compiler.futureType(String));

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
    reg("begin",            Int, {Int},            ffi_begin,         true);
    reg("go",               Int, {},               ffi_sched_immediate, true);
    reg("sched",            Int, {},               ffi_sched_immediate, true);
    reg("sched",            Int, {Int, Float},     ffi_sched,         true);
    reg("schedPolicy",      Int, {Int, Float, Int}, ffi_schedPolicy,  true);
    reg("setTempo",         Int, {Int, Float},     ffi_setTempo,      true);
    reg("schedTempoChange", Int, {Int, Float, Float, Float}, ffi_schedTempoChange, true);
    reg("clockBeats",       Float, {Int},          ffi_clockBeats,    true);
    reg("clockTempo",       Float, {Int},          ffi_clockTempo,    true);

    // Node operations (rtSafe — queued via command bundling)
    reg("newNode",          Int, {String, Int},    ffi_newNode,       true);
    reg("freeNode",         Int, {Int},            ffi_freeNode,      true);
    reg("freeAllNodes",     Int, {},               ffi_freeAllNodes,  true);
    reg("channelOffset",    Int, {Int},            ffi_channelOffset, true);

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
