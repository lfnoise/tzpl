-- Non-flat (AoS) voicer behavioral checks, live audio.
--
-- A pause() in the voice body forces AoS mode. Each voice emits DC =
-- (ph + 1) * amp where ph is a per-voice INIT-RATE random draw: the master
-- fold (L = v0+v2, R = v1+v3; meter = max(L,R)) then measures which values
-- the voices actually hold.
--
-- Checks:
--  1. Per-voice init draws are seeded and DISTINCT (the sum is strictly
--     inside (0.2, 0.4) for amp 0.1 -- identical zero-state draws would give
--     exactly 0.2).
--  2. Init-rate state SURVIVES noteOn (per-field reset): a second round of
--     notes on reused voices reads the same peak. The old whole-struct
--     memset wiped ph and nothing recomputed it (round 2 = exactly 0.2).
--  3. A per-note seq table inside pause starts on pattern[0] (its index
--     delayVar init(1, -1) is written at init and restored at noteOn).
import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import audio_engine.*;
import clock.*;

fn nfProbe() S {
	voicer(4, fn() S {
		let amp = noteParam("val", ControlSpec { lo: 0.0, hi: 1.0, init: 0.0, warp: ControlWarp.linear });
		let ph = urand(1, Rate.init);
		let g = gate();
		g pause(fn() S { (ph + 1.0) * amp })
	}) outlet
}
nfProbe defSynthX("nfprobe") await;

fn nfSeq() S {
	voicer(4, fn() S {
		let f = noteParam("val", ControlSpec { lo: 0.0, hi: 1.0, init: 0.1, warp: ControlWarp.linear });
		let pattern = [2.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0];
		let g = gate();
		g pause(fn() S { (0.5 lfimp seq(pattern * f, 8)) })
	}) outlet
}
nfSeq defSynthX("nfseq") await;

let t = allocTapID();
begin(); tapMaster(t, TapMode.tapMeter); go(0);

var failures = 0;
fn check(label String, ok Bool, detail String) Void {
	if (ok) { "PASS %^ (%^)" fmt(label, detail) println; }
	else { "FAIL %^ (%^)" fmt(label, detail) println; failures = failures + 1; }
}

begin(); newNode("nfprobe", 100); connect(100, 0, 0, 0); go(0);

-- Round 1: fill all four voices.
begin();
noteOn(100, 1, [0.1]);
noteOn(100, 2, [0.1]);
noteOn(100, 3, [0.1]);
noteOn(100, 4, [0.1]);
sched(0);
await delayReal(0.3);
let m1 = tapPeak(t);
begin(); allNotesOff(100); sched(0);
await delayReal(0.2);

-- Round 2: reuse the voices.
begin();
noteOn(100, 5, [0.1]);
noteOn(100, 6, [0.1]);
noteOn(100, 7, [0.1]);
noteOn(100, 8, [0.1]);
sched(0);
await delayReal(0.3);
let m2 = tapPeak(t);

check("seeded distinct init draws", m1 > 0.201 && m1 < 0.399, "peak %^" fmt(m1));
check("init state survives noteOn", abs(m1 - m2) < 0.001, "round1 %^ round2 %^" fmt(m1, m2));

begin(); freeNode(100); sched(0);
await delayReal(0.2);
begin(); newNode("nfseq", 101); connect(101, 0, 0, 0); go(0);
begin(); noteOn(101, 1, [0.01]); sched(0);
await delayReal(0.3);
let ms = tapPeak(t);
check("seq first step (init -1)", abs(ms - 0.02) < 0.005, "peak %^ (want 0.02, broken 0.01)" fmt(ms));

if (failures == 0) { "NONFLAT BEHAVIOR PASS" println; }
else { "NONFLAT BEHAVIOR FAIL" println; }
engineStop();
