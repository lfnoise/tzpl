-- music_fx_demos.x -- three demonstrations tying the music, instruments,
-- and effects libraries together (about 3.5 minutes end to end).
--
-- One persistent node graph: three instruments from lang/modules/instruments.x
-- (wtLead, ksPluck, resonBank), each through a gain trim into its own effect
-- chain from lang/modules/effects.x, all chains meeting at a shared reverb
-- bus (the engine splices fan-in mixers automatically):
--
--     wtLead(101)    -> gain(211) -> fxChorus(201) -> fxEcho(202) --\
--     ksPluck12(102) -> gain(212) -> fxPingPong(203) ---------------+-> fxReverb(205) -> out
--     resonBank(103) -> gain(213) -> fxPhaser(204) -----------------/
--
-- When this file is evaluated in the app (GUI), it also builds live
-- controls: a "mixer" panel with one level slider per chain and an output
-- meter, plus one spec-derived panel per instrument and per effect
-- (controls(node) materializes every declared control as a correctly
-- ranged widget). Drag while the demos play; note that each demo re-dials
-- the instrument/effect panels at its start. Headless runs are unaffected
-- (the widgets are just never shown).
--
-- Three demos then play different music through that graph, one per
-- composition dialect, each a multi-section arrangement with several
-- simultaneous parts and beat-scheduled effect-control moves:
--
--   1. music.pat    -- a 128-beat groove in four 32-beat sections: the
--                      weighted-random melody, tresillo bass, and pad triads
--                      open low (A), climb an octave and busier (B), thin to
--                      a breakdown with the filter closed and long echoes (C),
--                      and reprise the opening with the filter open and a
--                      fourth pad voice (D). Panel moves land at every
--                      section boundary.
--   2. music.media  -- a 104-beat canon in six sections built from ONE
--                      six-beat theme by the temporal-media algebra:
--                      exposition (theme against its retrograde), canon at
--                      the fourth below two beats late, the theme INVERTED
--                      on the bells, a stretto with entries one beat apart,
--                      a recap of the canon, and a coda of rung chords.
--   3. music.spans  -- 128 beats of Bohlen-Pierce (13 divisions of the
--                      tritave) in four 8-cycle sections: the euclidean
--                      bass thickens 5-in-13 -> 6 -> 3 -> 7, the bell
--                      figures rotate, reverse, thin out, then double speed,
--                      and the offbeat stabs drop away for the sparse third
--                      section and pile up for the last; the phaser and
--                      delays deepen section by section.
--
-- Run interactively (Ctrl-C to stop; the driver stops each demo itself):
--   ./build/app/tzpl_app --nogui --wait -I lang/modules -I bridge/modules \
--       examples/music_fx_demos.x

import synthdef.*;
import synthc.compile.*;
import wavetables.*;
import common_ugens.*;
import instruments.*;
import effects.*;
import clock.*;
import audio_engine.*;
import music.pat.*;
import music.media.*;
import music.spans.*;
import music.play.*;
import ui.*;

---------------------------------------------------------------------------
-- A per-chain gain trim: stereo in, one smoothed level control, out.
-- The three instances are the demo's mixer.

fn fxGain() S {
	let x = inlet(FLOAT32, 2);
	let lvl = control("level", ControlSpec { lo: 0.0, hi: 2.0, init: 1.0,
	                                         warp: ControlWarp.linear }) lag(0.02);
	(x * lvl) outlet
}

---------------------------------------------------------------------------
-- Compile the instrument and effect defs (async; await before first play).

let cGain   = fxGain defSynthX("fxGain");
let cLead   = wtLead() defSynthX("wtLead");
let cPluck  = ksPluck(12) defSynthX("ksPluck12");
let cBank   = resonBank() defSynthX("resonBank");
let cChorus = fxChorus defSynthX("fxChorus");
let cEcho   = fxEcho defSynthX("fxEcho");
let cPong   = fxPingPong defSynthX("fxPingPong");
let cPhase  = fxPhaser defSynthX("fxPhaser");
let cVerb   = fxReverb defSynthX("fxReverb");
await cGain; await cLead; await cPluck; await cBank;
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
const kGainLead  = 211; -- per-chain level trims (the mixer)
const kGainPluck = 212;
const kGainBank  = 213;

engineStart();
masterGain(0.3);   -- the pad trim takes the mix down; make it back up here

