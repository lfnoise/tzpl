-- Destructuring in match

-- Enum destructure
enum Option { some Int, none }

fn unwrap(opt Option) Int {
    match (opt) {
        Option.some(v): return v;
        Option.none: return -1;
    }
    -1
}
Option.some(42) unwrap println;
Option.none unwrap println;

-- Tuple destructure in match
fn swap(t (Int, Int)) (Int, Int) {
    match (t) {
        (a, b): return (b, a);
    }
    t
}
(1, 2) swap println;

-- List destructure in match
fn head_or_default(xs List<Int>, default Int) Int {
    match (xs) {
        h :: t: return h;
        nil: return default;
    }
    default
}
head_or_default(List(10, 20, 30), 0) println;
head_or_default(nil, 0) println;

-- Nested cons pattern
let xs = List(1, 2, 3);
match (xs) {
    a :: b :: rest: {
        a println;
        b println;
        rest println;
    }
    _: "short" println;
}

-- Struct in match
struct Point { x Int, y Int }
fn describe(p Point) Void {
    match (p) {
        Point { x: 0, y: 0 }: "origin" println;
        Point { x: a, y: b }: {
            a println;
            b println;
        }
    }
}
describe(Point { x: 0, y: 0 });
describe(Point { x: 3, y: 4 });

-- Array in match
fn matchArr(arr [Int]) Void {
    match (arr) {
        [1, 2, 3]: "exact match" println;
        [h, ...t]: {
            h println;
            t println;
        }
        _: "unknown" println;
    }
}
matchArr([1, 2, 3]);
matchArr([10, 20, 30]);
