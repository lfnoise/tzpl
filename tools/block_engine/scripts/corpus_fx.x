-- Renders one effect from lang/modules/effects.x for the block-engine
-- reference corpus (devplans/BLOCK_ENGINE_PLAN.md, M0).
--   CORPUS_ITEM  the effect's synthdef function, e.g. fxEcho
-- A stereo test source (decaying noise and saw hits, a saw chord, a sine
-- sweep) feeds the effect at its default control values. The safety limiter
-- is off so a NaN or a blow-up shows in the render instead of being zapped.
import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import effects.*;
import audio_engine.*;

fn envOr(name String, dflt String) String = match (getEnv(name)) {
	Option.some(s): s;
	Option.none: dflt;
};

fn corpusSrc() S {
	let tone = white(2) * 0.5 + [220.0, 331.0] lfsaw * 0.5;
	let hits = [2.0, 3.0] lfsaw tr decay2(0.002, 0.25) * tone;
	-- The fourth, 0 Hz voice is the zero channel the per-sample compiler
	-- padded this 3-voice chord to (power-of-two channel counts); the M0
	-- references were rendered with it, so it is spelled out here.
	let chord = ([110.0, 165.0, 220.5, 0.0] lfsaw * 0.08) sum;
	let sweep = 0.25 sinosc * 600.0 + 900.0 |> sinosc * 0.1;
	hits * 0.6 + chord + sweep |> outlet
}

fn fxGraph(name String) () S = match (name) {
	"fxTremolo": fxTremolo;
	"fxVibrato": fxVibrato;
	"fxFlanger": fxFlanger;
	"fxChorus": fxChorus;
	"fxPhaser": fxPhaser;
	"fxWah": fxWah;
	"fxAutoWah": fxAutoWah;
	"fxRotary": fxRotary;
	"fxEcho": fxEcho;
	"fxPingPong": fxPingPong;
	"fxMultiTap": fxMultiTap;
	"fxMatrixDelay": fxMatrixDelay;
	"fxDiffuser": fxDiffuser;
	"fxReverb": fxReverb;
	"fxSympathetic": fxSympathetic;
	"fxCompressor": fxCompressor;
	"fxLimiter": fxLimiter;
	"fxExpander": fxExpander;
	"fxNoiseGate": fxNoiseGate;
	"fxBooster": fxBooster;
	"fxSwell": fxSwell;
	"fxDistortion": fxDistortion;
	"fxLoFi": fxLoFi;
	"fxFilter": fxFilter;
	"fxPitchShift": fxPitchShift;
	"fxGranulator": fxGranulator;
	"fxFormant": fxFormant;
	_: {
		panic("corpus_fx: unknown effect " $ name);
		fxTremolo
	}
};

let name = envOr("CORPUS_ITEM", "fxEcho");
corpusSrc defSynthX("corpusSrc") await;
fxGraph(name) defSynthX(name) await;

safetyLimiter(false);
begin();
newNode("corpusSrc", 100);
newNode(name, 101);
connect(100, 0, 101, 0);
connect(101, 0, 0, 0);
sched(0);
