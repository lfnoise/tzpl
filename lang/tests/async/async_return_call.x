-- A call in return position inside an async fn must NOT become a tail call:
-- a tail call replaces the async frame, and the callee's op_return then
-- jumps to that frame's null returnPC (this used to segfault). Covers the
-- explicit-return form, the trailing-expression form, and both with and
-- without a real suspension beforehand.

fn plain(x Int) Int { x * 2 }

async fn explicitReturn(flag Bool) Int {
    if (flag) { return plain(21); }
    3
}

async fn trailingCall(x Int) Int {
    plain(x)
}

async fn afterAwait(flag Bool) Int {
    let a = await ready(5);
    if (flag) { return plain(a); }
    a
}

async fn trailingAfterAwait(x Int) Int {
    let a = await ready(x);
    plain(a)
}

-- a lambda (mono body path) in return position
async fn viaLambda(x Int) Int {
    let f = fn(y Int) Int { y + 100 };
    return f(x);
}

explicitReturn(true) await println;
explicitReturn(false) await println;
trailingCall(7) await println;
afterAwait(true) await println;
afterAwait(false) await println;
trailingAfterAwait(8) await println;
viaLambda(1) await println;
"done" println;
