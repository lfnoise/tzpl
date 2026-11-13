-- Qualified multi-field enum patterns report arity/shape mismatches in
-- terms of the case, like the bare caseName(a, b) form.
enum Shape { circle Float, rect(Float, Float) }
fn f(s Shape) Float {
    match (s) {
        Shape.rect(a, b, c): a;
        Shape.circle(x, y): x;
    }
}
f(Shape.circle(1.0)) println;
