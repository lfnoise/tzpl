-- Cross-process actor messaging over NATS (actor model Phase 3) + the NRT actor
-- server (serveActors). An actor registers a name and a NATS subject is bridged
-- to it; a message published to that subject (here, to ourselves -- the same
-- path another process would use) is decoded and delivered into the actor's
-- mailbox. serveActors() parks the actor loop until the NATS delivery wakes it.
--
-- Run via integration-tests/scripts/actor_nats_loopback.sh (needs nats-server).

import nats.*;
import messageEncoding.*;
import message.*;

async fn worker(self Actor<Msg>, init Msg) Void {
    while (true) {
        let m = await receive(self);
        println("ACTOR GOT " $ (m toString));
    }
}

worker spawn(Msg.int(0)) register('w);
runActors();                 -- let worker reach its first receive and park

-- Bridge the NATS subject "actors.w" to the local actor "w": decode each payload
-- and enqueue it. (A remote process would publish to this subject.) The helper
-- wraps the onMessageMsg + isMessage + decode + sendByName pattern.
natsBridgeActor("actors.w", 'w);

-- Deliver two messages over NATS (cross-process transport, here looped back).
natsPubMsg("actors.w", encode(Msg.string("hello over nats")));
natsPubMsg("actors.w", encode(Msg.int(123)));

"serving..." println;
serveActors();               -- park; NATS deliveries wake us, the actor runs
