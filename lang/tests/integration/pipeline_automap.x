-- Pipeline + automap integration

fn double(x Int) Int = x * 2;
fn inc(x Int) Int = x + 1;
fn add(a Int, b Int) Int = a + b;

-- Pipeline with auto-mapping
[1, 2, 3] double println;

-- Chained pipelines on arrays
[1, 2, 3] double println;
[10, 20, 30] inc println;

-- Pipeline with explicit @
[[1, 2], [3, 4]] @ reverse println;

-- Nested automap with pipeline
let nested = [[1, 2, 3], [4, 5, 6]];
nested double println;

-- Auto-map add with scalar
[1, 2, 3] add(10) println;

-- Auto-map add with two arrays (zip)
[1, 2, 3] add([10, 20, 30]) println;

-- Pipeline chaining with lambda
let result = [1, 2, 3, 4, 5] map(fn(x Int) { x * x }) filter(fn(x Int) { x > 5 });
result println;

-- Sort and reverse
[5, 3, 1, 4, 2] sort reverse println;

-- Take and drop chained
[1, 2, 3, 4, 5, 6, 7, 8, 9, 10] take(5) reverse println;
