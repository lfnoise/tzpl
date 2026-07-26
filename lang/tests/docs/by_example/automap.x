-- Auto-mapping from Tzopilotl by Example

-- A function expecting Int, given an array of Int, maps automatically
fn double(x Int) Int = x * 2;

let arr = [1, 2, 3, 4, 5];
arr double println;

-- Mixed: one auto-mapped arg, one scalar
fn add(a Int, b Int) Int = a + b;
arr add(10) println;

-- Multiple auto-mapped args (zip semantics)
arr add([10, 20, 30, 40, 50]) println;

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

-- @ on binary operators: redundant on arithmetic, which already auto-maps
println(10 + [1, 2, 3]);
println(10 + [1, 2, 3] @);

-- Equality has a whole-array meaning, so @ changes the result
println([1, 2, 3] == [1, 9, 3]);
println([1, 2, 3] @ == [1, 9, 3] @);
println([1, 2, 3] @ == 2);

-- '$' likewise joins containers, or elements one level down
println(["a", "b"] $ ["c", "d"]);
println(["a", "b"] @ $ ["c", "d"] @);
println(["a", "b"] @ $ "!");
println([[1, 2], [3]] @ $ [9]);

-- @ chooses the depth at which nested operands line up
let m = [[1, 2], [3, 4]];
println(m + [10, 20]);
println(m @@ + [10, 20] @);

-- Tuple mapped over an array vs tuple of arrays
println((10, 20) + [1, 2, 3]);
println((10, 20) + [1, 2, 3] @);

-- Cartesian product with @1, @2
add([1, 2] @1, [10, 20] @2) println;
println([1, 2] @1 + [10, 20] @2);

fn mul(a Int, b Int) Int = a * b;
mul([1, 2, 3] @1, [10, 100] @2) println;

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
