-- DSP Math Utilities

-- ============================================================
-- Constants
-- ============================================================

const PI = 3.14159265358979323846;
const kTwoPi = 2.0 * PI;
const kDegToRad = PI / 180.0;
const kRadToDeg = 180.0 / PI;
const kMinToSecs = 60.0;
const kSecsToMin = 1.0 / 60.0;

let kLog10 = 10.0 log;
let kLog001 = 0.001 log;
let kLog01 = 0.01 log;
let kLogp1 = 0.1 log;
let kLog2o2 = 2.0 log / 2.0;
let kInvLog2 = 1.0 / (2.0 log);
let kSqrt2 = 2.0 sqrt;
let kInvSqrt2 = 1.0 / kSqrt2;

-- NaN constant (IEEE 754)
let NAN = 0.0 / 0.0;

-- The following are available as LangX builtins and are not redefined:
--   sqrt, cbrt, abs, floor, ceil, round, trunc, frac,
--   log, log2, log10, log1p, exp, exp2, expm1,
--   sin, cos, tan, asin, acos, atan, atan2,
--   sinh, cosh, tanh, asinh, acosh, atanh,
--   sinpi, cospi, tanpi, pow, hypot, copysign,
--   min, max, clamp, sign, isNan, isInf, isFinite

-- ============================================================
-- Sign functions
-- ============================================================

fn sgn(x Float) Float = x < 0.0 ? -1.0 : x > 0.0 ? 1.0 : 0.0;
fn sgnn(x Float) Float = x <= 0.0 ? -1.0 : 1.0;            -- zero is -1
fn sgnp(x Float) Float = x < 0.0 ? -1.0 : 1.0;             -- zero is +1
fn ustep(x Float) Float = x < 0.0 ? 0.0 : x > 0.0 ? 1.0 : 0.5;  -- Heaviside

fn cmp(a Float, b Float) Float = a < b ? -1.0 : a > b ? 1.0 : a == b ? 0.0 : NAN;

-- ============================================================
-- Rounding to quantum
-- ============================================================

fn roundq(x Float, q Float) Float = q == 0.0 ? x : q * (x / q) round;
fn roundUp(x Float, q Float) Float = q == 0.0 ? x : q * (x / q) ceil;
fn roundDn(x Float, q Float) Float = q == 0.0 ? x : q * (x / q) floor;

-- ============================================================
-- Powers and roots
-- ============================================================

fn sq(x Float) Float = x * x;
fn cb(x Float) Float = x * x * x;
fn qu(x Float) Float = x sq sq;
fn ssq(x Float) Float = x * x abs;                              -- signed square
fn ssqrt(x Float) Float = x sgn * (x abs sqrt);                 -- signed square root
fn scbrt(x Float) Float = x sgn * (x abs cbrt);                 -- signed cube root
fn spow(x Float, y Float) Float = x sgn * (x abs pow(y));       -- signed exponentiation

fn qurt(x Float) Float = x pow(0.25);                           -- fourth root

fn evenpow(x Float, p Float) Float = x abs pow(p);
fn oddpow(x Float, p Float) Float = x sgn * x evenpow(p);

-- ============================================================
-- Trig extensions
-- ============================================================

fn sin2pi(x Float) Float = 2.0 * x |> sinpi;
fn cos2pi(x Float) Float = 2.0 * x |> cospi;
fn tan2pi(x Float) Float = 2.0 * x |> tanpi;

fn sinc(a Float) Float = a == 0.0 ? 1.0 : a sin / a;

-- ============================================================
-- Fractional / principal value
-- ============================================================

-- frac is a builtin: frac(x) = x - floor(x)
fn princ1(x Float) Float = x - x round;

-- ============================================================
-- Unipolar / bipolar conversions
-- ============================================================

fn uni(x Float) Float = 0.5 + 0.5 * x;                     -- bipolar to unipolar
fn bi(x Float) Float = 2.0 * x - 1.0;                       -- unipolar to bipolar
fn lin(x Float, mul Float, add Float) Float = mul * x + add; -- slope intercept

-- ============================================================
-- Range predicates
-- ============================================================

fn isuni(x Float) Bool = 0.0 <= x && x <= 1.0;              -- in unipolar range
fn isbi(x Float) Bool = -1.0 <= x && x <= 1.0;              -- in bipolar range
fn isint(x Float) Bool = x == x floor;

-- ============================================================
-- Clamping
-- ============================================================

