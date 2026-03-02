-- Struct construction auto-mapping

struct Point { x Int, y Int }

-- Array in first field, scalar in second
Point { x: [10, 20, 30], y: 4 } println;

-- Scalar in first field, array in second
Point { x: 5, y: [100, 200, 300] } println;

-- Both arrays (zipped)
Point { x: [1, 2, 3], y: [10, 20, 30] } println;

-- Different lengths (min)
Point { x: [1, 2, 3, 4, 5], y: [10, 20, 30] } println;

-- Single-element array
Point { x: [42], y: 7 } println;

-- Fields in different order
Point { y: [10, 20], x: 5 } println;

-- Three-field struct
struct Vec3 { x Float, y Float, z Float }

-- One auto-mapped field
Vec3 { x: [1.0, 2.0, 3.0], y: 0.0, z: 1.0 } println;

-- Two auto-mapped fields
Vec3 { x: [1.0, 2.0], y: [3.0, 4.0], z: 5.0 } println;

-- All fields auto-mapped
Vec3 { x: [1.0, 2.0], y: [3.0, 4.0], z: [5.0, 6.0] } println;

-- Int-to-Float promotion in auto-mapped field
Vec3 { x: [1, 2, 3], y: 0.0, z: 0.0 } println;

-- Auto-mapped result passed to auto-mapping function
fn add_points(a Point, b Point) Point = Point { x: a.x + b.x, y: a.y + b.y };
let base = Point { x: [1, 2, 3], y: 0 };
let offset = Point { x: 0, y: 100 };
base add_points(offset) println;

-- Cartesian @1/@2 struct construction
Point { x: [1, 2, 3] @1, y: [10, 20] @2 } println;

-- Cartesian @1/@2/@3 struct construction (3-level)
Vec3 { x: [1, 2] @1, y: [3, 4] @2, z: [5, 6] @3 } println;
