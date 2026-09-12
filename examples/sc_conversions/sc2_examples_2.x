-- Tzopilotl translations of SuperCollider examples from
-- sc-examples/SC2-examples/examples-2.txt. Shared machinery (Klank/Klang/
-- Resonz/Crackle stand-ins, the texture spawn loops) lives in sc2_common.x;
-- sc2_examples_1.x explains the texture timing model.
--
-- This file is mostly Klank (a summed bank of Ringz = `ring`) and Klang
-- (a bank of fsinxosc) driven by textures. Per-instance random banks are
-- Rate.reset vectors; stereo banks (`Array.fill(2, {spec})`) are 2p
-- channels. When the modes are iid the two banks are simply the odd/even
-- channels and `sum(2)` reduces them (it groups channel i into output
-- i % 2); when a bank derives from a p-channel base (#8, #9, #12, #16) the
-- 2p channels are block-ordered and need `transpose(p) sum(2)`.
--
-- Status (examples-2.txt numbering):
--   1  rocks on rails           -- DONE (rocksOnRails)
--   2  bouncing lightbulbs      -- DONE (bouncing) -- Spawn w/ random dt
--   3  Klang lots-o-sines       -- DONE (klangSines)
--   4  clustered sines          -- DONE (clusteredSines)
--   5  Klank impulses           -- DONE (klankImpulses)
--   6  Klank noise bursts       -- DONE (klankBursts)
--   7  BrownNoise resonators    -- DONE (brownResonators)
--   8  just-scale resonators    -- DONE (justResonators)
--   9  odd-harmonic resonators  -- DONE (oddResonators)
--   10 swept resonant noise     -- DONE (sweptResonators)
--   11 coolant                  -- DONE (coolant)
--   12 pulsing bottles          -- DONE (pulsingBottles)
--   13 what was I thinking?     -- DONE (whatWasIThinking) -- Pulse is lfbpulse
--                                  (not band-limited)
--   14 narrow band Crackle      -- DONE (crackleBand)
--   15 resonant dust            -- DONE (resonantDust)
--   16 police state             -- DONE (policeState)
--   17 uplink                   -- DONE (uplink)
--   18 data space               -- DONE (dataSpace)
--   19 cymbalism                -- DONE (cymbalism)
--   20 cymbalism accelerando    -- DONE (cymbalismAccel)
--   21 ring modulated Klank     -- DONE (ringModKlank)

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import filters.*;
import audio_engine as ae;
import clock.*;
import sc2_common.*;

let tags = ["sc2-examples-2"];

-- Equal-power texture envelope keyed to the note gate.
fn xenv(trans Float) S = linen(gate(), trans, trans) rpanfun;

---------------------------------------------------------------------------
-- 1. rocks on rails (examples-2.txt:15-38)
-- OverlapTexture(.., 2, 3, 4, 2): sustain 2, transition 3, 4 overlapping.
-- Klank of 20 modes (freq 200 + linrand 3000, ring 0.2 + rand 1) excited by
-- Resonz(Dust 100 * 0.04, XLine 3000 -> 300 over 8 s, bwr 0.2), panned by a
-- Line between two random positions over 8 s.

const kRocksTrans = 3.0;
const kRocksHold = 5.0;
const kRocksInterval = 1.25;
const kRocksVoices = 7;          -- ceil(life 8 / 1.25)

fn rocksVoice() S {
	let p = 20;
	let freqs = linrandv(200, 3200, p, Rate.reset);
	let rings = 0.2 + urand(p, Rate.reset) * 1.0;
	let exc = resonz(dust(100) * 0.04, xline(3000, 300, 8), 0.2);
	let bank = klank(exc, freqs, 1, rings);
	let panPos = line(birand(1, Rate.reset), birand(1, Rate.reset), 8);
	(bank * xenv(kRocksTrans)) pan(panPos) join
}

fn rocksOnRails() S = voicer(kRocksVoices, rocksVoice) sum(2) gainOutlet;

rocksOnRails defSynthX("rocksOnRails", tags) await;

---------------------------------------------------------------------------
-- 2. bouncing lightbulbs, pencils, cans (examples-2.txt:42-70)
-- Spawn(.., 2, { 0.6 + 0.6.rand }): a new bouncer every 0.6-1.2 s, each cut
-- off by Env([1, 1, 0], [3, 0.001]) -- a 3 s hold then an instant fade.
-- Klank of 4 modes excited by Decay(Impulse(XLine 5 -> 600 Hz over 4 s) *
-- XLine(0.09 -> 0.000009), 0.001): accelerating, dying bounces.

const kBounceHold = 3.0;
const kBounceVoices = 6;         -- ceil(life 3 / min interval 0.6)

fn bouncingVoice() S {
	let g = gate();
	let freqs = 400 + urand(4, Rate.reset) * 8000;
	let amps = urand(4, Rate.reset);
	let rings = 0.01 + urand(4, Rate.reset) * 0.1;
	let rate = xline(5 + birand(1, Rate.reset) * 2, 600, 4);
	let amp = xline(0.09, 0.000009, 4);
	let exc = (lfimp(rate) * amp) decay(0.001);
	let env = linen(g, 0.001, 0.001);
	(klank(exc, freqs, amps, rings) * env) pan(birand(1, Rate.reset)) join
}

fn bouncing() S = voicer(kBounceVoices, bouncingVoice) sum(2) gainOutlet;

bouncing defSynthX("bouncing", tags) await;

---------------------------------------------------------------------------
-- 3. Klang - lots-o-sines (examples-2.txt:74-86)
-- XFadeTexture(.., 4, 4, 2): 60 sines per channel at 40 + linrand 10000 Hz,
-- amplitude 0.1/60 each.

const kXf44Trans = 4.0;          -- shared by every XFadeTexture(.., 4, 4, ..)
const kXf44Hold = 8.0;
const kXf44Interval = 8.0;
const kXfVoices = 2;

fn klangSinesVoice() S {
	let n = 60;
	let freqs = linrandv(40, 10040, 2 * n, Rate.reset);
	(klangModes(freqs, 0.1 / n) sum(2)) * xenv(kXf44Trans)
}

fn klangSines() S = voicer(kXfVoices, klangSinesVoice) sum(2) gainOutlet;

klangSines defSynthX("klangSines", tags) await;

---------------------------------------------------------------------------
-- 4. clustered sines (examples-2.txt:90-108)
-- XFadeTexture(.., 4, 4, 2): 80 sines per channel in [f1, f1 + 4 f1) with
-- amplitude f1 / freq, f1 = 100 + rand 1000.

fn clusteredSinesVoice() S {
	let n = 80;
	let f1 = 100 + urand(1, Rate.reset) * 1000;
	let freqs = f1 + urand(2 * n, Rate.reset) * (4.0 * f1);
	(klangModes(freqs, (f1 / freqs) * (0.3 / n)) sum(2)) * xenv(kXf44Trans)
}

fn clusteredSines() S = voicer(kXfVoices, clusteredSinesVoice) sum(2) gainOutlet;

clusteredSines defSynthX("clusteredSines", tags) await;

---------------------------------------------------------------------------
-- 5. Klank - bank of resonators excited by impulses (examples-2.txt:112-130)
-- OverlapTexture(.., 6, 6, 5, 2): 15 modes (80 + linrand 10000, amps rand2,
-- ring 0.2 + rand 8) excited by Dust 0.7 * 0.04, panned.

const kK5Trans = 6.0;
const kK5Hold = 12.0;
const kK5Interval = 2.4;         -- 12 / 5
const kK5Voices = 8;             -- ceil(life 18 / 2.4)

fn klankImpulsesVoice() S {
	let p = 15;
	let freqs = linrandv(80, 10080, p, Rate.reset);
	let amps = birand(p, Rate.reset);
	let rings = 0.2 + urand(p, Rate.reset) * 8;
	let bank = klank(dust(0.7) * 0.04, freqs, amps, rings);
	(bank * xenv(kK5Trans)) pan(birand(1, Rate.reset)) join
}

fn klankImpulses() S = voicer(kK5Voices, klankImpulsesVoice) sum(2) gainOutlet;

klankImpulses defSynthX("klankImpulses", tags) await;

---------------------------------------------------------------------------
-- 6. Klank - excited by noise bursts (examples-2.txt:134-152)
-- OverlapTexture(.., 8, 8, 5, 2): two 8-mode banks excited by
-- Decay(Dust 0.6 * 0.001, 3.1) * WhiteNoise.

const kK6Trans = 8.0;
const kK6Hold = 16.0;
const kK6Interval = 3.2;         -- 16 / 5
const kK6Voices = 8;             -- ceil(life 24 / 3.2)

fn klankBurstsVoice() S {
	let p = 8;
	let freqs = linrandv(80, 10080, 2 * p, Rate.reset);
	let rings = 0.2 + urand(2 * p, Rate.reset) * 4;
	let exc = ((dust(0.6) * 0.001) decay(3.1)) * white();
	(klankModes(exc, freqs, 1, rings) sum(2)) * xenv(kK6Trans)
}

fn klankBursts() S = voicer(kK6Voices, klankBurstsVoice) sum(2) gainOutlet;

klankBursts defSynthX("klankBursts", tags) await;

---------------------------------------------------------------------------
-- 7. resonators at random frequencies excited by BrownNoise (examples-2.txt:156-173)
-- XFadeTexture(.., 6, 6, 2): two 32-mode banks, amps rand2, ring 0.5 + rand 2,
-- excited by BrownNoise * 0.001.

const kXf66Trans = 6.0;
const kXf66Hold = 12.0;
const kXf66Interval = 12.0;

fn brownResonatorsVoice() S {
	let p = 32;
	let freqs = linrandv(80, 10080, 2 * p, Rate.reset);
	let amps = birand(2 * p, Rate.reset);
	let rings = 0.5 + urand(2 * p, Rate.reset) * 2;
	(klankModes(red(1) * 0.001, freqs, amps, rings) sum(2)) * xenv(kXf66Trans)
}

fn brownResonators() S = voicer(kXfVoices, brownResonatorsVoice) sum(2) gainOutlet;

brownResonators defSynthX("brownResonators", tags) await;

---------------------------------------------------------------------------
-- 8. resonators tuned in a harmonic series from a just scale (examples-2.txt:177-200)
-- XFadeTexture(.., 1, 7, 2): sustain 1, transition 7. One of 15 just ratios
-- is chosen per instance (times 120 Hz); each bank's 12 modes sit at
-- freq * (1..12) + rand2 0.5, amplitude 1/n, ring 0.5 + rand 4, excited by
-- BrownNoise * 0.001. The p-channel series broadcasts over the 2p-channel
-- random offsets, so the banks come out block-ordered: transpose(p) sum(2).

const kJustTrans = 7.0;
const kJustHold = 8.0;
const kJustInterval = 8.0;

fn justRatio() S {
	let ratios = [1.0, 1.125, 1.25, 1.333, 1.5, 1.667, 1.875, 2.0,
		2.25, 2.5, 2.667, 3.0, 3.333, 3.75, 4.0] @ asSignal;
	select(irand(0, (ratios length) - 1, 1, Rate.reset), ratios) * 120.0
}

fn harmonicIdx(p Int) [Int] {
	var idx [Int] = [];
	for (i : (1..p)) { idx push!(i); }
	idx
}

fn justResonatorsVoice() S {
	let p = 12;
	let idx = harmonicIdx(p) vec;
	let freq = justRatio();
	let freqs = idx * freq + birand(2 * p, Rate.reset) * 0.5;
	let rings = 0.5 + urand(2 * p, Rate.reset) * 4;
	(klankModes(red(1) * 0.001, freqs, 1 / idx, rings) transpose(p) sum(2)) * xenv(kJustTrans)
}

fn justResonators() S = voicer(kXfVoices, justResonatorsVoice) sum(2) gainOutlet;

justResonators defSynthX("justResonators", tags) await;

---------------------------------------------------------------------------
-- 9. same, odd harmonics only, shorter rings, LFNoise2 exciter (examples-2.txt:204-228)
-- freq * (1, 3, 5, ..), ring 0.2 + rand 0.8, excited by LFNoise2 at 8 kHz *
-- 0.004 (lfnoise3 stands in for LFNoise2).

fn oddResonatorsVoice() S {
	let p = 12;
	let idx = harmonicIdx(p) vec;
	let freq = justRatio();
	let freqs = (idx * 2 - 1) * freq + birand(2 * p, Rate.reset) * 0.5;
	let rings = 0.2 + urand(2 * p, Rate.reset) * 0.8;
	let exc = ((8000 asSignal) lfnoise3(1)) * 0.004;
	(klankModes(exc, freqs, 1 / idx, rings) transpose(p) sum(2)) * xenv(kJustTrans)
}

fn oddResonators() S = voicer(kXfVoices, oddResonatorsVoice) sum(2) gainOutlet;

oddResonators defSynthX("oddResonators", tags) await;

---------------------------------------------------------------------------
-- 10. swept resonant noise band exciting a resonator bank (examples-2.txt:232-253)
-- OverlapTexture(.., 4, 4, 5, 2): Resonz(WhiteNoise * 0.007, FSinOsc.kr(0.1 +
-- rand 0.2, 12 + rand2 12, 60 + rand2 24).midicps, 0.1) into two 10-mode banks.

const kSweptTrans = 4.0;
const kSweptHold = 8.0;
const kSweptInterval = 1.6;      -- 8 / 5
const kSweptVoices = 8;          -- ceil(life 12 / 1.6)

fn sweptResonatorsVoice() S {
	let p = 10;
	let lfo = ((0.1 + urand(1, Rate.reset) * 0.2) fsinosc) * (12 + birand(1, Rate.reset) * 12)
		+ (60 + birand(1, Rate.reset) * 24);
	let sweep = resonz(white() * 0.007, lfo nnhz, 0.1);
	let freqs = linrandv(80, 10080, 2 * p, Rate.reset);
	let rings = 0.5 + urand(2 * p, Rate.reset) * 2;
	(klankModes(sweep, freqs, 1, rings) sum(2)) * xenv(kSweptTrans)
}

fn sweptResonators() S = voicer(kSweptVoices, sweptResonatorsVoice) sum(2) gainOutlet;

sweptResonators defSynthX("sweptResonators", tags) await;

---------------------------------------------------------------------------
-- 11. coolant (examples-2.txt:258-275)
-- XFadeTexture(.., 4, 4, 2): two 10-mode banks (40 + rand 2000, default ring)
-- excited by OnePole(BrownNoise [0.002, 0.002], 0.95) -- a stereo exciter,
-- channel i of the 2p bank taking exciter i % 2, which is exactly the
-- odd/even split sum(2) reduces.

fn coolantVoice() S {
	let p = 10;
	let freqs = 40 + urand(2 * p, Rate.reset) * 2000;
	let exc = (red(2) * 0.002) onepole(0.95);
	(klankModes(exc, freqs, 1, 1.0) sum(2)) * xenv(kXf44Trans)
}

fn coolant() S = voicer(kXfVoices, coolantVoice) sum(2) gainOutlet;

coolant defSynthX("coolant", tags) await;

---------------------------------------------------------------------------
-- 12. pulsing bottles (examples-2.txt:279-296)
-- No texture: Mix of 6 bottles, each Resonz(WhiteNoise * LFPulse(4 + rand 10,
-- width rand 0.7, 0.8/n), 400 + linrand 7000, 0.01) panned by a slow
-- SinOsc.kr(0.1 + rand 0.4, rand 2pi). Everything random is chosen once
-- when the synth is built (Rate.init).

fn pulsingBottles() S {
	let n = 6;
	let pulseF = 4 + urand(n, Rate.init) * 10;
	let width = urand(n, Rate.init) * 0.7;
	let gateAmp = (pulseF lfupulse(width)) * (0.8 / n);
	let rf = linrandv(400, 7400, n, Rate.init);
	let sig = resonz(white(n) * gateAmp, rf, 0.01);
	let panF = 0.1 + urand(n, Rate.init) * 0.4;
	let panPh = urand(n, Rate.init) * twopi;
	(sig pan(panF sinosc(panPh)) join) transpose(n) sum(2) gainOutlet
}

pulsingBottles defSynthX("pulsingBottles", tags) await;

---------------------------------------------------------------------------
-- 13. what was I thinking? (examples-2.txt:300-320)
-- RLPF(Pulse(freq, width, 0.04), LFNoise1 2000 + 2400, 0.2) where freq =
-- max(SinOsc.kr(4) + 80, Decay(LFPulse.ar(0.1, width .05, Impulse 8 * 500), 2))
-- and width = LFNoise1(0.157) * 0.4 + 0.5; then each side adds two CombL
-- taps whose delay wanders with its own LFNoise1. Pulse.ar is band-limited
-- in SC; lfbpulse is the nearest thing here.

fn whatWasIThinking() S {
	let f1 = (4 sinosc) + 80;
	let f2 = (((0.1 asSignal) lfupulse(0.05)) * (lfimp(8) * 500)) decay(2);
	let freq = max(f1, f2);
	let width = ((0.157 asSignal) lfnoise1(1)) * 0.4 + 0.5;
	let cutoff = ((0.2 asSignal) lfnoise1(1)) * 2000 + 2400;
	let z = ((freq lfbpulse(width)) * 0.04) rlpf(cutoff, 0.2);
	let y = z * 0.6;
	let rates = urand(4, Rate.init) * 0.3;
	let dts = (rates lfnoise1(4)) * 0.025 + 0.035;
	let combs = y combl(dts, 0.06, 1);
	(z + (combs transpose(2) sum(2))) gainOutlet
}

whatWasIThinking defSynthX("whatWasIThinking", tags) await;

---------------------------------------------------------------------------
-- 14. narrow band filtered Crackle noise (examples-2.txt:325-338)
-- Spawn(.., 2, 1) with Env.linen(2, 5, 2): one every second, hold 7, fall 2.
-- Resonz(Crackle(1.97 + rand 0.03) * 0.15, XLine(rf, rf * (1 + rand2 0.2), 9), 0.2)
-- with rf = 80 + rand 2000, panned.

const kSpawnLinenHold = 7.0;     -- rise 2 + sustain 5
const kSpawnLinenFall = 2.0;
const kSpawnLinenVoices = 10;    -- ceil(life 9 / interval 1)

fn crackleBandVoice() S {
	let g = gate();
	let rf = 80 + urand(1, Rate.reset) * 2000;
	let rf2 = rf + birand(1, Rate.reset) * 0.2 * rf;
	let sig = resonz(crackle(1.97 + urand(1, Rate.reset) * 0.03) * 0.15, xline(rf, rf2, 9), 0.2);
	(sig * linen(g, 2, 2)) pan(birand(1, Rate.reset)) join
}

fn crackleBand() S = voicer(kSpawnLinenVoices, crackleBandVoice) sum(2) gainOutlet;

crackleBand defSynthX("crackleBand", tags) await;

---------------------------------------------------------------------------
-- 15. resonant dust (examples-2.txt:342-354)
-- As #14 with Resonz(Dust(50 + rand 800) * 0.3, XLine(rf, rf * (1 + rand2 0.5), 9), 0.1).

fn resonantDustVoice() S {
	let g = gate();
	let rf = 80 + urand(1, Rate.reset) * 2000;
	let rf2 = rf + birand(1, Rate.reset) * 0.5 * rf;
	let sig = resonz(dust(50 + urand(1, Rate.reset) * 800) * 0.3, xline(rf, rf2, 9), 0.1);
	(sig * linen(g, 2, 2)) pan(birand(1, Rate.reset)) join
}

fn resonantDust() S = voicer(kSpawnLinenVoices, resonantDustVoice) sum(2) gainOutlet;

resonantDust defSynthX("resonantDust", tags) await;

---------------------------------------------------------------------------
-- 16. police state (examples-2.txt:358-376)
-- No texture: 4 sirens SinOsc(SinOsc.kr(0.02 + rand 0.1, rand 2pi, rand 600,
-- 1000 + rand2 300), 0, LFNoise2(100 + rand2 20) * 0.1) panned, plus a
-- stereo LFNoise2 whose frequency and amplitude are themselves LFNoise2s,
-- all through CombN(0.3, 0.3, 3). lfnoise3 stands in for LFNoise2.

fn policeState() S {
	let n = 4;
	let lfoF = 0.02 + urand(n, Rate.init) * 0.1;
	let lfoPh = urand(n, Rate.init) * twopi;
	let depth = urand(n, Rate.init) * 600;
	let center = 1000 + birand(n, Rate.init) * 300;
	let freq = (lfoF sinosc(lfoPh)) * depth + center;
	let ampNoise = ((100 + birand(n, Rate.init) * 20) lfnoise3(n)) * 0.1;
	let sirens = (((freq sinosc) * ampNoise) pan(birand(n, Rate.init)) join) transpose(n) sum(2);
	let nf = (([0.4, 0.4] vec) lfnoise3(2)) * 90 + 620;
	let na = (([0.3, 0.3] vec) lfnoise3(2)) * 0.15 + 0.18;
	let noise = (nf lfnoise3(2)) * na;
	((sirens + noise) combn(0.3, 3)) gainOutlet
}

policeState defSynthX("policeState", tags) await;

---------------------------------------------------------------------------
-- 17. uplink (examples-2.txt:380-393)
-- OverlapTexture(.., 4, 1, 5, 2): LFPulse.ar(freq, 0.5, 0.04) where freq is
-- the sum of two LFPulse.kr(rand 20, rand 1, LFPulse.kr(rand 4, rand 1,
-- rand 8000, rand 2000)) -- pulse-modulated pulses. LFPulse(freq, width,
-- mul, add). Panned rand2 0.8.

const kUplinkTrans = 1.0;
const kUplinkHold = 5.0;
const kUplinkInterval = 1.0;     -- 5 / 5
const kUplinkVoices = 7;         -- ceil(life 7 / 1)

fn pulseTerm(outerMax Float, innerMax Float) S {
	let inner = ((urand(1, Rate.reset) * innerMax) lfupulse(urand(1, Rate.reset))) * (urand(1, Rate.reset) * 8000)
		+ urand(1, Rate.reset) * 2000;
	((urand(1, Rate.reset) * outerMax) lfupulse(urand(1, Rate.reset))) * inner
}

fn uplinkVoice() S {
	let freq = pulseTerm(20, 4) + pulseTerm(20, 4);
	(((freq lfupulse(0.5)) * 0.04) * xenv(kUplinkTrans)) pan(birand(1, Rate.reset) * 0.8) join
}

fn uplink() S = voicer(kUplinkVoices, uplinkVoice) sum(2) gainOutlet;

uplink defSynthX("uplink", tags) await;

---------------------------------------------------------------------------
-- 18. data space (examples-2.txt:399-412)
-- OverlapTexture(.., 6, 1, 4, 2): three pulse-modulated pulse terms (the first
-- ten times faster), panned by LFNoise0(rand 3) * 0.8, then a per-instance
-- CombL(dtime, dtime, 3) with dtime = 0.1 + rand 0.25 (max delay fixed at
-- 0.35 s here, since the ring buffer is sized at init).

const kDataTrans = 1.0;
const kDataHold = 7.0;
const kDataInterval = 1.75;      -- 7 / 4
const kDataVoices = 6;           -- ceil(life 9 / 1.75)

fn dataSpaceVoice() S {
	let freq = pulseTerm(200, 40) + pulseTerm(20, 4) + pulseTerm(20, 4);
	let dtime = 0.1 + urand(1, Rate.reset) * 0.25;
	let panPos = ((urand(1, Rate.reset) * 3) lfnoise0(1)) * 0.8;
	let sig = (((freq lfupulse(0.5)) * 0.04) pan(panPos) join) combl(dtime, 0.35, 3);
	sig * xenv(kDataTrans)
}

fn dataSpace() S = voicer(kDataVoices, dataSpaceVoice) sum(2) gainOutlet;

dataSpace defSynthX("dataSpace", tags) await;

---------------------------------------------------------------------------
-- 19. cymbalism (examples-2.txt:416-434)
-- XFadeTexture(.., 4, 4, 2): two 15-mode banks in [f1, f1 + f2) with f1 =
-- 500 + rand 2000, f2 = rand 8000, ring 1 + rand 4, excited by
-- Decay(Impulse(0.5 + rand 3), 0.004) * WhiteNoise * 0.03.

fn cymbalVoice(impulseRate S) S {
	let p = 15;
	let f1 = 500 + urand(1, Rate.reset) * 2000;
	let f2 = urand(1, Rate.reset) * 8000;
	let freqs = f1 + urand(2 * p, Rate.reset) * f2;
	let rings = 1 + urand(2 * p, Rate.reset) * 4;
	let exc = ((lfimp(impulseRate)) decay(0.004)) * white() * 0.03;
	(klankModes(exc, freqs, 1, rings) sum(2)) * xenv(kXf44Trans)
}

fn cymbalismVoice() S = cymbalVoice(0.5 + urand(1, Rate.reset) * 3);

fn cymbalism() S = voicer(kXfVoices, cymbalismVoice) sum(2) gainOutlet;

cymbalism defSynthX("cymbalism", tags) await;

---------------------------------------------------------------------------
-- 20. cymbalism accellerando (examples-2.txt:438-457)
-- As #19 with the impulse rate on XLine(0.5 + linrand 4, 0.5 + rand 35, 12).

fn cymbalismAccelVoice() S =
	cymbalVoice(xline(linrandv(0.5, 4.5, 1, Rate.reset), 0.5 + urand(1, Rate.reset) * 35, 12));

fn cymbalismAccel() S = voicer(kXfVoices, cymbalismAccelVoice) sum(2) gainOutlet;

cymbalismAccel defSynthX("cymbalismAccel", tags) await;

---------------------------------------------------------------------------
-- 21. ring modulated Klank (examples-2.txt:461-472)
-- OverlapTexture(.., 4, 4, 4, 2): an 8-mode Klank (100..10000 Hz, ring
-- 0.2..1) on Dust 20 * 0.02, ring-modulated by SinOsc(LFNoise2(1 + rand2 0.3)
-- * 200 + 350 + rand 50), panned.

const kRmTrans = 4.0;
const kRmHold = 8.0;
const kRmInterval = 2.0;         -- 8 / 4
const kRmVoices = 6;             -- ceil(life 12 / 2)

fn ringModKlankVoice() S {
	let p = 8;
	let freqs = 100 + urand(p, Rate.reset) * 9900;
	let rings = 0.2 + urand(p, Rate.reset) * 0.8;
	let a = klank(dust(20) * 0.02, freqs, 1, rings);
	let modF = ((1 + birand(1, Rate.reset) * 0.3) lfnoise3(1)) * 200 + 350 + urand(1, Rate.reset) * 50;
	(((modF sinosc) * a) * xenv(kRmTrans)) pan(birand(1, Rate.reset)) join
}

fn ringModKlank() S = voicer(kRmVoices, ringModKlankVoice) sum(2) gainOutlet;

ringModKlank defSynthX("ringModKlank", tags) await;

---------------------------------------------------------------------------
-- driver

fn playAllSc2() {
	go(coro fn() Float {
		"start playing SC2 examples-2" println;

		"rocksOnRails" texture(kRocksVoices, kRocksInterval, kRocksHold, 16.0, kRocksTrans) yieldAll;
		"bouncing" randomTexture(kBounceVoices, 0.6, 1.2, kBounceHold, 12.0, 0.001) yieldAll;
		"klangSines" texture(kXfVoices, kXf44Interval, kXf44Hold, 16.0, kXf44Trans) yieldAll;
		"clusteredSines" texture(kXfVoices, kXf44Interval, kXf44Hold, 16.0, kXf44Trans) yieldAll;
		"klankImpulses" texture(kK5Voices, kK5Interval, kK5Hold, 16.0, kK5Trans) yieldAll;
		"klankBursts" texture(kK6Voices, kK6Interval, kK6Hold, 16.0, kK6Trans) yieldAll;
		"brownResonators" texture(kXfVoices, kXf66Interval, kXf66Hold, 24.0, kXf66Trans) yieldAll;
		"justResonators" texture(kXfVoices, kJustInterval, kJustHold, 16.0, kJustTrans) yieldAll;
		"oddResonators" texture(kXfVoices, kJustInterval, kJustHold, 16.0, kJustTrans) yieldAll;
		"sweptResonators" texture(kSweptVoices, kSweptInterval, kSweptHold, 12.0, kSweptTrans) yieldAll;
		"coolant" texture(kXfVoices, kXf44Interval, kXf44Hold, 16.0, kXf44Trans) yieldAll;
		"pulsingBottles" playFor(10.0) yieldAll;
		"whatWasIThinking" playFor(12.0) yieldAll;
		"crackleBand" texture(kSpawnLinenVoices, 1.0, kSpawnLinenHold, 12.0, kSpawnLinenFall) yieldAll;
		"resonantDust" texture(kSpawnLinenVoices, 1.0, kSpawnLinenHold, 12.0, kSpawnLinenFall) yieldAll;
		"policeState" playFor(12.0) yieldAll;
		"uplink" texture(kUplinkVoices, kUplinkInterval, kUplinkHold, 10.0, kUplinkTrans) yieldAll;
		"dataSpace" texture(kDataVoices, kDataInterval, kDataHold, 12.0, kDataTrans) yieldAll;
		"cymbalism" texture(kXfVoices, kXf44Interval, kXf44Hold, 16.0, kXf44Trans) yieldAll;
		"cymbalismAccel" texture(kXfVoices, kXf44Interval, kXf44Hold, 16.0, kXf44Trans) yieldAll;
		"ringModKlank" texture(kRmVoices, kRmInterval, kRmHold, 12.0, kRmTrans) yieldAll;

		ae.endRender(0.1);
		"done playing SC2 examples-2" println;
	}());
}

fn renderSc2() {
	let h = "/tmp/sc2_examples_2.wav" ae.renderNRT(700, playAllSc2);
	await ae.renderDone(h);
	"render done" println;
}

--renderSc2();
--playAllSc2();
