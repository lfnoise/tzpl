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

#include "tzpl_plugin_abi.h"
#include "synthdef_synth.hpp"
#include "synthdef_cpp_codegen.hpp"
#include "synthdef_audio_io.hpp"
#include "synthdef_compile.hpp"
#include "synthdef_builtin_ops.hpp"
#include "synthdef_common_ops.hpp"
#include "synthdef_examples.hpp"
#include "synthdef_from_sexpr.hpp"
#include <iostream>
#include <iomanip>
#include <float.h>
#include <unistd.h>

// Forward declare test function
namespace synthdef {
    void test_sexpr_integration();
}


void test_rates() {
    printf("test_rates\n");
    using namespace synthdef;
    assert(constSignalRate < initSignalRate);
    assert(initSignalRate < resetSignalRate);
    assert(resetSignalRate < eventSignalRate);
    assert(eventSignalRate < audioSignalRate);
    assert(constSignalRate < audioSignalRate);
    assert(constSignalRate.max(initSignalRate) == initSignalRate);
    assert(initSignalRate.max(constSignalRate) == initSignalRate);
    assert(initSignalRate.max(resetSignalRate) == resetSignalRate);
    assert(SignalRate(resetSignalRate).max(initSignalRate) == resetSignalRate);

    assert(constSignalRate != initSignalRate);
    assert(constSignalRate == constSignalRate);
}

void test_num_type() {
    printf("test_num_type\n");
    using namespace synthdef;
    assert(NumType::empty.is_empty());
    assert(NumType::any.is_concrete() == false);
    assert(NumType::any_int.is_concrete() == false);
    assert(NumType::any_float.is_concrete() == false);
    assert(NumType::i32.is_concrete());
    assert(NumType::i64.is_concrete());
    assert(NumType::f32.is_concrete());
    assert(NumType::f64.is_concrete());
    assert(NumType::i32.is_i32());
    assert(NumType::i64.is_i64());
    assert(NumType::f32.is_f32());
    assert(NumType::f64.is_f64());
    assert(NumType::i32.supports_int());
    assert(NumType::i64.supports_int());
    assert(NumType::f32.supports_float());
    assert(NumType::f64.supports_float());
    assert(!NumType::i32.supports_float());
    assert(!NumType::i64.supports_float());
    assert(!NumType::f32.supports_int());
    assert(!NumType::f64.supports_int());
    assert(NumType::i32.supports_32_bits());
    assert(NumType::i64.supports_64_bits());
    assert(NumType::f32.supports_32_bits());
    assert(NumType::f64.supports_64_bits());
    assert(!NumType::i32.supports_64_bits());
    assert(!NumType::i64.supports_32_bits());
    assert(!NumType::f32.supports_64_bits());
    assert(!NumType::f64.supports_32_bits());
    assert(NumType::i32.is_32_bits());
    assert(NumType::i64.is_64_bits());
    assert(NumType::f32.is_float());
    assert(NumType::f64.is_float());
    assert(NumType::any_float.is_float());
    assert(NumType::i32.is_int());
    assert(NumType::i64.is_int());
    assert(NumType::any_int.is_int());
    assert(!NumType::any.is_int());
    assert(!NumType::any.is_float());
    assert((NumType::i32 & NumType::i64) == NumType::empty);
    assert((NumType::i32 & NumType::f32) == NumType::empty);
    assert((NumType::i32 & NumType::f64) == NumType::empty);
    assert((NumType::i32 | NumType::i64) == NumType::any_int);
    assert((NumType::i32 | NumType::f32) != NumType::any);
    assert((NumType::i32 | NumType::f64) != NumType::any);
}

#if 0
void test_constant_vector() {
    printf("test_constant\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_constant"));

    S a = new Constant(VectorT<i64>{vector<i64>{10, 20, 30}});
    S b = new Constant(VectorT<i64>{vector<i64>{1, 2, 3}});
    S brev = new Constant(VectorT<i64>{vector<i64>{3, 2, 1}});

    // [(10, 1), (20, 2), (10, 3), (20, 1), (10, 2), (20, 3)]

//    printf("a: %s\n", a->str().c_str());
//    printf("b: %s\n", b->str().c_str());
//    printf("(a + b): %s\n", (a + b)->str().c_str());
//    printf("(a & b): %s\n", (a & b)->str().c_str());
//    printf("(a | b): %s\n", (a | b)->str().c_str());
//    printf("(a ^ b): %s\n", (a ^ b)->str().c_str());
//    printf("a.pow(b): %s\n", a.pow(b)->str().c_str());
    assert((a + b).equals(S(vector<i64>{11, 22, 33})));
    assert((a - b).equals(S(vector<i64>{9, 18, 27})));
    assert((a * b).equals(S(vector<i64>{10, 40, 90})));
    assert((a / b).equals(S(vector<i64>{10, 10, 10})));
    assert((a % b).equals(S(vector<i64>{0, 0, 0})));
    assert((a & b).equals(S(vector<i64>{0, 0, 2})));
    assert((a | b).equals(S(vector<i64>{11, 22, 31})));
    assert((a ^ b).equals(S(vector<i64>{11, 22, 29})));
    assert((a << b).equals(S(vector<i64>{20, 80, 240})));
    assert((a >> b).equals(S(vector<i64>{5, 5, 3})));
    assert(a.ushr(b).equals(S(vector<i64>{5, 5, 3})));
    assert(a.pow(b).equals(S(vector<f64>{10., 400., 27000.})));

    assert(b.eq(b).equals(S(vector<i64>{-1, -1, -1})));
    assert(b.eq(brev).equals(S(vector<i64>{0, -1, 0})));
    assert(b.ne(b).equals(S(vector<i64>{0, 0, 0})));
    assert(b.ne(brev).equals(S(vector<i64>{-1, 0, -1})));
    assert(b.lt(brev).equals(S(vector<i64>{-1, 0, 0})));
    assert(b.le(brev).equals(S(vector<i64>{-1, -1, 0})));
    assert(b.gt(brev).equals(S(vector<i64>{0, 0, -1})));
    assert(b.ge(brev).equals(S(vector<i64>{0, -1, -1})));
printf("E\n");

    assert((-a).equals(S(vector<i64>{-10, -20, -30})));
printf("F\n");

    assert(S(vector<f64>{1., 4., 9.}).sqrt().equals(S(vector<f64>{1., 2., 3.})));
    assert(S(vector<f64>{1., 8., 27.}).cbrt().equals(S(vector<f64>{1., 2., 3.})));
    assert(S(vector<f64>{1.1, 2.2, 3.3}).floor().equals(S(vector<f64>{1., 2., 3.})));
    assert(S(vector<f64>{1.1, 2.2, 3.3}).ceil().equals(S(vector<f64>{2., 3., 4.})));
    assert(S(vector<f64>{1.1, 2.5, 3.9}).round().equals(S(vector<f64>{1., 3., 4.})));
    assert(S(vector<f64>{-1.1, 1.1, 2.2}).trunc().equals(S(vector<f64>{-1., 1., 2.})));
printf("G\n");
}

