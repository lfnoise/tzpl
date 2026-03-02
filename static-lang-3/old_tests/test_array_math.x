-- Advanced array and tuple math

-- Dot product (manual, element by element)
fn dot3(a1 Int, a2 Int, a3 Int, b1 Int, b2 Int, b3 Int) Int = a1 * b1 + a2 * b2 + a3 * b3;

println(dot3(1, 2, 3, 4, 5, 6));

-- Array of floats: numerical methods

-- Compute sum of array elements manually
let vals = [1.0, 2.0, 3.0, 4.0, 5.0];
-- Can't index yet, but we can do array math

-- Scalar multiplication
let doubled = vals * 2.0;
println(doubled);

-- Arithmetic sequences via arrays
let seq1 = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
let seq2 = [10, 9, 8, 7, 6, 5, 4, 3, 2, 1];

-- Element-wise sum should all be 11
let elevenths = seq1 + seq2;
println(elevenths);

-- Element-wise product
let products = seq1 * seq2;
println(products);

-- Nested operations
let x = [1, 2, 3];
let y = [4, 5, 6];

-- (x + y) * 2
let result = (x + y) * 2;
println(result);

-- x * y + x (element-wise)
let result2 = x * y + x;
println(result2);

-- Tuple operations chained
let p1 = (1.0, 2.0);
let p2 = (3.0, 4.0);

-- Midpoint = (p1 + p2) / 2.0  -- division would promote to fraction, let's use * 0.5
let midpoint = (p1 + p2) * 0.5;
println(midpoint);

-- Vector from p1 to p2
let vec = p2 - p1;
println(vec);

-- Scale vector
let scaled = vec * 3.0;
println(scaled);

-- Tuple with mixed numeric types
let mixed1 = (1, 2.5);
let mixed2 = (3, 1.5);
let mixed_sum = mixed1 + mixed2;
println(mixed_sum);

let mixed_prod = mixed1 * mixed2;
println(mixed_prod);

-- Array broadcast operations
let base = [100, 200, 300];

-- Add constant
println(base + 1);

-- Subtract constant
println(base - 50);

-- Multiply by constant
println(base * 2);

-- Tuple op Array
-- Each tuple field is broadcast across the array
let tup = (10, 20);
let arr = [1, 2, 3];

-- Tuple + Array: each tuple field added to the array
let ta_sum = tup + arr;
println(ta_sum);

-- Tuple * Array
let ta_prod = tup * arr;
println(ta_prod);

-- Tuple - Array
let ta_diff = tup - arr;
println(ta_diff);

-- Array op Tuple
-- Array + Tuple
let at_sum = arr + tup;
println(at_sum);

-- Array * Tuple
let at_prod = arr * tup;
println(at_prod);

-- Array - Tuple
let at_diff = arr - tup;
println(at_diff);

-- Mixed numeric types: float tuple with int array
let ftup = (1.5, 2.5);
let iarr = [10, 20, 30];

let ft_sum = ftup + iarr;
println(ft_sum);

let ft_prod = ftup * iarr;
println(ft_prod);

-- Int array with float tuple
let it_sum = iarr + ftup;
println(it_sum);

-- Mixed numeric types 2: mixed tuple with int array
let fitup = (1.5, 2, 3/5);

let ft_sum = fitup + iarr;
println(ft_sum);

let ft_prod = fitup * iarr;
println(ft_prod);

-- Int array with float tuple
let it_sum = iarr + fitup;
println(it_sum);

-- Larger arrays
let big = [1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144];
let ones = [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1];
let fib_plus_one = big + ones;
println(fib_plus_one);
