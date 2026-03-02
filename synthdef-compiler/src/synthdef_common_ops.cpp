//
//  synthdef_common_ops.cpp
//  synthdef-after-rust-cpp-xcode
//
//  Created by James McCartney on 3/6/24.
//

#include "synthdef_common_ops.hpp"

namespace synthdef {

//    S dot(S a, S b, usize rows) { return sum(a * b, rows); }

    
    S sgn(S a) { return sel2(a > 0, 1, sel2(a < 0, -1, 0)); }
    S sgnz(S a) { return sel2(a > 0, 1, -1); }
        
    S sq(S a) { return a * a; }
    S cb(S a) { return a * a * a; }
    S qu(S a) { return sq(sq(a)); }
    
    S ssq(S a) { return a * abs(a); }
    S ssqrt(S x) { return sgn(x) * sqrt(abs(x)); }
    S spow(S a, S b) { return sgn(a) * pow(abs(a), b); }

    S divz(S num, S den, S alt) { return sel2(den != 0, num/den, alt); }
    S sinc(S x) { return sel2(x == 0, 1, sin(x)/x); }
    S sinc1(S x) { return sinc(pi*x); }

    S princ1(S x) { return x - round(x); }
    S frac(S a) { return a - floor(a); }

    S isuni(S x) { return min(0 <= x, x <= 1); }
    S isbi(S x) { return min(-1 <= x, x <= 1); }
    S isint(S x) { return x == floor(x); }

    // range mappings
    S bi(S x) { return 2*x - 1; }
    S uni(S x) { return .5*x + .5; }
    S lin(S x, S m, S b) { return m*x + b; }

    S clip(S x, S a, S b) { return min(max(a,x),b); }
    S clip0(S x, S a) { return clip(x,0,a); }
    S clip2(S x, S a) { return clip(x,-a,a); }
    S uclip(S x) { return clip(x,0,1); }
    S bclip(S x) { return clip2(x,1); }
    S excess(S x, S b) { return x - clip2(x,b); }

    S wrap(S x, S a, S b) { return a + (x-a) % (b-a); }
    S wrap0(S x, S a) { return x % a; }
    S wrap2(S x, S a) { return wrap(x,-a,a); }
    S uwrap(S x) { return frac(x); }
    S bwrap(S x) { return wrap(x,-1,1); }

    S fold(S x, S a, S b) { return fold0(x-a, b-a) + a; }
    S fold0(S x, S a) { return ufold(x/a)*a; }
    S fold2(S x, S a) { return fold(x,-a,a); }
    S ufold(S x) { return 1 - abs(1 - x + 2 * floor(.5*x)); }
    S bfold(S x) { return fold(x,-1,1); }

    S unilin(S x, S a, S b) { return a + x*(b-a); }
    S uniexp(S x, S a, S b) { return a * pow(b/a, x); }
    S linuni(S x, S a, S b) { return (x-a)/(b-a); }
    S expuni(S x, S a, S b) { return log(x/a)/log(b/a); }

    S bilin(S x, S a, S b) { return unilin(uni(x),a,b); }
    S biexp(S x, S a, S b) { return uniexp(uni(x),a,b); }
    S linbi(S x, S a, S b) { return bi(linuni(x,a,b)); }
    S expbi(S x, S a, S b) { return bi(expuni(x,a,b)); }

    S linlin(S x, S a, S b, S c, S d) { return unilin(linuni(x,a,b),c,d); }
    S linexp(S x, S a, S b, S c, S d) { return uniexp(linuni(x,a,b),c,d); }
    S explin(S x, S a, S b, S c, S d) { return unilin(expuni(x,a,b),c,d); }
    S expexp(S x, S a, S b, S c, S d) { return uniexp(expuni(x,a,b),c,d); }

    S linlinc(S x, S a, S b, S c, S d) { return linlin(clip(x,a,b),a,b,c,d); } // linear to linear with clamping
    S linexpc(S x, S a, S b, S c, S d) { return linexp(clip(x,a,b),a,b,c,d); } // linear to exponential with clamping
    S explinc(S x, S a, S b, S c, S d) { return explin(clip(x,a,b),a,b,c,d); } // exponential to linear with clamping
    S expexpc(S x, S a, S b, S c, S d) { return expexp(clip(x,a,b),a,b,c,d); } // exponential to exponential with clamping