begin();
newNode("wtLead", kLead);
fillBuffer(kLead, 0, 1, sawTables(1.0));    -- the bank the oscillator reads
newNode("ksPluck12", kPluck);
newNode("resonBank", kBank);
newNode("fxGain", kGainLead);
newNode("fxGain", kGainPluck);
newNode("fxGain", kGainBank);
newNode("fxChorus", kChorus);
newNode("fxEcho", kEcho);
newNode("fxPingPong", kPong);
newNode("fxPhaser", kPhase);
newNode("fxReverb", kVerb);
-- lead chain: trim, chorus thickens, echo repeats
connect(kLead, 0, kGainLead, 0);
connect(kGainLead, 0, kChorus, 0);
connect(kChorus, 0, kEcho, 0);
connect(kEcho, 0, kVerb, 0);
-- pluck chain: trim, stereo ping-pong echoes
connect(kPluck, 0, kGainPluck, 0);
connect(kGainPluck, 0, kPong, 0);
connect(kPong, 0, kVerb, 0);
-- pad chain: trim, swept phaser
connect(kBank, 0, kGainBank, 0);
connect(kGainBank, 0, kPhase, 0);
connect(kPhase, 0, kVerb, 0);
-- everything meets at the reverb, reverb to the speakers
connect(kVerb, 0, 0, 0);
-- starting balance: the sustained noise-driven pads dominate at unity and
-- the low strings vanish, so cut the pads hard and boost the bass; the
-- mixer sliders trim from here
setControl(kGainLead, 0, 0.9);
setControl(kGainPluck, 0, 1.8);
setControl(kGainBank, 0, 0.4);
sched(0);

-- The instruments declare [freq, amp] note params -- freqVoice's layout.
let leadV  = freqVoice(kLead, 8);
let pluckV = freqVoice(kPluck, 12);
let bankV  = freqVoice(kBank, 8);

-- Anchor degree 0 an octave below A440 so basses have room underneath.
let keyA = transposeRoot(et12, -12.0);

---------------------------------------------------------------------------
-- Live controls. In the app GUI this builds a "mixer" panel (per-chain
-- level sliders bound straight to the gain trims, plus an output meter on
-- the reverb bus) and one spec-derived panel per instrument and effect --
-- controls(node) turns every control the def declares into a correctly
-- ranged, engine-bound widget. Headless, the widgets simply never render.

panel("mixer");
slider("lead", ControlSpec { lo: 0.0, hi: 2.0, init: 0.9, warp: ControlWarp.linear })
	bindControl(kGainLead, "level");
slider("bass", ControlSpec { lo: 0.0, hi: 2.0, init: 1.8, warp: ControlWarp.linear })
	bindControl(kGainPluck, "level");
slider("pads", ControlSpec { lo: 0.0, hi: 2.0, init: 0.4, warp: ControlWarp.linear })
	bindControl(kGainBank, "level");
meter("mix", kVerb);
panel("");

-- timbre panels, one per def: wtLead's envelopes/filter, ksPluck's
-- pluck/decay/damp, resonBank's attack/release/decay, and each effect
controls(kLead);
controls(kPluck);
controls(kBank);
controls(kChorus);
controls(kEcho);
controls(kPong);
controls(kPhase);
controls(kVerb);

---------------------------------------------------------------------------
-- Shared section plumbing: four event streams played back to back, each
-- cut to `span` beats and shifted onto its section start.

fn seq4(a List<Event>, b List<Event>, c List<Event>, d List<Event>,
        span Float) List<Event> =
    cat(a takeDur(span),
    cat((b takeDur(span)) offset(span),
    cat((c takeDur(span)) offset(2.0 * span),
        (d takeDur(span)) offset(3.0 * span))));

---------------------------------------------------------------------------
-- Demo 1: music.pat -- a four-section groove in A dorian (128 beats).

fn padLayer(roots List<Int>, off Int, dur Float, amp Float) List<Event> =
    bind(roots map(fn(d Int) Int { d + off }) degree,
         List(dur) cyc,
         List(amp) cyc,
         List(0.95) cyc);

