-- Explicit @ requests auto-mapping when constructing GENERIC (template)
-- structs and enums: the type parameter binds from the element type, and one
-- value is built per element. Without @, an array binds the type parameter to
-- the array type (so Box<[Int]> is still constructible).

-- Generic tuple struct (already rode the call auto-map path; kept for coverage)
struct Box<T>(T);
println(Box([1, 2, 3] @));
-- Expected: [Box<Int>(1), Box<Int>(2), Box<Int>(3)]

-- Generic struct literal, explicit @
struct Cell<T> { v T }
println(Cell { v: [1, 2, 3] @ });
-- Expected: [Cell<Int> { v: 1 }, Cell<Int> { v: 2 }, Cell<Int> { v: 3 }]

-- No @ keeps the whole array as the payload (binds T := [Int])
println(Cell { v: [1, 2, 3] });
-- Expected: Cell<[Int]> { v: [1, 2, 3] }

-- Generic enum, explicit @
enum Opt<T> { some T, none }
println(Opt.some([10, 20, 30] @));
-- Expected: [Opt<Int>.some(10), Opt<Int>.some(20), Opt<Int>.some(30)]

println(Opt.some([1, 2, 3]));
-- Expected: Opt<[Int]>.some([1, 2, 3])

-- String element type
println(Opt.some(["a", "b"] @));
-- Expected: [Opt<String>.some(a), Opt<String>.some(b)]

-- Cartesian @1/@2 over a two-parameter generic struct literal
struct Pair<T, U> { a T, b U }
println(Pair { a: [1, 2] @1, b: ["x", "y"] @2 });
-- Expected: [[Pair<Int, String> { a: 1, b: x }, Pair<Int, String> { a: 1, b: y }], [Pair<Int, String> { a: 2, b: x }, Pair<Int, String> { a: 2, b: y }]]

-- Mixed: one @ field, one broadcast scalar field
println(Pair { a: [1, 2, 3] @, b: 9 });
-- Expected: [Pair<Int, Int> { a: 1, b: 9 }, Pair<Int, Int> { a: 2, b: 9 }, Pair<Int, Int> { a: 3, b: 9 }]
