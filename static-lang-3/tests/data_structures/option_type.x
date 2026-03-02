-- Option type: built-in Option<T> enum

-- Map subscript returns Option
let m = ["a": 1, "b": 2];
m["a"] println;
m["z"] println;

-- unwrap extracts value from some
m["a"] unwrap println;

-- unwrapOr provides default for none
m["z"] unwrapOr(99) println;
m["a"] unwrapOr(99) println;

-- isSome and isNone
m["a"] isSome println;
m["a"] isNone println;
m["z"] isSome println;
m["z"] isNone println;

-- get(map, key) returns Option
get(m, "a") println;
get(m, "z") println;
get(m, "a") unwrap println;

-- get(map, key, default) returns V directly
get(m, "a", 0) println;
get(m, "z", 99) println;

-- Explicit Option construction
let o1 = Option.some(42);
o1 println;

let o2 = Option<Int>.none;
o2 println;

-- Pattern matching on map results
fn show(opt Option<Int>) Void {
    match (opt) {
        Option.some(x): x println;
        Option.none: "none" println;
    }
}
show(m["a"]);
show(m["z"]);

-- String-valued map
let sm = ["x": "hello", "y": "world"];
sm["x"] unwrap println;
sm["z"] unwrapOr("default") println;
sm["z"] isNone println;
