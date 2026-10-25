-- ugen_cases.x -- the common_ugens.x / filters.x side of the reference-ugen
-- comparison (block engine plan M7). Writes one synthc C++ plugin source per
-- case and channel count to $UC_OUT/uc_<case>_<chans>.cpp; run_ugen_compare.sh
-- compiles them and bench_ugen_compare runs each next to its tensor-ugens
-- reference.
--
-- The parameters match bench_ugen_compare.cpp's TU cases, converted to our
-- conventions: frequencies in Hertz (TU: cycles per sample), times in seconds
-- (TU: samples), and our lags and decays fall by 40 dB in their time where
-- TU's fall by 60 dB, so a TU time of n samples is n / fs * ln(0.01) / ln(0.001)
-- seconds here. Q (TU) is 1 / rq here, and a bandwidth in octaves for
-- bpf/brf/peakEQ.
--
-- Kinds: an oscillator has no inlet; a filter reads inlet "in"; an audio-rate
-- frequency case also reads inlet "freq", in Hertz.

import synthdef.*;
import common_ugens.*;
import filters.*;
import synthc.compile.*;
import std.fs.*;
import std.result.*;

let kFs = 48000.0;
let kFm = 440.0;
let kFc = 1000.0;
let kQ = 2.0;
let k40of60 = 0.01 log / 0.001 log;    -- 2/3

-- TU's n samples (60 dB) as our seconds (40 dB).
fn secs(n Float) Float = n / kFs * k40of60;
-- ... and as our samples, for lag(x, u, d), which takes samples.
fn samps(n Float) Float = n * k40of60;

-- The bandwidth in octaves whose alpha equals the cookbook's sin(w0) / (2 Q).
fn bwOfQ(fc Float, q Float) Float {
	let w0 = 2.0 * pi * fc / kFs;
	let y = 1.0 / (2.0 * q);
	let asinhY = (y + (y * y + 1.0) sqrt) log;
	asinhY * 2.0 / 2.0 log * w0 sin / w0
}

let kBw = bwOfQ(kFc, kQ);

let oscNames = ["phasor", "sinosc", "fsinosc", "lfupar", "lfvsaw", "lfupulse", "lfzpulse",
                "white", "pink"];
let oscFns = [
	fn(ch Int) S { phasor(fill(ch, kFm)) },
	fn(ch Int) S { sinosc(fill(ch, kFm)) },
	fn(ch Int) S { fsinosc(fill(ch, kFm)) },
	fn(ch Int) S { lfupar(fill(ch, kFm)) },
	fn(ch Int) S { lfvsaw(fill(ch, kFm), 0.3) },
	fn(ch Int) S { lfupulse(fill(ch, kFm), 0.3) },
	fn(ch Int) S { lfzpulse(fill(ch, kFm), 0.3) },
	fn(ch Int) S { white(ch) },
	fn(ch Int) S { pink(ch) },
];

let filtNames = ["lag", "lag3", "lagud", "decay", "decay2", "leakdc", "leaky",
                 "lpf", "rlpf", "rhpf", "bpf", "brf", "apf", "peq", "lsf", "hsf",
                 "ringz", "formlet"];
let filtFns = [
	fn(x S) S { x lag(secs(480.0)) },
	fn(x S) S { x lag3(3.0 * secs(480.0)) },
	fn(x S) S { x lag(scalar(samps(48.0)), scalar(samps(4800.0))) },
	fn(x S) S { x decay(secs(4800.0)) },
	fn(x S) S { x decay2(secs(48.0), secs(4800.0)) },
	fn(x S) S { x leakdc(0.995) },
	fn(x S) S { x leaky(0.995) },
	fn(x S) S { x lpf(kFc) },
	fn(x S) S { x rlpf(kFc, 1.0 / kQ) },
	fn(x S) S { x rhpf(kFc, 1.0 / kQ) },
	fn(x S) S { x bpf(kFc, kBw) },
	fn(x S) S { x brf(kFc, kBw) },
	fn(x S) S { x apf(kFc, 1.0 / kQ) },
	fn(x S) S { x peakEQ(kFc, scalar(6.0), kBw) },
	fn(x S) S { x lowShelf(kFc, scalar(6.0), 1.0 / kQ) },
	fn(x S) S { x highShelf(kFc, scalar(6.0), 1.0 / kQ) },
	fn(x S) S { x ring(kFc, secs(4800.0) / k40of60) },
	fn(x S) S { x pling(kFc, secs(48.0) / k40of60, secs(4800.0) / k40of60) },
];

let afNames = ["rlpf_af", "ringz_af"];
let afFns = [
	fn(x S, f S) S { x rlpf(f, 1.0 / kQ) },
	fn(x S, f S) S { x ring(f, secs(4800.0) / k40of60) },
];

let outDir = getEnv("UC_OUT") unwrapOr("/tmp/ugen_compare");
let only = getEnv("UC_FILTER") unwrapOr("");

fn emit(name String, chans Int, g GraphFn) Void {
	if (only length > 0 && !(name contains(only))) { return; }
	let def = "uc_%^_%^" fmt(name, chans);
	let cpp = compileToCpp(makeGraph(g), def);
	if (cpp startsWith("error:")) {
		println("FAIL %^: %^" fmt(def, cpp));
		return;
	}
	match (writeFileResult(outDir $ "/" $ def $ ".cpp", cpp)) {
		ok(_): println("wrote " $ def);
		err(e): println("FAIL %^: %^" fmt(def, e));
	}
}

for (ch : [1, 4]) {
	for (i : (0..oscNames length - 1)) {
		let f = oscFns[i];
		emit(oscNames[i], ch, fn() S { f(ch) outlet });
	}
	for (i : (0..filtNames length - 1)) {
		let f = filtFns[i];
		emit(filtNames[i], ch, fn() S { inlet(FLOAT32, ch, "in") f outlet });
	}
	for (i : (0..afNames length - 1)) {
		let f = afFns[i];
		emit(afNames[i], ch, fn() S {
			f(inlet(FLOAT32, ch, "in"), inlet(FLOAT32, ch, "freq")) outlet
		});
	}
}
