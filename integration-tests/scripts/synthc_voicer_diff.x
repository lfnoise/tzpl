-- synthc voicer differential test (M4.0 + M4.1).
--
-- Validates the flat-voice-mode voicer codegen for non-branching voice bodies:
-- per-voice instance-var arrays (voice_v<serial>[maxVoices]), the noteParam ->
-- voicer_params[i][serial] gather, the RowVoicer / voicer_params struct fields,
-- per-voice init, and the five note-lifecycle functions (M4.0); plus per-voice
-- 1-sample delay state (voice_d<sn>[maxVoices], zeroed per noteOn) and n-way
-- select for envelope state machines (M4.1). Both Tier-1 (analysis dump) and
-- Tier-2 (generated C++) must match the C++ compiler.
--
-- Still deferred: multichannel voice bodies and per-voice RNG (M4.1 cont.), and
-- ring-buffer delays inside voices -- combn / variable delays (M4.2).

import synthdef.*;
import common_ugens.*;
import filters.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkVoicer(name String, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let sx = g toSynthSexpr(name);
	let ctx = g importGraph(name) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (import/analysis errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	let dumpV = cfDumpCompare(ctx dumpToString, synthdefAnalysisDump(sx, false));
	-- voicerCppCompare order-normalizes the per-voice delay-advance lines (the C++
	-- emits them in pointer-hash order; see difftest.x).
	let cppV = voicerCppCompare(genCpp(ctx, name), synthdefGenCppFromSexpr(sx, 0, false));
	if (dumpV == "PASS" && cppV == "PASS") { println("  PASS " $ name); }
	else {
		`failures = `failures + 1;
		println("  FAIL " $ name);
		if (dumpV != "PASS") { println("  [dump] " $ dumpV); }
		if (cppV != "PASS") { println("  [cpp] " $ cppV); }
	}
}

-- minimal voicer: gate * amp * sine; two user params (amp, freq), 2 voices
fn tiny() S {
	voicer(2, fn() {
		let g   = gate();
		let amp = noteParam("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		let f   = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		(fs() / f) sin * g * amp
	}) sum outlet
}

-- more voices + a third user param, extra arithmetic (still delay-free)
fn tiny8() S {
	voicer(8, fn() {
		let g    = gate();
		let f    = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let amp  = noteParam("amp",  ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		let det  = noteParam("det",  ControlSpec { lo: 0.0, hi: 1.0, init: 0.1, warp: ControlWarp.linear });
		((fs() / f) sin * g * amp + (fs() / f) cos * det) * 0.5
	}) sum outlet
}

-- gate-only voicer (no extra user params): exercises numUserParams == 0
fn gateonly() S {
	voicer(4, fn() {
		let g = gate();
		(fs() / 440.0) sin * g * 0.2
	}) sum outlet
}

-- M4.1: per-voice 1-sample delays. sineVoice = fadein + lag (1-pole) envelope
-- and a phasor-based sine oscillator -- all delayVar(1) state, replicated per
-- voice (voice_d<sn>[maxVoices]).
fn sineVoice() S {
	let g   = gate();
	let pch = noteParam("pitch", ControlSpec { lo: 0.0, hi: 127.0, init: 60.0, warp: ControlWarp.linear });
	let amp = noteParam("amp",   ControlSpec { lo: 0.0, hi: 1.0,   init: 0.5,  warp: ControlWarp.linear });
	let env = g fadein(0.01) lag(0.3);
	pch nnhz sinosc cb * env * amp
}
fn sineSynth() S = voicer(32, sineVoice) sum outlet;

-- M4.1: n-way select via an adsr envelope state machine (+ per-voice delays).
fn adsrSynth() S {
	voicer(8, fn() {
		let g    = gate();
		let f    = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let amp  = noteParam("amp",  ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		let env  = g adsr(0.01, 0.1, 0.7, 0.3);
		(fs() / f) sin * env * amp
	}) sum outlet
}

-- M4.1: multichannel voice body (flatVoiceChans = 2). A detuned 2-channel
-- signal per voice -> flat loop runs maxVoices*2, per-voice vars index [i>>1].
fn stereoSynth() S {
	voicer(4, fn() {
		let g   = gate();
		let f   = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let amp = noteParam("amp",  ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		(fs() / ([1.0, 1.01] * f)) sin * g * amp
	}) sum outlet
}

-- M4.1: per-voice RNG. white() noise -> voice_rgen<g>[maxVoices], seeded per
-- voice in init + noteOn, indexed by voice in the body.
fn noiseSynth() S {
	voicer(4, fn() {
		let g   = gate();
		let amp = noteParam("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		white() * g fadein(0.01) * amp * 0.2
	}) sum outlet
}

-- M4.1: multichannel body + per-voice RNG together.
fn stereoNoiseSynth() S {
	voicer(4, fn() {
		let g   = gate();
		let amp = noteParam("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		([1.0, 1.0] * white()) * g fadein(0.01) * amp * 0.2
	}) sum outlet
}

-- M4.1: additive voice with a 7-partial harmonic vector -> the constant is
-- zero-extended to 8 (power of two), exercising the reduce-input padding inside
-- a voicer (the organ pattern).
fn additiveSynth() S {
	voicer(8, fn() {
		let g    = gate();
		let f    = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let amp  = noteParam("amp",  ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		let harms = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0] vec;
		let amps  = [1.0, 0.7, 0.5, 0.4, 0.25, 0.2, 0.15] vec;
		((f * harms) sinosc * amps) sum * g fadein(0.01) * amp * 0.1
	}) sum outlet
}

-- M4.2: a runtime-sized ring buffer (comb) inside a voice -- per-voice calloc'd
-- buffer, vread with interpolation, write, and the per-voice wrpos advance.
fn combSynth() S {
	voicer(4, fn() {
		let g   = gate();
		let f   = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let amp = noteParam("amp",  ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		let buf = delayVar(0.05 * fs());
		let exc = white() * g;
		let tap = buf vread(fs() / max(f, 20.0));
		(exc + tap * 0.5) write(buf);
		tap * amp * 0.5
	}) sum outlet
}

-- M4.2: multichannel FIXED ring buffers (ring filter z2) inside a voice. The
-- 6-partial vector pads to 8 channels; each partial's z2 ring is a fixed [2]
-- buffer (voice_dN[mv*chans][2]), memset per noteOn, advanced per voice.
fn ringSynth() S {
	voicer(4, fn() {
		let g   = gate();
		let f   = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let amp = noteParam("amp",  ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		let ratios = [0.5, 1.0, 2.0, 2.76, 5.4, 8.93] vec;
		let amps   = [0.5, 1.0, 0.6, 0.4, 0.25, 0.15] vec;
		let dcys   = [3.5, 3.0, 2.0, 1.4, 0.8, 0.5] vec;
		let modes = (white() * g fadein(0.001)) ring(f * ratios, dcys) * amps;
		modes sum * amp * 0.25
	}) sum outlet
}

checkVoicer("tiny", tiny);
checkVoicer("tiny8", tiny8);
checkVoicer("gateonly", gateonly);
checkVoicer("sine_voice", sineSynth);
checkVoicer("adsr_synth", adsrSynth);
checkVoicer("stereo_synth", stereoSynth);
checkVoicer("noise_synth", noiseSynth);
checkVoicer("stereo_noise", stereoNoiseSynth);
checkVoicer("additive_synth", additiveSynth);
checkVoicer("comb_synth", combSynth);
checkVoicer("ring_synth", ringSynth);

if (`failures == 0) {
	println("M4 VOICER DIFF PASS");
} else {
	println("M4 VOICER DIFF FAIL: " $ `failures toString);
}
