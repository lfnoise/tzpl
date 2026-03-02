-- QA: Edge cases in auto-mapping

-- Auto-map a function over empty array
fn double(x Int) Int = x * 2;
let empty [Int] = [];
empty double println;

-- Auto-map over single-element array
[42] double println;

-- @ on empty array
let e [Int] = [];
e @ double println;

-- Nested auto-map on empty
let ne [[Int]] = [];
-- ne @@ double println;

-- Auto-map with identity
fn id(x Int) Int = x;
[1, 2, 3] id println;

-- Multiple levels of @
[[[1, 2], [3, 4]], [[5, 6], [7, 8]]] @@ reverse println;

-- Cartesian with single-element arrays
let cart1 = [1] @1 + [10] @2;
cart1 println;

-- Cartesian with different sized arrays
let cart2 = [1, 2, 3] @1 + [10] @2;
cart2 println;

let cart3 = [1] @1 + [10, 20, 30] @2;
cart3 println;

-- Auto-map binary ops with empty
-- println([1, 2, 3] @ + 0);

-- Auto-map field access on empty array of structs
struct Point { x Int, y Int }
let pts [Point] = [];
-- pts.x println;  -- what happens with empty?

-- Auto-map over array of structs
let points = [Point{1, 2}, Point{3, 4}];
points.x println;
points.y println;

-- Auto-map with comparison operator
println([1, 5, 3, 7, 2] @ > 3);

-- Auto-map with boolean operations
println([1, 2, 3, 4, 5] @ > 2);
println([1, 2, 3, 4, 5] @ == 3);

-- Space pipeline + auto-map
fn negate(x Int) Int = -x;
[1, 2, 3] negate println;
[1, 2, 3] @ negate println;

-- Auto-map preserves order
fn add10(x Int) Int = x + 10;
[5, 3, 1, 4, 2] add10 println;

-- Auto-map on list
fn square(x Int) Int = x * x;
List(1, 2, 3) square println;

-- Chained auto-map
fn inc(x Int) Int = x + 1;
[1, 2, 3] double inc println;

-- Auto-map with string function
fn greet(name String) String = "Hello, " $ name;
["Alice", "Bob"] greet println;
