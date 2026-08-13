-- samplebank end-to-end (sampler half): a voicer sampler whose per-note bank
-- lookup selects a sample by (pitch, velocity) and plays it back at a rate
-- derived from the resolved sample's rootKey and source sample rate. Driven
-- by run_samplebank_test.sh, which renders it at SB_PITCH=60 (low zone,
-- rootKey 60 -> rate 1, must equal the 440 Hz fixture) and SB_PITCH=72
-- (high zone, rootKey 72 -> rate 1, must equal the 660 Hz fixture).
import synthdef.*;
import common_ugens.*;
import synthc.compile.*;
import audio_engine.*;

fn sampler() S {
	let bank = sampleBankVar();
	voicer(8, fn() S {
		let g = gate();
		let pitch = noteParam("pitch", ControlSpec { lo: 0.0, hi: 127.0, init: 60.0, warp: ControlWarp.linear });
		let vel = noteParam("vel", ControlSpec { lo: 0.0, hi: 127.0, init: 100.0, warp: ControlWarp.linear });
		let h = bank lookup(pitch, vel);
		-- Playback rate: semitone shift from the sample's root key, corrected
		-- for the sample's source rate vs the engine rate (sd = 1/fs).
		let rate = ((pitch f64 - (h rootKey) f64) / 12.0) exp2 * (h sampleRate) * (T() f64);
		-- Per-voice playhead (zeroed at note-on).
		let d = delayVar();
		let pos = d read(1);
		(pos + rate) -> d;
		h vread(pos, Interpolation.cubic) f32 * g
	}) sum(1) outlet
}

defSynthX(sampler, "sbSampler");

let pitch = match (getEnv("SB_PITCH")) {
	some(s): s parseInt unwrap toFloat;
	none: 60.0;
};
let vel = match (getEnv("SB_VEL")) {
	some(s): s parseInt unwrap toFloat;
	none: 100.0;
};

-- Zone layout: "pitch" splits the keyboard into two pitch ranges;
-- "vel" layers the SAME full pitch range into two velocity bands (tiles
-- may share a pitch range as long as their velocity ranges are disjoint).
let velLayout = match (getEnv("SB_LAYOUT")) {
	some(s): s == "vel";
	none: false;
};

begin();
newNode("sbSampler", 100);
if (velLayout) {
	loadSampleBank(100, 0, [
		sampleZone("/tmp/sb_fix_lo.wav", 0, 127, 0, 63, 60),
		sampleZone("/tmp/sb_fix_hi.wav", 0, 127, 64, 127, 60),
	]);
} else {
	loadSampleBank(100, 0, [
		sampleZone("/tmp/sb_fix_lo.wav", 0, 65, 0, 127, 60),
		sampleZone("/tmp/sb_fix_hi.wav", 66, 127, 0, 127, 72),
	]);
}
connect(100, 0, 0, 0);
noteOn(100, 1, [pitch, vel]);
sched(0);
