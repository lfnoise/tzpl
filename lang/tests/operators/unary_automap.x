-- Test: unary ! and ~ auto-map over arrays, tuples, and lists

-- Logical not on arrays
println(![true, false, true]);            -- expect: [false, true, false]
println(![[true, false], [false, true]]); -- expect: [[false, true], [true, false]]

-- Bitwise not on arrays
println(~[0, 1, -1]);                    -- expect: [-1, -2, 0]
println(~[[0, 1], [-1, 2]]);             -- expect: [[-1, -2], [0, -3]]

-- Logical not on tuples
println(!(true, false, true));            -- expect: (false, true, false)

-- Bitwise not on tuples
println(~(0, 1, -1));                    -- expect: (-1, -2, 0)

-- Logical not on list
let bools = [true, false, true] toList;
println(!bools);                          -- expect: [false, true, false ...]

-- Bitwise not on list
let ints = [0, 1, -1] toList;
println(~ints);                           -- expect: [-1, -2, 0 ...]

-- Verify negation still works (regression)
println(-[1, 2, 3]);                     -- expect: [-1, -2, -3]
println(-(1, 2, 3));                     -- expect: (-1, -2, -3)
