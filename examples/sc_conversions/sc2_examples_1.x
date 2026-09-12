-- Tzopilotl translations of SuperCollider examples from
-- sc-examples/SC2-examples/examples-1.txt.
--
-- SC's `Spawn`/`OverlapTexture`/`XFadeTexture` re-invoke a graph *function*
-- per event and can vary its topology each time; Tzopilotl compiles one
-- fixed graph per synthdef. Where the original spawns overlapping copies of
-- a graph with fresh per-event randomization, we use one voicer whose voice
-- body rolls its random values once per note-on (`Rate.reset`), driven by a
-- script that spawns overlapping notes on a timer -- see `hellIsBusy` below
-- for the template; later sections reuse it.
--
-- Status (examples-1.txt numbering):
--   1  analog bubbles       -- already ported as `bubbles` in
--                              examples/example_synthdefs.x:28 (cross-ref only)
--   2  LFO/pulse/RLPF       -- DONE (lfoPulseRlpf)
--   3  hell is busy         -- DONE (hellIsBusy) -- voicer+spawn template
--   4  pond life             -- DONE (pondLife)
--   5  alien froggies        -- DONE (alienFroggies) -- SC2 Formant is an
--                              oscillator (sc2Formant); rate is script state
--   6  random sine waves     -- DONE (randomSineWaves)
--   7  random pulsations     -- DONE (randomPulsations) -- amclip approx,
--                              unverified formula, see its comment
--   8  moto rev              -- DONE (motoRev)
--   9  scratchy              -- DONE (scratchy)
--   10 tremulate             -- DONE (tremulate) -- XFadeTexture == small-n
--                              OverlapTexture, reuses overlapTextureSpawner
--   11 reso-pulse            -- DONE (resoPulse) -- new delayn/delayc ugens
--   12 sprinkler             -- DONE (sprinkler, sprinklerMouse) -- BPZ2 is
--                              exactly (x - x.z2) * 0.5, see local `bpz2`
--   13 harmonic swimming     -- DONE (harmonicSwimming)
--   14 harmonic tumbling     -- DONE (harmonicTumbling)

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import filters.*;
import audio_engine as ae;
import clock.*;
import sc2_common.*;   -- gainOutlet, the texture spawn loops, Klank & co.

let tags = ["sc2-examples-1"];

---------------------------------------------------------------------------
-- 2. LFO modulation of Pulse waves and resonant filters (examples-1.txt:28-35)
-- SC: CombL.ar(RLPF.ar(LFPulse.ar(FSinOsc.kr(0.05,80,160),0.4,0.05),
--         FSinOsc.kr([0.6,0.7],3600,4000), 0.2), 0.3, [0.2,0.25], 2)
-- SC2's LFPulse.ar/.kr was (freq, width, mul, add) -- no iphase argument at
-- all (confirmed by the author). So the two extra positional args here are
-- width=0.4 and mul=0.05, not a width/phase pair.

fn lfoPulseRlpf() S {
	let pulseFreq = 0.05 fsinosc * 80 + 160;
	let pulse = (pulseFreq lfupulse(0.4)) * 0.05;
	let cutoff = [0.6, 0.7] fsinosc * 3600 + 4000;
	(pulse rlpf(cutoff, 0.2)) combl([0.2, 0.25], 0.3, 2) gainOutlet
}

lfoPulseRlpf defSynthX("lfoPulseRlpf", tags) await;

