-- Struct destructuring

struct Point { x Int, y Int }

-- Basic struct destructure
let Point { x: px, y: py } = Point { x: 3, y: 4 };
px println;
py println;

-- In function body
fn destructInFn(p Point) Int {
    let Point { x: lx, y: ly } = p;
    lx + ly
}
Point { x: 5, y: 6 } destructInFn println;

-- Match with struct pattern
fn describe(p Point) Void {
    match (p) {
        Point { x: a, y: b }: {
            a println;
            b println;
        }
    }
}
describe(Point { x: 10, y: 20 });

-- Tuple struct destructure
struct Pair(Int, Int);
let Pair(a, b) = Pair(100, 200);
a println;
b println;
