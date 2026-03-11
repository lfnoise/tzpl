-- QA: Deep auto-mapping edge cases

-- Auto-map unary negation on array
println(-[1, 2, 3]);          -- [-1, -2, -3]
println(-[-1, 0, 1]);         -- [1, 0, -1]

-- Auto-map unary negation on nested array
println(-[[1, 2], [3, 4]]);   -- [[-1, -2], [-3, -4]]

-- BUG: ! and ~ don't auto-map (see bug_unary_automap.x)
-- Workaround: use map
println([true, false, true] map(fn(x Bool) Bool { !x }));
println([0, 1, -1] map(fn(x Int) Int { ~x }));

-- Auto-map function over array of arrays
fn sum_arr(arr [Int]) Int = arr fold(0, fn(a Int, b Int) Int { a + b });
[[1, 2, 3], [4, 5], [6]] sum_arr println;

-- Auto-map with @ on nested structure
fn inc(x Int) Int = x + 1;
[[1, 2], [3, 4]] @ map(inc) println;

-- Auto-map struct construction with multiple array fields
struct Pair { a Int, b Int }
let p = Pair { a: [1, 2, 3], b: [10, 20, 30] };
p println;

-- Chained auto-map operations
fn double(x Int) Int = x * 2;
fn isPositive(x Int) Bool = x > 0;
[-2, -1, 0, 1, 2] map(double) map(isPositive) println;

-- Auto-map on list
fn square(x Int) Int = x * x;
List(1, 2, 3, 4) square println;

-- Auto-map preserves list type
List(1, 2, 3) double println;

-- Negation auto-map on tuple
println(-(1, 2, 3));
