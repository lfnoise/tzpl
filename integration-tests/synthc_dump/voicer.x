-- Graphs from the retired synthc_voicer_diff.x (synthc vs C++ oracle), kept as an
-- analysis-dump corpus for the block-engine compiler.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import filters.*;

fn tiny() S {
	voicer(2, fn() {
		let g   = gate();
		let amp = noteParam("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		let f   = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		(fs() / f) sin * g * amp
	}) sum outlet
}
fn tiny8() S {
	voicer(8, fn() {
		let g    = gate();
		let f    = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let amp  = noteParam("amp",  ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		let det  = noteParam("det",  ControlSpec { lo: 0.0, hi: 1.0, init: 0.1, warp: ControlWarp.linear });
		((fs() / f) sin * g * amp + (fs() / f) cos * det) * 0.5
	}) sum outlet
}
fn gateonly() S {
	voicer(4, fn() {
		let g = gate();
		(fs() / 440.0) sin * g * 0.2
	}) sum outlet
}
fn sineVoice() S {
	let g   = gate();
	let pch = noteParam("pitch", ControlSpec { lo: 0.0, hi: 127.0, init: 60.0, warp: ControlWarp.linear });
	let amp = noteParam("amp",   ControlSpec { lo: 0.0, hi: 1.0,   init: 0.5,  warp: ControlWarp.linear });
	let env = g fadein(0.01) lag(0.3);
	pch nnhz sinosc cb * env * amp
}
fn sineSynth() S = voicer(32, sineVoice) sum outlet;
fn adsrSynth() S {
	voicer(8, fn() {
		let g    = gate();
		let f    = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let amp  = noteParam("amp",  ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		let env  = g adsr(0.01, 0.1, 0.7, 0.3);
		(fs() / f) sin * env * amp
	}) sum outlet
}
fn stereoSynth() S {
	voicer(4, fn() {
		let g   = gate();
		let f   = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let amp = noteParam("amp",  ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		(fs() / ([1.0, 1.01] * f)) sin * g * amp
	}) sum outlet
}
fn noiseSynth() S {
	voicer(4, fn() {
		let g   = gate();
		let amp = noteParam("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		white() * g fadein(0.01) * amp * 0.2
	}) sum outlet
}
fn stereoNoiseSynth() S {
	voicer(4, fn() {
		let g   = gate();
		let amp = noteParam("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		([1.0, 1.0] * white()) * g fadein(0.01) * amp * 0.2
	}) sum outlet
}
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
fn unusedParamSynth() S {
	voicer(8, fn() {
		let f   = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let amp = noteParam("amp",  ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		let g   = gate();
		white() * g * amp * 0.2
	}) sum outlet
}

dumpCorpus([
	("tiny", tiny),
	("tiny8", tiny8),
	("gateonly", gateonly),
	("sine_voice", sineSynth),
	("adsr_synth", adsrSynth),
	("stereo_synth", stereoSynth),
	("noise_synth", noiseSynth),
	("stereo_noise", stereoNoiseSynth),
	("additive_synth", additiveSynth),
	("comb_synth", combSynth),
	("ring_synth", ringSynth),
	("unused_param", unusedParamSynth),
]);