    S unilinc(S x, S a, S b, S c, S d) { return unilin(uclip(x),a,b); } // unipolar to linear with clamping
    S uniexpc(S x, S a, S b, S c, S d) { return uniexp(uclip(x),a,b); } // unipolar to exponential with clamping
    S bilinc(S x, S a, S b, S c, S d)  { return bilin(bclip(x),a,b); }  // bipolar to linear with clamping
    S biexpc(S x, S a, S b, S c, S d)  { return biexp(bclip(x),a,b); }  // bipolar to exponential with clamping

    S invert_about(S x, S a) { return 2*a - x; }
    S invert_between(S x, S a, S b) { return a + b - x; }

    // warping
    S bwarp(S x, S w) {
        // bipolar to bipolar warping
        S u = .5 * x - .5;
        return (w + u)/(w*x - u);
    }
    S bwarps(S x, S w) {
        // bipolar to bipolar S warping
        S wx = w*x;
        S sx = sgn(x);
        return sx*(wx-x)/(2*wx-sx*w-x);
    }
    S warp(S x, S w) { // unipolar to unipolar warping
        return w*x/(w*bi(x)-x-1);
    }
    S warps(S x, S w) { // unipolar to unipolar S warping
        return uni(bwarps(bi(x),w));
    }

    S phasewarp1(S p, S w) { return uni((p-w) / sel2(p<w, w, (1-w))); }

    // range mapping with warping
    S unilinw(S x, S w, S a, S b)  { return unilin(warp(x,w),a,b); } // unipolar to linear with warping
    S unilinws(S x, S w, S a, S b) { return unilin(warps(x,w),a,b); } // unipolar to linear with S warping
    S bilinw(S x, S w, S a, S b)   { return bilin(warp(x,w),a,b); }   // bipolar to linear with warping
    S bilinws(S x, S w, S a, S b)  { return bilin(warps(x,w),a,b); }  // bipolar to linear with S warping
    S linlinw(S x, S w, S a, S b, S c, S d)  { return unilin(warp(linuni(x,a,b),w),c,d); }  // linear to linear with warping
    S linlinws(S x, S w, S a, S b, S c, S d) { return unilin(warps(linuni(x,a,b),w),c,d); } // linear to linear with S warping

    // chebyshev polynomials
    S cheby(S x, int n) {
        switch (n) { // FIXME: use SwitchExpr.
            case 0: return 1;
            case 1: return x;
            case 2: return 2*pow(x,2) - 1;
            case 3: return 4*pow(x,3) - 3*x;
            case 4: return 8*pow(x,4) - 8*pow(x,2) + 1;
            case 5: return 16*pow(x,5) - 20*pow(x,3) + 5*x;
            case 6: return 32*pow(x,6) - 48*pow(x,4) + 18*pow(x,2) - 1;
            default: return cos(n*acos(x)); // continuously variable index
        }
    }

    S chebyv(S x, S n) { return cos(n*acos(x)); }

    // sigmoid saturation/distortion functions
    // roughly in order from quickest to slowest to saturate.
    S sigmoid(S x, int n) { // FIXME: use SwitchExpr.
        switch (n) {
            case 0: return sel2(abs(x)>1.5, sgn(x), x - (4/27)*cb(x));
            case 1: return erf(sqrt(pi)/2*x);
            case 2: return 2*x/(abs(2*x)+3/(2+4*sq(x)));
            case 3: return (27*x+cb(x))/(27+9*sq(x));
            case 4: return tanh(x);
            case 5: return sgn(x)*(1-1/(1+abs(x)+sq(x)+qu(x)));
            case 6: return x/sqrt(1+sq(x));
            case 7: return (2/pi)*atan(pi/2*x);
            default: return x/(1+abs(x));
        }
    }

    // units conversions
    S ampdb(S x) { return 20 * log10(x); }
    S dbamp(S x) { return exp10(x/20); }

    S hzw(S x) { return x*twopi*sd(); } // Hertz to radians per sample
    S whz(S x) { return x*fs()/twopi; } // radians per sample to Hertz

    S nnhz(S x) { return 440 * exp2((x-69)/12); }
    S hznn(S x) { return 69 + 12 * log2(x/440); }

    S ratiocents(S x) { return 1200 * log2(x); }
    S centsratio(S x) { return exp2(x/1200); }

    S ratiosemi(S x) { return 12 * log2(x); }
    S semiratio(S x) { return exp2(x/12); }

    S ochhz(S x) { return 440 * exp2(x-69/12); }
    S hzoct(S x) { return 69/12 + log2(x/440); }

    S degrad(S x) { return x * (pi/180); }
    S raddeg(S x) { return x * (180/pi); }

    S bpmhz(S x) { return x/60; } // beats per minute to Hertz
    S hzbpm(S x) { return x*60; } // Hertz to beats per minute

