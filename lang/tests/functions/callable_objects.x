-- Callable objects: defining `call` makes a type callable via function-call syntax

-- Basic callable struct
struct Adder { amount Int }
fn call(a Adder, x Int) Int = x + a.amount;

let add5 = Adder { 5 };
add5(10) println;
add5(0) println;
add5(-3) println;

-- Multiple arities
struct Multiplier { factor Float }
fn call(m Multiplier, x Float) Float = x * m.factor;
fn call(m Multiplier, x Float, y Float) Float = (x + y) * m.factor;

let dbl = Multiplier { 2.0 };
dbl(3.0) println;
dbl(1.5, 2.5) println;

-- Overloaded call for different argument types
struct Formatter { prefix String }
fn call(f Formatter, x Int) String = f.prefix $ x toString;
fn call(f Formatter, x String) String = f.prefix $ x;

let fmt = Formatter { ">> " };
fmt(42) println;
fmt("hello") println;

-- Callable with UFCS (space piping) -- should work naturally
let r1 = add5 call(20);
r1 println;

-- Chained calls: first call returns another callable
struct Counter { start Int }
struct Stepper { base Int, step Int }
fn call(c Counter, step Int) Stepper = Stepper { c.start, step };
fn call(s Stepper, n Int) Int = s.base + s.step * n;

let counter = Counter { 100 };
counter(5)(3) println;

-- Template call function
struct Wrapper { value Int }
fn call<T>(w Wrapper, f (Int) T) T = f(w.value);

let w = Wrapper { 10 };
let sq = fn(x Int) Int { x * x };
w(sq) println;

-- Auto-mapping with callable objects (UFCS style)
let nums = [1, 2, 3, 4, 5];
nums @ add5 println;
