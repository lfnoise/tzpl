//
//  math_funs.hpp
//  tensor-ugens
//
//  Created by James McCartney on 10/17/26.
//

#ifndef math_funs_h
#define math_funs_h

#include <cmath>
#include "vec_math.hpp"

// Constants of the sample type, computed in double and rounded once.
const Z pi       = Z(M_PI);
const Z twopi    = Z(2.*M_PI);
const Z halfSqrt = Z(M_SQRT1_2);        // square root of one half = .7071...
const Z twoSqrt  = Z(M_SQRT2);          // square root of 2
const Z phi      = Z((std::sqrt(5.)+1.)/2.); // golden ratio
const Z halfLog2 = Z(.5 * M_LN2);   // Used for filter coefficients.
const Z e        = Z(M_E);
const Z log01    = Z(std::log(.01));
const Z log001   = Z(std::log(.001));

// Each function works for T = Z (a sample) or T = V (a vector of samples), by
// calling the vmath:: primitives from vec_math.hpp. Comparisons that produce a
// value go through vmath::truth, giving 1 or 0, and selections through sel2,
// which evaluates both alternatives.

template <class B, class T> T sel2(B a, T b, T c) { return vmath::select(a, b, c); }

template <class T> T sgn(T a) { return sel2(a > T(0), T(1), sel2(a < T(0), T(-1), T(0))); }
template <class T> T sgnz(T a) { return sel2(a > T(0), T(1), T(-1)); }

template <class T> T sq(T a) { return a * a; }
template <class T> T cb(T a) { return a * a * a; }
template <class T> T qu(T a) { return sq(sq(a)); }

template <class T> T ssq(T a) { return a * vmath::abs(a); }
template <class T> T ssqrt(T x) { return sgn(x) * vmath::sqrt(vmath::abs(x)); }
template <class T> T spow(T a, T b) { return sgn(a) * vmath::pow(vmath::abs(a), b); }

template <class T> T recip(T a) { return T(1) / a; }
template <class T> T divz(T num, T den, T alt) { return sel2(den != T(0), num/den, alt); }
template <class T> T sinc(T x) { return sel2(x == T(0), T(1), vmath::sin(x)/x); }
template <class T> T sinc1(T x) { return sinc(T(pi)*x); }

template <class T> T princ1(T x) { return x - vmath::round(x); }
template <class T> T frac(T a) { return a - vmath::floor(a); }

template <class T> T isuni(T x) { return vmath::truth(T(0) <= x) * vmath::truth(x <= T(1)); }
template <class T> T isbi(T x) { return vmath::truth(T(-1) <= x) * vmath::truth(x <= T(1)); }
template <class T> T isint(T x) { return vmath::truth(x == vmath::floor(x)); }

// range mappings
template <class T> T bi(T x) { return T(2)*x - T(1); }
template <class T> T uni(T x) { return T(0.5)*x + T(0.5); }
template <class T> T lin(T x, T m, T b) { return m*x + b; }

template <class T> T clip(T x, T a, T b) { return vmath::min(vmath::max(a,x), b); }
template <class T> T clip0(T x, T a) { return clip(x, T(0), a); }
template <class T> T clip2(T x, T a) { return clip(x, -a, a); }
template <class T> T uclip(T x) { return clip(x, T(0), T(1)); }
template <class T> T bclip(T x) { return clip2(x, T(1)); }
template <class T> T excess(T x, T b) { return x - clip2(x, b); }

template <typename T> T mod(T a, T b) {
    T r = vmath::fmod(a, b);
    return sel2(r < T(0), r + b, r);
}
    
template <class T> T wrap(T x, T a, T b) { return a + mod(x - a, b - a); }
template <class T> T wrap0(T x, T a) { return mod(x, a); }
template <class T> T wrap2(T x, T a) { return wrap(x, -a, a); }
template <class T> T uwrap(T x) { return frac(x); }
template <class T> T bwrap(T x) { return wrap(x, T(-1), T(1)); }

template <class T> T ufold(T x) { return T(1) - vmath::abs(T(1) - x + T(2) * vmath::floor(T(0.5)*x)); }
template <class T> T fold0(T x, T a) { return ufold(x / a) * a; }
template <class T> T fold(T x, T a, T b) { return fold0(x - a, b - a) + a; }
template <class T> T fold2(T x, T a) { return fold(x, -a, a); }
template <class T> T bfold(T x) { return fold(x, T(-1), T(1)); }

template <class T> T unilin(T x, T a, T b) { return a + x*(b - a); }
template <class T> T uniexp(T x, T a, T b) { return a * vmath::pow(b/a, x); }
template <class T> T linuni(T x, T a, T b) { return (x - a) / (b - a); }
template <class T> T expuni(T x, T a, T b) { return vmath::log(x/a) / vmath::log(b/a); }

