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
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ABI version stamp.
 *
 * Generated plugins export the version of the header they were compiled
 * against as a data symbol:
 *
 *     extern "C" int64_t tzpl_abi_version = TZPL_PLUGIN_ABI_VERSION;
 *
 * Loaders read it via dlsym. A MISSING symbol is not a version -- it means
 * the plugin predates versioning, and such a plugin is REFUSED. It cannot be
 * loaded safely: plugins built before 2026-04-03 use a tzpl_SynthDef whose
 * fields after `funs` sit 8 bytes earlier (sizeof 176 rather than 184),
 * because swapBuffer was appended to tzpl_SynthFuns -- which tzpl_SynthDef
 * embeds BY VALUE -- without a version bump. Read through today's layout such
 * a plugin yields non-zero port counts paired with NULL array pointers, so
 * walking its ports dereferences null. There is no symbol that distinguishes
 * the two pre-versioning layouts, hence the blanket refusal. Rebuild any
 * plugin that is refused.
 *
 * Loaders also refuse plugins whose version is NEWER than the header they
 * were built against, since a newer plugin may assume changed layouts.
 *
 * Bump this for ANY change to the layout of these structs or to calling
 * conventions -- INCLUDING appending a field to a struct that another struct
 * embeds by value. Appending to tzpl_SynthFuns or tzpl_PortDef is NOT an
 * additive change, because it relocates every field after them in their
 * embedding struct; that is exactly the mistake this comment now documents.
 * Only genuinely additive changes may skip the bump: new optional exported
 * symbols (probe with dlsym, as loadBufferDefs does) and new enum values in
 * reserved space. */
#define TZPL_PLUGIN_ABI_VERSION 1

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
    tzpl_errClockOutOfRange,

    /* A fixed-size engine resource is full (e.g. a silo's signal-tap table).
       Append new codes at the end only -- these values cross the ABI. */
    tzpl_errResourceLimit,
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

/* Control warp types for mapping (position 0..1 -> value).
 * Ordinals are the lang-side synthdef.x ControlWarp ordinal + 1;
 * 0 (None) means the plugin predates warp emission. */
typedef enum tzpl_ControlWarp {
    tzpl_cwNone = 0,
    tzpl_cwLinear,
    tzpl_cwExponential,
    tzpl_cwStep,         /* step size in tzpl_ControlSpec.param */
    tzpl_cwSignedSquare,
    tzpl_cwCubed
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

    /* Buffer swap -- replaces buffer pointer, returns old buffer for NRT cleanup */
    struct tzpl_Buffer* (*swapBuffer)(struct tzpl_SynthData* synth, int64_t bufID,
                                      struct tzpl_Buffer* newBuffer);
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

/* Sample buffer -- externally managed, swappable at runtime.
 * Buffers may be any length: generated code wraps sample indices at the
 * actual `length` (see tzpl_buf_* in tzpl_delay_interp.hpp). The allocation
 * is still rounded up to a power of two and `mask` kept valid so plugins
 * compiled before the wrap-at-length change (which emit `index & mask`)
 * remain memory-safe against new buffers. */
typedef struct tzpl_Buffer {
    double** data;     /* one allocation per channel */
    int64_t length;    /* actual content length in frames; sample indices wrap here */
    int64_t mask;      /* nextPowerOfTwo(length) - 1: allocation bound, legacy wrap */
    int64_t chans;     /* channel count (power of 2) */
} tzpl_Buffer;

/* Buffer creation and destruction utilities */
static inline tzpl_Buffer* tzpl_createBuffer(int64_t numChannels, int64_t length) {
    tzpl_Buffer* buf = (tzpl_Buffer*)calloc(1, sizeof(tzpl_Buffer));
    if (!buf) return NULL;
    /* Allocation is rounded up to a power of two (index wrap happens at
     * `length`; the rounding keeps `mask` valid for legacy plugins and
     * guarantees at least one frame per channel). */
    int64_t allocLen = 1;
    while (allocLen < length) allocLen <<= 1;
    buf->length = length;
    buf->mask = allocLen - 1;
    /* Round chans up to power of 2 */
    int64_t allocChans = 1;
    while (allocChans < numChannels) allocChans <<= 1;
    buf->chans = allocChans;
    buf->data = (double**)calloc((size_t)allocChans, sizeof(double*));
    if (!buf->data) { free(buf); return NULL; }
    for (int64_t i = 0; i < allocChans; i++) {
        buf->data[i] = (double*)calloc((size_t)allocLen, sizeof(double));
        if (!buf->data[i]) {
            for (int64_t j = 0; j < i; j++) free(buf->data[j]);
            free(buf->data); free(buf);
            return NULL;
        }
    }
    return buf;
}

static inline void tzpl_freeBuffer(tzpl_Buffer* buf) {
    if (!buf) return;
    if (buf->data) {
        for (int64_t i = 0; i < buf->chans; i++)
            free(buf->data[i]);
        free(buf->data);
    }
    free(buf);
}

/* Plugin load function type - returns a SynthDef describing the plugin */
typedef tzpl_SynthDef (*tzpl_LoadSynthDefFun)(void);

/* Buffer definition - describes one swappable sample buffer slot.
 * Delivered via the OPTIONAL plugin symbol "loadBufferDefs" (not by growing
 * tzpl_SynthDef, whose by-value return from "load" would make the struct
 * size an ABI break): plugins compiled before this addition simply lack the
 * symbol, which means they declare no buffers. */
typedef struct tzpl_BufferDef {
    const char* name;
    tzpl_SignalType type;  /* element type + minimum channel count the graph uses */
    int64_t bufID;         /* id to pass to swapBuffer / engine loadBuffer */
} tzpl_BufferDef;

typedef struct tzpl_BufferDefList {
    int num_buffers;
    tzpl_BufferDef* buffers;
} tzpl_BufferDefList;

/* Optional plugin symbol "loadBufferDefs" */
typedef tzpl_BufferDefList (*tzpl_LoadBufferDefsFun)(void);

/* Category tags - free-form strings declared in the synthdef source (e.g.
 * "test", "fx", "percussion"). Delivered via the OPTIONAL plugin symbol
 * "loadTags"; plugins without tags (or compiled before the symbol existed)
 * simply lack it. Purely additive -- no ABI version bump. */
typedef struct tzpl_TagList {
    int num_tags;
    const char** tags;
} tzpl_TagList;

/* Optional plugin symbol "loadTags" */
typedef tzpl_TagList (*tzpl_LoadTagsFun)(void);

#ifdef __cplusplus
}
#endif

#endif /* tzpl_plugin_abi_h */
