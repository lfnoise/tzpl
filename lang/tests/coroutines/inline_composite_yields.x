-- Phase 4g.12: coroutines yielding inline composite types travel through
-- yield/resume/next/toList without box/unbox round-trips.

-- Fraction yields (2-word Inline) ------------------------------------

coro fn frac_walk(n Int) Fraction {
    var i = 1;
    while (i <= n) {
        yield 1/i;
        i = i + 1;
    }
}

println("-- Fraction via for-loop --");
for (f : 3 frac_walk) {
    f println;
}

println("-- Fraction via next --");
let fw = 3 frac_walk;
fw next println;
fw next println;
fw next println;
fw next println;

println("-- Fraction via toList --");
3 frac_walk toList println;

-- Tuple yields (2-word Inline) ---------------------------------------

coro fn ipairs(n Int) (Int, Int) {
    var i = 0;
    while (i < n) {
        yield (i, i * i);
        i = i + 1;
    }
}

println("-- Tuple via for-loop --");
for (p : 3 ipairs) {
    p println;
}

println("-- Tuple via next --");
let ip = 3 ipairs;
ip next println;
ip next println;
ip next println;
ip next println;

println("-- Tuple via toList --");
3 ipairs toList println;

-- Struct yields (Inline composite) -----------------------------------

struct Point { x Int; y Int; }

coro fn point_walk(n Int) Point {
    var i = 0;
    while (i < n) {
        yield Point { x: i, y: i * i };
        i = i + 1;
    }
}

println("-- Struct via for-loop --");
for (p : 3 point_walk) {
    p println;
}

println("-- Struct via toList --");
3 point_walk toList println;
