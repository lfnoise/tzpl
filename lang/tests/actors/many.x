-- Lightweight: spawn many actors and deliver a message to each. Each worker
-- accumulates into a shared total via a forwarder, proving thousands of actors
-- interleave on one event loop. Only summary output (deterministic).
import sexprs.*;

-- A collector actor sums everything it receives; workers forward to it.
let total = &0;
let collector = spawn(async fn(self Actor<SExpr>, init SExpr) Void {
    while (true) {
        let m = await receive(self);
        match (m) { SExpr.int(n): total <- *total + n; _: {} }
    }
}, SExpr.int(0));

async fn worker(self Actor<SExpr>, init SExpr) Void {
    let m = await receive(self);
    send(collector, m);
}

for (k : (0..4999)) {
    let w = spawn(worker, SExpr.int(0));
    send(w, SExpr.int(1));
}
runActors();
println("collected=" $ (*total toString));
