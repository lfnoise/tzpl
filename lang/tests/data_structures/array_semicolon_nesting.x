-- Runs of semicolons in an array literal close levels of nesting: n
-- adjacent ';' split at level n, so these pairs are the same value.
[1, 2, 3; 4; 5, 6] println;
([1, 2, 3; 4; 5, 6] == [[1, 2, 3], [4], [5, 6]]) println;
[1, 2; 3, 4;; 5, 6; 7, 8] println;
([1, 2; 3, 4;; 5, 6; 7, 8] == [[[1, 2], [3, 4]], [[5, 6], [7, 8]]]) println;

-- every group is wrapped at every level
[1;; 2] println;
-- a trailing run adds depth without an empty group
[1, 2, 3;] println;
[1; 2;;] println;

-- persistent vectors nest the same way
#[1.5, 2.5; 3.5] println;

-- elements are any expressions; lambdas keep their own semicolons
let xs = [1 + 1, [9, 8] length; 3 * 3];
xs[1][0] println;
let fs = [fn(x Int) Int { let y = x; y + 1 }; fn(x Int) Int { x * 2 }];
fs[0][0](1) println;
fs[1][0](5) println;

-- an ordinary statement semicolon after a literal is unchanged
let plain = [1, 2]; plain println;
