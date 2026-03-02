//
//  synthdef_common_ops.hpp
//  synthdef-after-rust-cpp-xcode
//
//  Created by James McCartney on 3/6/24.
//

#pragma once

#include "synthdef_builtin_ops.hpp"

namespace synthdef {
    
//    S dot(S a, S b, usize rows = 1);
    
    S sgn(S a); // sign function
    S sgnz(S a); // non-zero sign function
        
    S sq(S a);
    S cb(S a);
    
    S ssq(S a);
    S ssqrt(S x);
    S spow(S a, S b);


    S divz(S numer, S denom, S alt); // protects against divide by zero.
    S sinc(S x);
    S sinc1(S x); // sinc with zero crossings at integer values of x.

    S princ1(S x);
    S frac(S a);

    S isuni(S x);
    S isbi(S x);
    S isint(S x);

    // range mappings
    S bi(S x);
    S uni(S x);
    S lin(S x, S m, S b);

    S clip(S x, S a, S b);
    S clip0(S x, S a);
    S clip2(S x, S a);
    S uclip(S x);
    S bclip(S x);
    S excess(S x, S b);

    S wrap(S x, S a, S b);
    S wrap0(S x, S a);
    S wrap2(S x, S a);
    S uwrap(S x);
    S bwrap(S x);

    S fold(S x, S a, S b);
    S fold0(S x, S a);
    S fold2(S x, S a);
    S ufold(S x);
    S bfold(S x);

    S unilin(S x, S a, S b);
    S uniexp(S x, S a, S b);
    S linuni(S x, S a, S b);
    S expuni(S x, S a, S b);

    S bilin(S x, S a, S b);
    S biexp(S x, S a, S b);
    S linbi(S x, S a, S b);
    S expbi(S x, S a, S b);

    S linlin(S x, S a, S b, S c, S d);
    S linexp(S x, S a, S b, S c, S d);
    S explin(S x, S a, S b, S c, S d);
    S expexp(S x, S a, S b, S c, S d);

    S linlinc(S x, S a, S b, S c, S d); // linear to linear with clamping
    S linexpc(S x, S a, S b, S c, S d); // linear to exponential with clamping
    S explinc(S x, S a, S b, S c, S d); // exponential to linear with clamping
    S expexpc(S x, S a, S b, S c, S d); // exponential to exponential with clamping

    S unilinc(S x, S a, S b, S c, S d); // unipolar to linear with clamping
    S uniexpc(S x, S a, S b, S c, S d); // unipolar to exponential with clamping
    S bilinc(S x, S a, S b, S c, S d);  // bipolar to linear with clamping
    S biexpc(S x, S a, S b, S c, S d);  // bipolar to exponential with clamping

    S invert(S x, S a);
    S invert(S x, S a, S b);

    // warping
    S bwarp(S x, S w);  // bipolar to bipolar warping
    S bwarps(S x, S w); // bipolar to bipolar S warping
    S warp(S x, S w); // unipolar to unipolar warping
    S warps(S x, S w); // unipolar to unipolar S warping

    S phasewarp1(S p, S w);

    // range mapping with warping
    S unilinw(S x, S w, S a, S b);  // unipolar to linear with warping
    S unilinws(S x, S w, S a, S b); // unipolar to linear with S warping
    S bilinw(S x, S w, S a, S b);   // bipolar to linear with warping
    S bilinws(S x, S w, S a, S b);  // bipolar to linear with S warping
    S linlinw(S x, S w, S a, S b, S c, S d);  // linear to linear with warping
    S linlinws(S x, S w, S a, S b, S c, S d); // linear to linear with S warping

    // chebyshev polynomials
    S cheby(S x, int n);

    S chebyv(S x, S n);

    // sigmoid saturation/distortion functions
    // roughly in order from quickest to slowest to saturate.
    S sigmoid(S x, int n);

    // units conversions
    S ampdb(S x);
    S dbamp(S x);

    S hzw(S x); // Hertz to radians per sample
    S whz(S x); // radians per sample to Hertz

    S nnhz(S x);
    S hznn(S x);

    S ratiocents(S x);
    S centsratio(S x);

    S ratiosemi(S x);
    S semiratio(S x);

    S ochhz(S x);
    S hzoct(S x);

    S degrad(S x);
    S raddeg(S x);

    S bpmhz(S x); // beats per minute to Hertz
    S hzbpm(S x); // Hertz to beats per minute

    S bpmsec(S x); // bpmsec is an involution.
    S secbpm(S x); // bpmsec is an involution.

