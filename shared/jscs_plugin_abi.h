/*
 *  jscs_plugin_abi.h
 *  jscs audio engine
 *
 *  Pure C plugin ABI - shared between engine and synthdef-compiler.
 *  Defines the interface for dynamically loadable synth plugins.
 */

#ifndef jscs_plugin_abi_h
#define jscs_plugin_abi_h

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct jscs_Engine;
struct jscs_Node;
struct jscs_SynthData;

/* Error codes */
typedef enum jscs_SErr {
    jscs_errNone = 0,
    jscs_errInternal,
    jscs_errNodeIDAlreadyTaken,
    jscs_errNodeDefNotFound,
    jscs_errNodeNotFound,
    jscs_errNoteNotFound,
    jscs_errControlNotFound,
    jscs_errDeviceNotFound,
    jscs_errAlreadyAdded,
    jscs_errAlreadyRemoved,
    jscs_errSiloOutOfRange,
    jscs_errInputOutOfRange,
    jscs_errOutputOutOfRange,
    jscs_errNoAudioDevices,
    jscs_errAudioNotInitialized,
    jscs_errCommandsQueuedButNotSent,
    jscs_errNoActiveBundle,
    jscs_errEngineInUse,
    jscs_errCyclicConnection,
    
    jscs_errTypeMismatch,
    jscs_errRateMismatch,
    jscs_errChanMismatch,
    jscs_errNumPortsMismatch,
    
    jscs_errNotImplemented,
    
    jscs_errTooLate,
} jscs_SErr;

/* Signal rate - defines how signals are processed */
typedef enum jscs_Rate {
    jscs_constRate = 0,  /* Constant value */
    jscs_initRate,       /* Initialized once */
    jscs_resetRate,      /* Reset rate */
    jscs_eventRate,      /* Event rate */
    jscs_audioRate       /* Audio Rate */
} jscs_Rate;

/* Element type - bit 0 = is_float, bit 1 = is_64bit */
typedef enum jscs_ElemType {
    jscs_kI32 = 0,  /* 32-bit integer */
    jscs_kF32 = 1,  /* 32-bit float */
    jscs_kI64 = 2,  /* 64-bit integer */
    jscs_kF64 = 3,  /* 64-bit float */
} jscs_ElemType;

/* Signal type descriptor - combines element type, rate, and channel count */
typedef struct jscs_SignalType {
    jscs_ElemType elem;  /* Element type */
    jscs_Rate rate;      /* Signal rate */
    int chans;           /* Number of channels */
} jscs_SignalType;

/* Slice for passing multi-channel data */
typedef struct jscs_Slice {
    int chans;
    void* data;
} jscs_Slice;

/* Parameter pair for note control */
typedef struct jscs_ParamPair {
    int index;
    float value;
} jscs_ParamPair;

/* Control kinds */
typedef enum jscs_ControlKind {
    jscs_ckContinuous = 0,
    jscs_ckTrigger,
    jscs_ckBoolean,
    jscs_ckSelect
} jscs_ControlKind;

/* Control warp types for mapping */
typedef enum jscs_ControlWarp {
    jscs_cwNone = 0,
    jscs_cwLinear,
    jscs_cwExponential
} jscs_ControlWarp;

/* Control specification */
typedef struct jscs_ControlSpec {
    double lo, hi, init, param;
    jscs_ControlWarp warp;
    jscs_ControlKind kind;
} jscs_ControlSpec;

/* Port definition - describes an input or output port */
typedef struct jscs_PortDef {
    const char* name;
    jscs_SignalType type;
} jscs_PortDef;

/* Control definition - describes a control parameter */
typedef struct jscs_ControlDef {
    const char* name;
    jscs_SignalType type;
    uint64_t id;
    jscs_ControlSpec spec;
} jscs_ControlDef;

/* Control dispatch table - for hierarchical control lookup */
struct jscs_ControlDispatchTable;

typedef struct jscs_ControlDispatchTableEntry {
    int which;  /* 0 = table, 1 = def */
    union {
        struct jscs_ControlDispatchTable* table;
        jscs_ControlDef* def;
    };
} jscs_ControlDispatchTableEntry;

typedef struct jscs_ControlDispatchTable {
    size_t size;
    const char** names;
    jscs_ControlDispatchTableEntry* entries;
} jscs_ControlDispatchTable;

/* Function pointers for synth operations */
typedef struct jscs_SynthFuns {
    struct jscs_SynthData* (*alloc)(void);
    jscs_SErr (*free)(struct jscs_SynthData* synth);
    jscs_SErr (*init)(struct jscs_SynthData* synth);
    jscs_SErr (*uninit)(struct jscs_SynthData* synth);  /* optional */
    jscs_SErr (*reset)(struct jscs_SynthData* synth);   /* optional */
    jscs_SErr (*event)(struct jscs_SynthData* synth, uint64_t id,
                       jscs_Slice dst, jscs_Slice data);
    void (*processEvents)(struct jscs_SynthData* synth);
    void (*processAudio)(struct jscs_SynthData* synth);

    /* Note allocation (all optional) */
    jscs_SErr (*noteOn)(struct jscs_SynthData* synth, int64_t now,
                        int noteID, int n, float* params);
    jscs_SErr (*noteOff)(struct jscs_SynthData* synth, int64_t now, int noteID);
    jscs_SErr (*allNotesOff)(struct jscs_SynthData* synth, int64_t now);
    jscs_SErr (*noteSetParams)(struct jscs_SynthData* synth, int noteID,
                               int n, jscs_ParamPair* params);
    jscs_SErr (*noteSetParamRange)(struct jscs_SynthData* synth, int noteID,
                                   int first, int length, float* values);
} jscs_SynthFuns;

/* Synth definition - returned by plugin load function */
typedef struct jscs_SynthDef {
    const char* name;
    jscs_SynthFuns funs;
    int num_ins;
    int num_outs;
    int num_controls;
    jscs_PortDef* ins;
    jscs_PortDef* outs;
    jscs_ControlDef* controls;
    jscs_ControlDispatchTable controlDispatch;
} jscs_SynthDef;

/* Synth instance data - base struct (plugins extend this with custom data) */
typedef struct jscs_SynthData {
    jscs_SynthFuns funs;
    struct jscs_Engine* engine;  /* Opaque pointer to audio engine */
    struct jscs_Node* node;      /* Opaque pointer to containing node */
    int num_ins;
    int num_outs;
    int num_controls;
    void** inlets;    /* Array of input data pointers */
    void** outlets;   /* Array of output data pointers */
    void** controls;  /* Array of control data pointers */
    double fs, sd;  /* Sample rate, sample duration */
    /* Custom data follows in derived structs... */
} jscs_SynthData;

/* Plugin load function type - returns a SynthDef describing the plugin */
typedef jscs_SynthDef (*jscs_LoadSynthDefFun)(void);

#ifdef __cplusplus
}
#endif

#endif /* jscs_plugin_abi_h */
