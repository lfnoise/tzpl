-- Auto-mapped field access over Arrays and Lists

struct Point { x Int, y Int }

-- Array of tuples
let tuples = [(1, 2), (3, 4), (5, 6)];
tuples.0 println;
tuples.1 println;

-- Array of structs
let points = [Point{1, 2}, Point{3, 4}, Point{5, 6}];
points.x println;
points.y println;

-- List of structs
let point_list = List(Point{1, 2}, Point{3, 4});
point_list.x println;
point_list.y println;

-- Nested: List of Arrays of tuples
let nested = List([(1, 2), (3, 4)], [(5, 6)]);
nested.1 println;

-- Nested: Array of Arrays of tuples
let nested2 = [[(1, 2)], [(3, 4), (5, 6)]];
nested2.0 println;

-- Tuple field access chain
((1, 2), (3, 4)).1 .0 println;
