-- QA: Edge cases in Ref operations

-- Basic ref operations
let r = &0;
*r println;
r <- 42;
*r println;

-- Ref set returns the value assigned
let v = (r <- 99);
v println;
*r println;

-- Multiple refs, independent mutation
let a = &10;
let b = &20;
a <- *a + 1;
b <- *b + 1;
*a println;
*b println;

-- Ref to different types
let ri = &42;
let rf = &3.14;
let rs = &"hello";
let rb = &true;

*ri println;
*rf println;
*rs println;
*rb println;

ri <- 100;
rf <- 2.718;
rs <- "world";
rb <- false;

*ri println;
*rf println;
*rs println;
*rb println;

-- Ref as function parameter (mutation visible to caller)
fn increment(r2 Ref<Int>) Void {
    r2 <- *r2 + 1;
}

let counter = &0;
increment(counter);
increment(counter);
increment(counter);
*counter println;  -- 3

-- Ref in a loop
let sum = &0;
for (i : (1..10)) {
    sum <- *sum + i;
}
*sum println;  -- 55

-- Ref to array
let arr_ref = &[1, 2, 3];
*arr_ref println;
arr_ref <- [4, 5, 6];
*arr_ref println;

-- Ref to tuple
let tup_ref = &(1, "hello");
*tup_ref println;
tup_ref <- (2, "world");
*tup_ref println;

-- Ref to struct
struct Point { x Float, y Float }
let pt_ref = &Point{1.0, 2.0};
*pt_ref println;
pt_ref <- Point{3.0, 4.0};
*pt_ref println;

-- Ref arithmetic (dereference then operate)
let x2 = &10;
let y2 = &20;
println(*x2 + *y2);
println(*x2 * *y2);

-- Ref swap pattern
let p = &1;
let q = &2;
let temp = *p;
p <- *q;
q <- temp;
*p println;  -- 2
*q println;  -- 1
