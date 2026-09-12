-- Tzopilotl translations of SuperCollider examples from
-- sc-examples/SC2-examples/examples-8.txt. Shared machinery lives in
-- sc2_common.x; sc2_examples_1.x explains the texture timing model.
--
-- Status (examples-8.txt blocks, in order):
--   1  modal space               -- DONE (modalSpace) -- DegreeToKey built inline
--   2  algorhythmic rhythms      -- DONE (algoRhythms) -- APPROX: the 20%-per-
--                                   4-beats rescramble/refrequency (a script
--                                   action) is not modelled; each voice keeps
--                                   its Rate.init rhythm (16 coin flips at
--                                   5/16, not exactly 5 hits) and frequency
--   3  wolf tones                -- DONE (wolfTones) -- the script scheduler
--                                   (random duration, new freq + lag each
--                                   time) is an in-graph countdown timer
--   4  Landon Rose Klank spawn   -- DONE (klankSpawn) -- Spawn index cycles
--                                   the 5 specs via a script + noteParam
--   5  screen zones              -- DONE (screenZones) -- all four processes
--                                   run, gated by the mouse quadrant (no Pause)
--   6  phase mod + adsr          -- DONE (phaseModAdsr)
--   7  chaotic Sequencer         -- DONE (chaoticSequencer)
--   8  sampled voice rhythms     -- skipped (needs the ":Sounds:floating_1"
--                                   sound file; otherwise = #2 with PlayBuf)

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import filters.*;
import audio_engine as ae;
import clock.*;
import sc2_common.*;

let tags = ["sc2-examples-8"];

fn xenv(trans Float) S = linen(gate(), trans, trans) rpanfun;

---------------------------------------------------------------------------
-- Local ugens

-- HPZ2: two-zero highpass, (x - 2 x[n-1] + x[n-2]) / 4.
fn hpz2(x S) S = (x - 2 * (x z1) + (x z2)) * 0.25;

-- SC2's Formant oscillator (see sc2_examples_1.x, alien froggies).
fn sc2Formant(fundfreq AsSignal, formfreq AsSignal, bwfreq AsSignal) S {
	let a = bwfreq / fundfreq;
	let b = formfreq / fundfreq;
	let p = fundfreq phasor;
	let w = select2(a * p < 1, (1 - cos2pi(a * p)) * 0.5, 0);
	w * sin2pi(b * p)
}

---------------------------------------------------------------------------
-- 1. modal space (examples-8.txt:15-40)
-- DegreeToKey(dorian, MouseX 0..15, 12, 1, 72): the mouse indexes a 7-note
-- scale table, octave-wrapping, from MIDI 72; +/- LFNoise1(3) * 0.04
-- semitones per channel, into a sine * 0.1. Drone: LFPulse fifths (MIDI 48,
-- 55, width 0.15) through RLPF swept by a 0.1 Hz sine over MIDI 62..82,
-- rq 0.1, * 0.1. CombN(0.31, 0.31, 2) mixed with the dry signal.

fn modalSpace() S {
	let scale = [0, 2, 3, 5, 7, 9, 10] @ asSignal;
	let i = mouseX(0, 15) floor;
	let oct = (i / 7) floor;
	let deg = i - 7 * oct;
	let key = 72 + 12 * oct + select(deg i32, scale);
	let lead = (((key + ((3 asSignal) lfnoise1(2)) * 0.04) nnhz) sinosc) * 0.1;
	let cutoff = ((0.1 sinosc) * 10 + 72) nnhz;
	let drone = (((([48, 55] vec) nnhz) lfupulse(0.15)) rlpf(cutoff, 0.1)) * 0.1;
	let mix = lead + drone;
	((mix combn(0.31, 2)) + mix) gainOutlet
}

modalSpace defSynthX("modalSpace", tags) await;

---------------------------------------------------------------------------
-- 2. algorhythmic rhythms (examples-8.txt:44-76)
-- Eight voices, each an ImpulseSequencer over a scrambled 16-step pattern
-- (5 hits) clocked at 8 Hz, Decay2(0.002, 0.3) shaping a soft-clipped Resonz
-- (white * 4, exprand 100..3200, bwr 0.008), panned; plus a shaker (HPZ2 of
-- Dust2 whose density is a lagged 2 Hz pulse, Resonz swept 3600..4400) and a
-- bass drum (an 8-step sequence into Decay2(0.001, 0.5) * 80 Hz sine).
-- The trigger is Impulse (SC's LFPulse.ar(8) trigger fires on its rising
-- edge; iseq advances on every sample the trigger is > 0).

fn algoRhythms() S {
	let t = lfimp(8);
	var mix = 0 asSignal;
	for (v : (1..8)) {
		let freq = exprand(100, 3200, 1, Rate.init);
		let pat = (urand(16, Rate.init) < 0.3125) f32;
		let hits = iseq(t, pat, 16);
		let tone = resonz(white() * 4, freq, 0.008) softclip;
		let voice = (hits decay2(0.002, 0.3)) * tone;
		mix = mix + (voice pan(birand(1, Rate.init)) join);
	}
	let density = (((2 asSignal) lfupulse(0.12)) * 10000) lag(0.1);
	let shaker = resonz(hpz2(dust2(density) * 2), (2 sinosc) * 400 + 4000, 0.2);
	let bass = (iseq(t, [1.0, 0.0, 0.2, 0.0, 0.4, 0.0, 0.2, 0.0] vec, 8) decay2(0.001, 0.5)) * ((80 sinosc) * 0.2);
	(mix + shaker + bass) gainOutlet
}

algoRhythms defSynthX("algoRhythms", tags) await;

---------------------------------------------------------------------------
-- 3. wolf tones (examples-8.txt:80-102)
-- Four Resonz(PinkNoise * 4, freq, bwr 0.002) voices, panned; a scheduler
-- picks, for each voice, a duration from [1/16, 1/8, 1/2, 2] s, sets the
-- frequency to 200 + rand 600 with a lag of half that duration, and
-- reschedules itself after the duration. Here the scheduler is in the
-- graph: a per-voice sample countdown that, on expiry, latches a new
-- duration, lag time and frequency. CombN(0.1, [0.09, 0.08], 1) * 0.5 + dry.

fn wolfTones() S {
	let n = 4;
	let durs = [0.0625, 0.125, 0.5, 2.0] @ asSignal;
	let count = delayVar();
	let expired = count(1) <= 0;
	let newDur = select(irand(0, 3, n), durs);
	count <- select2(expired, newDur * fs(), count(1) - 1);
	let lagTime = (newDur * 0.5) sampleAndHold(expired);
	let freq = ((200 + urand(n) * 600) sampleAndHold(expired)) lag(lagTime);
	let voices = resonz(pink(n) * 4, freq, 0.002);
	let mix = (voices pan(birand(n, Rate.init)) join) transpose(n) sum(2);
	(0.5 * (mix comb([0.09, 0.08] vec, 0.1, 1.0, Interpolation.none)) + mix) gainOutlet
}

wolfTones defSynthX("wolfTones", tags) await;

---------------------------------------------------------------------------
-- 4. Landon Rose Klank spawn (examples-8.txt:106-133)
-- Spawn every 2 s; instance i uses spec i mod 5 (four modes each, ring 3 s)
-- excited by stereo PinkNoise * 0.001 under Env.sine(4): a 4 s half-sine
-- window. The two channels are independent banks: modes are listed twice,
-- interleaved, so the even/odd channels take pink L / pink R and sum(2)
-- folds them per side.

const kLrHold = 4.0;
const kLrInterval = 2.0;
const kLrVoices = 3;             -- ceil(life 4 / 2)

fn klankSpawnVoice() S {
	let g = gate();
	let idx = noteParam("spec", ControlSpec{0.0, 4.0, 0.0, ControlWarp.linear}) i32;
	let specs = [
		([32, 32, 43, 43, 54, 54, 89, 89] vec) nnhz,
		([10, 10, 34, 34, 80, 80, 120, 120] vec) nnhz,
		([67, 67, 88, 88, 90, 90, 100, 100] vec) nnhz,
		([14, 14, 23, 23, 34, 34, 45, 45] vec) nnhz,
		([76, 76, 88, 88, 99, 99, 124, 124] vec) nnhz
	];
	let freqs = select(idx, specs);
	let env = ((g rising) oneshot(4)) sinpi;
	(klankModes(pink(2) * 0.001, freqs, 1, 3.0) sum(2)) * env
}

fn klankSpawn() S = voicer(kLrVoices, klankSpawnVoice) sum(2) gainOutlet;

klankSpawn defSynthX("klankSpawn", tags) await;

coro fn klankSpawner(nodeID Int, totalDur Float) Float {
	var i = 0;
	var elapsed = 0.0;
	while (elapsed < totalDur) {
		let noteID = i % kLrVoices;
		let spec = i % 5;
		ae.begin(); ae.noteOn(nodeID, noteID, [spec toFloat, 1.0]); ae.sched(0);
		go(coro fn() Float {
			yield kLrHold;
			ae.begin(); ae.noteOff(nodeID, noteID); ae.sched(0);
		}());
		i = i + 1;
		elapsed = elapsed + kLrInterval;
		yield kLrInterval;
	}
}

---------------------------------------------------------------------------
-- 5. screen zones (examples-8.txt:137-161)
-- The screen is four quadrants, each running one process: pink noise (top
-- left), a 700 Hz sine (top right), a 100 Hz LFSaw (bottom left), a Formant
-- (bottom right), all tremolo'd by a 6 Hz sine 0.1..0.2. SC paused the
-- others; here all four run and the quadrant test gates them. Screen y is 0
-- at the top; mouseY is 0 at the bottom, hence the inversion.

fn screenZones() S {
	let x = mouseX();
	let y = 1 - mouseY();
	let am = (6 sinosc) * 0.05 + 0.15;
	let q1 = (x < 0.5) * (y < 0.5);
	let q2 = (x >= 0.5) * (y < 0.5);
	let q3 = (x < 0.5) * (y >= 0.5);
	let q4 = (x >= 0.5) * (y >= 0.5);
	let z1 = (pink() * am) * q1;
	let z2 = ((700 sinosc) * am) * q2;
	let z3 = ((100 lfsaw) * 0.5 * am) * q3;
	let z4 = (sc2Formant(21, 2100, 80) * am) * q4;
	(z1 + z2 + z3 + z4) gainOutlet
}

screenZones defSynthX("screenZones", tags) await;

---------------------------------------------------------------------------
-- 6. slight mod of "phase mod with slow beats" (examples-8.txt:165-191)
-- As examples-6 #13, but each instance's output is retriggered by an
-- Env.adsr(0.001, 0.01, 0.25, 0.04) on impulses whose rate XLines from
-- exprand(10, 40) to 3x or 0.3x of itself over 12 s. mx.poll = the script
-- samples MouseX at each spawn and sends it as a noteParam.

const kPmTrans = 4.0;
const kPmHold = 8.0;
const kPmInterval = 2.0;        -- 8 / 4
const kPmVoices = 6;            -- ceil(life 12 / 2)

fn phaseModAdsrVoice() S {
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
	let rate = exprand(10, 40, 1, Rate.reset);
	let factor = select(irand(0, 1, 1, Rate.reset), [3.0, 0.3] @ asSignal);
	let trig = lfimp(xline(rate, factor * rate, 12));
	let env = adsr(trig timedGate(0.011), 0.001, 0.01, 0.25, 0.04);
	(((f2 sinosc(a * (1.0 / twopi))) * 0.1) * env) * xenv(kPmTrans)
}

fn phaseModAdsr() S = voicer(kPmVoices, phaseModAdsrVoice) sum(2) gainOutlet;

phaseModAdsr defSynthX("phaseModAdsr", tags) await;

coro fn phaseModSpawner(nodeID Int, totalDur Float) Float {
	var id = 0;
	var elapsed = 0.0;
	while (elapsed < totalDur) {
		let m Float = ae.getSharedInput(0);
		let x = 100.0 * exp(m * log(60.0));
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
-- 7. using Sequencer to generate a chaotic waveform (examples-8.txt:195-214)
-- The logistic map x = a x (1 - x) stepped on each Impulse(MouseX 100..10000
-- exp), a = MouseY 3.4..3.99; minus its ~0.72 DC bias, lowpassed at the
-- clock frequency.

fn chaoticSequencer() S {
	let a = mouseY(3.4, 3.99);
	let f = mouseXExp(100, 10000);
	let trig = lfimp(f);
	let x = delayVar() init(1, urand(1, Rate.init));
	let x1 = x(1);
	let seqv = x <- select2(trig > 0, a * x1 * (1 - x1), x1);
	((seqv - 0.72) lpf(f)) gainOutlet
}

chaoticSequencer defSynthX("chaoticSequencer", tags) await;

---------------------------------------------------------------------------
-- driver

fn playAllSc8() {
	go(coro fn() Float {
		"start playing SC2 examples-8" println;
		ae.setSharedInput(0, 0.5);
		ae.setSharedInput(1, 0.5);

		"modalSpace" playFor(10.0) yieldAll;
		"algoRhythms" playFor(12.0) yieldAll;
		"wolfTones" playFor(12.0) yieldAll;

		"-- klankSpawn --" println;
		let lrNode = "klankSpawn" play;
		klankSpawner(lrNode, 12.0) yieldAll;
		yield kLrHold + 0.5;
		lrNode stop;

		"screenZones" playFor(8.0) yieldAll;

		"-- phaseModAdsr --" println;
		let pmNode = "phaseModAdsr" play;
		phaseModSpawner(pmNode, 10.0) yieldAll;
		yield kPmHold + kPmTrans;
		pmNode stop;

		"chaoticSequencer" playFor(8.0) yieldAll;

		ae.endRender(0.1);
		"done playing SC2 examples-8" println;
	}());
}

fn renderSc8() {
	let h = "/tmp/sc2_examples_8.wav" ae.renderNRT(160, playAllSc8);
	await ae.renderDone(h);
	"render done" println;
}

--renderSc8();
--playAllSc8();