-- a triad every `dur` beats: the same root stream stacked at 0 / +2 / +4
fn padTriads(roots List<Int>, dur Float, amp Float) List<Event> =
    merge(padLayer(roots, 0, dur, amp),
          merge(padLayer(roots, 2, dur, amp), padLayer(roots, 4, dur, amp)));

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
    setControl(kPluck, 2, 0.4);     -- damp: warm but with audible harmonics
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

    -- A (0-32): weighted random degrees with breathing rests, cycled
    -- durations that cross the bar, brownian dynamics
    let melodyA = bind(
        wpicks([degree(0), degree(1), degree(2), degree(3),
                degree(4), degree(6), rest()],
               [3.0, 2.0, 3.0, 1.5, 2.5, 1.0, 2.0]),
        List(0.5, 0.25, 0.25, 0.5, 0.5, 1.0) cyc,
        browns(0.25, 0.6, 0.08),
        List(0.85) cyc);
    let bassA = bind(
        List(-14, -14, -10, -14, -14, -11) cyc degree,
        List(0.75, 0.75, 0.5) cyc,
        List(0.6) cyc,
        List(0.9) cyc);
    let padsA = padTriads(List(0, -1, 1, -3) cyc, 4.0, 0.26);

    -- B (32-64): the melody climbs an octave and gets busier, the bass
    -- keeps the tresillo but walks, the pads move to new roots
    let melodyB = bind(
        wpicks([degree(7), degree(8), degree(9), degree(11),
                degree(6), degree(4), rest()],
               [3.0, 2.0, 2.5, 1.5, 2.0, 1.5, 1.0]),
        List(0.25, 0.25, 0.5, 0.25, 0.25, 0.5) cyc,
        browns(0.3, 0.65, 0.08),
        List(0.8) cyc);
    let bassB = bind(
        List(-14, -12, -11, -10, -11, -12) cyc degree,
        List(0.75, 0.75, 0.5) cyc,
        List(0.65) cyc,
        List(0.9) cyc);
    let padsB = padTriads(List(3, 2, -1, 0) cyc, 4.0, 0.26);

    -- C (64-96): breakdown -- the melody mostly rests, the bass halves its
    -- density and sinks, the pads stretch to eight-beat dyad-like swells
    let melodyC = bind(
        wpicks([degree(4), degree(2), degree(1), rest(), rest()],
               [2.0, 2.0, 1.5, 3.0, 2.0]),
        List(1.0, 0.5, 1.5) cyc,
        browns(0.2, 0.45, 0.06),
        List(0.9) cyc);
    let bassC = bind(
        List(-14, -17, -14, -16) cyc degree,
        List(1.5, 1.5, 1.0) cyc,
        List(0.55) cyc,
        List(0.95) cyc);
    let padsC = padTriads(List(-3, -4) cyc, 8.0, 0.22);

    -- D (96-128): reprise of A (the same lazy streams re-read from their
    -- heads replay the same take), with a fourth pad voice on top
    let padsD = merge(padsA, padLayer(List(0, -1, 1, -3) cyc, 6, 4.0, 0.2));

    play(seq4(melodyA, melodyB, melodyC, melodyA, 32.0), leadV, keyA, dorian);
    play(seq4(bassA, bassB, bassC, bassA, 32.0), pluckV, keyA, dorian);
    play(seq4(padsA, padsB, padsC, padsD, 32.0), bankV, keyA, dorian);

    -- panel moves at the section boundaries, beat-scheduled on the same
    -- clock as the players: brighter into B, closed and cavernous for the
    -- breakdown, wide open with tight echoes for the reprise
    let b0 = getBeats() + getLatency();
    bundle()
        setControl(kLead, 4, 1600.0)
        setControl(kEcho, 1, 0.45)
        sched(0, 0, b0 + 32.0);
    bundle()
        setControl(kLead, 4, 500.0)
        setControl(kEcho, 0, 0.6)
        setControl(kVerb, 0, 6.0)
        sched(0, 0, b0 + 64.0);
    bundle()
        setControl(kLead, 4, 2400.0)
        setControl(kEcho, 0, 0.4)
        setControl(kEcho, 1, 0.55)
        setControl(kVerb, 0, 3.0)
        sched(0, 0, b0 + 96.0);
}

---------------------------------------------------------------------------
-- Demo 2: music.media -- a six-section canon in A minor (104 beats), all
-- of it grown from one six-beat theme by the temporal-media algebra.

fn mdeg(d Int, dur Float) Music<Pitch> = note(dur, degree(d));

