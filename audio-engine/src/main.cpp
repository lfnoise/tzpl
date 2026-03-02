//
//  main.cpp
//  audio engine
//
//  Created by James McCartney on 2/2/21.
//

#include "jscs_sexpr.hpp"
#include "jscs_client_interface.hpp"
#include "jscs_node.hpp"
#include <thread>
#include <chrono>
#include <cassert>
#include <dlfcn.h>
#include <random>

/*

QA tests?
    starting a cross fade while one is in progress.
    connections with type and channel mismatches
    

current to do:

audio input

master gain

reactive 
    set control.
    toposorted reactive nodes
    signal triggers

buffers
    bufrd
    bufwr
    tables
    file load, save
    file streaming
    convolution

cross fade controls


built in small modules:
    gain, bias, mixer (ins,gain,mute,solo), wet/dry/xfade2, pause, switch, varispeed
    split/join, 
    tone generators, noise generators..

7th order lagrange partial sample delay

bypass.  special case of wet/dry cross fader.

silos -> workers + tracks with work stealing ??

note on/off/control by signals?

- xfader: more options: equal power, smoothstep, fade-out-fade-in \/

-note on/off by commands

xx-per silo mute, solo, gain.

-move node hash table to RT side

-crossfade connections

-NRT processCommandsThread

-dead nodes queue

-safetyLimiter

-bundles in name only..

-processScheduledEvents


-silos


---

xxnodes process a small buffer at a time. maybe 64 samples.

-ports can have any type: i32 i64 f32 f64 x32 x64

no pulling between nodes. topological sort.
xxgraph can only be altered at buffer boundaries.
    add/remove nodes.
    add/remove connections.



 */


using namespace engine;


// TODO: for c++20 change to std::bit_ceil which is constexpr
constexpr int NextPowerOfTwo_(int k, int n) {
    if (n >= k) return n;
    if (n == (1 << 30)) return n; // that's as bit as we're gonna go.
    return NextPowerOfTwo_(k, n*2);
}

constexpr int NextPowerOfTwo(int k) {
    return NextPowerOfTwo_(k, 1);
}

enum class ShouldCache { no, yes };
struct VoicesInRows {};
struct VoicesInColumns {};

template <int MaxVoices, int NumParams, class RowsOrCols>
class Voicer {
    int noteID_[MaxVoices];
    i64 noteOnTime_[MaxVoices];
    i64 noteOffTime_[MaxVoices];
    static const int kCacheSize = NextPowerOfTwo(4*MaxVoices);
    static const int kCacheMask = kCacheSize - 1;
    u16 activeVoiceCache_[kCacheSize];
    int activeVoices_ = 0;
    int numNoteOns_ = 0;
    int numNoteOffs_ = 0;
    
    static constexpr bool voicesAreInColumns 
        = std::is_same_v<RowsOrCols, VoicesInColumns>;

    using VoiceParamMatrix = std::conditional_t<voicesAreInColumns,
                                    f32[1+NumParams][MaxVoices],
                                    f32[MaxVoices][1+NumParams]>;

    VoiceParamMatrix* params_;

    int allocVoice() {
        i64 minNoteOffTime = std::numeric_limits<i64>::max();
        i64 minNoteOnTime = std::numeric_limits<i64>::max();
        int minNoteOffIndex = -1;
        int minNoteOnIndex = -1;
        for (int i = 0; i < MaxVoices; ++i) {
            if (get(i,0) > 0.) { // gate is on.
                if (noteOnTime_[i] < minNoteOnTime) {
                    minNoteOnTime = noteOnTime_[i];
                    minNoteOnIndex = i;
                }
            } else { // gate is off.
                if (noteOffTime_[i] < minNoteOffTime) {
                    minNoteOffTime = noteOffTime_[i];
                    minNoteOffIndex = i;
                }
            }
        }
        int index = minNoteOffIndex >= 0 ? minNoteOffIndex : minNoteOnIndex;
        assert(index >= 0);
        return index;
    }
public:
    Voicer() {
        memset(noteID_, 0, sizeof(noteID_));
        memset(noteOnTime_, 0, sizeof(noteOnTime_));
        memset(noteOffTime_, 0, sizeof(noteOffTime_));
        memset(activeVoiceCache_, 0, sizeof(activeVoiceCache_));
        //memset(params_, 0, sizeof(VoiceParamMatrix));
    }
    
    int maxVoices() const { return MaxVoices; }
    int activeVoices() const { return activeVoices_; }
    int numNoteOns() const { return numNoteOns_; }
    int numNoteOffs() const { return numNoteOffs_; }
    int numParams() const { return NumParams; }
    f32* out() { return params_; }
    
    void setParams(f32* p) {
        //printf("setParams %p\n", p);
        params_ = (VoiceParamMatrix*)p;
    }

    int findVoice(int noteID, ShouldCache shouldCache = ShouldCache::yes) {
        // check cache first.
        int cacheIndex = u32(noteID) & kCacheMask;
        int voiceIndex = activeVoiceCache_[cacheIndex];
        if (noteID == noteID_[voiceIndex]) return voiceIndex;

        if (noteID < 0) return -1; // invalid noteID

        printf("note cache miss %d\n", noteID);

        // linear search if wasn't in cache.
        for (voiceIndex = 0; voiceIndex < MaxVoices; ++voiceIndex) {
            if (noteID_[voiceIndex] == noteID) {
                if (shouldCache == ShouldCache::yes) activeVoiceCache_[cacheIndex] = voiceIndex;
                return voiceIndex;
            }
        }
        return -1;
    }


