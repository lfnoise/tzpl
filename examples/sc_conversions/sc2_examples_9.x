-- Tzopilotl translations of SuperCollider examples from
-- sc-examples/SC2-examples/examples-9.txt. Shared machinery lives in
-- sc2_common.x; sc2_examples_1.x explains the texture timing model.
--
-- Status (examples-9.txt blocks, in order):
--   1  deep trip                  -- DONE (deepTrip)
--   2  percussion solo in 10/8    -- DONE (percussionSolo) -- the Pseq/Prand
--                                    stream is a script (percussionSpawner)
--                                    stepping a 4-voice hit voicer at 1/8 s
--   3  sawed cymbals              -- DONE (sawedCymbals)
--   4  sidereal time              -- DONE (siderealTime)
--   5  pentatonic pipes           -- DONE (pentatonicPipes) -- the 20 s root
--                                    change and degreeToKey are script-side
--   6  ostinoodles                -- DONE (ostinoodles) -- scrambled 4-note
--                                    sequence computed per spawn, 4 noteParams
--   7  bowed garage door springs  -- DONE (garageSprings)
--   8  contamination zone         -- DONE (contaminationZone)
--   9  array manipulation         -- DONE (arrayManipulation) -- `scramble`
--                                    approximated by a random channel rotation
--                                    per instance (a rotation, not a permutation)

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import filters.*;
import audio_engine as ae;
import clock.*;
import sc2_common.*;

let tags = ["sc2-examples-9"];

fn xenv(trans Float) S = linen(gate(), trans, trans) rpanfun;

-- Six AllpassN(0.04 max, [rand 0.04, rand 0.04], 16) stages in series, as
-- #6, #7 and #8 apply to their summed texture.
fn allpassChain6(z S) S {
	var y = z;
	for (i : (1..6)) {
		y = y alpasn(urand(2, Rate.init) * 0.04, 16);
	}
	y
}

-- degreeToKey(scale, 12): scale degree (may be negative) -> semitones.
fn degreeToKey(degree Int, scale [Int]) Float {
	let n = scale length;
	var d = degree;
	var oct = 0;
	while (d < 0) { d = d + n; oct = oct - 1; }
	while (d >= n) { d = d - n; oct = oct + 1; }
	(scale[d] + 12 * oct) toFloat
}

---------------------------------------------------------------------------
-- 1. deep trip (examples-9.txt:4-20)
-- OverlapTexture(.., 12, 4, 4, 2): a sine at a slowly wandering pitch
-- (LFNoise1(rand 0.3) * 60 + 70 midi), amplitude-modulated by LFNoise2 at
-- f * rand 0.5 whose own amplitude is max(0, LFNoise1(rand 8) * (SinOsc(rand 40)
-- * 0.1)); panned by LFNoise1(rand 5); plus two 20 s CombNs at random
-- 0.3..0.5 s stereo delays. All per instance.

const kTripTrans = 4.0;
const kTripHold = 16.0;
const kTripInterval = 4.0;      -- 16 / 4
const kTripVoices = 5;          -- ceil(life 20 / 4)

fn deepTripVoice() S {
	let f = (((urand(1, Rate.reset) * 0.3) lfnoise1(1)) * 60 + 70) nnhz;
	let amp = (((urand(1, Rate.reset) * 8) lfnoise1(1)) * (((urand(1, Rate.reset) * 40) sinosc) * 0.1)) max(0);
	let noise = ((f * (urand(1, Rate.reset) * 0.5)) lfnoise3(1)) * amp;
	let z = (f sinosc) * noise;
	let s = z pan((urand(1, Rate.reset) * 5) lfnoise1(1)) join;
	let dt1 = 0.3 + urand(2, Rate.reset) * 0.2;
	let dt2 = 0.3 + urand(2, Rate.reset) * 0.2;
	(s + (s comb(dt1, 0.5, 20, Interpolation.none)) + (s comb(dt2, 0.5, 20, Interpolation.none))) * xenv(kTripTrans)
}

fn deepTrip() S = voicer(kTripVoices, deepTripVoice) sum(2) gainOutlet;

deepTrip defSynthX("deepTrip", tags) await;

