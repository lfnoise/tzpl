-- Two actors bounce a counter back and forth (async-lambda behaviors capturing
-- each other), stopping at 6. Exercises actor-to-actor send + the event-loop
-- cascade, and closure capture of actor references.
import message.*;

-- Seed a Ref<Actor<Msg>> so ping can reference pong (spawned after it).
let dummy = spawn(async fn(s Actor<Msg>, i Msg) Void {}, Msg.int(0));
let pongRef = &dummy;

let ping = spawn(async fn(self Actor<Msg>, init Msg) Void {
    while (true) {
        let m = await receive(self);
        match (m) {
            Msg.int(n): {
                println("ping " $ (n toString));
                if (n < 6) { send(*pongRef, Msg.int(n + 1)); }
            }
            _: {}
        }
    }
}, Msg.int(0));

let pong = spawn(async fn(self Actor<Msg>, init Msg) Void {
    while (true) {
        let m = await receive(self);
        match (m) {
            Msg.int(n): { println("pong " $ (n toString)); send(ping, Msg.int(n + 1)); }
            _: {}
        }
    }
}, Msg.int(0));

pongRef <- pong;
send(ping, Msg.int(0));
runActors();
println("done");
