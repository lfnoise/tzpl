-- taps_test: engine-level signal taps (audio_engine.tapOutlet / tapMaster /
-- untap / tapPeak / tapRms / tapChans / tapSamples). No `ui` widget involved.
--
-- Runs with no audio stream, so commands execute synchronously at submit and
-- nothing is rendered -- the levels stay at zero. What this covers is the API
-- surface, the error codes, and (at the end) that taps created inside an
-- offline render target the RENDER engine rather than the live one.
import audio_engine.*;

println("--- taps_test -----------------------------");

-- A DC source: "+" with both inlets constant, wired to Audio Out.
let build = begin();
newNode("+", 700);
setInput(700, 0, 0.5);
setInput(700, 1, 0.0);
connect(700, 0, 0, 0);
"build err: %^ (expect 0)" fmt(go(0)) println;

-- Tap ids come from the engine so they can never collide with the ones the
-- ui widgets and the graph view hand out.
let a = allocTapID();
let b = allocTapID();
"ids distinct: %^ (expect true)" fmt(a != b) println;
"ids nonzero: %^ (expect true)" fmt(a != 0 && b != 0) println;

-- Install a meter tap on the node's outlet.
begin();
tapOutlet(700, 0, a, TapMode.tapMeter);
"tapOutlet err: %^ (expect 0)" fmt(go(0)) println;
"tapExists: %^ (expect true)" fmt(tapExists(a)) println;
"tapChans: %^ (expect 2)" fmt(tapChans(a)) println;
"tapPeak: %^ (expect 0.0, nothing rendered)" fmt(tapPeak(a)) println;
"tapRms: %^ (expect 0.0)" fmt(tapRms(a)) println;

-- A scope tap on the same outlet gets its OWN id and its own FIFO: one
-- consumer per tap is the rule, so two readers means two taps.
begin();
tapOutlet(700, 0, b, TapMode.tapScope);
"scope tap err: %^ (expect 0)" fmt(go(0)) println;
let xs = tapSamples(b, 512);
"samples length: %^ (expect 0, nothing rendered)" fmt(xs length) println;

-- Error paths.
begin();
tapOutlet(999, 0, allocTapID(), TapMode.tapMeter);
"tap on missing node err: %^ (expect 4 = errNodeNotFound)" fmt(go(0)) println;

-- Audio Out (node 0) has no outlets, which is exactly why tapMaster exists.
begin();
tapOutlet(0, 0, allocTapID(), TapMode.tapMeter);
"tap on Audio Out err: %^ (expect 12 = errOutputOutOfRange)" fmt(go(0)) println;

let m = allocTapID();
begin();
tapMaster(m, TapMode.tapMeter);
"tapMaster err: %^ (expect 0)" fmt(go(0)) println;
"master tapExists: %^ (expect true)" fmt(tapExists(m)) println;

-- Unknown ids read as absent rather than failing.
"unknown tapExists: %^ (expect false)" fmt(tapExists(123456)) println;
"unknown tapPeak: %^ (expect 0.0)" fmt(tapPeak(123456)) println;

-- Explicit teardown -- a script tap has no owner but the script.
begin(); untap(a);
"untap err: %^ (expect 0)" fmt(go(0)) println;
begin(); untap(b); go(0);
begin(); untap(m); go(0);
begin(); untap(a);
"double untap err: %^ (expect 4 = errNodeNotFound)" fmt(go(0)) println;

-- freeVmTaps sweeps everything THIS VM made, and nothing else. Taps behind
-- the app's meters and scopes are tagged to the host and never swept.
let t1 = allocTapID();
let t2 = allocTapID();
begin(); tapOutlet(700, 0, t1, TapMode.tapMeter); go(0);
begin(); tapOutlet(700, 0, t2, TapMode.tapScope); go(0);
"before sweep: %^ %^ (expect true true)" fmt(tapExists(t1), tapExists(t2)) println;
"freeVmTaps freed: %^ (expect 2)" fmt(freeVmTaps()) println;
"after sweep: %^ %^ (expect false false)" fmt(tapExists(t1), tapExists(t2)) println;
"freeVmTaps again: %^ (expect 0)" fmt(freeVmTaps()) println;

-- freeAllTaps is the nuclear option: every tap, whoever made it.
let t3 = allocTapID();
begin(); tapOutlet(700, 0, t3, TapMode.tapMeter); go(0);
"freeAllTaps freed: %^ (expect 1)" fmt(freeAllTaps()) println;
"after nuke: %^ (expect false)" fmt(tapExists(t3)) println;

begin(); freeNode(700); go(0);

-- Taps created inside an offline render resolve against the RENDER engine,
-- not the live one, because every audio_engine function routes through the
-- active render context. That can't be exercised here: this harness runs a
-- bare VM with no NRT VM in its AppContext, so renderNRT declines. Asserting
-- it declines (rather than crashing, as it used to) is still worth doing.
let h = renderNRT("/tmp/taps_test_nrt.wav", 0.1, fn() Void { });
"renderNRT without an NRT VM: %^ (expect 0 = declined, not a crash)"
	fmt(h) println;

println("--- taps_test done ------------------------");