void test_constant_matrix_math() {
    printf("test_constant_matrix_math\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_constant_matrix_math"));
    S a = new Constant(VectorT<i64>{{2, 3}, vector<i64>{1, 2, 3, 4, 5, 6}});
    S b = new Constant(VectorT<i64>{{2, 3}, vector<i64>{10, 20, 30, 40, 50, 60}});
    S c = new Constant(VectorT<i64>{{1, 3}, vector<i64>{70, 80, 90}});
    S d = new Constant(VectorT<i64>{{2, 1}, vector<i64>{100, 200}});
    S z = new Constant(i64(500));
    
    assert((a + b).equals(S({2, 3}, vector<i64>{11, 22, 33, 44, 55, 66})));
    assert((a + c).equals(S({2, 3}, vector<i64>{71, 82, 93, 74, 85, 96})));
    assert((a + d).equals(S({2, 3}, vector<i64>{101, 102, 103, 204, 205, 206})));
    assert((a + z).equals(S({2, 3}, vector<i64>{501, 502, 503, 504, 505, 506})));
    assert((c + d).equals(S({2, 3}, vector<i64>{170, 180, 190, 270, 280, 290})));
    assert((c + z).equals(S({1, 3}, vector<i64>{570, 580, 590})));
    assert((d + z).equals(S({2, 1}, vector<i64>{600, 700})));
    printf("(d + z).equals(S({3, 1}, vector<i64>{600, 700, 800})) %d\n",
        (d + z).equals(S({3, 1}, vector<i64>{600, 700, 800})));
    //assert(!(d + z).equals(S({3, 1}, vector<i64>{600, 700, 800})));
    printf("a.reduce(Col, BinaryOp::Add) %s\n", a.reduce(BinaryOp::Add, Col)->str().c_str());
    printf("a.reduce(Row, BinaryOp::Add) %s\n", a.reduce(BinaryOp::Add, Row)->str().c_str());
    assert(a.reduce(BinaryOp::Add, Col).equals(S({2, 1}, vector<i64>{6, 15})));
    assert(a.reduce(BinaryOp::Add, Row).equals(S({1, 3}, vector<i64>{5, 7, 9})));
//    assert((a / b).equals(S(vector<i64>{0, 0, 0, 0, 0, 0})));
//    assert((a % b).equals(S(vector<i64>{1, 2, 3, 4, 5, 6})));
//    assert((a & b).equals(S(vector<i64>{1, 0, 1, 0, 1, 0})));
//    assert((a | b).equals(S(vector<i64>{7, 10, 11, 7, 13, 15})));
//    assert((a ^ b).equals(S(vector<i64>{6, 10, 10, 7, 12, 15})));
//    assert((a << b).equals(S(vector<i64>{64, 256, 2304, 32, 128, 576})));
//    assert((a >> b).equals(S(vector<i64>{0, 0, 0, 2, 2, 3})));
//    assert(a.ushr(b).equals(S(vector<i64>{0, 0, 0, 2, 2, 3})));
    //assert(a.pow(b).equals(S
}

void test_constant_vec_ops() {
    printf("test_constant_vec_ops\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_constant_vec_ops"));
    Constant* a = new Constant(VectorT<i64>{vector<i64>{1, 2, 3, 4, 5, 6}});
    Constant* b = new Constant(VectorT<i64>{vector<i64>{7, 8, 9}});
    Constant* c = new Constant(VectorT<i64>{vector<i64>{2, 5, 3, 4, 1, 0}});
    Constant* d = new Constant(VectorT<i64>{vector<i64>{0, 0, 3, 3}});
    
    assert(S(a->take(3)).equals(S(vector<i64>{1, 2, 3})));
    assert(S(a->skip(3)).equals(S(vector<i64>{4, 5, 6})));
    assert(S(a->stride(2)).equals(S(vector<i64>{1, 3, 5})));
    assert(S(a->stutter(2)).equals(S(vector<i64>{1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6})));
    assert(S(b->cyc(3)).equals(S(vector<i64>{7, 8, 9, 7, 8, 9, 7, 8, 9})));
    assert(S(a->rotate(-2)).equals(S(vector<i64>{5, 6, 1, 2, 3, 4})));
    assert(S(a->rotate(2)).equals(S(vector<i64>{3, 4, 5, 6, 1, 2})));
    assert(S(a->transpose()).equals(S({6, 1}, vector<i64>{1, 2, 3, 4, 5, 6})));
//    assert(S(a->transpose(3)).equals(S(vector<i64>{1, 4, 2, 5, 3, 6})));
//    assert(S(a->transpose(1)).equals(S(a)));
    assert(S(a->at(c)).equals(S(vector<i64>{3, 6, 4, 5, 2, 1})));
    assert(S(a->at(d)).equals(S(vector<i64>{1, 1, 4, 4})));
}
#endif

void test_rewriting() {
    printf("test_rewriting\n");
    using namespace synthdef;
    {
        PushSynth ps(new Synth("test_rewriting"));
        S a = fs();
        S b(1.);
        S c = a + b;
        assert(!c.equals(a));
    }
    {
        PushSynth ps(new Synth("test_rewriting"));
        S a = fs();
        S b(0.);
        S c = a + b;
        assert(c.equals(a));
    }
    {
        PushSynth ps(new Synth("test_rewriting"));
        S a = fs();
        S b(0.);
        S c = a * b;
        assert(c->get_scalar() == 0.);
    }
    {
        PushSynth ps(new Synth("test_rewriting"));
        S a = fs();
        S b(0.);
        S c = b * a;
        assert(c->get_scalar() == 0.);
    }
}

template <typename T>
auto show_classification(T x)
{
    switch (std::fpclassify(x))
    {
        case FP_INFINITE:
            return "Inf";
        case FP_NAN:
            return "NaN";
        case FP_NORMAL:
            return "normal";
        case FP_SUBNORMAL:
            return "subnormal";
        case FP_ZERO:
            return "zero";
        default:
            return "unknown";
    }
}

void test_ftos() {
    printf("test_ftos\n");
    using namespace synthdef;
    
//    printf("0.1 %s\n---\n", synthdef::ftos(0.1).c_str());
//    printf("50.0 %s\n---\n", synthdef::ftos(50.0).c_str());
//    printf("pi %s\n---\n", synthdef::ftos(M_PI).c_str());
//    
//    u64 u = 0x37a16c3000000000;
//    f64 d;
//    memcpy(&d, &u, 8);
//    printf("0x37a16c3000000000 %.18e %s\n---\n", d, synthdef::ftos(d).c_str());
//    
//    f64 d1 = nextafter(d, 1.0);
//    printf("d1 %.18e %s\n---\n", d1, synthdef::ftos(d1).c_str());
    
    
    // test all possible 32 bit floats, round trip through synthdef::ftos and std::stod and ensure they are equal
    u32 pass = 0;
    u32 denorm = 0;
    u32 fail = 0;
    for (u32 i = 0x00000000; i < 0xff800000; i += 16383) {
        f32 f;
        memcpy(&f, &i, 4);
        bool is_denorm = std::fpclassify(f) == FP_SUBNORMAL;
        if (is_denorm) { ++denorm; }
        string s = synthdef::ftos(f);
        if (s.empty()) {
            printf("empty string for %.18e %x\n", f, i);
            return;
        }
        if (is_denorm) { 
            
//            printf("i %10d pass %10d DENORM %10d fail %10d %22.18e %s\n", i, pass, denorm, fail, f, s.c_str());
            continue; 
        }
        f32 f2 = std::stof(s);
        u32 i2;
        memcpy(&i2, &f2, 4);
        if (f == f2 || std::isnan(f) == std::isnan(f2)) {
            ++pass;
        } else {
            ++fail;
            printf("i %10d pass %10d denorm %10d fail %10d %22.18e %s\n", i, pass, denorm, fail, f, s.c_str());
        }
        //assert(d == d2);
        //printf("---\n");
        //if ((i & 0xfffff) == 0) 
        {
        }
    }
    printf("pass %10d denorm %10d fail %10d\n", pass, denorm, fail);
}

