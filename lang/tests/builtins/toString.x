-- toString builtin function

-- Bool
println(toString(true));
println(toString(false));

-- Int
println(toString(0));
println(toString(42));
println(toString(-7));

-- Float
println(toString(3.14));
println(toString(-0.5));
println(toString(0.0));

-- Symbol
println(toString('hello));
println(toString('world));

-- String (identity)
println(toString("abc"));
println(toString(""));

-- Fraction
println(toString(1 / 2));
println(toString(3 / 4));

-- Complex
println(toString(Complex(3.0, 4.0)));
println(toString(1i));

-- Array of scalars
println(toString([1, 2, 3]));
println(toString([true, false, true]));
println(toString(["hello", "world"]));

-- Nested arrays
println(toString([[1, 2], [3, 4]]));

-- Tuple
println(toString((1, "two", 3.0)));
println(toString((true, 42)));

-- Nested tuple
println(toString(((1, 2), (3, 4))));

-- List
println(toString(List(10, 20, 30)));
println(toString(1 :: 2 :: 3 :: nil));

-- Struct
struct Point { x Float, y Float }
println(toString(Point { x: 1.0, y: 2.0 }));

struct Person { name String, age Int }
println(toString(Person { name: "Alice", age: 30 }));

-- Nested struct
struct Line { start Point, end Point }
println(toString(Line { start: Point { x: 0.0, y: 0.0 }, end: Point { x: 3.0, y: 4.0 } }));

-- Enum
enum Color {
    red,
    green,
    blue
}
println(toString(Color.red));
println(toString(Color.blue));

enum Option {
    some Int,
    none
}
println(toString(Option.some(42)));
println(toString(Option.none));

-- Enum with nested data
enum Shape {
    circle Float,
    rect (Float, Float)
}
println(toString(Shape.circle(5.0)));
println(toString(Shape.rect((3.0, 4.0))));

-- Map
println(toString(["a": 1, "b": 2]));

-- Set
println(toString(Set(10, 20, 30)));

-- Array of structs
println(toString([Point { x: 1.0, y: 2.0 }, Point { x: 3.0, y: 4.0 }]));

-- Array of enums
println(toString([Option.some(1), Option.none, Option.some(3)]));

-- Deeply nested: array of tuples of structs
println(toString([(Point { x: 0.0, y: 0.0 }, "origin"), (Point { x: 1.0, y: 1.0 }, "unit")]));

-- toString result is a String: verify by concatenation
let s = toString(42);
println("The answer is " $ s);

-- toString via pipeline
42 toString println;
[1, 2, 3] toString println;
