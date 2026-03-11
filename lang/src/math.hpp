// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

//
//  math.hpp
//  lispish
//
//  Created by James McCartney on 11/29/25.
//

#ifndef math_hpp
#define math_hpp

#include "base_types.hpp"

const f64 kTwoPi = 2. * M_PI;
const f64 kDegToRad = M_PI / 180.;
const f64 kRadToDeg = 180. / M_PI;
const f64 kMinToSecs = 60.; // beats per minute to beats per second
const f64 kSecsToMin = 1. / 60.; // beats per second to beats per minute

const f64 kLog10 = std::log(10.);
const f64 kLog001 = std::log(0.001);
const f64 kLog01  = std::log(0.01);
const f64 kLogp1   = std::log(0.1);
const f64 kLog2o2  = std::log(2.)/2.;
const f64 kInvLog2  = 1./std::log(2.);
const f64 kSqrt2  = std::sqrt(2.);
const f64 kInvSqrt2 = 1. / kSqrt2;

// functions
inline f64 sgn(f64 x) { return x < 0. ? -1. : x > 0. ? 1. : 0.; }
inline f64 sgnn(f64 x) { return x <= 0. ? -1. : 1.; } // zero is -1.
inline f64 sgnp(f64 x) { return x < 0. ? -1. : 1.; } // zero is 1.
inline f64 ustep(f64 x) { return x < 0. ? 0. : x > 0. ? 1. : .5; } // unipolar step. Heaviside function

inline f64 cmp(f64 a, f64 b) { return a < b ? -1. : a > b ? 1. : a == b ? 0. : NAN; }

inline f64 roundq(f64 x, f64 q)   { return q==0. ? x : q * std::round(x/q); }
inline f64 roundUp(f64 x, f64 q) { return q==0. ? x : q * std::ceil(x/q); }
inline f64 roundDn(f64 x, f64 q) { return q==0. ? x : q * std::floor(x/q); }

inline f64 sq(f64 x) { return x*x; }
inline f64 cb(f64 x) { return x*x*x; }
inline f64 qu(f64 x) { return sq(sq(x)); }
inline f64 ssq(f64 x) { return x*fabs(x); } // signed square
inline f64 ssqrt(f64 x) { return sgn(x)*std::sqrt(fabs(x)); } // signed square root
inline f64 scbrt(f64 x) { return sgn(x)*std::cbrt(fabs(x)); } // signed cube root
inline f64 spow(f64 x, f64 y) { return sgn(x)*::pow(fabs(x), y); } // signed exponentiation

inline f64 qurt(f64 x) { return std::pow(x,.25); }

inline f64 evenpow(f64 x, f64 p) { return std::pow(std::abs(x), p); }
inline f64 oddpow(f64 x, f64 p) { return sgn(x) * evenpow(x, p); }

inline f64 sinpi(f64 x) { return __sinpi(x); }
inline f64 cospi(f64 x) { return __cospi(x); }
inline f64 tanpi(f64 x) { return __tanpi(x); }
inline f64 sin2pi(f64 x) { return __sinpi(2.*x); }
inline f64 cos2pi(f64 x) { return __cospi(2.*x); }
inline f64 tan2pi(f64 x) { return __tanpi(2.*x); }

inline f64 sinc(f64 a) { return a == 0. ? 1. : std::sin(a) / a; }

inline f64 frac(f64 x)   { return x - std::floor(x); }
inline f64 princ1(f64 x) { return x - std::round(x); }

inline f64 uni(f64 x) { return .5 + .5 * x; } // bipolar to unipolar
inline f64 bi(f64 x)  { return 2. * x - 1.; } // unipolar to bipolar
inline f64 lin(f64 x, f64 mul, f64 add)  { return mul * x + add; } // slope intercept formula

inline bool isuni(f64 x) { return 0. <= x && x <= 1.; } // is in unipolar range
inline bool isbi(f64 x) { return -1. <= x && x <= 1.; } // is in bipolar range
inline bool isint(f64 x) { return x == std::floor(x); }

