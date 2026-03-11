-- Match pattern matching

-- Literal patterns on integers
fn test_int_match(x Int) Void {
    match (x) {
        1: "one" println;
        2: "two" println;
        3: "three" println;
        _: "other" println;
    }
}
test_int_match(1);
test_int_match(2);
test_int_match(3);
test_int_match(99);

-- Binding pattern
fn test_binding(x Int) Void {
    match (x) {
        n: n println;
    }
}
test_binding(42);

-- Wildcard pattern
fn test_wildcard(x Int) Void {
    match (x) {
        _: "matched wildcard" println;
    }
}
test_wildcard(0);

-- Enum pattern matching
enum Option {
    some Int,
    none
}

fn unwrap_or(opt Option, default Int) Int {
    match (opt) {
        Option.some(value): return value;
        Option.none: return default;
    }
    return default;
}
unwrap_or(Option.some(42), 0) println;
unwrap_or(Option.none, -1) println;

-- Enum with multiple types
enum Shape {
    circle Float,
    rect Float,
    unknown
}

fn describe_shape(s Shape) Void {
    match (s) {
        Shape.circle(r): {
            "circle" println;
            r println;
        }
        Shape.rect(side): {
            "rect" println;
            side println;
        }
        Shape.unknown: "unknown" println;
    }
}
describe_shape(Shape.circle(3.14));
describe_shape(Shape.rect(5.0));
describe_shape(Shape.unknown);

-- Tuple pattern
let tup = (3, 4);
match (tup) {
    (x, y): {
        x println;
        y println;
    }
}

-- Struct pattern
struct Point { x Int, y Int }
fn test_struct_match(p Point) Void {
    match (p) {
        Point { x: a, y: b }: {
            a println;
            b println;
        }
    }
}
test_struct_match(Point { x: 10, y: 20 });

-- Bool literal pattern
fn test_bool(b Bool) Void {
    match (b) {
        true: "yes" println;
        false: "no" println;
    }
}
test_bool(true);
test_bool(false);

-- Match with return value
fn fibonacci(n Int) Int {
    match (n) {
        0: return 0;
        1: return 1;
        _: return fibonacci(n - 1) + fibonacci(n - 2);
    }
    return 0;
}
0 fibonacci println;
1 fibonacci println;
5 fibonacci println;
10 fibonacci println;
