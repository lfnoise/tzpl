-- Regression: a plain lambda nested inside a `coro fn` (or a coroutine
-- lambda) lost its trailing-expression return. genLambda keyed the
-- "no implicit return in a coroutine body" rule on the inherited
-- inCoroutineFn_ flag rather than the lambda's own isCoroutine, so every
-- `fn() T { expr }` written inside a coroutine returned nothing. A live
-- session's `define(px, fn() S { ... })` graph functions hit this: the
-- defs rendered silence and a voicer def reported no noteParams.

coro fn session() Int {
    let a = fn() Int { 40 + 2 };
    a() println;
    let m = fn(x Float) Float { x * 2.0 };
    m(1.5) println;
    yield 1;
    -- trailing if-else and match inside the nested lambda
    let b = fn(flag Bool) Int { if (flag) { 7 } else { 8 } };
    b(true) println;
    let c = fn(n Int) Int { match (n) { 1: 10; _: 20; } };
    c(1) println;
    yield 2;
    -- and a lambda nested in a coroutine lambda
    let inner = coro fn() Int {
        let d = fn() Int { 99 };
        yield d();
        0
    }();
    inner next println;
    yield 3;
}

let s = session();
s next println;
s next println;
s next println;
s next println;
"done" println;
