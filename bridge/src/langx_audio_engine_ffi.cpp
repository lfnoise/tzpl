//
//  langx_audio_engine_ffi.cpp
//  bridge
//
//  FFI bridge: wraps audio-engine client functions into the CFun signature
//  expected by the Language X VM, and registers them with the compiler.
//

#include "langx_audio_engine_ffi.hpp"
#include "langx.hpp"
#include "jscs_client_interface.hpp"

// Both langx and audio-engine define i64/f64/etc. in different ways.
// audio-engine: namespace engine { using i64 = long; }
// langx:        using i64 = int64_t;  (which is long long on macOS)
// We use explicit namespace qualification and casts where needed.

namespace bridge {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Retrieve the Engine* stored in the VM's userData.
static engine::Engine* getEngine(ts::VM& vm) {
    return static_cast<engine::Engine*>(vm.userData());
}

// Convert a jscs_SErr to an Int return value (0 = success).
static void returnErr(ts::VM& vm, u16 dst, jscs_SErr err) {
    vm.reg(dst).i = static_cast<i64>(err);
}

// Extract a C string from a Language X String object in a register.
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
    int policy = static_cast<int>(vm.reg(argBase + 1).i);
    returnErr(vm, dst, engine::sched(time, static_cast<engine::SchedPolicy>(policy)));
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
static void ffi_setInput(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::PortAddr port{static_cast<engine::i64>(vm.reg(argBase).i),
                          static_cast<int>(vm.reg(argBase + 1).i)};
    engine::f64 val = vm.reg(argBase + 2).f;
    returnErr(vm, dst, engine::setInput(port, 1, &val));
}

// fn setInputX(nodeID: Int, portIndex: Int, value: Float,
//              xfade: Float, curve: Int) -> Int
static void ffi_setInputX(ts::VM& vm, u16 dst, u16, u16 argBase) {
    engine::PortAddr port{static_cast<engine::i64>(vm.reg(argBase).i),
                          static_cast<int>(vm.reg(argBase + 1).i)};
    engine::f64 val = vm.reg(argBase + 2).f;
    f64 xfade = vm.reg(argBase + 3).f;
    auto curve = static_cast<engine::FadeCurve>(vm.reg(argBase + 4).i);
    returnErr(vm, dst, engine::setInput(port, 1, &val, xfade, curve));
}

// fn setControl(nodeID: Int, controlID: Int, value: Float) -> Int
// Convenience: sets a single float control value.
static void ffi_setControl(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    auto controlID = static_cast<engine::i64>(vm.reg(argBase + 1).i);
    engine::f64 val = vm.reg(argBase + 2).f;
    returnErr(vm, dst, engine::setControl(nodeID, controlID, 1, &val));
}

// ---------------------------------------------------------------------------
// Note / voice management
// ---------------------------------------------------------------------------

// fn noteOn(nodeID: Int, noteID: Int, params: Array[Float]) -> Int
static void ffi_noteOn(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto nodeID = static_cast<engine::i64>(vm.reg(argBase).i);
    int noteID = static_cast<int>(vm.reg(argBase + 1).i);

    // Extract float array from Language X Array[Float] object
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

// ---------------------------------------------------------------------------
// Scheduling policy constants
// ---------------------------------------------------------------------------

// fn schedImmediate() -> Int
static void ffi_schedImmediate(ts::VM& vm, u16 dst, u16, u16) {
    vm.reg(dst).i = engine::schedImmediate;
}

// fn schedBetterLateThanNever() -> Int
static void ffi_schedBetterLateThanNever(ts::VM& vm, u16 dst, u16, u16) {
    vm.reg(dst).i = engine::schedBetterLateThanNever;
}

// fn schedOnTimeOnly() -> Int
static void ffi_schedOnTimeOnly(ts::VM& vm, u16 dst, u16, u16) {
    vm.reg(dst).i = engine::schedOnTimeOnly;
}

// ---------------------------------------------------------------------------
// Fade curve constants
// ---------------------------------------------------------------------------

// fn fadeLinear() -> Int
static void ffi_fadeLinear(ts::VM& vm, u16 dst, u16, u16) {
    vm.reg(dst).i = engine::fadeLinear;
}

// fn fadeExponential() -> Int
static void ffi_fadeExponential(ts::VM& vm, u16 dst, u16, u16) {
    vm.reg(dst).i = engine::fadeExponential;
}

// fn fadeSmoothstep() -> Int
static void ffi_fadeSmoothstep(ts::VM& vm, u16 dst, u16, u16) {
    vm.reg(dst).i = engine::fadeSmoothstep;
}

// fn fadeEqualPower() -> Int
static void ffi_fadeEqualPower(ts::VM& vm, u16 dst, u16, u16) {
    vm.reg(dst).i = engine::fadeEqualPower;
}

// ---------------------------------------------------------------------------
// Error code constants
// ---------------------------------------------------------------------------

// fn errNone() -> Int
static void ffi_errNone(ts::VM& vm, u16 dst, u16, u16) {
    vm.reg(dst).i = jscs_errNone;
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
    // pure=false for all (side-effecting), rtSafe varies.
    auto reg = [&](const char* name, ts::Type* retType,
                   std::vector<ts::Type*> params, R fn, bool rtSafe = false) {
        compiler.registerForeignFunction(name, retType, std::move(params), fn,
                                          /*pure=*/false, rtSafe);
    };

    // Engine lifecycle
    reg("engineStart",      Void, {},              ffi_engineStart);
    reg("engineStop",       Void, {},              ffi_engineStop);
    reg("isAudioRunning",   Bool, {},              ffi_isAudioRunning);
    reg("getStreamTime",    Float, {},             ffi_getStreamTime);
    reg("masterGain",       Void, {Float},         ffi_masterGain);
    reg("safetyLimiter",    Void, {Bool},          ffi_safetyLimiter);

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

    // Scheduling policy constants (pure, rtSafe)
    auto regConst = [&](const char* name, R fn) {
        compiler.registerForeignFunction(name, Int, {}, fn, /*pure=*/true, /*rtSafe=*/true);
    };
    regConst("schedImmediate",          ffi_schedImmediate);
    regConst("schedBetterLateThanNever", ffi_schedBetterLateThanNever);
    regConst("schedOnTimeOnly",         ffi_schedOnTimeOnly);
    regConst("fadeLinear",              ffi_fadeLinear);
    regConst("fadeExponential",         ffi_fadeExponential);
    regConst("fadeSmoothstep",          ffi_fadeSmoothstep);
    regConst("fadeEqualPower",          ffi_fadeEqualPower);
    regConst("errNone",                 ffi_errNone);
}

void setEngineOnVM(void* vm_ptr, engine::Engine* engine) {
    auto* vm = static_cast<ts::VM*>(vm_ptr);
    vm->setUserData(engine);
}

} // namespace bridge
