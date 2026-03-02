-- Test mixed array/list auto-mapping in function calls
-- (Previously errored: "mixed array/list auto-mapping not yet supported")

fn add(a Int, b Int) Int = a + b;

-- Implicit: both args need unwrapping — list + array
let tup = (10, 20, 30);
let lst = List(10, 20, 30);
let arr = [1, 2, 3];
let r1 = add(lst, arr);
println(r1);
-- Expected: List(11, 22, 33) — result is List since List is present

-- Explicit: both with @
let r2 = add(lst @, arr @);
println(r2);
-- Expected: List(11, 22, 33)

-- Binary op on list @ + array @
let r3 = lst @ + arr @;
println(r3);
-- Expected: List(11, 22, 33)

println(lst + arr); 
-- Expected: List([11, 12, 13], [21, 22, 23], [31, 32, 33])

println(lst + arr @);
-- Expected: [List(11, 21, 31), List(12, 22, 32), List(13, 23, 33)]

println(tup + arr); 
-- Expected: ([11, 12, 13], [21, 22, 23], [31, 32, 33])

println(tup + arr @);
-- Expected: [(11, 21, 31), (12, 22, 32), (13, 23, 33)]