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
//  Test/example plugins for the block engine (ABI v3).
//

#include "tzpl_test_plugins.hpp"
#include <cmath>
#include <map>
#include <string>
#include <cstring>

using namespace engine;

namespace {

PortInfo* portArray(std::initializer_list<PortInfo> ports) {
    // Lives as long as the def (NodeDefInfo holds raw pointers): leaked on
    // purpose, these are registered once per engine in tests and demos.
    PortInfo* p = new PortInfo[ports.size()];
    int i = 0;
    for (PortInfo const& x : ports) p[i++] = x;
    return p;
}

tzpl_SignalType f32Type(int chans) { return {tzpl::shapeRow(chans), tzpl_kF32, tzpl_audioRate}; }

PortInfo specPort(char const* name, tzpl_SignalType type, f64 lo, f64 hi, f64 init) {
    PortInfo p{name, type};
    p.hasSpec = 1;
    p.spec = tzpl_ControlSpec{lo, hi, init, 0., tzpl_cwLinear, tzpl_ckContinuous};
    return p;
}

template <class T>
tzpl_SynthData* allocT(int32_t) { return new T(); }
template <class T>
tzpl_SErr freeT(tzpl_SynthData* s) { delete static_cast<T*>(s); return tzpl_errNone; }
tzpl_SErr initNone(tzpl_SynthData*) { return tzpl_errNone; }

} // namespace

// ===========================================================================
// SinOsc
// ===========================================================================

struct SinOsc : tzpl_SynthData {
    f64 phase = 0.;
};

static void SinOsc_process(tzpl_SynthData* synth, int32_t off, int32_t n) {
    auto* o = static_cast<SinOsc*>(synth);
    f32 const* freq = inChan<f32>(synth, 0, 0) + off;
    f32 const* amp = inChan<f32>(synth, 1, 0) + off;
    f32* l = outChan<f32>(synth, 0, 0) + off;
    f32* r = outChan<f32>(synth, 0, 1) + off;
    f64 freqmul = 2. * M_PI / synth->fs;
    for (int i = 0; i < n; ++i) {
        l[i] = r[i] = amp[i] * (f32)std::sin(o->phase);
        o->phase += freqmul * freq[i];
    }
}

void engine::createSineNode(Engine* e) {
    NodeDefInfo info{};
    info.name = "sinosc";
    info.num_ins = 2;
    info.num_outs = 1;
    info.ins = portArray({specPort("freq", f32Type(1), 20., 20000., 261.625565),
                          specPort("amp", f32Type(1), 0., 1., 0.25)});
    info.outs = portArray({PortInfo{"out", f32Type(2)}});
    info.funs.alloc = allocT<SinOsc>;
    info.funs.free = freeT<SinOsc>;
    info.funs.init = initNone;
    info.funs.process = SinOsc_process;
    addNodeDef(e, info);
}

// ===========================================================================
// AddOp / MulOp
// ===========================================================================

template <bool Mul>
static void BinOp_process(tzpl_SynthData* synth, int32_t off, int32_t n) {
    for (int c = 0; c < 2; ++c) {
        f32 const* a = inChan<f32>(synth, 0, c) + off;
        f32 const* b = inChan<f32>(synth, 1, c) + off;
        f32* out = outChan<f32>(synth, 0, c) + off;
        for (int i = 0; i < n; ++i) out[i] = Mul ? a[i] * b[i] : a[i] + b[i];
    }
}

static void createBinOp(Engine* e, char const* name, bool mul) {
    NodeDefInfo info{};
    info.name = name;
    info.num_ins = 2;
    info.num_outs = 1;
    info.ins = portArray({PortInfo{"a", f32Type(2)}, PortInfo{"b", f32Type(2)}});
    info.outs = portArray({PortInfo{"out", f32Type(2)}});
    info.funs.alloc = allocT<tzpl_SynthData>;
    info.funs.free = freeT<tzpl_SynthData>;
    info.funs.init = initNone;
    info.funs.process = mul ? BinOp_process<true> : BinOp_process<false>;
    addNodeDef(e, info);
}

void engine::createAddOpNode(Engine* e) { createBinOp(e, "+", false); }
void engine::createMulOpNode(Engine* e) { createBinOp(e, "*", true); }

// ===========================================================================
// VoicerTest -- a voiced sine synth
// ===========================================================================

