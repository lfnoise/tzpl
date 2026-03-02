-- Test match pattern matching

-- 1. Literal patterns on integers
fn test_int_match(x Int) Void {
    match (x) {
        1: println("One");
        2: println("Two");
        3: println("Three");
        _: println("Other");
    }
}

test_int_match(1);
test_int_match(2);
test_int_match(3);
test_int_match(99);

-- 2. Binding pattern
fn test_binding(x Int) Void {
    match (x) {
        n: println(n);
    }
}

test_binding(42);

-- 3. Wildcard pattern
fn test_wildcard(x Int) Void {
    match (x) {
        _: println("matched wildcard");
    }
}

test_wildcard(0);

-- 4. Enum pattern matching
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

let a = unwrap_or(Option.some(42), 0);
println(a);

let b = unwrap_or(Option.none, -1);
println(b);

-- 5. Enum with multiple types
enum Shape {
    circle Float,
    rect Float,
    unknown
}

fn describe_shape(s Shape) Void {
    match (s) {
        Shape.circle(r): {
            println("Circle with radius:");
            println(r);
        }
        Shape.rect(side): {
            println("Rectangle with side:");
            println(side);
        }
        Shape.unknown: println("Unknown shape");
    }
}

describe_shape(Shape.circle(3.14));
describe_shape(Shape.rect(5.0));
describe_shape(Shape.unknown);

-- 6. Guarded patterns
fn classify(x Int) Void {
    match (x) {
        n if (n > 0): println("positive");
        n if (n < 0): println("negative");
        0: println("zero");
        _: println("unreachable");
    }
}

classify(10);
classify(-5);
classify(0);

-- 7. Tuple pattern
let tup = (3, 4);
match (tup) {
    (x, y): {
        println(x);
        println(y);
    }
}

-- 8. Struct pattern
struct Point {
    x Int,
    y Int
}

fn test_struct_match(p Point) Void {
    match (p) {
        Point { x: a, y: b }: {
            println(a);
            println(b);
        }
    }
}

test_struct_match(Point { x: 10, y: 20 });

-- 9. Bool literal pattern
fn test_bool(b Bool) Void {
    match (b) {
        true: println("yes");
        false: println("no");
    }
}

test_bool(true);
test_bool(false);

-- 10. Match with return value from function
fn fibonacci(n Int) Int {
    match (n) {
        0: return 0;
        1: return 1;
        _: return fibonacci(n - 1) + fibonacci(n - 2);
    }
    return 0;
}

println(fibonacci(0));
println(fibonacci(1));
println(fibonacci(5));
println(fibonacci(10));
