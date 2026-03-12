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
#include "tzpl.hpp"
#include "value.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_engine.hpp"
#include <thread>
#include <chrono>

// Both tzpl and engine define i64/f64/etc. in different ways.
// engine: namespace engine { using i64 = long; }
// tzpl:        using i64 = int64_t;  (which is long long on macOS)
// We use explicit namespace qualification and casts where needed.

namespace bridge {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Retrieve the Engine* stored in the VM's userData.
static engine::Engine* getEngine(ts::VM& vm) {
    return static_cast<engine::Engine*>(vm.userData());
}

// Convert a tzpl_SErr to an Int return value (0 = success).
static void returnErr(ts::VM& vm, u16 dst, tzpl_SErr err) {
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

// fn sleep(seconds: Float) -> Void
// Blocking sleep — NRT only.  Will be replaced by a proper event scheduler.
static void ffi_sleep(ts::VM& vm, u16, u16, u16 argBase) {
    f64 seconds = vm.reg(argBase).f;
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
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
    returnErr(vm, dst, engine::begin(getEngine(vm), silo));
}

// fn go() -> Int
static void ffi_go(ts::VM& vm, u16 dst, u16, u16) {
    returnErr(vm, dst, engine::go());
}

// fn sched(time: Float) -> Int
static void ffi_sched(ts::VM& vm, u16 dst, u16, u16 argBase) {
    f64 time = vm.reg(argBase).f;
    returnErr(vm, dst, engine::sched(time));
}

// fn schedPolicy(time: Float, policy: Int) -> Int
static void ffi_schedPolicy(ts::VM& vm, u16 dst, u16, u16 argBase) {
    f64 time = vm.reg(argBase).f;
    auto policy = static_cast<engine::SchedPolicy>(vm.reg(argBase + 1).i);
    returnErr(vm, dst, engine::sched(time, policy));
}

// ---------------------------------------------------------------------------
// Node operations
// ---------------------------------------------------------------------------

// fn newNode(defName: String, nodeID: Int) -> Int
static void ffi_newNode(ts::VM& vm, u16 dst, u16, u16 argBase) {
    const char* defName = regString(vm, argBase);
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase + 1).i);
    returnErr(vm, dst, engine::newNode(defName, nodeID));
}

// fn freeNode(nodeID: Int) -> Int
static void ffi_freeNode(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    returnErr(vm, dst, engine::freeNode(nodeID));
}

// fn freeAllNodes() -> Int
static void ffi_freeAllNodes(ts::VM& vm, u16 dst, u16, u16) {
    returnErr(vm, dst, engine::freeAllNodes());
}

// fn channelOffset(offset: Int) -> Int
static void ffi_channelOffset(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto offset = static_cast<engine::i32>(vm.reg(argBase).i);
    returnErr(vm, dst, engine::channelOffset(offset));
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
    returnErr(vm, dst, engine::connect(src, dst_port));
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
    returnErr(vm, dst, engine::connect(src, dst_port, xfade, curve));
}

// fn disconnectInput(dstNode: Int, dstPort: Int) -> Int
static void ffi_disconnectInput(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::PortAddr dst_port{static_cast<engine::i64>(vm.reg(argBase).i),
                              static_cast<int>(vm.reg(argBase + 1).i)};
    returnErr(vm, dst, engine::disconnectInput(dst_port));
}

// fn disconnectInputX(dstNode: Int, dstPort: Int, xfade: Float, curve: Int) -> Int
static void ffi_disconnectInputX(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::PortAddr dst_port{static_cast<engine::i64>(vm.reg(argBase).i),
                              static_cast<int>(vm.reg(argBase + 1).i)};
    f64 xfade = vm.reg(argBase + 2).f;
    auto curve = static_cast<engine::FadeCurve>(vm.reg(argBase + 3).i);
    returnErr(vm, dst, engine::disconnectInput(dst_port, xfade, curve));
}

// fn disconnectOutput(srcNode: Int, srcPort: Int) -> Int
static void ffi_disconnectOutput(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::PortAddr src{static_cast<engine::i64>(vm.reg(argBase).i),
                         static_cast<int>(vm.reg(argBase + 1).i)};
    returnErr(vm, dst, engine::disconnectOutput(src));
}