inline f64 clamp(f64 x, f64 a, f64 b) { return std::clamp(x,a,b); }
inline f64 clamp2(f64 x, f64 a) { return clamp(x,-a,a); }
inline f64 uclamp(f64 x) { return clamp(x,0.,1.); }
inline f64 bclamp(f64 x) { return clamp2(x,1.); }

inline f64 wrap(f64 x) { return frac(x); }
inline f64 wrap(f64 x, f64 a) { return frac(x/a) * a; }
inline f64 wrap(f64 x, f64 a, f64 b) { return wrap(x-a, b-a) + a; }
inline f64 bwrap(f64 x) { return wrap(x+1., 2.) - 1.; }

inline f64 fold(f64 x) { return 1. - std::abs(1. - x + 2. * std::floor(.5*x)); }
inline f64 fold(f64 x, f64 a) { return fold(x/a) * a; }
inline f64 fold(f64 x, f64 a, f64 b) { return fold(x-a, b-a) + a; }
inline f64 bfold(f64 x) { return fold(x + 1., 2.) - 1.; }

inline i32 wrap(i32 x, i32 a)
{
    assert(a >= 1);
    i32 c = x % a;
    return c<0 ? c+a : c;
}

inline i64 wrap(i64 x, i64 a)
{
    assert(a >= 1);
    i64 c = x % a;
    return c<0 ? c+a : c;
}

inline i64 wrap(i64 x, i64 a, i64 b)
{
    return a + wrap(x-a, b-a+1);
}

inline i32 fold(i32 in, i32 b)
{
    i32 b2 = b+b;
    i32 c = wrap(in, b2);
    if (c>b) c = b2-c;
    return c;
}

inline i64 fold(i64 in, i64 b)
{
    i64 b2 = b+b;
    i64 c = wrap(in, b2);
    if (c>b) c = b2-c;
    return c;
}

inline i64 fold(i64 in, i64 lo, i64 hi)
{
    i64 b = hi - lo;
    i64 b2 = b+b;
    i64 c = wrap(in - lo, b2);
    if (c>b) c = b2-c;
    return c + lo;
}

inline i64 oddMirror(i64 in, i64 b)
{
    i64 b2 = b+b-2;
    i64 c = wrap(in, b2);
    if (c>b-1) c = b2-c;
    return c;
}

inline i64 evenMirror(i64 in, i64 b)
{
    i64 b2 = b+b;
    i64 c = wrap(in, b2);
    if (c>=b) c = b2-1-c;
    return c;
}


inline i64 idiv(i64 a, i64 b)
{
    i64 c;
    if (b) {
        if (a<0) c = (a+1)/b - 1;
        else c = a/b;
    } else c = a;
    return c;
}

inline i64 idivz(i64 a, i64 b, i64 z)
{
    i64 c;
    if (b) {
        if (a<0) c = (a+1)/b - 1;
        else c = a/b;
    } else c = z;
    return c;
}

// gradual step functions

// uclamp is one.

inline f64 smoothstep1(f64 x)
{
    f64 t = uclamp(x);
    return t*t*(3. - 2*t);
}

inline f64 smoothstep2(f64 x) {
    x = uclamp(x);
    return x*x*x*(10. - x*(15.+x*6.));
}

inline f64 smooth_adjustment(f64 a, f64 b, f64 k)
{
	return .125 * k * qu(std::max(0., k - std::abs(a-b)) / k);
}

inline f64 smoothmin(f64 a, f64 b, f64 k)
{
	return std::min(a,b) - smooth_adjustment(a,b,k);
}

inline f64 smoothmax(f64 a, f64 b, f64 k)
{
	return std::max(a,b) + smooth_adjustment(a,b,k);
}

// sigmoids
inline f64 distort(f64 x) { return x / (1. + std::abs(x)); }
inline f64 softclip(f64 x) {
    f64 ax = std::abs(x);
    return ax<.5 ? x : (ax-.25)/x;
}
inline f64 sigmoid1(f64 x) { return x / sqrt(x*x+1.); }

// window functions
inline f64 rectWin(f64 x) { return isuni(x) ? 1. : 0.; }
inline f64 triWin(f64 x)  { return fmax(0., 1. - std::abs(bi(x))); }
inline f64 hanWin(f64 x)  { return isuni(x) ? sq(sinpi(x)) : 0.; }
inline f64 sinWin(f64 x)  { return isuni(x) ? sinpi(x) : 0.; }

