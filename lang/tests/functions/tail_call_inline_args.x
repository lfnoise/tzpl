-- Regression: tail calls with multi-word Inline composite args.
-- op_tail_call_lambda / op_tail_call_template_lambda copied one word per
-- ARG (losing trailing words and shifting later args), and the lambda-call
-- opcodes placed free vars at numArgs (arg count) instead of after the
-- params' full WORD span.

struct N3 { a Float, b Float, c Float }   -- 3-word inline struct

-- expression body => tail call through a function-typed param
fn applyT(x N3, f (N3, Float, Float) Float) Float = f(x, 10.0, 20.0);

let n = N3 { a: 1.0, b: 2.0, c: 3.0 };

applyT(n, fn(v N3, p Float, q Float) Float {
    "v=%^ %^ %^ p=%^ q=%^" fmt(v.a, v.b, v.c, p, q) println;
    v.c + p + q
}) println;

-- named fn through the same tail-call path
fn take3(v N3, p Float, q Float) Float = v.b + p + q;
fn applyN(x N3, f (N3, Float, Float) Float) Float = f(x, 100.0, 200.0);
applyN(n, take3) println;

-- lambda with a capture AND an inline param: free var must land after the
-- param's word span, not at slot 1
let bias = 1000.0;
fn applyB(x N3, f (N3) Float) Float = f(x);
applyB(n, fn(v N3) Float { v.a + v.b + v.c + bias }) println;

-- non-tail (block body) sanity
fn applyBlock(x N3, f (N3, Float, Float) Float) Float {
    let r = f(x, 7.0, 8.0);
    r
}
applyBlock(n, fn(v N3, p Float, q Float) Float { v.a * 100.0 + p + q }) println;
