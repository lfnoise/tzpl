-- The library synths (instruments.x voicers, effects.x fx*) and the extra
-- graphs of the retired synthc_prod_diff.x: the analysis-dump corpus for
-- production graphs.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import filters.*;
import effects.*;
import instruments.*;

fn arith() S { (inlet(FLOAT32, 4) * 0.5 + 0.5) sin * [0.1, 0.2, 0.3, 0.4] |> outlet }
fn echoTest() S {
	let x = inlet(FLOAT32, 4); let d = delayVar(); d init(2, 0.0);
	let r = d read(2); d write(x + r * 0.5); (x + r) |> outlet
}

dumpCorpus([
	("lib_wtLead", wtLead()),
	("lib_ksPluck", ksPluck()),
	("lib_ksPluck12", ksPluck(12)),
	("lib_resonBank", resonBank()),
	("lib_smpPerc", smpPerc()),
	("lib_smpLoopTail", smpLoopTail()),
	("lib_smpLoopEnv", smpLoopEnv()),
	("arith", arith),
	("echo", echoTest),
	("fxTremolo", fxTremolo),
	("fxVibrato", fxVibrato),
	("fxFlanger", fxFlanger),
	("fxChorus", fxChorus),
	("fxPhaser", fxPhaser),
	("fxWah", fxWah),
	("fxAutoWah", fxAutoWah),
	("fxRotary", fxRotary),
	("fxEcho", fxEcho),
	("fxPingPong", fxPingPong),
	("fxMultiTap", fxMultiTap),
	("fxMatrixDelay", fxMatrixDelay),
	("fxDiffuser", fxDiffuser),
	("fxReverb", fxReverb),
	("fxSympathetic", fxSympathetic),
	("fxCompressor", fxCompressor),
	("fxLimiter", fxLimiter),
	("fxExpander", fxExpander),
	("fxNoiseGate", fxNoiseGate),
	("fxBooster", fxBooster),
	("fxSwell", fxSwell),
	("fxDistortion", fxDistortion),
	("fxLoFi", fxLoFi),
	("fxFilter", fxFilter),
	("fxPitchShift", fxPitchShift),
	("fxGranulator", fxGranulator),
	("fxFormant", fxFormant),
]);
