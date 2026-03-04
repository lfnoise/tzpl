import synthdefs.*;

-- common math

fn sign(x) = (x > 0) - (x < 0);

fn cmp(a, b) = (a > b) - (a < b);

fn ustep(x) = (x > 0);
fn ustepz(x) = sgn(x) uni;

fn cmpl(x) = 1 - x;  -- complement

fn frac(x) = x - x floor;
fn round(x) = floor(x + 0.5);
fn princ1(x) = x - x round;

-- round, floor, ceil with a quantum
fn round(x, q) = select2(q == 0, x, q * round(x / q));
fn floor(x, q) = select2(q == 0, x, q * floor(x / q));
fn ceil (x, q) = select2(q == 0, x, q * ceil (x / q));

fn sq(x) = x * x;
fn cb(x) = x * x * x;
fn qu(x) = x sq sq;

fn ssq(x) = x * x abs;  -- signed square
fn ssqrt(x) = x sgn * x abs sqrt;  -- signed real square root

fn spow(x, y) = x sgn * x abs pow(y);  -- signed pow

fn sin2pi(x) = sinpi(2 * x);
fn cos2pi(x) = cospi(2 * x);
fn tan2pi(x) = tanpi(2 * x);

fn sinc(x) = select2(x == 0, 1, x / x sin);

-- fast approximation of sine
fn fsin(x) {
    let q = x princ1;
    q * (8 - 16 * abs(q))
}

-- fast approximation of sine with extra precision
fn fsinx(x) {
    let a = x fsin;
    a + 0.224 * a * (abs(a) - 1)
}

fn fcos(x) = fsin(x+0.25); -- fast approximation of cosine
fn fcosx(x) = fsinx(x+0.25); -- fast approximation of cosine with extra precision


fn smoothStep(x) {
	let t = x uclip;
	t sq * (3 - 2 * t)
}

fn smoothStep2(x) {
	let t = x uclip;
	t cb * (10 - t * (15 + 6 * t))
}

fn bsmoothStep(x) {  -- bipolar in and out
	let t = x bclip;
	0.5 * t * (3 - t sq)
}

fn smoothAdjustment(a, b, k) = 0.125 * k * qu(max(0, k - abs(a - b)) / k);

fn smoothMin(a, b, k) = min(a, b) - smoothAdjustment(a, b, k);

fn smoothMax(a, b, k) = max(a, b) + smoothAdjustment(a, b, k);


-- clipping

fn max0(x) = x max(0);    -- a.k.a. relu = recified linear unit

fn clip(x, a, b) = x max(a) min(b);    -- clip between a <= x <= b
fn clip0(x, a) = x clip(0, a);         -- clip between 0 <= x <= a
fn clip2(x, a) = x clip(-a, a);        -- clip between -a <= x <= +a
fn uclip(x) = x clip(0, 1);            -- clip to unipolar range
fn bclip(x) = x clip(-1, 1);           -- clip to bipolar range

fn wrap(x, a, b) = a + mod(x-a, b-a);  -- wrap between a <= x < b
fn wrap0(x, a) = mod(x, a);            -- wrap between 0 <= x < a
fn wrap2(x, a) = x wrap(-a, a);        -- wrap between -a <= x < +a
fn uwrap(x) = x frac;                  -- wrap to unipolar range
fn bwrap(x) = x wrap(-1, 1);           -- wrap to bipolar range

fn fold(x, a, b) = fold0(x-a, b-a) + a;          -- fold between a <= x < b
fn fold0(x, a) = ufold(x/a)*a;                   -- fold between 0 <= x < a
fn fold2(x, a) = x fold(-a,a);                   -- fold between -a <= x < +a
fn ufold(x) = 1 - abs(1 - x + 2 * floor(0.5*x));  -- fold to unipolar range
fn bfold(x) = x fold(-1, 1);                     -- fold to bipolar range

fn excess(x, b) = x - x clip2(b);

-- sigmoids

fn distort(x) = x / (1 + x abs);

fn softclip_old(x) {
    let ax = x abs;
	sel(ax < 0.5, x, (ax - 0.25) / x)
}

fn softclip(x) = x - 4/27 * x clip(-1.25, 1.25) cb;


