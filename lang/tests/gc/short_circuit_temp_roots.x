-- The right operand of a short-circuit || (or &&) runs only on one path, so
-- its temporaries must not stay allocated past the expression: a String temp
-- of `s == "b"` would otherwise sit in every later stack map while holding,
-- on the skip path, whatever an earlier tenant of that register left (an Int)
-- -- and the GC would trace it as an object. The call after the `let` must
-- root s, t and the arrays, but not the right operand's "b" temporary.

fn helper(n Int) Int = n + 1;

fn pick(s String, k Int) Int {
    let width = k * 2;
    let either = s == "a" || s == "b";
    let t = s $ "!";
    let m = helper(width);
    either ? m + t length : m
}

disassemble(pick);
pick("b", 3) println;
pick("c", 3) println;
