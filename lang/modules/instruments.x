-- instruments.x -- a library of note-playing instrument synthdefs.
--
-- Two layers, like effects.x:
--
--   1. Ugen layer: composable voice-building functions (Karplus-Strong
--      string, ringing-filter bank, sample-bank playback helpers, standard
--      note parameters) building on synthdef, common_ugens, and filters.
--
--   2. Synthdef layer: pure graph functions, each wrapping a polyphonic
--      voicer around one standard synthesis model:
--          smpPerc     percussive sample player (one shot, no looping)
--          smpLoopTail sample player: sustain loop, then the sample's own
--                      decay segment (the audio after the loop) on release
--          smpLoopEnv  sample player: loops through sustain and release,
--                      envelope flat until the release decay
--          wtLead      band-limited wavetable oscillator with filter and
--                      amplitude envelopes
--          resonBank   ringing-filter bank excited by enveloped noise
--          ksPluck     Karplus-Strong string excited by a noise pluck
--      They have no side effects. Each is a factory taking maxVoices
--      (default kMaxVoices = 8) and returning the graph function, so a
--      def can be compiled with more or fewer voices:
--          import instruments.*;
--          ksPluck() defSynthX("ksPluck");
--          ksPluck(16) defSynthX("ksPluck16");
--      or import examples/note_synthdefs.x which registers them all at
--      the default voice count.
--
-- Playing notes. Each def is a voicer: trigger voices with
--     noteOn(nodeID, noteID, [params...]);
--     noteOff(nodeID, noteID);
-- Note params are kept to what genuinely varies per note -- pitch and
-- velocity (sample players) or freq and amp (synthesis models); params
-- omitted from the array take their spec defaults, and `gate` is implicit
-- and always last. Everything panel-like -- envelope segments, cutoff,
-- resonance, damping -- is a regular control, as on a physical synth:
-- node-level, shared by all voices, set with
--     setControl(nodeID, controlIndex, value);
-- (controls are indexed in declaration order, documented per def below).
--
-- Per-node data. The sample players resolve notes against sample bank 0 --
-- load one per node before playing:
--     newNode("smpPerc", n);
--     loadSampleBank(n, 0, [sampleZone("kick.wav", 0, 127, 0, 127, 36, -1.0, -1.0), ...]);
-- wtLead reads wavetable bank 0 (a wavetables.oscTables-format bank using
-- the default 16384-sample tables, 30 per bank) -- fill it per node:
--     newNode("wtLead", n);
--     fillBuffer(n, 0, 1, sawTables(1.0));

import synthdef.*;
import common_ugens.*;
import filters.*;

---------------------------------------------------------------------------
-- Shared helpers

const kMaxVoices = 8;

fn _lin(lo Float, hi Float, init Float) ControlSpec =
	ControlSpec { lo: lo, hi: hi, init: init, warp: ControlWarp.linear };
fn _exp(lo Float, hi Float, init Float) ControlSpec =
	ControlSpec { lo: lo, hi: hi, init: init, warp: ControlWarp.exponential };

-- Standard note parameters. Frequency-driven models declare freq/amp;
-- the sample players declare pitch/vel (MIDI-style, as the bank lookup
-- maps pitch and velocity zones).
fn noteFreq() S = noteParam("freq", _exp(20.0, 12000.0, 440.0));
fn noteAmp() S = noteParam("amp", _lin(0.0, 1.0, 0.5));
fn notePitch() S = noteParam("pitch", _lin(0.0, 127.0, 60.0));
fn noteVel() S = noteParam("vel", _lin(0.0, 127.0, 100.0));

-- MIDI velocity (0..127) to amplitude: normalized and squared for a
-- roughly perceptual taper.
fn velAmp(vel AsSignal) S = (vel asSignal * (1.0 / 127.0)) sq;

---------------------------------------------------------------------------
-- Sample-bank playback helpers

-- Playback rate for the resolved sample, in source frames per output
-- sample: semitone shift from the sample's root key, corrected for the
-- source file's sample rate. All inputs are reset-rate, so the whole
-- expression folds to one value latched at note-on.
fn bankRate(h BankSample, pitch AsSignal) S =
	((pitch asSignal f64 - (h rootKey) f64) * (1.0 / 12.0)) exp2 * (h sampleRate) * (T() f64);

-- Interpolated stereo read of the resolved sample, silenced once the
-- playhead runs off the end (mono samples duplicate to both channels).
fn bankRead(h BankSample, pos S, chans Int = 2) S =
	h vread(pos, Interpolation.cubic, chans) f32 * ((pos < (h length)) f32);

---------------------------------------------------------------------------
-- Karplus-Strong string

