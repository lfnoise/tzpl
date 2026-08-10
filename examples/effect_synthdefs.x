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

fxTremolo defSynthX("fxTremolo", ["fx"]);
fxVibrato defSynthX("fxVibrato", ["fx"]);
fxFlanger defSynthX("fxFlanger", ["fx"]);
fxChorus defSynthX("fxChorus", ["fx"]);
fxPhaser defSynthX("fxPhaser", ["fx"]);
fxWah defSynthX("fxWah", ["fx"]);
fxAutoWah defSynthX("fxAutoWah", ["fx"]);
fxRotary defSynthX("fxRotary", ["fx"]);
fxEcho defSynthX("fxEcho", ["fx"]);
fxPingPong defSynthX("fxPingPong", ["fx"]);
fxMultiTap defSynthX("fxMultiTap", ["fx"]);
fxMatrixDelay defSynthX("fxMatrixDelay", ["fx"]);
fxDiffuser defSynthX("fxDiffuser", ["fx"]);
fxReverb defSynthX("fxReverb", ["fx"]);
fxSympathetic defSynthX("fxSympathetic", ["fx"]);
fxCompressor defSynthX("fxCompressor", ["fx"]);
fxLimiter defSynthX("fxLimiter", ["fx"]);
fxExpander defSynthX("fxExpander", ["fx"]);
fxNoiseGate defSynthX("fxNoiseGate", ["fx"]);
fxBooster defSynthX("fxBooster", ["fx"]);
fxSwell defSynthX("fxSwell", ["fx"]);
fxDistortion defSynthX("fxDistortion", ["fx"]);
fxLoFi defSynthX("fxLoFi", ["fx"]);
fxFilter defSynthX("fxFilter", ["fx"]);
fxPitchShift defSynthX("fxPitchShift", ["fx"]);
fxGranulator defSynthX("fxGranulator", ["fx"]);
fxFormant defSynthX("fxFormant", ["fx"]);

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