-- clamp(x, a, b) is a builtin
fn clamp2(x Float, a Float) Float = x clamp(-a, a);
fn uclamp(x Float) Float = x clamp(0.0, 1.0);
fn bclamp(x Float) Float = x clamp2(1.0);

-- ============================================================
-- Wrapping (Float)
-- ============================================================

fn wrap(x Float) Float = x frac;
fn wrap(x Float, a Float) Float = frac(x / a) * a;
fn wrap(x Float, a Float, b Float) Float = (x - a) wrap(b - a) + a;
fn bwrap(x Float) Float = (x + 1.0) wrap(2.0) - 1.0;

-- ============================================================
-- Wrapping (Int)
-- ============================================================

fn wrap(x Int, a Int) Int {
    let c = x % a;
    c < 0 ? c + a : c
}

fn wrap(x Int, a Int, b Int) Int = a + (x - a) wrap(b - a + 1);

-- ============================================================
-- Bending (Float)
-- ============================================================

fn bend(x Float) Float = 1.0 - abs(1.0 - x + 2.0 * (0.5 * x) floor);
fn bend(x Float, a Float) Float = bend(x / a) * a;
fn bend(x Float, a Float, b Float) Float = (x - a) bend(b - a) + a;
fn bbend(x Float) Float = (x + 1.0) bend(2.0) - 1.0;

-- ============================================================
-- Bending (Int)
-- ============================================================

fn bend(n Int, b Int) Int {
    let b2 = b + b;
    let c = n wrap(b2);
    c > b ? b2 - c : c
}

fn bend(n Int, lo Int, hi Int) Int {
    let b = hi - lo;
    let b2 = b + b;
    let c = (n - lo) wrap(b2);
    (c > b ? b2 - c : c) + lo
}

-- ============================================================
-- Mirroring (Int)
-- ============================================================

fn oddMirror(n Int, b Int) Int {
    let b2 = b + b - 2;
    let c = n wrap(b2);
    c > b - 1 ? b2 - c : c
}

fn evenMirror(n Int, b Int) Int {
    let b2 = b + b;
    let c = n wrap(b2);
    c >= b ? b2 - 1 - c : c
}

-- ============================================================
-- Integer division (floored)
-- ============================================================

