-- media.x -- polymorphic temporal media, inspired by Paul Hudak's Haskore
-- and the temporal-media algebra of the Haskell School of Music.
--
-- Music<T> is an algebra of notes, rests, sequential and parallel
-- composition, and control annotations, generic over the note payload T.
-- Composition operators (fixed token set -- no new infix tokens exist):
--
--     a $ b     sequential (Haskore's :+: inspired this)
--     a & b     parallel   (Haskore's :=: inspired this)
--
--     let tune = line([note(1.0, Pitch.degree(0.0)),
--                      note(1.0, Pitch.degree(2.0))])
--                & trans(-7.0, note(2.0, Pitch.degree(0.0)));
--     tune perform play(freqVoice(101));
--
-- `perform` interprets a Music into the shared List<Event>; the canned
-- overloads cover Music<Pitch> and Music<Note>, and the generic form takes
-- an event-builder so any payload type can be performed. Payloads are
-- UN-TIMED by design: time lives in the note(dur, T) case, and Event is the
-- OUTPUT of perform (Music<Event> would double-represent onset/duration).
--
-- RT-safe: pure.

export music.core.*;

enum Music<T> {
    note (Float, T),            -- duration (beats), payload
    rest Float,
    mseq (Music<T>, Music<T>),  -- a then b
    mpar (Music<T>, Music<T>),  -- a with b
    ctl (Control, Music<T>),    -- annotation applied to a subtree
}

enum Control {
    tempo Float,     -- divide durations (2.0 = twice as fast)
    trans Float,     -- transpose step/degree pitches (real-valued)
    dyn Float,       -- scale amplitude
    inst Int,        -- instrument tag, emitted as an 'inst event param
}

---------------------------------------------------------------------------
-- Construction

fn note<T>(d Float, x T) Music<T> = Music<T>.note((d, x));

-- A rest for concrete pitch payloads; for other payloads write
-- Music<T>.rest(d) directly (bare generic calls cannot infer T).
fn mrest(d Float) Music<Pitch> = Music<Pitch>.rest(d);

fn $<T>(a Music<T>, b Music<T>) Music<T> = Music<T>.mseq((a, b));
fn &<T>(a Music<T>, b Music<T>) Music<T> = Music<T>.mpar((a, b));

-- Sequential composition of a non-empty array.
fn line<T>(ms [Music<T>]) Music<T> {
    var acc = ms[0];
    var i = 1;
    while (i < ms length) { acc = acc $ ms[i]; i = i + 1; }
    acc
}

-- Parallel composition of a non-empty array.
fn chord<T>(ms [Music<T>]) Music<T> {
    var acc = ms[0];
    var i = 1;
    while (i < ms length) { acc = acc & ms[i]; i = i + 1; }
    acc
}

-- n repetitions in sequence (n >= 1).
fn times<T>(n Int, m Music<T>) Music<T> {
    var acc = m;
    var i = 1;
    while (i < n) { acc = acc $ m; i = i + 1; }
    acc
}

-- Control wrappers
fn tempo<T>(k Float, m Music<T>) Music<T> = Music<T>.ctl((Control.tempo(k), m));
fn trans<T>(k Float, m Music<T>) Music<T> = Music<T>.ctl((Control.trans(k), m));
fn dyn<T>(k Float, m Music<T>) Music<T> = Music<T>.ctl((Control.dyn(k), m));
fn inst<T>(i Int, m Music<T>) Music<T> = Music<T>.ctl((Control.inst(i), m));

---------------------------------------------------------------------------
-- Algebra

-- Duration in beats (tempo annotations included).
fn mdur<T>(m Music<T>) Float {
    match (m) {
        Music.note(p): p.0;
        Music.rest(d): d;
        Music.mseq(p): { let (a, b) = p; mdur(a) + mdur(b) }
        Music.mpar(p): { let (a, b) = p; max(mdur(a), mdur(b)) }
        Music.ctl(p): {
            let (c, inner) = p;
            match (c) {
                Control.tempo(k): mdur(inner) / k;
                _: mdur(inner);
            }
        }
    }
}

-- Retrograde: reverse in time. Parallel branches are padded with rests so
-- both still END together after reversal (following Haskore's definition).
fn retro<T>(m Music<T>) Music<T> {
    match (m) {
        Music.note(p): m;
        Music.rest(d): m;
        Music.mseq(p): { let (a, b) = p; retro(b) $ retro(a) }
        Music.mpar(p): {
            let (a, b) = p;
            let da = mdur(a);
            let db = mdur(b);
            if (da >= db) {
                retro(a) & (Music<T>.rest(da - db) $ retro(b))
            } else {
                (Music<T>.rest(db - da) $ retro(a)) & retro(b)
            }
        }
        Music.ctl(p): { let (c, inner) = p; Music<T>.ctl((c, retro(inner))) }
    }
}

-- Map over payloads, preserving structure.
fn mmap<T, U>(m Music<T>, f (T) U) Music<U> {
    match (m) {
        Music.note(p): Music<U>.note((p.0, f(p.1)));
        Music.rest(d): Music<U>.rest(d);
        Music.mseq(p): { let (a, b) = p; Music<U>.mseq((mmap(a, f), mmap(b, f))) }
        Music.mpar(p): { let (a, b) = p; Music<U>.mpar((mmap(a, f), mmap(b, f))) }
        Music.ctl(p): { let (c, inner) = p; Music<U>.ctl((c, mmap(inner, f))) }
    }
}

---------------------------------------------------------------------------
-- Performance: Music -> List<Event>

-- Interpretation context threaded through the tree.
struct Ctx {
    t Float,        -- absolute onset of this subtree, beats
    tempoK Float,   -- accumulated tempo factor (durations divide by this)
    transK Float,   -- accumulated transposition
    ampK Float,     -- accumulated dynamics factor
    inst Int,       -- innermost instrument tag (-1 = none)
}

fn initialCtx(t0 Float = 0.0) Ctx =
    Ctx { t: t0, tempoK: 1.0, transK: 0.0, ampK: 1.0, inst: -1 };

-- Transpose a symbolic pitch by k (steps for step pitches, degrees for
-- degree pitches; hz/ratio/cents/rest are unchanged).
fn transposed(p Pitch, k Float) Pitch {
    match (p) {
        Pitch.step(s): Pitch.step(s + k);
        Pitch.degree(d): Pitch.degree(d + k);
        _: p;
    }
}

-- Generic interpretation: mk(payload, onset, dur, ctx) builds each event.
fn perform<T>(m Music<T>, mk (T, Float, Float, Ctx) Event, ctx Ctx) List<Event> {
    match (m) {
        Music.note(p): List(mk(p.1, ctx.t, p.0 / ctx.tempoK, ctx));
        Music.rest(d): nil;
        Music.mseq(p): {
            let (a, b) = p;
            cat(perform(a, mk, ctx),
                perform(b, mk, Ctx { ...ctx, t: ctx.t + mdur(a) / ctx.tempoK }))
        }
        Music.mpar(p): {
            let (a, b) = p;
            merge(perform(a, mk, ctx), perform(b, mk, ctx))
        }
        Music.ctl(p): {
            let (c, inner) = p;
            match (c) {
                Control.tempo(k): perform(inner, mk, Ctx { ...ctx, tempoK: ctx.tempoK * k });
                Control.trans(k): perform(inner, mk, Ctx { ...ctx, transK: ctx.transK + k });
                Control.dyn(k):   perform(inner, mk, Ctx { ...ctx, ampK: ctx.ampK * k });
                Control.inst(i):  perform(inner, mk, Ctx { ...ctx, inst: i });
            }
        }
    }
}

fn _instParams(base [(Symbol, Float)], c Ctx) [(Symbol, Float)] {
    if (c.inst < 0) {
        base
    } else {
        base $ [('inst, c.inst toFloat)]
    }
}

-- Canned interpretation for bare pitches: amp 0.5, sustain = 0.9 * dur.
fn perform(m Music<Pitch>, t0 Float = 0.0, legato Float = 0.9) List<Event> =
    perform(m, fn(p Pitch, t Float, d Float, c Ctx) Event {
        event(t, d, transposed(p, c.transK), 0.5 * c.ampK, d * legato,
              _instParams([(Symbol, Float)](), c))
    }, initialCtx(t0));

-- A payload carrying amplitude and extra synth params alongside the pitch.
struct MNote {
    pitch Pitch,
    amp Float,
    params [(Symbol, Float)],
}

fn mnote(pitch Pitch, amp Float = 0.5,
         params [(Symbol, Float)] = [(Symbol, Float)]()) MNote =
    MNote { pitch: pitch, amp: amp, params: params };

fn perform(m Music<MNote>, t0 Float = 0.0, legato Float = 0.9) List<Event> =
    perform(m, fn(n MNote, t Float, d Float, c Ctx) Event {
        event(t, d, transposed(n.pitch, c.transK), n.amp * c.ampK, d * legato,
              _instParams(n.params, c))
    }, initialCtx(t0));
