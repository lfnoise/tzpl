-- Random number generation builtins

-- urand() -> Float: uniform [0.0, 1.0)
fn test_urand() {
    var i = 0;
    var ok = true;
    while (i < 1000) {
        let x = urand();
        if (x < 0.0 || x >= 1.0) { ok = false; }
        i = i + 1;
    }
    ok println;
}
test_urand();

-- brand() -> Float: bipolar [-1.0, 1.0)
fn test_brand() {
    var i = 0;
    var ok = true;
    while (i < 1000) {
        let x = brand();
        if (x < -1.0 || x >= 1.0) { ok = false; }
        i = i + 1;
    }
    ok println;
}
test_brand();

-- irand(lo, hi) -> Int: uniform [lo, hi]
fn test_irand_range() {
    var i = 0;
    var ok = true;
    while (i < 1000) {
        let x = irand(5, 15);
        if (x < 5 || x > 15) { ok = false; }
        i = i + 1;
    }
    ok println;
}
test_irand_range();

-- irand: reversed args should still work (lo > hi)
fn test_irand_reversed() {
    var i = 0;
    var ok = true;
    while (i < 100) {
        let x = irand(15, 5);
        if (x < 5 || x > 15) { ok = false; }
        i = i + 1;
    }
    ok println;
}
test_irand_reversed();

-- irand: single value range
(irand(7, 7) == 7) println;

-- irand: not constant-folded (different values on repeated calls)
fn test_irand_not_folded() {
    var saw_different = false;
    let first = irand(1, 1000000);
    var i = 0;
    while (i < 20) {
        if (irand(1, 1000000) != first) { saw_different = true; }
        i = i + 1;
    }
    saw_different println;
}
test_irand_not_folded();

-- xrand(lo, hi) -> Float: exponentially distributed [lo, hi)
fn test_xrand_range() {
    var i = 0;
    var ok = true;
    while (i < 1000) {
        let x = xrand(1.0, 100.0);
        if (x < 1.0 || x >= 100.0) { ok = false; }
        i = i + 1;
    }
    ok println;
}
test_xrand_range();

-- xrand: not constant-folded
fn test_xrand_not_folded() {
    var saw_different = false;
    let first = xrand(1.0, 1000000.0);
    var i = 0;
    while (i < 20) {
        if (xrand(1.0, 1000000.0) != first) { saw_different = true; }
        i = i + 1;
    }
    saw_different println;
}
test_xrand_not_folded();

-- pick([T]) -> T: choose a random element
fn test_pick_int() {
    let a = [10, 20, 30, 40, 50];
    var i = 0;
    var ok = true;
    while (i < 100) {
        let x = pick(a);
        if (x != 10 && x != 20 && x != 30 && x != 40 && x != 50) { ok = false; }
        i = i + 1;
    }
    ok println;
}
test_pick_int();

-- pick with float array
fn test_pick_float() {
    let a = [1.5, 2.5, 3.5];
    var i = 0;
    var ok = true;
    while (i < 100) {
        let x = pick(a);
        if (x != 1.5 && x != 2.5 && x != 3.5) { ok = false; }
        i = i + 1;
    }
    ok println;
}
test_pick_float();

-- pick with string array
fn test_pick_string() {
    let a = ["hello", "world"];
    var i = 0;
    var ok = true;
    while (i < 100) {
        let x = pick(a);
        if (x != "hello" && x != "world") { ok = false; }
        i = i + 1;
    }
    ok println;
}
test_pick_string();

-- pick with single-element array
(pick([42]) == 42) println;

-- picks([T]) -> List<T>: infinite lazy list of random picks
fn test_picks_list() {
    let a = [10, 20, 30];
    var xs = picks(a) take(100);
    var i = 0;
    var ok = true;
    while (i < 100) {
        let x = xs head;
        if (x != 10 && x != 20 && x != 30) { ok = false; }
        xs = xs tail;
        i = i + 1;
    }
    ok println;
}
test_picks_list();

-- picks list: values are random (not all the same)
fn test_picks_list_varied() {
    let xs = picks([1, 2, 3, 4, 5]) take(50);
    var saw_different = false;
    let first = xs head;
    var cur = xs tail;
    var i = 0;
    while (i < 49) {
        if (cur head != first) { saw_different = true; }
        cur = cur tail;
        i = i + 1;
    }
    saw_different println;
}
test_picks_list_varied();

-- picks([T], Int) -> [T]: array of n random picks
fn test_picks_array() {
    let a = [10, 20, 30];
    let r = picks(a, 50);
    (r length == 50) println;
    var i = 0;
    var ok = true;
    while (i < 50) {
        let x = r[i];
        if (x != 10 && x != 20 && x != 30) { ok = false; }
        i = i + 1;
    }
    ok println;
}
test_picks_array();

