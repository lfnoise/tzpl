-- Test array indexing with [expr]

-- Basic indexing
let a = [10, 20, 30, 40, 50];
println(a[0]);
println(a[2]);
let i = 3;
println(a[i]);

-- Struct with array field; array of structs with field access: a[i].x
struct Point { x Float, y Float }
let points = [Point { x: 1.0, y: 2.0 }, Point { x: 3.0, y: 4.0 }];
println(points[0].x);
println(points[1].y);

-- Function returning array, then indexing: f(x, y)[i]
fn make_array(a Int, b Int) [Int] { [a, b, a + b] }
println(make_array(10, 20)[2]);

-- Indexing and then pipelining: a[i] f(y)
fn add(x Int, y Int) Int = x + y;
println(a[1] add(5));

-- Lambda stored in variable, called after indexing from array
let double = fn(x Int) Int { x * 2 };
let arr_with_val = [5, 10, 15];
println(double(arr_with_val[1]));