fn sigmoid0(x) = sel(abs(x)>1.5, sgn(x), x - (4/27)*cb(x));
fn sigmoid1(x) = erf(sqrt(pi)/2*x);
fn sigmoid2(x) = 2*x/(abs(2*x)+3/(2+4*sq(x)));
fn sigmoid3(x) = (27*x+cb(x))/(27+9*sq(x));
fn sigmoid4(x) = tanh(x);
fn sigmoid5(x) = sgn(x)*(1-1/(1+abs(x)+sq(x)+qu(x)));
fn sigmoid6(x) = x / sqrt(x sq + 1);
fn sigmoid7(x) = (2/pi)*atan(pi/2*x);
fn sigmoid8(x) = x/(1+abs(x));

--

fn isuni(x) = 0 <= x && x <= 1;  -- is in unipolar range

fn isbi(x) = -1 <= x && x <= 1;  -- is in bipolar range

fn isint(x) = x == x floor;

-- transforming between unipolar and a linear or exponential range

-- bipolar to unipolar
fn uni(x) = 0.5 + 0.5 * x;

-- unipolar to bipolar
fn bi(x) = 2 * x - 1;

-- the line formula, m*x + b
fn lin(x, m, b) = m * x + b;

-- exponential formula
fn axb(x, a, b) = a * pow(x, b);

-- unipolar to linear
fn unilin(x, a, b) = a + x * (b - a);
fn lerp(x, a, b) = a + x * (b - a);

-- unipolar to exponential
fn uniexp(x, a, b) = a * pow(b / a, x);

-- linear to unipolar
fn linuni(x, a, b) = (x - a) / (b - a);

-- exponential to unipolar
fn expuni(x, a, b) = log(x / a) / log(b / a);

-- bipolar to linear
fn bilin(x, a, b) = x uni unilin(a, b);

-- bipolar to exponential
fn biexp(x, a, b) = x uni uniexp(a, b);

-- linear to bipolar
fn linbi(x, a, b) = x linuni(a, b) bi;

-- exponential to bipolar
fn expbi(x, a, b) = x expuni(a, b) bi;


-- transforming between two ranges
-- linear to linear
fn linlin(x, a, b, c, d) = x linuni(a, b) unilin(c, d);

-- linear to exponential
fn linexp(x, a, b, c, d) = x linuni(a, b) uniexp(c, d);

-- exponential to linear
fn explin(x, a, b, c, d) = x expuni(a, b) unilin(c, d);

-- exponential to exponential
fn expexp(x, a, b, c, d) = x expuni(a, b) uniexp(c, d);


-- unipolar to unipolar warps
fn warp_pow(x, p) = pow(x, p);  -- reciprocal of p gives a curve inverted around y = x.

fn warp_sin(x) = sinpi(0.5 * x);  -- warp_asin is the inverse about y = x.

fn warp_asin(x) = (2 / pi) * arcsin(x);


-- unipolar to unipolar warps, double reflection (i.e., rotated 180 about (.5, .5))
fn warp_pow_r(x, p) = 1 - pow(1 - x, p);  -- reciprocal of p gives a curve inverted around y = x.

fn warp_sin_r(x) = 1 - sinpi(0.5 * (1 - x));  -- warp_asinR is the inverse about y = x.

fn warp_asin_r(x) = 1 - (2 / pi) * arcsin(1 - x);


-- unipolar to unipolar S warps
fn swarp_pow(x, p) = sel(x < 0.5, 0.5 * pow(2 * x, p), 1 - 0.5 * pow(2 - 2 * x, p));

fn swarp_sin(x) = sinpi(x - 1) uni;

fn swarp_asin(x) = x bi asin / pi + 0.5;


-- unipolar to unipolar S warps, double reflection
fn swarp_pow_r(x, p) = 0.5 * sel(x < 0.5, (1 - pow(1 - 2 * x, p)), (1 + pow(2 * x - 1, p)));

fn swarp_sin_r(x) = sel(x < 0.5, 0.5 * sinpi(x), 1 + 0.5 * sinpi(x + 1));

fn swarp_asin_r(x) = sel(x < 0.5, asin(2 * x) / pi, 1 + asin(2 * x - 2) / pi);


-- general purpose warp
fn bwarp(x, w) {  -- bipolar to bipolar warping
    let u = 0.5 * x - 0.5;
    (w + u) / (w * x - u)
}

fn bswarp(x, w) {  -- bipolar to bipolar S warping
    select2(x == 0, 0, {
    	let wx = w * x;
    	let sx = x sgn;
    	sx * (wx - x) / (2 * wx - sx * w - x)
	})
}

-- unipolar to unipolar warping
fn warp(x, w) = (w * x) / (w * bi(x) - x + 1);

