-- QA: Edge cases in Option type

-- Option from map lookup
let m = ["a": 1, "b": 2];
let found = m["a"];
let not_found = m["c"];

found println;
not_found println;

-- unwrap
found unwrap println;

-- unwrapOr
found unwrapOr(99) println;
not_found unwrapOr(99) println;

-- isSome / isNone
found isSome println;
found isNone println;
not_found isSome println;
not_found isNone println;

-- Option in match
fn process(opt Option<Int>) String {
    match (opt) {
        Option.some(v): return fmt("got %^", v);
        Option.none: return "nothing";
    }
    "unreachable"
}

process(Option.some(42)) println;
process(Option<Int>.none) println;

-- Chained map lookups
let m2 = ["x": 10, "y": 20];
m2["x"] unwrapOr(0) println;
m2["z"] unwrapOr(0) println;

-- Option ordinal and tag
ordinal(Option.some(1)) println;
ordinal(Option<Int>.none) println;
tag(Option.some(1)) println;
tag(Option<Int>.none) println;
