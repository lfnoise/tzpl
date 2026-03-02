-- Test enum/enum declarations and construction

-- Basic enum with mixed cases (data and no-data)
enum Option {
    some Int,
    none
}

-- Union with all data cases
enum Result {
    ok String,
    error String
}

-- Union with multiple types
enum Value {
    integer Int,
    floating Float,
    text String,
    flag Bool,
    nothing
}

-- Construct and print enum cases
let x = Option.some(42);
let y = Option.none;
println(x);
println(y);

let success = Result.ok("Hello!");
let failure = Result.error("Oops");
println(success);
println(failure);

-- Multiple types
let v1 = Value.integer(100);
let v2 = Value.floating(3.14);
let v3 = Value.text("hello");
let v4 = Value.flag(true);
let v5 = Value.nothing;
println(v1);
println(v2);
println(v3);
println(v4);
println(v5);

-- Enum as function parameter
fn showOption(opt Option) Void {
    println(opt);
}

showOption(Option.some(99));
showOption(Option.none);

-- Enum as function return value
fn makeOption(val Int) Option = Option.some(val);

let result = makeOption(7);
println(result);
