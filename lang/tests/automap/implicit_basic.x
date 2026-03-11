-- Implicit auto-mapping basics

-- Scalar function auto-mapped over array
fn double(x Int) Int = x * 2;
[1, 2, 3, 4, 5] double println;

-- Auto-mapped with float return
fn half(x Int) Float = x / 2.0;
[1, 2, 3, 4, 5] half println;

-- Mixed: one auto-mapped arg, one scalar
fn add(a Int, b Int) Int = a + b;
[1, 2, 3] add(10) println;

-- Two arrays (zip semantics)
[1, 2, 3] add([10, 20, 30]) println;

-- Different lengths (zip = shortest)
[1, 2, 3, 4, 5] add([100, 200]) println;

-- String function auto-mapped
fn greet(name String) String = "Hello, " $ name $ "!";
["Alice", "Bob", "Charlie"] greet println;

-- Single element array
[42] double println;

-- Nested: function on array of arrays maps at depth
fn inc(x Int) Int = x + 1;
let nested = [[1, 2, 3], [4, 5, 6]];
nested inc println;

-- Three levels deep
let deep = [[[1, 2], [3, 4]], [[5, 6], [7, 8]]];
deep double println;

-- Mixed: nested array with scalar
nested add(10) println;
