-- Test implicit auto-mapping

--- Single auto-mapped argument ---
fn double(x Int) Int = x * 2;

let arr = [1, 2, 3, 4, 5];
let result = double(arr);
println(result);
-- Expected: [2, 4, 6, 8, 10]

--- Auto-mapped with float return ---
fn half(x Int) Float = x / 2.0;

let halved = half(arr);
println(halved);
-- Expected: [0.5, 1, 1.5, 2, 2.5]

--- Mixed: one auto-mapped arg, one scalar ---
fn add(a Int, b Int) Int = a + b;

let added = add(arr, 10);
println(added);
-- Expected: [11, 12, 13, 14, 15]

--- Multiple auto-mapped args (zip semantics) ---
let arr2 = [10, 20, 30, 40, 50];
let sums = add(arr, arr2);
println(sums);
-- Expected: [11, 22, 33, 44, 55]

--- Multiple auto-mapped args with different lengths (zip = shortest) ---
let short = [100, 200];
let zipped = add(arr, short);
println(zipped);
-- Expected: [101, 202]

--- Auto-mapping with string functions ---
fn greet(name String) String = "Hello, " $ name $ "!";

let names = ["Alice", "Bob", "Charlie"];
let greetings = greet(names);
println(greetings);
-- Expected: [Hello, Alice!, Hello, Bob!, Hello, Charlie!]

--- Struct construction: implicit auto-mapping ---

struct Point {
    x Int,
    y Int
}

-- Array in first field, scalar in second
let ps1 = Point { x: [10, 20, 30], y: 4 };
println(ps1);
-- Expected: [Point { x: 10, y: 4 }, Point { x: 20, y: 4 }, Point { x: 30, y: 4 }]

-- Scalar in first field, array in second
let ps2 = Point { x: 5, y: [100, 200, 300] };
println(ps2);
-- Expected: [Point { x: 5, y: 100 }, Point { x: 5, y: 200 }, Point { x: 5, y: 300 }]

-- Arrays in both fields (zipped element-wise)
let ps3 = Point { x: [1, 2, 3], y: [10, 20, 30] };
println(ps3);
-- Expected: [Point { x: 1, y: 10 }, Point { x: 2, y: 20 }, Point { x: 3, y: 30 }]

-- Arrays of different lengths (result length = min)
let ps4 = Point { x: [1, 2, 3, 4, 5], y: [10, 20, 30] };
println(ps4);
-- Expected: [Point { x: 1, y: 10 }, Point { x: 2, y: 20 }, Point { x: 3, y: 30 }]

-- Single-element array
let ps5 = Point { x: [42], y: 7 };
println(ps5);
-- Expected: [Point { x: 42, y: 7 }]

-- Three-field struct with one auto-mapped field
struct Vec3 {
    x Float,
    y Float,
    z Float
}

let vs1 = Vec3 { x: [1.0, 2.0, 3.0], y: 0.0, z: 1.0 };
println(vs1);
-- Expected: [Vec3 { x: 1, y: 0, z: 1 }, Vec3 { x: 2, y: 0, z: 1 }, Vec3 { x: 3, y: 0, z: 1 }]

-- Three-field struct with two auto-mapped fields
let vs2 = Vec3 { x: [1.0, 2.0], y: [3.0, 4.0], z: 5.0 };
println(vs2);
-- Expected: [Vec3 { x: 1, y: 3, z: 5 }, Vec3 { x: 2, y: 4, z: 5 }]

-- Three-field struct with all fields auto-mapped
let vs3 = Vec3 { x: [1.0, 2.0], y: [3.0, 4.0], z: [5.0, 6.0] };
println(vs3);
-- Expected: [Vec3 { x: 1, y: 3, z: 5 }, Vec3 { x: 2, y: 4, z: 6 }]

-- Int-to-Float promotion in auto-mapped field
let vs4 = Vec3 { x: [1, 2, 3], y: 0.0, z: 0.0 };
println(vs4);
-- Expected: [Vec3 { x: 1, y: 0, z: 0 }, Vec3 { x: 2, y: 0, z: 0 }, Vec3 { x: 3, y: 0, z: 0 }]

-- Auto-mapped result passed to function that auto-maps
fn add_points(a Point, b Point) Point = Point { x: a.x + b.x, y: a.y + b.y };

let base = Point { x: [1, 2, 3], y: 0 };
let offset = Point { x: 0, y: 100 };
let shifted = add_points(base, offset);
println(shifted);
-- Expected: [Point { x: 1, y: 100 }, Point { x: 2, y: 100 }, Point { x: 3, y: 100 }]

-- Fields given in different order than declaration
let ps8 = Point { y: [10, 20], x: 5 };
println(ps8);
-- Expected: [Point { x: 5, y: 10 }, Point { x: 5, y: 20 }]