---------------------------------------------------------------------------
-- 2. percussion solo in 10/8 (examples-9.txt:24-83)
-- A pattern stream (10 beats of rest, an intro, 30 random bars from 14
-- patterns, a 3x7-beat tehai, the sam) stepped every 1/8 s; each nonzero
-- value gates a hit: Resonz(Decay2(trig, 0.002, 0.1) * WhiteNoise * 70,
-- 60.midicps, 0.02, mul 4).distort * 0.4, over a drone of two detuned saw
-- pairs (60, 67) through LPF(108.midicps) * 0.007.
-- The stream is a script (percussionSpawner) driving a 4-voice hit voicer
-- with the value as a noteParam; the drone sits outside the voicer.

const kPercVoices = 4;

fn percHitVoice() S {
	let g = gate();
	let v = noteParam("v", ControlSpec{0.0, 5.0, 1.0, ControlWarp.linear});
	let env = ((g rising) * v) decay2(0.002, 0.1);
	let hit = resonz(env * (white() * 70), 60 nnhz, 0.02) * 4;
	(hit distort) * 0.4
}

fn percussionSolo() S {
	let hits = voicer(kPercVoices, percHitVoice) sum;
	let drone = ((([60, 60.04] vec) nnhz lfsaw) + (([67, 67.04] vec) nnhz lfsaw)) lpf(108 nnhz) * 0.007;
	(hits + drone) gainOutlet
}

percussionSolo defSynthX("percussionSolo", tags) await;

