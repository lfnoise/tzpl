-- nats.x
-- Script wrapper for the NATS messaging FFI.
-- Re-exports `nats_ffi.*` so users can write `import nats.*;` and get the
-- NATS functions in scope, plus the NATS->actor bridge helper.

export nats_ffi.*;

import messageEncoding.*;   -- isMessage / decode
import message.*;    -- Msg

-- Bridge a NATS subject to a local actor: every TZB-encoded message published to
-- `subject` (by this or any other process) is decoded and delivered into the
-- mailbox of the actor registered as `name`. Wraps the onMessageMsg + isMessage +
-- decode + sendByName pattern. Drive the actor with runActors()/serveActors() so
-- it processes mail as NATS deliveries arrive (serveActors parks until woken).
fn natsBridgeActor(subject String, name Symbol) Void {
    onMessageMsg(subject, fn(b Bytes) {
        if (isMessage(b)) { sendByName(name, decode(b)); }
    });
}
