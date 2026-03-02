-- === Basic coroutine ===
coro fn count(n Int) Int {
    var i = 0;
    while (i < n) {
        yield i;
        i = i + 1;
    }
}

let c = 3 count;
c next println;
c next println;
c next println;
c next println;

-- === Coroutine with nested function calls ===
fn double(x Int) Int = x * 2;

coro fn doubled_range(n Int) Int {
    var i = 0;
    while (i < n) {
        yield i double;
        i = i + 1;
    }
}

let d = 3 doubled_range;
d next println;
d next println;
d next println;
d next println;

-- === Multiple yield points ===
coro fn multi_yield() Int {
    yield 10;
    yield 20;
    yield 30;
}

let m = multi_yield();
m next println;
m next println;
m next println;
m next println;

-- === Yield from within if/else ===
coro fn conditional_yield(flag Bool) Int {
    if (flag) {
        yield 1;
        yield 2;
    } else {
        yield 100;
        yield 200;
    }
    yield 999;
}

let ct = true conditional_yield;
ct next println;
ct next println;
ct next println;
ct next println;

let cf = false conditional_yield;
cf next println;
cf next println;
cf next println;
cf next println;

-- === Yield from a sequence of loops ===
coro fn two_loops() Int {
    var i = 0;
    while (i < 3) {
        yield i;
        i = i + 1;
    }
    i = 10;
    while (i < 13) {
        yield i;
        i = i + 1;
    }
}

let tl = two_loops();
tl next println;
tl next println;
tl next println;
tl next println;
tl next println;
tl next println;
tl next println;

-- === Yielding strings (Obj* values) ===
coro fn greetings() String {
    yield "hello";
    yield "world";
    yield "goodbye";
}

let g = greetings();
g next println;
g next println;
g next println;
g next println;

-- === Yielding arrays (Obj* values) ===
coro fn make_arrays() [Int] {
    yield [1, 2, 3];
    yield [4, 5];
    yield [6];
}

let ma = make_arrays();
ma next println;
ma next println;
ma next println;
ma next println;

-- === Multiple coroutines interleaved ===
let a = 3 count;
let b = 3 count;
a next println;
b next println;
a next println;
b next println;
a next println;
b next println;

-- === Storing coroutine in a variable, passing around ===
fn drain(c Coroutine<Int>) Void {
    var result = c next;
    while (result isSome) {
        result println;
        result = c next;
    }
}

let dr = 4 count;
dr drain;

-- === Calling one coroutine from another ===
coro fn inner() Int {
    yield 1;
    yield 2;
}

coro fn outer_coro() Int {
    let ic = inner();
    var r = ic next;
    while (r isSome) {
        yield r unwrap;
        r = ic next;
    }
    yield 99;
}

let o = outer_coro();
o next println;
o next println;
o next println;
o next println;

-- === Coroutine yielding tuples ===
coro fn pairs(n Int) (Int, Int) {
    var i = 0;
    while (i < n) {
        yield (i, i * i);
        i = i + 1;
    }
}

let p = 3 pairs;
p next println;
p next println;
p next println;
p next println;

-- === Yield from match/switch ===
enum Shape {
    circle Float,
    rect Float
}

coro fn describe_shapes() String {
    let shapes = [Shape.circle(1.0), Shape.rect(4.0), Shape.circle(2.5)];
    var i = 0;
    while (i < shapes length) {
        match (shapes[i]) {
            Shape.circle(r): yield "circle";
            Shape.rect(w): yield "rect";
        }
        i = i + 1;
    }
}

let ds = describe_shapes();
ds next println;
ds next println;
ds next println;
ds next println;

-- === Conditional coroutine selection (same yield type) ===
coro fn evens() Int {
    var i = 0;
    while (i < 6) {
        yield i;
        i = i + 2;
    }
}

coro fn odds() Int {
    var i = 1;
    while (i < 6) {
        yield i;
        i = i + 2;
    }
}

let selected = true ? evens() : odds();
selected drain;

let selected2 = false ? evens() : odds();
selected2 drain;

-- === Resume after done returns none repeatedly ===
let done_c = 1 count;
done_c next println;
done_c next println;
done_c next println;
done_c next println;

-- === Empty coroutine (no yields) ===
coro fn empty_coro() Int {
}

let ec = empty_coro();
ec next println;
ec next println;

-- === Coroutine with accumulating state ===
coro fn running_sum(n Int) Int {
    var sum = 0;
    var i = 1;
    while (i <= n) {
        sum = sum + i;
        yield sum;
        i = i + 1;
    }
}

let rs = 5 running_sum;
rs next println;
rs next println;
rs next println;
rs next println;
rs next println;
rs next println;

-- === Fibonacci coroutine (infinite, take first 8) ===
coro fn fibs() Int {
    var a = 0;
    var b = 1;
    while (true) {
        yield a;
        let tmp = a + b;
        a = b;
        b = tmp;
    }
}

