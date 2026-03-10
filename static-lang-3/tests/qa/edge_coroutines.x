-- QA: Coroutine edge cases

-- Basic coroutine
coro fn count(n Int) Int {
    var i = 0;
    while (i < n) {
        yield i;
        i = i + 1;
    }
}

let c = count(3);
c next println;   -- Option<Int>.some(0)
c next println;   -- Option<Int>.some(1)
c next println;   -- Option<Int>.some(2)
c next println;   -- Option<Int>.none

-- Pipeline yield form
coro fn py() Int { 1 yield; 2 yield; 3 yield; }
let p = py();
p next println;
p next println;
p next println;
p next println;

-- For loop over coroutine
for (x : count(4)) { x println; }

-- toList on coroutine
count(5) toList println;

-- Empty coroutine (no yields)
coro fn empty_coro() Int {}
let e = empty_coro();
e next println;   -- Option<Int>.none

-- Coroutine yielding strings
coro fn greetings() String {
    yield "hello";
    yield "world";
}
let g = greetings();
g next println;
g next println;
g next println;

-- Multiple independent instances
let c1 = count(3);
let c2 = count(3);
c1 next println;
c2 next println;
c1 next println;
c2 next println;

-- toList on exhausted coroutine
let c3 = count(2);
c3 next println;
c3 next println;
c3 next println;  -- none

-- yieldAll delegation
coro fn inner_ints() Int {
    yield 10;
    yield 20;
}

coro fn outer_delegate() Int {
    yield 1;
    yieldAll(inner_ints());
    yield 99;
}
outer_delegate() toList println;

-- yieldAll pipeline form
coro fn outer_pipe() Int {
    yield 1;
    inner_ints() yieldAll;
    yield 99;
}
outer_pipe() toList println;

-- Coroutine with conditional yields
coro fn cond_yield(flag Bool) Int {
    if (flag) {
        yield 1;
        yield 2;
    } else {
        yield 100;
        yield 200;
    }
    yield 999;
}
true cond_yield toList println;
false cond_yield toList println;
