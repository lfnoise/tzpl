-- actors.x -- orchestration helpers for the actor model.
--
-- The actor primitives themselves (spawn / send / receive / register /
-- sendByName / runActors) are built-ins and need no import. This module adds
-- the NRT->silo bridge: sending a message by name to an actor living in a silo.

import audio_engine.*;   -- siloDeliverBytes (the C++ transport)
import message.*;        -- encode (SExpr -> Bytes)
import sexprs.*;         -- SExpr

-- Send an SExpr message to the actor registered as `name` in silo `silo`.
-- Encodes the message and ships the bytes to the silo's delivery trampoline,
-- which decodes it on the silo's RT thread and enqueues it into that actor.
-- Delivery is asynchronous; the message lands on the silo's next block.
fn siloSend(silo Int, name Symbol, msg SExpr) Void {
    siloDeliverBytes(silo, name, encode(msg));
}
