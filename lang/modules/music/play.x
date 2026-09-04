-- play.x -- players: schedule scores on the audio engine (engine-coupled).
--
-- The engine half of the player. music.score lowers events to a pure
-- List<(Float, NoteCmd)>; this module turns each group of commands sharing
-- an onset into ONE first-class Bundle (bundles.x) and schedules it at an
-- absolute beat on the engine's TempoClock -- sample-accurate, chords
-- atomic. The driving coroutine runs on the NRT tempo scheduler (clock.go)
-- and sleeps between onsets.
--
--     import music.play.*;
--     import audio_engine.*;
--     engineStart();
--     voiceBundle(freqVoice(101), "myvoicer") go(0);
--     let p = play(bind(...), freqVoice(101));
--     ...
--     p stop;
--
-- A Player is a handle: `replace(p, events)` swaps the playing sequence and
-- `enqueue(p, events)` appends one to play when the current ends (both take
-- effect at the player's next wake). For code running INSIDE a silo task,
-- use `scorePlayer` with spawn() instead -- it fires playNote/releaseNote
-- immediately on the silo's RT thread.
--
-- Requires the audio engine bridge (tzpl_app); not loadable under plain tzpl.

export music.score.*;
export bundles.*;       -- Bundle/EngineCmd are part of the player surface

import audio_engine.*;
import clock.*;

-- Commands closer together than this are considered simultaneous and share
-- a Bundle.
const kGroupEps = 0.000000001;

fn toEngineCmd(c NoteCmd) EngineCmd {
    match (c) {
        noteOn(n, id, ps): EngineCmd.noteOn(n, id, ps);
        noteOff(n, id): EngineCmd.noteOff(n, id);
        noteSetParams(n, id, first, ps): EngineCmd.noteSetParams(n, id, first, ps);
    }
}

-- Graph setup for a Voice as a reusable Bundle: create the voicer node and
-- connect it to the silo output.
fn voiceBundle(v Voice, def String) Bundle =
    bundle() newNode(def, v.node) connect(v.node, 0, 0, 0);

---------------------------------------------------------------------------
-- Live player (NRT scheduler -> beat-scheduled Bundles)

struct PlayerState {
    cur List<(Float, NoteCmd)>,      -- remaining commands, times score-relative
    queue [List<(Float, NoteCmd)>],  -- scores pending after cur ends
    origin Float,                    -- scheduler beat where cur's t=0 sits
    stopped Bool,
}

struct Player {
    state Ref<PlayerState>,
    voice Voice,
    tuning Tuning,
    scale Scale,
    silo Int,
    clock Int,
}

coro fn _playerCo(st Ref<PlayerState>, silo Int, clock Int) Float {
    -- anchor the score's beat 0 at the current scheduler beat
    st <- PlayerState { ...(*st), origin: getBeats() };
    var at = 0.0;               -- score-relative beat this coroutine is at
    var lastOrigin = (*st).origin;
    while (true) {
        let s = *st;
        if (s.stopped) { break; }
        if (s.origin != lastOrigin) {   -- replaced / dequeued: restart clock base
            lastOrigin = s.origin;
            at = 0.0;
        }
        if (s.cur isNil) {
            if (s.queue isEmpty) { break; }
            st <- PlayerState { cur: s.queue[0], queue: s.queue drop(1),
                                origin: getBeats(), stopped: false };
        } else {
            let t0 = (s.cur head).0;
            if (t0 > at + kGroupEps) {
                -- sleep until the group is due
                yield t0 - at;
                at = t0;
            } else {
                -- bundle every command sharing this onset, schedule at the
                -- absolute engine beat (sample-accurate)
                var b = bundle();
                var cur = s.cur;
                while (cur notNil && (cur head).0 <= t0 + kGroupEps) {
                    b = b add((cur head).1 toEngineCmd);
                    cur = cur tail;
                }
                b sched(silo, clock, s.origin + t0 + getLatency());
                st <- PlayerState { ...(*st), cur: cur };
            }
        }
    }
}

-- Every player started by play() and not yet stopped. Module functions share
-- the one array object (top-level vars are snapshotted into functions by
-- value, so it is mutated in place and never reassigned). stopAll() stops
-- and drops them all -- the recovery when a Player handle was rebound and
-- the old player plays on.
var _players = [Player]();

-- Next free noteID range. Every score stream a player starts (play /
-- replace / enqueue) takes a fresh range of `poly` ids, so streams sounding
-- at the same time on the same node can never collide note ids -- a
-- collision lets one stream's noteOff find the other stream's voice, and
-- the voice that missed its release stays stuck on.
let _nextIdBase = &0;

fn _takeIdBase(poly Int) Int {
    let base = *_nextIdBase;
    _nextIdBase <- base + poly;
    base
}

fn _prunePlayers() Void {
    let live = _players filter(fn(q Player) Bool = !(*q.state).stopped);
    _players clear! append!(live);
}

-- Compile events through the tuning and start playing on `silo`/`clock`.
-- Returns the Player handle.
fn play(events List<Event>, v Voice, t Tuning = et12, s Scale = major,
        silo Int = 0, clock Int = 0) Player {
    let st = &PlayerState {
        cur: score(events, v, t, s, _takeIdBase(v.poly)),
        queue: [List<(Float, NoteCmd)>](),
        origin: 0.0,
        stopped: false,
    };
    go(_playerCo(st, silo, clock));
    let p = Player { state: st, voice: v, tuning: t, scale: s, silo: silo, clock: clock };
    _prunePlayers();
    _players push!(p);
    p
}

-- Stop every player play() has started and not yet stopped, releasing what
-- their voices hold. Safe to run any number of times.
fn stopAll() Void {
    _players stop;      -- auto-maps stop(Player) over the array
    _players clear!;
}

-- Stop the OLDEST still-running player (FIFO -- peel off from the start).
-- Returns true if a player was stopped, false if none were running.
fn stopFirst() Bool {
    _prunePlayers();
    if (_players isEmpty) { return false; }
    _players[0] stop;
    _prunePlayers();
    true
}

-- Stop the NEWEST still-running player (LIFO -- undo the latest play).
-- Returns true if a player was stopped, false if none were running.
fn stopLast() Bool {
    _prunePlayers();
    if (_players isEmpty) { return false; }
    let q = _players pop!;
    q stop;
    true
}

-- Stop at the next wake and release everything the voice is holding.
fn stop(p Player) Void {
    p.state <- PlayerState { ...(*p.state), stopped: true };
    bundle() allNotesOff(p.voice.node) go(p.silo);
}

-- Swap the playing sequence for `events` (takes effect at the next wake;
-- the new score starts from its beat 0 there).
fn replace(p Player, events List<Event>) Void {
    p.state <- PlayerState {
        ...(*p.state),
        cur: score(events, p.voice, p.tuning, p.scale,
                   _takeIdBase(p.voice.poly)),
        origin: getBeats(),
    };
    -- The old score's pending noteOffs are dropped with it; release what the
    -- voice holds so the swapped-out notes don't stay stuck on.
    bundle() allNotesOff(p.voice.node) go(p.silo);
}

-- Append a sequence to play after the current one ends.
fn enqueue(p Player, events List<Event>) Void {
    let s = *p.state;
    s.queue push!(score(events, p.voice, p.tuning, p.scale,
                        _takeIdBase(p.voice.poly)));
}

---------------------------------------------------------------------------
-- Silo-task player: walk a score with immediate playNote/releaseNote.
-- Use from code loaded INTO a silo:  fn start() Void { spawn(0, scorePlayer(sc)); }
-- (noteSetParams is not emitted by score(); it is ignored here.)

coro fn scorePlayer(sc0 List<(Float, NoteCmd)>) Float {
    var sc = sc0;
    var at = 0.0;
    while (sc notNil) {
        let tc = sc head;
        if (tc.0 > at + kGroupEps) {
            yield tc.0 - at;
            at = tc.0;
        } else {
            match (tc.1) {
                noteOn(n, id, ps): playNote(n, id, ps);
                noteOff(n, id): releaseNote(n, id);
                noteSetParams(n, id, first, ps): 0;
            }
            sc = sc tail;
        }
    }
}

---------------------------------------------------------------------------
-- Offline render

-- Render a FINITE event stream to an audio file. Builds the voice graph,
-- plays the score on the render-local scheduler, and returns the render
-- handle (await renderDone(h) or poll isRenderDone(h)).
fn render(events List<Event>, v Voice, def String, path String, durSecs Float,
          t Tuning = et12, s Scale = major) Int {
    renderNRT(path, durSecs, fn() Void {
        voiceBundle(v, def) go(0);
        let st = &PlayerState {
            cur: score(events, v, t, s),
            queue: [List<(Float, NoteCmd)>](),
            origin: 0.0,
            stopped: false,
        };
        go(_playerCo(st, 0, 0));
    })
}
