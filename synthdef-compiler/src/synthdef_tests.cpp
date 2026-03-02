#include "jscs_plugin_abi.h"
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
    return jscs_errNone;
}

int test_bubbles_uninit(test_bubbles* p) {
    // FIXME genUninitFun
    free(p->d4[0]); p->d4[0] = nullptr;
    free(p->d4[1]); p->d4[1] = nullptr;
    return jscs_errNone;
}

int test_bubbles_reset(test_bubbles* p) {
    // FIXME genResetFun
    return jscs_errNone;
}

int test_bubbles_event(test_bubbles* p, u64 paramID, usize rows, usize cols, f64* values) {
    // FIXME genEventFun
    return jscs_errNone;
}

int test_bubbles_process_events(test_bubbles* p) {
    // FIXME genHandleEventsFun
    return jscs_errNone;
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

    return jscs_errNone;
}

SynthFuns test_bubbles_funs = {
    .alloc = (SynthData* (*)())test_bubbles_alloc,
    .free = (int (*)(SynthData*))test_bubbles_free,
    .init = (int (*)(SynthData*))test_bubbles_init,
    .uninit = (int (*)(SynthData*))test_bubbles_uninit,
    .reset = (int (*)(SynthData*))test_bubbles_reset,
    .event = (int (*)(SynthData*, u64, jscs_Slice, jscs_Slice))test_bubbles_event,
    .process_events = (int (*)(SynthData*))test_bubbles_process_events,
    .tick = (int (*)(SynthData*))test_bubbles_tick
};

extern "C" jscs_SynthDef load() {
    jscs_SynthDef def;
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
    
    jscs_SynthDef def = load();
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

extern void test_transforms();

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

    test("init_urand_test", 20, init_urand_test);
    test("select_test", 12, select_test);
    test("switch_test", 12, switch_test);
    test("pause_bubbles", 10, pause_bubbles);
    test("dustone", 8, dustone);
    test("dust1", 8, dust1);
    test("sahtone1", 8, sahtone1);
    test("sahtone2", 8, sahtone2);
    test("violet_tremolo", 8, violet_tremolo);
    test("white_test", 3, [](){ outlet(white(2) * .2); });
    test("red_test", 12, [](){ outlet(red(2) * .2); });
    test("pink_test", 3, [](){ outlet(pink(2) * .2); });
    test("pinke_test", 3, [](){ outlet(pinke(2) * .2); });
    test("blue_test", 3, [](){ outlet(blue(2) * .2); });
    test("violet_test", 3, [](){ outlet(violet(2) * .2); });
    test("tog_pause", 8, tog_pause);
    test("pch_seq", 8, pch_seq);
    test("pull_nested", 8, pull_nested);
    test("pulltwo", 8, pulltwo);
    test("mod1_test", 8, mod1_test);
    test("mod4_test", 8, mod4_test);
    test("mod5_test", 8, mod5_test);

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
