-- async/await Phase C: awaitAll / gather combinators.
--
-- These are ordinary generic `async fn`s -- no native support. Because the async
-- loop drains in virtual-beat order, awaiting a set of already-spawned futures in
-- turn still completes only when the LAST resolves. This mirrors lang/modules/
-- futures.x; kept inline so the test is self-contained.

-- Wait for every future; return after the last resolves.
async fn awaitAll<T>(fs [Future<T>]) Void {
    var i = 0;
    while (i < fs length) { await fs[i]; i = i + 1; }
}

-- Wait for every future and collect values in the order of `fs`.
async fn gather<T>(fs [Future<T>]) [T] {
    var out [T] = [];
    var i = 0;
    while (i < fs length) { out push!(await fs[i]); i = i + 1; }
    out
}

async fn part(tag String, beats Float) Void {
    await delay(beats);
    tag print; " ready" println;
}

async fn load(id Int, beats Float) Int {
    await delay(beats);
    id * 10
}

-- awaitAll over Future<Void>: the "load N parts, wait for all" barrier. Parts are
-- spawned out of beat order but become ready in beat order (B@1, C@2, A@3), and
-- the barrier returns only after the last.
"-- awaitAll --" println;
let pa = part("A(3)", 3.0);
let pb = part("B(1)", 1.0);
let pc = part("C(2)", 2.0);
await awaitAll([pa, pb, pc]);
"all parts ready" println;

-- gather over Future<Int>: values come back in spawn order, not resolve order.
"-- gather --" println;
let l0 = load(1, 2.0);
let l1 = load(2, 1.0);
let l2 = load(3, 3.0);
let vals = await gather([l0, l1, l2]);
"vals = " print; vals println;
"total = " print; (vals[0] + vals[1] + vals[2]) println;