    S bpmsec(S x) { return 60/x; } // bpmsec is an involution.
    S secbpm(S x) { return 60/x; } // bpmsec is an involution.

    // pass a signal only if it is above or below a threshold
    S above(S x, S m) { return sel2(x < m, 0, x); }
    S below(S x, S m) { return sel2(x < m, x, 0); }

    // pass a signal only if its absolute value is above or below a threshold
    S absabove(S x, S m) { return sel2(abs(x) < m, 0, x); }
    S absbelow(S x, S m) { return sel2(abs(x) < m, x, 0); }

    // variants of amplitude modulation
    S vca(S x, S a) { return x * max(0,a); }
    S scaleneg(S x, S a) { return sel2(x < 0, x*a, x); }
    S scalepos(S x, S a) { return sel2(x > 0, x*a, x); }
    S scalenegpos(S x, S a, S b) { return sel2(x < 0, x*a, x*b); }

    // variants of ring modulation
    S ring1(S a, S b) { return a*b + a; }
    S ring2(S a, S b) { return a*b + a + b; }
    S ring3(S a, S b) { return a*a*b; }
    S ring4(S a, S b) { return a*a*b - a*b*b; }

    // various other functions from SuperCollider
    S sumsq(S a, S b) { return sq(a) + sq(b); }
    S difsq(S a, S b) { return sq(a) - sq(b); }
    S sqsum(S a, S b) { return sq(a+b); }
    S sqdif(S a, S b) { return sq(a-b); }
    S absdif(S a, S b) { return abs(a-b); }
    S thresh(S a, S b) { return sel2(a<b, 0, a); }

    S rising(S x) { return x > z1(x); }
    S falling(S x) { return x < z1(x); }
    S nochange(S x) { return x == z1(x); }
    S changing(S x) { return x != z1(x); }
    S peak(S x) { return min(x < z1(x), z1(x) > z2(x)); }
    S trough(S x) { return min(x > z1(x), z1(x) < z2(x)); }

    // triggers
    S tr(S x) { return min(x > 0, z1(x) <= 0); } // 'triggerization'. detects transition from x <= 0 to x > 0.
    S eoc(S x) { return abs(x-z1(x)) > .5; }   // end of cycle. detects wrapping of a unipolar phasor
    S init() { // one initial impulse, then zero forever.
        D d;
        d.init(1,1);
        d.write(0);
        return d(1);
    }
        
    S sah(S x, S t) {
        D y;
        return y = sel2(t > 0, x, y[1]);
    }

    // flip flops
    S toggle(S x) {
        D y;
        return y = sel2(x>0, 1-y(1), y(1));
    }

    // set, reset
    S setreset(S s, S r) {
        D y;
        return y = sel2(r>0, 0, sel2(s>0, 1, y(1)));
    }

    // set, reset, toggle
    S srt(S s, S r, S t) {
        D y;
        return y = sel2(r>0, 0, sel2(s>0, 1, sel2(t>0, 1-y(1), y(1))));
    }

    // unipolar phasor waveshaping
    S sawshift(S x, S shift) { return frac(x+shift); } // phase shift a unipolar sawtooth.
    S quadr(S x) { return sawshift(x, .25); }  // quadrature
    //S multiphasor(S x, int n) { return sawshift(x, 1./iota(n,1)); }

    S sin1(S x) { return sinpi(2*x); }
    S cos1(S x) { return cospi(2*x); }
    S tan1(S x) { return tanpi(2*x); }

    S sin2pi(S x)  { return sinpi(2*x); }
    S cos2pi(S x)  { return cospi(2*x); }
    S usin2pi(S x) { return uni(sin2pi(x)); }
    S ucos2pi(S x) { return uni(cos2pi(x)); }

    S fsin(S x) { // fast approximation of sine
        S q = princ1(x);
        return q*(8-16*abs(q));
    }
    S fsinx(S x) { // fast approximation of sine with extra precision
        S a = fsin(x);
        return a + .224*a*(abs(a)-1);
    }
    S fcos(S x) { return fsin(x+.25); } // fast approximation of cosine
    S fcosx(S x) { return fsinx(x+.25); } // fast approximation of cosine

        // square waves
    S usquare(S x) { return x < .5; }
    S bsquare(S x) { return sel2(x < .5, -1, 1); }

        // triangle waves
    S utri(S x) { return abs(bi(x)); }
    S btri(S x) { return abs(4*x-2)-1; }