template <class T> T bilin(T x, T a, T b) { return unilin(uni(x), a, b); }
template <class T> T biexp(T x, T a, T b) { return uniexp(uni(x), a, b); }
template <class T> T linbi(T x, T a, T b) { return bi(linuni(x, a, b)); }
template <class T> T expbi(T x, T a, T b) { return bi(expuni(x, a, b)); }

template <class T> T linlin(T x, T a, T b, T c, T d) { return unilin(linuni(x, a, b), c, d); }
template <class T> T linexp(T x, T a, T b, T c, T d) { return uniexp(linuni(x, a, b), c, d); }
template <class T> T explin(T x, T a, T b, T c, T d) { return unilin(expuni(x, a, b), c, d); }
template <class T> T expexp(T x, T a, T b, T c, T d) { return uniexp(expuni(x, a, b), c, d); }

template <class T> T linlinc(T x, T a, T b, T c, T d) { return linlin(clip(x, a, b), a, b, c, d); }
template <class T> T linexpc(T x, T a, T b, T c, T d) { return linexp(clip(x, a, b), a, b, c, d); }
template <class T> T explinc(T x, T a, T b, T c, T d) { return explin(clip(x, a, b), a, b, c, d); }
template <class T> T expexpc(T x, T a, T b, T c, T d) { return expexp(clip(x, a, b), a, b, c, d); }

template <class T> T unilinc(T x, T a, T b) { return unilin(uclip(x), a, b); }
template <class T> T uniexpc(T x, T a, T b) { return uniexp(uclip(x), a, b); }
template <class T> T bilinc(T x, T a, T b)  { return bilin(bclip(x), a, b); }
template <class T> T biexpc(T x, T a, T b)  { return biexp(bclip(x), a, b); }

template <class T> T invert_about(T x, T a) { return T(2)*a - x; }
template <class T> T invert_between(T x, T a, T b) { return a + b - x; }

// warping
template <class T> T bwarp(T x, T w) {
    T u = T(0.5) * x - T(0.5);
    return (w + u) / (w*x - u);
}
template <class T> T bwarps(T x, T w) {
    T wx = w*x;
    T sx = sgn(x);
    return sx*(wx - x) / (T(2)*wx - sx*w - x);
}
template <class T> T warp(T x, T w) { return w*x / (w*bi(x) - x - T(1)); }
template <class T> T warps(T x, T w) { return uni(bwarps(bi(x), w)); }

template <class T> T phasewarp1(T p, T w) { return uni((p - w) / sel2(p < w, w, (T(1) - w))); }

// range mapping with warping
template <class T> T unilinw(T x, T w, T a, T b)  { return unilin(warp(x, w), a, b); }
template <class T> T unilinws(T x, T w, T a, T b) { return unilin(warps(x, w), a, b); }
template <class T> T bilinw(T x, T w, T a, T b)   { return bilin(warp(x, w), a, b); }
template <class T> T bilinws(T x, T w, T a, T b)  { return bilin(warps(x, w), a, b); }
template <class T> T linlinw(T x, T w, T a, T b, T c, T d)  { return unilin(warp(linuni(x, a, b), w), c, d); }
template <class T> T linlinws(T x, T w, T a, T b, T c, T d) { return unilin(warps(linuni(x, a, b), w), c, d); }

// units conversions
template <class T> T ampdb(T x) { return T(20) * vmath::log10(x); }
template <class T> T dbamp(T x) { return vmath::pow(T(10), x / T(20)); }

// Hertz to/from Mel scale
// 1000/log(17/7) = 1127.010480334157439
// 1/700 = 0.00142857142857143
// 17/7 = 2.428571428571429
template <class T> T hzmel(T x) { 
    return T(1127.010480334157439) * vmath::log(T(1) + T(0.00142857142857143) * x); 
}
template <class T> T melhz(T x) { 
    return T(700.) * (vmath::pow(T(2.428571428571429), T(.001) * x) - T(1)); 
}

template <class T> T nnhz(T x) { return T(440) * vmath::exp2((x - T(69)) / T(12)); }
template <class T> T hznn(T x) { return T(69) + T(12) * vmath::log2(x / T(440)); }

template <class T> T ratiocents(T x) { return T(1200) * vmath::log2(x); }
template <class T> T centsratio(T x) { return vmath::exp2(x / T(1200)); }

template <class T> T ratiosemi(T x) { return T(12) * vmath::log2(x); }
template <class T> T semiratio(T x) { return vmath::exp2(x / T(12)); }

template <class T> T octhz(T x) { return T(440) * vmath::exp2(x - T(69)/T(12)); }
template <class T> T hzoct(T x) { return T(69)/T(12) + vmath::log2(x / T(440)); }

