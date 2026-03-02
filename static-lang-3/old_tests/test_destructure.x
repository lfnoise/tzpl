-- Test: Pattern destructuring in declarations

-- Tuple destructuring
let (x, y) = (1, 2);
println(x);
println(y);

let (a, b, c) = (10, 20, 30);
println(a);
println(b);
println(c);

-- Struct destructuring
struct Point {
    x Int
    y Int
}

let Point { x: px, y: py } = Point { x: 3, y: 4 };
println(px);
println(py);

-- Array fixed destructuring
let [d, e, f] = [100, 200, 300];
println(d);
println(e);
println(f);

-- Array head/rest
let [head, ...tail] = [1, 2, 3, 4];
println(head);
println(tail);

-- Array head/discard rest
let [first, ...] = [10, 20, 30];
println(first);

-- var destructuring (mutable bindings)
var (mx, my) = (100, 200);
println(mx);
println(my);
mx = 999;
println(mx);

-- const destructuring
const (cx, cy) = (42, 84);
println(cx);
println(cy);

-- Nested pattern: tuple of values
let (n1, n2) = (7, 8);
println(n1);
println(n2);

-- Pattern destructuring in function body
fn destructInFn(p Point) Int {
    let Point { x: lx, y: ly } = p;
    lx + ly
}
println(destructInFn(Point { x: 5, y: 6 }));

-- Array patterns in match
fn matchArray(arr [Int]) Int {
    match (arr) {
        [1, 2, 3]: return 100;
        [head, ...tail]: return head;
        _: return -1;
    }
}

println(matchArray([1, 2, 3]));
println(matchArray([42, 10, 20]));

-- Tuple head/rest
let (t1, t2, ...trest) = (10, 20, 30, 40, 50);
println(t1);
println(t2);
println(trest);

-- Tuple head/discard rest
let (tf, ...) = (100, 200, 300);
println(tf);
