-- Lightweight: spawn many actors and deliver a message to each. Each worker
-- accumulates into a shared total via a forwarder, proving thousands of actors
-- interleave on one event loop. Only summary output (deterministic).
import std.message.*;

-- A collector actor sums everything it receives; workers forward to it.
let total = &0;
let collector = spawn(async fn(self Actor<Msg>, init Msg) Void {
    while (true) {
        let m = await receive(self);
        match (m) { Msg.int(n): total <- *total + n; _: {} }
    }
}, Msg.int(0));

async fn worker(self Actor<Msg>, init Msg) Void {
    let m = await receive(self);
    send(collector, m);
}

for (k : (0..4999)) {
    let w = spawn(worker, Msg.int(0));
    send(w, Msg.int(1));
}
runActors();
println("collected=" $ (*total toString));
