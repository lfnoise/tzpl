-- QA: Edge cases in string formatting

-- Basic fmt
"%^ + %^ = %^" fmt(1, 2, 3) println;

-- Indexed placeholders
"%0 and %1 and %0" fmt("x", "y") println;

-- Literal percent
"100%% done" fmt() println;

-- No placeholders
"no placeholders" fmt() println;

-- Single placeholder
"value: %^" fmt(42) println;

-- Multiple types
"int=%^, float=%^, str=%^, bool=%^" fmt(42, 3.14, "hi", true) println;

-- Array in fmt
"arr: %^" fmt([1, 2, 3]) println;

-- Tuple in fmt
"tup: %^" fmt((1, "hello")) println;

-- List in fmt
"list: %^" fmt(List(1, 2, 3)) println;

-- Empty string format
"" fmt() println;

-- All indexed
"%0%1%2" fmt("a", "b", "c") println;

-- Repeated index
"%0%0%0" fmt("x") println;

-- fmt with struct
struct Point { x Int, y Int }
"point: %^" fmt(Point{3, 4}) println;

-- fmt with fraction
"frac: %^" fmt(1/3) println;

-- Complex number in fmt
"complex: %^" fmt(3.0 + 4i) println;