    f32* getRow(int row) {
        // a row of a single parameter for all voices if VoicesInColumns
        // a row of all params for one voice if not VoicesInColumns
        return &(*params_)[row][0];
    }
    
    f32 get(int voice, int param) const {
        if constexpr (voicesAreInColumns) {
            return (*params_)[param][voice];
        } else {
            return (*params_)[voice][param];
        }
    }
    void set(int voice, int param, f32 value) {
        if constexpr (voicesAreInColumns) {
            (*params_)[param][voice] = value;
        } else {
            (*params_)[voice][param] = value;
        }
    }

    jscs_SErr noteOn(i64 now, int noteID, int n, f32* params, int& voiceIndex) {
        //printf(">Voicer noteOn %d %qd\n", noteID, now);
        if (noteID < 0) return jscs_errNoteNotFound;
        voiceIndex = allocVoice();
        noteOnTime_[voiceIndex] = now;
        noteID_[voiceIndex] = noteID;
        int cacheIndex = u32(noteID) & kCacheMask;
        activeVoiceCache_[cacheIndex] = voiceIndex;
        ++activeVoices_;
        ++numNoteOns_;
        set(voiceIndex, 0, 1.f); // set gate on.
        setNoteParamRange(voiceIndex, 0, n, params);
        return jscs_errNone;
    }
    
    jscs_SErr noteOff(i64 now, int noteID) {
        int voiceIndex = findVoice(noteID, ShouldCache::no);
        if (voiceIndex < 0) return jscs_errNoteNotFound;
        --activeVoices_;
        ++numNoteOffs_;
        noteOffTime_[voiceIndex] = now;
        set(voiceIndex, 0, 0.f); // set gate off.
        return jscs_errNone;
    }
    
    void allOff(i64 now) {
        for (int voiceIndex = 0; voiceIndex < MaxVoices; ++voiceIndex) {
            if (get(voiceIndex, 0) > 0.f) {
                noteOffTime_[voiceIndex] = now;
                set(voiceIndex, 0, 0.f); // set gate off.
            }
        }
        activeVoices_ = 0;
    }

    
    void setNoteParamRange(int voiceIndex, int first, int length, f32* params) {
        length = std::min(length, NumParams-first);
        for (int i = 0; i < length; ++i) {
            set(voiceIndex, i+first+1, params[i]);
        }
    }
        
    void setNoteParams(int voiceIndex, int n, jscs_ParamPair* params) {
        for (int i = 0; i < n; ++i) {
            jscs_ParamPair& p = params[i];
            if (p.index < 0 || p.index >= NumParams) continue;
            set(voiceIndex, p.index+1, p.value);
        }
    }
};


// ColumnVoicer is better for SIMD code where all voices are evaluated in parallel.
// RowVoicer is better for scalar code.

// In ColumnVoicer, the parameters of each voice are in a column of the parameter matrix.
// In RowVoicer, the parameters of each voice are in a row of the parameter matrix.

template <int MaxVoices, int NumParams>
using ColumnVoicer = Voicer<MaxVoices, NumParams, VoicesInColumns>;

template <int MaxVoices, int NumParams>
using RowVoicer    = Voicer<MaxVoices, NumParams, VoicesInRows>;

f32 bwarp(f32 x, f32 w) {  // bipolar to bipolar warping
    f32 u = .5f * x - .5f;
    return (w + u) / (w * x - u);
}


inline f64 nnhz(f64 nn) { return 440. * exp2((nn - 69.)*(1./12.)); }

inline f64 calcDecay(f64 amount, f64 decaytime, f64 sampleRate)
{
    if (decaytime == 0.) {
        return 0.;
    } else if (decaytime > 0.) {
        //printf("calcDecay %g %g %g -> %g\n", amount, decaytime, sampleRate, pow(amount, decaytime * sampleRate));
        return pow(amount, 1./(decaytime * sampleRate));
    } else if (decaytime < 0.) {
        return -pow(amount, 1./(-decaytime * sampleRate));
    } else {
        return 0.;
    }
}

struct VoicerTest : jscs_SynthData
{
    //params: pitch, veloc, drive, pan, atkTime, relTime.
    static const int kNumVoices = 32;
    static const int kNumParams = 6;
    RowVoicer<kNumVoices, kNumParams> voicer;
    struct Voice {
        f64 ampL, ampR, ampLgoal, ampRgoal, level, atkCoeff, relCoeff;
        f64 freq, phase, drive;
    } voices[kNumVoices] = {0};
    f64 freqmul;
    f64 amplag;
    f32 params[kNumVoices][kNumParams];
};

jscs_SynthData* VoicerTest_alloc() {
    return (jscs_SynthData*)new VoicerTest();
}

jscs_SErr VoicerTest_free(VoicerTest* o) {
    delete o;
    return jscs_errNone;
}

jscs_SErr VoicerTest_init(VoicerTest* o) {
    printf("VoicerTest_init %p\n", o);
    o->voicer.setParams((f32*)o->params);
    o->voicer.setParams((f32*)&o->params);
    o->freqmul = 2. * M_PI / o->fs;
    o->amplag = calcDecay(.01, .01, o->fs);
    return jscs_errNone;
}

jscs_SErr VoicerTest_uninit(VoicerTest* o) {
    return jscs_errNone;
}