---------------------------------------------------------------------------
-- The texture spawn loop shared by every OverlapTexture/XFadeTexture/Spawn
-- example (#3-#7, #10, #11) is `overlapTextureSpawner` in sc2_common.x,
-- which also documents the timing model (sustain / transition / overlap ->
-- interval, hold, voice count) these examples' constants are derived from.

---------------------------------------------------------------------------
-- 3. hell is busy (examples-1.txt:39-49)
-- SC: OverlapTexture.ar({ Pan2.ar(FSinOsc.ar(400+2000.0.rand,
--         LFPulse.kr(1+10.0.rand, 0.7.rand, 0.04)), 1.0.rand2) }, 4, 4, 8, 2)
--
-- FSinOsc.ar(freq, mul, add) -- the LFPulse is the CARRIER's amplitude
-- (mul), not a phase modulator; and LFPulse.kr(freq, width, mul, add) has
-- no separate phase argument, so its 3rd positional (0.04) is amplitude
-- scale, and 0.7.rand is width, not phase. Both point the same way: this
-- has to be `carrier * pulse * env`, not `fsinosc(pulse)` (which would
-- wrongly use pulse as a phase-mod input).

-- OverlapTexture(.., 4, 4, 8, 2): sustain 4, transition 4, 8 overlapping.
const kHellTrans = 4.0;
const kHellHold = 8.0;       -- sustain + transition
const kHellInterval = 1.0;   -- (sustain + transition) / overlap
const kHellVoices = 12;      -- ceil(life 12 / interval 1)

fn hellIsBusyVoice() S {
	let g = gate();
	let freq = frand(400, 2400, 1, Rate.reset);
	let pulseRate = frand(1, 11, 1, Rate.reset);
	let pulseWidth = frand(0, 0.7, 1, Rate.reset);
	let panPos = birand(1, Rate.reset);
	let pulse = (pulseRate lfupulse(pulseWidth)) * 0.04;
	let env = linen(g, kHellTrans, kHellTrans) rpanfun;
	((freq fsinosc) * pulse * env) pan(panPos) join
}

-- voicer() returns one channel PER VOICE (voice-major: v0L, v0R, v1L, ...),
-- so a stereo voice body needs `sum(2)` -- whose cyclic grouping yields
-- all-L / all-R -- and a mono body needs `sum`. Forgetting this leaves an
-- N*chans-channel outlet of which only the first two are ever heard.
fn hellIsBusy() S = voicer(kHellVoices, hellIsBusyVoice) sum(2) gainOutlet;

hellIsBusy defSynthX("hellIsBusy", tags) await;

---------------------------------------------------------------------------
-- 4. pond life (examples-1.txt:53-64)
-- SC: OverlapTexture.ar({ Pan2.ar(SinOsc.ar(FSinOsc.kr(20+30.0.rand,
--     100+300.0.rand, 500+2000.0.linrand), 0, LFPulse.kr(3/(1+8.0.rand),
--     0.2+0.3.rand, 0.04)), 1.0.rand2) }, 8, 4, 8, 2)
--
-- Same voicer+spawn shape as #3, but the carrier pitch is itself modulated
-- by a continuously-running FSinOsc.kr LFO (a real vibrato, not a one-shot
-- choice) whose own freq/depth/center are each rolled once per note-on.
-- `500 + 2000.0.linrand`: linrand (uniform, weighted toward its low end)
-- has no Tzopilotl equivalent at any level; approximated as `n * min(u1,u2)`,
-- the standard "min of two uniforms" construction for that skew.

-- OverlapTexture(.., 8, 4, 8, 2): sustain 8, transition 4, 8 overlapping.
const kPondTrans = 4.0;
const kPondHold = 12.0;      -- sustain + transition
const kPondInterval = 1.5;   -- (sustain + transition) / overlap
const kPondVoices = 11;      -- ceil(life 16 / interval 1.5)

fn pondLifeVoice() S {
	let g = gate();
	let lfoFreq = frand(20, 50, 1, Rate.reset);
	let lfoDepth = frand(100, 400, 1, Rate.reset);
	let lfoCenter = 500 + 2000 * min(urand(1, Rate.reset), urand(1, Rate.reset));
	let carrierFreq = ((lfoFreq fsinosc) * lfoDepth) + lfoCenter;
	let pulseRate = 3 / (1 + frand(0, 8, 1, Rate.reset));
	let pulseWidth = frand(0.2, 0.5, 1, Rate.reset);
	let amp = (pulseRate lfupulse(pulseWidth)) * 0.04;
	let panPos = birand(1, Rate.reset);
	let env = linen(g, kPondTrans, kPondTrans) rpanfun;
	((carrierFreq sinosc) * amp * env) pan(panPos) join
}

fn pondLife() S = voicer(kPondVoices, pondLifeVoice) sum(2) gainOutlet;

pondLife defSynthX("pondLife", tags) await;

---------------------------------------------------------------------------
-- 5. alien froggies (examples-1.txt:68-76)
-- SC: var rate = 11.0; OverlapTexture.ar({ arg rep;
--     rate = (rate * (0.2.bilinrand.exp)).fold(1.0, 30.0);
--     Formant.ar(rate, exprand(200,3000.0), 9.0.rand * rate + rate, 0.05)
-- }, 0.5, 0.25, 5, 1)
--
-- `rate` is genuine sequential state carried ACROSS spawns (each depends on
-- the last) -- that dependency lives in the spawning script, not the graph,
-- so it's plain host Float math there, passed in as a noteParam.
-- bilinrand (bipolar, weighted toward 0) is approximated as (u1-u2)*n, the
-- standard "difference of two uniforms" triangular-distribution trick.
-- SC2's Formant is an OSCILLATOR, not a filter (per the author): every
-- period of fundfreq emits one sine burst at formfreq under a raised-cosine
-- window one period of bwfreq wide (bwfreq >= fundfreq keeps the grain
-- inside the period, so `a` >= 1 below). The structure is the author's;
-- two details are mine and worth a glance: the window is gated on
-- `a*p < 1` (a single grain per period rather than a periodic raised
-- cosine), and the window is written out as (1 - cos)/2 to avoid any
-- doubt about how unary minus binds against a postfix `uni`.

-- OverlapTexture(.., 0.5, 0.25, 5, 1): sustain .5, transition .25, 5 overlapping.
const kFroggyTrans = 0.25;
const kFroggyHold = 0.75;      -- sustain + transition
const kFroggyInterval = 0.15;  -- (sustain + transition) / overlap
const kFroggyVoices = 7;       -- ceil(life 1.0 / interval 0.15)

fn sc2Formant(fundfreq AsSignal, formfreq AsSignal, bwfreq AsSignal) S {
	let a = bwfreq / fundfreq;
	let b = formfreq / fundfreq;
	let p = fundfreq phasor;
	let w = select2(a * p < 1, (1 - cos2pi(a * p)) * 0.5, 0);
	w * sin2pi(b * p)
}

fn alienFroggiesVoice() S {
	let g = gate();
	let rate = noteParam("rate", ControlSpec{1.0, 30.0, 11.0, ControlWarp.linear});
	let formfreq = exprand(200, 3000, 1, Rate.reset);
	let bwfreq = frand(0, 9, 1, Rate.reset) * rate + rate;
	let env = linen(g, kFroggyTrans, kFroggyTrans) rpanfun;
	sc2Formant(rate, formfreq, bwfreq) * 0.05 * env
}

fn alienFroggies() S = voicer(kFroggyVoices, alienFroggiesVoice) sum gainOutlet;

alienFroggies defSynthX("alienFroggies", tags) await;

-- Carries `rate` as running script-side state across spawns.
coro fn alienFroggiesSpawner(nodeID Int, totalDur Float) Float {
	var id = 0;
	var elapsed = 0.0;
	var rate = 11.0;
	while (elapsed < totalDur) {
		let noteID = id;
		let u1 Float = urand();
		let u2 Float = urand();
		let jitter = exp((u1 - u2) * 0.2);
		rate = max(1.0, min(30.0, rate * jitter));
		ae.begin(); ae.noteOn(nodeID, noteID, [rate, 1.0]); ae.sched(0);
		go(coro fn() Float {
			yield kFroggyHold;
			ae.begin(); ae.noteOff(nodeID, noteID); ae.sched(0);
		}());
		id = (id + 1) % kFroggyVoices;
		elapsed = elapsed + kFroggyInterval;
		yield kFroggyInterval;
	}
}

---------------------------------------------------------------------------
-- 6. random sine waves (examples-1.txt:80-91)
-- SC: n=12; env=Env.linen(2,5,2); Spawn.ar({
--     Pan2.ar(FSinOsc.ar(2000.0.rand, EnvGen.kr(env,1,0,0.02)), 1.0.rand2)
-- }, 2, 9/n)
--
-- Spawn.ar(func, numChannels, dt) spawns a fresh instance roughly every `dt`
-- seconds; each instance's whole life is governed by its own envelope
-- (2+5+2 = 9s here), not an explicit gate release -- so we drive the
-- voicer's gate high for rise + sustain (7s: linen rises while the gate is
-- high, then holds) and release it, letting linen's fall (2s) finish the
-- shape; `overlapTextureSpawner` does exactly that with hold = 7.
-- EnvGen.kr(env, mul, add, levelScale, ...) -- SC2 order (confirmed by the
-- author): here (env, mul=1, add=0, levelScale=0.02), i.e. the linen shape
-- scaled to a peak of 0.02.

