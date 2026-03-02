-- QA: FIXED - Closures can capture variables from any ancestor lambda scope
-- Previously, a closure nested 3+ levels deep could only capture from its
-- immediate parent and top-level scope. Now captures propagate through
-- intermediate lambdas automatically.

-- 2 levels deep (works):
let x = 100;
let f2 = fn(a Int) {
    let g = fn(b Int) Int { x + a + b };
    g(3)
};
f2(2) println;   -- 105

-- 3 levels deep (was BUG, now fixed):
let f3 = fn(a Int) {
    let g = fn(b Int) {
        let h = fn(c Int) Int { x + a + b + c };
        h(4)
    };
    g(3)
};
f3(2) println;   -- 109

-- h CAN capture from g (parent) and x (top-level):
let f3_workaround = fn(a Int) {
    let g = fn(b Int) {
        let h = fn(c Int) Int { x + b + c };
        h(4)
    };
    g(a + 3)  -- pass a's contribution through g's parameter
};
f3_workaround(2) println;  -- 100 + 5 + 4 = 109

-- Rebinding to a local also works now:
let f4 = fn(a Int) {
    let a2 = a;
    let g = fn(b Int) {
        let h = fn(c Int) Int { x + a2 + b + c };
        h(4)
    };
    g(3)
};
f4(2) println;   -- 109

-- 4 levels deep (captures from great-grandparent):
let f5 = fn(a Int) {
    let g = fn(b Int) {
        let h = fn(c Int) {
            let k = fn(d Int) Int { x + a + b + c + d };
            k(5)
        };
        h(4)
    };
    g(3)
};
f5(2) println;   -- 100 + 2 + 3 + 4 + 5 = 114
