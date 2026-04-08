-- RBJ Audio EQ Cookbook Biquad Filters
-- Reference: Robert Bristow-Johnson's Audio EQ Cookbook
--
-- fc  = cutoff or center frequency in Hertz
-- bw  = bandwidth in octaves
-- rq  = resonance expressed as the reciprocal of Q (1/Q)
-- db  = gain in decibels (for shelving and parametric EQ)

import synthdef.*;
import common_ugens.*;

type BiquadCoeffs = (S, S, S, S, S);  -- b0, b1, b2, a1, a2

let kLog2o2 = 2.0 log / 2.0;  -- ln(2) / 2

-- ============================================================
-- Internal helpers
-- ============================================================

-- angular frequency: w0 = 2*pi*fc/fs
fn _w0(fc AsSignal) S = twopi * fc * T();

-- alpha from rq (= 1/Q): alpha = sin(w0) / (2*Q) = sin(w0) * rq / 2
fn _alphaRQ(sw0, rq) = sw0 * rq * 0.5;

-- alpha from bandwidth in octaves:
-- alpha = sin(w0) * sinh(ln(2)/2 * bw * w0 / sin(w0))
fn _alphaBW(w0, sw0, bw) = sw0 * (kLog2o2 * bw * w0 / sw0) sinh;

-- normalize all coefficients by a0
fn _normCoeffs(b0, b1, b2, a0, a1, a2) BiquadCoeffs {
    let ra0 = 1.0 / a0;
    (b0 * ra0, b1 * ra0, b2 * ra0, a1 * ra0, a2 * ra0)
}

-- ============================================================
-- Coefficient functions
-- ============================================================

-- resonant lowpass filter coefficients
fn rlpfCoeffs(fc AsSignal, rq AsSignal) BiquadCoeffs {
    let w0 S = fc _w0;
    let sw0 S = w0 sin;
    let cw0 S = w0 cos;
    let alpha S = sw0 _alphaRQ(rq);
    let omc S = 1.0 - cw0;
    _normCoeffs(omc * 0.5, omc, omc * 0.5, 1.0 + alpha, -2.0 * cw0, 1.0 - alpha)
}

-- resonant highpass filter coefficients
fn rhpfCoeffs(fc AsSignal, rq AsSignal) BiquadCoeffs {
    let w0 S = fc _w0;
    let sw0 S = w0 sin;
    let cw0 S = w0 cos;
    let alpha S = sw0 _alphaRQ(rq);
    let opc S = 1.0 + cw0;
    _normCoeffs(opc * 0.5, -opc, opc * 0.5, 1.0 + alpha, -2.0 * cw0, 1.0 - alpha)
}

-- bandpass filter coefficients (constant 0 dB peak gain)
fn bpfCoeffs(fc AsSignal, bw AsSignal) BiquadCoeffs {
    let w0 S = fc _w0;
    let sw0 S = w0 sin;
    let cw0 S = w0 cos;
    let alpha S = w0 _alphaBW(sw0, bw);
    _normCoeffs(alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * cw0, 1.0 - alpha)
}

-- band reject (notch) filter coefficients
fn brfCoeffs(fc AsSignal, bw AsSignal) BiquadCoeffs {
    let w0 S = fc _w0;
    let sw0 S = w0 sin;
    let cw0 S = w0 cos;
    let alpha S = w0 _alphaBW(sw0, bw);
    let mc2 S = -2.0 * cw0;
    _normCoeffs(1.0, mc2, 1.0, 1.0 + alpha, mc2, 1.0 - alpha)
}

-- allpass filter coefficients
fn apfCoeffs(fc AsSignal, rq AsSignal) BiquadCoeffs {
    let w0 S = fc _w0;
    let sw0 S = w0 sin;
    let cw0 S = w0 cos;
    let alpha S = sw0 _alphaRQ(rq);
    let mc2 S = -2.0 * cw0;
    _normCoeffs(1.0 - alpha, mc2, 1.0 + alpha, 1.0 + alpha, mc2, 1.0 - alpha)
}

-- low shelf filter coefficients
fn lowShelfCoeffs(fc AsSignal, db AsSignal, rq AsSignal) BiquadCoeffs {
    let A = (db * 0.025) exp10;  -- 10^(dBgain/40)
    let w0 S = fc _w0;
    let sw0 S = w0 sin;
    let cw0 S = w0 cos;
    let alpha S = sw0 _alphaRQ(rq);
    let tsa S = 2.0 * A sqrt * alpha;
    let ap1 = A + 1.0;
    let am1 = A - 1.0;
    _normCoeffs(
        A * (ap1 - am1 * cw0 + tsa),
        2.0 * A * (am1 - ap1 * cw0),
        A * (ap1 - am1 * cw0 - tsa),
        ap1 + am1 * cw0 + tsa,
        -2.0 * (am1 + ap1 * cw0),
        ap1 + am1 * cw0 - tsa
    )
}