#if 0

void bubbles_test() {
    printf("test_bubbles\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_bubbles"));
    
    bubbles();
    
    gSynth->graphAnalysis();
    
    gSynth->dump();
    printf("test_bubbles done\n");
    string s = cppCodeGen(gSynth);
    printf("%s\n", s.c_str());
    
}

void pause_bubbles_test() {
    printf("pause_bubbles_test ----------------------------------------------\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_bubbles"));
    
    pause_bubbles();
    
    gSynth->graphAnalysis();
    
    gSynth->dump();
    printf("test_bubbles done\n");
    string s = cppCodeGen(gSynth);
    printf("%s\n", s.c_str());
    
}

#endif

#if 0
using namespace synthdef;



extern SynthFuns test_bubbles_funs;

typedef struct test_bubbles {
    SynthFuns* funs;
    void** inlets;
    void** outlets;
    f64 fs, sd;

    // constants
    f32 c27[2];

    // instance variables
    f64 v5;
    f32 v78;
    f32 v107;
    f32 v125;
    f64 v32[2];

    // delays
    f32 *d4[2];
    f32 d3;
    f64 d2[2];
    f64 d1[2];
    f64 d0;

    u64 d4_wrpos;
    u64 d4_mask;
} test_bubbles;

test_bubbles* test_bubbles_alloc() {
    test_bubbles* p = (test_bubbles*)calloc(1, sizeof(test_bubbles));
    p->funs = &test_bubbles_funs;
    p->outlets = (void**)calloc(1, sizeof(void*));
    return p;
}

void test_bubbles_free(test_bubbles* p) {
    int test_bubbles_uninit(test_bubbles* p);
    test_bubbles_uninit(p);
    free(p);
}

int test_bubbles_init(test_bubbles* p) {
    static f32 k27[2] = {8.0f, 7.23f};
    memcpy(p->c27, k27, 2 * sizeof(f32));

    // LOOP  1 [] Init 1 -
    f32 v2 = p->sd; // 2 Broadcast
    p->v5 = f64((0.4f * v2)); // 3 Rate
    p->v78 = (440.0f * v2); // 11 Rate
    p->v107 = (10.0f * v2); // 18 Rate
    p->v125 = (0.2f * p->fs); // 24 Rate


    // LOOP  3 [1] Init 2 -
    for (usize i = 0; i < 2; ++i) {
        p->v32[i] = f64((p->c27[i] * v2)); // 7 Rate
    }

    p->d4_wrpos = 0;
    u64 d4_size = nextPowerOfTwo(4+u64(ceil(p->v125)));
    p->d4_mask = d4_size - 1;
    p->d4[0] = (f32*)calloc(d4_size, sizeof(f32));
    p->d4[1] = (f32*)calloc(d4_size, sizeof(f32));
    return tzpl_errNone;
}

int test_bubbles_uninit(test_bubbles* p) {
    // FIXME genUninitFun
    free(p->d4[0]); p->d4[0] = nullptr;
    free(p->d4[1]); p->d4[1] = nullptr;
    return tzpl_errNone;
}

int test_bubbles_reset(test_bubbles* p) {
    // FIXME genResetFun
    return tzpl_errNone;
}

int test_bubbles_event(test_bubbles* p, u64 paramID, usize rows, usize cols, f64* values) {
    // FIXME genEventFun
    return tzpl_errNone;
}

int test_bubbles_process_events(test_bubbles* p) {
    // FIXME genHandleEventsFun
    return tzpl_errNone;
}

int test_bubbles_tick(test_bubbles* p) {
    // FIXME genTickFun

    // LOOP  0 [1] Audio 1 -
    f64 v4 = p->d0; // 1 FanOut
    f64 v6 = (v4 + p->v5); // 4 FanOut
    p->d0 = (v6 - std::floor(v6)); // 5
    f32 v11 = f32(v4); // 12 FanOut
    f32 v26 = ((48.0f * (v11 - std::floor(v11))) + -24.0f); // 13 Broadcast
    f32 v109 = p->d3; // 17 FanOut
    p->d3 = std::min(1.0f, (v109 + p->v107)); // 19
    f32 v115 = ((v109 * v109) * v109); // 23 Broadcast


    // LOOP  2 [1 0 3] Audio 2 -
    for (usize i = 0; i < 2; ++i) {
        f64 v31 = p->d1[i]; // 6 FanOut
        f64 v33 = (v31 + p->v32[i]); // 8 FanOut
        p->d1[i] = (v33 - std::floor(v33)); // 9
        f64 v80 = p->d2[i]; // 10 FanOut
        f32 v38 = f32(v31); // 14 FanOut
        f64 v82 = (v80 + f64((p->v78 * std::exp2((1.0f + (0.083333336f * (v26 + ((6.0f * (v38 - std::floor(v38))) + -3.0f)))))))); // 15 FanOut
        p->d2[i] = (v82 - std::floor(v82)); // 16
        f32 v87 = f32(v80); // 20 FanOut
        f32 v90 = (v87 - std::floor(v87)); // 21 FanOut
        f32 v92 = (v90 - std::round(v90)); // 22 FanOut
        f32 v128 = (((0.04f * (v92 * (8.0f - std::abs((16.0f * v92))))) * v115) + (p->d4[i][(p->d4_wrpos - u64(p->v125)) & p->d4_mask] * 0.70794576f)); // 25 FanOut
        p->d4[i][p->d4_wrpos & p->d4_mask] = v128; // 26
        ((f32**)p->outlets)[0][i] = v128; // 27
    }
    ++p->d4_wrpos;

    return tzpl_errNone;
}

SynthFuns test_bubbles_funs = {
    .alloc = (SynthData* (*)())test_bubbles_alloc,
    .free = (int (*)(SynthData*))test_bubbles_free,
    .init = (int (*)(SynthData*))test_bubbles_init,
    .uninit = (int (*)(SynthData*))test_bubbles_uninit,
    .reset = (int (*)(SynthData*))test_bubbles_reset,
    .event = (int (*)(SynthData*, u64, tzpl_Slice, tzpl_Slice))test_bubbles_event,
    .process_events = (int (*)(SynthData*))test_bubbles_process_events,
    .tick = (int (*)(SynthData*))test_bubbles_tick
};

extern "C" tzpl_SynthDef load() {
    tzpl_SynthDef def;
    def.name = "test_bubbles";
    def.funs = test_bubbles_funs;
    def.num_ins = 0;
    def.num_outs = 1;
    def.num_controls = 0;
    def.ins = nullptr;
    def.outs = (PortDef*)calloc(def.num_outs, sizeof(PortDef));
    def.controls = nullptr;
    def.outs[0] = {"out0", {kF32, audioRate, 1}, 2};
    return def;
}