let f = fibs();
f next println;
f next println;
f next println;
f next println;
f next println;
f next println;
f next println;
f next println;

-- === Pipeline yield: value yield; ===
coro fn pipeline_yield_test() Int {
    1 yield;
    2 yield;
    3 yield;
}

let py = pipeline_yield_test();
py next println;
py next println;
py next println;
py next println;

-- === Explicit call yield: yield(value) ===
coro fn explicit_yield_test() Int {
    yield(10);
    yield(20);
    yield(30);
}

let ey = explicit_yield_test();
ey next println;
ey next println;
ey next println;
ey next println;

-- === Mixed yield forms ===
coro fn mixed_yield() String {
    yield "prefix";
    "pipeline" yield;
    yield("explicit");
}

let my = mixed_yield();
my next println;
my next println;
my next println;
my next println;

-- === yieldAll basic delegation ===
coro fn inner_ints() Int {
    yield 10;
    yield 20;
    yield 30;
}

coro fn outer_delegate() Int {
    yield 1;
    yieldAll(inner_ints());
    yield 99;
}

let od = outer_delegate();
od next println;
od next println;
od next println;
od next println;
od next println;
od next println;

-- === yieldAll in pipeline form ===
coro fn outer_pipe() Int {
    yield 1;
    inner_ints() yieldAll;
    yield 99;
}

let op = outer_pipe();
op next println;
op next println;
op next println;
op next println;
op next println;
op next println;

-- === yieldAll with code before and after ===
coro fn three_sources() Int {
    var i = 0;
    while (i < 2) {
        yield i;
        i = i + 1;
    }
    inner_ints() yieldAll;
    yield 999;
}

let ts = three_sources();
ts next println;
ts next println;
ts next println;
ts next println;
ts next println;
ts next println;
ts next println;

-- === yieldAll with strings ===
coro fn inner_greet() String {
    yield "hello";
    yield "world";
}

coro fn outer_greet() String {
    yield "start";
    inner_greet() yieldAll;
    yield "end";
}

let og = outer_greet();
og next println;
og next println;
og next println;
og next println;
og next println;

-- === For-loop over coroutine (basic) ===
coro fn count_to(n Int) Int {
    var i = 1;
    while (i <= n) {
        yield i;
        i = i + 1;
    }
}

for (x : count_to(5)) {
    x println;
}

-- === For-loop over coroutine with break ===
for (x : fibs()) {
    if (x > 10) { break; }
    x println;
}

-- === For-loop over coroutine created inline ===
for (s : greetings()) {
    s println;
}

-- === For-loop over string-yielding coroutine ===
coro fn colors() String {
    yield "red";
    yield "green";
    yield "blue";
}

for (c : colors()) {
    c println;
}

-- === For-loop replaces manual drain ===
let dr2 = 4 count;
for (x : dr2) {
    x println;
}

-- === toList: finite coroutine ===
let tl1 = 5 count toList;
tl1 println;

-- === toList: empty coroutine (no yields) ===
let tl2 = 0 count toList;
tl2 println;

-- === toList: single-element coroutine ===
let tl3 = 1 count toList;
tl3 println;

-- === toList: coroutine yielding strings ===
let tl4 = greetings() toList;
tl4 println;

-- === toList: infinite coroutine with take ===
let tl5 = fibs() toList take(8);
tl5 println;

-- === toList: infinite coroutine with take(1) ===
let tl6 = fibs() toList take(1);
tl6 println;

-- === toList + head ===
let tl7 = 5 count toList head;
tl7 println;

-- === toList + tail ===
let tl8 = 5 count toList tail;
tl8 println;

-- === toList + length (finite) ===
let tl9 = 5 count toList length;
tl9 println;

-- === toList + drop ===
let tl10 = 5 count toList drop(3);
tl10 println;

-- === toList + collect ===
let tl11 = fibs() toList collect(6);
tl11 println;

-- === toList + map ===
let tl12 = 3 count toList map(fn(x Int) Int = x * 10);
tl12 println;

-- === toList + zip ===
coro fn small_range() Int {
    var i = 10;
    while (i < 13) {
        yield i;
        i = i + 1;
    }
}
let tl13 = zip(3 count toList, small_range() toList);
tl13 println;

-- === toList on already-done coroutine ===
let done_c = 1 count;
done_c next;
done_c next;
let tl14 = done_c toList;
tl14 println;

-- === toList: coroutine yielding tuples ===
let tl15 = 3 pairs toList;
tl15 println;

-- === toList on coroutine with yieldAll ===
let tl16 = outer_delegate() toList;
tl16 println;

-- === toList + for-loop over resulting list ===
for (x : 4 count toList) {
    x println;
}