-- Plucked-string loop: a delay line tuned to freq with a one-pole damping
-- filter and a loss factor in the feedback path. Feed any excitation in
-- (a noise burst for a pluck, sustained noise for a bowed scrape).
-- decayTime is the -60 dB ring time in seconds; damp (0..0.95) rolls off
-- highs in the loop, darkening the string and shortening treble decay.
-- The one-pole is non-resonant with unity DC gain, so the loop gain never
-- exceeds `loss` and the string cannot blow up. freq is clamped at 20 Hz
-- to keep the tap inside the buffer for inactive voices (voicer note
-- params read 0 there, and fs()/0 would NaN-propagate under -ffast-math).
fn ksString(exc S, freq AsSignal, decayTime AsSignal = 3.0, damp AsSignal = 0.35) S {
	let f = freq asSignal max(20.0);
	let buf = delayVar(0.05 * fs());
	let tap = buf vread(fs() / f);
	let loss = (f * decayTime asSignal) decay60dB;
	buf <- exc + tap onepole(damp) * loss;
	tap
}

---------------------------------------------------------------------------
-- Ringing-filter bank

-- Harmonic mode data: partial ratios, amplitudes, and per-mode ring-time
-- scale (higher partials die faster, like a plucked or struck string).
let kRingRatios = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0];
let kRingAmps   = [1.0, 0.62, 0.45, 0.35, 0.28, 0.22, 0.18, 0.15];
let kRingTimes  = [1.0, 0.78, 0.62, 0.50, 0.42, 0.36, 0.31, 0.27];

-- Bank of ringing filters (the `ring` ugen) at harmonics of freq, ringing
-- at the excitation. decayTime is the fundamental's -60 dB time in
-- seconds. A resonator's peak gain grows with its ring time, and the
-- power it collects from broadband excitation grows with ringTime * fs,
-- so the excitation is normalized by 1/sqrt(decayTime * fs()) to keep
-- output level roughly independent of the decay setting. Modes that would
-- land above Nyquist are muted. decayTime is clamped at 50 ms: it divides
-- into both the normalization and the ring coefficient, so a zero (which
-- inactive voicer voices read for every note param, should a caller wire
-- one in) would NaN-propagate through the whole voice sum under
-- -ffast-math.
fn ringBank(exc S, freq AsSignal, decayTime AsSignal = 2.5) S {
	let t = decayTime asSignal max(0.05);
	let f = freq asSignal * kRingRatios vec;
	let lim = 0.45 * fs();
	let audible = f < lim;
	let norm = 3.0 / (t * fs()) sqrt;
	((exc * norm) ring(f min(lim), t * kRingTimes vec) * kRingAmps vec * audible) sum
}

---------------------------------------------------------------------------
---------------------------------------------------------------------------
-- Synthdef layer: ready-made note-playing defs.
--
-- Each def is a factory: call it (optionally with a voice count) to get
-- the pure graph function, and compile that with defSynthX. The sample
-- players are stereo out; the synthesis models are mono out. Controls are
-- declared in the graph function (outside the voice body) and read by the
-- voices as free variables.

-- 1. Percussive sample player: the resolved sample plays once from its
--    start at the pitch-shifted rate and stops at its end. Note-off is
--    ignored (gate is still declared -- it is the voicer's implicit
--    column-0 parameter -- but a one-shot lets the sample ring out).
--    noteOn params: [pitch, vel]    controls: none
fn smpPerc(maxVoices Int = kMaxVoices) () S {
	fn() S {
		let bank = sampleBankVar();
		voicer(maxVoices, fn() S {
			let pitch = notePitch();
			let vel = noteVel();
			let g = gate();
			let h = bank lookup(pitch, vel);
			let pos = h loopPhasor(bankRate(h, pitch), 0.0);
			h bankRead(pos) * vel velAmp
		}) sum(2) outlet
	}
}

-- 2. Sustain-loop sample player with the decay recorded in the sample:
--    loops over the sample's sustain loop while the note is held; on
--    release the playhead runs on past the loop through the sample's own
--    decay segment to the end. The sample provides its attack and decay,
--    so there is no synthetic envelope.
--    noteOn params: [pitch, vel]    controls: none
fn smpLoopTail(maxVoices Int = kMaxVoices) () S {
	fn() S {
		let bank = sampleBankVar();
		voicer(maxVoices, fn() S {
			let pitch = notePitch();
			let vel = noteVel();
			let g = gate();
			let h = bank lookup(pitch, vel);
			let pos = h loopPhasor(bankRate(h, pitch), g);
			h bankRead(pos) * vel velAmp
		}) sum(2) outlet
	}
}

