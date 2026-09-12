-- Tzopilotl translations of SuperCollider examples from
-- sc-examples/SC2-examples/examples-12.txt. Shared machinery lives in
-- sc2_common.x; sc2_examples_1.x explains the texture timing model.
--
-- Status (examples-12.txt blocks, in order):
--   1  arachnid espresso          -- DONE (arachnidEspresso) -- pattern
--                                    .scramble approximated by a random
--                                    rotation; sequence length 1..4 via seq's
--                                    signal-rate length
--   2  inharmonic warbulence      -- DONE (inharmonicWarbulence)
--   3  clipped inharmonic warb.   -- DONE (clippedWarbulence)
--   4  pulse harmonic warbulence  -- DONE (pulseWarbulence)
--   5  drone + rhythm             -- DONE (drone / droneChords / droneRhythm:
--                                    three textures = three nodes, each with
--                                    its own copy of the (linear) CombN +
--                                    reversed-dry mix; the "i > n" and coin
--                                    conditions are script-side per spawn)
--   6  early space music, side 1  -- DONE (spaceMusic1) -- per-instance graph
--                                    choice via if_ (two subgraphs)
--   7  early space music, side 2  -- DONE (spaceMusic2) -- seven subgraphs
--                                    via switch; branch 5's synth.sched loop
--                                    (a new random resonance every random
--                                    duration) is approximated by a fixed
--                                    per-instance clock
--   8  hocketuplets               -- DONE (hocketuplets) -- .scramble of the
--                                    4-note cell approximated by rotation
--   9  the ugly part of town      -- DONE (uglyTown) -- the Pbind/Prout is a
--                                    script coroutine driving a 3-note voicer

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import filters.*;
import audio_engine as ae;
import clock.*;
import sc2_common.*;

let tags = ["sc2-examples-12"];

fn xenv(trans Float) S = linen(gate(), trans, trans) rpanfun;

---------------------------------------------------------------------------
-- Local ugens

-- LPZ2: two-zero lowpass, (x + 2 x[n-1] + x[n-2]) / 4.
fn lpz2(x S) S = (x + 2 * (x z1) + (x z2)) * 0.25;

-- A comb whose decay time may be NEGATIVE (SC's CombA/CombN with a negative
-- decay: negative feedback, odd harmonics -- the "pipe"). Cubic interpolation.
fn combSigned(x S, delayTime AsSignal, maxDelayTime AsSignal, decayTime AsSignal) S {
	let m = decay60dB((decayTime abs) / delayTime);
	let a = select2(decayTime < 0, 0 - m, m);   -- (synthc has no `sign` unary op)
	let y = delayVar(maxDelayTime * fs());
	y <- x + a * y(delayTime * fs(), Interpolation.cubic)
}

-- degreeToKey for a 7-note scale: degree d -> 12 * floor(d / 7) + scale[d mod 7].
fn degreeToKey(d S, scale [S]) S {
	let oct = (d / 7) floor;
	let idx = (d - oct * 7) i32;
	oct * 12 + select(idx, scale)
}

-- Mix.arFill(n, { CombN.ar(sig, 0.3, [rrand(0.1,0.3), rrand(0.1,0.3)], decay) }):
-- n stereo combs on a stereo signal with 2n random delays (cyclic broadcast
-- pairs channel i of the delays with signal channel i % 2), summed back to
-- stereo.
fn combCloud(sig S, n Int, decayTime Float) S {
	let dts = 0.1 + urand(2 * n, Rate.init) * 0.2;
	(sig comb(dts, 0.3, decayTime, Interpolation.none)) sum(2)
}

-- k AllpassN/AllpassL stages with random stereo delays (maxDelay, decay).
fn allpassChain(sig S, k Int, maxDelay Float, decayTime Float) S {
	var y = sig;
	for (i : (1..k)) {
		y = y alpasn(urand(2, Rate.init) * maxDelay, decayTime);
	}
	y
}

