-- Mixed array/list auto-mapping

fn add(a Int, b Int) Int = a + b;

let lst = List(10, 20, 30);
let arr = [1, 2, 3];
let tup = (10, 20, 30);

-- Binary op: list @ + array @
println(lst @ + arr @);

-- Tuple + array
println(tup + arr);

-- Tuple + array @
println(tup + arr @);