-- Spawn dt = 9/12; Env.linen(2, 5, 2): life 9s, 12 voices == n.
const kSineVoices = 12;
const kSineInterval = 9.0 / 12.0;
const kSineRise = 2.0;
const kSineFall = 2.0;
const kSineHold = 7.0;       -- rise + sustain: gate stays high through both

fn randomSineWavesVoice() S {
	let g = gate();
	let freq = frand(0, 2000, 1, Rate.reset);
	let panPos = birand(1, Rate.reset);
	let env = (linen(g, kSineRise, kSineFall)) * 0.02;
	((freq fsinosc) * env) pan(panPos) join
}

fn randomSineWaves() S = voicer(kSineVoices, randomSineWavesVoice) sum(2) gainOutlet;

randomSineWaves defSynthX("randomSineWaves", tags) await;

---------------------------------------------------------------------------
-- 7. random pulsations (examples-1.txt:95-106)
-- SC: n=8; env=Env.linen(2,5,2,0.02); Spawn.ar({
--     Pan2.ar(
--         FSinOsc.ar(2000.0.rand, EnvGen.kr(env)).amclip(SinOsc.ar(8+80.0.linrand)),
--         SinOsc.kr(0.3+0.5.rand, 2pi.rand, 0.7))
-- }, 2, 9/n)
--
-- Env.linen's own 4th arg (0.02) is its peak level, so EnvGen.kr(env) with
-- all defaults already carries that scale -- same net envelope as #6.
-- amclip(a,b): SC's two-operand amplitude clip -- 0 when b<=0, else a
-- clipped to +/-b. No Tzopilotl equivalent exists; implemented locally
-- rather than added to common_ugens.x since the exact formula is from
-- memory, not verified against SC2 source the way the other fixes here were.
-- The pan position is itself a slow SinOsc LFO (not a fixed per-instance
-- value like every other example here), with its own freq/phase chosen
-- once per note-on.

