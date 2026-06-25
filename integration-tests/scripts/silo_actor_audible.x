-- Audible test of SILO ACTORS (actor model Phase 2).
--
-- A silo actor is an `async fn` running on a silo's RT VM; it times itself with
-- `await delay(beats)`, which now resolves on the audio beat because the silo's
-- per-block tick drives the actor event loop against tempo clock 0. This is the
-- musical-scheduling payoff: the actor wakes sample-accurately on the beat.
--
-- Run interactively so the engine keeps playing:
--   ./build/app/tzpl_app --nogui --wait \
--       -I lang/modules -I bridge/modules \
--       integration-tests/scripts/silo_actor_audible.x
-- You should hear a 6-note scale, a note every half beat at 120 BPM. Ctrl-C to stop.

import audio_engine.*;
import futures.*;

-- Silo task module (installed on silo 0's RT VM). `melody` is an actor whose
-- behavior plays a scale, advancing time with `await delay`. start() spawns it.
let taskCode = """
import audio_engine.*;
import sexprs.*;

let scale = [60.0, 63.0, 65.0, 67.0, 70.0, 72.0];

async fn melody(self Actor<SExpr>, init SExpr) Void {
    var i = 0;
    while (true) {
        playNote(101, i % 16, [scale[i % 6], 0.6, 4.7, 0.0, 0.01, 0.2]);
        await delay(0.4);
        releaseNote(101, i % 16);
        await delay(0.1);
        i = i + 1;
    }
}

fn start() Void { spawn(melody, SExpr.int(0)); }
""";

"starting engine..." println;
engineStart();
masterGain(0.3);
setTempo(0, 120.0);

-- Voicer node on silo 0 (the actor triggers notes on node 101).
begin(0);
newNode("voicer", 101);
connect(101, 0, 0, 0);
sched();

attachVM(0);
let err = await siloLoad(0, taskCode);
"task load err=[" print; err print; "]" println;

siloStartAt(clockBeats(0), [0]);
"PLAYING -- a silo actor plays a note every half beat at 120 BPM. Ctrl-C to stop." println;
