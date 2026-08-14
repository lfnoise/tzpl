-- Compile every effects and instruments library def to a real dylib.
--
-- The byte-diff suites (effects_prod_diff.x, synthc_prod_diff.x) prove the
-- two compilers agree, but agreement is not validity: both can emit the SAME
-- invalid C++ and still pass. This test closes that gap by pushing every
-- library def through defSynthX -- clang compile, link, dlopen -- and
-- asserting each def actually registers.
--
-- Regression: fxSympathetic once emitted an event-loop reference to a local
-- (v11) declared in another iso-group's guarded block, in both compilers
-- (fixed by promoting cross-iso-group temps to instance variables at the end
-- of computeIsoGroups).

import synthdef.*;
import synthc.compile.*;
import std.result.*;
import effects.*;
import instruments.*;
import audio_engine.*;

let fails = &0;

-- defSynthXChecked, not defSynthX: a failed compile must be REPORTED and
-- counted, not halt the sweep (defSynthX panics on failure).
async fn cc(f GraphFn, name String) Void {
	match (f defSynthXChecked(name) await) {
		ok(cpp): {
			if (listSynthDefs() contains(name)) { println("PASS " $ name); }
			else { fails <- *fails + 1; println("FAIL " $ name $ " (compiled but not registered)"); }
		}
		err(msg): { fails <- *fails + 1; println("FAIL " $ name $ ": " $ msg); }
	}
}

cc(fxTremolo, "fxTremolo") await;
cc(fxVibrato, "fxVibrato") await;
cc(fxFlanger, "fxFlanger") await;
cc(fxChorus, "fxChorus") await;
cc(fxPhaser, "fxPhaser") await;
cc(fxWah, "fxWah") await;
cc(fxAutoWah, "fxAutoWah") await;
cc(fxRotary, "fxRotary") await;
cc(fxEcho, "fxEcho") await;
cc(fxPingPong, "fxPingPong") await;
cc(fxMultiTap, "fxMultiTap") await;
cc(fxMatrixDelay, "fxMatrixDelay") await;
cc(fxDiffuser, "fxDiffuser") await;
cc(fxReverb, "fxReverb") await;
cc(fxSympathetic, "fxSympathetic") await;
cc(fxCompressor, "fxCompressor") await;
cc(fxLimiter, "fxLimiter") await;
cc(fxExpander, "fxExpander") await;
cc(fxNoiseGate, "fxNoiseGate") await;
cc(fxBooster, "fxBooster") await;
cc(fxSwell, "fxSwell") await;
cc(fxDistortion, "fxDistortion") await;
cc(fxLoFi, "fxLoFi") await;
cc(fxFilter, "fxFilter") await;
cc(fxPitchShift, "fxPitchShift") await;
cc(fxGranulator, "fxGranulator") await;
cc(fxFormant, "fxFormant") await;

cc(smpPerc(), "smpPerc") await;
cc(smpLoopTail(), "smpLoopTail") await;
cc(smpLoopEnv(), "smpLoopEnv") await;
cc(wtLead(), "wtLead") await;
cc(resonBank(), "resonBank") await;
cc(ksPluck(), "ksPluck") await;

if (*fails == 0) { println("FX COMPILE ALL PASS"); }
else { println("FX COMPILE ALL FAIL: " $ (*fails) toString); }
