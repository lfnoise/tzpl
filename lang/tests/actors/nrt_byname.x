-- Two NRT actors addressed by NAME: register + sendByName within one VM. This is
-- the same name-based local addressing the silo/NATS bridges use cross-VM, here
-- entirely on the main VM. Distinct from pingpong.x, which uses direct actor refs.
-- Exercises register-from-the-spawn-site (actor spawn(...) register('name)) and
-- sendByName resolving a process-global name to a local mailbox.
import message.*;

-- relay: on int n, forward n+1 to 'collector by name (until n reaches 4).
async fn relay(self Actor<Msg>, init Msg) Void {
    while (true) {
        let m = await receive(self);
        match (m) {
            Msg.int(n): {
                println("relay " $ (n toString));
                if (n < 4) { sendByName('collector, Msg.int(n + 1)); }
            }
            _: {}
        }
    }
}

-- collector: on int n, print it and bounce n+1 back to 'relay by name.
async fn collector(self Actor<Msg>, init Msg) Void {
    while (true) {
        let m = await receive(self);
        match (m) {
            Msg.int(n): {
                println("collector " $ (n toString));
                sendByName('relay, Msg.int(n + 1));
            }
            _: {}
        }
    }
}

relay spawn(Msg.int(0)) register('relay);
collector spawn(Msg.int(0)) register('collector);

sendByName('relay, Msg.int(0));
runActors();
println("done");
