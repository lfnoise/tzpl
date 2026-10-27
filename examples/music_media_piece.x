-- music_media_piece.x -- a short, through-composed piece in A minor (about
-- 90 seconds at 96 bpm), written with the music.media algebra and played
-- on the instruments.x and effects.x libraries.
--
-- Draft 1. Everything is written out: one chord progression per section,
-- one melody, fixed accompaniment figures. Nothing is random.
--
-- Four parts, each kept to its own register:
--
--     bass   wtLead, dark saw    A1-A2   (degrees -14..-8)
--     pluck  ksPluck arpeggios   A2-E4   (degrees  -8..4)
--     pad    resonBank, bowed    B3-A4   (degrees   1..7)
--     lead   wtLead melody       A4-A5   (degrees   7..14)
--
-- Form (bars of 4 beats):
--
--     intro  8   the pluck alone for four bars, then the bass joins
--     A      8   the melody enters: Am F Dm E | Am F E Am
--     B      8   the pad enters, the pluck drops to quarter notes, an
--                answering melody: F C G Am | F C E E (half cadence)
--     A'     8   the melody returns over everything; the bass drives
--     coda   4   F E Am, the last chord held
--
-- Graph: the bass goes straight to the output, dry; the other three meet
-- at a shared reverb, the lead through an echo first.
--
--     wtLead(301) -> gain(311) -> fxEcho(321) -\
--     ksPluck(302) -> gain(312) ---------------+-> fxReverb(322) -> out
--     resonBank(303) -> gain(313) -------------/
--     wtLead(304) -> gain(314) -------------------------------------> out
--
-- In the app, a "mixer" panel has a level slider per part.
--
-- Run interactively:
--   ./build/app/tzpl_app --nogui --wait -I lang/modules -I bridge/modules \
--       examples/music_media_piece.x
-- or render it:
--   ./build/app/tzpl_app --nrt piece.wav -I lang/modules -I bridge/modules \
--       examples/music_media_piece.x

import synthdef.*;
import synthc.compile.*;
import wavetables.*;
import common_ugens.*;
import instruments.*;
import effects.*;
import clock.*;
import audio_engine.*;
import music.media.*;
import music.play.*;
import ui.*;

---------------------------------------------------------------------------
-- A per-part gain trim: stereo in, one smoothed level control, out.

fn partGain() S {
	let x = inlet(FLOAT32, 2);
	let lvl = control("level", ControlSpec { lo: 0.0, hi: 4.0, init: 1.0,
	                                         warp: ControlWarp.linear }) lag(0.02);
	(x * lvl) outlet
}

let cGain  = partGain defSynth("partGain");
let cLead  = wtLead() defSynth("wtLead");
let cPluck = ksPluck(12) defSynth("ksPluck12");
let cBank  = resonBank() defSynth("resonBank");
let cEcho  = fxEcho defSynth("fxEcho");
let cVerb  = fxReverb defSynth("fxReverb");
await cGain; await cLead; await cPluck; await cBank; await cEcho; await cVerb;

---------------------------------------------------------------------------
-- Engine and graph.

const kLead  = 301;
const kPluck = 302;
const kPad   = 303;
const kBass  = 304;
const kGLead  = 311;
const kGPluck = 312;
const kGPad   = 313;
const kGBass  = 314;
const kEcho  = 321;
const kVerb  = 322;

engineStart();
masterGain(0.8);

begin();
newNode("wtLead", kLead);
fillBuffer(kLead, 0, 1, sawTables(1.0));
newNode("wtLead", kBass);
fillBuffer(kBass, 0, 1, sawTables(1.0));
newNode("ksPluck12", kPluck);
newNode("resonBank", kPad);
newNode("partGain", kGLead);
newNode("partGain", kGPluck);
newNode("partGain", kGPad);
newNode("partGain", kGBass);
newNode("fxEcho", kEcho);
newNode("fxReverb", kVerb);
connect(kLead, 0, kGLead, 0);
connect(kGLead, 0, kEcho, 0);
connect(kEcho, 0, kVerb, 0);
connect(kPluck, 0, kGPluck, 0);
connect(kGPluck, 0, kVerb, 0);
connect(kPad, 0, kGPad, 0);
connect(kGPad, 0, kVerb, 0);
connect(kVerb, 0, 0, 0);
connect(kBass, 0, kGBass, 0);
connect(kGBass, 0, 0, 0);