struct VoicerTest : tzpl_SynthData {
    static constexpr int kVoices = 32;
    struct Voice { f64 phase = 0., level = 0.; } voices[kVoices];
};

static tzpl_SErr VoicerTest_resetVoice(tzpl_SynthData* synth, int32_t v, int32_t) {
    auto* o = static_cast<VoicerTest*>(synth);
    if (v >= 0 && v < VoicerTest::kVoices) o->voices[v].phase = 0.;
    return tzpl_errNone;
}

static void VoicerTest_process(tzpl_SynthData* synth, int32_t off, int32_t n) {
    auto* o = static_cast<VoicerTest*>(synth);
    f32* l = outChan<f32>(synth, 0, 0) + off;
    f32* r = outChan<f32>(synth, 0, 1) + off;
    for (int i = 0; i < n; ++i) l[i] = r[i] = 0.f;
    f64 atk = 1. - std::exp(-1. / (0.005 * synth->fs));
    f64 rel = 1. - std::exp(-1. / (0.2 * synth->fs));
    for (int v = 0; v < VoicerTest::kVoices; ++v) {
        auto& vc = o->voices[v];
        f32 const* gate = inChan<f32>(synth, 0, v * 3) + off;
        f32 const* note = inChan<f32>(synth, 0, v * 3 + 1) + off;
        f32 const* amp = inChan<f32>(synth, 0, v * 3 + 2) + off;
        if (vc.level < 1e-6 && synth->ins[0].isConst[v * 3] && gate[0] <= 0.f) continue;
        for (int i = 0; i < n; ++i) {
            f64 target = gate[i] > 0.f ? amp[i] : 0.;
            vc.level += (target - vc.level) * (gate[i] > 0.f ? atk : rel);
            f64 freq = 440. * std::exp2((note[i] - 69.) / 12.);
            f32 x = (f32)(vc.level * std::sin(vc.phase));
            vc.phase += 2. * M_PI * freq / synth->fs;
            l[i] += x;
            r[i] += x;
        }
    }
}

void engine::createVoicerTestNode(Engine* e) {
    NodeDefInfo info{};
    info.name = "voicerTest";
    info.num_ins = 1;
    info.num_outs = 1;
    info.ins = portArray({PortInfo{"notes",
        {tzpl::shapeMatrix(VoicerTest::kVoices, 3), tzpl_kF32, tzpl_audioRate}}});
    info.outs = portArray({PortInfo{"out", f32Type(2)}});
    info.voiced = 1;
    info.maxVoices = VoicerTest::kVoices;
    info.funs.alloc = allocT<VoicerTest>;
    info.funs.free = freeT<VoicerTest>;
    info.funs.init = initNone;
    info.funs.process = VoicerTest_process;
    info.funs.resetVoice = VoicerTest_resetVoice;
    addNodeDef(e, info);
}

// ===========================================================================
// Pass -- out = in, any shape
// ===========================================================================

template <typename T>
static void Pass_process(tzpl_SynthData* synth, int32_t off, int32_t n) {
    Node* node = (Node*)synth->node;
    int chans = node->outs[0].chans_;
    for (int c = 0; c < chans; ++c) {
        T const* in = inChan<T>(synth, 0, c) + off;
        T* out = outChan<T>(synth, 0, c) + off;
        for (int i = 0; i < n; ++i) out[i] = in[i];
    }
}

void engine::createPassNode(Engine* e, char const* name, tzpl_Shape shape, tzpl_ElemType elem) {
    NodeDefInfo info{};
    info.name = strdup(name);
    info.num_ins = 1;
    info.num_outs = 1;
    tzpl_SignalType type{shape, elem, tzpl_audioRate};
    info.ins = portArray({PortInfo{"in", type}});
    info.outs = portArray({PortInfo{"out", type}});
    info.funs.alloc = allocT<tzpl_SynthData>;
    info.funs.free = freeT<tzpl_SynthData>;
    info.funs.init = initNone;
    switch (elem) {
        case tzpl_kF32: info.funs.process = Pass_process<f32>; break;
        case tzpl_kF64: info.funs.process = Pass_process<f64>; break;
        case tzpl_kI32: info.funs.process = Pass_process<i32>; break;
        case tzpl_kI64: info.funs.process = Pass_process<i64>; break;
    }
    addNodeDef(e, info);
}

void engine::createPassNode(Engine* e, char const* name, int numChannels) {
    createPassNode(e, name, tzpl::shapeRow(numChannels), tzpl_kF32);
}

