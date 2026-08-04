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
//  tzpl_test_plugins.cpp
//  audio engine
//
//  Test/example plugin implementations: SinOsc, AddOp, MulOp, VoicerTest.
//

#include "tzpl_test_plugins.hpp"

using namespace engine;

// ---------------------------------------------------------------------------
// bwarp — bipolar to bipolar warping
// ---------------------------------------------------------------------------

f32 engine::bwarp(f32 x, f32 w) {
    f32 u = .5f * x - .5f;
    return (w + u) / (w * x - u);
}

// ===========================================================================
// VoicerTest plugin
// ===========================================================================

struct VoicerTest : tzpl_SynthData
{
    static const int kNumVoices = 32;
    static const int kNumParams = 6;
    RowVoicer<kNumVoices, kNumParams> voicer;
    struct Voice {
        f64 ampL, ampR, ampLgoal, ampRgoal, level, atkCoeff, relCoeff;
        f64 freq, phase, drive;
    } voices[kNumVoices] = {0};
    f64 freqmul;
    f64 amplag;
    // Voicer's row layout is [gate | params]: 1+kNumParams floats per voice.
    // Sized kNumParams wide, every full-voice walk (allocVoice, allOff,
    // processAudio) read and wrote past the end of the array.
    f32 params[kNumVoices][kNumParams + 1];
};

tzpl_SynthData* VoicerTest_alloc() {
    return (tzpl_SynthData*)new VoicerTest();
}

tzpl_SErr VoicerTest_free(VoicerTest* o) {
    delete o;
    return tzpl_errNone;
}

tzpl_SErr VoicerTest_init(VoicerTest* o) {
    printf("VoicerTest_init %p\n", o);
    o->voicer.setParams((f32*)o->params);
    o->freqmul = 2. * M_PI / o->fs;
    o->amplag = calcDecay(.01, .01, o->fs);
    return tzpl_errNone;
}

tzpl_SErr VoicerTest_uninit(VoicerTest* o) {
    return tzpl_errNone;
}

void VoicerTest_processAudio(VoicerTest* o) {
    f32* out = getOut<f32>(o, 0);
    out[0] = 0.f;
    out[1] = 0.f;
    for (int i = 0; i < VoicerTest::kNumVoices; ++i) {
        f32* row = o->voicer.getRow(i);
        VoicerTest::Voice& v = o->voices[i];

        f32 gate = row[0];
        v.level += (gate > v.level ? v.atkCoeff : v.relCoeff) * (gate - v.level);
        v.ampL += o->amplag * (v.ampLgoal - v.ampL);
        v.ampR += o->amplag * (v.ampRgoal - v.ampR);
        f32 wave = v.level * sin(v.drive * sin(v.phase));
        out[0] += v.ampL * wave;
        out[1] += v.ampR * wave;
        v.phase += v.freq;
        if (gate < 1.) v.drive *= .99997;
    }
}

tzpl_SErr VoicerTest_updateParameters(VoicerTest* o, int voiceIndex) {
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
    return tzpl_errNone;
}

tzpl_SErr VoicerTest_noteOn(VoicerTest* o, int64_t now, int noteID, int n, f32* params) {
    int voiceIndex;
    tzpl_SErr err = o->voicer.noteOn(now, noteID, n, params, voiceIndex);
    if (err != tzpl_errNone) return err;
    printf("noteOn  %4d %3d act %3d  %8.2f\n",
        noteID, voiceIndex,
        o->voicer.activeVoices(), ampdb(o->voices[voiceIndex].level));

    f32* row = o->voicer.getRow(voiceIndex);
    f32 defaultParams[VoicerTest::kNumParams] = {
        60., .5, 1., 0., .1, 2.
    };
    if (n < VoicerTest::kNumParams) {
        memcpy(row+n+1, defaultParams+n, (VoicerTest::kNumParams-n)*sizeof(f32));
    }

    return VoicerTest_updateParameters(o, voiceIndex);
}

tzpl_SErr VoicerTest_noteOff(VoicerTest* o, i64 now, int noteID) {
    int voiceIndex = o->voicer.findVoice(noteID, ShouldCache::no);
    o->voicer.noteOff(now, noteID);
    printf("noteOff %4d %3d act %3d\n",
        noteID, voiceIndex, o->voicer.activeVoices());
    return tzpl_errNone;
}

tzpl_SErr VoicerTest_allNotesOff(VoicerTest* o, i64 now) {
    o->voicer.allOff(now);
    return tzpl_errNone;
}

tzpl_SErr VoicerTest_noteSetParams(VoicerTest* o, int noteID, int n, tzpl_ParamPair* params) {
    int voiceIndex = o->voicer.findVoice(noteID);
    if (voiceIndex < 0) return tzpl_errNoteNotFound;
    o->voicer.setNoteParams(noteID, n, params);
    return VoicerTest_updateParameters(o, voiceIndex);
}

tzpl_SErr VoicerTest_noteSetParamRange(VoicerTest* o, int noteID, int first, int length, f32* values) {
    int voiceIndex = o->voicer.findVoice(noteID);
    if (voiceIndex < 0) return tzpl_errNoteNotFound;
    o->voicer.setNoteParamRange(voiceIndex, first, length, values);
    return VoicerTest_updateParameters(o, voiceIndex);
}

