-- music_fx_demos.x -- three demonstrations tying the music, instruments,
-- and effects libraries together.
--
-- One persistent node graph: three instruments from lang/modules/instruments.x
-- (wtLead, ksPluck, resonBank), each feeding its own effect chain from
-- lang/modules/effects.x, all chains meeting at a shared reverb bus (the
-- engine splices fan-in mixers automatically):
--
--     wtLead(101)    -> fxChorus(201) -> fxEcho(202) --\
--     ksPluck12(102) -> fxPingPong(203) ---------------+-> fxReverb(205) -> out
--     resonBank(103) -> fxPhaser(204) -----------------/
--
-- Three demos then play different music through that graph, one per
-- composition dialect, each with several simultaneous parts and
-- beat-scheduled effect-control moves:
--
--   1. music.pat    -- a groove: weighted-random melody with rests, a
--                      tresillo bass, and slow pad triads (three players
--                      on one clock); at beat 16 the lead filter opens and
--                      the echo regenerates harder.
--   2. music.media  -- a canon: the theme against its own retrograde, an
--                      answer a fourth down entering two beats late over a
--                      drone, and a double-speed statement on the lead.
--   3. music.spans  -- Bohlen-Pierce (13 divisions of the tritave):
--                      a 5-in-13 euclidean bass, bell figures that reverse
--                      every third cycle with amp riding a slow sine, and
--                      offbeat stabs; the phaser and delays deepen as it runs.
--
-- Run interactively (Ctrl-C to stop; the driver stops each demo itself):
--   ./build/app/tzpl_app --nogui --wait -I lang/modules -I bridge/modules \
--       examples/music_fx_demos.x

import synthdef.*;
import synthc.compile.*;
import wavetables.*;
import instruments.*;
import effects.*;
import clock.*;
import audio_engine.*;
import music.pat.*;
import music.media.*;
import music.spans.*;
import music.play.*;

---------------------------------------------------------------------------
-- Compile the instrument and effect defs (async; await before first play).

let cLead   = wtLead() defSynthX("wtLead");
let cPluck  = ksPluck(12) defSynthX("ksPluck12");
let cBank   = resonBank() defSynthX("resonBank");
let cChorus = fxChorus defSynthX("fxChorus");
let cEcho   = fxEcho defSynthX("fxEcho");
let cPong   = fxPingPong defSynthX("fxPingPong");
let cPhase  = fxPhaser defSynthX("fxPhaser");
let cVerb   = fxReverb defSynthX("fxReverb");
await cLead; await cPluck; await cBank;
await cChorus; await cEcho; await cPong; await cPhase; await cVerb;

---------------------------------------------------------------------------
-- Engine and the persistent graph.

const kLead   = 101;    -- wtLead: wavetable lead with filter envelopes
const kPluck  = 102;    -- ksPluck12: Karplus-Strong strings, 12 voices
const kBank   = 103;    -- resonBank: ringing-filter bank
const kChorus = 201;
const kEcho   = 202;
const kPong   = 203;
const kPhase  = 204;
const kVerb   = 205;    -- shared reverb bus

engineStart();
masterGain(0.2);

begin();
newNode("wtLead", kLead);
fillBuffer(kLead, 0, 1, sawTables(1.0));    -- the bank the oscillator reads
newNode("ksPluck12", kPluck);
newNode("resonBank", kBank);
newNode("fxChorus", kChorus);
newNode("fxEcho", kEcho);
newNode("fxPingPong", kPong);
newNode("fxPhaser", kPhase);
newNode("fxReverb", kVerb);
-- lead chain: chorus thickens, echo repeats
connect(kLead, 0, kChorus, 0);
connect(kChorus, 0, kEcho, 0);
connect(kEcho, 0, kVerb, 0);
-- pluck chain: stereo ping-pong echoes
connect(kPluck, 0, kPong, 0);
connect(kPong, 0, kVerb, 0);
-- pad chain: swept phaser
connect(kBank, 0, kPhase, 0);
connect(kPhase, 0, kVerb, 0);
-- everything meets at the reverb, reverb to the speakers
connect(kVerb, 0, 0, 0);
sched(0);

-- The instruments declare [freq, amp] note params -- freqVoice's layout.
let leadV  = freqVoice(kLead, 8);
let pluckV = freqVoice(kPluck, 12);
let bankV  = freqVoice(kBank, 8);

-- Anchor degree 0 an octave below A440 so basses have room underneath.
let keyA = transposeRoot(et12, -12.0);

---------------------------------------------------------------------------
-- Demo 1: music.pat -- a three-layer groove in A dorian.

