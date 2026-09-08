-- Effect synthdefs.
--
-- Registers the ready-made stereo effect defs from the effects module
-- (lang/modules/effects.x). Each def is an insert effect: a stereo inlet
-- named "in", controls for the interesting parameters, and a stereo outlet.
-- Patch another node's output into an effect node with `connect`.
--
-- All defs are tagged "fx" so the plugin browser can group them.

import synthdef.*;
import synthc.compile.*;
import effects.*;

fxTremolo defSynthX("fxTremolo", ["fx"]) await;
fxVibrato defSynthX("fxVibrato", ["fx"]) await;
fxFlanger defSynthX("fxFlanger", ["fx"]) await;
fxChorus defSynthX("fxChorus", ["fx"]) await;
fxPhaser defSynthX("fxPhaser", ["fx"]) await;
fxWah defSynthX("fxWah", ["fx"]) await;
fxAutoWah defSynthX("fxAutoWah", ["fx"]) await;
fxRotary defSynthX("fxRotary", ["fx"]) await;
fxEcho defSynthX("fxEcho", ["fx"]) await;
fxPingPong defSynthX("fxPingPong", ["fx"]) await;
fxMultiTap defSynthX("fxMultiTap", ["fx"]) await;
fxMatrixDelay defSynthX("fxMatrixDelay", ["fx"]) await;
fxDiffuser defSynthX("fxDiffuser", ["fx"]) await;
fxReverb defSynthX("fxReverb", ["fx"]) await;
fxSympathetic defSynthX("fxSympathetic", ["fx"]) await;
fxCompressor defSynthX("fxCompressor", ["fx"]) await;
fxLimiter defSynthX("fxLimiter", ["fx"]) await;
fxExpander defSynthX("fxExpander", ["fx"]) await;
fxNoiseGate defSynthX("fxNoiseGate", ["fx"]) await;
fxBooster defSynthX("fxBooster", ["fx"]) await;
fxSwell defSynthX("fxSwell", ["fx"]) await;
fxDistortion defSynthX("fxDistortion", ["fx"]) await;
fxLoFi defSynthX("fxLoFi", ["fx"]) await;
fxFilter defSynthX("fxFilter", ["fx"]) await;
fxPitchShift defSynthX("fxPitchShift", ["fx"]) await;
fxGranulator defSynthX("fxGranulator", ["fx"]) await;
fxFormant defSynthX("fxFormant", ["fx"]) await;

let kEffectSynths = [
	"fxTremolo",
	"fxVibrato",
	"fxFlanger",
	"fxChorus",
	"fxPhaser",
	"fxWah",
	"fxAutoWah",
	"fxRotary",
	"fxEcho",
	"fxPingPong",
	"fxMultiTap",
	"fxMatrixDelay",
	"fxDiffuser",
	"fxReverb",
	"fxSympathetic",
	"fxCompressor",
	"fxLimiter",
	"fxExpander",
	"fxNoiseGate",
	"fxBooster",
	"fxSwell",
	"fxDistortion",
	"fxLoFi",
	"fxFilter",
	"fxPitchShift",
	"fxGranulator",
	"fxFormant"
];
