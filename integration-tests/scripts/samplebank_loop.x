-- samplebank loop end-to-end (sampler half): a voicer sampler whose playhead
-- is a loopPhasor over the resolved sample's sustain loop. Driven by
-- run_samplebank_loop_test.sh, which loads fixtures whose loop points and
-- root key come from the WAV smpl chunk (or explicit sampleZone overrides)
-- and asserts the rendered audio stays phase-locked to an analytic sine
-- across many fractional-length loop wraps.
--
-- Env:  SBL_WAV    fixture path            (default /tmp/sbl_fix.wav)
--       SBL_PITCH  note pitch              (default 60.0)
--       SBL_SUS    loopPhasor sustain      (default 1.0; 0.0 = run past loop)
--       SBL_LS/LE  explicit zone loop points (default -1.0 = from the file)
import synthdef.*;
import common_ugens.*;
import synthc.compile.*;
import audio_engine.*;

fn loopSampler() S {
	let bank = sampleBankVar();
	voicer(8, fn() S {
		let g = gate();
		let pitch = noteParam("pitch", ControlSpec { lo: 0.0, hi: 127.0, init: 60.0, warp: ControlWarp.linear });
		let vel = noteParam("vel", ControlSpec { lo: 0.0, hi: 127.0, init: 100.0, warp: ControlWarp.linear });
		let sus = noteParam("sus", ControlSpec { lo: 0.0, hi: 1.0, init: 1.0, warp: ControlWarp.linear });
		let h = bank lookup(pitch, vel);
		let rate = ((pitch f64 - (h rootKey) f64) / 12.0) exp2 * (h sampleRate) * (T() f64);
		-- The note is never released in this test, so g stays 1; sus alone
		-- decides whether the playhead keeps looping or runs out.
		h vread(loopPhasor(h, rate, sus), Interpolation.cubic) f32 * g
	}) sum(1) outlet
}

defSynthX(loopSampler, "sblSampler");

fn envFloat(name String, dflt Float) Float {
	match (getEnv(name)) {
		some(s): s parseFloat unwrap;
		none: dflt;
	}
}

let wav = match (getEnv("SBL_WAV")) { some(s): s; none: "/tmp/sbl_fix.wav"; };
let pitch = envFloat("SBL_PITCH", 60.0);
let sus = envFloat("SBL_SUS", 1.0);
let ls = envFloat("SBL_LS", -1.0);
let le = envFloat("SBL_LE", -1.0);

begin();
newNode("sblSampler", 100);
-- rootKey -1: comes from the fixture's smpl chunk. loKey 0 makes the
-- no-metadata fallback (rootKey = loKey = 0) render at a wildly wrong rate,
-- so a broken chunk read cannot silently pass.
loadSampleBank(100, 0, [sampleZone(wav, 0, 127, 0, 127, -1, ls, le)]);
connect(100, 0, 0, 0);
noteOn(100, 1, [pitch, 100.0, sus]);
sched(0);