tzpl_SynthFuns VoicerTest_funs = {
    .alloc = VoicerTest_alloc,
    .free = (tzpl_SErr(*)(tzpl_SynthData*))VoicerTest_free,
    .init = (tzpl_SErr(*)(tzpl_SynthData*))VoicerTest_init,
    .uninit = (tzpl_SErr(*)(tzpl_SynthData*))VoicerTest_uninit,
    .reset = nullptr,
    .event = nullptr,
    .processAudio = (void(*)(tzpl_SynthData*))VoicerTest_processAudio,

    .allNotesOff = (tzpl_SErr(*)(tzpl_SynthData*,int64_t))VoicerTest_allNotesOff,
    .noteOn = (tzpl_SErr(*)(tzpl_SynthData*,int64_t,int,int,f32*))VoicerTest_noteOn,
    .noteOff = (tzpl_SErr(*)(tzpl_SynthData*,int64_t,int))VoicerTest_noteOff,
    .noteSetParams = (tzpl_SErr(*)(tzpl_SynthData*,int,int,tzpl_ParamPair*))VoicerTest_noteSetParams,
    .noteSetParamRange = (tzpl_SErr(*)(tzpl_SynthData*,int,int,int,f32*))VoicerTest_noteSetParamRange,
};

void engine::createVoicerTestNode(Engine* e)
{
    NodeDefInfo info;
    memset(&info, 0, sizeof(info));

    info.name = "voicer";
    info.num_outs = 1;
    PortInfo out{"out", {tzpl_kF32, tzpl_audioRate, 2}};
    info.outs = (PortInfo*)calloc(info.num_outs, sizeof(PortInfo));
    info.outs[0] = out;
    info.funs = VoicerTest_funs;

    addNodeDef(e, info);
}

// ===========================================================================
// AddOp plugin
// ===========================================================================

struct AddOp : tzpl_SynthData
{};

tzpl_SynthData* AddOp_alloc() {
    return (tzpl_SynthData*)new AddOp();
}

tzpl_SErr AddOp_free(tzpl_SynthData* synth) {
    delete (AddOp*)synth;
    return tzpl_errNone;
}

tzpl_SErr AddOp_init(tzpl_SynthData* synth)
{
    return tzpl_errNone;
}

tzpl_SErr AddOp_uninit(tzpl_SynthData* synth)
{
    return tzpl_errNone;
}

void AddOp_processAudio(tzpl_SynthData* synth)
{
    f32* a = (f32*)getInput(synth, 0);
    f32* b = (f32*)getInput(synth, 1);
    f32* out = getOut<f32>(synth, 0);
    out[0] = a[0] + b[0];
    out[1] = a[1] + b[1];
}

tzpl_SynthFuns AddOp_funs = {
    .alloc = AddOp_alloc,
    .free = AddOp_free,
    .init = AddOp_init,
    .uninit = AddOp_uninit,
    .reset = nullptr,
    .event = nullptr,
    .processAudio = AddOp_processAudio,
};

void engine::createAddOpNode(Engine* e)
{
    NodeDefInfo info;
    memset(&info, 0, sizeof(info));

    info.name = "+";
    info.num_ins = 2;
    info.num_outs = 1;

    PortInfo a{"a", {tzpl_kF32, tzpl_audioRate, 2}};
    PortInfo b{"b", {tzpl_kF32, tzpl_audioRate, 2}};
    info.ins = (PortInfo*)calloc(info.num_ins, sizeof(PortInfo));
    info.ins[0] = a;
    info.ins[1] = b;

    PortInfo out{"out", {tzpl_kF32, tzpl_audioRate, 2}};
    info.outs = (PortInfo*)calloc(info.num_outs, sizeof(PortInfo));
    info.outs[0] = out;
    info.funs = AddOp_funs;

    addNodeDef(e, info);
}

// ===========================================================================
// MulOp plugin
// ===========================================================================

struct MulOp : tzpl_SynthData
{};

tzpl_SynthData* MulOp_alloc() {
    return (tzpl_SynthData*)new MulOp();
}

tzpl_SErr MulOp_free(tzpl_SynthData* synth) {
    delete (MulOp*)synth;
    return tzpl_errNone;
}

tzpl_SErr MulOp_init(tzpl_SynthData* synth)
{
    return tzpl_errNone;
}

tzpl_SErr MulOp_uninit(tzpl_SynthData* synth)
{
    return tzpl_errNone;
}

void MulOp_processAudio(tzpl_SynthData* synth)
{
    f32* a = (f32*)getInput(synth, 0);
    f32* b = (f32*)getInput(synth, 1);
    f32* out = getOut<f32>(synth, 0);
    out[0] = a[0] * b[0];
    out[1] = a[1] * b[1];
}

tzpl_SynthFuns MulOp_funs = {
    .alloc = MulOp_alloc,
    .free = MulOp_free,
    .init = MulOp_init,
    .uninit = MulOp_uninit,
    .reset = nullptr,
    .event = nullptr,
    .processAudio = MulOp_processAudio,
};

