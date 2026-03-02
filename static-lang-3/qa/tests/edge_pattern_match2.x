-- QA: Pattern matching edge cases round 2

-- Array pattern with rest
match ([1, 2, 3, 4, 5]) {
    [head, ...tail]: {
        head println;   -- 1
        tail println;   -- [2, 3, 4, 5]
    }
}

-- Cons pattern
match (List(1, 2, 3)) {
    h :: t: { h println; t println; }
    nil: println("empty");
}

-- Nested cons
match (List(1, 2, 3)) {
    a :: b :: rest: {
        a println;     -- 1
        b println;     -- 2
        rest println;  -- List(3)
    }
    _: println("short list");
}

-- Guard pattern
fn classify(n Int) String {
    match (n) {
        x if (x > 100): return "big";
        x if (x > 0): return "positive";
        0: return "zero";
        _: return "negative";
    }
    ""
}
classify(200) println;
classify(42) println;
classify(0) println;
classify(-5) println;

-- Enum pattern with payload extraction
enum Shape {
    circle Float,
    rect (Float, Float),
    point
}

fn area(s Shape) Float {
    match (s) {
        Shape.circle(r): return 3.14159 * r * r;
        Shape.rect(dims): return dims.0 * dims.1;
        Shape.point: return 0.0;
    }
    0.0
}

area(Shape.circle(5.0)) println;
area(Shape.rect((3.0, 4.0))) println;
area(Shape.point) println;

-- Symbol patterns
fn sym_match(s Symbol) String {
    match (s) {
        'hello: return "greeting";
        'bye: return "farewell";
        _: return "unknown";
    }
    ""
}
sym_match('hello) println;
sym_match('bye) println;
sym_match('other) println;

-- Boolean pattern
fn bool_match(b Bool) String {
    match (b) {
        true: return "yes";
        false: return "no";
    }
    ""
}
bool_match(true) println;
bool_match(false) println;

-- Nested tuple pattern
match ((1, (2, 3))) {
    (a, (b, c)): println(a + b + c);  -- 6
}

-- Struct pattern
struct Point { x Float, y Float }
match (Point{3.0, 4.0}) {
    Point{x: a, y: b}: println(a + b);  -- 7.0
}

-- Match with multiple cases for same enum
fn describe_shape(s Shape) String {
    match (s) {
        Shape.circle(r) if (r > 10.0): return "large circle";
        Shape.circle(_): return "small circle";
        Shape.rect(_): return "rectangle";
        Shape.point: return "point";
    }
    ""
}
describe_shape(Shape.circle(20.0)) println;
describe_shape(Shape.circle(3.0)) println;
describe_shape(Shape.rect((1.0, 2.0))) println;
describe_shape(Shape.point) println;
