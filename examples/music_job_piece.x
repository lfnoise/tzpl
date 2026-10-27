-- music_job_piece.x -- a dissonant, distorted piece whose FORM is chosen
-- at random each time it plays (32 bars, about 75 seconds at 108 bpm), written
-- with the music.job hierarchical forms over music.shape sections.
--
-- The notes are all written out. What varies is the structure: a
-- music.job Coll of four-bar section markers picks the order of the middle
-- sections, their transpositions (unison, a semitone, a tritone), the
-- climax variant, and how the piece winds down. Every run is a different
-- reading; set TZPL_LANG_SEED to repeat one. The chosen form is printed.
--
-- The sections (16 beats each), in E, chromatic:
--
--     drone     bass pedal, a semitone-and-tritone cluster on the pad,
--               a few plucked notes
--     grind     a distorted bass riff in 3+3+2 sixteenths, developed over
--               four bars, with offbeat tritone stabs
--     machine   a 3-note pluck ostinato in sixteenths against the 4/4 bar,
--               the bass leaping octaves and tritones as it sinks by
--               semitones, a slow lead line
--     drift     clusters on the pad, high harmonics, a quiet lead
--     scream    everything at once, the lead heavily driven
--     scream'   the same with the lead inverted and the bass line reversed
--     collapse  the riff at half speed, a last ringing tritone
--
-- A small drum kit (drumKit, below) plays in every section but drift: two
-- distant kicks in the drone, kick patterns that follow the bass in grind
-- and scream, hats in dotted eighths against the bar in machine, and fills
-- into the last bar of grind and scream. scream' moves the snare off the
-- beat.
--
-- Form (a music.job Coll):
--
--     drone
--     3 x shuffled { grind (0, +1 or +6) | machine (0 or -2) | drift }
--     one of { scream | scream' }               (weighted 60/40)
--     one of { 2 x { grind | machine +6 } | drift +6, scream +1 }
--     collapse
--
-- Graph: the bass and the drums are distorted and stay dry; the lead is
-- distorted and echoed; the pluck, pad and lead meet at a shared reverb.
--
--     wtLead(401)    -> gain(411) -> fxDistortion(421) -> fxEcho(422) -\
--     ksPluck(402)   -> gain(412) -> fxFilter(425), low-pass ----------+-> fxReverb(424) -> out
--     resonBank(403) -> gain(413) ------------------------------------/
--     wtLead(404)    -> gain(414) -> fxDistortion(423) ---------------------------------> out
--     drumKit(405)   -> gain(415) -> fxDistortion(426) ---------------------------------> out
--
-- Run interactively:
--   ./build/app/tzpl_app --nogui --wait -I lang/modules -I bridge/modules \
--       examples/music_job_piece.x
-- or render it:
--   ./build/app/tzpl_app --nrt piece.wav -I lang/modules -I bridge/modules \
--       examples/music_job_piece.x

import synthdef.*;
import synthc.compile.*;
import wavetables.*;
import common_ugens.*;
import filters.*;
import instruments.*;
import effects.*;
import clock.*;
import audio_engine.*;
import music.job.*;
import music.shape.{Shape};   -- synthdef also defines a Shape
import music.play.*;
import ui.*;

---------------------------------------------------------------------------
-- A per-part gain trim: stereo in, one smoothed level control, out.

fn partGain() S {
	let x = inlet(FLOAT32, 2);
	let lvl = control("level", ControlSpec { lo: 0.0, hi: 12.0, init: 1.0,
	                                         warp: ControlWarp.linear }) lag(0.02);
	(x * lvl) outlet
}

-- A small drum kit: one voicer whose per-note params choose the drum. A
-- sine body falling `sweep` octaves (the kick and tom thump) blends into
-- high-passed noise (snare and hats) by `noise`; both decay over `decay`
-- seconds.
fn drumKit() S {
	voicer(12, fn() S {
		let f = noteFreq();
		let a = noteAmp();
		let noise = noteParam("noise", ControlSpec { lo: 0.0, hi: 1.0, init: 0.0,
		                                             warp: ControlWarp.linear });
		let dcy = noteParam("decay", ControlSpec { lo: 0.01, hi: 2.0, init: 0.2,
		                                           warp: ControlWarp.exponential }) max(0.01);
		let sweep = noteParam("sweep", ControlSpec { lo: 0.0, hi: 6.0, init: 0.0,
		                                             warp: ControlWarp.linear });
		let ncut = noteParam("ncut", ControlSpec { lo: 100.0, hi: 15000.0, init: 2000.0,
		                                           warp: ControlWarp.exponential }) max(100.0);
		let t = gate() tr;
		let body = sinosc(f * (sweep * t decay2(0.0005, 0.04)) exp2) * (1.0 - noise);
		let hiss = white() hpf(ncut) * noise;
		(body + hiss) * t decay2(0.001, dcy) * a
	}) sum outlet
}

let cGain  = partGain defSynth("partGain");
let cDrums = drumKit defSynth("drumKit");
let cLead  = wtLead() defSynth("wtLead");
let cPluck = ksPluck(12) defSynth("ksPluck12");
let cBank  = resonBank() defSynth("resonBank");
let cDist  = fxDistortion defSynth("fxDistortion");
let cFilt  = fxFilter defSynth("fxFilter");
let cEcho  = fxEcho defSynth("fxEcho");
let cVerb  = fxReverb defSynth("fxReverb");
await cGain; await cDrums; await cLead; await cPluck; await cBank;
await cDist; await cFilt; await cEcho; await cVerb;

---------------------------------------------------------------------------
-- Engine and graph.

const kLead  = 401;
const kPluck = 402;
const kPad   = 403;
const kBass  = 404;
const kGLead  = 411;
const kGPluck = 412;
const kGPad   = 413;
const kGBass  = 414;
const kLeadDist = 421;
const kEcho     = 422;
const kBassDist = 423;
const kVerb     = 424;
const kPluckLP   = 425;
const kDrumDist  = 426;
const kDrums  = 405;
const kGDrums = 415;

engineStart();
masterGain(0.7);

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
newNode("fxDistortion", kLeadDist);
newNode("fxEcho", kEcho);
newNode("fxDistortion", kBassDist);
newNode("fxReverb", kVerb);
newNode("fxFilter", kPluckLP);
newNode("drumKit", kDrums);
newNode("partGain", kGDrums);
newNode("fxDistortion", kDrumDist);
connect(kLead, 0, kGLead, 0);
connect(kGLead, 0, kLeadDist, 0);
connect(kLeadDist, 0, kEcho, 0);
connect(kEcho, 0, kVerb, 0);
connect(kPluck, 0, kGPluck, 0);
connect(kGPluck, 0, kPluckLP, 0);
connect(kPluckLP, 0, kVerb, 0);
connect(kDrums, 0, kGDrums, 0);
connect(kGDrums, 0, kDrumDist, 0);
connect(kDrumDist, 0, 0, 0);
connect(kPad, 0, kGPad, 0);
connect(kGPad, 0, kVerb, 0);
connect(kVerb, 0, 0, 0);
connect(kBass, 0, kGBass, 0);
connect(kGBass, 0, kBassDist, 0);
connect(kBassDist, 0, 0, 0);

-- lead: a resonant, nasal saw, held notes
setInput(kLead, "attack", 0.02);
setInput(kLead, "decay", 0.5);
setInput(kLead, "sustain", 0.7);
setInput(kLead, "release", 0.4);
setInput(kLead, "cutoff", 1200.0);
setInput(kLead, "envOct", 1.5);
setInput(kLead, "res", 0.25);
setInput(kLead, "fdecay", 0.6);
-- bass: a saw with a hard filter snap, for the distortion to bite on
setInput(kBass, "attack", 0.003);
setInput(kBass, "decay", 0.25);
setInput(kBass, "sustain", 0.6);
setInput(kBass, "release", 0.08);
setInput(kBass, "cutoff", 300.0);
setInput(kBass, "envOct", 3.0);
setInput(kBass, "res", 0.4);
setInput(kBass, "fdecay", 0.12);
-- pluck: a softer, darker string, then a low-pass to take the edge off
setInput(kPluck, "pluck", 0.006);
setInput(kPluck, "decay", 1.2);
setInput(kPluck, "damp", 0.45);
setInput(kPluckLP, "type", 0.0);         -- low-pass
setInput(kPluckLP, "freq", 1800.0);
setInput(kPluckLP, "rq", 0.9);
-- pad: bowed clusters, slow in and out
setInput(kPad, "attack", 1.0);
setInput(kPad, "release", 1.5);
setInput(kPad, "decay", 4.0);
-- distortion tone: dark on the bass, open on the lead
setInput(kBassDist, "tone", 0.3);
setInput(kLeadDist, "tone", 0.6);
-- echo a dotted eighth at 108 bpm
setInput(kEcho, "time", 0.417);
setInput(kEcho, "feedback", 0.35);
setInput(kEcho, "mix", 0.25);
setInput(kVerb, "decay", 3.0);
setInput(kVerb, "mix", 0.25);
setInput(kGLead, "level", 1.0);
-- the plucked strings, filtered, are about 18 dB quieter than the other defs
setInput(kGPluck, "level", 8.0);
setInput(kGPad, "level", 0.4);
setInput(kGBass, "level", 1.0);
setInput(kGDrums, "level", 1.0);
-- drums: a little grit, no reverb
setInput(kDrumDist, "drive", 8.0);
setInput(kDrumDist, "tone", 0.5);
setInput(kDrumDist, "level", 0.5);
sched(0);

let leadV  = freqVoice(kLead, 8);
let bassV  = freqVoice(kBass, 8);
let pluckV = freqVoice(kPluck, 12);
let padV   = freqVoice(kPad, 8);
let drumV  = voice(kDrums, ['freq, 'amp, 'noise, 'decay, 'sweep, 'ncut],
                    [100.0, 0.5, 0.0, 0.2, 0.0, 2000.0], 12);

-- degree 0 is E3 (165 Hz); the scale is all twelve semitones
let keyE = transposeRoot(et12, -17.0);
let chroma = chromatic(12);

panel("mixer");
slider("lead", ControlSpec { lo: 0.0, hi: 8.0, init: 1.0, warp: ControlWarp.linear })
	bindInput(kGLead, "level");
slider("pluck", ControlSpec { lo: 0.0, hi: 12.0, init: 8.0, warp: ControlWarp.linear })
	bindInput(kGPluck, "level");
slider("pad", ControlSpec { lo: 0.0, hi: 8.0, init: 0.4, warp: ControlWarp.linear })
	bindInput(kGPad, "level");
slider("bass", ControlSpec { lo: 0.0, hi: 8.0, init: 1.0, warp: ControlWarp.linear })
	bindInput(kGBass, "level");
slider("drums", ControlSpec { lo: 0.0, hi: 12.0, init: 1.0, warp: ControlWarp.linear })
	bindInput(kGDrums, "level");
meter("reverb bus", kVerb);
panel("");

---------------------------------------------------------------------------
-- Sections. Each part is a list of shapes played together (one per voice
-- of a chord); an empty list is tacet. Shape frames are [dur, degree, amp]
-- in beats and semitones above E3; amp 0 is a rest.

struct Section {
    name String,
    bass [Shape],
    pluck [Shape],
    pad [Shape],
    lead [Shape],
    drums [Shape],
    bassDrive Float,    -- fxDistortion drive at the section's start
    bassLevel Float,    -- and its output level, which falls as drive rises
    leadDrive Float,
    leadLevel Float,
    verbMix Float,
}

fn cell(rows [[Float]]) Shape = shape(rows);

fn loopShape(sh Shape, n Int) Shape {
    var s = sh;
    var i = 1;
    while (i < n) { s = catShapes(s, sh); i = i + 1; }
    s
}

-- one held shape per chord tone
fn held(beats Float, degs [Float], amp Float) [Shape] {
    var out = [Shape]();
    for (d : degs) { out push!(cell([[beats, d, amp]])); }
    out
}

-- the riff: E E F E Bb A, one bar of 3+3+2 sixteenths, twice
let riff = cell([[0.75, -24.0, 0.8], [0.75, -24.0, 0.6], [0.5, -23.0, 0.7],
                 [0.75, -24.0, 0.8], [0.75, -18.0, 0.7], [0.5, -19.0, 0.6]]);

-- grind's bass develops the riff over four bars: as written; ending a
-- semitone low; answered a tritone up; broken into a run
let grindBass = cell([
    [0.75, -24.0, 0.8], [0.75, -24.0, 0.6], [0.5, -23.0, 0.7],
    [0.75, -24.0, 0.8], [0.75, -18.0, 0.7], [0.5, -19.0, 0.6],
    [0.75, -24.0, 0.8], [0.75, -24.0, 0.6], [0.5, -23.0, 0.7],
    [0.75, -24.0, 0.8], [0.75, -25.0, 0.7], [0.5, -26.0, 0.6],
    [0.75, -18.0, 0.8], [0.75, -18.0, 0.6], [0.5, -17.0, 0.7],
    [0.75, -18.0, 0.8], [0.75, -12.0, 0.7], [0.5, -13.0, 0.6],
    [0.5, -24.0, 0.9], [0.5, 0.0, 0.0], [0.25, -24.0, 0.7], [0.25, -23.0, 0.7],
    [0.25, -21.0, 0.7], [0.25, -20.0, 0.7], [0.5, -19.0, 0.8], [0.5, -18.0, 0.8],
    [1.0, 0.0, 0.0]]);

-- machine's bass: root, octave, tritone in dotted quarters, sinking a
-- semitone each bar
let machineBass = cell([
    [1.5, -24.0, 0.7], [1.5, -12.0, 0.5], [1.0, -18.0, 0.6],
    [1.5, -25.0, 0.7], [1.5, -13.0, 0.5], [1.0, -19.0, 0.6],
    [1.5, -26.0, 0.7], [1.5, -14.0, 0.5], [1.0, -20.0, 0.6],
    [1.5, -27.0, 0.7], [1.5, -15.0, 0.5], [1.0, -21.0, 0.6]]);

-- scream's bass: syncopated octave leaps, a bar on F, a falling run
let screamBass = cell([
    [1.0, -24.0, 0.9], [0.5, -12.0, 0.7], [0.5, -13.0, 0.7], [1.0, -24.0, 0.9], [1.0, -18.0, 0.8],
    [1.0, -24.0, 0.9], [0.5, -12.0, 0.7], [0.5, -13.0, 0.7], [0.5, -24.0, 0.9], [0.5, -23.0, 0.8],
    [1.0, -17.0, 0.8],
    [1.5, -23.0, 0.9], [0.5, -11.0, 0.7], [1.0, -23.0, 0.8], [1.0, -18.0, 0.8],
    [0.5, -12.0, 0.8], [0.5, -13.0, 0.8], [0.5, -15.0, 0.8], [0.5, -16.0, 0.8],
    [0.5, -18.0, 0.8], [0.5, -19.0, 0.8], [1.0, -24.0, 0.9]]);

-- offbeat tritone stabs, F and B, one bar
let stabLo = cell([[1.5, 0.0, 0.0], [0.5, 1.0, 0.7], [1.0, 0.0, 0.0], [1.0, 1.0, 0.6]]);
let stabHi = cell([[1.5, 0.0, 0.0], [0.5, 7.0, 0.7], [1.0, 0.0, 0.0], [1.0, 7.0, 0.6]]);

-- three sixteenths against four: E Bb F, accented on the first
let ostinato = cell([[0.25, 0.0, 0.8], [0.25, 6.0, 0.4], [0.25, 1.0, 0.5]]);

let machineLead = cell([[3.0, 13.0, 0.5], [1.0, 12.0, 0.45], [4.0, 19.0, 0.5],
                        [2.0, 18.0, 0.5], [2.0, 17.0, 0.45], [4.0, 13.0, 0.5]]);

let screamLead = cell([[1.0, 12.0, 0.7], [1.0, 13.0, 0.7], [2.0, 18.0, 0.8],
                       [0.5, 19.0, 0.8], [0.5, 18.0, 0.7], [1.0, 13.0, 0.7],
                       [4.0, 24.0, 0.9], [2.0, 25.0, 0.8], [2.0, 18.0, 0.7],
                       [2.0, 12.0, 0.6]]);

-- Drums. Rows are [dur, hz, amp, noise, decay, sweep, ncut]; each drum is
-- the [hz, noise, decay, sweep, ncut] part.
let drumDims = ['dur, 'hz, 'amp, 'noise, 'decay, 'sweep, 'ncut];
let drKick    = [48.0, 0.08, 0.4, 2.5, 4000.0];
let drSnare   = [180.0, 0.7, 0.16, 0.7, 1200.0];
let drHat     = [300.0, 1.0, 0.045, 0.0, 7000.0];
let drOpenHat = [300.0, 1.0, 0.25, 0.0, 6000.0];
let drTom     = [95.0, 0.15, 0.3, 1.0, 2000.0];

-- one drum at the given onsets within `len` beats; amps cycle
fn drumHits(drum [Float], onsets [Float], amps [Float], len Float) Shape {
    var rows = [[Float]]();
    if (onsets[0] > 0.0) { rows push!([onsets[0], 100.0, 0.0, 0.0, 0.1, 0.0, 1000.0]); }
    var i = 0;
    while (i < onsets length) {
        let next = i + 1 < onsets length ? onsets[i + 1] : len;
        rows push!([next - onsets[i], drum[0], amps[i % amps length],
                    drum[1], drum[2], drum[3], drum[4]]);
        i = i + 1;
    }
    shape(rows, drumDims)
}

-- the same one-bar pattern n times
fn drumBars(drum [Float], onsets [Float], amps [Float], n Int) Shape =
    loopShape(drumHits(drum, onsets, amps, 4.0), n);

-- onsets every `step` beats up to `len`
fn onsetGrid(step Float, len Float) [Float] {
    var out = [Float]();
    var t = 0.0;
    while (t < len - 0.001) { out push!(t); t = t + step; }
    out
}

let hats8  = onsetGrid(0.5, 4.0);
let hats16 = onsetGrid(0.25, 4.0);

-- 64 sixteenths = 16 beats; the 3-note cell needs 21 1/3 repeats, so
-- 21 of them plus one note
let ostinatoBlock = catShapes(loopShape(ostinato, 21), cell([[0.25, 0.0, 0.8]]));

let sections = [
    Section {
        name: "drone",
        bass: [cell([[16.0, -24.0, 0.6]])],
        pluck: [cell([[4.0, 0.0, 0.0], [6.0, 13.0, 0.6], [3.0, 7.0, 0.5], [3.0, 12.0, 0.5]])],
        pad: held(16.0, [0.0, 1.0, 6.0], 0.5),
        lead: [Shape](),
        drums: [drumHits(drKick, [0.0, 8.0], [0.6, 0.5], 16.0)],
        bassDrive: 6.0, bassLevel: 1.0, leadDrive: 10.0, leadLevel: 0.5, verbMix: 0.3,
    },
    Section {
        name: "grind",
        bass: [grindBass],
        pluck: [loopShape(stabLo, 4), loopShape(stabHi, 4)],
        pad: [Shape](),
        lead: [Shape](),
        drums: [catShapes(drumBars(drKick, [0.0, 0.75, 2.0, 2.75], [1.0, 0.7, 0.9, 0.7], 3),
                          drumHits(drKick, [0.0], [1.0], 4.0)),
                catShapes(drumBars(drSnare, [1.0, 3.0], [0.8], 3),
                          drumHits(drSnare, [1.0, 2.0, 2.5, 3.0, 3.25, 3.5, 3.75],
                                   [0.6, 0.6, 0.7, 0.7, 0.8, 0.9, 1.0], 4.0)),
                drumBars(drHat, hats8, [0.5, 0.3], 4)],
        bassDrive: 26.0, bassLevel: 0.25, leadDrive: 10.0, leadLevel: 0.5, verbMix: 0.15,
    },
    Section {
        name: "machine",
        bass: [machineBass],
        pluck: [ostinatoBlock],
        pad: [Shape](),
        lead: [machineLead],
        drums: [drumBars(drKick, [0.0, 2.5], [0.9, 0.7], 4),
                drumHits(drHat, onsetGrid(0.75, 16.0), [0.6, 0.35, 0.35], 16.0),
                drumHits(drTom, [15.0, 15.5], [0.7, 0.9], 16.0)],
        bassDrive: 12.0, bassLevel: 0.73, leadDrive: 14.0, leadLevel: 0.64, verbMix: 0.2,
    },
    Section {
        name: "drift",
        bass: [Shape](),
        pluck: [cell([[1.0, 0.0, 0.0], [0.5, 19.0, 0.4], [2.5, 0.0, 0.0], [0.5, 25.0, 0.4],
                      [3.5, 0.0, 0.0], [0.5, 18.0, 0.4], [7.5, 0.0, 0.0]])],
        pad: [cell([[8.0, -1.0, 0.45], [8.0, 2.0, 0.45]]),
              cell([[8.0, 0.0, 0.45], [8.0, 3.0, 0.45]]),
              cell([[8.0, 5.0, 0.4], [8.0, 8.0, 0.4]])],
        lead: [cell([[2.0, 0.0, 0.0], [3.0, 24.0, 0.35], [1.0, 23.0, 0.3],
                     [4.0, 18.0, 0.35], [6.0, 0.0, 0.0]])],
        drums: [Shape](),
        bassDrive: 4.0, bassLevel: 0.5, leadDrive: 6.0, leadLevel: 1.0, verbMix: 0.4,
    },
    Section {
        name: "scream",
        bass: [screamBass],
        pluck: [ostinatoBlock],
        pad: held(16.0, [0.0, 1.0, 6.0], 0.4),
        lead: [screamLead],
        drums: [drumBars(drKick, [0.0, 1.5, 2.0, 3.5], [1.0, 0.7, 1.0, 0.7], 4),
                catShapes(drumBars(drSnare, [1.0, 3.0], [1.0], 3),
                          drumHits(drSnare, [1.0, 3.0, 3.25, 3.5, 3.75],
                                   [1.0, 0.7, 0.8, 0.9, 1.0], 4.0)),
                drumBars(drHat, hats16, [0.55, 0.25, 0.35, 0.25], 4)],
        bassDrive: 32.0, bassLevel: 0.21, leadDrive: 30.0, leadLevel: 0.25, verbMix: 0.2,
    },
    Section {
        name: "scream'",
        bass: [screamBass retrograde],
        pluck: [ostinatoBlock],
        pad: held(16.0, [0.0, 1.0, 6.0], 0.4),
        lead: [screamLead invertDim('degree, 18.0)],
        drums: [drumBars(drKick, [0.0, 0.75, 2.0, 2.75], [1.0, 0.7, 1.0, 0.7], 4),
                drumBars(drSnare, [1.5, 3.0], [0.9, 1.0], 4),
                catShapes(drumBars(drHat, hats16, [0.55, 0.25, 0.35, 0.25], 3),
                          drumHits(drOpenHat, [0.0, 2.0], [0.6], 4.0))],
        bassDrive: 32.0, bassLevel: 0.21, leadDrive: 30.0, leadLevel: 0.25, verbMix: 0.2,
    },
    Section {
        name: "collapse",
        bass: [catShapes(riff scaleDim('dur, 2.0), cell([[8.0, -24.0, 0.5]]))],
        pluck: [cell([[12.0, 0.0, 0.0], [4.0, 1.0, 0.6]]),
                cell([[12.0, 0.0, 0.0], [4.0, 7.0, 0.6]])],
        pad: held(16.0, [0.0, 1.0, 6.0], 0.3),
        lead: [Shape](),
        drums: [drumHits(drKick, [0.0, 4.0, 8.0], [1.0, 0.8, 1.0], 16.0),
                drumHits(drSnare, [6.0], [0.7], 16.0)],
        bassDrive: 20.0, bassLevel: 0.41, leadDrive: 10.0, leadLevel: 0.5, verbMix: 0.35,
    },
];

const kDrone = 0;
const kGrind = 1;
const kMachine = 2;
const kDrift = 3;
const kScream = 4;
const kScreamInv = 5;
const kCollapse = 6;

---------------------------------------------------------------------------
-- The form: a music.job Coll of section markers. Each marker is a
-- one-frame shape [dur, sec, tr]; realizing the Coll gives one event per
-- section, carrying the section and its transposition as params.

fn blk(sec Int, tr Int) Coll =
    Coll.leaf(shape([[16.0, sec toFloat, tr toFloat]], ['dur, 'sec, 'tr]));

let form = Coll.seqc([
    blk(kDrone, 0),
    Coll.rpt((3, Coll.sel((Behavior.shuffled, [
        Coll.sel((Behavior.atRandom, [blk(kGrind, 0), blk(kGrind, 1), blk(kGrind, 6)])),
        Coll.sel((Behavior.atRandom, [blk(kMachine, 0), blk(kMachine, -2)])),
        blk(kDrift, 0)])))),
    Coll.sel((Behavior.weighted([0.6, 0.4]), [blk(kScream, 0), blk(kScreamInv, 0)])),
    Coll.sel((Behavior.atRandom, [
        Coll.rpt((2, Coll.sel((Behavior.atRandom, [blk(kGrind, 0), blk(kMachine, 6)])))),
        Coll.seqc([blk(kDrift, 6), blk(kScream, 1)])])),
    blk(kCollapse, 0)]);

fn param(e Event, k Symbol) Float {
    for (p : e.params) { if (p.0 == k) { return p.1; } }
    0.0
}

fn sectionOf(e Event) Section = sections[param(e, 'sec) toInt];

fn audible(es List<Event>) List<Event> {
    var keep = [Event]();
    for (e : es) { if (e.amp > 0.0) { keep push!(e); } }
    keep toList
}

-- One part's events across the whole form: each section's shapes for that
-- part, transposed and placed at the section's start. Rests (amp 0) drop.
fn partEvents(marks List<Event>, pick (Section) [Shape], legato Float) List<Event> {
    var out List<Event> = nil;
    for (m : marks) {
        var blkEvents List<Event> = nil;
        for (sh : pick(m sectionOf)) {
            let es = (sh transposeDim('degree, param(m, 'tr))) shapeEvents(m.t, legato);
            blkEvents = merge(blkEvents, es audible);
        }
        out = cat(out, blkEvents);
    }
    out
}

---------------------------------------------------------------------------
-- Realize one reading of the form and play it.

fn runPiece() Void {
    go(coro fn() Float {
        setTempo(0, 108.0);
        let marks = form realize;
        var total = 0.0;
        for (m : marks) {
            let tr = param(m, 'tr) toInt;
            ("bar %^  %^%^" fmt((m.t / 4.0) toInt + 1, (m sectionOf).name,
                            tr == 0 ? "" : " (" $ (tr > 0 ? "+" : "") $ tr toString $ ")")) println;
            total = m.t + m.dur;
        }
        play(partEvents(marks, fn(s Section) [Shape] { s.bass }, 0.9), bassV, keyE, chroma);
        play(partEvents(marks, fn(s Section) [Shape] { s.pluck }, 0.5), pluckV, keyE, chroma);
        play(partEvents(marks, fn(s Section) [Shape] { s.pad }, 1.0), padV, keyE, chroma);
        play(partEvents(marks, fn(s Section) [Shape] { s.lead }, 0.95), leadV, keyE, chroma);
        play(partEvents(marks, fn(s Section) [Shape] { s.drums }, 0.5), drumV, keyE, chroma);

        -- each section sets its distortion drives and levels and the reverb amount
        let b0 = getBeats() + getLatency();
        for (m : marks) {
            let s = m sectionOf;
            bundle()
                setInput(kBassDist, "drive", s.bassDrive)
                setInput(kBassDist, "level", s.bassLevel)
                setInput(kLeadDist, "drive", s.leadDrive)
                setInput(kLeadDist, "level", s.leadLevel)
                setInput(kVerb, "mix", s.verbMix)
                sched(0, 0, b0 + m.t);
        }
        yield (total + 8.0);
        stopAll();
        "piece done" println;
    }());
}

runPiece();
