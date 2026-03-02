
fn add(a Int, b Int) Int = a + b;

let tup = (10, 20);
let lis = List(10, 20);
let arr = [1, 2, 3];

/*
-- Each tuple field is broadcast across the array, returning a tuple of arrays. Output is correct.
println(add(lis, arr));   -- ([11, 12, 13], [21, 22, 23])

-- This should apply each element of the array to the tuple and return an array of tuples.
println(add(lis, arr @)); -- should be [(11, 21), (12, 22), (13, 23)]) but gives same result as above.


*/


List(1, 2, 3) sqrt println