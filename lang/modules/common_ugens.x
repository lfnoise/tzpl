import synthdef.*;

const pi = 3.14159265358979323846264338327950;
const twopi = 2 * pi;

-- common math

-- safe division.
fn divz(numer, denom, otherwise) = select2(denom == 0.0, otherwise, numer / denom);

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

-- unipolar sine wave
fn usin(x) = 1 - x cos uni;
fn usinpi(x) = 1 - x cospi uni;
fn usin2pi(x) = 1 - x cos2pi uni;

fn sinc(x) = divz(x sin, x, 1);
fn sincpi(x) = divz(x sin2pi, x, 1);

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

fn softclip(x) {
    let ax = x abs;
	select2(ax < 0.5, x, (ax - 0.25) / x)
}

fn sigmoid0(x) = select2(abs(x)>1.5, sgn(x), x - (4/27)*cb(x));
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
fn swarp_pow(x, p) = select2(x < 0.5, 0.5 * pow(2 * x, p), 1 - 0.5 * pow(2 - 2 * x, p));

fn swarp_sin(x) = sinpi(x - 1) uni;

fn swarp_asin(x) = x bi asin / pi + 0.5;


-- unipolar to unipolar S warps, double reflection
fn swarp_pow_r(x, p) = 0.5 * select2(x < 0.5, (1 - pow(1 - 2 * x, p)), (1 + pow(2 * x - 1, p)));

fn swarp_sin_r(x) = select2(x < 0.5, 0.5 * sinpi(x), 1 + 0.5 * sinpi(x + 1));

fn swarp_asin_r(x) = select2(x < 0.5, asin(2 * x) / pi, 1 + asin(2 * x - 2) / pi);


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

fn hzw(x) S = x * twopi * T(); -- Hertz to radians per sample
fn whz(x) S = x * fs() / twopi; -- radians per sample to Hertz

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

fn scaleneg(x, a) = select2(x < 0, x * a, x);
fn scalepos(x, a) = select2(x > 0, x * a, x);
fn scalenegpos(x, a, b) = x * select2(x < 0, a, b);

fn above(x, a) = select2(x > a, x, 0);
fn below(x, a) = select2(x < a, x, 0);

fn absabove(x, a) = select2(x abs > a, x, 0);
fn absbelow(x, a) = select2(x abs < a, x, 0);