        // variable triangle and saw
        // an amplitude scaled difference of parabolas
    S par(S x) { return sq(bi(x)); }   // unipolar ramp to parabola
    S vartri(S x, S pwm) { return (.25 / (pwm - sq(pwm))) * (par(x) - par(frac(x - pwm))); }
    S varsaw(S x, S pwm) { return bi(vartri(x, pwm)); }

        // pulse waves
    S upulse(S x, S pwm) { return x < pwm; }                          // unipolar pulse wave
    S bpulse(S x, S pwm) { return bi(upulse(x,pwm)); }                // bipolar pulse wave
    S zpulse(S x, S pwm) { return sel2(x < pwm, 1 - pwm, -pwm); }      // zero DC pulse wave

        // pulse waves. as in SuperCollider, these always output one value of the opposite polarity each cycle.
    S upulse1(S x, S pwm) { return sel2(eoc(x), pwm < .5, x < pwm); }       // unipolar
    S bpulse1(S x, S pwm) { return bi(upulse1(x, pwm)); }                  // bipolar
    S zpulse1(S x, S pwm) { return sel2(eoc(x), pwm < .5, zpulse(x,pwm)); } // zero DC

    // triangle waves with various starting phases
        // bipolar
    S btri0(S x) { return 1-abs(2-4*quadr(x)); } // 0   degrees. bipolar output. '\,  trisin
    S btri1(S x) { return abs(2-4*x)-1; }        // 90  degrees. bipolar output. \/   tricos
    S btri2(S x) { return abs(2-4*quadr(x))-1; } // 180 degrees. bipolar output. ,/' -trisin
    S btri3(S x) { return 1-abs(2-4*x); }        // 270 degrees. bipolar output. /\  -tricos
        // unipolar
    S utri0(S x) { return 1-abs(1-2*quadr(x)); } // 0   degrees. unipolar output. '\,  trisin
    S utri1(S x) { return abs(1-2*x); }          // 90  degrees. unipolar output. \/   tricos
    S utri2(S x) { return abs(1-2*quadr(x)); }   // 180 degrees. unipolar output. ,/' -trisin
    S utri3(S x) { return 1-abs(1-2*x); }        // 270 degrees. unipolar output. /\  -tricos

    // trapezoid waves
    S trapez0(S x) { return bclip(2-abs(4-8*quadr(x))); } // 0   degrees. bipolar output.
    S trapez1(S x) { return bclip(abs(4-8*x)-2); }        // 90  degrees. bipolar output.
    S trapez2(S x) { return bclip(abs(4-8*quadr(x))-2); } // 180 degrees. bipolar output.
    S trapez3(S x) { return bclip(2-abs(4-8*x)); }        // 270 degrees. bipolar output.

        // inspired by the Intellijel Rubicon waveform.
    S izigzag(S x) { return abs(1 - 2*x) - (x > .5); } // moving inwards
    S ozigzag(S x) { return abs(1 - 2*x) - (x < .5); } // moving outwards

    // smooth min, max, step
    S smooth_adjustment(S a, S b, S k) { return .125 * k * qu(max(0, k - abs(a-b)) / k); }
        
    S smin(S a, S b, S k) { return min(a,b) - smooth_adjustment(a,b,k); }
    S smax(S a, S b, S k) { return max(a,b) + smooth_adjustment(a,b,k); }
        
    S sstep(S x) { // smooth step
        S t = uclip(x);
        return sq(t) * (3 - 2*t);
    }

    S sstep2(S x) { // smoother step
        S t = uclip(x);
        return cb(t) * (x * (x * 6 - 15) + 10);
    }

    S lerp(S p, S a, S b) { return a+p*(b-a); }  // linear interpolation. (same as unilin).

    // waveforms with dynamic shape
    // sine pulses with a saw envelope
    S sawenvsin(S x, S n, S m) { return sq(sinpi(n*x)) * pow(1-x, m); }
    // sine pulses with a saw envelope of alternating polarity
    S altsawenvsin(S x, S n, S m) {
        return bsquare(x) * sawenvsin(frac(2*x), n/2, m);
    }

    // differentiation
    S diff(S x) { return x - z1(x); }  // unscaled sample to sample difference
    S slope(S x) { return diff(x) * fs(); }
    S accel(S x) { return slope(slope(x)); }
    S jerk(S x) { return slope(accel(x)); }

    // integration
    S unscaledIntegrator(S x)    { D y; return y += x; }
    S trapezoidalIntegrator(S x) { D y; return y += (x + z1(x)) * (sd()/2); }
    S forwardIntegrator(S x)     { D y; return y += z1(x)*sd(); }
    S backwardIntegrator(S x)    { D y; return y += x*sd(); }

