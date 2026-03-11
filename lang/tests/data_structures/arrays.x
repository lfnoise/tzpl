-- Array tests

-- Literal
let a = [1, 2, 3, 4, 5];
a println;

-- Single element
println([42]);

-- Indexing
a[0] println;
a[2] println;
a[4] println;

-- Length
a length println;
[10, 20, 30] length println;

-- Nested arrays
let nested = [[1, 2, 3], [4, 5, 6]];
nested println;
nested[0] println;
nested[1][2] println;

-- Array arithmetic: element-wise
let b = [10, 20, 30, 40, 50];
println(a + b);
println(a * b);
println(b - a);

-- Scalar arithmetic
println(a + 100);
println(a * 2);
println(-a);

-- Concatenation
println([1, 2, 3] $ [4, 5, 6]);

-- String array
let names = ["Alice", "Bob", "Charlie"];
names println;
names[0] println;
names length println;

-- Bool array
let bools = [true, false, true];
bools println;

-- Float array
let floats = [1.5, 2.5, 3.5];
floats println;
println(floats + 0.5);

-- Mixed array operations
let x = [1, 2, 3];
let y = [4, 5, 6];
println((x + y) * 2);
println(x * y + x);