fn zapgremlins(x) {
    let ax = x abs;
    select2(1e-15 < ax && ax < 1e15, x, 0);
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

fn chain(x S, n Int, f fn(S)S) S = n <= 0 ? x : chain(f(x), n-1, f);


-- common unit generator functions

fn z1(a S) S {
	let d = delayVar();
	d <- a;
	d(1)
}
fn z1(a S, i AsSignal) S {
	let d = delayVar() init(1, i);
	d <- a;
	d(1)
}

fn z2(a S) S {
	let d = delayVar();
	d <- a;
	d at(2)
}

-- 'triggerization'. detects transition from x <= 0 to x > 0.
fn tr(x S) = min(x > 0, x z1 <= 0);

-- End of cycle. Emit a trigger when phasor wraps.
fn eoc(x S) = abs(x - x z1) > 0.5;

-- one initial impulse, then zero forever.
fn init() S {
	let d = delayVar() init(1, 1);
	d <- 0;
	d read(1)
}

fn sampleAndHold(x S, t S) S {
	let y = delayVar();
	y <- select2(t > 0, x, y(1))
}

fn once(x S) S {
	let y = delayVar();
	y <- select2(x > 0, 1, y(1))
}

-- flip flops

fn toggle(x S) S {
	let y = delayVar();
	y <- select2(x > 0, 1 - y(1), y(1))
}

fn setReset(s S, r S) S {
	let y = delayVar();
	y <- select2(r > 0, 0, select2(s > 0, 1, y(1)))
}

fn setResetToggle(s S, r S, t S) S {
	let y = delayVar();
	y <- select2(r > 0, 0, select2(s > 0, 1, select2(t > 0, 1 - y(1), y(1))))
}

-- trigger divider
fn trDiv(x S, n, offset=0) {
	let t = x > 0;
	let c = delayVar();
	let c1 = c(1);
	c <- (c1 + t) i32 % n;
	y <- t * (c1 == offset)
}

-- trigger counter with reset
fn trCount(x S, reset AsSignal) S {
	let y = delayVar();
	y <- select2(reset > 0, 0, y(1) + (x > 0))
}

-- trigger counter
fn trCount(x S) S {
	let y = delayVar();
	y <- y(1) + (x > 0)
}

-- when triggered, makes a line from 1 to 0 over duration. can be used as a timer or a phasor.
fn oneshot1(trig S, dur) S { 
	let dt = 1 / (fs() * dur);
	let y = delayVar();
	y <- select2(trig > 0, 1, max(0, y(1) - dt))
}

-- when triggered, makes a line from 0 to 1 over duration. can be used as a timer or a phasor.
fn oneshot(trig S, dur) S { 
	let x = oneshot1(trig, dur);
	(x != 0) * (1 - x)
}


fn timedGate(trig S, dur) S = oneshot1(trig, dur) != 0;

-- triggered burst of n impulses
fn burst(trig S, dur, n) S = (n * oneshot(trig, dur)) frac eoc;


-- triggered burst of n impulses with warped timing
fn burst(trig S, dur, n, w) S = (n * oneshot(trig, dur) warp(w)) frac eoc;


-- sequencer
fn seq(trigger S, pattern AsSignal, length AsSignal) S {
	let index = delayVar() init(1, -1);
	let oldIndex = index(1);
	let newIndex = select2(trigger > 0, (oldIndex + 1) % length, oldIndex);
	index <- newIndex;
	pattern at(newIndex)
}

-- impulse sequencer
fn iseq(trigger S, pattern AsSignal, length AsSignal) S {
	let index = delayVar() init(1, -1);
	let oldIndex = index(1);
	let newIndex = select2(trigger > 0, (oldIndex + 1) % length, oldIndex);
	index <- newIndex;
	select2(trigger > 0, pattern at(newIndex), 0)
}

-- envelopes

fn asr(gate S, a, s, r) S {
	let a = a decay40dB;
	let r = r decay40dB;
	let y = delayVar();

	-- stages: 0: gate off (released + waiting for attack) 1: gate on (attack + sustain).
	-- Unlike adsr the stage is memoryless -- it is just gate>0 -- so it needs no
	-- delay line; select it directly.

	let y1 = y(1);
	let stage = gate > 0;
	let goal = select(stage, [0 asSignal, s asSignal]);
	let coef = select(stage, [r asSignal, a asSignal]);
	
    y <- goal + coef * (y1 - goal)
}

fn adsr(gate S, a, d, s, r) S {
	let a = a decay40dB;
	let d = d decay40dB;
	let r = r decay40dB;
	let stage = delayVar();
	let y = delayVar();

	-- stages: 0: gate off (released + waiting for attack), 1: attack, 2: decay + sustain

	let y1 = y(1);
	stage <- select(stage(1), [gate > 0, (y1 > 0.99)+1, (gate > 0)*2]);
	let goal = select(stage(0), [0 asSignal, 1 asSignal, s asSignal]);
	let coef = select(stage(0), [r asSignal, a asSignal, d asSignal]);
	
    y <- goal + coef * (y1 - goal)
}

-- simple filters

-- lag
fn _lag(x S, a AsSignal) {
	let y = delayVar();
    y <- x + a * (y(1) - x)
}

fn lag(x S, t AsSignal) S {
    let a = decay40dB(t * fs());
	x _lag(a)
}
fn lag2(x S, t AsSignal) S {
    let a = decay40dB(t/2 * fs());
	x _lag(a) _lag(a)
}
fn lag3(x S, t AsSignal) S {
    let a = decay40dB(t/3 * fs());
	x _lag(a) _lag(a)
}

-- lag with different up and down time constants
fn _lag(x S, u AsSignal, d AsSignal) S {
	let y = delayVar();
	let a = select2(x < y(1), u, d);
    y <- x + a * (y(1) - x)
}

fn lag(x S, u AsSignal, d AsSignal) S {
	let u = u decay40dB;
	let d = d decay40dB;
	x _lag(u, d)
}
fn lag2(x S, u AsSignal, d AsSignal) S {
	let u = decay40dB(u/2);
	let d = decay40dB(d/2);
	x _lag(u, d) _lag(u, d)
}
fn lag3(x S, u AsSignal, d AsSignal) S {
	let u = decay40dB(u/3);
	let d = decay40dB(d/3);
	x _lag(u, d) _lag(u, d) _lag(u, d)
}

 
-- leaky integrator
fn leaky(x S, a AsSignal) S {
	let y = delayVar();
    y <- x + a * y(1)
}

fn onepole(x S, a AsSignal) S {
	let y = delayVar();
	y <- x + a * (y(1) - x)
}

fn onezero(x S, a AsSignal) S = x + a * (z1(x) - x);

fn leakdc(x S, k AsSignal) S {
	let y = delayVar();
	y <- x - x z1 + k * y(1)
}

-- exponential decay
fn decay(x S, t AsSignal) S = leaky(x, decay40dB(t * fs()));

fn decay2(x S, atk AsSignal, dcy AsSignal) S = x decay(dcy) - x decay(atk);

-- differentiation
fn diff(x S) S = x - x z1;   -- unscaled sample-to-sample difference
fn slope(x S) S = diff(x) * fs();
fn accel(x S) S = slope(slope(x));
fn jerk(x S) S = slope(accel(x));

-- integration
fn unscaledIntegrator(x S)    S { let y = delayVar(); y <- y(1) + x }
fn trapezoidalIntegrator(x S) S { let y = delayVar(); y <- y(1) + (x + x z1) * (T()/2) }
fn forwardIntegrator(x S)     S { let y = delayVar(); y <- y(1) + x z1 * T() }
fn backwardIntegrator(x S)    S { let y = delayVar(); y <- y(1) + x*T() }

-- integration with reset
fn unscaledIntegrator(x S, r S)    S { 
	let y = delayVar();
	y <- select2(r > 0, 0, y(1) + x) 
}
fn trapezoidalIntegrator(x S, r S) S { 
	let y = delayVar();
	y <- select2(r > 0, 0, y(1) + (x + x z1) * (T()/2)) 
}
fn forwardIntegrator(x S, r S) S { 
	let y = delayVar();
	y <- select2(r > 0, 0, y(1) + x z1 * T())
}
fn backwardIntegrator(x S, r S) S { 
	let y = delayVar();
	y <- select2(r > 0, 0, y(1) + x * T())
}

-- min and max followers
fn minfollow(x S, r S) S { 
	let y = delayVar();
	y <- select2(r > 0, x, min(x, y(1))) 
}

fn maxfollow(x S, r S) S { 
	let y = delayVar();
	y <- select2(r > 0, x, max(x, y(1)))
}


-- cubic panning function approximation. sqrt(sq(f(x)) + sq(f(1-x))) ~= 1 with max abs error less than +/- 0.0006 dB
fn panfun(x AsSignal) {
    let a = 0.337403011047526069;
    let b = -1.334338069017510398;
    let c = -a-b-1;
    1 + x * (c + x * (b + x * a))
}

fn panfuns(x AsSignal) = [x, 1 - x] panfun;

fn pan(x S, pos S) [S] = x * pos uni panfuns;
fn pan(x S, pos AsConstantSignal) [S] = x * pos uni panfuns;

-- signal movement
fn rising(x S)   S = x > x z1;
fn falling(x S)  S = x < x z1;
fn changing(x S) S = x != x z1;
fn nochange(x S) S = x == x z1;
fn localmax(x S) S = (x < x z1) * (x z1 > x z2);
fn localmin(x S) S = (x > x z1) * (x z1 < x z2);

fn fadein(x, fadeinTime) S {
    let dt = 1 / (fadeinTime * fs());
    let y = delayVar();
    min(1, dt + y(1)) f64 write(y) f32 cb * x
}

fn pinkingFilter(x S) S {
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


fn pinkingFilterEco(x S) S
{
    let b0 = delayVar(); (0.99765 * b0(1) + x * 0.0990460) write(b0);
    let b1 = delayVar(); (0.96300 * b1(1) + x * 0.2965164) write(b1);
    let b2 = delayVar(); (0.57000 * b2(1) + x * 1.0526913) write(b2);
    b0() + b1() + b2() + x * 0.1848
}

/*
fn pinkingFilterMat(x S) S {
    -- from Paul Kellett
    let bCoeffs = [0.0555179, 0.0750759, 0.1538520, 0.3104856, 0.5329522, -0.0168980];
    let aCoeffs = [0.99886,   0.99332,   0.96900,   0.86650,   0.55000,   -0.7616   ];
	let d1 = delayVar();
	let d2 = delayVar();

	d2 <- x * 0.115926;
    0.5362 * x + d1 write(aCoeffs * d1(1) + bCoeffs * x) sum + d2(1)
}

fn pinkingFilterEcoMat(x S) S
{
    let bCoeffs = [0.0990460, 0.2965164, 1.0526913];
    let aCoeffs = [0.99765, 0.96300, 0.57000];
	let d = delayVar();
    0.1848 * x + d write(aCoeffs * d(1) + bCoeffs * x) sum;
}

fn pinkmf(chans Int = 1) S = 0.25 * white(chans) pinkingFilterMat;
fn pinkmfe(chans Int = 1) S = 0.25 * white(chans) pinkingFilterEcoMat;
*/


fn white(chans Int = 1) S = birand(chans);

fn pinkf(chans Int = 1) S = 0.25 * white(chans) pinkingFilter;
fn pinkfe(chans Int = 1) S = 0.25 * white(chans) pinkingFilterEco;


fn violet(chans Int = 1) S = 0.5 * white(chans) diff;

fn blue(chans Int = 1) S = 2.0 * pinkf(chans) diff;

fn red(chans Int = 1, a=0.05) S {
	let y = delayVar();
	y <- (y(1) + chans birand * a) bfold_cheaper
}

fn gray(chans Int = 1) S {
	let r = delayVar() init(1, rand64(chans, Rate.init));
	(r <- r(1) ^ (1 << (rand64(chans) & 63))) f64 * 1.084202172485504434e-19 |> f32
}

-- 1 with probability `prob`, else zero, each sample.
fn coin(prob AsSignal, chans Int = 1) S = urand(chans) < prob;

-- velvet noise = `density` 1's each second (on average), otherwise zero.
fn velvet(density AsSignal, chans Int = 1) S = coin(density * T(), chans);

-- dust = `density` values of random unipolar values each second (on average), otherwise zero.
fn dust(density AsSignal, chans Int = 1) S = urand(chans) * velvet(density, chans);

-- dust2 = `density` values of random bipolar values each second (on average), otherwise zero.
fn dust2(density AsSignal, chans Int = 1) S = birand(chans) * velvet(density, chans);

-- randomly panned, randomly timed, ramdom amplitude impulses
fn pandust(density AsSignal, chans Int = 1) S = urand(chans) panfuns join * velvet(density, chans);

fn dustep(freq AsSignal, chans Int = 1) S = white(chans) sampleAndHold(velvet(freq, chans));

fn exprand(a AsSignal, b AsSignal, chans Int = 1, rate = Rate.audio) S = urand(chans, rate) uniexp(a, b);


-- unipolar waveshapers

fn sawshift(x, shift) = frac(x + shift); -- phase shift a unipolar sawtooth.
fn quadrature(x) = frac(x + 0.25) ;  -- shift a phasor by one quarter cycle

-- triangle waves
    -- Unipolar ramp to bipolar triangle wave.
fn btri(x) = abs(4 * x - 2) - 1;
fn btri0(x) = 1 - abs(4 * x quadrature - 2);   --   0 degrees initial phase
fn btri1(x) = abs(4 * x - 2) - 1;              --  90 degrees initial phase
fn btri2(x) = abs(4 * x quadrature - 2) - 1;   -- 180 degrees initial phase
fn btri3(x) = 1 - abs(4 * x - 2);              -- 270 degrees initial phase

fn utri0(x) = 1 - abs(1 - 2 * x quadrature);   --   0 degrees. unipolar output. '\,  trisin
fn utri1(x) = abs(1 - 2 * x);                  --  90 degrees. unipolar output. \/   tricos
fn utri2(x) = abs(1 - 2 * x quadrature);       -- 180 degrees. unipolar output. ,/' -trisin
fn utri3(x) = 1 - abs(1 - 2 * x);              -- 270 degrees. unipolar output. /\  -tricos

-- trapezoid waves
fn trapez0(x) = bclip(2 - abs(4 - 8 * x quadrature));    --   0 degrees. bipolar output.
fn trapez1(x) = bclip(abs(4 - 8 * x) - 2);               --  90 degrees. bipolar output.
fn trapez2(x) = bclip(abs(4 - 8 * x quadrature) - 2);    -- 180 degrees. bipolar output.
fn trapez3(x) = bclip(2 - abs(4 - 8 * x));               -- 270 degrees. bipolar output.

-- pulse waves
fn upulse(x, pwm) = x < pwm;              -- unipolar pulse wave
fn bpulse(x, pwm) = upulse(x,pwm) bi;     -- bipolar pulse wave
fn zpulse(x, pwm) = frac(x - pwm) - x;    -- zero DC pulse wave

-- pulse waves. As in SuperCollider, these always output at least one value of the opposite polarity each cycle.
fn upulse1(x, pwm) = x eoc select2(pwm < 0.5, x < pwm);       -- unipolar
fn bpulse1(x, pwm) = x upulse1(pwm) bi;                       -- bipolar
fn zpulse1(x, pwm) = x eoc select2(pwm < 0.5, x zpulse(pwm)); -- zero DC

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
fn bsquare(x) = (x < 0.5) bi;

-- window functions
fn han(x)     = x sinpi sq; -- Hanning window
fn ham(x)     = 0.54 - 0.46 * x cos2pi; -- Hamming window
fn sinwin(x)  = x sinpi; -- aka cosine window
fn sincwin(x) = x bi sincpi;
fn triwin(x)  = x utri3; 
fn welwin(x)  = 1 - x bi sq ; -- Welch window
fn quadwin(x) = 1 - x bi sq sq ;
fn octwin(x)  = 1 - x bi sq sq sq ;
fn trapezwin(x) = min(1, 2 * x triwin);

-- phasor generates a unipolar sawtooth. 
-- It is the core of many oscillators.
fn phasor(fm AsSignal, pm S) S {
	let phase = delayVar();
	phase <- frac(phase(1) + fm f64 * T() f64);
	frac(phase(1) f32 + pm)
}
fn phasor(fm AsSignal, pm AsConstantSignal) S {
	let phase = delayVar() init(1, pm);
	phase <- frac(phase(1) + fm f64 * T() f64);
	phase(1) f32
}
fn phasor(fm AsSignal) S {
	let phase = delayVar();
	phase <- frac(phase(1) + fm f64 * T() f64);
	phase(1) f32
}

-- low frequency oscillators

-- sawtooth
fn lfsaw(fm AsSignal, pm AsSignal = 0) S = phasor(fm, pm) bi;

-- impulse
fn lfimp(fm AsSignal, pm AsConstantSignal = 0.999999) S{
	let phase = delayVar() init(1, pm);
	let phase0 = frac(phase(1) + fm f64 * T() f64) write(phase);
	abs((phase0 - phase(1)) f32) > 0.5
}

-- triangle
fn lftri(fm AsSignal, pm AsSignal = 0) S = phasor(fm, pm) btri;

-- unipolar parabola
fn lfupar(fm AsSignal, pm AsSignal = 0) S = phasor(fm, pm) par;

-- trapezoid
fn lftrap(fm AsSignal, pm AsSignal = 0) S = phasor(fm, pm) trapez0;

-- unipolar square
fn lfusqr(fm AsSignal, pm AsSignal = 0) S = phasor(fm, pm) usquare;

-- bipolar square
fn lfsqr(fm AsSignal, pm AsSignal = 0) S = phasor(fm, pm) bsquare;

-- zig zag wave inward towards zero
fn lfzig(fm AsSignal, pm AsSignal = 0) S = phasor(fm, pm) izigzag;

-- zig zag wave away from zero
fn lfzag(fm AsSignal, pm AsSignal = 0) S = phasor(fm, pm) ozigzag;

-- bipolar parabola
fn lfpar(fm AsSignal, pm AsSignal = 0) S = phasor(fm, pm) par bi;

-- variable sawtooth wave
fn lfvsaw(fm AsSignal, pwm AsSignal, pm AsSignal = 0) S = phasor(fm, pm) varsaw(pwm);

-- unipolar pulse wave
fn lfupulse(fm AsSignal, pwm AsSignal, pm AsSignal = 0) S = phasor(fm, pm) upulse1(pwm);

-- bipolar pulse wave
fn lfbpulse(fm AsSignal, pwm AsSignal, pm AsSignal = 0) S = phasor(fm, pm) bpulse1(pwm);

-- zero DC pulse wave
fn lfzpulse(fm AsSignal, pwm AsSignal, pm AsSignal = 0) S = phasor(fm, pm) zpulse(pwm);

-- sine oscillator
fn sinosc(fm AsSignal, pm AsSignal = 0) S = phasor(fm, pm) sin2pi;

-- fast sine approximation oscillator
fn fsinosc(fm AsSignal, pm AsSignal = 0) S = phasor(fm, pm) fsin;

-- fast sine approximation oscillator with extra precision
fn fsinxosc(fm AsSignal, pm AsSignal = 0) S = phasor(fm, pm) fsinx;

-- band limited impulse oscillator
fn blip(fm AsSignal, pm AsSignal, numHarmonics AsSignal) S {
	let phase = delayVar();
	let p = phase(1);
	phase <- frac(p + fm f64 * T() f64);
	let pp = frac(p f32 + pm);
	
	let nyq = fs() * 0.5;
	let maxN = floor(nyq / fm abs max(16.0));
	let n = numHarmonics clip(1, maxN);
	let na = n floor;
	let nb = na + 1;
	
	-- shaping the ramp with smoothStep mitigates a broadband click which would otherwise 
	-- happen with simple linear interpolation.
	let nfrac = smoothStep(n - na); 
	
	let naScale = 0.5 / na;
	let nbScale = 0.5 / nb;
	let na2 = 2 * na + 1;
	let nb2 = na2 + 2;
	let d = pp sin2pi;
	
	let a = select2(d == 0, 1, naScale * (sin2pi(na2 * pp) / d - 1));
	let b = select2(d == 0, 1, nbScale * (sin2pi(nb2 * pp) / d - 1));
    a + nfrac * (b - a)
}



-- variable sharpness sawtooth oscillator
fn smoothSaw(fm AsSignal, sharpness AsSignal) S {
	let p = phasor(fm) - 0.5 |> frac bi;
	let w = 1 - p abs pow(sharpness exp2);
	p * w
}

-- variable sharpness sawtooth oscillator with phase modulation
fn smoothSaw(fm AsSignal, pm AsSignal, sharpness AsSignal) S {
	let p = phasor(fm, pm) - 0.5 |> frac bi;
	let w = 1 - p abs pow(sharpness exp2);
	p * w
}

-- variable sharpness square wave oscillator
fn smoothSquare(fm AsSignal, sharpness AsSignal) S {
	let phs = phasor(fm);
	let p = phs - 0.5 |> frac bi;
	let q = 2 * phs |> frac bi;
	let c = (p < 0) bi;
	let w = 1 - q abs pow(sharpness exp2);
	c * w
}

-- variable sharpness square wave oscillator with phase modulation
fn smoothSquare(fm AsSignal, pm AsSignal, sharpness AsSignal) S {
	let phs = phasor(fm, pm);
	let p = phs - 0.5 |> frac bi;
	let q = 2 * phs |> frac bi;
	let c = (p < 0) bi;
	let w = 1 - q abs pow(sharpness exp2);
	c * w
}

-- sawtooth windowed sine
fn sawWinSin(fm AsSignal, freqScale AsSignal) S {
	let p = phasor(fm);
	let q = frac(p * freqScale);
	q sin2pi * (1 - p)
}

-- sawtooth windowed unipolar sine
fn sawWinUsin(fm AsSignal, freqScale AsSignal) S {
	let p = phasor(fm);
	let q = frac(p * freqScale);
	q sin2pi sq * (1 - p)
}

-- unipolar sine windowed sine
fn usinWinSin(fm AsSignal, freqScale AsSignal) S {
	let p = phasor(fm);
	let q = frac(p * freqScale);
	q sin2pi * usin2pi(1 - p)
}

-- unipolar sine windowed unipolar sine
fn usinWinUsin(fm AsSignal, freqScale AsSignal) S {
	let p = phasor(fm);
	let q = frac(p * freqScale);
	q usin2pi * usin2pi(1 - p)
}



-- comb delays

-- interpolated comb delay
fn comb(x S, delayTime AsSignal, maxDelayTime AsSignal, decayTime AsSignal, interp Interpolation = Interpolation.lagrange) S {
	let a = decay60dB(decayTime / delayTime);
	let delaySamples = delayTime * fs();
	let maxDelaySamples = maxDelayTime * fs();
	let y = delayVar(maxDelaySamples);
	y <- x + a * y(delaySamples, interp)
}

-- no interpolation comb delay
fn combn(x S, delayTime AsSignal, decayTime AsSignal) S {
	x comb(delayTime, delayTime, decayTime, Interpolation.none)
}

-- linear interpolation comb delay
fn combl(x S, delayTime AsSignal, maxDelayTime AsSignal, decayTime AsSignal) S {
	x comb(delayTime, maxDelayTime, decayTime, Interpolation.linear)
}

-- cubic no interpolation comb delay
fn combc(x S, delayTime AsSignal, maxDelayTime AsSignal, decayTime AsSignal) S {
	x comb(delayTime, maxDelayTime, decayTime, Interpolation.cubic)
}


-- interpolated all pass delay
fn alpas(x S, delayTime AsSignal, maxDelayTime AsSignal, decayTime AsSignal, interp Interpolation = Interpolation.lagrange) S {
	let a = decay60dB(decayTime / delayTime);
	let delaySamples = delayTime * fs();
	let maxDelaySamples = maxDelayTime * fs();
	let y = delayVar(maxDelaySamples);
	let dread = y(delaySamples, interp);
	let dwrite = dread * a + x;
	y <- dwrite;
	dread - a * dwrite
}

-- no interpolation all pass delay
fn alpasn(x S, delayTime AsSignal, decayTime AsSignal) S {
	x alpas(delayTime, delayTime, decayTime, Interpolation.none)
}

-- linear interpolation all pass delay
fn alpasl(x S, delayTime AsSignal, maxDelayTime AsSignal, decayTime AsSignal) S {
	x alpas(delayTime, maxDelayTime, decayTime, Interpolation.linear)
}

-- cubic no interpolation all pass delay
fn alpasc(x S, delayTime AsSignal, maxDelayTime AsSignal, decayTime AsSignal) S {
	x alpas(delayTime, maxDelayTime, decayTime, Interpolation.cubic)
}

-- random reverb made of chained all pass delays
fn apverb(x S, delayTime Float, decayTime Float, n Int = 6) S {
	let f = fn(x S) S {
		let dly = rand(0.002, delayTime);
		x alpasn(dly, decayTime)
	};
	x chain(n, f)
}



fn pull(gate S, initVal AsConstantSignal, gatedFun fn()S) S {
	let d = delayVar();
	d init(0, initVal);
	if_(gate > 0, fn(){ gatedFun() write(d) }, fn(){ d(1) })
}


fn pause(gate S, gatedFun fn()S) S = if_(gate > 0, fn(){ gate * gatedFun() });