void test_synthdef() {
    printf("test_synthdef\n");
//    Engine e;
    
    tzpl_SynthDef def = load();
    assert(def.num_ins == 0);
    assert(def.num_outs == 1);
    assert(def.num_controls == 0);
    
    SynthData* data = def.funs->alloc();
    f32 outs[2];
    data->outlets[0] = outs;
    assert(data->funs == &test_bubbles_funs);
    assert(data->fs == 0);
    
    def.funs->init(data, 48000);
    //std::print("{} fs {}\n", (void*)data, data->fs);
    assert(data->fs == 48000);
    
    AudioEngine e;
    
    e.streamParams = { "default", 2, 0, 256, 48000.};

    e.processFun = [&](AudioEngine* e, f32* out, f32 const* in, int numFrames) {
        int chans = e->streamParams.channels;
        for (int i = 0, j = 0; i < numFrames; ++i, j += chans) {
            def.funs->tick(data);
            out[j] = outs[0];
            out[j+1] = outs[1];
        }
    };
    
    initAudio(&e);
    startAudio(&e);
    sleep(8);
    stopAudio(&e);
    uninitAudio(&e);
}

#endif

// ---- Event rate processing tests ----

void test_iso_groups_single_control() {
    printf("test_iso_groups_single_control\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_iso1"));

    // One control feeds one outlet
    S c = control("freq", {20, 20000, 440, 0}, NumType::f32, 1);
    outlet(c);

    gSynth->graphAnalysis();

    // Should have exactly one iso-group containing the control's downstream tree
    assert(gSynth->isoGroups.size() == 1);
    IsoGroup* g = gSynth->isoGroups[0];
    assert(g->controls.size() == 1);
    assert(g->activates.empty());
}

void test_iso_groups_two_controls() {
    printf("test_iso_groups_two_controls\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_iso2"));

    // Two controls feed a shared expression
    S c1 = control("freq", {20, 20000, 440, 0}, NumType::f32, 1);
    S c2 = control("amp", {0, 1, 0.5, 0}, NumType::f32, 1);
    outlet(c1 * c2);

    gSynth->graphAnalysis();

    // Should have iso-groups: one per control, plus one for the multiply
    // c1 -> iso0 (controls={c1})
    // c2 -> iso1 (controls={c2})
    // c1*c2 -> iso2 (controls={c1,c2}), activated by iso0 and iso1
    assert(gSynth->isoGroups.size() >= 2);

    // The iso-group containing the multiply should have both controls
    bool found_merged = false;
    for (IsoGroup* g : gSynth->isoGroups) {
        if (g->controls.size() == 2) {
            found_merged = true;
        }
    }
    assert(found_merged);
}

void test_control_codegen() {
    printf("test_control_codegen\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_ctrl_cg"));

    S c = control("freq", {20, 20000, 440, 0}, NumType::f32, 1);
    outlet(c);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Should contain control read: p->controls[
    assert(code.find("p->controls[") != string::npos);
    // Should contain activation flag
    assert(code.find("ctrl0_active") != string::npos);
}

void test_event_fun_codegen() {
    printf("test_event_fun_codegen\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_evt_cg"));

    S c1 = control("freq", {20, 20000, 440, 0}, NumType::f32, 1);
    S c2 = control("amp", {0, 1, 0.5, 0}, NumType::f32, 1);
    outlet(c1 * c2);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Should contain switch on id in the event function
    assert(code.find("switch (id)") != string::npos);
    // Should contain memcpy for control data
    assert(code.find("memcpy") != string::npos);
}

void test_process_events_codegen() {
    printf("test_process_events_codegen\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_pe_cg"));

    S c = control("freq", {20, 20000, 440, 0}, NumType::f32, 1);
    outlet(c);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Should contain processEvents function with iso-group activation
    assert(code.find("processEvents") != string::npos);
    assert(code.find("iso0") != string::npos);
}

void test_multichannel_control_codegen() {
    printf("test_multichannel_control_codegen\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_mc_ctrl"));

    S c = control("pos", {-1, 1, 0, 0}, NumType::f32, 4);
    outlet(c);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Should contain array access for multichannel control
    assert(code.find("p->controls[") != string::npos);
}

void test_control_def_has_id() {
    printf("test_control_def_has_id\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_ctrl_id"));

    S c1 = control("freq", {20, 20000, 440, 0}, NumType::f32, 1);
    S c2 = control("amp", {0, 1, 0.5, 0}, NumType::f32, 1);
    outlet(c1 * c2);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // ControlDef should have the serial as id field
    // The format is: def.controls[N] = {"name", {type, rate, chans}, serial};
    auto c1_ctrl = c1.as<Control>();
    auto c2_ctrl = c2.as<Control>();
    string id0 = "}, " + std::to_string(c1_ctrl->serial) + "}";
    string id1 = "}, " + std::to_string(c2_ctrl->serial) + "}";
    assert(code.find(id0) != string::npos);
    assert(code.find(id1) != string::npos);
}

void test_voicer_codegen() {
    printf("test_voicer_codegen\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_voicer"));

    // Build a simple voicer: noteParam("freq") * gate(), 16 voices
    // No branching in body, so should use flat voice mode (SoA, no per-voice loop).
    Graph* subgraph = new Graph(gSynth, gGraph);
    S voiceBody;
    {
        PushGraph pg(subgraph);
        S freq = addExpr(new NoteParam({20, 20000, 440, 0}, NumType::f32, 1, "freq"));
        S gate = addExpr(new NoteParam({0, 1, 0, 0}, NumType::f32, 1, "gate"));
        S result = freq * gate;
        voiceBody = addExpr(new PhiNodeExpr(result));
    }

    S voicer = addExpr(new VoicerExpr(16, voiceBody));
    // Sum the 16 voices down to 1 channel
    outlet(reduce(voicer, BinaryOp::Add, 1));

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Flat voice mode: no VoiceState struct, no per-voice loop
    assert(code.find("VoiceState") == string::npos);
    assert(code.find("for (int v = 0; v < 16; ++v)") == string::npos);
    assert(code.find("vparams") == string::npos);

    // SoA voice state arrays
    assert(code.find("voice_v0[16]") != string::npos);
    assert(code.find("voice_v1[16]") != string::npos);

    // Flat loop with correct iteration count (16)
    assert(code.find("i < 16") != string::npos);

    // Voice-local inst vars present
    assert(code.find("voice_v0") != string::npos);
    assert(code.find("voice_v1") != string::npos);

    // SIMD should be used now (NoteParam is SIMD-eligible)
    assert(code.find("SIMD") != string::npos);
    assert(code.find("flat voice") != string::npos);

    // RowVoicer: 16 voices, 1 user param (gate is internal, not counted)
    assert(code.find("RowVoicer<16, 1>") != string::npos);
    assert(code.find("_noteOn") != string::npos);
    assert(code.find("_noteOff") != string::npos);
    assert(code.find("_allNotesOff") != string::npos);
    assert(code.find("tzpl_voicer.hpp") != string::npos);

    printf("  flat voice mode: SoA layout, flat loops, proper indexing\n");
}

