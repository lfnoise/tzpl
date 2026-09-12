-- Tzopilotl translations of SuperCollider examples from
-- sc-examples/SC2-examples/examples-6.txt. Shared machinery lives in
-- sc2_common.x; sc2_examples_1.x explains the texture timing model.
--
-- Status (examples-6.txt blocks, in order):
--   1  sweepy noise               -- DONE (sweepyNoise)
--   2  string wander-cluster      -- DONE (stringWander) -- the wandering
--                                    note is script state (noteWalkSpawner)
--   3  string wander w/ vibrato   -- DONE (stringWanderVibrato)
--   4  pipe wander-cluster        -- DONE (pipeWander) -- negative-feedback comb
--   5  comb delay sweeps          -- DONE (combSweeps)
--   6  repeating harmonic Klank   -- DONE (harmonicKlank)
--   7  repeating inharmonic Klank -- DONE (inharmonicKlank)
--   8  noise burst sweep          -- DONE (noiseBurstSweep)
--   9  saucer base                -- DONE (saucerBase)
--   10 alien meadow               -- DONE (alienMeadow)
--   11 fast LFOs with slow beats  -- DONE (fastLfoBeats)
--   12 birdies                    -- DONE (birdies)
--   13 phase modulation, slow beats -- DONE (phaseModBeats) -- mouse x sampled
--                                    per spawn by the script (mx.poll)
--   14 hard sync sawtooth         -- DONE (hardSyncSaw) -- local syncSaw
--   15 noise modulated sines      -- DONE (noiseModSines)
--   16 noise modulated sawtooths  -- DONE (noiseModSaws)
--   17 RLPF4 (mouse)              -- DONE (rlpf4Sweep) -- two cascaded rlpf
--                                    stand in for the 4-pole; local pingPong
--   18 RLPF4 (MIDI note)          -- skipped (needs MIDI input)

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import filters.*;
import audio_engine as ae;
import clock.*;
import sc2_common.*;

let tags = ["sc2-examples-6"];

fn xenv(trans Float) S = linen(gate(), trans, trans) rpanfun;

---------------------------------------------------------------------------
-- Local ugens

-- LPZ2: two-zero lowpass, (x + 2 x[n-1] + x[n-2]) / 4.
fn lpz2(x S) S = (x + 2 * (x z1) + (x z2)) * 0.25;

-- A comb with NEGATIVE feedback (SC's CombL/CombA with a negative decay
-- time): the feedback coefficient is -0.001^(delay/|decay|), which
-- emphasizes odd harmonics -- the "pipe" sound. Linear interpolation.
fn combNeg(x S, delayTime AsSignal, maxDelayTime AsSignal, decayTime AsSignal) S {
	let a = 0.0 - decay60dB(decayTime / delayTime);
	let y = delayVar(maxDelayTime * fs());
	y <- x + a * y(delayTime * fs(), Interpolation.linear)
}

-- SyncSaw(syncFreq, sawFreq): a sawtooth at sawFreq whose phase resets on
-- every period of syncFreq -- the saw phase is the sync phasor scaled by
-- the frequency ratio, wrapped.
fn syncSaw(syncFreq S, sawFreq S) S = ((syncFreq phasor) * (sawFreq / syncFreq)) frac bi;

-- RLPF4(in, cutoffHz, resonance 0..1): stand-in for SC2's 4-pole resonant
-- lowpass -- two cascaded rlpf stages with rq = 1 - resonance (floored).
fn rlpf4(in S, fc AsSignal, res AsSignal) S {
	let rq = (1 - res) max(0.02);
	(in rlpf(fc, rq)) rlpf(fc, rq)
}

-- PingPongN(left, right, maxDelay, delay, feedback): each channel's delayed
-- output feeds back into the OTHER channel -- a 2-channel ring with the
-- feedback read rotated by one channel.
fn pingPong(in S, delayTime Float, feedback AsSignal) S {
	let y = delayVar(delayTime * fs() + 8);
	y <- in + feedback * (y(delayTime * fs(), Interpolation.none) rotate(1))
}

