-- Depth @@ / @@@ auto-mapping for construction. A nested array with @@ builds
-- a nested array of constructed values, peeling one layer per @. Works across
-- tuple-struct, struct-literal, and enum construction, concrete and generic.

-- Concrete tuple struct @@ (this once produced pointer garbage; now correct)
struct TS(Int);
println(TS([[1, 2], [3, 4]] @@));
-- Expected: [[TS(1), TS(2)], [TS(3), TS(4)]]

-- Concrete struct literal @@
struct S { a Int }
println(S { a: [[1, 2], [3, 4]] @@ });
-- Expected: [[S { a: 1 }, S { a: 2 }], [S { a: 3 }, S { a: 4 }]]

-- Concrete enum @@
enum E { a Int, b Int }
println(E.a([[1, 2], [3, 4]] @@));
-- Expected: [[E.a(1), E.a(2)], [E.a(3), E.a(4)]]

-- Generic struct literal @@
struct Box<T> { v T }
println(Box { v: [[1, 2], [3, 4]] @@ });
-- Expected: [[Box<Int> { v: 1 }, Box<Int> { v: 2 }], [Box<Int> { v: 3 }, Box<Int> { v: 4 }]]

-- Generic enum @@
enum Opt<T> { some T, none }
println(Opt.some([[1, 2], [3, 4]] @@));
-- Expected: [[Opt<Int>.some(1), Opt<Int>.some(2)], [Opt<Int>.some(3), Opt<Int>.some(4)]]

-- Generic tuple struct @@ (also once garbage)
struct Wrap<T>(T);
println(Wrap([[1, 2], [3, 4]] @@));
-- Expected: [[Wrap<Int>(1), Wrap<Int>(2)], [Wrap<Int>(3), Wrap<Int>(4)]]

-- Depth 3 (@@@)
println(S { a: [[[1, 2]], [[3]]] @@@ });
-- Expected: [[[S { a: 1 }, S { a: 2 }]], [[S { a: 3 }]]]

-- @@ mapped field + broadcast scalar field
struct P<T, U> { a T, b U }
println(P { a: [[1, 2], [3, 4]] @@, b: 9 });
-- Expected: [[P<Int, Int> { a: 1, b: 9 }, P<Int, Int> { a: 2, b: 9 }], [P<Int, Int> { a: 3, b: 9 }, P<Int, Int> { a: 4, b: 9 }]]

-- Int -> Float element promotion at depth
struct F { a Float }
println(F { a: [[1, 2], [3, 4]] @@ });
-- Expected: [[F { a: 1.0 }, F { a: 2.0 }], [F { a: 3.0 }, F { a: 4.0 }]]