inline f64 birectWin(f64 x) { return isbi(x) ? 1. : 0.; }
inline f64 bitriWin(f64 x)  { return fmax(0., 1. - std::abs(x)); }
inline f64 bihanWin(f64 x)  { return isbi(x) ? uni(cospi(x)) : 0.; }
inline f64 bisinWin(f64 x)  { return isbi(x) ? cospi(.5*x) : 0.; }

// linear interpolation
inline f64 lerp(f64 x, f64 a, f64 b) { return a + x * (b - a); } // same as unilin.

// transforming between unipolar and a linear or exponential range
inline f64 unilin(f64 x, f64 a, f64 b) { return a + x * (b - a); }
inline f64 uniexp(f64 x, f64 a, f64 b) { return a * std::pow(b/a, x); }
inline f64 linuni(f64 x, f64 a, f64 b) { return (x - a) / (b - a); }
inline f64 expuni(f64 x, f64 a, f64 b) { return std::log(x/a)/std::log(b/a); }

inline f64 bilin(f64 x, f64 a, f64 b) { return unilin(uni(x),a,b); }
inline f64 biexp(f64 x, f64 a, f64 b) { return uniexp(uni(x),a,b); }
inline f64 linbi(f64 x, f64 a, f64 b) { return bi(linuni(x,a,b)); }
inline f64 expbi(f64 x, f64 a, f64 b) { return bi(expuni(x,a,b)); }

// transforming between two ranges
inline f64 linlin(f64 x, f64 a, f64 b, f64 c, f64 d) { return unilin(linuni(x, a, b), c, d); }
inline f64 linexp(f64 x, f64 a, f64 b, f64 c, f64 d) { return uniexp(linuni(x, a, b), c, d); }
inline f64 explin(f64 x, f64 a, f64 b, f64 c, f64 d) { return unilin(expuni(x, a, b), c, d); }
inline f64 expexp(f64 x, f64 a, f64 b, f64 c, f64 d) { return uniexp(expuni(x, a, b), c, d); }

// unipolar to unipolar warps
inline f64 warp_pow(f64 x, f64 p) { return std::pow(x, p); }  // reciprocal of p gives a curve inverted around y = x.
inline f64 warp_sin(f64 x) { return sinpi(.5 * x); } // warp_asin is the inverse about y = x.
inline f64 warp_asin(f64 x) { return (2./M_PI) * std::asin(x); }

// unipolar to unipolar warps, double reflection (i.e., rotated 180 about (0.5,0.5))
inline f64 warp_powR(f64 x, f64 p) { return 1. - std::pow(1. - x, p); }  // reciprocal of p gives a curve inverted around y = x.
inline f64 warp_sinR(f64 x) { return 1. - sinpi(.5 * (1. - x)); } // warp_asinR is the inverse about y = x.
inline f64 warp_asinR(f64 x) { return 1. - (2./M_PI) * std::asin(1. - x); }

// unipolar to unipolar S warps
inline f64 warp_powS(f64 x, f64 p) { return x < .5 ? .5 * std::pow(2. * x, p) : 1. - .5 * std::pow(2. - 2. * x, p); }
inline f64 warp_sinS(f64 x) { return uni(sinpi(x - 1.)); }
inline f64 warp_asinS(f64 x) { return std::asin(bi(x))/M_PI + .5; }

// unipolar to unipolar S warps, double reflection
inline f64 warp_powSR(f64 x, f64 p) { return x < .5 ? .5 * (1. - std::pow(1. - 2. * x, p)) : .5 * (1. + std::pow(2. * x - 1., p)); }
inline f64 warp_sinSR(f64 x) { return x < .5 ? .5 * sinpi(x) : 1. + .5 * sinpi(x + 1.); }
inline f64 warp_asinSR(f64 x) { return x < .5 ? std::asin(2.*x)/M_PI : 1. + std::asin(2.*x - 2.)/M_PI; }

