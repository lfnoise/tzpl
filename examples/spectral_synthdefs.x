-- Spectral processing examples.
--
-- Each effect is a short-time FFT process built on `spectralChain`: the
-- input is cut into overlapping windowed frames, each frame goes through a
-- body function as a spectrum, and the processed frames are resynthesized
-- and overlap-added. Writing_SynthDefs §13 explains the frame layout and
-- the spec* helpers used here.
--
-- Every effect is a plain function of its input signal (spBandPass,
-- spGate, ...), so it can be used in any graph; each also has a demo
-- synthdef that runs it on a synthetic source. The demos are registered
-- below and tagged "spectral". playSpectralExamples() auditions them all in
-- turn; renderSpectralExamples() renders the same sequence to a file.
-- Run with audio on.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import filters.*;

let tags = ["spectral"];

-- Frame size and hop for every example: 1024-sample frames (21 ms at 48k,
-- 47 Hz bins) with 4x overlap. A chain delays its input by fftSize samples.
const N = 1024;
const HOP = 256;

fn spec(x S, body GraphFn1) S = spectralChain(x, N, HOP, body);

---------------------------------------------------------------------------
-- Sources

-- A slowly drifting saw chord: dense harmonics for the filters to carve.
fn chordSrc() S {
	let roots = [110.0, 164.8, 220.0, 277.2, 329.6];
	let drift = 0.1 asSignal lfnoise1(5) * 0.003 + 1.0;
	(roots asSignal * drift) lfsaw sum * 0.06
}

-- A stepped melody on a filtered saw: something with pitch to rob or shift.
fn melodySrc() S {
	let note = 5.0 asSignal lfnoise0(1) * 7.0 + 57.0 |> round;
	let osc = note nnhz lfsaw;
	osc lpf(2500.0) * 0.25
}

-- A pure melody buried in hiss, for the noise gate.
fn noisyToneSrc() S {
	let note = 3.0 asSignal lfnoise0(1) * 5.0 + 72.0 |> round;
	(note nnhz sinosc * 0.2) + (white() * 0.03)
}

-- Noise bursts through a wandering band-pass: a rhythmic, "talking"
-- spectral envelope for cross-synthesis.
fn burstSrc() S {
	let env = dust(7.0) decay(0.12);
	let center = 0.7 asSignal lfnoise1(1) linexp(-1.0, 1.0, 300.0, 3000.0);
	white() bpf(center, 1.0) * env * 2.0
}

-- A mono effect output, panned center.
fn stereoOut(x S) S = [x, x] join outlet;

---------------------------------------------------------------------------
-- Brick-wall band-pass: keep only the bins between lo and hi Hz. The mask
-- is a [N/2] row, so it multiplies the real and imaginary rows alike.

fn spBandPass(x S, lo AsSignal, hi AsSignal) S = x spec(fn(frame S) {
	let f = specBinFreqs(N);
	frame * ((f >= lo) * (f <= hi))
});

fn spBandPassDemo() S {
	let width = control("width", espec(1.05, 4.0, 1.5)) lag(0.1);
	let center = 0.08 sinosc linexp(-1.0, 1.0, 200.0, 4000.0);
	chordSrc() + (white() * 0.05) |> spBandPass(center / width, center * width) |> stereoOut
}

spBandPassDemo defSynth("spBandPass", tags) await;

---------------------------------------------------------------------------
-- Spectral tilt: a gain of dbPerOct per octave around 1 kHz, exact at every
-- bin (no filter ripple, no phase shift).

fn spTilt(x S, dbPerOct AsSignal) S = x spec(fn(frame S) {
	let octaves = specBinFreqs(N) max(20.0) / 1000.0 |> log2;
	frame * (octaves * dbPerOct |> dbamp)
});

fn spTiltDemo() S {
	let tilt = 0.1 sinosc * 3.0;   -- sweeps -3 .. +3 dB/octave
	chordSrc() spTilt(tilt) |> stereoOut
}

spTiltDemo defSynth("spTilt", tags) await;

---------------------------------------------------------------------------
-- Noise gate per bin: zero every bin whose amplitude is under the
-- threshold. The tone's few strong bins pass; the hiss, spread thin over
-- all of them, mostly does not.

fn spGate(x S, threshDb AsSignal) S = x spec(fn(frame S) {
	frame * (frame specAmps(N) > (threshDb dbamp))
});

fn spGateDemo() S {
	let on = 0.25 lfupulse(0.5);   -- 2 s gated, 2 s dry
	let thresh = control("thresh", lspec(-80.0, -20.0, -46.0));
	let src = noisyToneSrc();
	select2(on, src spGate(thresh), src spec(fn(frame S) { frame })) |> stereoOut
}

spGateDemo defSynth("spGate", tags) await;

---------------------------------------------------------------------------
-- Spectral contrast: each bin's amplitude raised to the power p, relative
-- to the frame's strongest bin, keeping its phase. p > 1 sharpens the
-- peaks (weak bins fall away); p < 1 flattens the spectrum toward noise.
-- The gain is capped so near-silent bins are not boosted without limit.

fn spContrast(x S, p AsSignal) S = x spec(fn(frame S) {
	let amps = frame specAmps(N);
	let rel = amps / (amps maxOf + 1.0e-9) + 1.0e-6;
	frame * (rel pow(p - 1.0) min(30.0))
});

fn spContrastDemo() S {
	let p = 0.07 sinosc linexp(-1.0, 1.0, 0.5, 3.0);
	chordSrc() + (white() * 0.02) |> spContrast(p) |> stereoOut
}