-- melodic inversion about degree 2 (the theme spans degrees 0..5, so the
-- mirror stays inside one octave); non-degree pitches pass through
fn invDeg(p Pitch) Pitch {
    match (p) {
        Pitch.degree(d): Pitch.degree(4 - d.0, d.1);
        _: p;
    }
}

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
    let themeR = theme retro;
    let themeInv = theme mmap(invDeg);
    let expo = theme $ themeR;                        -- palindrome, 12 beats

    -- Sections (beats):  1 exposition 12 | 2 canon 24 | 3 inversion 18 |
    --                    4 stretto 18    | 5 recap 18 | 6 coda 14  = 104

    -- plucked strings: the leading voice throughout
    let pluckPart =
        expo                                              -- 1: states the palindrome
        $ (expo $ (expo trans(1)))                        -- 2: dux, then a step up
        $ (theme tempo(2.0) dyn(0.55) times(6))           -- 3: murmured under the bells
        $ (theme $ (theme trans(1)) $ (theme trans(-1)))  -- 4: stretto lead
        $ (expo $ (theme trans(2) dyn(1.1)))              -- 5: recap + rising close
        $ (chord([mdeg(0, 4.0), mdeg(2, 4.0), mdeg(4, 4.0)])         -- 6: rung chords
           $ chord([mdeg(-1, 4.0), mdeg(1, 4.0), mdeg(4, 4.0)])
           $ (chord([mdeg(0, 6.0), mdeg(2, 6.0), mdeg(4, 6.0), mdeg(7, 6.0)]) dyn(1.1)));

    -- filter bank: drone, canon answer, and the inverted theme as bells
    let bankPart =
        (mrest(6.0) $ (note(6.0, degree(-7)) dyn(0.4)))                  -- 1: drone enters
        $ (mrest(2.0) $ (expo trans(-3) dyn(0.65))                       -- 2: comes, a
           $ (theme trans(-3) dyn(0.6)) $ mrest(4.0))                    --    fourth down
        $ (((themeInv dyn(0.8)) $ (themeInv retro) $ (themeInv trans(2)))-- 3: INVERTED
           | (note(18.0, degree(-7)) dyn(0.35)))                         --    over drone
        $ (mrest(1.0) $ (theme trans(-3)) $ (theme trans(-2)) $ mrest(5.0)) -- 4: stretto mid
        $ ((mrest(2.0) $ (expo trans(-3) dyn(0.6)) $ mrest(4.0))         -- 5: recap answer
           | (note(18.0, degree(-14)) dyn(0.35)))                        --    over deep drone
        $ (mrest(2.0)                                                    -- 6: final swell
           $ (chord([note(12.0, degree(-7)), note(12.0, degree(-3))]) dyn(0.5)));

    -- lead: silent at first, then sparkle in the upper octave
    let leadPart =
        mrest(12.0)                                                      -- 1: tacet
        $ (mrest(12.0) $ (theme tempo(2.0) dyn(0.6) times(4)))           -- 2: late sparkle
        $ (theme tempo(2.0) trans(7) dyn(0.5) times(6))                  -- 3: high halo
        $ (mrest(2.0) $ (theme tempo(2.0) trans(4) times(2))             -- 4: stretto top
           $ mrest(1.0) $ (theme tempo(2.0) trans(7) times(3)))
        $ ((theme trans(7) dyn(0.6)) $ (themeR trans(7) dyn(0.55))       -- 5: recap up top
           $ (theme tempo(2.0) trans(7) times(2)))
        $ ((theme tempo(4.0) times(4)) $ mrest(8.0));                    -- 6: last flourish

    play(pluckPart perform, pluckV, keyA, minor);
    play(bankPart perform, bankV, keyA, minor);
    play(leadPart perform(0.0, 0.6), leadV, keyA, minor);
    pluckPart mdur
}

---------------------------------------------------------------------------
-- Demo 3: music.spans -- four 8-cycle sections of euclidean patterns in
-- Bohlen-Pierce (13ED3), 128 beats.

fn spd(x Int) Pattern<Pitch> = pure(degree(x));

-- render four patterns back to back, 8 cycles of 4 beats each
fn spanSeq4(a Pattern<Event>, b Pattern<Event>, c Pattern<Event>,
            d Pattern<Event>) List<Event> =
    cat(patEvents(a, 8.0, 4.0),
    cat((patEvents(b, 8.0, 4.0)) offset(32.0),
    cat((patEvents(c, 8.0, 4.0)) offset(64.0),
        (patEvents(d, 8.0, 4.0)) offset(96.0))));