void VoicerTest_processAudio(VoicerTest* o) {
    f32* out = getOut<f32>(o, 0);
    out[0] = 0.f;
    out[1] = 0.f;
    for (int i = 0; i < VoicerTest::kNumVoices; ++i) {
        //params: gate, pitch, veloc, drive, pan, atk, rel.
        f32* row = o->voicer.getRow(i);
        VoicerTest::Voice& v = o->voices[i];
        
        f32 gate = row[0];
        v.level += (gate > v.level ? v.atkCoeff : v.relCoeff) * (gate - v.level); // amp envelope
        v.ampL += o->amplag * (v.ampLgoal - v.ampL);  // move pan levels
        v.ampR += o->amplag * (v.ampRgoal - v.ampR);
        f32 wave = v.level * sin(v.drive * sin(v.phase)); // sine waveshaping of sine times level.
        out[0] += v.ampL * wave; // do panning.
        out[1] += v.ampR * wave;
        v.phase += v.freq; // advance phase.
        if (gate < 1.) v.drive *= .99997; // if released, then decay the drive.
    }
}

jscs_SErr VoicerTest_updateParameters(VoicerTest* o, int voiceIndex) {
    VoicerTest::Voice& v = o->voices[voiceIndex];
    f32* row = o->voicer.getRow(voiceIndex);
    
    f32 amp = std::min(1.f, row[2]*row[2]);
    f32 pan = .5*row[4]+.5;
    if (v.level < .0001) {
        v.ampL = v.ampLgoal = amp * sqrt(1.f - pan);
        v.ampR = v.ampRgoal = amp * sqrt(pan);
    } else {
        v.ampLgoal = amp * sqrt(1.f - pan);
        v.ampRgoal = amp * sqrt(pan);
    }
    v.freq = o->freqmul * nnhz(row[1]);
    v.atkCoeff = 1. - calcDecay(.01, row[5], o->fs);
    v.relCoeff = 1. - calcDecay(.001, row[6], o->fs);
    v.drive = row[3];
    return jscs_errNone;
}

inline f64 ampdb(f64 amp) { return 20. * log10(amp); }

jscs_SErr VoicerTest_noteOn(VoicerTest* o, int64_t now, int noteID, int n, f32* params) {
    int voiceIndex;
    jscs_SErr err = o->voicer.noteOn(now, noteID, n, params, voiceIndex);
    if (err != jscs_errNone) return err;
    //printf(">VoicerTest_noteOn %d\n", voiceIndex);
    printf("noteOn  %4d %3d act %3d  %8.2f\n",
        noteID, voiceIndex,
        o->voicer.activeVoices(), ampdb(o->voices[voiceIndex].level));


    f32* row = o->voicer.getRow(voiceIndex);
    f32 defaultParams[VoicerTest::kNumParams] = {
        //params: gate, pitch, veloc, drive, pan, atk, rel.
        60., .5, 1., 0., .1, 2.
    };
    if (n < VoicerTest::kNumParams) {
        memcpy(row+n+1, defaultParams+n, (VoicerTest::kNumParams-n)*sizeof(f32));
    }
    
//    printf("defaults %f %f %f %f %f %f\n",
//        defaultParams[0], defaultParams[1], defaultParams[2], defaultParams[3], defaultParams[4], defaultParams[5]);
//    printf("<row %f %f %f %f %f %f %f\n",
//        row[0], row[1], row[2], row[3], row[4], row[5], row[6]);
    
    return VoicerTest_updateParameters(o, voiceIndex);
    // don't reset the level or phase.
    // if the voice is being stolen then resetting the level or phase
    // would cause a discontinuity.
    
//    printf("gate %f\n", row[0]);
//    printf("hz %f\n", nnhz(row[1]));
//    printf("v.freq %f\n", v.freq);
//    printf("amp %f\n", amp);
//    printf("pan %f\n", pan);
//    printf("v.ampLgoal %f\n", v.ampLgoal);
//    printf("v.ampRgoal %f\n", v.ampRgoal);
//    printf("v.ampL %f\n", v.ampL);
//    printf("v.ampR %f\n", v.ampR);
//    printf("v.atkCoeff %g\n", v.atkCoeff);
//    printf("v.relCoeff %g\n", v.relCoeff);
}

jscs_SErr VoicerTest_noteOff(VoicerTest* o, i64 now, int noteID) {
    int voiceIndex = o->voicer.findVoice(noteID, ShouldCache::no);
    o->voicer.noteOff(now, noteID);
    printf("noteOff %4d %3d act %3d\n",
        noteID, voiceIndex, o->voicer.activeVoices());
    return jscs_errNone;
}

jscs_SErr VoicerTest_allNotesOff(VoicerTest* o, i64 now) {
    o->voicer.allOff(now);
    return jscs_errNone;
}

jscs_SErr VoicerTest_noteSetParams(VoicerTest* o, int noteID, int n, jscs_ParamPair* params) {
    int voiceIndex = o->voicer.findVoice(noteID);
    if (voiceIndex < 0) return jscs_errNoteNotFound;
    o->voicer.setNoteParams(noteID, n, params);
    return VoicerTest_updateParameters(o, voiceIndex);
}

jscs_SErr VoicerTest_noteSetParamRange(VoicerTest* o, int noteID, int first, int length, f32* values) {
    int voiceIndex = o->voicer.findVoice(noteID);
    if (voiceIndex < 0) return jscs_errNoteNotFound;
    o->voicer.setNoteParamRange(voiceIndex, first, length, values);
    return VoicerTest_updateParameters(o, voiceIndex);
}