---------------------------------------------------------------------------
-- Script helper: a texture spawner that tells each instance whether it is
-- "active" (SC's `arg spawn, i; if (i > n and 0.8.coin ...)` guards), as a
-- noteParam.

coro fn gatedSpawner(nodeID Int, numVoices Int, interval Float, holdTime Float, totalDur Float,
                     minIndex Int, prob Float) Float {
	var id = 0;
	var i = 0;
	var elapsed = 0.0;
	while (elapsed < totalDur) {
		let u Float = urand();
		let active = (i > minIndex && u < prob) ? 1.0 : 0.0;
		let noteID = id;
		ae.begin(); ae.noteOn(nodeID, noteID, [active, 1.0]); ae.sched(0);
		go(coro fn() Float {
			yield holdTime;
			ae.begin(); ae.noteOff(nodeID, noteID); ae.sched(0);
		}());
		id = (id + 1) % numVoices;
		i = i + 1;
		elapsed = elapsed + interval;
		yield interval;
	}
}

fn pActive() S = noteParam("active", ControlSpec{0.0, 1.0, 1.0, ControlWarp.linear});

---------------------------------------------------------------------------
-- 1. arachnid espresso (examples-12.txt:4-26)
-- OverlapTexture(.., 19, 0.5, 5, 2). Each instance is a plucked string
-- (2/3) or a pipe (1/3: negative comb decay, pitch doubled); a clock at
-- 8, 4 or 2 Hz drives an ImpulseSequencer over one of three 8-step patterns;
-- each impulse opens a Trig gate for 0.3..0.9 beats that lets LPZ2'd
-- LFNoise2 (rate 24 f, capped 12 kHz) excite a CombA tuned to the current
-- note of a 1..4-note random sequence (lagged by 0.1 beat). Six AllpassL
-- stages (0.03, 3 s) on the whole texture.

const kArachTrans = 0.5;
const kArachHold = 19.5;
const kArachInterval = 3.9;      -- 19.5 / 5
const kArachVoices = 6;          -- ceil(life 20 / 3.9)

fn arachnidVoice() S {
	let pipe = urand(1, Rate.reset) < 0.3333;
	let p = select2(pipe, -1.0, 1.0);
	let pf = select2(pipe, 2.0, 1.0);
	let r = select(irand(0, 2, 1, Rate.reset), [8, 4, 2] @ asSignal);
	let c = lfimp(r);
	let patterns = [
		[1, 1, 0, 0, 0, 0, 0, 0] vec,
		[1, 1, 1, 0, 0, 0, 0, 0] vec,
		[1, 1, 1, 1, 0, 0, 0, 0] vec
	];
	let pat = select(irand(0, 2, 1, Rate.reset), patterns) rotate(irand(0, 7, 1, Rate.reset));
	let t = iseq(c, pat, 8);
	let e = t timedGate((0.3 + urand(1, Rate.reset) * 0.6) / r);
	let notes = (24 + urand(4, Rate.reset) * 72) nnhz;
	let len = select(irand(0, 3, 1, Rate.reset), [1, 2, 3, 4] @ asSignal);
	let f = pf * (seq(t, notes, len) lag(0.1 / r));
	let n = lpz2(((f * 24) min(12000) lfnoise3(1)) * 0.2 * e);
	let decayTime = ((exprand(90, 240, 1, Rate.reset) / f) min(0.5)) * p;
	((combSigned(n, 1 / f, 0.1, decayTime) * 0.2) * xenv(kArachTrans)) pan(birand(1, Rate.reset)) join
}

fn arachnidEspresso() S {
	let sig = voicer(kArachVoices, arachnidVoice) sum(2);
	allpassChain(sig, 6, 0.03, 3.0) gainOutlet
}

arachnidEspresso defSynthX("arachnidEspresso", tags) await;

---------------------------------------------------------------------------
-- 2. inharmonic warbulence (examples-12.txt:31-51)
-- OverlapTexture(.., 12.8, 6.4, 6, 2): twelve FSinOscs at f * (1 + rand 12),
-- each amplitude-warbled by a sine at a common rate r (XLine between two
-- exprand(0.1, 20) over 25.6 s) * rrand(0.9, 1.1), clipped at 0, level 2/g,
-- panned; the instance is scaled by min(1, 500/f). Five stereo CombN(0.3,
-- 0.1..0.3, 8) on the output, * 0.3.

const kWarbTrans = 6.4;
const kWarbHold = 19.2;
const kWarbInterval = 3.2;       -- 19.2 / 6
const kWarbVoices = 8;           -- ceil(life 25.6 / 3.2)

fn warbulenceCore(f S, r S, gains S, ampOffset Float) S {
	let n = 12;
	let g = 1 + urand(n, Rate.reset) * 12;
	let lfo = ((r * (0.9 + urand(n, Rate.reset) * 0.2)) sinosc(urand(n, Rate.reset))) * 0.08 + ampOffset;
	let sig = ((f * g) fsinosc) * (lfo max(0)) * (2 / g) * gains;
	(sig pan(birand(n, Rate.reset)) join) transpose(n) sum(2)
}

fn inharmonicWarbulenceVoice() S {
	let f = (24 + urand(1, Rate.reset) * 72) nnhz;
	let a = (500 / f) min(1);
	let r = xline(exprand(0.1, 20, 1, Rate.reset), exprand(0.1, 20, 1, Rate.reset), 25.6);
	(a * warbulenceCore(f, r, 1 asSignal, -0.04)) * xenv(kWarbTrans)
}

fn inharmonicWarbulence() S {
	let sig = voicer(kWarbVoices, inharmonicWarbulenceVoice) sum(2);
	(combCloud(sig, 5, 8.0) * 0.3) gainOutlet
}

inharmonicWarbulence defSynthX("inharmonicWarbulence", tags) await;

---------------------------------------------------------------------------
-- 3. clipped inharmonic warbulence (examples-12.txt:56-76)
-- As #2 but the warble rate is one global LFNoise1(1/16) mapped 0.1..20 Hz,
-- each partial is clipped at 0 (half-wave), LeakDC, eight combs of 20 s.

fn clippedWarbulenceVoice() S {
	let f = (24 + urand(1, Rate.reset) * 72) nnhz;
	let a = (500 / f) min(1);
	let r = ((0.0625 asSignal) lfnoise1(1)) linexp(-1, 1, 0.1, 20);
	let n = 12;
	let g = 1 + urand(n, Rate.reset) * 12;
	let lfo = ((r * (0.9 + urand(n, Rate.reset) * 0.2)) sinosc(urand(n, Rate.reset))) * 0.08 - 0.04;
	let sig = ((((f * g) fsinosc) * (lfo max(0))) max(0)) * (2 / g);
	((a * ((sig pan(birand(n, Rate.reset)) join) transpose(n) sum(2))) * xenv(kWarbTrans))
}

fn clippedWarbulence() S {
	let sig = (voicer(kWarbVoices, clippedWarbulenceVoice) sum(2)) leakdc(0.995);
	(combCloud(sig, 8, 20.0) * 0.3) gainOutlet
}

clippedWarbulence defSynthX("clippedWarbulence", tags) await;

---------------------------------------------------------------------------
-- 4. pulse harmonic warbulence (examples-12.txt:83-100)
-- Harmonics f (i+1), amplitude 1/(i+1), warble 0.1/-0.05, the whole instance
-- chopped by LFPulse(exprand(0.2, 1.2), width 0.1..0.2); five combs, * 0.5.

fn pulseWarbulenceVoice() S {
	let f = (24 + urand(1, Rate.reset) * 72) nnhz;
	let r = xline(exprand(0.1, 20, 1, Rate.reset), exprand(0.1, 20, 1, Rate.reset), 25.6);
	let n = 12;
	var idx [Int] = [];
	for (i : (1..n)) { idx push!(i); }
	let harm = idx vec;
	let lfo = ((r * (0.9 + urand(n, Rate.reset) * 0.2)) sinosc(urand(n, Rate.reset))) * 0.1 - 0.05;
	let sig = ((f * harm) fsinosc) * (lfo max(0)) * (1 / harm);
	let mix = (sig pan(birand(n, Rate.reset)) join) transpose(n) sum(2);
	let chop = exprand(0.2, 1.2, 1, Rate.reset) lfupulse(0.1 + urand(1, Rate.reset) * 0.1);
	(mix * chop) * xenv(kWarbTrans)
}

fn pulseWarbulence() S {
	let sig = voicer(kWarbVoices, pulseWarbulenceVoice) sum(2);
	(combCloud(sig, 5, 8.0) * 0.5) gainOutlet
}

pulseWarbulence defSynthX("pulseWarbulence", tags) await;

---------------------------------------------------------------------------
-- 5. drone + rhythm (examples-12.txt:105-130)
-- Three OverlapTextures summed, then CombN(0.5, 0.5, 6) + reversed dry.
-- CombN is linear, so each texture (its own node here) carries its own copy
-- of that output stage and the sum is the same.
--  a) drone (4, 4, 8): LPF(LFSaw([f, f+0.2], LFNoise2(f * [.05, .04]) * 0.06),
--     rrand(1000, 3000)), f = midicps(24 or 36, +/- 0.08).
--  b) chords (4, 6, 3): from the 3rd spawn on, 80% of instances play a sine
--     at 60/72 + a dorian degree, +/- 0.05, amp 0.04..0.07.
--  c) rhythm (6, 6, 6): from the 10th spawn on, an ImpulseSequencer over a
--     scrambled [1,1,1,0,0,0] at 1.5/3/6 Hz fires Decay2 grains on an LFPulse
--     at 48/60/72/84 + degree, into RLPF(exprand 800..2000, 0.1).

