-- tuning.x -- tunings (pitch systems) and scales (degree subsets).
--
-- A Tuning maps real-valued STEPS to frequencies. Two kinds:
--   ed(divisions, periodRatio) -- equal division of any period; divisions is
--       a REAL number, so 8.5-EDO or 13 divisions of the tritave (Bohlen-
--       Pierce) are first-class.
--   table(cents, periodRatio)  -- unequal step sizes: the cents of each step
--       within one repeating period (ascending, cents[0] == 0). This is what
--       `ed` cannot express: JI gamuts, well-temperaments, any Scala .scl
--       file. Fractional step positions interpolate linearly in cents.
--
-- A Scale is a different layer (the Scala .scl/.kbm distinction): it selects
-- a subset of a tuning's steps per scale DEGREE (major = 7 of 12), plus the
-- steps added per repetition (`wrap`). Scales work identically over both
-- tuning kinds. Degrees are integers; in-between pitches are accidentals
-- (Pitch.degree's alteration, in tuning steps), not fractional degrees.
--
-- Anchoring: rootHz is the frequency at step rootStep. Default constructors
-- anchor step 0 at middle C (261.6256 Hz); `et12` anchors 440 Hz at step 69
-- so steps coincide with MIDI note numbers. Re-anchor with root().
--
-- RT-safe: pure math, no IO.

import music.event.*;   -- Pitch, for pitchHz

const middleC = 261.6255653005986;   -- 440 * 2^(-9/12)

enum TuningKind {
    ed (Float, Float),      -- (divisions, periodRatio); divisions is REAL
    table ([Float], Float), -- (cents per step within period, ascending, [0]=0; periodRatio)
}

struct Tuning {
    kind TuningKind,
    rootHz Float,      -- frequency at rootStep
    rootStep Float,    -- step number pinned to rootHz
}

struct Scale {
    degrees [Float],   -- tuning steps per scale degree, ascending, [0]=0
    wrap Float,        -- steps added per scale repetition
}

---------------------------------------------------------------------------
-- Tuning constructors

-- n equal divisions of the octave (n is real).
fn edo(n Float) Tuning = ed(n, 2.0);

-- n equal divisions of an arbitrary period ratio (both real).
fn ed(n Float, period Float) Tuning =
    Tuning { kind: TuningKind.ed(n, period), rootHz: middleC, rootStep: 0.0 };

-- Bohlen-Pierce: 13 equal divisions of the tritave.
fn bp() Tuning = ed(13.0, 3.0);

-- 12ED2 anchored so that step == MIDI note number (69 -> 440 Hz).
const et12 = Tuning { kind: TuningKind.ed(12.0, 2.0), rootHz: 440.0, rootStep: 69.0 };

-- Unequal tuning from a cents table (one entry per step within the period,
-- ascending, cents[0] must be 0).
fn scala(cents [Float], period Float = 2.0) Tuning =
    Tuning { kind: TuningKind.table(cents, period), rootHz: middleC, rootStep: 0.0 };

-- Just-intonation gamut from frequency ratios (ascending, ratios[0] == 1).
fn ji(ratios [Float], period Float = 2.0) Tuning =
    scala(ratios map(fn(r Float) Float { r log2 * 1200.0 }), period);

-- Re-anchor: the returned tuning has frequency hz at step `step`.
fn root(t Tuning, hz Float, step Float) Tuning =
    Tuning { ...t, rootHz: hz, rootStep: step };

-- Chromatic transposition of the whole performance: re-anchor so degree 0
-- (and every root-relative pitch) sounds k tuning steps higher. This is a
-- property of the tuning, not of any note -- degree pitches keep their
-- spelling. In equal tunings this is a plain frequency shift; in table
-- tunings the new key picks up the temperament's flavor at that root.
fn transposeRoot(t Tuning, k Float) Tuning =
    root(t, stepHz(t, t.rootStep + k), t.rootStep);

---------------------------------------------------------------------------
-- Scale constructors

const major        = Scale { degrees: [0.0, 2.0, 4.0, 5.0, 7.0, 9.0, 11.0], wrap: 12.0 };
const minor        = Scale { degrees: [0.0, 2.0, 3.0, 5.0, 7.0, 8.0, 10.0], wrap: 12.0 };
const dorian       = Scale { degrees: [0.0, 2.0, 3.0, 5.0, 7.0, 9.0, 10.0], wrap: 12.0 };
const pentMajor    = Scale { degrees: [0.0, 2.0, 4.0, 7.0, 9.0], wrap: 12.0 };
const pentMinor    = Scale { degrees: [0.0, 3.0, 5.0, 7.0, 10.0], wrap: 12.0 };
const wholeTone    = Scale { degrees: [0.0, 2.0, 4.0, 6.0, 8.0, 10.0], wrap: 12.0 };

-- The trivial scale over k steps: every step is a degree.
fn chromatic(k Int) Scale {
    var degs = [Float]();
    for (i : (0..k - 1)) { degs push!(i toFloat); }
    Scale { degrees: degs, wrap: k toFloat }
}

---------------------------------------------------------------------------
-- Resolution: steps -> Hz and back

fn _periodCents(period Float) Float = period log2 * 1200.0;

-- Cents above the period start for a real position within a cents table,
-- interpolating linearly between adjacent entries (the entry after the last
-- is the period itself).
fn _tableCentsAt(cents [Float], periodCents Float, pos Float) Float {
    let i = pos floor toInt;
    let frac = pos - i toFloat;
    let c0 = cents[i];
    let c1 = i + 1 < cents length ? cents[i + 1] : periodCents;
    c0 + frac * (c1 - c0)
}

-- Frequency of a real-valued step.
fn stepHz(t Tuning, step Float) Float {
    match (t.kind) {
        TuningKind.ed(p): {
            let (n, period) = p;
            t.rootHz * period pow((step - t.rootStep) / n)
        }
        TuningKind.table(p): {
            let (cents, period) = p;
            let len = cents length toFloat;
            let pc = period _periodCents;
            let rel = step - t.rootStep;
            let cyc = (rel / len) floor;
            let pos = rel - cyc * len;
            t.rootHz * ((cyc * pc + _tableCentsAt(cents, pc, pos)) / 1200.0) exp2
        }
    }
}

-- Real-valued step whose frequency is hz (inverse of stepHz).
fn hzStep(t Tuning, hz Float) Float {
    match (t.kind) {
        TuningKind.ed(p): {
            let (n, period) = p;
            t.rootStep + n * (hz / t.rootHz) log2 / period log2
        }
        TuningKind.table(p): {
            let (cents, period) = p;
            let len = cents length;
            let pc = period _periodCents;
            let total = (hz / t.rootHz) log2 * 1200.0;
            let cyc = (total / pc) floor;
            let rem = total - cyc * pc;
            -- find the step whose cents bracket rem
            var i = len - 1;
            while (i > 0 && cents[i] > rem) { i = i - 1; }
            let c0 = cents[i];
            let c1 = i + 1 < len ? cents[i + 1] : pc;
            let frac = c1 > c0 ? (rem - c0) / (c1 - c0) : 0.0;
            t.rootStep + cyc * (len toFloat) + i toFloat + frac
        }
    }
}

-- Tuning step of a scale degree. Wraps through `degrees` adding `wrap` per
-- repetition (degree -1 is the top of the scale one wrap down).
fn degreeStep(s Scale, d Int) Float {
    let n = s.degrees length;
    var cyc = d // n;
    var idx = d - cyc * n;
    if (idx < 0) { idx = idx + n; cyc = cyc - 1; }
    s.degrees[idx] + cyc toFloat * s.wrap
}

-- Frequency of a scale degree, with optional accidental in tuning steps.
-- Degrees are ROOT-relative (degree 0 sounds at rootHz), like ratio and
-- cents; step and hz are absolute.
fn degreeHz(t Tuning, s Scale, d Int, acc Float = 0.0) Float =
    stepHz(t, t.rootStep + degreeStep(s, d) + acc);

-- Resolve a symbolic Pitch to Hz. The single play-time resolver used by the
-- players; rest resolves to 0 Hz (players skip rests before calling this).
fn pitchHz(p Pitch, t Tuning, s Scale) Float {
    match (p) {
        Pitch.hz(f):     f;
        Pitch.step(st):  stepHz(t, st);
        Pitch.degree(d): degreeHz(t, s, d.0, d.1);
        Pitch.ratio(r):  t.rootHz * r;
        Pitch.cents(c):  t.rootHz * (c / 1200.0) exp2;
        Pitch.rest:      0.0;
    }
}
