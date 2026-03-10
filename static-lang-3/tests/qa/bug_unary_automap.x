-- QA: BUG - Unary ! and ~ don't auto-map over arrays
-- Unary - auto-maps correctly, but ! and ~ don't.

-- Unary negation auto-maps (works):
println(-[1, 2, 3]);          -- [-1, -2, -3]
println(-[[1, 2], [3, 4]]);   -- [[-1, -2], [-3, -4]]

-- BUG: Unary ! doesn't auto-map over Bool array:
-- println(![true, false, true]);  -- Error: "Logical not requires boolean operand"
-- Expected: [false, true, false]

-- BUG: Unary ~ doesn't auto-map over Int array:
-- println(~[0, 1, -1]);  -- Error: "Bitwise not requires integer operand"
-- Expected: [-1, -2, 0]

-- Workaround for !: use map
println([true, false, true] map(fn(x Bool) Bool { !x }));

-- Workaround for ~: use map
println([0, 1, -1] map(fn(x Int) Int { ~x }));

-- Also test that - works on tuples and lists
println(-(1, 2, 3));          -- (-1, -2, -3)
println(-List(1, 2, 3));      -- should work?