-- A random dorian degree, chosen once per note-on. The scale array must be
-- built INSIDE the graph function: a SignalExpr created at module top level
-- has no graph to attach to (and currently segfaults rather than diagnose).
fn dorianDegree() S = select(irand(0, 6, 1, Rate.reset), [0, 2, 3, 5, 7, 9, 10] @ asSignal);

const kDroneTrans = 4.0;
const kDroneHold = 8.0;
const kDroneInterval = 1.0;      -- 8 / 8
const kDroneVoices = 12;         -- ceil(life 12 / 1)

fn droneVoice() S {
	let base = select(irand(0, 1, 1, Rate.reset), [24, 36] @ asSignal);
	let f = (base + birand(1, Rate.reset) * 0.08) nnhz;
	let freqs = f + ([0.0, 0.2] vec);
	let amp = ((f * ([0.05, 0.04] vec)) lfnoise3(2)) * 0.06;
	(((freqs lfsaw) * amp) lpf(1000 + urand(1, Rate.reset) * 2000)) * xenv(kDroneTrans)
}

fn drone() S {
	let a = voicer(kDroneVoices, droneVoice) sum(2);
	((a combn(0.5, 6)) + (a reverse)) gainOutlet
}

drone defSynthX("drone", tags) await;

const kChordTrans = 6.0;
const kChordHold = 10.0;
const kChordInterval = 3.3333333;  -- 10 / 3
const kChordVoices = 5;            -- ceil(life 16 / 3.33)