void test_branching_voice_stays_looped() {
    printf("test_branching_voice_stays_looped\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_voicer_branching"));

    // Build a voicer with branching (IfElseExpr) in the body.
    // This should fall back to per-voice loop mode (AoS).
    Graph* voicerSubgraph = new Graph(gSynth, gGraph);
    S voiceBody;
    {
        PushGraph pg(voicerSubgraph);
        S freq = addExpr(new NoteParam({20, 20000, 440, 0}, NumType::f32, 1, "freq"));
        S gate = addExpr(new NoteParam({0, 1, 0, 0}, NumType::f32, 1, "gate"));

        // IfElseExpr: if (gate > 0.5) then freq else 0
        S cond = gate > S(0.5f);

        Graph* thenGraph = new Graph(gSynth, gGraph);
        S thenBody;
        {
            PushGraph pg2(thenGraph);
            thenBody = addExpr(new PhiNodeExpr(freq));
        }
        Graph* elseGraph = new Graph(gSynth, gGraph);
        S elseBody;
        {
            PushGraph pg3(elseGraph);
            elseBody = addExpr(new PhiNodeExpr(S(0.0f)));
        }
        S ifExpr = addExpr(new IfElseExpr(cond, thenBody, elseBody));

        voiceBody = addExpr(new PhiNodeExpr(ifExpr));
    }

    S voicer = addExpr(new VoicerExpr(8, voiceBody));
    outlet(reduce(voicer, BinaryOp::Add, 1));

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Branching body: should use AoS mode with per-voice loop
    assert(code.find("VoiceState") != string::npos);
    assert(code.find("for (int v = 0; v < 8; ++v)") != string::npos);

    // Should NOT have SoA flat arrays
    assert(code.find("voice_v0[8]") == string::npos);

    printf("  branching voice body falls back to per-voice loop\n");
}

void test_voicer_sexpr_parse() {
    printf("test_voicer_sexpr_parse\n");
    using namespace synthdef;

    // A simple voicer: NoteParam "freq", gate * freq in subgraph
    std::string sexprText = R"(
        (Synth test_voicer_sexpr
            (Graph 5 (
                (0 NoteParam "freq" 1 (ControlSpec 20.0 20000.0 440.0 0))
                (1 NoteParam "gate" 1 (ControlSpec 0.0 1.0 0.0 0))
                (2 BinaryOp mul (0 1))
                (3 Voicer 8
                    (Graph 2 (
                        (0 NoteParam "freq" 1 (ControlSpec 20.0 20000.0 440.0 0))
                        (1 NoteParam "gate" 1 (ControlSpec 0.0 1.0 0.0 0))
                        (2 BinaryOp mul (0 1)))))
                (5 Outlet "out" 3))))
    )";

    auto result = synthFromSExprText(sexprText);
    assert(result.has_value());

    Synth* synth = result.value();
    assert(synth->name == "test_voicer_sexpr");

    // Verify we got noteParams
    assert(!synth->noteParams.empty());

    {
        PushSynth ps(synth);
        synth->graphAnalysis();

        string code = cppCodeGen(synth);
        // No branching in body, so flat voice mode is used (no VoiceState)
        assert(code.find("VoiceState") == string::npos);
        assert(code.find("RowVoicer") != string::npos);
        assert(code.find("_noteOn") != string::npos);
    }

    printf("  sexpr parse and codegen succeeded\n");
}

void test_switch_codegen() {
    printf("test_switch_codegen\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_switch_codegen"));

    // Build a switch expression with 3 cases
    S selector = addExpr(new Control({0, 2, 0, 0}, NumType::i32, 1, "sel"));

    Graph* case0Graph = new Graph(gSynth, gGraph);
    S case0Body;
    {
        PushGraph pg(case0Graph);
        case0Body = addExpr(new PhiNodeExpr(S(100.0f)));
    }
    Graph* case1Graph = new Graph(gSynth, gGraph);
    S case1Body;
    {
        PushGraph pg(case1Graph);
        case1Body = addExpr(new PhiNodeExpr(S(200.0f)));
    }
    Graph* case2Graph = new Graph(gSynth, gGraph);
    S case2Body;
    {
        PushGraph pg(case2Graph);
        case2Body = addExpr(new PhiNodeExpr(S(300.0f)));
    }

    S sw = addExpr(new SwitchExpr(selector, {case0Body, case1Body, case2Body}));
    outlet(sw);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Verify switch codegen
    assert(code.find("switch(") != string::npos);
    assert(code.find("case 0:") != string::npos);
    assert(code.find("case 1:") != string::npos);
    assert(code.find("case 2:") != string::npos);
    assert(code.find("break;") != string::npos);
    // Bounds check with std::min
    assert(code.find("std::min(u32(") != string::npos);

    printf("  switch codegen produces correct C++ switch statement\n");
}

void test_for_loop_codegen() {
    printf("test_for_loop_codegen\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_for_loop_codegen"));

    // Build a for loop: for_(3, body)
    S count = S(3);

    Graph* bodyGraph = new Graph(gSynth, gGraph);
    S body;
    {
        PushGraph pg(bodyGraph);
        S i = addExpr(new VarExpr("i", NumType::any_int));
        // Use the loop variable so it's not dead code
        S result = i + S(1);
        body = addExpr(new PhiNodeExpr(result));
    }

    S loop = addExpr(new ForLoopExpr(count, body));
    outlet(loop);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Verify for loop codegen
    assert(code.find("for (i32 i = 0; i <") != string::npos);
    assert(code.find("++i)") != string::npos);

    printf("  for loop codegen produces correct C++ for loop\n");
}

void test_for_loop_codegen_dynamic_count() {
    printf("test_for_loop_codegen_dynamic_count\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_for_loop_dyn"));

    // Build a for loop with dynamic count from a control
    S count = addExpr(new Control({1, 8, 4, 0}, NumType::i32, 1, "count"));

    Graph* bodyGraph = new Graph(gSynth, gGraph);
    S body;
    {
        PushGraph pg(bodyGraph);
        S i = addExpr(new VarExpr("i", NumType::any_int));
        body = addExpr(new PhiNodeExpr(S(1.0f)));
    }

    S loop = addExpr(new ForLoopExpr(count, body));
    outlet(loop);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Verify for loop codegen with dynamic count references the control
    assert(code.find("for (i32 i = 0; i <") != string::npos);

    printf("  for loop with dynamic count codegen works\n");
}

void test_switch_sexpr_parse() {
    printf("test_switch_sexpr_parse\n");
    using namespace synthdef;

    std::string sexprText = R"(
        (Synth test_switch_sexpr
            (Graph 4 (
                (0 Control "sel" 1 (ControlSpec 0 2 0 0))
                (4 SwitchExpr (0)
                    (Graph 1 (
                        (1 Constant 1 12 (100.0))))
                    (Graph 2 (
                        (2 Constant 1 12 (200.0))))
                    (Graph 3 (
                        (3 Constant 1 12 (300.0)))))
                (5 Outlet "out" 4))))
    )";

    auto result = synthFromSExprText(sexprText);
    assert(result.has_value());

    Synth* synth = result.value();
    assert(synth->name == "test_switch_sexpr");

    {
        PushSynth ps(synth);
        synth->graphAnalysis();
        string code = cppCodeGen(synth);

        assert(code.find("switch(") != string::npos);
        assert(code.find("case 0:") != string::npos);
        assert(code.find("case 1:") != string::npos);
        assert(code.find("case 2:") != string::npos);
    }

    printf("  switch sexpr parse and codegen succeeded\n");
}

