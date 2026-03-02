-- Test explicit postfix @ auto-mapping (the "each" operator)
-- Each test demonstrates behavior that DIFFERS from implicit auto-mapping.

--- @ selects a different overload ---
-- process([Int]) matches directly without @, returning arr[0].
-- With postfix @, the scalar overload process(Int) is selected and auto-mapped.
fn process(x Int) Int = x * 10;
fn process(arr [Int]) Int = arr[0];

let a = process([1, 2, 3]);
println(a);
-- Expected: 1

let b = process([1, 2, 3] @);
println(b);
-- Expected: [10, 20, 30]

--- @ on nested arrays with overloaded function ---
-- Without @: implicit auto-map at depth 1 selects process([Int]).
-- With @: explicit depth-1 unwrap also selects process([Int]) -- same result.
-- With @@: unwraps 2 levels to Int, selects process(Int), deep-maps.
let nested = [[1, 2], [3, 4]];

let c = process(nested);
println(c);
-- Expected: [1, 3]

let d = process(nested @@);
println(d);
-- Expected: [[10, 20], [30, 40]]

--- Cartesian product @1/@2 ---
-- Unique to explicit @. No implicit equivalent.
fn add(a Int, b Int) Int = a + b;
let cart = add([1, 2] @1, [10, 20] @2);
println(cart);
-- Expected: [[11, 21], [12, 22]]

--- Cartesian with more elements ---
fn mul(a Int, b Int) Int = a * b;
let cart2 = mul([1, 2, 3] @1, [10, 100] @2);
println(cart2);
-- Expected: [[10, 100], [20, 200], [30, 300]]

--- Implicit deep auto-mapping (depth 2) ---
-- Verifies the fix: implicit auto-mapping now works at any depth.
fn double(x Int) Int = x * 2;
let nested2 = [[1, 2, 3], [4, 5, 6]];
let doubled = double(nested2);
println(doubled);
-- Expected: [[2, 4, 6], [8, 10, 12]]

--- Implicit deep auto-mapping (depth 3) ---
let deep = [[[1, 2], [3, 4]], [[5, 6], [7, 8]]];
let tripled = double(deep);
println(tripled);
-- Expected: [[[2, 4], [6, 8]], [[10, 12], [14, 16]]]

--- Implicit deep auto-mapping with mixed scalar ---
let r_mixed = add(nested2, 10);
println(r_mixed);
-- Expected: [[11, 12, 13], [14, 15, 16]]