template <class T> T degrad(T x) { return x * (T(pi)/T(180)); }
template <class T> T raddeg(T x) { return x * (T(180)/T(pi)); }

template <class T> T bpmhz(T x) { return x / T(60); }
template <class T> T hzbpm(T x) { return x * T(60); }

template <class T> T bpmsec(T x) { return T(60) / x; } // involution
template <class T> T secbpm(T x) { return T(60) / x; } // involution

// pass only if above/below threshold
template <class T> T above(T x, T m) { return sel2(x < m, T(0), x); }
template <class T> T below(T x, T m) { return sel2(x < m, x, T(0)); }

// pass only if abs above/below threshold
template <class T> T absabove(T x, T m) { return sel2(vmath::abs(x) < m, T(0), x); }
template <class T> T absbelow(T x, T m) { return sel2(vmath::abs(x) < m, x, T(0)); }

// amplitude modulation variants
template <class T> T vca(T x, T a) { return x * vmath::max(T(0), a); }
template <class T> T scaleneg(T x, T a) { return sel2(x < T(0), x*a, x); }
template <class T> T scalepos(T x, T a) { return sel2(x > T(0), x*a, x); }
template <class T> T scalenegpos(T x, T a, T b) { return sel2(x < T(0), x*a, x*b); }

// ring modulation variants
template <class T> T ring1(T a, T b) { return a*b + a; }
template <class T> T ring2(T a, T b) { return a*b + a + b; }
template <class T> T ring3(T a, T b) { return a*a*b; }
template <class T> T ring4(T a, T b) { return a*a*b - a*b*b; }

// SuperCollider-style helpers
template <class T> T sumsq(T a, T b) { return sq(a) + sq(b); }
template <class T> T difsq(T a, T b) { return sq(a) - sq(b); }
template <class T> T sqsum(T a, T b) { return sq(a + b); }
template <class T> T sqdif(T a, T b) { return sq(a - b); }
template <class T> T absdif(T a, T b) { return vmath::abs(a - b); }
template <class T> T thresh(T a, T b) { return sel2(a < b, T(0), a); }

// unipolar phasor waveshaping
template <class T> T sawshift(T x, T shift) { return frac(x + shift); } // phase shift
template <class T> T quadr(T x) { return sawshift(x, T(0.25)); }

template <class T> T sinpi(T x) { return vmath::sinpi(x); }
template <class T> T cospi(T x) { return vmath::cospi(x); }
template <class T> T tanpi(T x) { return vmath::tanpi(x); }
template <class T> T sin2pi(T x) { return vmath::sinpi(T(2)*x); }
template <class T> T cos2pi(T x) { return vmath::cospi(T(2)*x); }
template <class T> T tan2pi(T x) { return vmath::tanpi(T(2)*x); }

template <class T> T sin1(T x) { return sinpi(T(2)*x); }
template <class T> T cos1(T x) { return cospi(T(2)*x); }
template <class T> T tan1(T x) { return tanpi(T(2)*x); }

template <class T> T usin(T x) { return uni(sin1(x)); }

template <class T> T fsin(T x) { // fast sine approx
    T q = princ1(x);
    return q * (T(8) - T(16) * vmath::abs(q));
}
template <class T> T fsinx(T x) { // fast sine approx (extra precision)
    T a = fsin(x);
    return a + T(0.224) * a * (vmath::abs(a) - T(1));
}
template <class T> T fcos(T x)  { return fsin(x + T(0.25)); }
template <class T> T fcosx(T x) { return fsinx(x + T(0.25)); }

// square waves
template <class T> T usquare(T x) { return vmath::truth(x < T(0.5)); }
template <class T> T bsquare(T x) { return sel2(x < T(0.5), T(-1), T(1)); }

// triangle waves
template <class T> T utri(T x) { return vmath::abs(bi(x)); }
template <class T> T btri(T x) { return vmath::abs(T(4)*x - T(2)) - T(1); }

// variable triangle and saw (difference of parabolas)
template <class T> T par(T x) { return sq(bi(x)); }
template <class T> T varsaw(T x, T pwm) { return (T(0.25) / (pwm - sq(pwm))) * (par(x) - par(frac(x - pwm))); }
template <class T> T vartri(T x, T pwm) { return uni(varsaw(x, pwm)); }

// pulse waves
template <class T> T upulse(T x, T pwm) { return vmath::truth(x < pwm); }
template <class T> T bpulse(T x, T pwm) { return bi(upulse(x, pwm)); }
template <class T> T zpulse(T x, T pwm) { return sel2(x < pwm, T(1) - pwm, -pwm); }

template <class T> T eoc(T x, T x1) { return vmath::truth(vmath::abs(x - x1) > T(0.5)); }

