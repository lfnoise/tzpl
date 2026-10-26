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
//  test_block_engine.cpp
//
//  The block engine (devplans/BLOCK_ENGINE_PLAN.md §3.7), driven offline
//  with hand-written v3 test plugins. Every check compares sample by sample
//  against a closed form. Sample rate 1000 Hz at 60 BPM, so beat b on clock
//  0 is sample 1000 b and fractional event times are easy to state.
//
//  Usage: test_block_engine [--verbose]
//

#include "tzpl_client_interface.hpp"
#include "tzpl_engine.hpp"
#include "tzpl_node.hpp"
#include "tzpl_test_plugins.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <print>
#include <string>
#include <vector>

using namespace engine;

namespace {

int gFailures = 0;
int gChecks = 0;
bool gVerbose = false;
std::string gCase;

void fail(std::string const& msg) {
    ++gFailures;
    std::println("FAIL [{}] {}", gCase, msg);
}

void check(bool ok, std::string const& msg) {
    ++gChecks;
    if (!ok) fail(msg);
}

// Compare a recording against expected(k) for k in [from, to).
void checkSeries(std::vector<f32> const& got, int from, int to,
                 std::function<f64(int)> const& expected, f64 tol, std::string const& what) {
    ++gChecks;
    if ((int)got.size() < to) {
        fail(std::format("{}: only {} samples recorded, need {}", what, got.size(), to));
        return;
    }
    int bad = 0;
    for (int k = from; k < to; ++k) {
        f64 want = expected(k);
        if (!(std::fabs(got[k] - want) <= tol)) {
            if (bad < 5) fail(std::format("{}: sample {} = {} want {}", what, k, got[k], want));
            ++bad;
        }
    }
    if (bad > 5) fail(std::format("{}: {} bad samples in all", what, bad));
}

struct Rig {
    Engine* e = nullptr;
    int frames;
    int chans;
    i64 rendered = 0;
    std::vector<f32> out; // interleaved device output

    Rig(int blockSize, int deviceFrames, int numSilos = 1, int numChans = 2)
        : frames(deviceFrames), chans(numChans)
    {
        EngineConfig cfg;
        cfg.numSilos = numSilos;
        cfg.blockSize = blockSize;
        AudioStreamParameters asp{};
        asp.deviceName = "nrt";
        asp.channels = numChans;
        asp.bufferFrames = deviceFrames;
        asp.sampleRate = 1000.;
        e = newEngineNRT(cfg, asp);
        safetyLimiter(e, kOff);
        recordings.clear();
        vtestResets.clear();
        createCounterNode(e);
        createVoicedTestNode(e);
    }
    ~Rig() { freeEngine(e); }

    // Render whole device buffers until at least `total` samples exist.
    void renderTo(i64 total) {
        std::vector<f32> buf((size_t)frames * chans);
        while (rendered < total) {
            renderNRTBlock(e, buf.data());
            out.insert(out.end(), buf.begin(), buf.end());
            rendered += frames;
        }
    }

    f32 outAt(int frame, int ch) const { return out[(size_t)frame * chans + ch]; }