-- Spawn dt = 9/8; Env.linen(2, 5, 2, 0.02): life 9s, 8 voices == n.
const kPulseVoices = 8;
const kPulseInterval = 9.0 / 8.0;
const kPulseRise = 2.0;
const kPulseFall = 2.0;
const kPulseHold = 7.0;      -- rise + sustain

fn amclip(a S, b S) S = select2(b <= 0, 0, a clip2(b));

fn randomPulsationsVoice() S {
	let g = gate();
	let freq = frand(0, 2000, 1, Rate.reset);
	let env = (linen(g, kPulseRise, kPulseFall)) * 0.02;
	let carrier = (freq fsinosc) * env;
	let modFreq = 8 + 80 * min(urand(1, Rate.reset), urand(1, Rate.reset));
	let modulator = modFreq sinosc;
	let panLfoFreq = frand(0.3, 0.8, 1, Rate.reset);
	let panLfoPhase = frand(0, 2 * pi, 1, Rate.reset);
	let panPos = (panLfoFreq sinosc(panLfoPhase)) * 0.7;
	(carrier amclip(modulator)) pan(panPos) join
}

fn randomPulsations() S = voicer(kPulseVoices, randomPulsationsVoice) sum(2) gainOutlet;

randomPulsations defSynthX("randomPulsations", tags) await;