fn droneChordVoice() S {
	let active = pActive();
	let base = select(irand(0, 1, 1, Rate.reset), [60, 72] @ asSignal);
	let degree = dorianDegree();
	let freqs = (base + degree + birand(2, Rate.reset) * 0.05) nnhz;
	let amp = 0.04 + urand(1, Rate.reset) * 0.03;
	let a = ((freqs sinosc) * amp * active) * xenv(kChordTrans);
	(a combn(0.5, 6)) + (a reverse)
}

fn droneChords() S = voicer(kChordVoices, droneChordVoice) sum(2) gainOutlet;

droneChords defSynthX("droneChords", tags) await;

const kRhyTrans = 6.0;
const kRhyHold = 12.0;
const kRhyInterval = 2.0;        -- 12 / 6
const kRhyVoices = 9;            -- ceil(life 18 / 2)

fn droneRhythmVoice() S {
	let active = pActive();
	let clockRate = select(irand(0, 2, 1, Rate.reset), [1.5, 3, 6] @ asSignal);
	let pat = ([1, 1, 1, 0, 0, 0] vec) rotate(irand(0, 5, 1, Rate.reset));
	let trig = iseq(lfimp(clockRate), pat, 6);
	let base = select(irand(0, 3, 1, Rate.reset), [48, 60, 72, 84] @ asSignal);
	let degree = dorianDegree();
	let freqs = (base + degree + birand(2, Rate.reset) * 0.03) nnhz;
	let pulse = (freqs lfupulse(0.4)) * (0.03 + urand(1, Rate.reset) * 0.05);
	let grains = (trig decay2(0.004, 0.2 + urand(1, Rate.reset) * 0.5)) * pulse;
	let a = ((grains rlpf(exprand(800, 2000, 1, Rate.reset), 0.1)) * active) * xenv(kRhyTrans);
	(a combn(0.5, 6)) + (a reverse)
}

fn droneRhythm() S = voicer(kRhyVoices, droneRhythmVoice) sum(2) gainOutlet;

droneRhythm defSynthX("droneRhythm", tags) await;

---------------------------------------------------------------------------
-- 6. early space music LP, side 1 (examples-12.txt:135-171)
-- OverlapTexture(.., 4, 4, 6, 2): each instance is one of two graphs.
--  A) ten detuned stereo LFSaw pairs at a scale note (major, octaves 21..93)
--     with 4..6 Hz vibrato, through LPZ2 twice, and (30%, below 1400 Hz) an
--     RLPF swept by a slow sine.
--  B) a sine whose pitch is an LFTri (0.25..0.5 Hz, depth linrand 4..30
--     semitones) offset by a fast LFSaw pair whose rates XLine over 12 s,
--     into CombN(0.3, 0.15..0.3, 4).
-- Output: CombN(0.5, [0.5, 0.47], 7) + reversed dry.