    // pass a signal only if it is above or below a threshold
    S above(S x, S m);
    S below(S x, S m);

    // pass a signal only if its absolute value is above or below a threshold
    S absabove(S x, S m);
    S absbelow(S x, S m);

    // variants of amplitude modulation
    S vca(S x, S a);
    S scaleneg(S x, S a);
    S scalepos(S x, S a);
    S scalenegpos(S x, S a, S b);

    // variants of ring modulation
    S ring1(S a, S b);
    S ring2(S a, S b);
    S ring3(S a, S b);
    S ring4(S a, S b);

    // various other functions from SuperCollider
    S sumsq(S a, S b);
    S difsq(S a, S b);
    S sqsum(S a, S b);
    S sqdif(S a, S b);
    S absdif(S a, S b);
    S thresh(S a, S b);

    S rising(S x);
    S falling(S x);
    S nochange(S x);
    S changing(S x);
    S peak(S x);
    S trough(S x);

    // triggers
    S tr(S x); // 'triggerization'. detects transition from x <= 0 to x > 0.
    S eoc(S x);   // end of cycle. detects wrapping of a unipolar phasor
    S init(); // one initial impulse, then zero forever.
        
    S sah(S x, S t);

    // flip flops
    S toggle(S x);

    // set, reset
    S setreset(S s, S r) ;

    // set, reset, toggle
    S srt(S s, S r, S t);

    // unipolar phasor waveshaping
    S sawshift(S x, S shift); // phase shift a unipolar sawtooth.
    S quadr(S x);  // quadrature
    //S multiphasor(S x, int n);

    S sin1(S x);
    S cos1(S x);
    S tan1(S x);

    S sin2pi(S x) ;
    S cos2pi(S x) ;
    S usin2pi(S x);
    S ucos2pi(S x);

    S fsin(S x);  // fast approximation of sine
    S fsinx(S x); // fast approximation of sine with extra precision
    S fcos(S x);  // fast approximation of cosine
    S fcosx(S x); // fast approximation of cosine

        // square waves
    S usquare(S x); // unipolar square wave
    S bsquare(S x); // bipolar square wave

        // triangle waves
    S utri(S x); // unipolar triangle wave
    S btri(S x); // bipolar triangle wave

        // variable triangle and saw
        // an amplitude scaled difference of parabolas
    S par(S x);   // unipolar ramp to parabola
    S vartri(S x, S pwm);
    S varsaw(S x, S pwm);

        // pulse waves
    S upulse(S x, S pwm);                          // unipolar pulse wave
    S bpulse(S x, S pwm);                // bipolar pulse wave
    S zpulse(S x, S pwm);      // zero DC pulse wave

        // pulse waves. as in SuperCollider, these always output one value of the opposite polarity each cycle.
    S upulse1(S x, S pwm);       // unipolar
    S bpulse1(S x, S pwm);                  // bipolar
    S zpulse1(S x, S pwm); // zero DC

    // triangle waves with various starting phases
        // bipolar
    S btri0(S x); // 0   degrees. bipolar output. '\,  trisin
    S btri1(S x);        // 90  degrees. bipolar output. \/   tricos
    S btri2(S x); // 180 degrees. bipolar output. ,/' -trisin
    S btri3(S x);        // 270 degrees. bipolar output. /\  -tricos
        // unipolar
    S utri0(S x); // 0   degrees. unipolar output. '\,  trisin
    S utri1(S x);          // 90  degrees. unipolar output. \/   tricos
    S utri2(S x);   // 180 degrees. unipolar output. ,/' -trisin
    S utri3(S x);        // 270 degrees. unipolar output. /\  -tricos

    // trapezoid waves
    S trapez0(S x); // 0   degrees. bipolar output.
    S trapez1(S x);        // 90  degrees. bipolar output.
    S trapez2(S x); // 180 degrees. bipolar output.
    S trapez3(S x);        // 270 degrees. bipolar output.

        // inspired by the Intellijel Rubicon waveform.
    S izigzag(S x); // moving inwards
    S ozigzag(S x); // moving outwards

    // smooth min, max, step
    S smooth_adjustment(S a, S b, S k);
        
    S smin(S a, S b, S k);
    S smax(S a, S b, S k);
        
    S sstep(S x); // smooth step
    S sstep2(S x); // smoother step

    S lerp(S p, S a, S b);  // linear interpolation. (same as unilin).

    // waveforms with dynamic shape
    // sine pulses with a saw envelope
    S sawenvsin(S x, S n, S m);
    // sine pulses with a saw envelope of alternating polarity
    S altsawenvsin(S x, S n, S m);

