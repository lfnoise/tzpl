-- Phase 4g.25: multi-word values flow correctly through trailing
-- expressions, if-expressions, match-expressions, block expressions,
-- and lambdas with multi-word return types.
--
-- Before this fix, multi-word return values used a 1-Word op_mov in
-- genBlockForValue/genSwitchStmtForValue, and the wrapping resultReg
-- in genIfExpr / function bodies was sized with allocReg() instead of
-- allocSlot(returnType). So `fn pick(b, x, y) Complex { if (b) { x }
-- else { y } }` returned `1+0i` (only word 0 copied; word 1 picked up
-- whatever was adjacent on the register stack).
--
-- Explicit `return` always worked because op_return reads sizeWords_
-- from its register; only the trailing-expression path was broken.

struct Pt { x Int; y Int }

-- --- Identity through trailing expression ---
fn id_c(c Complex) Complex { c }
fn id_f(f Fraction) Fraction { f }
fn id_p(p Pt) Pt { p }
fn id_t(t (Int, Int)) (Int, Int) { t }

id_c(1.0+2.0i) println;
id_f(3/7) println;
id_p(Pt{x:1, y:2}) println;
id_t((1, 2)) println;

-- --- if/else as trailing expression ---
fn pick_c(b Bool, x Complex, y Complex) Complex { if (b) { x } else { y } }
fn pick_f(b Bool, x Fraction, y Fraction) Fraction { if (b) { x } else { y } }
fn pick_p(b Bool, x Pt, y Pt) Pt { if (b) { x } else { y } }
fn pick_t(b Bool, x (Int, Int), y (Int, Int)) (Int, Int) {
    if (b) { x } else { y }
}

pick_c(true,  1.0+2.0i, 3.0+4.0i) println;
pick_c(false, 1.0+2.0i, 3.0+4.0i) println;
pick_f(true,  1/2, 3/4) println;
pick_f(false, 1/2, 3/4) println;
pick_p(true,  Pt{x:1, y:2}, Pt{x:3, y:4}) println;
pick_p(false, Pt{x:1, y:2}, Pt{x:3, y:4}) println;
pick_t(true,  (1, 2), (3, 4)) println;
pick_t(false, (1, 2), (3, 4)) println;

-- --- match as trailing expression ---
fn unwrap_or_c(opt Option<Complex>, d Complex) Complex {
    match (opt) {
        Option.some(c): c;
        Option.none: d;
    }
}
fn unwrap_or_p(opt Option<Pt>, d Pt) Pt {
    match (opt) {
        Option.some(p): p;
        Option.none: d;
    }
}

unwrap_or_c(Option.some(1.0+2.0i), 99.0+99.0i) println;
unwrap_or_c(Option<Complex>.none,  99.0+99.0i) println;
unwrap_or_p(Option.some(Pt{x:1, y:2}), Pt{x:9, y:9}) println;
unwrap_or_p(Option<Pt>.none,           Pt{x:9, y:9}) println;

-- --- Nested if returning multi-word ---
fn classify(c Complex, threshold Float) Complex {
    if (c == (1.0+2.0i)) {
        if (threshold > 0.5) { c } else { 0.0+0.0i }
    } else {
        c + (1.0+0.0i)
    }
}
classify(1.0+2.0i, 1.0) println;
classify(1.0+2.0i, 0.0) println;
classify(3.0+4.0i, 0.0) println;

-- --- Lambda with trailing if returning multi-word ---
let f = fn(b Bool, x Complex, y Complex) Complex { if (b) { x } else { y } };
f(true,  1.0+2.0i, 3.0+4.0i) println;
f(false, 1.0+2.0i, 3.0+4.0i) println;

-- --- let-bound if-expression with multi-word result ---
let c1 = if (true)  { 1.0+2.0i } else { 3.0+4.0i };
let c2 = if (false) { 1.0+2.0i } else { 3.0+4.0i };
c1 println;
c2 println;

let f1 = if (true)  { 1/2 } else { 3/4 };
let f2 = if (false) { 1/2 } else { 3/4 };
f1 println;
f2 println;

let p1 = if (true)  { Pt{x:1, y:2} } else { Pt{x:3, y:4} };
let p2 = if (false) { Pt{x:1, y:2} } else { Pt{x:3, y:4} };
p1 println;
p2 println;

-- --- Match returning multi-word with multiple cases ---
fn pick_match(b Int) Pt {
    match (b) {
        0: Pt{x:0, y:0};
        1: Pt{x:1, y:1};
        _: Pt{x:99, y:99};
    }
}
pick_match(0) println;
pick_match(1) println;
pick_match(7) println;

-- --- Map subscript Inline Option as trailing expression ---
let m = [1: 1.0+2.0i];
fn lookup(k Int) Option<Complex> { m[k] }
lookup(1) println;
lookup(2) println;

-- --- if-expression returning Inline Option (3-word value) ---
fn maybe(b Bool, x Complex) Option<Complex> {
    if (b) { Option.some(x) } else { Option<Complex>.none }
}
maybe(true,  1.0+2.0i) println;
maybe(false, 1.0+2.0i) println;

-- --- Lambda returning Inline Option ---
let lam = fn(b Bool, x Fraction) Option<Fraction> {
    if (b) { Option.some(x) } else { Option<Fraction>.none }
};
lam(true,  7/8) println;
lam(false, 7/8) println;

-- --- Trailing match producing Inline Option ---
fn flip(opt Option<Complex>) Option<Complex> {
    match (opt) {
        Option.some(c): Option<Complex>.none;
        Option.none: Option.some(99.0+99.0i);
    }
}
flip(Option.some(1.0+2.0i)) println;
flip(Option<Complex>.none) println;
