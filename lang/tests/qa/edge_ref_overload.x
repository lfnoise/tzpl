-- QA: Ref operations and <- overloading

-- Basic Ref
let r = &42;
*r println;          -- 42
r <- 99;
*r println;          -- 99

-- Ref to different types
let ri = &0;
let rf = &3.14;
let rs = &"hello";
let rb = &true;

ri <- 42;
rf <- 2.718;
rs <- "world";
rb <- false;

*ri println;
*rf println;
*rs println;
*rb println;

-- Ref in struct
struct Counter { value Ref<Int> }
let c = Counter { value: &0 };
c.value <- 1;
c.value <- *c.value + 1;
c.value <- *c.value + 1;
*c.value println;    -- 3

-- Multiple refs to different values
let a = &10;
let b = &20;
let sum = *a + *b;
sum println;         -- 30

-- Ref swap pattern
let x = &1;
let y = &2;
let tmp = *x;
x <- *y;
y <- tmp;
*x println;  -- 2
*y println;  -- 1

-- Accumulator pattern with <- overloading
struct Accumulator { total Ref<Int>, count Ref<Int> }
fn <-(acc Accumulator, val Int) Int {
    acc.total <- *(acc.total) + val;
    acc.count <- *(acc.count) + 1;
    val
}

let acc = Accumulator { total: &0, count: &0 };
acc <- 10;
acc <- 20;
acc <- 30;
*(acc.total) println;   -- 60
*(acc.count) println;   -- 3