    // with reset
    S unscaledIntegrator(S x, S r)    { D y; return y = sel2(r>0, 0, y(1) + x); }
    S trapezoidalIntegrator(S x, S r) { D y; return y = sel2(r>0, 0, y(1) + (x + z1(x)) * (sd()/2)); }
    S forwardIntegrator(S x, S r)     { D y; return y = sel2(r>0, 0, y(1) + z1(x)*sd()); }
    S backwardIntegrator(S x, S r)    { D y; return y = sel2(r>0, 0, y(1) + x*sd()); }

    S minfollow(S x, S r) { D y; return y = sel2(r > 0, x, min(x,y(1))); }
    S maxfollow(S x, S r) { D y; return y = sel2(r > 0, x, max(x,y(1))); }

    S leaky(S x, S a) {
        D y;
        return y = x + a * y(1);
    }

    S by(S start, S step) {
        auto y = D();
        y.init(-1, start);
        y += step;
        return y(1);
    }
    S grow(S start, S step) {
        auto y = D();
        y.init(-1, start);
        y = y(1) * step;
        return y(1);
    }


    // exponential decay
    S decayCoeff(S n, S amp) { return pow(amp, 1/n); }

    S decay40dB(S n) { return decayCoeff(n, .01); }
    S decay60dB(S n) { return decayCoeff(n, .001); }

    S decay(S x, S t) { return leaky(x, decay40dB(t*fs())); }
    S decay2(S x, S atk, S dcy) { return decay(x, dcy) - decay(x, atk); }

//    //S iqimpulse(S x, S k) { // from Inigo Quiles https://www.iquilezles.org/www/articles/functions/functions.htm
//    //    S h = k*x;
//    //    return h*exp(1-h);
//    //}
//
    S combn(S x, S delayTime, S decayTime) {
        S a = decay60dB(decayTime/delayTime);
        S delaySamples = delayTime*fs();
        D y(delaySamples);
        return y = x + y.v(delaySamples) * a;
    }
    S combl(S x, S delayTime, S maxDelayTime, S decayTime) {
        S a = decay60dB(decayTime/delayTime);
        S dt = delayTime * fs();
        S b = frac(dt);
        D y(maxDelayTime * fs());
        return y = x + lerp(b, y.v(dt), y.v(dt+1)) * a;
    }

    // simple filters
    S onepole(S x, S a) { D y; return y = x + a*(y(1) - x); }
    S onezero(S x, S a) { return x + a*(z1(x) - x); }
    S leakdc(S x, S k) { D y; return y = x - z1(x) + k*y(1); }

    S lag(S x, S t) {
        S a = decay40dB(t * fs());
        D y;
        return y = x + a * (y(1) - x);
    }

    // panning and crossfading

    S panfun(S x) {
        // cubic polynomial to preserve equal power. max absolute dB error < .0006 dB.
        S a = 0.337403011047526069;
        S b = -1.334338069017510398;
        S c = -a-b-1;
        return 1 + x*(c + x*(b + x*a));
    }

//    S panfuns(S x) {
//        return panfun(cat({x,1-x}));
//    }

//    //S xfade(S pos, S a, S b) {
//    //    return sum(vec(a, b) * panfuns(uni(pos)));
//    //}

//    S pan(S pos, S x) {
//        return x * panfuns(uni(pos));
//    }


    // oscillator functions
    S phasor(S phaseIncrement, S pm) {
        D phase;
        phase = frac(phase(1) + cast_f64(phaseIncrement));
        return frac(cast_f32(phase) + pm);
    }

    S sinosc(S fm, S pm) { return sin1(phasor(fm * sd(), pm)); }
    S fsinosc(S fm, S pm) { return fsin(phasor(fm * sd(), pm)); }
    S fsinxosc(S fm, S pm) { return fsinx(phasor(fm * sd(), pm)); }
//    std::pair<S,S> sincos_osc(S fm, S pm) {
//        S p = phasor(fm * sd(), pm);
//        return sincos(p);
//    }

