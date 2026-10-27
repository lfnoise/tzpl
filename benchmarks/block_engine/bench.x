-- Block-engine benchmark synths (block engine plan M9). run.sh renders this
-- with --nrt for 61 s and for 1 s and reports the difference as CPU time per
-- node-second, so start-up and compilation cancel out.
--
-- BENCH picks the case, COUNT the number of nodes (each its own synth
-- instance, all summed to the output). Cases:
--   knobs     two resonant lowpasses on noise; every control constant
--   sinlpf1   one sine through a one-pole lowpass
--   sinlpf16  16 channels of the same
--   chorus    fxChorus (feedforward delays through a cubic kernel)
--   echo      fxEcho (a feedback delay line)
--   pingpong  fxPingPong
--   bank      resonBank(8), four notes held for the whole render
--   pluck     ksPluck(12), a note every 0.5 s: about six strings ringing,
--             the rest done (idle voices)
--   bankseq   resonBank(8), a note every second held for 0.5 s
--   cycle     a node pair in a feedback loop (tanh shaper -> one-pole -> back),
--             a sample at a time; a node here is one pair
--   cycleblk  the same with feedbackMode block
--   chain     the same pair without the loop (the shaper reads a constant)
--
-- Notes are scheduled up front on clock slot 0 (60 BPM: a beat is a second).

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import filters.*;
import effects.*;
import instruments.*;
import audio_engine.*;

fn envOr(name String, dflt String) String = match (getEnv(name)) {
	Option.some(s): s;
	Option.none: dflt;
};
let which = envOr("BENCH", "knobs");
let count = envOr("COUNT", "16") parseInt unwrap;

fn knobs() S {
	let cutoff = control("cutoff", ControlSpec { lo: 50.0, hi: 10000.0, init: 1200.0, warp: ControlWarp.exponential });
	let rq = control("rq", ControlSpec { lo: 0.05, hi: 1.0, init: 0.2, warp: ControlWarp.linear });
	let amp = control("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.001, warp: ControlWarp.linear });
	let detune = control("detune", ControlSpec { lo: 0.0, hi: 0.1, init: 0.01, warp: ControlWarp.linear });
	let c2 = cutoff * (1.0 + detune) * 1.5;
	((white(2) rlpf(cutoff, rq) rlpf(c2, rq)) * amp) outlet
}

fn sinlpf1() S = (sinosc(440.0) lpf(1000.0) * 0.01) outlet;
fn sinlpf16() S {
	let f = 440.0 + [0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0];
	(sinosc(f) lpf(1000.0) * 0.01) outlet
}

if (which == "knobs") { knobs defSynth("b") await; }
if (which == "sinlpf1") { sinlpf1 defSynth("b") await; }
if (which == "sinlpf16") { sinlpf16 defSynth("b") await; }
if (which == "chorus") { fxChorus defSynth("b") await; }
if (which == "echo") { fxEcho defSynth("b") await; }
if (which == "pingpong") { fxPingPong defSynth("b") await; }
if (which == "bank" || which == "bankseq") { resonBank(8) defSynth("b") await; }
if (which == "pluck") { ksPluck(12) defSynth("b") await; }

fn shaper() S = (inlet(FLOAT32, 1, "in") * 0.97 + sinosc(3.0) * 0.2) tanh outlet;
fn smooth() S = (inlet(FLOAT32, 1, "in") onepole(0.9) * 0.01) outlet;
let pairs = which == "cycle" || which == "cycleblk" || which == "chain";
if (pairs) {
	shaper defSynth("mfA") await;
	smooth defSynth("mfB") await;
	if (which == "cycleblk") { feedbackMode(0, FeedbackMode.feedbackBlock); }
}

-- A note on `node` at `beat`, released at `beat + hold`.
fn note(node Int, id Int, beat Float, hold Float, freq Float) Void {
	begin(); noteOn(node, id, [freq, 0.001]); sched(0, 0, beat);
	begin(); noteOff(node, id); sched(0, 0, beat + hold);
}

let ratios = [1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 1.125, 1.875];
var k = 0;
while (k < count) {
	let node = 100 + k;
	if (pairs) {
		let a = 1000 + 2 * k;
		begin(); newNode("mfA", a); newNode("mfB", a + 1); sched(0);
		begin(); connect(a, 0, a + 1, 0); connect(a + 1, 0, 0, 0); sched(0);
		if (which != "chain") { begin(); connect(a + 1, 0, a, 0); sched(0); }
	} else {
		begin(); newNode("b", node); connect(node, 0, 0, 0); sched(0);
	}
	let base = 110.0 * (1.0 + 0.03 * k toFloat);
	if (which == "bank") {
		var v = 0;
		while (v < 4) {
			begin(); noteOn(node, v, [220.0 * (v + 1) toFloat, 0.001]); sched(0);
			v = v + 1;
		}
	}
	if (which == "pluck" || which == "bankseq") {
		let step = which == "pluck" ? 0.5 : 1.0;
		var i = 0;
		var t = 0.0;
		while (t < 61.0) {
			note(node, i, t, step * 0.5, base * ratios[i % 8]);
			i = i + 1;
			t = t + step;
		}
	}
	k = k + 1;
}