---------------------------------------------------------------------------
-- 8. moto rev (examples-1.txt:110-114)
-- SC: RLPF.ar(LFPulse.ar(SinOsc.kr(0.2, 0, 10, 21), 0.1), 100, 0.1).clip2(0.4)
-- LFPulse.ar(freq, width, mul, add) -- one extra positional arg here (0.1)
-- is width; mul/add default to their SC defaults (1, 0), so no extra scale
-- is needed. SinOsc.kr keeps its phase arg (SC2 confirmed), here just 0.

fn motoRev() S =
	0.2 sinosc * 10 + 21
	|> lfupulse(0.1)
	rlpf(100, 0.1)
	clip2(0.4) gainOutlet;

motoRev defSynthX("motoRev", tags) await;

---------------------------------------------------------------------------
-- 9. scratchy (examples-1.txt:118-121)
-- SC: RHPF.ar(BrownNoise.ar([0.5,0.5], -0.49).max(0) * 20, 5000, 1)

-- `red` already gives two independent brown-noise channels (chans=2). Note
-- the `|>` before `rhpf`: without it, `* 20` followed by a bare `rhpf(...)`
-- on the next line parses as `20 rhpf(5000, 1)` (postfix juxtaposition binds
-- tighter than the preceding `*` across the line break), not as continuing
-- the pipeline -- a real gotcha worth documenting in the write-tzpl skill.
fn scratchy() S =
	2 red * 0.5 - 0.49
	|> max(0) * 20
	|> rhpf(5000, 1) gainOutlet;

scratchy defSynthX("scratchy", tags) await;

---------------------------------------------------------------------------
-- 10. tremulate (examples-1.txt:125-141)
-- SC: CombN.ar(XFadeTexture.ar({ arg sp;
--     f = 500+400.rand; r = 30+60.rand;
--     Mix.ar(Pan2.ar(FSinOsc.ar(f*[1.0,1.2,1.5,1.8], max(0,
--         LFNoise2.kr([r,r,r,r], 0.1))), [rand2,rand2,rand2,rand2]))
-- }, 2, 0.5, 2), 0.1, 0.1, 1)
--
-- XFadeTexture.ar(func, overlap, xfadeTime, n) is the same spawn-and-fade
-- shape as OverlapTexture -- just with few enough simultaneous voices (2)
-- that it reads as a crossfade rather than a dense overlap -- so this
-- reuses `overlapTextureSpawner` directly.
-- lfnoise3 (cubic) stands in for LFNoise2 (parabolic), as elsewhere. `r` is
-- ONE shared draw reused across all 4 chord partials (SC's literal
-- [r,r,r,r], not 4 independent ones) -- built here as a single lfnoise3
-- channel that broadcasts across the 4-channel partial bank, same cyclic
-- broadcast used for #13/#14's freqs. Panning is genuinely 4 independent
-- draws, combined via the same block-order transpose(4)/sum(2) reduction.
-- CombN wraps the whole texture's summed output, not each instance, so it's
-- applied outside the voicer.

