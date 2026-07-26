-- Auto-mapped '$': the elementwise form accepts every operand pair the scalar
-- operator does. Before the fix the per-element path fell through to the
-- numeric opcodes, so string elements were "added" as raw pointers (crash) and
-- array/tuple elements were added componentwise instead of concatenated.

-- Strings
println(["a", "b"] @ $ "!");
println("<" $ ["a", "b"] @);
println(["a", "b"] @ $ ["c", "d"] @);

-- Arrays as elements
println([[1, 2], [3]] @ $ [9]);
println([[1, 2], [3]] @ $ [[9], [8]] @);

-- Lists
println(List("a", "b") @ $ "!");
println(List("a", "b") @ $ ["c", "d"] @);
println(["a", "b"] @ $ List("c", "d") @);

-- Persistent vectors
println(#["a", "b"] @ $ "!");
println(#[#["a"], #["b"]] @@ $ "!");

-- Tuples as elements
println([(1, 2), (3, 4)] @ $ (9,));

-- Deep (@@) and Cartesian (@1/@2) forms
println([["a", "b"], ["c"]] @@ $ "!");
println(["a", "b"] @1 $ ["x", "y"] @2);

-- A user-defined '$' overload still wins over the built-in rule
struct Tag { s String }
fn $(a Tag, b Tag) Tag { Tag { s: a.s $ "+" $ b.s } }
println([Tag{"x"}, Tag{"y"}] @ $ Tag{"z"});

-- Plain (non-mapped) concatenation is unchanged
println("a" $ "b");
println([1, 2] $ [3]);
println(List(1, 2) $ List(3));
println(#[1, 2] $ #[3]);
println((1, 2) $ (3,));
