-- QA: Edge cases in closures and scoping

-- Closure capturing a let variable
let x = 10;
let f = fn() { x };
f() println;

-- Closure capturing a var variable
var y = 10;
let g = fn() { y };
y = 20;
g() println;  -- Is it 10 or 20?

-- Closure capturing a Ref
let r = &10;
let h = fn() { *r };
r <- 42;
h() println;  -- Should be 42 since refs are mutable

-- Nested closures
let a = 1;
let outer = fn() {
    let b = 2;
    let inner = fn(x Int) { a + b + x };
    inner
};
let inner_fn = outer();
inner_fn(3) println;  -- Should be 6

-- Closure as return value - counter pattern
fn make_counter() {
    let count = &0;
    fn() {
        let old = *count;
        count <- *count + 1;
        old
    }
}

let c = make_counter();
c() println;  -- 0
c() println;  -- 1
c() println;  -- 2

-- Variable shadowing
let val = 100;
fn test_shadow() Int {
    let val = 200;
    val
}
test_shadow() println;  -- 200
val println;  -- 100 (outer unaffected)

-- Lambda with no captures
let pure = fn(x Int, y Int) { x + y };
pure(3, 4) println;

-- Closure used in map
let offset = 100;
[1, 2, 3] map(fn(x Int) { x + offset }) println;

-- Closure used in filter
let threshold = 3;
[1, 2, 3, 4, 5] filter(fn(x Int) { x > threshold }) println;

-- Closure used in fold
[1, 2, 3, 4, 5] fold(0, fn(acc Int, x Int) { acc + x }) println;

-- Function returning a function
fn make_adder(n Int) {
    fn(x Int) { x + n }
}
let add5 = make_adder(5);
let add10 = make_adder(10);
add5(3) println;
add10(3) println;

-- Closure capturing another closure
let double = fn(x Int) { x * 2 };
let double_then_add = fn(x Int, y Int) { double(x) + y };
double_then_add(3, 4) println;  -- 10