-- XFadeTexture(.., 2, 0.5, 2 chans): sustain 2, transition .5, two voices
-- alternating -- the next starts as the previous begins its fall.
const kTremVoices = 2;
const kTremTrans = 0.5;
const kTremHold = 2.5;       -- sustain + transition
const kTremInterval = 2.5;   -- sustain + transition

fn tremulateVoice() S {
	let g = gate();
	let f = frand(500, 900, 1, Rate.reset);
	let r = frand(30, 90, 1, Rate.reset);
	let chordRatios = [1.0, 1.2, 1.5, 1.8] vec;
	let freqs = f * chordRatios;
	let ampEnv = ((r lfnoise3(1)) * 0.1) max(0);
	let partials = (freqs fsinosc) * ampEnv;
	let panPos = birand(4, Rate.reset);
	let stereo = (partials pan(panPos) join) transpose(4) sum(2);
	let env = linen(g, kTremTrans, kTremTrans) rpanfun;
	stereo * env
}

fn tremulate() S = voicer(kTremVoices, tremulateVoice) sum(2) combn(0.1, 1) gainOutlet;

tremulate defSynthX("tremulate", tags) await;

---------------------------------------------------------------------------
-- 11. reso-pulse (examples-1.txt:145-162)
-- SC: lfoFreq=6; lfo=LFNoise0.kr(lfoFreq,1000,1200);
-- left = RLPF.ar(OverlapTexture.ar({
--     f = #[25,30,34,37,41,42,46,49,53,54,58,61,63,66].choose.midicps;
--     LFPulse.ar(f, 0.2, 1, LFPulse.ar(2*f+0.5.rand2, 0.2, 1))
-- }, 4, 2, 4, 1) * 0.02, lfo, MouseX.kr(0.2, 0.02, 'exponential'));
-- delayTime = 2/lfoFreq; right = DelayN.ar(left, delayTime, delayTime);
--
-- `.choose` on a literal array has no in-graph equivalent; approximated by
-- indexing the same literal array with an `irand`-chosen index via
-- `select`, both at Rate.reset (chosen once per note-on, matching SC's
-- per-instance choice). Both LFPulses are `(freq, width, mul, add)`, so the
-- outer's mul=1 is the SC default (no extra scale) and add=inner pulse.
-- `delayn` is a new plain (no-feedback) delay-line ugen, added to
-- common_ugens.x alongside comb/combn/combl -- SC's DelayN/DelayC had no
-- Tzopilotl equivalent before this.

-- OverlapTexture(.., 4, 2, 4, 1): sustain 4, transition 2, 4 overlapping.
const kResoTrans = 2.0;
const kResoHold = 6.0;       -- sustain + transition
const kResoInterval = 1.5;   -- (sustain + transition) / overlap
const kResoVoices = 6;       -- ceil(life 8 / interval 1.5)

fn resoPulseVoice() S {
	let g = gate();
	-- select()'s exprs array wants each element individually `asSignal`'d;
	-- `@` maps it per element (a bare `[Int] asSignal` would instead hit the
	-- "whole array as one vec" overload). It must be built HERE, inside the
	-- graph function: signal nodes created at module top level have no graph
	-- to attach to (and currently segfault rather than diagnose).
	let notes = [25, 30, 34, 37, 41, 42, 46, 49, 53, 54, 58, 61, 63, 66] @ asSignal;
	let idx = irand(0, (notes length) - 1, 1, Rate.reset);
	let f = (select(idx, notes)) nnhz;
	let innerFreq = 2 * f + birand(1, Rate.reset) * 0.5;
	let innerPulse = innerFreq lfupulse(0.2);
	let outerPulse = (f lfupulse(0.2)) + innerPulse;
	let env = linen(g, kResoTrans, kResoTrans) rpanfun;
	outerPulse * env
}

fn resoPulse() S {
	let lfo = ((6 asSignal) lfnoise0(1)) * 1000 + 1200;
	let bandwidth = mouseXExp(0.2, 0.02);
	let left = (voicer(kResoVoices, resoPulseVoice) sum * 0.02) rlpf(lfo, bandwidth);
	let delayTime = 2.0 / 6.0;
	let right = left delayn(delayTime, delayTime);
	[left, right] join gainOutlet
}