    tzpl_SErr now(std::function<void()> const& build, int silo = 0) {
        begin(e);
        build();
        return go(silo);
    }
    // A bundle at fractional sample time t (beat t / 1000 on clock 0).
    tzpl_SErr at(f64 t, std::function<void()> const& build, int silo = 0) {
        begin(e);
        build();
        return sched(silo, 0, t / 1000., schedBetterLateThanNever);
    }
};

std::vector<f32> const& rec(char const* name, int ch = 0) {
    static std::vector<f32> empty;
    auto it = recordings.find(name);
    if (it == recordings.end() || ch >= (int)it->second.size()) return empty;
    return it->second[ch];
}

// The Audio Out port, and a rec node pulled into the graph through it.
constexpr PortAddr kOut{0, 0};

void addRec(Rig& r, char const* name, tzpl_Shape shape, i64 id) {
    createRecNode(r.e, name, shape);
    r.now([&] {
        newNode(name, id);
        connect({id, 0}, kOut);
    });
}

void addPass(Rig& r, char const* name, tzpl_Shape shape, i64 id) {
    createPassNode(r.e, name, shape);
    r.now([&] { newNode(name, id); });
}

void setAll(i64 id, int port, std::vector<f64> const& v, f64 ramp = 0.,
            FadeCurve curve = fadeLinear) {
    setInput(PortAddr{id, port}, Region::all(), std::span<f64 const>(v), ramp, curve);
}

// ---------------------------------------------------------------------------

// tzpl_Shape carries its rank: a 1 x 3 matrix and a row of 3 share dims but
// are different shapes.
void testShapes() {
    gCase = "shapes";
    check(tzpl::rank(tzpl::shapeScalar()) == 0, "scalar rank 0");
    check(tzpl::rank(tzpl::shapeRow(3)) == 1, "row rank 1");
    check(tzpl::rank(tzpl::shapeMatrix(1, 3)) == 2, "1x3 matrix rank 2");
    check(tzpl::rank(tzpl::shapeCuboid(2, 1, 1)) == 3, "cuboid rank 3");
    check(!(tzpl::shapeMatrix(1, 3) == tzpl::shapeRow(3)), "1x3 matrix != row 3");
    check(!(tzpl::shapeRow(1) == tzpl::shapeScalar()), "row 1 != scalar");
    check(tzpl::shapeChans(1) == tzpl::shapeScalar(), "one channel by count is a scalar");
    check(tzpl::broadcast(tzpl::shapeRow(3), tzpl::shapeMatrix(2, 1)) == tzpl::shapeMatrix(2, 3),
          "broadcast takes the larger rank");
    check(tzpl::shapeString(tzpl::shapeMatrix(1, 3)) == "1x3", "1x3 spelled at its rank");
    check(tzpl::validShape(tzpl::shapeMatrix(2, 3)), "2x3 valid");
    check(!tzpl::validShape(tzpl_Shape{{1, 1, 2, 3}, 1}), "dim outside the rank refused");
    check(!tzpl::validShape(tzpl_Shape{{1, 1, 1, 1}, 5}), "rank over the maximum refused");
}

// Every audio thread flushes subnormals to zero (M9): here the NRT renderer,
// which runs silo 0 on the calling thread.
void testDenormals() {
    gCase = "denormals";
    volatile float a = 1e-30f, b = 1e-10f;
    check(a * b != 0.f, "subnormal results before any block (default FP mode)");
    Rig r(64, 64);
    r.renderTo(64);
    check(a * b == 0.f, "subnormal results flush to zero on an audio thread");
}

void testBlockSizes() {
    for (int block : {1, 4, 64, 256}) {
        for (int dev : {37, 64, 100}) {
            gCase = std::format("block {} device {}", block, dev);
            Rig r(block, dev);
            addRec(r, "rec", tzpl::shapeRow(1), 10);
            r.now([&] { newNode("counter", 11); connect({11, 0}, {10, 0}); });
            r.renderTo(700);
            checkSeries(rec("rec"), 0, 600, [](int k) { return k; }, 0., "counter");
            // The rec node is on Audio Out (fan-in of one), output 0.
            bool zero = true;
            for (f32 x : r.out) zero = zero && x == 0.f;
            check(zero, "Audio Out carries the rec node's zero output");
        }
    }
}

void testStartFree() {
    for (int block : {4, 64, 256}) {
        gCase = std::format("start/free block {}", block);
        Rig r(block, 100);
        addRec(r, "rec", tzpl::shapeRow(1), 10);
        // Starts at 17.3 -> first sample 18; freed at 150 -> zero from 150.
        r.at(17.3, [&] { newNode("counter", 11); connect({11, 0}, {10, 0}); });
        r.at(150., [&] { freeNode(11); });
        r.renderTo(400);
        checkSeries(rec("rec"), 0, 400, [](int k) {
            return k < 18 || k >= 150 ? 0. : (f64)(k - 18);
        }, 0., "counter start 17.3, free 150");
    }
}

void testStages() {
    for (int block : {1, 64, 256}) {
        gCase = std::format("stages block {}", block);
        Rig r(block, 100);
        addRec(r, "rec", tzpl::shapeRow(4), 10);
        addPass(r, "p4", tzpl::shapeRow(4), 11);
        r.now([&] { connect({11, 0}, {10, 0}); setAll(11, 0, {1.}); });
        // ch0: step to 2 at 30.
        r.at(30., [&] { setInput(PortAddr{11, 0}, Region::element(0), std::vector<f64>{2.}); });
        // ch1: ramp 1 -> 5 starting at 10.5 over 0.04 s (40 samples).
        r.at(10.5, [&] { setInput(PortAddr{11, 0}, Region::element(1), std::vector<f64>{5.}, 0.04); });
        // ch2: impulse 9 at 40.
        r.at(40., [&] { impulseInput(PortAddr{11, 0}, Region::element(2), std::vector<f64>{9.}); });
        // ch3: ramp 1 -> 3 at 20, interrupted at 40 by a ramp to 0 over 20.
        r.at(20., [&] { setInput(PortAddr{11, 0}, Region::element(3), std::vector<f64>{3.}, 0.04); });
        r.at(40., [&] { setInput(PortAddr{11, 0}, Region::element(3), std::vector<f64>{0.}, 0.02); });
        r.renderTo(400);
        checkSeries(rec("rec", 0), 0, 400, [](int k) { return k < 30 ? 1. : 2.; }, 0., "step at 30");
        checkSeries(rec("rec", 1), 0, 400, [](int k) {
            return 1. + 4. * fadePosition(k, 10.5, 40);
        }, 1e-6, "ramp at 10.5");
        checkSeries(rec("rec", 2), 0, 400, [](int k) { return k == 40 ? 9. : 1.; }, 0., "impulse at 40");
        checkSeries(rec("rec", 3), 0, 400, [](int k) {
            f64 mid = 1. + 2. * fadePosition(40, 20, 40);
            if (k < 40) return 1. + 2. * fadePosition(k, 20, 40);
            return mid + (0. - mid) * fadePosition(k, 40, 20);
        }, 1e-6, "ramp interrupted by a ramp");

        // Constness: after the changes settle every channel is constant.
        Node* p = r.e->silos_[0].nrt_getNode(11);
        for (int c = 0; c < 4; ++c)
            check(p->ins[0].isConst_[c] == 1, std::format("ch {} constant once settled", c));
    }
}

void testRegions() {
    gCase = "regions";
    Rig r(64, 64);
    addRec(r, "rec", tzpl::shapeMatrix(2, 3), 10);
    addPass(r, "p23", tzpl::shapeMatrix(2, 3), 11);
    r.now([&] { connect({11, 0}, {10, 0}); setAll(11, 0, {1., 2., 3., 4., 5., 6.}); });
    u32 off[4] = {0, 0, 1, 1}, ext[4] = {1, 1, 1, 2};   // row 1, cols 1..2
    r.at(10., [&] { setInput(PortAddr{11, 0}, Region::boxed(off, ext), std::vector<f64>{-1.}); });
    r.renderTo(128);
    f64 base[6] = {1, 2, 3, 4, 5, 6};
    for (int c = 0; c < 6; ++c) {
        bool inBox = c == 4 || c == 5;
        checkSeries(rec("rec", c), 0, 128, [&](int k) {
            return inBox && k >= 10 ? -1. : base[c];
        }, 0., std::format("box element {}", c));
    }
    // A region outside the shape aborts the bundle.
    u32 off2[4] = {0, 0, 1, 2}, ext2[4] = {1, 1, 1, 2};
    check(r.now([&] { setInput(PortAddr{11, 0}, Region::boxed(off2, ext2), std::vector<f64>{0.}); })
          == tzpl_errRegionOutOfRange, "out-of-range region refused");
}

void testBroadcast() {
    gCase = "broadcast {3}->{6}";
    {
        Rig r(64, 64);
        addRec(r, "rec", tzpl::shapeRow(6), 10);
        addPass(r, "p3", tzpl::shapeRow(3), 11);
        r.now([&] { connect({11, 0}, {10, 0}); setAll(11, 0, {1., 2., 3.}); });
        r.renderTo(64);
        for (int c = 0; c < 6; ++c)
            checkSeries(rec("rec", c), 0, 64, [&](int) { return 1. + c % 3; }, 0., std::format("ch {}", c));
    }
    gCase = "broadcast {2,3}->{4,3}";
    {
        Rig r(64, 64);
        addRec(r, "rec", tzpl::shapeMatrix(4, 3), 10);
        addPass(r, "p23", tzpl::shapeMatrix(2, 3), 11);
        r.now([&] { connect({11, 0}, {10, 0}); setAll(11, 0, {1., 2., 3., 4., 5., 6.}); });
        r.renderTo(64);
        for (int c = 0; c < 12; ++c)
            checkSeries(rec("rec", c), 0, 64, [&](int) { return 1. + c % 6; }, 0., std::format("ch {}", c));
    }
    gCase = "fold {8}->{2}";
    {
        Rig r(64, 64);
        addRec(r, "rec", tzpl::shapeRow(2), 10);
        addPass(r, "p8", tzpl::shapeRow(8), 11);
        r.now([&] { connect({11, 0}, {10, 0}); setAll(11, 0, {1., 2., 3., 4., 5., 6., 7., 8.}); });
        r.renderTo(64);
        checkSeries(rec("rec", 0), 0, 64, [](int) { return 1. + 3. + 5. + 7.; }, 0., "even sum");
        checkSeries(rec("rec", 1), 0, 64, [](int) { return 2. + 4. + 6. + 8.; }, 0., "odd sum");
    }
    gCase = "fold {3}->{2}";
    {
        Rig r(64, 64);
        addRec(r, "rec", tzpl::shapeRow(2), 10);
        addPass(r, "p3", tzpl::shapeRow(3), 11);
        r.now([&] { connect({11, 0}, {10, 0}); setAll(11, 0, {1., 2., 3.}); });
        r.renderTo(64);
        checkSeries(rec("rec", 0), 0, 64, [](int) { return 1. + 3.; }, 0., "ch 0");
        checkSeries(rec("rec", 1), 0, 64, [](int) { return 2.; }, 0., "ch 1");
    }
}

template <typename F>
f64 curveOf(f64 x, f64 a, f64 b) { return F::fade(x, a, b); }

void testXFades() {
    struct C { FadeCurve curve; f64 (*f)(f64, f64, f64); char const* name; };
    C curves[] = {
        {fadeLinear, curveOf<LinearFade<f64>>, "linear"},
        {fadeExponential, curveOf<ExponentialFade<f64>>, "exponential"},
        {fadeSmoothstep, curveOf<SmoothStepFade<f64>>, "smoothstep"},
        {fadeEqualPower, curveOf<EqPowFade<f64>>, "equal power"},
        {fadeOutIn, curveOf<OutInFade<f64>>, "out-in"},
        {fadeEaseInCubic, curveOf<EaseInCubicFade<f64>>, "ease in cubic"},
        {fadeEaseOutCubic, curveOf<EaseOutCubicFade<f64>>, "ease out cubic"},
    };
    for (C const& cv : curves) {
        gCase = std::format("xfade {}", cv.name);
        Rig r(64, 100);
        addRec(r, "rec", tzpl::shapeRow(1), 10);
        addPass(r, "pa", tzpl::shapeRow(1), 11);
        addPass(r, "pb", tzpl::shapeRow(1), 12);
        r.now([&] { setAll(11, 0, {1.}); setAll(12, 0, {2.}); connect({11, 0}, {10, 0}); });
        // A -> B over 0.1 s from 30.25.
        r.at(30.25, [&] { reconnectOutput({11, 0}, {12, 0}, 0.1, cv.curve); });
        r.renderTo(300);
        checkSeries(rec("rec"), 0, 300, [&](int k) {
            return cv.f(fadePosition(k, 30.25, 100), 1., 2.);
        }, 1e-5, "A->B");
        check(r.e->silos_[0].nrt_getNode(12)->outs[0].dstList_ != nullptr
              && r.e->silos_[0].nrt_getNode(12)->outs[0].dstList_->node_->nodeID == 10,
              "fader retired, B feeds rec directly");
    }

    gCase = "nested xfades";
    {
        Rig r(64, 100);
        addRec(r, "rec", tzpl::shapeRow(1), 10);
        addPass(r, "pa", tzpl::shapeRow(1), 11);
        addPass(r, "pb", tzpl::shapeRow(1), 12);
        addPass(r, "pc", tzpl::shapeRow(1), 13);
        addPass(r, "pd", tzpl::shapeRow(1), 14);
        r.now([&] {
            setAll(11, 0, {1.}); setAll(12, 0, {2.}); setAll(13, 0, {4.}); setAll(14, 0, {8.});
            connect({11, 0}, {10, 0});
        });
        // A->B from 20 over 100; halfway, B->C from 70 over 100; then C->D
        // from 120 over 100: three and four sources audible at once.
        r.at(20., [&] { replaceNode(11, 12, 0.1, fadeLinear); });
        r.at(70., [&] { replaceNode(12, 13, 0.1, fadeLinear); });
        r.at(120., [&] { replaceNode(13, 14, 0.1, fadeLinear); });
        r.renderTo(500);
        auto lin = [](f64 x, f64 a, f64 b) { return a + x * (b - a); };
        checkSeries(rec("rec"), 0, 500, [&](int k) {
            f64 x1 = fadePosition(k, 20, 100), x2 = fadePosition(k, 70, 100),
                x3 = fadePosition(k, 120, 100);
            return lin(x1, 1., lin(x2, 2., lin(x3, 4., 8.)));
        }, 1e-5, "A->B->C->D");
        // Every fader retired: D feeds rec directly.
        Node* d = r.e->silos_[0].nrt_getNode(14);
        check(d->outs[0].dstList_ && d->outs[0].dstList_->node_->nodeID == 10
              && !d->outs[0].dstList_->next_, "all faders retired, D feeds rec directly");
    }

    gCase = "setInputX on a connected input";
    {
        Rig r(64, 64);
        addRec(r, "rec", tzpl::shapeRow(1), 10);
        addPass(r, "pa", tzpl::shapeRow(1), 11);
        r.now([&] { setAll(11, 0, {1.}); connect({11, 0}, {10, 0}); });
        r.at(5.5, [&] { setInputX(PortAddr{10, 0}, Region::all(), std::vector<f64>{3.}, 0.05); });
        r.renderTo(200);
        checkSeries(rec("rec"), 0, 200, [](int k) {
            return 1. + 2. * fadePosition(k, 5.5, 50);
        }, 1e-5, "signal -> constant");
        check(r.e->silos_[0].nrt_getNode(10)->ins[0].srcPort_ == nullptr,
              "input left unconnected, holding the constant");
    }

    gCase = "scheduled cut (step fader)";
    {
        Rig r(64, 64);
        addRec(r, "rec", tzpl::shapeRow(1), 10);
        addPass(r, "pa", tzpl::shapeRow(1), 11);
        addPass(r, "pb", tzpl::shapeRow(1), 12);
        r.now([&] { setAll(11, 0, {1.}); setAll(12, 0, {2.}); connect({11, 0}, {10, 0}); });
        r.at(37.5, [&] { reconnectOutput({11, 0}, {12, 0}); });
        r.at(90., [&] { disconnectInput({10, 0}); });
        r.renderTo(200);
        checkSeries(rec("rec"), 0, 200, [](int k) {
            return k < 38 ? 1. : k < 90 ? 2. : 0.;
        }, 0., "reconnect at 37.5, disconnect at 90");
    }
}

// Feedback between nodes (MF). An integrator patched through its own
// output: y = counter + y one sample late, so y[k] = k(k+1)/2, whatever the
// block and device sizes. In block mode the late input is a block late.
void testCycles() {
    for (int block : {64, 7}) {
        for (int dev : {64, 37}) {
            gCase = std::format("cycle sample mode block {} device {}", block, dev);
            Rig r(block, dev);
            createAddOpNode(r.e);
            addRec(r, "y", tzpl::shapeRow(2), 10);
            check(r.now([&] {
                newNode("counter", 11);
                newNode("+", 12);
                connect({11, 0}, {12, 0});
                connect({12, 0}, {12, 1});   // closes the cycle
                connect({12, 0}, {10, 0});
            }) == tzpl_errNone, "a self-connection is accepted");
            r.renderTo(400);
            checkSeries(rec("y"), 0, 300, [](int k) { return 0.5 * k * (k + 1); }, 0., "y = k + y[k-1]");
        }
    }
    {
        gCase = "cycle through two nodes";
        Rig r(64, 37);
        createAddOpNode(r.e);
        createPassNode(r.e, "p2", tzpl::shapeRow(2));
        addRec(r, "y", tzpl::shapeRow(2), 10);
        check(r.now([&] {
            newNode("counter", 11);
            newNode("+", 12);
            newNode("p2", 13);
            connect({11, 0}, {12, 0});
            connect({12, 0}, {13, 0});
            connect({13, 0}, {12, 1});
            connect({13, 0}, {10, 0});
        }) == tzpl_errNone, "a two-node cycle is accepted");
        r.renderTo(400);
        // the delay sits on one of the cycle's connections: either way the
        // loop is one sample long and p2 carries the integral
        auto const& y = rec("y");
        bool lagged = y.size() > 2 && y[1] == 0.f;
        checkSeries(y, 0, 300, [&](int k) { return lagged ? 0.5 * k * (k - 1) : 0.5 * k * (k + 1); }, 0.,
                    "integral through + and pass");
        // and it can be undone
        check(r.now([&] { disconnectInput({12, 1}); }) == tzpl_errNone, "disconnect the cycle");
        r.renderTo(500);
        checkSeries(y, 450, 500, [](int k) { return (f64)k; }, 0., "after disconnecting: the counter alone");
    }
    {
        gCase = "cycle block mode";
        Rig r(64, 64);
        feedbackMode(r.e, 0, FeedbackMode::block);
        createAddOpNode(r.e);
        addRec(r, "y", tzpl::shapeRow(2), 10);
        r.now([&] {
            newNode("counter", 11);
            newNode("+", 12);
            connect({11, 0}, {12, 0});
            connect({12, 0}, {12, 1});
            connect({12, 0}, {10, 0});
        });
        r.renderTo(400);
        std::vector<f64> want(400);
        for (int k = 0; k < 400; ++k) want[k] = k + (k >= 64 ? want[k - 64] : 0.);
        checkSeries(rec("y"), 0, 384, [&](int k) { return want[k]; }, 0., "y = k + y[k-64]");
    }
}

void testVoices() {
    for (int block : {1, 64, 256}) {
        gCase = std::format("voices block {}", block);
        Rig r(block, 100);
        addRec(r, "rec", tzpl::shapeRow(4), 10);
        r.now([&] { newNode("vtest", 11); connect({11, 0}, {10, 0}); });
        f32 p100[1] = {100.f}, p7[1] = {7.f};
        r.at(20., [&] { noteOn(11, 1, 1, p100); });
        r.at(25.5, [&] { noteOn(11, 2, 1, p7); });
        r.at(50., [&] { noteOff(11, 1); });
        r.at(60., [&] {
            tzpl_ParamPair pp{0, 20.f};
            noteSetParams(11, 2, 1, &pp);
        });
        r.renderTo(300);
        // Voice 0 for note 1 at 20, voice 1 for note 2 at 26.
        checkSeries(rec("rec", 0), 0, 300, [](int k) {
            return k < 20 || k >= 50 ? 0. : 100. + (k - 20);
        }, 0., "voice 0");
        checkSeries(rec("rec", 1), 0, 300, [](int k) {
            if (k < 26) return 0.;
            return (k < 60 ? 7. : 20.) + (k - 26);
        }, 0., "voice 1");
        bool ok = vtestResets.size() == 2 && vtestResets[0].sample == 20 && vtestResets[0].voice == 0
                  && vtestResets[1].sample == 26 && vtestResets[1].voice == 1;
        check(ok, std::format("resetVoice at 20 (v0) and 26 (v1); got {} resets", vtestResets.size()));
    }
}

// A silo task scheduler stand-in: one task that plays a note at its wake time.
struct FakeTasks {
    f64 wake;
    bool fired = false;
};

f64 fakeNext(void* sched, Silo*) {
    auto* t = static_cast<FakeTasks*>(sched);
    return t->fired ? INFINITY : t->wake;
}

void fakeRun(void* sched, Silo* s, f64 time) {
    auto* t = static_cast<FakeTasks*>(sched);
    if (t->fired || time < t->wake) return;
    t->fired = true;
    f32 p[1] = {50.f};
    rtNoteOn(s, 11, 9, 1, p);
}

struct InstallTasksCmd : Command {
    FakeTasks* t;
    explicit InstallTasksCmd(FakeTasks* t) : t(t) {}
    void doRT(Silo* s) override {
        s->taskSched_ = t;
        s->taskNextFn_ = fakeNext;
        s->taskRunFn_ = fakeRun;
    }
};

void testTasks() {
    gCase = "silo task wakeups";
    FakeTasks tasks{133.5};
    Rig r(64, 100);
    addRec(r, "rec", tzpl::shapeRow(4), 10);
    r.now([&] {
        newNode("vtest", 11);
        connect({11, 0}, {10, 0});
        sendCommand(new InstallTasksCmd(&tasks));
    });
    r.renderTo(300);
    checkSeries(rec("rec", 0), 0, 300, [](int k) {
        return k < 134 ? 0. : 50. + (k - 134);
    }, 0., "note from a task at 133.5");
}

void testSilosTapsLimiter() {
    gCase = "multi-silo mixdown";
    {
        Rig r(64, 100, 3);
        createPassNode(r.e, "p1", tzpl::shapeRow(1));
        for (int s = 0; s < 3; ++s) {
            r.now([&] {
                newNode("p1", 20);
                setAll(20, 0, {0.1 * (s + 1)});
                connect({20, 0}, kOut);
            }, s);
        }
        r.renderTo(200);
        bool ok = true;
        for (int k = 0; k < 200; ++k)
            ok = ok && std::fabs(r.outAt(k, 0) - 0.6f) < 1e-6 && std::fabs(r.outAt(k, 1) - 0.6f) < 1e-6;
        check(ok, "three silos sum to 0.6 on both channels");
    }
    gCase = "taps";
    {
        Rig r(64, 100);
        createPassNode(r.e, "p1", tzpl::shapeRow(1));
        r.now([&] {
            newNode("p1", 20);
            setAll(20, 0, {0.5});
            connect({20, 0}, kOut);
            tapOutlet(20, 0, 77, tapMeter);
        });
        r.renderTo(2000);
        check(std::fabs(tapPeak(r.e, 77) - 0.5f) < 1e-6, std::format("tap peak {}", tapPeak(r.e, 77)));
        check(std::fabs(tapRms(r.e, 77) - 0.5f) < 1e-5, std::format("tap rms {}", tapRms(r.e, 77)));
    }
    gCase = "safety limiter";
    {
        Rig r(64, 100);
        safetyLimiter(r.e, kOn);
        createPassNode(r.e, "p1", tzpl::shapeRow(1));
        r.now([&] { newNode("p1", 20); setAll(20, 0, {4.}); connect({20, 0}, kOut); });
        r.renderTo(3000);
        f32 mx = 0;
        for (f32 x : r.out) mx = std::max(mx, std::fabs(x));
        check(mx <= 1.0001f, std::format("limited to full scale, peak {}", mx));
    }
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--verbose") == 0) gVerbose = true;

    testShapes();
    testDenormals();
    testBlockSizes();
    testStartFree();
    testStages();
    testRegions();
    testBroadcast();
    testXFades();
    testCycles();
    testVoices();
    testTasks();
    testSilosTapsLimiter();

    if (gFailures == 0) {
        std::println("test_block_engine: {} checks passed", gChecks);
        return 0;
    }
    std::println("test_block_engine: {} of {} checks FAILED", gFailures, gChecks);
    return 1;
}
