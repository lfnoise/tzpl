-- QA: Advanced coroutine edge cases

-- Coroutine yielding tuples
coro fn pairs() (Int, Int) {
    yield (1, 10);
    yield (2, 20);
    yield (3, 30);
}
pairs() toList println;

-- Coroutine yielding structs
struct Point { x Int, y Int }
coro fn points() Point {
    yield Point{1, 2};
    yield Point{3, 4};
}
points() toList println;

-- Coroutine with complex control flow
coro fn fizzbuzz(n Int) String {
    var i = 1;
    while (i <= n) {
        if (i % 15 == 0) { yield "FizzBuzz"; }
        else if (i % 3 == 0) { yield "Fizz"; }
        else if (i % 5 == 0) { yield "Buzz"; }
        else { yield toString(i); }
        i = i + 1;
    }
}
fizzbuzz(15) toList println;

-- Coroutine used in filter
coro fn naturals() Int {
    var i = 1;
    while (true) {
        yield i;
        i = i + 1;
    }
}
let evens = naturals() toList filter(fn(x Int) Bool { x % 2 == 0 }) take(5);
evens println;

-- Coroutine used in map
let doubled = naturals() toList map(fn(x Int) Int { x * 2 }) take(5);
doubled println;

-- Multiple coroutines interleaved
coro fn letters() String {
    yield "a";
    yield "b";
    yield "c";
}
let nums = naturals();
let lets = letters();
nums next println;
lets next println;
nums next println;
lets next println;
nums next println;
lets next println;
nums next println;
lets next println;   -- none (exhausted)

-- Coroutine as function argument
fn consume_n(c Coroutine<Int>, n Int) [Int] {
    var result [Int] = [];
    var i = 0;
    while (i < n) {
        let v = c next;
        match (v) {
            Option.some(x): result = result push(x);
            Option.none: { return result; }
        }
        i = i + 1;
    }
    result
}
consume_n(naturals(), 5) println;