---------------------------------------------------------------------------
-- Script-side helpers for the wander-cluster family (#2-#5): the MIDI note
-- is a random walk ACROSS spawns (note = fold(note + rand 15 - 7, lo, hi)),
-- so it lives in the spawning script and arrives as a noteParam.

-- OverlapTexture(.., 4/3, 4/3, 6, 2) for all four wander examples.
const kWanderTrans = 1.3333333;
const kWanderHold = 2.6666667;     -- sustain + transition
const kWanderInterval = 0.4444444; -- (4/3 + 4/3) / 6
const kWanderVoices = 9;           -- ceil(life 4 / 0.444)

fn foldF(x Float, lo Float, hi Float) Float {
	var y = x;
	if (y > hi) { y = 2.0 * hi - y; }
	if (y < lo) { y = 2.0 * lo - y; }
	y
}

coro fn noteWalkSpawner(nodeID Int, numVoices Int, interval Float, holdTime Float, totalDur Float,
                        note0 Float, lo Float, hi Float, sweep Bool) Float {
	var note = note0;
	let u0 Float = urand();
	var endNote = foldF(note0 + u0 * 15 - 7, lo, hi);
	var id = 0;
	var elapsed = 0.0;
	while (elapsed < totalDur) {
		let u1 Float = urand();
		note = foldF(note + u1 * 15 - 7, lo, hi);
		let u2 Float = urand();
		endNote = foldF(endNote + u2 * 15 - 7, lo, hi);
		let params = sweep ? [note, endNote, 1.0] : [note, 1.0];
		let noteID = id;
		ae.begin(); ae.noteOn(nodeID, noteID, params); ae.sched(0);
		go(coro fn() Float {
			yield holdTime;
			ae.begin(); ae.noteOff(nodeID, noteID); ae.sched(0);
		}());
		id = (id + 1) % numVoices;
		elapsed = elapsed + interval;
		yield interval;
	}
}

coro fn wanderTexture(name String, note0 Float, lo Float, hi Float, sweep Bool, dur Float) Float {
	"-- %^ --" fmt(name) println;
	let node = name play;
	noteWalkSpawner(node, kWanderVoices, kWanderInterval, kWanderHold, dur, note0, lo, hi, sweep) yieldAll;
	yield kWanderHold + kWanderTrans;
	node stop;
}

fn pNote(lo Float, hi Float) S =
	noteParam("note", ControlSpec{lo, hi, (lo + hi) * 0.5, ControlWarp.linear});

---------------------------------------------------------------------------
-- 1. sweepy noise (examples-6.txt:15-25)
-- LFSaw(MouseX 4..60 Hz exp, mul MouseY 200..8000 exp, add 1.2 mul) sweeps an
-- RLPF (1/Q 0.1) on stereo white noise * 0.03; CombN(0.3, 0.3, 2) mixed in.

fn sweepyNoise() S {
	let lfoDepth = mouseYExp(200, 8000);
	let lfoRate = mouseXExp(4, 60);
	let freq = (lfoRate lfsaw) * lfoDepth + lfoDepth * 1.2;
	let filtered = (white(2) * 0.03) rlpf(freq, 0.1);
	((filtered combn(0.3, 2)) + filtered) gainOutlet
}

sweepyNoise defSynthX("sweepyNoise", tags) await;

---------------------------------------------------------------------------
-- 2. string wander-cluster (examples-6.txt:29-43)
-- CombA(WhiteNoise * 0.008, 0.01, 1/freq, 1000/freq): a Karplus-Strong-ish
-- string at the wandering note (fold 50..120), panned. CombA is allpass
-- interpolated; combc (cubic) is the nearest here. The max delay is 20 ms
-- (MIDI 50 is 15 ms; SC's 10 ms would have clipped it).

fn stringWanderVoice() S {
	let note = pNote(50.0, 120.0);
	let delay = 1 / (note nnhz);
	(((white() * 0.008) combc(delay, 0.02, delay * 1000)) * xenv(kWanderTrans)) pan(birand(1, Rate.reset)) join
}

fn stringWander() S = voicer(kWanderVoices, stringWanderVoice) sum(2) gainOutlet;

stringWander defSynthX("stringWander", tags) await;

---------------------------------------------------------------------------
-- 3. string wander-cluster with vibrato (examples-6.txt:47-64)
-- As #2 (fold 60..110) with the comb's delay tracking a 4..8 Hz, 1% vibrato
-- on the frequency; the decay time follows the unmodulated frequency.

fn stringWanderVibratoVoice() S {
	let note = pNote(60.0, 110.0);
	let freq0 = note nnhz;
	let decayTime = 1000 / freq0;
	let freq = ((4 + urand(1, Rate.reset) * 4) sinosc) * 0.01 * freq0 + freq0;
	let delay = 1 / freq;
	(((white() * 0.008) combc(delay, 0.02, decayTime)) * xenv(kWanderTrans)) pan(birand(1, Rate.reset)) join
}

fn stringWanderVibrato() S = voicer(kWanderVoices, stringWanderVibratoVoice) sum(2) gainOutlet;

stringWanderVibrato defSynthX("stringWanderVibrato", tags) await;

---------------------------------------------------------------------------
-- 4. pipe wander-cluster (examples-6.txt:68-82)
-- CombL(LPZ2(WhiteNoise * 0.01), 0.01, 1/freq, -0.4): a NEGATIVE decay time
-- gives negative feedback -- odd harmonics only, a closed pipe. Note fold
-- 80..120.

fn pipeWanderVoice() S {
	let note = pNote(80.0, 120.0);
	let delay = 1 / (note nnhz);
	((combNeg(lpz2(white() * 0.01), delay, 0.02, 0.4)) * xenv(kWanderTrans)) pan(birand(1, Rate.reset)) join
}

fn pipeWander() S = voicer(kWanderVoices, pipeWanderVoice) sum(2) gainOutlet;

pipeWander defSynthX("pipeWander", tags) await;

---------------------------------------------------------------------------
-- 5. comb delay sweeps (examples-6.txt:86-105)
-- Two random walks (note, endNote, both fold 50..120); each instance's comb
-- delay sweeps along Line(note, endNote, 4 s) in MIDI space, with the decay
-- time from the start note.

fn combSweepsVoice() S {
	let note = pNote(50.0, 120.0);
	let endNote = noteParam("endNote", ControlSpec{50.0, 120.0, 85.0, ControlWarp.linear});
	let noteSweep = line(note, endNote, 4);
	let delay = 1 / (noteSweep nnhz);
	let decayTime = 1000 / (note nnhz);
	(((white() * 0.005) combc(delay, 0.02, decayTime)) * xenv(kWanderTrans)) pan(birand(1, Rate.reset)) join
}

fn combSweeps() S = voicer(kWanderVoices, combSweepsVoice) sum(2) gainOutlet;

combSweeps defSynthX("combSweeps", tags) await;

---------------------------------------------------------------------------
-- 6. repeating harmonic Klank (examples-6.txt:109-129)
-- OverlapTexture(.., 8, 2, 4, 2): Decay(Dust 0.8 * 0.01, 3.4) * LFSaw(linrand 40)
-- excites two 8-mode Klanks whose modes are harmonics (1..12) of one of
-- 400..1600 Hz, ring 0.4 + rand 3.

const kHkTrans = 2.0;
const kHkHold = 10.0;
const kHkInterval = 2.5;        -- 10 / 4
const kHkVoices = 5;            -- ceil(life 12 / 2.5)

fn harmonicKlankVoice() S {
	let p = 8;
	let s = ((dust(0.8) * 0.01) decay(3.4)) * (linrandv(0, 40, 1, Rate.reset) lfsaw);
	let fundamentals = [400, 500, 600, 700, 800, 900, 1000, 1200, 1400, 1500, 1600] @ asSignal;
	let f = select(irand(0, 10, 1, Rate.reset), fundamentals);
	let freqs = f * (irand(1, 12, 2 * p, Rate.reset) f32);
	let rings = 0.4 + urand(2 * p, Rate.reset) * 3;
	(klankModes(s, freqs, 1, rings) sum(2)) * xenv(kHkTrans)
}

fn harmonicKlank() S = voicer(kHkVoices, harmonicKlankVoice) sum(2) gainOutlet;

harmonicKlank defSynthX("harmonicKlank", tags) await;

---------------------------------------------------------------------------
-- 7. repeating inharmonic Klank (examples-6.txt:133-152)
-- OverlapTexture(.., 8, 8, 4, 2): same exciter (Dust * 0.004), modes at
-- 80 + linrand 10000, ring 0.4 + rand 4.

const kIkTrans = 8.0;
const kIkHold = 16.0;
const kIkInterval = 4.0;        -- 16 / 4
const kIkVoices = 6;            -- ceil(life 24 / 4)

fn inharmonicKlankVoice() S {
	let p = 8;
	let s = ((dust(0.8) * 0.004) decay(3.4)) * (linrandv(0, 40, 1, Rate.reset) lfsaw);
	let freqs = linrandv(80, 10080, 2 * p, Rate.reset);
	let rings = 0.4 + urand(2 * p, Rate.reset) * 4;
	(klankModes(s, freqs, 1, rings) sum(2)) * xenv(kIkTrans)
}

fn inharmonicKlank() S = voicer(kIkVoices, inharmonicKlankVoice) sum(2) gainOutlet;

inharmonicKlank defSynthX("inharmonicKlank", tags) await;

---------------------------------------------------------------------------
-- 8. noise burst sweep (examples-6.txt:156-167)
-- Bursts: max(0, -LFSaw(MouseX 10..60 exp)) gates white noise into a Resonz
-- (bwr 0.1) whose centre (MouseY 400..8000 exp) is swept +/- itself by a
-- 0.2 Hz sine.

fn noiseBurstSweep() S {
	let lfoRate = mouseXExp(10, 60);
	let amp = max(0, 0 - (lfoRate lfsaw));
	let cf0 = mouseYExp(400, 8000);
	let cfreq = (0.2 sinosc) * cf0 + 1.05 * cf0;
	(resonz(white() * amp, cfreq, 0.1)) gainOutlet
}

noiseBurstSweep defSynthX("noiseBurstSweep", tags) await;

---------------------------------------------------------------------------
-- 9. saucer base (examples-6.txt:171-184)
-- OverlapTexture(.., 6, 2, 4, 2): three nested sines, each modulating the
-- next's frequency (a rand 20, b rand 1000, c rand 5000), * 0.1, panned.

const kSaucerTrans = 2.0;
const kSaucerHold = 8.0;
const kSaucerInterval = 2.0;    -- 8 / 4
const kSaucerVoices = 5;        -- ceil(life 10 / 2)

fn saucerVoice() S {
	let a = urand(1, Rate.reset) * 20;
	let b = urand(1, Rate.reset) * 1000;
	let c = urand(1, Rate.reset) * 5000;
	let m1 = (a sinosc) * b + 1.1 * b;
	let m2 = (m1 sinosc) * c + 1.1 * c;
	(((m2 sinosc) * 0.1) * xenv(kSaucerTrans)) pan(birand(1, Rate.reset)) join
}

fn saucerBase() S = voicer(kSaucerVoices, saucerVoice) sum(2) gainOutlet;

saucerBase defSynthX("saucerBase", tags) await;

---------------------------------------------------------------------------
-- 10. alien meadow (examples-6.txt:188-201)
-- OverlapTexture(.., 6, 2, 6, 2): SinOsc at b (rand 5000) +/- 10% by a rand-20 Hz
-- sine, amplitude a 0..0.1 sine at c (rand 20 Hz), panned.

const kMeadowInterval = 1.3333333;  -- 8 / 6
const kMeadowVoices = 8;            -- ceil(life 10 / 1.333)

fn meadowVoice() S {
	let a = urand(1, Rate.reset) * 20;
	let b = urand(1, Rate.reset) * 5000;
	let c = urand(1, Rate.reset) * 20;
	let freq = (a sinosc) * (0.1 * b) + b;
	let amp = (c sinosc) * 0.05 + 0.05;
	(((freq sinosc) * amp) * xenv(kSaucerTrans)) pan(birand(1, Rate.reset)) join
}

fn alienMeadow() S = voicer(kMeadowVoices, meadowVoice) sum(2) gainOutlet;

alienMeadow defSynthX("alienMeadow", tags) await;

---------------------------------------------------------------------------
-- 11. fast LFOs with slow beats (examples-6.txt:205-219)
-- OverlapTexture(.., 8, 4, 4, 2): a stereo pair of modulator rates a0 and
-- a0 +/- 1 Hz (beating), carrier b (rand 2000) +/- rand * b, and a stereo
-- amplitude LFO pair c = a +/- 1 Hz -- all the beating comes from the
-- near-identical channel pairs, so the voice is stereo without Pan2.

const kFastTrans = 4.0;
const kFastHold = 12.0;
const kFastInterval = 3.0;      -- 12 / 4
const kFastVoices = 6;          -- ceil(life 16 / 3)

fn fastLfoVoice() S {
	let a0 = 40 + urand(1, Rate.reset) * 200;
	let a = a0 + ([0.0, 1.0] vec) * birand(1, Rate.reset);
	let b = urand(1, Rate.reset) * 2000;
	let c = a + birand(2, Rate.reset);
	let freq = (a sinosc) * (urand(1, Rate.reset) * b) + b;
	let amp = (c sinosc) * 0.05 + 0.05;
	((freq sinosc) * amp) * xenv(kFastTrans)
}

fn fastLfoBeats() S = voicer(kFastVoices, fastLfoVoice) sum(2) gainOutlet;

fastLfoBeats defSynthX("fastLfoBeats", tags) await;

---------------------------------------------------------------------------
-- 12. birdies (examples-6.txt:223-236)
-- OverlapTexture(.., 7, 4, 4, 2): a chirp -- SinOsc whose frequency is a
-- lagged LFSaw (mul -(1000 + rand 800), add 4000 +/- 1200) running at a rate
-- set by two summed LFPulses (0.4..1.4 Hz, width 0.1..0.9, mul 4..7,
-- add 2 / 0); amplitude a lagged LFPulse(0.2..0.7 Hz, width 0.4) * 0.02.

const kBirdTrans = 4.0;
const kBirdHold = 11.0;
const kBirdInterval = 2.75;     -- 11 / 4
const kBirdVoices = 6;          -- ceil(life 15 / 2.75)

fn birdiesVoice() S {
	let p1 = ((0.4 + urand(1, Rate.reset)) lfupulse(0.1 + urand(1, Rate.reset) * 0.8)) * (4 + urand(1, Rate.reset) * 3) + 2;
	let p2 = ((0.4 + urand(1, Rate.reset)) lfupulse(0.1 + urand(1, Rate.reset) * 0.8)) * (4 + urand(1, Rate.reset) * 3);
	let sawRate = p1 + p2;
	let freq = ((sawRate lfsaw) * (0 - (1000 + urand(1, Rate.reset) * 800)) + (4000 + birand(1, Rate.reset) * 1200)) lag(0.05);
	let amp = (((0.2 + urand(1, Rate.reset) * 0.5) lfupulse(0.4)) * 0.02) lag(0.3);
	(((freq sinosc) * amp) * xenv(kBirdTrans)) pan(birand(1, Rate.reset)) join
}

fn birdies() S = voicer(kBirdVoices, birdiesVoice) sum(2) gainOutlet;

birdies defSynthX("birdies", tags) await;

---------------------------------------------------------------------------
-- 13. phase modulation with slow beats (examples-6.txt:240-261)
-- OverlapTexture(.., 4, 4, 4, 2): three chained FSinOsc stages, each a stereo
-- pair at [f, f +/- 1] with f = rand(x), scaled by MouseY 0..2 (the index)
-- and ADDED to the previous stage's output (FSinOsc(freq, mul, add)); the
-- sum drives the phase of a final SinOsc pair. `mx.poll` samples MouseX
-- (100..6000 exp) once per instance: the spawner reads the mouse at each
-- spawn and passes it as a noteParam. Phase is radians in SC, cycles here.

const kPmTrans = 4.0;
const kPmHold = 8.0;
const kPmInterval = 2.0;        -- 8 / 4
const kPmVoices = 6;            -- ceil(life 12 / 2)

fn phaseModVoice() S {
	let x = noteParam("x", ControlSpec{100.0, 6000.0, 1000.0, ControlWarp.exponential});
	let my = mouseY(0, 2);
	var a = 0 asSignal;
	for (i : (1..3)) {
		let f = urand(1, Rate.reset) * x;
		let f2 = f + ([0.0, 1.0] vec) * birand(1, Rate.reset);
		a = (f2 fsinosc) * my + a;
	}
	let f = urand(1, Rate.reset) * x;
	let f2 = f + ([0.0, 1.0] vec) * birand(1, Rate.reset);
	((f2 sinosc(a * (1.0 / twopi))) * 0.1) * xenv(kPmTrans)
}

fn phaseModBeats() S = voicer(kPmVoices, phaseModVoice) sum(2) gainOutlet;

phaseModBeats defSynthX("phaseModBeats", tags) await;

coro fn phaseModSpawner(nodeID Int, totalDur Float) Float {
	var id = 0;
	var elapsed = 0.0;
	while (elapsed < totalDur) {
		let m Float = ae.getSharedInput(0);           -- MouseX, normalized
		let x = 100.0 * exp(m * log(60.0));           -- 100..6000 exponential
		let noteID = id;
		ae.begin(); ae.noteOn(nodeID, noteID, [x, 1.0]); ae.sched(0);
		go(coro fn() Float {
			yield kPmHold;
			ae.begin(); ae.noteOff(nodeID, noteID); ae.sched(0);
		}());
		id = (id + 1) % kPmVoices;
		elapsed = elapsed + kPmInterval;
		yield kPmInterval;
	}
}

---------------------------------------------------------------------------
-- 14. hard sync sawtooth with LFO (examples-6.txt:265-276)
-- OverlapTexture(.., 4, 4, 4, 2): SyncSaw([f, f + 0.2], SinOsc.kr(0.2, [0, rand],
-- 2f, 3f), 0.05) with f = midicps(30 + rand 50); the summed texture goes
-- through CombN(0.3, 0.3, 4) mixed with its own channel-reversed dry signal.

const kSyncTrans = 4.0;
const kSyncHold = 8.0;
const kSyncInterval = 2.0;      -- 8 / 4
const kSyncVoices = 6;          -- ceil(life 12 / 2)

fn hardSyncVoice() S {
	let f = (30 + urand(1, Rate.reset) * 50) nnhz;
	let sawFreq = ((0.2 asSignal) sinosc(([0.0, 1.0] vec) * urand(1, Rate.reset))) * (2 * f) + 3 * f;
	let sync = f + ([0.0, 0.2] vec);
	(syncSaw(sync, sawFreq) * 0.05) * xenv(kSyncTrans)
}

fn hardSyncSaw() S {
	let a = voicer(kSyncVoices, hardSyncVoice) sum(2);
	((a combn(0.3, 4)) + (a reverse)) gainOutlet
}

hardSyncSaw defSynthX("hardSyncSaw", tags) await;

---------------------------------------------------------------------------
-- 15. noise modulated sines (examples-6.txt:280-291)
-- FSinOsc([f, f + 0.2], LFNoise2.kr(f * [0.15, 0.16], 0.1)): sines whose
-- amplitude is noise at 15-16% of their own frequency; f = midicps(60 + rand 40).
-- Same CombN + reversed-dry mix.

fn noiseModSinesVoice() S {
	let f = (60 + urand(1, Rate.reset) * 40) nnhz;
	let freqs = f + ([0.0, 0.2] vec);
	let amp = ((f * ([0.15, 0.16] vec)) lfnoise3(2)) * 0.1;
	((freqs fsinosc) * amp) * xenv(kSyncTrans)
}

fn noiseModSines() S {
	let a = voicer(kSyncVoices, noiseModSinesVoice) sum(2);
	((a combn(0.3, 4)) + (a reverse)) gainOutlet
}

noiseModSines defSynthX("noiseModSines", tags) await;

---------------------------------------------------------------------------
-- 16. noise modulated sawtooths (examples-6.txt:295-306)
-- As #15 with LFSaw and a 0.5 s comb.

fn noiseModSawsVoice() S {
	let f = (60 + urand(1, Rate.reset) * 40) nnhz;
	let freqs = f + ([0.0, 0.2] vec);
	let amp = ((f * ([0.15, 0.16] vec)) lfnoise3(2)) * 0.1;
	((freqs lfsaw) * amp) * xenv(kSyncTrans)
}

fn noiseModSaws() S {
	let a = voicer(kSyncVoices, noiseModSawsVoice) sum(2);
	((a combn(0.5, 4)) + (a reverse)) gainOutlet
}

noiseModSaws defSynthX("noiseModSaws", tags) await;

---------------------------------------------------------------------------
-- 17. RLPF4 - 4th order resonant lowpass filter (examples-6.txt:310-325)
-- Perfect fifths of a stepped random note (LFNoise0 0.2 Hz, 33..47) into the
-- 4-pole lowpass, cutoff swept by a stereo pair of LFSaws (MouseX 0.04..40
-- Hz and double) mapped exponentially 0.01..0.4 of the sample rate,
-- resonance MouseY 0..0.99; then a PingPong delay (0.16 s, 0.6 feedback).
-- RLPF4's cutoff in SC2 was a fraction of the sample rate.

fn rlpf4Sweep() S {
	let lfofreq = mouseXExp(0.04, 40);
	let note = ((((0.2 asSignal) lfnoise0(1)) * 7 + 40) round(1)) nnhz;
	let in = (note * ([1.0, 1.5] vec)) lfsaw;
	let sawLfo = 0 - ((lfofreq * ([1.0, 2.0] vec)) lfsaw);
	let cutoff = (sawLfo linexp(-1, 1, 0.01, 0.4)) * fs();
	let z = rlpf4(in, cutoff, mouseY(0, 0.99)) * 0.1;
	(pingPong(z, 0.16, 0.6)) gainOutlet
}

rlpf4Sweep defSynthX("rlpf4Sweep", tags) await;

---------------------------------------------------------------------------
-- driver

fn playAllSc6() {
	go(coro fn() Float {
		"start playing SC2 examples-6" println;
		ae.setSharedInput(0, 0.5);
		ae.setSharedInput(1, 0.5);

		"sweepyNoise" playFor(8.0) yieldAll;
		"stringWander" wanderTexture(75.0, 50.0, 120.0, false, 12.0) yieldAll;
		"stringWanderVibrato" wanderTexture(85.0, 60.0, 110.0, false, 12.0) yieldAll;
		"pipeWander" wanderTexture(100.0, 80.0, 120.0, false, 12.0) yieldAll;
		"combSweeps" wanderTexture(85.0, 50.0, 120.0, true, 12.0) yieldAll;
		"harmonicKlank" texture(kHkVoices, kHkInterval, kHkHold, 10.0, kHkTrans) yieldAll;
		"inharmonicKlank" texture(kIkVoices, kIkInterval, kIkHold, 12.0, kIkTrans) yieldAll;
		"noiseBurstSweep" playFor(8.0) yieldAll;
		"saucerBase" texture(kSaucerVoices, kSaucerInterval, kSaucerHold, 10.0, kSaucerTrans) yieldAll;
		"alienMeadow" texture(kMeadowVoices, kMeadowInterval, kSaucerHold, 10.0, kSaucerTrans) yieldAll;
		"fastLfoBeats" texture(kFastVoices, kFastInterval, kFastHold, 12.0, kFastTrans) yieldAll;
		"birdies" texture(kBirdVoices, kBirdInterval, kBirdHold, 12.0, kBirdTrans) yieldAll;

		"-- phaseModBeats --" println;
		let pmNode = "phaseModBeats" play;
		phaseModSpawner(pmNode, 10.0) yieldAll;
		yield kPmHold + kPmTrans;
		pmNode stop;

		"hardSyncSaw" texture(kSyncVoices, kSyncInterval, kSyncHold, 10.0, kSyncTrans) yieldAll;
		"noiseModSines" texture(kSyncVoices, kSyncInterval, kSyncHold, 10.0, kSyncTrans) yieldAll;
		"noiseModSaws" texture(kSyncVoices, kSyncInterval, kSyncHold, 10.0, kSyncTrans) yieldAll;
		"rlpf4Sweep" playFor(10.0) yieldAll;

		ae.endRender(0.1);
		"done playing SC2 examples-6" println;
	}());
}

fn renderSc6() {
	let h = "/tmp/sc2_examples_6.wav" ae.renderNRT(420, playAllSc6);
	await ae.renderDone(h);
	"render done" println;
}

--renderSc6();
--playAllSc6();