jscs_SynthFuns VoicerTest_funs = {
    .alloc = VoicerTest_alloc,
    .free = (jscs_SErr(*)(jscs_SynthData*))VoicerTest_free,
    .init = (jscs_SErr(*)(jscs_SynthData*))VoicerTest_init,
    .uninit = (jscs_SErr(*)(jscs_SynthData*))VoicerTest_uninit,
    .reset = nullptr,
    .event = nullptr,
    .processAudio = (void(*)(jscs_SynthData*))VoicerTest_processAudio,
        
    .allNotesOff = (jscs_SErr(*)(jscs_SynthData*,int64_t))VoicerTest_allNotesOff,
    .noteOn = (jscs_SErr(*)(jscs_SynthData*,int64_t,int,int,f32*))VoicerTest_noteOn,
    .noteOff = (jscs_SErr(*)(jscs_SynthData*,int64_t,int))VoicerTest_noteOff,
    .noteSetParams = (jscs_SErr(*)(jscs_SynthData*,int,int,jscs_ParamPair*))VoicerTest_noteSetParams,
    .noteSetParamRange = (jscs_SErr(*)(jscs_SynthData*,int,int,int,f32*))VoicerTest_noteSetParamRange,
};

void createVoicerTestNode(engine::Engine* e)
{
    NodeDefInfo info;
    memset(&info, 0, sizeof(info));
    
    info.name = "voicer";
    info.num_outs = 1;
    PortInfo out{"out", {jscs_kF32, jscs_audioRate, 2}};
    info.outs = (PortInfo*)calloc(info.num_outs, sizeof(PortInfo));
    info.outs[0] = out;
    info.funs = VoicerTest_funs;
    
    addNodeDef(e, info);
};




struct AddOp : jscs_SynthData
{};

jscs_SynthData* AddOp_alloc() {
    return (jscs_SynthData*)new AddOp();
}

jscs_SErr AddOp_free(jscs_SynthData* synth) {
    delete (AddOp*)synth;
    return jscs_errNone;
}


jscs_SErr AddOp_init(jscs_SynthData* synth)
{
    return jscs_errNone;
}

jscs_SErr AddOp_uninit(jscs_SynthData* synth)
{
    return jscs_errNone;
}

void AddOp_processAudio(jscs_SynthData* synth)
{
    f32* a = (f32*)getInput(synth, 0);
    f32* b = (f32*)getInput(synth, 1);
    f32* out = getOut<f32>(synth, 0);
    out[0] = a[0] + b[0];
    out[1] = a[1] + b[1];
}

jscs_SynthFuns AddOp_funs = {
    .alloc = AddOp_alloc,
    .free = AddOp_free,
    .init = AddOp_init,
    .uninit = AddOp_uninit,
    .reset = nullptr,
    .event = nullptr,
    .processAudio = AddOp_processAudio,
};


void createAddOpNode(engine::Engine* e)
{
    NodeDefInfo info;
    memset(&info, 0, sizeof(info));
    
    info.name = "+";
    info.num_ins = 2;
    info.num_outs = 1;
    
    PortInfo a{"a", {jscs_kF32, jscs_audioRate, 2}};
    PortInfo b{"b", {jscs_kF32, jscs_audioRate, 2}};
    info.ins = (PortInfo*)calloc(info.num_ins, sizeof(PortInfo));
    info.ins[0] = a;
    info.ins[1] = b;
    
    PortInfo out{"out", {jscs_kF32, jscs_audioRate, 2}};
    info.outs = (PortInfo*)calloc(info.num_outs, sizeof(PortInfo));
    info.outs[0] = out;
    info.funs = AddOp_funs;
    
    addNodeDef(e, info);
};




struct MulOp : jscs_SynthData
{};


jscs_SynthData* MulOp_alloc() {
    return (jscs_SynthData*)new MulOp();
}

jscs_SErr MulOp_free(jscs_SynthData* synth) {
    delete (MulOp*)synth;
    return jscs_errNone;
}

jscs_SErr MulOp_init(jscs_SynthData* synth)
{
    return jscs_errNone;
}

jscs_SErr MulOp_uninit(jscs_SynthData* synth)
{
    return jscs_errNone;
}

void MulOp_processAudio(jscs_SynthData* synth)
{
    f32* a = (f32*)getInput(synth, 0);
    f32* b = (f32*)getInput(synth, 1);
    f32* out = getOut<f32>(synth, 0);
    out[0] = a[0] * b[0];
    out[1] = a[1] * b[1];
}

jscs_SynthFuns MulOp_funs = {
    .alloc = MulOp_alloc,
    .free = MulOp_free,
    .init = MulOp_init,
    .uninit = MulOp_uninit,
    .reset = nullptr,
    .event = nullptr,
    .processAudio = MulOp_processAudio,
};

void createMulOpNode(engine::Engine* e)
{
    NodeDefInfo info;
    memset(&info, 0, sizeof(info));
    
    info.name = "*";

    info.num_ins = 2;
    info.num_outs = 1;
    
    PortInfo a{"a", {jscs_kF32, jscs_audioRate, 2}};
    PortInfo b{"b", {jscs_kF32, jscs_audioRate, 2}};
    info.ins = (PortInfo*)calloc(info.num_ins, sizeof(PortInfo));
    info.ins[0] = a;
    info.ins[1] = b;
    
    PortInfo out{"out", {jscs_kF32, jscs_audioRate, 2}};
    info.outs = (PortInfo*)calloc(info.num_outs, sizeof(PortInfo));
    info.outs[0] = out;
    
    info.funs = MulOp_funs;
    
    addNodeDef(e, info);
};



struct SinOsc : jscs_SynthData
{
    f64 freqmul;
    f64 phase;
};

jscs_SynthData* SinOsc_alloc() {
    return (jscs_SynthData*)new SinOsc();
}

jscs_SErr SinOsc_free(jscs_SynthData* synth) {
    delete (SinOsc*)synth;
    return jscs_errNone;
}

