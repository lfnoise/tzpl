-- Tzopilotl translations of SuperCollider examples from
-- sc-examples/SC2-examples/examples-3.txt. Shared machinery lives in
-- sc2_common.x; sc2_examples_1.x explains the texture timing model.
--
-- Status (examples-3.txt blocks, in order):
--   1  analogue daze            -- DONE (analogueDaze)
--   2  analogue daze, condensed -- same patch as 1; one translation
--   3  native algorhythms       -- DONE (nativeAlgorhythms) -- Prand over
--                                  Pseqs approximated: one of the nine
--                                  patterns per texture instance
--   4  synthetic piano          -- DONE (syntheticPiano)
--   5  piano excitation plot    -- skipped (Synth.plot demo of #4's exciter)
--   6  reverberated sine perc.  -- DONE (sinePercolation)
--   7  reverberated noise bursts-- DONE (noiseBurstsReverb)
--   8  Mouse control            -- DONE (mouseSine)
--   9  analog bubbles w/ mouse  -- DONE (mouseBubbles)

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import filters.*;
import audio_engine as ae;
import clock.*;
import sc2_common.*;

let tags = ["sc2-examples-3"];

fn xenv(trans Float) S = linen(gate(), trans, trans) rpanfun;

---------------------------------------------------------------------------
-- 1/2. analogue daze (examples-3.txt:15-86, condensed at 94-118)
-- Two copies of a VCO->VCF voice: an 8-step Sequencer of MIDI notes (offset
-- by octaves) clocked by Impulse.kr, lagged 50 ms into LFPulse (width swept
-- by a slow SinOsc 0.1..0.9), into RLPF (cutoff swept 600..3400, 1/Q = 1/15);
-- plus an analogue snare (Decay(Impulse 2 Hz, 0.15) * LFNoise0 whose rate
-- LFNoise1-sweeps 2..14 kHz). Dry pair reversed + CombN(3/8 s, 5 s), all
-- under a one-minute Env.linen(2, 56, 2).
-- Sequencer.kr(list, trig) is `seq(trig, pattern, length)`; the SC2
-- per-voice random LFO phases (2pi.rand) are Rate.init draws.

fn anaSyn(octave Int, clockRate Float, pwmRate Float, fltRate Float) S {
	let pattern = ([55, 63, 60, 63, 57, 65, 62, 65] vec) + 12 * octave;
	let freq = (seq(lfimp(clockRate), pattern, 8) nnhz) lag(0.05);
	let width = (pwmRate sinosc(urand(1, Rate.init) * twopi)) * 0.4 + 0.5;
	let cutoff = (fltRate sinosc(urand(1, Rate.init) * twopi)) * 1400 + 2000;
	((freq lfupulse(width)) * 0.1) rlpf(cutoff, 1.0 / 15.0)
}

fn analogueDaze() S {
	let snareRate = ((0.3 asSignal) lfnoise1(1)) * 6000 + 8000;
	let snare = (lfimp(2) decay(0.15)) * ((snareRate lfnoise0(2)) * 0.07);
	let g = ([anaSyn(1, 8.0, 0.31, 0.2), anaSyn(0, 2.0, 0.13, 0.11)] join) + snare;
	let z = 0.4 * ((g combn(0.375, 5)) + (g reverse));
	((z fadein(2)) fadeout(58, 2)) gainOutlet
}

analogueDaze defSynthX("analogueDaze", tags) await;

---------------------------------------------------------------------------
-- 3. native algorhythms (examples-3.txt:123-170)
-- OverlapTexture(.., 8, 4, 4, 2): sustain 8, transition 4, 4 overlapping.
-- Each instance: a random base frequency 40..340, an ImpulseSequencer at
-- 10 Hz stepping a rhythm pattern whose values are the impulse amplitudes,
-- Decay(.., 0.1) * PinkNoise * 0.01 exciting an 8-mode Klank at
-- freq + linrand(4 freq), ring 0.2 + linrand 3, panned.
-- SC's Prand(Pseq...) stream picks a new pattern each time one finishes; here
-- one of the nine is chosen per instance (Rate.reset) and looped for its
-- 12 s -- the 8-step patterns are written out twice so all nine are 16 steps.

const kNativeTrans = 4.0;
const kNativeHold = 12.0;
const kNativeInterval = 3.0;     -- (8 + 4) / 4
const kNativeVoices = 6;         -- ceil(life 16 / 3)

fn nativeVoice() S {
	let n = 8;
	let freq = 40 + urand(1, Rate.reset) * 300;
	let patterns = [
		[2.0, 0.0, 2.0, 0.0, 1.0, 0.0, 1.0, 1.0,  2.0, 0.0, 2.0, 0.0, 1.0, 0.0, 1.0, 1.0] vec,
		[2.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0,  2.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0] vec,
		[2.0, 0.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0,  2.0, 0.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0] vec,
		[2.0, 0.3, 0.3, 1.0, 0.3, 0.3, 1.0, 0.3,  2.0, 0.3, 0.3, 1.0, 0.3, 0.3, 1.0, 0.3] vec,
		[2.0, 0.0, 0.3, 0.0, 0.3, 0.0, 0.3, 0.0,  2.0, 0.0, 0.3, 0.0, 0.3, 0.0, 0.3, 0.0] vec,
		[2.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0,  2.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0] vec,
		[2.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,  2.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0] vec,
		[0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0,  0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0] vec,
		[1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0,  0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0] vec
	];
	let pat = select(irand(0, 8, 1, Rate.reset), patterns);
	let trig = iseq(lfimp(10), pat, 16);
	let exc = (trig decay(0.1)) * pink() * 0.01;
	let freqs = linrandv(freq, 5 * freq, n, Rate.reset);
	let rings = linrandv(0.2, 3.2, n, Rate.reset);
	(klank(exc, freqs, 1, rings) * xenv(kNativeTrans)) pan(birand(1, Rate.reset)) join
}

fn nativeAlgorhythms() S = voicer(kNativeVoices, nativeVoice) sum(2) gainOutlet;

nativeAlgorhythms defSynthX("nativeAlgorhythms", tags) await;

---------------------------------------------------------------------------
-- 4. synthetic piano (examples-3.txt:177-206)
-- Six keys, each a random MIDI pitch 36..89 struck by Impulse(0.1 + rand 0.4)
-- * 0.1 through Decay2(0.008, 0.04); three detuned strings per key (-0.05,
-- 0, +0.04 semitones), each a CombL tuned to 1/freq with a 6 s decay and its
-- own LFNoise2(3000) hammer noise; panned low-left to high-right. All the
-- randomness is fixed when the synth is built (Rate.init). The comb's max
-- delay is the lowest key's period (MIDI 36 = 65 Hz -> 15 ms; 20 ms used).

fn syntheticPiano() S {
	let n = 6;
	-- irand is integer-typed; the signal type system has no implicit int ->
	-- float promotion (a float `detune` added to it fails inference), so cast.
	let pitch = irand(36, 89, n, Rate.init) f32;
	let strike = lfimp(0.1 + urand(n, Rate.init) * 0.4) * 0.1;
	let hammerEnv = strike decay2(0.008, 0.04);
	var strings = 0 asSignal;
	for (detune : [-0.05, 0.0, 0.04]) {
		let delayTime = 1.0 / ((pitch + detune) nnhz);
		let hammer = ((3000 asSignal) lfnoise3(n)) * hammerEnv;
		strings = strings + (hammer combl(delayTime, 0.02, 6));
	}
	let panPos = (pitch - 36) / 27.0 - 1;
	(strings pan(panPos) join) transpose(n) sum(2) gainOutlet
}

syntheticPiano defSynthX("syntheticPiano", tags) await;

---------------------------------------------------------------------------
-- 6. reverberated sine percussion (examples-3.txt:224-245)
-- Ten percolators: Resonz(Dust(2/d) * 50, 200 + rand 3000, bwr 0.003), summed;
-- a 48 ms predelay; seven parallel CombL(0.1 max, LFNoise1(rand 0.1) * 0.04 +
-- 0.05, 15 s) summed; then four AllpassN(0.05 max, [rand 0.05, rand 0.05], 1)
-- in series -- which makes the reverb stereo. Dry + 0.2 * wet.

-- (reverbTail lives in sc2_common.x; file 5's reverbs use it too.)

fn sinePercolation() S {
	let d = 10;
	let s = resonz(dust(2.0 / d, d) * 50, 200 + urand(d, Rate.init) * 3000, 0.003) sum;
	let y = reverbTail(s delayn(0.048, 0.048), 7);
	(s + 0.2 * y) gainOutlet
}

sinePercolation defSynthX("sinePercolation", tags) await;

---------------------------------------------------------------------------
-- 7. reverberated noise bursts (examples-3.txt:249-267)
-- Decay(Dust 0.6 * 0.2, 0.15) * PinkNoise into the same reverb with six
-- combs; dry + wet.

fn noiseBurstsReverb() S {
	let s = ((dust(0.6) * 0.2) decay(0.15)) * pink();
	let y = reverbTail(s delayn(0.048, 0.048), 6);
	(s + y) gainOutlet
}

noiseBurstsReverb defSynthX("noiseBurstsReverb", tags) await;

---------------------------------------------------------------------------
-- 8. Mouse control (examples-3.txt:271-275)
-- SinOsc at MouseX 200..2000 Hz (exponential), amplitude 0.4.

fn mouseSine() S = ((mouseXExp(200, 2000) sinosc) * 0.4) gainOutlet;

mouseSine defSynthX("mouseSine", tags) await;

---------------------------------------------------------------------------
-- 9. analog bubbles - with mouse control (examples-3.txt:279-293)
-- LFSaw(freq, mul, add): lfo 1 rate from MouseY 0.1..10 Hz, depth 24
-- semitones, offset by lfo 2 (rate MouseX 2..40 Hz, depth -3, offset 80),
-- to Hz, into a sine with a 0.2 s / 2 s comb echo.

fn mouseBubbles() S {
	let lfo2 = 80 - (mouseXExp(2, 40) lfsaw) * 3;
	let freq = ((mouseYExp(0.1, 10) lfsaw) * 24 + lfo2) nnhz;
	(((freq sinosc) * 0.04) combn(0.2, 2)) gainOutlet
}

mouseBubbles defSynthX("mouseBubbles", tags) await;

---------------------------------------------------------------------------
-- driver

fn playAllSc3() {
	go(coro fn() Float {
		"start playing SC2 examples-3" println;
		-- Headless the mouse slots read 0; park the mouse mid-screen so the
		-- mouse examples (#8, #9) render at mid-range values.
		ae.setSharedInput(0, 0.5);
		ae.setSharedInput(1, 0.5);

		"analogueDaze" playFor(62.0) yieldAll;
		"nativeAlgorhythms" texture(kNativeVoices, kNativeInterval, kNativeHold, 18.0, kNativeTrans) yieldAll;
		"syntheticPiano" playFor(20.0) yieldAll;
		"sinePercolation" playFor(16.0) yieldAll;
		"noiseBurstsReverb" playFor(16.0) yieldAll;
		"mouseSine" playFor(5.0) yieldAll;
		"mouseBubbles" playFor(8.0) yieldAll;

		ae.endRender(0.1);
		"done playing SC2 examples-3" println;
	}());
}

fn renderSc3() {
	let h = "/tmp/sc2_examples_3.wav" ae.renderNRT(240, playAllSc3);
	await ae.renderDone(h);
	"render done" println;
}

--renderSc3();
--playAllSc3();