resoPulse defSynthX("resoPulse", tags) await;

---------------------------------------------------------------------------
-- 12. sprinkler (examples-1.txt:167-176)
-- SC: BPZ2.ar(WhiteNoise.ar(LFPulse.kr(LFPulse.kr(0.09, 0.16, 10, 7), 0.25, 0.1)))
-- and a MouseX-controlled variant of the same idea.
--
-- Three things worth noting about this one:
-- - BPZ2 is SC's *fixed* 2-zero bandpass (no freq/bw args) -- it's exactly
--   `(x - x.z2) * 0.5`, not something needing a `bpf` approximation.
-- - The outer LFPulse's output isn't chained into WhiteNoise as a signal
--   argument (WhiteNoise.ar has no such input) -- it's WhiteNoise's `mul`,
--   i.e. it's amplitude-gating the noise into bursts, which is what makes
--   this a "sprinkler". Easy to misread as a modulation chain at a glance.
-- - LFPulse.kr(freq, width, mul, add) has no phase arg -- the inner call's
--   extra positionals are width=0.16, mul=10, add=7; the outer's are
--   width=0.25, mul=0.1.

fn bpz2(x S) S = (x - x z2) * 0.5;

fn sprinkler() S {
	let innerPulse = (0.09 lfupulse(0.16)) * 10 + 7;
	let burstGate = (innerPulse lfupulse(0.25)) * 0.1;
	(white() * burstGate) bpz2 gainOutlet
}

sprinkler defSynthX("sprinkler", tags) await;

fn sprinklerMouse() S {
	let burstGate = (mouseX(0.2, 50) lfupulse(0.25)) * 0.1;
	(white() * burstGate) bpz2 gainOutlet
}

sprinklerMouse defSynthX("sprinklerMouse", tags) await;

---------------------------------------------------------------------------
-- 13. harmonic swimming (examples-1.txt:181-202)
-- SC: 20 partials daisy-chained through FSinOsc.ar(freq, mul, add) -- SC2's
-- FSinOsc had no iphase argument, just (freq, mul, add), so this is a
-- straightforward additive chain: each partial's own amplitude envelope
-- (mul) gets added (add=z) onto the running sum of the partials before it.
-- Rather than 20 separate mono oscillator nodes added one by one (as the SC
-- code literally does, and as a host-language loop would translate too
-- literally), this builds ONE n-channel oscillator and reduces it with
-- `sum` -- cheaper for the compiled graph, and the idiom `apverbTest` (in
-- example_synthdefs.x) already uses for the same "many independent partials"
-- shape. The `transpose(n) sum(2)` pairing follows that same example: the
-- per-partial-per-side envelope is built in *block* order (channels
-- [0,n) = left, [n,2n) = right) so it lines up with the n-channel oscillator
-- bank via cyclic broadcast (channel i uses freqs[i % n]); `transpose(n)`
-- then reshapes block order into the interleaved order `sum(2)` reduces
-- (output[c] = sum of input channels c, c+2, c+4, ... -- see ReduceExpr /
-- reduce_rows in synthdef_cpp_codegen.cpp).
-- Per-partial modulation rate `[4.0.rand2, 4.0.rand2]` is chosen once per
-- partial when this graph is built -- the Rate.init analog, since each SC
-- `{}.play` evaluation instantiates a fresh graph with fresh choices baked
-- in, and a fresh Tzopilotl plugin instance re-rolls Rate.init once at load.