// fn disconnectNode(nodeID: Int) -> Int
static void ffi_disconnectNode(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    returnErr(vm, dst, engine::disconnectNode(nodeID));
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
    returnErr(vm, dst, engine::reconnectOutput(oldSrc, newSrc, xfade, curve));
}

// fn replaceNode(oldNodeID: Int, newNodeID: Int, xfade: Float, curve: Int) -> Int
static void ffi_replaceNode(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto oldID = static_cast<engine::i64>(vm.reg(argBase).i);
    auto newID = static_cast<engine::i64>(vm.reg(argBase + 1).i);
    f64 xfade = vm.reg(argBase + 2).f;
    auto curve = static_cast<engine::FadeCurve>(vm.reg(argBase + 3).i);
    returnErr(vm, dst, engine::replaceNode(oldID, newID, xfade, curve));
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
    returnErr(vm, dst, engine::setInput(port, 1, &val));
}

// fn setInputX(nodeID: Int, portIndex: Int, value: Float,
//              xfade: Float, curve: Int) -> Int
static void ffi_setInputX(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::PortAddr port{static_cast<engine::i64>(vm.reg(argBase).i),
                          static_cast<int>(vm.reg(argBase + 1).i)};
    engine::f32 val = static_cast<engine::f32>(vm.reg(argBase + 2).f);
    f64 xfade = vm.reg(argBase + 3).f;
    auto curve = static_cast<engine::FadeCurve>(vm.reg(argBase + 4).i);
    returnErr(vm, dst, engine::setInput(port, 1, &val, xfade, curve));
}

// fn setControl(nodeID: Int, controlID: Int, value: Float) -> Int
// Convenience: sets a single float control value.
static void ffi_setControl(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    auto controlID = static_cast<engine::i64>(vm.reg(argBase + 1).i);
    engine::f32 val = static_cast<engine::f32>(vm.reg(argBase + 2).f);
    returnErr(vm, dst, engine::setControl(nodeID, controlID, 1, &val));
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

    returnErr(vm, dst, engine::noteOn(nodeID, noteID, length, params));
}

// fn noteOff(nodeID: Int, noteID: Int) -> Int
static void ffi_noteOff(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    int noteID = static_cast<int>(vm.reg(argBase + 1).i);
    returnErr(vm, dst, engine::noteOff(nodeID, noteID));
}

// fn allNotesOff(nodeID: Int) -> Int
static void ffi_allNotesOff(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    returnErr(vm, dst, engine::allNotesOff(nodeID));
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

    returnErr(vm, dst, engine::noteSetParamRange(nodeID, noteID, first, length, params));
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
        arr->v.push_back(s);
    }
    vm.reg(dst).o = arr;
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

    using R = void (*)(ts::VM&, u16, u16, u16);

    // Helper to reduce registration boilerplate.
    // All functions go into the "audio_engine" module namespace.
    // pure=false for all (side-effecting), rtSafe varies.
    auto reg = [&](const char* name, ts::Type* retType,
                   std::vector<ts::Type*> params, R fn, bool rtSafe = false) {
        compiler.registerForeignModuleFunction("audio_engine", name, retType,
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

    // Blocking sleep (NRT only — temporary, will be replaced by a scheduler)
    reg("sleep",            Void, {Float},         ffi_sleep);

    // Plugin loading (NRT only)
    reg("loadPlugins",      Bool, {String},                ffi_loadPlugins);
    reg("loadPlugin",       Bool, {String, String},        ffi_loadPlugin);

    // Command bundling (rtSafe — these just queue commands via lock-free FIFO)
    reg("begin",            Int, {Int},            ffi_begin,         true);
    reg("go",               Int, {},               ffi_go,            true);
    reg("sched",            Int, {Float},          ffi_sched,         true);
    reg("schedPolicy",      Int, {Float, Int},     ffi_schedPolicy,   true);

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

    // Enum constants (SchedPolicy, FadeCurve, Err, Enable) are defined
    // in the Tzopilotl module: bridge/modules/audio_engine.x
    // The foreign functions above merge into that module, so
    // `import audio_engine.*;` gives access to both enums and functions.
}

void setEngineOnVM(void* vm_ptr, engine::Engine* engine) {
    auto* vm = static_cast<ts::VM*>(vm_ptr);
    vm->setUserData(engine);
}

} // namespace bridge
