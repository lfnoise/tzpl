-- live/pattern.x -- pattern proxies: play Event streams into a live proxy.
--
-- A proxy whose definition uses voicer + noteParam is a note target. The
-- Voice is derived automatically from the proxy's scanned noteParams
-- (names in declaration order, defaults from each spec's init; gate is
-- engine-managed and excluded), so
--
--     import live.*;
--     import music.pat.*;
--
--     let keys = ndef(2);
--     keys <- fn() S {
--         voicer(16, fn() S {
--             let f = noteParam("freq", espec(20.0, 2000.0, 440.0));
--             let a = noteParam("amp", lspec(0.0, 1.0, 0.5));
--             sinosc(f) * a * (gate() adsr(0.01, 0.1, 0.7, 0.3))
--         }) sum
--     };
--     keys play;
--     let pl = play(keys, bind(...events...));
--     ...
--     pl stop;
--
-- The target node is re-resolved from the proxy at every emission, so a
-- mid-pattern redefine retargets seamlessly: notes ringing on the old
-- source fade out with the crossfade while new noteOns land on the new
-- source. The Voice is derived when the proxy's definition is there: a
-- play() issued while the first definition is still compiling (the cell
-- above, run as one) parks its events and scores them the moment the
-- noteParams are known, so the sequence starts as soon as the sound
-- exists instead of playing every note at the spec defaults. (The Voice's
-- param names are latched once derived; a redefine that renames
-- noteParams needs a fresh play().)
--
-- Requires the audio engine bridge (tzpl_app); not loadable under plain tzpl.

export music.score.*;

import bundles.*;
import audio_engine.*;
import clock.*;
import live.proxy.*;

-- Commands closer together than this are simultaneous and share a Bundle.
const _kGroupEps = 0.000000001;

---------------------------------------------------------------------------
-- Player handle

struct PxPlayerState {
    cur List<(Float, NoteCmd)>,      -- remaining commands, times score-relative
    queue [List<(Float, NoteCmd)>],  -- scores pending after cur ends
    pending [List<Event>],           -- sequences waiting for the proxy's Voice
    origin Float,                    -- scheduler beat where cur's t=0 sits
    stopped Bool,
}

struct PxPlayer {
    state Ref<PxPlayerState>,
    px Proxy,
    voice Ref<Voice>,                -- names empty until the definition landed
    poly Int,
    tuning Tuning,
    scale Scale,
    clock Int,
}

fn _voiceOf(px Proxy, poly Int) Voice {
    let ps = *px.state;
    voice(0, ps.noteNames, ps.noteDefaults, poly)
}

fn _hasVoice(pl PxPlayer) Bool = (*pl.voice).names length > 0;

fn _scoreFor(pl PxPlayer, events List<Event>) List<(Float, NoteCmd)> =
    score(events, *pl.voice, pl.tuning, pl.scale, _takeNoteIdBase(pl.poly));

-- Disjoint noteID ranges per stream: a collision lets one stream's noteOff
-- release the other stream's voice, and the missed release sticks a note.
let _nextNoteIdBase = &0;
fn _takeNoteIdBase(poly Int) Int {
    let base = *_nextNoteIdBase;
    _nextNoteIdBase <- base + poly;
    base
}

-- Rebuild a scored command against the proxy's CURRENT source node.
fn _cmdOnNode(c NoteCmd, node Int) EngineCmd {
    match (c) {
        noteOn(n, id, ps): EngineCmd.noteOn(node, id, ps);
        noteOff(n, id): EngineCmd.noteOff(node, id);
        noteSetParams(n, id, first, ps): EngineCmd.noteSetParams(node, id, first, ps);
    }
}

-- Beats between checks while a play() waits for its proxy to finish
-- compiling (tens of milliseconds at any usual tempo).
const _kBuildPoll = 0.05;

-- Score any sequences parked by play()/replace()/enqueue() while the
-- proxy's definition was compiling. True once nothing is pending; false
-- while the build is still in flight.
fn _scorePending(pl PxPlayer) Bool {
    let s = *pl.state;
    if (s.pending length == 0) { return true; }
    let px = pl.px;
    if (!(pl _hasVoice)) {
        if ((*px.state).noteNames length > 0) {
            pl.voice <- _voiceOf(px, pl.poly);
        } else if ((*px.state).building) {
            return false;
        } else {
            println("live: proxy has no noteParams -- define it with a voicer before playing events into it");
        }
    }
    var cur = s.cur;
    var queue = s.queue;
    for (es : s.pending) {
        let sc = pl _scoreFor(es);
        if (cur isNil && queue length == 0) { cur = sc; } else { queue push!(sc); }
    }
    pl.state <- PxPlayerState { ...(*pl.state), cur: cur, queue: queue,
                                pending: [List<Event>](), origin: getBeats() };
    true
}

