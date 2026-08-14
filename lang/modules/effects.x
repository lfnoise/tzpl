-- effects.x -- a library of common audio effects.
--
-- Two layers:
--
--   1. Ugen layer: composable signal functions `fn effect(x S, ...) S`
--      building on common_ugens and filters. Unless noted, an effect
--      processes whatever channel count flows through it (broadcasting).
--      Effects documented as "mono in" should be fed a single channel
--      (e.g. `x sum`); effects documented as "stereo out" return a
--      2-channel signal.
--
--   2. Synthdef layer: pure graph functions `fxTremolo()`, `fxEcho()`, ...
--      each declaring a stereo inlet, named controls, and an outlet.
--      They have no side effects; compile them with defSynthX, e.g.
--          import effects.*;
--          fxEcho defSynthX("fxEcho");
--      or import examples/effect_synthdefs.x which registers them all.

import synthdef.*;
import common_ugens.*;
import filters.*;

---------------------------------------------------------------------------
-- Shared helpers

-- linear dry/wet crossfade
fn drywet(dry S, wet S, m AsSignal) S = dry + m * (wet - dry);

-- one-sample slew with separate rise and fall time constants (-40 dB times).
fn smooth2(x S, riseT AsSignal, fallT AsSignal) S {
	let u = decay40dB(riseT * fs());
	let d = decay40dB(fallT * fs());
	let y = delayVar();
	let coef = select2(x > y(1), u, d);
	y <- x + coef * (y(1) - x)
}

-- channel-linked envelope follower: rectify, reduce to one channel, smooth.
fn envfollow(x S, atk AsSignal, rel AsSignal) S = x abs maxOf smooth2(atk, rel);