-- unipolar to unipolar S warping
fn swarp(x, w) = uni(bswarp(bi(x), w));

-- invert a signal about y=a.
fn invert(x, a) = 2 * a - x;

-- invert a signal within a range. (or equivalently, invert about y = (a+b)/2 )
fn invert(x, a, b) = a + b - x;

-- variable order chebyshev
fn chebyv(x, n) = cos(x acos * n);

-- musical units conversion

let kSecsToMin = 1/60;
let kMinToSecs = 60;
let kDegToRad = pi/180;
let kRadToDeg = 180/pi;

fn octnn(x) = x * 12;   -- octaves to note number
fn nnoct(x) = x / 12;    -- note number to octaves

fn nncents(x) = x * 100;    -- note number to cents
fn centsnn(x) = x / 100;   -- cents to note number

fn octcents(x) = x * 1200;    -- octaves to cents
fn centsoct(x) = x / 1200;    -- cents to octaves

fn nnhz(x) = 440 * exp2((x - 69) / 12);  -- note number to Hertz
fn hznn(x) = 69 + 12 * log2(x / 440);    -- Hertz to note number

fn octhz(x) = 440 * exp2(x - 5.75);    -- octaves to Hertz
fn hzoct(x) = 5.75 + log2(x / 440);    -- Hertz to octaves

fn centshz(x) = 440 * exp2((x - 6900) / 1200);    -- cents to Hert
fn hzcents(x) = 6900 + log2(x / 440);    -- Hertz to cents

fn centsratio(x) = exp2(x / 1200);    -- cents to frequency ratio
fn ratiocents(x) = log2(x) * 1200;   -- frequency ratio to cents

fn stratio(x) = exp2(x / 12);    -- semitones to frequency ratio
fn ratiost(x) = log2(x) * 12;    -- frequency ratio to semitones

fn octratio(x) = exp2(x);    -- octaves to frequency ratio
fn ratiooct(x) = log2(x);    -- frequency ratio to octaves

fn dbamp(x) = pow(10, 0.05 * x);  -- decibels to linear amplitude
fn ampdb(x) = 20 * log10(x);     -- linear amplitude to decibels

fn bpmhz(x) = x * kSecsToMin;    -- beats per minute to Hertz
fn hzbpm(x) = x * kMinToSecs;    -- Hertz to beats per minute

fn bpmsec(x) = 60 / x;    -- beats per minute to seconds
fn secbpm(x) = 60 / x;    -- seconds to beats per minute

fn degrad(x) = x * kDegToRad;    -- degrees to radians
fn raddeg(x) = x * kRadToDeg;   -- radians to degrees

fn hzsec(x) = 1 / x;    -- Hertz to seconds
fn sechz(x) = 1 / x;    -- seconds to Hertz

fn cycrad(x) = 2 * pi * x;    -- cycles to radians
fn radcyc(x) = x / (2 * pi);  -- radians to cycles

fn cycdeg(x) = x * 360;    -- cycles to degrees
fn degcyc(x) = x / 360;    -- degrees to cycles

-- indirect conversions
fn secnn(x) = x sechz hznn;        -- period to note number
fn secoct(x) = x sechz hzoct;      -- period to octaves
fn seccents(x) = x sechz hzcents;  -- period to cents

fn nnsec(x) = x nnhz hzsec;        -- note number to period
fn octsec(x) = x octhz hzsec;      -- octaves to period
fn centssec(x) = x centshz hzsec;  -- cents to period

fn bpmnn(x) = x bpmhz hznn;        -- BPM to note number
fn bpmoct(x) = x bpmhz hzoct;      -- BPM to octaves
fn bpmcents(x) = x bpmhz hzcents;  -- BPM to cents

fn nnbpm(x) = x nnhz hzbpm;        -- BPM to note number
fn octbpm(x) = x octhz hzbpm;      -- BPM to octaves
fn centsbpm(x) = x centshz hzbpm;  -- BPM to cents

-- variants of ring modulation
fn ring1(a, b) = a * b + a;
fn ring2(a, b) = a * b + a + b;
fn ring3(a, b) = a * a * b;
fn ring4(a, b) = a * b * (a - b);

fn sumsq(a, b) = a sq + b sq;
fn sqsum(a, b) = sq(a + b);
fn difsq(a, b) = a sq - b sq;
fn sqdif(a, b) = sq(a - b);
fn absdif(a, b) = abs(a - b);

