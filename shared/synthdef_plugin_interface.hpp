//
//  synthdef_plugin_interface.hpp
//  sapf-after-rust-cpp-xcode
//
//  Created by James McCartney on 8/6/24.
//

#ifndef synthdef_plugin_interface_hpp
#define synthdef_plugin_interface_hpp

#include "synthdef_types.hpp"
#include <cstdint>

namespace synthdef {

enum SErr {
    errNone,
    errInternal,
    errNodeIDAlreadyTaken,
    errNodeDefNotFound,
    errNodeNotFound,
    errNoteNotFound,
    errControlNotFound,
    errDeviceNotFound,
    errAlreadyAdded,
    errAlreadyRemoved,
    errSiloOutOfRange,
    errInputOutOfRange,
    errOutputOutOfRange,
    errNoAudioDevices,
    errAudioNotInitialized,
    errCommandsQueuedButNotSent,
    errNoActiveBundle,
    errEngineInUse,
    errCyclicConnection,

    errTypeMismatch,
    errRateMismatch,
    errChanMismatch,
    errNumPortsMismatch,

    errNotImplemented,
    
    errTooLate,
};

// signal rate
enum Rate {
    constRate,
    initRate,
    resetRate,
    pushRate,
    pullRate
};

enum ElemType {
    kI32, kI64, kF32, kF64,
};

struct SignalType {
    ElemType elem; 
    Rate rate; 
    int chans; // number of channels.
};

struct slice_t { int chans; void* data; };

struct ParamPair {
	int index;
	f32 value;
};

struct PortDef {
    const char* name;
    SignalType type;
};

enum ControlKind {
    ckContinuous, ckTrigger, ckBoolean, ckSelect
};

enum ControlWarp {
    cwNone, cwLinear, cwExponential
};

struct ControlSpecC {
    f64 lo, hi, init, param;
    ControlWarp warp;
    ControlKind kind;
};

struct ControlDef {
    const char* name;
    SignalType type;
    u64 id;
    ControlSpecC spec;
};

struct SynthData;

struct SynthFuns {
    SynthData* (*alloc)();
    SErr (*free)(SynthData* synth);
    SErr (*init)(SynthData* synth);
    SErr (*uninit)(SynthData* synth); // optional
    SErr (*reset)(SynthData* synth); // optional
    SErr (*event)(SynthData* synth, u64 id, slice_t dst, slice_t data);
    void (*process_events)(SynthData* p);
    void (*pull)(SynthData* synth);
    
    // note allocation. all optional.
    SErr (*noteOn)(SynthData* synth, i64 now, int noteID, int n, f32* params);
    SErr (*noteOff)(SynthData* synth, i64 now, int noteID);
    SErr (*allNotesOff)(SynthData* synth, i64 now);
    SErr (*noteSetParams)(SynthData* synth, int noteID, int n, ParamPair* params);
    SErr (*noteSetParamRange)(SynthData* synth, int noteID, int first, int length, f32* values);
};

struct ControlDispatchTable;
struct ControlDispatchTableEntry {
    enum {
        kControlTable,
        kControlDef,
    };
    int which;
    union {
        ControlDispatchTable* table;
        ControlDef* def;
    };
};

struct ControlDispatchTable {
    usize size;
    const char** names;
    ControlDispatchTableEntry* entries;
};

struct SynthDef {
    const char* name;
    SynthFuns funs;
    int num_ins;
    int num_outs;
    int num_controls;
    PortDef* ins;
    PortDef* outs;
    ControlDef* controls;
    ControlDispatchTable controlDispatch;
};

struct SynthData {
    SynthFuns funs;
    struct Engine* engine; // Opaque pointer to the real time audio engine.
    struct Node* node;
    int num_ins;
    int num_outs;
    int num_controls;
    void** inlets;
    void** outlets;
    void** controls;
    f64 fs, sd;        // the sampling rate, sample duration.
    // custom data follows..
};



#if 0 // not yet implemented
void dispatchControl(const char* path, std::function<void(void)> fun);
#endif



//template <typename T, typename... Args>
//T select(size_t index, Args... args) {
//    size_t i = 0;
//    T result = T(0);
//
//    // Use a fold expression to increment `i` and conditionally set `result`
//    auto select_helper = [&](auto& stream) {
//        if (i == index) {
//            result = stream.next();
//        }
//        ++i;
//    };
//
//    // Unpack the streams and apply the lambda to each one
//    (select_helper(args), ...);
//
//    return result;
//}



}

#endif /* synthdef_plugin_interface_hpp */
