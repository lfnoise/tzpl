-- Voicer allocation for multiple noteOn in ONE bundle (same sample time).
--
-- A raw voicer outputs one channel per voice; the channel adapter folds the
-- 4 voice channels to stereo (L = v0+v2, R = v1+v3) and the master meter
-- reports max(L, R). Each voice emits its noteParam as DC while its gate is
-- on, so the meter value pins down which voice holds which note's params.
--
-- Case A (within poly): 3 notes must land on 3 voices with their own values.
-- Case B (over poly): 6 notes into 4 voices -- the 2 steals share the same
-- noteOnTime, and the old lowest-index tie-break stole voice 0 BOTH times,
-- so one note vanished and its voice kept the last note's params. The
-- rotating scan in Voicer::allocVoice (shared/tzpl_voicer.hpp) spreads
-- same-time steals round-robin.
import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import audio_engine.*;
import clock.*;

fn dcVoice() S {
	voicer(4, fn() S {
		let v = noteParam("val", ControlSpec { lo: 0.0, hi: 1.0, init: 0.0, warp: ControlWarp.linear });
		v * gate()
	}) outlet
}
dcVoice defSynth("dcvoicer") await;

let t = allocTapID();
begin(); tapMaster(t, TapMode.tapMeter); go(0);

var failures = 0;
fn check(label String, got Float, want Float) Void {
	if (abs(got - want) < 0.005) { "PASS %^ (peak %^)" fmt(label, got) println; }
	else { "FAIL %^ (peak %^, want %^)" fmt(label, got, want) println; failures = failures + 1; }
}

-- Case A: fresh node, one bundle, 3 notes within poly.
-- v0..v2 = 0.01, 0.02, 0.04 -> L 0.05, R 0.02 -> peak 0.05.
-- (All params aliased to the last note would read 0.08; a collapse to one
-- voice would read 0.04.)
begin(); newNode("dcvoicer", 100); connect(100, 0, 0, 0); go(0);
begin();
noteOn(100, 1, [0.01]);
noteOn(100, 2, [0.02]);
noteOn(100, 3, [0.04]);
sched(0);
await delayReal(0.3);
check("bundle within poly", tapPeak(t), 0.05);
begin(); freeNode(100); sched(0);
await delayReal(0.2);

-- Case B: fresh node, one bundle, 6 notes into 4 voices.
-- Fills v0..v3 = 0.01, 0.02, 0.04, 0.08; note 5 steals v0 (0.16), note 6
-- steals v1 (0.32): L = 0.16+0.04 = 0.20, R = 0.32+0.08 = 0.40 -> peak 0.40.
-- The old tie-break stole v0 twice (note 5 lost): peak 0.36.
begin(); newNode("dcvoicer", 101); connect(101, 0, 0, 0); go(0);
begin();
noteOn(101, 1, [0.01]);
noteOn(101, 2, [0.02]);
noteOn(101, 3, [0.04]);
noteOn(101, 4, [0.08]);
noteOn(101, 5, [0.16]);
noteOn(101, 6, [0.32]);
sched(0);
await delayReal(0.3);
check("bundle over poly (steal tie)", tapPeak(t), 0.40);

-- Case C: per-note seq table (VecAt on a per-voice vector). `ones * f` is an
-- 8-wide noteParam-dependent vector; seq's at(index) read must add the voice
-- base offset -- without it every voice reads voice 0's table and all notes
-- get note 1's frequency (the sawArp bug). All-ones pattern makes the seq
-- output equal f at every step, so the DC level decodes the table each voice
-- actually read: v0 = 0.01 (L), v1 = 0.04 (R) -> peak 0.04; with the bug both
-- voices read v0's table -> peak 0.01.
fn seqVoice() S {
	voicer(4, fn() S {
		let f = noteParam("val", ControlSpec { lo: 0.0, hi: 1000.0, init: 100.0, warp: ControlWarp.linear });
		let ones = [1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0];
		(0.5 lfimp seq(ones * f, 8)) * gate() * 0.0001
	}) outlet
}
seqVoice defSynthX("seqvoicer") await;