void test_for_loop_sexpr_parse() {
    printf("test_for_loop_sexpr_parse\n");
    using namespace synthdef;

    std::string sexprText = R"(
        (Synth test_for_sexpr
            (Graph 4 (
                (0 Constant 1 8 (4))
                (4 ForExpr (0)
                    (Graph 3 (
                        (1 VarExpr "i")
                        (2 Constant 1 8 (1))
                        (3 BinaryOp + (1 2)))))
                (5 Outlet "out" 4))))
    )";

    auto result = synthFromSExprText(sexprText);
    assert(result.has_value());

    Synth* synth = result.value();
    assert(synth->name == "test_for_sexpr");

    {
        PushSynth ps(synth);
        synth->graphAnalysis();
        string code = cppCodeGen(synth);

        assert(code.find("for (i32 i = 0; i <") != string::npos);
    }

    printf("  for loop sexpr parse and codegen succeeded\n");
}

void test_spectral_chain_codegen() {
    printf("test_spectral_chain_codegen\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_spectral"));

    // Build a spectral chain: FFT -> identity (pass through) -> IFFT
    S input = inlet(NumType::f32, 1, "in");
    S result = spectral_chain(input, 256, 128, [](S frame) {
        // Identity: just pass the spectrum through
        return frame;
    });
    outlet(result);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Verify spectral chain codegen produces expected structures
    assert(code.find("tzpl_fft.hpp") != string::npos);
    assert(code.find("JscsFFTSetup*") != string::npos);
    assert(code.find("spec") != string::npos);
    assert(code.find("_fftsetup") != string::npos);
    assert(code.find("_inbuf") != string::npos);
    assert(code.find("_outbuf") != string::npos);
    assert(code.find("_window") != string::npos);
    assert(code.find("_hopcount") != string::npos);
    assert(code.find("tzpl_fft_create(256)") != string::npos);
    assert(code.find("tzpl_fft_forward") != string::npos);
    assert(code.find("tzpl_fft_inverse") != string::npos);
    assert(code.find("tzpl_window_sqrt_hann") != string::npos);

    printf("  spectral chain codegen produces correct C++ code\n");
}

void test_spectral_chain_multichannel_codegen() {
    printf("test_spectral_chain_multichannel_codegen\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_spectral_mc"));

    // Build a stereo spectral chain
    S input = inlet(NumType::f32, 2, "in");
    S result = spectral_chain(input, 512, 256, [](S frame) {
        // Scale the spectrum by 0.5
        return frame * S(0.5f);
    });
    outlet(result);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Verify stereo handling (2 channel buffers)
    assert(code.find("_inbuf[2]") != string::npos);
    assert(code.find("_outbuf[2]") != string::npos);

    printf("  multichannel spectral chain codegen works\n");
}

void test_spectral_chain_sexpr_parse() {
    printf("test_spectral_chain_sexpr_parse\n");
    using namespace synthdef;

    std::string sexprText = R"(
        (Synth test_spectral_sexpr
            (Graph 3 (
                (0 Inlet "in" 12 1)
                (3 SpectralChainExpr (0) 256 128
                    (Graph 2 (
                        (1 SpectralFrameInput 256)
                        (2 BinaryOp mul (1 1)))))
                (4 Outlet "out" 3))))
    )";

    auto result = synthFromSExprText(sexprText);
    assert(result.has_value());

    Synth* synth = result.value();
    assert(synth->name == "test_spectral_sexpr");

    {
        PushSynth ps(synth);
        synth->graphAnalysis();
        string code = cppCodeGen(synth);

        assert(code.find("tzpl_fft_forward") != string::npos);
        assert(code.find("tzpl_fft_inverse") != string::npos);
    }

    printf("  spectral chain sexpr parse and codegen succeeded\n");
}

extern void test_transforms();

void test_simd_codegen_stereo() {
    printf("test_simd_codegen_stereo\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_simd_stereo"));

    // Simple stereo arithmetic: all SIMD-eligible ops
    S a = control("a", {0, 1, 0.5, 0}, NumType::f32, 2);
    S b = control("b", {0, 1, 0.3, 0}, NumType::f32, 2);
    S sig = sin(a) * b + S(0.1f);
    outlet(sig);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth, 4, 2); // minSimdWidth=2 to enable 2-channel SIMD
    string code_default = cppCodeGen(gSynth); // default minSimdWidth=4 skips 2-channel

    // Should contain SIMD 2x when enabled
    assert(code.find("SIMD 2x") != string::npos);
    // Should NOT contain SIMD 2x with default settings
    assert(code_default.find("SIMD 2x") == string::npos);
}

void test_simd_codegen_quad() {
    printf("test_simd_codegen_quad\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_simd_quad"));

    // 4-channel arithmetic
    S a = control("a", {0, 1, 0.5, 0}, NumType::f32, 4);
    S sig = sin(a) * S(0.25f);
    outlet(sig);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Should contain SIMD 4x comment
    assert(code.find("SIMD 4x") != string::npos);
}

void test_simd_codegen_with_delay() {
    printf("test_simd_codegen_with_delay\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_simd_delay"));

    // Stereo oscillator: phasor uses a 1-sample delay
    S sig = fsinosc(vec(440, 550)) * S(0.1f);
    outlet(sig);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth, 4, 2); // minSimdWidth=2 to enable 2-channel SIMD

    // Should contain SIMD 2x -- delays should not disqualify
    assert(code.find("SIMD 2x") != string::npos);
    // Should contain delay gather pattern
    assert(code.find("p->d") != string::npos);
}

void test_simd_codegen_with_comb() {
    printf("test_simd_codegen_with_comb\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_simd_comb"));

    // Stereo comb filter: ring buffer delay
    S sig = fsinosc(vec(440, 550));
    sig = combn(sig, .01, 2);
    outlet(sig * S(0.1f));

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth, 4, 2); // minSimdWidth=2 to enable 2-channel SIMD

    // Should contain SIMD -- comb uses ring buffer delays
    assert(code.find("SIMD 2x") != string::npos);
}

void test_simd_flat_voice_codegen() {
    printf("test_simd_flat_voice_codegen\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_simd_flat_voice"));

    // 8 voices, each 1-chan: freq * gate
    // totalCount = 8 * 1 = 8, SIMD width 4
    Graph* subgraph = new Graph(gSynth, gGraph);
    S voiceBody;
    {
        PushGraph pg(subgraph);
        S freq = addExpr(new NoteParam({20, 20000, 440, 0}, NumType::f32, 1, "freq"));
        S gate = addExpr(new NoteParam({0, 1, 0, 0}, NumType::f32, 1, "gate"));
        S result = freq * gate;
        voiceBody = addExpr(new PhiNodeExpr(result));
    }
    S voicer = addExpr(new VoicerExpr(8, voiceBody));
    outlet(reduce(voicer, BinaryOp::Add, 1));

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Should use SIMD in flat voice mode
    assert(code.find("SIMD") != string::npos);
    assert(code.find("flat voice") != string::npos);

    // Should have voicer_params gather for NoteParam
    assert(code.find("voicer_params") != string::npos);

    // Should still be flat voice mode (no VoiceState)
    assert(code.find("VoiceState") == string::npos);

    printf("  SIMD flat voice mode: NoteParam gather, SIMD flat loops\n");
}

