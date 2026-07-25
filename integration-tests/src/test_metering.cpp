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
//  test_metering.cpp
//  integration-tests
//
//  Tests for engine metering and monitoring: signal taps (install, remove,
//  the dense-prefix tap table, the per-silo budget) and -- as Phase 14 lands
//  -- the master meter and engine performance counters.
//
//  Everything runs on an NRT engine driven by renderNRTBlock(), so there is
//  no audio device and no timing nondeterminism: commands execute inline at
//  submit and each rendered block is exactly reproducible.
//

#include "tzpl_client_interface.hpp"
#include "tzpl_engine.hpp"   // Silo::kMaxTaps, Engine::kMaxMasterTaps
#include "tzpl_silo.hpp"
#include "tzpl_test_plugins.hpp"

#include <cmath>
#include <format>
#include <print>
#include <string_view>
#include <vector>

using namespace engine;

static int gTestsPassed = 0;
static int gTestsFailed = 0;

static void check(bool condition, std::string_view description) {
    if (condition) {
        std::print("  PASS: {}\n", description);
        ++gTestsPassed;
    } else {
        std::print("  FAIL: {}\n", description);
        ++gTestsFailed;
    }
}

static bool near(f32 a, f32 b, f32 eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// bufferFrames is deliberately larger than TapSlot::kDefaultPublishPeriod so
// one rendered block always publishes at least one peak/rms window.
static Engine* newTapEngine(AudioStreamParameters& params, int numSilos = 1) {
    params.channels = 2;
    params.bufferFrames = 1024;
    params.sampleRate = 44100.0;
    params.deviceName = "default";
    params.firstChannel = 0;

    EngineConfig config;
    config.numSilos = numSilos;

    Engine* e = newEngineNRT(config, params);
    createAddOpNode(e);
    safetyLimiter(e, kOff);
    masterGain(e, 1.f);
    return e;
}

// A "+" node whose inlets hold constants: its outlet is an exact stereo DC
// value, so a tap on it has a known peak and rms. It must be connected to the
// output -- nodes are topologically sorted FROM the output node, so an
// unconnected node never runs and its outlet stays zero.
static void addDCSource(Engine* e, i64 id, f32 value, int silo = 0) {
    f32 v[2] = {value, value};
    f32 zero[2] = {0.f, 0.f};
    begin(e);
    newNode("+", id);
    setInput({id, 0}, 2, v);
    setInput({id, 1}, 2, zero);
    connect({id, 0}, {0, 0});
    go(silo);
}

static void render(Engine* e, AudioStreamParameters const& params, int blocks = 2) {
    std::vector<f32> buf((size_t)params.bufferFrames * params.channels);
    for (int i = 0; i < blocks; ++i) renderNRTBlock(e, buf.data());
}

static tzpl_SErr tapNode(Engine* e, i64 nodeID, i64 tapID, int mode, int silo = 0) {
    tzpl_SErr err = begin(e);
    if (err != tzpl_errNone) return err;
    tapOutlet(nodeID, 0, tapID, mode);
    return go(silo);
}

// UntapCmd frees the registry entry in its stage-2 doNRT, which an NRT engine
// runs from drainNRTQueues() inside renderNRTBlock -- so the entry survives
// until the next rendered block. Callers that assert on tapExists must render.
static tzpl_SErr untapID(Engine* e, i64 tapID, int silo = 0) {
    tzpl_SErr err = begin(e);
    if (err != tzpl_errNone) return err;
    untap(tapID);
    return go(silo);
}

// ---------------------------------------------------------------------------
// Tap lifecycle
// ---------------------------------------------------------------------------

static void test_tap_basics() {
    std::print("Test: tap install / read / remove\n");

    AudioStreamParameters params{};
    Engine* e = newTapEngine(params);
    addDCSource(e, 10, 0.5f);

    check(tapNode(e, 10, 1, tapMeter) == tzpl_errNone, "tapOutlet on an f32 outlet succeeds");
    check(tapExists(e, 1), "the tap is registered");
    check(tapChans(e, 1) == 2, "tapChans reports the outlet's channel count");

    render(e, params);
    check(near(tapPeak(e, 1), 0.5f), std::format("meter peak reads the DC value (got {})", tapPeak(e, 1)));
    check(near(tapRms(e, 1), 0.5f), std::format("meter rms reads the DC value (got {})", tapRms(e, 1)));

    check(untapID(e, 1) == tzpl_errNone, "untap succeeds");
    render(e, params);
    check(!tapExists(e, 1), "the tap is gone from the registry");
    check(tapPeak(e, 1) == 0.f, "an unknown tapID reads 0");

    // A second tap on a node with a non-f32 outlet is rejected. (Every test
    // plugin outlet is f32, so this exercises the unknown-node path instead.)
    check(tapNode(e, 999, 2, tapMeter) == tzpl_errNodeNotFound,
          "tapping a nonexistent node fails");
    check(!tapExists(e, 2), "a failed tap leaves no registry entry");

    check(tapNode(e, 10, 3, 7) == tzpl_errNotImplemented, "an unknown tap mode is rejected");

    freeEngine(e);
}

static void test_tap_scope_drain() {
    std::print("Test: scope tap capture\n");

    AudioStreamParameters params{};
    Engine* e = newTapEngine(params);
    addDCSource(e, 10, 0.25f);

    check(tapNode(e, 10, 1, tapScope) == tzpl_errNone, "scope tap installs");
    render(e, params, 1);

    std::vector<f32> buf(4096);
    int chans = tapChans(e, 1);
    int want = (int)(buf.size() / chans) * chans;
    int n = tapDrain(e, 1, buf.data(), want);
    check(n > 0, std::format("tapDrain returned samples (got {})", n));
    check(n % chans == 0, "tapDrain returns whole interleaved frames");

    bool allDC = true;
    for (int i = 0; i < n; ++i) if (!near(buf[(size_t)i], 0.25f)) allDC = false;
    check(allDC, "captured samples are the known DC value");

    freeEngine(e);
}

// The RT tap table keeps live entries in a dense prefix and fills a hole by
// moving the LAST entry down. Removing a middle tap must therefore leave both
// neighbours installed and publishing -- the failure mode that a naive
// swap-erase would silently produce.
static void test_tap_table_compaction() {
    std::print("Test: tap table compaction (remove from the middle)\n");

    AudioStreamParameters params{};
    Engine* e = newTapEngine(params);
    addDCSource(e, 10, 0.125f);
    addDCSource(e, 11, 0.250f);
    addDCSource(e, 12, 0.500f);

    tapNode(e, 10, 1, tapMeter);
    tapNode(e, 11, 2, tapMeter);
    tapNode(e, 12, 3, tapMeter);
    render(e, params);
    check(near(tapPeak(e, 1), 0.125f) && near(tapPeak(e, 2), 0.250f)
              && near(tapPeak(e, 3), 0.500f),
          "all three taps publish their own node's level");

    check(untapID(e, 2) == tzpl_errNone, "the middle tap is removed");
    render(e, params);
    check(!tapExists(e, 2), "the removed tap is gone");
    check(near(tapPeak(e, 1), 0.125f),
          std::format("the first tap still publishes (got {})", tapPeak(e, 1)));
    check(near(tapPeak(e, 3), 0.500f),
          std::format("the last tap still publishes after being moved down (got {})",
                      tapPeak(e, 3)));

    // Removing the last remaining entries in turn must not disturb the others.
    untapID(e, 1);
    render(e, params);
    check(near(tapPeak(e, 3), 0.500f), "the survivor still publishes after a second removal");

    freeEngine(e);
}

// A freed node's taps stop publishing (and read silence rather than freezing),
// but the registry entry survives until someone untaps it.
static void test_tap_node_freed() {
    std::print("Test: tap on a freed node\n");

    AudioStreamParameters params{};
    Engine* e = newTapEngine(params);
    addDCSource(e, 10, 0.75f);
    addDCSource(e, 11, 0.25f);
    tapNode(e, 10, 1, tapMeter);
    tapNode(e, 11, 2, tapMeter);
    render(e, params);
    check(near(tapPeak(e, 1), 0.75f), "tap publishes before the node is freed");

    begin(e);
    freeNode(10);
    go(0);
    render(e, params);

    check(tapExists(e, 1), "the registry entry outlives the node (untap is the only teardown)");
    check(tapPeak(e, 1) == 0.f, "a tap on a freed node reads silence, not its last value");
    check(near(tapPeak(e, 2), 0.25f), "the other tap is unaffected by the compaction");

    check(untapID(e, 1) == tzpl_errNone, "the orphaned tap can still be untapped");
    render(e, params);
    check(!tapExists(e, 1), "and is then gone");

    freeEngine(e);
}

// The per-silo tap budget is enforced at bundle submit, so callers find out
// synchronously instead of being handed a tapID that reads silence forever.
static void test_tap_budget() {
    std::print("Test: per-silo tap budget\n");

    AudioStreamParameters params{};
    Engine* e = newTapEngine(params);
    addDCSource(e, 10, 0.5f);

    int const cap = Silo::kMaxTaps;
    bool allOK = true;
    for (int i = 0; i < cap; ++i) {
        if (tapNode(e, 10, 100 + i, tapMeter) != tzpl_errNone) allOK = false;
    }
    check(allOK, std::format("{} taps install on one silo", cap));

    tzpl_SErr err = tapNode(e, 10, 100 + cap, tapMeter);
    check(err == tzpl_errResourceLimit,
          std::format("the tap past the limit fails synchronously (got {})", (int)err));
    check(!tapExists(e, 100 + cap), "the rejected tap left no registry entry");

    // Freeing one slot makes room again -- once the untap's stage-2 cleanup
    // has actually run and removed the registry entry the budget counts.
    check(untapID(e, 100) == tzpl_errNone, "untap frees a slot");
    render(e, params);
    check(tapNode(e, 10, 100 + cap, tapMeter) == tzpl_errNone,
          "a new tap fits once a slot is free");

    freeEngine(e);
}

// The budget is per silo: filling silo 0 must not stop silo 1 from metering.
static void test_tap_budget_is_per_silo() {
    std::print("Test: tap budget is per silo\n");

    AudioStreamParameters params{};
    Engine* e = newTapEngine(params, /*numSilos=*/2);
    addDCSource(e, 10, 0.5f, /*silo=*/0);
    addDCSource(e, 20, 0.5f, /*silo=*/1);

    for (int i = 0; i < Silo::kMaxTaps; ++i) tapNode(e, 10, 100 + i, tapMeter, 0);
    check(tapNode(e, 10, 999, tapMeter, 0) == tzpl_errResourceLimit, "silo 0 is full");
    check(tapNode(e, 20, 1000, tapMeter, 1) == tzpl_errNone, "silo 1 still accepts taps");

    freeEngine(e);
}

static void test_alloc_tap_id() {
    std::print("Test: tap id allocator\n");

    AudioStreamParameters params{};
    Engine* e = newTapEngine(params);
    i64 a = allocTapID(e);
    i64 b = allocTapID(e);
    check(a != 0 && b != 0, "allocTapID never returns 0 (0 means 'no tap')");
    check(a != b, "allocTapID returns distinct ids");
    check(allocTapID(nullptr) == 0, "allocTapID(null) is safe");

    freeEngine(e);
}

// ---------------------------------------------------------------------------
// Master meter
// ---------------------------------------------------------------------------

// The master meter is measured on the buffer the device receives, so it must
// follow the master gain.
static void test_master_meter_post_gain() {
    std::print("Test: master meter is post-gain\n");

    AudioStreamParameters params{};
    Engine* e = newTapEngine(params);
    addDCSource(e, 10, 0.5f);
    render(e, params, 4);

    check(masterChans(e) == params.channels, "masterChans matches the stream width");
    check(near(masterPeak(e, -1), 0.5f),
          std::format("master peak reads the DC value (got {})", masterPeak(e, -1)));
    check(near(masterRms(e, -1), 0.5f),
          std::format("master rms reads the DC value (got {})", masterRms(e, -1)));
    check(near(masterPeak(e, 0), 0.5f), "per-channel peak reads channel 0");
    check(masterPeak(e, 99) == 0.f, "an out-of-range channel reads 0");

    masterGain(e, 0.5f);
    render(e, params, 8);
    check(near(masterPeak(e, -1), 0.25f),
          std::format("halving the master gain halves the meter (got {})",
                      masterPeak(e, -1)));

    freeEngine(e);
}

// ...and on the buffer AFTER the safety limiter, so it can never read hot
// while the limiter is holding the output down.
static void test_master_meter_post_limiter() {
    std::print("Test: master meter is post-limiter\n");

    AudioStreamParameters params{};
    Engine* e = newTapEngine(params);
    safetyLimiter(e, kOn);
    addDCSource(e, 10, 2.0f);
    render(e, params, 8);

    f32 pk = masterPeak(e, -1);
    check(pk <= 1.0f + 1e-3f,
          std::format("the limiter keeps the master meter at or under 1.0 (got {})", pk));
    check(pk > 0.f, "the master meter is not simply reading silence");

    EngineStats st;
    getEngineStats(e, st);
    check(st.limiterGain < 1.f,
          std::format("the reported limiter gain shows it pulling down (got {})",
                      st.limiterGain));
    check(st.clipCount > 0, "samples at full scale are counted as clips");

    freeEngine(e);
}

static void test_master_peak_hold() {
    std::print("Test: master peak hold\n");

    AudioStreamParameters params{};
    Engine* e = newTapEngine(params);
    addDCSource(e, 10, 0.8f);
    render(e, params, 4);
    check(near(masterPeakHold(e, -1), 0.8f, 1e-3f), "peak hold catches the level");

    // Silence: the hold falls slowly, so a reader that polls infrequently
    // still sees a recent peak rather than whichever block it landed on.
    begin(e);
    disconnectInput({0, 0});
    go(0);
    render(e, params, 2);
    f32 held = masterPeakHold(e, -1);
    check(masterPeak(e, -1) < 1e-6f, "instantaneous peak drops to silence");
    check(held > 0.1f, std::format("peak hold still holds shortly after (got {})", held));

    freeEngine(e);
}

// ---------------------------------------------------------------------------
// Master taps
// ---------------------------------------------------------------------------

static tzpl_SErr tapMasterID(Engine* e, i64 tapID, int mode, int silo = 0) {
    tzpl_SErr err = begin(e);
    if (err != tzpl_errNone) return err;
    tapMaster(tapID, mode);
    return go(silo);
}

static void test_master_tap_meter() {
    std::print("Test: master meter tap\n");

    AudioStreamParameters params{};
    Engine* e = newTapEngine(params);
    addDCSource(e, 10, 0.5f);

    check(tapMasterID(e, 50, tapMeter) == tzpl_errNone, "tapMaster installs");
    check(tapExists(e, 50), "the master tap is registered");
    check(tapChans(e, 50) == params.channels, "the master tap is as wide as the stream");

    render(e, params, 4);
    check(near(tapPeak(e, 50), 0.5f),
          std::format("the master tap reads the master level (got {})", tapPeak(e, 50)));
    check(near(tapPeak(e, 50), masterPeak(e, -1), 1e-3f),
          "the master tap agrees with the always-on master meter");

    check(untapID(e, 50) == tzpl_errNone, "the master tap can be untapped");
    render(e, params);
    check(!tapExists(e, 50), "and is then gone");

    freeEngine(e);
}

static void test_master_tap_scope() {
    std::print("Test: master scope tap\n");

    AudioStreamParameters params{};
    Engine* e = newTapEngine(params);
    addDCSource(e, 10, 0.25f);
    check(tapMasterID(e, 51, tapScope) == tzpl_errNone, "master scope tap installs");

    render(e, params, 1);
    std::vector<f32> buf(4096);
    int chans = tapChans(e, 51);
    int n = tapDrain(e, 51, buf.data(), (int)(buf.size() / chans) * chans);
    check(n > 0 && n % chans == 0,
          std::format("tapDrain returns whole master frames (got {})", n));
    bool allDC = true;
    for (int i = 0; i < n; ++i) if (!near(buf[(size_t)i], 0.25f)) allDC = false;
    check(allDC, "the captured master samples are the known DC value");

    freeEngine(e);
}

// Master taps are silo-0-only: silo 0's thread is the one that runs the
// post-limiter section, so no other thread may touch the master tap table.
static void test_master_tap_silo_rule() {
    std::print("Test: master taps are silo-0-only\n");

    AudioStreamParameters params{};
    Engine* e = newTapEngine(params, /*numSilos=*/2);

    check(tapMasterID(e, 52, tapMeter, /*silo=*/1) == tzpl_errSiloOutOfRange,
          "tapMaster on a worker silo is rejected");
    check(!tapExists(e, 52), "the rejected master tap left no registry entry");

    check(tapMasterID(e, 53, tapMeter, 0) == tzpl_errNone, "tapMaster on silo 0 works");
    check(untapID(e, 53, /*silo=*/1) == tzpl_errSiloOutOfRange,
          "untapping a master tap from a worker silo is rejected");
    check(tapExists(e, 53), "the master tap survives the rejected untap");
    check(untapID(e, 53, 0) == tzpl_errNone, "untapping from silo 0 works");

    freeEngine(e);
}

static void test_master_tap_budget() {
    std::print("Test: master tap budget\n");

    AudioStreamParameters params{};
    Engine* e = newTapEngine(params);

    for (int i = 0; i < Engine::kMaxMasterTaps; ++i) {
        tapMasterID(e, 60 + i, tapMeter);
    }
    check(tapMasterID(e, 60 + Engine::kMaxMasterTaps, tapMeter) == tzpl_errResourceLimit,
          "the master tap past the limit fails synchronously");
    // Node taps draw on a separate budget.
    addDCSource(e, 10, 0.5f);
    check(tapNode(e, 10, 90, tapMeter) == tzpl_errNone,
          "a full master table does not block node taps");

    freeEngine(e);
}

// ---------------------------------------------------------------------------
// Performance counters
// ---------------------------------------------------------------------------

// Asserts counts, positivity and finiteness only -- never absolute wall times,
// which are nondeterministic under load.
static void test_engine_stats() {
    std::print("Test: engine performance counters\n");

    AudioStreamParameters params{};
    Engine* e = newTapEngine(params, /*numSilos=*/2);
    addDCSource(e, 10, 0.5f);

    int const blocks = 16;
    render(e, params, blocks);

    EngineStats st;
    getEngineStats(e, st);
    check(st.blockCount == (u64)blocks,
          std::format("blockCount counts every rendered block (got {})", st.blockCount));
    check(st.silos.size() == 2, "one stats row per silo");
    check(st.silos[0].blockCount == (u64)blocks, "silo 0 counted every block");
    check(st.sampleRate == params.sampleRate && st.bufferFrames == params.bufferFrames,
          "the snapshot carries the stream format");
    check(st.blockBudgetMs > 0. && std::isfinite(st.blockBudgetMs),
          "the per-block budget is set from the stream format");
    check(st.blockAvgMs > 0. && std::isfinite(st.blockAvgMs), "average block time is measured");
    check(st.blockMaxMs >= st.blockAvgMs, "max block time is at least the average");
    check(std::isfinite(st.loadPercent) && st.loadPercent >= 0., "load percent is finite");
    check(st.silos[0].avgMs > 0. && std::isfinite(st.silos[0].avgMs),
          "per-silo DSP time is measured");
    check(st.deviceXruns == 0 && !st.deviceTelemetry,
          "an NRT engine reports no device telemetry");
    check(st.rtExceptionCount == 0 && st.badBlockSizeCount == 0,
          "no silent-failure counters tripped");

    // Queue depths are sampled per block; with everything drained they settle.
    check(st.silos[0].fromNrtDepth >= 0 && st.silos[0].toNrtDepth >= 0,
          "queue depths are non-negative");

    freeEngine(e);
}

static void test_reset_engine_stats() {
    std::print("Test: resetEngineStats\n");

    AudioStreamParameters params{};
    Engine* e = newTapEngine(params);
    addDCSource(e, 10, 0.5f);
    render(e, params, 8);

    EngineStats before;
    getEngineStats(e, before);

    resetEngineStats(e);
    EngineStats cleared;
    getEngineStats(e, cleared);
    check(cleared.blockCount == before.blockCount,
          "blockCount stays monotone across a reset");
    check(cleared.blockMaxMs == 0., "max block time is cleared");
    check(cleared.overBudgetCount == 0 && cleared.engineDropouts == 0,
          "dropout counters are cleared");
    check(cleared.clipCount == 0, "the clip counter is cleared");

    // The epoch handshake: the next block must restart the running maximum
    // rather than restoring the pre-reset one.
    render(e, params, 4);
    EngineStats after;
    getEngineStats(e, after);
    check(after.blockMaxMs > 0. && std::isfinite(after.blockMaxMs),
          "a fresh maximum accumulates after the reset");

    freeEngine(e);
}

int main() {
    std::print("=== Metering & monitoring tests ===\n\n");

    test_tap_basics();
    test_tap_scope_drain();
    test_tap_table_compaction();
    test_tap_node_freed();
    test_tap_budget();
    test_tap_budget_is_per_silo();
    test_alloc_tap_id();
    test_master_meter_post_gain();
    test_master_meter_post_limiter();
    test_master_peak_hold();
    test_master_tap_meter();
    test_master_tap_scope();
    test_master_tap_silo_rule();
    test_master_tap_budget();
    test_engine_stats();
    test_reset_engine_stats();

    std::print("\n=== {} passed, {} failed ===\n", gTestsPassed, gTestsFailed);
    return gTestsFailed == 0 ? 0 : 1;
}
