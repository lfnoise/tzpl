-- Qualified enum patterns destructure multi-field case data the same way the
-- bare form does: Shape.rect(w, h) == rect(w, h) == Shape.rect((w, h)).

enum Shape { circle Float, rect(Float, Float), tri(Float, Float, Float), empty }

fn describe(s Shape) String {
    match (s) {
        Shape.circle(r): "circle " $ r toString;
        Shape.rect(1.0, h): "unit-wide rect " $ h toString;   -- literal sub-pattern
        Shape.rect(w, _): "rect of width " $ w toString;       -- wildcard
        Shape.tri(a, ...): "tri starting " $ a toString;       -- rest
        Shape.empty: "empty";
    }
}

[Shape.circle(1.5), Shape.rect(1.0, 4.0), Shape.rect(2.0, 3.0),
 Shape.tri(3.0, 4.0, 5.0), Shape.empty] @ describe @ println;

-- Bare and double-paren forms still agree.
fn area(s Shape) Float {
    match (s) {
        rect(w, h): w * h;
        Shape.tri((a, b, c)): a + b + c;
        _: 0.0;
    }
}
area(Shape.rect(2.0, 3.0)) println;
area(Shape.tri(1.0, 2.0, 3.0)) println;

-- Generic enum with a tuple payload, nested enum pattern inside.
enum Pairish<T> { both(T, Option<T>), one T }
fn sumP(p Pairish<Int>) Int {
    match (p) {
        Pairish.both(a, Option.some(b)): a + b;
        Pairish.both(a, _): a;
        Pairish.one(x): x;
    }
}
sumP(Pairish.both(1, Option.some(10))) println;
sumP(Pairish.both(2, Option<Int>.none)) println;
sumP(Pairish<Int>.one(5)) println;

-- A single sub-pattern still binds a tuple payload whole.
fn whole(s Shape) Void {
    match (s) {
        Shape.rect(t): t println;
        _: "not a rect" println;
    }
}
whole(Shape.rect(7.0, 8.0));
