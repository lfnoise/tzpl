-- actors.x -- orchestration helpers for the actor model.
--
-- The actor primitives themselves (spawn / send / receive / register /
-- sendByName / runActors) are built-ins and need no import. This module adds
-- the NRT->silo bridge: sending a message by name to an actor living in a silo.

import audio_engine.*;   -- siloDeliverBytes (the C++ transport)
import message.*;        -- encode (SExpr -> Bytes)
import sexprs.*;         -- SExpr

-- Send an SExpr message to the actor registered as `name` in silo `silo`,
-- delivered as soon as it reaches the silo (the next audio block).
fn siloSend(silo Int, name Symbol, msg SExpr) Void {
    siloDeliverBytes(silo, name, encode(msg));
}

-- Run the cross-silo message router on the NRT (main) thread: drain every
-- silo's outbox and forward each message to its destination silo's actor. Must
-- run on the main thread (the from_nrt_ producer); blocks, polling. A musical
-- conductor that also routes silo<->silo traffic runs this.
fn runActorServer() Void {
    while (true) {
        pumpSiloOutboxes();
        sleepMs(2);
    }
}

-- Schedule an SExpr message to the named silo actor for a specific beat:
-- it lands in the actor's mailbox sample-accurately when TempoClock `clock`
-- reaches `beat`, using the engine's late-bound beat scheduler. This is how a
-- conductor (NRT or external) commands silo actors on the musical grid.
fn siloSendAt(silo Int, clock Int, beat Float, name Symbol, msg SExpr) Void {
    siloDeliverBytesAt(silo, clock, beat, name, encode(msg));
}
