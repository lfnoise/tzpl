-- Variadic functions combined with explicit @ auto-mapping

-- Untyped variadic with @ auto-mapping
fn tup(...x) = x;

-- Single @ auto-maps one argument
tup(1, 2, [3, 4, 5]@, 6) println;

-- Multiple args, no @
tup(1, 2, 3) println;

-- @ on first arg
tup([10, 20]@, "b") println;

-- @ on last arg
tup("a", [10, 20]@) println;

-- Typed variadic with @ auto-mapping
fn sumAll(...xs Int) Int {
    var total = 0;
    for (x : xs) { total = total + x; }
    total
}
sumAll([1, 2, 3]@, 10) println;
sumAll(100, [1, 2, 3]@) println;

-- println with @ auto-mapping
[10, 20, 30] @ println;

-- print with @ auto-mapping
[1, 2, 3] @ print;
println();