spContrastDemo defSynth("spContrast", tags) await;

---------------------------------------------------------------------------
-- Robot and whisper: keep the magnitudes, replace the phases. A phase of
-- pi k in bin k makes every frame a symmetric pulse centered in its window
-- (zero phase would put it at the frame's edges, where the window is near
-- zero), so the output buzzes at the frame rate (fs / HOP = 187.5 Hz at
-- 48k) whatever the input's pitch; random phase dissolves pitch into breath.

fn spRobot(x S) S = x spec(fn(frame S) { specPolar(frame specMag, specBins(N) * pi) });

fn spWhisper(x S) S = x spec(fn(frame S) {
	specPolar(frame specMag, urand(N // 2) * twopi)
});

fn spRobotDemo() S = melodySrc() spRobot * 1.5 |> stereoOut;
fn spWhisperDemo() S = melodySrc() spWhisper * 2.0 |> stereoOut;

spRobotDemo defSynth("spRobot", tags) await;
spWhisperDemo defSynth("spWhisper", tags) await;

---------------------------------------------------------------------------
-- Frequency shift by whole bins: bin k moves to bin k + shift (wrapped
-- bins are masked off). Adding the same number of Hz to every partial
-- makes harmonic sounds inharmonic. A partial moved by s bins needs its
-- phase advanced 2 pi s HOP / N more per frame; shifting in steps of
-- N / HOP bins makes that a whole number of turns, so no correction is
-- needed: the shift moves in 4-bin steps (187.5 Hz at 48k).

fn spShift(x S, steps AsSignal) S = x spec(fn(frame S) {
	let shift = steps asSignal round * (N // HOP) |> i32;
	let k = specBins(N);
	frame rotate(0 - shift) * ((k >= shift) * (k < N // 2 + shift))
});

fn spShiftDemo() S {
	let steps = 0.5 asSignal lfnoise0(1) * 3.0;   -- -3 .. +3 steps, new every 2 s
	melodySrc() spShift(steps) |> stereoOut
}

spShiftDemo defSynth("spShift", tags) await;

---------------------------------------------------------------------------
-- Spectral blur: each bin's magnitude follows its value in earlier frames
-- (a one-pole lowpass across frames, frame-to-frame state kept in a
-- delayVar inside the body), with the current phase. Transients smear
-- into a wash; amount 0 is the dry signal, amount near 1 a long tail.

fn spBlur(x S, amount AsSignal) S = x spec(fn(frame S) {
	let m = frame specMag;
	let d = delayVar();
	let prev = d read(1);   -- the smoothed magnitudes one frame ago
	let sm = prev + ((m - prev) * (1.0 - amount));
	sm -> d;
	specPolar(sm, frame specPhase)
});

fn spBlurDemo() S {
	let amount = control("amount", lspec(0.0, 0.99, 0.95));
	burstSrc() + (melodySrc() * 0.5) |> spBlur(amount) |> stereoOut
}

spBlurDemo defSynth("spBlur", tags) await;

---------------------------------------------------------------------------
-- Spectral freeze: while `freeze` is on, the magnitudes captured when it
-- went on are held and resynthesized with fresh random phases every frame
-- (holding the phases too would repeat one frame, a buzz at the frame
-- rate). While it is off the frame passes unchanged.

fn spFreeze(x S, freeze AsSignal) S = x spec(fn(frame S) {
	let frozen = freeze asSignal > 0.5;
	let d = delayVar();
	let held = select2(frozen, d read(1), frame specMag);
	held -> d;
	select2(frozen, specPolar(held, urand(N // 2) * twopi), frame)
});

fn spFreezeDemo() S {
	let freeze = 1.0 - (0.2 lfupulse(0.4));   -- 2 s live, then frozen for 3
	melodySrc() spFreeze(freeze) |> stereoOut
}

spFreezeDemo defSynth("spFreeze", tags) await;

---------------------------------------------------------------------------
-- Cross-synthesis: impose one sound's spectral envelope on another. The
-- chain runs on a two-channel signal [carrier, modulator], so its frame is
-- [2, 2, N/2] -- a [2, N/2] spectrum per channel -- and the body can
-- combine them: each carrier bin is scaled by the modulator's amplitude in
-- that bin. The result fills both output channels.

fn spCross(carrier S, modulator S, gain AsSignal) S {
	let both = spec([carrier, modulator] join, fn(frame S) {
		let car = frame at(0, 0);
		let mod = frame at(1, 0);
		let out = car * (mod specAmps(N) * gain);
		[out, out] stack
	});
	both at(0)
}

fn spCrossDemo() S {
	let gain = control("gain", espec(5.0, 500.0, 60.0));
	spCross(chordSrc(), burstSrc(), gain) |> stereoOut
}

spCrossDemo defSynth("spCross", tags) await;

---------------------------------------------------------------------------
-- Play the demos

import audio_engine as ae;
import clock.*;

let kSpectralDemos = [
	"spBandPass", "spTilt", "spGate", "spContrast", "spRobot",
	"spWhisper", "spShift", "spBlur", "spFreeze", "spCross",
];

fn playSpectralExamples() {
	go(coro fn() Float {
		for (name : kSpectralDemos) { name playFor(8.0) yieldAll; }
		ae.endRender(0.1);   -- ends a render; does nothing when playing live
		"done" println;
	}());
}

fn renderSpectralExamples() {
	let h = "/tmp/spectral_examples.wav" ae.renderNRT(8.0 * kSpectralDemos length, playSpectralExamples);
	await ae.renderDone(h);
	"render done" println;
}

--playSpectralExamples();
--renderSpectralExamples();
