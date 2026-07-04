-- Scenario 2, audible: multi-phase. Start a rhythm section on one downbeat,
-- then bring in a chord pad on a later 8-beat line -- all synchronized.
--   ./build/app/tzpl_app --nogui --wait -I lang/modules -I bridge/modules \
--       integration-tests/scripts/silo_scenario2_audible.x

import audio_engine.*;
import futures.*;
import silo.*;

let bassTask  = "import audio_engine.*; let p = [36.0, 36.0, 43.0, 41.0]; coro fn play() Float { var i = 0; while (true) { playNote(101, i % 16, [p[i % 4], 0.7, 4.7, 0.0, 0.005, 0.15]); yield 0.9; releaseNote(101, i % 16); yield 0.1; i = i + 1; } } fn start() Void { spawn(0, play()); }";
let arpTask   = "import audio_engine.*; let p = [72.0, 76.0, 79.0, 84.0]; coro fn play() Float { var i = 0; while (true) { playNote(201, i % 16, [p[i % 4], 0.25, 4.5, 0.0, 0.005, 0.1]); yield 0.2; releaseNote(201, i % 16); yield 0.05; i = i + 1; } } fn start() Void { spawn(0, play()); }";
let chordTask = "import audio_engine.*; coro fn play() Float { var b = 0; while (true) { playNote(301, b, [60.0, 0.3, 5.0, 0.0, 0.05, 0.5]); playNote(301, b + 1, [64.0, 0.3, 5.0, 0.0, 0.05, 0.5]); playNote(301, b + 2, [67.0, 0.3, 5.0, 0.0, 0.05, 0.5]); yield 3.5; releaseNote(301, b); releaseNote(301, b + 1); releaseNote(301, b + 2); yield 0.5; b = b + 3; } } fn start() Void { spawn(0, play()); }";

fn preparePart(silo Int, node Int, code String) Future<String> {
    begin(); newNode("voicer", node); connect(node, 0, 0, 0); sched(silo);
    attachVM(silo);
    siloLoad(silo, code)
}

async fn startSession() Void {
    -- phase 1: rhythm section (bass + arp) on the next downbeat
    await awaitAll([preparePart(0, 101, bassTask), preparePart(1, 201, arpTask)]);
    siloStartAt(quantizeUp(clockBeats(0), 4.0), [0, 1]);
    "phase 1 (bass + arp) starting on the next downbeat" println;

    -- phase 2: bring in the chord pad on a later 8-beat line
    await awaitAll([preparePart(2, 301, chordTask)]);
    siloStartAt(quantizeUp(clockBeats(0), 8.0), [2]);
    "phase 2 (chords) entering on the next 8-beat line" println;
}

engineStart();
masterGain(0.22);
setTempo(0, 110.0);
await startSession();
"session running -- Ctrl-C to stop." println;