fn demo3() Float {
    -- panel: glassy short lead stabs, tighter strings, bell-like bank,
    -- and delays that get pushed deeper section by section
    begin();
    setControl(kLead, 0, 0.002);
    setControl(kLead, 3, 0.15);     -- short release
    setControl(kLead, 4, 1800.0);
    setControl(kLead, 5, 3.0);
    setControl(kPluck, 1, 1.6);
    setControl(kPluck, 2, 0.3);
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
    -- is a degree, so a euclidean 13-grid walks the whole gamut
    let bpKey = bp() root(110.0, 0.0);
    let bpScale = chromatic(13);

    -- bass (plucked strings): the euclidean pulse thickens across the
    -- sections -- 5-in-13, walking 6-in-13, a sparse low 3, a full 7.
    -- Degrees sit around -6 (about 66 Hz from the 110 Hz root): a full
    -- tritave down (-13, under 37 Hz) is felt more than heard.
    let bassA = spd(-6) euclid(5, 13) events(0.7, 0.5);
    let bassB = fastcat([spd(-6), spd(-4)]) euclid(6, 13) events(0.7, 0.45);
    let bassC = spd(-8) euclid(3, 13) events(0.75, 0.7);
    let bassD = fastcat([spd(-6), spd(-2), spd(-4)]) euclid(7, 13, 1) events(0.7, 0.4);

    -- bells (filter bank): a rotating six-step figure that reverses on a
    -- cycle count, thins to a slow euclidean toll, then doubles speed
    let bellsA = [13, 16, 19, 22, 20, 17] spd fastcat
        iterp(6)
        every(3, fn(p Pattern<Pitch>) Pattern<Pitch> { p rev })
        events(0.45, 0.25)
        withAmp(sinePat() slow(3.0) range(0.18, 0.45) segment(6));
    let bellsB = [13, 17, 20, 24, 21, 18] spd fastcat
        iterp(3)
        every(2, fn(p Pattern<Pitch>) Pattern<Pitch> { p rev })
        events(0.45, 0.25)
        withAmp(sinePat() slow(2.0) range(0.2, 0.45) segment(6));
    let bellsC = [19, 22, 26] spd fastcat
        euclid(5, 8)
        events(0.5, 0.4)
        withAmp(sinePat() slow(6.0) range(0.15, 0.35) segment(4));
    let bellsD = [13, 16, 19, 22, 24, 20] spd fastcat
        fast(2.0)
        iterp(6)
        every(2, fn(p Pattern<Pitch>) Pattern<Pitch> { p rev })
        events(0.4, 0.2)
        withAmp(sinePat() slow(2.0) range(0.2, 0.5) segment(8));

    -- stabs (lead): offbeat hits at the top of the gamut that multiply,
    -- fall almost silent for the third section, and pile up for the last
    let stabsA = spd(26) euclid(3, 8, 1) late(0.125) events(0.3, 0.3);
    let stabsB = fastcat([spd(26), spd(23)]) euclid(4, 8, 1) late(0.125) events(0.3, 0.28);
    let stabsC = spd(30) euclid(1, 8, 4) events(0.5, 0.25);
    let stabsD = fastcat([spd(26), spd(28), spd(23)]) euclid(5, 8, 1) late(0.125) events(0.28, 0.3);

    play(spanSeq4(bassA, bassB, bassC, bassD), pluckV, bpKey, bpScale);
    play(spanSeq4(bellsA, bellsB, bellsC, bellsD), bankV, bpKey, bpScale);
    play(spanSeq4(stabsA, stabsB, stabsC, stabsD), leadV, bpKey, bpScale);

    -- deepen the effects at each section boundary: phaser bites, then the
    -- reverb yawns open for the sparse section, then tight and driven
    let b0 = getBeats() + getLatency();
    bundle()
        setControl(kPhase, 1, 0.5)
        sched(0, 0, b0 + 32.0);
    bundle()
        setControl(kVerb, 0, 8.0)
        setControl(kPong, 1, 0.6)
        sched(0, 0, b0 + 64.0);
    bundle()
        setControl(kVerb, 0, 4.0)
        setControl(kPhase, 0, 0.8)
        setControl(kEcho, 1, 0.55)
        sched(0, 0, b0 + 96.0);
    128.0
}

---------------------------------------------------------------------------
-- Driver: run the demos in sequence on the tempo clock, letting each
-- demo's tails ring out before the next begins.

fn runDemos() Void {
    go(coro fn() Float {
        "== demo 1: music.pat groove -- 128 beats, four sections ==" println;
        setTempo(0, 112.0);
        demo1();
        yield 130.0;
        stopAll();
        yield 2.0;

        "== demo 2: music.media canon -- 104 beats, six sections ==" println;
        setTempo(0, 96.0);
        let len2 = demo2();
        yield (len2 + 4.0);
        stopAll();
        yield 2.0;

        "== demo 3: music.spans in Bohlen-Pierce -- 128 beats, four sections ==" println;
        setTempo(0, 116.0);
        let len3 = demo3();
        yield (len3 + 6.0);
        stopAll();
        "demos done -- Ctrl-C to quit" println;
    }());
}

runDemos();

"music_fx_demos loaded -- three demos running on the clock" println;
