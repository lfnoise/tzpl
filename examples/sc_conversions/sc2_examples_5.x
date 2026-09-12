-- Tzopilotl translations of SuperCollider examples from
-- sc-examples/SC2-examples/examples-5.txt: all of them process the AUDIO
-- INPUT (AudioIn.ar([1,2])). Shared machinery lives in sc2_common.x.
--
-- Every example is written as a function of its source signal and is
-- registered twice: `<name>Live` feeds it the stereo audio input
-- (inlet(FLOAT32, 2) -- run with an input device, a CD or a mic), and
-- `<name>Demo` feeds it a synthetic rhythmic "CD-like" signal so the
-- processing can be heard and rendered headless (renderSc5 plays the demos).
-- Mouse-driven parameters read the shared-input mouse; the driver parks it
-- mid-screen for headless renders.
--
-- Status (examples-5.txt blocks, in order):
--   1  input thru                 -- DONE (inputThru)
--   2  distort input              -- DONE (distortInput)
--   3  ring modulate input        -- DONE (ringModInput)
--   4  ring modulate using ring1  -- DONE (ring1Input)
--   5  filter the input           -- DONE (filterInput)
--   6  input limiter              -- DONE (inputLimiter) -- local compander
--   7  input noise gate           -- DONE (inputNoiseGate)
--   8  pitch shift input          -- DONE (pitchShiftInput) -- ratio -> semitones;
--                                    the 4 ms time dispersion is dropped
--   9  PitchShift granulate       -- DONE (granulateInput) -- granulator;
--                                    time dispersion only (no pitch dispersion)
--   10 echo input                 -- DONE (echoInput)
--   11 ring modulate & echo       -- DONE (ringModEcho)
--   12 ring mod & resonant filter -- DONE (ringModFiltered)
--   13 distort, ring mod & echo   -- DONE (noiseFest)
--   14 loop recorder              -- DONE (loopRecorder)
--   15 sweep verb                 -- DONE (sweepVerb)
--   16 monastic resonance         -- DONE (monasticResonance)

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import filters.*;
import effects.*;
import audio_engine as ae;
import clock.*;
import sc2_common.*;

let tags = ["sc2-examples-5"];
let liveTags = ["sc2-examples-5", "live-input"];

---------------------------------------------------------------------------
-- Sources

-- AudioIn.ar([1,2], gain)
fn liveIn(gain AsSignal) S = inlet(FLOAT32, 2) * gain;

-- A stand-in for "a CD playing": an 8-note sawtooth riff with a sine bass
-- an octave down and a noise hi-hat, in stereo.
fn demoIn(gain AsSignal) S {
	let clock = lfimp(2);
	let freq = seq(clock, [48, 55, 60, 63, 67, 62, 58, 53] vec, 8) nnhz;
	let tone = (freq lfsaw) * (clock decay(0.35)) * 0.4;
	let bass = ((freq * 0.5) sinosc) * (clock decay(0.5)) * 0.4;
	let hat = white(2) * (lfimp(8) decay(0.03)) * 0.12;
	(([tone + bass, tone * 0.7 + bass] join) + hat) * gain
}

---------------------------------------------------------------------------
-- Helpers

-- SC's Compander(in, control, thresh, slopeBelow, slopeAbove): the control
-- signal's amplitude envelope sets a gain of (env / thresh) ^ (slope - 1),
-- with slopeAbove when the envelope is over the threshold, slopeBelow under.
-- CompanderD is the same with the input as its own control (as here).
fn compander(x S, ctl S, thresh, slopeBelow, slopeAbove) S {
	let env = (ctl envfollow(0.01, 0.1)) max(1e-6);
	let rel = env / thresh;
	x * select2(env > thresh, rel pow(slopeAbove - 1), rel pow(slopeBelow - 1))
}

-- PitchShift's pitch ratio -> pitchShift's semitones.
fn ratioToSemitones(r) = 12 * ((r max(0.01)) log) * 1.4426950408889634;

-- SinOsc.ar(freq, [0, 0.5pi]): a stereo pair of modulators in quadrature.
fn quadMod(freq S) S = freq sinosc([0.0, 0.25] vec);

