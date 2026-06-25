-- Two actors bounce a counter back and forth (async-lambda behaviors capturing
-- each other), stopping at 6. Exercises actor-to-actor send + the event-loop
-- cascade, and closure capture of actor references.
import sexprs.*;

-- Seed a Ref<Actor<SExpr>> so ping can reference pong (spawned after it).
let dummy = spawn(async fn(s Actor<SExpr>, i SExpr) Void {}, SExpr.int(0));
let pongRef = &dummy;

let ping = spawn(async fn(self Actor<SExpr>, init SExpr) Void {
    while (true) {
        let m = await receive(self);
        match (m) {
            SExpr.int(n): {
                println("ping " $ (n toString));
                if (n < 6) { send(*pongRef, SExpr.int(n + 1)); }
            }
            _: {}
        }
    }
}, SExpr.int(0));

let pong = spawn(async fn(self Actor<SExpr>, init SExpr) Void {
    while (true) {
        let m = await receive(self);
        match (m) {
            SExpr.int(n): { println("pong " $ (n toString)); send(ping, SExpr.int(n + 1)); }
            _: {}
        }
    }
}, SExpr.int(0));

pongRef <- pong;
send(ping, SExpr.int(0));
runActors();
println("done");
