-- Audible test of the silo task scheduler.
--
-- Run it interactively so the process stays alive while the engine plays:
--   ./build/app/tzpl_app --nogui --wait \
--       -I lang/modules -I bridge/modules \
--       integration-tests/scripts/silo_task_audible.x
--
-- You should hear a note every half beat (120 BPM) as the spawned coroutine
-- task resumes on the silo's tempo clock and triggers the voicer. Ctrl-C to stop.

import audio_engine.*;
import futures.*;

-- The silo task module (installed on silo 0's RT VM). A coroutine plays a
-- 6-note scale, advancing time by yielding beat-deltas; start() spawns it.
let taskCode = "import audio_engine.*; let scale = [60.0, 63.0, 65.0, 67.0, 70.0, 72.0]; coro fn melody() Float { var i = 0; while (true) { playNote(101, i % 16, [scale[i % 6], 0.6, 4.7, 0.0, 0.01, 0.2]); yield 0.4; releaseNote(101, i % 16); yield 0.1; i = i + 1; } } fn start() Void { spawn(0, melody()); }";

"starting engine..." println;
engineStart();
masterGain(0.3);
setTempo(0, 120.0);

-- Build the voicer node on silo 0 (the task triggers notes on node 101).
begin(0);
newNode("voicer", 101);
connect(101, 0, 0, 0);
sched();

attachVM(0);
let err = await siloLoad(0, taskCode);
"task load err=[" print; err print; "]" println;

siloStartAt(clockBeats(0), [0]);
"PLAYING -- a note every half beat at 120 BPM. Ctrl-C to stop." println;