void test_simd_flat_voice_stereo_osc() {
    printf("test_simd_flat_voice_stereo_osc\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_simd_fv_osc"));

    // 4 voices, each 2-chan (stereo) sine oscillator
    // totalCount = 4 * 2 = 8, SIMD width 4
    // Each 4-wide vector spans 2 voices (2 chans each)
    Graph* subgraph = new Graph(gSynth, gGraph);
    S voiceBody;
    {
        PushGraph pg(subgraph);
        S freq = addExpr(new NoteParam({20, 20000, 440, 0}, NumType::f32, 2, "freq"));
        S sig = fsinosc(freq);
        voiceBody = addExpr(new PhiNodeExpr(sig));
    }
    S voicer = addExpr(new VoicerExpr(4, voiceBody));
    outlet(reduce(voicer, BinaryOp::Add, 2));

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Should use SIMD in flat voice mode with delays
    assert(code.find("SIMD") != string::npos);
    assert(code.find("flat voice") != string::npos);
    // Voice-local delay should be present
    assert(code.find("voice_d") != string::npos);

    printf("  SIMD flat voice stereo osc: delays + NoteParam gather\n");
}

// ---------------------------------------------------------------------------
// Delay interpolation tests
// ---------------------------------------------------------------------------

void test_delay_interp_none() {
    printf("test_delay_interp_none\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_delay_interp_none"));

    S sig = fsinosc(S(440.0));
    S dt = S(0.01) * fs();
    D y(dt);
    S out = y = sig + y.v(dt, interpNone) * S(0.5);
    outlet(out);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // interpNone should NOT call tzpl_delay_* functions (but header include is OK)
    assert(code.find("tzpl_delay_none(") == string::npos);
    assert(code.find("tzpl_delay_linear(") == string::npos);
    // The variable delay (d1) should have direct buffer access, not interpolation calls
    assert(code.find("_wrpos") != string::npos || code.find("d1[") != string::npos);
    printf("  delay interpNone generates direct buffer access\n");
}

void test_delay_interp_linear() {
    printf("test_delay_interp_linear\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_delay_interp_linear"));

    S sig = fsinosc(S(440.0));
    S dt = S(0.01) * fs();
    D y(S(0.02) * fs());
    S out = y = sig + y.v(dt, interpLinear) * S(0.5);
    outlet(out);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    assert(code.find("tzpl_delay_linear") != string::npos);
    printf("  delay interpLinear generates tzpl_delay_linear call\n");
}

void test_delay_interp_cubic() {
    printf("test_delay_interp_cubic\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_delay_interp_cubic"));

    S sig = fsinosc(S(440.0));
    S dt = S(0.01) * fs();
    D y(S(0.02) * fs());
    S out = y = sig + y.v(dt, interpCubic) * S(0.5);
    outlet(out);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    assert(code.find("tzpl_delay_cubic") != string::npos);
    printf("  delay interpCubic generates tzpl_delay_cubic call\n");
}

void test_delay_interp_lagrange() {
    printf("test_delay_interp_lagrange\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_delay_interp_lagrange"));

    S sig = fsinosc(S(440.0));
    S dt = S(0.01) * fs();
    D y(S(0.02) * fs());
    S out = y = sig + y.v(dt, interpLagrange) * S(0.5);
    outlet(out);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    assert(code.find("tzpl_delay_lagrange") != string::npos);
    printf("  delay interpLagrange generates tzpl_delay_lagrange call\n");
}

void test_delay_interp_sinc() {
    printf("test_delay_interp_sinc\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_delay_interp_sinc"));

    S sig = fsinosc(S(440.0));
    S dt = S(0.01) * fs();
    D y(S(0.02) * fs());
    S out = y = sig + y.v(dt, interpSinc) * S(0.5);
    outlet(out);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    assert(code.find("tzpl_delay_sinc") != string::npos);
    printf("  delay interpSinc generates tzpl_delay_sinc call\n");
}

void test_delay_interp_simd() {
    printf("test_delay_interp_simd\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_delay_interp_simd"));

    // Stereo variable-delay with cubic interpolation
    S sig = fsinosc(vec(440, 550));
    S dt = S(0.01) * fs();
    D y(S(0.02) * fs());
    S out = y = sig + y.v(dt, interpCubic) * S(0.5);
    outlet(out);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth, 4, 2);

    // SIMD interp should use the kernel function via lambda
    assert(code.find("tzpl_interp_cubic") != string::npos);
    assert(code.find("SIMD 2x") != string::npos);
    printf("  delay SIMD interpolation uses tzpl_interp_cubic kernel\n");
}

// ---------------------------------------------------------------------------
// Buffer tests
// ---------------------------------------------------------------------------

void test_buf_fix_read() {
    printf("test_buf_fix_read\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_buf_fix_read"));

    B buf;
    S out = buf.read(42, 1, 0);
    outlet(out);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Should have a buffer pointer in the struct
    assert(code.find("tzpl_Buffer* buf0") != string::npos);
    // Should have null-safe read
    assert(code.find("p->buf0") != string::npos);
    // Init should set to nullptr
    assert(code.find("buf0 = nullptr") != string::npos);
    printf("  BufFixRead generates null-safe buffer read\n");
}

void test_buf_var_read_interp() {
    printf("test_buf_var_read_interp\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_buf_var_read_interp"));

    B buf;
    S index = fsinosc(S(1.0)) * S(100.0);
    S out = buf.vread(index, interpCubic, 1, 0);
    outlet(out);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    assert(code.find("tzpl_buf_cubic") != string::npos);
    printf("  BufVarRead with cubic interpolation generates tzpl_buf_cubic\n");
}

void test_buf_var_read_none() {
    printf("test_buf_var_read_none\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_buf_var_read_none"));

    B buf;
    S index = fsinosc(S(1.0)) * S(100.0);
    S out = buf.vread(index, interpNone, 1, 0);
    outlet(out);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // interpNone should NOT call tzpl_buf_* interpolation functions
    assert(code.find("tzpl_buf_none(") == string::npos);
    assert(code.find("tzpl_buf_linear(") == string::npos);
    // Should have direct buffer access with mask
    assert(code.find("buf0->mask") != string::npos);
    printf("  BufVarRead interpNone generates direct buffer access\n");
}

void test_buf_write() {
    printf("test_buf_write\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_buf_write"));

    B buf;
    S sig = fsinosc(S(440.0));
    S index = S(0.0);
    buf.write(sig, index);
    outlet(sig);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Write should be null-guarded
    assert(code.find("if (p->buf0)") != string::npos);
    assert(code.find("buf0->data") != string::npos);
    printf("  BufWrite generates null-guarded write\n");
}

void test_buf_length() {
    printf("test_buf_length\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_buf_length"));

    B buf;
    S len = buf.length();
    // Use len in a context that accepts f64 (length returns f64)
    S index = len * S(0.5);
    S out = buf.vread(index, interpNone, 1, 0);
    outlet(out);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    assert(code.find("buf0->length") != string::npos);
    printf("  BufLength generates buffer length access\n");
}

