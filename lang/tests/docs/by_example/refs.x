-- Refs from Tzopilotl by Example

-- Create a ref with &
let x = &42;
*x println;

-- Mutate with <-
x <- 100;
*x println;

-- Works with any type
let s = &"hello";
s <- "world";
*s println;

-- Explicit type annotation
let r Ref<Int> = &0;

-- Ref set returns the value
let v = (x <- 77);
v println;

-- Refs as function arguments
fn increment(r Ref<Int>) Int {
    let old = *r;
    r <- *r + 1;
    old
}

let counter = &0;
counter increment println;
counter increment println;
*counter println;

-- Overloading <-
struct Accumulator { total Ref<Int>, count Ref<Int> }

fn <-(acc Accumulator, val Int) Int {
    acc.total <- *(acc.total) + val;
    acc.count <- *(acc.count) + 1;
    val
}

let acc = Accumulator{&0, &0};
acc <- 10;
acc <- 20;
acc <- 30;
*(acc.total) println;
*(acc.count) println;
