-- Shared helpers for the SC2-examples translations (sc2_examples_N.x).
--
-- SC2 UGens that have no direct Tzopilotl ugen, built from what exists:
--   Klank  -> klankModes / klank    (ring == Ringz; a Klank is a sum of them)
--   Klang  -> klangModes             (a bank of fsinosc)
--   Resonz -> resonz                 (bpf, with its octave bandwidth derived
--                                     from Resonz's bandwidth ratio)
--   Crackle -> crackle               (the chaotic map itself)
-- plus the texture spawn-loop machinery every OverlapTexture/XFadeTexture/
-- Spawn example drives its voicer with -- see the notes on
-- overlapTextureSpawner for the timing model.

import synthdef.*;
import common_ugens.*;
import filters.*;
import audio_engine as ae;
import clock.*;

-- Shared output stage with a gain control. Works for any channel count.
fn gainOutlet(x S) S {
	let gain = control("gain", ControlSpec{0.001, 1.0, 0.2, ControlWarp.exponential}) lag(0.1);
	x * gain |> outlet
}

---------------------------------------------------------------------------
-- Envelopes

-- One-shot percussive attack-decay from a trigger, analog style: the attack
-- segment rises to exactly 1 over `attack` seconds along SC's curve map
-- ((1 - exp(c t)) / (1 - exp(c)); c = -2 is concave-down, i.e. sharp), then
-- an exponential decay starts from that peak (-40 dB in `decayTime`).
-- Unlike decay2 (a difference of exponentials) there is a real corner at
-- the peak. This is EnvGen(Env([0, 1, 0], [attack, decayTime], c)).
fn percEnv(trig S, attack, decayTime, curve Float) S {
	let a = trig oneshot1b(attack);            -- 1 -> 0 over the attack, then 0
	let attacking = a != 0;
	let ec = curve exp;
	let att = (1 - ec pow(1 - a)) / (1.0 - ec);
	let endTrig = (attacking == 0) * (z1(attacking) != 0);
	let dec = endTrig decay(decayTime);
	select2(attacking, att, dec)
}

---------------------------------------------------------------------------
-- Reverb tail used by several examples (files 3 and 5): numCombs parallel
-- CombL(0.1 max, LFNoise1(rand 0.1) * 0.04 + 0.05, 15 s) summed, then four
-- AllpassN(0.05 max, [rand 0.05, rand 0.05], 1) in series -- the stereo
-- random allpass delays are what make the tail stereo. Feed it the
-- predelayed mono signal.
fn reverbTail(z S, numCombs Int) S {
	let dts = ((urand(numCombs, Rate.init) * 0.1) lfnoise1(numCombs)) * 0.04 + 0.05;
	var y = (z combl(dts, 0.1, 15)) sum;
	for (i : (1..4)) {
		y = y alpasn(urand(2, Rate.init) * 0.05, 1);
	}
	y
}

---------------------------------------------------------------------------
-- Random helpers

-- SC's linrand(n): uniform weighted toward 0 -- min of two uniforms.
fn linrandv(lo, hi, chans Int, rate Rate) S =
	lo + (hi - lo) * min(urand(chans, rate), urand(chans, rate));

---------------------------------------------------------------------------
-- Klank / Klang / Resonz / Crackle

-- One Ringz per mode, driven by the same excitation: `freqs`, `amps` and
-- `ringTimes` are per-mode vectors (amps may be a plain 1). Returns the
-- per-mode vector so the caller chooses the reduction: `sum` for a mono
-- bank, `sum(2)` when the modes are two independent iid banks interleaved
-- per channel, `transpose(p) sum(2)` when they are block-ordered (bank 0
-- in channels [0,p), bank 1 in [p,2p)). Modes above Nyquist are muted.
fn klankModes(exc S, freqs S, amps AsSignal, ringTimes AsSignal) S {
	let lim = 0.45 * fs();
	let audible = freqs < lim;
	(exc ring(freqs min(lim), ringTimes)) * amps * audible
}

fn klank(exc S, freqs S, amps AsSignal, ringTimes AsSignal) S =
	klankModes(exc, freqs, amps, ringTimes) sum;

-- A bank of sines: per-mode vector, reduce at the call site as for klank.
-- SC2's Klang was a bank of two-pole filters with poles on the unit circle;
-- a bank of fsinxosc is the stable way to get the same thing (author's note).
fn klangModes(freqs S, amps AsSignal) S = (freqs fsinxosc) * amps;

-- Resonz(in, freq, bwr): bwr is bandwidth / centre frequency. bpf's bandwidth
-- is in octaves: the band [f(1 - bwr/2), f(1 + bwr/2)] spans
-- log2((2 + bwr) / (2 - bwr)) octaves. Both have unity gain at the centre.
fn resonz(in S, freq AsSignal, bwr AsSignal) S =
	in bpf(freq, (((2 + bwr) / (2 - bwr)) log) * 1.4426950408889634);

-- Crackle(chaosParam): y0 = |y1 * k - y2 - 0.05|, the same map SC uses
-- (y1 starts at 0.3). k in 1..2; it gets noisier toward 2.
fn crackle(k AsSignal) S {
	let y = delayVar(2) init(1, 0.3);
	y <- (y(1) * k - y(2) - 0.05) abs
}

---------------------------------------------------------------------------
-- Texture spawn loops
--
-- SC2's OverlapTexture.ar(func, sustainTime, transitionTime, overlap, chans):
-- each instance rises over transitionTime, holds flat for sustainTime, and
-- falls over transitionTime (life = sustain + 2*transition); `overlap` is
-- the NUMBER of simultaneous instances, so a new one starts every
-- (sustainTime + transitionTime) / overlap seconds. XFadeTexture is the
-- two-instance case (next one starts as the previous begins its fall, i.e.
-- every sustain + transition seconds). The crossfade is equal power.
-- Spawn.ar(func, chans, nextTime) with an Env.linen(r, s, f) is the same
-- shape with interval = nextTime, hold = r + s, fall = f.
--
-- Translation: a voicer whose body rolls its "per instance" randomization
-- once per note-on (Rate.reset), enveloped by
--     linen(gate(), transitionTime, transitionTime) rpanfun
-- (rpanfun warps linen's linear ramp into the equal-power crossfade curve).
-- The spawner starts a note every `interval` seconds, holds its gate for
-- `holdTime` = sustain + transition (rise + flat), then releases it so
-- linen's fall supplies the transition out, round-robining across
-- `numVoices` note IDs; numVoices must cover ceil(life / interval). The
-- transition time is baked into the voicer's compiled graph, so each
-- example keeps its own kXxxTrans constant and the spawner args must agree.

coro fn overlapTextureSpawner(nodeID Int, numVoices Int, interval Float, holdTime Float, totalDur Float) Float {
	var id = 0;
	var elapsed = 0.0;
	while (elapsed < totalDur) {
		let noteID = id;
		ae.begin(); ae.noteOn(nodeID, noteID, [0.0]); ae.sched(0);
		go(coro fn() Float {
			yield holdTime;
			ae.begin(); ae.noteOff(nodeID, noteID); ae.sched(0);
		}());
		id = (id + 1) % numVoices;
		-- Only wait out the remainder of totalDur: a texture whose interval is
		-- long (dancing shadows: 49 s) would otherwise overrun by most of an
		-- interval of silence after its last spawn.
		let dt = min(interval, totalDur - elapsed);
		elapsed = elapsed + interval;
		yield dt;
	}
}

-- Spawn with a per-event random interval in [minInterval, maxInterval).
coro fn randomIntervalSpawner(nodeID Int, numVoices Int, minInterval Float, maxInterval Float, holdTime Float, totalDur Float) Float {
	var id = 0;
	var elapsed = 0.0;
	while (elapsed < totalDur) {
		let noteID = id;
		ae.begin(); ae.noteOn(nodeID, noteID, [0.0]); ae.sched(0);
		go(coro fn() Float {
			yield holdTime;
			ae.begin(); ae.noteOff(nodeID, noteID); ae.sched(0);
		}());
		id = (id + 1) % numVoices;
		let u Float = urand();
		let dt = minInterval + (maxInterval - minInterval) * u;
		let wait = min(dt, totalDur - elapsed);
		elapsed = elapsed + dt;
		yield wait;
	}
}

-- Play one texture-style example: start its voicer node, spawn notes every
-- `interval` seconds (each held `holdTime`) for `dur` seconds, let the last
-- note finish (hold + its fall `tail`), then free the node.
coro fn texture(name String, voices Int, interval Float, holdTime Float, dur Float, tail Float) Float {
	"-- %^ --" fmt(name) println;
	let node = name play;
	overlapTextureSpawner(node, voices, interval, holdTime, dur) yieldAll;
	-- The spawner returns at `dur`; its last note started at the last
	-- multiple of `interval` below dur, so only that note's REMAINING hold
	-- plus its fall is left to wait out (a 16 s-interval texture would
	-- otherwise end with ~15 s of silence).
	let lastStart = interval * (((dur - 0.001) / interval) floor);
	let remaining = holdTime - (dur - lastStart);
	yield max(0.0, remaining) + tail;
	node stop;
}

coro fn randomTexture(name String, voices Int, minInterval Float, maxInterval Float, holdTime Float, dur Float, tail Float) Float {
	"-- %^ --" fmt(name) println;
	let node = name play;
	randomIntervalSpawner(node, voices, minInterval, maxInterval, holdTime, dur) yieldAll;
	-- The last note started somewhere in the final interval; waiting the
	-- full hold + tail is at most one interval too long, which is small here.
	yield holdTime + tail;
	node stop;
}
