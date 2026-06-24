-- async/await Phase A: eager async fns, ready(), top-level (blocking) await.

async fn compute(x Int) Int {
    let a = await ready(x + 1);
    let b = await ready(a * 10);
    b + 5
}

-- nested async: one async fn awaits another
async fn doubler(n Int) Int {
    let c = await compute(n);
    c * 2
}

-- explicit return + if-with-return inside async
async fn early(x Int) Int {
    if (x > 0) { return await ready(x); }
    await ready(0 - x)
}

-- void async fn
async fn announce(tag String, v Int) Void {
    let got = await ready(v);
    tag print; got println;
}

"compute(4) = " print; (await compute(4)) println;
"doubler(7) = " print; (await doubler(7)) println;
"early(-3) = " print; (await early(0 - 3)) println;
"early(5) = " print; (await early(5)) println;
await announce("announce: ", 99);

-- await a plain (non-async) value via ready at top level
let direct = await ready(42);
"direct = " print; direct println;