    // differentiation
    S diff(S x);  // unscaled sample to sample difference
    S slope(S x); // change per second
    S accel(S x);
    S jerk(S x);

    // integration
    S unscaledIntegrator(S x)   ;
    S trapezoidalIntegrator(S x);
    S forwardIntegrator(S x)    ;
    S backwardIntegrator(S x)   ;

    // with reset
    S unscaledIntegrator(S x, S r)   ;
    S trapezoidalIntegrator(S x, S r);
    S forwardIntegrator(S x, S r)    ;
    S backwardIntegrator(S x, S r)   ;

    S minfollow(S x, S r);
    S maxfollow(S x, S r);

    S leaky(S x, S a);

    S by(S start, S step);
    S grow(S start, S step);


    // exponential decay
    S decayCoeff(S n, S amp);

    S decay40dB(S n);
    S decay60dB(S n);

    S decay(S x, S t);
    S decay2(S x, S atk, S dcy);

    //S iqimpulse(S x, S k); // from Inigo Quiles https://www.iquilezles.org/www/articles/functions/functions.htm

    S combn(S x, S delayTime, S decayTime);
    S combl(S x, S delayTime, S maxDelayTime, S decayTime);

    // simple filters
    S onepole(S x, S a);
    S onezero(S x, S a);
    S leakdc(S x, S k);

    S lag(S x, S t);

    // panning and crossfading

    S panfun(S x);

    S panfuns(S x);

    //S xfade(S pos, S a, S b) {
    //    return sum(vec(a, b) * panfuns(uni(pos)));
    //}

    S pan(S pos, S x);


    // oscillator functions
    S phasor(S phaseIncrement, S pm = 0);

    S sinosc(S fm, S pm = 0);
    S fsinosc(S fm, S pm = 0);
    S fsinxosc(S fm, S pm = 0);
//    std::pair<S, S> sincos_osc(S fm, S pm = 0);

        // lf = low frequency. geometrically precise, non-antialiased oscillators.
    S lfsaw (S fm, S pm = 0);       // saw
    S lfimp (S fm, S pm = 0);       // impulses
    S lftri (S fm, S pm = 0);       // triangle wave
    S lftrap(S fm, S pm = 0);       // trapezoid wave
    S lfusqr(S fm, S pm = 0);       // unipolar square
    S lfsqr (S fm, S pm = 0);       // bipolar square
    S lfzig (S fm, S pm = 0);       // inwards zigzag
    S lfzag (S fm, S pm = 0);       // outwards zigzag
    S lfpar (S fm, S pm = 0);       // parabolic wave
    S lfvsaw  (S fm, S pwm, S pm = 0);  // variable saw
    S lfupulse(S fm, S pwm, S pm = 0);  // variable unipolar pulse
    S lfpulse (S fm, S pwm, S pm = 0);  // variable bipolar pulse
    S lfzpulse(S fm, S pwm, S pm = 0);  // zero DC variable pulse

    // random numbers and noise
    S rand(S a, S b, usize chans = 1, SignalRate rate = audioSignalRate);
    S irand(S n, usize chans, SignalRate rate = audioSignalRate);
    S xrand(S a, S b, usize chans = 1, SignalRate rate = audioSignalRate);
    S rand2(S a, usize chans = 1, SignalRate rate = audioSignalRate);
    S linrand(S a, S b, usize chans = 1, SignalRate rate = audioSignalRate);
    S trirand(S a, S b, usize chans = 1, SignalRate rate = audioSignalRate);
    S randsign(usize chans = 1, SignalRate rate = audioSignalRate);

    S pinkingFilter(S x);
    S pinkingFilterEco(S x);

    S white(usize chans = 1);
    S WHITE(usize chans = 1);     // random full positive or negative

    S red(usize chans = 1)   ;
    S pink(usize chans = 1)  ;
    S pinke(usize chans = 1) ;
    S blue(usize chans = 1)  ;
    S violet(usize chans = 1);

    S coin(S p, usize chans = 1, SignalRate rate = audioSignalRate);

    S velvet(S freq, usize chans = 1);
    S dust(S freq, usize chans = 1);
    S dust2(S freq, usize chans = 1);

    S dustep(S freq, usize chans = 1);
    S fadein(S x, S t);

    S tremolo(S x, S freq, S amount, S pm = 0);

    S pause(S env, std::function<S()> gated);
    S pull(S gate, std::function<S()> f, S initval = 0);



}
