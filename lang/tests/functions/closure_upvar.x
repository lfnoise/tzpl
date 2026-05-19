-- Lua-style upvalues: closures capture mutable `var` by reference.
-- `let` is still captured by value.

-- Basic: read-after-write visible through the closure
fn basic() Void {
    var x = 10;
    let f = fn() Int { x };
    x = 20;
    f() println;        -- 20
    x = 30;
    f() println;        -- 30
}
basic();

-- Writeback: mutation inside the closure visible to the enclosing scope
fn writeback() Void {
    var x = 1;
    let inc = fn() Void { x = x + 1; };
    inc(); inc(); inc();
    x println;          -- 4
}
writeback();

-- Sharing: sibling closures over the same var see each other's writes
fn shared() Void {
    var counter = 0;
    let bump = fn() Void { counter = counter + 1; };
    let peek = fn() Int { counter };
    bump(); bump(); bump();
    peek() println;     -- 3
    bump();
    peek() println;     -- 4
}
shared();

-- Close-on-return: closure outlives its defining frame
fn makeCounter() fn() Int {
    var n = 0;
    fn() Int {
        n = n + 1;
        n
    }
}
let c1 = makeCounter();
let c2 = makeCounter();
c1() println;           -- 1
c1() println;           -- 2
c2() println;           -- 1 (independent counter)
c1() println;           -- 3

-- Nested closures sharing the same captured var
fn nested() Void {
    var x = 100;
    let outer = fn() Void {
        let inner = fn() Int { x };
        x = x + 1;
        inner() println;
    };
    outer();            -- 101
    outer();            -- 102
    x println;          -- 102
}
nested();

-- Captured `let` still snapshots
fn letUnchanged() Void {
    let v = 7;
    let f = fn() Int { v };
    f() println;        -- 7
}
letUnchanged();

-- Multi-word value type captured by reference (Complex)
fn complexUpvar() Void {
    var c = 1.0 + 2.0i;
    let read = fn() Complex { c };
    c = 5.0 + 6.0i;
    read() println;     -- 5+6i
    let bump = fn() Void { c = c + (10.0 + 0.0i); };
    bump();
    read() println;     -- 15+6i
}
complexUpvar();

-- Multi-word value type captured by value (bug-fix regression: both
-- inline words must be copied, not just the first one).
fn letComplex() Void {
    let c = 3.0 + 4.0i;
    let f = fn() Complex { c };
    f() println;        -- 3+4i
}
letComplex();

-- String capture: exercises the closed-store write-barrier path
fn stringUpvar() Void {
    var s = "hello";
    let read = fn() String { s };
    s = "world";
    read() println;     -- world
    let bump = fn() Void { s = s $ "!"; };
    bump(); bump();
    read() println;     -- world!!
}
stringUpvar();
