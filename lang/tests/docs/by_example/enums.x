-- Enums from Language X by Example

enum Option {
    some Int,
    none
}

enum Result {
    ok String,
    error String
}

enum Value {
    integer Int,
    floating Float,
    text String,
    nothing
}

-- Construction
let x = Option.some(42);
let y = Option.none;
let ok = Result.ok("success");

x println;
y println;
ok println;

-- Pattern match to extract values
fn unwrap_or(opt Option, default Int) Int {
    match (opt) {
        Option.some(value): return value;
        Option.none: return default;
    }
    default
}

unwrap_or(Option.some(42), 0) println;
unwrap_or(Option.none, 99) println;

-- ordinal
enum Color { red, green, blue }

ordinal(Color.red) println;
ordinal(Color.green) println;
ordinal(Color.blue) println;

ordinal(Option.some(42)) println;
ordinal(Option.none) println;