-- CombL(in, 0.5, delay, 4, 1, in): echo mixed with the input.
fn echoMix(in S, delayTime S) S = (in combl(delayTime, 0.5, 4)) + in;

---------------------------------------------------------------------------
-- 1. input thru (examples-5.txt:12-15)
fn inputThru(src S) S = src gainOutlet;

-- 2. distort input (examples-5.txt:19-25): gain MouseX 1..100 (exp) into
--    distort, * 0.4.
fn distortInput(src S) S = ((src * mouseXExp(1, 100)) distort * 0.4) gainOutlet;

-- 3. ring modulate input (examples-5.txt:29-39): by a quadrature SinOsc pair
--    at MouseX 10..4000 Hz (exp).
fn ringModInput(src S) S = (src * quadMod(mouseXExp(10, 4000))) gainOutlet;

-- 4. ring modulate using ring1 (examples-5.txt:43-53): input * mod + input.
fn ring1Input(src S) S = ((src * 0.5) ring1(quadMod(mouseXExp(10, 4000)))) gainOutlet;

-- 5. filter the input (examples-5.txt:57-67): RLPF, cutoff MouseX 100..12000
--    (exp), rQ MouseY 0.01..1 (exp), input attenuated by 0.4 * sqrt(rQ).
fn filterInput(src S) S {
	let rQ = mouseYExp(0.01, 1);
	((src * 0.4 * (rQ sqrt)) rlpf(mouseXExp(100, 12000), rQ)) gainOutlet
}

-- 6. input limiter (examples-5.txt:71-82): CompanderD, threshold MouseX
--    0.01..0.5, slope 1 below / 0.1 above.
fn inputLimiter(src S) S = compander(src, src, mouseX(0.01, 0.5), 1.0, 0.1) gainOutlet;

-- 7. input noise gate (examples-5.txt:86-99): slope 10 below / 1 above.
fn inputNoiseGate(src S) S = compander(src, src, mouseX(0.01, 0.5), 10.0, 1.0) gainOutlet;

-- 8. pitch shift input (examples-5.txt:103-113): 40 ms grains, ratio MouseX
--    0..2. pitchShift takes semitones and a fixed window; the 4 ms time
--    dispersion has no equivalent here.
fn pitchShiftInput(src S) S =
	((src * 0.5) pitchShift(ratioToSemitones(mouseX(0, 2)), 0.04)) gainOutlet;

-- 9. use PitchShift to granulate input (examples-5.txt:117-130): 0.1 s
--    grains at unity pitch, time dispersion MouseY 0..grainSize. granulator
--    reads grains at random offsets up to `spread` back, which is the time
--    dispersion; the pitch dispersion (MouseX) is not modelled.
fn granulateInput(src S) S =
	((src * 0.5) granulator(10.0, mouseY(0, 0.1), 0.0, 1.0, 0.1)) gainOutlet;

-- 10. echo input (examples-5.txt:134-147): CombL 0.5 s max, delay MouseX
--     0..0.5, 4 s decay, mixed with the input.
fn echoInput(src S) S = echoMix(src * 0.1, mouseX(0, 0.5)) gainOutlet;

-- 11. ring modulate & echo input (examples-5.txt:151-164): modulator MouseX
--     10..2000 (exp), delay MouseY 0..0.5.
fn ringModEcho(src S) S =
	echoMix((src * 0.4) * quadMod(mouseXExp(10, 2000)), mouseY(0, 0.5)) gainOutlet;

-- 12. ring modulated and resonant filtered input (examples-5.txt:168-181):
--     RLPF cutoff MouseY 100..12000 (exp), rQ 0.1.
fn ringModFiltered(src S) S =
	(((src * 0.2) * quadMod(mouseXExp(10, 4000))) rlpf(mouseYExp(100, 12000), 0.1)) gainOutlet;

-- 13. distort, ring modulate & echo input (examples-5.txt:185-200): gain 20
--     into distort, ring1 by the modulator, * 0.02, echoed.
fn noiseFest(src S) S {
	let in = (((src * 20) distort) ring1(quadMod(mouseXExp(10, 2000)))) * 0.02;
	echoMix(in, mouseY(0, 0.5)) gainOutlet
}

