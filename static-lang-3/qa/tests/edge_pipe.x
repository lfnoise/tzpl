-- QA: Edge cases in pipeline syntax

-- Basic space pipeline
fn double(x Int) Int = x * 2;
fn inc(x Int) Int = x + 1;

42 double println;
42 inc println;

-- Chained space pipeline
42 double inc println;  -- (42 * 2) + 1 = 85
42 inc double println;  -- (42 + 1) * 2 = 86

-- Pipeline with multi-arg function
fn add(a Int, b Int) Int = a + b;
42 add(10) println;  -- 52

-- Pipe operator |>
-- 42 |> double |> inc |> println;

-- Space pipeline with array functions
[3, 1, 4, 1, 5, 9] sort reverse println;

-- Pipeline with take/drop
[1, 2, 3, 4, 5] take(3) println;
[1, 2, 3, 4, 5] drop(2) println;
[1, 2, 3, 4, 5] take(3) reverse println;

-- Pipeline with map/filter
[1, 2, 3, 4, 5] map(fn(x Int) { x * x }) println;
[1, 2, 3, 4, 5] filter(fn(x Int) { x > 2 }) println;
[1, 2, 3, 4, 5] filter(fn(x Int) { x > 2 }) map(fn(x Int) { x * 2 }) println;

-- Pipeline with string functions
"hello world" length println;
"  hello  " trim println;
"hello" toUpper println;
"HELLO" toLower println;

-- Pipeline with math
-42 abs println;
3.7 floor println;
3.2 ceil println;

-- Pipeline with struct fields
struct Point { x Int, y Int }
let p = Point{3, 4};
p.x println;
p.y println;

-- Multiple results through pipeline
let arr = [5, 3, 1, 4, 2];
arr sort println;
arr reverse println;
arr sort reverse println;
arr sort take(3) println;

-- Pipeline with println (side effect)
"test" println;
42 println;
[1, 2, 3] println;
