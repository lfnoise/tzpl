-- Silo -> NRT actor messaging (actor model Phase 2c, the reverse direction). A
-- silo actor sends messages to an actor living at NRT (the main VM). The silo
-- can't allocate on the audio thread, so it pushes the encoded message into its
-- fixed lock-free outbox with targetSilo = -1 (meaning "NRT"); the main thread
-- (runActorServer) drains it, decodes the SExpr, and delivers it by name to the
-- NRT actor, then drives the NRT actor loop so it processes the mail.

import audio_engine.*;
import futures.*;
import actors.*;          -- runActorServer
import message.*;         -- decode (also re-exported use of sendByName builtin)
import sexprs.*;

-- NRT actor: registered as "conductor", prints each pitch it receives. Spawned
-- on the main VM before the server loop starts.
async fn conductor(self Actor<SExpr>, init SExpr) Void {
    while (true) {
        let m = await receive(self);
        match (m) {
            SExpr.float(p): { "NRT GOT " print; p println; }
            _: {}
        }
    }
}

-- silo 0: a sender actor that emits a rising line to the NRT "conductor", one
-- note per beat. The sends run inside the tick (gCurrentSilo set), after delay.
let silo0 = """
import audio_engine.*;
import message.*;
import sexprs.*;

async fn sender(self Actor<SExpr>, init SExpr) Void {
    let scale = [60.0, 64.0, 67.0, 72.0];
    var i = 0;
    while (i < 4) {
        await delay(1.0);
        siloOutbox(-1, toSymbol("conductor"), encode(SExpr.float(scale[i % 4])));
        i = i + 1;
    }
}
spawn(sender, SExpr.int(0));
""";

"starting engine..." println;
engineStart();
masterGain(0.3);
setTempo(0, 120.0);

attachVM(0);
let e0 = await siloLoad(0, silo0);
"load=[" print; e0 print; "]" println;

-- Spawn the NRT conductor, then run the router/driver loop (blocks).
conductor spawn(SExpr.int(0)) register('conductor);
"routing silo 0 -> NRT conductor ..." println;
runActorServer();