// general purpose warp
inline f64 bwarp(f64 x, f64 w) {  // bipolar to bipolar warping
    f64 u = .5*x-.5;
    return (w + u)/(w*x-u);
}
inline f64 bwarpS(f64 x, f64 w) {  // bipolar to bipolar S warping
    if (x == 0.) return 0.;
    f64 wx = w*x;
    f64 sx = sgn(x);
    return sx*(wx-x)/(2.*wx-sx*w-x);
}
inline f64 warp(f64 x, f64 w) { return (w*x) /(w*bi(x)-x+1.); } // unipolar to unipolar warping
inline f64 warpS(f64 x, f64 w) { return uni(bwarpS(bi(x),w)); } // unipolar to unipolar S warping

inline f64 invert(f64 x, f64 a) { return 2.*a - x;  }      // invert a signal about y=a.
inline f64 invert(f64 x, f64 a, f64 b) { return a+b-x;  }  // invert a signal within a range. (or equivalently, invert about y = (a+b)/2 )

// musical units conversion

inline f64 octnn(f64 x) { return x * 12.; }
inline f64 nnoct(f64 x) { return x / 12.; }

inline f64 nncents(f64 x) { return x * 100.; }
inline f64 centsnn(f64 x) { return x / 100.; }

inline f64 octcents(f64 x) { return x * 1200.; }
inline f64 centsoct(f64 x) { return x / 1200.; }

inline f64 nnhz(f64 x) { return 440. * std::exp2((x - 69.) / 12.); } // MIDI note number to Hertz
inline f64 hznn(f64 x) { return 69. + 12. * std::log2(x / 440.); } // Hertz to MIDI note number

inline f64 sthz(f64 note) { return 440. * std::exp2((note - 9.) / 12.); } // semitones to Hertz
inline f64 hzst(double freq) { return std::log2(freq / 440.) * 12. + 9.; } // Hertz to semitones

inline f64 octhz(f64 x) { return 440. * std::exp2(x - 5.75); } // octaves to Hertz
inline f64 hzoct(f64 x) { return 5.75 + std::log2(x / 440.); } // Hertz to octaves

inline f64 centshz(f64 x) { return 440. * std::exp2((x - 6900.) / 1200.); }
inline f64 hzcents(f64 x) { return std::log2(x / 440.) * 1200. + 6900.; }

inline f64 centsratio(f64 x) { return std::exp2(x / 1200.); }
inline f64 ratiocents(f64 x) { return std::log2(x) * 1200.; }

inline f64 semiratio(f64 x) { return std::exp2(x / 12.); }  // semitones to ratio
inline f64 ratiosemi(f64 x) { return std::log2(x) * 12.; }  // ratio to semitones

inline f64 octratio(f64 x) { return std::exp2(x); }  // semitones to ratio
inline f64 ratiooct(f64 x) { return std::log2(x); }  // ratio to semitones

// Hertz to/from Mel scale
// 1000/log(17/7) = 1127.010480334157439
// 1/700 = 0.00142857142857143
// 17/7 = 2.428571428571429
inline f64 hzmel(f64 x) { return 1127.010480334157439 * log(1. + 0.00142857142857143 * x); }
inline f64 melhz(f64 x) { return 700. * (pow(2.428571428571429, .001 * x) - 1.); }

inline f64 dbamp(f64 x) { return std::pow(10., .05 * x); }   // decibels to linear amplitude
inline f64 ampdb(f64 x) { return 20 * std::log10(x);  }      // linear amplitude to decibels

inline f64 bpmhz(f64 x) { return x*kSecsToMin; }   // beats per minute to Hertz
inline f64 hzbpm(f64 x) { return x*kMinToSecs;  }  // Hertz to beats per minute

inline f64 bpmsec(f64 x) { return 60./x; }   // beats per minute to seconds. involution.
inline f64 secbpm(f64 x) { return 60./x;  }  // seconds to beats per minute. involution.

inline f64 degrad(f64 x) { return x*kDegToRad; } // degrees to radians
inline f64 raddeg(f64 x) { return x*kRadToDeg; } // radians to degrees

inline f64 hzsec(f64 x) { return 1./x; }
inline f64 sechz(f64 x) { return 1./x; }

inline f64 cycrad(f64 x) { return 2. * M_PI * x; }
inline f64 radcyc(f64 x) { return x / (2. * M_PI); }

