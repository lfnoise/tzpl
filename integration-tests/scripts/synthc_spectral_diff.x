-- synthc spectral differential test (M4.4).
--
-- Validates the spectral-chain codegen: the FFT frame loop (per-sample ring
-- write/read, hop-triggered windowed forward FFT -> body subgraph on the packed
-- split-complex frame -> inverse FFT + overlap-add), the per-chain struct fields,
-- init (tzpl_fft_create + calloc + sqrt-Hann window), and uninit. Both Tier-1
-- (analysis dump) and Tier-2 (generated C++) must match the C++ compiler.
--
-- Mirrors the C++ spectral_identity / spectral_scale tests, plus a stereo case
-- exercising per-channel ring buffers and FFT slicing.

import synthdef.*;
import common_ugens.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkSpectral(name String, synthFn GraphFn) Void {
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
	let cppV = fullCompare(genCpp(ctx, name), synthdefGenCppFromSexpr(sx, 0, false));
	if (dumpV == "PASS" && cppV == "PASS") { println("  PASS " $ name); }
	else {
		`failures = `failures + 1;
		println("  FAIL " $ name);
		if (dumpV != "PASS") { println("  [dump] " $ dumpV); }
		if (cppV != "PASS") { println("  [cpp] " $ cppV); }
	}
}

-- identity: pass the FFT frame through unchanged (spectral_identity)
fn identity() S {
	let sig = 0.3 lfsaw;
	spectralChain(sig, 256, 128, fn(frame S) { frame }) outlet
}

-- magnitude scaling in the frequency domain (spectral_scale)
fn scale() S {
	let sig = 0.3 lfsaw;
	spectralChain(sig, 256, 128, fn(frame S) { frame * 0.5 }) outlet
}

-- stereo: per-channel ring buffers + FFT slicing (inputChans = 2)
fn stereoScale() S {
	let sig = [0.3, 0.4] lfsaw;
	spectralChain(sig, 512, 256, fn(frame S) { frame * 0.5 }) outlet
}

checkSpectral("identity", identity);
checkSpectral("scale", scale);
checkSpectral("stereo_scale", stereoScale);

if (`failures == 0) {
	println("M4 SPECTRAL DIFF PASS");
} else {
	println("M4 SPECTRAL DIFF FAIL: " $ `failures toString);
}
