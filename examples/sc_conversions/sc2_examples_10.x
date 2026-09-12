-- Tzopilotl translations of SuperCollider examples from
-- sc-examples/SC2-examples/examples-10.txt ("Mixed textures"). Shared
-- machinery lives in sc2_common.x; sc2_examples_1.x explains the texture
-- timing model.
--
-- Status (examples-10.txt blocks, in order):
--   1  mix of three other examples -- DONE (mixSweepers + mixRingKlanks +
--                                    mixBottles: three voicer synths played
--                                    together by the driver)
--   2  pentatonic pipes and bells -- DONE (pentatonic) -- the 4-channel Spawn
--                                    is a 4-channel voice; Env 'sine' curve
--                                    approximated by linen rpanfun
--   3  tarmac                      -- DONE (tarmacA + tarmacB, each with its
--                                    own 6-allpass tail; pattern .scramble
--                                    approximated by a random rotate)
--   4  dancing shadows             -- DONE (dancingShadows) -- Prand of Pseqs
--                                    approximated by one pattern per instance;
--                                    the 5 random scale notes are not sorted
--   5  choip choip choip           -- DONE (choip)

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import filters.*;
import audio_engine as ae;
import clock.*;
import sc2_common.*;

let tags = ["sc2-examples-10"];

fn xenv(trans Float) S = linen(gate(), trans, trans) rpanfun;

---------------------------------------------------------------------------
-- Local helpers

