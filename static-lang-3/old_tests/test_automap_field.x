-- Test auto-map field access over Arrays and Lists

-- Struct for testing
struct Point {
    x Int,
    y Int
}

-- Array of tuples: tuple field access
let tuples = [(1, 2), (3, 4), (5, 6)];
println(tuples.0);
println(tuples.1);

-- Array of structs: struct field access
let points = [Point{1, 2}, Point{3, 4}, Point{5, 6}];
println(points.x);
println(points.y);

-- List of structs: struct field access
let point_list = List(Point{1, 2}, Point{3, 4});
println(point_list.x);
println(point_list.y);

-- Nested: List of Arrays of tuples (depth 2)
let nested = List([(1, 2), (3, 4)], [(5, 6)]);
println(nested.1);

-- Nested: Array of Arrays of tuples (depth 2)
let nested2 = [[(1, 2)], [(3, 4), (5, 6)]];
println(nested2.0);

-- Nested: Array of tuples of array of tuples.
[([(1, 2), (3, 4)], [(5, 6), (7, 8)]), ([(11, 12), (13, 14)], [(15, 16), (17, 18)])] println;
[([(1, 2), (3, 4)], [(5, 6), (7, 8)]), ([(11, 12), (13, 14)], [(15, 16), (17, 18)])].1 println;
[([(1, 2), (3, 4)], [(5, 6), (7, 8)]), ([(11, 12), (13, 14)], [(15, 16), (17, 18)])].1 .0 println;

((1, 2), (3, 4)).1 .0 println;