const kSpace1Trans = 4.0;
const kSpace1Hold = 8.0;
const kSpace1Interval = 1.3333333;  -- 8 / 6
const kSpace1Voices = 9;            -- ceil(life 12 / 1.333)

-- Scale tables as functions, not top-level lets: a SignalExpr created at
-- module top level has no graph to attach to (and currently segfaults).
fn kMajor() [S] = [0, 2, 4, 5, 7, 9, 11] @ asSignal;
fn kOctaves() [S] = [21, 33, 45, 57, 69, 81, 93] @ asSignal;

fn spaceSawGraph() S {
	let f = (select(irand(0, 6, 1, Rate.reset), kMajor()) + select(irand(0, 6, 1, Rate.reset), kOctaves())) nnhz;
	let vib = (exprand(4, 6, 10, Rate.reset) sinosc) * 0.008 + 1;
	let ff = f * vib;                                        -- 10 channels
	let detune = 0.99 + urand(20, Rate.reset) * 0.02;        -- 10 stereo pairs
	let saws = ((ff * detune) lfsaw) * 0.01;                 -- cyclic: pairs share ff
	let x = lpz2(lpz2(saws sum(2)));
	let useFilter = (urand(1, Rate.reset) < 0.3) * (f < 1400);
	let cutoff = ((0.3 + urand(1, Rate.reset) * 0.5) sinosc) * (f * (0.5 + urand(1, Rate.reset) * 2.5))
		+ f * (4 + urand(1, Rate.reset) * 8);
	select2(useFilter, x rlpf(cutoff, 0.1), x)
}

fn spaceTriGraph() S {
	let sgn1 = select2(urand(1, Rate.reset) < 0.5, -1.0, 1.0);
	let rr = ([1.0, 1.0] vec) + ([0.0, 1.0] vec) * (birand(1, Rate.reset) * 0.1);
	let rates = xline(exprand(4, 12, 1, Rate.reset) * rr, exprand(4, 12, 1, Rate.reset) * rr, 12) * sgn1;
	let sgn2 = select2(urand(1, Rate.reset) < 0.5, -1.0, 1.0);
	let saw = (rates lfsaw) * (2 + urand(1, Rate.reset) * 14) + (40 + urand(1, Rate.reset) * 80);
	let f = ((exprand(0.25, 0.5, 1, Rate.reset) * sgn2) lftri) * linrandv(4, 30, 1, Rate.reset) + saw;
	-- CombN(.., dt, dt, 4) with a per-instance random dt: the ring buffer is
	-- sized once at init, before any note-on has rolled the Rate.reset value,
	-- so give combl an explicit constant max delay (0.3 s) instead.
	((f nnhz) sinosc * 0.02) combl(0.15 + urand(1, Rate.reset) * 0.15, 0.3, 4)
}

fn spaceMusic1Voice() S {
	let a = if_(urand(1, Rate.reset) < 0.5, spaceSawGraph, spaceTriGraph);
	a * xenv(kSpace1Trans)
}

fn spaceMusic1() S {
	let a = voicer(kSpace1Voices, spaceMusic1Voice) sum(2);
	((a comb([0.5, 0.47] vec, 0.5, 7, Interpolation.none)) + (a reverse)) gainOutlet
}

spaceMusic1 defSynthX("spaceMusic1", tags) await;

---------------------------------------------------------------------------
-- 7. early space music LP, side 2 (examples-12.txt:176-253)
-- OverlapTexture(.., 2, 4, 6, 2): each instance is one of seven graphs
-- (chosen with switch on a per-instance irand):
--  0 alien-meadow nested sines  1 fast-LFO beats  2 noise-modulated sines
--  3 pulse harmonic warbulence  4 resonant pink noise whose centre steps
--    every 1/16, 1/8, 1/2 or 2 s (SC re-schedules a random duration each
--    step; here the step clock is fixed per instance)  5 LFNoise-pitched
--    sine with nested noise amplitude, LFNoise1 pan  6 two 15-mode Klanks
--    (exprand 100..6000, ring 2..6) on a noise-gated LFPulse whose rate
--    XLines, distorted.
-- Five stereo CombN(0.3, 0.1..0.3, 8) on the output, * 0.3.

