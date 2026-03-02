-- Nested destructuring

-- Nested tuples
let ((n1, n2), n3) = ((7, 8), 9);
n1 println;
n2 println;
n3 println;

-- Tuple of tuple and value
let ((a, b), c) = ((1, 2), 3);
a println;
b println;
c println;

-- Pattern in function
struct Point { x Int, y Int }
fn add_point(p Point) Int {
    let Point { x: px, y: py } = p;
    px + py
}
Point { x: 10, y: 20 } add_point println;