jscs_SErr SinOsc_init(jscs_SynthData* synth)
{
    SinOsc* o = (SinOsc*)synth;
    
    getIn<f32>(synth,0)[0] = 261.625565; // freq
    getIn<f32>(synth,1)[0] = 0.25f; // amp
    o->freqmul = 2. * M_PI / synth->fs;
    o->phase = 0.;
    
    return jscs_errNone;
}

jscs_SErr SinOsc_uninit(jscs_SynthData* synth)
{
    return jscs_errNone;
}

void SinOsc_processAudio(jscs_SynthData* synth)
{
    SinOsc* o = (SinOsc*)synth;
    f32* freq = (f32*)getInput(o, 0);
    f32* amp  = (f32*)getInput(o, 1);
    f32* out = getOut<f32>(synth, 0);
    out[0] = *amp * sin(o->phase);
    out[1] = out[0];
    o->phase += o->freqmul * *freq;
}


jscs_SynthFuns SinOsc_funs = {
    .alloc = SinOsc_alloc,
    .free = SinOsc_free,
    .init = SinOsc_init,
    .uninit = SinOsc_uninit,
    .reset = nullptr,
    .event = nullptr,
    .processAudio = SinOsc_processAudio,
};

void createSineNode(engine::Engine* e)
{
    NodeDefInfo info;
    memset(&info, 0, sizeof(info));
    
    info.name = "sinosc";
        
    info.num_ins = 2;
    info.num_outs = 1;
    
    PortInfo freq{"freq", {jscs_kF32, jscs_audioRate, 1}};
    PortInfo amp{"amp", {jscs_kF32, jscs_audioRate, 1}};
    info.ins = (PortInfo*)calloc(info.num_ins, sizeof(PortInfo));
    info.ins[0] = freq;
    info.ins[1] = amp;
    
    PortInfo out{"out", {jscs_kF32, jscs_audioRate, 2}};
    info.outs = (PortInfo*)calloc(info.num_outs, sizeof(PortInfo));
    info.outs[0] = out;

    info.funs = SinOsc_funs;   
    
    addNodeDef(e, info);
};

void sleepf(double t) {
    std::this_thread::sleep_for(std::chrono::duration<double>(t));
}

void sleepSec(int t) {
    std::this_thread::sleep_for(std::chrono::seconds(t));
}

void loadDef_test()
{
    EngineConfig config;
    config.numSilos = 1;
    
    AudioStreamParameters asp{ "default", 2, 0, 256, 48000.};
    auto e = newEngine(config, asp);

    loadDef(e, "/Users/jamesmcc/sapf-build-5", "bubbles");
}

void test0() 
{
    printf("--- test0 ---------------------------------\n");
    using namespace engine;

    EngineConfig config;
    config.numSilos = 1;
    
    AudioStreamParameters asp{ "default", 2, 0, 256, 48000.};
    auto e = newEngine(config, asp);

    createSineNode(e);
    
    printf("start\n");
    startAudio(e);
    printf("running %d\n", isAudioRunning(e));

    sleepSec(1);

    printf("create graph with only a sine oscillator\n");
    f32 freq;
    f32 amp;

    begin(e, 0);
    newNode("sinosc", 101);
    freq = 240;
    setInput({101, 0}, 1, &freq);
    amp = 0.15;
    setInput({101, 1}, 1, &amp, .2);
    connect({101, 0}, {0, 0});
    go();

    sleepSec(8);

 
    printf("stop\n");
    stopAudio(e);

    printf("free engine\n");
    freeEngine(e);
}

