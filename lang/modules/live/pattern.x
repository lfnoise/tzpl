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
-- source. (The Voice's param names are latched at play(); a redefine that
-- renames noteParams needs a fresh play().)
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
    origin Float,                    -- scheduler beat where cur's t=0 sits
    stopped Bool,
}

struct PxPlayer {
    state Ref<PxPlayerState>,
    px Proxy,
    voice Voice,
    tuning Tuning,
    scale Scale,
    clock Int,
}

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

coro fn _pxPlayerCo(st Ref<PxPlayerState>, px Proxy, clock Int) Float {
    st <- PxPlayerState { ...(*st), origin: getBeats() };
    var at = 0.0;
    var lastOrigin = (*st).origin;
    while (true) {
        let s = *st;
        if (s.stopped) { break; }
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
            st <- PxPlayerState { cur: q0, queue: rest, origin: getBeats(),
                                  stopped: false };
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
    let ps = *px.state;
    if (ps.noteNames length == 0) {
        println("live: proxy has no noteParams -- define it with a voicer before playing events into it");
    }
    let v = voice(0, ps.noteNames, ps.noteDefaults, poly);
    let st = &PxPlayerState {
        cur: score(events, v, t, s, _takeNoteIdBase(poly)),
        queue: [List<(Float, NoteCmd)>](),
        origin: 0.0,
        stopped: false,
    };
    go(_pxPlayerCo(st, px, clock));
    PxPlayer { state: st, px: px, voice: v, tuning: t, scale: s, clock: clock }
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
    pl.state <- PxPlayerState {
        ...(*pl.state),
        cur: score(events, pl.voice, pl.tuning, pl.scale,
                   _takeNoteIdBase(pl.voice.poly)),
        origin: getBeats(),
    };
    -- the swapped-out score's pending noteOffs are dropped with it
    let node = (*pl.px.state).src;
    if (node != 0) { bundle() allNotesOff(node) go(pl.px.silo); }
}

-- Append a sequence to play after the current one ends.
fn enqueue(pl PxPlayer, events List<Event>) Void {
    let s = *pl.state;
    s.queue push!(score(events, pl.voice, pl.tuning, pl.scale,
                        _takeNoteIdBase(pl.voice.poly)));
}