inline f64 cycdeg(f64 x) { return 360. * x; }
inline f64 degcyc(f64 x) { return x / 360.; }

// indirect conversions
inline f64 secnn(f64 x) { return hznn(sechz(x)); } // period to note number
inline f64 secoct(f64 x) { return hzoct(sechz(x)); } // period to octaves
inline f64 seccents(f64 x) { return hzcents(sechz(x)); } // period to cents

inline f64 nnsec(f64 x) { return hzsec(nnhz(x)); } // note number to period
inline f64 octsec(f64 x) { return hzsec(octhz(x)); } // octaves to period
inline f64 centssec(f64 x) { return hzsec(centshz(x)); } // cents to period

inline f64 bpmnn(f64 x) { return hznn(bpmhz(x)); } // BPM to note number
inline f64 bpmoct(f64 x) { return hzoct(bpmhz(x)); } // BPM to octaves
inline f64 bpmcents(f64 x) { return hzcents(bpmhz(x)); } // BPM to cents

inline f64 nnbpm(f64 x) { return hzbpm(nnhz(x)); } // BPM to note number
inline f64 octbpm(f64 x) { return hzbpm(octhz(x)); } // BPM to octaves
inline f64 centsbpm(f64 x) { return hzbpm(centshz(x)); } // BPM to cents



// variants of ring modulation
inline f64 ring1(f64 a, f64 b) { return a*b + a; }
inline f64 ring2(f64 a, f64 b) { return a*b + a + b; }
inline f64 ring3(f64 a, f64 b) { return a*a*b; }
inline f64 ring4(f64 a, f64 b) { return a*b*(a-b); }

inline f64 sumsq(f64 a, f64 b) { return sq(a)+sq(b); }
inline f64 sqsum(f64 a, f64 b) { return sq(a+b); }
inline f64 difsq(f64 a, f64 b) { return sq(a)-sq(b); }
inline f64 sqdif(f64 a, f64 b) { return sq(a-b); }
inline f64 absdif(f64 a, f64 b) { return std::abs(a-b); }

inline f64 vca(f64 x, f64 a) { return x * fmax(0., a); }
inline f64 scaleneg(f64 x, f64 a) { return x < 0. ? x * a : x; }
inline f64 scalepos(f64 x, f64 a) { return x > 0. ? x * a : x; }
inline f64 scalenegpos(f64 x, f64 a, f64 b) { return x < 0. ? x * a : x * b; }

inline f64 above(f64 x, f64 a) { return x > a ? x : 0.; }
inline f64 below(f64 x, f64 a) { return x < a ? x : 0.; }
inline f64 absabove(f64 x, f64 a) { return std::abs(x) > a ? x : 0.; }
inline f64 absbelow(f64 x, f64 a) { return std::abs(x) < a ? x : 0.; }


// an integer power of an integer
inline i32 ipow(i32 base, i32 exp)
{
    i32 result = 1;
    for (;;) {
        if (exp & 1) result *= base;
        exp >>= 1;
        if (!exp) break;
        base *= base;
    }
    return result;
}

inline i64 ipow(i64 base, i64 exp)
{
    i64 result = 1;
    for (;;) {
        if (exp & 1) result *= base;
        exp >>= 1;
        if (!exp) break;
        base *= base;
    }
    return result;
}

// an integer power of a floating point number
inline f64 ipowf(f64 base, i64 exp)
{
    f64 result = 1.;
    for (;;) {
        if (exp & 1) result *= base;
        exp >>= 1;
        if (!exp) break;
        base *= base;
    }
    return result;
}

template <class T>
inline T gcd(T u, T v)
{
    u = std::abs(u);
    v = std::abs(v);
    if (u <= 1 || v <= 1) return 1;
    do {
        if (u<v) std::swap(u,v);
        u = u % v;
    } while (u>0);
    return v;
}

template <class T>
inline T lcm(T u, T v)
{
    if (u<=0 || v<=0) return 0;
    if (v==1) return u;
    if (u==1) return v;
    return u/gcd(u,v) * v;
}