fn vca(x, a) = x * max(0, a);

fn scaleneg(x, a) = sel(x < 0, x * a, x);
fn scalepos(x, a) = sel(x > 0, x * a, x);
fn scalenegpos(x, a, b) = x * sel(x < 0, a, b);

fn above(x, a) = sel(x > a, x, 0);
fn below(x, a) = sel(x < a, x, 0);

fn absabove(x, a) = sel(x abs > a, x, 0);
fn absbelow(x, a) = sel(x abs < a, x, 0);

fn zapgremlins(x) {
    let ax = x abs;
    sel(1e-15 < ax && ax < 1e15, x, 0);
}

fn decayCoeff(n, amp) = amp pow(1/n);  -- calculate coefficient to decay to amp in n cycles.

fn decay40dB(n) = decayCoeff(n, 0.01);   -- calculate coefficient to decay by 40 dB in n cycles.

fn decay60dB(n) = decayCoeff(n, 0.001);  -- calculate coefficient to decay by 60 dB in n cycles.

---------

fn bfold(x) = 1 - abs(x - 1 - 4 * floor(0.25*x + 0.25));


    -- Bipolar triangular wave folding can be cheaper if the
    -- input can be assumed to be bounded in [-3, 5]
fn bfold_cheap(x) = abs(abs(x - 1) - 2) - 1;

    -- input can be assumed to be bounded in [-2, 2]
fn bfold_cheaper(x) = select2(x < -1, -2 - x, select2(x > 1, 2 - x, x));


-- chain

fn chain(x, n, f) = n times(x, f);


-- common unit generator functions

fn z1(a SignalExpr) {
	let d = delayVar();
	d <- a;
	d(1)
}

fn z2(a SignalExpr) {
	let d = delayVar();
	d <- a;
	d(2)
}

-- 'triggerization'. detects transition from x <= 0 to x > 0.
fn tr(x SignalExpr) = min(x > 0, x z1 <= 0);

-- End of cycle. Emit a trigger when phasor wraps.
fn eoc(x SignalExpr) = abs(x - x z1) > 0.5;

-- one initial impulse, then zero forever.
fn init() {
	let d = delayVar() init(1, 1) ;
	d <- 0;
	d read(1)
}

fn sampleAndHold(x SignalExpr, t SignalExpr) {
	let y = delayVar();
	select2(t > 0, x, y(1)) write(y)
}

-- flip flops

fn toggle(x SignalExpr) {
	let y = delayVar();
	select2(x > 0, 1 - y(1), y(1)) write(y)
}

fn setReset(s SignalExpr, r SignalExpr) {
	let y = delayVar();
	select2(r > 0, 0, select2(s > 0, 1, y(1))) write(y)
}

fn setResetToggle(s SignalExpr, r SignalExpr, t SignalExpr) {
	let y = delayVar();
	select2(r > 0, 0, select2(s > 0, 1, select2(t > 0, 1 - y(1), y(1)))) write(y)
}

-- simple filters

fn lag(x, t) {
    let a = decay40dB(t * fs());
    let y = delayVar();
    y <- x + a * (y(1) - x)
}

-- leaky integrator
fn leaky(x SignalExpr, a) {
	let y = delayVar();
    y <- x + a * y(1)
}

fn onepole(x, a) {
	let y = delayVar();
	y <- x + a * (y(1) - x)
}

fn onezero(x, a) = x + a * (z1(x) - x);

fn leakdc(x, k) {
	let y = delayVar();
	y <- x - z1(x) + k * y(1)
}

-- exponential decay
fn decay(x, t) = leaky(x, decay40dB(t * fs()));

fn decay2(x, atk, dcy) = x decay(dcy) - x decay(atk);

-- differentiation
fn diff(x SignalExpr) = x - x z1;   -- unscaled sample to sample difference
fn slope(x SignalExpr) = diff(x) * fs();
fn accel(x SignalExpr) = slope(slope(x));
fn jerk(x SignalExpr) = slope(accel(x));

-- integration
fn unscaledIntegrator(x SignalExpr)    { let y = delayVar(); y <- y(1) + x }
fn trapezoidalIntegrator(x SignalExpr) { let y = delayVar(); y <- y(1) + (x + x z1) * (T()/2) }
fn forwardIntegrator(x SignalExpr)     { let y = delayVar(); y <- y(1) + x z1 * T() }
fn backwardIntegrator(x SignalExpr)    { let y = delayVar(); y <- y(1) + x*T() }

