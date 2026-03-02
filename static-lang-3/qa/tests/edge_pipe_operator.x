-- QA: Pipe operator |> edge cases

fn double(x Int) Int = x * 2;
fn inc(x Int) Int = x + 1;
fn square(x Int) Int = x * x;

-- Basic pipe
5 |> double println;       -- 10

-- Chained pipes
5 |> double |> inc println;   -- 11

-- Pipe with expression on left
3 + 4 |> double println;    -- 14

-- Pipe into println
42 |> println;

-- Pipe with multi-arg function (partial application?)
fn add(a Int, b Int) Int = a + b;

-- Pipe chain with square
2 |> square |> double println;  -- 8

-- Float pipe
fn half(x Float) Float = x / 2.0;
10.0 |> half println;    -- 5.0
10.0 |> half |> half println;  -- 2.5

-- String pipe
fn exclaim(s String) String = s $ "!";
"hello" |> exclaim println;  -- hello!
"hi" |> exclaim |> exclaim println;  -- hi!!
