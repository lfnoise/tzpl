-- Array indexing

-- Basic indexing
let a = [10, 20, 30, 40, 50];
a[0] println;
a[2] println;
a[4] println;

-- Index with variable
let i = 3;
a[i] println;

-- Indexing result of function
fn make_array(a Int, b Int) [Int] { [a, b, a + b] }
make_array(10, 20)[2] println;

-- Struct field after index
struct Point { x Float, y Float }
let points = [Point { x: 1.0, y: 2.0 }, Point { x: 3.0, y: 4.0 }];
points[0].x println;
points[1].y println;

-- Nested array indexing
let nested = [[1, 2, 3], [4, 5, 6], [7, 8, 9]];
nested[0][0] println;
nested[1][2] println;
nested[2][1] println;

-- Index and pipeline
fn add(x Int, y Int) Int = x + y;
a[1] add(5) println;

-- Lambda called on indexed value
let double = fn(x Int) Int { x * 2 };
a[1] double println;

-- Index into single-element array
let single = [42];
single[0] println;

-- Cyclic indexing: out-of-bounds wraps around
let c = [10, 20, 30];
c[3] println;          -- 10 (wraps to 0)
c[4] println;          -- 20 (wraps to 1)
c[5] println;          -- 30 (wraps to 2)
c[6] println;          -- 10 (wraps to 0)

-- Cyclic indexing: negative indices
c[-1] println;         -- 30 (last element)
c[-2] println;         -- 20
c[-3] println;         -- 10
c[-4] println;         -- 30 (wraps)

-- Cyclic indexing with variable index
let j = -1;
c[j] println;          -- 30

-- Cyclic indexing with array of indices
c[(-2..4) toArray] println;

-- Single-element array: all indices map to the same element
single[0] println;
single[1] println;
single[-1] println;
single[999] println;
