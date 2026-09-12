-- Tzopilotl translations of SuperCollider examples from
-- sc-examples/SC2-examples/examples-7.txt. Shared machinery lives in
-- sc2_common.x; sc2_examples_1.x explains the texture timing model.
--
-- Status (examples-7.txt blocks, in order):
--   1  interaural time delay panning -- DONE (itdPanning) -- griot strings,
--                                       ITD pan via two linear delays
--   2  aleatoric quartet             -- DONE (aleatoricQuartet)
--   3  ritual hymn (Klang beating)   -- DONE (ritualHymn)
--   4  the church of chance          -- DONE (churchOfChance)
--   5  analog daze, changing pattern -- DONE (analogDazeVariant) -- the
--                                       Ref'd pattern is 8 shared-input slots
--                                       rewritten by a script every 8 s
--   6  Berlin 1977 + bass            -- DONE (berlinBass)
--   7  Griot duet (2 x 10 strings)   -- DONE (griotDuet)
--   8  tapping tools                 -- DONE (tappingTools)
-- Mouse-driven parameters read the shared-input mouse; the driver parks it
-- mid-screen for headless renders.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import filters.*;
import audio_engine as ae;
import clock.*;
import sc2_common.*;

let tags = ["sc2-examples-7"];

fn xenv(trans Float) S = linen(gate(), trans, trans) rpanfun;

---------------------------------------------------------------------------
-- Local helpers

-- DelayL(in, maxDelay, delayTime): linearly interpolated variable delay.
fn delayl(x S, delayTime AsSignal, maxDelayTime Float) S {
	let y = delayVar(maxDelayTime * fs() + 8);
	y <- x;
	y(delayTime * fs(), Interpolation.linear)
}

-- Griot-style string network: an n-line delayVar tapped at `tapSamples`,
-- each tap lowpassed at `damp` and fed back at 0.98 plus the excitation.
-- Returns the filtered taps (the caller mixes them).
fn stringNetwork(exc S, tapSamples S, maxDelaySec Float, damp AsSignal) S {
	let d = delayVar(maxDelaySec * fs());
	let tapped = d(tapSamples, Interpolation.none);
	let filt = (tapped lpf(damp)) * 0.98;
	d <- filt + exc;
	filt
}

-- A 12-note melody of string numbers: a random walk (probability `p` of
-- moving by -1/0/+1) wrapped to 0..n-1.
fn griotMelody(p Float, n Float) [Float] {
	var chan = 2.0;
	var m [Float] = [];
	for (i : (1..12)) {
		let c Float = urand();
		if (c < p) {
			let u Float = urand();
			if (u < 0.3333) { chan = chan - 1.0; }
			if (u > 0.6667) { chan = chan + 1.0; }
			if (chan < 0.0) { chan = chan + n; }
			if (chan > n - 1.0) { chan = chan - n; }
		}
		m push!(chan);
	}
	m
}

-- Spawns one string pluck every `interval` seconds on `numVoices` exciter
-- voices (noteParams: string index, amplitude), regenerating the melody
-- every `regen` notes.
coro fn griotSpawner(nodeID Int, numVoices Int, interval Float, totalDur Float,
                     p Float, n Float, regen Int) Float {
	var melody = griotMelody(p, n);
	var i = 0;
	var elapsed = 0.0;
	while (elapsed < totalDur) {
		if (i > 0 && i % regen == 0) { melody = griotMelody(p, n); }
		let noteID = i % numVoices;
		let amp Float = urand() * 0.1;
		ae.begin(); ae.noteOn(nodeID, noteID, [melody[i % 12], amp, 1.0]); ae.sched(0);
		go(coro fn() Float {
			yield 0.01;
			ae.begin(); ae.noteOff(nodeID, noteID); ae.sched(0);
		}());
		i = i + 1;
		elapsed = elapsed + interval;
		yield interval;
	}
}

fn pString(hi Float) S = noteParam("string", ControlSpec{0.0, hi, 2.0, ControlWarp.linear});
fn pAmp() S = noteParam("amp", ControlSpec{0.0, 0.1, 0.05, ControlWarp.linear});

