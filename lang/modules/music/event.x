-- event.x -- the shared note-event representation for the music libraries.
--
-- Every music dialect (music.pat streams, music.media, music.shape/job,
-- music.spans) produces or consumes List<Event>. An Event is symbolic about
-- pitch: it carries a Pitch specification that is only resolved to Hz at
-- play time through a Tuning and Scale (see music.tuning), so the same event
-- stream can be performed in 12ED2, Bohlen-Pierce, a Scala table, or any
-- real-numbered division of any period.
--
-- Time is Float beats throughout. `t` is the ABSOLUTE onset in beats, which
-- makes parallel combination a lazy ordered merge and keeps all dialects on
-- one clock. `dur` and `sustain` are distinct on purpose:
--   dur     -- the note VALUE: beats this event occupies in its melody
--              (drives retrograde, stretch, and onset spacing)
--   sustain -- the SOUNDING time: beats until the note is released
--              (drives noteOff scheduling; staccato/legato live here)
--
-- RT-safe: pure data + combinators, no IO.

-- A symbolic pitch, resolved by music.tuning pitchHz at play time.
enum Pitch {
    hz Float,       -- absolute frequency, bypasses the tuning
    step Float,     -- real-valued step in the Tuning (12ED2 step == MIDI nn)
    degree Float,   -- real-valued scale degree, resolved via Scale + Tuning
    ratio Float,    -- frequency ratio above the tuning root
    cents Float,    -- cents above the tuning root
    rest,           -- no note; time still advances
}

struct Event {
    t Float,                    -- onset, absolute beats
    dur Float,                  -- note value in beats (see header)
    sustain Float,              -- sounding time in beats (see header)
    amp Float,
    pitch Pitch,
    params [(Symbol, Float)],   -- extra per-note synth controls by name
}

-- Convenience constructor with the common defaults.
fn event(t Float, dur Float, pitch Pitch, amp Float = 0.5,
         sustain Float = 0.0, params [(Symbol, Float)] = [(Symbol, Float)]()) Event {
    Event {
        t: t, dur: dur,
        sustain: sustain > 0.0 ? sustain : dur * 0.8,
        amp: amp, pitch: pitch, params: params,
    }
}

fn isRest(e Event) Bool {
    match (e.pitch) {
        Pitch.rest: true;
        _: false;
    }
}

---------------------------------------------------------------------------
-- Transformations. All lazy-safe: they map over the list without forcing it.

-- Shift onsets by dt beats.
fn offset(es List<Event>, dt Float) List<Event> =
    es map(fn(e Event) Event { Event { ...e, t: e.t + dt } });

-- Scale time by k (onsets, note values, and sustains).
fn stretch(es List<Event>, k Float) List<Event> =
    es map(fn(e Event) Event {
        Event { ...e, t: e.t * k, dur: e.dur * k, sustain: e.sustain * k }
    });

-- Scale amplitude by k.
fn gain(es List<Event>, k Float) List<Event> =
    es map(fn(e Event) Event { Event { ...e, amp: e.amp * k } });

-- Keep events whose onset falls before `beats`. Safe cut for infinite
-- streams (this is the sanctioned way to bound a stream before rendering).
fn takeDur(es List<Event>, beats Float) List<Event> =
    es takeWhile(fn(e Event) Bool { e.t < beats });

coro fn _mergeCo(a0 List<Event>, b0 List<Event>) Event {
    var a = a0;
    var b = b0;
    while (a notNil && b notNil) {
        if ((a head).t <= (b head).t) {
            yield a head;
            a = a tail;
        } else {
            yield b head;
            b = b tail;
        }
    }
    while (a notNil) { yield a head; a = a tail; }
    while (b notNil) { yield b head; b = b tail; }
}

-- Merge two onset-ordered event streams into one onset-ordered stream.
-- Lazy: works on infinite streams (elements are pulled on demand).
-- This is parallel combination for every dialect.
fn merge(a List<Event>, b List<Event>) List<Event> = _mergeCo(a, b) toList;

---------------------------------------------------------------------------
-- Stable formatting for golden tests and inspection.

fn toString(p Pitch) String {
    match (p) {
        Pitch.hz(f):     "hz(%^)" fmt(f);
        Pitch.step(s):   "step(%^)" fmt(s);
        Pitch.degree(d): "degree(%^)" fmt(d);
        Pitch.ratio(r):  "ratio(%^)" fmt(r);
        Pitch.cents(c):  "cents(%^)" fmt(c);
        Pitch.rest:      "rest";
    }
}

fn showEvent(e Event) String {
    let base = "(t: %^, dur: %^, sus: %^, amp: %^, %^)"
        fmt(e.t, e.dur, e.sustain, e.amp, e.pitch toString);
    if (e.params length == 0) {
        base
    } else {
        var extra = "";
        for (p : e.params) {
            extra = extra $ " %^: %^" fmt(p.0, p.1);
        }
        base $ " +[" $ extra $ " ]"
    }
}

-- Print each event of a finite stream on its own line.
fn showEvents(es List<Event>) Void {
    for (e : es) { e showEvent println; }
}
