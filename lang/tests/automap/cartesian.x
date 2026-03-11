-- Cartesian product with @1/@2

fn add(a Int, b Int) Int = a + b;
fn mul(a Int, b Int) Int = a * b;

-- Basic @1/@2
let cart = add([1, 2] @1, [10, 20] @2);
cart println;

-- With more elements
let cart2 = mul([1, 2, 3] @1, [10, 100] @2);
cart2 println;

-- Cartesian on binary ops
let j = [1, 2] @1 + [10, 20] @2;
j println;

-- Different sizes
let cart3 = add([1, 2, 3] @1, [10, 20, 30, 40] @2);
cart3 println;