-- picks array: zero length
(picks([1, 2, 3], 0) length == 0) println;

-- picks with string array
fn test_picks_string() {
    let a = ["cat", "dog"];
    let r = picks(a, 20);
    var i = 0;
    var ok = true;
    while (i < 20) {
        let x = r[i];
        if (x != "cat" && x != "dog") { ok = false; }
        i = i + 1;
    }
    ok println;
}
test_picks_string();

-- rand(lo, hi) -> Float: uniform [lo, hi)
fn test_rand_range() {
    var i = 0;
    var ok = true;
    while (i < 1000) {
        let x = rand(5.0, 10.0);
        if (x < 5.0 || x >= 10.0) { ok = false; }
        i = i + 1;
    }
    ok println;
}
test_rand_range();

-- rands(lo, hi) -> List<Float>: infinite lazy list of uniform [lo, hi)
fn test_rands_list() {
    var xs = rands(5.0, 10.0) take(100);
    var i = 0;
    var ok = true;
    while (i < 100) {
        let x = xs head;
        if (x < 5.0 || x >= 10.0) { ok = false; }
        xs = xs tail;
        i = i + 1;
    }
    ok println;
}
test_rands_list();

-- rands(n, lo, hi) -> [Float]: array of n uniform [lo, hi)
fn test_rands_array() {
    let r = rands(100, 5.0, 10.0);
    (r length == 100) println;
    var i = 0;
    var ok = true;
    while (i < 100) {
        if (r[i] < 5.0 || r[i] >= 10.0) { ok = false; }
        i = i + 1;
    }
    ok println;
}
test_rands_array();

-- urands() -> List<Float>: infinite lazy list of uniform [0, 1)
fn test_urands_list() {
    var xs = urands() take(100);
    var i = 0;
    var ok = true;
    while (i < 100) {
        let x = xs head;
        if (x < 0.0 || x >= 1.0) { ok = false; }
        xs = xs tail;
        i = i + 1;
    }
    ok println;
}
test_urands_list();

-- urands(n) -> [Float]: array of n uniform [0, 1)
fn test_urands_array() {
    let r = urands(100);
    (r length == 100) println;
    var i = 0;
    var ok = true;
    while (i < 100) {
        if (r[i] < 0.0 || r[i] >= 1.0) { ok = false; }
        i = i + 1;
    }
    ok println;
}
test_urands_array();

-- brands() -> List<Float>: infinite lazy list of bipolar [-1, 1)
fn test_brands_list() {
    var xs = brands() take(100);
    var i = 0;
    var ok = true;
    while (i < 100) {
        let x = xs head;
        if (x < -1.0 || x >= 1.0) { ok = false; }
        xs = xs tail;
        i = i + 1;
    }
    ok println;
}
test_brands_list();

-- brands(n) -> [Float]: array of n bipolar [-1, 1)
fn test_brands_array() {
    let r = brands(100);
    (r length == 100) println;
    var i = 0;
    var ok = true;
    while (i < 100) {
        if (r[i] < -1.0 || r[i] >= 1.0) { ok = false; }
        i = i + 1;
    }
    ok println;
}
test_brands_array();

-- irands(lo, hi) -> List<Int>: infinite lazy list of uniform ints [lo, hi]
fn test_irands_list() {
    var xs = irands(5, 15) take(100);
    var i = 0;
    var ok = true;
    while (i < 100) {
        let x = xs head;
        if (x < 5 || x > 15) { ok = false; }
        xs = xs tail;
        i = i + 1;
    }
    ok println;
}
test_irands_list();

-- irands(n, lo, hi) -> [Int]: array of n uniform ints [lo, hi]
fn test_irands_array() {
    let r = irands(100, 5, 15);
    (r length == 100) println;
    var i = 0;
    var ok = true;
    while (i < 100) {
        if (r[i] < 5 || r[i] > 15) { ok = false; }
        i = i + 1;
    }
    ok println;
}
test_irands_array();

-- xrands(lo, hi) -> List<Float>: infinite lazy list of exponential [lo, hi)
fn test_xrands_list() {
    var xs = xrands(1.0, 100.0) take(100);
    var i = 0;
    var ok = true;
    while (i < 100) {
        let x = xs head;
        if (x < 1.0 || x >= 100.0) { ok = false; }
        xs = xs tail;
        i = i + 1;
    }
    ok println;
}
test_xrands_list();

-- xrands(n, lo, hi) -> [Float]: array of n exponential [lo, hi)
fn test_xrands_array() {
    let r = xrands(100, 1.0, 100.0);
    (r length == 100) println;
    var i = 0;
    var ok = true;
    while (i < 100) {
        if (r[i] < 1.0 || r[i] >= 100.0) { ok = false; }
        i = i + 1;
    }
    ok println;
}
test_xrands_array();