begin(); freeNode(101); sched(0);
await delayReal(0.2);
begin(); newNode("seqvoicer", 102); connect(102, 0, 0, 0); go(0);
begin();
noteOn(102, 1, [100.0]);
noteOn(102, 2, [400.0]);
sched(0);
await delayReal(0.3);
check("per-note seq table (VecAt voice base)", tapPeak(t), 0.04);

-- Case D: put on a per-voice vector (flat-voice VecPut own-loop block).
-- Each voice's table row gets f*2 written at runtime index 0, read back at
-- the same index: DC = 2f per voice. v0 = 0.02 (L), v1 = 0.04 (R) -> peak
-- 0.04. A voice-naive put (single memcpy span, unsubscripted per-voice
-- index/value) either fails to compile or corrupts rows.
fn putVoice() S {
	voicer(4, fn() S {
		let f = noteParam("val", ControlSpec { lo: 0.0, hi: 1.0, init: 0.1, warp: ControlWarp.linear });
		let ones = [1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0];
		let k = (phasor(100) * 0.99) i32;
		((ones * f) put(k, f * 2.0)) at(k) * gate()
	}) outlet
}
putVoice defSynthX("putvoicer") await;

begin(); freeNode(102); sched(0);
await delayReal(0.2);
begin(); newNode("putvoicer", 103); connect(103, 0, 0, 0); go(0);
begin();
noteOn(103, 1, [0.01]);
noteOn(103, 2, [0.02]);
sched(0);
await delayReal(0.3);
check("per-voice put table (VecPut voice base)", tapPeak(t), 0.04);

-- Case E: per-voice delay init values. seq's index delayVar is declared
-- init(1, -1), so the impulse at note start lands on pattern[0] (2f). The
-- old code dropped per-voice delay inits (init skipped them, noteOn zeroed
-- them), starting at pattern[1] (1f). One note at f = 0.01: fixed 0.02,
-- broken 0.01.
fn seqInitVoice() S {
	voicer(4, fn() S {
		let f = noteParam("val", ControlSpec { lo: 0.0, hi: 1.0, init: 0.1, warp: ControlWarp.linear });
		let pattern = [2.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0];
		(0.5 lfimp seq(pattern * f, 8)) * gate()
	}) outlet
}
seqInitVoice defSynthX("seqinitvoicer") await;

begin(); freeNode(103); sched(0);
await delayReal(0.2);
begin(); newNode("seqinitvoicer", 104); connect(104, 0, 0, 0); go(0);
begin(); noteOn(104, 1, [0.01]); sched(0);
await delayReal(0.3);
check("seq first step (delay init -1)", tapPeak(t), 0.02);

-- Case F: reused-voice ring bleed. A per-voice feedback echo rings up on
-- note 1; retriggering the (single) voice at amplitude 0 must be SILENT --
-- ring reads are zero-guarded against the write head, which noteOn resets,
-- so the previous note's tail cannot bleed into the reused voice. (Before
-- the guard, the runtime ring kept circulating the old tail.)
fn bleedVoice() S {
	voicer(1, fn() S {
		let a = noteParam("val", ControlSpec { lo: 0.0, hi: 1.0, init: 0.0, warp: ControlWarp.linear });
		let g = gate();
		let d = delayVar(0.5);
		let tap = d vread(0.2 * fs(), Interpolation.none);
		d <- (a * g) + tap * 0.9;
		tap
	}) outlet
}
bleedVoice defSynthX("bleedvoicer") await;

begin(); freeNode(104); sched(0);
await delayReal(0.2);
begin(); newNode("bleedvoicer", 105); connect(105, 0, 0, 0); go(0);
begin(); noteOn(105, 1, [0.2]); sched(0);
await delayReal(0.5);
let ringing = tapPeak(t);
begin(); noteOn(105, 2, [0.0]); sched(0);
await delayReal(0.5);
let bled = tapPeak(t);
if (ringing > 0.05 && bled < 0.001) { "PASS reused-voice ring bleed (ring %^, retrigger %^)" fmt(ringing, bled) println; }
else { "FAIL reused-voice ring bleed (ring %^, retrigger %^)" fmt(ringing, bled) println; failures = failures + 1; }

if (failures == 0) { "VOICER BUNDLE PARAMS PASS" println; }
else { "VOICER BUNDLE PARAMS FAIL" println; }
engineStop();
