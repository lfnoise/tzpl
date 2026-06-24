-- async/await Phase B: delay(beats) -- real suspend across the async event loop.
-- Spawning an async fn runs it eagerly to its first `await delay(...)`, where it
-- suspends and returns a Pending Future. A top-level (blocking) await pumps the
-- loop, advancing virtual beats and resuming each coroutine when its delay fires.

async fn afterDelay(tag String, beats Float) Void {
    await delay(beats);
    tag print; " done" println;
}

-- Several delays inside one coroutine: sequential suspends that each resume and
-- accumulate before the body returns.
async fn chain() Int {
    var total = 0;
    await delay(1.0); total = total + 1;
    await delay(1.0); total = total + 10;
    await delay(1.0); total = total + 100;
    total
}

"-- start --" println;

-- Spawn out of beat order; each suspends immediately (nothing prints yet).
let fa = afterDelay("A(3)", 3.0);
let fb = afterDelay("B(1)", 1.0);
let fc = afterDelay("C(2)", 2.0);

-- Awaiting fa pumps the whole loop: timers fire in beat order B(1), C(2), A(3),
-- so the prints come out in that order even though fa was awaited first.
await fa;
await fb;
await fc;

"chain = " print; (await chain()) println;

"-- end --" println;
