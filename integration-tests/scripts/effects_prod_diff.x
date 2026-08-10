-- Effects library differential test.
--
-- Runs every fx* synthdef graph from the effects module through synthc at
-- the production config (rewrites ON, SIMD width 4) and asserts the output
-- byte-matches the C++ compiler at the same config -- the same check
-- synthc_prod_diff.x applies to the instrument corpus.

import synthdef.*;
import effects.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkFx(name String, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let sx = g toSynthSexpr(name);
	let ctx = g importGraph(name, true) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	let v = voicerCppCompare(genCpp(ctx, name, 4), synthdefGenCppFromSexpr(sx, 4, true));
	if (v == "PASS") { println("  PASS " $ name); }
	else { `failures = `failures + 1; println("  FAIL " $ name); println(v); }
}

checkFx("fxTremolo", fxTremolo);
checkFx("fxVibrato", fxVibrato);
checkFx("fxFlanger", fxFlanger);
checkFx("fxChorus", fxChorus);
checkFx("fxPhaser", fxPhaser);
checkFx("fxWah", fxWah);
checkFx("fxAutoWah", fxAutoWah);
checkFx("fxRotary", fxRotary);
checkFx("fxEcho", fxEcho);
checkFx("fxPingPong", fxPingPong);
checkFx("fxMultiTap", fxMultiTap);
checkFx("fxMatrixDelay", fxMatrixDelay);
checkFx("fxDiffuser", fxDiffuser);
checkFx("fxReverb", fxReverb);
checkFx("fxSympathetic", fxSympathetic);
checkFx("fxCompressor", fxCompressor);
checkFx("fxLimiter", fxLimiter);
checkFx("fxExpander", fxExpander);
checkFx("fxNoiseGate", fxNoiseGate);
checkFx("fxBooster", fxBooster);
checkFx("fxSwell", fxSwell);
checkFx("fxDistortion", fxDistortion);
checkFx("fxLoFi", fxLoFi);
checkFx("fxFilter", fxFilter);
checkFx("fxPitchShift", fxPitchShift);
checkFx("fxGranulator", fxGranulator);
checkFx("fxFormant", fxFormant);

if (`failures == 0) { println("FX PROD DIFF PASS"); }
else { println("FX PROD DIFF FAIL: " $ `failures toString); }
