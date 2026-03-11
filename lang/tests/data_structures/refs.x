-- Ref tests (mutable references)

-- Basic ref creation and dereference
let x = &42;
*x println;

-- Ref mutation with <-
x <- 100;
*x println;

-- Ref with different types
let ri = &0;
ri <- 99;
*ri println;

let rf = &0.0;
rf <- 3.14;
*rf println;

let rs = &"hello";
rs <- "world";
*rs println;

-- Ref in loop
let sum = &0;
var i = 1;
while (i <= 5) {
    sum <- *sum + i;
    i = i + 1;
}
*sum println;

-- Function taking a Ref
fn increment(r Ref<Int>) Void {
    r <- *r + 1;
}
let counter = &0;
increment(counter);
increment(counter);
increment(counter);
*counter println;

-- Swap using refs
let p = &10;
let q = &20;
let tmp = *p;
p <- *q;
q <- tmp;
*p println;
*q println;