        // lf = low frequency. geometrically precise, non-antialiased oscillators.
    S lfsaw (S fm, S pm) { return bi(phasor(fm * sd(), pm)); }            // saw
    S lfimp (S fm, S pm) { return eoc(phasor(fm * sd(), pm)); }           // impulses
    S lftri (S fm, S pm) { return btri(phasor(fm * sd(), pm)); }          // triangle wave
    S lftrap(S fm, S pm) { return bclip(lftri(fm, pm) * 2); }            // trapezoid wave
    S lfusqr(S fm, S pm) { return usquare(phasor(fm * sd(), pm)); }       // unipolar square
    S lfsqr (S fm, S pm) { return bsquare(phasor(fm * sd(), pm)); }       // bipolar square
    S lfzig (S fm, S pm) { return izigzag(phasor(fm * sd(), pm)); }       // inwards zigzag
    S lfzag (S fm, S pm) { return ozigzag(phasor(fm * sd(), pm)); }       // outwards zigzag
    S lfpar (S fm, S pm) { return bi(par(phasor(fm * sd(), pm))); }       // parabolic wave
    S lfvsaw  (S fm, S pwm, S pm) { return varsaw(phasor(fm * sd(), pm), pwm); }  // variable saw
    S lfupulse(S fm, S pwm, S pm) { return upulse1(phasor(fm * sd(), pm), pwm); }  // variable unipolar pulse
    S lfpulse (S fm, S pwm, S pm) { return bpulse1(phasor(fm * sd(), pm), pwm); }  // variable bipolar pulse
    S lfzpulse(S fm, S pwm, S pm) { return zpulse(phasor(fm * sd(), pm),pwm); }    // zero DC variable pulse

    // random numbers and noise
    S rand(S a, S b, usize chans, SignalRate rate) {
        return unilin(urand(chans, rate), a, b);
    }
    S xrand(S a, S b, usize chans, SignalRate rate) {
        return uniexp(urand(chans, rate), a, b);
    }
    S birand(S a, usize chans, SignalRate rate) {
        return a * birand(chans, rate);
    }
    S linrand(S a, S b, usize chans, SignalRate rate) {
        return unilin(min(urand(chans, rate), urand(chans, rate)), a, b);
    }
    S trirand(S a, S b, usize chans, SignalRate rate) {
        return unilin(urand(chans, rate) - urand(chans, rate), a, b);
    }

    S coin(S p, usize chans, SignalRate rate) {
        return cast_f32(urand(chans, rate) < p);
    }
    S irand(S n, usize chans, SignalRate rate) {
        return floor(n * urand(chans, rate));
    }
    S randsign(usize chans, SignalRate rate) {
        return sel2(urand(chans, rate) < 0.5, -1, 1);
    }


    S pinkingFilter(S x)
    {
        // from Paul Kellett
        D b0, b1, b2, b3, b4, b5, b6;
        b0 = .99886 * b0(1) + x * .0555179;
        b1 = .99332 * b1(1) + x * .0750759;
        b2 = .96900 * b2(1) + x * .1538520;
        b3 = .86650 * b3(1) + x * .3104856;
        b4 = .55000 * b4(1) + x * .5329522;
        b5 = -.7616 * b5(1) - x * .0168980;
        b6 = x * .115926;
        return b0 + b1 + b2 + b3 + b4 + b5 + b6(1) + x * .5362;
    }

    S pinkingFilterEco(S x)
    {
        D b0, b1, b2;
        b0 = 0.99765 * b0(1) + x * 0.0990460;
        b1 = 0.96300 * b1(1) + x * 0.2965164;
        b2 = 0.57000 * b2(1) + x * 1.0526913;
        return b0 + b1 + b2 + x * 0.1848;
    }

    S white(usize chans) { return birand(chans); }
    S WHITE(usize chans) { return randsign(chans); }     // random full positive or negative

    S red(usize chans)    { return bfold(unscaledIntegrator(bi(urand(chans)) * .1)); }
    S pink(usize chans)   { return .5 * pinkingFilter(white(chans)); }
    S pinke(usize chans)  { return .5 * pinkingFilterEco(white(chans)); }
    S blue(usize chans)   { return diff(pink(chans)); }
    S violet(usize chans) { return diff(white(chans)); }

    S velvet(S freq, usize chans) {
        return coin(freq * sd(), chans);
    }
    S dust(S freq, usize chans)   {
        return urand(chans) * velvet(freq, chans);
    }
    S dust2(S freq, usize chans)  {
        return birand(chans) * velvet(freq, chans);
    }

    S dustep(S freq, usize chans) {
        return sah(white(chans), velvet(freq, chans));
    }
    S fadein(S x, S t) {
        S dt = sd()/t;
        D y;
        y = min(1, y(1) + dt);
        return x * cb(y);
    }

    S tremolo(S x, S freq, S amount, S pm) {
        return x *  bilin(fsinosc(freq, pm), 1-amount, 1);
    }
    
    S pause(S env, std::function<S()> gated) {
        return if_(env > 0, [&]() { return env * gated(); });
    }