// pulse waves with a sample of each level every cycle, however narrow the pulse:
// a narrow one is high on the first sample of each cycle (where the phase
// wrapped since x1), and a wide one low on the last (where it will wrap by the
// next sample, if it increases by as much as it did since x1), so the pulse is
// still high for the first part of the cycle.
template <class T> T upulse1(T x, T x1, T pwm) {
	T u = upulse(x, pwm);
	T d = x - x1;
	auto first = vmath::abs(d) > T(0.5);
	return sel2(pwm < T(0.5), sel2(first, T(1), u), sel2(x + d >= T(1), sel2(first, u, T(0)), u));
}
template <class T> T bpulse1(T x, T x1, T pwm) { return bi(upulse1(x, x1, pwm)); }
template <class T> T zpulse1(T x, T x1, T pwm) { return upulse1(x, x1, pwm) - pwm; }

// triangle waves with various starting phases
// bipolar
template <class T> T btri0(T x) { return T(1) - vmath::abs(T(2) - T(4)*quadr(x)); }
template <class T> T btri1(T x) { return vmath::abs(T(2) - T(4)*x) - T(1); }
template <class T> T btri2(T x) { return vmath::abs(T(2) - T(4)*quadr(x)) - T(1); }
template <class T> T btri3(T x) { return T(1) - vmath::abs(T(2) - T(4)*x); }
// unipolar
template <class T> T utri0(T x) { return T(1) - vmath::abs(T(1) - T(2)*quadr(x)); }
template <class T> T utri1(T x) { return vmath::abs(T(1) - T(2)*x); }
template <class T> T utri2(T x) { return vmath::abs(T(1) - T(2)*quadr(x)); }
template <class T> T utri3(T x) { return T(1) - vmath::abs(T(1) - T(2)*x); }

// trapezoid waves
template <class T> T trapez0(T x) { return bclip(T(2) - vmath::abs(T(4) - T(8)*quadr(x))); }
template <class T> T trapez1(T x) { return bclip(vmath::abs(T(4) - T(8)*x) - T(2)); }
template <class T> T trapez2(T x) { return bclip(vmath::abs(T(4) - T(8)*quadr(x)) - T(2)); }
template <class T> T trapez3(T x) { return bclip(T(2) - vmath::abs(T(4) - T(8)*x)); }

// Intellijel Rubicon-inspired
template <class T> T izigzag(T x) { return vmath::abs(T(1) - T(2)*x) - vmath::truth(x > T(0.5)); }
template <class T> T ozigzag(T x) { return vmath::abs(T(1) - T(2)*x) - vmath::truth(x < T(0.5)); }

// smooth min, max, step
template <class T> T smooth_adjustment(T a, T b, T k) { return T(0.125) * k * qu(vmath::max(T(0), k - vmath::abs(a - b)) / k); }

template <class T> T distort(T x) { return x / (T(1) + vmath::abs(x)); }
template <class T> T softclip(T x) {
    T ax = vmath::abs(x);
    return sel2(ax < T(.5), x, (ax-T(.25))/x);
}


template <class T> T smin(T a, T b, T k) { return vmath::min(a, b) - smooth_adjustment(a, b, k); }
template <class T> T smax(T a, T b, T k) { return vmath::max(a, b) + smooth_adjustment(a, b, k); }

template <class T> T sstep(T x) {
    T t = uclip(x);
    return sq(t) * (T(3) - T(2)*t);
}

template <class T> T sstep2(T x) {
    T t = uclip(x);
    return cb(t) * (x * (x * T(6) - T(15)) + T(10)); // kept your original formula
}

template <class T> T lerp(T p, T a, T b) { return a + p*(b - a); }  // same as unilin

// waveforms with dynamic shape
template <class T> T sawenvsin(T x, T n, T m) { return sq(sinpi(n*x)) * vmath::pow(T(1) - x, m); }
template <class T> T altsawenvsin(T x, T n, T m) { return bsquare(x) * sawenvsin(frac(T(2)*x), n/T(2), m); }

template <class T> T decayCoeff(T n, T amp) { return vmath::pow(amp, T(1)/n); }

template <class T> T decay40dB(T n) { return decayCoeff(n, T(.01)); }
template <class T> T decay60dB(T n) { return decayCoeff(n, T(.001)); }

template <class T> T cubicInterpolate(T x, T y0, T y1, T y2, T y3)
{
    // 4-point, 3rd-order Hermite (x-form)
    T c0 = y1;
    T c1 = T(0.5) * (y2 - y0);
    T c2 = y0 - T(2.5) * y1 + T(2) * y2 - T(0.5) * y3;
    T c3 = T(1.5) * (y1 - y2) + T(0.5) * (y3 - y0);
    
    return ((c3 * x + c2) * x + c1) * x + c0;
}

#endif /* math_funs_h */
