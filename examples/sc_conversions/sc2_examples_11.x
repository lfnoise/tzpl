-- Tzopilotl translations of SuperCollider examples from
-- sc-examples/SC2-examples/examples-11.txt: seven mouse-strummed
-- instruments. Shared machinery lives in sc2_common.x.
--
-- Strumming: each string has a trigger point on the mouse's x axis (0.25 ..
-- 0.75); HPZ1.kr(mousex > point).abs is an impulse when the mouse crosses it
-- (HPZ1 is half the first difference, so the impulse is 0.5). The instruments
-- are vectorized across their strings (one n-channel graph, panned per
-- string, then transpose(n) sum(2)). Headless there is no mouse, so the
-- driver strums by sweeping shared-input slot 0 back and forth (`strum`).
-- Note: in every source example `LPF.ar(out, 12000)` is a statement whose
-- result is discarded (the returned value is LeakDC.ar(out) / the allpass
-- chain), so it is not applied here either.
--
-- Status (examples-11.txt blocks, in order):
--   1  strummable guitar          -- DONE (strummableGuitar)
--   2  strummable 12 string guitar-- DONE (twelveString)
--   3  bidirectional guitar       -- DONE (bidirectionalGuitar) -- negative-
--                                    feedback comb for the downstroke strings
--   4  harmonic zither            -- DONE (harmonicZither)
--   5  strummable metals          -- DONE (strummableMetals)
--   6  strummable pipes           -- DONE (strummablePipes)
--   7  strummable silk            -- DONE (strummableSilk)

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import filters.*;
import audio_engine as ae;
import clock.*;
import sc2_common.*;

let tags = ["sc2-examples-11"];

---------------------------------------------------------------------------
-- Local helpers

-- HPZ1: (x - x[n-1]) / 2.
fn hpz1(x S) S = (x - x z1) * 0.5;