-- 14. loop recorder (examples-5.txt:206-229): a 1 s stereo delay loop; while
--     the input's amplitude (Amplitude.kr -> envfollow) is over 0.05 the new
--     input is written, otherwise the loop's previous contents recycle --
--     with a 0.1 s lag on the switch so the two crossfade.
fn loopRecorder(src S) S {
	let maxCycleTime = 1.0;
	let d = delayVar(maxCycleTime * fs() + 64);
	let recycle = d(maxCycleTime * fs(), Interpolation.none);
	let inputAmp = src envfollow(0.01, 0.01);
	let sel = (inputAmp > 0.05) lag(0.1);
	let out = src * sel + recycle * (1 - sel);
	d <- out;
	out gainOutlet
}

-- 15. sweep verb (examples-5.txt:233-252): input * 0.01 summed to mono, 48 ms
--     predelay, six modulated combs + four stereo allpasses, LeakDC.
fn sweepVerb(src S) S {
	let z = ((src * 0.01) sum) delayn(0.048, 0.048);
	(reverbTail(z, 6) leakdc(0.995)) gainOutlet
}

-- 16. monastic resonance (examples-5.txt:257-280): decay time MouseX 0..16 s,
--     delay scale MouseY 0.01..1 applied to eight random comb delays
--     (0.05 +/- 0.04) * scale; five stereo allpasses; LeakDC.
fn monasticResonance(src S) S {
	let decayTime = mouseX(0, 16);
	let delayScale = mouseY(0.01, 1);
	let z = ((src * 0.005) sum) delayn(0.048, 0.048);
	let dts = (0.05 + birand(8, Rate.init) * 0.04) * delayScale;
	var y = (z combl(dts, 0.1, decayTime)) sum;
	for (i : (1..5)) {
		y = y alpasn(urand(2, Rate.init) * 0.05, 1);
	}
	(y leakdc(0.995)) gainOutlet
}

---------------------------------------------------------------------------
-- registration: live-input and demo variants of each

fn inputThruLive() S = inputThru(liveIn(1.0));
fn inputThruDemo() S = inputThru(demoIn(1.0));
fn distortInputLive() S = distortInput(liveIn(1.0));
fn distortInputDemo() S = distortInput(demoIn(1.0));
fn ringModInputLive() S = ringModInput(liveIn(1.0));
fn ringModInputDemo() S = ringModInput(demoIn(1.0));
fn ring1InputLive() S = ring1Input(liveIn(1.0));
fn ring1InputDemo() S = ring1Input(demoIn(1.0));
fn filterInputLive() S = filterInput(liveIn(1.0));
fn filterInputDemo() S = filterInput(demoIn(1.0));
fn inputLimiterLive() S = inputLimiter(liveIn(1.0));
fn inputLimiterDemo() S = inputLimiter(demoIn(1.0));
fn inputNoiseGateLive() S = inputNoiseGate(liveIn(1.0));
fn inputNoiseGateDemo() S = inputNoiseGate(demoIn(1.0));
fn pitchShiftInputLive() S = pitchShiftInput(liveIn(1.0));
fn pitchShiftInputDemo() S = pitchShiftInput(demoIn(1.0));
fn granulateInputLive() S = granulateInput(liveIn(1.0));
fn granulateInputDemo() S = granulateInput(demoIn(1.0));
fn echoInputLive() S = echoInput(liveIn(1.0));
fn echoInputDemo() S = echoInput(demoIn(1.0));
fn ringModEchoLive() S = ringModEcho(liveIn(1.0));
fn ringModEchoDemo() S = ringModEcho(demoIn(1.0));
fn ringModFilteredLive() S = ringModFiltered(liveIn(1.0));
fn ringModFilteredDemo() S = ringModFiltered(demoIn(1.0));
fn noiseFestLive() S = noiseFest(liveIn(1.0));
fn noiseFestDemo() S = noiseFest(demoIn(1.0));
fn loopRecorderLive() S = loopRecorder(liveIn(1.0));
fn loopRecorderDemo() S = loopRecorder(demoIn(1.0));
fn sweepVerbLive() S = sweepVerb(liveIn(1.0));
fn sweepVerbDemo() S = sweepVerb(demoIn(1.0));
fn monasticResonanceLive() S = monasticResonance(liveIn(1.0));
fn monasticResonanceDemo() S = monasticResonance(demoIn(1.0));

