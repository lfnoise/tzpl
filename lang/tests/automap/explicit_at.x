-- Explicit @ (each) operator

-- @ selects a different overload
fn process(x Int) Int = x * 10;
fn process(arr [Int]) Int = arr[0];

-- Without @: matches [Int] overload
[1, 2, 3] process println;

-- With @: unwraps to Int, selects scalar overload
[1, 2, 3] @ process println;

-- @@ on nested arrays
let nested = [[1, 2], [3, 4]];
nested process println;
nested @@ process println;

-- Reverse with @
[[1, 2, 3], [4, 5, 6]] @ reverse println;

-- @ on nested
[[[1, 2, 3], [4, 5]], [[6, 7], [8, 9, 10]]] @@ reverse println;

-- Space pipeline with @
fn double(x Int) Int = x * 2;
[1, 2, 3] @ double println;