-- A comb with NEGATIVE feedback (CombL with a negative decay time):
-- feedback -0.001^(delay/|decay|). (Same as file 6's local copy.)
fn combNeg(x S, delayTime AsSignal, maxDelayTime AsSignal, decayTime AsSignal) S {
	let a = 0.0 - decay60dB(decayTime / delayTime);
	let y = delayVar(maxDelayTime * fs());
	y <- x + a * y(delayTime * fs(), Interpolation.linear)
}

fn idxVec(n Int) S {
	var v [Int] = [];
	for (i : (0..(n - 1))) { v push!(i); }
	v vec
}

-- Mode-major mode-index vector for `strings` strings x `modes` modes: channel
-- k = j * strings + i holds the harmonic number j + 1, so that the string
-- index is k % strings -- which is exactly how a `strings`-channel exciter
-- broadcasts over the bank and how sum(strings) folds it back per string.
fn harmonicVec(strings Int, modes Int) S {
	var v [Float] = [];
	for (j : (1..modes)) {
		for (i : (1..strings)) { v push!(j toFloat); }
	}
	v vec
}

-- The strum impulses for n strings with trigger points at `first + i*spacing`.
fn strumTriggers(n Int, first Float, spacing Float) S {
	let points = (idxVec(n) * spacing) + first;
	hpz1((mouseX(0, 1) > points) f32)
}

fn stringPans(n Int, spacing Float, first Float) S = (idxVec(n) * spacing) + first;

-- Six allpasses in series with random stereo delays (0.1 max, 4 s decay).
fn sixAllpasses(x S) S {
	var y = x;
	for (i : (1..6)) {
		y = y alpasn(urand(2, Rate.init) * 0.05, 4);
	}
	y
}

---------------------------------------------------------------------------
-- 1. strummable guitar (examples-11.txt:4-24)
-- Six strings e a d g b e (MIDI 52..76), trigger points 0.25 + 0.1 i; a
-- PinkNoise pluck under Decay(0.05) into a CombL tuned to the string period
-- with a 4 s decay; panned i*0.2 - 0.5; LeakDC.

fn strummableGuitar() S {
	let n = 6;
	let pitches = [52, 57, 62, 67, 71, 76] vec;
	let trigger = strumTriggers(n, 0.25, 0.1) abs;
	let pluck = pink(n) * (trigger decay(0.05));
	let period = 1 / (pitches nnhz);
	let strings = pluck combl(period, 0.02, 4);
	(((strings pan(stringPans(n, 0.2, -0.5)) join) transpose(n) sum(2)) leakdc(0.995)) gainOutlet
}

strummableGuitar defSynthX("strummableGuitar", tags) await;

---------------------------------------------------------------------------
-- 2. strummable 12 string guitar (examples-11.txt:29-56)
-- As #1 plus a second string an octave up (period / 2) triggered 0.015
-- later along the mouse axis.

fn twelveString() S {
	let n = 6;
	let pitches = [52, 57, 62, 67, 71, 76] vec;
	let period = 1 / (pitches nnhz);
	let trigger1 = strumTriggers(n, 0.25, 0.1) abs;
	let trigger2 = strumTriggers(n, 0.265, 0.1) abs;
	let string1 = (pink(n) * (trigger1 decay(0.05))) combl(period, 0.02, 4);
	let string2 = (pink(n) * (trigger2 decay(0.05))) combl(period * 0.5, 0.02, 4);
	((((string1 + string2) pan(stringPans(n, 0.2, -0.5)) join) transpose(n) sum(2)) leakdc(0.995)) gainOutlet
}

twelveString defSynthX("twelveString", tags) await;

---------------------------------------------------------------------------
-- 3. bidirectional strummable guitar (examples-11.txt:60-87)
-- The signed HPZ1 impulse: upstrokes (+) pluck PinkNoise strings at pitch1
-- with a 4 s comb; downstrokes (-) pluck BrownNoise strings a fifth up
-- (pitch1 + 7) through a comb with a NEGATIVE 4 s decay (odd harmonics).

fn bidirectionalGuitar() S {
	let n = 6;
	let pitches1 = [52, 57, 62, 67, 71, 76] vec;
	let pitches2 = pitches1 + 7;
	let trigger = strumTriggers(n, 0.25, 0.1);
	let pluck1 = pink(n) * ((trigger max(0)) decay(0.05));
	let pluck2 = red(n) * (((0 - trigger) max(0)) decay(0.05));
	let string1 = pluck1 combl(1 / (pitches1 nnhz), 0.02, 4);
	let string2 = combNeg(pluck2, 1 / (pitches2 nnhz), 0.02, 4);
	((((string1 + string2) pan(stringPans(n, 0.2, -0.5)) join) transpose(n) sum(2)) leakdc(0.995)) gainOutlet
}

bidirectionalGuitar defSynthX("bidirectionalGuitar", tags) await;

---------------------------------------------------------------------------
-- 4. harmonic zither (examples-11.txt:91-114)
-- Twelve strings tuned to a harmonic series (fractional MIDI), trigger
-- points 0.25 + i * 0.5/11, 8 s comb decay, panned across -0.75..0.75.

fn harmonicZither() S {
	let n = 12;
	let pitches = [50, 53.86, 57.02, 59.69, 62, 64.04, 65.86, 67.51, 69.02, 71.69, 72.88, 74] vec;
	let trigger = strumTriggers(n, 0.25, 0.5 / 11.0) abs;
	let pluck = pink(n) * (trigger decay(0.05));
	let strings = pluck combl(1 / (pitches nnhz), 0.02, 8);
	(((strings pan(stringPans(n, 1.5 / 11.0, -0.75)) join) transpose(n) sum(2)) leakdc(0.995)) gainOutlet
}

harmonicZither defSynthX("harmonicZither", tags) await;

---------------------------------------------------------------------------
-- 5. strummable metals (examples-11.txt:118-142)
-- Eight "strings" (trigger points 0.25 + 0.07 i), each a 15-mode Klank at
-- 300 i + linrand 8000 Hz, ring 1 + rand 4, plucked by PinkNoise under
-- Decay(0.05) * 0.04. The 8 x 15 modes are laid out mode-major so the
-- 8-channel pluck broadcasts to its own string's modes and sum(8) folds
-- the modes back per string.

fn strummableMetals() S {
	let n = 8;
	let modes = 15;
	let trigger = strumTriggers(n, 0.25, 0.07) abs;
	let pluck = pink(n) * (trigger decay(0.05)) * 0.04;
	let freqs = (idxVec(n) * 300) + linrandv(0, 8000, n * modes, Rate.init);
	let rings = 1 + urand(n * modes, Rate.init) * 4;
	let metal = klankModes(pluck, freqs, 1, rings) sum(n);
	(((metal pan(stringPans(n, 0.2, -0.5)) join) transpose(n) sum(2)) leakdc(0.995)) gainOutlet
}

strummableMetals defSynthX("strummableMetals", tags) await;

---------------------------------------------------------------------------
-- 6. strummable pipes (examples-11.txt:146-171)
-- Eight harmonic Klanks (15 harmonics of a scale note 60 + [0,3,5,7,10,12,
-- 15,17], ring rand 0.2) blown by PinkNoise under Lag(Trig(trigger, 1),
-- 0.2) * 0.01 -- a 1 s breath per strum; then LeakDC and six random
-- stereo allpasses (0.1 max, 4 s).

fn pipesLike(scale S, ringLo Float, ringHi Float, pluckFn fn(S) S) S {
	let n = 8;
	let modes = 15;
	let trigger = strumTriggers(n, 0.25, 0.07) abs;
	let pluck = pluckFn(trigger);
	let freqs = harmonicVec(n, modes) * ((scale + 60) nnhz);
	let rings = ringLo + urand(n * modes, Rate.init) * (ringHi - ringLo);
	let metal = klankModes(pluck, freqs, 1, rings) sum(n);
	let out = ((metal pan(stringPans(n, 0.2, -0.5)) join) transpose(n) sum(2)) leakdc(0.995);
	(sixAllpasses(out)) gainOutlet
}

fn breath(trigger S) S = pink(8) * ((trigger timedGate(1)) lag(0.2) * 0.01);

fn strummablePipes() S = pipesLike([0, 3, 5, 7, 10, 12, 15, 17] vec, 0.0, 0.2, breath);

strummablePipes defSynthX("strummablePipes", tags) await;

---------------------------------------------------------------------------
-- 7. strummable silk (examples-11.txt:175-200)
-- As #6 with a different scale (60 + [-2,0,3,5,7,10,12,15]), rings
-- rrand(0.3, 1.0), and the breath chopped into 14 Hz impulses under
-- Decay(0.04).

fn silkPluck(trigger S) S =
	pink(8) * ((lfimp(14) * ((trigger timedGate(1)) lag(0.2) * 0.01)) decay(0.04));

fn strummableSilk() S = pipesLike([-2, 0, 3, 5, 7, 10, 12, 15] vec, 0.3, 1.0, silkPluck);

strummableSilk defSynthX("strummableSilk", tags) await;

---------------------------------------------------------------------------
-- driver

-- Strum: sweep the mouse x (shared-input slot 0) 0 -> 1 -> 0, `cycles`
-- times, 1.5 s each way, in 10 ms steps.
coro fn strum(name String, cycles Int) Float {
	"-- %^ --" fmt(name) println;
	let node = name play;
	yield 0.5;
	var c = 0;
	while (c < cycles) {
		var k = 0;
		while (k <= 150) {
			ae.setSharedInput(0, (k toFloat) / 150.0);
			k = k + 1;
			yield 0.01;
		}
		while (k > 0) {
			k = k - 1;
			ae.setSharedInput(0, (k toFloat) / 150.0);
			yield 0.01;
		}
		c = c + 1;
	}
	yield 4.0;
	node stop;
}

fn playAllSc11() {
	go(coro fn() Float {
		"start playing SC2 examples-11" println;
		ae.setSharedInput(0, 0.0);
		ae.setSharedInput(1, 0.5);

		"strummableGuitar" strum(3) yieldAll;
		"twelveString" strum(3) yieldAll;
		"bidirectionalGuitar" strum(3) yieldAll;
		"harmonicZither" strum(3) yieldAll;
		"strummableMetals" strum(3) yieldAll;
		"strummablePipes" strum(3) yieldAll;
		"strummableSilk" strum(3) yieldAll;

		ae.endRender(0.1);
		"done playing SC2 examples-11" println;
	}());
}

fn renderSc11() {
	let h = "/tmp/sc2_examples_11.wav" ae.renderNRT(160, playAllSc11);
	await ae.renderDone(h);
	"render done" println;
}

--renderSc11();
--playAllSc11();
