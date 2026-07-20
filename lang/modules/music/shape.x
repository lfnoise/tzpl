-- shape.x -- shapes: multidimensional sequences + morphological operators,
-- inspired by HMSL (the Hierarchical Music Specification Language).
--
-- A Shape is a sequence of FRAMES, each frame a vector of Floats over named
-- DIMENSIONS (default [dur, degree, amp]). Morphological operators act on
-- one dimension at a time and return new shapes; auto-mapping over the
-- frame arrays keeps them terse. `shapeEvents` renders a shape to the
-- shared List<Event>: dur becomes the note value, one of degree/step/hz/
-- cents becomes the pitch, amp the amplitude -- and every OTHER dimension
-- becomes a per-note synth param of the same name -- an idea drawn from
-- HMSL: melodies over any set of control dimensions, not just pitch.
--
--     let sh = shape([[0.5, 0.0, 0.4], [0.5, 2.0, 0.5], [1.0, 4.0, 0.6]]);
--     sh retrograde invertDim('degree, 2.0) shapeEvents play(v);
--
-- RT-safe: pure.

export music.core.*;

struct Shape {
    data [[Float]],   -- frames x dims
    dims [Symbol],    -- one name per column
}

fn shape(data [[Float]], dims [Symbol] = ['dur, 'degree, 'amp]) Shape =
    Shape { data: data, dims: dims };

fn numFrames(sh Shape) Int = sh.data length;

-- Index of a dimension, or -1 when absent.
fn dimIndex(sh Shape, dim Symbol) Int {
    var i = 0;
    while (i < sh.dims length) {
        if (sh.dims[i] == dim) { return i; }
        i = i + 1;
    }
    -1
}

-- One dimension as an array (empty if absent).
fn dimOf(sh Shape, dim Symbol) [Float] {
    let d = sh dimIndex(dim);
    var out = [Float]();
    if (d >= 0) {
        for (fr : sh.data) { out push!(fr[d]); }
    }
    out
}

fn _copyData(sh Shape) [[Float]] {
    var out = [[Float]]();
    for (fr : sh.data) { out push!(fr copy); }
    out
}

-- Replace one dimension's values (no-op if absent or length mismatch).
fn withDim(sh Shape, dim Symbol, vals [Float]) Shape {
    let d = sh dimIndex(dim);
    if (d < 0 || vals length != sh.data length) { return sh; }
    var data = sh _copyData;
    var i = 0;
    while (i < data length) {
        data[i][d] = vals[i];
        i = i + 1;
    }
    Shape { data: data, dims: sh.dims }
}

---------------------------------------------------------------------------
-- Morphological operators. Each returns a new Shape; missing dimensions
-- make the operator a no-op (shapes stay total).

-- Reverse the frame order.
fn retrograde(sh Shape) Shape {
    var data = [[Float]]();
    var i = sh.data length - 1;
    while (i >= 0) {
        data push!(sh.data[i] copy);
        i = i - 1;
    }
    Shape { data: data, dims: sh.dims }
}

-- Apply f to every value of one dimension.
fn warpDim(sh Shape, dim Symbol, f (Float) Float) Shape {
    let d = sh dimIndex(dim);
    if (d < 0) { return sh; }
    var data = sh _copyData;
    var i = 0;
    while (i < data length) {
        data[i][d] = f(data[i][d]);
        i = i + 1;
    }
    Shape { data: data, dims: sh.dims }
}

-- Reflect a dimension around a center value.
fn invertDim(sh Shape, dim Symbol, center Float) Shape =
    sh warpDim(dim, fn(x Float) Float { center + center - x });

-- Shift a dimension by amt.
fn transposeDim(sh Shape, dim Symbol, amt Float) Shape =
    sh warpDim(dim, fn(x Float) Float { x + amt });

-- Scale a dimension by k.
fn scaleDim(sh Shape, dim Symbol, k Float) Shape =
    sh warpDim(dim, fn(x Float) Float { x * k });

-- Snap a dimension to a grid.
fn quantizeDim(sh Shape, dim Symbol, grid Float) Shape =
    sh warpDim(dim, fn(x Float) Float { (x / grid + 0.5) floor * grid });

-- Frames of a, then frames of b (dims taken from a).
fn catShapes(a Shape, b Shape) Shape {
    var data = a _copyData;
    for (fr : b.data) { data push!(fr copy); }
    Shape { data: data, dims: a.dims }
}

-- Alternate frames from a and b, continuing with the longer.
fn interleave(a Shape, b Shape) Shape {
    var data = [[Float]]();
    var i = 0;
    let n = max(a.data length, b.data length);
    while (i < n) {
        if (i < a.data length) { data push!(a.data[i] copy); }
        if (i < b.data length) { data push!(b.data[i] copy); }
        i = i + 1;
    }
    Shape { data: data, dims: a.dims }
}

---------------------------------------------------------------------------
-- Rendering

-- Total duration in beats (sum of the dur dimension; frames count 1 beat
-- each when there is no dur dimension).
fn shapeDur(sh Shape) Float {
    let d = sh dimIndex('dur);
    if (d < 0) { return sh.data length toFloat; }
    var t = 0.0;
    for (fr : sh.data) { t = t + fr[d]; }
    t
}

-- Render to events. Pitch comes from the first of degree/step/hz/cents
-- present; every other dimension besides dur/amp/legato becomes a synth
-- param named after it. The degree dimension stays Float so morphological
-- operators can interpolate; rendering rounds it to the nearest whole
-- scale degree (use a step/hz/cents dimension for continuous pitch).
fn shapeEvents(sh Shape, t0 Float = 0.0, legato Float = 0.9) List<Event> {
    let dDur = sh dimIndex('dur);
    let dDeg = sh dimIndex('degree);
    let dStep = sh dimIndex('step);
    let dHz = sh dimIndex('hz);
    let dCents = sh dimIndex('cents);
    let dAmp = sh dimIndex('amp);
    let dLeg = sh dimIndex('legato);
    var es = [Event]();
    var t = t0;
    for (fr : sh.data) {
        let dur = dDur >= 0 ? fr[dDur] : 1.0;
        let pitch = dDeg >= 0 ? Pitch.degree(fr[dDeg] round toInt, 0.0)
                  : dStep >= 0 ? Pitch.step(fr[dStep])
                  : dHz >= 0 ? Pitch.hz(fr[dHz])
                  : dCents >= 0 ? Pitch.cents(fr[dCents])
                  : Pitch.rest;
        var ps = [(Symbol, Float)]();
        var d = 0;
        while (d < sh.dims length) {
            let name = sh.dims[d];
            if (d != dDur && d != dDeg && d != dStep && d != dHz
                && d != dCents && d != dAmp && d != dLeg) {
                ps push!((name, fr[d]));
            }
            d = d + 1;
        }
        es push!(Event {
            t: t, dur: dur,
            sustain: dur * (dLeg >= 0 ? fr[dLeg] : legato),
            amp: dAmp >= 0 ? fr[dAmp] : 0.5,
            pitch: pitch, params: ps,
        });
        t = t + dur;
    }
    es toList
}