// ===========================================================================
// counter
// ===========================================================================

struct Counter : tzpl_SynthData {
    i64 count = 0;
};

static void Counter_process(tzpl_SynthData* synth, int32_t off, int32_t n) {
    auto* o = static_cast<Counter*>(synth);
    f32* out = outChan<f32>(synth, 0, 0) + off;
    for (int i = 0; i < n; ++i) out[i] = (f32)o->count++;
}

void engine::createCounterNode(Engine* e) {
    NodeDefInfo info{};
    info.name = "counter";
    info.num_ins = 0;
    info.num_outs = 1;
    info.outs = portArray({PortInfo{"out", f32Type(1)}});
    info.funs.alloc = allocT<Counter>;
    info.funs.free = freeT<Counter>;
    info.funs.init = initNone;
    info.funs.process = Counter_process;
    addNodeDef(e, info);
}

// ===========================================================================
// vtest -- voiced probe
// ===========================================================================

std::vector<engine::VTestReset> engine::vtestResets;

struct VTest : tzpl_SynthData {
    i64 age[4] = {0, 0, 0, 0};
    i64 processed = 0;   // samples processed so far (the node's own clock)
};

static tzpl_SErr VTest_resetVoice(tzpl_SynthData* synth, int32_t v, int32_t) {
    auto* o = static_cast<VTest*>(synth);
    if (v >= 0 && v < 4) o->age[v] = 0;
    vtestResets.push_back({o->processed, v});
    return tzpl_errNone;
}

static void VTest_process(tzpl_SynthData* synth, int32_t off, int32_t n) {
    auto* o = static_cast<VTest*>(synth);
    for (int v = 0; v < 4; ++v) {
        f32 const* gate = inChan<f32>(synth, 0, v * 2) + off;
        f32 const* val = inChan<f32>(synth, 0, v * 2 + 1) + off;
        f32* out = outChan<f32>(synth, 0, v) + off;
        i64 age = o->age[v];
        for (int i = 0; i < n; ++i) out[i] = gate[i] * (val[i] + (f32)age++);
        o->age[v] = age;
    }
    o->processed += n;
}

void engine::createVoicedTestNode(Engine* e) {
    NodeDefInfo info{};
    info.name = "vtest";
    info.num_ins = 1;
    info.num_outs = 1;
    info.ins = portArray({PortInfo{"notes", {tzpl::shapeMatrix(4, 2), tzpl_kF32, tzpl_audioRate}}});
    info.outs = portArray({PortInfo{"out", f32Type(4)}});
    info.voiced = 1;
    info.maxVoices = 4;
    info.funs.alloc = allocT<VTest>;
    info.funs.free = freeT<VTest>;
    info.funs.init = initNone;
    info.funs.process = VTest_process;
    info.funs.resetVoice = VTest_resetVoice;
    addNodeDef(e, info);
}

// ===========================================================================
// rec -- records its input
// ===========================================================================

std::map<std::string, std::vector<std::vector<f32>>> engine::recordings;

struct Rec : tzpl_SynthData {
    std::vector<std::vector<f32>>* store = nullptr;
};

static void Rec_process(tzpl_SynthData* synth, int32_t off, int32_t n) {
    auto* o = static_cast<Rec*>(synth);
    Node* node = (Node*)synth->node;
    if (!o->store) {
        o->store = &recordings[node->def->info_.name];
        o->store->assign(node->ins[0].chans_, {});
    }
    for (int c = 0; c < node->ins[0].chans_; ++c) {
        f32 const* in = inChan<f32>(synth, 0, c) + off;
        (*o->store)[c].insert((*o->store)[c].end(), in, in + n);
    }
    f32* out = outChan<f32>(synth, 0, 0) + off;
    for (int i = 0; i < n; ++i) out[i] = 0.f;
}

void engine::createRecNode(Engine* e, char const* name, tzpl_Shape shape) {
    NodeDefInfo info{};
    info.name = strdup(name);
    info.num_ins = 1;
    info.num_outs = 1;
    info.ins = portArray({PortInfo{"in", {shape, tzpl_kF32, tzpl_audioRate}}});
    info.outs = portArray({PortInfo{"out", f32Type(1)}});
    info.funs.alloc = allocT<Rec>;
    info.funs.free = freeT<Rec>;
    info.funs.init = initNone;
    info.funs.process = Rec_process;
    addNodeDef(e, info);
}