-- lead: a soft attack, a gentle filter bloom on each note
setInput(kLead, "attack", 0.01);
setInput(kLead, "decay", 0.4);
setInput(kLead, "sustain", 0.6);
setInput(kLead, "release", 0.35);
setInput(kLead, "cutoff", 900.0);
setInput(kLead, "envOct", 2.0);
setInput(kLead, "res", 0.5);
setInput(kLead, "fdecay", 0.3);
-- bass: a dark, round saw with a short filter blip
setInput(kBass, "attack", 0.005);
setInput(kBass, "decay", 0.3);
setInput(kBass, "sustain", 0.8);
setInput(kBass, "release", 0.15);
setInput(kBass, "cutoff", 220.0);
setInput(kBass, "envOct", 1.5);
setInput(kBass, "res", 0.7);
setInput(kBass, "fdecay", 0.15);
-- pluck: a bright string with a medium ring, harp-like under the arpeggio
setInput(kPluck, "decay", 2.5);
setInput(kPluck, "damp", 0.25);
-- pad: bowed, slow in and out
setInput(kPad, "attack", 0.6);
setInput(kPad, "release", 1.0);
setInput(kPad, "decay", 3.0);
-- echo a dotted eighth at 96 bpm, kept low; a medium room
setInput(kEcho, "time", 0.469);
setInput(kEcho, "feedback", 0.25);
setInput(kEcho, "mix", 0.18);
setInput(kVerb, "decay", 2.5);
setInput(kVerb, "mix", 0.22);
-- draft balance, from solo renders: pluck 3 dB under the bass, lead 1 dB
-- under, pad 6 dB under (RMS while each part plays)
setInput(kGLead, "level", 1.2);
setInput(kGPluck, "level", 2.0);
setInput(kGPad, "level", 0.27);
setInput(kGBass, "level", 0.76);
sched(0);

let leadV  = freqVoice(kLead, 8);
let bassV  = freqVoice(kBass, 8);
let pluckV = freqVoice(kPluck, 12);
let padV   = freqVoice(kPad, 8);

-- degree 0 is A3 (220 Hz)
let keyA = transposeRoot(et12, -12.0);

panel("mixer");
slider("lead", ControlSpec { lo: 0.0, hi: 4.0, init: 1.2, warp: ControlWarp.linear })
	bindInput(kGLead, "level");
slider("pluck", ControlSpec { lo: 0.0, hi: 4.0, init: 2.0, warp: ControlWarp.linear })
	bindInput(kGPluck, "level");
slider("pad", ControlSpec { lo: 0.0, hi: 4.0, init: 0.27, warp: ControlWarp.linear })
	bindInput(kGPad, "level");
slider("bass", ControlSpec { lo: 0.0, hi: 4.0, init: 0.76, warp: ControlWarp.linear })
	bindInput(kGBass, "level");
meter("reverb bus", kVerb);
panel("");

---------------------------------------------------------------------------
-- Harmony. Degrees are in A natural minor (0 = A3); the E chord raises
-- its third to G#.

struct Chord {
    bass Int,       -- root in the bass register
    arp [Pitch],    -- four pluck tones, low to high
    pad [Pitch],    -- three pad tones
}

fn dg(d Int) Pitch = degree(d);
fn sh(d Int) Pitch = degree(d, 1.0);

let chAm = Chord { bass: -14, arp: [dg(-7), dg(-5), dg(-3), dg(0)], pad: [dg(2), dg(4), dg(7)] };
let chF  = Chord { bass: -9,  arp: [dg(-5), dg(-2), dg(0), dg(2)],  pad: [dg(2), dg(5), dg(7)] };
let chDm = Chord { bass: -11, arp: [dg(-4), dg(-2), dg(0), dg(3)],  pad: [dg(3), dg(5), dg(7)] };
let chE  = Chord { bass: -10, arp: [dg(-3), sh(-1), dg(1), dg(4)],  pad: [dg(1), dg(4), sh(6)] };
let chC  = Chord { bass: -12, arp: [dg(-5), dg(-3), dg(-1), dg(2)], pad: [dg(2), dg(4), dg(6)] };
let chG  = Chord { bass: -8,  arp: [dg(-4), dg(-1), dg(1), dg(3)],  pad: [dg(1), dg(3), dg(6)] };

let progA = [chAm, chF, chDm, chE, chAm, chF, chE, chAm];
let progB = [chF, chC, chG, chAm, chF, chC, chE, chE];

-- one bar per chord, joined in order
fn bars(prog [Chord], bar (Chord) Music<Pitch>) Music<Pitch> {
    var m = mrest(0.0);
    for (c : prog) { m = m $ bar(c); }
    m
}

---------------------------------------------------------------------------
-- Accompaniment figures, one bar each.

let arpOrder = [0, 1, 2, 3, 2, 1, 2, 3];

fn arpEighths(c Chord) Music<Pitch> {
    var m = mrest(0.0);
    for (i : arpOrder) { m = m $ note(0.5, c.arp[i]); }
    m
}

fn arpQuarters(c Chord) Music<Pitch> =
    line([note(1.0, c.arp[0]), note(1.0, c.arp[2]),
          note(1.0, c.arp[1]), note(1.0, c.arp[3])]);

fn bassWhole(c Chord) Music<Pitch> = note(4.0, dg(c.bass));