fn padLayer(off Int) List<Event> =
    bind(List(0, -1, 1, -3) cyc map(fn(d Int) Int { d + off }) degree,
         List(4.0) cyc,
         List(0.26) cyc,
         List(0.95) cyc);

fn demo1() Void {
    randSeed(112);

    -- panel: plucky sweeping lead, dark long bass strings, slow-swell pads,
    -- and demo-specific delay/reverb settings
    begin();
    setControl(kLead, 0, 0.004);    -- attack
    setControl(kLead, 4, 700.0);    -- cutoff base
    setControl(kLead, 5, 4.0);      -- envOct: sweep four octaves
    setControl(kLead, 6, 0.25);     -- res (1/Q -- smaller is sharper)
    setControl(kLead, 8, 0.3);      -- fdecay
    setControl(kPluck, 1, 2.2);     -- string decay
    setControl(kPluck, 2, 0.55);    -- damp: darker strings
    setControl(kBank, 0, 0.4);      -- pad attack swell
    setControl(kBank, 2, 5.0);      -- mode ring-out
    setControl(kEcho, 0, 0.4);      -- echo time (seconds)
    setControl(kEcho, 1, 0.35);     -- echo feedback
    setControl(kEcho, 2, 0.3);      -- echo mix
    setControl(kPong, 0, 0.268);    -- dotted-eighth ping-pong at 112 bpm
    setControl(kPong, 1, 0.3);
    setControl(kPong, 2, 0.25);
    setControl(kVerb, 0, 3.0);      -- reverb decay
    setControl(kVerb, 2, 0.28);     -- reverb mix
    sched(0);

    -- melody: weighted random degrees with breathing rests, cycled
    -- durations that cross the bar, brownian dynamics
    let melody = bind(
        wpicks([degree(0), degree(1), degree(2), degree(3),
                degree(4), degree(6), rest()],
               [3.0, 2.0, 3.0, 1.5, 2.5, 1.0, 2.0]),
        List(0.5, 0.25, 0.25, 0.5, 0.5, 1.0) cyc,
        browns(0.25, 0.6, 0.08),
        List(0.85) cyc);

    -- bass: tresillo (3+3+2 eighths) on roots, fifths, and fourths,
    -- two octaves down on the plucked strings
    let bassLine = bind(
        List(-14, -14, -10, -14, -14, -11) cyc degree,
        List(0.75, 0.75, 0.5) cyc,
        List(0.6) cyc,
        List(0.9) cyc);

    -- pads: a triad every four beats, three stacked bind layers
    let pads = merge(padLayer(0), merge(padLayer(2), padLayer(4)));

    play(melody takeDur(32.0), leadV, keyA, dorian);
    play(bassLine takeDur(32.0), pluckV, keyA, dorian);
    play(pads takeDur(31.0), bankV, keyA, dorian);

    -- at beat 16, open the lead filter and push the echo regeneration --
    -- a beat-scheduled panel move riding the same clock as the players
    bundle()
        setControl(kLead, 4, 2400.0)
        setControl(kEcho, 1, 0.55)
        sched(0, 0, getBeats() + 16.0 + getLatency());
}

---------------------------------------------------------------------------
-- Demo 2: music.media -- a canon in A minor across all three instruments.

fn mdeg(d Int, dur Float) Music<Pitch> = note(dur, degree(d));

fn demo2() Float {
    -- panel: rounder lead, brighter longer strings, struck-bell bank,
    -- echo on the beat at 96 bpm, bigger room
    begin();
    setControl(kLead, 0, 0.03);
    setControl(kLead, 4, 1200.0);
    setControl(kLead, 5, 2.0);
    setControl(kPluck, 1, 3.5);
    setControl(kPluck, 2, 0.3);
    setControl(kBank, 0, 0.01);
    setControl(kBank, 2, 3.5);
    setControl(kEcho, 0, 0.625);
    setControl(kEcho, 1, 0.4);
    setControl(kChorus, 0, 0.15);   -- slower chorus
    setControl(kVerb, 0, 5.0);
    setControl(kVerb, 2, 0.33);
    sched(0);

    let theme = line([mdeg(0, 1.0), mdeg(2, 0.5), mdeg(3, 0.5), mdeg(4, 1.0),
                      mdeg(2, 0.5), mdeg(5, 0.75), mdeg(4, 0.25),
                      mdeg(3, 0.5), mdeg(0, 1.0)]);   -- 6 beats

    -- plucked strings: theme, its retrograde, then a closing statement
    -- up a step -- an 18-beat palindrome-plus-coda
    let pluckPart = theme $ (theme retro) $ (theme trans(1) dyn(1.1));

    -- filter bank: the same palindrome a fourth down, two beats late,
    -- softer, over a held drone on the low fifth
    let answer = mrest(2.0) $ ((theme $ (theme retro)) trans(-3) dyn(0.65));
    let bankPart = answer | (note(18.0, degree(-7)) dyn(0.45));

    -- lead: sits out the first half, then the theme twice at double speed
    let leadPart = mrest(12.0) $ (theme tempo(2.0) times(2) dyn(0.8));

    play(pluckPart perform, pluckV, keyA, minor);
    play(bankPart perform, bankV, keyA, minor);
    play(leadPart perform(0.0, 0.6), leadV, keyA, minor);
    pluckPart mdur
}