-- integration with reset
fn unscaledIntegrator(x SignalExpr, r SignalExpr)    { 
	let y = delayVar();
	select2(r > 0, 0, y(1) + x) write(y) 
}
fn trapezoidalIntegrator(x SignalExpr, r SignalExpr) { 
	let y = delayVar();
	select2(r > 0, 0, y(1) + (x + x z1) * (T()/2)) write(y) 
}
fn forwardIntegrator(x SignalExpr, r SignalExpr) { 
	let y = delayVar();
	select2(r > 0, 0, y(1) + x z1 * T()) write(y)
}
fn backwardIntegrator(x SignalExpr, r SignalExpr) { 
	let y = delayVar();
	select2(r > 0, 0, y(1) + x * T()) write(y)
}

-- min and max followers
fn minfollow(x SignalExpr, r SignalExpr) { 
	let y = delayVar();
	select2(r > 0, x, min(x, y(1))) write(y) 
}

fn maxfollow(x SignalExpr, r SignalExpr) { 
	let y = delayVar();
	select2(r > 0, x, max(x, y(1))) write(y)
}


-- cubic panning function approximation. sqrt(sq(f(x)) + sq(f(1-x))) ~= 1 with max abs error less than +/- 0.0006 dB
fn panfun(x) {
    let a = 0.337403011047526069;
    let b = -1.334338069017510398;
    let c = -a-b-1;
    1 + x * (c + x * (b + x * a))
}

fn panfuns(x) = [x, 1 - x] panfun;

fn pan(x, pos) = x * pos uni panfuns;

-- signal movement
fn rising(x SignalExpr)   = x > x z1;
fn falling(x SignalExpr)  = x < x z1;
fn changing(x SignalExpr) = x != x z1;
fn nochange(x SignalExpr) = x == x z1;
fn localmax(x SignalExpr) = (x < x z1) * (x z1 > x z2);
fn localmin(x SignalExpr) = (x > x z1) * (x z1 < x z2);

fn fadein(x, t) {
    let dt = 1 / (t * fs());
    let y = delayVar();
    min(1, dt + y(1)) write(y) cb * x
}


fn pinkingFilter(x) {
    -- from Paul Kellett
    let b0 = delayVar(); (0.99886 * b0(1) + x * 0.0555179) write(b0);
    let b1 = delayVar(); (0.99332 * b1(1) + x * 0.0750759) write(b1);
    let b2 = delayVar(); (0.96900 * b2(1) + x * 0.1538520) write(b2);
    let b3 = delayVar(); (0.86650 * b3(1) + x * 0.3104856) write(b3);
    let b4 = delayVar(); (0.55000 * b4(1) + x * 0.5329522) write(b4);
    let b5 = delayVar(); (-0.7616 * b5(1) - x * 0.0168980) write(b5);
    let b6 = delayVar(); (x * 0.115926) write(b6);
    return b0() + b1() + b2() + b3() + b4() + b5() + b6(1) + x * 0.5362;
}

fn pinkingFilterMat(x) {
    -- from Paul Kellett
    let bCoeffs = [0.0555179, 0.0750759, 0.1538520, 0.3104856, 0.5329522, -0.0168980];
    let aCoeffs = [0.99886,   0.99332,   0.96900,   0.86650,   0.55000,   -0.7616   ];
	let d1 = delayVar();
	let d2 = delayVar();

	d2 <- x * 0.115926;
    0.5362 * x + d1 write(aCoeffs * d1(1) + bCoeffs * x) sum + d2(1)
}


fn pinkingFilterEco(x)
{
    let b0 = delayVar(); (0.99765 * b0(1) + x * 0.0990460) write(b0);
    let b1 = delayVar(); (0.96300 * b1(1) + x * 0.2965164) write(b1);
    let b2 = delayVar(); (0.57000 * b2(1) + x * 1.0526913) write(b2);
    b0 + b1 + b2 + x * 0.1848
}

fn pinkingFilterEcoMat(x)
{
    let bCoeffs = [0.0990460, 0.2965164, 1.0526913];
    let aCoeffs = [0.99765, 0.96300, 0.57000];
	let d = delayVar();
    0.1848 * x + d1 write(aCoeffs * d(1) + bCoeffs * x) sum;
}


fn irand(n Int, cast = i64) = (urand() * n) floor cast;


fn white(chans Int = 1) = birand(chans);