void test1()
{
    printf("--- test1 ---------------------------------\n");
    using namespace engine;

    EngineConfig config;
    config.numSilos = 2;
    int siloA = 0;
    int siloB = 1;

    AudioStreamParameters asp{ "default", 2, 0, 256, 96000.};
    
    auto e = newEngine(config, asp);
    printDevices(e);
    
    createAddOpNode(e);
    createMulOpNode(e);
    createSineNode(e);
    
    startAudio(e);
    printf("running %d\n", isAudioRunning(e));

    sleepSec(1);

    printf("create graph A\n");
    f32 freq;
    f32 amp;

    begin(e, siloA);
    newNode("sinosc", 101);
    newNode("sinosc", 102);
    newNode("+", 103);
    newNode("*", 203);
    freq = 240;
    setInput({101, 0}, 1, &freq);
    freq = 360.5;
    setInput({102, 0}, 1, &freq);
    connect({101, 0}, {103, 0});
    connect({102, 0}, {103, 1});
    connect({103, 0}, {0, 0});
    go();

//    sleepSec(2);
//    
//    printf("replace 103 -> 203 no fade\n");
//    begin(e, siloA);
//    replaceNode(103, 203);
//    go();
//    
//    sleepSec(2);
//    
//    printf("replace 203 -> 103 no fade\n");
//    begin(e, siloA);
//    replaceNode(203, 103);
//    go();
 
    
    sleepSec(2);
    
    for (int h = 0; h < 3; ++h) {
        printf("replace 103 -> 203 w fade\n");
        begin(e, siloA);
        replaceNode(103, 203, 1.25);
        go();
        
        sleepSec(2);
        
        printf("replace 203 -> 103 w fade\n");
        begin(e, siloA);
        replaceNode(203, 103, 1.25);
        go();
     
        sleepSec(2);
    }

    begin(e, siloB);
    printf("create graph B\n");
    newNode("sinosc", 101);
    newNode("sinosc", 102);
    newNode("+", 103);
    freq = 480;
    setInput({101, 0}, 1, &freq);
    freq = 840;
    setInput({102, 0}, 1, &freq);
    amp = .125;
    setInput({101, 1}, 1, &amp);
    setInput({102, 1}, 1, &amp);
    connect({101, 0}, {103, 0});
    connect({102, 0}, {103, 1});
    connect({103, 0}, {0, 0});
    go();
    
    sleepSec(1);
    
    printf("attempt to change a connection.\n");
    begin(e, siloB);
    newNode("sinosc", 104);
    freq = 400;
    setInput({104, 0}, 1, &freq);
    connect({104, 0}, {103, 0}, 0.1);
    go();
    
    sleepSec(1);
    printf("ok, set it back.\n");
    begin(e, siloB);
    connect({101, 0}, {103, 0}, 0.1);
    go();
    sleepf(.4);
    begin(e, siloB);
    disconnectNode(104);
    go();
    sleepSec(2);
    
    printf("slide freq to 600 in 8 seconds\n");
    freq = 600;
    begin(e, siloA);
    setInput({101, 0}, 1, &freq, 8);
    go();
    
    sleepSec(4);   
    printf("attempt to start a new slide while the first is running\n");
    freq = 400;
    begin(e, siloA);
    setInput({101, 0}, 1, &freq, 8);
    go();
    sleepSec(4);   
    printf("first slide should be done about now\n");
    sleepSec(4);   
    printf("second slide should be done about now\n");
    
    sleepSec(3);
    
    printf("disconnect a sinosc\n");
    begin(e, siloA);
    disconnectInput({103, 1}, .5);
    go();
    
    sleepSec(3);

    printf("reconnect a sinosc\n");
    begin(e, siloA);
    connect({102, 0}, {103, 1}, .5);
    go();

    sleepSec(4);
    
    
    printf("tremolo\n");
    for (int i = 0; i < 16; ++i) {
        amp = (i&1) ? .2 : .02;
        begin(e, siloA);
        setInput({101, 1}, 1, &amp, .1, fadeSmoothstep);
        setInput({102, 1}, 1, &amp, .1, fadeSmoothstep);
        go();
    
        sleepf(.2);
    }
    sleepSec(2);
    
    printf("very loud (engage safety limiter).\n");
    amp = 20.;
    begin(e, siloA);
    setInput({101, 1}, 1, &amp, .04);
    go();
    
    sleepSec(1);
    
    printf("set amp .2\n");
    amp = .2;
    begin(e, siloA);
    setInput({101, 1}, 1, &amp, .04);
    go();
    
    sleepSec(1);

    printf("random amplitudes 0.1 .. 20.0\n");
    for (int i = 0; i < 8; ++i) {
//            if (i == 50) {
//                printf("postGain .5\n");
//                safetyLimiter(e, true, .5f);
//            }
        { static std::mt19937 rng{std::random_device{}()};
          amp = std::uniform_int_distribution<int>(0, 199)(rng) * .1f; }
        begin(e, siloA);
        setInput({101, 1}, 1, &amp, .04);
        go();
        
        sleepf(.1);
    }
    
    printf("set amp .2\n");
    amp = .2;
    begin(e, siloA);
    setInput({101, 1}, 1, &amp, .04);
    go();
    
    sleepSec(2);
    
//        printf("postGain 1.\n");
//        safetyLimiter(e, true, 1.f);
//
//        sleepSec(2);
//        for (int i = 0; i < 8; ++i) {
//            printf("off\n");
//            safetyLimiter(e, false, 1.f);
//            sleepSec(1);
//            printf("on\n");
//            safetyLimiter(e, true, 1.f);
//            sleepSec(1);
//        }

    printf("set graph B amps silent\n");
    amp = 0.;
    begin(e, siloB);
    setInput({101, 1}, 1, &amp, 3., fadeEaseOutCubic);
    setInput({102, 1}, 1, &amp, 3., fadeEaseOutCubic);
    go();
    
    sleepSec(4);

    printf("set graph A amps silent\n");
    amp = 0.;
    begin(e, siloA);
    setInput({101, 1}, 1, &amp, 3., fadeEaseOutCubic);
    setInput({102, 1}, 1, &amp, 3., fadeEaseOutCubic);
    go();
    
    sleepSec(4);

    printf("free graph B\n");
    begin(e, siloB);
    freeAllNodes();
    go();
    
    sleepSec(1);

    printf("free graph A\n");
    begin(e, siloA);
    freeAllNodes();
      go();

    sleepSec(1);

    printf("stop\n");
    stopAudio(e);

    printf("free engine\n");
    freeEngine(e);
}

void test5() 
{
    printf("--- test5 ---------------------------------\n");
    using namespace engine;

    EngineConfig config;
    config.numSilos = 1;
    
    AudioStreamParameters asp{ "default", 2, 0, 256, 48000.};
    auto e = newEngine(config, asp);

    createAddOpNode(e);
    createSineNode(e);
    
    printf("start\n");
    startAudio(e);
    printf("running %d\n", isAudioRunning(e));

    sleepSec(1);

    printf("create graph A\n");
    f32 freq;
    f32 amp;

    begin(e, 0);
    newNode("sinosc", 101);
    newNode("+", 102);
    freq = 240;
    setInput({101, 0}, 1, &freq);
    amp = 0.15;
    setInput({101, 1}, 1, &amp, .2);
    connect({101, 0}, {102, 0});
    connect({102, 0}, {0, 0});
    go();

    sleepSec(1);
 
    printf("test fan out\n");
    for (int i = 0; i < 4; ++i) {
        begin(e, 0);
        connect({101, 0}, {102, 1}, 0.3); // test fan out.
        go();

        sleepf(.4);

        begin(e, 0);
        disconnectInput({102, 1}, 0.3); // test fan out.
        go();

        sleepf(.4);
    }
    
     sleepSec(2);

//    printf("mute\n");
//    amp = 0.;
//    begin(e, 0);
//    setInput({0,0}, 1, &amp, 0.2);
//    go();
//    sleepf(.5);
//
//    printf("free graph\n");
//    begin(e, 0);
//    freeNode(101);
//    freeNode(102);
//    go();
   
//    sleepSec(1);
 
    printf("stop\n");
    stopAudio(e);

    printf("free engine\n");
    freeEngine(e);
}

