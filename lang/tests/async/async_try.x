-- Postfix `try` inside async fns: the early return must route through
-- op_async_return (resolving the Future), same as normal returns.
import std.result.*;

fn check(n Int) Result<Int, String> {
    n >= 0 ? Result<Int, String>.ok(n) : Result<Int, String>.err("negative")
}

async fn load(n Int) Result<Int, String> {
    let v = await ready(n);
    let c = check(v) try;              -- err path: op_async_return
    let d = check(c - 10) try;         -- second try after an await
    Result<Int, String>.ok(d * 100)
}

fn report(r Result<Int, String>) Void {
    match (r) {
        ok(n): println(n);
        err(m): println(m);
    }
}

report(await load(42));    -- ok path: 3200
report(await load(-1));    -- first try propagates
report(await load(5));     -- second try propagates

-- Option-try in an async fn
async fn parseAsync(s String) Option<Int> {
    let raw = await ready(s);
    let n = parseInt(raw) try;
    Option.some(n + 1)
}

println(await parseAsync("41"));
println(await parseAsync("nah"));
