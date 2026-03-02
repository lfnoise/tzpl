-- QA: Template enum type inference - NOT A BUG
-- When a template enum has multiple type params, all must be specified
-- if only one case constrains some of them. This is correct behavior:
-- Result.ok(42) can't infer E because E is unconstrained.

enum MyResult<T, E> {
    ok T,
    err E
}

-- Explicit type annotations required (correct behavior):
let s = MyResult<Int, String>.ok(42);
let f = MyResult<Int, String>.err("oops");
s println;
f println;

-- Option works because it only has one type param
let opt = Option.some(42);
opt println;

-- Option.none also needs explicit type (correct — nothing to infer from)
let n = Option<Int>.none;
n println;
