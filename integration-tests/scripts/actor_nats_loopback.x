-- Cross-process actor messaging over NATS (actor model Phase 3) + the NRT actor
-- server (serveActors). An actor registers a name and a NATS subject is bridged
-- to it; a message published to that subject (here, to ourselves -- the same
-- path another process would use) is decoded and delivered into the actor's
-- mailbox. serveActors() parks the actor loop until the NATS delivery wakes it.
--
-- Run via integration-tests/scripts/actor_nats_loopback.sh (needs nats-server).

import nats.*;
import message.*;
import sexprs.*;

async fn worker(self Actor<SExpr>, init SExpr) Void {
    register(toSymbol("w"), self);
    while (true) {
        let m = await receive(self);
        println("ACTOR GOT " $ (m toString));
    }
}

spawn(worker, SExpr.int(0));
runActors();                 -- let worker register itself and park

-- Bridge the NATS subject "actors.w" to the local actor "w": decode the payload
-- and enqueue it. (A remote process would publish to this subject.)
onMessageMsg("actors.w", fn(b Bytes) {
    if (isMessage(b)) { sendByName(toSymbol("w"), decode(b)); }
});

-- Deliver two messages over NATS (cross-process transport, here looped back).
natsPubMsg("actors.w", encode(SExpr.string("hello over nats")));
natsPubMsg("actors.w", encode(SExpr.int(123)));

"serving..." println;
serveActors();               -- park; NATS deliveries wake us, the actor runs