fn idiv(a Int, b Int) Int {
    if (b != 0) {
        if (a < 0) { (a + 1) // b - 1 } else { a // b }
    } else {
        a
    }
}

fn idivz(a Int, b Int, z Int) Int {
    if (b != 0) {
        if (a < 0) { (a + 1) // b - 1 } else { a // b }
    } else {
        z
    }
}

-- ============================================================
-- Gradual step functions
-- ============================================================

fn smoothstep1(x Float) Float {
    let t = x uclamp;
    t * t * (3.0 - 2.0 * t)
}

fn smoothstep2(x Float) Float {
    let t = x uclamp;
    t * t * t * (10.0 - t * (15.0 + t * 6.0))
}

fn smooth_adjustment(a Float, b Float, k Float) Float =
    0.125 * k * (max(0.0, k - (a - b) abs) / k) qu;

fn smoothmin(a Float, b Float, k Float) Float =
    min(a, b) - a smooth_adjustment(b, k);

fn smoothmax(a Float, b Float, k Float) Float =
    max(a, b) + a smooth_adjustment(b, k);

-- ============================================================
-- Sigmoids
-- ============================================================

fn distort(x Float) Float = x / (1.0 + x abs);

fn softclip(x Float) Float {
    let ax = x abs;
    ax < 0.5 ? x : (ax - 0.25) / x
}

fn sigmoid1(x Float) Float = x / (x * x + 1.0) sqrt;

-- ============================================================
-- Window functions (unipolar domain)
-- ============================================================

fn rectWin(x Float) Float = x isuni ? 1.0 : 0.0;
fn triWin(x Float) Float = max(0.0, 1.0 - x bi abs);
fn hanWin(x Float) Float = x isuni ? x sinpi sq : 0.0;
fn sinWin(x Float) Float = x isuni ? x sinpi : 0.0;

-- Window functions (bipolar domain)
fn birectWin(x Float) Float = x isbi ? 1.0 : 0.0;
fn bitriWin(x Float) Float = max(0.0, 1.0 - x abs);
fn bihanWin(x Float) Float = x isbi ? x cospi uni : 0.0;
fn bisinWin(x Float) Float = x isbi ? 0.5 * x |> cospi : 0.0;

-- ============================================================
-- Linear interpolation
-- ============================================================

fn lerp(x Float, a Float, b Float) Float = a + x * (b - a);

-- ============================================================
-- Unipolar / linear / exponential range mapping
-- ============================================================

fn unilin(x Float, a Float, b Float) Float = a + x * (b - a);
fn uniexp(x Float, a Float, b Float) Float = a * (b / a) pow(x);
fn linuni(x Float, a Float, b Float) Float = (x - a) / (b - a);
fn expuni(x Float, a Float, b Float) Float = (x / a) log / (b / a) log;

fn bilin(x Float, a Float, b Float) Float = x uni unilin(a, b);
fn biexp(x Float, a Float, b Float) Float = x uni uniexp(a, b);
fn linbi(x Float, a Float, b Float) Float = x linuni(a, b) bi;
fn expbi(x Float, a Float, b Float) Float = x expuni(a, b) bi;

-- Range-to-range mapping
fn linlin(x Float, a Float, b Float, c Float, d Float) Float = x linuni(a, b) unilin(c, d);
fn linexp(x Float, a Float, b Float, c Float, d Float) Float = x linuni(a, b) uniexp(c, d);
fn explin(x Float, a Float, b Float, c Float, d Float) Float = x expuni(a, b) unilin(c, d);
fn expexp(x Float, a Float, b Float, c Float, d Float) Float = x expuni(a, b) uniexp(c, d);

-- ============================================================
-- Unipolar-to-unipolar warps
-- ============================================================

fn warp_pow(x Float, p Float) Float = x pow(p);
fn warp_sin(x Float) Float = 0.5 * x |> sinpi;
fn warp_asin(x Float) Float = (2.0 / PI) * x asin;

-- Double reflection (rotated 180 about (0.5, 0.5))
fn warp_powR(x Float, p Float) Float = 1.0 - (1.0 - x) pow(p);
fn warp_sinR(x Float) Float = 1.0 - (0.5 * (1.0 - x)) sinpi;
fn warp_asinR(x Float) Float = 1.0 - (2.0 / PI) * (1.0 - x) asin;

-- S-curve warps
fn warp_powS(x Float, p Float) Float =
    x < 0.5 ? 0.5 * (2.0 * x) pow(p) : 1.0 - 0.5 * (2.0 - 2.0 * x) pow(p);
fn warp_sinS(x Float) Float = (x - 1.0) sinpi uni;
fn warp_asinS(x Float) Float = x bi asin / PI + 0.5;

-- S-curve warps, double reflection
fn warp_powSR(x Float, p Float) Float =
    x < 0.5
        ? 0.5 * (1.0 - (1.0 - 2.0 * x) pow(p))
        : 0.5 * (1.0 + (2.0 * x - 1.0) pow(p));

fn warp_sinSR(x Float) Float =
    x < 0.5 ? 0.5 * x sinpi : 1.0 + 0.5 * (x + 1.0) sinpi;

fn warp_asinSR(x Float) Float =
    x < 0.5 ? (2.0 * x) asin / PI : 1.0 + (2.0 * x - 2.0) asin / PI;

-- General purpose warps
fn bwarp(x Float, w Float) Float {
    let u = 0.5 * x - 0.5;
    (w + u) / (w * x - u)
}

fn bwarpS(x Float, w Float) Float {
    if (x == 0.0) { return 0.0; }
    let wx = w * x;
    let sx = x sgn;
    sx * (wx - x) / (2.0 * wx - sx * w - x)
}

fn warp(x Float, w Float) Float = (w * x) / (w * x bi - x + 1.0);
fn warpS(x Float, w Float) Float = x bi bwarpS(w) uni;

-- ============================================================
-- Inversion
-- ============================================================

fn invert(x Float, a Float) Float = 2.0 * a - x;
fn invert(x Float, a Float, b Float) Float = a + b - x;

-- ============================================================
-- Musical units: note / octave / cent conversions
-- ============================================================

fn octnn(x Float) Float = x * 12.0;
fn nnoct(x Float) Float = x / 12.0;

fn nncents(x Float) Float = x * 100.0;
fn centsnn(x Float) Float = x / 100.0;

fn octcents(x Float) Float = x * 1200.0;
fn centsoct(x Float) Float = x / 1200.0;

-- ============================================================
-- Musical units: frequency conversions
-- ============================================================

fn nnhz(x Float) Float = 440.0 * ((x - 69.0) / 12.0) exp2;
fn hznn(x Float) Float = 69.0 + 12.0 * (x / 440.0) log2;

fn sthz(note Float) Float = 440.0 * ((note - 9.0) / 12.0) exp2;
fn hzst(freq Float) Float = (freq / 440.0) log2 * 12.0 + 9.0;

fn octhz(x Float) Float = 440.0 * (x - 5.75) exp2;
fn hzoct(x Float) Float = 5.75 + (x / 440.0) log2;

fn centshz(x Float) Float = 440.0 * ((x - 6900.0) / 1200.0) exp2;
fn hzcents(x Float) Float = (x / 440.0) log2 * 1200.0 + 6900.0;

fn centsratio(x Float) Float = x / 1200.0 |> exp2;
fn ratiocents(x Float) Float = x log2 * 1200.0;

fn semiratio(x Float) Float = x / 12.0 |> exp2;
fn ratiosemi(x Float) Float = x log2 * 12.0;

fn octratio(x Float) Float = x exp2;
fn ratiooct(x Float) Float = x log2;

-- ============================================================
-- Mel scale
-- ============================================================

fn hzmel(x Float) Float = 1127.010480334157439 * (1.0 + 0.00142857142857143 * x) log;
fn melhz(x Float) Float = 700.0 * (2.428571428571429 pow(0.001 * x) - 1.0);

-- ============================================================
-- Decibels
-- ============================================================

fn dbamp(x Float) Float = 10.0 pow(0.05 * x);
fn ampdb(x Float) Float = 20.0 * x log10;

-- ============================================================
-- BPM / tempo
-- ============================================================

fn bpmhz(x Float) Float = x * kSecsToMin;
fn hzbpm(x Float) Float = x * kMinToSecs;

fn bpmsec(x Float) Float = 60.0 / x;     -- involution
fn secbpm(x Float) Float = 60.0 / x;     -- involution

-- ============================================================
-- Angle conversions
-- ============================================================

fn degrad(x Float) Float = x * kDegToRad;
fn raddeg(x Float) Float = x * kRadToDeg;

fn hzsec(x Float) Float = 1.0 / x;
fn sechz(x Float) Float = 1.0 / x;

fn cycrad(x Float) Float = kTwoPi * x;
fn radcyc(x Float) Float = x / kTwoPi;

fn cycdeg(x Float) Float = 360.0 * x;
fn degcyc(x Float) Float = x / 360.0;

-- ============================================================
-- Indirect conversions
-- ============================================================

fn secnn(x Float) Float = x sechz hznn;
fn secoct(x Float) Float = x sechz hzoct;
fn seccents(x Float) Float = x sechz hzcents;

fn nnsec(x Float) Float = x nnhz hzsec;
fn octsec(x Float) Float = x octhz hzsec;
fn centssec(x Float) Float = x centshz hzsec;

fn bpmnn(x Float) Float = x bpmhz hznn;
fn bpmoct(x Float) Float = x bpmhz hzoct;
fn bpmcents(x Float) Float = x bpmhz hzcents;

fn nnbpm(x Float) Float = x nnhz hzbpm;
fn octbpm(x Float) Float = x octhz hzbpm;
fn centsbpm(x Float) Float = x centshz hzbpm;

-- ============================================================
-- Ring modulation variants
-- ============================================================

fn ring1(a Float, b Float) Float = a * b + a;
fn ring2(a Float, b Float) Float = a * b + a + b;
fn ring3(a Float, b Float) Float = a * a * b;
fn ring4(a Float, b Float) Float = a * b * (a - b);

-- ============================================================
-- Signal math
-- ============================================================

fn sumsq(a Float, b Float) Float = a sq + b sq;
fn sqsum(a Float, b Float) Float = (a + b) sq;
fn difsq(a Float, b Float) Float = a sq - b sq;
fn sqdif(a Float, b Float) Float = (a - b) sq;
fn absdif(a Float, b Float) Float = (a - b) abs;

-- ============================================================
-- Level control
-- ============================================================

fn vca(x Float, a Float) Float = x * max(0.0, a);
fn scaleneg(x Float, a Float) Float = x < 0.0 ? x * a : x;
fn scalepos(x Float, a Float) Float = x > 0.0 ? x * a : x;
fn scalenegpos(x Float, a Float, b Float) Float = x < 0.0 ? x * a : x * b;

-- ============================================================
-- Threshold functions
-- ============================================================

fn above(x Float, a Float) Float = x > a ? x : 0.0;
fn below(x Float, a Float) Float = x < a ? x : 0.0;
fn absabove(x Float, a Float) Float = x abs > a ? x : 0.0;
fn absbelow(x Float, a Float) Float = x abs < a ? x : 0.0;

-- ============================================================
-- Integer exponentiation
-- ============================================================

fn ipow(b Int, e Int) Int {
    var base = b;
    var n = e;
    var result = 1;
    while (n != 0) {
        if ((n & 1) != 0) { result = result * base; }
        n = n >> 1;
        if (n != 0) { base = base * base; }
    }
    result
}

fn ipowf(b Float, e Int) Float {
    var base = b;
    var n = e;
    var result = 1.0;
    while (n != 0) {
        if ((n & 1) != 0) { result = result * base; }
        n = n >> 1;
        if (n != 0) { base = base * base; }
    }
    result
}

-- ============================================================
-- GCD / LCM
-- ============================================================

fn gcd(a Int, b Int) Int {
    var u = a abs;
    var v = b abs;
    if (u <= 1 || v <= 1) { return 1; }
    var running = true;
    while (running) {
        if (u < v) {
            let tmp = u;
            u = v;
            v = tmp;
        }
        u = u % v;
        running = u > 0;
    }
    v
}

fn lcm(a Int, b Int) Int {
    if (a <= 0 || b <= 0) { return 0; }
    if (b == 1) { return a; }
    if (a == 1) { return b; }
    a // gcd(a, b) * b
}

-- ============================================================
-- Polynomial interpolation
-- ============================================================

fn cubicInterpolate(x Float, y0 Float, y1 Float, y2 Float, y3 Float) Float {
    -- 4-point, 3rd-order Hermite (x-form)
    let c0 = y1;
    let c1 = 0.5 * (y2 - y0);
    let c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
    let c3 = 1.5 * (y1 - y2) + 0.5 * (y3 - y0);
    ((c3 * x + c2) * x + c1) * x + c0
}

fn lagrangeInterpolate(x Float, y0 Float, y1 Float, y2 Float, y3 Float) Float {
    -- 4-point, 3rd-order Lagrange (x-form)
    let c0 = y1;
    let c1 = y2 - (1.0 / 3.0) * y0 - 0.5 * y1 - (1.0 / 6.0) * y3;
    let c2 = 0.5 * (y0 + y2) - y1;
    let c3 = (1.0 / 6.0) * (y3 - y0) + 0.5 * (y1 - y2);
    ((c3 * x + c2) * x + c1) * x + c0
}

fn cubicInterpolate2(x Float, y0 Float, y1 Float, y2 Float, y3 Float) Float {
    -- 4-point, 3rd-order Hermite (x-form), alternate
    let x2 = x * x;
    let x3 = x * x2;
    let x3b = 1.5 * x3;
    let c0 = x2 - 0.5 * (x + x3);
    let c1 = 1.0 - 2.5 * x2 + x3b;
    let c2 = 0.5 * x + 2.0 * x2 - x3b;
    let c3 = 0.5 * (x3 - x2);
    c0 * y0 + c1 * y1 + c2 * y2 + c3 * y3
}

-- ============================================================
-- Feedback / decay calculations
-- ============================================================

fn CalcFeedback(delaytime Float, decaytime Float) Float {
    if (delaytime == 0.0) {
        0.0
    } else if (decaytime > 0.0) {
        exp(kLog001 * delaytime / decaytime)
    } else if (decaytime < 0.0) {
        -exp(kLog001 * delaytime / -decaytime)
    } else {
        0.0
    }
}

fn CalcDecayRate(decaytime Float, sampleRate Float) Float {
    if (decaytime == 0.0) {
        0.0
    } else if (decaytime > 0.0) {
        exp(kLog001 / (decaytime * sampleRate))
    } else if (decaytime < 0.0) {
        -exp(kLog001 / (-decaytime * sampleRate))
    } else {
        0.0
    }
}

-- ============================================================
-- Denormal elimination
-- ============================================================

let kTiny = 10.0 pow(-15.0);
let kHuge = 10.0 pow(15.0);

fn zapgremlins(x Float) Float {
    let absx = x abs;
    if (absx > kTiny && absx < kHuge) { x } else { 0.0 }
}