void test_buf_swap_buffer() {
    printf("test_buf_swap_buffer\n");
    using namespace synthdef;
    PushSynth ps(new Synth("test_buf_swap"));

    B buf0, buf1;
    S out = buf0.read(0) + buf1.read(0);
    outlet(out);

    gSynth->graphAnalysis();
    string code = cppCodeGen(gSynth);

    // Should generate swapBuffer function with cases for both buffers
    assert(code.find("swapBuffer") != string::npos);
    assert(code.find("case 0:") != string::npos);
    assert(code.find("case 1:") != string::npos);
    printf("  swapBuffer generated with cases for all buffers\n");
}

void test_buf_sexpr_parse() {
    printf("test_buf_sexpr_parse\n");
    using namespace synthdef;

    // Test parsing BufVarRead with interpolation from s-expression
    std::string sexprText = R"(
        ((0 Constant 1 12 (1.5))
         (1 BufVarRead 0 "cubic" 1 0 (0))
         (2 Outlet "out" 1))
    )";

    auto result = synthFromSExprText(sexprText, "test_buf_sexpr");
    if (!result.has_value()) {
        printf("  sexpr parse error: %s\n", result.error().c_str());
    }
    assert(result.has_value());

    Synth* synth = result.value();
    {
        PushSynth ps(synth);
        synth->graphAnalysis();
        string code = cppCodeGen(gSynth);

        // Should have buffer struct field
        assert(code.find("tzpl_Buffer* buf0") != string::npos);
        // Should have buffer data access with cubic interpolation
        assert(code.find("tzpl_buf_cubic") != string::npos);
        // Should have swapBuffer
        assert(code.find("swapBuffer") != string::npos);
    }
    printf("  buffer s-expression parsing produces correct codegen\n");
}

void all_tests() {
    using namespace synthdef;

    test_rates();
    test_num_type();
//    test_constant_vector();
//    test_constant_vec_ops();
//    test_constant_matrix_math();
    test_rewriting();
//    bubbles_test();
//    pause_bubbles_test();
    test_ftos();
    //test_transforms();
    //test_synthdef();

    // Delay interpolation tests
    test_delay_interp_none();
    test_delay_interp_linear();
    test_delay_interp_cubic();
    test_delay_interp_lagrange();
    test_delay_interp_sinc();
    test_delay_interp_simd();

    // Buffer tests
    test_buf_fix_read();
    test_buf_var_read_interp();
    test_buf_var_read_none();
    test_buf_write();
    test_buf_length();
    test_buf_swap_buffer();
    test_buf_sexpr_parse();

    // Control flow codegen tests
    test_switch_codegen();
    test_for_loop_codegen();
    test_for_loop_codegen_dynamic_count();
    test_switch_sexpr_parse();
    test_for_loop_sexpr_parse();

    // SIMD codegen tests
    test_simd_codegen_stereo();
    test_simd_codegen_quad();
    test_simd_codegen_with_delay();
    test_simd_codegen_with_comb();
    test_simd_flat_voice_codegen();
    test_simd_flat_voice_stereo_osc();

    // Voicer tests (assertion-based, run before compilation tests that may exit)
    test_voicer_codegen();
    test_branching_voice_stays_looped();
    test_voicer_sexpr_parse();

    // Spectral chain tests
    test_spectral_chain_codegen();
    test_spectral_chain_multichannel_codegen();
    test_spectral_chain_sexpr_parse();

    // SIMD compilation tests (generate, compile and link)
    test("simd_stereo_sin", 2, [](){
        // Stereo sine: SIMD-eligible, should use f32x2 or f64x2
        S a = control("freq", {20, 20000, 440, 0}, NumType::f32, 2);
        S sig = sin(a) * S(0.1f);
        outlet(sig);
    });
    test("simd_quad_mul", 2, [](){
        // 4-channel multiply: should use f32x4 or f64x4
        S a = control("a", {0, 1, 0.5, 0}, NumType::f32, 4);
        S b = control("b", {0, 1, 0.3, 0}, NumType::f32, 4);
        outlet(a * b);
    });

    test("simd_stereo_osc", 2, [](){
        // Stereo oscillator using phasor (has delay) -- tests SIMD delay codegen
        S sig = fsinosc(vec(440, 550)) * S(0.1f);
        outlet(sig);
    });

    // Spectral chain compilation tests (compiles and links the generated plugin)
    test("spectral_identity", 2, [](){
        // White noise through an identity spectral chain
        S noise = birand(1) * S(0.1f);
        S result = spectral_chain(noise, 256, 128, [](S frame) {
            return frame; // pass-through
        });
        outlet(result);
    });
    test("spectral_scale", 2, [](){
        // Stereo noise through a spectral scaling chain
        S noise = birand(2) * S(0.1f);
        S result = spectral_chain(noise, 512, 256, [](S frame) {
            return frame * S(0.5f);
        });
        outlet(result);
    });

    test("bubbles", 5, bubbles);
//    test("bubbles_lite", 5, bubbles_lite);
//    test("no_reduce_test", 8, no_reduce_test);
//    test("stutter_test", 12, stutter_test);


//    test("permute_test", 12, permute_test);
//    test("rotate_test", 12, rotate_test);
//    test("skip_test", 12, skip_test);
//    test("take_test", 12, take_test);
//    test("stride_test", 12, stride_test);
//    test("reverse_test", 12, reverse_test);
//    test("reverse_rows_test", 12, reverse_rows_test);
//    test("no_reverse_test", 12, no_reverse_test);

    test("init_urand_test", 2, init_urand_test);
    test("select_test", 2, select_test);
    test("switch_test", 2, switch_test);
    test("pause_bubbles", 2, pause_bubbles);
    test("dustone", 2, dustone);
    test("dust1", 2, dust1);
    test("sahtone1", 2, sahtone1);
    test("sahtone2", 2, sahtone2);
    test("violet_tremolo", 2, violet_tremolo);
    test("white_test", 2, [](){ outlet(white(2) * .2); });
    test("red_test", 2, [](){ outlet(red(2) * .2); });
    test("pink_test", 2, [](){ outlet(pink(2) * .2); });
    test("pinke_test", 2, [](){ outlet(pinke(2) * .2); });
    test("blue_test", 2, [](){ outlet(blue(2) * .2); });
    test("violet_test", 2, [](){ outlet(violet(2) * .2); });
    test("tog_pause", 2, tog_pause);
    test("pch_seq", 2, pch_seq);
    test("pull_nested", 2, pull_nested);
    test("pulltwo", 2, pulltwo);
    test("mod1_test", 2, mod1_test);
    test("mod4_test", 2, mod4_test);
    test("mod5_test", 2, mod5_test);

    // Test s-expression integration
    test_sexpr_integration();

    // Event rate processing tests
    test_iso_groups_single_control();
    test_iso_groups_two_controls();
    test_control_codegen();
    test_event_fun_codegen();
    test_process_events_codegen();
    test_multichannel_control_codegen();
    test_control_def_has_id();

#if 0
//    test("init_urand_test", 20, init_urand_test);
//    test("init_urand_test", 20, init_urand_test);
//    test("init_urand_test", 20, init_urand_test);
//    test("reduce_test", 12, reduce_test);
//    test("bubbles", 8, bubbles);
//
//    xxxtest("sincos", 8, [](){ );
#endif
    printf("all tests passed\n");
}