inputThruLive defSynthX("inputThruLive", liveTags) await;
inputThruDemo defSynthX("inputThruDemo", tags) await;
distortInputLive defSynthX("distortInputLive", liveTags) await;
distortInputDemo defSynthX("distortInputDemo", tags) await;
ringModInputLive defSynthX("ringModInputLive", liveTags) await;
ringModInputDemo defSynthX("ringModInputDemo", tags) await;
ring1InputLive defSynthX("ring1InputLive", liveTags) await;
ring1InputDemo defSynthX("ring1InputDemo", tags) await;
filterInputLive defSynthX("filterInputLive", liveTags) await;
filterInputDemo defSynthX("filterInputDemo", tags) await;
inputLimiterLive defSynthX("inputLimiterLive", liveTags) await;
inputLimiterDemo defSynthX("inputLimiterDemo", tags) await;
inputNoiseGateLive defSynthX("inputNoiseGateLive", liveTags) await;
inputNoiseGateDemo defSynthX("inputNoiseGateDemo", tags) await;
pitchShiftInputLive defSynthX("pitchShiftInputLive", liveTags) await;
pitchShiftInputDemo defSynthX("pitchShiftInputDemo", tags) await;
granulateInputLive defSynthX("granulateInputLive", liveTags) await;
granulateInputDemo defSynthX("granulateInputDemo", tags) await;
echoInputLive defSynthX("echoInputLive", liveTags) await;
echoInputDemo defSynthX("echoInputDemo", tags) await;
ringModEchoLive defSynthX("ringModEchoLive", liveTags) await;
ringModEchoDemo defSynthX("ringModEchoDemo", tags) await;
ringModFilteredLive defSynthX("ringModFilteredLive", liveTags) await;
ringModFilteredDemo defSynthX("ringModFilteredDemo", tags) await;
noiseFestLive defSynthX("noiseFestLive", liveTags) await;
noiseFestDemo defSynthX("noiseFestDemo", tags) await;
loopRecorderLive defSynthX("loopRecorderLive", liveTags) await;
loopRecorderDemo defSynthX("loopRecorderDemo", tags) await;
sweepVerbLive defSynthX("sweepVerbLive", liveTags) await;
sweepVerbDemo defSynthX("sweepVerbDemo", tags) await;
monasticResonanceLive defSynthX("monasticResonanceLive", liveTags) await;
monasticResonanceDemo defSynthX("monasticResonanceDemo", tags) await;

---------------------------------------------------------------------------
-- driver (plays the demo variants)

fn playAllSc5() {
	go(coro fn() Float {
		"start playing SC2 examples-5 (demo sources)" println;
		ae.setSharedInput(0, 0.5);
		ae.setSharedInput(1, 0.5);

		"inputThruDemo" playFor(6.0) yieldAll;
		"distortInputDemo" playFor(6.0) yieldAll;
		"ringModInputDemo" playFor(6.0) yieldAll;
		"ring1InputDemo" playFor(6.0) yieldAll;
		"filterInputDemo" playFor(6.0) yieldAll;
		"inputLimiterDemo" playFor(6.0) yieldAll;
		"inputNoiseGateDemo" playFor(6.0) yieldAll;
		"pitchShiftInputDemo" playFor(6.0) yieldAll;
		"granulateInputDemo" playFor(6.0) yieldAll;
		"echoInputDemo" playFor(8.0) yieldAll;
		"ringModEchoDemo" playFor(8.0) yieldAll;
		"ringModFilteredDemo" playFor(6.0) yieldAll;
		"noiseFestDemo" playFor(8.0) yieldAll;
		"loopRecorderDemo" playFor(8.0) yieldAll;
		"sweepVerbDemo" playFor(8.0) yieldAll;
		"monasticResonanceDemo" playFor(10.0) yieldAll;

		ae.endRender(0.1);
		"done playing SC2 examples-5" println;
	}());
}

fn renderSc5() {
	let h = "/tmp/sc2_examples_5.wav" ae.renderNRT(200, playAllSc5);
	await ae.renderDone(h);
	"render done" println;
}

--renderSc5();
--playAllSc5();