fn pinkf(chans Int = 1) = 0.25 * white(chans) pinkingFilter;
fn pinkfe(chans Int = 1) = 0.25 * white(chans) pinkingFilterEco;

fn pinkmf(chans Int = 1) = 0.25 * white(chans) pinkingFilterMat;
fn pinkmfe(chans Int = 1) = 0.25 * white(chans) pinkingFilterEcoMat;

fn violet(chans Int = 1) = 0.5 * white(chans) diff;

fn blue(chans Int = 1) = 0.5 * pinkf(chans) diff;

fn red(chans Int = 1, a=0.05) {
	let y = delayVar();
	(y(1) + chans birand * a) bfold_cheaper write(y)
}

fn coin(prob, chans Int = 1) = urand(chans) < prob;

fn velvet(freq, chans Int = 1) = coin(freq * T(), chans);

fn dust(freq, chans Int = 1) = urand(chans) * velvet(freq, chans);

fn dust2(freq, chans Int = 1) = birand(chans) * velvet(freq, chans);

fn dustep(freq, chans Int = 1) = white(chans) sampleAndHold(velvet(freq, chans));

fn exprand(a, b, chans Int = 1, rate = rates.audio) = urand(chans, rate) uniexp(a, b);


-- unipolar waveshapers

fn sawshift(x, shift) = frac(x + shift); -- phase shift a unipolar sawtooth.
fn quadr(x) = frac(x + 0.25) ;  -- shift a phasor by one quarter cycle

-- triangle waves
    -- Unipolar ramp to bipolar triangle wave.
fn btri(x) = abs(4 * x - 2) - 1;
fn btri0(x) = 1 - abs(4 * x quadr - 2);   --   0 degrees initial phase
fn btri1(x) = abs(4 * x - 2) - 1;         --  90 degrees initial phase
fn btri2(x) = abs(4 * x quadr - 2) - 1;   -- 180 degrees initial phase
fn btri3(x) = 1 - abs(4 * x - 2);         -- 270 degrees initial phase

fn utri0(x) = 1 - abs(1 - 2 * x quadr);   --   0 degrees. unipolar output. '\,  trisin
fn utri1(x) = abs(1 - 2 * x);             --  90 degrees. unipolar output. \/   tricos
fn utri2(x) = abs(1 - 2 * x quadr);       -- 180 degrees. unipolar output. ,/' -trisin
fn utri3(x) = 1 - abs(1 - 2 * x);         -- 270 degrees. unipolar output. /\  -tricos

-- trapezoid waves
fn trapez0(x) = bclip(2 - abs(4 - 8 * x quadr));    --   0 degrees. bipolar output.
fn trapez1(x) = bclip(abs(4 - 8 * x) - 2);          --  90 degrees. bipolar output.
fn trapez2(x) = bclip(abs(4 - 8 * x quadr) - 2);    -- 180 degrees. bipolar output.
fn trapez3(x) = bclip(2 - abs(4 - 8 * x));          -- 270 degrees. bipolar output.

-- pulse waves
fn upulse(x, pwm) = x < pwm;              -- unipolar pulse wave
fn bpulse(x, pwm) = upulse(x,pwm) bi;     -- bipolar pulse wave
fn zpulse(x, pwm) = frac(x - pwm) - x;    -- zero DC pulse wave

    -- pulse waves. As in SuperCollider, these always output one value of the opposite polarity each cycle.
fn upulse1(x, pwm) = x eoc sel(pwm < 0.5, x < pwm);       -- unipolar
fn bpulse1(x, pwm) = x upulse1(pwm) bi;                  -- bipolar
fn zpulse1(x, pwm) = x eoc sel(pwm < 0.5, x zpulse(pwm)); -- zero DC

    -- inspired by the Intellijel Rubicon waveform.
fn izigzag(x) = x utri1 - (x > 0.5);  -- moving inwards
fn ozigzag(x) = x utri1 - (x < 0.5);  -- moving outwards

-- Bipolar ramp to bipolar triangle wave.
fn bbtri(x) = abs(2 * x - 4 * x uni floor) - 1;
	
    -- variable triangle and saw
    -- an amplitude scaled difference of parabolas
fn par(x) = x bi sq;   -- unipolar ramp to parabola
fn vartri(x, pwm) = (0.25 / (pwm - sq(pwm))) * (par(x) - par(frac(x - pwm)));
fn varsaw(x, pwm) = bi(vartri(x, pwm));