void test2()
{
    printf("--- test2 ---------------------------------\n");
    using namespace engine;

    EngineConfig config;
    config.numSilos = 8;
    
    AudioStreamParameters asp{ "default", 2, 0, 256, 96000.};
    auto e = newEngine(config, asp);

    createAddOpNode(e);
    createSineNode(e);
    
    printf("start\n");
    startAudio(e);
    printf("running %d\n", isAudioRunning(e));

    sleepSec(1);

    f32 freq;
    f32 amp;

    f32 latency = .02;
    f32 dt = .2;
    printf("start graphs on %d threads\n", config.numSilos);
    f64 t0 = getStreamTime(e);
    for (int i = 0; i < config.numSilos; ++i) {
        begin(e, i);
        newNode("sinosc", 101);
        freq = 240 + 60 * i;
        setInput({101, 0}, 1, &freq);
        amp = 0.0;
        setInput({101, 1}, 1, &amp);
        amp = 0.05;
        setInput({101, 1}, 1, &amp, .2, fadeEaseInCubic);
        connect({101, 0}, {0, 0});
        f64 t = t0 + latency + i * dt;
        sched(t);
    }
    
    sleepSec(5);
    
    printf("change freqs\n");
    t0 = getStreamTime(e);
    dt = .5;
    for (int i = 0; i < config.numSilos; ++i) {
        freq = 360 + 180 * i;
        begin(e, i);
        setInput({101, 0}, 1, &freq, .5, fadeExponential);
        f64 t = t0 + latency + i * dt;
        sched(t);
    }

    sleepSec(8);

    printf("reset freqs\n");
    t0 = getStreamTime(e);
    for (int i = 0; i < config.numSilos; ++i) {
        freq = 240 + 60 * i;
        begin(e, i);
        setInput({101, 0}, 1, &freq, .5, fadeExponential);
        f64 t = t0 + latency + i * dt;
        sched(t);
//            go(e, i);
//            sleepf(.2);
    }

    sleepSec(8);
    
    printf("stop\n");
    stopAudio(e);
    sleepSec(1);
    printf("start\n");
    startAudio(e);

    
    sleepSec(4);

    printf("set silent\n");
    amp = 0.;
    t0 = getStreamTime(e);
    dt = .4;
    for (int i = 0; i < config.numSilos; ++i) {
        begin(e, i);
        setInput({101, 1}, 1, &amp, .5, fadeEaseOutCubic);
        f64 t = t0 + latency + i * dt;
        sched(t);
    }

    sleepSec(6);

    printf("stop\n");
    stopAudio(e);

    printf("free engine\n");
    freeEngine(e);
}

