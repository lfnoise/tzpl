-- QA: iter (infinite list generator) and lazy list edge cases

-- Basic iter: seed + function
let powers_of_2 = iter(1, fn(x Int) Int { x * 2 });
powers_of_2 take(8) println;

-- Fibonacci via iter of tuples
let fibs = iter((0, 1), fn(p (Int, Int)) (Int, Int) { (p.1, p.0 + p.1) });
fibs map(fn(p (Int, Int)) Int { p.0 }) take(10) println;

-- iter with floats
let halving = iter(1.0, fn(x Float) Float { x / 2.0 });
halving take(5) println;

-- Lazy list: head and tail
let nats = iter(0, fn(x Int) Int { x + 1 });
nats head println;
nats tail head println;
nats tail tail head println;

-- take(0) on infinite list
nats take(0) println;

-- Infinite list operations
let squares = nats map(fn(x Int) Int { x * x });
squares take(6) println;

-- dropWhile + take on infinite list
nats dropWhile(fn(x Int) Bool { x < 100 }) take(5) println;

-- zip two infinite lists
let evens = nats filter(fn(x Int) Bool { x % 2 == 0 });
let odds = nats filter(fn(x Int) Bool { x % 2 == 1 });
zip(evens, odds) take(5) println;

-- cyc (cycle a finite list infinitely)
cyc(List(1, 2, 3)) take(7) println;

-- stutter on infinite list
nats stutter(3) take(9) println;

-- stride on infinite list
nats stride(3) take(5) println;
