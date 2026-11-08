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
-- A Player is a handle: `replace(p, events, quant)` swaps the playing
-- sequence at the next beat on a `quant`-beat grid (0: now), and
-- `enqueue(p, events)` appends one to play when the current ends.
-- `play(..., quant)` starts on the grid the same way. For code running
-- INSIDE a silo task, use `scorePlayer` with spawn() instead -- it fires
-- playNote/releaseNote immediately on the silo's RT thread.
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
        noteSetParams(n, id, first, ps, ramp): EngineCmd.noteSetParams(n, id, first, ps, ramp);
    }
}

-- Graph setup for a Voice as a reusable Bundle: create the voicer node and
-- connect it to the silo output.
fn voiceBundle(v Voice, def String) Bundle =
    bundle() newNode(def, v.node) connect(v.node, 0, 0, 0);

---------------------------------------------------------------------------
-- Live player (NRT scheduler -> beat-scheduled Bundles)

-- A score waiting in the queue, with the noteID range it was scored with.
struct QueuedScore {
    sc List<(Float, NoteCmd)>,
    idBase Int,
}

-- One player coroutine's handle, shared with replace(). `cutoff` is the
-- beat where a replacing stream takes over (kNever while this stream is the
-- current one): the coroutine schedules nothing at or after it. `idBase` is
-- the noteID range of the score it is playing now (it changes on dequeue),
-- so a replace can release exactly this stream's notes at the cutoff.
struct PlayerStream {
    cutoff Ref<Float>,
    idBase Ref<Int>,
    done Ref<Bool>,
}

struct PlayerState {
    queue [QueuedScore],                 -- scores pending after the current ends
    live [PlayerStream],                  -- coroutines that may still schedule
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

-- "Not superseded" cutoff: far past any beat a session reaches.
const kNever = 1000000000000.0;

-- Slack when rounding up to the grid: a call made from a handler that ran
-- at (or a hair after) a grid beat takes that beat, not the next one.
const kQuantEps = 0.000001;

-- The first beat at or after now on a grid of `quant` beats (counted from
-- scheduler beat 0). quant <= 0 means now.
fn nextBeatOnGrid(quant Float) Float {
    let now = getBeats();
    if (quant <= 0.0) { return now; }
    let b = ceil(now / quant - kQuantEps) * quant;
    if (b < now) { now } else { b }
}

fn _newStream(idBase Int) PlayerStream =
    PlayerStream { cutoff: &kNever, idBase: &idBase, done: &false };

-- Plays one stream (and, while it is the current one, the queue after it)
-- with the score's beat 0 at scheduler beat `start`. Sleeps and scheduled
-- beats share one timeline: every bundle goes out at origin + t + latency,
-- and origin only advances by the score's own length.
coro fn _playerCo(st Ref<PlayerState>, silo Int, clock Int,
                  sc0 List<(Float, NoteCmd)>, start Float, me PlayerStream) Float {
    let now = getBeats();
    if (start > now + kGroupEps) { yield start - now; }
    var origin = start;         -- scheduler beat where cur's t=0 sits
    var cur = sc0;
    var at = 0.0;               -- score-relative beat this coroutine is at
    while (true) {
        let s = *st;
        if (s.stopped) { break; }
        let cut = *me.cutoff;
        if (cur isNil) {
            -- a superseded stream leaves the queue to the one replacing it
            if (cut < kNever || s.queue isEmpty) { break; }
            let q = s.queue[0];
            let rest = s.queue drop(1);
            s.queue clear! append!(rest);
            origin = origin + at;
            at = 0.0;
            cur = q.sc;
            me.idBase <- q.idBase;
        } else {
            let t0 = (cur head).0;
            if (origin + t0 >= cut - kGroupEps) { break; }   -- replaced from here on
            if (t0 > at + kGroupEps) {
                -- sleep until the group is due
                yield t0 - at;
                at = t0;
            } else {
                -- bundle every command sharing this onset, schedule at the
                -- absolute engine beat (sample-accurate)
                var b = bundle();
                while (cur notNil && (cur head).0 <= t0 + kGroupEps) {
                    b = b add((cur head).1 toEngineCmd);
                    cur = cur tail;
                }
                b sched(silo, clock, origin + t0 + getLatency());
            }
        }
    }
    me.done <- true;
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

-- Start a new stream on `p` at beat `start`, superseding every stream it
-- has running from that beat on.
fn _startStream(p Player, events List<Event>, start Float) Void {
    let s = *p.state;
    for (l : s.live) {
        if (!(*l.done) && *l.cutoff > start) {
            l.cutoff <- start;
            -- the old score's pending noteOffs go with it: release its whole
            -- noteID range at the switch beat. The ranges are disjoint, so
            -- this cannot touch the new stream's notes starting there.
            var b = bundle();
            let base = *l.idBase;
            var i = 0;
            while (i < p.voice.poly) {
                b = b noteOff(p.voice.node, base + i);
                i = i + 1;
            }
            b sched(p.silo, p.clock, start + getLatency());
        }
    }
    let live = s.live filter(fn(l PlayerStream) Bool = !(*l.done));
    s.live clear! append!(live);
    let base = _takeIdBase(p.voice.poly);
    let me = _newStream(base);
    s.live push!(me);
    go(_playerCo(p.state, p.silo, p.clock,
                 score(events, p.voice, p.tuning, p.scale, base), start, me));
}

-- Compile events through the tuning and start playing on `silo`/`clock`,
-- at the next beat on a grid of `quant` beats (0: now). Returns the Player
-- handle.
fn play(events List<Event>, v Voice, t Tuning = et12, s Scale = major,
        silo Int = 0, clock Int = 0, quant Float = 0.0) Player {
    let st = &PlayerState {
        queue: [QueuedScore](),
        live: [PlayerStream](),
        stopped: false,
    };
    let p = Player { state: st, voice: v, tuning: t, scale: s, silo: silo, clock: clock };
    _startStream(p, events, nextBeatOnGrid(quant));
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

-- Swap the playing sequence for `events` at the next beat on a grid of
-- `quant` beats (0: now). The old sequence plays up to that beat and its
-- notes are released there; the new one starts from its beat 0 there. The
-- queue is kept and follows the new sequence.
fn replace(p Player, events List<Event>, quant Float = 0.0) Void {
    if ((*p.state).stopped) { return; }
    _startStream(p, events, nextBeatOnGrid(quant));
}

-- Append a sequence to play after the current one ends.
fn enqueue(p Player, events List<Event>) Void {
    let s = *p.state;
    let base = _takeIdBase(p.voice.poly);
    s.queue push!(QueuedScore { sc: score(events, p.voice, p.tuning, p.scale, base),
                            idBase: base });
}

---------------------------------------------------------------------------
-- Silo-task player: walk a score with immediate playNote/releaseNote.
-- Use from code loaded INTO a silo:  fn start() Void { spawn(0, scorePlayer(sc)); }

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
                noteSetParams(n, id, first, ps, ramp): setNoteParams(n, id, first, ps, ramp);
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
            queue: [QueuedScore](),
            live: [PlayerStream](),
            stopped: false,
        };
        go(_playerCo(st, 0, 0, score(events, v, t, s), 0.0, _newStream(0)));
    })
}