    S pull(S gate, std::function<S()> f, S initval) {
        D d;
        d.init(0, initval);
        return if_(gate > 0, [&]() { return d = f(); }, [&](){ return d(1); });
    }
    
//    
//    // biquads
//    S biquadGainAdj([b0,b1,b2,a1,a2], S a0, S gain) {
//        S a0_inv = 1/a0;
//        S a0_inv_gain = a0_inv * gain;
//        [b0*a0_inv_gain, b1*a0_inv_gain, b2*a0_inv_gain, a1*a0_inv, a2*a0_inv]
//    }
//
//    // biquad coefficients from Robert Bristow-Johnson's filter cookbook
//    // low pass filter coefficients
//    S lpfCoeffs(S w0, S rQ=twoSqrt, S gain=1) {
////        [sn,cs] = sincos(w0);
//        S sn = sin(w0);
//        S cs = cos(w0);
//        alpha = sn*.5*rQ;
//
//        b0 = b2 = .5*(1 - cs);
//        b1 = 2 * b0;
//
//        a0 =  1 + alpha;
//        a1 = -2 * cs;
//        a2 =  1 - alpha;
//
//        biquadGainAdj([b0,b1,b2,a1,a2],a0,gain);
//    }
//
//    // high pass filter coefficients
//    S hpfCoeffs(S w0, S rQ=twoSqrt, S gain=1) {
//        [sn,cs] = sincos(w0);
//        S alpha = sn*.5rQ;
//        
//        S b0 = b2 = .5(1 + cs);
//        S b1 = -2 * b0;
//
//        S a0 =  1 + alpha;
//        S a1 = -2 * cs;
//        S a2 =  1 - alpha;
//
//        biquadGainAdj([b0,b1,b2,a1,a2],a0,gain);
//    }
//
//    // band pass filter coefficients
//    S bpfCoeffs(S w0, S rQ=twoSqrt, S gain=1) {
//        [sn,cs] = sincos(w0);
//        S alpha = sn*.5rQ;
//
//        S b0 =  alpha;
//        S b1 =  0;
//        S b2 = -alpha;
//        
//        S a0 =  1 + alpha;
//        S a1 = -2 * cs;
//        S a2 =  1 - alpha;
//
//        biquadGainAdj([b0,b1,b2,a1,a2],a0,gain);
//    }
//
//    // band stop filter coefficients
//    S bsfCoeffs(S w0, S rQ=twoSqrt, S gain=1) {
//        [sn,cs] = sincos(w0);
//        S alpha = sn*.5rQ;
//
//        S b0 = 1;
//        S b1 =  -2 * cs;
//        S b2 = 1;
//
//        S a0 =  1 + alpha;
//        S a1 = -2 * cs;
//        S a2 =  1 - alpha;
//
//        biquadGainAdj([b0,b1,b2,a1,a2],a0,gain);
//    }
//
//    // all pass filter coefficients
//    S apfCoeffs(S w0, S rQ=twoSqrt, S gain=1) {
//        [sn,cs] = sincos(w0);
//        S alpha = sn*.5rQ;
//
//        S a0 =  1 + alpha;
//        S a1 = -2 * cs;
//        S a2 =  1 - alpha;
//        
//        S b0 = a2;
//        S b1 = a1;
//        S b2 = a0;
//
//        biquadGainAdj([b0,b1,b2,a1,a2],a0,gain);
//    }
//
//    // parametric EQ filter coefficients
//    S peqCoeffs(S w0, S bw, S gaindb) {
//        [sn,cs] = sincos(w0);
//        S gain = dbamp(gaindb);
//        S A = dbamp(.5 * gain);
//
//        // bw * log(2) is the first term of the taylor series for 2*sinh(log(2)/2 * bw) == 1/Q.
//        S alpha = sn * bw * halfLog2;
//        S alphaA     = alpha * A;
//        S alphaOverA = alpha / A;
//                    
//        S a0 =  1 + alphaOverA;
//        S a1 = -2 * cs;
//        S a2 =  1 - alphaOverA;
//                
//        S b0 =  1 + alphaA;
//        S b1 =  0;
//        S b2 =  1 - alphaA;
//
//        biquadGainAdj([b0,b1,b2,a1,a2],a0,gain);
//    }
//
//    // low shelf filter coefficients
//    S lsfCoeffs(S w0, S gaindb) {
//        [sn,cs] = sincos(w0);
//
//        S gain = dbamp(gaindb);
//        S A = dbamp(.5 * gain);
//        S Ap1 = A + 1;
//        S Am1 = A - 1;
//        S Asqrt = sqrt(A);
//
//        S alpha = sn * halfSqrt;
//        S alpha2Asqrt = 2 * alpha * Asqrt;
//        S Am1cs = Am1*cs;
//        S Ap1cs = Ap1*cs;
//                    
//        S b0 =   ( Ap1 - Am1cs + alpha2Asqrt );
//        S b1 =  2( Am1 - Ap1cs               );
//        S b2 =   ( Ap1 - Am1cs - alpha2Asqrt );
//        S a0 =     Ap1 + Am1cs + alpha2Asqrt;
//        S a1 = -2( Am1 + Ap1cs               );
//        S a2 =     Ap1 + Am1cs - alpha2Asqrt;
//
//        biquadGainAdj([b0,b1,b2,a1,a2],a0,gain);
//    }
//
//
//    // high shelf filter coefficients
//    S hsfCoeffs(S w0, S gaindb) {
//        [sn,cs] = sincos(w0);
//
//        S gain = dbamp(gaindb);
//        S A = dbamp(.5 * gain);
//        S Ap1 = A + 1;
//        S Am1 = A - 1;
//        S Asqrt = sqrt(A);
//
//        S alpha = sn * halfSqrt;
//        S alpha2Asqrt = 2 * alpha * Asqrt;
//        S Am1cs = Am1*cs;
//        S Ap1cs = Ap1*cs;
//                    
//        S b0 =   ( Ap1 + Am1cs + alpha2Asqrt );
//        S b1 = -2( Am1 + Ap1cs               );
//        S b2 =   ( Ap1 + Am1cs - alpha2Asqrt );
//        S a0 =     Ap1 - Am1cs + alpha2Asqrt;
//        S a1 =  2( Am1 - Ap1cs               );
//        S a2 =     Ap1 - Am1cs - alpha2Asqrt;
//
//        biquadGainAdj([b0,b1,b2,a1,a2],a0,gain);
//    }
//
//    // first order filter coefficients
//    S lpf1Coeffs(S w0) {
//        S b0 = 1;
//        S b1 = .12;
//        S a0 = 1;
//        S a1 = exp(-w0);
//        S adj = (1-a1)/(b0+b1);
//        [b0*adj, b1*adj, a1]
//    }
//
//    S hpf1Coeffs(S w0) {
//        S b0 = 1;
//        S b1 = -1;
//        S a1 = exp(-w0);
//        S adj = (1+a1)/2;
//        [b0*adj, b1*adj, a1]
//    }
//
//    // 6 dB/oct filters
//    S lpf1(S x, S freq)          = firstOrderFilter(x, lpf1Coeffs(freq.hzw));
//    S hpf1(S x, S freq)          = firstOrderFilter(x, hpf1Coeffs(freq.hzw));
//
//    // 12 dB/oct filters
//    lpf(x,freq)           = biquad(x, lpfCoeffs(freq.hzw));						// low pass
//    hpf(x,freq)           = biquad(x, hpfCoeffs(freq.hzw));                     // high pass
//    bpf(x,freq,bw)        = biquad(x, bpfCoeffs(freq.hzw, bw*log(2)));          // band pass
//    bsf(x,freq,bw)        = biquad(x, bsfCoeffs(freq.hzw, bw*log(2)));          // band stop
//
//    rlpf(x,freq,rQ)       = biquad(x, lpfCoeffs(freq.hzw, rQ));                 // resonant low pass
//    rhpf(x,freq,rQ)       = biquad(x, hpfCoeffs(freq.hzw, rQ));                 // resonant high pass
//
//    apf(x,freq,rQ)        = biquad(x, apfCoeffs(freq.hzw, rQ));					// all pass
//
//    peq(x,freq,bw,gaindb) = biquad(x, peqCoeffs(freq.hzw, bw, gaindb));         // parametric EQ
//    lsf(x,freq,gaindb)    = biquad(x, lsfCoeffs(freq.hzw, gaindb));             // low shelf
//    hsf(x,freq,gaindb)    = biquad(x, hsfCoeffs(freq.hzw, gaindb));             // high shelf
//
//    // 24 dB / oct filters
//    lpf4(x,freq) { coeffs = lpfCoeffs(freq.hzw); x.biquad(coeffs).biquad(coeffs) }
//    hpf4(x,freq) { coeffs = hpfCoeffs(freq.hzw); x.biquad(coeffs).biquad(coeffs) }
//
//    svf(x, freq, rQ) { // state variable filter
//        f = 2*sin(freq*pi/sr);
//        lp = lp' + f*bp';
//        hp = x - lp - q*bp';
//        bp = bp' + f*hp;
//        bs = hp + lp;
//        [lp,hp,bp,bs]
//    }
//
}