coro fn _pxPlayerCo(pl PxPlayer) Float {
    let st = pl.state;
    let px = pl.px;
    let clock = pl.clock;
    st <- PxPlayerState { ...(*st), origin: getBeats() };
    var at = 0.0;
    var lastOrigin = (*st).origin;
    while (true) {
        if ((*st).stopped) { break; }
        if (!(pl _scorePending)) {
            yield _kBuildPoll;          -- definition still compiling
            continue;
        }
        let s = *st;
        if (s.origin != lastOrigin) {   -- replaced / dequeued: restart clock base
            lastOrigin = s.origin;
            at = 0.0;
        }
        if (s.cur isNil) {
            if (s.queue length == 0) { break; }
            let q0 = s.queue[0];
            var rest = [List<(Float, NoteCmd)>]();
            var i = 1;
            while (i < s.queue length) { rest push!(s.queue[i]); i = i + 1; }
            st <- PxPlayerState { ...(*st), cur: q0, queue: rest,
                                  origin: getBeats() };
        } else {
            let t0 = (s.cur head).0;
            if (t0 > at + _kGroupEps) {
                yield t0 - at;          -- sleep until the group is due
                at = t0;
            } else {
                -- one Bundle per onset group, aimed at the proxy's source
                -- as it is NOW; a silent proxy (src 0) skips the group.
                let node = (*px.state).src;
                var b = bundle();
                var cur = s.cur;
                while (cur notNil && (cur head).0 <= t0 + _kGroupEps) {
                    b = b add((cur head).1 _cmdOnNode(node));
                    cur = cur tail;
                }
                if (node != 0 && b length > 0) {
                    b sched(px.silo, clock, s.origin + t0 + getLatency());
                }
                st <- PxPlayerState { ...(*st), cur: cur };
            }
        }
    }
}

---------------------------------------------------------------------------
-- Playing events into a proxy

-- Compile events through the tuning and start playing into `px`. The Voice
-- comes from the proxy's current definition's noteParams.
fn play(px Proxy, events List<Event>, t Tuning = et12, s Scale = major,
        poly Int = 16, clock Int = 0) PxPlayer {
    let st = &PxPlayerState {
        cur: nil,
        queue: [List<(Float, NoteCmd)>](),
        pending: [events],
        origin: 0.0,
        stopped: false,
    };
    let pl = PxPlayer { state: st, px: px, voice: &_voiceOf(px, poly), poly: poly,
                        tuning: t, scale: s, clock: clock };
    -- score now when the definition is there; otherwise the coroutine
    -- does it as soon as the compile lands
    pl _scorePending;
    go(_pxPlayerCo(pl));
    pl
}

-- Stop at the next wake and release everything the proxy's source holds.
fn stop(pl PxPlayer) Void {
    pl.state <- PxPlayerState { ...(*pl.state), stopped: true };
    let node = (*pl.px.state).src;
    if (node != 0) { bundle() allNotesOff(node) go(pl.px.silo); }
}

-- Swap the playing sequence (takes effect at the player's next wake; the
-- new score starts from its beat 0 there).
fn replace(pl PxPlayer, events List<Event>) Void {
    if (pl _hasVoice) {
        pl.state <- PxPlayerState {
            ...(*pl.state),
            cur: pl _scoreFor(events),
            origin: getBeats(),
        };
    } else {
        -- still waiting for the definition: this sequence takes the place
        -- of whatever was parked
        pl.state <- PxPlayerState { ...(*pl.state), pending: [events] };
    }
    -- the swapped-out score's pending noteOffs are dropped with it
    let node = (*pl.px.state).src;
    if (node != 0) { bundle() allNotesOff(node) go(pl.px.silo); }
}

-- Append a sequence to play after the current one ends.
fn enqueue(pl PxPlayer, events List<Event>) Void {
    let s = *pl.state;
    if (pl _hasVoice) {
        s.queue push!(pl _scoreFor(events));
    } else {
        s.pending push!(events);
    }
}