fn percussionStream() [Float] {
	var s [Float] = [];
	for (i : (1..10)) { s push!(0.0); }
	let intro = [
		[0.9, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0],
		[0.9, 0.0, 0.0, 0.2, 0.0, 0.0, 0.0, 0.2, 0.0, 0.0],
		[0.9, 0.0, 0.0, 0.2, 0.0, 0.2, 0.0, 0.2, 0.0, 0.0],
		[0.9, 0.0, 0.0, 0.2, 0.0, 0.0, 0.0, 0.2, 0.0, 0.2]
	];
	for (p : intro) { for (rep : (1..2)) { for (v : p) { s push!(v); } } }
	let solo = [
		[0.9, 0.0, 0.0, 0.7, 0.0, 0.2, 0.0, 0.7, 0.0, 0.0],
		[0.9, 0.2, 0.0, 0.7, 0.0, 0.2, 0.0, 0.7, 0.0, 0.0],
		[0.9, 0.0, 0.0, 0.7, 0.0, 0.2, 0.0, 0.7, 0.0, 0.2],
		[0.9, 0.0, 0.0, 0.7, 0.2, 0.2, 0.0, 0.7, 0.0, 0.0],
		[0.9, 0.0, 0.0, 0.7, 0.0, 0.2, 0.2, 0.7, 0.2, 0.0],
		[0.9, 0.2, 0.2, 0.7, 0.2, 0.2, 0.2, 0.7, 0.2, 0.2],
		[0.9, 0.2, 0.2, 0.7, 0.2, 0.2, 0.2, 0.7, 0.0, 0.0],
		[0.9, 0.0, 0.0, 0.7, 0.2, 0.2, 0.2, 0.7, 0.0, 0.0],
		[0.9, 0.0, 0.4, 0.0, 0.4, 0.0, 0.4, 0.0, 0.4, 0.0],
		[0.9, 0.0, 0.0, 0.4, 0.0, 0.0, 0.4, 0.2, 0.4, 0.2],
		[0.9, 0.0, 0.2, 0.7, 0.0, 0.2, 0.0, 0.7, 0.0, 0.0],
		[0.9, 0.0, 0.0, 0.7, 0.0, 0.0, 0.0, 0.7, 0.0, 0.0],
		[0.9, 0.7, 0.7, 0.0, 0.0, 0.2, 0.2, 0.2, 0.0, 0.0],
		[0.9, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
	];
	for (bar : (1..30)) {
		let k Int = irand(0, 13);
		for (v : solo[k]) { s push!(v); }
	}
	let tehai = [2.0, 0.0, 0.2, 0.5, 0.0, 0.2, 0.9,
	             1.5, 0.0, 0.2, 0.5, 0.0, 0.2, 0.9,
	             1.5, 0.0, 0.2, 0.5, 0.0, 0.2];
	for (rep : (1..3)) { for (v : tehai) { s push!(v); } }
	s push!(5.0);
	s
}

coro fn percussionSpawner(nodeID Int) Float {
	let stream = percussionStream();
	var id = 0;
	for (v : stream) {
		if (v > 0.0) {
			let noteID = id;
			ae.begin(); ae.noteOn(nodeID, noteID, [v, 1.0]); ae.sched(0);
			go(coro fn() Float {
				yield 0.05;
				ae.begin(); ae.noteOff(nodeID, noteID); ae.sched(0);
			}());
			id = (id + 1) % kPercVoices;
		}
		yield 0.125;
	}
}

---------------------------------------------------------------------------
-- 3. sawed cymbals (examples-9.txt:87-105)
-- OverlapTexture(.., 4, 4, 6, 2): two 15-mode Klanks in [f1, f1 + f2), ring
-- 2 + rand 4, excited by LFSaw(XLine(rand 600, rand 600, 12)) * 0.0005.

const kCymTrans = 4.0;
const kCymHold = 8.0;
const kCymInterval = 1.3333333;  -- 8 / 6
const kCymVoices = 9;            -- ceil(life 12 / 1.333)

fn sawedCymbalsVoice() S {
	let p = 15;
	let f1 = 500 + urand(1, Rate.reset) * 2000;
	let f2 = urand(1, Rate.reset) * 8000;
	let freqs = f1 + urand(2 * p, Rate.reset) * f2;
	let rings = 2 + urand(2 * p, Rate.reset) * 4;
	let exc = (xline(1 + urand(1, Rate.reset) * 599, 1 + urand(1, Rate.reset) * 599, 12) lfsaw) * 0.0005;
	(klankModes(exc, freqs, 1, rings) sum(2)) * xenv(kCymTrans)
}

fn sawedCymbals() S = voicer(kCymVoices, sawedCymbalsVoice) sum(2) gainOutlet;

sawedCymbals defSynthX("sawedCymbals", tags) await;

---------------------------------------------------------------------------
-- 4. sidereal time (examples-9.txt:109-131)
-- Same texture: modes exprand(100, 6000), ring 2 + rand 4; exciter LFPulse
-- at XLine(exprand(40,300) -> exprand(40,300), 12), width rrand(0.1, 0.9),
-- amplitude 0.002 * max(0, LFNoise2(rand 8)); Klank.distort * 0.1, then a
-- per-instance CombN(0.6, 0.1 + rand 0.5, 8) mixed with the reversed dry.

fn siderealVoice() S {
	let p = 15;
	let freqs = exprand(100, 6000, 2 * p, Rate.reset);
	let rings = 2 + urand(2 * p, Rate.reset) * 4;
	let f3 = xline(exprand(40, 300, 1, Rate.reset), exprand(40, 300, 1, Rate.reset), 12);
	let amp = 0.002 * ((((urand(1, Rate.reset) * 8) lfnoise3(1))) max(0));
	let in = (f3 lfupulse(0.1 + urand(1, Rate.reset) * 0.8)) * amp;
	let out = ((klankModes(in, freqs, 1, rings) sum(2)) distort) * 0.1;
	let dt = 0.1 + urand(1, Rate.reset) * 0.5;
	((out comb(dt, 0.6, 8, Interpolation.none)) + (out reverse)) * xenv(kCymTrans)
}

fn siderealTime() S = voicer(kCymVoices, siderealVoice) sum(2) gainOutlet;

siderealTime defSynthX("siderealTime", tags) await;

---------------------------------------------------------------------------
-- 5. pentatonic pipes (examples-9.txt:135-164)
-- OverlapTexture(.., 10, 0.01, 5, 2): each instance is PinkNoise under a
-- one-shot Env.linen(0.2, 8, 1, level 20, 'sine') into Resonz(f, 0.002,
-- mul 4).distort * 0.2, panned; f = degreeToKey(rand 20, [0,3,5,7,10]) +
-- root, with root = 36 + rand 12 re-chosen every 20 s -- both script-side,
-- f arriving as a noteParam. The 9.2 s envelope is driven by the note gate
-- (hold = rise + sustain), the texture's 10 ms fade is negligible. Right
-- half of the screen (MouseX > 0.5) pulses the output with max(0, SinOsc 5 Hz).
-- Then CombN(0.3, 0.3, 8) mixed with the reversed dry signal.

const kPipesHold = 8.2;          -- rise 0.2 + sustain 8
const kPipesFall = 1.0;
const kPipesInterval = 2.0;      -- (10 + 0.01) / 5
const kPipesVoices = 6;          -- ceil(life 9.2 / 2)

fn pentatonicPipesVoice() S {
	let g = gate();
	let f = noteParam("freq", ControlSpec{20.0, 5000.0, 220.0, ControlWarp.exponential});
	let e = linen(g, 0.2, kPipesFall);
	let amp = ((1 - (e cospi)) * 0.5) * 20;          -- 'sine' segment shape
	let sig = (resonz(pink() * amp, f, 0.002) * 4) distort * 0.2;
	sig pan(birand(1, Rate.reset)) join
}

fn pentatonicPipes() S {
	let out = voicer(kPipesVoices, pentatonicPipesVoice) sum(2);
	let pulsed = out * select2(mouseX(0, 1) > 0.5, max(0, 5 sinosc), 1);
	((pulsed combn(0.3, 8)) + (pulsed reverse)) gainOutlet
}

pentatonicPipes defSynthX("pentatonicPipes", tags) await;

coro fn pentatonicSpawner(nodeID Int, totalDur Float) Float {
	let mode = [0, 3, 5, 7, 10];
	var root Int = 36 + irand(0, 11);
	var id = 0;
	var elapsed = 0.0;
	var sinceRoot = 0.0;
	while (elapsed < totalDur) {
		if (sinceRoot >= 20.0) { root = 36 + irand(0, 11); sinceRoot = 0.0; }
		let degree Int = irand(0, 19);
		let midi = degreeToKey(degree, mode) + root toFloat;
		let freq = 440.0 * exp((midi - 69.0) / 12.0 * log(2.0));
		let noteID = id;
		ae.begin(); ae.noteOn(nodeID, noteID, [freq, 1.0]); ae.sched(0);
		go(coro fn() Float {
			yield kPipesHold;
			ae.begin(); ae.noteOff(nodeID, noteID); ae.sched(0);
		}());
		id = (id + 1) % kPipesVoices;
		elapsed = elapsed + kPipesInterval;
		sinceRoot = sinceRoot + kPipesInterval;
		yield kPipesInterval;
	}
}

---------------------------------------------------------------------------
-- 6. ostinoodles (examples-9.txt:168-192)
-- OverlapTexture(.., 6, 3, 6, 2): per instance a scrambled 4-note sequence
-- (degrees [0..3] + rand2 16 in the major scale on root = 81 + rand2 6) stepped
-- by Impulse at XLine(exprand(4,24) -> exprand(4,24), 12) into an LFTri with a
-- Decay2(0.004, 0.3) * 0.1 envelope per step, panned; then six AllpassN
-- stages. The scramble happens in the spawner; the four notes are noteParams.

const kOstTrans = 3.0;
const kOstHold = 9.0;
const kOstInterval = 1.5;        -- 9 / 6
const kOstVoices = 8;            -- ceil(life 12 / 1.5)

fn ostinoodlesVoice() S {
	let n0 = noteParam("n0", ControlSpec{20.0, 5000.0, 440.0, ControlWarp.exponential});
	let n1 = noteParam("n1", ControlSpec{20.0, 5000.0, 440.0, ControlWarp.exponential});
	let n2 = noteParam("n2", ControlSpec{20.0, 5000.0, 440.0, ControlWarp.exponential});
	let n3 = noteParam("n3", ControlSpec{20.0, 5000.0, 440.0, ControlWarp.exponential});
	let trig = lfimp(xline(exprand(4, 24, 1, Rate.reset), exprand(4, 24, 1, Rate.reset), 12));
	let f = seq(trig, [n0, n1, n2, n3] join, 4);
	let env = (trig decay2(0.004, 0.3)) * 0.1;
	(((f lftri) * env) * xenv(kOstTrans)) pan(birand(1, Rate.reset)) join
}

fn ostinoodles() S = allpassChain6(voicer(kOstVoices, ostinoodlesVoice) sum(2)) gainOutlet;

ostinoodles defSynthX("ostinoodles", tags) await;

coro fn ostinoodlesSpawner(nodeID Int, totalDur Float) Float {
	let major = [0, 2, 4, 5, 7, 9, 11];
	let root Int = 81 + irand(-6, 6);
	var id = 0;
	var elapsed = 0.0;
	while (elapsed < totalDur) {
		let offset Int = irand(-16, 16);
		-- scramble [0,1,2,3] + offset: a random permutation by swaps
		var degs = [offset, offset + 1, offset + 2, offset + 3];
		for (i : (0..3)) {
			let j Int = irand(0, 3);
			let t = degs[i]; degs[i] = degs[j]; degs[j] = t;
		}
		var params [Float] = [];
		for (d : degs) {
			let midi = degreeToKey(d, major) + root toFloat;
			params push!(440.0 * exp((midi - 69.0) / 12.0 * log(2.0)));
		}
		params push!(1.0);
		let noteID = id;
		ae.begin(); ae.noteOn(nodeID, noteID, params); ae.sched(0);
		go(coro fn() Float {
			yield kOstHold;
			ae.begin(); ae.noteOff(nodeID, noteID); ae.sched(0);
		}());
		id = (id + 1) % kOstVoices;
		elapsed = elapsed + kOstInterval;
		yield kOstInterval;
	}
}

---------------------------------------------------------------------------
-- 7. bowed garage door springs (examples-9.txt:196-218)
-- OverlapTexture(.., 8, 3, 4, 2): a 4-mode Klank (50..2000 Hz, ring 0.2..12)
-- on PinkNoise * (LFNoise1(rand 3) * 0.0008 + 0.0022), full-wave rectified
-- (.abs) with a random sign, panned by LFNoise1(rand 1); six AllpassN stages.

const kSpringTrans = 3.0;
const kSpringHold = 11.0;
const kSpringInterval = 2.75;    -- 11 / 4
const kSpringVoices = 6;         -- ceil(life 14 / 2.75)

fn springExciter() S = pink() * (((urand(1, Rate.reset) * 3) lfnoise1(1)) * 0.0008 + 0.0022);
fn randomSign() S = (urand(1, Rate.reset) < 0.5) bi;

fn garageSpringsVoice() S {
	let freqs = 50 + urand(4, Rate.reset) * 1950;
	let rings = 0.2 + urand(4, Rate.reset) * 11.8;
	let sig = (klank(springExciter(), freqs, 1, rings) abs) * randomSign();
	(sig * xenv(kSpringTrans)) pan((urand(1, Rate.reset)) lfnoise1(1)) join
}

fn garageSprings() S = allpassChain6(voicer(kSpringVoices, garageSpringsVoice) sum(2)) gainOutlet;

garageSprings defSynthX("garageSprings", tags) await;

---------------------------------------------------------------------------
-- 8. contamination zone (examples-9.txt:222-252)
-- As #7 (ring 0.2..4) through RLPF(SinOsc(linrand 1) * 0.7 f + f, 0.1) with
-- f = exprand(800, 8000), Pan2 level pulsed by LFPulse(linrand 15, width
-- 0.2 + rand 0.2); six AllpassN stages.

fn contaminationVoice() S {
	let freqs = 50 + urand(4, Rate.reset) * 1950;
	let rings = 0.2 + urand(4, Rate.reset) * 3.8;
	let f = exprand(800, 8000, 1, Rate.reset);
	let cutoff = ((linrandv(0, 1, 1, Rate.reset)) sinosc) * (0.7 * f) + f;
	let sig = ((klank(springExciter(), freqs, 1, rings) abs) * randomSign()) rlpf(cutoff, 0.1);
	let level = (linrandv(0, 15, 1, Rate.reset)) lfupulse(0.2 + urand(1, Rate.reset) * 0.2);
	((sig * level) * xenv(kSpringTrans)) pan((urand(1, Rate.reset)) lfnoise1(1)) join
}

fn contaminationZone() S = allpassChain6(voicer(kSpringVoices, contaminationVoice) sum(2)) gainOutlet;

contaminationZone defSynthX("contaminationZone", tags) await;

---------------------------------------------------------------------------
-- 9. array manipulation of multi channel audio (examples-9.txt:256-303)
-- XFadeTexture(.., 9, 1, 2): six channels -- two FSinOsc pings, two Resonz'd
-- GrayNoise bursts, two Klanks on Dust -- scrambled, split in halves, the
-- first half echoed (CombN 0.3 max, rand 0.1..0.3, 4 s, mixed with dry), the
-- second rectified, concatenated, scrambled again, clumped into stereo pairs
-- and mixed. A per-instance random `rotate` stands in for each `scramble`
-- (a rotation rather than a full permutation).

const kArrTrans = 1.0;
const kArrHold = 10.0;
const kArrInterval = 10.0;
const kArrVoices = 2;

fn arrayVoice() S {
	let a = ((urand(2, Rate.reset) * 2000) fsinosc) * ((lfimp(urand(2, Rate.reset) * 8) decay2(0.01, 0.3)) * 0.1);
	let b = resonz(gray(2) * ((((urand(2, Rate.reset) * 4) lfnoise3(2)) * 2) max(0)), urand(2, Rate.reset) * 2000, 0.05);
	let cf = 80 + urand(16, Rate.reset) * 2920;
	let cr = 0.2 + urand(16, Rate.reset) * 1.8;
	let c = klankModes(dust(2, 2) * 0.1, cf, 1, cr) sum(2);
	let all = ([a, b, c] join) rotate(irand(0, 5, 1, Rate.reset));
	let d = all take(3);
	let e = (all drop(3)) abs;
	let dEcho = (d comb(0.1 + urand(3, Rate.reset) * 0.2, 0.3, 4, Interpolation.none)) + d;
	let f = ([dEcho, e] join) rotate(irand(0, 5, 1, Rate.reset));
	(f sum(2)) * xenv(kArrTrans)
}

fn arrayManipulation() S = voicer(kArrVoices, arrayVoice) sum(2) gainOutlet;

arrayManipulation defSynthX("arrayManipulation", tags) await;

---------------------------------------------------------------------------
-- driver

fn playAllSc9() {
	go(coro fn() Float {
		"start playing SC2 examples-9" println;
		ae.setSharedInput(0, 0.5);
		ae.setSharedInput(1, 0.5);

		"deepTrip" texture(kTripVoices, kTripInterval, kTripHold, 16.0, kTripTrans) yieldAll;

		"-- percussionSolo --" println;
		let percNode = "percussionSolo" play;
		percussionSpawner(percNode) yieldAll;
		yield 1.0;
		percNode stop;

		"sawedCymbals" texture(kCymVoices, kCymInterval, kCymHold, 12.0, kCymTrans) yieldAll;
		"siderealTime" texture(kCymVoices, kCymInterval, kCymHold, 12.0, kCymTrans) yieldAll;

		"-- pentatonicPipes --" println;
		let pipesNode = "pentatonicPipes" play;
		pentatonicSpawner(pipesNode, 24.0) yieldAll;
		yield kPipesHold + kPipesFall + 0.5;
		pipesNode stop;

		"-- ostinoodles --" println;
		let ostNode = "ostinoodles" play;
		ostinoodlesSpawner(ostNode, 12.0) yieldAll;
		yield kOstHold + kOstTrans;
		ostNode stop;

		"garageSprings" texture(kSpringVoices, kSpringInterval, kSpringHold, 12.0, kSpringTrans) yieldAll;
		"contaminationZone" texture(kSpringVoices, kSpringInterval, kSpringHold, 12.0, kSpringTrans) yieldAll;
		"arrayManipulation" texture(kArrVoices, kArrInterval, kArrHold, 20.0, kArrTrans) yieldAll;

		ae.endRender(0.1);
		"done playing SC2 examples-9" println;
	}());
}

fn renderSc9() {
	let h = "/tmp/sc2_examples_9.wav" ae.renderNRT(420, playAllSc9);
	await ae.renderDone(h);
	"render done" println;
}

--renderSc9();
--playAllSc9();