-- ClipNoise: random +/-1 each sample.
-- (synthc has no lowering for the `sign` unary op -- "Unknown unary
-- operator: sign" -- so the sign is taken with a comparison.)
fn clipNoise(chans Int) S = (white(chans) > 0) bi;

-- degreeToKey(deg, scale, 12): scale[deg mod len] + 12 * (deg div len).
-- `deg` may be an integer-typed signal (irand) -- cast first.
fn degreeToKey(deg S, scale S, len Int) S {
	let d = deg f32;
	let oct = (d / len) floor;
	(scale at(d - oct * len)) + 12 * oct
}

-- Pick one of several 16-step patterns per instance, rotated by a random
-- amount (a cheap stand-in for .scramble / for Prand switching patterns).
fn choosePattern(patterns [S]) S {
	let n = patterns length;
	let pat = select(irand(0, n - 1, 1, Rate.reset), patterns);
	pat rotate(irand(0, 15, 1, Rate.reset))
}

---------------------------------------------------------------------------
-- 1. mix of three other examples (examples-10.txt:6-51)
-- Three OverlapTextures summed: swept-resonant-noise Klanks (5 overlapping,
-- 10 modes), ring-modulated Klanks (4 overlapping), and pulsing bottles on
-- ClipNoise (4 overlapping). Each is its own voicer synth; the driver plays
-- the three nodes at once.

-- 1a: OverlapTexture(.., 4, 4, 5, 2)
const kSwTrans = 4.0;
const kSwHold = 8.0;
const kSwInterval = 1.6;        -- 8 / 5
const kSwVoices = 8;            -- ceil(life 12 / 1.6)

fn mixSweepersVoice() S {
	let p = 10;
	let lfo = ((0.1 + urand(1, Rate.reset) * 0.2) fsinosc) * (12 + birand(1, Rate.reset) * 12)
		+ (60 + birand(1, Rate.reset) * 24);
	let sweep = resonz(white() * (urand(1, Rate.reset) * 0.003), lfo nnhz, 0.1);
	let freqs = linrandv(80, 10080, 2 * p, Rate.reset);
	let rings = 0.5 + urand(2 * p, Rate.reset) * 2;
	(klankModes(sweep, freqs, 1, rings) sum(2)) * xenv(kSwTrans)
}

fn mixSweepers() S = voicer(kSwVoices, mixSweepersVoice) sum(2) gainOutlet;

mixSweepers defSynthX("mixSweepers", tags) await;

-- 1b: OverlapTexture(.., 4, 4, 4, 2)
const kRkTrans = 4.0;
const kRkHold = 8.0;
const kRkInterval = 2.0;        -- 8 / 4
const kRkVoices = 6;            -- ceil(life 12 / 2)

fn mixRingKlanksVoice() S {
	let p = 8;
	let freqs = 100 + urand(p, Rate.reset) * 9900;
	let rings = 0.2 + urand(p, Rate.reset) * 0.8;
	let a = klank(dust(10) * (urand(1, Rate.reset) * 0.02), freqs, 1, rings);
	let modF = ((1 + birand(1, Rate.reset) * 0.3) lfnoise3(1)) * 200 + 350 + urand(1, Rate.reset) * 50;
	(((modF sinosc) * a) * xenv(kRkTrans)) pan(birand(1, Rate.reset)) join
}

fn mixRingKlanks() S = voicer(kRkVoices, mixRingKlanksVoice) sum(2) gainOutlet;

mixRingKlanks defSynthX("mixRingKlanks", tags) await;

-- 1c: OverlapTexture(.., 10, 1, 4, 2): Resonz(ClipNoise * LFPulse(4 + linrand 15,
-- width rand 0.7, 5/4), 80 + linrand 900, 0.01), panned by a slow SinOsc.
const kBtTrans = 1.0;
const kBtHold = 11.0;
const kBtInterval = 2.75;       -- 11 / 4
const kBtVoices = 5;            -- ceil(life 13 / 2.75)

fn mixBottlesVoice() S {
	let gateAmp = (linrandv(4, 19, 1, Rate.reset) lfupulse(urand(1, Rate.reset) * 0.7)) * 1.25;
	let sig = resonz(clipNoise(1) * gateAmp, linrandv(80, 980, 1, Rate.reset), 0.01);
	let panPos = (0.1 + urand(1, Rate.reset) * 0.4) sinosc(urand(1, Rate.reset));
	(sig * xenv(kBtTrans)) pan(panPos) join
}

fn mixBottles() S = voicer(kBtVoices, mixBottlesVoice) sum(2) gainOutlet;

mixBottles defSynthX("mixBottles", tags) await;

---------------------------------------------------------------------------
-- 2. pentatonic pipes and bells (examples-10.txt:55-113)
-- Spawn(.., 4, dur 8): every 8 s a new root (36 + rand 12) and a 4-channel
-- instance [pipesL, pipesR, bellsL, bellsR]: five "pipes" -- Resonz(BrownNoise
-- * Env.linen(0.2, 6, 1, level 20, 'sine'), f, 0.002, mul 4).distort * 0.04
-- at pentatonic degrees of the root, panned -- and four "bells" -- Decay2
-- (Impulse(rand 2), 0.01, 2) * (FSinOsc(f) + FSinOsc(f + rand2 2)) three
-- octaves up, panned at level 0.05. Everything is cut by Env([1,1,0],[8,
-- 0.01]). Pipes then get CombN(0.3, 0.3, 8) mixed with their reversed dry
-- signal plus two random-delay combs; bells get six random allpasses (8 s).
-- The gate is held for the 7.2 s linen (0.2 + 6 + 1) so linen's fall is the
-- 1 s release; rpanfun stands in for the 'sine' curve.

const kPentHold = 7.2;
const kPentInterval = 8.0;
const kPentVoices = 2;

fn pentatonicVoice() S {
	let g = gate();
	-- (built inside the graph fn: a signal made at module scope segfaults)
	let kPentMode = [0, 3, 5, 7, 10] vec;
	let root = irand(36, 47, 1, Rate.reset) f32;
	-- pipes
	let pipeF = (degreeToKey(irand(0, 19, 5, Rate.reset), kPentMode, 5) + root) nnhz;
	let amp = (linen(g, 0.2, 1) rpanfun) * 20;
	let pipes = ((resonz(red(5) * amp, pipeF, 0.002) * 4) distort * 0.04) pan(birand(5, Rate.reset)) join;
	let pipesLR = pipes transpose(5) sum(2);
	-- bells
	let bellF = (degreeToKey(irand(0, 9, 4, Rate.reset), kPentMode, 5) + root + 36) nnhz;
	let strike = lfimp(urand(4, Rate.reset) * 2);
	let bellEnv = strike decay2(0.01, 2);
	let bell = bellEnv * ((bellF fsinosc) + ((bellF + birand(4, Rate.reset) * 2) fsinosc));
	let bellsLR = ((bell pan(birand(4, Rate.reset)) join) transpose(4) sum(2)) * 0.05;
	[pipesLR, bellsLR] join
}

fn pentatonic() S {
	let chans = voicer(kPentVoices, pentatonicVoice) sum(4);
	let mix1 = chans take(2);
	let mix2 = chans drop(2);
	let m1 = (mix1 combn(0.3, 8)) + (mix1 reverse)
		+ (mix1 combl(0.1 + urand(1, Rate.init) * 0.2, 0.3, 8))
		+ (mix1 combl(0.1 + urand(1, Rate.init) * 0.2, 0.3, 8));
	var m2 = mix2;
	for (i : (1..6)) {
		m2 = m2 alpasn(urand(2, Rate.init) * 0.05, 8);
	}
	(m1 + m2) gainOutlet
}

pentatonic defSynthX("pentatonic", tags) await;

---------------------------------------------------------------------------
-- 3. tarmac (examples-10.txt:117-176)
-- Two OverlapTextures, then six AllpassN(0.04, [rand, rand], 16 s) on the
-- sum. The tail is linear, so each texture gets its own six-allpass chain
-- (separate synths; the driver plays both).
-- A: OverlapTexture(.., 12, 3, 6, 2): a 4-mode Klank (linrand 50..2000, ring
--    0.2..12) on PinkNoise * (LFNoise1 0.0008 + 0.0022), half-wave rectified
--    with a random sign, into RLPF whose cutoff is a Decay2 per 8 Hz clock
--    tick (0.004, d in 0.05..0.5) scaled rand 5000 + 100..200; panned by
--    LFNoise1(rand 1) with level Decay2 of an ImpulseSequencer pattern (one
--    of eight, .scramble -> random rotate).
-- B: OverlapTexture(.., 8, 3, 4, 2): Klank (linrand 700..6000, ring linrand
--    0.2..12) on PinkNoise * 0.0008, LFNoise1 pan, level LFNoise1 whose rate
--    is itself LFNoise1(1)*3+4, 0.2..1.0.

const kTarATrans = 3.0;
const kTarAHold = 15.0;
const kTarAInterval = 2.5;      -- 15 / 6
const kTarAVoices = 8;          -- ceil(life 18 / 2.5)
const kTarBTrans = 3.0;
const kTarBHold = 11.0;
const kTarBInterval = 2.75;     -- 11 / 4
const kTarBVoices = 6;          -- ceil(life 14 / 2.75)

fn tarmacPatterns() [S] = [
	[1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1] vec,
	[1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1] vec,
	[1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0] vec,
	[1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0] vec,
	[1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0] vec,
	[1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0] vec,
	[1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0] vec,
	[1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0] vec
];

fn tarmacAVoice() S {
	let t = lfimp(8);
	let d = 0.05 + urand(1, Rate.reset) * 0.45;
	let exc = pink() * (((urand(1, Rate.reset) * 3) lfnoise1(1)) * 0.0008 + 0.0022);
	let bank = klank(exc, linrandv(50, 2000, 4, Rate.reset), 1, 0.2 + urand(4, Rate.reset) * 11.8);
	let sign = select2(urand(1, Rate.reset) < 0.5, -1, 1);
	let rect = (bank max(0)) * sign;
	let cutoff = (t decay2(0.004, d)) * (urand(1, Rate.reset) * 5000) + (100 + urand(1, Rate.reset) * 100);
	let filt = rect rlpf(cutoff, 0.2);
	let level = (iseq(t, choosePattern(tarmacPatterns()), 16) decay2(0.004, d));
	((filt * level) * xenv(kTarATrans)) pan((urand(1, Rate.reset) lfnoise1(1))) join
}

fn allpassTail6(z S, decayTime Float) S {
	var y = z;
	for (i : (1..6)) {
		y = y alpasn(urand(2, Rate.init) * 0.04, decayTime);
	}
	y
}

fn tarmacA() S = allpassTail6(voicer(kTarAVoices, tarmacAVoice) sum(2), 16) gainOutlet;

tarmacA defSynthX("tarmacA", tags) await;

fn tarmacBVoice() S {
	let bank = klank(pink() * 0.0008, linrandv(700, 6000, 4, Rate.reset), 1, linrandv(0.2, 12, 4, Rate.reset));
	let levelRate = ((1 asSignal) lfnoise1(1)) * 3 + 4;
	let level = (levelRate lfnoise1(1)) * 0.4 + 0.6;
	((bank * level) * xenv(kTarBTrans)) pan((urand(1, Rate.reset) lfnoise1(1))) join
}

fn tarmacB() S = allpassTail6(voicer(kTarBVoices, tarmacBVoice) sum(2), 16) gainOutlet;

tarmacB defSynthX("tarmacB", tags) await;

---------------------------------------------------------------------------
-- 4. dancing shadows (examples-10.txt:180-244)
-- OverlapTexture(.., 45, 2, 0.95, 2): essentially one 49 s instance at a
-- time. Per instance: a clock at 5..5.5 Hz, five random scale notes (SC
-- sorts them; not sorted here), and n = 20 sines at pentatonic-ish degrees
-- (0..37 over the 5-note scale + 30), each pulsed by an ImpulseSequencer of
-- a rhythm pattern (one of eighteen Prand'd Pseqs -> one pattern per
-- instance here) scaled by (sqrt(40)*6)/(n*sqrt(freq)), through
-- Decay2(0.005, 6/rate) and an LFNoise1 amplitude, panned. Plus a melody:
-- Resonz(WhiteNoise, lagged pitch, 0.01) * 1.5 whose degree random-walks
-- (0.3 chance per clock of +/- up to 2, folded 0..7) over the scale + 84.

const kDsTrans = 2.0;
const kDsHold = 47.0;
const kDsInterval = 49.47;      -- 47 / 0.95
const kDsVoices = 1;

fn dancingPatterns() [S] = [
	[2.0, 0.0, 2.0, 0.0, 1.0, 0.0, 1.0, 1.0,  2.0, 0.0, 2.0, 0.0, 1.0, 0.0, 1.0, 1.0] vec,
	[2.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0,  2.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0] vec,
	[2.0, 0.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0,  2.0, 0.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0] vec,
	[2.0, 0.3, 0.3, 1.0, 0.3, 0.3, 1.0, 0.3,  2.0, 0.3, 0.3, 1.0, 0.3, 0.3, 1.0, 0.3] vec,
	[2.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0,  2.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0] vec,
	[2.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0,  2.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0] vec,
	[2.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,  2.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0] vec,
	[0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0,  0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0] vec,
	[1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0,  1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0] vec,
	[1.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0,  1.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0] vec,
	[1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0,  1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0] vec,
	[1.0, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0,  1.0, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0] vec,
	[0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, 1.0,  0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, 1.0] vec,
	[0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0,  0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0] vec,
	[2.0, 0.0, 1.0, 1.0, 0.0, 1.0, 0.0, 0.0,  2.0, 0.0, 1.0, 1.0, 0.0, 1.0, 0.0, 0.0] vec,
	[0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0,  0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0] vec,
	[2.0, 1.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0,  2.0, 1.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0] vec,
	[1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0,  0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0] vec
];

fn dancingShadowsVoice() S {
	let n = 20;
	let rate = 5 + urand(1, Rate.reset) * 0.5;
	let trig = lfimp(rate);
	let notes = urand(5, Rate.reset) * 12;
	let freq = (degreeToKey(irand(0, 37, n, Rate.reset), notes, 5) + 30) nnhz;
	let pulse = iseq(trig, choosePattern(dancingPatterns()), 16) * ((6.324555 * 6) / (n * (freq sqrt)));
	let env = pulse decay2(0.005, 6 / rate);
	let amp = ((urand(n, Rate.reset) * 0.5) lfnoise1(n)) * env;
	let sines = (((freq sinosc) * amp) pan(birand(n, Rate.reset)) join) transpose(n) sum(2);
	-- melody: a folded random walk of the degree, stepped on the clock
	let deg = delayVar() init(1, irand(0, 7, 1, Rate.reset) f32);
	let step = select2(urand() < 0.3, irand(-2, 2, 1) f32, 0);
	let newDeg = select2(trig > 0, (deg(1) + step) fold(0, 8), deg(1));
	deg <- newDeg;
	let melodyFreq = ((degreeToKey(newDeg, notes, 5) + 84) nnhz) lag(0.05);
	let melody = resonz(white(), melodyFreq, 0.01) * 1.5;
	(sines + melody) * xenv(kDsTrans)
}

fn dancingShadows() S = voicer(kDsVoices, dancingShadowsVoice) sum(2) gainOutlet;

dancingShadows defSynthX("dancingShadows", tags) await;

---------------------------------------------------------------------------
-- 5. choip choip choip (examples-10.txt:248-271)
-- OverlapTexture(.., 10, 1, 8, 2): impulses whose rate XLines between two
-- exprand(1, 30) values over 12 s excite Decay2(0.01, 0.2) * SinOsc whose
-- frequency is f (XLine between exprand 600..8000) minus 0.9 f * Decay2(
-- impulses, 0.05, 0.5) -- a downward chirp per impulse; impulse amplitude
-- XLines between exprand(0.01, 0.5); pan Lines between two random spots.
-- Four AllpassN(0.1, [rand 0.05, rand 0.05], 4) on the sum.

const kChTrans = 1.0;
const kChHold = 11.0;
const kChInterval = 1.375;      -- 11 / 8
const kChVoices = 9;            -- ceil(life 12 / 1.375)
const kChT = 12.0;

fn choipVoice() S {
	let impulses = lfimp(xline(exprand(1, 30, 1, Rate.reset), exprand(1, 30, 1, Rate.reset), kChT));
	let f = xline(exprand(600, 8000, 1, Rate.reset), exprand(600, 8000, 1, Rate.reset), kChT);
	let ampLine = xline(exprand(0.01, 0.5, 1, Rate.reset), exprand(0.01, 0.5, 1, Rate.reset), kChT);
	let chirp = (impulses decay2(0.05, 0.5)) * (0 - 0.9 * f) + f;
	let sig = ((impulses * ampLine) decay2(0.01, 0.2)) * (chirp sinosc);
	let panPos = line(birand(1, Rate.reset), birand(1, Rate.reset), kChT);
	(sig * xenv(kChTrans)) pan(panPos) join
}

fn choip() S {
	var z = voicer(kChVoices, choipVoice) sum(2);
	for (i : (1..4)) {
		z = z alpasn(urand(2, Rate.init) * 0.05, 4);
	}
	z gainOutlet
}

choip defSynthX("choip", tags) await;

---------------------------------------------------------------------------
-- driver

fn playAllSc10() {
	go(coro fn() Float {
		"start playing SC2 examples-10" println;
		ae.setSharedInput(0, 0.5);
		ae.setSharedInput(1, 0.5);

		"-- mix of three (sweepers + ring klanks + bottles) --" println;
		let n1 = "mixSweepers" play;
		let n2 = "mixRingKlanks" play;
		let n3 = "mixBottles" play;
		go(overlapTextureSpawner(n1, kSwVoices, kSwInterval, kSwHold, 16.0));
		go(overlapTextureSpawner(n2, kRkVoices, kRkInterval, kRkHold, 16.0));
		go(overlapTextureSpawner(n3, kBtVoices, kBtInterval, kBtHold, 16.0));
		yield 16.0 + kBtHold + kBtTrans;
		n1 stop; n2 stop; n3 stop;

		"pentatonic" texture(kPentVoices, kPentInterval, kPentHold, 16.0, 1.0) yieldAll;

		"-- tarmac (A + B) --" println;
		let ta = "tarmacA" play;
		let tb = "tarmacB" play;
		go(overlapTextureSpawner(ta, kTarAVoices, kTarAInterval, kTarAHold, 16.0));
		go(overlapTextureSpawner(tb, kTarBVoices, kTarBInterval, kTarBHold, 16.0));
		yield 16.0 + kTarAHold + kTarATrans + 4.0;
		ta stop; tb stop;

		-- one 49 s instance would be faithful; play a shorter one
		"-- dancingShadows --" println;
		let ds = "dancingShadows" play;
		overlapTextureSpawner(ds, kDsVoices, kDsInterval, 26.0, 1.0) yieldAll;
		yield 26.0 + kDsTrans;
		ds stop;

		"choip" texture(kChVoices, kChInterval, kChHold, 14.0, kChTrans) yieldAll;

		ae.endRender(0.1);
		"done playing SC2 examples-10" println;
	}());
}

fn renderSc10() {
	let h = "/tmp/sc2_examples_10.wav" ae.renderNRT(300, playAllSc10);
	await ae.renderDone(h);
	"render done" println;
}

--renderSc10();
--playAllSc10();
