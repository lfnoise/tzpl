-- Auto-mapping a scalar function over a persistent vector whose elements are
-- mutable arrays (#[[Int]]) used to crash with std::bad_alloc: auto-map over a
-- pvec yields a pvec at every level (#[#[Int]]), but emitPVecCallLevel peeled
-- the source assuming uniform pvec nesting and read the inner [Int] array as a
-- pvec. The fix freezes an array-typed source element to a pvec before
-- recursing. Mixed array/pvec nesting at any depth now works.
fn add1(x Int) Int = x + 1;

-- pvec of arrays, scalar fn (the crashing case) -> all-pvec result
(#[[1, 2], [3, 4]] add1) println;      -- #[#[2, 3], #[4, 5]]
(#[[1, 2], [3, 4]] @@ add1) println;   -- explicit depth-2, same

-- pvec of pvecs and array of pvecs (already worked; guard against regression)
(#[#[1, 2], #[3, 4]] add1) println;    -- #[#[2, 3], #[4, 5]]
([#[1, 2], #[3, 4]] add1) println;     -- #[#[2, 3], #[4, 5]]

-- deeper mixed nesting: pvec of array of array
(#[[[1, 2]], [[3]]] add1) println;     -- #[#[#[2, 3]], #[#[4]]]

-- pure array nesting still preserves array structure
([[1, 2], [3, 4]] add1) println;       -- [[2, 3], [4, 5]]

-- a function that consumes the array element (no inner auto-map) is unaffected
fn firstOf(a [Int]) Int = a[0];
(#[[10, 20], [30, 40]] map(firstOf)) println;  -- #[10, 30]
