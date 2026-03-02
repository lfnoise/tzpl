-- QA: Auto-mapping of binary operators (now fixed)
-- Previously caused SEGFAULT, now should work

-- Array > scalar
println([1, 2, 3] > 2);    -- [false, false, true]
println([1, 2, 3] < 2);    -- [true, false, false]
println([1, 2, 3] >= 2);   -- [false, true, true]
println([1, 2, 3] <= 2);   -- [true, true, false]
println([1, 2, 3] == 2);   -- [false, true, false]
println([1, 2, 3] != 2);   -- [true, false, true]

-- Scalar > array (should also auto-map)
println(2 > [1, 2, 3]);    -- [true, false, false]
println(2 < [1, 2, 3]);    -- [false, false, true]
println(2 == [1, 2, 3]);   -- [false, true, false]

-- Float comparisons
println([1.0, 2.0, 3.0] > 1.5);   -- [false, true, true]
println([1.0, 2.0, 3.0] < 2.5);   -- [true, true, false]

-- Arithmetic auto-mapping
println([1, 2, 3] + 10);     -- [11, 12, 13]
println([1, 2, 3] * 2);      -- [2, 4, 6]
println(10 - [1, 2, 3]);     -- [9, 8, 7]
println(10 + [1, 2, 3]);     -- [11, 12, 13]

-- Tuple > scalar
println((1, 2, 3) > 2);    -- (false, false, true)
println((1, 2, 3) + 10);   -- (11, 12, 13)

-- List > scalar
println(List(1, 2, 3) > 2);
println(List(1, 2, 3) + 10);

-- Nested: auto-map comparison on struct field
struct Point { x Int, y Int }
let pts = [Point{1, 10}, Point{2, 20}, Point{3, 30}];
println(pts.x > 1);       -- [false, true, true]
println(pts.y >= 20);     -- [false, true, true]
