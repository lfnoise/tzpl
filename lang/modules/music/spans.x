-- spans.x -- patterns as functions of time, inspired by Tidal Cycles.
--
-- A Pattern<T> is a callable value: query it with a time Span (in CYCLES,
-- half-open [a, b)) and it returns the Haps -- value fragments -- active in
-- that window. `whole` is the hap's full extent (none for continuous
-- signals); `part` is the fragment inside the query. Everything else is
-- combinators over queries, so structure is computed, never stored:
--
--     let p = stack([
--         pure(degree(0)) euclid(3, 8),
--         fastcat([pure(degree(4)), pure(degree(7))]) fast(2.0) rev,
--     ]);
--     patEvents(p events, 4.0) play(freqVoice(101));
--
-- Time is Float cycles. Policy for the floating point edge: spans are
-- half-open; fragments shorter than kEps are dropped; cycle iteration
-- starts at floor(a + kEps); onset tests compare within kEps.
--
-- Combination uses named functions (the operator set is fixed; there is no
-- `#`): zipl samples the right pattern at each left hap's onset, and
-- withAmp/withLegato/withParam specialize it for Pattern<Event>.
--
-- RT-safe: pure.

export music.core.*;

const kEps = 0.000000001;

struct Span { a Float, b Float }        -- half-open [a, b), in cycles

struct Hap<T> {
    whole Option<Span>,  -- full extent of the value (none = continuous signal)
    part Span,           -- the fragment inside the query span
    value T,
}

struct Pattern<T> {
    query (Span) [Hap<T>],
}

fn span(a Float, b Float) Span = Span { a: a, b: b };

-- Patterns are callable: p(span) or p call(span).
fn call<T>(p Pattern<T>, s Span) [Hap<T>] = p.query(s);

-- Query the first n cycles.
fn cycles<T>(p Pattern<T>, n Float) [Hap<T>] = p call(span(0.0, n));

fn _sect(x Span, y Span) Span = span(max(x.a, y.a), min(x.b, y.b));

---------------------------------------------------------------------------
-- Sources

-- One occurrence of x per cycle.
fn pure<T>(x T) Pattern<T> {
    Pattern<T> {
        query: fn(s Span) [Hap<T>] {
            var out = [Hap<T>]();
            var c = (s.a + kEps) floor;
            while (c < s.b - kEps) {
                let p = _sect(s, span(c, c + 1.0));
                if (p.b - p.a > kEps) {
                    out push!(Hap<T> { whole: Option.some(span(c, c + 1.0)), part: p, value: x });
                }
                c = c + 1.0;
            }
            out
        },
    }
}

-- A continuous signal: value is f(midpoint of the query span), whole = none.
fn signal(f (Float) Float) Pattern<Float> {
    Pattern<Float> {
        query: fn(s Span) [Hap<Float>] {
            [Hap<Float> { whole: Option<Span>.none, part: s, value: f((s.a + s.b) * 0.5) }]
        },
    }
}

-- Continuous unipolar sine / saw over one cycle.
fn sinePat() Pattern<Float> =
    signal(fn(t Float) Float { (t * 6.283185307179586) sin * 0.5 + 0.5 });
fn sawPat() Pattern<Float> = signal(fn(t Float) Float { t - t floor });

-- A constant continuous value.
fn steady(x Float) Pattern<Float> = signal(fn(t Float) Float { x });

---------------------------------------------------------------------------
-- Time combinators

fn _mapTimes<T>(p Pattern<T>, qf (Float) Float, rf (Float) Float) Pattern<T> {
    Pattern<T> {
        query: fn(s Span) [Hap<T>] {
            var out = [Hap<T>]();
            for (h : p call(span(qf(s.a), qf(s.b)))) {
                let w = match (h.whole) {
                    Option.some(sp): Option.some(span(rf(sp.a), rf(sp.b)));
                    Option.none: Option<Span>.none;
                };
                out push!(Hap<T> { whole: w, part: span(rf(h.part.a), rf(h.part.b)), value: h.value });
            }
            out
        },
    }
}

-- Speed up by k (k cycles of p per output cycle).
fn fast<T>(p Pattern<T>, k Float) Pattern<T> =
    _mapTimes(p, fn(t Float) Float { t * k }, fn(t Float) Float { t / k });

-- Slow down by k.
fn slow<T>(p Pattern<T>, k Float) Pattern<T> = fast(p, 1.0 / k);

