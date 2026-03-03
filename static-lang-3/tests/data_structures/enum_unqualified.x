-- Unqualified enum pattern matching
-- When the subject type is known to be an enum, case names
-- should not need to be qualified with the enum type name.

enum Shape {
    circle Float,
    rect (Float, Float),
    point
}

-- Unqualified data and no-data cases
fn describe(s Shape) String {
    match (s) {
        circle(r) : "circle %^" fmt(r);
        rect(dims) : "rect %^ %^" fmt(dims.0, dims.1);
        point : "point";
    }
}

Shape.circle(3.0) describe println;
Shape.rect((4.0, 5.0)) describe println;
Shape.point describe println;

-- Unqualified with tuple destructuring (multi-element)
fn area(s Shape) Float {
    match (s) {
        circle(r) : 3.14159 * r * r;
        rect(w, h) : w * h;
        point : 0.0;
    }
}

Shape.circle(2.0) area println;
Shape.rect((3.0, 4.0)) area println;
Shape.point area println;

-- Nested enum in another enum
enum Wrapper {
    wrapped Shape,
    empty
}

fn describeWrapper(w Wrapper) String {
    match (w) {
        wrapped(s) : s describe;
        empty : "empty";
    }
}

Wrapper.wrapped(Shape.circle(1.0)) describeWrapper println;
Wrapper.wrapped(Shape.point) describeWrapper println;
Wrapper.empty describeWrapper println;

-- Mixed qualified and unqualified in same match
fn mixed(s Shape) String {
    match (s) {
        Shape.circle(r) : "qualified circle";
        rect(dims) : "unqualified rect";
        point : "unqualified point";
    }
}

Shape.circle(5.0) mixed println;
Shape.rect((1.0, 2.0)) mixed println;
Shape.point mixed println;
