-- synthc flat-voice SIMD differential test (M5.3).
--
-- Validates the flat-voice SIMD codegen against the C++ compiler at width 4: the
-- flat trip-count width decision, Case A / Case B (stride) loop emission, the
-- voice-local SoA load/splat/gather var-refs, the non-voice channel-wrap forms,
-- the noteParam per-voice gather, and the per-voice delay SIMD paths (1-sample
-- contiguous load/store, ring gather/scatter, and the variable-read interpolation
-- lambda). RNG / select voicers stay SIMD-disqualified (correct-scalar), matching
-- the C++; the corpus mixes both so the disqualifier is exercised too.

import synthdef.*;
import common_ugens.*;
import filters.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkVoicerSimd(name String, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let sx = g toSynthSexpr(name);
	let ctx = g importGraph(name) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	-- voicerCppCompare order-normalizes the per-voice delay-advance lines.
	let v = voicerCppCompare(genCpp(ctx, name, 4), synthdefGenCppFromSexpr(sx, 4, false));
	if (v == "PASS") { println("  PASS " $ name); }
	else { `failures = `failures + 1; println("  FAIL " $ name); println(v); }
}

let ampSpec  = ControlSpec { lo: 0.0,  hi: 1.0,    init: 0.5,   warp: ControlWarp.linear };
let freqSpec = ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear };

-- 32 mono voices: voice SoA load + non-voice splat + noteParam gather (Case B).
fn sineVoice() S {
	let g   = gate();
	let pch = noteParam("pitch", ControlSpec { lo: 0.0, hi: 127.0, init: 60.0, warp: ControlWarp.linear });
	let amp = noteParam("amp", ampSpec);
	let env = g fadein(0.01) lag(0.3);
	pch nnhz sinosc cb * env * amp
}
fn sineSynth() S = voicer(32, sineVoice) sum outlet;

-- multichannel voice body (2 chans/voice): per-voice vars index [i>>1], non-voice
-- 2-chan constant gathers, voicer width = maxVoices*2.
fn stereoSynth() S {
	voicer(4, fn() {
		let g = gate(); let f = noteParam("freq", freqSpec); let amp = noteParam("amp", ampSpec);
		(fs() / ([1.0, 1.01] * f)) sin * g * amp
	}) sum outlet
}

-- 8-partial additive (8-chan voice body): non-voice constants wrap by & 7, plus
-- per-voice 1-sample phasor delays vectorized.
fn additiveSynth() S {
	voicer(8, fn() {
		let g = gate(); let f = noteParam("freq", freqSpec); let amp = noteParam("amp", ampSpec);
		let harms = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0] vec;
		let amps = [1.0, 0.7, 0.5, 0.4, 0.25, 0.2, 0.15] vec;
		((f * harms) sinosc * amps) sum * g fadein(0.01) * amp * 0.1
	}) sum outlet
}

-- per-voice fixed ring (z2 modal filter, 8-chan): ring gather/scatter per voice.
fn ringSynth() S {
	voicer(4, fn() {
		let g = gate(); let f = noteParam("freq", freqSpec); let amp = noteParam("amp", ampSpec);
		let ratios = [0.5, 1.0, 2.0, 2.76, 5.4, 8.93] vec;
		let amps = [0.5, 1.0, 0.6, 0.4, 0.25, 0.15] vec;
		let dcys = [3.5, 3.0, 2.0, 1.4, 0.8, 0.5] vec;
		let modes = (white() * g fadein(0.001)) ring(f * ratios, dcys) * amps;
		modes sum * amp * 0.25
	}) sum outlet
}

-- per-voice runtime ring + cubic vread, NO rng: exercises the flat-voice variable-
-- read interpolation lambda (every corpus vread voicer also has rng, so this is the
-- only synth that hits the per-voice genSimdInterp path).
fn vreadSynth() S {
	voicer(32, fn() {
		let g = gate(); let f = noteParam("freq", freqSpec); let amp = noteParam("amp", ampSpec);
		let buf = delayVar(0.05 * fs());
		((fs() / f) sin * g) write(buf);
		buf vread(fs() / max(f, 20.0)) * amp
	}) sum outlet
}

-- RNG voicer: SIMD-disqualified, must stay correct-scalar (matches the C++).
fn noiseSynth() S {
	voicer(4, fn() {
		let g = gate(); let amp = noteParam("amp", ampSpec);
		white() * g fadein(0.01) * amp * 0.2
	}) sum outlet
}

-- ADSR state-machine voicer: select -> disqualified, stays scalar.
fn adsrSynth() S {
	voicer(8, fn() {
		let g = gate(); let f = noteParam("freq", freqSpec); let amp = noteParam("amp", ampSpec);
		(fs() / f) sin * g adsr(0.01, 0.1, 0.7, 0.3) * amp
	}) sum outlet
}

checkVoicerSimd("sine_synth", sineSynth);
checkVoicerSimd("stereo_synth", stereoSynth);
checkVoicerSimd("additive_synth", additiveSynth);
checkVoicerSimd("ring_synth", ringSynth);
checkVoicerSimd("vread_synth", vreadSynth);
checkVoicerSimd("noise_synth", noiseSynth);
checkVoicerSimd("adsr_synth", adsrSynth);

if (`failures == 0) { println("M5 VOICER SIMD DIFF PASS"); }
else { println("M5 VOICER SIMD DIFF FAIL: " $ `failures toString); }
