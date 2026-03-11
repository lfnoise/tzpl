-- Pipeline operators: |> and space pipeline

-- Space pipeline: x f === f(x)
fn double(x Int) Int = x * 2;
fn inc(x Int) Int = x + 1;
fn negate(x Int) Int = 0 - x;

5 double println;
5 inc println;

-- Pipe operator (|>)
3 + 4 |> double println;
5 * 2 |> inc println;
0 - 5 |> negate println;

-- Space pipeline with extra args: x f(y) === f(x, y)
fn add(a Int, b Int) Int = a + b;
fn mul(a Int, b Int) Int = a * b;
3 add(4) println;
5 mul(3) println;

-- Chaining: x f g === g(f(x))
5 double inc println;
5 inc double println;

-- Chaining with args
fn clamp(x Int, lo Int, hi Int) Int = x < lo ? lo : x > hi ? hi : x;
15 clamp(0, 10) println;
-5 clamp(0, 10) println;
5 clamp(0, 10) println;

-- Pipeline with println
3 double println;
7 inc println;

-- Pipeline on float functions
fn half(x Float) Float = x / 2.0;
fn third(x Float) Float = x / 3.0;
10.0 half println;
9.0 third println;

-- Mixed pipeline: x f(y) g(z)
fn average(a Float, b Float) Float = (a + b) / 2.0;
3.0 average(7.0) average(10.0) println;

-- Reverse with space pipeline
[1, 2, 3, 4, 5] reverse println;
