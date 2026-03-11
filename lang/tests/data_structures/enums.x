-- Enum tests

-- Basic enum
enum Option {
    some Int,
    none
}

let x = Option.some(42);
let y = Option.none;
x println;
y println;

-- Enum with string
enum Result {
    ok String,
    error String
}

let success = Result.ok("Hello!");
let failure = Result.error("Oops");
success println;
failure println;

-- Multiple types
enum Value {
    integer Int,
    floating Float,
    text String,
    flag Bool,
    nothing
}

Value.integer(100) println;
Value.floating(3.14) println;
Value.text("hello") println;
Value.flag(true) println;
Value.nothing println;

-- As function parameter
fn showOption(opt Option) Void {
    opt println;
}
showOption(Option.some(99));
showOption(Option.none);

-- As function return
fn makeOption(val Int) Option = Option.some(val);
7 makeOption println;

-- Pattern matching
fn unwrap(opt Option) Int {
    match (opt) {
        Option.some(v): return v;
        Option.none: return -1;
    }
    -1
}
Option.some(42) unwrap println;
Option.none unwrap println;

-- ordinal (case index)
ordinal(Option.some(42)) println;
ordinal(Option.none) println;
ordinal(Value.integer(100)) println;
ordinal(Value.floating(3.14)) println;
ordinal(Value.text("hello")) println;
ordinal(Value.flag(true)) println;
ordinal(Value.nothing) println;
