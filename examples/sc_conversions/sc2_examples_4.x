-- Tzopilotl translations of SuperCollider examples from
-- sc-examples/SC2-examples/examples-4.txt. Shared machinery lives in
-- sc2_common.x; sc2_examples_1.x explains the texture timing model.
--
-- Status (examples-4.txt blocks, in order):
--   1  Berlin 1977              -- DONE (berlin1977)
--   2  metal plate              -- DONE (metalPlate)
--   3  Griot modeling           -- DONE (griot) -- its Spawn function is a
--                                  script (a melody of string numbers), so
--                                  a script coroutine drives a 3-voice
--                                  exciter inside the string network
--   4  sample and hold liquid.  -- DONE (liquidities)
--   5  random panning sines     -- DONE (randomPanningSines)
-- The mouse-driven ones read the shared-input mouse; headless the mouse sits
-- at its low end.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import filters.*;
import audio_engine as ae;
import clock.*;
import sc2_common.*;

let tags = ["sc2-examples-4"];

fn xenv(trans Float) S = linen(gate(), trans, trans) rpanfun;

---------------------------------------------------------------------------
-- 1. Berlin 1977 (examples-4.txt:16-56)
-- Clock from MouseX 5..20 Hz; an 8-note Sequencer, transposed every 16
-- beats by a random choice from [-12, -7, -5, 0, 2, 5] (a Sequencer whose
-- "pattern" is a function, clocked by PulseDivider(clock, 16)); Decay2
-- envelopes on amplitude (0.1 + 0.02 floor) and filter cutoff (FSinOsc-scaled
-- 800 + 1400); LFPulse with a stereo pair of width LFOs (phases 0 and pi/2)
-- into RLPF (1/Q 0.15) into CombN(0.2, [0.2, 0.17], 1.5).
-- PulseDivider is `clock * ((clock trCount) % 16 == 1)`; the per-trigger
-- random choice is a per-sample irand latched by sampleAndHold on that
-- divided clock. Decay2.kr(in, attack, decay, mul, add) is SC2's order.

fn berlin1977() S {
	let clockRate = mouseX(5, 20);
	let clockTime = 1 / clockRate;
	let clock = lfimp(clockRate);
	let pattern = [55, 60, 63, 62, 60, 67, 63, 58] vec;
	let base = seq(clock, pattern, 8);
	let div = clock * (((clock trCount) % 16) == 1);
	let transposes = [-12, -7, -5, 0, 2, 5] vec;
	let transpose = (transposes at(irand(0, 5, 1))) sampleAndHold(div);
	let freq = (base + transpose) nnhz;
	let amp = (clock decay2(0.05 * clockTime, 2 * clockTime)) * 0.1 + 0.02;
	let filt = (clock decay2(0.05 * clockTime, 2 * clockTime)) * ((0.17 fsinosc) * 800) + 1400;
	let width = ((0.08 asSignal) sinosc([0.0, 0.25] vec)) * 0.45 + 0.5;
	let osc = (freq lfupulse(width)) * amp;
	let filtered = osc rlpf(filt, 0.15);
	(filtered comb([0.2, 0.17] vec, 0.2, 1.5, Interpolation.none)) gainOutlet
}

berlin1977 defSynthX("berlin1977", tags) await;

---------------------------------------------------------------------------
-- 2. metal plate (examples-4.txt:60-91)
-- Four 30 ms delay lines tapped at random 15..30 ms, each tap lowpassed
-- (MouseX 10..5000 Hz) and scaled 0.98, written back plus a shared
-- excitation Decay2(Impulse 0.5 * 0.2, 0.01, 0.2) * LFNoise2(MouseY 10..8000);
-- output is the mix of the filtered taps. A single 4-channel delayVar with
-- per-channel variable-read taps is the whole network.

fn metalPlate() S {
	let n = 4;
	let taps = (0.015 + urand(n, Rate.init) * 0.015) * fs();
	let exc = ((lfimp(0.5) * 0.2) decay2(0.01, 0.2)) * (mouseY(10, 8000) lfnoise3(1));
	let d = delayVar(0.03 * fs());
	let tapped = d(taps, Interpolation.none);
	let filt = (tapped lpf(mouseX(10, 5000))) * 0.98;
	d <- filt + exc;
	(filt sum) gainOutlet
}

metalPlate defSynthX("metalPlate", tags) await;

---------------------------------------------------------------------------
-- 3. Griot modeling (examples-4.txt:95-146)
-- Five 10 ms delay lines ("strings") tapped at 3, 3.5, 4, 4.5, 5 ms with the
-- same lowpass feedback as #2; the excitation is a Spawn every 0.1 s of a
-- 0.21 s LFNoise2(MouseY 10..10000) burst (Env [0,1,0] [0.01, 0.2], amp
-- rand 0.1) sent into ONE string, chosen from a 12-note melody of string
-- numbers (a random walk) regenerated every 144 notes.
-- The Spawn function is a script, so it stays one: `griotSpawner` builds
-- the melody and sends noteOn(string, amp) to a 3-voice exciter voicer
-- inside the synth (3 covers a 0.21 s life at 0.1 s spacing); each voice
-- emits its burst on the 5-channel exciter bus at its string's index, and
-- `sum(5)` folds the voices onto the bus per string.

const kGriotStrings = 5;
const kGriotVoices = 3;

fn griotVoice() S {
	let g = gate();
	let string = noteParam("string", ControlSpec{0.0, 4.0, 2.0, ControlWarp.linear});
	let amp = noteParam("amp", ControlSpec{0.0, 0.1, 0.05, ControlWarp.linear});
	-- EnvGen(Env([0,1,0], [0.01, 0.2], -2)): a one-shot percussive attack-
	-- decay fired from the note-on edge (percEnv in sc2_common), independent
	-- of the gate hold. (An open question remains about SC2's LFNoise2
	-- spectrum at audio rates -- see police state in sc2_examples_2.x.)
	let burst = percEnv(g rising, 0.01, 0.2, -2.0) * (mouseY(10, 10000) lfnoise3(1)) * amp;
	burst * (([0, 1, 2, 3, 4] vec) == string)
}