-- 3. Loop-through-decay sample player: keeps looping the sustain loop
--    through both sustain and release; a two-segment sustain-release
--    envelope -- 1.0 from time zero while the note is held (the sample
--    carries its own attack), exponential decay over `release` seconds
--    after note-off -- shapes the tail.
--    noteOn params: [pitch, vel]    controls: 0 release
fn smpLoopEnv(maxVoices Int = kMaxVoices) () S {
	fn() S {
		let bank = sampleBankVar();
		let rel = control("release", _exp(0.02, 8.0, 0.4));
		voicer(maxVoices, fn() S {
			let pitch = notePitch();
			let vel = noteVel();
			let g = gate();
			let h = bank lookup(pitch, vel);
			let pos = h loopPhasor(bankRate(h, pitch), 1.0);
			let env = g susrel(1.0, rel);
			h bankRead(pos) * env * vel velAmp
		}) sum(2) outlet
	}
}

-- 4. Wavetable lead: band-limited wavetable oscillator (the 1/3-octave
--    `osc` family; fill buffer 0 with a wavetables.* bank) through a
--    resonant lowpass. Separate ADSR envelopes for amplitude and filter;
--    the filter envelope sweeps the cutoff up to `envOct` octaves above
--    its base. res is 1/Q -- smaller is more resonant.
--    noteOn params: [freq, amp]
--    controls: 0 attack, 1 decay, 2 sustain, 3 release, 4 cutoff,
--              5 envOct, 6 res, 7 fattack, 8 fdecay, 9 fsustain
fn wtLead(maxVoices Int = kMaxVoices) () S {
	fn() S {
		let b = bufferVar();
		let atk = control("attack", _exp(0.001, 2.0, 0.005));
		let dcy = control("decay", _exp(0.01, 4.0, 0.2));
		let sus = control("sustain", _lin(0.0, 1.0, 0.7));
		let rel = control("release", _exp(0.01, 8.0, 0.3));
		let cut = control("cutoff", _exp(40.0, 12000.0, 800.0));
		let envOct = control("envOct", _lin(0.0, 8.0, 3.0));
		let res = control("res", _lin(0.05, 2.0, 0.6));
		let fatk = control("fattack", _exp(0.001, 2.0, 0.01));
		let fdcy = control("fdecay", _exp(0.01, 4.0, 0.25));
		let fsus = control("fsustain", _lin(0.0, 1.0, 0.3));
		voicer(maxVoices, fn() S {
			let f = noteFreq();
			let a = noteAmp();
			let g = gate();
			let env = g adsr(atk, dcy, sus, rel);
			let fenv = g adsr(fatk, fdcy, fsus, rel);
			let cutoff = (cut * (envOct * fenv) exp2) min(0.45 * fs());
			b osc(f) rlpf(cutoff, res) * env * a * 0.35
		}) sum outlet
	}
}

-- 5. Resonant filter bank: harmonic ringBank excited by gated noise.
--    Holding the note sustains the excitation (a bowed, breathy tone);
--    releasing it lets the modes ring out over `decay` seconds.
--    noteOn params: [freq, amp]
--    controls: 0 attack, 1 release, 2 decay
fn resonBank(maxVoices Int = kMaxVoices) () S {
	fn() S {
		let atk = control("attack", _exp(0.001, 1.0, 0.01));
		let rel = control("release", _exp(0.005, 2.0, 0.05));
		let dcy = control("decay", _exp(0.1, 12.0, 2.5));
		voicer(maxVoices, fn() S {
			let f = noteFreq();
			let a = noteAmp();
			let g = gate();
			let env = g asr(atk, 1.0, rel);
			(white() * env) ringBank(f, dcy) * a * 0.7
		}) sum outlet
	}
}

-- 6. Karplus-Strong pluck: ksString excited by a noise burst with an
--    exponential decay envelope of `pluck` seconds. decay is the string's
--    ring time; damp darkens it. Note-off is ignored (the string rings).
--    noteOn params: [freq, amp]
--    controls: 0 pluck, 1 decay, 2 damp
fn ksPluck(maxVoices Int = kMaxVoices) () S {
	fn() S {
		let pluckT = control("pluck", _exp(0.001, 0.1, 0.008));
		let dcy = control("decay", _exp(0.1, 12.0, 3.0));
		let damp = control("damp", _lin(0.0, 0.95, 0.35));
		voicer(maxVoices, fn() S {
			let f = noteFreq();
			let a = noteAmp();
			let g = gate();
			let exc = white() * g tr decay2(0.0005, pluckT);
			exc ksString(f, dcy, damp) * a * 0.5
		}) sum outlet
	}
}