fn harmonicSwimming() S {
	let n = 20;
	let f = 50;
	let offset = line(0, -0.02, 60);
	var idx [Int] = [];
	for (i : (1..n)) { idx push!(i); }
	let freqs = f * (idx vec);
	let rate = 6 + birand(2 * n, Rate.init) * 4;
	let ampEnv = ((rate lfnoise1(2 * n)) * 0.02 + offset) max(0);
	(freqs fsinosc) * ampEnv |> transpose(n) sum(2) gainOutlet
}

harmonicSwimming defSynthX("harmonicSwimming", tags) await;

---------------------------------------------------------------------------
-- 14. harmonic tumbling (examples-1.txt:206-229)
-- SC: 10 partials, same additive daisy-chain structure as #13, each gated
-- by a Dust-triggered Decay2 grain envelope; vectorized the same way as #13
-- (one n-channel oscillator + transpose/sum, not 10 separate nodes). Each
-- partial's own Dust instance is independent (dust(trig, 2n) draws 2n
-- independent impulse trains sharing the trig density), but its decay time
-- (`0.5.rand`, chosen once per partial in SC) is shared by both channels of
-- that partial, so decayTimes is only n-wide and broadcasts cyclically over
-- the 2n-wide dust/decay2 signal the same way freqs does.

fn harmonicTumbling() S {
	let n = 10;
	let f = 80;
	let trig = xline(10, 0.1, 60);
	var idx [Int] = [];
	for (i : (1..n)) { idx push!(i); }
	let freqs = f * (idx vec);
	let decayTimes = urand(n, Rate.init) * 0.5;
	let grain = (dust(trig, 2 * n) * 0.02) decay2(0.005, decayTimes);
	(freqs fsinosc) * grain |> transpose(n) sum(2) gainOutlet
}

harmonicTumbling defSynthX("harmonicTumbling", tags) await;

---------------------------------------------------------------------------
-- driver

fn playAllSc1() {
	go(coro fn() Float {
		"start playing SC2 examples-1" println;
		-- Headless the mouse slots read 0; park the mouse mid-screen so the
		-- mouse examples (#11 reso-pulse, #12 sprinklerMouse) render mid-range.
		ae.setSharedInput(0, 0.5);
		ae.setSharedInput(1, 0.5);

		"lfoPulseRlpf" playFor(8.0) yieldAll;
		"hellIsBusy" texture(kHellVoices, kHellInterval, kHellHold, 24.0, kHellTrans) yieldAll;
		"pondLife" texture(kPondVoices, kPondInterval, kPondHold, 30.0, kPondTrans) yieldAll;

		"-- alienFroggies --" println;
		let froggyNode = "alienFroggies" play;
		alienFroggiesSpawner(froggyNode, 10.0) yieldAll;
		yield kFroggyHold + kFroggyTrans;
		froggyNode stop;

		-- Spawn + Env.linen: hold = rise + sustain; tail = the fall.
		"randomSineWaves" texture(kSineVoices, kSineInterval, kSineHold, 16.0, kSineFall) yieldAll;
		"randomPulsations" texture(kPulseVoices, kPulseInterval, kPulseHold, 16.0, kPulseFall) yieldAll;

		"motoRev" playFor(6.0) yieldAll;
		"scratchy" playFor(6.0) yieldAll;
		"tremulate" texture(kTremVoices, kTremInterval, kTremHold, 15.0, kTremTrans) yieldAll;
		"resoPulse" texture(kResoVoices, kResoInterval, kResoHold, 18.0, kResoTrans) yieldAll;
		"sprinkler" playFor(6.0) yieldAll;
		"sprinklerMouse" playFor(6.0) yieldAll;
		"harmonicSwimming" playFor(10.0) yieldAll;
		"harmonicTumbling" playFor(10.0) yieldAll;

		ae.endRender(0.1);
		"done playing SC2 examples-1" println;
	}());
}

fn renderSc1() {
	let h = "/tmp/sc2_examples_1.wav" ae.renderNRT(320, playAllSc1);
	await ae.renderDone(h);
	"render done" println;
}

--renderSc1();
--playAllSc1();