-- high shelf filter coefficients
fn highShelfCoeffs(fc AsSignal, db AsSignal, rq AsSignal) BiquadCoeffs {
    let A = (db * 0.025) exp10;  -- 10^(dBgain/40)
    let w0 S = fc _w0;
    let sw0 S = w0 sin;
    let cw0 S = w0 cos;
    let alpha S = sw0 _alphaRQ(rq);
    let tsa S = 2.0 * A sqrt * alpha;
    let ap1 = A + 1.0;
    let am1 = A - 1.0;
    _normCoeffs(
        A * (ap1 + am1 * cw0 + tsa),
        -2.0 * A * (am1 + ap1 * cw0),
        A * (ap1 + am1 * cw0 - tsa),
        ap1 - am1 * cw0 + tsa,
        2.0 * (am1 - ap1 * cw0),
        ap1 - am1 * cw0 - tsa
    )
}

-- parametric EQ (peaking) filter coefficients
fn peakEQCoeffs(fc AsSignal, db AsSignal, bw AsSignal) BiquadCoeffs {
    let A = (db * 0.025) exp10;  -- 10^(dBgain/40)
    let w0 S = fc _w0;
    let sw0 S = w0 sin;
    let cw0 S = w0 cos;
    let alpha S = w0 _alphaBW(sw0, bw);
    let mc2 S = -2.0 * cw0;
    _normCoeffs(
        1.0 + alpha * A,
        mc2,
        1.0 - alpha * A,
        1.0 + alpha / A,
        mc2,
        1.0 - alpha / A
    )
}

-- lowpass filter coefficients (default rq = sqrt(0.5))
fn lpfCoeffs(fc AsSignal) BiquadCoeffs = rlpfCoeffs(fc, 0.5 sqrt);

-- highpass filter coefficients (default rq = sqrt(0.5))
fn hpfCoeffs(fc AsSignal) BiquadCoeffs = rhpfCoeffs(fc, 0.5 sqrt);

-- ============================================================
-- Biquad filter (Transposed Direct Form II)
-- ============================================================

fn biquad(in S, coeffs BiquadCoeffs) S {
    let b0 = coeffs.0;
    let b1 = coeffs.1;
    let b2 = coeffs.2;
    let a1 = coeffs.3;
    let a2 = coeffs.4;
    let s1 = delayVar();
    let s2 = delayVar();
    let out = b0 * in + s1(1);
    s1 <- b1 * in - a1 * out + s2(1);
    s2 <- b2 * in - a2 * out;
    out
}

-- a cascade of two biquads with the same coefficients
fn biquad2(in S, coeffs BiquadCoeffs) S = in biquad(coeffs) biquad(coeffs);

-- ============================================================
-- Filter unit generators
-- ============================================================

-- 12 dB/8ve filters
fn lpf(in S, fc AsSignal) S = in biquad(fc lpfCoeffs);
fn hpf(in S, fc AsSignal) S = in biquad(fc hpfCoeffs);
fn rlpf(in S, fc AsSignal, rq AsSignal) S = in biquad(fc rlpfCoeffs(rq));
fn rhpf(in S, fc AsSignal, rq AsSignal) S = in biquad(fc rhpfCoeffs(rq));
fn bpf(in S, fc AsSignal, bw AsSignal) S = in biquad(fc bpfCoeffs(bw));
fn brf(in S, fc AsSignal, bw AsSignal) S = in biquad(fc brfCoeffs(bw));
fn apf(in S, fc AsSignal, rq AsSignal) S = in biquad(fc apfCoeffs(rq));
fn lowShelf(in S, fc AsSignal, db S, rq AsSignal) S = in biquad(fc lowShelfCoeffs(db, rq));
fn highShelf(in S, fc AsSignal, db S, rq AsSignal) S = in biquad(fc highShelfCoeffs(db, rq));
fn peakEQ(in S, fc AsSignal, db S, bw AsSignal) S = in biquad(fc peakEQCoeffs(db, bw));


-- 24 dB/8ve filters
fn lpf2(in S, fc AsSignal) S = in biquad2(fc lpfCoeffs);
fn hpf2(in S, fc AsSignal) S = in biquad2(fc hpfCoeffs);
fn rlpf2(in S, fc AsSignal, rq AsSignal) S = in biquad2(fc rlpfCoeffs(rq));
fn rhpf2(in S, fc AsSignal, rq AsSignal) S = in biquad2(fc rhpfCoeffs(rq));
fn apf2(in S, fc AsSignal, rq AsSignal) S = in biquad2(fc apfCoeffs(rq));

fn crossover(in S, freq AsSignal) (S, S) = (in lpf2(freq), in hpf2(freq));

-- ringing filter
fn ring(x S, freq AsSignal, ringTime AsSignal) S {
	let K = log001 * T();
	let R = 1 + K / ringTime;
	let cs = freq hzw cos
	let a1 = 2 * R * cs;
	let a2 = -(R sq);
	let b0 = 0.5;
	let y = delayVar();
	y <- b0 * (x - z2(x)) + a1 * y(1) + a2 * y(2);
}

fn pling(x S, freq AsSignal, atkTime AsSignal, dcyTime AsSignal) S {
	x ring(freq, dcyTime) - x ring(freq atkTime)
}