fn usquare(x) = x < 0.5;
fn bsquare(x) = sel(x < 0.5, -1, 1);

-- phasor generates a unipolar sawtooth. 
-- It is the core of many oscillators.
fn phasor(fm, pm=0) {
	let phase = delayVar();
	let p = frac(phase(1) + fm * T()) write(phase);
	pm == 0 ? p : frac(p + pm)
}

fn lfsaw(fm, pm=0) = phasor(fm, pm) bi;
fn lfimp(fm, pm=0) = phasor(fm, pm) eoc;
fn lftri(fm, pm=0) = phasor(fm, pm) btri;
fn lfpar(fm, pm=0) = phasor(fm, pm) par;
fn lftrap(fm, pm=0) = phasor(fm, pm) trapez0;
fn lfusqr(fm, pm=0) = phasor(fm, pm) usquare;
fn lfsqr(fm, pm=0) = phasor(fm, pm) bsquare;
fn lfzig(fm, pm=0) = phasor(fm, pm) izigzag;
fn lfzag(fm, pm=0) = phasor(fm, pm) ozigzag;
fn lfpar(fm, pm=0) = phasor(fm, pm) par bi;
fn lfvsaw(fm, pwm, pm=0) = phasor(fm, pm) varsaw(pwm);
fn lfupulse(fm, pwm, pm=0) = phasor(fm, pm) upulse1(pwm);
fn lfbpulse(fm, pwm, pm=0) = phasor(fm, pm) bpulse1(pwm);
fn lfzpulse(fm, pwm, pm=0) = phasor(fm, pm) zpulse(pwm);


fn sinosc(fm, pm=0) = phasor(fm, pm) sin2pi;
fn sinosc(fm, pm=0) = phasor(fm, pm) sin2pi;

fn fsinosc(fm, pm=0) = phasor(fm, pm) fsin;
fn fsinxosc(fm, pm=0) = phasor(fm, pm) fsinx;

fn combn(x SignalExpr, delay_time, decay_time) {
	let a = decay60dB(decay_time / delay_time);
	let delay_samples = delay_time * fs();
	let y = delayVar(delay_samples);
	x + a * y(delay_samples) |> write(y)
}

fn combl(x SignalExpr, delay_time, max_delay_time, decay_time) {
	let a = decay60dB(decay_time / delay_time);
	let delay_samples = delay_time * fs();
	let b = delay_samples frac;
	let y = delayVar(max_delay_time * fs());
	x + a * lerp(b, y(delay_samples), y(delay_samples+1)) |> write(y)
}


fn pull(gate SignalExpr, initVal, gatedFun) {
	let d = delayVar();
	d init(0, initVal asSignal);
	if_(gate > 0, fn(){ gatedFun() write(d) }, fn(){ d(1) })
}
fn pause(gate SignalExpr, gatedFun) = if_(gate > 0, fn(){ gate * gatedFun() });

-- tests

fn bubbles() = 0.4 lfsaw * 24 + [8, 7.23] lfsaw * 3 + 81 |> nnhz sinosc * 4c |> combn(0.2,4) outlet;

bubbles defSynth("bubbles");

fn dustone() = 0.2 * decay2(dust(4, 2), 0.04, 0.3) * 800 fsinxosc |> outlet;

dustone defSynth("dustone");

fn init_urand_test() {
	let detune = [-1, 1] vec;
	let freqs = exprand(100, 600, 8, rates.init) + detune;
	let ampPhases = urand(16, rates.init);
	let ampFreqs = exprand(0.1, 0.5, 16, rates.init);
	let oscs = freqs fsinosc cb * ampFreqs fsinosc(ampPhases) uni;
	oscs sum(2) * 0.1 |> outlet
}

init_urand_test defSynth("init_urand_test");


fn pause_bubbles() {
    let gate = 0.5 sinosc - 0.5;
    let out = gate pause(fn(){   
        let freq = nnhz(0.4 lfsaw * 24 + [8, 7.23] lfsaw * 3 + 81);
        0.05 * freq fsinosc 
	});
    out fadein(0.1) combn(0.2, 4) outlet
}

pause_bubbles defSynth("pause_bubbles");

fn tog_pause() {
    let s0 = 1 lfusqr;
    let s0f = s0      lag(0.05) - 0.01;
    let s1f = s0 cmpl lag(0.05) - 0.01;
    let out = s0f pause(fn(){ 0.1 * 2 blue }) + s1f pause(fn(){ fsinxosc(100 + 2 white) max(-0.2) cb * 0.15  });
    out fadein(0.1) combn(0.2, 2) outlet
}