---------------------------------------------------------------------------
-- Demo 3: music.spans -- euclidean patterns in Bohlen-Pierce (13ED3).

fn spd(x Int) Pattern<Pitch> = pure(degree(x));

fn demo3() Float {
    -- panel: glassy short lead stabs, tighter strings, bell-like bank,
    -- and delays that will be pushed deeper while it plays
    begin();
    setControl(kLead, 0, 0.002);
    setControl(kLead, 3, 0.15);     -- short release
    setControl(kLead, 4, 1800.0);
    setControl(kLead, 5, 3.0);
    setControl(kPluck, 1, 1.6);
    setControl(kPluck, 2, 0.45);
    setControl(kBank, 0, 0.003);    -- struck, not bowed
    setControl(kBank, 1, 0.03);
    setControl(kBank, 2, 4.0);
    setControl(kPhase, 0, 0.25);    -- slow phaser sweep on the bells
    setControl(kPhase, 1, 0.35);
    setControl(kEcho, 0, 0.31);
    setControl(kEcho, 2, 0.35);
    setControl(kPong, 0, 0.345);
    setControl(kPong, 1, 0.45);
    setControl(kVerb, 0, 4.0);
    setControl(kVerb, 2, 0.3);
    sched(0);

    -- 13 equal divisions of the tritave, anchored at 110 Hz; every step
    -- is a degree, so euclid(5, 13) is five pulses around the whole gamut
    let bpKey = bp() root(110.0, 0.0);
    let bpScale = chromatic(13);

    -- bass: 5-in-13 euclidean pulse on the root a tritave down
    let bassPat = spd(-13) euclid(5, 13) events(0.7, 0.5);

    -- bells: six-step figure in the upper tritave, rotating its start
    -- each cycle, reversed every third cycle, amp riding a slow sine
    let bells = fastcat([spd(13), spd(16), spd(19), spd(22), spd(20), spd(17)])
        iterp(6)
        every(3, fn(p Pattern<Pitch>) Pattern<Pitch> { p rev })
        events(0.45, 0.25)
        withAmp(sinePat() slow(3.0) range(0.18, 0.45) segment(6));

    -- stabs: three offbeat hits per cycle at the top of the gamut
    let stabs = spd(26) euclid(3, 8, 1) late(0.125) events(0.3, 0.3);

    -- 8 cycles of 4 beats = 32 beats
    play(patEvents(bassPat, 8.0, 4.0), pluckV, bpKey, bpScale);
    play(patEvents(bells, 8.0, 4.0), bankV, bpKey, bpScale);
    play(patEvents(stabs, 8.0, 4.0), leadV, bpKey, bpScale);

    -- deepen the effects as the pattern runs: phaser bites at beat 16,
    -- both delays regenerate harder at beat 24
    let b0 = getBeats() + getLatency();
    bundle()
        setControl(kPhase, 1, 0.6)
        setControl(kVerb, 0, 7.0)
        sched(0, 0, b0 + 16.0);
    bundle()
        setControl(kPong, 1, 0.65)
        setControl(kEcho, 1, 0.55)
        sched(0, 0, b0 + 24.0);
    32.0
}

---------------------------------------------------------------------------
-- Driver: run the demos in sequence on the tempo clock, letting each
-- demo's tails ring out before the next begins.

fn runDemos() Void {
    go(coro fn() Float {
        "== demo 1: music.pat groove -- melody + tresillo bass + pads ==" println;
        setTempo(0, 112.0);
        demo1();
        yield 34.0;
        stopAll();
        yield 2.0;

        "== demo 2: music.media canon -- theme, retrograde, drone ==" println;
        setTempo(0, 96.0);
        let len2 = demo2();
        yield (len2 + 4.0);
        stopAll();
        yield 2.0;

        "== demo 3: music.spans in Bohlen-Pierce -- euclidean bells ==" println;
        setTempo(0, 116.0);
        let len3 = demo3();
        yield (len3 + 6.0);
        stopAll();
        "demos done -- Ctrl-C to quit" println;
    }());
}

runDemos();

"music_fx_demos loaded -- three demos running on the clock" println;