void test3()
{
    printf("--- test3 ---------------------------------\n");
    using namespace engine;

    EngineConfig config;
    config.numSilos = 1;
    
    AudioStreamParameters asp{ "default", 2, 0, 256, 96000.};
    auto e = newEngine(config, asp);

    createVoicerTestNode(e);
    
    printf("start\n");
    startAudio(e);
    printf("running %d\n", isAudioRunning(e));

    sleepSec(1);

    printf("create graph A\n");
    
    begin(e, 0);
        newNode("voicer", 101);
        connect({101, 0}, {0,0});
    go();
    
    sleepSec(1);
    
    const int numPitches = 6;
    f32 pitches[numPitches] = {60, 65, 67, 70, 74, 77};
    
    int noteID = 0;
//    {
//        begin(e, 0);
//        f32 params[3] = {36., .6, 15.};
//        noteOn(101, noteID, 3, params);
//        go();
//        ++noteID;
//    }
    
    
    sleepSec(1);
    
    for (int k = 0; k < 20; ++k) {
        f64 dt = .1;
        f64 latency = .02;
        f64 t0 = getStreamTime(e);
        {
            f32 root = pitches[0] - 1 * k + -2;
            f32 veloc = .7;
            f32 drive = 4.7;
            f32 params[6] = { root, veloc, drive, -.3, .01, .2 };
            begin(e, 0);
            f64 t = t0 + latency;
            noteOn(101, noteID, 6, params);
            params[0] += 7;
            params[3] = .3;
            noteOn(101, noteID+1, 6, params);
            sched(t);
            
            begin(e, 0);
            noteOff(101, noteID);
            noteOff(101, noteID+1);
            t += numPitches * dt;
            sched(t);
            
            noteID+=2;
        }
        for (int i = 0; i < numPitches; ++i) {
            begin(e, 0);
                //params: pitch, veloc, drive, pan, atk, rel.
                //f32 params[6];
            f32 pitch = pitches[i] - 1 * k + 10;
            f32 veloc = .5 + .04 * (numPitches - i - 1);
            f32 drive = 1. + .3 * k;
            f32 pan = -0.8 + (1.6 / (numPitches-1)) * i;
            f32 params[6] = { pitch, veloc, drive, pan, .01, .2 };
            noteOn(101, noteID, 6, params);
            
            f64 t = t0 + latency + i * dt;
            sched(t);
            
            t += .1 + .04 * k;
            begin(e, 0);
            noteOff(101, noteID);
            sched(t);
            ++noteID;
        }
        sleepf(.6);

        t0 = getStreamTime(e);
        {
            f32 root = pitches[0] - 1 * k + -4;
            f32 veloc = .7;
            f32 drive = 4.7;
                //params: pitch, veloc, drive, pan, atk, rel.
            f32 params[6] = { root, veloc, drive, -.3, .01, .2 };
            begin(e, 0);
            f64 t = t0 + latency;
            noteOn(101, noteID, 6, params);
            params[0] += 7;
            params[3] = .3;
            noteOn(101, noteID+1, 6, params);
            sched(t);
            
            begin(e, 0);
            noteOff(101, noteID);
            noteOff(101, noteID+1);
            t += numPitches * dt;
            sched(t);
            
            noteID+=2;
        }
        for (int i = 0; i < numPitches; ++i) {
            begin(e, 0);
                //params: pitch, veloc, drive, pan, atk, rel.
                //f32 params[6];
            f32 pitch = pitches[numPitches-i-1] - 1 * k + 8;
            f32 veloc = .5 + .04 * (numPitches - i - 1);
            f32 drive = 1.15 + .3 * k;
            f32 pan = -0.8 + (1.6 / (numPitches-1)) * i;
            f32 params[6] = { pitch, veloc, drive, pan, .01, .2 };
            noteOn(101, noteID, 6, params);

            f64 t = t0 + latency + i * dt;
            sched(t);
            
            t += .1 + .04 * k;
            begin(e, 0);
            noteOff(101, noteID);
            sched(t);
            ++noteID;
        }
        sleepf(.6);
    }
    sleepSec(4);

    
    
    //for (int k = 0; k < 8; ++k) {
    for (int k = 0; k < 8; ++k) {
        f64 dt = .25;
        f64 latency = .02;
        f64 t0 = getStreamTime(e);
        for (int i = 0; i < numPitches; ++i) {
            begin(e, 0);
                //params: pitch, veloc, drive, pan, atk, rel.
                //f32 params[6];
            f32 pitch = pitches[i] - 5 * k + 10;
            noteOn(101, noteID, 1, &pitch);
            f32 drive = 1. + 2.3 * k;
            noteSetParamRange(101, noteID, 2, 1, &drive);
            
            f64 t = t0 + latency + i * dt;
            sched(t);
            
            t += 2.;
            begin(e, 0);
            noteOff(101, noteID);
            sched(t);
            ++noteID;
        }
        sleepSec(3);

        t0 = getStreamTime(e);
        for (int i = 0; i < numPitches; ++i) {
            begin(e, 0);
                //params: pitch, veloc, drive, pan, atk, rel.
                //f32 params[6];
            f32 pitch = 2 + pitches[i] - 5 * k + 10;
            noteOn(101, noteID, 1, &pitch);
            f32 drive = 2. + 2.3 * k;
            noteSetParamRange(101, noteID, 2, 1, &drive);

            f64 t = t0 + latency + i * dt;
            sched(t);
            
            t += 2.;
            begin(e, 0);
            noteOff(101, noteID);
            sched(t);
            ++noteID;
        }
        sleepSec(3);
    }
    sleepSec(4);
    printf("allNotesOff\n");
    begin(e, 0);
        allNotesOff(101);
    go();
    sleepSec(5);

    printf("stop\n");
    stopAudio(e);

    printf("free engine\n");
    freeEngine(e);
}

void test4()
{
    printf("--- test4 ---------------------------------\n");
    using namespace engine;

    EngineConfig config;
    config.numSilos = 10;
    
    AudioStreamParameters asp{ "default", 2, 0, 256, 48000.};
    //AudioStreamParameters asp{ "Apple Inc.: MacBook Mic & Speakers", 2, 0, 256, 48000.};
    auto e = newEngine(config, asp);

    createSineNode(e);

    printf("start audio\n");
    startAudio(e);
     //printf("running %d\n", isAudioRunning(e));    

    sleepSec(1);

    //printf("create graph A\n");
    f32 freq;
    f32 amp;

//    f32 latency = .02;
//    f32 dt = .2;
    printf("create graphs on %d threads\n", config.numSilos);
    for (int i = 0; i < config.numSilos; ++i) {
        sleepf(.5);
        begin(e, i);
        newNode("sinosc", 101);
        freq = 240 + 73.371 * i;
        setInput({101, 0}, 1, &freq);
        amp = 0.0;
        setInput({101, 1}, 1, &amp);
        amp = 0.05;
        setInput({101, 1}, 1, &amp, .5, fadeEaseInCubic);
        connect({101, 0}, {0, 0});
        go();
    }
 
    sleepSec(4);

    for (int i = 0; i < config.numSilos; ++i) {
        sleepf(.5);
        begin(e, config.numSilos-i-1);
        amp = 0.00;
        setInput({101, 1}, 1, &amp, .5, fadeEaseOutCubic);
        go();
    }
    sleepSec(1);

    printf("stop\n");
    stopAudio(e);

    printf("free engine\n");
    freeEngine(e);
}

int main(int argc, const char * argv[])
{
    test_sexpr();
//    printf("argc %d\n", argc);
//    for (int i = 0; i < argc; ++i) {
//        printf("   arg %d '%s'\n", i, argv[i]);
//    }
    try {
            //test0();
            test5();         
            test1();
            test2();
            test4();
            test3();
    } catch (std::exception& exc) {
        printf("an exception occurred: %s\n", exc.what());
    } catch (int& errc) {
        printf("an exception occurred: %d\n", errc);
    } catch (...) {
        printf("an unknown exception occurred.\n");
    }
    return 0;
}

