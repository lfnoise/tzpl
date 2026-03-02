-- Test struct declarations, construction, and field access

-- Basic struct
struct Point {
    x Float,
    y Float
}

let p = Point { x: 1.0, y: 2.0 };
println(p);
println(p.x);
println(p.y);

-- Struct with mixed types
struct Person {
    name String,
    age Int
}

let alice = Person { name: "Alice", age: 30 };
println(alice);
println(alice.name);
println(alice.age);

-- Struct field access in expressions
let p2 = Point { x: 3.0, y: 4.0 };
let dist_sq = p.x * p.x + p.y * p.y;
println(dist_sq);

-- Struct as function argument
fn magnitude(pt Point) Float = pt.x * pt.x + pt.y * pt.y;

println(magnitude(p));
println(magnitude(p2));

-- Struct with int-to-float promotion
let p3 = Point { x: 5, y: 10 };
println(p3);
println(p3.x);

-- Nested structs
struct Line {
    start Point,
    end Point
}

let line = Line { start: p, end: p2 };
println(line);
println(line.start);
println(line.end);
println(line.start.x);
println(line.end.y);

-- Struct fields used in control flow
fn is_origin(pt Point) Bool {
    if (pt.x == 0.0 && pt.y == 0.0) {
        return true;
    }
    false
}

let origin = Point { x: 0.0, y: 0.0 };
println(is_origin(origin));
println(is_origin(p));

-- Positional struct construction (no field names)
let p4 = Point{3.0, 4.0};
println(p4);
println(p4.x);
println(p4.y);

-- Positional with int-to-float promotion
let p5 = Point{7, 8};
println(p5);
println(p5.x);

-- Positional nested struct
let line2 = Line{p4, p5};
println(line2);
println(line2.start.x);
println(line2.end.y);
