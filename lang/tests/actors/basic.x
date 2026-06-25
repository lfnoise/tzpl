-- Basic actor lifecycle: spawn, send (queued + delivered-to-parked), receive,
-- runActors drains to quiescence. Messages are SExpr.
import sexprs.*;

-- Counter actor: accumulates ints, prints the running total.
async fn counter(self Actor<SExpr>, init SExpr) Void {
    var total = 0;
    while (true) {
        let m = await receive(self);
        match (m) {
            SExpr.int(n):   { total = total + n; println("total=" $ (total toString)); }
            SExpr.symbol(s): { println("done at " $ (total toString)); }
            _: {}
        }
    }
}

let c = spawn(counter, SExpr.int(0));

-- These queue (the actor is parked on its first receive).
var i = 1;
while (i <= 4) { send(c, SExpr.int(i)); i = i + 1; }
send(c, SExpr.symbol(toSymbol("stop")));
runActors();

-- A one-shot actor that completes after a single message.
async fn once(self Actor<SExpr>, init SExpr) Void {
    let m = await receive(self);
    println("once got " $ (m toString));
}
let o = spawn(once, SExpr.int(0));
send(o, SExpr.string("hello"));
runActors();
println("ok");
