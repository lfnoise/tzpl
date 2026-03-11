-- Array of structs integration

struct Point { x Float, y Float }

-- Create array of points
let points = [
    Point { x: 1.0, y: 2.0 },
    Point { x: 3.0, y: 4.0 },
    Point { x: 5.0, y: 6.0 }
];
points println;

-- Field access auto-maps
points.x println;
points.y println;

-- Struct function on array (auto-maps)
fn magnitude_sq(pt Point) Float = pt.x * pt.x + pt.y * pt.y;
points magnitude_sq println;

-- Construct array of structs via auto-map
let xs = [1.0, 2.0, 3.0];
let ps = Point { x: xs, y: 0.0 };
ps println;

-- Index into array of structs
points[0] println;
points[1].x println;
points[2].y println;

-- Sort points by field isn't directly supported, but we can show indexing works
let sorted_x = points.x sort;
sorted_x println;