---------------------------------------------------------------------------
-- 1. interaural time delay panning (examples-7.txt:16-77)
-- The Griot string model (five 10 ms lines tapped at 3..5 ms, LPF 2000 fixed,
-- 0.98 feedback; plucks LFNoise2(MouseY 10..10000) * Env([0,1,0],[0.01,0.2],-2)
-- into one string chosen by a 12-note random walk, coin 0.4, every 0.1 s),
-- mixed to mono and panned ONLY by interaural delay: left delayed by MouseX
-- 0..2 ms, right by 2..0 ms.

const kItdVoices = 3;

fn itdVoice() S {
	let g = gate();
	let string = pString(4.0);
	let amp = pAmp();
	let burst = percEnv(g rising, 0.01, 0.2, -2.0) * (mouseY(10, 10000) lfnoise3(1)) * amp;
	burst * (([0, 1, 2, 3, 4] vec) == string)
}

fn itdPanning() S {
	let exc = voicer(kItdVoices, itdVoice) sum(5);
	let taps = ([0.003, 0.0035, 0.004, 0.0045, 0.005] vec) * fs();
	let out = stringNetwork(exc, taps, 0.01, 2000) sum;
	let left = out delayl(mouseX(0, 0.002), 0.002);
	let right = out delayl(mouseX(0.002, 0), 0.002);
	([left, right] join) gainOutlet
}

itdPanning defSynthX("itdPanning", tags) await;

---------------------------------------------------------------------------
-- 2. aleatoric quartet (examples-7.txt:81-123)
-- Four comb-string instruments excited by PinkNoise gated by max(0,
-- LFNoise1(8) * dmul + dadd) -- MouseX (0.01..1) sets the density: dmul =
-- 0.5 amp / density, dadd = amp - dmul. Pitch: LFNoise0 at 1, 0.5 or 0.25 Hz
-- (+/- 7 semitones around 66 +/- 30), rounded to semitones, lagged 0.2 s;
-- CombL(exc, 0.02, 1/freq, 3); panned; five stereo AllpassN; LeakDC.

fn aleatoricQuartet() S {
	let n = 4;
	let amp = 0.07;
	let density = mouseX(0.01, 1);
	let dmul = (1 / density) * 0.5 * amp;
	let dadd = amp - dmul;
	let gateSig = max(0, ((8 asSignal) lfnoise1(n)) * dmul + dadd);
	let exc = pink(n) * gateSig;
	let rate = select(irand(0, 2, n, Rate.init), [1.0, 0.5, 0.25] @ asSignal);
	let center = 66 + birand(n, Rate.init) * 30;
	let note = ((rate lfnoise0(n)) * 7 + center) round(1);
	let freq = (note lag(0.2)) nnhz;
	let strings = exc combl(1 / freq, 0.02, 3);
	var sig = (strings pan(birand(n, Rate.init)) join) transpose(n) sum(2);
	for (i : (1..5)) {
		sig = sig alpasn(urand(2, Rate.init) * 0.05, 1);
	}
	(sig leakdc(0.995)) gainOutlet
}

aleatoricQuartet defSynthX("aleatoricQuartet", tags) await;

