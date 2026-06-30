-- silo_actors.x -- silo-side (real-time) actor-send helper.
--
-- Importable INTO silo code: everything here is real-time safe. This is the
-- counterpart to actors.x, whose NRT-side helpers (siloSend / siloSendAt /
-- runActorServer) call non-rt-safe transports and so cannot be imported into a
-- silo. The silo->NRT and silo->silo direction goes through the lock-free outbox
-- (siloOutbox), drained on the main thread by runActorServer.

import audio_engine.*;   -- siloOutbox (the rt-safe outbox transport)
import messageEncoding.*;        -- encode (Msg -> Bytes)
import message.*;         -- Msg

-- From inside a silo actor or task, send a Msg to an actor elsewhere.
--   target = -1  -> an actor registered at NRT (the main VM)
--   target >= 0  -> the actor named `name` in that silo
-- Real-time safe: a by-value push into the silo's lock-free outbox; the main
-- thread's runActorServer drains it, decodes, and routes by name. Returns an
-- error code -- errInternal if the encoded message exceeds the outbox cap
-- (OutboxMsg::kMaxBytes, ~4 KB) or the outbox is full, otherwise errNone.
fn siloPost(target Int, name Symbol, msg Msg) Int {
    siloOutbox(target, name, encode(msg))
}