const kSpace2Trans = 4.0;
const kSpace2Hold = 6.0;
const kSpace2Interval = 1.0;     -- 6 / 6
const kSpace2Voices = 10;        -- ceil(life 10 / 1)

fn s2Meadow() S {
	let a = urand(1, Rate.reset) * 20;
	let b = urand(1, Rate.reset) * 5000;
	let c = urand(1, Rate.reset) * 20;
	let freq = (a sinosc) * (0.1 * b) + b;
	((freq sinosc) * ((c sinosc) * 0.08 + 0.08)) pan(birand(1, Rate.reset)) join
}

fn s2FastLfo() S {
	let a0 = 40 + urand(1, Rate.reset) * 200;
	let a = a0 + ([0.0, 1.0] vec) * birand(1, Rate.reset);
	let b = exprand(50, 2400, 1, Rate.reset);
	let c = a + birand(2, Rate.reset);
	let freq = (a sinosc) * (urand(1, Rate.reset) * b) + b;
	(freq sinosc) * ((c sinosc) * 0.025 + 0.025)
}

fn s2NoiseSines() S {
	let f = (60 + urand(1, Rate.reset) * 40) nnhz;
	let freqs = f + ([0.0, 0.2] vec);
	(freqs fsinosc) * (((f * ([0.15, 0.16] vec)) lfnoise3(2)) * 0.1)
}

fn s2PulseWarb() S {
	let f = (24 + urand(1, Rate.reset) * 72) nnhz;
	let r = xline(exprand(0.1, 20, 1, Rate.reset), exprand(0.1, 20, 1, Rate.reset), 25.6);
	let n = 12;
	var idx [Int] = [];
	for (i : (1..n)) { idx push!(i); }
	let harm = idx vec;
	let lfo = ((r * (0.9 + urand(n, Rate.reset) * 0.2)) sinosc(urand(n, Rate.reset))) * 0.1 - 0.05;
	let sig = ((f * harm) fsinosc) * (lfo max(0)) * (1 / harm);
	let mix = (sig pan(birand(n, Rate.reset)) join) transpose(n) sum(2);
	mix * (exprand(0.2, 1.2, 1, Rate.reset) lfupulse(0.1 + urand(1, Rate.reset) * 0.1))
}

fn s2ResoPink() S {
	let dur = select(irand(0, 3, 1, Rate.reset), [0.0625, 0.125, 0.5, 2.0] @ asSignal);
	let step = lfimp(1 / dur);
	let freqCtl = ((200 + urand(1) * 600) sampleAndHold(step)) lag(dur * 0.5);
	(resonz(pink() * 10, freqCtl, 0.002)) pan(birand(1, Rate.reset)) join
}

fn s2NoisePitch() S {
	let f = (((urand(1, Rate.reset) * 0.3) lfnoise1(1)) * 60 + 70) nnhz;
	let inner = ((urand(1, Rate.reset) * 40) sinosc) * 0.1;
	let ampCtl = (((urand(1, Rate.reset) * 8) lfnoise1(1)) * inner) max(0);
	let amp = ((f * urand(1, Rate.reset) * 0.5) lfnoise3(1)) * ampCtl;
	((f sinosc) * amp) pan((urand(1, Rate.reset) * 5) lfnoise1(1)) join
}

fn s2Klank() S {
	let p = 15;
	let freqs = exprand(100, 6000, 2 * p, Rate.reset);
	let rings = 2 + urand(2 * p, Rate.reset) * 4;
	let f3 = xline(exprand(40, 300, 1, Rate.reset), exprand(40, 300, 1, Rate.reset), 10);
	let gateN = (((urand(1, Rate.reset) * 8) lfnoise3(1)) max(0)) * 0.002;
	let in = (f3 lfupulse(0.1 + urand(1, Rate.reset) * 0.8)) * gateN;
	((klankModes(in, freqs, 1, rings) sum(2)) distort) * 0.3
}

fn spaceMusic2Voice() S {
	let a = switch(irand(0, 6, 1, Rate.reset),
		[s2Meadow, s2FastLfo, s2NoiseSines, s2PulseWarb, s2ResoPink, s2NoisePitch, s2Klank]);
	a * xenv(kSpace2Trans)
}

fn spaceMusic2() S {
	let sig = voicer(kSpace2Voices, spaceMusic2Voice) sum(2);
	(combCloud(sig, 5, 8.0) * 0.3) gainOutlet
}

