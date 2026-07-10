-- Plays a short phrase through every voicer-based instrument defined in
-- examples/instrument_synthdefs.x (needs examples/ on the module path).
--
-- For each synth we:
--   1. spawn a node with synthdef.play(...)
--   2. trigger a 4-note arpeggio with noteOn / noteOff
--   3. let the tail ring out, then free the node
--
-- Run from tzpl_app:
--     ./build/app/tzpl_app --nogui lang/scratch/instrumentTest.x

import synthdef.*;
import audio_engine as ae;
import clock.*;
import common_ugens.*;

import instrument_synthdefs.*;

---------------------------------------------------------------------------
-- helpers

-- Wrap a noteOn in a single-command bundle.
fn note(nodeID Int, noteID Int, freq Float, amp Float) Int {
	ae.begin();
	ae.noteOn(nodeID, noteID, [freq, amp, 1.0]);
	ae.sched(0);
	noteID
}

-- Wrap a noteOff in a single-command bundle.
fn release(nodeID Int, noteID Int) Int {
	ae.begin();
	ae.noteOff(nodeID, noteID);
	ae.sched(0);
	noteID
}

-- A short 4-note ascending arpeggio at the given root MIDI note.
-- `gateTime` is how long each note holds before noteOff.
-- `step` is how long between successive note-on events.
-- `tail` is silence after the last release before the coro returns.
coro fn arpeggio(name String, root Float, amp Float,
                 gateTime Float, step Float, tail Float) Float {
	"-- %^ --" fmt(name) println;
	let nodeID = name play;
	yield 0.05;

	let degrees = [0.0, 4.0, 7.0, 12.0];
	var i = 0;
	while (i < degrees length) {
		let noteID = i + 1;
		let freq = nnhz(root + degrees[i]);
		note(nodeID, noteID, freq, amp);
		yield gateTime;
		release(nodeID, noteID);
		yield (step - gateTime);
		i = i + 1;
	}

	yield tail;
	nodeID stop;
	yield 0.1;
}

-- A drum-style trigger: 4 hits at a fixed pitch.
coro fn drumPattern(name String, freq Float, amp Float,
                    step Float, tail Float) Float {
	"-- %^ --" fmt(name) println;
	let nodeID = name play;
	yield 0.05;

	var i = 0;
	while (i < 4) {
		let noteID = i + 1;
		note(nodeID, noteID, freq, amp);
		yield 0.02;
		release(nodeID, noteID);
		yield (step - 0.02);
		i = i + 1;
	}

	yield tail;
	nodeID stop;
	yield 0.1;
}

---------------------------------------------------------------------------
-- run the demo

fn playAll() {
	go(coro fn() Float {
		"start playing instrument demo" println;

		-- pitched, sustained voicers: hold each note ~0.55s of a 0.7s step.
		"organ"     arpeggio(60.0, 0.4, 0.55, 0.7, 0.4) yieldAll;
		"fmBrass"   arpeggio(57.0, 0.4, 0.55, 0.7, 0.5) yieldAll;
		"sawLead"   arpeggio(64.0, 0.35, 0.45, 0.6, 0.4) yieldAll;
		"pwmPad"    arpeggio(60.0, 0.45, 0.9, 1.1, 0.8) yieldAll;
		"subBass"   arpeggio(36.0, 0.6, 0.55, 0.7, 0.4) yieldAll;

		-- pitched, percussive (decay-driven) voicers: short gate, longer tail.
		"fmBell"    arpeggio(72.0, 0.5, 0.05, 0.7, 2.5) yieldAll;
		"pluck"     arpeggio(64.0, 0.5, 0.05, 0.5, 1.5) yieldAll;
		"modalBell" arpeggio(67.0, 0.5, 0.05, 0.8, 3.5) yieldAll;

		-- drums: fixed freq, just rhythmic triggers.
		"kick"  drumPattern(60.0, 0.8, 0.5, 0.3) yieldAll;
		"snare" drumPattern(220.0, 0.7, 0.5, 0.3) yieldAll;

		"done playing instrument demo" println;
	}());
}

playAll();

"done evaluating" println;


