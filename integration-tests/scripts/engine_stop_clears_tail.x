-- engineStop clears captured audio state: a feedback echo is fed a short
-- burst, rings, and the engine is stopped and restarted. Before the fix the
-- tail froze in the delay line and resumed playing on engineStart; now
-- stopAudio resets every node (generated plugins re-init, preserving
-- engine-installed buffers/banks), so the restart begins from silence.
-- Runs live audio (quiet 440 Hz burst); see run_engine_stop_tail_test.sh.
import audio_engine.*;
import synthdef.*;
import common_ugens.*;
import effects.*;
import clock.*;

-- Control-gated sine into a long feedback echo (0.4s delay, 0.9 feedback,
-- wet-only), so the tail rings for many seconds after the burst ends.
fn tailVoice() S {
	let amp = control("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.0, warp: ControlWarp.linear });
	(sinosc(440.0) * amp) echo(0.4, 0.9, 1.0, 1.0) outlet
}

tailVoice defSynth("tailtest") await;

begin();
newNode("tailtest", 100);
connect(100, 0, 0, 0);
"build err: %^ (expect 0)" fmt(go(0)) println;

let t = allocTapID();
begin();
tapMaster(t, TapMode.tapMeter);
"tap err: %^ (expect 0)" fmt(go(0)) println;

-- Fire a quiet burst as long as the delay time so the echoes overlap into a
-- continuous tail (a shorter burst rings as a pulse train with silent gaps,
-- and a single meter window could land in a gap).
begin(); setControl(100, 0, 0.2); sched(0);
await delayReal(0.4);
begin(); setControl(100, 0, 0.0); sched(0);
await delayReal(0.6);

let ringing = tapPeak(t);
"peak while ringing: %^" fmt(ringing) println;

-- No wait between stop and start: delayReal is slaved to the engine clock,
-- which halts while audio is stopped (stopAudio itself holds a 100ms mute
-- window, and the cleared state does not depend on how long we stay stopped).
engineStop();
engineStart();
-- Take the max over a second of meter windows: without the fix the tail
-- resumes and shows up in at least one window; with it every window is 0.
var after = 0.0;
var i = 0;
while (i < 5) {
	await delayReal(0.2);
	after = max(after, tapPeak(t));
	i = i + 1;
}
"max peak after stop/start: %^" fmt(after) println;

if (ringing > 0.01 && after < 0.001) { "TAIL CLEAR PASS" println; }
else { "TAIL CLEAR FAIL (ringing %^, after %^)" fmt(ringing, after) println; }
engineStop();