spaceMusic2 defSynthX("spaceMusic2", tags) await;

---------------------------------------------------------------------------
-- 8. hocketuplets (examples-12.txt:259-288)
-- OverlapTexture(.., 8, 4, 6, 2): a root (61 +/- 4, fixed per synth) and per
-- instance a 4-degree cell [0, 3, 4, 6] transposed by -16..16 degrees,
-- scrambled (rotated here), mapped through the major scale; a clock at
-- 1/2/3 Hz (or 1/2/3/4/6 for higher offsets) steps the sequence and fires
-- Decay2(0.004, 2/r) * 0.1 on a Pulse (width 0.1..0.9) into LPF(1..10 kHz),
-- panned. Six AllpassN(0.04, 4 s) stages on the texture.

const kHockTrans = 4.0;
const kHockHold = 12.0;
const kHockInterval = 2.0;       -- 12 / 6
const kHockVoices = 8;           -- ceil(life 16 / 2)

fn hocketVoice() S {
	let root = 61 + (irand(-4, 4, 1, Rate.init) f32);
	let offset = irand(-16, 16, 1, Rate.reset) f32;
	let rLow = select(irand(0, 2, 1, Rate.reset), [1, 2, 3] @ asSignal);
	let rHigh = select(irand(0, 4, 1, Rate.reset), [1, 2, 3, 4, 6] @ asSignal);
	let r = select2(offset < 7, rLow, rHigh);
	let cell = (([0, 3, 4, 6] vec) + offset) rotate(irand(0, 3, 1, Rate.reset));
	let sequence = (degreeToKey(cell, kMajor()) + root) nnhz;
	let trig = lfimp(r) * 3;
	let f = seq(trig, sequence, 4);
	let env = (trig decay2(0.004, 2 / r)) * 0.1;
	let sig = ((f lfbpulse(0.1 + urand(1, Rate.reset) * 0.8)) * env) lpf(1000 + urand(1, Rate.reset) * 9000);
	(sig * xenv(kHockTrans)) pan(birand(1, Rate.reset)) join
}

fn hocketuplets() S {
	let z = voicer(kHockVoices, hocketVoice) sum(2);
	allpassChain(z, 6, 0.04, 4.0) gainOutlet
}

hocketuplets defSynthX("hocketuplets", tags) await;

---------------------------------------------------------------------------
-- 9. the ugly part of town (examples-12.txt:295-334)
-- A Pbind over a Prout: phrases of 12..24 three-note chords [n, n+ivalA,
-- n+ivalB] (ivals 3..10 and 6..20 semitones) stepping a random 2..5-step
-- interval sequence, notes folded into +/- 18 around MIDI 60-ish, durations
-- 0.4 / rrand(2, 5) with a 0.4 s phrase-final chord; each phrase picks a
-- pulse width, a filter harmonic (3..24 * freq, capped 18 kHz) and rq.
-- ugenFunc: Pan2(RLPF(Pulse(freq, pw), freq * harmonics, rq) * EnvGen(env,
-- amp)). The Prout is a script coroutine; the ugenFunc is a voicer voice
-- with noteParams (freq, pw, harmonics, rq); the (unspecified) Pbind
-- envelope is a gated asr with a short release. Six AllpassN(0.05, 3 s)
-- stages, mixed 50% with the dry chords.

const kUglyVoices = 6;

fn uglyVoice() S {
	let g = gate();
	let freq = noteParam("freq", ControlSpec{20.0, 20000.0, 261.0, ControlWarp.exponential});
	let pw = noteParam("pw", ControlSpec{0.1, 0.9, 0.5, ControlWarp.linear});
	let harmonics = noteParam("harmonics", ControlSpec{3.0, 24.0, 8.0, ControlWarp.linear});
	let rq = noteParam("rq", ControlSpec{0.15, 0.3, 0.2, ControlWarp.linear});
	let ffreq = (freq * harmonics) min(18000);
	let env = asr(g, 0.005, 1.0, 0.05) * 0.1;
	((freq lfbpulse(pw)) rlpf(ffreq, rq)) * env
}

fn uglyTown() S {
	let in = voicer(kUglyVoices, uglyVoice) sum;
	let signal = allpassChain(in, 6, 0.05, 3.0);
	(in + 0.5 * (signal - in)) gainOutlet
}

uglyTown defSynthX("uglyTown", tags) await;

