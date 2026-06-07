-- N-dimensional cartesian auto-mapping (depth > 2) over arrays,
-- persistent vectors, and mixtures. The nest depth follows the largest
-- cartesian dimension and is not capped.

fn add3(x Int, y Int, z Int) Int { x + y + z }
fn add4(w Int, x Int, y Int, z Int) Int { w + x + y + z }

-- --- arrays ---
add3([1, 2]@1, [10, 20]@2, [100, 200]@3) println;       -- 2x2x2 nested array
add4([1, 2]@1, [10, 20]@2, [100, 200]@3, [1000, 2000]@4) println;  -- depth 4
add3([1, 2, 3]@1, [10, 20]@2, [100]@3) println;         -- ragged dimensions

-- broadcast: a non-mapped scalar arg is reused at every combination
add3([1, 2]@1, [10, 20]@2, 1000) println;

-- transposed dimension assignment
add3([1, 2]@3, [10, 20]@2, [100, 200]@1) println;

-- --- persistent vectors ---
add3(#[1, 2]@1, #[10, 20]@2, #[100, 200]@3) println;    -- 2x2x2 nested #[...]
add4(#[1, 2]@1, #[10, 20]@2, #[100, 200]@3, #[1000, 2000]@4) println;

-- --- mixed array + persistent vector: each level's container kind follows
-- the argument supplying that cartesian dimension (dim1 array, dim2 #[...],
-- dim3 array) ---
add3([1, 2]@1, #[10, 20]@2, [100, 200]@3) println;
