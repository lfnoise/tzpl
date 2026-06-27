-- Two NRT actors addressed by NAME: register + sendByName within one VM. This is
-- the same name-based local addressing the silo/NATS bridges use cross-VM, here
-- entirely on the main VM. Distinct from pingpong.x, which uses direct actor refs.
-- Exercises register-from-the-spawn-site (actor spawn(...) register('name)) and
-- sendByName resolving a process-global name to a local mailbox.
import sexprs.*;

-- relay: on int n, forward n+1 to 'collector by name (until n reaches 4).
async fn relay(self Actor<SExpr>, init SExpr) Void {
    while (true) {
        let m = await receive(self);
        match (m) {
            SExpr.int(n): {
                println("relay " $ (n toString));
                if (n < 4) { sendByName('collector, SExpr.int(n + 1)); }
            }
            _: {}
        }
    }
}

-- collector: on int n, print it and bounce n+1 back to 'relay by name.
async fn collector(self Actor<SExpr>, init SExpr) Void {
    while (true) {
        let m = await receive(self);
        match (m) {
            SExpr.int(n): {
                println("collector " $ (n toString));
                sendByName('relay, SExpr.int(n + 1));
            }
            _: {}
        }
    }
}

relay spawn(SExpr.int(0)) register('relay);
collector spawn(SExpr.int(0)) register('collector);

sendByName('relay, SExpr.int(0));
runActors();
println("done");