fn uglyFold(x Float) Float {
	var y = x;
	while (y > 18.0 || y < -18.0) {
		if (y > 18.0) { y = 36.0 - y; }
		if (y < -18.0) { y = -36.0 - y; }
	}
	y
}

coro fn uglySpawner(nodeID Int, totalDur Float) Float {
	let u0 Float = urand();
	var note = (u0 * 12.0 - 6.0) round;
	var id = 0;
	var elapsed = 0.0;
	while (elapsed < totalDur) {
		let len Int = irand(2, 5);
		let ivalA Int = irand(3, 10);
		let ivalB Int = ivalA + irand(3, 10);
		let dur = 0.4 / (irand(2, 5) toFloat);
		var seqSteps [Float] = [];
		for (k : (1..len)) { let u Float = urand(); seqSteps push!((u * 12.0 - 6.0) round); }
		let pw Float = 0.1 + urand() * 0.8;
		let harmonics Float = 3.0 + urand() * 21.0;
		let rq Float = 0.15 + urand() * 0.15;
		let count Int = irand(12, 24);
		var i = 0;
		while (i <= count && elapsed < totalDur) {
			let fnote = uglyFold(note);
			let noteDur = i == count ? 0.4 : dur;
			for (iv : [0.0, ivalA toFloat, ivalB toFloat]) {
				let noteID = id;
				let freq = (fnote + iv + 60.0) nnhz;
				ae.begin(); ae.noteOn(nodeID, noteID, [freq, pw, harmonics, rq, 1.0]); ae.sched(0);
				go(coro fn() Float {
					yield noteDur;
					ae.begin(); ae.noteOff(nodeID, noteID); ae.sched(0);
				}());
				id = (id + 1) % kUglyVoices;
			}
			if (i < count) { note = note + seqSteps[i % len]; }
			elapsed = elapsed + noteDur;
			yield noteDur;
			i = i + 1;
		}
	}
}

---------------------------------------------------------------------------
-- driver

fn playAllSc12() {
	go(coro fn() Float {
		"start playing SC2 examples-12" println;
		ae.setSharedInput(0, 0.5);
		ae.setSharedInput(1, 0.5);

		"arachnidEspresso" texture(kArachVoices, kArachInterval, kArachHold, 16.0, kArachTrans) yieldAll;
		"inharmonicWarbulence" texture(kWarbVoices, kWarbInterval, kWarbHold, 16.0, kWarbTrans) yieldAll;
		"clippedWarbulence" texture(kWarbVoices, kWarbInterval, kWarbHold, 16.0, kWarbTrans) yieldAll;
		"pulseWarbulence" texture(kWarbVoices, kWarbInterval, kWarbHold, 16.0, kWarbTrans) yieldAll;

		"-- drone + rhythm --" println;
		let droneNode = "drone" play;
		let chordNode = "droneChords" play;
		let rhyNode = "droneRhythm" play;
		go(coro fn() Float { overlapTextureSpawner(droneNode, kDroneVoices, kDroneInterval, kDroneHold, 30.0) yieldAll; }());
		go(coro fn() Float { gatedSpawner(chordNode, kChordVoices, kChordInterval, kChordHold, 30.0, 1, 0.8) yieldAll; }());
		gatedSpawner(rhyNode, kRhyVoices, kRhyInterval, kRhyHold, 30.0, 8, 1.0) yieldAll;
		yield kRhyHold + kRhyTrans;
		droneNode stop; chordNode stop; rhyNode stop;

		"spaceMusic1" texture(kSpace1Voices, kSpace1Interval, kSpace1Hold, 16.0, kSpace1Trans) yieldAll;
		"spaceMusic2" texture(kSpace2Voices, kSpace2Interval, kSpace2Hold, 16.0, kSpace2Trans) yieldAll;
		"hocketuplets" texture(kHockVoices, kHockInterval, kHockHold, 16.0, kHockTrans) yieldAll;

		"-- uglyTown --" println;
		let uglyNode = "uglyTown" play;
		uglySpawner(uglyNode, 16.0) yieldAll;
		yield 1.0;
		uglyNode stop;

		ae.endRender(0.1);
		"done playing SC2 examples-12" println;
	}());
}

fn renderSc12() {
	let h = "/tmp/sc2_examples_12.wav" ae.renderNRT(400, playAllSc12);
	await ae.renderDone(h);
	"render done" println;
}

--renderSc12();
--playAllSc12();