-- allpass with an explicit feedback coefficient g (via alpasn's decay time)
fn _apg(x S, delaySec Float, g Float) S =
	x alpasn(delaySec, delaySec * (6.907755278982137 / (0.0 - g log)));

-- feedback comb with a one-pole damping filter in the loop.
-- delaySec may be a signal; maxDelaySec must be constant.
fn dampcomb(x S, delaySec AsSignal, maxDelaySec Float, decayTime AsSignal, damp AsSignal) S {
	let ds = (delaySec * fs()) clip(2.0, maxDelaySec * fs());
	let y = delayVar(maxDelaySec * fs() + 4.0);
	let tap = y vread(ds, Interpolation.linear);
	let coef = decay60dB(decayTime / delaySec);
	y <- x + coef * tap onepole(damp);
	tap
}

---------------------------------------------------------------------------
-- Modulation effects

-- tremolo: periodic amplitude modulation.
fn tremolo(x S, rate AsSignal = 5.0, depth AsSignal = 0.6) S =
	x * (1.0 - depth * phasor(rate) usin2pi);

-- vibrato: periodic pitch modulation via a modulated delay line.
-- depth is the peak pitch deviation in semitones. A sine-swept delay of
-- amplitude A seconds at `rate` Hz deviates pitch by a peak ratio of
-- 2*pi*rate*A, so A = (2^(depth/12) - 1) / (2*pi*rate) -- deeper or slower
-- vibrato needs a longer sweep. A is capped at 50 ms (very slow, very deep
-- settings saturate below the requested depth).
fn vibrato(x S, rate AsSignal = 5.0, depth AsSignal = 0.3) S {
	let maxA = 0.05;
	let d = delayVar((2.0 * maxA + 0.002) * fs());
	d <- x;
	let ratioDev = (depth asSignal stratio - 1.0) max(0.0);
	let amp = (ratioDev / (twopi * rate asSignal max(0.01))) min(maxA);
	d vread(2.0 + (amp + amp * phasor(rate) sin2pi) * fs())
}

-- flanger: short swept delay with feedback, mixed with the dry signal.
fn flanger(x S, rate AsSignal = 0.25, depth AsSignal = 1.0,
           fb AsSignal = 0.5, m AsSignal = 0.5) S {
	let d = delayVar(0.009 * fs());
	let sweep = phasor(rate) usin2pi;
	let dly = 2.0 + (0.0005 + 0.005 * depth * sweep) * fs();
	let tap = d vread(dly);
	d <- x + fb * tap;
	x drywet(tap, m)
}

-- chorus: three delay taps modulated by detuned LFOs.
fn chorus(x S, rate AsSignal = 0.3, depth AsSignal = 0.5, m AsSignal = 0.5) S {
	let d = delayVar(0.035 * fs());
	d <- x;
	let base = 0.018 * fs();
	let sweep = 0.007 * fs() * depth;
	let t1 = d vread(2.0 + base + sweep * phasor(rate) usin2pi);
	let t2 = d vread(2.0 + base + sweep * phasor(rate * 0.87, 0.33) usin2pi);
	let t3 = d vread(2.0 + base + sweep * phasor(rate * 1.13, 0.67) usin2pi);
	x drywet((t1 + t2 + t3) * 0.577, m)
}

-- first-order allpass section for the phaser, transposed direct form II
-- (one state): y = -c*x + s1; s = x + c*y
fn apStage(x S, c S) S {
	let s = delayVar();
	let y = s(1) - c * x;
	s <- x + c * y;
	y
}

-- phaser: cascade of swept first-order allpasses summed with the dry signal.
fn phaser(x S, rate AsSignal = 0.4, minFreq AsSignal = 200.0, maxFreq AsSignal = 2000.0,
          fb AsSignal = 0.2, depth AsSignal = 1.0, stages Int = 6) S {
	let f = phasor(rate) usin2pi uniexp(minFreq, maxFreq) min(0.45 * fs());
	let t = tanpi(f * T());
	let c = (1.0 - t) / (1.0 + t);
	let fbv = delayVar();
	let stage = fn(v S) S { v apStage(c) };
	let ap = (x + fb * fbv(1)) chain(stages, stage);
	fbv <- ap;
	(x + depth * ap) * 0.707
}

-- wah: resonant bandpass swept by a pedal position (0..1).
fn wah(x S, pos AsSignal) S {
	let f = pos uclip uniexp(350.0, 2200.0);
	x bpf(f, 0.6) * 1.5
}

-- auto wah: the pedal follows the input envelope.
fn autowah(x S, sens AsSignal = 4.0, atk AsSignal = 0.01, rel AsSignal = 0.15) S =
	x wah(uclip(x envfollow(atk, rel) * sens));

-- rotary speaker: crossover into a fast horn and slower drum rotor, each
-- with Doppler (modulated delay), amplitude modulation, and panning.
-- Mono in (sum stereo first), stereo out. speed in Hz; classic slow ~0.7,
-- fast ~6.5; the speed change is slewed like real rotor inertia.
fn rotary(x S, speed AsSignal = 0.7, m AsSignal = 1.0) S {
	let sp = speed asSignal lag3(0.8);
	let (lo, hi) = x crossover(800.0);

	let hp = phasor(sp);
	let hdv = delayVar(0.004 * fs());
	hdv <- hi;
	let horn = hdv vread(2.0 + 0.0009 * fs() * hp usin2pi);
	let hornT = horn * (0.55 + 0.45 * hp quadrature usin2pi);
	let hornSt = hornT pan(hp sin2pi * 0.85) join;

	let rp = phasor(sp * 0.87);
	let rdv = delayVar(0.003 * fs());
	rdv <- lo;
	let rotor = rdv vread(2.0 + 0.0004 * fs() * rp usin2pi);
	let rotorT = rotor * (0.8 + 0.2 * rp quadrature usin2pi);
	let rotorSt = rotorT pan(rp sin2pi * 0.4) join;

	x drywet(hornSt + rotorSt, m)
}

---------------------------------------------------------------------------
-- Delay effects

-- echo: feedback delay mixed with the dry signal.
fn echo(x S, time AsSignal = 0.4, fb AsSignal = 0.4, m AsSignal = 0.35,
        maxTime Float = 2.0) S {
	let d = delayVar(maxTime * fs() + 4.0);
	let tap = d vread((time * fs()) clip(2.0, maxTime * fs()), Interpolation.linear);
	d <- x + fb * tap;
	x drywet(tap, m)
}

-- ping pong echoes: two cross-fed delay lines bouncing left/right.
-- Mono in, stereo out.
fn pingpong(x S, time AsSignal = 0.3, fb AsSignal = 0.5, m AsSignal = 0.35,
            maxTime Float = 2.0) S {
	let dl = delayVar(maxTime * fs() + 4.0);
	let dr = delayVar(maxTime * fs() + 4.0);
	let ts = (time * fs()) clip(2.0, maxTime * fs());
	let l = dl vread(ts, Interpolation.linear);
	let r = dr vread(ts, Interpolation.linear);
	dl <- x + fb * r;
	dr <- l;
	x drywet([l, r] join, m)
}

-- multitap delay: one write head, a vector of read taps. times (seconds)
-- and gains may be vectors; returns the per-tap signals (mix/pan yourself).
-- Mono in.
fn multitap(x S, times AsSignal, gains AsSignal, maxTime Float = 2.0) S {
	let d = delayVar(maxTime * fs() + 4.0);
	d <- x;
	d vread((times * fs()) clip(2.0, maxTime * fs()), Interpolation.linear) * gains
}

-- stereo delay with a rotation feedback matrix: the stereo feedback pair is
-- rotated by `angle` radians each pass, so echoes migrate across the image.
-- Stereo in, stereo out.
fn matrixDelay(x S, timeL AsSignal = 0.375, timeR AsSignal = 0.5,
               fb AsSignal = 0.6, angle AsSignal = 0.5, m AsSignal = 0.35,
               maxTime Float = 2.0) S {
	let dl = delayVar(maxTime * fs() + 4.0);
	let dr = delayVar(maxTime * fs() + 4.0);
	let l = dl vread((timeL * fs()) clip(2.0, maxTime * fs()), Interpolation.linear);
	let r = dr vread((timeR * fs()) clip(2.0, maxTime * fs()), Interpolation.linear);
	let c = angle cos;
	let s = angle sin;
	dl <- x take(1) + fb * (c * l - s * r);
	dr <- x rotate(1) take(1) + fb * (s * l + c * r);
	x drywet([l, r] join, m)
}

-- diffuser: chained allpasses smearing transients without obvious echoes.
-- size scales the delay lengths; diffusion is the allpass coefficient.
fn diffuser(x S, size Float = 1.0, diffusion Float = 0.625, m AsSignal = 1.0) S {
	let wet = x
		_apg(0.0047 * size, diffusion)
		_apg(0.0071 * size, diffusion)
		_apg(0.0103 * size, diffusion)
		_apg(0.0137 * size, diffusion);
	x drywet(wet, m)
}

---------------------------------------------------------------------------
-- Reverb and sympathetic strings

let kCombTimesL = [0.025306, 0.026939, 0.028956, 0.030748,
                   0.032244, 0.033809, 0.035306, 0.036666];
let kCombTimesR = [0.025828, 0.027460, 0.029478, 0.031270,
                   0.032766, 0.034331, 0.035828, 0.037188];

-- reverb: freeverb-style -- mono-summed input into eight damped combs per
-- channel (right channel detuned), then four series allpasses per channel.
-- decayTime is the -60 dB time; damp rolls off highs in the tail.
-- Stereo in, stereo out.
fn reverb(x S, decayTime AsSignal = 2.5, damp AsSignal = 0.5, m AsSignal = 0.33) S {
	let inm = x sum * 0.4;
	let combL = inm dampcomb(kCombTimesL vec, 0.04, decayTime, damp) sum * 0.125;
	let combR = inm dampcomb(kCombTimesR vec, 0.04, decayTime, damp) sum * 0.125;
	let wetL = combL _apg(0.012608, 0.5) _apg(0.010000, 0.5) _apg(0.007732, 0.5) _apg(0.005102, 0.5);
	let wetR = combR _apg(0.013129, 0.5) _apg(0.010522, 0.5) _apg(0.008254, 0.5) _apg(0.005624, 0.5);
	x drywet([wetL, wetR] join, m)
}

-- resonant / sympathetic strings: a bank of damped combs tuned to a just
-- scale on `root`, ringing along with the input. Mono in.
fn sympathetic(x S, root AsSignal = 110.0, decayTime AsSignal = 4.0,
               damp AsSignal = 0.6, m AsSignal = 0.25) S {
	let ratios = [1.0, 1.125, 1.25, 1.3333333, 1.5, 1.6666667, 1.875, 2.0] vec;
	let freqs = root max(20.0) * ratios;
	let strings = x dampcomb(1.0 / freqs, 0.06, decayTime, damp);
	x drywet(strings sum * 0.35, m)
}

---------------------------------------------------------------------------
-- Dynamics

-- compressor: reduces gain above threshold by ratio, with makeup gain.
fn compressor(x S, threshDb AsSignal = -24.0, ratio AsSignal = 4.0,
              atk AsSignal = 0.01, rel AsSignal = 0.12, makeupDb AsSignal = 0.0) S {
	let env = x envfollow(atk, rel) max(1e-6);
	let overDb = (env ampdb - threshDb) max(0.0);
	x * dbamp(overDb * (1.0 / ratio - 1.0) + makeupDb)
}

-- limiter: hard gain ceiling with fast attack.
fn limiter(x S, ceilDb AsSignal = -1.0, rel AsSignal = 0.05) S {
	let env = x envfollow(0.001, rel) max(1e-9);
	x * min(1.0, ceilDb dbamp / env)
}

-- downward expander: reduces gain below threshold by (ratio - 1).
fn expander(x S, threshDb AsSignal = -45.0, ratio AsSignal = 2.0,
            atk AsSignal = 0.005, rel AsSignal = 0.1) S {
	let env = x envfollow(atk, rel) max(1e-6);
	let underDb = (threshDb - env ampdb) max(0.0);
	x * dbamp(underDb * (1.0 - ratio))
}

-- noise gate: opens above threshold, with attack/release smoothing.
fn noiseGate(x S, threshDb AsSignal = -50.0, atk AsSignal = 0.005, rel AsSignal = 0.1) S {
	let env = x envfollow(0.001, 0.05) max(1e-6);
	x * smooth2(env ampdb > threshDb, atk, rel)
}

-- booster: gain maximizer. Drives the signal up by gainDb and limits the
-- result at ceilDb, raising loudness rather than just amplitude.
fn booster(x S, gainDb AsSignal = 6.0, ceilDb AsSignal = -0.5, rel AsSignal = 0.08) S =
	(x * gainDb dbamp) limiter(ceilDb, rel);

-- swell: removes attacks by fading each new note in over atkT seconds
-- (the "slow gear" effect). The gain is the ratio of a slow-attack envelope
-- to a fast one, so a fresh transient ducks the gain, which then recovers.
fn swell(x S, atkT AsSignal = 0.35) S {
	let fast = x envfollow(0.002, 0.05);
	let slow = x envfollow(atkT, 0.05);
	x * divz(slow, fast, 0.0) min(1.0)
}

---------------------------------------------------------------------------
-- Distortion and color

-- distortion: DC-blocked drive into tanh, then a tone lowpass and level.
fn distortion(x S, driveDb AsSignal = 18.0, tone AsSignal = 0.5, level AsSignal = 0.5) S {
	let f = tone uclip uniexp(500.0, 8000.0);
	(x leakdc(0.995) * driveDb dbamp) tanh lpf(f) * level
}

-- lo fi: sample rate reduction (sample & hold) plus bit depth reduction.
fn lofi(x S, rateHz AsSignal = 8000.0, bits AsSignal = 8.0) S =
	x sampleAndHold(rateHz lfimp) round(exp2(1.0 - bits));

-- Chamberlin state variable filter; returns (lowpass, highpass, bandpass,
-- notch). rq is 1/Q (smaller = more resonant).
fn svf(x S, freq AsSignal, rq AsSignal) (S, S, S, S) {
	let f = 2.0 * min(freq * T(), 0.24) sinpi;
	let lpv = delayVar();
	let bpv = delayVar();
	let lp = lpv(1) + f * bpv(1);
	let hp = x - lp - rq * bpv(1);
	let bp = bpv(1) + f * hp;
	lpv <- lp;
	bpv <- bp;
	(lp, hp, bp, hp + lp)
}

-- multimode filter effect: typ selects 0 lowpass, 1 highpass, 2 bandpass,
-- 3 notch.
fn multiFilter(x S, typ AsSignal, freq AsSignal, rq AsSignal) S {
	let (lp, hp, bp, br) = x svf(freq, rq);
	-- select's index must be an integer type; a float (e.g. a control) would
	-- fail type inference
	typ asSignal i32 select([lp, hp, bp, br])
}

---------------------------------------------------------------------------
-- Pitch effects

-- pitch shifter: two-tap crossfaded delay ("granular" shifter). windowSec
-- must be constant; larger windows smear more but warble less.
fn pitchShift(x S, semitones AsSignal = 0.0, windowSec Float = 0.1) S {
	let d = delayVar(windowSec * fs() + 8.0);
	d <- x;
	let ratio = semitones asSignal stratio;
	let f = (1.0 - ratio) / windowSec;
	let p1 = phasor(f);
	let p2 = frac(p1 + 0.5);
	let w = windowSec * fs();
	d vread(2.0 + p1 * w) * p1 sinwin + d vread(2.0 + p2 * w) * p2 sinwin
}

-- one grain stream for the granulator
fn _grain(d DelayVar, grainRate S, spreadSamps S, ratio S, maxIdx S, phase0 Float) S {
	let p = phasor(grainRate, phase0);
	let off = (urand() * spreadSamps) sampleAndHold(p eoc);
	let dur = fs() / grainRate;
	let idx = (off + p * dur * (1.0 - ratio) + 2.0) clip(2.0, maxIdx);
	d vread(idx) * p han
}

-- granulator: four overlapping Hann-windowed grain streams reading from a
-- ring buffer of the live input at random offsets up to `spread` seconds
-- back, optionally repitched by `pitch` semitones.
fn granulator(x S, grainRate AsSignal = 12.0, spread AsSignal = 0.25,
              pitch AsSignal = 0.0, m AsSignal = 0.5, maxSpread Float = 1.0) S {
	let d = delayVar((maxSpread + 0.05) * fs());
	d <- x;
	let gr = grainRate asSignal;
	let ratio = pitch asSignal stratio;
	let ss = spread asSignal min(maxSpread) * fs();
	let maxIdx = (maxSpread + 0.04) * fs();
	let g = _grain(d, gr, ss, ratio, maxIdx, 0.0)
	      + _grain(d, gr, ss, ratio, maxIdx, 0.25)
	      + _grain(d, gr, ss, ratio, maxIdx, 0.5)
	      + _grain(d, gr, ss, ratio, maxIdx, 0.75);
	x drywet(g * 0.6, m)
}

---------------------------------------------------------------------------
-- Vocal formant filter

-- four formants (F1..F4) per vowel: a, e, i, o, u (tenor voice data;
-- four rather than five so the formant bank keeps a power-of-two
-- channel count)
let kVowelFreqs = [
	[650.0, 1080.0, 2650.0, 2900.0],
	[400.0, 1700.0, 2600.0, 3200.0],
	[290.0, 1870.0, 2800.0, 3250.0],
	[400.0, 800.0, 2600.0, 2800.0],
	[350.0, 600.0, 2700.0, 2900.0]
];
let kVowelAmps = [
	[1.0, 0.501, 0.447, 0.398],
	[1.0, 0.200, 0.251, 0.200],
	[1.0, 0.178, 0.126, 0.100],
	[1.0, 0.316, 0.251, 0.251],
	[1.0, 0.100, 0.141, 0.200]
];
let kVowelBws = [
	[0.123, 0.083, 0.045, 0.045],
	[0.175, 0.047, 0.038, 0.038],
	[0.138, 0.048, 0.036, 0.037],
	[0.100, 0.100, 0.038, 0.043],
	[0.114, 0.100, 0.037, 0.041]
];

fn _vowelTable(tab [[Float]], vi S, vj S, vf S) S {
	let vecs = [tab[0] vec, tab[1] vec, tab[2] vec, tab[3] vec, tab[4] vec];
	vf lerp(vi select(vecs), vj select(vecs))
}

-- vocal formant filter: five parallel bandpasses at vowel formants.
-- vowel sweeps 0=a, 1=e, 2=i, 3=o, 4=u with interpolation between
-- neighbors. Mono in.
fn formant(x S, vowel AsSignal = 0.0, m AsSignal = 1.0) S {
	let v = vowel asSignal clip(0.0, 4.0);
	let vi = v floor;
	let vj = min(vi + 1.0, 4.0);
	let vf = v - vi;
	let f = kVowelFreqs _vowelTable(vi, vj, vf);
	let a = kVowelAmps _vowelTable(vi, vj, vf);
	let bw = kVowelBws _vowelTable(vi, vj, vf);
	x drywet((x bpf(f, bw) * a) sum * 2.0, m)
}

---------------------------------------------------------------------------
---------------------------------------------------------------------------
-- Synthdef layer: ready-made stereo effect defs.
--
-- Each is a pure graph function: stereo inlet -> effect -> outlet, with
-- controls for the interesting parameters. Compile with defSynthX.

fn _fxIn() S = inlet(FLOAT32, 2);

fn _lin(lo Float, hi Float, init Float) ControlSpec =
	ControlSpec { lo: lo, hi: hi, init: init, warp: ControlWarp.linear };
fn _exp(lo Float, hi Float, init Float) ControlSpec =
	ControlSpec { lo: lo, hi: hi, init: init, warp: ControlWarp.exponential };

fn _mixCtl(init Float = 0.5) S = control("mix", _lin(0.0, 1.0, init));

fn fxTremolo() S {
	let x = _fxIn();
	let rate = control("rate", _exp(0.1, 20.0, 5.0));
	let depth = control("depth", _lin(0.0, 1.0, 0.6));
	x tremolo(rate, depth) outlet
}

fn fxVibrato() S {
	let x = _fxIn();
	let rate = control("rate", _exp(0.1, 12.0, 5.0));
	let depth = control("depth", _exp(0.01, 12.0, 0.3));  -- semitones
	x vibrato(rate, depth) outlet
}

fn fxFlanger() S {
	let x = _fxIn();
	let rate = control("rate", _exp(0.05, 5.0, 0.25));
	let depth = control("depth", _lin(0.0, 1.0, 1.0));
	let fb = control("feedback", _lin(0.0, 0.95, 0.5));
	let m = _mixCtl();
	x flanger(rate, depth, fb, m) outlet
}

fn fxChorus() S {
	let x = _fxIn();
	let rate = control("rate", _exp(0.05, 3.0, 0.3));
	let depth = control("depth", _lin(0.0, 1.0, 0.5));
	let m = _mixCtl();
	x chorus(rate, depth, m) outlet
}

fn fxPhaser() S {
	let x = _fxIn();
	let rate = control("rate", _exp(0.05, 8.0, 0.4));
	let fb = control("feedback", _lin(0.0, 0.9, 0.2));
	let depth = control("depth", _lin(0.0, 1.0, 1.0));
	x phaser(rate, 200.0, 2000.0, fb, depth) outlet
}

fn fxWah() S {
	let x = _fxIn();
	let pos = control("pedal", _lin(0.0, 1.0, 0.5));
	x wah(pos lag(0.02)) outlet
}

fn fxAutoWah() S {
	let x = _fxIn();
	let sens = control("sens", _exp(0.5, 16.0, 4.0));
	let atk = control("attack", _exp(0.001, 0.1, 0.01));
	let rel = control("release", _exp(0.02, 1.0, 0.15));
	x autowah(sens, atk, rel) outlet
}

fn fxRotary() S {
	let x = _fxIn();
	let speed = control("speed", _exp(0.1, 8.0, 0.7));
	let m = _mixCtl(1.0);
	let wet = (x sum * 0.7) rotary(speed, 1.0);
	x drywet(wet, m) outlet
}

fn fxEcho() S {
	let x = _fxIn();
	let time = control("time", _exp(0.02, 2.0, 0.4)) lag(0.2);
	let fb = control("feedback", _lin(0.0, 0.95, 0.4));
	let m = _mixCtl(0.35);
	x echo(time, fb, m) outlet
}

fn fxPingPong() S {
	let x = _fxIn();
	let time = control("time", _exp(0.02, 2.0, 0.3)) lag(0.2);
	let fb = control("feedback", _lin(0.0, 0.95, 0.5));
	let m = _mixCtl(0.35);
	let wet = (x sum * 0.7) pingpong(time, fb, 1.0);
	x drywet(wet, m) outlet
}

fn fxMultiTap() S {
	let x = _fxIn();
	let time = control("time", _exp(0.05, 2.0, 0.8)) lag(0.2);
	let m = _mixCtl(0.35);
	let taps = (x sum * 0.7) multitap(
		time * [0.25, 0.5, 0.75, 1.0] vec,
		[0.9, 0.7, 0.55, 0.4] vec);
	-- even taps left, odd taps right (channel counts must stay powers of two,
	-- so rotate+stride rather than drop+stride)
	let wet = [taps stride(2) sum, taps rotate(1) stride(2) sum] join;
	x drywet(wet, m) outlet
}

fn fxMatrixDelay() S {
	let x = _fxIn();
	let timeL = control("timeL", _exp(0.02, 2.0, 0.375)) lag(0.2);
	let timeR = control("timeR", _exp(0.02, 2.0, 0.5)) lag(0.2);
	let fb = control("feedback", _lin(0.0, 0.95, 0.6));
	let angle = control("angle", _lin(0.0, 1.5707963, 0.5)) lag(0.1);
	let m = _mixCtl(0.35);
	x matrixDelay(timeL, timeR, fb, angle, m) outlet
}

fn fxDiffuser() S {
	let x = _fxIn();
	let m = _mixCtl(1.0);
	x diffuser(1.0, 0.625, m) outlet
}

fn fxReverb() S {
	let x = _fxIn();
	let dcy = control("decay", _exp(0.2, 15.0, 2.5));
	let damp = control("damp", _lin(0.0, 0.9, 0.5));
	let m = _mixCtl(0.33);
	x reverb(dcy, damp, m) outlet
}

fn fxSympathetic() S {
	let x = _fxIn();
	let root = control("root", _exp(30.0, 400.0, 110.0));
	let dcy = control("decay", _exp(0.5, 12.0, 4.0));
	let damp = control("damp", _lin(0.0, 0.9, 0.6));
	let m = _mixCtl(0.25);
	let w = (x sum * 0.7) sympathetic(root, dcy, damp, 1.0);
	x drywet([w, w] join, m) outlet
}

fn fxCompressor() S {
	let x = _fxIn();
	let thresh = control("threshold", _lin(-60.0, 0.0, -24.0));
	let ratio = control("ratio", _exp(1.0, 20.0, 4.0));
	let atk = control("attack", _exp(0.001, 0.1, 0.01));
	let rel = control("release", _exp(0.02, 1.0, 0.12));
	let makeup = control("makeup", _lin(0.0, 24.0, 0.0));
	x compressor(thresh, ratio, atk, rel, makeup) outlet
}

fn fxLimiter() S {
	let x = _fxIn();
	let ceilDb = control("ceiling", _lin(-24.0, 0.0, -1.0));
	let rel = control("release", _exp(0.01, 0.5, 0.05));
	x limiter(ceilDb, rel) outlet
}

fn fxExpander() S {
	let x = _fxIn();
	let thresh = control("threshold", _lin(-80.0, 0.0, -45.0));
	let ratio = control("ratio", _lin(1.0, 8.0, 2.0));
	let atk = control("attack", _exp(0.001, 0.1, 0.005));
	let rel = control("release", _exp(0.02, 1.0, 0.1));
	x expander(thresh, ratio, atk, rel) outlet
}

fn fxNoiseGate() S {
	let x = _fxIn();
	let thresh = control("threshold", _lin(-80.0, -10.0, -50.0));
	let atk = control("attack", _exp(0.001, 0.05, 0.005));
	let rel = control("release", _exp(0.02, 1.0, 0.1));
	x noiseGate(thresh, atk, rel) outlet
}

fn fxBooster() S {
	let x = _fxIn();
	let gain = control("gain", _lin(0.0, 24.0, 6.0));
	let ceilDb = control("ceiling", _lin(-12.0, 0.0, -0.5));
	let rel = control("release", _exp(0.01, 0.5, 0.08));
	x booster(gain, ceilDb, rel) outlet
}

fn fxSwell() S {
	let x = _fxIn();
	let atk = control("attack", _exp(0.05, 2.0, 0.35));
	x swell(atk) outlet
}

fn fxDistortion() S {
	let x = _fxIn();
	let drive = control("drive", _lin(0.0, 40.0, 18.0));
	let tone = control("tone", _lin(0.0, 1.0, 0.5));
	let level = control("level", _lin(0.0, 1.0, 0.5));
	x distortion(drive, tone, level) outlet
}

fn fxLoFi() S {
	let x = _fxIn();
	let rate = control("rate", _exp(500.0, 20000.0, 8000.0));
	let bits = control("bits", _lin(2.0, 16.0, 8.0));
	x lofi(rate, bits) outlet
}

fn fxFilter() S {
	let x = _fxIn();
	let typ = choice("type", 4);
	let freq = control("freq", _exp(40.0, 12000.0, 1200.0)) lag(0.05);
	let rq = control("rq", _lin(0.05, 2.0, 1.0));
	x multiFilter(typ, freq, rq) outlet
}

fn fxPitchShift() S {
	let x = _fxIn();
	let semi = control("semitones", _lin(-24.0, 24.0, 0.0));
	let m = _mixCtl(1.0);
	x drywet(x pitchShift(semi), m) outlet
}

fn fxGranulator() S {
	let x = _fxIn();
	let rate = control("rate", _exp(1.0, 100.0, 12.0));
	let spr = control("spread", _lin(0.0, 1.0, 0.25));
	let pitch = control("pitch", _lin(-24.0, 24.0, 0.0));
	let m = _mixCtl(0.5);
	x granulator(rate, spr, pitch, m) outlet
}

fn fxFormant() S {
	let x = _fxIn();
	let vowel = control("vowel", _lin(0.0, 4.0, 0.0)) lag(0.05);
	let m = _mixCtl(1.0);
	let w = (x sum * 0.7) formant(vowel, 1.0);
	x drywet([w, w] join, m) outlet
}
