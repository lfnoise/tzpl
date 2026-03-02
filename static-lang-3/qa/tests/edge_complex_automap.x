-- QA: Complex auto-mapping interactions

-- Auto-map with default arguments
fn greet(name String, greeting String = "Hello") String = greeting $ " " $ name;
["Alice", "Bob", "Charlie"] greet println;

-- Auto-map with 2-arg function
fn add(a Int, b Int) Int = a + b;

-- What happens when both args are arrays?
-- Should this zip or error?
-- println(add([1, 2, 3], [10, 20, 30]));

-- Auto-map with string operations
["hello", "world", "test"] toUpper println;
["HELLO", "WORLD"] toLower println;

-- Auto-map length on strings
["hi", "hello", "hey"] length println;

-- Auto-map with ternary
fn is_positive(x Int) Bool = x > 0;
[1, -2, 3, -4, 5] is_positive println;

-- Nested auto-map on struct fields
struct Point { x Int, y Int }
let pts = [Point{1, 10}, Point{2, 20}, Point{3, 30}];

-- Field access auto-maps
pts.x println;
pts.y println;

-- Auto-map a function on extracted fields
pts.x double println;

fn double(x Int) Int = x * 2;

-- Auto-map struct construction
let xs = [1, 2, 3];
let new_pts = Point { x: xs, y: 0 };
new_pts println;

-- Auto-map with comparison
pts.x println;
println(pts.x > 1);

-- Chain: extract field, auto-map, filter
let big_x = pts.x filter(fn(v Int) { v > 1 });
big_x println;

-- Auto-map on list of tuples
let tuples = List((1, 10), (2, 20), (3, 30));
tuples.0 println;
tuples.1 println;