tog_pause defSynth("tog_pause")


fn pull_nested() {
	let gate1 = 0.3 fsinosc max0;
    let out = gate1 pause(fn(){ 
        let gate2 = 2 fsinosc max0;
        let out = gate2 pause(fn(){
       		let freq = nnhz(81 + 24 * 0.4 lfsaw + 3 * [8, 7.23] lfsaw);
            0.04 * freq fsinxosc
		});
        out fadein(0.1) combn(0.2, 4);
	});
    out outlet
}

pull_nested defSynth("pull_nested");


fn pulltwo() {
    let pullFun1 = fn(){
        let amp = 0.2 * dust(4, 2) decay2(0.04, 0.3);
        amp * 800 fsinxosc
    };
    let pullFun2 = fn(){
       	let freq = nnhz(81 + 24 * 0.4 lfsaw + 3 * [8, 7.23] lfsaw);
        0.04 * fsinxosc(freq) fadein(0.1) combn(0.2, 4)
    };
    let gate1 = 2.71828 fsinosc max0;
    let gate2 = 3.14159 fsinosc max0;
    let out = gate1 pause(pullFun1) + gate2 pause(pullFun2);
    out outlet
}

pulltwo defSynth("pulltwo");


fn pch_seq() {
    let pch = 3 lfimp pull(200, fn(){ exprand(125, 1000) });
    let out = 0.1 * fsinxosc(pch lag(0.05) + [0,1]) cb;
    out fadein(0.1) combn(0.2, 4) outlet
}


pch_seq defSynth("pch_seq");


fn() SignalExpr { 0.3 sinosc biexp(1h, 6k) velvet(2) * 0.2 |> outlet } defSynth("dust1");


fn sahtone1() {
	let freq = 2 white sampleAndHold(coin(3*T())) biexp(120, 800) lag(0.04);
	let in = freq lftri * 0.2;
	in combn(0.2, 2) outlet
}

sahtone1 defSynth("sahtone1");



fn sahtone2() {
	let freq = 2 white sampleAndHold(3 lfimp) biexp(120, 800) lag(0.04);
	let in = freq fsinxosc tanh * 0.2;
	in combn(0.2, 2) outlet
}

sahtone2 defSynth("sahtone2");


fn mod1_test() {
	let c = 1.4 fsinosc bilin(0.1, 0.95);
	let in = [60, 61] lfsaw([0, 0.25]) * 0.1;
	in chain(4, fn(x){x onepole(c)}) outlet
	in chain(4, fn(x){x onepole(c)}) outlet
}

mod1_test defSynth("mod1_test");

fn mod4_test() {
	let c = 0.4 fsinosc bilin(0.1, 0.95);
	let amp = lfimp(1/1.2) decay2(0.01, 0.2) * 0.6;
	let in = [200, 251] lfsaw([0, 0.25]);
	let out = amp * in chain(4, fn(x){x onepole(c)});
	out combn([0.3,0.15], 4) outlet
}

mod4_test defSynth("mod4_test");

fn mod5_test() {
	let dv = 0.2;
	let delayTimes = [0.3, 0.15];
	let delayMod = 0.5 fsinosc lin(dv, 1);
	let delayMax = (1 + dv) * delayTimes;
	let amp = lfimp(1/1.2) decay2(0.01, 0.2) * 0.6;
	let in = [200, 251] lfzig([0, 0.25]);
	let out = amp * in chain(4, fn(x){x onepole(0.9)});
	out combl(delayTimes * delayMod, delayMax, 4) outlet
}

mod5_test defSynth("mod5_test");


fn white_test() = outlet(2 white * 0.2);

white_test defSynth("white_test");


fn pink_test() = outlet(2 pinkf * 0.2);

pink_test defSynth("pink_test");


fn violet_test() = outlet(2 violet * 0.2);

violet_test defSynth("violet_test");


fn blue_test() = outlet(2 blue * 0.2);

blue_test defSynth("blue_test");


fn red_test() = outlet(2 red * 0.2);

red_test defSynth("red_test");

fn bubbles_lite() = 0.4 lfsaw * 24 + 8 lfsaw * 3 + 81 |> nnhz sinosc * 4c |> outlet;

-- bubbles_lite defSynth("bubbles_lite")

"DONE IMPORTING COMMON UGENS MODULE" println


