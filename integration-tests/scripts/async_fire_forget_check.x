-- Fire-and-forget async fns whose continuations run only if a cross-thread
-- resolution drains the async ready queue itself. NO top-level await anywhere
-- in this script, so nobody is parked to drain it -- the resolver must.
-- Regression for a silent live-coding session: a proxy's compile finished on
-- the async I/O worker but the continuation that swaps the new source in
-- never ran, because define/play were evaluated without an await.
--   (a) I/O completion drain: readFileAsync resolves on the worker thread.
--   (b) shutdown drain: whatever is still pending runs when the VM tears down.
import std.futures.*;

let dir = "/tmp/tzpl_test_async_ff";
makeDir(dir);
writeFile(dir $ "/in.txt", "payload");

async fn loadAndWrite(tag String) Void {
    let content = await readFileAsync(dir $ "/in.txt");
    match (content) {
        some(c): writeFile(dir $ "/" $ tag $ ".out", "FIREFORGET " $ tag $ " ran: " $ c);
        none: writeFile(dir $ "/" $ tag $ ".out", "readfail");
    }
}

loadAndWrite("a");   -- discarded Future
loadAndWrite("b");   -- discarded Future
println("FIREFORGET body done");
