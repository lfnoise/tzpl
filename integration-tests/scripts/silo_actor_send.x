-- Cross-VM actor messaging (actor model Phase 2b): an NRT "conductor" sends
-- messages BY NAME to an actor living in a silo. The message (an SExpr) is
-- encoded to bytes on the NRT side, shipped across the silo command FIFO, and
-- decoded + delivered into the named actor's mailbox on the silo's RT thread.
--
-- Run (kept alive via --nats-url; needs nats-server):
--   ./build/app/tzpl_app --nogui --nats-url nats://127.0.0.1:14222 \
--       -I lang/modules -I bridge/modules integration-tests/scripts/silo_actor_send.x

import audio_engine.*;
import futures.*;
import actors.*;          -- siloSend
import sexprs.*;

-- The silo actor registers itself as "voice" and plays a note for each pitch
-- message it receives. Spawned at module top level so it is registered as soon
-- as the module loads (before any message is sent).
let taskCode = """
import audio_engine.*;
import sexprs.*;

async fn voice(self Actor<SExpr>, init SExpr) Void {
    var id = 0;
    while (true) {
        let m = await receive(self);
        match (m) {
            SExpr.float(pitch): {
                playNote(101, id % 16, [pitch, 0.6, 4.7, 0.0, 0.01, 0.2]);
                id = id + 1;
            }
            _: {}
        }
    }
}

voice spawn(SExpr.int(0)) register('voice);
""";

"starting engine..." println;
engineStart();
masterGain(0.3);
setTempo(0, 120.0);

begin(0);
newNode("voicer", 101);
connect(101, 0, 0, 0);
sched();

attachVM(0);
let err = await siloLoad(0, taskCode);
"load err=[" print; err print; "]" println;

-- Conductor: send three notes from NRT to the silo actor, by name (cross-VM).
siloSend(0, toSymbol("voice"), SExpr.float(60.0));
siloSend(0, toSymbol("voice"), SExpr.float(64.0));
siloSend(0, toSymbol("voice"), SExpr.float(67.0));
"sent 3 notes to silo actor 'voice'" println;
