-- pat.x -- lazy-list pattern streams, inspired by SuperCollider's Patterns
-- library and by Sound as Pure Form (SAPF).
--
-- Patterns ARE values: ordinary lazy List<T> streams, usually infinite.
-- Everything composes with the existing list builtins -- cyc, ncyc, hang,
-- take, drop, stutter, lace via join, zip, scan, urands/rands/irands/
-- xrands/picks -- this module adds the missing musical vocabulary and
-- `bind`, which zips per-key streams into a List<Event> (the Pbind analog).
--
--     import music.pat.*;
--     randSeed(42);
--     let es = bind([0, 1, 2, 4] cyc degree,
--                   List(0.25) cyc,
--                   amp: rands(0.2, 0.6));
--
-- Streams end at the shortest key (any finite key stream bounds the whole
-- bind, like Pfin); use `takeDur`/`take` to cut infinite results.
--
-- RT-safe: pure combinators over the per-VM RNG, no IO.

export music.core.*;

---------------------------------------------------------------------------
-- Deterministic series

-- Infinite arithmetic series: a, a+d, a+2d, ...
fn series(a Float, d Float) List<Float> = iter(a, fn(x Float) Float { x + d });

-- Infinite geometric series: a, a*r, a*r^2, ...
fn geom(a Float, r Float) List<Float> = iter(a, fn(x Float) Float { x * r });

-- A one-element stream (for embedding scalars between patterns).
fn once<T>(x T) List<T> = x :: nil;

-- Embed sub-streams in sequence: one level of lazy flattening.
fn embed<T>(ls List<List<T>>) List<T> = ls join;

---------------------------------------------------------------------------
-- Random streams (all infinite; seed with randSeed for reproducibility)

coro fn _brownsCo(lo Float, hi Float, step Float) Float {
    var x = (lo + hi) * 0.5;
    while (true) {
        yield x;
        var nx = x + brand() * step;
        -- reflect back into [lo, hi]
        while (nx > hi || nx < lo) {
            if (nx > hi) { nx = hi + hi - nx; }
            if (nx < lo) { nx = lo + lo - nx; }
        }
        x = nx;
    }
}

-- Brownian walk in [lo, hi], reflecting at the bounds (Pbrown).
fn browns(lo Float, hi Float, step Float) List<Float> = _brownsCo(lo, hi, step) toList;

coro fn _gaussesCo(mu Float, sigma Float) Float {
    while (true) { yield gauss(mu, sigma); }
}

-- Normally distributed stream (Pgauss).
fn gausses(mu Float, sigma Float) List<Float> = _gaussesCo(mu, sigma) toList;

coro fn _coinsCo(p Float) Bool {
    while (true) { yield coin(p); }
}

-- Weighted Bool stream.
fn coins(p Float) List<Bool> = _coinsCo(p) toList;

coro fn _wpicksCo<T>(xs [T], ws [Float]) T {
    while (true) { yield wpick(xs, ws); }
}

-- Weighted random choice stream (Pwrand).
fn wpicks<T>(xs [T], ws [Float]) List<T> = _wpicksCo(xs, ws) toList;

coro fn _xpicksCo<T>(xs [T]) T {
    var last = -1;
    while (true) {
        var i = irand(0, xs length - 1);
        while (xs length > 1 && i == last) { i = irand(0, xs length - 1); }
        last = i;
        yield xs[i];
    }
}

-- Random choice stream that never repeats an element twice in a row (Pxrand).
fn xpicks<T>(xs [T]) List<T> = _xpicksCo(xs) toList;

coro fn _shufsCo<T>(xs [T]) T {
    while (true) {
        let sh = xs muss;
        for (x : sh) { yield x; }
    }
}

-- Concatenated fresh shuffles: each pass through xs is a new ordering (Pshuf
-- per repeat).
fn shufs<T>(xs [T]) List<T> = _shufsCo(xs) toList;

---------------------------------------------------------------------------
-- Structural combinators

coro fn _laceCo<T>(ls [List<T>]) T {
    var cur = ls copy;
    var alive = true;
    while (alive) {
        alive = false;
        var i = 0;
        while (i < cur length) {
            if (cur[i] notNil) {
                alive = true;
                yield cur[i] head;
                cur[i] = cur[i] tail;
            }
            i = i + 1;
        }
    }
}

-- Round-robin interleave: one element from each live stream per pass,
-- skipping exhausted streams (Ppatlace). Tolerant of unequal and infinite
-- lengths.
fn lace<T>(ls [List<T>]) List<T> = _laceCo(ls) toList;

coro fn _stutterNsCo<T>(ns List<Int>, xs List<T>) T {
    var n = ns;
    var x = xs;
    while (n notNil && x notNil) {
        var k = n head;
        while (k > 0) {
            yield x head;
            k = k - 1;
        }
        n = n tail;
        x = x tail;
    }
}

-- Stutter with a per-element repeat-count stream (Pdup): element i of xs is
-- repeated ns[i] times (0 drops it). Stream first, like the scalar-count
-- stutter builtin: `xs stutter(ns)`.
fn stutter<T>(xs List<T>, ns List<Int>) List<T> = _stutterNsCo(ns, xs) toList;

---------------------------------------------------------------------------
-- Pitch lifting. Scalar constructors; auto-mapping lifts them over lists:
--     [0, 2, 4] cyc degree           -- List<Pitch> of scale degrees
-- (`degree` itself lives in music.event, re-exported through the core.)

fn step(x Float) Pitch = Pitch.step(x);
fn hz(x Float) Pitch = Pitch.hz(x);
fn ratio(x Float) Pitch = Pitch.ratio(x);
fn cents(x Float) Pitch = Pitch.cents(x);
fn rest() Pitch = Pitch.rest;

---------------------------------------------------------------------------
-- bind: zip per-key streams into an Event stream (the Pbind analog).

fn _allLive(ss [List<Float>]) Bool {
    for (s : ss) { if (s isNil) { return false; } }
    true
}

coro fn _bindCo(pitch List<Pitch>, dur List<Float>, amp List<Float>,
                legato List<Float>, names [Symbol], streams0 [List<Float>]) Event {
    var p = pitch;
    var d = dur;
    var a = amp;
    var l = legato;
    var streams = streams0 copy;
    var t = 0.0;
    while (p notNil && d notNil && a notNil && l notNil && streams _allLive) {
        let dv = d head;
        var ps = [(Symbol, Float)]();
        var i = 0;
        while (i < streams length) {
            ps push!((names[i], streams[i] head));
            streams[i] = streams[i] tail;
            i = i + 1;
        }
        yield Event {
            t: t, dur: dv, sustain: dv * (l head),
            amp: a head, pitch: p head, params: ps,
        };
        t = t + dv;
        p = p tail; d = d tail; a = a tail; l = l tail;
    }
}

-- Assemble an event stream by zipping one stream per event field. Onsets are
-- the running sum of `dur`; `sustain = dur * legato`. The stream ends at the
-- shortest input (any finite key acts like Pfin). Layer bind results into
-- polyphony with `merge`.
fn bind(pitch List<Pitch>, dur List<Float>,
        amp List<Float> = List(0.5) cyc,
        legato List<Float> = List(0.8) cyc,
        extras [(Symbol, List<Float>)] = [(Symbol, List<Float>)]()) List<Event> {
    var names = [Symbol]();
    var streams = [List<Float>]();
    for (e : extras) { names push!(e.0); streams push!(e.1); }
    _bindCo(pitch, dur, amp, legato, names, streams) toList
}