-- Shift earlier / later by off cycles (Tidal's <~ and ~>).
fn early<T>(p Pattern<T>, off Float) Pattern<T> =
    _mapTimes(p, fn(t Float) Float { t + off }, fn(t Float) Float { t - off });
fn late<T>(p Pattern<T>, off Float) Pattern<T> = early(p, 0.0 - off);

-- Reverse each cycle.
fn rev<T>(p Pattern<T>) Pattern<T> {
    Pattern<T> {
        query: fn(s Span) [Hap<T>] {
            var out = [Hap<T>]();
            var c = (s.a + kEps) floor;
            while (c < s.b - kEps) {
                let cs = _sect(s, span(c, c + 1.0));
                if (cs.b - cs.a > kEps) {
                    let m = c + c + 1.0;   -- reflection: t -> m - t within cycle c
                    for (h : p call(span(m - cs.b, m - cs.a))) {
                        let w = match (h.whole) {
                            Option.some(sp): Option.some(span(m - sp.b, m - sp.a));
                            Option.none: Option<Span>.none;
                        };
                        out push!(Hap<T> { whole: w, part: span(m - h.part.b, m - h.part.a), value: h.value });
                    }
                }
                c = c + 1.0;
            }
            out
        },
    }
}

-- One cycle from each pattern in rotation (each pattern's own time advances
-- once per rotation).
fn slowcat<T>(ps [Pattern<T>]) Pattern<T> {
    Pattern<T> {
        query: fn(s Span) [Hap<T>] {
            var out = [Hap<T>]();
            var c = (s.a + kEps) floor;
            while (c < s.b - kEps) {
                let cs = _sect(s, span(c, c + 1.0));
                if (cs.b - cs.a > kEps) {
                    let n = ps length;
                    var i = c toInt % n;
                    if (i < 0) { i = i + n; }
                    let d = ((c / n toFloat) + kEps) floor;   -- inner cycle number
                    let shift = c - d;
                    for (h : ps[i] call(span(cs.a - shift, cs.b - shift))) {
                        let w = match (h.whole) {
                            Option.some(sp): Option.some(span(sp.a + shift, sp.b + shift));
                            Option.none: Option<Span>.none;
                        };
                        out push!(Hap<T> { whole: w, part: span(h.part.a + shift, h.part.b + shift), value: h.value });
                    }
                }
                c = c + 1.0;
            }
            out
        },
    }
}

-- All patterns squeezed into one cycle, in order.
fn fastcat<T>(ps [Pattern<T>]) Pattern<T> = fast(slowcat(ps), ps length toFloat);

-- All patterns at once.
fn stack<T>(ps [Pattern<T>]) Pattern<T> {
    Pattern<T> {
        query: fn(s Span) [Hap<T>] {
            var out = [Hap<T>]();
            for (p : ps) {
                for (h : p call(s)) { out push!(h); }
            }
            out
        },
    }
}

-- Apply f to the pattern every n-th cycle (cycle numbers where c mod n == 0).
fn every<T>(p Pattern<T>, n Int, f (Pattern<T>) Pattern<T>) Pattern<T> {
    let fp = f(p);
    Pattern<T> {
        query: fn(s Span) [Hap<T>] {
            var out = [Hap<T>]();
            var c = (s.a + kEps) floor;
            while (c < s.b - kEps) {
                let cs = _sect(s, span(c, c + 1.0));
                if (cs.b - cs.a > kEps) {
                    var ci = c toInt % n;
                    if (ci < 0) { ci = ci + n; }
                    for (h : ci == 0 ? fp call(cs) : p call(cs)) { out push!(h); }
                }
                c = c + 1.0;
            }
            out
        },
    }
}

-- Rotate the pattern one n-th further left each cycle.
fn iterp<T>(p Pattern<T>, n Int) Pattern<T> {
    Pattern<T> {
        query: fn(s Span) [Hap<T>] {
            var out = [Hap<T>]();
            var c = (s.a + kEps) floor;
            while (c < s.b - kEps) {
                let cs = _sect(s, span(c, c + 1.0));
                if (cs.b - cs.a > kEps) {
                    var ci = c toInt % n;
                    if (ci < 0) { ci = ci + n; }
                    let off = ci toFloat / n toFloat;
                    for (h : early(p, off) call(cs)) { out push!(h); }
                }
                c = c + 1.0;
            }
            out
        },
    }
}

---------------------------------------------------------------------------
-- Structure

-- Euclidean onsets: k pulses over n slots (Bresenham form), rotated by rot.
fn euclidMask(k Int, n Int, rot Int) [Bool] {
    var out = [Bool]();
    var i = 0;
    while (i < n) {
        var j = (i + rot) % n;
        if (j < 0) { j = j + n; }
        out push!(j * k % n < k);
        i = i + 1;
    }
    out
}

-- Distribute p over the true slots of a Euclidean rhythm.
fn euclid<T>(p Pattern<T>, k Int, n Int, rot Int = 0) Pattern<T> {
    let mask = euclidMask(k, n, rot);
    let silence = Pattern<T> { query: fn(s Span) [Hap<T>] { [Hap<T>]() } };
    var slots = [Pattern<T>]();
    for (b : mask) { slots push!(b ? p : silence); }
    fastcat(slots)
}

-- Sample a (usually continuous) pattern n times per cycle, giving discrete
-- onsets.
fn segment<T>(p Pattern<T>, n Int) Pattern<T> {
    Pattern<T> {
        query: fn(s Span) [Hap<T>] {
            var out = [Hap<T>]();
            let w = 1.0 / n toFloat;
            var k = ((s.a + kEps) / w) floor;
            while (k * w < s.b - kEps) {
                let slot = span(k * w, (k + 1.0) * w);
                let part = _sect(s, slot);
                if (part.b - part.a > kEps) {
                    let vs = p call(slot);
                    if (vs length > 0) {
                        out push!(Hap<T> { whole: Option.some(slot), part: part, value: vs[0].value });
                    }
                }
                k = k + 1.0;
            }
            out
        },
    }
}

---------------------------------------------------------------------------
-- Values

fn pmap<T, U>(p Pattern<T>, f (T) U) Pattern<U> {
    Pattern<U> {
        query: fn(s Span) [Hap<U>] {
            var out = [Hap<U>]();
            for (h : p call(s)) {
                out push!(Hap<U> { whole: h.whole, part: h.part, value: f(h.value) });
            }
            out
        },
    }
}

-- Scale a Float pattern into [lo, hi] (for signals in 0..1).
fn range(p Pattern<Float>, lo Float, hi Float) Pattern<Float> =
    p pmap(fn(x Float) Float { lo + x * (hi - lo) });

-- Haps ordered by part start (queries do not guarantee an order).
fn sortHaps<T>(hs [Hap<T>]) [Hap<T>] {
    var out = [Hap<T>]();
    for (h : hs) {
        var i = out length;
        out push!(h);
        while (i > 0 && out[i - 1].part.a > h.part.a) {
            out[i] = out[i - 1];
            i = i - 1;
        }
        out[i] = h;
    }
    out
}

-- Keep only haps whose onset lies inside the queried part.
fn filterOnsets<T>(p Pattern<T>) Pattern<T> {
    Pattern<T> {
        query: fn(s Span) [Hap<T>] {
            var out = [Hap<T>]();
            for (h : p call(s)) {
                match (h.whole) {
                    Option.some(sp): {
                        if ((sp.a - h.part.a) abs < kEps) { out push!(h); }
                    }
                    Option.none: 0;
                }
            }
            out
        },
    }
}

-- Structure from the LEFT: each left hap keeps its extent, its value
-- combined with the right pattern sampled at the hap's onset (the common
-- use of Tidal's #).
fn zipl<A, B, C>(pa Pattern<A>, pb Pattern<B>, f (A, B) C) Pattern<C> {
    Pattern<C> {
        query: fn(s Span) [Hap<C>] {
            var out = [Hap<C>]();
            for (h : pa call(s)) {
                let vs = pb call(h.part);
                if (vs length > 0) {
                    out push!(Hap<C> { whole: h.whole, part: h.part, value: f(h.value, vs[0].value) });
                }
            }
            out
        },
    }
}

---------------------------------------------------------------------------
-- Event patterns and the bridge to the player

-- Lift a pitch pattern to events. In Pattern<Event> values, `t` and `dur`
-- are placeholders (the hap supplies timing) and `sustain` holds the LEGATO
-- FRACTION; patEvents turns them into absolute beats.
fn events(pp Pattern<Pitch>, amp Float = 0.5, legato Float = 0.8) Pattern<Event> =
    pp pmap(fn(p Pitch) Event { event(0.0, 1.0, p, amp, legato) });

fn withAmp(pe Pattern<Event>, pa Pattern<Float>) Pattern<Event> =
    zipl(pe, pa, fn(e Event, a Float) Event { Event { ...e, amp: a } });

fn withLegato(pe Pattern<Event>, pl Pattern<Float>) Pattern<Event> =
    zipl(pe, pl, fn(e Event, l Float) Event { Event { ...e, sustain: l } });

fn withParam(pe Pattern<Event>, name Symbol, pv Pattern<Float>) Pattern<Event> =
    zipl(pe, pv, fn(e Event, x Float) Event {
        Event { ...e, params: e.params $ [(name, x)] }
    });

-- Overlay: left structure, both events' params merged (right appended),
-- right's amp kept.
fn mix(a Pattern<Event>, b Pattern<Event>) Pattern<Event> =
    zipl(a, b, fn(ea Event, eb Event) Event {
        Event { ...ea, amp: eb.amp, params: ea.params $ eb.params }
    });

fn _sortEvents(es [Event]) [Event] {
    -- insertion sort by onset (stable; onset counts are modest)
    var out = [Event]();
    for (e : es) {
        var i = out length;
        out push!(e);
        while (i > 0 && out[i - 1].t > e.t) {
            out[i] = out[i - 1];
            i = i - 1;
        }
        out[i] = e;
    }
    out
}

-- Render `cycles` cycles of an event pattern to the shared event list.
-- One cycle spans beatsPerCycle beats; only onsets are kept (fragments of
-- events that started earlier are dropped).
fn patEvents(p Pattern<Event>, cycles Float, beatsPerCycle Float = 4.0) List<Event> {
    var out = [Event]();
    for (h : p filterOnsets call(span(0.0, cycles))) {
        match (h.whole) {
            Option.some(w): {
                let t = w.a * beatsPerCycle;
                let dur = (w.b - w.a) * beatsPerCycle;
                let legato = h.value.sustain > 0.0 ? h.value.sustain : 0.8;
                out push!(Event {
                    ...h.value,
                    t: t, dur: dur, sustain: dur * legato,
                });
            }
            Option.none: 0;
        }
    }
    out _sortEvents toList
}
