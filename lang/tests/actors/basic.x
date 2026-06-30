-- Basic actor lifecycle: spawn, send (queued + delivered-to-parked), receive,
-- runActors drains to quiescence. Messages are Msg.
import message.*;

-- Counter actor: accumulates ints, prints the running total.
async fn counter(self Actor<Msg>, init Msg) Void {
    var total = 0;
    while (true) {
        let m = await receive(self);
        match (m) {
            Msg.int(n):   { total = total + n; println("total=" $ (total toString)); }
            Msg.symbol(s): { println("done at " $ (total toString)); }
            _: {}
        }
    }
}

let c = spawn(counter, Msg.int(0));

-- These queue (the actor is parked on its first receive).
var i = 1;
while (i <= 4) { send(c, Msg.int(i)); i = i + 1; }
send(c, Msg.symbol(toSymbol("stop")));
runActors();

-- A one-shot actor that completes after a single message.
async fn once(self Actor<Msg>, init Msg) Void {
    let m = await receive(self);
    println("once got " $ (m toString));
}
let o = spawn(once, Msg.int(0));
send(o, Msg.string("hello"));
runActors();
println("ok");
