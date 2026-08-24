-- samplebank end-to-end, NON-FLAT (AoS) half: the same voicer sampler as
-- samplebank_sampler.x but with the playback wrapped in pause(gate) -- the
-- control flow forces AoS mode, exercising the VoiceState bank lookup slots
-- (vs.luN_*), the per-voice resolver, and the AoS per-voice playhead delay.
-- Driven by run_nonflat_voicer_test.sh at SB_PITCH=60/72; at the zone's root
-- key the playback rate is exactly 1, so each render must reproduce its
-- fixture sample-for-sample (onset-aligned).
import synthdef.*;
import common_ugens.*;
import synthc.compile.*;
import audio_engine.*;

fn samplerAoS() S {
	let bank = sampleBankVar();
	voicer(8, fn() S {
		let g = gate();
		let pitch = noteParam("pitch", ControlSpec { lo: 0.0, hi: 127.0, init: 60.0, warp: ControlWarp.linear });
		let vel = noteParam("vel", ControlSpec { lo: 0.0, hi: 127.0, init: 100.0, warp: ControlWarp.linear });
		let h = bank lookup(pitch, vel);
		let rate = ((pitch f64 - (h rootKey) f64) / 12.0) exp2 * (h sampleRate) * (T() f64);
		let d = delayVar();
		let pos = d read(1);
		(pos + rate) -> d;
		g pause(fn() S { h vread(pos, Interpolation.cubic) f32 })
	}) sum(1) outlet
}

defSynthX(samplerAoS, "sbSamplerAoS");

let pitch = match (getEnv("SB_PITCH")) {
	some(s): s parseInt unwrap toFloat;
	none: 60.0;
};

begin();
newNode("sbSamplerAoS", 100);
loadSampleBank(100, 0, [
	sampleZone("/tmp/sb_fix_lo.wav", 0, 65, 0, 127, 60),
	sampleZone("/tmp/sb_fix_hi.wav", 66, 127, 0, 127, 72),
]);
connect(100, 0, 0, 0);
noteOn(100, 1, [pitch, 100.0]);
sched(0);