inline f64 cubicInterpolate(f64 x, f64 y0, f64 y1, f64 y2, f64 y3)
{
    // 4-point, 3rd-order Hermite (x-form)
    f64 c0 = y1;
    f64 c1 = 0.5 * (y2 - y0);
    f64 c2 = y0 - 2.5 * y1 + 2. * y2 - 0.5 * y3;
    f64 c3 = 1.5 * (y1 - y2) + 0.5 * (y3 - y0);
    
    return ((c3 * x + c2) * x + c1) * x + c0;
}


inline f64 lagrangeInterpolate(f64 x, f64 y0, f64 y1, f64 y2, f64 y3)
{
    // 4-point, 3rd-order Lagrange (x-form)
    f64 c0 = y1;
    f64 c1 = y2 - 1/3. * y0 - .5 * y1 - 1/6. * y3;
    f64 c2 = .5 * (y0 + y2) - y1;
    f64 c3 = 1/6. * (y3 - y0) + .5 * (y1 - y2);
    return ((c3 * x + c2) * x + c1) * x + c0;
}

inline f64 cubicInterpolate2(f64 x, f64 y0, f64 y1, f64 y2, f64 y3)
{
    // 4-point, 3rd-order Hermite (x-form)
    f64 x2 = x*x;
    f64 x3 = x*x2;
    f64 x3b = 1.5 * x3;
    f64 c0 = x2 - .5 * (x + x3);
    f64 c1 = 1. - 2.5*x2 + x3b;
    f64 c2 = .5 * x + 2. * x2 - x3b;
    f64 c3 = .5 * (x3 - x2);
    
    return c0*y0 + c1*y1 + c2*y2 + c3*y3;
}

inline f64 CalcFeedback(f64 delaytime, f64 decaytime)
{
    if (delaytime == 0.) {
        return 0.;
    } else if (decaytime > 0.) {
        return std::exp(kLog001 * delaytime / decaytime);
    } else if (decaytime < 0.) {
        return -std::exp(kLog001 * delaytime / -decaytime);
    } else {
        return 0.;
    }
}

inline f64 CalcDecayRate(f64 decaytime, f64 sampleRate)
{
    if (decaytime == 0.) {
        return 0.;
    } else if (decaytime > 0.) {
        return std::exp(kLog001 / (decaytime * sampleRate));
    } else if (decaytime < 0.) {
        return -std::exp(kLog001 / (-decaytime * sampleRate));
    } else {
        return 0.;
    }
}

template <class Float>
inline Float zapgremlins(Float x)
{
    Float absx = std::abs(x);
    // very small numbers fail the first test, eliminating denormalized numbers
    //    (zero also fails the first test, but that is OK since it returns zero.)
    // very large numbers fail the second test, eliminating infinities
    // Not-a-Numbers fail both tests and are eliminated.
    return (absx > 1e-15 && absx < 1e15) ? x : 0.;
}

inline x64 frac(x64 a)
{
	return x64(frac(a.real()), frac(a.imag()));
}

inline x64 round(x64 a)
{
	return x64(std::round(a.real()), std::round(a.imag()));
}

inline x64 trunc(x64 a)
{
	return x64(std::trunc(a.real()), std::trunc(a.imag()));
}

inline x64 ceil(x64 a)
{
	return x64(std::ceil(a.real()), std::ceil(a.imag()));
}

inline x64 floor(x64 a)
{
	return x64(std::floor(a.real()), std::floor(a.imag()));
}

inline x64 wrap(x64 a, x64 b)
{
	return b * frac(a/b);
}




inline x32 frac(x32 a)
{
	return x32(frac(a.real()), frac(a.imag()));
}

inline x32 round(x32 a)
{
	return x32(std::round(a.real()), std::round(a.imag()));
}

inline x32 trunc(x32 a)
{
	return x32(std::trunc(a.real()), std::trunc(a.imag()));
}

inline x32 ceil(x32 a)
{
	return x32(std::ceil(a.real()), std::ceil(a.imag()));
}

inline x32 floor(x32 a)
{
	return x32(std::floor(a.real()), std::floor(a.imag()));
}

inline x32 wrap(x32 a, x32 b)
{
	return b * frac(a/b);
}


#endif /* math_hpp */
