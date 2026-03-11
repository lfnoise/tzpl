/*
 *  tzpl_plugin_abi.h
 *  tzpl audio engine
 *
 *  Pure C plugin ABI - shared between engine and synthdef-compiler.
 *  Defines the interface for dynamically loadable synth plugins.
 */

#ifndef tzpl_plugin_abi_h
#define tzpl_plugin_abi_h

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct tzpl_Engine;
struct tzpl_Node;
struct tzpl_SynthData;

/* Error codes */
typedef enum tzpl_SErr {
    tzpl_errNone = 0,
    tzpl_errInternal,
    tzpl_errNodeIDAlreadyTaken,
    tzpl_errNodeDefNotFound,
    tzpl_errNodeNotFound,
    tzpl_errNoteNotFound,
    tzpl_errControlNotFound,
    tzpl_errDeviceNotFound,
    tzpl_errAlreadyAdded,
    tzpl_errAlreadyRemoved,
    tzpl_errSiloOutOfRange,
    tzpl_errInputOutOfRange,
    tzpl_errOutputOutOfRange,
    tzpl_errNoAudioDevices,
    tzpl_errAudioNotInitialized,
    tzpl_errCommandsQueuedButNotSent,
    tzpl_errNoActiveBundle,
    tzpl_errEngineInUse,
    tzpl_errCyclicConnection,
    
    tzpl_errTypeMismatch,
    tzpl_errRateMismatch,
    tzpl_errChanMismatch,
    tzpl_errNumPortsMismatch,
    
    tzpl_errNotImplemented,
    
    tzpl_errTooLate,
} tzpl_SErr;

/* Signal rate - defines how signals are processed */
typedef enum tzpl_Rate {
    tzpl_constRate = 0,  /* Constant value */
    tzpl_initRate,       /* Initialized once */
    tzpl_resetRate,      /* Reset rate */
    tzpl_eventRate,      /* Event rate */
    tzpl_audioRate       /* Audio Rate */
} tzpl_Rate;

/* Element type - bit 0 = is_float, bit 1 = is_64bit */
typedef enum tzpl_ElemType {
    tzpl_kI32 = 0,  /* 32-bit integer */
    tzpl_kF32 = 1,  /* 32-bit float */
    tzpl_kI64 = 2,  /* 64-bit integer */
    tzpl_kF64 = 3,  /* 64-bit float */
} tzpl_ElemType;

/* Signal type descriptor - combines element type, rate, and channel count */
typedef struct tzpl_SignalType {
    tzpl_ElemType elem;  /* Element type */
    tzpl_Rate rate;      /* Signal rate */
    int chans;           /* Number of channels */
} tzpl_SignalType;

/* Slice for passing multi-channel data */
typedef struct tzpl_Slice {
    int chans;
    void* data;
} tzpl_Slice;

/* Parameter pair for note control */
typedef struct tzpl_ParamPair {
    int index;
    float value;
} tzpl_ParamPair;

/* Control kinds */
typedef enum tzpl_ControlKind {
    tzpl_ckContinuous = 0,
    tzpl_ckTrigger,
    tzpl_ckBoolean,
    tzpl_ckSelect
} tzpl_ControlKind;

/* Control warp types for mapping */
typedef enum tzpl_ControlWarp {
    tzpl_cwNone = 0,
    tzpl_cwLinear,
    tzpl_cwExponential
} tzpl_ControlWarp;

/* Control specification */
typedef struct tzpl_ControlSpec {
    double lo, hi, init, param;
    tzpl_ControlWarp warp;
    tzpl_ControlKind kind;
} tzpl_ControlSpec;

/* Port definition - describes an input or output port */
typedef struct tzpl_PortDef {
    const char* name;
    tzpl_SignalType type;
} tzpl_PortDef;

/* Control definition - describes a control parameter */
typedef struct tzpl_ControlDef {
    const char* name;
    tzpl_SignalType type;
    uint64_t id;
    tzpl_ControlSpec spec;
} tzpl_ControlDef;

/* Control dispatch table - for hierarchical control lookup */
struct tzpl_ControlDispatchTable;

typedef struct tzpl_ControlDispatchTableEntry {
    int which;  /* 0 = table, 1 = def */
    union {
        struct tzpl_ControlDispatchTable* table;
        tzpl_ControlDef* def;
    };
} tzpl_ControlDispatchTableEntry;

typedef struct tzpl_ControlDispatchTable {
    size_t size;
    const char** names;
    tzpl_ControlDispatchTableEntry* entries;
} tzpl_ControlDispatchTable;

/* Function pointers for synth operations */
typedef struct tzpl_SynthFuns {
    struct tzpl_SynthData* (*alloc)(void);
    tzpl_SErr (*free)(struct tzpl_SynthData* synth);
    tzpl_SErr (*init)(struct tzpl_SynthData* synth);
    tzpl_SErr (*uninit)(struct tzpl_SynthData* synth);  /* optional */
    tzpl_SErr (*reset)(struct tzpl_SynthData* synth);   /* optional */
    tzpl_SErr (*event)(struct tzpl_SynthData* synth, uint64_t id,
                       tzpl_Slice dst, tzpl_Slice data);
    void (*processEvents)(struct tzpl_SynthData* synth);
    void (*processAudio)(struct tzpl_SynthData* synth);

    /* Note allocation (all optional) */
    tzpl_SErr (*noteOn)(struct tzpl_SynthData* synth, int64_t now,
                        int noteID, int n, float* params);
    tzpl_SErr (*noteOff)(struct tzpl_SynthData* synth, int64_t now, int noteID);
    tzpl_SErr (*allNotesOff)(struct tzpl_SynthData* synth, int64_t now);
    tzpl_SErr (*noteSetParams)(struct tzpl_SynthData* synth, int noteID,
                               int n, tzpl_ParamPair* params);
    tzpl_SErr (*noteSetParamRange)(struct tzpl_SynthData* synth, int noteID,
                                   int first, int length, float* values);
} tzpl_SynthFuns;

/* Synth definition - returned by plugin load function */
typedef struct tzpl_SynthDef {
    const char* name;
    tzpl_SynthFuns funs;
    int num_ins;
    int num_outs;
    int num_controls;
    tzpl_PortDef* ins;
    tzpl_PortDef* outs;
    tzpl_ControlDef* controls;
    tzpl_ControlDispatchTable controlDispatch;
} tzpl_SynthDef;

/* Synth instance data - base struct (plugins extend this with custom data) */
typedef struct tzpl_SynthData {
    tzpl_SynthFuns funs;
    struct tzpl_Engine* engine;  /* Opaque pointer to audio engine */
    struct tzpl_Node* node;      /* Opaque pointer to containing node */
    int num_ins;
    int num_outs;
    int num_controls;
    void** inlets;    /* Array of input data pointers */
    void** outlets;   /* Array of output data pointers */
    void** controls;  /* Array of control data pointers */
    double fs, sd;  /* Sample rate, sample duration */
    /* Custom data follows in derived structs... */
} tzpl_SynthData;

/* Plugin load function type - returns a SynthDef describing the plugin */
typedef tzpl_SynthDef (*tzpl_LoadSynthDefFun)(void);

#ifdef __cplusplus
}
#endif

#endif /* tzpl_plugin_abi_h */