fn bassHalves(c Chord) Music<Pitch> =
    note(2.0, dg(c.bass)) $ note(2.0, dg(c.bass + 4));

-- root, root on the and-of-two, fifth: the A' drive
fn bassDrive(c Chord) Music<Pitch> =
    line([note(1.5, dg(c.bass)), note(0.5, dg(c.bass)), note(2.0, dg(c.bass + 4))]);

fn padChord(c Chord, beats Float) Music<Pitch> =
    chord([note(beats, c.pad[0]), note(beats, c.pad[1]), note(beats, c.pad[2])]);

---------------------------------------------------------------------------
-- Melodies. Degree 7 is A4, 14 is A5.

fn n(d Int, beats Float) Music<Pitch> = note(beats, dg(d));
fn ns(d Int, beats Float) Music<Pitch> = note(beats, sh(d));

-- the motif: E C A C | F E C -- a falling third, then a step up to fall
-- again; bars 5-6 repeat it, bars 7-8 cadence through G# to A
let melA = line([
    n(11, 1.5), n(9, 0.5), n(7, 1.0), n(9, 1.0),        -- Am
    n(12, 1.5), n(11, 0.5), n(9, 2.0),                  -- F
    n(10, 1.5), n(12, 0.5), n(14, 1.0), n(12, 1.0),     -- Dm
    n(11, 1.5), n(10, 0.5), n(8, 1.0), mrest(1.0),      -- E
    n(11, 1.5), n(9, 0.5), n(7, 1.0), n(9, 1.0),        -- Am
    n(12, 1.5), n(11, 0.5), n(9, 2.0),                  -- F
    n(11, 1.0), n(8, 1.0), n(11, 1.0), ns(13, 1.0),     -- E
    n(14, 3.0), mrest(1.0)]);                           -- Am

-- the answer: longer notes, rising where A fell, ending on B over E
let melB = line([
    n(12, 2.0), n(11, 1.0), n(9, 1.0),                  -- F
    n(9, 1.0), n(11, 1.0), n(13, 2.0),                  -- C
    n(13, 1.5), n(12, 0.5), n(10, 2.0),                 -- G
    n(11, 4.0),                                         -- Am
    n(12, 1.5), n(14, 0.5), n(12, 1.0), n(11, 1.0),     -- F
    n(9, 2.0), n(11, 2.0),                              -- C
    ns(13, 2.0), n(11, 1.0), n(10, 1.0),                -- E
    n(8, 3.0), mrest(1.0)]);                            -- E

let melCoda = line([
    n(12, 1.5), n(11, 0.5), n(9, 2.0),                  -- F
    n(11, 1.0), n(10, 1.0), n(8, 2.0),                  -- E
    n(7, 8.0) dyn(0.8)]);                               -- Am

---------------------------------------------------------------------------
-- The parts, section by section: intro | A | B | A' | coda.

-- gallery-excerpt-begin
let leadPart =
    mrest(32.0)
    $ melA
    $ (melB dyn(0.9))
    $ melA
    $ melCoda;

let pluckPart =
    (bars(progA, arpEighths) dyn(0.85))
    $ bars(progA, arpEighths)
    $ (bars(progB, arpQuarters) dyn(0.8))
    $ bars(progA, arpEighths)
    $ bars([chF, chE], arpEighths)
    $ arpQuarters(chAm) $ mrest(4.0);

let bassPart =
    mrest(16.0) $ bars([chAm, chF, chE, chAm], bassWhole)
    $ bars(progA, bassWhole)
    $ bars(progB, bassHalves)
    $ bars(progA, bassDrive)
    $ bars([chF, chE], bassWhole) $ bassWhole(chAm) $ note(4.0, dg(-14));

let padPart =
    mrest(64.0)
    $ (bars(progB, fn(c Chord) Music<Pitch> { padChord(c, 4.0) }) dyn(0.7))
    $ (bars(progA, fn(c Chord) Music<Pitch> { padChord(c, 4.0) }) dyn(0.55))
    $ ((padChord(chF, 4.0) $ padChord(chE, 4.0) $ padChord(chAm, 8.0)) dyn(0.6));
-- gallery-excerpt-end

---------------------------------------------------------------------------
-- Play it on the clock; stop once the last chord has rung out.

fn runPiece() Void {
    go(coro fn() Float {
        setTempo(0, 96.0);
        play(bassPart perform(0.0, 0.95), bassV, keyA, minor);
        -- the strings are quiet for their peak level: play them at twice the
        -- default note amplitude
        play((pluckPart dyn(2.0)) perform, pluckV, keyA, minor);
        play(padPart perform(0.0, 1.0), padV, keyA, minor);
        play(leadPart perform(0.0, 0.92), leadV, keyA, minor);
        yield (pluckPart mdur + 6.0);
        stopAll();
        "piece done" println;
    }());
}

runPiece();
