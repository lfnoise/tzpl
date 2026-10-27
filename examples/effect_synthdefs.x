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

fxTremolo defSynth("fxTremolo", ["fx"]) await;
fxVibrato defSynth("fxVibrato", ["fx"]) await;
fxFlanger defSynth("fxFlanger", ["fx"]) await;
fxChorus defSynth("fxChorus", ["fx"]) await;
fxPhaser defSynth("fxPhaser", ["fx"]) await;
fxWah defSynth("fxWah", ["fx"]) await;
fxAutoWah defSynth("fxAutoWah", ["fx"]) await;
fxRotary defSynth("fxRotary", ["fx"]) await;
fxEcho defSynth("fxEcho", ["fx"]) await;
fxPingPong defSynth("fxPingPong", ["fx"]) await;
fxMultiTap defSynth("fxMultiTap", ["fx"]) await;
fxMatrixDelay defSynth("fxMatrixDelay", ["fx"]) await;
fxDiffuser defSynth("fxDiffuser", ["fx"]) await;
fxReverb defSynth("fxReverb", ["fx"]) await;
fxSympathetic defSynth("fxSympathetic", ["fx"]) await;
fxCompressor defSynth("fxCompressor", ["fx"]) await;
fxLimiter defSynth("fxLimiter", ["fx"]) await;
fxExpander defSynth("fxExpander", ["fx"]) await;
fxNoiseGate defSynth("fxNoiseGate", ["fx"]) await;
fxBooster defSynth("fxBooster", ["fx"]) await;
fxSwell defSynth("fxSwell", ["fx"]) await;
fxDistortion defSynth("fxDistortion", ["fx"]) await;
fxLoFi defSynth("fxLoFi", ["fx"]) await;
fxFilter defSynth("fxFilter", ["fx"]) await;
fxPitchShift defSynth("fxPitchShift", ["fx"]) await;
fxGranulator defSynth("fxGranulator", ["fx"]) await;
fxFormant defSynth("fxFormant", ["fx"]) await;

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
