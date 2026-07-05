-- ui_piano_roll.x -- edit a melody in a pianoRoll while a task plays it.
--
-- A pianoRoll widget holds a note list -- flat (pitch, startBeat,
-- durBeats) triplets -- with pitch in steps of 1/edo octave. A clock
-- coroutine steps through the roll in 16ths and drives a synth through
-- its materialized control widgets, re-reading notes(roll) live, so
-- clicking notes in or out is heard on the next pass.
--
-- Change edo to 19, 31, or 53 for those equal temperaments (1200 =
-- cents, 1 = octaves); stepFreq converts steps to Hz for any edo, and
-- the seed melody below transposes with it.
--
-- Run: evaluate this file in the app, with audio on.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import audio_engine.*;
import clock.*;
import ui.*;

let edo = 12;
let loopBeats = 4.0;

fn beep() S {
	let freq = control("freq", ControlSpec { lo: 20.0, hi: 8000.0, init: 440.0, warp: ControlWarp.exponential });
	let amp = control("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.0, warp: ControlWarp.linear });
	freq sinosc(0) mul(amp) outlet
}

beep defSynthX("beep");
masterGain(0.2);
let node = play("beep");

-- The roll and a level meter join the synth's own controls in the
-- "beep" panel (control(node, ...) defaults to the def-name panel).
panel("beep");
let roll = pianoRoll("melody", loopBeats, edo);
let lvl = meter("level", node);
panel("");
let freqW = control(node, "freq");
let ampW = control(node, "amp");

-- Seed: a major-ish arpeggio built from scale steps of the chosen edo
-- (root, major third ~ edo/3, fifth ~ 7*edo/12, octave).
let root = 5.0 * edo;
let third = root + round(edo / 3.0);
let fifth = root + round(7.0 * edo / 12.0);
roll setNotes([root, 0.0, 0.5, third, 1.0, 0.5, fifth, 2.0, 0.5, root + edo, 3.0, 1.0]);

-- Steps of 1/edo octave -> Hz. Pitch 0 is MIDI 0's pitch class, so in
-- 12-edo pitches ARE MIDI note numbers (step 69 = A4 = 440).
fn stepFreq(p Float) Float = 440.0 * pow(2.0, p / edo - 69.0 / 12.0);

-- Step the roll in 16ths on the tempo clock, gating amp at note
-- starts and ends. Driving the control WIDGETS (setValue) means the
-- panel shows the player's moves.
go(coro fn() Float {
	var beat = 0.0;
	var soundingUntil = -1.0;
	while (true) {
		if (soundingUntil >= 0.0 && beat >= soundingUntil - 0.001) {
			ampW setValue(0.0);
			soundingUntil = -1.0;
		}
		for (n : roll notes clump(3)) {
			if (abs(n[1] - beat) < 0.001) {
				freqW setValue(n[0] stepFreq);
				ampW setValue(0.3);
				soundingUntil = beat + n[2];
			}
		}
		beat = beat + 0.25;
		if (beat >= loopBeats - 0.001) { beat = 0.0; }
		yield 0.25;
	}
}());

"melody playing -- click notes into the roll" println;
