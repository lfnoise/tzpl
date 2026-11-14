-- Cartesian `@1`/`@2` mapping supports Array and persistent-vector args.
-- A List arg used to segfault, and an `@@` depth mixed with a cartesian
-- dimension produced garbage (heap addresses as Ints), and a dimension with
-- nothing marked for it (`@2` without `@1`) segfaulted or read an unset
-- length; all are type errors, for named functions, lambda values, and
-- binary operators alike.
fn k(a Int, b Int) Int = a + b;
let h = fn(a, b) { a + b };
let ys = [1, 2] toList;
let a = k(ys @1, [10, 20] @2);
let b = h(ys @1, [10, 20] @2);
let c = ys @1 + [10, 20] @2;
let d = k([[1], [2, 3]] @@, [1] @2);
let e = h([[1], [2, 3]] @@, [1] @2);
let g = [[1], [2, 3]] @@ + [1] @2;
let i = k(1, [10, 20] @2);
let j = h(1, [10, 20] @2);
let m = [1, 2] + [10, 20] @2;