void engine::createMulOpNode(Engine* e)
{
    NodeDefInfo info;
    memset(&info, 0, sizeof(info));

    info.name = "*";

    info.num_ins = 2;
    info.num_outs = 1;

    PortInfo a{"a", {tzpl_kF32, tzpl_audioRate, 2}};
    PortInfo b{"b", {tzpl_kF32, tzpl_audioRate, 2}};
    info.ins = (PortInfo*)calloc(info.num_ins, sizeof(PortInfo));
    info.ins[0] = a;
    info.ins[1] = b;

    PortInfo out{"out", {tzpl_kF32, tzpl_audioRate, 2}};
    info.outs = (PortInfo*)calloc(info.num_outs, sizeof(PortInfo));
    info.outs[0] = out;

    info.funs = MulOp_funs;

    addNodeDef(e, info);
}

// ===========================================================================
// SinOsc plugin
// ===========================================================================

struct SinOsc : tzpl_SynthData
{
    f64 freqmul;
    f64 phase;
};

tzpl_SynthData* SinOsc_alloc() {
    return (tzpl_SynthData*)new SinOsc();
}

tzpl_SErr SinOsc_free(tzpl_SynthData* synth) {
    delete (SinOsc*)synth;
    return tzpl_errNone;
}

tzpl_SErr SinOsc_init(tzpl_SynthData* synth)
{
    SinOsc* o = (SinOsc*)synth;

    getIn<f32>(synth,0)[0] = 261.625565; // freq
    getIn<f32>(synth,1)[0] = 0.25f; // amp
    o->freqmul = 2. * M_PI / synth->fs;
    o->phase = 0.;

    return tzpl_errNone;
}

tzpl_SErr SinOsc_uninit(tzpl_SynthData* synth)
{
    return tzpl_errNone;
}

void SinOsc_processAudio(tzpl_SynthData* synth)
{
    SinOsc* o = (SinOsc*)synth;
    f32* freq = (f32*)getInput(o, 0);
    f32* amp  = (f32*)getInput(o, 1);
    f32* out = getOut<f32>(synth, 0);
    out[0] = *amp * sin(o->phase);
    out[1] = out[0];
    o->phase += o->freqmul * *freq;
}

tzpl_SynthFuns SinOsc_funs = {
    .alloc = SinOsc_alloc,
    .free = SinOsc_free,
    .init = SinOsc_init,
    .uninit = SinOsc_uninit,
    .reset = nullptr,
    .event = nullptr,
    .processAudio = SinOsc_processAudio,
};

void engine::createSineNode(Engine* e)
{
    NodeDefInfo info;
    memset(&info, 0, sizeof(info));

    info.name = "sinosc";

    info.num_ins = 2;
    info.num_outs = 1;

    PortInfo freq{"freq", {tzpl_kF32, tzpl_audioRate, 1}};
    PortInfo amp{"amp", {tzpl_kF32, tzpl_audioRate, 1}};
    info.ins = (PortInfo*)calloc(info.num_ins, sizeof(PortInfo));
    info.ins[0] = freq;
    info.ins[1] = amp;

    PortInfo out{"out", {tzpl_kF32, tzpl_audioRate, 2}};
    info.outs = (PortInfo*)calloc(info.num_outs, sizeof(PortInfo));
    info.outs[0] = out;

    info.funs = SinOsc_funs;

    addNodeDef(e, info);
}

// ===========================================================================
// Pass plugin -- out = in, at a chosen channel width
// ===========================================================================

template <int N>
static void Pass_processAudio(tzpl_SynthData* synth)
{
    f32 const* in = (f32 const*)getInput(synth, 0);
    f32* out = getOut<f32>(synth, 0);
    for (int c = 0; c < N; ++c) out[c] = in[c];
}

void engine::createPassNode(Engine* e, char const* name, int numChannels)
{
    NodeDefInfo info;
    memset(&info, 0, sizeof(info));

    info.name = name;
    info.num_ins = 1;
    info.num_outs = 1;

    tzpl_SignalType type{tzpl_kF32, tzpl_audioRate, numChannels};

    info.ins = (PortInfo*)calloc(info.num_ins, sizeof(PortInfo));
    info.ins[0] = PortInfo{"in", type};

    info.outs = (PortInfo*)calloc(info.num_outs, sizeof(PortInfo));
    info.outs[0] = PortInfo{"out", type};

    info.funs.alloc  = []() -> tzpl_SynthData* { return new tzpl_SynthData(); };
    info.funs.free   = [](tzpl_SynthData* s) -> tzpl_SErr { delete s; return tzpl_errNone; };
    switch (numChannels) {
        case 1: info.funs.processAudio = Pass_processAudio<1>; break;
        case 2: info.funs.processAudio = Pass_processAudio<2>; break;
        case 4: info.funs.processAudio = Pass_processAudio<4>; break;
        case 8: info.funs.processAudio = Pass_processAudio<8>; break;
        default: info.funs.processAudio = nullptr; break;
    }

    addNodeDef(e, info);
}
