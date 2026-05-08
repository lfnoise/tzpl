-- Voicer-based instrument synthdefs.
--
-- Each synthdef wraps a polyphonic voicer whose body declares per-note
-- parameters ("freq", "amp") and reads `gate()`. The voices are summed
-- to produce the synth's mono output.
--
-- Trigger a voice from Tzopilotl with the audio_engine bridge:
--     noteOn(nodeID, noteID, [freq, amp, gate]) ;
--     noteOff(nodeID, noteID) ;
-- (parameter order matches the order in which noteParam is declared in
-- the voice body; `gate` is implicit and always last).

import synthdef.*;
import common_ugens.*;
import filters.*;

---------------------------------------------------------------------------
-- Helpers shared across voicers

const kMaxVoices = 8;

-- Standard pitch parameter (exponential warp).
fn pNoteFreq() S =
	noteParam("freq", ControlSpec { lo: 20.0, hi: 12000.0, init: 440.0, warp: ControlWarp.exponential });

-- Standard amplitude parameter (linear warp).
fn pNoteAmp() S =
	noteParam("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });

---------------------------------------------------------------------------
-- 1. Additive organ.
--    Sum of harmonic sines with a drawbar-like spectrum and ADSR.

fn organVoice() S {
	let f = pNoteFreq();
	let a = pNoteAmp();
	let g = gate();

	let env = adsr(g, 0.02, 0.1, 0.9, 0.25);

	let harms = [1, 2, 3, 4, 5, 6, 8] vec;
	let amps  = [1.0, 0.7, 0.5, 0.4, 0.25, 0.2, 0.15] vec;
	let partials = (f * harms) sinosc * amps;

	partials sum * env * a * 0.18
}

fn organ() S = voicer(kMaxVoices, organVoice) sum outlet;

organ defSynth("organ");

---------------------------------------------------------------------------
-- 2. FM bell.
--    Carrier sine plus a high-ratio modulator with a fast-decaying index.

fn fmBellVoice() S {
	let f = pNoteFreq();
	let a = pNoteAmp();
	let g = gate();

	-- Note-on triggers a tone that always rings out.
	let amp = decay2(g, 0.005, 2.5);
	let idx = decay2(g, 0.005, 0.6) * 6.0;

	let modulator = (f * 3.5) sinosc * idx;
	let car = f sinosc(modulator);

	car * amp * a * 0.4
}

fn fmBell() S = voicer(kMaxVoices, fmBellVoice) sum outlet;

fmBell defSynth("fmBell");

---------------------------------------------------------------------------
-- 3. FM brass.
--    Sustained 1:1 ratio FM with index that follows the envelope.

fn fmBrassVoice() S {
	let f = pNoteFreq();
	let a = pNoteAmp();
	let g = gate();

	let env = adsr(g, 0.05, 0.15, 0.8, 0.25);
	let idx = env * 4.0;

	let modulator = f sinosc * idx;
	let car = f sinosc(modulator);

	car * env * a * 0.3
}

fn fmBrass() S = voicer(kMaxVoices, fmBrassVoice) sum outlet;

fmBrass defSynth("fmBrass");

---------------------------------------------------------------------------
-- 4. Subtractive saw lead.
--    Two detuned saws through a resonant lowpass with envelope-modulated cutoff.

fn sawLeadVoice() S {
	let f = pNoteFreq();
	let a = pNoteAmp();
	let g = gate();

	let env = adsr(g, 0.01, 0.2, 0.7, 0.3);

	-- Two saws detuned by ~5 cents in opposite directions.
	let detune = [0.997, 1.003] vec;
	let saws = (f * detune) lfsaw sum;

	-- Cutoff sweeps from 1x to 8x the note frequency, capped at 11 kHz.
	let cutoff = min(f * (1.0 + 7.0 * env), 11000.0);
	let filt = saws rlpf(cutoff, 0.4);

	filt * env * a * 0.2
}

fn sawLead() S = voicer(kMaxVoices, sawLeadVoice) sum outlet;

sawLead defSynth("sawLead");

---------------------------------------------------------------------------
-- 5. PWM pad.
--    Variable-sharpness square with slow LFOs modulating sharpness and
--    amplitude, plus a long ADSR for pad-style sustain.

fn pwmPadVoice() S {
	let f = pNoteFreq();
	let a = pNoteAmp();
	let g = gate();

	let env = adsr(g, 0.4, 0.3, 0.8, 0.6);

	-- Slow tremolo, mapped from bipolar -> 0.6..1.0 amplitude window.
	let trem = 0.25 sinosc bilin(0.6, 1.0);

	-- Slowly varying "sharpness" param to smoothSquare gives the pad motion.
	let sharp = 0.3 sinosc bilin(2.0, 6.0);
	let voice = f smoothSquare(sharp);

	-- Gentle lowpass for warmth.
	let cutoff = min(f * 6.0, 8000.0);
	let filt = voice lpf(cutoff);

	filt * trem * env * a * 0.18
}

fn pwmPad() S = voicer(kMaxVoices, pwmPadVoice) sum outlet;

pwmPad defSynth("pwmPad");

---------------------------------------------------------------------------
-- 6. Karplus-Strong pluck.
--    Per-voice feedback delay with a variable read tap at fs()/freq,
--    excited by a brief noise burst and damped by a one-pole lowpass.

fn pluckVoice() S {
	let f = pNoteFreq();
	let a = pNoteAmp();
	let g = gate();

	let buf = delayVar(0.05);
	let exc = decay2(g, 0.001, 0.01) * white();
	let dlyTime = 1.0 / f;
	let tap = buf vread(dlyTime);
	let damp = tap lpf(2000.0) * 0.99;
	(exc + damp) write(buf);
	tap * a * 0.5
}

fn pluck() S = voicer(kMaxVoices, pluckVoice) sum outlet;

pluck defSynth("pluck");

---------------------------------------------------------------------------
-- 7. Modal bell.
--    Sum of ring filters at inharmonic partial ratios excited by an impulse.

fn modalBellVoice() S {
	let f = pNoteFreq();
	let a = pNoteAmp();
	let g = gate();

	-- Impulse excitation right at note-on.
	let strike = decay2(g, 0.0005, 0.005);

	-- Inharmonic partials, loosely tubular-bell-like.
	let ratios = [0.5, 1.0, 2.0, 2.76, 5.4, 8.93] vec;
	let amps   = [0.5, 1.0, 0.6, 0.4, 0.25, 0.15] vec;
	let dcys   = [3.5, 3.0, 2.0, 1.4, 0.8, 0.5] vec;

	let modes = strike ring(f * ratios, dcys) * amps;

	modes sum * a * 0.25
}

fn modalBell() S = voicer(kMaxVoices, modalBellVoice) sum outlet;

modalBell defSynth("modalBell");

---------------------------------------------------------------------------
-- 8. Kick drum.
--    Fast pitch envelope on a sine, plus a tiny noise click transient.

fn kickVoice() S {
	let a = pNoteAmp();
	let g = gate();

	-- Pitch sweep from ~120 Hz down to ~45 Hz over ~60 ms.
	let pEnv = decay2(g, 0.001, 0.06);
	let f = 45.0 + 75.0 * pEnv;

	let body = f sinosc * decay2(g, 0.001, 0.4);
	let click = decay2(g, 0.0001, 0.005) * 2 white * 0.5;

	(body + click) tanh * a * 0.7
}

fn kick() S = voicer(4, kickVoice) sum outlet;

kick defSynth("kick");

---------------------------------------------------------------------------
-- 9. Snare drum.
--    Bandpass-filtered noise mixed with two detuned tone components.

fn snareVoice() S {
	let a = pNoteAmp();
	let g = gate();

	let noiseEnv = decay2(g, 0.001, 0.18);
	let toneEnv  = decay2(g, 0.001, 0.08);

	let tones = [180.0, 330.0] vec sinosc * toneEnv * 0.6;
	let noise = 2 white bpf(4500.0, 1.5) * noiseEnv;

	(tones sum + noise) * a * 0.5
}

fn snare() S = voicer(4, snareVoice) sum outlet;

snare defSynth("snare");

---------------------------------------------------------------------------
-- 10. Sub bass.
--     Sine sub plus a filtered saw an octave up for harmonic content.

fn subBassVoice() S {
	let f = pNoteFreq();
	let a = pNoteAmp();
	let g = gate();

	let env = adsr(g, 0.005, 0.2, 0.8, 0.2);

	let sub = f sinosc;
	let cutoff = min(f * 6.0, 2000.0);
	let saw = (f * 2.0) lfsaw rlpf(cutoff, 0.6);

	(sub + saw * 0.4) * env * a * 0.5
}

fn subBass() S = voicer(4, subBassVoice) sum outlet;

subBass defSynth("subBass");

---------------------------------------------------------------------------

let kInstrumentSynths = [
	"organ",
	"fmBell",
	"fmBrass",
	"sawLead",
	"pwmPad",
	"pluck",
	"modalBell",
	"kick",
	"snare",
	"subBass",
];
