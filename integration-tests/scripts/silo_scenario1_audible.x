-- Scenario 1, audible: load three parts onto three silos, then start them all
-- together on the next 4-beat downbeat. Each part is a coroutine task playing a
-- distinct pattern on its silo's voicer, scheduled by beat.
--
-- Run interactively (Ctrl-C to stop):
--   ./build/app/tzpl_app --nogui --wait -I lang/modules -I bridge/modules \
--       integration-tests/scripts/silo_scenario1_audible.x

import audio_engine.*;
import std.futures.*;
import silo.*;

-- bass on silo 0 / node 101: one low note per beat
let bassTask = "import audio_engine.*; let p = [36.0, 36.0, 43.0, 41.0]; coro fn play() Float { var i = 0; while (true) { playNote(101, i % 16, [p[i % 4], 0.7, 4.7, 0.0, 0.005, 0.15]); yield 0.9; releaseNote(101, i % 16); yield 0.1; i = i + 1; } } fn start() Void { spawn(0, play()); }";

-- chords on silo 1 / node 201: a triad held for ~4 beats
let chordTask = "import audio_engine.*; coro fn play() Float { var b = 0; while (true) { playNote(201, b, [60.0, 0.3, 5.0, 0.0, 0.05, 0.5]); playNote(201, b + 1, [64.0, 0.3, 5.0, 0.0, 0.05, 0.5]); playNote(201, b + 2, [67.0, 0.3, 5.0, 0.0, 0.05, 0.5]); yield 3.5; releaseNote(201, b); releaseNote(201, b + 1); releaseNote(201, b + 2); yield 0.5; b = b + 3; } } fn start() Void { spawn(0, play()); }";

-- arpeggio on silo 2 / node 301: fast high notes
let arpTask = "import audio_engine.*; let p = [72.0, 76.0, 79.0, 84.0]; coro fn play() Float { var i = 0; while (true) { playNote(301, i % 16, [p[i % 4], 0.25, 4.5, 0.0, 0.005, 0.1]); yield 0.2; releaseNote(301, i % 16); yield 0.05; i = i + 1; } } fn start() Void { spawn(0, play()); }";

-- Build a part's voicer node on its silo, attach the silo VM, load the task.
fn preparePart(silo Int, node Int, code String) Future<String> {
    begin(); newNode("voicer", node); connect(node, 0, 0, 0); sched(silo);
    attachVM(silo);
    siloLoad(silo, code)
}

async fn startSession() Void {
    "loading parts..." println;
    await awaitAll([preparePart(0, 101, bassTask),
                   preparePart(1, 201, chordTask),
                   preparePart(2, 301, arpTask)]);
    "all parts loaded; starting on the next downbeat" println;
    siloStartAt(quantizeUp(clockBeats(0), 4.0), [0, 1, 2]);
    "started" println;
}

engineStart();
masterGain(0.22);
setTempo(0, 110.0);
await startSession();
"session running -- Ctrl-C to stop." println;
