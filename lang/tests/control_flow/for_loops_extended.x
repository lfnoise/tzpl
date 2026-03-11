-- Basic range
for (i : (1..5)) {
    println(i);
}

-- Stepped range
for (i : (0,2..10)) {
    println(i);
}

-- Descending range
for (i : (5,3..1)) {
    println(i);
}

-- Descending range with inferred step
for (i : (5..1)) {
    println(i);
}

-- For-loop over array
let arr = [10, 20, 30];
for (x : arr) {
    println(x);
}

-- For-loop over list
let lst = List(1, 2, 3);
for (x : lst) {
    println(x);
}

-- Range as value
let r = (1..5);
println(r);
println(length(r));
let a = toArray(r);
println(a);

let r2 = (5..1);
println(r2);
println(length(r2));
let a2 = toArray(r2);
println(a2);

let r3 = (0,7..100);
r3 toArray println;

-- toList (lazy)
let l1 = toList((1..5));
println(l1);

let l2 = toList((5..1));
println(l2);

let l3 = toList((0,2..10));
println(l3);

-- toList on infinite range (lazy, prints first 10 elements by default)
let linf = toList((1..));
println(linf);

-- ============================================================
-- Fraction ranges
-- ============================================================

-- Basic fraction range
for (i : (1/2..5/2)) {
    println(i);
}

-- Stepped fraction range
for (i : (1/4, 1/2..2/1)) {
    println(i);
}

-- Descending fraction range with inferred step
for (i : (3/1..1/2)) {
    println(i);
}

-- Descending fraction range with explicit step
for (i : (5/2, 2/1..1/2)) {
    println(i);
}

-- Fraction range as value
let fr1 = (1/2..5/2);
println(fr1);
println(length(fr1));
let fa1 = toArray(fr1);
println(fa1);

let fr2 = (3/1..1/2);
println(fr2);
println(length(fr2));
let fa2 = toArray(fr2);
println(fa2);

let fr3 = (1/4, 3/4..3/1);
println(fr3);
println(length(fr3));
println(toArray(fr3));

-- Single element fraction range
let fr4 = (1/2..1/2);
println(length(fr4));
println(toArray(fr4));

-- Fraction toList (lazy)
let fl1 = toList((1/3..7/3));
println(fl1);

let fl2 = toList((3/1..1/2));
println(fl2);

let fl3 = toList((1/4, 1/2..2/1));
println(fl3);

-- Infinite fraction range (lazy)
let flinf = toList((1/2..));
println(flinf);

-- Infinite fraction range with step (lazy)
let flinf2 = toList((1/4, 3/4..));
println(flinf2);
