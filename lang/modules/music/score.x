-- score.x -- compile Event streams to timestamped note commands (pure).
--
-- The engine-free half of the player: score() lowers a List<Event> into a
-- time-ordered List<(Float, NoteCmd)> -- noteOn at each onset, noteOff at
-- onset + sustain, noteIDs allocated round-robin from the Voice's pool,
-- rests skipped. NoteCmd deliberately mirrors the note cases of bundles.x's
-- EngineCmd (same payload order), so music.play adapts each command with a
-- one-line match. Keeping this module free of bridge imports means the whole
-- event->command pipeline golden-tests under plain `tzpl`.
--
-- RT-safe: pure, no IO.

export music.core.*;

-- Mirrors the note subset of bundles.x EngineCmd (payload order matches the
-- audio_engine FFI argument order).
enum NoteCmd {
    noteOn (Int, Int, [Float]),             -- node, noteID, params
    noteOff (Int, Int),                     -- node, noteID
    noteSetParams (Int, Int, Int, [Float]), -- node, noteID, firstParam, params
}

-- How events map onto one synth node's voice pool.
--   node     -- engine node id of the voicer
--   names    -- noteOn positional param order (= noteParam declaration order)
--   defaults -- value used when a name resolves nowhere
--   poly     -- noteID pool size, allocated round-robin
struct Voice {
    node Int,
    names [Symbol],
    defaults [Float],
    poly Int,
}

fn voice(node Int, names [Symbol], defaults [Float], poly Int = 16) Voice =
    Voice { node: node, names: names, defaults: defaults, poly: poly };

-- A Voice for the standard "voicer" instrument synthdefs, whose first two
-- noteParams are a MIDI-style note number and an amplitude.
fn pitchVoice(node Int, poly Int = 16) Voice =
    voice(node, ['pitch, 'amp], [60.0, 0.5], poly);

-- A Voice for synths taking frequency in Hz directly.
fn freqVoice(node Int, poly Int = 16) Voice =
    voice(node, ['freq, 'amp], [440.0, 0.5], poly);

-- Resolve one event to the positional [Float] params for v.names.
-- 'freq resolves the event's Pitch to Hz through the tuning; 'pitch resolves
-- to the equivalent MIDI-style note number (69 + 12*log2(hz/440)) for synths
-- that convert note number -> Hz internally (fractional values are the
-- point: any tuning survives the trip exactly). 'amp is the event amp;
-- anything else looks up e.params by name, then falls back to the default.
fn eventParams(e Event, v Voice, t Tuning, s Scale) [Float] {
    var out = [Float]();
    var i = 0;
    while (i < v.names length) {
        let name = v.names[i];
        if (name == 'freq) {
            out push!(pitchHz(e.pitch, t, s));
        } else if (name == 'pitch) {
            out push!(69.0 + 12.0 * (pitchHz(e.pitch, t, s) / 440.0) log2);
        } else if (name == 'amp) {
            out push!(e.amp);
        } else {
            var found = false;
            for (p : e.params) {
                if (!found && p.0 == name) {
                    out push!(p.1);
                    found = true;
                }
            }
            if (!found) { out push!(v.defaults[i]); }
        }
        i = i + 1;
    }
    out
}

-- index of the earliest pending release (linear scan; pool-bounded)
fn _minIdx(ts [Float]) Int {
    var m = 0;
    var i = 1;
    while (i < ts length) {
        if (ts[i] < ts[m]) { m = i; }
        i = i + 1;
    }
    m
}

coro fn _scoreCo(events List<Event>, v Voice, t Tuning, s Scale) (Float, NoteCmd) {
    var es = events;
    var relT = [Float]();   -- pending release times (unsorted; min-scanned)
    var relI = [Int]();     -- matching noteIDs
    var next = 0;
    while (es notNil) {
        let e = es head;
        -- flush releases due at or before this onset
        while (relT length > 0) {
            let m = relT _minIdx;
            if (relT[m] > e.t) { break; }
            yield (relT[m], NoteCmd.noteOff(v.node, relI[m]));
            relT[m] = relT[relT length - 1]; relT pop!;
            relI[m] = relI[relI length - 1]; relI pop!;
        }
        if (!(e isRest)) {
            let id = next % v.poly;
            next = next + 1;
            yield (e.t, NoteCmd.noteOn(v.node, id, eventParams(e, v, t, s)));
            relT push!(e.t + e.sustain);
            relI push!(id);
        }
        es = es tail;
    }
    -- flush remaining releases in time order
    while (relT length > 0) {
        let m = relT _minIdx;
        yield (relT[m], NoteCmd.noteOff(v.node, relI[m]));
        relT[m] = relT[relT length - 1]; relT pop!;
        relI[m] = relI[relI length - 1]; relI pop!;
    }
}

-- Lower an onset-ordered event stream to a time-ordered command stream.
-- Lazy: works on infinite streams (releases are interleaved as their times
-- come due). This is the golden-test surface for the player pipeline.
fn score(events List<Event>, v Voice, t Tuning = et12, s Scale = major)
        List<(Float, NoteCmd)> =
    _scoreCo(events, v, t, s) toList;

---------------------------------------------------------------------------
-- Stable formatting (mirrors bundles.x toString style)

fn _fmtFloats(a [Float]) String {
    var out = "[";
    var between = false;
    for (x : a) {
        if (between) { out = out $ ", "; } else { between = true; }
        out = out $ x toString;
    }
    out $ "]"
}

fn toString(c NoteCmd) String {
    match (c) {
        noteOn(n, id, ps): "noteOn(%^, %^, %^)" fmt(n, id, ps _fmtFloats);
        noteOff(n, id): "noteOff(%^, %^)" fmt(n, id);
        noteSetParams(n, id, first, ps):
            "noteSetParams(%^, %^, %^, %^)" fmt(n, id, first, ps _fmtFloats);
    }
}

-- Print each command of a finite score on its own line.
fn showScore(sc List<(Float, NoteCmd)>) Void {
    for (tc : sc) {
        "%^: %^" fmt(tc.0, tc.1 toString) println;
    }
}