fn griot() S {
	let exc = voicer(kGriotVoices, griotVoice) sum(kGriotStrings);
	let taps = ([0.003, 0.0035, 0.004, 0.0045, 0.005] vec) * fs();
	let d = delayVar(0.01 * fs());
	let tapped = d(taps, Interpolation.none);
	let filt = (tapped lpf(mouseX(10, 10000))) * 0.98;
	d <- filt + exc;
	let out = filt sum;
	([out, out] join) gainOutlet
}

griot defSynthX("griot", tags) await;

-- A 12-note melody of string numbers: a random walk that moves (with
-- probability 0.4) by -1, 0 or +1 and wraps 0..4.
fn griotMelody() [Float] {
	var chan = 2.0;
	var m [Float] = [];
	for (i : (1..12)) {
		let c Float = urand();
		if (c < 0.4) {
			let u Float = urand();
			if (u < 0.3333) { chan = chan - 1.0; }
			if (u > 0.6667) { chan = chan + 1.0; }
			if (chan < 0.0) { chan = chan + 5.0; }
			if (chan > 4.0) { chan = chan - 5.0; }
		}
		m push!(chan);
	}
	m
}

coro fn griotSpawner(nodeID Int, totalDur Float) Float {
	var melody = griotMelody();
	var i = 0;
	var elapsed = 0.0;
	while (elapsed < totalDur) {
		if (i > 0 && i % 144 == 0) { melody = griotMelody(); }
		let noteID = i % kGriotVoices;
		let amp Float = urand() * 0.1;
		ae.begin(); ae.noteOn(nodeID, noteID, [melody[i % 12], amp, 1.0]); ae.sched(0);
		go(coro fn() Float {
			yield 0.01;
			ae.begin(); ae.noteOff(nodeID, noteID); ae.sched(0);
		}());
		i = i + 1;
		elapsed = elapsed + 0.1;
		yield 0.1;
	}
}

---------------------------------------------------------------------------
-- 4. sample and hold liquidities (examples-4.txt:150-176)
-- Clock Impulse(MouseX 1..200 Hz exp) * 0.4; frequency = WhiteNoise around
-- MouseY's centre (100..8000 Hz exp, +/- half) latched on the clock; pan
-- likewise; SinOsc with a Decay2 per clock (0.1 / 0.9 of the period) into
-- CombN(0.3, 0.3, 2).

fn liquidities() S {
	let clockRate = mouseXExp(1, 200);
	let clockTime = 1 / clockRate;
	let clock = lfimp(clockRate) * 0.4;
	let cf = mouseYExp(100, 8000);
	let freq = (white() * cf * 0.5 + cf) sampleAndHold(clock);
	let panPos = white() sampleAndHold(clock);
	let sig = (freq sinosc) * (clock decay2(0.1 * clockTime, 0.9 * clockTime));
	((sig pan(panPos) join) combn(0.3, 2)) gainOutlet
}

liquidities defSynthX("liquidities", tags) await;

---------------------------------------------------------------------------
-- 5. random panning sines (examples-4.txt:180-193)
-- XFadeTexture(.., 8, 8, 2): eight FSinOscs at 80 + linrand 2000, each with
-- its own LFNoise1 amplitude (0.4 + rand 0.8 Hz, 0.1..0.9) and LFNoise1 pan,
-- mixed at 0.4/n.

const kRpTrans = 8.0;
const kRpHold = 16.0;
const kRpInterval = 16.0;
const kRpVoices = 2;

fn randomPanningVoice() S {
	let n = 8;
	let freqs = linrandv(80, 2080, n, Rate.reset);
	let amps = ((0.4 + urand(n, Rate.reset) * 0.8) lfnoise1(n)) * 0.4 + 0.5;
	let pans = (0.4 + urand(n, Rate.reset) * 0.8) lfnoise1(n);
	let mix = (((freqs fsinosc) * amps) pan(pans) join) transpose(n) sum(2);
	mix * (0.4 / n) * xenv(kRpTrans)
}

fn randomPanningSines() S = voicer(kRpVoices, randomPanningVoice) sum(2) gainOutlet;

randomPanningSines defSynthX("randomPanningSines", tags) await;

---------------------------------------------------------------------------
-- driver

fn playAllSc4() {
	go(coro fn() Float {
		"start playing SC2 examples-4" println;
		-- Headless there is no mouse poller and the shared-input slots read 0,
		-- which pins every mouse-driven parameter to its low end (10 Hz
		-- damping filters, 10 Hz noise...). Park the mouse mid-screen.
		ae.setSharedInput(0, 0.5);
		ae.setSharedInput(1, 0.5);

		"berlin1977" playFor(20.0) yieldAll;
		"metalPlate" playFor(12.0) yieldAll;

		"-- griot --" println;
		let griotNode = "griot" play;
		griotSpawner(griotNode, 20.0) yieldAll;
		yield 0.5;
		griotNode stop;

		"liquidities" playFor(10.0) yieldAll;
		"randomPanningSines" texture(kRpVoices, kRpInterval, kRpHold, 32.0, kRpTrans) yieldAll;

		ae.endRender(0.1);
		"done playing SC2 examples-4" println;
	}());
}

fn renderSc4() {
	let h = "/tmp/sc2_examples_4.wav" ae.renderNRT(200, playAllSc4);
	await ae.renderDone(h);
	"render done" println;
}

--renderSc4();
--playAllSc4();