---------------------------------------------------------------------------
-- 3. ritual hymn in praise of the god of the LS-3000 (examples-7.txt:127-156)
-- XFadeTexture(.., 4, 4, 2): per channel 3n = 60 sines -- n random notes
-- (24 + rand 60), each with two partners detuned by +/- 5 Hz (channel 2 uses
-- the same notes, all three detuned), random phases, amplitude 0.1/n.
-- Layout: the 20 base notes cycle three times across 60 channels (a sum
-- doesn't care about order); mask keeps the first copy of each exact.

const kXf44Trans = 4.0;
const kXf44Hold = 8.0;
const kXf44Interval = 8.0;
const kXfVoices = 2;

fn firstCopyMask(n Int, copies Int) S {
	var m [Float] = [];
	for (i : (1..(n * copies))) { m push!(i <= n ? 0.0 : 1.0); }
	m vec
}

fn ritualHymnVoice() S {
	let n = 20;
	let d = 5.0;
	let base = (24 + urand(n, Rate.reset) * 60) nnhz;
	let p = (base ncyc(3)) + birand(3 * n, Rate.reset) * d * firstCopyMask(n, 3);
	let q = (base ncyc(3)) + birand(3 * n, Rate.reset) * d;
	let freqs = [p, q] join;
	let phases = urand(6 * n, Rate.reset);
	(((freqs fsinxosc(phases)) * (0.1 / n)) transpose(3 * n) sum(2)) * xenv(kXf44Trans)
}

fn ritualHymn() S = voicer(kXfVoices, ritualHymnVoice) sum(2) gainOutlet;

ritualHymn defSynthX("ritualHymn", tags) await;

---------------------------------------------------------------------------
-- 4. the church of chance (examples-7.txt:160-196)
-- XFadeTexture(.., 6, 3, 2): n = 8 notes from a major hexatonic scale
-- (0,2,4,5,7,9) + rand 7 octaves + k (24 + rand 12), each with harmonics
-- 1, 2, 4, 5, 6 detuned +/- 0.4 Hz; channel 2 the same notes redetuned;
-- random phases; amplitude 0.1/n. The 8 x 5 grid is the 8 notes cycled
-- five times times the 5 harmonics stuttered eight times.

const kXf63Trans = 3.0;
const kXf63Hold = 9.0;
const kXf63Interval = 9.0;

fn churchVoice() S {
	let n = 8;
	let m = 5;
	let d = 0.4;
	let k = 24 + urand(1, Rate.reset) * 12;
	let scale = [0.0, 2.0, 4.0, 5.0, 7.0, 9.0] @ asSignal;
	let notes = select(irand(0, 5, n, Rate.reset), scale) + (irand(0, 6, n, Rate.reset) f32) * 12 + k;
	let base = (notes nnhz) ncyc(m);
	let harms = ([1.0, 2.0, 4.0, 5.0, 6.0] vec) stutter(n);
	let core = base * harms;
	let p = core + birand(m * n, Rate.reset) * d;
	let q = core + birand(m * n, Rate.reset) * d;
	let freqs = [p, q] join;
	let phases = urand(2 * m * n, Rate.reset);
	(((freqs fsinxosc(phases)) * (0.1 / n)) transpose(m * n) sum(2)) * xenv(kXf63Trans)
}

fn churchOfChance() S = voicer(kXfVoices, churchVoice) sum(2) gainOutlet;

churchOfChance defSynthX("churchOfChance", tags) await;

---------------------------------------------------------------------------
-- 5. a variant of "analog daze" that changes the pattern (examples-7.txt:200-242)
-- As examples-3 #1 (two VCO->VCF voices + snare + reversed echo), but the
-- 8-step pattern is a Ref rewritten every 8 s by a Synth task: a base note
-- 42 + rand 12, a 5-note pitch set above it, and a figuration of 8 picks.
-- Here the pattern lives in shared-input slots 3..10, which the synth reads
-- as an 8-channel vector and `patternTask` rewrites every 8 s.

const kDazeSlot = 3;

fn dazePattern() S {
	var slots [S] = [];
	for (i : (0..7)) { slots push!(sharedIn(kDazeSlot + i)); }
	slots join
}

fn dazeVoice(octave Int, clockRate Float, pwmRate Float, fltRate Float) S {
	let freq = ((seq(lfimp(clockRate), dazePattern(), 8) + 12 * octave) nnhz) lag(0.05);
	let width = (pwmRate sinosc(urand(1, Rate.init) * twopi)) * 0.4 + 0.5;
	let cutoff = (fltRate sinosc(urand(1, Rate.init) * twopi)) * 1700 + 2000;
	((freq lfupulse(width)) * 0.1) rlpf(cutoff, 1.0 / 15.0)
}

fn analogDazeVariant() S {
	let snareRate = ((0.3 asSignal) lfnoise1(1)) * 6000 + 8000;
	let snare = (lfimp(2) decay(0.15)) * ((snareRate lfnoise0(2)) * 0.07);
	let g = ([dazeVoice(1, 8.0, 0.31, 0.2), dazeVoice(0, 2.0, 0.13, 0.11)] join) + snare;
	(0.4 * ((g combn(0.375, 4)) + (g reverse))) gainOutlet
}

analogDazeVariant defSynthX("analogDazeVariant", tags) await;

-- Rewrites the 8 pattern slots every 8 s for `totalDur` seconds.
coro fn patternTask(totalDur Float) Float {
	var elapsed = 0.0;
	while (elapsed < totalDur) {
		let b Float = urand();
		let basenote = 42.0 + (b * 12.0) floor;
		var pitchset [Float] = [];
		for (i : (1..5)) {
			let u Float = urand();
			pitchset push!(basenote + (u * 12.0) floor);
		}
		for (i : (0..7)) {
			let u Float = urand();
			let idx = ((u * 5.0) floor) toInt;
			ae.setSharedInput(kDazeSlot + i, pitchset[idx]);
		}
		elapsed = elapsed + 8.0;
		yield 8.0;
	}
}

---------------------------------------------------------------------------
-- 6. Berlin 1977 + bass (examples-7.txt:246-306)
-- Examples-4's Berlin with a second, bass voice: a note chosen from
-- [48, 43, 36] plus the current transposition, retriggered by a second
-- 16-beat divider offset by one beat, with its own Decay2 (16x slower) and
-- an RLPF whose cutoff is freq2 * (SinOsc 0.21 Hz * 4 + 8), 1/Q 0.07. MouseY
-- 0..1 is the echo send (CombN(0.2, [0.2, 0.17], 2) mixed with the dry).

fn berlinBass() S {
	let clockRate = mouseX(5, 20);
	let clockTime = 1 / clockRate;
	let clock = lfimp(clockRate);
	let count = clock trCount;
	let clock16 = clock * ((count % 16) == 1);
	let clock16b = clock * ((count % 16) == 0);
	let pattern = [55, 60, 63, 62, 60, 67, 63, 58] vec;
	let note = seq(clock, pattern, 8);
	let transpose = (([-12, -7, -5, 0, 2, 5] vec) at(irand(0, 5, 1))) sampleAndHold(clock16);
	let note2 = ((([48, 43, 36] vec) at(irand(0, 2, 1))) + transpose) sampleAndHold(clock16b);
	let freq = (note + transpose) nnhz;
	let freq2 = note2 nnhz;
	let amp = (clock decay2(0.05 * clockTime, 2 * clockTime)) * 0.1 + 0.02;
	let amp16 = (clock16b decay2(16 * 0.05 * clockTime, 16 * 2 * clockTime)) * 0.08 + 0.02;
	let filt = (clock decay2(0.05 * clockTime, 2 * clockTime)) * ((0.17 sinosc) * 800) + 1400;
	let width1 = ((0.08 asSignal) sinosc([0.0, 0.25] vec)) * 0.45 + 0.5;
	let width2 = ((0.12 asSignal) sinosc([0.0, 0.25] vec)) * 0.48 + 0.5;
	let lead = ((freq lfupulse(width1)) * amp) rlpf(filt, 0.15);
	let bass = ((freq2 lfupulse(width2)) * amp16) rlpf(freq2 * ((0.21 sinosc) * 4 + 8), 0.07);
	let sig = lead + bass;
	let wet = (sig * mouseY(0, 1)) comb([0.2, 0.17] vec, 0.2, 2, Interpolation.none);
	(wet + sig) gainOutlet
}

berlinBass defSynthX("berlinBass", tags) await;

---------------------------------------------------------------------------
-- 7. yet another "Griot modeling" variant: a duet (examples-7.txt:310-376)
-- Two instruments, the second playing half time (0.2 s) with its melody
-- regenerated twice as often (every 72 notes); each has 10 strings (taps
-- 1.5..6 ms) modelled twice, as stereo pairs -- a pluck is a stereo pair of
-- independent LFNoise2 bursts into lines 2m and 2m+1. Coin 0.6 on the walk.
-- Each network's 20 taps mix down to stereo by pairs (sum(2)).

const kDuetVoices = 3;

fn duetVoice() S {
	let g = gate();
	let string = pString(9.0);
	let amp = pAmp();
	let burst = percEnv(g rising, 0.01, 0.2, -2.0) * (mouseY(10, 10000) lfnoise3(2)) * amp;
	let pairIdx = [0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9] vec;
	burst * (pairIdx == string)
}

fn duetInstrument() S {
	let exc = voicer(kDuetVoices, duetVoice) sum(20);
	let taps = (([0.0015, 0.002, 0.0025, 0.003, 0.0035, 0.004, 0.0045, 0.005, 0.0055, 0.006] vec) stutter(2)) * fs();
	stringNetwork(exc, taps, 0.01, mouseX(10, 10000)) sum(2)
}

-- One instrument per node: the driver plays two nodes of this def, each
-- with its own spawner (noteOn addresses a node's single voicer).
fn griotDuet() S = duetInstrument() gainOutlet;

griotDuet defSynthX("griotDuet", tags) await;

---------------------------------------------------------------------------
-- 8. tapping tools (examples-7.txt:380-413)
-- Spawn(.., 2, 1): one object per second under Env.linen(1, 4, 1); each a
-- 4-mode Klank (400 + rand 8000, ring 0.01 + rand 0.1) excited by
-- Decay(Impulse((1 + linrand 20) * rate) * 0.03, 0.001) where `rate` is one
-- global XLine 64 -> 0.125 Hz over 60 s, panned; three stereo AllpassN(2 s).

const kTapVoices = 6;            -- ceil(life 6 / interval 1)
const kTapHold = 5.0;            -- rise 1 + sustain 4
const kTapFall = 1.0;

fn tappingTools() S {
	let rate = xline(64, 0.125, 60);
	let voice = fn() S {
		let g = gate();
		let freqs = 400 + urand(4, Rate.reset) * 8000;
		let rings = 0.01 + urand(4, Rate.reset) * 0.1;
		let impRate = linrandv(1, 21, 1, Rate.reset) * rate;
		let exc = (lfimp(impRate) * 0.03) decay(0.001);
		(klank(exc, freqs, 1, rings) * linen(g, 1, 1)) pan(birand(1, Rate.reset)) join
	};
	var sound = voicer(kTapVoices, voice) sum(2);
	for (i : (1..3)) {
		sound = sound alpasn(urand(2, Rate.init) * 0.05, 2);
	}
	sound gainOutlet
}

tappingTools defSynthX("tappingTools", tags) await;

---------------------------------------------------------------------------
-- driver

fn playAllSc7() {
	go(coro fn() Float {
		"start playing SC2 examples-7" println;
		ae.setSharedInput(0, 0.5);
		ae.setSharedInput(1, 0.5);

		"-- itdPanning --" println;
		let itdNode = "itdPanning" play;
		griotSpawner(itdNode, kItdVoices, 0.1, 16.0, 0.4, 5.0, 144) yieldAll;
		yield 0.5;
		itdNode stop;

		"aleatoricQuartet" playFor(16.0) yieldAll;
		"ritualHymn" texture(kXfVoices, kXf44Interval, kXf44Hold, 16.0, kXf44Trans) yieldAll;
		"churchOfChance" texture(kXfVoices, kXf63Interval, kXf63Hold, 18.0, kXf63Trans) yieldAll;

		"-- analogDazeVariant --" println;
		go(patternTask(24.0));
		"analogDazeVariant" playFor(24.0) yieldAll;

		"berlinBass" playFor(20.0) yieldAll;

		"-- griotDuet --" println;
		let duetA = "griotDuet" play;
		let duetB = "griotDuet" play;
		go(griotSpawner(duetA, kDuetVoices, 0.1, 20.0, 0.6, 10.0, 144));
		griotSpawner(duetB, kDuetVoices, 0.2, 20.0, 0.6, 10.0, 72) yieldAll;
		yield 0.5;
		duetA stop;
		duetB stop;

		"tappingTools" texture(kTapVoices, 1.0, kTapHold, 20.0, kTapFall) yieldAll;

		ae.endRender(0.1);
		"done playing SC2 examples-7" println;
	}());
}

fn renderSc7() {
	let h = "/tmp/sc2_examples_7.wav" ae.renderNRT(240, playAllSc7);
	await ae.renderDone(h);
	"render done" println;
}

--renderSc7();
--playAllSc7();
