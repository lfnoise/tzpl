-- Test Ref with structs and more complex scenarios

struct Point { x Float, y Float }

-- Ref to a struct
let p = &Point{1.0, 2.0};
*p println;
p <- Point{3.0, 4.0};
*p println;

-- Ref to array
let arr = &[1, 2, 3];
*arr println;
arr <- [4, 5, 6];
*arr println;

-- Function taking a Ref
fn increment(r Ref<Int>) Int {
    let old = *r;
    r <- *r + 1;
    old
}

let counter = &0;
increment(counter) println;
increment(counter) println;
increment(counter) println;
*counter println;

-- Overloaded <- operator
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
