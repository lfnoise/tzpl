-- Regression: an anonymous coroutine whose body ends in a trailing
-- expression segfaulted when it finished. genLambda compiled the trailing
-- expression as op_return (as for a plain lambda), which popped the
-- coroutine's flat frame -- whose returnPC is null, since yield/done switch
-- contexts by hand -- and jumped through it. A named `coro fn` was already
-- exempt; the lambda path now is too, and op_coro_done closes the body.

let c = coro fn() Float {
    var i = 0;
    while (i < 2) { yield 0.5; i = i + 1; }
    0.0
}();
c next println;
c next println;
c next println;
c next println;

-- Trailing if-else and match in a coroutine lambda take the same path.
let d = coro fn(flag Bool) Int {
    yield 1;
    if (flag) { 2 } else { 3 }
}(true);
d next println;
d next println;

let e = coro fn(n Int) Int {
    yield n;
    match (n) { 1: 10; _: 20; }
}(1);
e next println;
e next println;

-- The named form, for comparison.
coro fn named() Float { yield 7.0; 0.0 }
let f = named();
f next println;
f next println;
"done" println;
