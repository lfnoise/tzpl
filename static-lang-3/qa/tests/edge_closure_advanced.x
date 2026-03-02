-- QA: Advanced closure edge cases

-- Closure capturing multiple values
let a = 1;
let b = 2;
let c = 3;
let sum_abc = fn(x Int) Int { x + a + b + c };
sum_abc(10) println;   -- 16

-- Closure in array
let fns = [
    fn(x Int) Int { x + 1 },
    fn(x Int) Int { x * 2 },
    fn(x Int) Int { x * x }
];
fns[0](5) println;    -- 6
fns[1](5) println;    -- 10
fns[2](5) println;    -- 25

-- Closure factory in a loop
var multipliers [(Int) Int] = [];
for (i : (1..5)) {
    let n = i;
    multipliers = multipliers push(fn(x Int) Int { x * n });
}
multipliers[0](10) println;   -- 10
multipliers[1](10) println;   -- 20
multipliers[2](10) println;   -- 30
multipliers[3](10) println;   -- 40
multipliers[4](10) println;   -- 50

-- Closure over Ref (shared mutable state)
let count = &0;
let increment = fn(n Int) { count <- *count + n; };
let get_count = fn(dummy Int) Int { *count };
increment(5);
increment(3);
get_count(0) println;  -- 8

-- 2-level nested closure (works)
let x = 1;
let f1 = fn(aa Int) {
    let g = fn(bb Int) Int { x + aa + bb };
    g(3)
};
f1(2) println;   -- 6

-- BUG: 3-level nested can't capture grandparent (see bug_closure_depth.x)
-- Workaround: pass through parameters
let f2 = fn(aa Int) {
    let g = fn(bb Int) {
        let h = fn(cc Int) Int { x + bb + cc };
        h(4)
    };
    g(aa + 3)
};
f2(2) println;   -- 100... no, x=1: 1 + 5 + 4 = 10

-- Closure as return value from closure
fn make_adder(n Int) (Int) Int {
    return fn(x2 Int) Int { x2 + n };
}
let add10 = make_adder(10);
let add20 = make_adder(20);
add10(5) println;    -- 15
add20(5) println;    -- 25

-- Closure with default argument function
fn apply_default(f (Int) Int, x2 Int = 0) Int = f(x2);
apply_default(fn(x3 Int) Int { x3 + 100 }) println;     -- 100
apply_default(fn(x3 Int) Int { x3 + 100 }, 42) println;  -- 142
