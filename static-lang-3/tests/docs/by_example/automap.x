-- Auto-mapping from Language X by Example

-- A function expecting Int, given an array of Int, maps automatically
fn double(x Int) Int = x * 2;

let arr = [1, 2, 3, 4, 5];
arr double println;

-- Mixed: one auto-mapped arg, one scalar
fn add(a Int, b Int) Int = a + b;
println(add(arr, 10));

-- Multiple auto-mapped args (zip semantics)
println(add(arr, [10, 20, 30, 40, 50]));

-- Deep auto-mapping: nested arrays
let nested = [[1, 2, 3], [4, 5, 6]];
nested double println;

-- Auto-mapped struct construction
struct Point { x Int, y Int }

let ps = Point { x: [10, 20, 30], y: 4 };
ps println;

let ps2 = Point { x: [1, 2, 3], y: [10, 20, 30] };
ps2 println;

-- Explicit @ operator
fn process(x Int) Int = x * 10;
fn process(arr [Int]) Int = arr[0];

-- Without @: the [Int] overload is selected
[1, 2, 3] process println;

-- With @: unwrap one level, selects the Int overload
[1, 2, 3] @ process println;

-- @@ unwraps two levels
let nested2 = [[1, 2], [3, 4]];
nested2 @@ process println;

-- @ on binary operators
println(10 + [1, 2, 3] @);
println([1, 2, 3] @ + 10);
println([1, 2, 3] @ + [10, 20, 30] @);

-- Comparison
println([1, 5, 3] @ > 2);

-- Deep map with @@
println([[1, 2, 3], [4, 5, 6]] @@ + 10);

-- Tuple + array @
println((10, 20) + [1, 2, 3] @);

-- Cartesian product with @1, @2
println(add([1, 2] @1, [10, 20] @2));
println([1, 2] @1 + [10, 20] @2);

fn mul(a Int, b Int) Int = a * b;
println(mul([1, 2, 3] @1, [10, 100] @2));

-- Auto-map field access
let points = [Point{1, 2}, Point{3, 4}, Point{5, 6}];
points.x println;
points.y println;

-- Array of tuples: index access auto-maps
let tuples = [(1, 2), (3, 4), (5, 6)];
tuples.0 println;
tuples.1 println;

-- List of structs
let point_list = List(Point{1, 2}, Point{3, 4});
point_list.x println;

-- Chained: .field on nested collections maps at each level
let nested3 = [[(1, 2)], [(3, 4), (5, 6)]];
nested3.0 println;

-- Auto-mapping with Lists
List(1, 2, 3) double println;
println(List(1, 2, 3) + 10);
println(List(1, 2, 3) + List(10, 20, 30));

-- List @ on binary ops
println(List(1, 2, 3) @ + 10);

-- Mixed list + array automap
println(add(List(10, 20, 30), [1, 2, 3]));
